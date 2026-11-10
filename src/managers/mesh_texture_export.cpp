#include "mesh_texture_export.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "core_defs.h"
#include "dds_image_provider.h"
#include "dds_processor.h"
#include "gcm_texture.h"
#include "hash_utility.h"
#include "package_stream.h"
#include "txpk_file.h"
#include "x360_texture.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrent/QtConcurrentMap>
#include <QtEndian>

#include <algorithm>
#include <atomic>

#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

// "Props\Goalposts\postpads_col.pc.dds" -> "props/goalposts/postpads_col" (how the materials name textures).
QString normalizeTexturePath(const QString& in) {
    QString path = in.trimmed().toLower();
    path.replace(u'\\', u'/');
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    for (const QString& extension : {u".dds"_s, u".badds"_s, u".png"_s, u".tga"_s}) {
        if (path.endsWith(extension)) {
            path.chop(extension.size());
            break;
        }
    }
    for (const QString& platform : {u".pc"_s, u".ps3"_s, u".xbox360"_s}) {
        if (path.endsWith(platform)) {
            path.chop(platform.size());
            break;
        }
    }
    return path;
}

// The model's asset path without its type: "props/balls/models/ball.model.mesh" -> "props/balls/models/ball".
QString modelBase(const QString& meshPath) {
    QString path = QString(meshPath).replace(u'\\', u'/').toLower();
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    if (path.startsWith(u"0x"_s) || !path.contains(u'.'))
        return {};   // Only a hash: the material / texpack can't be named.
    for (const QString& suffix : {u".model.mesh"_s, u".mesh"_s, u".model"_s})
        if (path.endsWith(suffix))
            return path.chopped(suffix.size());
    return path.section(u'.', 0, 0);
}

QString keyOfAsset(const MeshTextureExport::Context& context, const QString& path, const QString& type) {
    const quint64 hash = BlobsetHash::hashForAsset(context.version, {path, type}, context.platform, context.gameKey);
    return BlobsetEntryRegistry::makeKey(hash);
}

