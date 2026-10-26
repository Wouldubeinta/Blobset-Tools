#include "m3mp_tool_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "compression_io.h"
#include "core_defs.h"
#include "dds_image_provider.h"
#include "dds_processor.h"
#include "m3mp_file.h"
#include "package_stream.h"
#include "utilities.h"
#include "xml_models.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

constexpr quint32 kM3mpMagic = 1347236685;      // "M3MP" (bytes "PM3M" on consoles)
constexpr quint32 kZstdFrameMagic = 0xFD2FB528u;
constexpr qsizetype kTextPreviewLimit = 256 * 1024;
constexpr qsizetype kHexPreviewLimit = 16 * 1024;

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

// LZMA properties V3 chunks leave out: lc 3, lp 0, pb 2, 2 MB dictionary (same as CompressionIO's V3 sections).
const QByteArray kV3LzmaProps = QByteArray::fromHex("5d00002000");

// One chunk of the M3MP payload: stored, ZSTD (V4), LZMA without properties (V3, Cricket 19),
// or LZMA with its 5 byte properties first (V1 / V2).
QByteArray decompressChunk(const QByteArray& chunk, quint32 uncompressedSize, int version, int index) {
    if (static_cast<quint32>(chunk.size()) == uncompressedSize) {
        // Stored, unless it's V1 / V2 LZMA (properties first) that came out as big as its data (AFL Live).
        if (version == static_cast<int>(CoreDefs::BlobsetVersion::V1) || version == static_cast<int>(CoreDefs::BlobsetVersion::V2))
            return CompressionIO::decodeV1Chunk(chunk, uncompressedSize);
        return chunk;
    }

    QByteArray out;
    const bool zstdFrame = chunk.size() >= 4 && qFromLittleEndian<quint32>(chunk.constData()) == kZstdFrameMagic;
    if (zstdFrame || version == static_cast<int>(CoreDefs::BlobsetVersion::V4))
        out = CompressionIO::decompressZstd(chunk, true);
    else if (version == static_cast<int>(CoreDefs::BlobsetVersion::V3))
        out = CompressionIO::decompressLzma(kV3LzmaProps + chunk, uncompressedSize);
    else
        out = CompressionIO::decompressLzma(chunk, uncompressedSize);

    if (static_cast<quint32>(out.size()) != uncompressedSize)
        fail(u"M3MP chunk %1 could not be decompressed (%2 of %3 bytes)."_s.arg(index).arg(out.size()).arg(uncompressedSize));
    return out;
}

// Mostly printable (UTF-8 allowed), no NUL bytes: shown as text.
bool looksLikeText(const QByteArray& data) {
    const qsizetype n = std::min<qsizetype>(data.size(), 4096);
    if (n == 0)
        return false;
    qsizetype printable = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const uchar c = static_cast<uchar>(data[i]);
        if (c == 0)
            return false;
        if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t')
            ++printable;
    }
    return printable * 100 >= n * 95;
}

QString hexDump(const QByteArray& data, qsizetype limit) {
    const qsizetype n = std::min(data.size(), limit);
    QString out;
    out.reserve(n * 4 + 64);
    for (qsizetype row = 0; row < n; row += 16) {
        out += u"%1  "_s.arg(static_cast<qlonglong>(row), 8, 16, QLatin1Char('0')).toUpper();
        QString ascii;
        for (qsizetype i = 0; i < 16; ++i) {
            if (row + i < n) {
                const uchar c = static_cast<uchar>(data[row + i]);
                out += u"%1 "_s.arg(static_cast<uint>(c), 2, 16, QLatin1Char('0')).toUpper();
                ascii += (c >= 0x20 && c < 0x7F) ? QChar(static_cast<char16_t>(c)) : QChar(u'.');
            } else {
                out += u"   "_s;
            }
            if (i == 7)
                out += u' ';
        }
        out += u" "_s + ascii + u'\n';
    }
    if (data.size() > limit)
        out += u"\n... first %1 of %2 bytes shown. Extract the file to see all of it."_s
                   .arg(DdsImageProvider::humanSize(limit), DdsImageProvider::humanSize(data.size()));
    return out;
}

} // namespace

