#include "extract_worker.h"
#include "asset_signatures.h"
#include "blobset_asset_reader.h"
#include "compression_io.h"
#include "dds_processor.h"
#include "gcm_texture.h"
#include "package_stream.h"
#include "txpk_file.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QSettings>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kConvMarkerOffset = 20;          // Offset of the "conv" marker inside a mini DDS TXPK header.
constexpr quint32 kZstdFrameMagic = 0xFD2FB528u;  // Zstd frame magic (little endian).
constexpr qint32 kMaxChunks = 1 << 20;            // Sanity limit for chunk tables read from a (possibly corrupt) archive.
constexpr qint64 kCopyBlockSize = 1 << 20;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

template <typename... Parts>
QString joinPath(const QString& first, const Parts&... rest) {
    static_assert((std::is_same_v<Parts, QString> && ...), "joinPath only accepts QString parts");
    QString result = first;
    ((result.append(u'/'), result.append(rest)), ...);
    return result;
}

QString entryName(int index, const QString& extension) {
    return QString::number(index) + u'.' + extension;
}

QString unknownPath(const QString& category, int index) {
    return joinPath(u"unknown"_s, category, entryName(index, u"dat"_s));
}

/// V1 / V2: the "unknown" sub folder an entry falls into (same rule as FileMappingWorker).
QString unknownCategoryV1(const BlobsetEntry& entry) {
    const bool mainRaw = entry.mainCompressedSize == entry.mainUnCompressedSize;
    const bool vramRaw = entry.vramCompressedSize == entry.vramUnCompressedSize;

    if (mainRaw)
        return vramRaw ? u"mainuncompressed_vramuncompressed"_s : u"mainuncompressed_vramcompressed"_s;
    return vramRaw ? u"maincompressed"_s : u"maincompressed_vramcompressed"_s;
}

qint32 readInt32(PackageStream& in) {
    return static_cast<qint32>(in.readUInt32());
}

std::unique_ptr<QFile> openWriter(const QString& path) {
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly))
        fail(u"Could not open '%1' for writing: %2"_s.arg(path, file->errorString()));
    return file;
}

void writeBytes(QIODevice& out, const QByteArray& data) {
    if (out.write(data) != data.size())
        fail(u"Failed writing to file: "_s + out.errorString());
}

void writeFileBytes(const QString& path, const QByteArray& data) {
    const auto out = openWriter(path);
    writeBytes(*out, data);
}

