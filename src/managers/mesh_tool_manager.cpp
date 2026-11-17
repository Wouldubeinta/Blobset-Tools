#include "mesh_tool_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "compression_io.h"
#include "core_defs.h"
#include "hash_utility.h"
#include "mesh_texture_export.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <memory>

using namespace Qt::StringLiterals;

// ==============================================================================================================
// MeshGeometry
// ==============================================================================================================

MeshGeometry::MeshGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {}

QByteArray MeshGeometry::quick3DVertices(const MeshFile& mesh) {
    QByteArray vertices = mesh.displayVertices;
    auto* v = reinterpret_cast<float*>(vertices.data());
    const qsizetype count = vertices.size() / MeshFile::kDisplayStride;
    for (qsizetype k = 0; k < count; ++k)
        v[k * 8 + 7] = 1.0f - v[k * 8 + 7];
    return vertices;
}

QByteArray MeshGeometry::decalVertices(const MeshFile& mesh) {
    const qsizetype count = mesh.displayVertices.size() / MeshFile::kDisplayStride;
    if (count == 0 || mesh.displayUv2.size() != count * 2 * qsizetype(sizeof(float)))
        return {};
    QByteArray vertices = mesh.displayVertices;
    auto* v = reinterpret_cast<float*>(vertices.data());
    const auto* uv = reinterpret_cast<const float*>(mesh.displayUv2.constData());
    // About half a millimetre on a player (1.8 m), more on bigger meshes: over the surface, not floating off it.
    const float offset = std::max(1e-5f, (mesh.boundsMax - mesh.boundsMin).length() * 0.0003f);
    for (qsizetype k = 0; k < count; ++k) {
        float* p = v + k * 8;
        for (int j = 0; j < 3; ++j)
            p[j] += p[3 + j] * offset;
        p[6] = uv[k * 2];
        p[7] = 1.0f - uv[k * 2 + 1];
    }
    return vertices;
}

void MeshGeometry::setPart(const MeshFile& mesh, const QByteArray& vertices, const QList<int>& submeshes) {
    QByteArray indices;
    for (const int s : submeshes) {
        const MeshFile::Submesh& sub = mesh.submeshes.at(s);
        indices += mesh.displayIndices.mid(qsizetype(sub.indexStart) * 4, qsizetype(sub.indexCount) * 4);
    }
    clear();
    setVertexData(vertices);
    setIndexData(indices);
    setStride(MeshFile::kDisplayStride);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 24, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    setBounds(mesh.boundsMin, mesh.boundsMax);
    update();
}

void MeshGeometry::setMesh(const MeshFile& mesh) {
    clear();
    setVertexData(quick3DVertices(mesh));
    setIndexData(mesh.displayIndices);
    setStride(MeshFile::kDisplayStride);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 24, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    setBounds(mesh.boundsMin, mesh.boundsMax);
    update();
}

void MeshGeometry::clearMesh() {
    clear();
    update();
}

// ==============================================================================================================
// MeshToolManager
// ==============================================================================================================

namespace {

// The .blendshapes.mdat entries that may belong to a skinned model: by name ("<part>.modelcomponent.mdl" ->
// "<part>.blendshapes"), then its neighbours in the blobset (V4 headers are sorted by name, so the part's
// ".blendshapes" comes right before its ".modelcomponent"). MeshFile::parseSkinned checks the vertex count.
QList<QByteArray> blendshapeCandidates(const BlobsetEntry& model, const QString& sourceName,
                                       const AppOrchestrator* orchestrator) {
    QList<BlobsetEntry> entries;
    const QString suffix = u".modelcomponent.mdl"_s;
    if (sourceName.endsWith(suffix, Qt::CaseInsensitive)) {
        const QString base = sourceName.chopped(suffix.size()).toLower();
        BlobsetEntry entry;
        if (BlobsetEntryRegistry::findEntry(BlobsetEntryRegistry::makeKey(BlobsetHash::hashV4(base, u"blendshapes"_s)),
                                            entry))
            entries.append(entry);
    }
    for (const qint64 index : {qint64(model.fileIndex) - 1, qint64(model.fileIndex) + 1}) {
        BlobsetEntry entry;
        if (index >= 0 && BlobsetEntryRegistry::findByIndex(quint32(index), entry)
            && std::none_of(entries.cbegin(), entries.cend(),
                            [&](const BlobsetEntry& e) { return e.fileIndex == entry.fileIndex; }))
            entries.append(entry);
    }

    QList<QByteArray> out;
    for (const BlobsetEntry& entry : std::as_const(entries)) {
        try {
            qint64 mainSize = -1;
            const QByteArray data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(),
                                                             orchestrator->blobsetVersion(), orchestrator->platform(),
                                                             &mainSize, true);
            if (data.size() >= 16 && qFromLittleEndian<quint32>(data.constData()) == 0x2480137Fu)
                out.append(data);
        } catch (const std::exception&) {
        }
    }
    return out;
}