bool readAsset(const MeshTextureExport::Context& context, const QString& key, QByteArray* data, qint64* mainSize,
               bool includeVram) {
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(key, entry))
        return false;
    try {
        *data = BlobsetAssetReader::read(entry, context.assetLocation, context.version, context.platform, mainSize,
                                         includeVram);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// TXPKs (same layout rules as TxpkToolManager::loadFromBytes)
// ---------------------------------------------------------------------------------------------------------------
struct TxpkTexture {
    QString path;          // Normalised.
    TXPK::Entry entry;
    qint64 dataOffset = -1;
};

QList<TxpkTexture> readTxpk(const QByteArray& data, qint64 mainSize, bool bigEndian) {
    QList<TxpkTexture> out;
    if (data.size() < 8)
        return out;
    const auto* raw = reinterpret_cast<const uchar*>(data.constData());
    const quint32 magic = bigEndian ? qFromBigEndian<quint32>(raw) : qFromLittleEndian<quint32>(raw);
    const quint32 count = bigEndian ? qFromBigEndian<quint32>(raw + 4) : qFromLittleEndian<quint32>(raw + 4);
    if ((magic != static_cast<quint32>(CoreDefs::FileType::TXPK) && magic != static_cast<quint32>(CoreDefs::FileType::KPXT))
        || count == 0 || qint64(count) * 16 + 8 > data.size())
        return out;

    TXPK txpk;
    try {
        QByteArray copy = data;   // QBuffer needs a non-const array (implicitly shared, no copy is made).
        QBuffer buffer(&copy);
        buffer.open(QIODevice::ReadOnly);
        PackageStream in(&buffer, bigEndian ? PackageStream::Big : PackageStream::Little);
        txpk.deserialize(in, bigEndian);
    } catch (const std::exception&) {
        return out;
    }

    if (mainSize < 0) {
        qint64 headerEnd = 0;
        for (const auto& eList = txpk.getEntries(); const TXPK::Entry& e : eList)
            headerEnd = std::max<qint64>(headerEnd, qint64(e.DDSPathOffset) + e.DDSPathSize);
        mainSize = headerEnd;
    }
    const auto fits = [&](qint64 offset, qint64 size) { return offset >= 0 && size >= 0 && offset + size <= data.size(); };
    for (const auto& eList = txpk.getEntries(); const TXPK::Entry& e : eList) {
        TxpkTexture texture;
        texture.path = normalizeTexturePath(e.DDSFilePath);
        texture.entry = e;
        // Data offsets are relative to the vram section (absolute in a few files).
        const qint64 candidates[2] = {mainSize + qint64(e.DDSDataOffset), qint64(e.DDSDataOffset)};
        for (const qint64 c : candidates) {
            if (fits(c, e.DDSDataSize) && (bigEndian || data.mid(c, 4) == "DDS ")) {
                texture.dataOffset = c;
                break;
            }
        }
        if (texture.dataOffset < 0)
            for (const qint64 c : candidates)
                if (fits(c, e.DDSDataSize)) {
                    texture.dataOffset = c;
                    break;
                }
        out.append(texture);
    }
    return out;
}

// A PC .dds of a TXPK texture (console data converted); empty when it can't be.
QByteArray txpkDds(const QByteArray& data, const TxpkTexture& texture, bool bigEndian) {
    if (texture.dataOffset < 0)
        return {};
    const QByteArray raw = data.mid(texture.dataOffset, texture.entry.DDSDataSize);
    if (!bigEndian)
        return raw;
    try {
        if (texture.entry.IsXbox360)
            return X360::toDds(texture.entry.X360Header, raw).value_or(QByteArray());
        return Gcm::toDds(texture.entry.GcmTexture, raw).value_or(QByteArray());
    } catch (const std::exception&) {
        return {};
    }
}

// Texture path -> (TXPK key, texture index) of every TXPK of the loaded blobset; built once per blobset (the
// headers are read on every core). False when it was cancelled (nothing is kept then). Call with s_indexMutex held.
QMutex s_indexMutex;
QString s_indexId;
QHash<QString, QPair<QString, int>> s_index;

bool ensureTxpkIndex(const MeshTextureExport::Context& context, const MeshTextureExport::Progress& progress,
                     const MeshTextureExport::Cancelled& cancelled) {
    const QString id = context.assetLocation + u'|' + QString::number(BlobsetEntryRegistry::generation());
    if (s_indexId == id)
        return true;
    const bool bigEndian = context.platform != static_cast<int>(CoreDefs::Platforms::Windows);
    const int total = int(context.txpkKeys.size());
    std::atomic<int> done{0};
    const QList<QStringList> lists = QtConcurrent::blockingMapped<QList<QStringList>>(
        context.txpkKeys, [&](const QString& key) {
            QStringList paths;
            if (!cancelled || !cancelled()) {
                QByteArray main;
                qint64 mainSize = -1;
                if (readAsset(context, key, &main, &mainSize, false)) {   // The header lists the textures.
                    for (const TxpkTexture& texture : readTxpk(main, main.size(), bigEndian))
                        paths.append(texture.path);
                }
            }
            const int n = ++done;
            if (progress && (n % 8 == 0 || n == total))
                progress(0.3 * n / std::max(1, total),
                         u"Finding the textures in the texture packs (%1 of %2)..."_s.arg(n).arg(total));
            return paths;
        });
    if (cancelled && cancelled())
        return false;
    s_index.clear();
    for (qsizetype k = 0; k < lists.size(); ++k)
        for (qsizetype i = 0; i < lists[k].size(); ++i)
            if (!lists[k][i].isEmpty() && !s_index.contains(lists[k][i]))
                s_index.insert(lists[k][i], {context.txpkKeys[k], int(i)});
    s_indexId = id;
    return true;
}

// Textures by path, decoded (and scaled down to `maxSize`, 0 = as stored). Each file / TXPK is read once; the
// reading and decoding run on every core.
struct Fetched {
    QHash<QString, QImage> images;   // Normalised path -> image.
    QStringList missing;             // "path" or "path (reason)".
};

Fetched fetchTextures(const QStringList& wanted, const MeshTextureExport::Context& context, const QString& modelBase,
                      int maxSize, const MeshTextureExport::Progress& progress,
                      const MeshTextureExport::Cancelled& cancelled) {
    Fetched out;
    QStringList paths;
    for (const QString& w : wanted) {
        const QString path = normalizeTexturePath(w);
        if (!path.isEmpty() && !paths.contains(path))
            paths.append(path);
    }
    if (paths.isEmpty())
        return out;
    if (!context.valid) {
        out.missing = paths;
        return out;
    }
    const bool bigEndian = context.platform != static_cast<int>(CoreDefs::Platforms::Windows);
    const auto stop = [&] { return cancelled && cancelled(); };

    // 1. Where each one is: its own file ("<path>.texture": the V4 games' .pc.dds), the model's texpack, else any
    //    TXPK of the game (the older games keep most textures in shared packs).
    struct Source {
        QString key;
        bool loose = false;
        QList<QPair<QString, int>> textures;   // Path, index in the TXPK.
    };
    QList<Source> sources;
    QHash<QString, int> packSource;
    const auto addToPack = [&](const QString& key, const QString& path, int index) {
        int s = packSource.value(key, -1);
        if (s < 0) {
            s = int(sources.size());
            packSource.insert(key, s);
            sources.append({key, false, {}});
        }
        sources[s].textures.append({path, index});
    };
    const QString ownKey = modelBase.isEmpty() ? QString() : keyOfAsset(context, modelBase, u"texpack"_s);
    QStringList ownPaths;
    bool ownRead = false;
    QStringList unplaced;
    for (const QString& path : std::as_const(paths)) {
        const QString key = keyOfAsset(context, path, u"texture"_s);
        BlobsetEntry entry;
        if (BlobsetEntryRegistry::findEntry(key, entry)) {
            sources.append({key, true, {{path, -1}}});
            continue;
        }
        if (!ownKey.isEmpty()) {
            if (!ownRead) {
                ownRead = true;
                QByteArray main;
                qint64 mainSize = -1;
                if (readAsset(context, ownKey, &main, &mainSize, false))
                    for (const TxpkTexture& texture : readTxpk(main, main.size(), bigEndian))
                        ownPaths.append(texture.path);
            }
            if (const qsizetype index = ownPaths.indexOf(path); index >= 0) {
                addToPack(ownKey, path, int(index));
                continue;
            }
        }
        unplaced.append(path);
    }
    if (!unplaced.isEmpty()) {
        QMutexLocker locker(&s_indexMutex);
        if (!ensureTxpkIndex(context, progress, cancelled))
            return out;   // Cancelled.
        for (const QString& path : std::as_const(unplaced)) {
            const auto hit = s_index.constFind(path);
            if (hit != s_index.constEnd())
                addToPack(hit.value().first, path, hit.value().second);
            else
                out.missing.append(path);
        }
    }

    // 2. Read every source once: the textures' .dds bytes.
    struct Raw {
        QString path;
        QByteArray dds;
    };
    int total = 0;
    for (const Source& source : std::as_const(sources))
        total += int(source.textures.size());
    const double readFrom = unplaced.isEmpty() ? 0.0 : 0.3;
    std::atomic<int> read{0};
    const QList<QList<Raw>> rawLists = QtConcurrent::blockingMapped<QList<QList<Raw>>>(sources, [&](const Source& source) {
        QList<Raw> raws;
        if (stop())
            return raws;
        if (source.loose) {
            raws.append({source.textures.first().first, DdsImageProvider::readDds(source.key)});
        } else {
            QByteArray data;
            qint64 mainSize = -1;
            const bool ok = readAsset(context, source.key, &data, &mainSize, true);
            const QList<TxpkTexture> textures = ok ? readTxpk(data, mainSize, bigEndian) : QList<TxpkTexture>();
            for (const auto& [path, index] : source.textures) {
                qsizetype at = index < textures.size() && textures[index].path == path ? index : -1;
                for (qsizetype i = 0; at < 0 && i < textures.size(); ++i)
                    if (textures[i].path == path)
                        at = i;
                raws.append({path, at >= 0 ? txpkDds(data, textures[at], bigEndian) : QByteArray()});
            }
        }
        const int n = (read += int(raws.size()));
        if (progress)
            progress(readFrom + (0.6 - readFrom) * n / std::max(1, total),
                     u"Reading textures (%1 of %2)..."_s.arg(n).arg(total));
        return raws;
    });
    if (stop())
        return out;

    // 3. Decode (and scale down).
    QList<Raw> all;
    for (const QList<Raw>& raws : rawLists)
        all += raws;
    std::atomic<int> decodedCount{0};
    const QList<QImage> images = QtConcurrent::blockingMapped<QList<QImage>>(all, [&](const Raw& raw) {
        QImage image;
        if (!stop() && !raw.dds.isEmpty()) {
            image = DdsProcessor::decodeDdsToQImage(raw.dds);
            if (maxSize > 0 && !image.isNull() && (image.width() > maxSize || image.height() > maxSize))
                image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        const int n = ++decodedCount;
        if (progress)
            progress(0.6 + 0.3 * n / std::max(1, int(all.size())),
                     u"Decoding textures (%1 of %2)..."_s.arg(n).arg(all.size()));
        return image;
    });
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (!images.value(i).isNull())
            out.images.insert(all[i].path, images[i]);
        else
            out.missing.append(all[i].path + (all[i].dds.isEmpty() ? QString() : u" (unsupported format)"_s));
    }
    return out;
}

bool hasTransparency(const QImage& image) {
    if (!image.hasAlphaChannel())
        return false;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (qAlpha(line[x]) < 250)
                return true;
    }
    return false;
}