/// Copies `count` bytes from the stream's current position into `out`.
void copyBytes(PackageStream& in, QIODevice& out, qint64 count) {
    while (count > 0) {
        const qint64 block = std::min(count, kCopyBlockSize);
        writeBytes(out, in.readBytes(block));
        count -= block;
    }
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

QString fevFileName(CoreDefs::Game game) {
    switch (game) {
    case CoreDefs::Game::AFLL:    return u"afll.fev"_s;
    case CoreDefs::Game::DBC14:   return u"cricket12.fev"_s;
    case CoreDefs::Game::RLL2:    return u"rll2.fev"_s;
    case CoreDefs::Game::RLL2WCE: return u"rll2wce.fev"_s;
    default:                      return u"Fmod_Events.fev"_s;
    }
}

/// BANK / WAV entries: the older games ship plain wav files, the rest FMOD banks (same as the mapping).
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

/// The mini header stores "convant2-temp-intermediate\<path>"; the mapping (and so the extracted file) only keeps <path>.
/// DBC14 (PS3) headers that store "intermediate/<path>.png" keep <path>.dds (same rule as the mapping).
QString miniRelativePath(const MiniTxpk& mini) {
    static const QString prefix = u"convant2-temp-intermediate/"_s;
    static const QString shortPrefix = u"intermediate/"_s;

    QString path = mini.ddsFilePath;
    path.replace(u'\\', u'/');
    if (path.startsWith(prefix, Qt::CaseInsensitive)) {
        path.remove(0, prefix.size());
    } else if (path.startsWith(shortPrefix, Qt::CaseInsensitive)) {
        path.remove(0, shortPrefix.size());
        const qsizetype dot = path.lastIndexOf(u'.');
        if (dot > path.lastIndexOf(u'/') && !path.endsWith(u".dds", Qt::CaseInsensitive))
            path = path.left(dot) + u".dds"_s;
    }
    return path;
}

/// V1 LZMA block: reads only the first chunk and returns its magic number (same rules as the mapping / C# tool):
///   - first chunk stored as it is -> magic read in the platform byte order,
///   - first chunk compressed      -> magic read little endian from the decompressed data.
quint32 peekLzmaMagic(PackageStream& in, qint64 offset, PackageStream::Endianness endian, QByteArray& firstChunk,
                      qint64 v2Uncompressed = -1) {
    in.seek(offset);

    if (v2Uncompressed >= 0) {
        // V2 (RLL4): the first chunk is int32 (size + 4) then the data; stored when it's a full chunk's size.
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

} // namespace

// ==============================================================================================================
// ExtractWorker
// ==============================================================================================================

ExtractWorker::ExtractWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& outputDir,
                             const QString& gameName, int gameId, int platform, QObject* parent)
    : QObject(parent),
      m_blobsetFile(blobsetFile),
      m_version(version),
      m_isBigEndian(isBigEndian),
      m_outDir(outputDir),
      m_gameId(gameId),
      m_gameName(gameName),
      m_platform(platform)
{
}

void ExtractWorker::extract_blobset() {
    emit updateConsole(QString("Extracting %1 Blobset, please wait..........").arg(m_gameName));

    if (!QDir().mkpath(m_outDir)) {
        emit taskFinished("Could not create output folder: " + m_outDir);
        return;
    }

    m_tempDir = std::make_unique<QTemporaryDir>();
    if (!m_tempDir->isValid()) {
        emit taskFinished("Could not create a temporary working folder.");
        return;
    }

    // m_version is the CoreDefs::BlobsetVersion value: 0 = V1, 1 = V2, 2 = V3, 3 = V4.
    switch (static_cast<CoreDefs::BlobsetVersion>(m_version)) {
    case CoreDefs::BlobsetVersion::V1:
        extractV1();
        break;
    case CoreDefs::BlobsetVersion::V2: // Same entry layout as V1 (see BlobsetFile::read).
        extractV2();
        break;
    case CoreDefs::BlobsetVersion::V3: // V4's layout, XORed LZMA chunks instead of ZSTD (Cricket 19).
        m_lzmaV3 = true;
        extractV4();
        m_lzmaV3 = false;
        break;
    case CoreDefs::BlobsetVersion::V4:
        extractV4();
        break;
    default:
        emit taskFinished(QString("Extraction for blobset version %1 is not implemented.").arg(m_version + 1));
        break;
    }

    m_tempDir.reset(); // Removes every temp file.
}

// ---------------------------------------------------------------------------
// Paths / temp files
// ---------------------------------------------------------------------------

QString ExtractWorker::blobPathFor(quint32 blobSetNumber) const {
    QString path = m_blobsetFile;
    const qsizetype pos = path.lastIndexOf(u"0.blob"_s);
    if (pos >= 0)
        path.replace(pos, 6, QString::number(blobSetNumber) + u".blob"_s);
    return path;
}

QString ExtractWorker::makeTarget(const QString& relativePath, QString* normalizedRelative) const {
    QString relative = relativePath;
    relative.replace(u'\\', u'/'); // MiniTxpk / mapping paths use '\'.

    const QString root = QDir::cleanPath(QDir(m_outDir).absolutePath()) + u'/';
    const QString full = QDir::cleanPath(QDir(m_outDir).absoluteFilePath(relative));

    if (!full.startsWith(root, Qt::CaseInsensitive))
        fail(u"Refusing to write outside the output folder: "_s + relativePath);

    const QString parent = QFileInfo(full).absolutePath();
    if (!QDir().mkpath(parent))
        fail(u"Could not create folder: "_s + parent);

    if (normalizedRelative)
        *normalizedRelative = relative;
    return full;
}

QString ExtractWorker::freshTempPath(int index, const QString& extension) {
    const QString path = m_tempDir->filePath(entryName(index, extension));
    QFile::remove(path);
    return path;
}

// ---------------------------------------------------------------------------
// Mini TXPK / DDS helpers
// ---------------------------------------------------------------------------

MiniTxpk ExtractWorker::readMini(PackageStream& in) const {
    MiniTxpk mini;
    mini.deserialize(in, m_isBigEndian, m_gameId);
    return mini;
}

MiniTxpk ExtractWorker::parseMini(const QByteArray& header, qint64* headerEnd) const {
    QByteArray data = header;
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);

    PackageStream stream(&buffer, m_isBigEndian ? PackageStream::Big : PackageStream::Little);
    const MiniTxpk mini = readMini(stream);

    if (headerEnd)
        *headerEnd = stream.position();
    return mini;
}

QByteArray ExtractWorker::toConsoleDds(const QByteArray& pixels, const Gcm::CellGcmTexture& gcm, bool isXbox360,
                                       const X360::TextureHeader& x360, const QString& name) {
    // The descriptor says exactly how the data is stored (same conversion the viewers use).
    const std::optional<QByteArray> dds = isXbox360 ? X360::toDds(x360, pixels) : Gcm::toDds(gcm, pixels);
    if (dds)
        return *dds;

    emit updateConsole(QString("Warning: %1 uses %2, which a .dds can't hold; the raw texture data was written.")
                           .arg(name, isXbox360 ? X360::formatName(x360.format()) : Gcm::formatName(gcm.format)));
    return pixels;
}

QString ExtractWorker::writeMiniDds(const MiniTxpk& mini, const QByteArray& pixelData) {
    QString relative;
    const QString fullPath = makeTarget(miniRelativePath(mini), &relative);

    if (m_isBigEndian)
        writeFileBytes(fullPath, toConsoleDds(pixelData, mini.gcm, mini.isXbox360, mini.x360, relative));
    else
        writeFileBytes(fullPath, pixelData);

    return relative;
}

/// `txpkFile` holds the main section (`mainSize` bytes) followed by the vram section.
QString ExtractWorker::extractTxpkDds(const QString& txpkFile, int mainSize, int index) {
    PackageStream in(txpkFile, m_isBigEndian ? PackageStream::Big : PackageStream::Little, QIODevice::ReadOnly);

    TXPK txpk;
    txpk.deserialize(in, m_isBigEndian);

    const qint64 fileLength = in.length();

    for (const TXPK::Entry& e : txpk.getEntries()) {
        // Data offsets are relative to the vram section; fall back to absolute offsets if that doesn't fit.
        qint64 start = static_cast<qint64>(mainSize) + e.DDSDataOffset;
        if (start + e.DDSDataSize > fileLength)
            start = e.DDSDataOffset;
        if (start + e.DDSDataSize > fileLength)
            fail(u"TXPK entry data lies outside of the file: "_s + e.DDSFilePath);

        in.seek(start);
        QByteArray data = in.readBytes(e.DDSDataSize);

        // Same name TXPK_File_List.csv lists for this texture.
        QString path = e.DDSFilePath;
        if (m_gameId > 15)
            path.replace(u".badds"_s, u".dds"_s); // Newer BigAnt games map .badds to .dds
        if (!path.endsWith(u".dds"_s, Qt::CaseInsensitive))
            path += u".dds"_s;

        const QString fullPath = makeTarget(path);

        if (m_isBigEndian)
            data = toConsoleDds(data, e.GcmTexture, e.IsXbox360, e.X360Header, path);

        writeFileBytes(fullPath, data);
    }

    return joinPath(u"dds_txpk"_s, entryName(index, u"txpk"_s));
}

