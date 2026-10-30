#include "bsb_tool_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "bsb_file.h"
#include "core_defs.h"
#include "creator_support.h"
#include "dds_image_provider.h"
#include "xml_models.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
    return file.readAll();
}

bool writeFile(const QString& path, const QByteArray& data, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = u"Could not create the folder of %1."_s.arg(QDir::toNativeSeparators(path));
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        *error = u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString());
        return false;
    }
    return true;
}

// "ui/bsb/1019.bsb" / "0x1A2B" -> "1019" / "0x1A2B"
QString baseNameOf(const QString& name) {
    QString base = QString(name).replace(u'\\', u'/');
    base = base.mid(base.lastIndexOf(u'/') + 1);
    const qsizetype dot = base.lastIndexOf(u'.');
    if (dot > 0)
        base = base.left(dot);
    return base.isEmpty() ? u"movie"_s : base;
}

} // namespace

BsbToolManager::BsbToolManager(QObject* parent) : TxpkToolManager(parent) {}

bool BsbToolManager::isV1Game() const {
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    return orchestrator && orchestrator->blobsetVersion() == static_cast<int>(CoreDefs::BlobsetVersion::V1);
}

// ---------------------------------------------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------------------------------------------

bool BsbToolManager::loadBsbBytes(const QByteArray& data, qint64 mainSize, const QString& sourceName) {
    m_movie.clear();
    m_movieInfo.clear();
    m_baseName = baseNameOf(sourceName);

    const std::optional<Bsb::Header> header = Bsb::readHeader(data);
    if (!header) {
        clearEntries();
        emit fileListChanged();
        emit movieChanged();
        finish(false, u"%1 isn't a BSB (the \" BSB\" header is missing or its sections don't fit)."_s.arg(sourceName));
        return false;
    }

    const qint64 mainPart = mainSize >= 0 ? mainSize : header->mainSize();
    const QByteArray movie = data.mid(header->movieOffset, header->movieSize);
    const QByteArray vram = data.mid(mainPart);

    // The textures: the TXPK main section followed by the vram section, like a standalone TXPK.
    if (header->txpkSize >= 8) {
        const QByteArray txpk = data.mid(header->txpkOffset, header->txpkSize) + vram;
        if (!loadFromBytes(txpk, header->txpkSize, header->bigEndian, sourceName))
            return false;
    } else {
        clearEntries();
        m_sourceName = sourceName;
        rebuildTree();
        emit fileListChanged();
    }

    // The movie asks for its textures by their original names (".tga", ...): they're extracted as .dds.
    bool renamed = false;
    for (int i = 0; i < m_entries.size(); ++i) {
        QString& path = m_entries[i].ddsPath;
        const qsizetype slash = path.lastIndexOf(u'/');
        const qsizetype dot = path.lastIndexOf(u'.');
        if (dot > slash && path.mid(dot + 1).compare(u"dds"_s, Qt::CaseInsensitive) != 0) {
            path = path.left(dot) + u".dds"_s;
            m_fileList[i] = path;
            renamed = true;
        }
    }
    if (renamed) {
        rebuildTree();
        emit fileListChanged();
    }

    m_movie = movie;
    m_movieInfo = Bsb::movieInfo(movie);
    m_movieInfo[u"size"_s] = DdsImageProvider::humanSize(movie.size());
    if (!m_movieInfo.contains(u"signature"_s))
        m_movieInfo[u"signature"_s] = u"unknown"_s;
    emit movieChanged();

    finish(true, u"Loaded %1: %2 movie (%3), %4 texture(s)."_s
                     .arg(sourceName, m_movieInfo.value(u"signature"_s).toString(),
                          DdsImageProvider::humanSize(movie.size()))
                     .arg(m_entries.size()));
    return true;
}

void BsbToolManager::loadBsb(const QString& bsbPath) {
    if (m_busy)
        return;
    const QString path = CreatorSupport::toLocalPath(bsbPath);
    try {
        QByteArray data = readFile(path);
        qint64 mainSize = -1;
        // Save As writes the vram section next to the main section.
        if (QFileInfo::exists(path + u".vram"_s)) {
            mainSize = data.size();
            data += readFile(path + u".vram"_s);
        }
        if (loadBsbBytes(data, mainSize, QFileInfo(path).fileName())) {
            const AppOrchestrator* orchestrator = AppOrchestrator::instance();
            m_source = CreatorSupport::fromFile(path, orchestrator ? orchestrator->blobsetVersion() : 0, m_platform);
        }
    } catch (const std::exception& ex) {
        clearEntries();
        emit fileListChanged();
        finish(false, QString::fromUtf8(ex.what()));
    }
}