// The first file of an M3MP ("stadiums\\nrl_aami\\stadium_t1.model.m3mp" holds "stadium_t1.gz.ps3", a PSSG):
// "M3MP" (or "PM3M" big endian), file and chunk counts, the file table {offset, size, path offset, 0}, the paths,
// then the chunk table {offset, compressed size, size}; the files are at their offsets in the decompressed chunks
// (or right after the tables when there are none). Empty when it isn't an M3MP.
QByteArray firstMemPackFile(const QByteArray& data, int version) {
    if (data.size() < 16)
        return {};
    const bool big = data.startsWith("PM3M");
    if (!big && !data.startsWith("M3MP"))
        return {};
    const auto u32 = [&](qint64 at) -> quint32 {
        if (at < 0 || at + 4 > data.size())
            return 0;
        return big ? qFromBigEndian<quint32>(data.constData() + at) : qFromLittleEndian<quint32>(data.constData() + at);
    };
    const quint32 files = u32(4), chunks = u32(8);
    if (files == 0 || 16 + qint64(files) * 16 + qint64(chunks) * 12 > data.size())
        return {};
    const quint32 offset = u32(16), size = u32(20);

    // The chunk table: at its header offset (it follows the paths).
    const qint64 tableEnd = u32(12);
    if (tableEnd < 16 + qint64(files) * 16 || tableEnd + qint64(chunks) * 12 > data.size())
        return {};
    QByteArray payload;
    if (chunks == 0) {
        payload = data.mid(tableEnd);
    } else {
        const bool lzma = version == static_cast<int>(CoreDefs::BlobsetVersion::V1)
                          || version == static_cast<int>(CoreDefs::BlobsetVersion::V2);
        for (quint32 c = 0; c < chunks; ++c) {
            const qint64 at = tableEnd + qint64(c) * 12;
            const quint32 chunkOffset = u32(at), packed = u32(at + 4), unpacked = u32(at + 8);
            if (qint64(chunkOffset) + packed > data.size())
                return {};
            const QByteArray chunk = data.mid(chunkOffset, packed);
            if (packed == unpacked)
                payload += lzma ? CompressionIO::decodeV1Chunk(chunk, unpacked) : chunk;
            else if (lzma)
                payload += CompressionIO::decompressLzma(chunk, unpacked);
            else
                return {};   // Other games' compressed mem packs: not meshes this decodes.
            if (qint64(payload.size()) >= qint64(offset) + size)
                break;   // The first file is complete.
        }
    }
    if (qint64(offset) + size > payload.size())
        return {};
    return payload.mid(offset, size);
}

} // namespace

MeshToolManager::MeshToolManager(QObject* parent) : QObject(parent), m_geometry(new MeshGeometry()) {
    m_geometry->setParent(this);
}

MeshToolManager::~MeshToolManager() {
    m_export.waitForFinished();   // The export reads its own copies, but writes files the user expects whole.
    m_textureJob.waitForFinished();
    delete m_textureDir;   // Removes the decoded textures (kept for the session: meshes share them).
}

QString MeshToolManager::appVersion() const {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QString() : u"v"_s + version;
}

float MeshToolManager::radius() const {
    return m_loaded ? std::max(0.001f, size().length() / 2) : 1.0f;
}

QString MeshToolManager::infoLine() const {
    if (!m_loaded)
        return {};
    const QVector3D s = size();
    return u"%1  |  %2  |  %3 vertices  |  %4 triangles  |  %5 submesh(es)  |  %6 x %7 x %8"_s
        .arg(m_sourceName, m_mesh.formatName)
        .arg(m_mesh.vertexCount)
        .arg(m_mesh.triangleCount())
        .arg(m_mesh.submeshes.size())
        .arg(s.x(), 0, 'f', 2).arg(s.y(), 0, 'f', 2).arg(s.z(), 0, 'f', 2);
}

