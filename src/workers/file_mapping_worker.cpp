#include "file_mapping_worker.h"
#include "asset_signatures.h"
#include "blobset_file.h"
#include "compression_io.h"
#include "m3mp_file.h"
#include "mini_txpk_file.h"
#include "package_stream.h"
#include "txpk_file.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kConvMarkerOffset = 20;           // Offset of the "conv" marker inside a mini DDS TXPK header.
constexpr quint32 kZstdFrameMagic = 0xFD2FB528u;   // Zstd frame magic (little endian).
constexpr qint32 kMaxChunks = 1 << 20;             // Sanity limit for chunk tables read from a (possibly corrupt) archive.
constexpr qint64 kHeaderPeekBytes = 8 * 1024 * 1024; // Upper bound for the part of a section parsed to list its files.
constexpr int kM3mpZstdChunks = 3;                 // V4 M3MP: the header lives in the first 3 chunks...
constexpr int kM3mpZstdChunksLarge = 4;            // ...or the first 4 when the main section is large.
constexpr quint32 kM3mpLargeMain = 9'000'000;
constexpr qint64 kStatusIntervalMs = 50;           // Throttle for the status text so the UI thread isn't flooded.

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

qint32 readInt32(PackageStream& in) {
    return static_cast<qint32>(in.readUInt32());
}

bool isZstdFrame(const QByteArray& chunk) {
    return chunk.size() >= 4 && qFromLittleEndian<quint32>(chunk.constData()) == kZstdFrameMagic;
}

/// First 4 bytes as a magic number, 0 when there is nothing to read (the entry then ends up as "unknown").
quint32 peekMagic(const QByteArray& data, PackageStream::Endianness endian) {
    if (data.size() < 4)
        return 0;

    return endian == PackageStream::Big ? qFromBigEndian<quint32>(data.constData())
                                        : qFromLittleEndian<quint32>(data.constData());
}

/// The path a mini DDS TXPK header stores at +20 starts with "convant2-temp-intermediate"; Don Bradman Cricket 14
/// (PS3) also has headers whose path starts with "intermediate/" (character emblems, stored as their .png names).
bool isMiniPathStart(const QByteArray& head) {
    return head.startsWith("conv") || head.startsWith("intermediate/") || head.startsWith("intermediate\\");
}

/// Checks the marker that identifies a mini DDS TXPK.
bool isMiniDdsTxpk(PackageStream& in, qint64 entryOffset) {
    if (entryOffset + kConvMarkerOffset + 4 > in.length())
        return false;

    in.seek(entryOffset + kConvMarkerOffset);
    return isMiniPathStart(in.readBytes(std::min<qint64>(13, in.length() - in.position())));
}

/// Same check as isMiniDdsTxpk, for a header that was already decompressed into memory.
/// Mini DDS headers have no magic of their own, so this is the only reliable way to spot a compressed one.
bool hasConvMarker(const QByteArray& header) {
    return header.size() >= kConvMarkerOffset + 4 && isMiniPathStart(header.mid(kConvMarkerOffset, 13));
}

/// Reads `size` bytes, refusing sizes that can't possibly fit into the file.
QByteArray readChunkBytes(PackageStream& in, qint64 size) {
    if (size < 0 || size > in.length() - in.position())
        fail(u"Invalid chunk size in blobset entry"_s);

    return in.readBytes(size);
}

/// Reads up to `maxBytes` starting at `offset`; never reads past the end of the file.
QByteArray peekBytes(PackageStream& in, qint64 offset, qint64 maxBytes) {
    const qint64 available = std::max<qint64>(0, in.length() - offset);
    in.seek(offset);
    return in.readBytes(std::min(maxBytes, available));
}

qint64 headerWindow(qint64 sectionSize) {
    return sectionSize > 0 ? std::min(sectionSize, kHeaderPeekBytes) : kHeaderPeekBytes;
}

/// V1 LZMA block: chunk count, [size + 4] * count, then per chunk: uncompressed size + data.
/// Decodes at most `maxChunks` chunks and stops once `maxBytes` were produced.
/// V2 (v2Uncompressed >= 0, the section's uncompressed size): no chunk table, see CompressionIO::readLzmaSectionV2.
QByteArray readLzmaBlock(PackageStream& in, qint64 offset,
                         qint32 maxChunks = std::numeric_limits<qint32>::max(),
                         qint64 maxBytes = std::numeric_limits<qint64>::max(),
                         qint64 v2Uncompressed = -1) {
    in.seek(offset);

    if (v2Uncompressed >= 0) {
        const qint64 limit = maxChunks == std::numeric_limits<qint32>::max()
                                 ? maxBytes
                                 : std::min<qint64>(maxBytes, static_cast<qint64>(maxChunks) * CompressionIO::kLzmaV2ChunkSize);
        return CompressionIO::readLzmaSectionV2(in, v2Uncompressed,
                                                limit == std::numeric_limits<qint64>::max() ? -1 : limit);
    }

    const qint32 chunkCount = readInt32(in);
    if (chunkCount <= 0 || chunkCount > kMaxChunks)
        fail(u"Invalid chunk count in blobset entry"_s);

    QList<qint32> compressedSizes;
    compressedSizes.reserve(chunkCount);
    for (qint32 j = 0; j < chunkCount; ++j)
        compressedSizes.append(readInt32(in) - 4); // Stored sizes include 4 extra bytes.

    QByteArray block;
    for (qint32 j = 0; j < chunkCount && j < maxChunks && block.size() < maxBytes; ++j) {
        const qint32 uncompressedSize = readInt32(in);
        const QByteArray chunk = readChunkBytes(in, compressedSizes[j]);

        block += CompressionIO::decodeV1Chunk(chunk, static_cast<quint32>(uncompressedSize));   // Stored or LZMA.
    }
    return block;
}

