#include "create_worker.h"

#include "blobset_file.h"
#include "bsb_file.h"
#include "compression_io.h"
#include "core_defs.h"
#include "file_mapping_worker.h"
#include "hash_utility.h"
#include "mini_txpk_file.h"
#include "package_stream.h"
#include "x360_texture.h"
#include "utilities.h"
#include "xml_models.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// ---------------------------------------------------------------------------
// Format constants
// ---------------------------------------------------------------------------

constexpr qint64 kLzmaChunkSize = 32768;     // V1: 32 KB chunks
constexpr qint64 kCopyBlockSize = 1 << 20;

// V1 header: [sha1 (optional)] magic, blobset count, files count, then one 32 byte entry per file.
constexpr qsizetype kSha1Size = 20;
constexpr qsizetype kV1HeaderSize = 12;
constexpr qsizetype kV1EntrySize = 32;
constexpr quint32 kV1BlobsetCount = 1;       // A created blobset only holds its own data.
constexpr quint32 kV1BlobsetNumber = 0;

constexpr quint32 kBlobMagic = 1112493122;   // 'BLOB'
constexpr quint32 kBolbMagic = 1112297282;   // 'BOLB' (what the C# tool writes)
constexpr quint32 kDdsMagic = 0x20534444;    // 'DDS '
constexpr qsizetype kDdsHeaderSize = 128;

// Same order the C# tool packs the files in.
const QStringList kV1Extensions = {u"dds"_s, u"txpk"_s, u"m3mp"_s, u"bsb"_s, u"bmf"_s,
                                   u"bank"_s, u"fsb"_s, u"fev"_s, u"wav"_s, u"dat"_s};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QString describeFailure(const QString& currentFile, const std::exception& ex) {
    QString message = u"Blobset creation failed.\n\n"_s;
    if (!currentFile.isEmpty())
        message += u"File: %1\n\n"_s.arg(currentFile);
    return message + QString::fromUtf8(ex.what());
}

/// Mapping keys and mod paths are compared case insensitive with '/' separators.
QString normalizedKey(QString path) {
    path.replace(u'\\', u'/');
    return path.toLower();
}

quint32 toU32(qint64 value, const QString& what) {
    if (value < 0 || value > std::numeric_limits<quint32>::max())
        fail(u"%1 doesn't fit into the 32 bit blobset header (%2)."_s.arg(what).arg(value));
    return static_cast<quint32>(value);
}

QByteArray readWholeFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(u"Could not open '%1': %2"_s.arg(path, file.errorString()));
    return file.readAll();
}

PackageStream::Endianness endianFor(bool isBigEndian) {
    return isBigEndian ? PackageStream::Big : PackageStream::Little;
}

/// "<folder><file>" hash string (2 + 6 hex characters) -> the 32 bit path hash stored in a V1 header.
quint32 parsePathHash(const QString& folderHash, const QString& fileHash, const QString& what) {
    bool ok = false;
    const quint32 hash = (folderHash + fileHash).toUInt(&ok, 16);
    if (!ok)
        fail(u"%1 - invalid folder/file hash '%2/%3'."_s.arg(what, folderHash, fileHash));
    return hash;
}

/// Where the optional 20 byte SHA1 ends and the "BLOB"/"BOLB" magic starts (0 or 20).
qsizetype probeHashSize(const QByteArray& header, bool isBigEndian) {
    const auto magicAt = [&](qsizetype offset) -> quint32 {
        if (header.size() < offset + 4)
            return 0;
        return isBigEndian ? qFromBigEndian<quint32>(header.constData() + offset)
                           : qFromLittleEndian<quint32>(header.constData() + offset);
    };
    const auto isMagic = [](quint32 magic) { return magic == kBlobMagic || magic == kBolbMagic; };

    if (isMagic(magicAt(0)))
        return 0;
    if (isMagic(magicAt(kSha1Size)))
        return kSha1Size;

    fail(u"The source blobset has an invalid BLOB magic signature."_s);
}

} // namespace

// ==============================================================================================================
// CreateWorker
// ==============================================================================================================