// ==========================================
// M3mpToolManager Implementation
// ==========================================
M3mpToolManager::M3mpToolManager(QObject* parent) : QObject(parent) {
    m_treeModel = new TreeModel(this);
}

M3mpToolManager::~M3mpToolManager() {
    m_cancel = true;
    m_extractFuture.waitForFinished(); // The worker thread still reads our members.
    DdsImageProvider::releaseImage(m_previewKey);
}

QString M3mpToolManager::appVersion() const {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QString() : u"v"_s + version;
}

void M3mpToolManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void M3mpToolManager::setStatus(bool ok, const QString& message) {
    if (m_statusOk == ok && m_statusText == message)
        return;
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}

void M3mpToolManager::finish(bool ok, const QString& message) {
    setStatus(ok, message);
    emit processComplete(ok, message);
}

void M3mpToolManager::setProgress(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (m_progress == percent)
        return;
    m_progress = percent;
    emit progressChanged();
}

void M3mpToolManager::clearEntries() {
    m_entries.clear();
    m_payload.clear();
    m_sourceName.clear();
    m_source = {};
    resetBrowser();
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool M3mpToolManager::loadFromBytes(const QByteArray& data, bool isBigEndian, int version, const QString& sourceName) {
    clearEntries();
    m_sourceName = sourceName;
    m_rootName = QString(sourceName).replace(u'/', u'_').replace(u'\\', u'_');
    if (m_rootName.trimmed().isEmpty())
        m_rootName = u"M3MP"_s;
    setProgress(0);

    try {
        if (data.size() < 16)
            fail(u"The file is too small to be an M3MP."_s);

        const uchar* raw = reinterpret_cast<const uchar*>(data.constData());
        const auto u32 = [&](int at) { return isBigEndian ? qFromBigEndian<quint32>(raw + at) : qFromLittleEndian<quint32>(raw + at); };
        if (u32(0) != kM3mpMagic)
            fail(u"Invalid M3MP magic number. Is this an M3MP, and does its platform (endianness) match the selected game?"_s);
        if (static_cast<qint64>(u32(4)) * 17 + 16 > data.size() || static_cast<qint64>(u32(8)) * 12 + 16 > data.size())
            fail(u"Invalid M3MP header (bad file or chunk count)."_s);

        QByteArray copy = data;
        QBuffer buffer(&copy);
        buffer.open(QIODevice::ReadOnly);
        PackageStream in(&buffer, isBigEndian ? PackageStream::Big : PackageStream::Little);

        M3MP m3mp;
        m3mp.deserialize(in, isBigEndian);

        const auto& chunks = m3mp.getCompressedEntries();
        if (chunks.isEmpty()) {
            // Nothing chunked: the files follow the tables.
            m_payload = data.mid(in.position());
        } else {
            for (int i = 0; i < chunks.size(); ++i) {
                const auto& c = chunks[i].info;
                if (static_cast<qint64>(c.offset) + c.compressedSize > data.size())
                    fail(u"M3MP chunk %1 lies outside of the file."_s.arg(i));
                m_payload += decompressChunk(data.mid(c.offset, c.compressedSize), c.uncompressedSize, version, i);
            }
        }

        int bad = 0;
        for (const M3MP::UnCompressedEntry& e : m3mp.getUnCompressedEntries()) {
            Entry entry;
            entry.offset = e.info.offset;
            entry.size = e.info.size;
            entry.valid = entry.offset + entry.size <= m_payload.size();
            if (!entry.valid)
                ++bad;

            QString path = e.filePath;
            path.replace(u'\\', u'/');
            while (path.startsWith(u'/'))
                path.remove(0, 1);
            entry.path = path.isEmpty() ? u"file_%1"_s.arg(m_entries.size()) : path;
            entry.storedPath = e.filePath.isEmpty() ? entry.path : e.filePath;
            m_entries.append(entry);
        }

        rebuildTree();
        emit fileListChanged();
        if (m_entries.isEmpty())
            finish(true, u"M3MP %1 is empty (0 files)."_s.arg(sourceName));
        else if (bad > 0)
            finish(false, u"Loaded %1 file(s) from %2, but %3 lie outside of the M3MP data."_s
                              .arg(m_entries.size()).arg(sourceName).arg(bad));
        else
            finish(true, u"Loaded %1 file(s) from %2 (%3)."_s
                             .arg(m_entries.size()).arg(sourceName, DdsImageProvider::humanSize(m_payload.size())));
        return true;
    } catch (const std::exception& ex) {
        clearEntries();
        emit fileListChanged();
        finish(false, QString::fromUtf8(ex.what()));
        return false;
    }
}

void M3mpToolManager::loadM3mp(const QString& m3mpPath, bool isBigEndian) {
    if (m_busy)
        return;

    const QString path = toLocalPath(m3mpPath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        clearEntries();
        emit fileListChanged();
        finish(false, u"Failed to read M3MP file: "_s + path);
        return;
    }

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const int version = orchestrator ? orchestrator->blobsetVersion() : static_cast<int>(CoreDefs::BlobsetVersion::V4);
    if (loadFromBytes(file.readAll(), isBigEndian, version, QFileInfo(path).fileName())) {
        int platform = static_cast<int>(CoreDefs::Platforms::Windows);
        if (isBigEndian)
            platform = orchestrator && orchestrator->platform() == static_cast<int>(CoreDefs::Platforms::Xbox360)
                           ? static_cast<int>(CoreDefs::Platforms::Xbox360) : static_cast<int>(CoreDefs::Platforms::PS3);
        m_source = CreatorSupport::fromFile(path, version, platform);
    }
}

void M3mpToolManager::loadM3mpFromBlobset(const QString& hexHash) {
    if (m_busy)
        return;

    const auto reportError = [this](const QString& message) {
        clearEntries();
        emit fileListChanged();
        finish(false, message);
    };

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        reportError(u"This M3MP (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return;
    }

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        reportError(u"The application orchestrator is unavailable."_s);
        return;
    }

    const int platform = orchestrator->platform();
    const int version = orchestrator->blobsetVersion();
    QByteArray data;
    try {
        data = BlobsetAssetReader::read(entry, orchestrator->gameLocation(), version, platform);
    } catch (const std::exception& ex) {
        reportError(QString::fromUtf8(ex.what()));
        return;
    }

    if (loadFromBytes(data, platform != static_cast<int>(CoreDefs::Platforms::Windows), version, u"0x"_s + hexHash.toUpper()))
        m_source = CreatorSupport::fromBlobset(entry, version, platform, u"m3mp"_s);
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

QByteArray M3mpToolManager::entryBytes(int index) const {
    if (index < 0 || index >= m_entries.size() || !m_entries[index].valid)
        return {};
    return m_payload.mid(m_entries[index].offset, m_entries[index].size);
}

void M3mpToolManager::clearPreview() {
    DdsImageProvider::releaseImage(m_previewKey);
    m_previewKey.clear();
    m_previewEntry = -1;
    m_previewKind.clear();
    m_previewUrl.clear();
    m_previewText.clear();
    m_infoLine.clear();
}

void M3mpToolManager::loadPreview(int index) {
    clearPreview();
    if (index < 0 || index >= m_entries.size())
        return;

    const Entry& entry = m_entries[index];
    m_previewEntry = index;
    const QString size = DdsImageProvider::humanSize(entry.size);

    if (!entry.valid) {
        m_previewKind = u"text"_s;
        m_previewText = u"This file's data lies outside of the M3MP."_s;
        m_infoLine = u"%1  |  invalid  |  %2"_s.arg(entry.path, size);
        setStatus(false, m_previewText + u" "_s + entry.path);
        return;
    }

    const QByteArray data = entryBytes(index);

    // Images: DDS (any DirectXTex format), PNG, JPEG, BMP.
    QImage image;
    QVariantMap info;
    if (data.startsWith("DDS ")) {
        image = DdsProcessor::decodeDdsToQImage(data);
        quint32 w = 0, h = 0, mips = 1;
        QString format;
        DdsImageProvider::parsePcDdsHeader(data, 0, &w, &h, &mips, &format);
        info = {{u"path"_s, entry.path}, {u"width"_s, w}, {u"height"_s, h}, {u"mipMaps"_s, mips},
                {u"format"_s, format}, {u"size"_s, size}};
    } else if (data.startsWith("\x89PNG") || data.startsWith("\xFF\xD8\xFF") || data.startsWith("BM")) {
        image = QImage::fromData(data);
        const char* format = data.startsWith("\x89PNG") ? "PNG" : (data.startsWith("BM") ? "BMP" : "JPEG");
        info = {{u"path"_s, entry.path}, {u"width"_s, image.width()}, {u"height"_s, image.height()},
                {u"mipMaps"_s, 1}, {u"format"_s, QString::fromLatin1(format)}, {u"size"_s, size}};
    }

    if (!image.isNull()) {
        m_previewKey = DdsImageProvider::storeImage(image);
        m_previewKind = u"image"_s;
        m_previewUrl = u"image://dds_provider/"_s + DdsImageProvider::makeId(m_previewKey, 0, {m_flipped, m_alphaEnabled});
        m_infoLine = DdsImageProvider::formatInfoLine(info);
    } else if (looksLikeText(data)) {
        m_previewKind = u"text"_s;
        m_previewText = QString::fromUtf8(data.left(kTextPreviewLimit));
        if (data.size() > kTextPreviewLimit)
            m_previewText += u"\n\n... first %1 of %2 shown. Extract the file to see all of it."_s
                                 .arg(DdsImageProvider::humanSize(kTextPreviewLimit), size);
        m_infoLine = u"%1  |  Text  |  %2"_s.arg(entry.path, size);
    } else {
        m_previewKind = u"hex"_s;
        m_previewText = data.isEmpty() ? u"(empty file)"_s : hexDump(data, kHexPreviewLimit);
        m_infoLine = u"%1  |  Binary  |  %2"_s.arg(entry.path, size);
    }
    setStatus(true, m_infoLine);
}

void M3mpToolManager::flipImage() {
    m_flipped = !m_flipped;
    emit viewChanged();
    if (m_previewKind == u"image"_s) {
        m_previewUrl = u"image://dds_provider/"_s + DdsImageProvider::makeId(m_previewKey, 0, {m_flipped, m_alphaEnabled});
        emit currentFileChanged();
    }
}

void M3mpToolManager::toggleAlpha(bool enable) {
    if (m_alphaEnabled == enable)
        return;
    m_alphaEnabled = enable;
    emit viewChanged();
    if (m_previewKind == u"image"_s) {
        m_previewUrl = u"image://dds_provider/"_s + DdsImageProvider::makeId(m_previewKey, 0, {m_flipped, m_alphaEnabled});
        emit currentFileChanged();
    }
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

QString M3mpToolManager::writeEntryTo(int index, const QString& outDir) const {
    const Entry& entry = m_entries[index];
    if (!entry.valid)
        return u"%1: its data lies outside of the M3MP."_s.arg(entry.path);

    const QString root = QDir::cleanPath(QDir(outDir).absolutePath()) + u'/';
    const QString full = QDir::cleanPath(QDir(outDir).absoluteFilePath(entry.path));
    if (!full.startsWith(root, Qt::CaseInsensitive))
        return u"%1: refusing to write outside of the output folder."_s.arg(entry.path);

    if (!QDir().mkpath(QFileInfo(full).absolutePath()))
        return u"%1: could not create its folder."_s.arg(entry.path);

    const QByteArray data = entryBytes(index);
    QSaveFile out(full);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        return u"%1: could not write the file (%2)."_s.arg(entry.path, out.errorString());
    return {};
}

void M3mpToolManager::extractSelected(const QString& outDirPath) {
    if (m_busy)
        return;
    if (m_currentEntry < 0) {
        finish(false, u"Select a file first."_s);
        return;
    }

    const QString outDir = toLocalPath(outDirPath);
    const QString error = QDir().mkpath(outDir) ? writeEntryTo(m_currentEntry, outDir)
                                                : u"Could not create the output folder: "_s + outDir;
    if (error.isEmpty())
        finish(true, u"Extracted: "_s + QDir::toNativeSeparators(QDir(outDir).absoluteFilePath(m_entries[m_currentEntry].path)));
    else
        finish(false, error);
}

void M3mpToolManager::extractAll(const QString& outDirPath) {
    if (m_busy)
        return;

    const int total = static_cast<int>(m_entries.size());
    if (total == 0) {
        finish(false, u"There are no files to extract."_s);
        return;
    }

    const QString outDir = toLocalPath(outDirPath);
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        finish(false, u"Could not create the output folder: "_s + outDir);
        return;
    }

    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Extracting %1 file(s)..."_s.arg(total));

    // Loading is blocked while busy, so m_entries / m_payload are not modified underneath the worker.
    m_extractFuture = QtConcurrent::run([this, total, outDir]() {
        int failed = 0;
        QString firstError;
        int lastPercent = -1;

        for (int i = 0; i < total && !m_cancel; ++i) {
            const QString error = writeEntryTo(i, outDir);
            if (!error.isEmpty()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = error;
            }

            const int percent = static_cast<int>((static_cast<qint64>(i) + 1) * 100 / total);
            if (percent != lastPercent) {
                lastPercent = percent;
                const QString message = u"Extracting (%1/%2): %3"_s.arg(i + 1).arg(total).arg(m_entries[i].path);
                QMetaObject::invokeMethod(this, [this, percent, message]() {
                    setProgress(percent);
                    setStatus(true, message);
                }, Qt::QueuedConnection);
            }
        }

        if (m_cancel)
            return;

        bool ok = (failed == 0);
        QString summary = ok ? u"Extracted %1 file(s) to %2."_s.arg(total).arg(QDir::toNativeSeparators(outDir))
                             : u"Extracted %1 of %2 file(s). %3 failed - first error: %4"_s
                                   .arg(total - failed).arg(total).arg(failed).arg(firstError);

        // The list the M3MP Creator rebuilds this M3MP from.
        const QString listError = writeCreatorList(outDir);
        if (listError.isEmpty()) {
            summary += u" %1 written for the M3MP Creator."_s.arg(CreatorSupport::m3mpListName());
        } else {
            ok = false;
            summary += u" "_s + listError;
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
// Folder tree / file list (same rules as TxpkToolManager)
// ---------------------------------------------------------------------------

void M3mpToolManager::setFilterText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_filterText == trimmed)
        return;
    m_filterText = trimmed;
    emit filterTextChanged();
    if (!m_entries.isEmpty())
        rebuildTree();
}

void M3mpToolManager::resetBrowser() {
    clearPreview();
    m_pathToEntry.clear();
    m_visibleCount = 0;
    m_currentFolderFiles.clear();
    m_folderEntries.clear();
    m_maxFileItemWidth = 180;
    m_currentFileIndex = -1;
    m_currentEntry = -1;
    m_treeModel->clear();

    emit currentFolderFilesChanged();
    emit currentFileChanged();
    emit treeRebuilt();
}

void M3mpToolManager::rebuildTree() {
    clearPreview();
    m_pathToEntry.clear();
    m_currentFolderFiles.clear();
    m_folderEntries.clear();
    m_maxFileItemWidth = 180;
    m_currentFileIndex = -1;
    m_currentEntry = -1;

    QList<QString> paths;
    paths.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const QString& path = m_entries[i].path;
        if (!m_filterText.isEmpty() && !path.contains(m_filterText, Qt::CaseInsensitive))
            continue;

        const QString key = m_rootName + u'/' + path.split(u'/', Qt::SkipEmptyParts).join(u'/');
        if (m_pathToEntry.contains(key))
            continue; // Duplicate path: the first one wins.
        m_pathToEntry.insert(key, i);
        paths.append(key);
    }

    m_visibleCount = static_cast<int>(paths.size());
    m_treeModel->buildTree(paths);

    emit currentFolderFilesChanged();
    emit currentFileChanged();
    emit treeRebuilt();
}

QModelIndex M3mpToolManager::rootFolderIndex() const {
    return m_treeModel->rowCount() > 0 ? m_treeModel->index(0, 0) : QModelIndex();
}

void M3mpToolManager::selectFolder(const QModelIndex& index) {
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
    m_maxFileItemWidth = std::max(180, static_cast<int>(longestCharCount * 6.8) + 36);

    emit currentFolderFilesChanged();
    selectFile(-1);
}

void M3mpToolManager::selectFile(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_folderEntries.size()) {
        if (m_currentFileIndex == -1 && m_currentEntry == -1 && m_previewKind.isEmpty())
            return;
        clearPreview();
        m_currentFileIndex = -1;
        m_currentEntry = -1;
        emit currentFileChanged();
        return;
    }

    m_currentFileIndex = fileIndex;
    m_currentEntry = m_folderEntries[fileIndex];
    loadPreview(m_currentEntry);
    emit currentFileChanged();
}

bool M3mpToolManager::handleFileKey(int qtKey, int rowsPerColumn) {
    if (qtKey != Qt::Key_Down && qtKey != Qt::Key_Up && qtKey != Qt::Key_Left && qtKey != Qt::Key_Right)
        return false;

    const int total = static_cast<int>(m_currentFolderFiles.size());
    if (total == 0)
        return false;

    const int perColumn = std::max(1, rowsPerColumn);
    const int current = m_currentFileIndex;
    int next = current;

    if (current < 0)
        next = 0;
    else if (qtKey == Qt::Key_Down)
        next = std::min(current + 1, total - 1);
    else if (qtKey == Qt::Key_Up)
        next = std::max(current - 1, 0);
    else if (qtKey == Qt::Key_Right)
        next = (current + perColumn < total) ? current + perColumn : total - 1;
    else if (qtKey == Qt::Key_Left)
        next = (current - perColumn >= 0) ? current - perColumn : current;

    if (next != current)
        selectFile(next);
    return true;
}

// ---------------------------------------------------------------------------
// M3MP Creator
// ---------------------------------------------------------------------------

void M3mpToolManager::reportProgress(int percent, const QString& message) {
    setProgress(percent);
    setStatus(true, message);
}

QString M3mpToolManager::writeCreatorList(const QString& outDir) const {
    XmlExtractFileInfo list;
    list.type = u"M3MP"_s;
    list.index = m_source.index >= 0 ? static_cast<quint32>(m_source.index) : 0;
    list.folderHash = m_source.folderHash;
    list.fileNameHash = m_source.fileHash;
    list.isCompressed = m_source.isCompressed;
    list.platform = CreatorSupport::platformName(m_source.platform);
    list.version = m_source.version;
    list.outputName = m_source.outputName.isEmpty() ? m_sourceName + u".m3mp"_s : m_source.outputName;

    for (const Entry& entry : m_entries) {
        ExtractFileEntry item;
        item.filePath = entry.storedPath;
        item.file = entry.path;
        list.entries.append(item);
    }

    const QString path = QDir(outDir).filePath(CreatorSupport::m3mpListName());
    if (!list.serialize(path))
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(path));
    return {};
}