/// V1 LZMA block: reads only the first chunk and returns its magic number (same rules as the C# tool):
///   - first chunk stored as it is -> magic read in the platform byte order (Reader.ReadInt32),
///   - first chunk compressed      -> magic read little endian from the decompressed data
///                                    (LZMA_IO.DecompressAndReadMagic).
quint32 peekLzmaMagic(PackageStream& in, qint64 offset, PackageStream::Endianness endian, QByteArray& firstChunk,
                      qint64 v2Uncompressed = -1) {
    in.seek(offset);

    if (v2Uncompressed >= 0) {
        // V2: the first chunk is int32 (size + 4) then the data; stored when it's a full chunk's size.
        const qint64 firstCompressed = static_cast<qint64>(readInt32(in)) - 4;
        const qint64 firstUncompressed = std::min<qint64>(CompressionIO::kLzmaV2ChunkSize, v2Uncompressed);
        if (firstCompressed <= 0)
            fail(u"Invalid V2 LZMA chunk size in blobset entry"_s);

        if (firstCompressed == firstUncompressed) {
            firstChunk = readChunkBytes(in, firstUncompressed);
            return peekMagic(firstChunk, endian);
        }
        firstChunk = CompressionIO::decompressLzma(readChunkBytes(in, firstCompressed),
                                                   static_cast<quint32>(firstUncompressed));
        return peekMagic(firstChunk, PackageStream::Little);
    }

    const qint32 chunkCount = readInt32(in);
    if (chunkCount <= 0 || chunkCount > kMaxChunks)
        fail(u"Invalid chunk count in blobset entry"_s);

    const qint64 firstCompressed = static_cast<qint64>(readInt32(in)) - 4; // Stored sizes include 4 extra bytes.

    in.seek(offset + 4 + 4 * static_cast<qint64>(chunkCount));             // Skip the rest of the size table.
    const qint32 firstUncompressed = readInt32(in);
    if (firstUncompressed < 0)
        fail(u"Invalid chunk size in blobset entry"_s);

    if (firstCompressed == firstUncompressed) {
        const QByteArray stored = readChunkBytes(in, firstUncompressed);
        firstChunk = CompressionIO::decodeV1Chunk(stored, static_cast<quint32>(firstUncompressed));
        // Really stored: the magic in the platform byte order. LZMA as big as its data: like any compressed chunk.
        return peekMagic(firstChunk, firstChunk == stored ? endian : PackageStream::Little);
    }

    firstChunk = CompressionIO::decompressLzma(readChunkBytes(in, firstCompressed),
                                               static_cast<quint32>(firstUncompressed));
    return peekMagic(firstChunk, PackageStream::Little);
}

/// V4 ZSTD chunk: int32 (size + 4), then the data.
QByteArray readZstdChunk(PackageStream& in) {
    const QByteArray chunk = readChunkBytes(in, static_cast<qint64>(readInt32(in)) - 4);
    return CompressionIO::decompressZstd(chunk, isZstdFrame(chunk));
}

QByteArray readZstdChunks(PackageStream& in, qint64 offset, int count) {
    in.seek(offset);

    QByteArray block;
    for (int j = 0; j < count && !in.atEnd(); ++j)
        block += readZstdChunk(in);
    return block;
}

/// Runs `parse` on a PackageStream that reads from an in-memory copy of `data`.
template <typename Parse>
void parseFromBytes(const QByteArray& data, PackageStream::Endianness endian, Parse&& parse) {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);

    PackageStream stream(&buffer, endian);
    parse(stream);
}

void writeFileAtomically(const QString& path, const QByteArray& data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail(u"Could not create the folder for '%1'."_s.arg(path));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(u"Could not open '%1' for writing: %2"_s.arg(path, file.errorString()));

    if (file.write(data) != data.size() || !file.commit())
        fail(u"Failed writing '%1': %2"_s.arg(path, file.errorString()));
}

QString versionLabel(CoreDefs::BlobsetVersion version) {
    return u"V%1"_s.arg(static_cast<int>(version) + 1);
}

// ---------------------------------------------------------------------------
// Mapped path builders - the mapping always stores '\' separators like the C# tool did.
// ---------------------------------------------------------------------------

QString indexedPath(const QString& directory, int index, const QString& extension) {
    return directory + u"\\"_s + QString::number(index) + u"."_s + extension;
}

QString unknownPath(const QString& category, int index) {
    return u"unknown\\"_s + category + u"\\"_s + QString::number(index) + u".dat"_s;
}

/// V1 / V2: the "unknown" sub folder an entry falls into, decided by how its sections are stored.
QString unknownCategoryV1(const BlobsetEntry& entry) {
    const bool mainRaw = entry.mainCompressedSize == entry.mainUnCompressedSize;
    const bool vramRaw = entry.vramCompressedSize == entry.vramUnCompressedSize;

    if (mainRaw)
        return vramRaw ? u"mainuncompressed_vramuncompressed"_s : u"mainuncompressed_vramcompressed"_s;
    return vramRaw ? u"maincompressed"_s : u"maincompressed_vramcompressed"_s;
}

/// The mini header stores "convant2-temp-intermediate\<path>"; the mapping only keeps <path>.
/// DBC14 (PS3) headers that store "intermediate\<path>.png" keep <path>.dds (the entry is a texture).
QString miniMappingPath(const MiniTxpk& mini) {
    static const QString prefix = u"convant2-temp-intermediate\\"_s;
    static const QString shortPrefix = u"intermediate\\"_s;

    QString path = mini.ddsFilePath;
    path.replace(u'/', u'\\');
    if (path.startsWith(prefix, Qt::CaseInsensitive)) {
        path.remove(0, prefix.size());
    } else if (path.startsWith(shortPrefix, Qt::CaseInsensitive)) {
        path.remove(0, shortPrefix.size());
        const qsizetype dot = path.lastIndexOf(u'.');
        if (dot > path.lastIndexOf(u'\\') && !path.endsWith(u".dds", Qt::CaseInsensitive))
            path = path.left(dot) + u".dds"_s;
    }
    return path;
}

QString fevFileName(CoreDefs::Game game) {
    switch (game) {
    case CoreDefs::Game::AFLL:    return u"afll.fev"_s;
    case CoreDefs::Game::DBC14:   return u"cricket12.fev"_s;
    case CoreDefs::Game::RLL2:    return u"rll2.fev"_s;
    case CoreDefs::Game::RLL2WCE: return u"rll2wce.fev"_s;
    default:                      return u"Fmod_Events.fev"_s;
    }
}