// ---------------------------------------------------------------------------
// LZMA / ZSTD chunk helpers
// ---------------------------------------------------------------------------

void ExtractWorker::lzmaBlockToDevice(PackageStream& in, QIODevice& out, qint64 v2Uncompressed) {
    if (v2Uncompressed >= 0) {
        // V2 (RLL4): no chunk table, see CompressionIO::readLzmaSectionV2.
        writeBytes(out, CompressionIO::readLzmaSectionV2(in, v2Uncompressed));
        return;
    }

    const int chunkCount = readInt32(in);
    if (chunkCount <= 0 || chunkCount > kMaxChunks)
        fail(u"Invalid chunk count in blobset entry"_s);

    QList<int> compressedSizes;
    for (int j = 0; j < chunkCount; ++j) {
        const int size = readInt32(in) - 4; // Stored sizes include 4 extra bytes.
        if (size < 0)
            fail(u"Invalid chunk size in blobset entry"_s);
        compressedSizes.append(size);
    }

    for (int j = 0; j < chunkCount; ++j) {
        const int uncompressedSize = readInt32(in);
        const QByteArray chunk = readChunkBytes(in, compressedSizes[j]);

        writeBytes(out, CompressionIO::decodeV1Chunk(chunk, static_cast<quint32>(uncompressedSize)));   // Stored or LZMA.
    }
}

QByteArray ExtractWorker::lzmaBlockToBytes(PackageStream& in, qint64 v2Uncompressed) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    lzmaBlockToDevice(in, buffer, v2Uncompressed);
    return data;
}

void ExtractWorker::zstdChunkToDevice(PackageStream& in, QIODevice& out) {
    if (m_lzmaV3) {
        // One 32 KB chunk of whichever section the stream is in.
        const bool mainPart = in.position() < m_v3MainCompressed;
        const qint64 sectionSize = mainPart ? m_v3MainSize : m_v3VramSize;
        writeBytes(out, CompressionIO::readLzmaSectionV3(in, sectionSize, CompressionIO::kLzmaV2ChunkSize));
        return;
    }

    const QByteArray chunk = readChunkBytes(in, static_cast<qint64>(readInt32(in)) - 4);
    writeBytes(out, CompressionIO::decompressZstd(chunk, isZstdFrame(chunk)));
}

/// Decompresses chunks until `maxBytes` of the stream were consumed (maxBytes < 0: until the end of the file).
void ExtractWorker::zstdChunksToDevice(PackageStream& in, QIODevice& out, qint64 maxBytes) {
    if (m_lzmaV3) {
        // The whole section the stream is in (V3 chunks don't know their section's end, the entry does).
        const bool mainPart = in.position() < m_v3MainCompressed;
        writeBytes(out, CompressionIO::readLzmaSectionV3(in, mainPart ? m_v3MainSize : m_v3VramSize));
        return;
    }

    const qint64 start = in.position();
    while (!in.atEnd() && (maxBytes < 0 || in.position() - start < maxBytes))
        zstdChunkToDevice(in, out);
}

QByteArray ExtractWorker::zstdChunksToBytes(PackageStream& in, qint64 maxBytes) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    zstdChunksToDevice(in, buffer, maxBytes);
    return data;
}

// ===========================================================================
// Extract Blobset Version 1 / 2 (same entry layout, mirrors FileMappingWorker::resolveV1)
// ===========================================================================

void ExtractWorker::extractV1() {
    extractLegacy(CoreDefs::BlobsetVersion::V1);
}

void ExtractWorker::extractV2() {
    extractLegacy(CoreDefs::BlobsetVersion::V2);
}