QVariantMap M3mpToolManager::inspectCreatorList(const QString& xmlPath, const QString& sourceDir) const {
    return CreatorSupport::inspectList(xmlPath, sourceDir, u"M3MP"_s);
}

void M3mpToolManager::createM3mp(const QString& xmlPathIn, const QString& sourceDirIn, const QString& outM3mpPathIn) {
    if (m_busy)
        return;

    const QString xmlPath = toLocalPath(xmlPathIn);
    const QString outPath = toLocalPath(outM3mpPathIn);
    const QVariantMap inspected = inspectCreatorList(xmlPath, sourceDirIn);
    if (!inspected.value(u"ok"_s).toBool()) {
        finish(false, inspected.value(u"error"_s).toString());
        return;
    }
    if (outPath.trimmed().isEmpty()) {
        finish(false, u"Choose where to save the new M3MP."_s);
        return;
    }
    const QString sourceDir = inspected.value(u"sourceDir"_s).toString();

    XmlExtractFileInfo list;
    list.deserialize(xmlPath);

    // The chunk compression has to match the game: the list's version, else the loaded game's.
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const int version = list.version >= 0 ? list.version
                        : orchestrator   ? orchestrator->blobsetVersion()
                                         : static_cast<int>(CoreDefs::BlobsetVersion::V4);
    const int platform = CreatorSupport::platformFromName(
        list.platform, orchestrator ? orchestrator->platform() : static_cast<int>(CoreDefs::Platforms::Windows));

    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Creating %1..."_s.arg(QFileInfo(outPath).fileName()));

    m_extractFuture = QtConcurrent::run([this, list, sourceDir, outPath, version, platform]() {
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
            constexpr qint64 kChunkSize = 32768;
            const bool bigEndian = platform != static_cast<int>(CoreDefs::Platforms::Windows);
            const int count = static_cast<int>(list.entries.size());
            const QString compression = version == static_cast<int>(CoreDefs::BlobsetVersion::V4) ? u"ZSTD"_s
                                        : version == static_cast<int>(CoreDefs::BlobsetVersion::V3)
                                            ? u"LZMA (no properties)"_s : u"LZMA"_s;
            log(u"%1 file(s), %2, blobset V%3, 32 KB %4 chunks."_s
                    .arg(count).arg(CreatorSupport::platformName(platform)).arg(version + 1).arg(compression), true);

            // 1. The files, one after the other, in the list's order.
            QByteArray payload;
            QList<QPair<quint32, quint32>> files;   // {offset, size}
            QList<QByteArray> paths;
            for (int i = 0; i < count && !m_cancel; ++i) {
                const ExtractFileEntry& item = list.entries[i];
                const QString shown = item.file.isEmpty() ? item.filePath : item.file;
                const QString file = CreatorSupport::resolveListFile(sourceDir, item.file, item.filePath);
                if (file.isEmpty())
                    fail(u"%1 is missing from %2."_s.arg(shown, QDir::toNativeSeparators(sourceDir)));
                QFile in(file);
                if (!in.open(QIODevice::ReadOnly))
                    fail(u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(file), in.errorString()));
                const QByteArray data = in.readAll();
                if (payload.size() + data.size() > std::numeric_limits<quint32>::max())
                    fail(u"The files add up to more than 4 GB."_s);
                files.append({static_cast<quint32>(payload.size()), static_cast<quint32>(data.size())});
                paths.append(item.filePath.toUtf8());
                payload += data;
                progress(static_cast<int>((i + 1) * 20 / count), u"Reading (%1/%2): %3"_s.arg(i + 1).arg(count).arg(shown));
            }
            if (m_cancel)
                return;

            // 2. 32 KB chunks. A chunk that doesn't get smaller is stored (compressed size == uncompressed size).
            QList<QByteArray> chunks;
            QList<quint32> chunkSizes;
            const qint64 chunkCount = (payload.size() + kChunkSize - 1) / kChunkSize;
            int stored = 0;
            for (qint64 c = 0; c < chunkCount && !m_cancel; ++c) {
                const QByteArray chunk = payload.mid(c * kChunkSize, kChunkSize);
                QByteArray packed;
                if (version == static_cast<int>(CoreDefs::BlobsetVersion::V4)) {
                    packed = CompressionIO::compressZstd(chunk);
                } else {
                    packed = CompressionIO::compressLzma(chunk);
                    if (version == static_cast<int>(CoreDefs::BlobsetVersion::V3))
                        packed.remove(0, 5);    // V3 leaves the LZMA properties out.
                }
                if (packed.isEmpty())
                    fail(u"Compressing chunk %1 failed."_s.arg(c));
                if (packed.size() >= chunk.size()) {
                    packed = chunk;
                    ++stored;
                }
                chunks.append(packed);
                chunkSizes.append(static_cast<quint32>(chunk.size()));
                progress(static_cast<int>(20 + (c + 1) * 70 / chunkCount),
                         u"Compressing chunk %1 of %2..."_s.arg(c + 1).arg(chunkCount));
            }
            if (m_cancel)
                return;
            if (stored > 0)
                log(u"%1 chunk(s) didn't compress and are stored as they are."_s.arg(stored), true);

            // 3. Header: counts, chunk table offset, file table, paths, chunk table, then the chunks.
            QByteArray header;
            QBuffer buffer(&header);
            buffer.open(QIODevice::WriteOnly);
            {
                PackageStream out(&buffer, bigEndian ? PackageStream::Big : PackageStream::Little);
                qint64 pathsSize = 0;
                for (const QByteArray& path : std::as_const(paths))
                    pathsSize += path.size() + 1;
                const qint64 chunkTable = 16 + 16LL * count + pathsSize;

                out.writeUInt32(kM3mpMagic);
                out.writeUInt32(static_cast<quint32>(count));
                out.writeUInt32(static_cast<quint32>(chunks.size()));
                out.writeUInt32(static_cast<quint32>(chunkTable));

                qint64 pathOffset = 16 + 16LL * count;
                for (int i = 0; i < count; ++i) {
                    out.writeUInt32(files[i].first);
                    out.writeUInt32(files[i].second);
                    out.writeUInt32(static_cast<quint32>(pathOffset));
                    out.writeUInt32(0);
                    pathOffset += paths[i].size() + 1;
                }
                for (const QByteArray& path : std::as_const(paths))
                    out.writeBytes(path + QByteArray(1, '\0'));

                qint64 chunkOffset = chunkTable + 12LL * chunks.size();
                for (qsizetype c = 0; c < chunks.size(); ++c) {
                    out.writeUInt32(static_cast<quint32>(chunkOffset));
                    out.writeUInt32(static_cast<quint32>(chunks[c].size()));
                    out.writeUInt32(chunkSizes[c]);
                    chunkOffset += chunks[c].size();
                }
                if (chunkOffset > std::numeric_limits<quint32>::max())
                    fail(u"The new M3MP would be larger than 4 GB."_s);
            }
            buffer.close();

            progress(92, u"Writing %1..."_s.arg(QFileInfo(outPath).fileName()));
            if (!QDir().mkpath(QFileInfo(outPath).absolutePath()))
                fail(u"Could not create the folder of %1."_s.arg(QDir::toNativeSeparators(outPath)));
            QSaveFile out(outPath);
            if (!out.open(QIODevice::WriteOnly))
                fail(u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(outPath), out.errorString()));
            qint64 total = out.write(header);
            for (const QByteArray& chunk : std::as_const(chunks))
                total += out.write(chunk);
            if (!out.commit())
                fail(u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(outPath), out.errorString()));

            // 4. <name>.xml for Modify / Create (the M3MP is stored in the blobset as it is).
            XmlModifyFileInfo info;
            info.index = list.index;
            info.folderHash = list.folderHash;
            info.fileNameHash = list.fileNameHash;
            info.isCompressed = list.isCompressed;
            info.mainCompressedSize = info.mainUnCompressedSize = static_cast<quint32>(total);
            for (const ExtractFileEntry& item : list.entries) {
                ModifyFileEntry entry;
                entry.filePath = item.filePath;
                info.entries.append(entry);
            }
            const QFileInfo outInfo(outPath);
            const QString xmlOut = outInfo.absolutePath() + u'/' + outInfo.completeBaseName() + u".xml"_s;
            if (!info.serialize(xmlOut))
                fail(u"Could not write %1."_s.arg(QDir::toNativeSeparators(xmlOut)));

            log(u"Wrote %1 (%2 of files in %3, %4 chunk(s))."_s
                    .arg(QDir::toNativeSeparators(outPath), DdsImageProvider::humanSize(payload.size()),
                         DdsImageProvider::humanSize(total)).arg(chunks.size()), true);
            log(u"Wrote %1 for Modify / Create."_s.arg(QDir::toNativeSeparators(xmlOut)), true);
            if (list.folderHash.isEmpty() && list.fileNameHash.isEmpty() && list.index == 0)
                log(u"The list doesn't say which blobset entry this M3MP replaces: set Index, FolderHash and "
                    u"FileNameHash in %1 before using it with Modify."_s.arg(outInfo.completeBaseName() + u".xml"_s), false);

            summary = u"Created %1 with %2 file(s)."_s.arg(outInfo.fileName()).arg(count);
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