/// BANK / WAV entries: the older games ship plain wav files, the rest FMOD banks.
QString audioExtension(CoreDefs::Game game) {
    switch (game) {
    case CoreDefs::Game::AFLL:
    case CoreDefs::Game::DBC14:
    case CoreDefs::Game::RLL2:
    case CoreDefs::Game::RLL2WCE:
        return u"wav"_s;
    default:
        return u"bank"_s;
    }
}

} // namespace

// ==============================================================================================================
// File lists (TXPK_File_List.csv / M3MP_File_List.csv)
// ==============================================================================================================

struct FileMappingWorker::FileLists {
    QByteArray txpk;
    QByteArray m3mp;
};

// ==============================================================================================================
// Mapping table codec
// ==============================================================================================================

QString FileMappingWorker::normalizePath(const QString& path) {
    QString result = path.trimmed();
    result.replace(u'/', u'\\');
    return result;
}

QString FileMappingWorker::validatePath(const QString& path) {
    if (path.isEmpty())
        return u"The file path can't be empty."_s;

    const QByteArray latin1 = path.toLatin1();
    if (QString::fromLatin1(latin1) != path)
        return u"The file path contains characters that can't be stored (Latin-1 only)."_s;

    if (latin1.size() > kMaxPathBytes)
        return u"The file path is too long (%1 of %2 bytes)."_s.arg(latin1.size()).arg(kMaxPathBytes);

    return {};
}

void FileMappingWorker::sortEntries(std::vector<MappingEntry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const MappingEntry& a, const MappingEntry& b) {
        const int order = a.path.compare(b.path, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.hash < b.hash;
    });
}

std::vector<FileMappingWorker::MappingEntry> FileMappingWorker::readMappingFile(const QString& file) {
    if (!QFile::exists(file))
        fail(u"The file mapping table doesn't exist: %1"_s.arg(file));

    PackageStream in(file, PackageStream::Little, QIODevice::ReadOnly);

    const quint32 count = in.readUInt32();
    constexpr qint64 kMinEntryBytes = 1 + 8; // Length byte + hash.
    if (static_cast<qint64>(count) * kMinEntryBytes > in.length())
        fail(u"The file mapping table is corrupted (entry count %1 doesn't fit into the file)."_s.arg(count));

    std::vector<MappingEntry> entries;
    entries.reserve(count);

    for (quint32 i = 0; i < count; ++i) {
        const quint8 pathLength = in.readByte();

        MappingEntry entry;
        entry.path = QString::fromLatin1(in.readBytes(pathLength));
        entry.hash = in.readUInt64();
        entries.push_back(std::move(entry));
    }
    return entries;
}

void FileMappingWorker::writeMappingFile(const QString& file, const std::vector<MappingEntry>& entries) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);

    PackageStream out(&buffer, PackageStream::Little);
    out.writeUInt32(static_cast<quint32>(entries.size()));

    for (const MappingEntry& entry : entries) {
        const QString problem = validatePath(entry.path);
        if (!problem.isEmpty())
            fail(u"Can't store '%1': %2"_s.arg(entry.path, problem)); // A truncated length byte would corrupt the table.

        const QByteArray path = entry.path.toLatin1();
        out.writeByte(static_cast<quint8>(path.size()));
        out.writeBytes(path);
        out.writeUInt64(entry.hash);
    }

    buffer.close();
    writeFileAtomically(file, bytes);
}

std::vector<FileMappingWorker::MappingEntry> FileMappingWorker::deserialize(const QString& inputMappingFile) {
    try {
        return readMappingFile(inputMappingFile);
    } catch (const std::exception&) {
        return {};
    }
}

// ==============================================================================================================
// FileMappingWorker
// ==============================================================================================================

// Standard worker constructor - maps session parameters down from AppOrchestrator
FileMappingWorker::FileMappingWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& outputMappingFile,
                                     int gameId, int platform, QObject* parent)
    : QObject(parent),
      m_blobsetFile(blobsetFile),
      m_version(version),
      m_isBigEndian(isBigEndian),
      m_outputMappingFile(outputMappingFile),
      m_gameId(gameId),
      m_platform(platform)
{
}

// Base execution router - triggers the version channel requested by the runtime settings
void FileMappingWorker::generate_mapping() {
    emit updateConsole(QString("Initializing map indexing for blobset version profile: v%1").arg(m_version + 1));

    try {
        // m_version is the CoreDefs::BlobsetVersion value: 0 = V1, 1 = V2, 2 = V3, 3 = V4.
        switch (static_cast<CoreDefs::BlobsetVersion>(m_version)) {
        case CoreDefs::BlobsetVersion::V1:
            mapV1();
            break;
        case CoreDefs::BlobsetVersion::V2:
            mapV2();
            break;
        case CoreDefs::BlobsetVersion::V3:
            mapV3();
            break;
        case CoreDefs::BlobsetVersion::V4:
            mapV4();
            break;
        default:
            emit taskFinished(QString("Error: Unsupported archive layout format specification: v%1").arg(m_version + 1));
            break;
        }
    } catch (const std::exception& ex) {
        emit taskFinished(QString("File mapping failed.\n\nFile: %1\n\n%2").arg(m_currentFile, QString::fromUtf8(ex.what())));
    }
}

// ---------------------------------------------------------------------------
// Paths / header
// ---------------------------------------------------------------------------

QString FileMappingWorker::blobPathFor(quint32 blobSetNumber) const {
    QString path = m_blobsetFile;
    const qsizetype pos = path.lastIndexOf(u"0.blob"_s);
    if (pos >= 0)
        path.replace(pos, 6, QString::number(blobSetNumber) + u".blob"_s);
    return path;
}