QString ExtractWorker::extractLegacyEntry(PackageStream& in, const BlobsetEntry& entry, int i) {
    const qint64 mainOffset = entry.mainFinalOffset;
    const qint64 vramOffset = entry.vramFinalOffset;
    const int mainSize = static_cast<int>(entry.mainUnCompressedSize);
    const int vramSize = static_cast<int>(entry.vramUnCompressedSize);
    const auto game = static_cast<CoreDefs::Game>(m_gameId);

    // V2 (RLL4): LZMA sections have no chunk table, the readers need each section's uncompressed size instead.
    const bool lzmaV2 = static_cast<CoreDefs::BlobsetVersion>(m_version) == CoreDefs::BlobsetVersion::V2;
    // V1: matching sizes can still be an LZMA block (AFL Live), see CompressionIO::isStoredV1Section.
    const bool mainRaw = lzmaV2 ? entry.mainCompressedSize == entry.mainUnCompressedSize
                                : CompressionIO::isStoredV1Section(in, mainOffset, entry.mainCompressedSize,
                                                                   entry.mainUnCompressedSize);
    const bool vramRaw = lzmaV2 ? entry.vramCompressedSize == entry.vramUnCompressedSize
                                : CompressionIO::isStoredV1Section(in, vramOffset, entry.vramCompressedSize,
                                                                   entry.vramUnCompressedSize);
    const qint64 mainV2 = lzmaV2 ? static_cast<qint64>(mainSize) : -1;
    const qint64 vramV2 = lzmaV2 ? static_cast<qint64>(vramSize) : -1;
    const PackageStream::Endianness endian = m_isBigEndian ? PackageStream::Big : PackageStream::Little;

    // Raw copy of the (uncompressed) main section into the output folder.
    const auto copyMainTo = [&](const QString& relativePath) {
        QString relative;
        const QString fullPath = makeTarget(relativePath, &relative);
        in.seek(mainOffset);
        const auto out = openWriter(fullPath);
        copyBytes(in, *out, mainSize);
        return relative;
    };

    // Identified V1 assets (static meshes, materials): main section followed by the raw vram section
    // (a mesh keeps its vertex data in vram), the same main + vram layout BlobsetAssetReader shows.
    const auto copyMainAndVramTo = [&](const QString& relativePath, bool mainCompressed) {
        QString relative;
        const QString fullPath = makeTarget(relativePath, &relative);
        const auto out = openWriter(fullPath);
        in.seek(mainOffset);
        if (mainCompressed)
            lzmaBlockToDevice(in, *out, mainV2);
        else
            copyBytes(in, *out, mainSize);
        if (vramSize > 0) {
            in.seek(vramOffset);
            copyBytes(in, *out, vramSize);
        }
        return relative;
    };

    // Main section, then (optionally) the vram section, each one a LZMA chunk block.
    const auto lzmaTo = [&](const QString& relativePath, bool withVram) {
        QString relative;
        const QString fullPath = makeTarget(relativePath, &relative);
        const auto out = openWriter(fullPath);
        in.seek(mainOffset);
        lzmaBlockToDevice(in, *out, mainV2);
        if (withVram) {
            in.seek(vramOffset);
            lzmaBlockToDevice(in, *out, vramV2);
        }
        return relative;
    };

    // Compressed mini DDS: the main block is the header, the pixels are in the vram section
    // (or behind the header when there is no vram section).
    const auto compressedMini = [&]() {
        in.seek(mainOffset);
        const QByteArray mainData = lzmaBlockToBytes(in, mainV2);

        qint64 headerEnd = 0;
        const MiniTxpk mini = parseMini(mainData, &headerEnd);

        QByteArray pixels;
        if (!vramRaw) {
            in.seek(vramOffset);
            pixels = lzmaBlockToBytes(in, vramV2);
        } else if (vramSize > 0) {
            in.seek(vramOffset);
            pixels = in.readBytes(vramSize);
        } else {
            pixels = mainData.mid(static_cast<qsizetype>(headerEnd));
        }
        return writeMiniDds(mini, pixels);
    };

    if (mainRaw) {
        if (vramRaw) {
            // ---- main raw, vram raw ----
            if (isMiniDdsTxpk(in, mainOffset)) {
                in.seek(mainOffset);
                const MiniTxpk mini = readMini(in);
                const qint64 headerEnd = in.position();

                QByteArray pixels;
                if (vramSize > 0) {
                    in.seek(vramOffset);
                    pixels = in.readBytes(vramSize);
                } else {
                    pixels = in.readBytes(std::max<qint64>(0, mainOffset + mainSize - headerEnd));
                }
                return writeMiniDds(mini, pixels);
            }

            in.seek(mainOffset);
            const quint32 magic = in.readUInt32();

            switch (static_cast<CoreDefs::FileType>(magic)) {
            case CoreDefs::FileType::PM3M:
            case CoreDefs::FileType::M3MP:
                return copyMainTo(joinPath(u"m3mp"_s, u"uncompressed"_s, entryName(i, u"m3mp"_s)));
            case CoreDefs::FileType::FSB:
            case CoreDefs::FileType::FSB5_LE:
            case CoreDefs::FileType::FSB4:
            case CoreDefs::FileType::FSB4_BE:
                return copyMainTo(joinPath(u"sound"_s, u"fsb"_s, entryName(i, u"fsb"_s)));
            case CoreDefs::FileType::FEV1_BE:
            case CoreDefs::FileType::FEV1:
                return copyMainTo(joinPath(u"sound"_s, u"fsb"_s, fevFileName(game)));
            case CoreDefs::FileType::BANK:
            case CoreDefs::FileType::WAV: {
                const QString audioType = audioExtension(game);
                return copyMainTo(joinPath(u"sound"_s, audioType, entryName(i, audioType)));
            }
            case CoreDefs::FileType::PNG:
                return copyMainTo(joinPath(u"png"_s, entryName(i, u"png"_s)));
            case CoreDefs::FileType::KPXT:
            case CoreDefs::FileType::TXPK: { // Raw main + raw vram (mostly empty 8 byte TXPKs on RLL3 PC).
                const QString tempPath = freshTempPath(i, u"txpk"_s);
                {
                    const auto out = openWriter(tempPath);
                    in.seek(mainOffset);
                    copyBytes(in, *out, mainSize);
                    if (vramSize > 0) {
                        in.seek(vramOffset);
                        copyBytes(in, *out, vramSize);
                    }
                }
                const QString progress = extractTxpkDds(tempPath, mainSize, i);
                QFile::remove(tempPath);
                return progress;
            }
            default:
                // Models, meshes, materials, animations, index buffers (see asset_signatures.h).
                if (const auto match = AssetSignatures::identifyV1(
                        peekBytes(in, mainOffset, std::min<qint64>(mainSize, AssetSignatures::kPeekBytes))))
                    return copyMainAndVramTo(AssetSignatures::mappedPath(*match, i), false);
                if (mainSize <= AssetSignatures::kIndexBufferMaxBytes
                    && AssetSignatures::looksLikeIndexBuffer(peekBytes(in, mainOffset, mainSize), m_isBigEndian))
                    return copyMainAndVramTo(AssetSignatures::mappedPath(AssetSignatures::indexBufferMatch(), i), false);
                if (const auto match = AssetSignatures::identifyCommon(
                        peekBytes(in, mainOffset, std::min<qint64>(mainSize, AssetSignatures::kPeekBytes)), mainSize, m_isBigEndian))
                    return copyMainAndVramTo(AssetSignatures::mappedPath(*match, i), false);
                return copyMainTo(unknownPath(u"mainuncompressed_vramuncompressed"_s, i));
            }
        }

        // ---- main raw, vram compressed ----
        if (isMiniDdsTxpk(in, mainOffset)) {
            in.seek(mainOffset);
            const MiniTxpk mini = readMini(in);

            in.seek(vramOffset);
            return writeMiniDds(mini, lzmaBlockToBytes(in, vramV2));
        }

        in.seek(mainOffset);
        const quint32 magic = in.readUInt32();

        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::KPXT:
        case CoreDefs::FileType::TXPK: {
            const QString tempPath = freshTempPath(i, u"txpk"_s);
            {
                const auto out = openWriter(tempPath);
                in.seek(mainOffset);
                copyBytes(in, *out, mainSize);
                in.seek(vramOffset);
                lzmaBlockToDevice(in, *out, vramV2);
            }
            const QString progress = extractTxpkDds(tempPath, mainSize, i);
            QFile::remove(tempPath);
            return progress;
        }
        default: {
            if (game == CoreDefs::Game::TTC) {
                // TTC: the main section is the mini header, the vram section the pixels.
                in.seek(mainOffset);
                const MiniTxpk mini = parseMini(in.readBytes(mainSize));

                in.seek(vramOffset);
                return writeMiniDds(mini, lzmaBlockToBytes(in, vramV2));
            }

            QString relative;
            const QString fullPath = makeTarget(unknownPath(u"mainuncompressed_vramcompressed"_s, i), &relative);
            const auto out = openWriter(fullPath);
            in.seek(mainOffset);
            copyBytes(in, *out, mainSize);
            in.seek(vramOffset);
            lzmaBlockToDevice(in, *out, vramV2);
            return relative;
        }
        }
    }

    // ---- main compressed: the first LZMA chunk tells what it is ----
    QByteArray firstChunk;
    const quint32 magic = AssetSignatures::canonicalMagic(peekLzmaMagic(in, mainOffset, endian, firstChunk, mainV2));

    // Compressed mini DDS headers start with the width, not a magic, so check "conv" first.
    if (hasConvMarker(firstChunk))
        return compressedMini();

    if (vramRaw) {
        // ---- main compressed, vram raw ----
        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::PM3M:
        case CoreDefs::FileType::M3MP:
            return lzmaTo(joinPath(u"m3mp"_s, u"compressed"_s, entryName(i, u"m3mp"_s)), false);
        case CoreDefs::FileType::BMF:
            return lzmaTo(joinPath(u"font"_s, entryName(i, u"bmf"_s)), false);
        case CoreDefs::FileType::PNG:
            return lzmaTo(joinPath(u"png"_s, entryName(i, u"png"_s)), false);
        case CoreDefs::FileType::BSB_BE:
        case CoreDefs::FileType::BSB:
            return lzmaTo(joinPath(u"ui"_s, u"bsb"_s, entryName(i, u"bsb"_s)), false);
        case CoreDefs::FileType::MINI:
            return compressedMini();
        case CoreDefs::FileType::KPXT:
        case CoreDefs::FileType::TXPK:   // AFL Live: compressed empty (8 byte) TXPKs without a vram section.
            return lzmaTo(joinPath(u"dds_txpk"_s, entryName(i, u"txpk"_s)), false);
        default:
            // Models, meshes, materials, animations, index buffers, content formats (see asset_signatures.h).
            if (const auto match = identifyCompressedV1(in, mainOffset, mainSize, mainV2, firstChunk))
                return copyMainAndVramTo(AssetSignatures::mappedPath(*match, i), true);
            return lzmaTo(unknownPath(u"maincompressed"_s, i), false);
        }
    }

    // ---- main compressed, vram compressed ----
    switch (static_cast<CoreDefs::FileType>(magic)) {
    case CoreDefs::FileType::MINI:
        return compressedMini();
    case CoreDefs::FileType::KPXT:
    case CoreDefs::FileType::TXPK: {
        const QString tempPath = freshTempPath(i, u"txpk"_s);
        {
            const auto out = openWriter(tempPath);
            in.seek(mainOffset);
            lzmaBlockToDevice(in, *out, mainV2);
            in.seek(vramOffset);
            lzmaBlockToDevice(in, *out, vramV2);
        }
        const QString progress = extractTxpkDds(tempPath, mainSize, i);
        QFile::remove(tempPath);
        return progress;
    }
    case CoreDefs::FileType::BMF:
        return lzmaTo(joinPath(u"font"_s, entryName(i, u"bmf"_s)), true);
    case CoreDefs::FileType::PNG:
        return lzmaTo(joinPath(u"png"_s, entryName(i, u"png"_s)), true);
    case CoreDefs::FileType::BSB_BE:
    case CoreDefs::FileType::BSB:
        return lzmaTo(joinPath(u"ui"_s, u"bsb"_s, entryName(i, u"bsb"_s)), true);
    default:
        // e.g. RLL4's mesh index buffers (main) with their vertex data (vram): both are kept.
        if (const auto match = identifyCompressedV1(in, mainOffset, mainSize, mainV2, firstChunk))
            return lzmaTo(AssetSignatures::mappedPath(*match, i), true);
        return lzmaTo(unknownPath(u"maincompressed_vramcompressed"_s, i), true);
    }
}