void BsbToolManager::loadBsbFromBlobset(const QString& hexHash, const QString& workspacePath) {
    if (m_busy)
        return;

    BlobsetEntry entry;
    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry) || !orchestrator) {
        clearEntries();
        emit fileListChanged();
        finish(false, u"This BSB (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return;
    }

    QByteArray data;
    qint64 mainSize = 0;
    try {
        data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                        orchestrator->platform(), &mainSize);
    } catch (const std::exception& ex) {
        clearEntries();
        emit fileListChanged();
        finish(false, QString::fromUtf8(ex.what()));
        return;
    }

    const QString name = workspacePath.isEmpty() ? u"0x"_s + hexHash.toUpper() : QString(workspacePath).replace(u'\\', u'/');
    if (loadBsbBytes(data, mainSize, name))
        m_source = CreatorSupport::fromBlobset(entry, orchestrator->blobsetVersion(), m_platform, u"bsb"_s);
}

// ---------------------------------------------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------------------------------------------

QString BsbToolManager::movieFileName() const {
    return m_baseName + u".swf"_s;
}

void BsbToolManager::extractMovie(const QString& outPath) {
    if (m_movie.isEmpty()) {
        finish(false, u"No BSB is loaded."_s);
        return;
    }
    const QString path = CreatorSupport::toLocalPath(outPath);
    QString error;
    if (writeFile(path, m_movie, &error))
        finish(true, u"Saved the movie to %1."_s.arg(QDir::toNativeSeparators(path)));
    else
        finish(false, error);
}

QString BsbToolManager::writeBsbList(const QString& outDir) const {
    XmlExtractFileInfo list;
    list.type = u"BSB"_s;
    list.index = m_source.index >= 0 ? static_cast<quint32>(m_source.index) : 0;
    list.folderHash = m_source.folderHash;
    list.fileNameHash = m_source.fileHash;
    list.isCompressed = m_source.isCompressed;
    list.platform = CreatorSupport::platformName(m_platform);
    list.version = m_source.version;
    list.outputName = m_baseName + u".bsb"_s;
    list.recordAlignment = m_recordAlignment;
    list.dataAlignment = m_dataAlignment;
    list.movie = movieFileName();

    for (int i = 0; i < m_entries.size(); ++i) {
        ExtractFileEntry entry;
        entry.filePath = m_entries[i].storedPath;
        entry.file = outputPath(i, false);
        entry.imageType = m_entries[i].imageType;
        list.entries.append(entry);
    }

    const QString path = QDir(outDir).filePath(CreatorSupport::bsbListName());
    if (!list.serialize(path))
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(path));
    return {};
}