bool FileMappingWorker::readHeader(BlobsetFile& blobset, CoreDefs::BlobsetVersion version) {
    m_currentFile = m_blobsetFile;

    QFile headerFile(m_blobsetFile);
    if (!headerFile.open(QIODevice::ReadOnly)) {
        emit taskFinished(version == CoreDefs::BlobsetVersion::V4
                              ? u"Failed to open modern V4 Blobset file."_s
                              : u"Failed to open legacy %1 Blobset file."_s.arg(versionLabel(version)));
        return false;
    }

    QDataStream headerStream(&headerFile);
    if (!blobset.read(headerStream, version, m_isBigEndian, m_gameId)) {
        emit taskFinished("Invalid BLOB Magic Signature.");
        return false;
    }

    if (headerStream.status() != QDataStream::Ok) {
        emit taskFinished("The blobset header is truncated or corrupted.");
        return false;
    }

    if (blobset.filesCount == 0) {
        emit taskFinished("Error: Selected database profile contains 0 active records registry logs entries.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Mini TXPK / TXPK / M3MP helpers
// ---------------------------------------------------------------------------

MiniTxpk FileMappingWorker::readMini(PackageStream& in) const {
    MiniTxpk mini;
    mini.deserialize(in, m_isBigEndian, m_gameId);
    return mini;
}

MiniTxpk FileMappingWorker::parseMini(const QByteArray& header) const {
    std::optional<MiniTxpk> mini;
    parseFromBytes(header, m_isBigEndian ? PackageStream::Big : PackageStream::Little,
                   [&](PackageStream& stream) { mini = readMini(stream); });
    return *mini;
}

void FileMappingWorker::addTxpkList(FileLists& lists, const QByteArray& header, int index) const {
    QList<QString> paths;

    try {
        parseFromBytes(header, m_isBigEndian ? PackageStream::Big : PackageStream::Little, [&](PackageStream& stream) {
            TXPK txpk;
            txpk.deserialize(stream, m_isBigEndian);

            for (const TXPK::Entry& e : txpk.getEntries()) {
                QString path = e.DDSFilePath;
                path.replace(u'/', u'\\');
                if (m_gameId > 15)
                    path.replace(u".badds"_s, u".dds"_s); // Newer BigAnt games map .badds to .dds
                if (!path.endsWith(u".dds"_s, Qt::CaseInsensitive))
                    path += u".dds"_s;
                paths.append(path);
            }
        });
    } catch (const std::exception&) {
        return; // The file list is a convenience, an unreadable header must not stop the mapping.
    }

    for (const QString& path : std::as_const(paths))
        lists.txpk += (path + u","_s + QString::number(index)).toUtf8() + "\r\n";
    lists.txpk += "\r\n";
}

void FileMappingWorker::addM3mpList(FileLists& lists, PackageStream& in, int index) const {
    QList<QString> paths;

    try {
        M3MP m3mp;
        m3mp.deserialize(in, m_isBigEndian);

        for (const M3MP::UnCompressedEntry& e : m3mp.getUnCompressedEntries()) {
            QString path = e.filePath;
            path.replace(u'/', u'\\');
            paths.append(path);
        }
    } catch (const std::exception&) {
        return;
    }

    for (const QString& path : std::as_const(paths))
        lists.m3mp += (path + u","_s + QString::number(index)).toUtf8() + "\r\n";
    lists.m3mp += "\r\n";
}

void FileMappingWorker::addM3mpList(FileLists& lists, const QByteArray& header, int index) const {
    parseFromBytes(header, m_isBigEndian ? PackageStream::Big : PackageStream::Little,
                   [&](PackageStream& stream) { addM3mpList(lists, stream, index); });
}

/// games/<game>/<platform>: the folder above the mapping's "data" folder.
QDir FileMappingWorker::platformDirectory() const {
    QDir dir = QFileInfo(m_outputMappingFile).absoluteDir();
    if (dir.dirName().compare(u"data"_s, Qt::CaseInsensitive) == 0)
        dir.cdUp();
    return dir;
}

void FileMappingWorker::writeFileLists(const FileLists& lists, bool append) const {
    const QDir platformDir = platformDirectory();

    const auto write = [&](const QString& name, const QByteArray& content) {
        const QString path = platformDir.filePath(name);
        if (append) {
            // Incremental update: only the new files' lists, after the ones already there.
            if (content.isEmpty())
                return;
            QByteArray all;
            QFile old(path);
            if (old.open(QIODevice::ReadOnly))
                all = old.readAll();
            writeFileAtomically(path, all + content);
            return;
        }
        if (content.isEmpty())
            QFile::remove(path); // Don't leave a stale list from an earlier run behind.
        else
            writeFileAtomically(path, content);
    };

    write(u"TXPK_File_List.csv"_s, lists.txpk);
    write(u"M3MP_File_List.csv"_s, lists.m3mp);
}

// ===========================================================================
// Classification - Blobset Version 1 / 2 (same entry layout, mirrors FileMapping.WriteV1)
// ===========================================================================

QString FileMappingWorker::resolveV1(PackageStream& in, const BlobsetEntry& entry, int index, FileLists& lists) {
    const qint64 mainOffset = entry.mainFinalOffset;
    const qint64 mainSize = entry.mainUnCompressedSize;
    // V1: matching sizes can still be an LZMA block (AFL Live), see CompressionIO::isStoredV1Section.
    const bool mainRaw = m_lzmaV2 ? entry.mainCompressedSize == entry.mainUnCompressedSize
                                  : CompressionIO::isStoredV1Section(in, entry.mainFinalOffset, entry.mainCompressedSize,
                                                                     entry.mainUnCompressedSize);
    const bool vramRaw = m_lzmaV2 ? entry.vramCompressedSize == entry.vramUnCompressedSize
                                  : CompressionIO::isStoredV1Section(in, entry.vramFinalOffset, entry.vramCompressedSize,
                                                                     entry.vramUnCompressedSize);
    const auto game = static_cast<CoreDefs::Game>(m_gameId);
    const PackageStream::Endianness endian = m_isBigEndian ? PackageStream::Big : PackageStream::Little;
    const qint64 mainV2 = m_lzmaV2 ? mainSize : -1;   // V2 (RLL4): LZMA sections have no chunk table.

    const auto unknown = [&]() {
        return unknownPath(unknownCategoryV1(entry), index);
    };
    // PS3 needs no header dump any more: Create / Modify build the texture descriptor from the imported .dds.
    const auto mini = [](const MiniTxpk& m) {
        return miniMappingPath(m);
    };

    if (mainRaw) {
        // Both vram cases start with the mini DDS TXPK check ("conv" at main offset + 20).
        if (isMiniDdsTxpk(in, mainOffset)) {
            in.seek(mainOffset);
            return mini(readMini(in));
        }

        in.seek(mainOffset);
        const quint32 magic = in.readUInt32();

        if (vramRaw) {
            // ---- main raw, vram raw ----
            switch (static_cast<CoreDefs::FileType>(magic)) {
            case CoreDefs::FileType::PM3M:
            case CoreDefs::FileType::M3MP:
                in.seek(mainOffset);
                addM3mpList(lists, in, index);
                return indexedPath(u"m3mp\\uncompressed"_s, index, u"m3mp"_s);
            case CoreDefs::FileType::FSB:
            case CoreDefs::FileType::FSB5_LE:
            case CoreDefs::FileType::FSB4:   // AFL Live (PC): FMOD 4 sound banks (music).
            case CoreDefs::FileType::FSB4_BE:
                return indexedPath(u"sound\\fsb"_s, index, u"fsb"_s);
            case CoreDefs::FileType::FEV1_BE:
            case CoreDefs::FileType::FEV1:
                return u"sound\\fsb\\"_s + fevFileName(game);
            case CoreDefs::FileType::BANK:
            case CoreDefs::FileType::WAV: {
                const QString extension = audioExtension(game);
                return indexedPath(u"sound\\"_s + extension, index, extension);
            }
            case CoreDefs::FileType::PNG:
                return indexedPath(u"png"_s, index, u"png"_s);
            case CoreDefs::FileType::KPXT:
            case CoreDefs::FileType::TXPK: // Mostly empty (8 byte) TXPKs on RLL3 PC.
                addTxpkList(lists, peekBytes(in, mainOffset, headerWindow(mainSize)), index);
                return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
            default:
                // Models, meshes, materials, animations, index buffers (see asset_signatures.h).
                if (const auto match = AssetSignatures::identifyV1(
                        peekBytes(in, mainOffset, std::min<qint64>(mainSize, AssetSignatures::kPeekBytes))))
                    return AssetSignatures::mappedPath(*match, index);
                if (mainSize <= AssetSignatures::kIndexBufferMaxBytes
                    && AssetSignatures::looksLikeIndexBuffer(peekBytes(in, mainOffset, mainSize), m_isBigEndian))
                    return AssetSignatures::mappedPath(AssetSignatures::indexBufferMatch(), index);
                // Formats recognised by their content (animation packs, collision, font glyphs, ...).
                if (const auto match = AssetSignatures::identifyCommon(
                        peekBytes(in, mainOffset, std::min<qint64>(mainSize, AssetSignatures::kPeekBytes)), mainSize, m_isBigEndian))
                    return AssetSignatures::mappedPath(*match, index);
                return unknown();
            }
        }

        // ---- main raw, vram compressed ----
        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::KPXT:
        case CoreDefs::FileType::TXPK:
            addTxpkList(lists, peekBytes(in, mainOffset, headerWindow(mainSize)), index);
            return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
        default: {
            if (game == CoreDefs::Game::TTC) {
                // TTC: the main section is the mini header, the vram section the pixels (same as the extractor).
                std::optional<MiniTxpk> ttcMini;
                try {
                    ttcMini = parseMini(peekBytes(in, mainOffset, headerWindow(mainSize)));
                } catch (const std::exception&) {
                    // Not a mini header after all: it stays an unknown file.
                }
                if (ttcMini)
                    return mini(*ttcMini);
            }
            return unknown();
        }
        }
    }

    // ---- main compressed: the first LZMA chunk tells what it is ----
    QByteArray firstChunk;
    const quint32 magic = AssetSignatures::canonicalMagic(peekLzmaMagic(in, mainOffset, endian, firstChunk, mainV2));

    // Compressed mini DDS headers start with the width, not a magic, so check "conv" first.
    if (hasConvMarker(firstChunk))
        return mini(parseMini(firstChunk));

    if (vramRaw) {
        // ---- main compressed, vram raw ----
        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::PM3M:
        case CoreDefs::FileType::M3MP:
            addM3mpList(lists, readLzmaBlock(in, mainOffset, std::numeric_limits<qint32>::max(), kHeaderPeekBytes, mainV2), index);
            return indexedPath(u"m3mp\\compressed"_s, index, u"m3mp"_s);
        case CoreDefs::FileType::BMF:
            return indexedPath(u"font"_s, index, u"bmf"_s);
        case CoreDefs::FileType::PNG:
            return indexedPath(u"png"_s, index, u"png"_s);
        case CoreDefs::FileType::BSB_BE:
        case CoreDefs::FileType::BSB:
            return indexedPath(u"ui\\bsb"_s, index, u"bsb"_s);
        case CoreDefs::FileType::MINI:
            return mini(parseMini(firstChunk));
        case CoreDefs::FileType::KPXT:
        case CoreDefs::FileType::TXPK:   // AFL Live: compressed empty (8 byte) TXPKs without a vram section.
            addTxpkList(lists, firstChunk, index);
            return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
        default:
            // Models, meshes, materials, animations, index buffers, content formats (see asset_signatures.h).
            if (const auto match = identifyCompressedV1(in, entry, firstChunk))
                return AssetSignatures::mappedPath(*match, index);
            return unknown();
        }
    }

    // ---- main compressed, vram compressed ----
    switch (static_cast<CoreDefs::FileType>(magic)) {
    case CoreDefs::FileType::MINI:
        return mini(parseMini(firstChunk));
    case CoreDefs::FileType::KPXT:
    case CoreDefs::FileType::TXPK:
        addTxpkList(lists, firstChunk, index);
        return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
    case CoreDefs::FileType::BMF:
        return indexedPath(u"font"_s, index, u"bmf"_s);
    case CoreDefs::FileType::PNG:
        return indexedPath(u"png"_s, index, u"png"_s);
    case CoreDefs::FileType::BSB_BE:
    case CoreDefs::FileType::BSB:
        return indexedPath(u"ui\\bsb"_s, index, u"bsb"_s);
    default:
        // e.g. RLL4's mesh index buffers (main) with their vertex data (vram).
        if (const auto match = identifyCompressedV1(in, entry, firstChunk))
            return AssetSignatures::mappedPath(*match, index);
        return unknown();
    }
}

/// V1 / V2 compressed main section that has no magic: signatures, then (when it's small enough to decompress)
/// a triangle index buffer, then the content formats. Same order as the uncompressed case.
std::optional<AssetSignatures::Match> FileMappingWorker::identifyCompressedV1(PackageStream& in, const BlobsetEntry& entry,
                                                                             const QByteArray& firstChunk) const {
    if (const auto match = AssetSignatures::identifyV1(firstChunk))
        return match;

    const qint64 mainSize = entry.mainUnCompressedSize;
    if (mainSize <= AssetSignatures::kIndexBufferMaxBytes) {
        const QByteArray mainPart = readLzmaBlock(in, entry.mainFinalOffset, std::numeric_limits<qint32>::max(),
                                              std::numeric_limits<qint64>::max(), m_lzmaV2 ? mainSize : -1);
        if (AssetSignatures::looksLikeIndexBuffer(mainPart, m_isBigEndian))
            return AssetSignatures::indexBufferMatch();
    }
    return AssetSignatures::identifyCommon(firstChunk, mainSize, m_isBigEndian);
}

// ===========================================================================
// Classification - Blobset Version 4
// ===========================================================================

// V4 signatures; V3 (Cricket 19) also still has the V1 era model / material layouts.
std::optional<AssetSignatures::Match> FileMappingWorker::identifyAsset(const QByteArray& data, qint64 mainSize) const {
    if (const auto match = AssetSignatures::identifyV4(data, mainSize))
        return match;
    return m_lzmaV3 ? AssetSignatures::identifyV1(data) : std::nullopt;
}

QString FileMappingWorker::resolveV4(PackageStream& in, const BlobsetEntry& entry, int index, FileLists& lists) {
    const qint64 mainSize = entry.mainUnCompressedSize;
    const bool mainRaw = entry.mainCompressedSize == entry.mainUnCompressedSize;
    const bool vramRaw = entry.vramCompressedSize == entry.vramUnCompressedSize;

    const auto unknown = [&](const QString& category) {
        return unknownPath(category, index);
    };
    // PS3 needs no header dump any more: Create / Modify build the texture descriptor from the imported .dds.
    const auto mini = [](const MiniTxpk& m) {
        return miniMappingPath(m);
    };

    // V4 keeps every asset in its own physical file, so everything is relative to the start of that file.
    if (in.length() <= 4)
        return unknown(u"mainuncompressed"_s);

    if (mainRaw) {
        if (isMiniDdsTxpk(in, 0)) {
            in.seek(0);
            return mini(readMini(in));
        }

        in.seek(0);
        const quint32 magic = in.readUInt32();

        if (vramRaw) {
            // ---- main raw, vram raw ----
            switch (static_cast<CoreDefs::FileType>(magic)) {
            case CoreDefs::FileType::M3MP:
                in.seek(0);
                addM3mpList(lists, in, index);
                return indexedPath(u"m3mp\\uncompressed"_s, index, u"m3mp"_s);
            case CoreDefs::FileType::WiseBNK:
                return indexedPath(u"sound\\bnk"_s, index, u"bnk"_s);
            case CoreDefs::FileType::WiseWEM:
                return indexedPath(u"sound\\wem"_s, index, u"wem"_s);
            case CoreDefs::FileType::TXPK:
                addTxpkList(lists, peekBytes(in, 0, headerWindow(mainSize)), index);
                return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
            default:
                // Models, texture lists, ... (see asset_signatures.h).
                if (const auto match = identifyAsset(peekBytes(in, 0, AssetSignatures::kPeekBytes), mainSize))
                    return AssetSignatures::mappedPath(*match, index);
                return unknown(u"mainuncompressed_vramuncompressed"_s);
            }
        }

        // ---- main raw, vram compressed ----
        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::TXPK:
            addTxpkList(lists, peekBytes(in, 0, headerWindow(mainSize)), index);
            return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
        default:
            if (const auto match = identifyAsset(peekBytes(in, 0, AssetSignatures::kPeekBytes), mainSize))
                return AssetSignatures::mappedPath(*match, index);
            return unknown(u"mainuncompressed_vramcompressed"_s);
        }
    }

    // ---- main compressed: the first chunk tells what it is (V4: ZSTD, V3: XORed LZMA) ----
    in.seek(0);
    const QByteArray firstChunk = m_lzmaV3 ? CompressionIO::readLzmaSectionV3(in, mainSize, CompressionIO::kLzmaV2ChunkSize)
                                           : readZstdChunk(in);

    // Compressed mini DDS headers start with the width, not a magic, so check "conv" first.
    if (hasConvMarker(firstChunk))
        return mini(parseMini(firstChunk));

    const quint32 magic = peekMagic(firstChunk, PackageStream::Little);

    if (vramRaw) {
        // ---- main compressed, vram raw ----
        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::M3MP: {
            if (m_lzmaV3) {
                in.seek(0);
                addM3mpList(lists, CompressionIO::readLzmaSectionV3(in, mainSize, kHeaderPeekBytes), index);
            } else {
                const int chunks = entry.mainUnCompressedSize > kM3mpLargeMain ? kM3mpZstdChunksLarge : kM3mpZstdChunks;
                addM3mpList(lists, readZstdChunks(in, 0, chunks), index);
            }
            return indexedPath(u"m3mp\\compressed"_s, index, u"m3mp"_s);
        }
        default:
            // Models, texture lists, ... (see asset_signatures.h).
            if (const auto match = identifyAsset(firstChunk, mainSize))
                return AssetSignatures::mappedPath(*match, index);
            return unknown(u"maincompressed_vramuncompressed"_s);
        }
    }

    // ---- main compressed, vram compressed ----
    switch (static_cast<CoreDefs::FileType>(magic)) {
    case CoreDefs::FileType::TXPK:
        addTxpkList(lists, firstChunk, index);
        return indexedPath(u"dds_txpk"_s, index, u"txpk"_s);
    case CoreDefs::FileType::MINI:
        return mini(parseMini(firstChunk));
    default:
        // e.g. cinematic animations, whose keys live in the vram section.
        if (const auto match = identifyAsset(firstChunk, mainSize))
            return AssetSignatures::mappedPath(*match, index);
        return unknown(u"maincompressed_vramcompressed"_s);
    }
}