void MeshToolManager::setStatus(bool ok, const QString& message) {
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}

void MeshToolManager::loadMeshFromBlobset(const QString& hexHash, const QString& displayName) {
    m_loaded = false;
    m_mesh = MeshFile();
    m_raw.clear();
    m_sourceName = displayName.isEmpty() ? u"0x"_s + hexHash.toUpper() : QString(displayName).replace(u'\\', u'/');
    m_geometry->clearMesh();
    ++m_loadId;   // Textures still loading for the previous mesh stop and are dropped.
    m_currentLoad->store(m_loadId);
    clearParts();

    const auto finish = [this](bool ok, const QString& message) {
        emit meshChanged();
        setStatus(ok, message);
    };

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry))
        return finish(false, u"This mesh (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator)
        return finish(false, u"The application orchestrator is unavailable."_s);

    qint64 mainSize = -1;
    try {
        // V4 meshes live in the main section; the older layout (RLL3 / RLL4) keeps its buffers in vram.
        m_raw = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                         orchestrator->platform(), &mainSize, true);
    } catch (const std::exception& ex) {
        return finish(false, QString::fromUtf8(ex.what()));
    }

    // Rugby League Live 2 / AFL Live models: a mem pack holding the model's PSSG ("<model>.model.m3mp").
    if (const QByteArray inner = firstMemPackFile(m_raw, orchestrator->blobsetVersion()); MeshFile::isPssg(inner))
        m_raw = inner;

    QString error;
    bool parsed = false;
    if (MeshFile::isPssg(m_raw)) {
        parsed = m_mesh.parsePssg(m_raw, &error);
    } else if (MeshFile::isSkinnedModel(m_raw)) {
        // RL26 skinned model: the positions come from its .blendshapes.mdat.
        const QList<QByteArray> candidates = blendshapeCandidates(entry, m_sourceName, orchestrator);
        if (candidates.isEmpty())
            parsed = m_mesh.parseSkinned(m_raw, QByteArray(), &error);
        for (const QByteArray& blendshapes : candidates) {
            QString candidateError;
            parsed = m_mesh.parseSkinned(m_raw, blendshapes, &candidateError);
            if (error.isEmpty())
                error = candidateError;
            if (parsed)
                break;
        }
    } else {
        parsed = m_mesh.parse(m_raw, &error, mainSize);
    }
    if (!parsed) {
        m_mesh = MeshFile();
        return finish(false, error);
    }

    m_loaded = true;
    m_geometry->setMesh(m_mesh);
    setParts({{-1, {}}}, {}, {}, m_mesh);   // Untextured until the textures are in.
    finish(true, infoLine());
    loadTextures();
}

void MeshToolManager::setParts(const QMap<int, QList<int>>& groups, const QStringList& files,
                               const QList<bool>& transparent, const MeshFile& mesh, const QStringList& decals) {
    // The models showing the old parts go when QML sees the new list; their geometries after that.
    const QList<MeshGeometry*> old = m_partGeometries;
    m_partGeometries.clear();
    QVariantList parts;
    const QByteArray vertices = MeshGeometry::quick3DVertices(mesh);
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        QList<int> submeshes = it.value();
        if (submeshes.isEmpty())   // The whole mesh.
            for (qsizetype s = 0; s < mesh.submeshes.size(); ++s)
                submeshes.append(int(s));
        auto* geometry = new MeshGeometry();
        geometry->setParent(this);
        geometry->setPart(mesh, vertices, submeshes);
        m_partGeometries.append(geometry);
        const QString file = it.key() >= 0 ? files.value(it.key()) : QString();
        parts.append(QVariantMap{{u"geometry"_s, QVariant::fromValue(geometry)},
                                 {u"texture"_s, file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString()},
                                 {u"transparent"_s, it.key() >= 0 && transparent.value(it.key())}});
    }
    // The decal layers (PSSG uniforms: logos, numbers): the same submeshes drawn again with the second UV set and
    // the decal texture, alpha tested, just over the surface. Each triangle shows only its own decals (the front and
    // back of a shirt can share the colour texture's area, so the decals can't be painted into it).
    QByteArray decalLayer;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const QString decal = it.key() >= 0 ? decals.value(it.key()) : QString();
        if (decal.isEmpty() || it.value().isEmpty())
            continue;
        if (decalLayer.isEmpty())
            decalLayer = MeshGeometry::decalVertices(mesh);
        if (decalLayer.isEmpty())
            break;
        auto* geometry = new MeshGeometry();
        geometry->setParent(this);
        geometry->setPart(mesh, decalLayer, it.value());
        m_partGeometries.append(geometry);
        parts.append(QVariantMap{{u"geometry"_s, QVariant::fromValue(geometry)},
                                 {u"texture"_s, QUrl::fromLocalFile(decal).toString()},
                                 {u"transparent"_s, true},
                                 {u"overlay"_s, true}});
    }
    m_parts = parts;
    emit partsChanged();
    for (MeshGeometry* geometry : old)
        geometry->deleteLater();
}