bool writeFile(const QString& path, const QByteArray& data, QString* error) {
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit())
        return true;
    *error = u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString());
    return false;
}

// The materials: in the mesh (V4), else the model's .material.mat.
QList<MeshFile::Material> loadMaterials(const MeshFile& mesh, const QString& base,
                                        const MeshTextureExport::Context& context) {
    QList<MeshFile::Material> materials = MeshFile::parseMaterials(mesh.materialBlock);
    if (materials.isEmpty() && context.valid && !base.isEmpty()) {
        QByteArray data;
        qint64 mainSize = -1;
        if (readAsset(context, keyOfAsset(context, base, u"material"_s), &data, &mainSize, false))
            materials = MeshFile::parseMaterials(data);
    }
    return materials;
}

// Each submesh's material index (by its surface's name; a single material serves every surface), -1 = none.
QList<int> submeshMaterials(const MeshFile& mesh, const QList<MeshFile::Material>& materials) {
    QHash<QString, int> byName;
    for (qsizetype i = 0; i < materials.size(); ++i)
        byName.insert(materials[i].name.toLower(), int(i));
    QList<int> out;
    for (const MeshFile::Submesh& sub : mesh.submeshes) {
        int index = byName.value(mesh.surfaces.value(sub.surface).toLower(), -1);
        if (index < 0 && materials.size() == 1)
            index = 0;
        out.append(index);
    }
    return out;
}