void BsbToolManager::extractBsbAll(const QString& outDirPath) {
    if (m_busy)
        return;
    if (m_movie.isEmpty()) {
        finish(false, u"No BSB is loaded."_s);
        return;
    }

    const QString outDir = CreatorSupport::toLocalPath(outDirPath);
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        finish(false, u"Could not create the output folder: "_s + outDir);
        return;
    }

    const int total = static_cast<int>(m_entries.size());
    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Extracting the movie and %1 texture(s)..."_s.arg(total));

    m_extractFuture = QtConcurrent::run([this, total, outDir]() {
        QString firstError;
        int failed = 0;

        QString error;
        if (!writeFile(QDir(outDir).filePath(movieFileName()), m_movie, &error)) {
            ++failed;
            firstError = error;
        }

        int lastPercent = -1;
        for (int i = 0; i < total && !m_cancel; ++i) {
            const QString textureError = writeEntryTo(i, outDir, false, false, true);
            if (!textureError.isEmpty()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = textureError;
            }
            const int percent = static_cast<int>((qint64(i) + 1) * 100 / std::max(1, total));
            if (percent != lastPercent) {
                lastPercent = percent;
                const QString message = u"Extracting (%1/%2): %3"_s.arg(i + 1).arg(total).arg(outputPath(i, false));
                QMetaObject::invokeMethod(this, [this, percent, message]() { reportProgress(percent, message); },
                                          Qt::QueuedConnection);
            }
        }
        if (m_cancel)
            return;

        const QString listError = writeBsbList(outDir);
        if (!listError.isEmpty()) {
            ++failed;
            if (firstError.isEmpty())
                firstError = listError;
        }

        const bool ok = failed == 0;
        const QString summary = ok ? u"Extracted %1 and %2 texture(s) to %3, with %4 for the BSB Creator."_s
                                         .arg(movieFileName()).arg(total)
                                         .arg(QDir::toNativeSeparators(outDir), CreatorSupport::bsbListName())
                                   : u"%1 item(s) failed - first error: %2"_s.arg(failed).arg(firstError);
        QMetaObject::invokeMethod(this, [this, ok, summary]() {
            setProgress(ok ? 100 : 0);
            setBusy(false);
            finish(ok, summary);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------------------------------------------
// BSB Creator
// ---------------------------------------------------------------------------------------------------------------

QVariantMap BsbToolManager::inspectBsbList(const QString& xmlPath, const QString& sourceDir) const {
    return CreatorSupport::inspectList(xmlPath, sourceDir, u"BSB"_s);
}

void BsbToolManager::createBsb(const QString& xmlPathIn, const QString& sourceDirIn, const QString& outBsbPathIn) {
    if (m_busy)
        return;

    const QString xmlPath = CreatorSupport::toLocalPath(xmlPathIn);
    const QString outPath = CreatorSupport::toLocalPath(outBsbPathIn);
    const QVariantMap inspected = inspectBsbList(xmlPath, sourceDirIn);
    if (!inspected.value(u"ok"_s).toBool()) {
        finish(false, inspected.value(u"error"_s).toString());
        return;
    }
    if (outPath.trimmed().isEmpty()) {
        finish(false, u"Choose where to save the new BSB."_s);
        return;
    }
    const QString sourceDir = inspected.value(u"sourceDir"_s).toString();

    XmlExtractFileInfo list;
    list.deserialize(xmlPath);

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const int fallbackPlatform = orchestrator ? orchestrator->platform() : static_cast<int>(CoreDefs::Platforms::Windows);
    if (!isV1Game())
        emit creatorLog(u"BSBs belong to the Blobset V1 games: load one of those to use the BSB with Modify / Create."_s,
                        false);

    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Creating %1..."_s.arg(QFileInfo(outPath).fileName()));

    m_extractFuture = QtConcurrent::run([this, list, sourceDir, outPath, fallbackPlatform]() {
        const auto log = [this](const QString& message, bool ok) {
            QMetaObject::invokeMethod(this, [this, message, ok]() { emit creatorLog(message, ok); }, Qt::QueuedConnection);
        };
        const auto progress = [this](int percent, const QString& message) {
            QMetaObject::invokeMethod(this, [this, percent, message]() { reportProgress(percent, message); },
                                      Qt::QueuedConnection);
        };

        bool ok = true;
        QString summary;
        try {
            // 1. The movie.
            if (list.movie.isEmpty())
                fail(u"The list doesn't name the movie (.swf)."_s);
            const QByteArray movie = readFile(QDir(sourceDir).filePath(list.movie));
            const QVariantMap info = Bsb::movieInfo(movie);
            if (info.isEmpty())
                fail(u"%1 isn't a Flash movie (FWS / CWS / GFX / CFX)."_s.arg(list.movie));
            log(u"Movie: %1 (%2 v%3, %4)."_s.arg(list.movie, info.value(u"signature"_s).toString())
                    .arg(info.value(u"version"_s).toInt()).arg(DdsImageProvider::humanSize(movie.size())), true);

            // 2. The textures, as a TXPK (the same builder the TXPK Creator uses).
            TxpkToolManager::BuiltTxpk built;
            if (!buildTxpk(list, sourceDir, fallbackPlatform, built, progress, log, &m_cancel))
                return;
            const bool bigEndian = built.platform != static_cast<int>(CoreDefs::Platforms::Windows);

            // 3. <name>.bsb = main section (header + movie + TXPK main) + vram section.
            const QByteArray mainPart = Bsb::build(movie, built.mainPart, bigEndian);
            progress(90, u"Writing %1..."_s.arg(QFileInfo(outPath).fileName()));
            QString error;
            if (!writeFile(outPath, mainPart + built.vram, &error))
                fail(error);

            // 4. <name>.xml with the blobset entry and the section sizes.
            XmlModifyFileInfo xml;
            xml.index = list.index;
            xml.folderHash = list.folderHash;
            xml.fileNameHash = list.fileNameHash;
            xml.isCompressed = list.isCompressed;
            xml.mainCompressedSize = xml.mainUnCompressedSize = static_cast<quint32>(mainPart.size());
            xml.vramCompressedSize = xml.vramUnCompressedSize = static_cast<quint32>(built.vram.size());
            xml.entries = built.entries;
            const QFileInfo outInfo(outPath);
            const QString xmlOut = outInfo.absolutePath() + u'/' + outInfo.completeBaseName() + u".xml"_s;
            if (!xml.serialize(xmlOut))
                fail(u"Could not write %1."_s.arg(QDir::toNativeSeparators(xmlOut)));

            log(u"Wrote %1 (main %2, vram %3)."_s.arg(QDir::toNativeSeparators(outPath),
                                                      DdsImageProvider::humanSize(mainPart.size()),
                                                      DdsImageProvider::humanSize(built.vram.size())), true);
            log(u"Wrote %1."_s.arg(QDir::toNativeSeparators(xmlOut)), true);
            if (list.folderHash.isEmpty() && list.fileNameHash.isEmpty() && list.index == 0)
                log(u"The list doesn't say which blobset entry this BSB replaces: name it like the original "
                    u"(e.g. ui\\bsb\\1019.bsb) in your mods folder."_s, false);

            summary = u"Created %1 with %2 texture(s)."_s.arg(outInfo.fileName()).arg(built.entries.size());
        } catch (const std::exception& ex) {
            ok = false;
            summary = QString::fromUtf8(ex.what());
        }

        QMetaObject::invokeMethod(this, [this, ok, summary]() {
            setProgress(ok ? 100 : 0);
            setBusy(false);
            finish(ok, summary);
        }, Qt::QueuedConnection);
    });
}