// ===========================================================================
// Shared mapping loop
// ===========================================================================

std::vector<FileMappingWorker::MappingEntry> FileMappingWorker::collectEntries(const BlobsetFile& blobset,
                                                                               CoreDefs::BlobsetVersion version,
                                                                               FileLists& lists,
                                                                               const QSet<quint64>* skipHashes) {
    const bool isV4 = version == CoreDefs::BlobsetVersion::V4;
    const bool isV3 = version == CoreDefs::BlobsetVersion::V3;   // V4's file layout, 32 bit hashes, LZMA chunks.
    const int fileCount = static_cast<int>(blobset.filesCount);
    const QString blobsetDir = QFileInfo(m_blobsetFile).absolutePath();
    const PackageStream::Endianness endian = m_isBigEndian ? PackageStream::Big : PackageStream::Little;
    constexpr quint32 kNoBlob = std::numeric_limits<quint32>::max();

    emit updateConsole(QString("Scanning %1 blobset entries...").arg(fileCount));

    std::vector<MappingEntry> mappings;
    mappings.reserve(fileCount);

    std::unique_ptr<PackageStream> blob; // V1 / V2: one blob file serves many entries, keep it open.
    quint32 openBlob = kNoBlob;
    QSet<quint32> missingBlobs;          // Blob containers that couldn't be opened (reported once).

    QElapsedTimer statusTimer;
    statusTimer.start();
    int lastPercent = -1;

    for (int i = 0; i < fileCount; ++i) {
        const BlobsetEntry& entry = blobset.entries[i];
        QString mapped;
        quint64 hash = 0;

        if (isV4 || isV3) {
            // V3 / V4 archives keep every asset in its own physical file, addressed by the hashes.
            hash = isV4 ? entry.folderFileHash64 : entry.folderFileHash32;   // V3: zero extended, like V1.
            m_currentFile = QDir(blobsetDir).filePath(entry.folderHashName + u"/"_s + entry.fileHashName);

            if (skipHashes && skipHashes->contains(hash)) {
                // Already in the mapping: kept as it is (incremental V4 update).
            } else if (QFile::exists(m_currentFile)) {
                PackageStream in(m_currentFile, PackageStream::Little, QIODevice::ReadOnly);
                if (isV3) {
                    // One unreadable entry becomes an unknown file instead of stopping the whole mapping.
                    try {
                        mapped = resolveV4(in, entry, i, lists);
                    } catch (const std::exception& ex) {
                        emit updateConsole(QString("Warning: entry %1 couldn't be classified (%2), mapped as unknown.")
                                               .arg(i).arg(QString::fromUtf8(ex.what())));
                        mapped = unknownPath(u"maincompressed"_s, i);
                    }
                } else {
                    mapped = resolveV4(in, entry, i, lists);
                }
            }
        } else {
            hash = entry.folderFileHash32; // Zero extended, exactly how the orchestrator looks it up.

            // Every V1 / V2 entry gets mapped: one that can't be read becomes an unknown file instead of
            // stopping the whole mapping.
            const auto fallback = [&](const QString& reason) {
                if (!reason.isEmpty())
                    emit updateConsole(QString("Warning: entry %1 couldn't be classified (%2), mapped as unknown.")
                                           .arg(i).arg(reason));
                return unknownPath(unknownCategoryV1(entry), i);
            };

            if (missingBlobs.contains(entry.blobSetNumber)) {
                mapped = fallback(QString());
            } else {
                try {
                    if (!blob || openBlob != entry.blobSetNumber) {
                        blob.reset();
                        m_currentFile = blobPathFor(entry.blobSetNumber);

                        if (!QFile::exists(m_currentFile)) {
                            missingBlobs.insert(entry.blobSetNumber);
                            fail(u"blob container not found: "_s + m_currentFile);
                        }

                        blob = std::make_unique<PackageStream>(m_currentFile, endian, QIODevice::ReadOnly);
                        openBlob = entry.blobSetNumber;
                    }
                    mapped = resolveV1(*blob, entry, i, lists);
                } catch (const std::exception& ex) {
                    mapped = fallback(QString::fromUtf8(ex.what()));
                }
            }
        }

        if (!mapped.isEmpty())
            mappings.push_back({mapped, hash});

        const int percent = static_cast<int>((static_cast<qint64>(i) + 1) * 100 / fileCount);
        if (percent != lastPercent) {
            lastPercent = percent;
            emit progressUpdated(percent);
        }

        if (!mapped.isEmpty() && statusTimer.elapsed() >= kStatusIntervalMs) {
            statusTimer.restart();
            emit updateStatus(QString("Status: Creating New File Mapping Data - %1").arg(mapped));
        }
    }

    return mappings;
}