CreateWorker::CreateWorker(const QString& outputBlobset, int version, bool isBigEndian, const QString& modsDir,
                           const QString& mappingFile, const QString& sourceBlobset, int gameId, int platform,
                           QObject* parent)
    : QObject(parent),
    m_outputBlobset(outputBlobset),
    m_version(version),
    m_isBigEndian(isBigEndian),
    m_modsDir(modsDir),
    m_mappingFile(mappingFile),
    m_gameId(gameId),
    m_platform(platform),
    m_sourceBlobset(sourceBlobset) {}

void CreateWorker::create_blobset() {
    emit updateConsole(u"Initializing complete archive synthesis..."_s);

    // m_version is the CoreDefs::BlobsetVersion value: 0 = V1, 1 = V2 (same layout), 2 = V3, 3 = V4.
    switch (static_cast<CoreDefs::BlobsetVersion>(m_version)) {
    case CoreDefs::BlobsetVersion::V1:
        createV1();
        break;
    case CoreDefs::BlobsetVersion::V2:
        // V1's layout; only the LZMA sections differ (no chunk table, see CompressionIO::buildLzmaSectionV2).
        m_lzmaV2 = true;
        createV1();
        m_lzmaV2 = false;
        break;
    default:
        emit taskFinished(u"Creation is only supported for V1/V2 blobsets currently."_s);
        break;
    }
}

// ---------------------------------------------------------------------------
// Planning
// ---------------------------------------------------------------------------

/// File mapping layout (little endian): u32 count, then per entry: u8 pathLength, path (latin1), u64 hash.
QHash<QString, quint64> CreateWorker::loadMappingData() const {
    QHash<QString, quint64> mapping;

    // PackageStream opens read/write (which would create a missing file), so check first.
    if (!QFile::exists(m_mappingFile))
        return mapping;

    try {
        PackageStream in(m_mappingFile, PackageStream::Little);

        const quint32 count = in.readUInt32();
        mapping.reserve(qMin<quint32>(count, 1'000'000));

        for (quint32 i = 0; i < count; ++i) {
            const quint8 pathLength = in.readByte();
            const QByteArray path = in.readBytes(pathLength);
            const quint64 hash = in.readUInt64();

            mapping.insert(normalizedKey(QString::fromLatin1(path)), hash);
        }
    } catch (const std::exception&) {
        return {};   // Missing / truncated mapping -> reported as "corrupted" by the caller.
    }

    return mapping;
}

