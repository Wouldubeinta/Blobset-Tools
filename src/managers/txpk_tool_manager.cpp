#include "txpk_tool_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "core_defs.h"
#include "dds_image_provider.h"
#include "dds_processor.h"
#include "gcm_texture.h"
#include "package_stream.h"
#include "txpk_file.h"
#include "x360_texture.h"
#include "xml_models.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

// QML hands folders / files over as "file:///..." URLs, C++ callers as plain paths.
QString toLocalPath(const QString& path) {
    if (path.startsWith(u"file:"_s, Qt::CaseInsensitive))
        return QUrl(path).toLocalFile();
    return path;
}

bool isPngFormat(const QString& format) {
    return format.compare(u"png"_s, Qt::CaseInsensitive) == 0;
}

// "folder/name.dds" -> "folder/name.<extension>"
QString withExtension(const QString& path, const QString& extension) {
    const qsizetype slash = path.lastIndexOf(u'/');
    const qsizetype dot = path.lastIndexOf(u'.');
    return (dot > slash ? path.left(dot) : path) + u'.' + extension;
}

// Console textures have no DDS header (same conversion as ExtractWorker::toConsoleDds): PS3 from the RSX
// descriptor, Xbox 360 from its D3D texture header (untiled). Empty for formats a .dds can't hold.
QByteArray toConsoleDds(const QByteArray& pixels, const Gcm::CellGcmTexture& gcm, bool isXbox360,
                        const X360::TextureHeader& x360) {
    if (isXbox360)
        return X360::toDds(x360, pixels).value_or(QByteArray());
    return Gcm::toDds(gcm, pixels).value_or(QByteArray());
}

} // namespace

// ==============================================================================================================
// TxpkToolManager
// ==============================================================================================================

TxpkToolManager::TxpkToolManager(QObject* parent) : QObject(parent) {
    m_treeModel = new TreeModel(this);

    // Save Changes ends with createTxpk(): tidy up its extraction folder and tell everyone the mods folder changed.
    connect(this, &TxpkToolManager::processComplete, this, [this](bool ok, const QString&) {
        if (!m_savingChanges)
            return;
        m_savingChanges = false;
        if (!m_editTemp.isEmpty())
            QDir(m_editTemp).removeRecursively();
        m_editTemp.clear();
        if (!ok)
            return;
        m_unsavedChanges = false;
        emit changesChanged();
        const QString target = modsTarget();
        if (AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
            orchestrator->notifyModFilesChanged();
            emit orchestrator->uiConsoleUpdate(u"Saved the edited TXPK to %1. Click Modify to put it into the game."_s
                                                   .arg(QDir::toNativeSeparators(target)));
        }
        setStatus(true, u"Saved to %1 - click Modify in the workspace to put it into the game."_s
                            .arg(QDir::toNativeSeparators(target)));
        emit changesSaved(target);
    });
}

TxpkToolManager::~TxpkToolManager() {
    m_cancel = true;
    m_extractFuture.waitForFinished(); // The worker thread still reads our members.
    DdsImageProvider::releaseImage(m_previewKey);
}

void TxpkToolManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void TxpkToolManager::clearEntries() {
    const bool hadChanges = !m_replacements.isEmpty();
    m_replacements.clear();
    m_unsavedChanges = false;
    m_workspacePath.clear();
    if (hadChanges)
        emit changesChanged();
    m_entries.clear();
    m_fileList.clear();
    m_data.clear();
    m_sourceName.clear();
    m_source = {};
    m_recordAlignment = 0;
    m_dataAlignment = 0;
    resetBrowser();
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool TxpkToolManager::loadFromBytes(const QByteArray& data, qint64 mainSize, bool isBigEndian,
                                    const QString& sourceName) {
    clearEntries();
    m_data = data;
    m_isBigEndian = isBigEndian;
    m_sourceName = sourceName;

    // Every texture lives below one top folder named after the TXPK, so textures without a folder show up too.
    m_rootName = sourceName;
    m_rootName.replace(u'/', u'_').replace(u'\\', u'_');
    if (m_rootName.trimmed().isEmpty())
        m_rootName = u"TXPK"_s;
    setProgress(0);

    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const int gameId = orchestrator ? orchestrator->gameId() : 0;
    if (!isBigEndian)
        m_platform = static_cast<int>(CoreDefs::Platforms::Windows);
    else if (orchestrator && orchestrator->platform() == static_cast<int>(CoreDefs::Platforms::Xbox360))
        m_platform = static_cast<int>(CoreDefs::Platforms::Xbox360);
    else
        m_platform = static_cast<int>(CoreDefs::Platforms::PS3);

    try {
        if (m_data.size() < 8)
            fail(u"The file is too small to be a TXPK."_s);

        // Cheap sanity check first: a wrong endianness would otherwise ask for billions of entries.
        const uchar* raw = reinterpret_cast<const uchar*>(m_data.constData());
        const quint32 magic = isBigEndian ? qFromBigEndian<quint32>(raw) : qFromLittleEndian<quint32>(raw);
        const quint32 count = isBigEndian ? qFromBigEndian<quint32>(raw + 4) : qFromLittleEndian<quint32>(raw + 4);

        // The bytes are always "KPXT": TXPK read little endian, KPXT read big endian.
        if (magic != static_cast<quint32>(CoreDefs::FileType::TXPK) &&
            magic != static_cast<quint32>(CoreDefs::FileType::KPXT))
            fail(u"Invalid TXPK magic number. Is this a TXPK, and does its platform (endianness) match the selected game?"_s);

        if (count == 0) {
            // Plenty of these in the older games, e.g. RLL3 PC and PS3 (8 byte placeholder TXPKs): valid, just
            // nothing to show. The viewer says so instead of looking like it is still loading.
            rebuildTree();
            emit fileListChanged();
            finish(true, u"TXPK %1 is empty (0 textures): nothing to preview or extract."_s.arg(sourceName));
            return true;
        }

        if (static_cast<qint64>(count) * 16 + 8 > m_data.size())
            fail(u"Invalid TXPK header (bad file count)."_s);

        QBuffer buffer(&m_data);
        buffer.open(QIODevice::ReadOnly);
        PackageStream in(&buffer, isBigEndian ? PackageStream::Big : PackageStream::Little);

        TXPK txpk;
        txpk.deserialize(in, isBigEndian);

        if (mainSize < 0) {
            // Standalone file: the vram section starts right after the header / info records.
            qint64 headerEnd = 0;
            for (const TXPK::Entry& e : txpk.getEntries())
                headerEnd = std::max<qint64>(headerEnd, static_cast<qint64>(e.DDSPathOffset) + e.DDSPathSize);
            mainSize = headerEnd;
        }

        const qint64 fileSize = m_data.size();
        const auto fits = [&](qint64 offset, qint64 size) {
            return offset >= 0 && size >= 0 && offset + size <= fileSize;
        };
        const auto looksRight = [&](qint64 offset) {
            return isBigEndian || m_data.mid(offset, 4) == "DDS "; // PC data is a complete .dds file.
        };

        for (const TXPK::Entry& e : txpk.getEntries()) {
            TxpkEntry entry;
            entry.dataSize = e.DDSDataSize;
            entry.width = e.DDSWidth;
            entry.height = e.DDSHeight;
            entry.gcm = e.GcmTexture;
            entry.isXbox360 = e.IsXbox360;
            entry.x360 = e.X360Header;
            entry.mipMaps = e.IsXbox360 ? static_cast<quint8>(e.X360Header.levelCount()) : e.GcmTexture.mipmap;
            entry.storedPath = e.DDSFilePath;
            entry.imageType = e.DDSImageType;
            entry.recordOffset = e.DDSPathOffset;
            entry.recordSize = e.DDSPathSize;
            entry.rawDataOffset = e.DDSDataOffset;

            // Data offsets are relative to the vram section; fall back to absolute offsets if that doesn't fit.
            const qint64 candidates[2] = { mainSize + static_cast<qint64>(e.DDSDataOffset),
                                           static_cast<qint64>(e.DDSDataOffset) };
            entry.dataOffset = -1;
            for (qint64 c : candidates) {
                if (fits(c, entry.dataSize) && looksRight(c)) { entry.dataOffset = c; break; }
            }
            if (entry.dataOffset < 0) {
                for (qint64 c : candidates) {
                    if (fits(c, entry.dataSize)) { entry.dataOffset = c; break; }
                }
            }

            QString path = e.DDSFilePath;
            path.replace(u'\\', u'/');
            if (gameId > 15)
                path.replace(u".badds"_s, u".dds"_s); // Newer BigAnt games map .badds to .dds
            while (path.startsWith(u'/'))
                path.remove(0, 1);
            if (!QFileInfo(path).fileName().contains(u'.'))
                path += u".dds"_s;
            entry.ddsPath = path;

            m_entries.append(entry);
            m_fileList.append(path);
        }

        // Layout, so the TXPK Creator can rebuild it the same way (records packed on the older games,
        // texture data 128 byte aligned).
        QList<QPair<qint64, qint64>> records, textures;
        for (const TxpkEntry& entry : std::as_const(m_entries)) {
            records.append({entry.recordOffset, entry.recordSize});
            textures.append({entry.rawDataOffset, entry.dataSize});
        }
        m_recordAlignment = CreatorSupport::detectAlignment(records, 0, 16);
        m_dataAlignment = CreatorSupport::detectAlignment(textures, 0, 128);
        if (m_dataAlignment > 0 && m_dataAlignment < 128)
            m_dataAlignment = 128;   // Sizes that happen to follow each other: the textures are still 128 aligned.
    } catch (const std::exception& ex) {
        clearEntries();
        emit fileListChanged();
        finish(false, QString::fromUtf8(ex.what()));
        return false;
    }

    rebuildTree();
    emit fileListChanged();
    finish(true, u"Loaded %1 texture(s) from %2."_s.arg(m_entries.size()).arg(sourceName));
    return true;
}

void TxpkToolManager::loadTxpk(const QString& txpkPath, bool isBigEndian) {
    if (m_busy)
        return;

    const QString path = toLocalPath(txpkPath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        clearEntries();
        emit fileListChanged();
        finish(false, u"Failed to read TXPK container: "_s + path);
        return;
    }

    if (loadFromBytes(file.readAll(), -1, isBigEndian, QFileInfo(path).fileName())) {
        const AppOrchestrator* orchestrator = AppOrchestrator::instance();
        m_source = CreatorSupport::fromFile(path, orchestrator ? orchestrator->blobsetVersion() : -1, m_platform);
    }
}

void TxpkToolManager::loadTxpkFromBlobset(const QString& hexHash) {
    if (m_busy)
        return;

    const auto reportError = [this](const QString& message) {
        clearEntries();
        emit fileListChanged();
        finish(false, message);
    };

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        reportError(u"This TXPK (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return;
    }

    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        reportError(u"The application orchestrator is unavailable."_s);
        return;
    }

    const int platform = orchestrator->platform();
    const bool isBigEndian = platform != static_cast<int>(CoreDefs::Platforms::Windows);

    // An edited copy in the mods folder (Save Changes / Replace File) is what Modify uses: show and edit that one.
    const QString workspacePath = orchestrator->pathFromHash(hexHash);
    const QString modsCopy = workspacePath.isEmpty() ? QString() : orchestrator->modFilePath(workspacePath);
    QByteArray data;
    qint64 mainSize = 0;
    bool fromMods = false;
    if (!modsCopy.isEmpty() && QFileInfo(modsCopy).isFile()) {
        QFile file(modsCopy);
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
            mainSize = -1;   // A standalone TXPK: main section followed by the vram section.
            fromMods = true;
        }
    }
    if (!fromMods) {
        try {
            data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                            platform, &mainSize);
        } catch (const std::exception& ex) {
            reportError(QString::fromUtf8(ex.what()));
            return;
        }
    }

    if (loadFromBytes(data, mainSize, isBigEndian, u"0x"_s + hexHash.toUpper())) {
        m_source = CreatorSupport::fromBlobset(entry, orchestrator->blobsetVersion(), m_platform, u"txpk"_s);
        m_workspacePath = workspacePath;
        emit changesChanged();
        if (fromMods)
            setStatus(true, u"Showing your edited copy from the mods folder: %1"_s.arg(QDir::toNativeSeparators(modsCopy)));
    }
}