/// Mirrors FileMappingWorker::identifyCompressedV1: signatures, then a triangle index buffer (whole main section,
/// when it's small enough), then the content formats.
std::optional<AssetSignatures::Match> ExtractWorker::identifyCompressedV1(PackageStream& in, qint64 mainOffset,
                                                                         qint64 mainSize, qint64 mainV2,
                                                                         const QByteArray& firstChunk) const {
    if (const auto match = AssetSignatures::identifyV1(firstChunk))
        return match;

    if (mainSize <= AssetSignatures::kIndexBufferMaxBytes) {
        in.seek(mainOffset);
        if (AssetSignatures::looksLikeIndexBuffer(lzmaBlockToBytes(in, mainV2), m_isBigEndian))
            return AssetSignatures::indexBufferMatch();
    }
    return AssetSignatures::identifyCommon(firstChunk, mainSize, m_isBigEndian);
}

/// Entry that couldn't be decoded: the mapping lists it as unknown\<category>\<i>.dat, so the stored bytes
/// (main section, then vram section, exactly as they sit in the blob) go there.
QString ExtractWorker::dumpLegacyEntry(PackageStream& in, const BlobsetEntry& entry, int i) {
    QString relative;
    const QString fullPath = makeTarget(unknownPath(unknownCategoryV1(entry), i), &relative);
    const auto out = openWriter(fullPath);

    in.seek(entry.mainFinalOffset);
    copyBytes(in, *out, std::min<qint64>(entry.mainCompressedSize, in.length() - in.position()));

    if (entry.vramCompressedSize > 0) {
        in.seek(entry.vramFinalOffset);
        copyBytes(in, *out, std::min<qint64>(entry.vramCompressedSize, in.length() - in.position()));
    }
    return relative;
}