void MeshToolManager::clearParts() {
    m_parts.clear();
    m_textureInfo.clear();
    emit partsChanged();
    for (MeshGeometry* geometry : std::as_const(m_partGeometries))
        geometry->deleteLater();   // QML may still hold them until it sees the new parts.
    m_partGeometries.clear();
    setTextureProgress(false, 0, QString());
}

void MeshToolManager::setTextureProgress(bool loading, double progress, const QString& stage) {
    if (m_texturesLoading == loading && qFuzzyCompare(m_textureProgress + 1, progress + 1) && m_textureStage == stage)
        return;
    m_texturesLoading = loading;
    m_textureProgress = progress;
    m_textureStage = stage;
    emit textureProgressChanged();
}

void MeshToolManager::loadTextures() {
    const MeshTextureExport::Context context = MeshTextureExport::currentContext();
    if (!context.valid)
        return;
    if (!m_textureDir)
        m_textureDir = new QTemporaryDir();
    if (!m_textureDir->isValid())
        return;
    m_textureInfo = u"Loading textures..."_s;
    emit partsChanged();
    setTextureProgress(true, 0, u"Loading textures..."_s);

    // Everything runs on the thread pool: finding, reading and decoding the textures (on every core) and writing
    // the PNGs Qt Quick 3D loads. The UI thread only gets the progress and, at the end, the finished parts.
    const int loadId = m_loadId;
    const std::shared_ptr<std::atomic<int>> current = m_currentLoad;
    current->store(loadId);
    const MeshFile mesh = m_mesh;
    const QString source = m_sourceName;
    const QString dir = m_textureDir->path();
    const QPointer<MeshToolManager> self(this);
    m_textureJob = QtConcurrent::run([self, loadId, current, mesh, source, context, dir]() {
        const auto lastPermille = std::make_shared<std::atomic<int>>(-1);
        const MeshTextureExport::Progress progress = [self, loadId, lastPermille](double fraction, const QString& stage) {
            const int permille = int(std::clamp(fraction, 0.0, 1.0) * 1000);
            if (permille / 10 == lastPermille->load() / 10 && permille != 1000)
                return;   // Only whole percents reach the UI.
            lastPermille->store(permille);
            QMetaObject::invokeMethod(qApp, [self, loadId, permille, stage]() {
                if (self && self->m_loadId == loadId)
                    self->setTextureProgress(true, permille / 1000.0, stage);
            }, Qt::QueuedConnection);
        };
        const MeshTextureExport::Cancelled cancelled = [current, loadId]() { return current->load() != loadId; };
        // 1024 px is plenty on screen and keeps decoding, writing and uploading quick (Extract keeps full size).
        const MeshTextureExport::Look look = MeshTextureExport::resolveLook(
            mesh, source, context, dir, 1024, progress, cancelled);
        QMetaObject::invokeMethod(qApp, [self, loadId, mesh, look]() {
            if (!self || self->m_loadId != loadId)
                return;   // Another mesh was loaded meanwhile (its PNGs stay for later).

            // One part per material (and one for the submeshes without), when there is something to texture.
            if (look.found > 0) {
                QMap<int, QList<int>> groups;
                for (qsizetype s = 0; s < mesh.submeshes.size(); ++s)
                    groups[look.submeshMaterial.value(s, -1)].append(int(s));
                self->setParts(groups, look.files, look.transparent, mesh, look.decalFiles);
            }
            self->m_textureInfo = look.found == 0 && look.missing.isEmpty()
                                      ? u"No textures (no material found)"_s
                                      : u"%1 texture(s)"_s.arg(look.found)
                                            + (look.missing.isEmpty() ? QString()
                                                                      : u", %1 not found"_s.arg(look.missing.size()));
            emit self->partsChanged();
            self->setTextureProgress(false, 1, QString());
            if (!look.missing.isEmpty())
                self->setStatus(true, u"Textures not found: "_s + look.missing.mid(0, 6).join(u", "_s)
                                          + (look.missing.size() > 6 ? u", ..."_s : QString()));
        }, Qt::QueuedConnection);
    });
}