// ---------------------------------------------------------------------------
// Edit: replace textures, Save Changes
// ---------------------------------------------------------------------------

QString TxpkToolManager::modsTarget() const {
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    return orchestrator && !m_workspacePath.isEmpty() ? QDir::toNativeSeparators(orchestrator->modFilePath(m_workspacePath))
                                                      : QString();
}

bool TxpkToolManager::replaceEntry(int entry, const QString& file) {
    if (m_busy || entry < 0 || entry >= m_entries.size())
        return false;
    const QString name = QFileInfo(m_entries[entry].ddsPath).fileName();
    QFile source(file);
    if (QFileInfo(file).suffix().compare(u"dds"_s, Qt::CaseInsensitive) != 0 || !source.open(QIODevice::ReadOnly)
        || source.read(4) != "DDS ") {
        setStatus(false, u"%1 needs a .dds texture (%2 isn't one)."_s.arg(name, QFileInfo(file).fileName()));
        return false;
    }
    m_replacements.insert(entry, QFileInfo(file).absoluteFilePath());
    m_unsavedChanges = true;
    emit changesChanged();
    if (entry == m_previewEntry || entry == m_currentEntry) {
        releasePreview();
        refreshPreview();
    }
    setStatus(true, u"Replaced %1 with %2 - Save Changes to write the TXPK into the mods folder."_s
                        .arg(name, QFileInfo(file).fileName()));
    return true;
}

bool TxpkToolManager::replaceFileAt(int fileIndex, const QString& pathOrUrl) {
    return replaceEntry(entryForFile(fileIndex), toLocalPath(pathOrUrl));
}

int TxpkToolManager::replaceByName(const QStringList& pathsOrUrls) {
    int replaced = 0;
    QStringList unmatched;
    for (const QString& item : pathsOrUrls) {
        const QString file = toLocalPath(item);
        const QString name = QFileInfo(file).fileName();
        int entry = -1;
        for (int i = 0; i < m_currentFolderFiles.size() && entry < 0; ++i)
            if (m_currentFolderFiles[i].compare(name, Qt::CaseInsensitive) == 0)
                entry = entryForFile(i);
        if (entry < 0) {
            int matches = 0;
            for (int i = 0; i < m_entries.size(); ++i) {
                if (QFileInfo(m_entries[i].ddsPath).fileName().compare(name, Qt::CaseInsensitive) == 0) {
                    entry = i;
                    ++matches;
                }
            }
            if (matches != 1)
                entry = -1;   // Not there, or in more than one folder: drop it onto the texture instead.
        }
        if (entry < 0)
            unmatched << name;
        else if (replaceEntry(entry, file))
            ++replaced;
    }
    if (!unmatched.isEmpty())
        setStatus(false, u"No (single) texture called %1 - drop a file onto the texture it should replace."_s
                             .arg(unmatched.join(u", "_s)));
    else if (replaced > 1)
        setStatus(true, u"Replaced %1 textures - Save Changes to write the TXPK into the mods folder."_s.arg(replaced));
    return replaced;
}

bool TxpkToolManager::isReplacedAt(int fileIndex) const {
    return m_replacements.contains(entryForFile(fileIndex));
}

void TxpkToolManager::revertAt(int fileIndex) {
    const int entry = entryForFile(fileIndex);
    if (m_busy || !m_replacements.remove(entry))
        return;
    m_unsavedChanges = true;
    emit changesChanged();
    if (entry == m_previewEntry || entry == m_currentEntry) {
        releasePreview();
        refreshPreview();
    }
    setStatus(true, u"%1 is back to the TXPK's own texture."_s.arg(QFileInfo(m_entries[entry].ddsPath).fileName()));
}

void TxpkToolManager::discardChanges() {
    if (m_busy || m_replacements.isEmpty())
        return;
    m_replacements.clear();
    m_unsavedChanges = false;
    emit changesChanged();
    releasePreview();
    refreshPreview();
}