// The material's colour texture: col_map, else another diffuse slot.
QString colourMap(const MeshFile::Material& material) {
    QString colour = material.map(u"col_map"_s);
    for (const QString& slot : {u"dif_map"_s, u"diffuse_map"_s, u"alb_map"_s, u"albedo_map"_s})
        if (colour.isEmpty())
            colour = material.map(slot);
    return colour;
}

} // namespace

MeshTextureExport::Context MeshTextureExport::currentContext() {
    Context context;
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator)
        return context;
    context.assetLocation = orchestrator->assetLocation();
    context.version = orchestrator->blobsetVersion();
    context.platform = orchestrator->platform();
    context.gameKey = BlobsetHash::gameKeyFor(orchestrator->currentGameName());
    context.txpkKeys = orchestrator->mappedKeysWithSuffix(u".txpk"_s);
    context.valid = !context.assetLocation.isEmpty();
    return context;
}

MeshTextureExport::Result MeshTextureExport::exportObj(const MeshFile& mesh, const QString& meshPath,
                                                       const QString& outDir, const QString& baseName,
                                                       const Context& context, const Progress& progress) {
    Result result;
    const QString base = modelBase(meshPath);

    // 1. The materials and each submesh's material.
    const QList<MeshFile::Material> materials = loadMaterials(mesh, base, context);
    const QList<int> submeshMaterial = submeshMaterials(mesh, materials);

    // 3. MTL names (the surface's last path part, made unique) and textures.
    const QString textureDirName = baseName + u"_textures"_s;
    const QDir textureDir(QDir(outDir).filePath(textureDirName));
    QStringList wanted;
    {
        QSet<int> used(submeshMaterial.cbegin(), submeshMaterial.cend());
        for (const int index : std::as_const(used))
            if (index >= 0)
                for (const auto& entry : materials[index].maps)
                    wanted.append(entry.second);
    }
    const Fetched fetched = fetchTextures(wanted, context, base, 0, progress, nullptr);
    QHash<QString, QString> exported;          // Normalised texture path -> file name ("" = not found).
    QHash<QString, bool> transparent;          // File name -> has see-through pixels.
    QSet<QString> fileNames;
    QStringList missing;
    int written = 0;
    QString error;

    missing = fetched.missing;
    const auto exportTexture = [&](const QString& texturePath) -> QString {
        const QString path = normalizeTexturePath(texturePath);
        if (exported.contains(path))
            return exported.value(path);
        exported.insert(path, QString());
        const QImage image = fetched.images.value(path);
        if (image.isNull())
            return {};   // Listed in fetched.missing.
        QString name = path.section(u'/', -1);
        static const QRegularExpression unsafe(u"[^A-Za-z0-9_.\\-]"_s);
        name.replace(unsafe, u"_"_s);
        QString fileName = name + u".png"_s;
        for (int n = 2; fileNames.contains(fileName.toLower()); ++n)
            fileName = name + u'_' + QString::number(n) + u".png"_s;
        if (!QDir().mkpath(textureDir.path()) || !image.save(textureDir.filePath(fileName), "PNG")) {
            missing.append(path + u" (couldn't be written)"_s);
            return {};
        }
        fileNames.insert(fileName.toLower());
        exported.insert(path, fileName);
        transparent.insert(fileName, hasTransparency(image));
        ++written;
        return fileName;
    };

    QStringList submeshNames;
    QHash<int, QString> materialNames;
    QSet<QString> usedNames;
    QByteArray mtl;
    for (const int index : std::as_const(submeshMaterial)) {
        if (index < 0) {
            submeshNames.append(QString());
            continue;
        }
        if (!materialNames.contains(index)) {
            const MeshFile::Material& material = materials[index];
            QString name = material.name.section(u'/', -1);
            static const QRegularExpression unsafe(u"[^A-Za-z0-9_.\\-]"_s);
            name.replace(unsafe, u"_"_s);
            if (name.isEmpty())
                name = u"material"_s;
            QString unique = name;
            for (int n = 2; usedNames.contains(unique.toLower()); ++n)
                unique = name + u'_' + QString::number(n);
            usedNames.insert(unique.toLower());
            materialNames.insert(index, unique);

            mtl += "\nnewmtl " + unique.toUtf8() + '\n';
            mtl += "# surface " + material.name.toUtf8() + ", shader " + material.shader.toUtf8() + '\n';
            mtl += "Ka 1.000 1.000 1.000\nKd 1.000 1.000 1.000\nKs 0.000 0.000 0.000\nNs 10.0\nd 1.0\nillum 2\n";

            // Colour: col_map (or another diffuse slot); normals: nrm_map. The other maps are exported and listed.
            const QString colour = colourMap(material);
            const QString normal = material.map(u"nrm_map"_s);
            const auto ref = [&](const QString& file) { return (textureDirName + u'/' + file).toUtf8(); };
            if (!colour.isEmpty()) {
                if (const QString file = exportTexture(colour); !file.isEmpty()) {
                    mtl += "map_Kd " + ref(file) + '\n';
                    if (material.punchthrough && transparent.value(file))
                        mtl += "map_d " + ref(file) + '\n';
                }
            }
            if (!normal.isEmpty()) {
                if (const QString file = exportTexture(normal); !file.isEmpty())
                    mtl += "map_Bump " + ref(file) + '\n';
            }
            for (const auto& entry : material.maps) {
                if (entry.second == colour || entry.second == normal)
                    continue;
                if (const QString file = exportTexture(entry.second); !file.isEmpty())
                    mtl += "# " + entry.first.toUtf8() + ' ' + ref(file) + '\n';
            }
        }
        submeshNames.append(materialNames.value(index));
    }

    // 4. The files.
    const QString objPath = QDir(outDir).filePath(baseName + u".obj"_s);
    const QString mtlName = baseName + u".mtl"_s;
    const bool withMtl = !materialNames.isEmpty();
    if (withMtl) {
        const QByteArray header = "# " + meshPath.toUtf8() + " - exported by Blobset Tools\n";
        if (!writeFile(QDir(outDir).filePath(mtlName), header + mtl, &error)) {
            result.message = error;
            return result;
        }
    }
    if (!writeFile(objPath, mesh.toObj(meshPath, withMtl ? mtlName : QString(), submeshNames), &error)) {
        result.message = error;
        return result;
    }

    result.ok = true;
    if (!withMtl) {
        result.message = u"Extracted %1 (no textures: %2)."_s.arg(
            QDir::toNativeSeparators(objPath),
            base.isEmpty() ? u"the mesh has no file name to find its material by"_s
            : !context.valid ? u"no game is loaded"_s
                             : u"no material was found for it"_s);
        return result;
    }
    result.message = u"Extracted %1, %2 and %3 texture(s) into %4."_s.arg(QFileInfo(objPath).fileName(), mtlName)
                         .arg(written)
                         .arg(QDir::toNativeSeparators(written > 0 ? textureDir.path() : outDir));
    if (!missing.isEmpty()) {
        result.message += u" %1 texture(s) weren't found: %2"_s.arg(missing.size())
                              .arg(missing.mid(0, 5).join(u", "_s) + (missing.size() > 5 ? u", ..."_s : QString()));
    }
    return result;
}