void FileMappingWorker::mapV1() {
    BlobsetFile blobset;
    if (!readHeader(blobset, CoreDefs::BlobsetVersion::V1))
        return;

    FileLists lists;
    std::vector<MappingEntry> entries = collectEntries(blobset, CoreDefs::BlobsetVersion::V1, lists);
    finish(entries, lists, u"File Mapping V1 Successfully Generated."_s);
}

// V2 (Rugby League Live 4): V1's entry layout and asset rules, only the LZMA sections are stored differently.
void FileMappingWorker::mapV2() {
    BlobsetFile blobset;
    if (!readHeader(blobset, CoreDefs::BlobsetVersion::V2))
        return;

    m_lzmaV2 = true;
    FileLists lists;
    std::vector<MappingEntry> entries = collectEntries(blobset, CoreDefs::BlobsetVersion::V2, lists);
    m_lzmaV2 = false;
    finish(entries, lists, u"File Mapping V2 Successfully Generated."_s);
}

// V3 (Cricket 19): V4's one-file-per-asset layout with 32 bit hashes and XORed LZMA chunks.
void FileMappingWorker::mapV3() {
    BlobsetFile blobset;
    if (!readHeader(blobset, CoreDefs::BlobsetVersion::V3))
        return;

    m_lzmaV3 = true;
    FileLists lists;
    std::vector<MappingEntry> entries = collectEntries(blobset, CoreDefs::BlobsetVersion::V3, lists);
    m_lzmaV3 = false;
    finish(entries, lists, u"File Mapping V3 Successfully Generated."_s);
}