void TxpkToolManager::saveChanges() {
    if (m_busy || m_entries.isEmpty())
        return;
    const QString target = modsTarget();
    if (target.isEmpty()) {
        finish(false, u"Save Changes needs a TXPK opened from the game's files (double click it in the workspace)."_s);
        return;
    }
    if (m_replacements.isEmpty()) {
        finish(false, u"Nothing to save: replace a texture first (drag a .dds onto it, or right click > Replace)."_s);
        return;
    }

    const QString temp = QDir(QDir::tempPath()).filePath(
        u"BlobsetTools-TXPK-%1"_s.arg(QDateTime::currentMSecsSinceEpoch()));
    const QHash<int, QString> replacements = m_replacements;
    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Saving changes: extracting the TXPK..."_s);

    m_extractFuture = QtConcurrent::run([this, temp, target, replacements]() {
        QString error;
        const int count = static_cast<int>(m_entries.size());
        for (int i = 0; i < count && error.isEmpty(); ++i) {
            if (m_cancel)
                error = u"Cancelled."_s;
            else if (!replacements.contains(i))
                error = writeEntryTo(i, temp, false, false, true);
            if (i % 16 == 0)
                QMetaObject::invokeMethod(this, [this, i, count]() { setProgress(i * 40 / std::max(1, count)); },
                                          Qt::QueuedConnection);
        }
        if (error.isEmpty())
            error = writeCreatorList(temp);
        for (auto it = replacements.cbegin(); it != replacements.cend() && error.isEmpty(); ++it) {
            const QString destination = QDir(temp).filePath(outputPath(it.key(), false));
            QDir().mkpath(QFileInfo(destination).absolutePath());
            QFile::remove(destination);
            if (!QFile::copy(it.value(), destination))
                error = u"Could not copy %1."_s.arg(QDir::toNativeSeparators(it.value()));
        }

        QMetaObject::invokeMethod(this, [this, temp, target, error]() {
            setBusy(false);
            if (!error.isEmpty()) {
                QDir(temp).removeRecursively();
                finish(false, u"Save Changes: "_s + error);
                return;
            }
            m_editTemp = temp;
            m_savingChanges = true;
            createTxpk(QDir(temp).filePath(CreatorSupport::txpkListName()), temp, target);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Preview / info
// ---------------------------------------------------------------------------

QByteArray TxpkToolManager::ddsBytesFor(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};

    const TxpkEntry& entry = m_entries[index];
    if (entry.dataOffset < 0)
        return {};

    const QByteArray raw = m_data.mid(entry.dataOffset, entry.dataSize);
    if (!m_isBigEndian)
        return raw;

    try {
        return toConsoleDds(raw, entry.gcm, entry.isXbox360, entry.x360);
    } catch (const std::exception&) {
        return {};
    }
}

void TxpkToolManager::releasePreview() {
    DdsImageProvider::releaseImage(m_previewKey);
    m_previewKey.clear();
    m_previewEntry = -1;
}

QString TxpkToolManager::previewImageUrl(int index) {
    if (index < 0 || index >= m_entries.size())
        return {};

    // Decode once per texture; flip / alpha only change the URL, the provider applies them.
    if (index != m_previewEntry) {
        releasePreview();

        const QString name = m_entries[index].ddsPath;
        QByteArray dds;
        if (m_replacements.contains(index)) {
            QFile replacement(m_replacements.value(index));
            if (replacement.open(QIODevice::ReadOnly))
                dds = replacement.readAll();
        } else {
            dds = ddsBytesFor(index);
        }
        if (dds.isEmpty()) {
            finish(false, u"The texture data of %1 lies outside of the TXPK or uses an unsupported format."_s.arg(name));
            return {};
        }

        const QImage image = DdsProcessor::decodeDdsToQImage(dds);
        if (image.isNull()) {
            finish(false, u"Could not decode %1 (unsupported DDS format)."_s.arg(name));
            return {};
        }

        m_previewKey = DdsImageProvider::storeImage(image);
        m_previewEntry = index;
    }

    return u"image://dds_provider/"_s + DdsImageProvider::makeId(m_previewKey, 0, {m_flipped, m_alphaEnabled});
}

void TxpkToolManager::refreshPreview() {
    if (m_currentEntry < 0)
        return;
    m_previewUrl = previewImageUrl(m_currentEntry);
    emit currentFileChanged();
}

void TxpkToolManager::flipImage() {
    m_flipped = !m_flipped;
    emit viewChanged();
    refreshPreview();
}

void TxpkToolManager::toggleAlpha(bool enable) {
    if (m_alphaEnabled == enable)
        return;
    m_alphaEnabled = enable;
    emit viewChanged();
    refreshPreview();
}

QVariantMap TxpkToolManager::entryInfo(int index) const {
    QVariantMap info;
    if (index < 0 || index >= m_entries.size())
        return info;

    const TxpkEntry& entry = m_entries[index];
    quint32 width = entry.width;
    quint32 height = entry.height;
    quint32 mipMaps = entry.mipMaps;
    QString format;

    if (m_isBigEndian) {
        format = entry.isXbox360 ? X360::formatName(entry.x360.format()) : Gcm::formatName(entry.gcm.format);
    } else if (entry.dataSize >= 128) {
        DdsImageProvider::parsePcDdsHeader(m_data, entry.dataOffset, &width, &height, &mipMaps, &format);
    }

    info[u"path"_s] = entry.ddsPath;
    info[u"width"_s] = width;
    info[u"height"_s] = height;
    info[u"mipMaps"_s] = mipMaps;
    info[u"format"_s] = format;
    info[u"size"_s] = DdsImageProvider::humanSize(entry.dataSize);
    return info;
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

QString TxpkToolManager::outputPath(int index, bool asPng) const {
    const QString& path = m_entries[index].ddsPath;
    return asPng ? withExtension(path, u"png"_s) : path;
}

QString TxpkToolManager::writeEntryTo(int index, const QString& outDir, bool asPng, bool flipped,
                                      bool alphaEnabled) const {
    const QString relative = outputPath(index, asPng);

    const QByteArray dds = ddsBytesFor(index);
    if (dds.isEmpty())
        return u"%1: no usable texture data."_s.arg(relative);

    const QString root = QDir::cleanPath(QDir(outDir).absolutePath()) + u'/';
    const QString full = QDir::cleanPath(QDir(outDir).absoluteFilePath(relative));
    if (!full.startsWith(root, Qt::CaseInsensitive))
        return u"%1: refusing to write outside of the output folder."_s.arg(relative);

    if (!QDir().mkpath(QFileInfo(full).absolutePath()))
        return u"%1: could not create its folder."_s.arg(relative);

    if (asPng) {
        const QImage image = DdsImageProvider::applyViewOptions(DdsProcessor::decodeDdsToQImage(dds),
                                                                {flipped, alphaEnabled});
        if (image.isNull())
            return u"%1: could not decode the texture."_s.arg(relative);
        if (!image.save(full, "PNG"))
            return u"%1: could not write the file."_s.arg(relative);
        return {};
    }

    QSaveFile out(full);
    if (!out.open(QIODevice::WriteOnly) || out.write(dds) != dds.size() || !out.commit())
        return u"%1: could not write the file (%2)."_s.arg(relative, out.errorString());

    return {};
}

void TxpkToolManager::extractOne(int index, const QString& outDirPath, const QString& format) {
    if (m_busy || index < 0 || index >= m_entries.size())
        return;

    const bool asPng = isPngFormat(format);
    const QString outDir = toLocalPath(outDirPath);
    const QString error = QDir().mkpath(outDir) ? writeEntryTo(index, outDir, asPng, m_flipped, m_alphaEnabled)
                                                : u"Could not create the output folder: "_s + outDir;

    if (error.isEmpty())
        finish(true, u"Extracted: "_s + QDir::toNativeSeparators(QDir(outDir).absoluteFilePath(outputPath(index, asPng))));
    else
        finish(false, error);
}

void TxpkToolManager::extractSelected(const QString& outDirPath, const QString& format) {
    if (m_currentEntry < 0) {
        finish(false, u"Select a texture first."_s);
        return;
    }
    extractOne(m_currentEntry, outDirPath, format);
}

void TxpkToolManager::extractAll(const QString& outDirPath, const QString& format) {
    if (m_busy)
        return;

    const int total = static_cast<int>(m_entries.size());
    if (total == 0) {
        finish(false, u"There are no textures to extract."_s);
        return;
    }

    const QString outDir = toLocalPath(outDirPath);
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        finish(false, u"Could not create the output folder: "_s + outDir);
        return;
    }

    const bool asPng = isPngFormat(format);
    const bool flipped = m_flipped;          // Snapshot: the user may toggle while the worker runs.
    const bool alphaEnabled = m_alphaEnabled;
    const QString label = asPng ? u"PNG"_s : u"DDS"_s;

    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Extracting %1 texture(s) as %2..."_s.arg(total).arg(label));

    // Runs on a worker thread so the UI (and the progress bar) stays responsive. Loading is blocked while busy,
    // so m_entries / m_data are not modified underneath it.
    m_extractFuture = QtConcurrent::run([this, total, outDir, asPng, flipped, alphaEnabled, label]() {
        int failed = 0;
        QString firstError;
        int lastPercent = -1;

        for (int i = 0; i < total && !m_cancel; ++i) {
            const QString error = writeEntryTo(i, outDir, asPng, flipped, alphaEnabled);
            if (!error.isEmpty()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = error;
            }

            const int percent = static_cast<int>((static_cast<qint64>(i) + 1) * 100 / total);
            if (percent != lastPercent) {
                lastPercent = percent;
                const QString message = u"Extracting (%1/%2): %3"_s.arg(i + 1).arg(total).arg(outputPath(i, asPng));
                QMetaObject::invokeMethod(this, [this, percent, message]() {
                    reportProgress(percent, message);
                }, Qt::QueuedConnection);
            }
        }

        if (m_cancel)
            return;

        bool ok = (failed == 0);
        QString summary = ok ? u"Extracted %1 texture(s) as %2 to %3."_s.arg(total).arg(label, outDir)
                             : u"Extracted %1 of %2 texture(s). %3 failed - first error: %4"_s
                                   .arg(total - failed).arg(total).arg(failed).arg(firstError);

        // DDS: the list the TXPK Creator rebuilds this TXPK from (PNG can't be put back as it is).
        if (!asPng) {
            const QString listError = writeCreatorList(outDir);
            if (listError.isEmpty()) {
                summary += u" %1 written for the TXPK Creator."_s.arg(CreatorSupport::txpkListName());
            } else {
                ok = false;
                summary += u" "_s + listError;
            }
        }

        QMetaObject::invokeMethod(this, [this, ok, summary]() {
            if (ok)
                setProgress(100);
            setBusy(false);
            finish(ok, summary);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Status bar / progress
// ---------------------------------------------------------------------------

QString TxpkToolManager::appVersion() const {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QString() : u"v"_s + version;
}

void TxpkToolManager::setStatus(bool ok, const QString& message) {
    if (m_statusOk == ok && m_statusText == message)
        return;
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}

void TxpkToolManager::finish(bool ok, const QString& message) {
    setStatus(ok, message);
    emit processComplete(ok, message);
}

void TxpkToolManager::setProgress(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (m_progress == percent)
        return;
    m_progress = percent;
    emit progressChanged();
}

void TxpkToolManager::reportProgress(int percent, const QString& message) {
    setProgress(percent);
    setStatus(true, message);
}

// ---------------------------------------------------------------------------
// Folder tree / file list
// ---------------------------------------------------------------------------

void TxpkToolManager::setFilterText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_filterText == trimmed)
        return;

    m_filterText = trimmed;
    emit filterTextChanged();

    if (!m_entries.isEmpty())
        rebuildTree();
}

void TxpkToolManager::resetBrowser() {
    releasePreview();
    m_pathToEntry.clear();
    m_visibleCount = 0;
    m_currentFolderFiles.clear();
    m_folderEntries.clear();
    m_maxFileItemWidth = 180;
    m_currentFileIndex = -1;
    m_currentEntry = -1;
    m_previewUrl.clear();
    m_infoLine.clear();

    m_treeModel->clear();

    emit currentFolderFilesChanged();
    emit currentFileChanged();
    emit treeRebuilt();
}

// Keeps only the entries that match the filter and (re)builds the folder tree out of them.
void TxpkToolManager::rebuildTree() {
    releasePreview();
    m_pathToEntry.clear();
    m_currentFolderFiles.clear();
    m_folderEntries.clear();
    m_maxFileItemWidth = 180;
    m_currentFileIndex = -1;
    m_currentEntry = -1;
    m_previewUrl.clear();
    m_infoLine.clear();

    QList<QString> paths;
    paths.reserve(m_entries.size());

    for (int i = 0; i < m_entries.size(); ++i) {
        const QString& path = m_entries[i].ddsPath;
        if (!m_filterText.isEmpty() && !path.contains(m_filterText, Qt::CaseInsensitive))
            continue;

        const QString key = m_rootName + u'/' + path.split(u'/', Qt::SkipEmptyParts).join(u'/');
        if (m_pathToEntry.contains(key))
            continue; // Duplicate path inside the TXPK: the first one wins.

        m_pathToEntry.insert(key, i);
        paths.append(key);
    }

    m_visibleCount = static_cast<int>(paths.size());
    m_treeModel->buildTree(paths);

    emit currentFolderFilesChanged();
    emit currentFileChanged();
    emit treeRebuilt();
}

QModelIndex TxpkToolManager::rootFolderIndex() const {
    return m_treeModel->rowCount() > 0 ? m_treeModel->index(0, 0) : QModelIndex();
}

void TxpkToolManager::selectFolder(const QModelIndex& index) {
    QStringList names;
    QList<int> entries;
    int longestCharCount = 0;

    TreeNode* node = index.isValid() ? static_cast<TreeNode*>(index.internalPointer()) : nullptr;
    if (node && node->isFolder) {
        const QString folderPath = m_treeModel->getPathFromIndex(index);
        for (const auto& child : node->children) {
            if (child->isFolder)
                continue;

            const int entry = m_pathToEntry.value(folderPath + u'/' + child->name, -1);
            if (entry < 0)
                continue;

            names.append(child->name);
            entries.append(entry);
            longestCharCount = std::max<int>(longestCharCount, static_cast<int>(child->name.length()));
        }
    }

    m_currentFolderFiles = names;
    m_folderEntries = entries;
    m_maxFileItemWidth = std::max(180, static_cast<int>(longestCharCount * 6.8) + 36); // Same sizing as MainForm.

    emit currentFolderFilesChanged();
    selectFile(-1);
}

// Same formatting as the DDS viewer's info strip.
QString TxpkToolManager::buildInfoLine(const QVariantMap& info) {
    return DdsImageProvider::formatInfoLine(info);
}

void TxpkToolManager::selectFile(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_folderEntries.size()) {
        if (m_currentFileIndex == -1 && m_currentEntry == -1 && m_previewUrl.isEmpty() && m_infoLine.isEmpty())
            return;
        releasePreview();
        m_currentFileIndex = -1;
        m_currentEntry = -1;
        m_previewUrl.clear();
        m_infoLine.clear();
        emit currentFileChanged();
        return;
    }

    m_currentFileIndex = fileIndex;
    m_currentEntry = m_folderEntries[fileIndex];
    m_previewUrl = previewImageUrl(m_currentEntry);     // Reports a decode problem through the status bar itself.
    m_infoLine = buildInfoLine(entryInfo(m_currentEntry));

    if (!m_previewUrl.isEmpty())
        setStatus(true, m_infoLine);

    emit currentFileChanged();
}

bool TxpkToolManager::handleFileKey(int qtKey, int rowsPerColumn) {
    if (qtKey != Qt::Key_Down && qtKey != Qt::Key_Up && qtKey != Qt::Key_Left && qtKey != Qt::Key_Right)
        return false;

    const int total = static_cast<int>(m_currentFolderFiles.size());
    if (total == 0)
        return false;

    const int perColumn = std::max(1, rowsPerColumn);
    const int current = m_currentFileIndex;
    int next = current;

    if (current < 0) {
        next = 0;
    } else if (qtKey == Qt::Key_Down) {
        next = std::min(current + 1, total - 1);
    } else if (qtKey == Qt::Key_Up) {
        next = std::max(current - 1, 0);
    } else if (qtKey == Qt::Key_Right) {
        next = (current + perColumn < total) ? current + perColumn : total - 1;
    } else if (qtKey == Qt::Key_Left) {
        next = (current - perColumn >= 0) ? current - perColumn : current;
    }

    if (next != current)
        selectFile(next);
    return true;
}

// ---------------------------------------------------------------------------
// TXPK Creator
// ---------------------------------------------------------------------------

QString TxpkToolManager::writeCreatorList(const QString& outDir) const {
    XmlExtractFileInfo list;
    list.type = u"TXPK"_s;
    list.index = m_source.index >= 0 ? static_cast<quint32>(m_source.index) : 0;
    list.folderHash = m_source.folderHash;
    list.fileNameHash = m_source.fileHash;
    list.isCompressed = m_source.isCompressed;
    list.platform = CreatorSupport::platformName(m_platform);
    list.version = m_source.version;
    list.outputName = m_source.outputName.isEmpty() ? m_sourceName + u".txpk"_s : m_source.outputName;
    list.recordAlignment = m_recordAlignment;
    list.dataAlignment = m_dataAlignment;

    for (int i = 0; i < m_entries.size(); ++i) {
        ExtractFileEntry entry;
        entry.filePath = m_entries[i].storedPath;
        entry.file = outputPath(i, false);
        entry.imageType = m_entries[i].imageType;
        list.entries.append(entry);
    }

    const QString path = QDir(outDir).filePath(CreatorSupport::txpkListName());
    if (!list.serialize(path))
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(path));
    return {};
}

QVariantMap TxpkToolManager::inspectCreatorList(const QString& xmlPath, const QString& sourceDir) const {
    return CreatorSupport::inspectList(xmlPath, sourceDir, u"TXPK"_s);
}

namespace {

struct BuiltTexture {
    QByteArray record;          // Info record (PS3: with its RSX descriptor).
    QByteArray data;            // vram data: PC the whole .dds, PS3 the converted pixels.
    quint32 consoleHeader = 0;  // Record offset of the RSX descriptor (PS3).
    quint32 bufferSize = 0;     // Zero padding between the path and the descriptor (PS3).
    Gcm::CellGcmTexture gcm;
    X360::TextureHeader x360;   // Xbox 360 descriptor.
    quint32 width = 0;
    quint32 height = 0;
    quint32 imageType = 1;
    QString storedPath;
};

void put32(QByteArray& out, qint64 at, quint32 value, bool bigEndian) {
    uchar* p = reinterpret_cast<uchar*>(out.data()) + at;
    if (bigEndian)
        qToBigEndian(value, p);
    else
        qToLittleEndian(value, p);
}

} // namespace

bool TxpkToolManager::buildTxpk(const XmlExtractFileInfo& list, const QString& sourceDir, int fallbackPlatform,
                               BuiltTxpk& result, const std::function<void(int, const QString&)>& progress,
                               const std::function<void(const QString&, bool)>& log, const std::atomic<bool>* cancel) {
    const int platform = CreatorSupport::platformFromName(list.platform, fallbackPlatform);
    const bool ps3 = platform == static_cast<int>(CoreDefs::Platforms::PS3);
    const bool x360 = platform == static_cast<int>(CoreDefs::Platforms::Xbox360);
    const bool console = ps3 || x360;
    const bool bigEndian = console;
    const quint32 recordAlignment = list.recordAlignment > 0 ? list.recordAlignment : (console ? 16u : 1u);
    // Xbox 360 texture data starts on 4 KB boundaries (its mip addresses are 4 KB units).
    const quint32 dataAlignment = std::max<quint32>(list.dataAlignment, x360 ? 4096u : 128u);
    const int count = static_cast<int>(list.entries.size());

    if (count == 0) {
        // An empty TXPK is just its 8 byte header (the games have plenty of those).
        result.mainPart = QByteArray("KPXT", 4) + QByteArray(4, '\0');
        result.vram.clear();
        result.entries.clear();
        result.platform = platform;
        return true;
    }

    log(u"%1 texture(s), %2, records aligned to %3, textures aligned to %4 bytes."_s
            .arg(count).arg(CreatorSupport::platformName(platform)).arg(recordAlignment).arg(dataAlignment), true);

    // 1. Every texture: its info record and its vram data.
    QList<BuiltTexture> textures;
    textures.reserve(count);
    for (int i = 0; i < count && !(cancel && *cancel); ++i) {
        const ExtractFileEntry& item = list.entries[i];
        const QString file = CreatorSupport::resolveListFile(sourceDir, item.file, item.filePath);
        const QString shown = item.file.isEmpty() ? item.filePath : item.file;
        if (file.isEmpty())
            fail(u"%1 is missing from %2."_s.arg(shown, QDir::toNativeSeparators(sourceDir)));

        QFile in(file);
        if (!in.open(QIODevice::ReadOnly))
            fail(u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(file), in.errorString()));
        const QByteArray dds = in.readAll();
        if (dds.size() < 128 || !dds.startsWith("DDS "))
            fail(u"%1 is not a valid DDS file."_s.arg(shown));

        BuiltTexture texture;
        texture.storedPath = item.filePath;
        texture.imageType = item.imageType;
        texture.height = qFromLittleEndian<quint32>(dds.constData() + 12);
        texture.width = qFromLittleEndian<quint32>(dds.constData() + 16);
        texture.data = dds;

        if (ps3) {
            QString error;
            const std::optional<Gcm::ImportedTexture> imported = Gcm::fromDds(dds, &error);
            if (!imported)
                fail(u"%1: %2"_s.arg(shown, error));
            if (!imported->note.isEmpty())
                log(u"%1: %2"_s.arg(shown, imported->note), true);
            texture.gcm = imported->texture;
            texture.data = imported->pixels;
        } else if (x360) {
            QString error;
            const std::optional<X360::ImportedTexture> imported = X360::fromDds(dds, &error);
            if (!imported)
                fail(u"%1: %2"_s.arg(shown, error));
            texture.x360 = imported->header;
            texture.data = imported->data;
        }

        const QByteArray pathBytes = item.filePath.toUtf8() + QByteArray(1, '\0');
        const quint32 unpadded = 20 + static_cast<quint32>(pathBytes.size());
        texture.consoleHeader = console ? (unpadded + 15u) & ~15u : 0;
        texture.bufferSize = console ? texture.consoleHeader - unpadded : 0;

        const qint64 descriptorSize = x360 ? X360::TextureHeader::kSize : Gcm::CellGcmTexture::kSize;
        texture.record = QByteArray(console ? texture.consoleHeader + descriptorSize : unpadded, '\0');
        put32(texture.record, 0, texture.width, bigEndian);
        put32(texture.record, 4, texture.height, bigEndian);
        put32(texture.record, 8, texture.imageType, bigEndian);
        put32(texture.record, 12, 20, bigEndian);                    // Path offset inside the record.
        // PC: the size of the texture data. Consoles: where the texture descriptor starts (written below).
        put32(texture.record, 16, console ? texture.consoleHeader : static_cast<quint32>(texture.data.size()), bigEndian);
        std::memcpy(texture.record.data() + 20, pathBytes.constData(), pathBytes.size());

        textures.append(texture);
        progress(static_cast<int>((i + 1) * 80 / count), u"Reading (%1/%2): %3"_s.arg(i + 1).arg(count).arg(shown));
    }
    if (cancel && *cancel)
        return false;

    // 2. Layout: header, info records, then the vram section with the texture data.
    const qint64 headerSize = 8 + 16LL * count;
    QList<qint64> recordOffsets, dataOffsets;
    qint64 position = CreatorSupport::alignUp(headerSize, recordAlignment);
    for (const BuiltTexture& texture : std::as_const(textures)) {
        recordOffsets.append(position);
        position = CreatorSupport::alignUp(position + texture.record.size(), recordAlignment);
    }
    const qint64 mainSize = recordOffsets.last() + textures.last().record.size();

    qint64 vramPosition = 0;
    for (const BuiltTexture& texture : std::as_const(textures)) {
        vramPosition = CreatorSupport::alignUp(vramPosition, dataAlignment);
        dataOffsets.append(vramPosition);
        vramPosition += texture.data.size();
    }
    const qint64 vramSize = vramPosition;
    if (mainSize + vramSize > std::numeric_limits<quint32>::max())
        fail(u"The new TXPK would be larger than 4 GB."_s);

    QByteArray mainPart(mainSize, '\0');
    std::memcpy(mainPart.data(), "KPXT", 4);   // Read little endian TXPK, big endian KPXT: the bytes are the same.
    put32(mainPart, 4, static_cast<quint32>(count), bigEndian);
    for (int i = 0; i < count; ++i) {
        BuiltTexture& texture = textures[i];
        if (ps3) {
            // The RSX descriptor: points at the pixels inside the vram section.
            texture.gcm.offset = static_cast<quint32>(dataOffsets[i]);
            QByteArray descriptor;
            QBuffer buffer(&descriptor);
            buffer.open(QIODevice::WriteOnly);
            {
                PackageStream out(&buffer, PackageStream::Big);
                texture.gcm.write(out);
            }
            std::memcpy(texture.record.data() + texture.consoleHeader, descriptor.constData(),
                        std::min<qsizetype>(descriptor.size(), Gcm::CellGcmTexture::kSize));
        } else if (x360) {
            // Its base / mip addresses are relative to the texture's own data, so it's stored as built.
            const QByteArray descriptor = texture.x360.toBytes();
            std::memcpy(texture.record.data() + texture.consoleHeader, descriptor.constData(), descriptor.size());
        }

        put32(mainPart, 8 + 16 * i, static_cast<quint32>(recordOffsets[i]), bigEndian);
        put32(mainPart, 12 + 16 * i, static_cast<quint32>(texture.record.size()), bigEndian);
        put32(mainPart, 16 + 16 * i, static_cast<quint32>(dataOffsets[i]), bigEndian);
        put32(mainPart, 20 + 16 * i, static_cast<quint32>(texture.data.size()), bigEndian);
        std::memcpy(mainPart.data() + recordOffsets[i], texture.record.constData(), texture.record.size());
    }


    // The vram section: every texture at its (aligned) offset.
    QByteArray vram(vramSize, '\0');
    for (int i = 0; i < count; ++i)
        std::memcpy(vram.data() + dataOffsets[i], textures[i].data.constData(), textures[i].data.size());

    result.mainPart = mainPart;
    result.vram = vram;
    result.platform = platform;
    result.entries.clear();
    for (const BuiltTexture& texture : std::as_const(textures)) {
        ModifyFileEntry entry;
        entry.filePath = texture.storedPath;
        entry.bufferSize = texture.bufferSize;
        entry.ddsType = ps3 ? texture.gcm.format : x360 ? static_cast<quint8>(texture.x360.format()) : 0;
        entry.ddsMipMaps = ps3 ? texture.gcm.mipmap : x360 ? static_cast<quint8>(texture.x360.levelCount()) : 0;
        entry.ddsWidth = static_cast<quint16>(texture.width);
        entry.ddsHeight = static_cast<quint16>(texture.height);
        entry.ddsImageType = static_cast<quint16>(texture.imageType);
        result.entries.append(entry);
    }
    return true;
}

void TxpkToolManager::createTxpk(const QString& xmlPathIn, const QString& sourceDirIn, const QString& outTxpkPathIn) {
    if (m_busy)
        return;

    const QString xmlPath = toLocalPath(xmlPathIn);
    const QString outPath = toLocalPath(outTxpkPathIn);
    const QVariantMap inspected = inspectCreatorList(xmlPath, sourceDirIn);
    if (!inspected.value(u"ok"_s).toBool()) {
        finish(false, inspected.value(u"error"_s).toString());
        return;
    }
    const QString sourceDir = inspected.value(u"sourceDir"_s).toString();
    if (outPath.trimmed().isEmpty()) {
        finish(false, u"Choose where to save the new TXPK."_s);
        return;
    }

    XmlExtractFileInfo list;
    list.deserialize(xmlPath);

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const int fallbackPlatform = orchestrator ? orchestrator->platform() : static_cast<int>(CoreDefs::Platforms::Windows);

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
            TxpkToolManager::BuiltTxpk built;
            if (!buildTxpk(list, sourceDir, fallbackPlatform, built, progress, log, &m_cancel))
                return;
            const qint64 mainSize = built.mainPart.size();
            const qint64 vramSize = built.vram.size();
            const int count = static_cast<int>(built.entries.size());

            // 3. <name>.txpk = main + vram.
            progress(85, u"Writing %1..."_s.arg(QFileInfo(outPath).fileName()));
            if (!QDir().mkpath(QFileInfo(outPath).absolutePath()))
                fail(u"Could not create the folder of %1."_s.arg(QDir::toNativeSeparators(outPath)));
            QSaveFile out(outPath);
            if (!out.open(QIODevice::WriteOnly))
                fail(u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(outPath), out.errorString()));
            out.write(built.mainPart);
            out.write(built.vram);
            if (!out.commit())
                fail(u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(outPath), out.errorString()));

            // 4. <name>.xml: what Modify / Create need to put it in the blobset.
            XmlModifyFileInfo info;
            info.index = list.index;
            info.folderHash = list.folderHash;
            info.fileNameHash = list.fileNameHash;
            info.isCompressed = list.isCompressed;
            info.mainCompressedSize = info.mainUnCompressedSize = static_cast<quint32>(mainSize);
            info.vramCompressedSize = info.vramUnCompressedSize = static_cast<quint32>(vramSize);
            info.entries = built.entries;
            const QFileInfo outInfo(outPath);
            const QString xmlOut = outInfo.absolutePath() + u'/' + outInfo.completeBaseName() + u".xml"_s;
            if (!info.serialize(xmlOut))
                fail(u"Could not write %1."_s.arg(QDir::toNativeSeparators(xmlOut)));

            log(u"Wrote %1 (main %2, vram %3)."_s.arg(QDir::toNativeSeparators(outPath),
                                                      DdsImageProvider::humanSize(mainSize),
                                                      DdsImageProvider::humanSize(vramSize)), true);
            log(u"Wrote %1 for Modify / Create."_s.arg(QDir::toNativeSeparators(xmlOut)), true);
            if (list.folderHash.isEmpty() && list.fileNameHash.isEmpty() && list.index == 0)
                log(u"The list doesn't say which blobset entry this TXPK replaces: set Index, FolderHash and "
                    u"FileNameHash in %1 before using it with Modify."_s.arg(outInfo.completeBaseName() + u".xml"_s), false);

            summary = u"Created %1 with %2 texture(s)."_s.arg(outInfo.fileName()).arg(count);
        } catch (const std::exception& ex) {
            ok = false;
            summary = QString::fromUtf8(ex.what());
        }

        QMetaObject::invokeMethod(this, [this, ok, summary]() {
            if (ok)
                setProgress(100);
            setBusy(false);
            finish(ok, summary);   // The creator page logs processComplete() too.
        }, Qt::QueuedConnection);
    });
}