MeshTextureExport::Look MeshTextureExport::resolveLook(const MeshFile& mesh, const QString& meshPath,
                                                       const Context& context, const QString& pngDir, int maxSize,
                                                       const Progress& progress, const Cancelled& cancelled) {
    Look look;
    const QString base = modelBase(meshPath);
    const QList<MeshFile::Material> materials = loadMaterials(mesh, base, context);
    look.submeshMaterial = submeshMaterials(mesh, materials);
    look.files.resize(materials.size());
    look.transparent.resize(materials.size());

    // The colour textures of the materials that are drawn. Each one becomes a PNG named after the game and the
    // texture, kept for the session: meshes sharing textures (stadiums) load them once.
    static QMutex cacheMutex;
    static QHash<QString, bool> cached;   // PNG file -> it has see-through pixels.
    const auto fileFor = [&](const QString& path) {
        const QByteArray id = QCryptographicHash::hash((context.assetLocation + u'|' + QString::number(BlobsetEntryRegistry::generation()) + u'|' + path).toUtf8(),
                                                       QCryptographicHash::Md5).toHex();
        return QDir(pngDir).filePath(QString::fromLatin1(id) + u'_' + QString::number(maxSize) + u".png"_s);
    };
    QStringList wanted;
    for (qsizetype i = 0; i < materials.size(); ++i) {
        if (!look.submeshMaterial.contains(int(i)))
            continue;
        const QString path = normalizeTexturePath(colourMap(materials[i]));
        if (path.isEmpty() || wanted.contains(path))
            continue;
        QMutexLocker locker(&cacheMutex);
        if (!cached.contains(fileFor(path)) || !QFileInfo::exists(fileFor(path)))
            wanted.append(path);
    }
    const Fetched fetched = fetchTextures(wanted, context, base, maxSize, progress, cancelled);
    look.missing = fetched.missing;
    if (cancelled && cancelled())
        return look;

    // PNGs for Qt Quick 3D (its Texture only takes files), written on every core with light compression.
    const QStringList paths = fetched.images.keys();
    std::atomic<int> savedCount{0};
    const QList<bool> saved = QtConcurrent::blockingMapped<QList<bool>>(paths, [&](const QString& path) {
        const QImage image = fetched.images.value(path);
        const QString file = fileFor(path);
        const bool ok = !(cancelled && cancelled()) && image.save(file, "PNG", 80);
        if (ok) {
            const bool transparent = hasTransparency(image);
            QMutexLocker locker(&cacheMutex);
            cached.insert(file, transparent);
        }
        const int n = ++savedCount;
        if (progress)
            progress(0.9 + 0.1 * n / std::max(1, int(paths.size())), u"Preparing textures..."_s);
        return ok;
    });
    Q_UNUSED(saved)
    QMutexLocker locker(&cacheMutex);
    for (qsizetype i = 0; i < materials.size(); ++i) {
        if (!look.submeshMaterial.contains(int(i)))
            continue;
        const QString path = normalizeTexturePath(colourMap(materials[i]));
        if (path.isEmpty())
            continue;
        const QString file = fileFor(path);
        if (!cached.contains(file))
            continue;
        look.files[i] = file;
        // Only alpha tested materials: other textures' alpha holds something else (and would cut holes in them).
        look.transparent[i] = materials[i].punchthrough && cached.value(file);
    }
    QSet<QString> distinct;
    for (const QString& file : std::as_const(look.files))
        if (!file.isEmpty())
            distinct.insert(file);
    look.found = int(distinct.size());
    return look;
}