void ExtractWorker::extractLegacy(CoreDefs::BlobsetVersion version) {
    const QString label = version == CoreDefs::BlobsetVersion::V2 ? u"V2"_s : u"V1"_s;
    QString currentFile = m_blobsetFile;

    try {
        BlobsetFile blobset;
        {
            QFile headerFile(m_blobsetFile);
            if (!headerFile.open(QIODevice::ReadOnly)) {
                emit taskFinished(QString("Failed to open legacy %1 Blobset file.").arg(label));
                return;
            }

            QDataStream headerStream(&headerFile);
            if (!blobset.read(headerStream, version, m_isBigEndian, m_gameId)) {
                emit taskFinished("Invalid BLOB Magic Signature.");
                return;
            }
        }

        const PackageStream::Endianness endian = m_isBigEndian ? PackageStream::Big : PackageStream::Little;
        const int fileCount = static_cast<int>(blobset.filesCount);
        constexpr quint32 kNoBlob = std::numeric_limits<quint32>::max();

        std::unique_ptr<PackageStream> blob; // One blob file serves many entries, keep it open.
        quint32 openBlob = kNoBlob;
        QSet<quint32> missingBlobs;          // Blob containers that don't exist (reported once).

        for (int i = 0; i < fileCount; ++i) {
            const BlobsetEntry& entry = blobset.entries[i];
            QString progress;

            const bool blobMissing = missingBlobs.contains(entry.blobSetNumber);

            if (!blobMissing && (!blob || openBlob != entry.blobSetNumber)) {
                blob.reset();
                currentFile = blobPathFor(entry.blobSetNumber);

                if (QFile::exists(currentFile)) {
                    blob = std::make_unique<PackageStream>(currentFile, endian, QIODevice::ReadOnly);
                    openBlob = entry.blobSetNumber;
                } else {
                    missingBlobs.insert(entry.blobSetNumber);
                    emit updateConsole(QString("Warning: blob container not found, its entries are skipped: %1").arg(currentFile));
                }
            }

            if (blob && !missingBlobs.contains(entry.blobSetNumber)) {
                // Same rule as the mapping: an entry that can't be decoded doesn't stop the extraction,
                // it lands in the unknown folder the mapping put it in.
                try {
                    progress = extractLegacyEntry(*blob, entry, i);
                } catch (const std::exception& ex) {
                    emit updateConsole(QString("Warning: entry %1 couldn't be decoded (%2), extracted as unknown.")
                                           .arg(i).arg(QString::fromUtf8(ex.what())));
                    try {
                        progress = dumpLegacyEntry(*blob, entry, i);
                    } catch (const std::exception& dumpError) {
                        emit updateConsole(QString("Warning: entry %1 skipped: %2").arg(i).arg(QString::fromUtf8(dumpError.what())));
                    }
                }
            }

            emitProgress(static_cast<int>((static_cast<qint64>(i) + 1) * 100 / fileCount));
            if (!progress.isEmpty())
                emit updateConsole(QString("Extracted: %1").arg(progress));
        }
    } catch (const std::exception& ex) {
        emit taskFinished(QString("Extraction failed.\n\nFile: %1\n\n%2").arg(currentFile, QString::fromUtf8(ex.what())));
        return;
    }

    emit taskSucceeded();
    emit taskFinished(QString("%1 Extraction Sequence Completed.").arg(label));
}