void MeshToolManager::extract(const QString& outDirPath, const QString& format) {
    if (!m_loaded) {
        setStatus(false, u"No mesh is loaded."_s);
        return;
    }

    const QString outDir = outDirPath.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(outDirPath).toLocalFile()
                                                                                  : outDirPath;
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        setStatus(false, u"Could not create the output folder: "_s + outDir);
        return;
    }

    const bool asObj = format.compare(u"obj"_s, Qt::CaseInsensitive) == 0;
    QString baseName = QFileInfo(m_sourceName).completeBaseName();
    if (baseName.endsWith(u".model"_s, Qt::CaseInsensitive) && m_mesh.formatName == u"PSSG"_s)
        baseName.chop(6);   // "stadium_t1.model.m3mp" -> stadium_t1.pssg / .obj
    if (asObj && baseName.endsWith(u".model"_s, Qt::CaseInsensitive))
        baseName.chop(6);   // "ball.model.mesh" -> ball.obj, ball.mtl, ball_textures.
    if (asObj && baseName.endsWith(u".modelcomponent"_s, Qt::CaseInsensitive))
        baseName.chop(15);  // "head.modelcomponent.mdl" -> head.obj
    if (baseName.isEmpty())
        baseName = u"mesh"_s;

    if (asObj) {
        // OBJ + MTL + textures: finding the textures can read many texture packs, so it runs in the background.
        if (m_busy)
            return;
        const MeshTextureExport::Context context = MeshTextureExport::currentContext();
        const MeshFile mesh = m_mesh;
        const QString source = m_sourceName;
        m_busy = true;
        emit busyChanged();
        setStatus(true, u"Exporting %1.obj with its textures..."_s.arg(baseName));
        const QPointer<MeshToolManager> self(this);
        m_export = QtConcurrent::run([self, mesh, source, outDir, baseName, context]() {
            const auto lastPercent = std::make_shared<std::atomic<int>>(-1);
            const MeshTextureExport::Progress progress = [self, lastPercent](double fraction, const QString& stage) {
                const int percent = int(std::clamp(fraction, 0.0, 1.0) * 100);
                if (lastPercent->exchange(percent) == percent)
                    return;
                QMetaObject::invokeMethod(qApp, [self, percent, stage]() {
                    if (self && self->m_busy)
                        self->setStatus(true, u"Exporting the OBJ: %1 (%2%)"_s.arg(stage).arg(percent));
                }, Qt::QueuedConnection);
            };
            const MeshTextureExport::Result result =
                MeshTextureExport::exportObj(mesh, source, outDir, baseName, context, progress);
            QMetaObject::invokeMethod(qApp, [self, result]() {
                if (!self)
                    return;
                self->m_busy = false;
                emit self->busyChanged();
                self->setStatus(result.ok, result.message);
                if (AppOrchestrator* orchestrator = AppOrchestrator::instance())
                    emit orchestrator->uiConsoleUpdate(u"Mesh Viewer: "_s + result.message);
            }, Qt::QueuedConnection);
        });
        return;
    }

    // The original file: skinned models keep their extension (their .blendshapes.mdat is a file of its own).
    const QString path = QDir(outDir).filePath(baseName + (m_mesh.formatName == u"PSSG"_s ? u".pssg"_s
                                                           : m_mesh.skinned               ? u".mdl"_s
                                                                                          : u".mesh"_s));
    const QByteArray& data = m_raw;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return;
    }
    setStatus(true, u"Extracted: "_s + QDir::toNativeSeparators(path));
}