void FileMappingWorker::mapV4() {
    BlobsetFile blobset;
    if (!readHeader(blobset, CoreDefs::BlobsetVersion::V4))
        return;

    // With an existing mapping only the changes are applied: entries whose hash is no longer in the blobset are
    // removed, hashes the mapping doesn't have yet are classified and added, everything else (including names
    // given by Resolve File Names or the File Mapping Editor) stays as it is. Delete the mapping for a full rebuild.
    std::vector<MappingEntry> existing;
    if (QFile::exists(m_outputMappingFile)) {
        try {
            existing = readMappingFile(m_outputMappingFile);
        } catch (const std::exception& ex) {
            emit updateConsole(u"The existing mapping couldn't be read (%1), rebuilding it."_s.arg(QString::fromUtf8(ex.what())));
            existing.clear();
        }
    }

    FileLists lists;
    if (existing.empty()) {
        std::vector<MappingEntry> entries = collectEntries(blobset, CoreDefs::BlobsetVersion::V4, lists);
        finish(entries, lists, u"File Mapping V4 Successfully Generated."_s);
        return;
    }

    QSet<quint64> current;
    current.reserve(blobset.entries.size());
    for (const BlobsetEntry& entry : std::as_const(blobset.entries))
        current.insert(entry.folderFileHash64);

    std::vector<MappingEntry> merged;
    merged.reserve(blobset.entries.size());
    QSet<quint64> known;
    int removed = 0;
    for (MappingEntry& entry : existing) {
        if (!current.contains(entry.hash) || known.contains(entry.hash)) {
            ++removed;   // Deleted from the blobset (or a duplicate line).
            continue;
        }
        known.insert(entry.hash);
        merged.push_back(std::move(entry));
    }

    emit updateConsole(u"Existing mapping: %1 file(s) kept, %2 removed, %3 new hash(es) to classify."_s
                           .arg(merged.size()).arg(removed).arg(current.size() - known.size()));

    std::vector<MappingEntry> added = collectEntries(blobset, CoreDefs::BlobsetVersion::V4, lists, &known);
    for (MappingEntry& entry : added)
        merged.push_back(std::move(entry));

    if (added.empty() && removed == 0) {
        emit progressUpdated(100);
        emit updateStatus("Status: Ready | Blobset Tools Sanbox Mode");
        emit taskSucceeded();
        emit taskFinished(u"File Mapping V4 is already up to date (%1 files)."_s.arg(merged.size()));
        return;
    }

    serialize(merged);
    writeFileLists(lists, true);   // New TXPK / M3MP lists are added to the existing ones.

    emit updateStatus("Status: Ready | Blobset Tools Sanbox Mode");
    emit taskSucceeded();
    emit taskFinished(u"File Mapping V4 updated: %1 added, %2 removed (%3 files)."_s
                          .arg(added.size()).arg(removed).arg(merged.size()));
}