// ===========================================================================
// Extract Blobset Version 4 (mirrors FileMappingWorker::resolveV4)
// ===========================================================================

std::optional<AssetSignatures::Match> ExtractWorker::identifyAsset(const QByteArray& data, qint64 mainSize) const {
    if (const auto match = AssetSignatures::identifyV4(data, mainSize))
        return match;
    return m_lzmaV3 ? AssetSignatures::identifyV1(data) : std::nullopt;
}

QString ExtractWorker::extractV4Entry(PackageStream& in, const QString& filePath, const BlobsetEntry& entry, int i) {
    const qint64 mainCompressedSize = entry.mainCompressedSize;

    // V3: the chunk helpers need this entry's section sizes.
    m_v3MainCompressed = entry.mainCompressedSize;
    m_v3MainSize = entry.mainUnCompressedSize;
    m_v3VramSize = entry.vramUnCompressedSize;
    const int mainSize = static_cast<int>(entry.mainUnCompressedSize);
    const bool mainRaw = entry.mainCompressedSize == entry.mainUnCompressedSize;
    const bool vramRaw = entry.vramCompressedSize == entry.vramUnCompressedSize;

    // Copies the whole physical file.
    const auto copyFileTo = [&](const QString& relativePath) {
        QString relative;
        const QString fullPath = makeTarget(relativePath, &relative);
        const auto out = openWriter(fullPath);
        in.seek(0);
        copyBytes(in, *out, in.length());
        return relative;
    };

    // Main chunks take the first mainCompressedSize bytes of the file; vram chunks follow.
    const auto mainChunksTo = [&](QIODevice& out) {
        in.seek(0);
        zstdChunksToDevice(in, out, mainCompressedSize);
    };
    const auto mainChunksToFile = [&](const QString& relativePath, bool withVram) {
        QString relative;
        const QString fullPath = makeTarget(relativePath, &relative);
        const auto out = openWriter(fullPath);
        mainChunksTo(*out);
        if (withVram) {
            in.seek(mainCompressedSize);
            zstdChunksToDevice(in, *out, -1);
        }
        return relative;
    };

    if (in.length() <= 4)
        return copyFileTo(unknownPath(u"mainuncompressed"_s, i));

    if (mainRaw) {
        if (vramRaw) {
            // ---- main raw, vram raw ----
            if (isMiniDdsTxpk(in, 0)) {
                in.seek(0);
                const MiniTxpk mini = readMini(in);
                return writeMiniDds(mini, in.readBytes(in.length() - in.position()));
            }

            in.seek(0);
            const quint32 magic = in.readUInt32();

            switch (static_cast<CoreDefs::FileType>(magic)) {
            case CoreDefs::FileType::M3MP:
                return copyFileTo(joinPath(u"m3mp"_s, u"uncompressed"_s, entryName(i, u"m3mp"_s)));
            case CoreDefs::FileType::WiseBNK:
                return copyFileTo(joinPath(u"sound"_s, u"bnk"_s, entryName(i, u"bnk"_s)));
            case CoreDefs::FileType::WiseWEM:
                return copyFileTo(joinPath(u"sound"_s, u"wem"_s, entryName(i, u"wem"_s)));
            case CoreDefs::FileType::TXPK:
                return extractTxpkDds(filePath, mainSize, i); // Raw main + raw vram: the file itself is the TXPK.
            default:
                // Models, texture lists, ... (see asset_signatures.h).
                if (const auto match = identifyAsset(peekBytes(in, 0, AssetSignatures::kPeekBytes), mainSize))
                    return copyFileTo(AssetSignatures::mappedPath(*match, i));
                return copyFileTo(unknownPath(u"mainuncompressed_vramuncompressed"_s, i));
            }
        }

        // ---- main raw, vram compressed ----
        if (isMiniDdsTxpk(in, 0)) {
            in.seek(0);
            const MiniTxpk mini = readMini(in);

            in.seek(mainSize);
            return writeMiniDds(mini, zstdChunksToBytes(in, -1));
        }

        in.seek(0);
        const quint32 magic = in.readUInt32();

        const auto rawMainZstdVramTo = [&](const QString& path) {
            const auto out = openWriter(path);
            in.seek(0);
            copyBytes(in, *out, mainSize);
            zstdChunksToDevice(in, *out, -1);
        };

        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::TXPK: {
            const QString tempPath = freshTempPath(i, u"txpk"_s);
            rawMainZstdVramTo(tempPath);
            const QString progress = extractTxpkDds(tempPath, mainSize, i);
            QFile::remove(tempPath);
            return progress;
        }
        default: {
            QString relative;
            if (const auto match = identifyAsset(peekBytes(in, 0, AssetSignatures::kPeekBytes), mainSize)) {
                rawMainZstdVramTo(makeTarget(AssetSignatures::mappedPath(*match, i), &relative));
                return relative;
            }
            rawMainZstdVramTo(makeTarget(unknownPath(u"mainuncompressed_vramcompressed"_s, i), &relative));
            return relative;
        }
        }
    }

    // ---- main compressed: the first ZSTD chunk tells what it is ----
    in.seek(0);
    QByteArray firstChunk;
    {
        QBuffer buffer(&firstChunk);
        buffer.open(QIODevice::WriteOnly);
        zstdChunkToDevice(in, buffer);
    }

    // Compressed mini DDS: the main section is the header, the pixels follow as vram chunks
    // (or as a raw vram section, or behind the header when there is no vram section).
    const auto compressedMini = [&]() {
        const bool pixelsInMain = vramRaw && entry.vramUnCompressedSize == 0;
        QByteArray mainData = firstChunk;
        if (pixelsInMain) {
            in.seek(0);
            mainData = zstdChunksToBytes(in, mainCompressedSize);
        }

        qint64 headerEnd = 0;
        const MiniTxpk mini = parseMini(mainData, &headerEnd);

        QByteArray pixels;
        if (pixelsInMain) {
            pixels = mainData.mid(static_cast<qsizetype>(headerEnd));
        } else {
            in.seek(mainCompressedSize);
            pixels = vramRaw ? in.readBytes(in.length() - in.position()) : zstdChunksToBytes(in, -1);
        }
        return writeMiniDds(mini, pixels);
    };

    // Compressed mini DDS headers start with the width, not a magic, so check "conv" first.
    if (hasConvMarker(firstChunk))
        return compressedMini();

    const quint32 magic = peekMagic(firstChunk, PackageStream::Little);

    if (vramRaw) {
        // ---- main compressed, vram raw ----
        switch (static_cast<CoreDefs::FileType>(magic)) {
        case CoreDefs::FileType::M3MP:
            return mainChunksToFile(joinPath(u"m3mp"_s, u"compressed"_s, entryName(i, u"m3mp"_s)), false);
        default:
            // Models, texture lists, ... (see asset_signatures.h).
            if (const auto match = identifyAsset(firstChunk, mainSize))
                return mainChunksToFile(AssetSignatures::mappedPath(*match, i), false);
            return mainChunksToFile(unknownPath(u"maincompressed_vramuncompressed"_s, i), false);
        }
    }

    // ---- main compressed, vram compressed ----
    switch (static_cast<CoreDefs::FileType>(magic)) {
    case CoreDefs::FileType::TXPK: {
        const QString tempPath = freshTempPath(i, u"txpk"_s);
        {
            const auto out = openWriter(tempPath);
            mainChunksTo(*out);
            in.seek(mainCompressedSize);
            zstdChunksToDevice(in, *out, -1);
        }
        const QString progress = extractTxpkDds(tempPath, mainSize, i);
        QFile::remove(tempPath);
        return progress;
    }
    case CoreDefs::FileType::MINI:
        return compressedMini();
    default:
        // e.g. cinematic animations, whose keys live in the vram section (kept, like the unknown dump does).
        if (const auto match = identifyAsset(firstChunk, mainSize))
            return mainChunksToFile(AssetSignatures::mappedPath(*match, i), true);
        return mainChunksToFile(unknownPath(u"maincompressed_vramcompressed"_s, i), true);
    }
}