QList<CreateWorker::ModFile> CreateWorker::collectModFiles(const QStringList& extensions) const {
    const QDir modsDir(m_modsDir);
    QList<ModFile> files;

    // Grouped by extension (in the order given), alphabetical inside each group, so every run produces the same layout.
    for (const QString& extension : extensions) {
        QStringList found;
        QDirIterator it(modsDir.absolutePath(), {u"*."_s + extension}, QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            found.append(it.next());

        found.sort(Qt::CaseInsensitive);

        for (const QString& path : std::as_const(found))
            files.append(ModFile{path, modsDir.relativeFilePath(path), extension});
    }

    return files;
}

/// Reads the original blobset header (needed for ".dat" files and xml infos without hashes).
BlobsetFile CreateWorker::loadSourceHeader() const {
    if (m_sourceBlobset.isEmpty() || !QFile::exists(m_sourceBlobset))
        fail(u"The original blobset header is needed for this file but couldn't be found: "_s + m_sourceBlobset);

    QFile file(m_sourceBlobset);
    if (!file.open(QIODevice::ReadOnly))
        fail(u"Could not open '%1': %2"_s.arg(m_sourceBlobset, file.errorString()));

    const qsizetype hashSize = probeHashSize(file.read(kSha1Size + 4), m_isBigEndian);
    file.seek(0);

    QDataStream in(&file);
    BlobsetFile source;

    // BlobsetFile only needs to know whether the SHA1 is there (it keys that off the game id).
    if (!source.read(in, CoreDefs::BlobsetVersion::V1, m_isBigEndian, hashSize > 0 ? 1 : 0))
        fail(u"The source blobset has an invalid BLOB magic signature."_s);

    return source;
}

QList<CreateWorker::ModJob> CreateWorker::planJobs(const QStringList& extensions,
                                                   const QHash<QString, quint64>& mapping) const {
    QList<ModJob> jobs;
    QHash<quint32, QString> claimedBy;
    std::optional<BlobsetFile> source;   // Loaded on demand.

    const auto sourceHeader = [&]() -> const BlobsetFile& {
        if (!source)
            source = loadSourceHeader();
        return *source;
    };

    for (const ModFile& file : collectModFiles(extensions)) {
        ModJob job;
        job.absolutePath = file.absolutePath;
        job.relativePath = file.relativePath;
        job.extension = file.extension;

        if (file.extension == u"txpk"_s || file.extension == u"m3mp"_s) {
            // These carry their hashes and section sizes in a sidecar xml.
            const QFileInfo info(file.absolutePath);
            const QString xmlPath = info.absolutePath() + u'/' + info.completeBaseName() + u".xml"_s;

            if (!QFile::exists(xmlPath))
                fail(u"Can't find the %1 xml info: %2"_s.arg(file.extension.toUpper(), xmlPath));

            XmlModifyFileInfo xml;
            if (!xml.deserialize(xmlPath))
                fail(u"%1 - XmlDeserialize failed."_s.arg(xmlPath));

            if (!xml.folderHash.isEmpty() && !xml.fileNameHash.isEmpty()) {
                job.pathHash = parsePathHash(xml.folderHash, xml.fileNameHash, file.relativePath);
            } else {
                // No hash in the xml: fall back to the original blobset entry it points at.
                const BlobsetFile& original = sourceHeader();
                if (xml.index >= static_cast<quint32>(original.entries.size()))
                    fail(u"%1 - blobset index %2 is out of range (the blobset has %3 files)."_s
                             .arg(file.relativePath).arg(xml.index).arg(original.entries.size()));
                job.pathHash = original.entries[xml.index].folderFileHash32;
            }

            job.mainUncompressed = xml.mainUnCompressedSize;
            job.vramUncompressed = xml.vramUnCompressedSize;
        } else if (file.extension == u"dat"_s) {
            // Unknown assets are extracted as "<index>.dat" and keep whatever the original entry looked like.
            bool ok = false;
            quint32 index = QFileInfo(file.absolutePath).completeBaseName().toUInt(&ok);
            const BlobsetFile& original = sourceHeader();
            if (!ok) {
                // A named .dat (Resolve File Names): find its original entry through the mapping or its name's hash.
                const auto mapped = mapping.constFind(normalizedKey(file.relativePath));
                const quint64 hash = mapped != mapping.constEnd()
                                         ? mapped.value()
                                         : BlobsetHash::hashForFile(m_version, file.relativePath, m_platform, QString());
                const auto found = std::find_if(original.entries.cbegin(), original.entries.cend(),
                                                [&](const BlobsetEntry& e) { return e.folderFileHash32 == hash; });
                if (found == original.entries.cend())
                    fail(u"%1 - a .dat file has to be named after its blobset index (e.g. 123.dat) or a mapped name; "
                         u"new assets need a known type (e.g. .dds)."_s.arg(file.relativePath));
                index = static_cast<quint32>(found - original.entries.cbegin());
            }

            if (index >= static_cast<quint32>(original.entries.size()))
                fail(u"%1 - blobset index %2 is out of range (the blobset has %3 files)."_s
                         .arg(file.relativePath).arg(index).arg(original.entries.size()));

            const BlobsetEntry& entry = original.entries[index];
            job.pathHash = entry.folderFileHash32;
            job.origMainCompressed = entry.mainCompressedSize;
            job.origMainUncompressed = entry.mainUnCompressedSize;
            job.origVramCompressed = entry.vramCompressedSize;
            job.origVramUncompressed = entry.vramUnCompressedSize;
        } else {
            // Everything else is located through the file mapping.
            const auto mapped = mapping.constFind(normalizedKey(file.relativePath));
            if (mapped == mapping.constEnd()) {
                // Not mapped: a new asset. The game finds it by the hash of its name (see hash_utility.h).
                job.pathHash = static_cast<quint32>(
                    BlobsetHash::hashForFile(m_version, file.relativePath, m_platform, QString()));
                job.isNew = true;
            } else {
                if (mapped.value() > std::numeric_limits<quint32>::max())
                    fail(u"%1 - the mapped hash isn't a 32 bit V1 hash."_s.arg(file.relativePath));
                job.pathHash = static_cast<quint32>(mapped.value());
            }
        }

        // Two files with the same hash would produce two entries for one asset.
        const auto claimed = claimedBy.constFind(job.pathHash);
        if (claimed != claimedBy.constEnd())
            fail(u"%1 and %2 both use the same path hash."_s.arg(claimed.value(), file.relativePath));

        claimedBy.insert(job.pathHash, file.relativePath);
        jobs.append(job);
    }

    return jobs;
}

// ---------------------------------------------------------------------------
// Mini TXPK (single DDS) header
// ---------------------------------------------------------------------------

QString CreateWorker::miniPathFor(const QString& relativePath) const {
    // The marker is "conv" + "ant2-temp-intermediate" (see the "conv" check in the file mapping / extract workers).
    const QString prefix = u"convant2-temp-intermediate"_s;

    QString path = relativePath;
    path.replace(u'/', u'\\');   // V1 stores '\' separators.
    return prefix + u'\\' + path;
}

QByteArray CreateWorker::buildMiniHeader(const ModJob& job, const QByteArray& dds, QByteArray& vram) const {
    if (dds.size() < kDdsHeaderSize || qFromLittleEndian<quint32>(dds.constData()) != kDdsMagic)
        fail(u"%1 is not a valid DDS file."_s.arg(job.relativePath));

    MiniTxpk mini;
    mini.ddsHeight = qFromLittleEndian<quint32>(dds.constData() + 12);
    mini.ddsWidth = qFromLittleEndian<quint32>(dds.constData() + 16);
    mini.ddsFilePath = miniPathFor(job.relativePath);

    vram = dds; // PC: the vram section is the whole .dds file.

    if (m_isBigEndian) {
        if (m_platform == static_cast<int>(CoreDefs::Platforms::PS3)) {
            // The RSX texture descriptor is built from the .dds itself (no header dump needed), and the
            // pixels are converted to the layout it describes.
            QString error;
            const std::optional<Gcm::ImportedTexture> texture = Gcm::fromDds(dds, &error);
            if (!texture)
                fail(u"%1: %2"_s.arg(job.relativePath, error));
            if (!texture->note.isEmpty())
                emit const_cast<CreateWorker*>(this)->updateConsole(u"%1: %2"_s.arg(job.relativePath, texture->note));

            mini.gcm = texture->texture;
            mini.layoutConsoleHeader(true);
            vram = texture->pixels;
        } else {
            // Xbox 360: the D3D texture header is built from the .dds and the data tiled / byte swapped to match.
            QString error;
            const std::optional<X360::ImportedTexture> texture = X360::fromDds(dds, &error);
            if (!texture)
                fail(u"%1: %2"_s.arg(job.relativePath, error));

            mini.isXbox360 = true;
            mini.x360 = texture->header;
            mini.layoutConsoleHeader(true);   // The games' Xbox 360 headers are 16 byte aligned too.
            vram = texture->data;
        }
    }

    return mini.writeAndReadHeader(m_isBigEndian);
}

// ---------------------------------------------------------------------------
// Section writers (all of them write through PackageStream)
// ---------------------------------------------------------------------------

/// Stores the data as it is.
CreateWorker::Section CreateWorker::writeRaw(PackageStream& out, const QByteArray& data) {
    Section section;
    section.offset = toU32(out.position(), u"Container offset"_s);
    section.compressedSize = section.uncompressedSize = toU32(data.size(), u"Section size"_s);

    out.writeBytes(data);
    return section;
}

/// Streams a file into the container as it is.
CreateWorker::Section CreateWorker::appendFile(PackageStream& out, const QString& path) {
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        fail(u"Could not open '%1': %2"_s.arg(path, in.errorString()));

    Section section;
    section.offset = toU32(out.position(), u"Container offset"_s);

    while (!in.atEnd()) {
        const QByteArray block = in.read(kCopyBlockSize);
        if (block.isEmpty())
            fail(u"Failed reading '%1': %2"_s.arg(path, in.errorString()));
        out.writeBytes(block);
    }

    section.compressedSize = section.uncompressedSize = toU32(out.position() - section.offset, u"Section size"_s);
    return section;
}

/// V1 chunk block: count, [compressed size + 4] * count, then per chunk: uncompressed size + data.
/// A chunk that doesn't shrink is stored as it is (compressed size == uncompressed size), like the extractor expects.
/// V2 (lzmaV2): self describing 32 KB chunks, no table (the chunk size argument doesn't apply).
CreateWorker::Section CreateWorker::writeLzma(PackageStream& out, const QByteArray& data, qint64 chunkSize,
                                              bool isBigEndian, bool lzmaV2) {
    Section section;
    section.offset = toU32(out.position(), u"Container offset"_s);
    section.uncompressedSize = toU32(data.size(), u"Section size"_s);

    if (data.isEmpty())
        return section;

    if (lzmaV2) {
        const QByteArray block = CompressionIO::buildLzmaSectionV2(data, isBigEndian);
        out.writeBytes(block);
        section.compressedSize = toU32(block.size(), u"Section size"_s);
        return section;
    }

    const qint64 chunkCount = Utilities::ChunkAmount(data.size(), chunkSize);
    const QList<qint64> chunkSizes = Utilities::ChunkSizes(data.size(), chunkCount, chunkSize);

    QList<QByteArray> payloads;
    payloads.reserve(chunkSizes.size());

    qint64 offset = 0;
    for (const qint64 size : chunkSizes) {
        const QByteArray chunk = data.mid(offset, size);
        offset += size;

        const QByteArray packed = CompressionIO::compressLzma(chunk);
        if (packed.isEmpty())
            fail(u"LZMA compression failed."_s);

        payloads.append(packed.size() < chunk.size() ? packed : chunk);
    }

    // The block is assembled in memory (with the right byte order) so the section size is known before it's written.
    QByteArray block;
    QBuffer buffer(&block);
    buffer.open(QIODevice::WriteOnly);
    PackageStream blockStream(&buffer, endianFor(isBigEndian));

    blockStream.writeUInt32(static_cast<quint32>(chunkSizes.size()));
    for (const QByteArray& payload : std::as_const(payloads))
        blockStream.writeUInt32(static_cast<quint32>(payload.size() + 4));
    for (qsizetype j = 0; j < payloads.size(); ++j) {
        blockStream.writeUInt32(static_cast<quint32>(chunkSizes[j]));
        blockStream.writeBytes(payloads[j]);
    }

    out.writeBytes(block);
    section.compressedSize = toU32(block.size(), u"Section size"_s);
    return section;
}

// ---------------------------------------------------------------------------
// Create Blobset Version 1
// ---------------------------------------------------------------------------

/// SHA1 slot: AFLL has none, TableTop Cricket has none on big endian, everything else reserves 20 bytes.
qsizetype CreateWorker::hashSizeV1() const {
    if (m_gameId == static_cast<int>(CoreDefs::Game::AFLL))
        return 0;
    if (m_gameId == static_cast<int>(CoreDefs::Game::TTC) && m_isBigEndian)
        return 0;
    return kSha1Size;
}

CreateWorker::EntryRecord CreateWorker::writeJobV1(PackageStream& out, const ModJob& job) const {
    EntryRecord record;
    record.pathHash = job.pathHash;

    if (job.extension == u"dds"_s) {
        // main = mini TXPK header, vram = the texture data (PC: the whole .dds, consoles: converted pixels).
        const QByteArray dds = readWholeFile(job.absolutePath);
        QByteArray pixels;
        const QByteArray miniHeader = buildMiniHeader(job, dds, pixels);

        record.mainPart = writeLzma(out, miniHeader, miniHeader.size(), m_isBigEndian, m_lzmaV2);
        record.vram = writeLzma(out, pixels, kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
    } else if (job.extension == u"txpk"_s) {
        const QByteArray data = readWholeFile(job.absolutePath);
        const qint64 mainSize = job.mainUncompressed;
        const qint64 vramSize = job.vramUncompressed;

        if (data.size() < mainSize + vramSize)
            fail(u"%1 is smaller than its xml info says (%2 bytes expected)."_s
                     .arg(job.relativePath).arg(mainSize + vramSize));

        record.mainPart = writeLzma(out, data.left(mainSize), kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
        record.vram = writeLzma(out, data.mid(mainSize, vramSize), kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
    } else if (job.extension == u"dat"_s) {
        const QByteArray data = readWholeFile(job.absolutePath);
        const qint64 mainSize = job.origMainUncompressed;
        const qint64 vramSize = job.origVramUncompressed;

        if (data.size() < mainSize + vramSize)
            fail(u"%1 is smaller than the original entry (%2 bytes expected)."_s
                     .arg(job.relativePath).arg(mainSize + vramSize));

        const QByteArray mainData = data.left(mainSize);
        const QByteArray vramData = data.mid(mainSize, vramSize);

        record.mainPart = (job.origMainCompressed == job.origMainUncompressed)
                          ? writeRaw(out, mainData)
                          : writeLzma(out, mainData, kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
        record.vram = (job.origVramCompressed == job.origVramUncompressed)
                          ? writeRaw(out, vramData)
                          : writeLzma(out, vramData, kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
    } else if (const QByteArray bsb = job.extension == u"bsb"_s ? readWholeFile(job.absolutePath) : QByteArray();
               Bsb::mainSectionSize(bsb) > 0) {
        // BSB (Extract / the BSB Creator write main + vram in one file): split again by its header.
        const qint64 mainSize = Bsb::mainSectionSize(bsb);
        const QByteArray vram = bsb.mid(mainSize);
        record.mainPart = writeLzma(out, bsb.left(mainSize), kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
        record.vram = vram.isEmpty() ? writeRaw(out, QByteArray())
                                     : writeLzma(out, vram, kLzmaChunkSize, m_isBigEndian, m_lzmaV2);
    } else {
        // m3mp, bmf, bank, fsb, fev, wav: stored as they are, no vram section.
        record.mainPart = appendFile(out, job.absolutePath);
        record.vram = writeRaw(out, QByteArray());
    }

    return record;
}

void CreateWorker::writeHeaderV1(PackageStream& out, const QList<EntryRecord>& entries, qsizetype hashSize) const {
    out.setEndianness(endianFor(m_isBigEndian));
    out.seek(0);

    if (hashSize > 0)
        out.writeBytes(QByteArray(hashSize, '\0'));   // Blank SHA1

    out.writeUInt32(kBolbMagic);
    out.writeUInt32(kV1BlobsetCount);
    out.writeUInt32(static_cast<quint32>(entries.size()));

    // The games' own headers are sorted by path hash (the lookup relies on it).
    QList<EntryRecord> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const EntryRecord& a, const EntryRecord& b) { return a.pathHash < b.pathHash; });

    for (const EntryRecord& e : std::as_const(sorted)) {
        out.writeUInt32(e.mainPart.offset);
        out.writeUInt32(e.mainPart.compressedSize);
        out.writeUInt32(e.mainPart.uncompressedSize);
        out.writeUInt32(e.vram.offset);
        out.writeUInt32(e.vram.compressedSize);
        out.writeUInt32(e.vram.uncompressedSize);
        out.writeUInt32(e.pathHash);
        out.writeUInt32(kV1BlobsetNumber);
    }
}

void CreateWorker::createV1() {
    QString currentFile;

    // The container is built next to the target and only replaces it once everything succeeded,
    // so a failure never destroys an existing blobset.
    const QString tempPath = m_outputBlobset + u".creating"_s;
    std::unique_ptr<PackageStream> out;

    try {
        emit updateConsole(u"Compiling full V1 index registry..."_s);

        const QHash<QString, quint64> mapping = loadMappingData();
        if (mapping.isEmpty()) {
            emit taskFinished(u"Mapping data is missing or corrupted. Try 'Update File Mapping Data' in Settings "
                              u"or validate the game files."_s);
            return;
        }

        // Keep the original blobset before the first Create ever replaces it. Later runs leave that copy alone,
        // so it stays the untouched original (it's also where ".dat" files read their original entries from).
        if (!m_sourceBlobset.isEmpty() && !QFile::exists(m_sourceBlobset) && QFile::exists(m_outputBlobset)) {
            currentFile = m_sourceBlobset;
            emit updateStatus(u"Backing up the original blobset..."_s);
            emit updateConsole(u"Backing up the original blobset to: %1"_s.arg(QDir::toNativeSeparators(m_sourceBlobset)));
            if (!QDir().mkpath(QFileInfo(m_sourceBlobset).absolutePath()))
                fail(u"Could not create the backup folder for: "_s + m_sourceBlobset);
            if (!QFile::copy(m_outputBlobset, m_sourceBlobset)) {
                QFile::remove(m_sourceBlobset);   // Never leave a partial copy that later runs would trust.
                fail(u"Could not back up the original blobset (is there enough free space?)."_s);
            }
            emit updateConsole(u"Original blobset backed up."_s);
        }

        // Everything is validated (and the source header read) before the output is touched.
        const QList<ModJob> jobs = planJobs(kV1Extensions, mapping);
        if (jobs.isEmpty()) {
            emit taskFinished(u"No mod files detected in workspace."_s);
            return;
        }

        const qsizetype hashSize = hashSizeV1();
        const qsizetype headerDataSize = hashSize + kV1HeaderSize + kV1EntrySize * jobs.size();

        currentFile = tempPath;
        if (!QDir().mkpath(QFileInfo(m_outputBlobset).absolutePath()))
            fail(u"Could not create the folder for: "_s + m_outputBlobset);
        if (QFile::exists(tempPath) && !QFile::remove(tempPath))
            fail(u"Could not remove the old temporary container."_s);

        out = std::make_unique<PackageStream>(tempPath, endianFor(m_isBigEndian));

        // Reserve the header, the real one is written once all offsets and sizes are known.
        out->writeBytes(QByteArray(headerDataSize, '\0'));

        QList<EntryRecord> entries;
        entries.reserve(jobs.size());

        for (qsizetype i = 0; i < jobs.size(); ++i) {
            const ModJob& job = jobs[i];
            currentFile = job.absolutePath;

            emit updateStatus(u"Creating File: %1"_s.arg(job.relativePath));
            if (job.isNew)
                emit updateConsole(u"New asset: %1 (hash %2)"_s.arg(job.relativePath, BlobsetHash::toHex(job.pathHash, 8)));
            entries.append(writeJobV1(*out, job));

            emitProgress(static_cast<int>(((i + 1) * 100) / jobs.size()));
            emit updateConsole(u"Compiled and appended chunk: %1"_s.arg(job.relativePath));
        }

        currentFile = m_outputBlobset;
        writeHeaderV1(*out, entries, hashSize);

        out.reset();   // Flushes and closes the temporary container.

        if (QFile::exists(m_outputBlobset) && !QFile::remove(m_outputBlobset))
            fail(u"Could not replace the existing blobset."_s);
        if (!QFile::rename(tempPath, m_outputBlobset))
            fail(u"Could not move the new blobset into place."_s);

        addNewFilesToMapping(jobs);
    } catch (const std::exception& ex) {
        out.reset();
        QFile::remove(tempPath);
        emit taskFinished(describeFailure(currentFile, ex));
        return;
    }

    emit taskSucceeded();
    emit taskFinished(u"Blobset Synthesis complete. Custom container successfully generated."_s);
}

/// New assets go into the file mapping too, so the browser shows them and later runs find them by name.
void CreateWorker::addNewFilesToMapping(const QList<ModJob>& jobs) {
    QList<std::pair<QString, quint64>> added;
    for (const ModJob& job : jobs)
        if (job.isNew)
            added.append({job.relativePath, job.pathHash});
    if (added.isEmpty())
        return;

    try {
        std::vector<FileMappingWorker::MappingEntry> entries =
            QFile::exists(m_mappingFile) ? FileMappingWorker::readMappingFile(m_mappingFile)
                                         : std::vector<FileMappingWorker::MappingEntry>();
        QSet<quint64> known;
        for (const auto& e : entries)
            known.insert(e.hash);
        for (const auto& [path, hash] : std::as_const(added)) {
            if (known.contains(hash))
                continue;
            entries.push_back({FileMappingWorker::normalizePath(path), hash});
            known.insert(hash);
        }
        FileMappingWorker::sortEntries(entries);
        FileMappingWorker::writeMappingFile(m_mappingFile, entries);
        emit updateConsole(u"Added %1 new file(s) to the file mapping."_s.arg(added.size()));
    } catch (const std::exception& ex) {
        emit updateConsole(u"Warning: the new files couldn't be added to the file mapping: "_s + QString::fromUtf8(ex.what()));
    }
}