void FileMappingWorker::finish(std::vector<MappingEntry>& entries, const FileLists& lists, const QString& message) {
    serialize(entries);
    writeFileLists(lists);

    emit updateStatus("Status: Ready | Blobset Tools Sanbox Mode");
    emit taskSucceeded();
    emit taskFinished(message);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

void FileMappingWorker::serialize(std::vector<MappingEntry>& entries) {
    m_currentFile = m_outputMappingFile;

    if (entries.empty())
        fail(u"Cannot execute disk dump because the tracking entry array was empty."_s);

    emit updateConsole(QString("Sorting %1 files alphabetically for workspace folder optimization...").arg(entries.size()));

    // Keep file paths clean and ordered alphabetically exactly like the C# OrderBy implementation
    sortEntries(entries);
    writeMappingFile(m_outputMappingFile, entries);
    createModFolders(entries);
}

/// Builds the matching mod sub folders (<platform>/mods/...) so nobody has to create them by hand.
void FileMappingWorker::createModFolders(const std::vector<MappingEntry>& entries) {
    // games/<game>/<platform>/mods (next to the "data" folder, where Modify / Create read the mods from).
    const QDir platformDir = platformDirectory();
    const QDir modsDir(platformDir.filePath(u"mods"_s));

    // Older versions created the folders in data/mods by mistake: remove that tree when it holds no files.
    const QString strayMods = QDir(QFileInfo(m_outputMappingFile).absolutePath()).filePath(u"mods"_s);
    if (QDir::cleanPath(strayMods).compare(QDir::cleanPath(modsDir.absolutePath()), Qt::CaseInsensitive) != 0
        && QFileInfo(strayMods).isDir()) {
        QDirIterator files(strayMods, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        if (!files.hasNext()) {
            QDir(strayMods).removeRecursively();
            emit updateConsole(u"Removed the empty mod folders that were wrongly created in data\\mods."_s);
        } else {
            emit updateConsole(u"Note: %1 holds files; mods belong in %2."_s
                                   .arg(QDir::toNativeSeparators(strayMods), QDir::toNativeSeparators(modsDir.absolutePath())));
        }
    }
    const QString root = QDir::cleanPath(modsDir.absolutePath()) + u"/"_s;

    QSet<QString> created;
    for (const MappingEntry& entry : entries) {
        QString relative = entry.path;
        relative.replace(u'\\', u'/');

        const QString folder = QFileInfo(relative).path();
        if (folder == u"."_s || created.contains(folder))
            continue;
        created.insert(folder);

        const QString full = QDir::cleanPath(modsDir.absoluteFilePath(folder));
        if (!full.startsWith(root, Qt::CaseInsensitive)) {
            emit updateConsole(QString("Skipped a mod folder outside the mods directory: %1").arg(entry.path));
            continue;
        }
        QDir().mkpath(full);
    }
}