void ExtractWorker::extractV4() {
    QString currentFile = m_blobsetFile;

    try {
        BlobsetFile blobset;
        {
            QFile headerFile(m_blobsetFile);
            if (!headerFile.open(QIODevice::ReadOnly)) {
                emit taskFinished(m_lzmaV3 ? "Failed to open V3 Blobset file." : "Failed to open modern V4 Blobset file.");
                return;
            }

            QDataStream headerStream(&headerFile);
            const auto layout = m_lzmaV3 ? CoreDefs::BlobsetVersion::V3 : CoreDefs::BlobsetVersion::V4;
            if (!blobset.read(headerStream, layout, m_isBigEndian, m_gameId)) {
                emit taskFinished("Invalid BLOB Magic Signature.");
                return;
            }
        }

        const int fileCount = static_cast<int>(blobset.filesCount);

        for (int i = 0; i < fileCount; ++i) {
            const BlobsetEntry& entry = blobset.entries[i];
            QString progress;

            // V4 archives keep every asset in its own physical file, addressed by the hashes.
            // The Mod Loader view (header in <game>\BlobsetMods\data): the modded file, else the game's own.
            currentFile = BlobsetAssetReader::dataFilePath(m_blobsetFile, entry);

            if (QFile::exists(currentFile)) {
                try {
                    PackageStream in(currentFile, PackageStream::Little, QIODevice::ReadOnly);
                    progress = extractV4Entry(in, currentFile, entry, i);
                } catch (const std::exception& ex) {
                    // One broken entry doesn't stop the rest of the extraction.
                    emit updateConsole(QString("Warning: entry %1 couldn't be extracted (%2): %3")
                                           .arg(i).arg(currentFile, QString::fromUtf8(ex.what())));
                }
            }

            emitProgress(static_cast<int>((static_cast<qint64>(i) + 1) * 100 / fileCount));
            if (!progress.isEmpty())
                emit updateStatus(QString("Extracting File: %1").arg(progress));
        }
    } catch (const std::exception& ex) {
        emit taskFinished(QString("Extraction failed.\n\nFile: %1\n\n%2").arg(currentFile, QString::fromUtf8(ex.what())));
        return;
    }

    emit taskSucceeded();
    emit taskFinished(m_lzmaV3 ? "V3 Extraction Sequence Completed." : "V4 Extraction Sequence Completed.");
}
