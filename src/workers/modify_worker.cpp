#include "modify_worker.h"
#include "blobset_file.h"
#include "bsb_file.h"
#include "compression_io.h"
#include "core_defs.h"
#include "file_mapping_worker.h"
#include "hash_utility.h"
#include "mini_txpk_file.h"
#include "x360_texture.h"
#include "utilities.h"
#include "xml_models.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <vector>
#include <stdexcept>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// ---------------------------------------------------------------------------
// Format constants
// ---------------------------------------------------------------------------

constexpr qint64 kLzmaChunkSize = 32768;     // V1: 32 KB chunks
constexpr qint64 kZstdChunkSize = 262144;    // V4: 256 KB chunks
constexpr qint64 kCopyBlockSize = 1 << 20;

// V1 header: [sha1 (optional)] magic, blobset count, files count, then one 32 byte entry per file.
constexpr qsizetype kSha1Size = 20;
constexpr qsizetype kV1HeaderSize = 12;
constexpr qsizetype kV1EntrySize = 32;
// Mods go into one extra container after the game's own: data-1 when the game has only data-0 (RLL3, ...),
// data-4 for RLL4 (data-0 .. data-3). The count comes from the pristine header, never from a guess.

// V4 header: magic, files count, one 8 byte hash per file, then one 16 byte size entry per file.
constexpr qsizetype kV4PreambleSize = 8;
constexpr qsizetype kV4HashSize = 8;
constexpr qsizetype kV4EntrySize = 16;

// V3 header (Cricket 19): magic, files count, then per file: main / vram compressed + uncompressed sizes,
// 32 bit hash, 0xEEEEEEEE.
constexpr qsizetype kV3PreambleSize = 8;
constexpr qsizetype kV3EntrySize = 24;

constexpr quint32 kBlobMagic = 1112493122;   // 'BLOB'
constexpr quint32 kBolbMagic = 1112297282;   // 'BOLB'
constexpr quint32 kDdsMagic = 0x20534444;    // 'DDS '
constexpr qsizetype kDdsHeaderSize = 128;
constexpr int kLastDdsGameId = 15;           // Games above this store DDS paths as ".badds".

const QStringList kV1Extensions = {u"dds"_s, u"txpk"_s, u"m3mp"_s, u"bsb"_s, u"bmf"_s,
                                   u"bank"_s, u"fsb"_s, u"fev"_s, u"wav"_s, u"dat"_s};
const QStringList kV4Extensions = {u"dds"_s, u"txpk"_s, u"m3mp"_s, u"wem"_s, u"bnk"_s};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QString describeFailure(const QString& currentFile, const std::exception& ex) {
    QString message = u"Modification failed.\n\n"_s;
    if (!currentFile.isEmpty())
        message += u"File: %1\n\n"_s.arg(currentFile);
    return message + QString::fromUtf8(ex.what());
}

/// Mapping keys and mod paths are compared case insensitive with '/' separators
/// (the V1 mapping stores '/', the V4 mapping stores '\').
QString normalizedKey(QString path) {
    path.replace(u'\\', u'/');
    return path.toLower();
}

bool isHexName(const QString& name) {
    static const QRegularExpression hex(u"^[0-9a-fA-F]+$"_s);
    return hex.match(name).hasMatch();
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

void writeBytes(QIODevice& out, const QByteArray& data) {
    if (out.write(data) != data.size())
        fail(u"Failed writing to file: "_s + out.errorString());
}

/// Packs a single 32 bit integer with the requested byte order.
QByteArray packInt32(qint32 value, QDataStream::ByteOrder order) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(order);
    stream << value;
    return bytes;
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

    fail(u"The backed up blobset header has an invalid BLOB magic signature."_s);
}

} // namespace

// ==============================================================================================================
// ModifyWorker
// ==============================================================================================================

ModifyWorker::ModifyWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& modsDir,
                           const QString& backupDir, const QString& mappingFile, int gameId, int platform,
                           QObject* parent)
    : QObject(parent),
      m_blobsetFile(blobsetFile),
      m_version(version),
      m_isBigEndian(isBigEndian),
      m_modsDir(modsDir),
      m_backupDir(backupDir),
      m_mappingFile(mappingFile),
      m_gameId(gameId),
      m_platform(platform) {}

void ModifyWorker::modify_blobset() {
    emit updateConsole(u"Starting asynchronous Blobset modification loop..."_s);

    // m_version is the CoreDefs::BlobsetVersion value: 0 = V1, 1 = V2, 2 = V3, 3 = V4.
    switch (static_cast<CoreDefs::BlobsetVersion>(m_version)) {
    case CoreDefs::BlobsetVersion::V1:
        modifyV1();
        break;
    case CoreDefs::BlobsetVersion::V2:
        // V1's layout; only the LZMA sections differ (no chunk table, see CompressionIO::buildLzmaSectionV2).
        m_lzmaV2 = true;
        modifyV1();
        m_lzmaV2 = false;
        break;
    case CoreDefs::BlobsetVersion::V3:
        // V4's one-file-per-asset routine with V3 sections (XORed LZMA chunks) and V3's header entries.
        m_lzmaV3 = true;
        modifyV4();
        m_lzmaV3 = false;
        break;
    case CoreDefs::BlobsetVersion::V4:
        modifyV4();
        break;
    default:
        emit taskFinished(u"Modification for blobset version %1 is not implemented."_s.arg(m_version + 1));
        break;
    }
}

// ---------------------------------------------------------------------------
// Paths / backups
// ---------------------------------------------------------------------------

QString ModifyWorker::blobPathFor(quint32 blobSetNumber) const {
    QString path = m_blobsetFile;
    const qsizetype pos = path.lastIndexOf(u"0.blob"_s);
    if (pos >= 0)
        path.replace(pos, 6, QString::number(blobSetNumber) + u".blob"_s);
    return path;
}

bool ModifyWorker::backupFilesV3V4(const QString& sourcePath, const QString& folderName, const QString& fileName) {
    const QString backupFolder = QDir(m_backupDir).absoluteFilePath(folderName);
    if (!QDir().mkpath(backupFolder))
        return false;

    const QString backupFile = QDir(backupFolder).absoluteFilePath(fileName);

    // Only the very first modification keeps the pristine game file.
    if (!QFile::exists(backupFile) && QFile::exists(sourcePath)) {
        if (!QFile::rename(sourcePath, backupFile) && !QFile::copy(sourcePath, backupFile))
            return false;
        emit updateConsole(u"Backed up original asset: %1/%2"_s.arg(folderName, fileName));
    }

    // The modified asset is always rebuilt from scratch.
    return !QFile::exists(sourcePath) || QFile::remove(sourcePath);
}

void ModifyWorker::backupBlobsetHeader() {
    if (!QDir().mkpath(m_backupDir))
        fail(u"Could not create the backup folder: "_s + m_backupDir);

    const QString backupBlobset = QDir(m_backupDir).absoluteFilePath(QFileInfo(m_blobsetFile).fileName());
    if (QFile::exists(backupBlobset))
        return;

    if (!QFile::copy(m_blobsetFile, backupBlobset))
        fail(u"Could not back up the blobset header to: "_s + backupBlobset);
    emit updateConsole(u"Backed up original blobset: %1"_s.arg(QFileInfo(backupBlobset).fileName()));
}

// ---------------------------------------------------------------------------
// Planning
// ---------------------------------------------------------------------------

/// File mapping layout (little endian): u32 count, then per entry: u8 pathLength, path (latin1), u64 hash.
QHash<QString, quint64> ModifyWorker::loadMappingData() const {
    QHash<QString, quint64> mapping;

    QFile file(m_mappingFile);
    if (!file.open(QIODevice::ReadOnly))
        return mapping;

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 count = 0;
    in >> count;
    mapping.reserve(qMin<quint32>(count, 1'000'000));

    for (quint32 i = 0; i < count; ++i) {
        quint8 pathLength = 0;
        in >> pathLength;

        QByteArray path(pathLength, '\0');
        if (in.readRawData(path.data(), pathLength) != pathLength)
            return {};

        quint64 hash = 0;
        in >> hash;

        if (in.status() != QDataStream::Ok)
            return {};

        mapping.insert(normalizedKey(QString::fromLatin1(path)), hash);
    }

    return mapping;
}

QStringList ModifyWorker::supportedExtensions(int version) {
    const auto v = static_cast<CoreDefs::BlobsetVersion>(version);
    QStringList types = v == CoreDefs::BlobsetVersion::V1 || v == CoreDefs::BlobsetVersion::V2 ? kV1Extensions
                                                                                               : kV4Extensions;
    // Modify takes any file that replaces one of the game's; these are the usual ones, so the Mod Manager counts
    // them as mod files (and finds a mod's root folder by them).
    for (const QString& other : {u"str"_s, u"lua"_s, u"mesh"_s, u"col"_s, u"hjo"_s, u"mat"_s, u"bsb"_s, u"bmf"_s,
                                 u"bank"_s, u"fsb"_s, u"fev"_s, u"wem"_s, u"bnk"_s, u"m3mp"_s, u"dat"_s})
        if (!types.contains(other))
            types.append(other);
    return types;
}

QList<ModifyWorker::ModFile> ModifyWorker::collectModFiles(const QStringList& extensions) const {
    const QStringList sources = m_modSources.isEmpty() ? QStringList{m_modsDir} : m_modSources;
    QList<ModFile> files;
    int overridden = 0;

    // The types with special handling first (in the order given), then every other type found in the mods folders
    // (str, lua, mesh, ...): those replace the blobset files they match, stored like the originals (planJobs()).
    QStringList order = extensions;
    {
        QSet<QString> others;
        for (const QString& source : sources) {
            QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString suffix = QFileInfo(it.next()).suffix().toLower();
                if (!suffix.isEmpty() && !extensions.contains(suffix))
                    others.insert(suffix);
            }
        }
        QStringList sorted(others.cbegin(), others.cend());
        sorted.sort();
        order += sorted;
    }

    // Grouped by extension (in that order), alphabetical inside each group, so every run produces the same layout.
    for (const QString& extension : std::as_const(order)) {
        const bool generic = !extensions.contains(extension);
        // Same path in two sources: the later source wins (it keeps the position the first one had).
        QHash<QString, qsizetype> byKey;
        QList<ModFile> group;
        for (const QString& source : sources) {
            const QDir sourceDir(source);
            if (!sourceDir.exists())
                continue;

            QStringList found;
            QDirIterator it(sourceDir.absolutePath(), {u"*."_s + extension}, QDir::Files,
                            QDirIterator::Subdirectories);   // Symlinks too: Vortex can deploy with links.
            while (it.hasNext())
                found.append(it.next());

            for (const QString& path : std::as_const(found)) {
                const QFileInfo info(path);
                if (generic && extension == u"xml"_s) {
                    // A TXPK / M3MP's <name>.xml is information for Modify, not a game file.
                    const QString base = info.absolutePath() + u'/' + info.completeBaseName();
                    if (QFileInfo::exists(base + u".txpk"_s) || QFileInfo::exists(base + u".m3mp"_s))
                        continue;
                }
                if (info.fileName().compare(u"blobsetmod.json"_s, Qt::CaseInsensitive) == 0)
                    continue;   // A mod's details (Mod Manager).
                const ModFile file{path, sourceDir.relativeFilePath(path), extension, generic};
                const QString key = normalizedKey(file.relativePath);
                const auto existing = byKey.constFind(key);
                if (existing != byKey.constEnd()) {
                    group[existing.value()] = file;
                    ++overridden;
                } else {
                    byKey.insert(key, group.size());
                    group.append(file);
                }
            }
        }

        std::sort(group.begin(), group.end(), [](const ModFile& a, const ModFile& b) {
            return a.relativePath.compare(b.relativePath, Qt::CaseInsensitive) < 0;
        });
        files.append(group);
    }

    if (sources.size() > 1) {
        emit const_cast<ModifyWorker*>(this)->updateConsole(
            u"%1 mod file(s) from %2 source folder(s)%3."_s.arg(files.size()).arg(sources.size())
                .arg(overridden > 0 ? u", %1 replaced by a mod later in the load order"_s.arg(overridden) : QString()));
    }
    return files;
}

ModifyWorker::HashIndex ModifyWorker::buildHashIndex(const BlobsetFile& blobset, bool use64BitHash) {
    HashIndex index;
    index.reserve(blobset.entries.size());

    for (qsizetype i = 0; i < blobset.entries.size(); ++i) {
        const BlobsetEntry& entry = blobset.entries[i];
        const quint64 hash = use64BitHash ? entry.folderFileHash64 : static_cast<quint64>(entry.folderFileHash32);

        if (!index.contains(hash))
            index.insert(hash, static_cast<quint32>(i));
    }

    return index;
}

ModifyWorker::ModJob ModifyWorker::resolveJob(const ModFile& file, const QHash<QString, quint64>& mapping,
                                              const HashIndex& indexByHash, const BlobsetFile& blobset) const {
    ModJob job;
    job.absolutePath = file.absolutePath;
    job.relativePath = file.relativePath;
    job.extension = file.extension;

    if (file.extension == u"txpk"_s || file.extension == u"m3mp"_s) {
        // These carry their blobset index, hashes and section sizes in a sidecar xml.
        const QFileInfo info(file.absolutePath);
        const QString xmlPath = info.absolutePath() + u'/' + info.completeBaseName() + u".xml"_s;

        if (!QFile::exists(xmlPath))
            fail(u"Can't find the %1 xml info: %2"_s.arg(file.extension.toUpper(), xmlPath));

        XmlModifyFileInfo xml;
        if (!xml.deserialize(xmlPath))
            fail(u"%1 - XmlDeserialize failed."_s.arg(xmlPath));

        job.headerIndex = xml.index;
        job.folderHash = xml.folderHash;
        job.fileHash = xml.fileNameHash;
        job.mainUncompressed = xml.mainUnCompressedSize;
        job.vramUncompressed = xml.vramUnCompressedSize;
    } else if (file.extension == u"dat"_s) {
        // Unknown V1 assets are extracted as "<index>.dat".
        bool ok = false;
        job.headerIndex = QFileInfo(file.absolutePath).completeBaseName().toUInt(&ok);
        if (!ok) {
            // A named .dat (Resolve File Names): found through the mapping or its name's hash.
            const auto mapped = mapping.constFind(normalizedKey(file.relativePath));
            const quint64 hash = mapped != mapping.constEnd()
                                     ? mapped.value()
                                     : BlobsetHash::hashForFile(m_version, file.relativePath, m_platform, m_gameKey);
            if (!indexByHash.contains(hash))
                fail(u"%1 - a .dat file has to be named after its blobset index (e.g. 123.dat) or a mapped name; "
                     u"new assets need a known type (e.g. .dds)."_s.arg(file.relativePath));
            job.headerIndex = indexByHash.value(hash);
        }
    } else {
        // Everything else is located through the file mapping, or by the hash of its name.
        const auto mapped = mapping.constFind(normalizedKey(file.relativePath));
        if (mapped != mapping.constEnd() && indexByHash.contains(mapped.value())) {
            job.headerIndex = indexByHash.value(mapped.value());
        } else {
            const quint64 hash = BlobsetHash::hashForFile(m_version, file.relativePath, m_platform, m_gameKey);
            if (indexByHash.contains(hash)) {
                job.headerIndex = indexByHash.value(hash);   // An existing asset, named the way the game names it.
            } else {
                // A new asset: it gets its own header entry (and V3 / V4 its own physical file).
                job.isNew = true;
                job.newHash = hash;
                const auto names = m_version == static_cast<int>(CoreDefs::BlobsetVersion::V4)
                                       ? Utilities::getHashString64(hash)
                                       : Utilities::getHashString32(static_cast<quint32>(hash));
                job.folderHash = names.first;
                job.fileHash = names.second;
                return job;
            }
        }
    }

    if (job.headerIndex >= static_cast<quint32>(blobset.entries.size()))
        fail(u"%1 - blobset index %2 is out of range (the blobset has %3 files)."_s
                 .arg(file.relativePath).arg(job.headerIndex).arg(blobset.entries.size()));

    // V4 addresses every asset by its own physical file.
    const BlobsetEntry& entry = blobset.entries[job.headerIndex];
    if (job.folderHash.isEmpty())
        job.folderHash = entry.folderHashName;
    if (job.fileHash.isEmpty())
        job.fileHash = entry.fileHashName;

    return job;
}

QList<ModifyWorker::ModJob> ModifyWorker::planJobs(const QStringList& extensions, const QHash<QString, quint64>& mapping,
                                                   const BlobsetFile& blobset, bool use64BitHash) const {
    const HashIndex indexByHash = buildHashIndex(blobset, use64BitHash);

    QList<ModJob> jobs;
    QHash<quint32, QString> claimedBy;
    QHash<quint64, QString> newClaimedBy;
    QStringList notInBlobset;   // Other file types that don't replace a file of the game (readmes, pictures, ...).

    for (const ModFile& file : collectModFiles(extensions)) {
        ModJob job = resolveJob(file, mapping, indexByHash, blobset);

        if (file.generic) {
            // Any other type only replaces an existing file: it's stored the way that file is.
            if (job.isNew) {
                notInBlobset.append(file.relativePath);
                continue;
            }
            const BlobsetEntry& entry = blobset.entries[job.headerIndex];
            job.generic = true;
            job.origMainCompressed = entry.mainCompressedSize;
            job.origMainUncompressed = entry.mainUnCompressedSize;
            job.origVramCompressed = entry.vramCompressedSize;
            job.origVramUncompressed = entry.vramUnCompressedSize;
        }

        if (use64BitHash && (!isHexName(job.folderHash) || !isHexName(job.fileHash)))
            fail(u"%1 - invalid folder/file hash '%2/%3'."_s.arg(file.relativePath, job.folderHash, job.fileHash));

        if (job.isNew) {
            const auto claimedNew = newClaimedBy.constFind(job.newHash);
            if (claimedNew != newClaimedBy.constEnd())
                fail(u"%1 and %2 are both new and have the same name hash."_s.arg(claimedNew.value(), file.relativePath));
            newClaimedBy.insert(job.newHash, file.relativePath);
            jobs.append(job);
            continue;
        }

        // Two mods writing the same entry would silently overwrite each other.
        const auto claimed = claimedBy.constFind(job.headerIndex);
        if (claimed != claimedBy.constEnd())
            fail(u"%1 and %2 both replace blobset index %3."_s.arg(claimed.value(), file.relativePath).arg(job.headerIndex));

        claimedBy.insert(job.headerIndex, file.relativePath);
        jobs.append(job);
    }

    if (!notInBlobset.isEmpty())
        emit const_cast<ModifyWorker*>(this)->updateConsole(
            u"Skipped %1 file(s) that don't match a file of the game (only known types can be added as new files): %2%3"_s
                .arg(notInBlobset.size()).arg(notInBlobset.mid(0, 10).join(u", "_s))
                .arg(notInBlobset.size() > 10 ? u", ..."_s : QString()));
    return jobs;
}

namespace {
// Any other file type: the main section is the original's main size (all of it when the original has no vram),
// the rest is the vram section.
void splitGeneric(const QByteArray& data, quint32 origMain, quint32 origVram, const QString& relativePath,
                  QByteArray& mainData, QByteArray& vramData) {
    if (origVram == 0) {
        mainData = data;
        vramData.clear();
        return;
    }
    if (data.size() < static_cast<qint64>(origMain))
        fail(u"%1 is smaller than the main section of the file it replaces (%2 bytes)."_s.arg(relativePath).arg(origMain));
    mainData = data.left(origMain);
    vramData = data.mid(origMain);
}
} // namespace

// ---------------------------------------------------------------------------
// Mini TXPK (single DDS) header
// ---------------------------------------------------------------------------

QString ModifyWorker::miniPathFor(const QString& relativePath) const {
    // The marker is "conv" + "ant2-temp-intermediate" (see the "conv" check in the file mapping / extract workers).
    const QString prefix = u"convant2-temp-intermediate"_s;

    QString path = relativePath;
    path.replace(u'\\', u'/');

    if (static_cast<CoreDefs::BlobsetVersion>(m_version) == CoreDefs::BlobsetVersion::V1) {
        path.replace(u'/', u'\\');
        return prefix + u'\\' + path;
    }

    if (m_gameId > kLastDdsGameId && path.endsWith(u".dds", Qt::CaseInsensitive)) {
        path.chop(4);
        path += u".badds"_s;
    }
    return prefix + u'/' + path;
}

QByteArray ModifyWorker::buildMiniHeader(const ModJob& job, const QByteArray& dds, QByteArray& vram) const {
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
                emit const_cast<ModifyWorker*>(this)->updateConsole(u"%1: %2"_s.arg(job.relativePath, texture->note));

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
// Section writers
// ---------------------------------------------------------------------------

/// Stores the data as it is.
ModifyWorker::Section ModifyWorker::writeRaw(QIODevice& out, const QByteArray& data) {
    Section section;
    section.offset = toU32(out.pos(), u"Container offset"_s);
    section.compressedSize = section.uncompressedSize = toU32(data.size(), u"Section size"_s);

    writeBytes(out, data);
    return section;
}

/// Streams a file into the container as it is.
ModifyWorker::Section ModifyWorker::appendFile(QIODevice& out, const QString& path) {
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        fail(u"Could not open '%1': %2"_s.arg(path, in.errorString()));

    Section section;
    section.offset = toU32(out.pos(), u"Container offset"_s);

    while (!in.atEnd()) {
        const QByteArray block = in.read(kCopyBlockSize);
        if (block.isEmpty())
            fail(u"Failed reading '%1': %2"_s.arg(path, in.errorString()));
        writeBytes(out, block);
    }

    section.compressedSize = section.uncompressedSize = toU32(out.pos() - section.offset, u"Section size"_s);
    return section;
}

/// V1 chunk block: count, [compressed size + 4] * count, then per chunk: uncompressed size + data.
/// A chunk that doesn't shrink is stored as it is (compressed size == uncompressed size), like the extractor expects.
/// V2 (lzmaV2): self describing 32 KB chunks, no table (the chunk size argument doesn't apply).
ModifyWorker::Section ModifyWorker::writeLzma(QIODevice& out, const QByteArray& data, qint64 chunkSize,
                                              QDataStream::ByteOrder order, bool lzmaV2) {
    Section section;
    section.offset = toU32(out.pos(), u"Container offset"_s);
    section.uncompressedSize = toU32(data.size(), u"Section size"_s);

    if (data.isEmpty())
        return section;

    if (lzmaV2) {
        const QByteArray block = CompressionIO::buildLzmaSectionV2(data, order == QDataStream::BigEndian);
        writeBytes(out, block);
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
        payloads.append(packed.size() < chunk.size() ? packed : chunk);
    }

    QByteArray block;
    block += packInt32(static_cast<qint32>(chunkSizes.size()), order);
    for (const QByteArray& payload : std::as_const(payloads))
        block += packInt32(static_cast<qint32>(payload.size() + 4), order);
    for (qsizetype j = 0; j < payloads.size(); ++j) {
        block += packInt32(static_cast<qint32>(chunkSizes[j]), order);
        block += payloads[j];
    }

    writeBytes(out, block);
    section.compressedSize = toU32(block.size(), u"Section size"_s);
    return section;
}

/// V3 (Cricket 19): XORed LZMA chunks, see CompressionIO::buildLzmaSectionV3.
ModifyWorker::Section ModifyWorker::writeLzmaV3(QIODevice& out, const QByteArray& data) {
    Section section;
    section.offset = toU32(out.pos(), u"Container offset"_s);
    section.uncompressedSize = toU32(data.size(), u"Section size"_s);
    if (data.isEmpty())
        return section;

    const QByteArray block = CompressionIO::buildLzmaSectionV3(data);
    writeBytes(out, block);
    section.compressedSize = toU32(block.size(), u"Section size"_s);
    return section;
}

/// V4 chunks: int32 (size + 4), then the data. Only full 256 KB chunks are compressed, unless
/// `compressPartialChunks` is set (the main section of a TXPK compresses everything).
ModifyWorker::Section ModifyWorker::writeZstd(QIODevice& out, const QByteArray& data, bool compressPartialChunks,
                                              QDataStream::ByteOrder order) {
    Section section;
    section.offset = toU32(out.pos(), u"Container offset"_s);
    section.uncompressedSize = toU32(data.size(), u"Section size"_s);

    if (data.isEmpty())
        return section;

    const qint64 chunkCount = Utilities::ChunkAmount(data.size(), kZstdChunkSize);
    const QList<qint64> chunkSizes = Utilities::ChunkSizes(data.size(), chunkCount, kZstdChunkSize);

    qint64 offset = 0;
    for (const qint64 size : chunkSizes) {
        const QByteArray chunk = data.mid(offset, size);
        offset += size;

        QByteArray payload = chunk;
        if (compressPartialChunks || size == kZstdChunkSize) {
            payload = CompressionIO::compressZstd(chunk);
            if (payload.isEmpty())
                fail(u"ZSTD compression failed."_s);
        }

        writeBytes(out, packInt32(static_cast<qint32>(payload.size() + 4), order));
        writeBytes(out, payload);
    }

    section.compressedSize = toU32(out.pos() - section.offset, u"Section size"_s);
    return section;
}

// ===========================================================================
// Modify Blobset Version 1
// ===========================================================================

ModifyWorker::HeaderUpdate ModifyWorker::writeJobV1(QFile& out, const ModJob& job, const BlobsetFile& original) const {
    const QDataStream::ByteOrder order = streamOrder();

    HeaderUpdate update;
    update.index = job.headerIndex;

    if (job.extension == u"dds"_s) {
        // main = mini TXPK header, vram = the texture data (PC: the whole .dds, consoles: converted pixels).
        const QByteArray dds = readWholeFile(job.absolutePath);
        QByteArray pixels;
        const QByteArray miniHeader = buildMiniHeader(job, dds, pixels);

        update.mainPart = writeLzma(out, miniHeader, miniHeader.size(), order, m_lzmaV2);
        update.vram = writeLzma(out, pixels, kLzmaChunkSize, order, m_lzmaV2);
    } else if (job.extension == u"txpk"_s) {
        const QByteArray data = readWholeFile(job.absolutePath);
        const qint64 mainSize = job.mainUncompressed;
        const qint64 vramSize = job.vramUncompressed;

        if (data.size() < mainSize + vramSize)
            fail(u"%1 is smaller than its xml info says (%2 bytes expected)."_s.arg(job.relativePath).arg(mainSize + vramSize));

        update.mainPart = writeLzma(out, data.left(mainSize), kLzmaChunkSize, order, m_lzmaV2);
        update.vram = writeLzma(out, data.mid(mainSize, vramSize), kLzmaChunkSize, order, m_lzmaV2);
    } else if (job.extension == u"dat"_s) {
        // Unknown assets keep whatever compression the original entry had.
        const BlobsetEntry& entry = original.entries[job.headerIndex];
        const QByteArray data = readWholeFile(job.absolutePath);
        const qint64 mainSize = entry.mainUnCompressedSize;
        const qint64 vramSize = entry.vramUnCompressedSize;

        if (data.size() < mainSize + vramSize)
            fail(u"%1 is smaller than the original entry (%2 bytes expected)."_s.arg(job.relativePath).arg(mainSize + vramSize));

        const QByteArray mainData = data.left(mainSize);
        const QByteArray vramData = data.mid(mainSize, vramSize);

        update.mainPart = (entry.mainCompressedSize == entry.mainUnCompressedSize)
                          ? writeRaw(out, mainData)
                          : writeLzma(out, mainData, kLzmaChunkSize, order, m_lzmaV2);
        update.vram = (entry.vramCompressedSize == entry.vramUnCompressedSize)
                          ? writeRaw(out, vramData)
                          : writeLzma(out, vramData, kLzmaChunkSize, order, m_lzmaV2);
    } else if (job.generic) {
        // Any other type (str, lua, mesh, ...): split and compressed like the entry it replaces.
        QByteArray mainData, vramData;
        splitGeneric(readWholeFile(job.absolutePath), job.origMainUncompressed, job.origVramUncompressed,
                     job.relativePath, mainData, vramData);
        update.mainPart = job.origMainCompressed == job.origMainUncompressed
                          ? writeRaw(out, mainData)
                          : writeLzma(out, mainData, kLzmaChunkSize, order, m_lzmaV2);
        update.vram = vramData.isEmpty() || job.origVramCompressed == job.origVramUncompressed
                      ? writeRaw(out, vramData)
                      : writeLzma(out, vramData, kLzmaChunkSize, order, m_lzmaV2);
    } else if (const QByteArray bsb = job.extension == u"bsb"_s ? readWholeFile(job.absolutePath) : QByteArray();
               Bsb::mainSectionSize(bsb) > 0) {
        // BSB (Extract / the BSB Creator write main + vram in one file): split again by its header, and compressed
        // the way the original entry was.
        const qint64 mainSize = Bsb::mainSectionSize(bsb);
        const QByteArray mainData = bsb.left(mainSize);
        const QByteArray vramData = bsb.mid(mainSize);
        const bool known = !job.isNew && job.headerIndex < quint32(original.entries.size());
        const bool mainRaw = known && original.entries[job.headerIndex].mainCompressedSize
                                          == original.entries[job.headerIndex].mainUnCompressedSize;
        const bool vramRaw = known && original.entries[job.headerIndex].vramCompressedSize
                                          == original.entries[job.headerIndex].vramUnCompressedSize;
        update.mainPart = mainRaw ? writeRaw(out, mainData) : writeLzma(out, mainData, kLzmaChunkSize, order, m_lzmaV2);
        update.vram = (vramData.isEmpty() || vramRaw) ? writeRaw(out, vramData)
                                                      : writeLzma(out, vramData, kLzmaChunkSize, order, m_lzmaV2);
    } else {
        // m3mp, bmf, bank, fsb, fev, wav: stored as they are, no vram section.
        update.mainPart = appendFile(out, job.absolutePath);
        update.vram = writeRaw(out, QByteArray());
    }

    return update;
}

void ModifyWorker::writeHeaderV1(const QList<HeaderUpdate>& updates, qsizetype hashSize, quint32 modBlobsetNumber) const {
    QFile header(m_blobsetFile);
    if (!header.open(QIODevice::ReadWrite))
        fail(u"Could not open the blobset header for writing: "_s + header.errorString());

    QDataStream out(&header);
    out.setByteOrder(streamOrder());

    // The blobset count follows the magic: the game's containers plus our mod container.
    header.seek(hashSize + 4);
    out << static_cast<quint32>(modBlobsetNumber + 1);

    for (const HeaderUpdate& update : updates) {
        header.seek(hashSize + kV1HeaderSize + kV1EntrySize * static_cast<qint64>(update.index));

        out << update.mainPart.offset << update.mainPart.compressedSize << update.mainPart.uncompressedSize
            << update.vram.offset << update.vram.compressedSize << update.vram.uncompressedSize;

        header.seek(header.pos() + 4);   // The path hash stays as it is.
        out << modBlobsetNumber;
    }

    if (out.status() != QDataStream::Ok)
        fail(u"Failed writing the blobset header."_s);
}

void ModifyWorker::modifyV1() {
    QString currentFile;

    try {
        emit updateConsole(m_lzmaV2 ? u"Validating structural mapping for V2 Modification..."_s
                                    : u"Validating structural mapping for V1 Modification..."_s);

        const QHash<QString, quint64> mapping = loadMappingData();
        if (mapping.isEmpty()) {
            emit taskFinished(u"Mapping data is missing or corrupted. Try 'Update File Mapping Data' in Settings "
                                     u"or validate the game files."_s);
            return;
        }

        // The pristine header: always the starting point, so running modify twice never stacks modifications.
        const QString headerBackup = QDir(m_backupDir).absoluteFilePath(QFileInfo(m_blobsetFile).fileName() + u".header"_s);
        QByteArray originalHeader = Utilities::readBlobsetHeader(headerBackup);
        if (originalHeader.isEmpty()) {
            // First Modify: keep the game's original header ([sha1] magic, blobset count, files count, 32 byte
            // entries). The game's files have to be unmodified at this point.
            QFile blobset(m_blobsetFile);
            if (!blobset.open(QIODevice::ReadOnly))
                fail(u"Could not open the blobset: "_s + blobset.errorString());
            const QByteArray start = blobset.read(kSha1Size + kV1HeaderSize);
            const qsizetype startHash = probeHashSize(start, m_isBigEndian);
            if (start.size() < startHash + kV1HeaderSize)
                fail(u"The blobset is too small to hold a header."_s);
            const quint32 files = m_isBigEndian ? qFromBigEndian<quint32>(start.constData() + startHash + 8)
                                                : qFromLittleEndian<quint32>(start.constData() + startHash + 8);
            const qint64 headerSize = startHash + kV1HeaderSize + kV1EntrySize * qint64(files);
            blobset.seek(0);
            originalHeader = blobset.read(headerSize);
            if (originalHeader.size() != headerSize)
                fail(u"The blobset header is truncated."_s);

            if (!QDir().mkpath(m_backupDir))
                fail(u"Could not create the backup folder: "_s + m_backupDir);
            QSaveFile out(headerBackup);
            if (!out.open(QIODevice::WriteOnly) || out.write(originalHeader) != originalHeader.size() || !out.commit())
                fail(u"Could not back up the blobset header to: "_s + headerBackup);
            emit updateConsole(u"Backed up the original blobset header: %1"_s.arg(QFileInfo(headerBackup).fileName()));
        }

        const qsizetype hashSize = probeHashSize(originalHeader, m_isBigEndian);

        BlobsetFile original;
        {
            QBuffer buffer(&originalHeader);
            buffer.open(QIODevice::ReadOnly);
            QDataStream in(&buffer);

            // BlobsetFile only needs to know whether the SHA1 is there (it keys that off the game id).
            if (!original.read(in, CoreDefs::BlobsetVersion::V1, m_isBigEndian, hashSize > 0 ? 1 : 0))
                fail(u"The backed up blobset header has an invalid BLOB magic signature."_s);
        }

        const QList<ModJob> jobs = planJobs(kV1Extensions, mapping, original, false);
        if (jobs.isEmpty()) {
            emit taskFinished(u"No mod files detected in workspace."_s);
            return;
        }

        // The mod container comes right after the game's own containers (pristine header count), so a game
        // container (e.g. RLL4's data-1 .. data-3) is never removed or overwritten.
        const quint32 modBlobsetNumber = std::max<quint32>(1, original.blobsetCount);
        const QString updateBlobsetPath = blobPathFor(modBlobsetNumber);
        if (updateBlobsetPath == m_blobsetFile)
            fail(u"Can't work out the update blobset (-%1) from the blobset name."_s.arg(modBlobsetNumber));
        emit updateConsole(u"Mods go into %1 (the game has %2 container(s))."_s
                               .arg(QFileInfo(updateBlobsetPath).fileName()).arg(original.blobsetCount));

        // Restore the original header, then rebuild the update container from scratch.
        currentFile = m_blobsetFile;
        {
            QFile header(m_blobsetFile);
            if (!header.open(QIODevice::ReadWrite))
                fail(u"Could not open the blobset for writing: "_s + header.errorString());
            writeBytes(header, originalHeader);
        }
        emit updateConsole(u"Restored original blobset header."_s);

        // New assets make the header grow over the start of data-0's own data. Those bytes are kept in the backup
        // folder (".spill"), put back first on every run, and the game files they belong to move into the mod
        // container (see below).
        const qint64 headerEnd = hashSize + kV1HeaderSize + kV1EntrySize * static_cast<qint64>(original.filesCount);
        const qint64 newFiles = std::count_if(jobs.cbegin(), jobs.cend(), [](const ModJob& j) { return j.isNew; });
        const qint64 growth = kV1EntrySize * newFiles;
        const QString spillPath = QDir(m_backupDir).absoluteFilePath(QFileInfo(m_blobsetFile).fileName() + u".spill"_s);
        QByteArray spill;
        {
            QFile spillFile(spillPath);
            if (spillFile.open(QIODevice::ReadOnly))
                spill = spillFile.readAll();
        }
        if (!spill.isEmpty() || growth > 0) {
            QFile data0(m_blobsetFile);
            if (!data0.open(QIODevice::ReadWrite))
                fail(u"Could not open the blobset for writing: "_s + data0.errorString());
            if (!spill.isEmpty()) {
                if (!data0.seek(headerEnd))
                    fail(u"Could not seek in the blobset."_s);
                writeBytes(data0, spill);
                emit updateConsole(u"Restored the game data behind the original header."_s);
            }
            if (growth > spill.size()) {
                // Still untouched game data at this point.
                if (!data0.seek(headerEnd + spill.size()))
                    fail(u"Could not seek in the blobset."_s);
                const QByteArray more = data0.read(growth - spill.size());
                if (more.size() != growth - spill.size())
                    fail(u"The blobset is too small to add %1 new file(s)."_s.arg(newFiles));
                spill += more;

                if (!QDir().mkpath(m_backupDir))
                    fail(u"Could not create the backup folder: "_s + m_backupDir);
                QSaveFile spillOut(spillPath);
                if (!spillOut.open(QIODevice::WriteOnly) || spillOut.write(spill) != spill.size() || !spillOut.commit())
                    fail(u"Could not back up the game data behind the header: "_s + spillPath);
            }
        }

        currentFile = updateBlobsetPath;
        if (QFile::exists(updateBlobsetPath) && !QFile::remove(updateBlobsetPath))
            fail(u"Could not remove the old update blobset."_s);

        QFile updateFile(updateBlobsetPath);
        if (!updateFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fail(u"Failed to open update blobset file stream: "_s + updateFile.errorString());

        QList<HeaderUpdate> updates;
        updates.reserve(jobs.size());
        QList<NewEntry> added;

        for (qsizetype i = 0; i < jobs.size(); ++i) {
            const ModJob& job = jobs[i];
            currentFile = job.absolutePath;

            emit updateStatus(u"Modifying File: %1"_s.arg(job.relativePath));
            const HeaderUpdate update = writeJobV1(updateFile, job, original);
            if (job.isNew) {
                added.append({static_cast<quint32>(job.newHash), update.mainPart, update.vram});
                emit updateConsole(u"New asset: %1 (hash %2)"_s.arg(job.relativePath, BlobsetHash::toHex(job.newHash, 8)));
            } else {
                updates.append(update);
            }

            emitProgress(static_cast<int>(((i + 1) * 100) / jobs.size()));
            emit updateConsole(u"Appended packed mod chunk: %1"_s.arg(job.relativePath));
        }

        if (growth > 0) {
            // Game files in data-0 that the bigger header covers are copied as they are (still compressed) into the
            // mod container; their entries point there.
            const qint64 newHeaderEnd = headerEnd + growth;
            QSet<quint32> replaced;
            for (const HeaderUpdate& u : std::as_const(updates))
                replaced.insert(u.index);

            QFile data0(m_blobsetFile);
            if (!data0.open(QIODevice::ReadOnly))
                fail(u"Could not open the blobset: "_s + data0.errorString());

            const auto overlaps = [&](quint32 offset, quint32 size) {
                return size > 0 && qint64(offset) < newHeaderEnd && qint64(offset) + size > headerEnd;
            };
            const auto copySection = [&](quint32 offset, quint32 compressed, quint32 uncompressed) {
                Section section;
                section.offset = toU32(updateFile.pos(), u"The update blobset"_s);
                section.compressedSize = compressed;
                section.uncompressedSize = uncompressed;
                if (compressed > 0) {
                    if (!data0.seek(offset))
                        fail(u"Could not seek in the blobset."_s);
                    const QByteArray bytes = data0.read(compressed);
                    if (bytes.size() != qsizetype(compressed))
                        fail(u"Could not read a game file from the blobset."_s);
                    writeBytes(updateFile, bytes);
                }
                return section;
            };

            int movedCount = 0;
            for (qsizetype i = 0; i < original.entries.size(); ++i) {
                const BlobsetEntry& e = original.entries[i];
                if (e.blobSetNumber != 0 || replaced.contains(quint32(i)))
                    continue;
                if (!overlaps(e.mainFinalOffset, e.mainCompressedSize) && !overlaps(e.vramFinalOffset, e.vramCompressedSize))
                    continue;

                HeaderUpdate moved;
                moved.index = quint32(i);
                moved.mainPart = copySection(e.mainFinalOffset, e.mainCompressedSize, e.mainUnCompressedSize);
                moved.vram = copySection(e.vramFinalOffset, e.vramCompressedSize, e.vramUnCompressedSize);
                updates.append(moved);
                ++movedCount;
            }
            emit updateConsole(u"Moved %1 game file(s) out of the way of the bigger header."_s.arg(movedCount));
        }

        updateFile.close();

        currentFile = m_blobsetFile;
        if (added.isEmpty())
            writeHeaderV1(updates, hashSize, modBlobsetNumber);
        else
            writeFullHeaderV1(originalHeader, original, updates, added, hashSize, modBlobsetNumber);

        addNewFilesToMapping(jobs);
    } catch (const std::exception& ex) {
        emit taskFinished(describeFailure(currentFile, ex));
        return;
    }

    emit taskSucceeded();
    emit taskFinished(m_lzmaV2 ? u"V2 modification routine compiled successfully."_s
                               : u"V1 modification routine compiled successfully."_s);
}

// ===========================================================================
// Modify Blobset Version 4 (BigAnt's newer games)
// ===========================================================================

ModifyWorker::HeaderUpdate ModifyWorker::writeJobV4(const ModJob& job, const QString& physicalPath) const {
    const QDataStream::ByteOrder order = streamOrder();

    if (!QDir().mkpath(QFileInfo(physicalPath).absolutePath()))
        fail(u"Could not create the folder for: "_s + physicalPath);

    QFile out(physicalPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        fail(u"Could not open '%1' for writing: %2"_s.arg(physicalPath, out.errorString()));

    HeaderUpdate update;
    update.index = job.headerIndex;

    if (job.extension == u"dds"_s) {
        // main = raw mini TXPK header, vram = the whole DDS file in 256 KB ZSTD chunks.
        const QByteArray dds = readWholeFile(job.absolutePath);
        QByteArray vram;
        const QByteArray miniHeader = buildMiniHeader(job, dds, vram); // V4 is PC only: vram is the whole .dds.

        update.mainPart = writeRaw(out, miniHeader);
        update.vram = m_lzmaV3 ? writeLzmaV3(out, dds) : writeZstd(out, dds, false, order);
    } else if (job.extension == u"txpk"_s) {
        const QByteArray data = readWholeFile(job.absolutePath);
        const qint64 mainSize = job.mainUncompressed;
        const qint64 vramSize = job.vramUncompressed;

        if (data.size() < mainSize + vramSize)
            fail(u"%1 is smaller than its xml info says (%2 bytes expected)."_s.arg(job.relativePath).arg(mainSize + vramSize));

        update.mainPart = m_lzmaV3 ? writeLzmaV3(out, data.left(mainSize)) : writeZstd(out, data.left(mainSize), true, order);
        update.vram = m_lzmaV3 ? writeLzmaV3(out, data.mid(mainSize, vramSize))
                               : writeZstd(out, data.mid(mainSize, vramSize), false, order);
    } else if (job.generic) {
        // Any other type (str, lua, mesh, ...): split and compressed (ZSTD / V3 LZMA) like the entry it replaces.
        QByteArray mainData, vramData;
        splitGeneric(readWholeFile(job.absolutePath), job.origMainUncompressed, job.origVramUncompressed,
                     job.relativePath, mainData, vramData);
        update.mainPart = job.origMainCompressed == job.origMainUncompressed
                          ? writeRaw(out, mainData)
                          : (m_lzmaV3 ? writeLzmaV3(out, mainData) : writeZstd(out, mainData, true, order));
        update.vram = vramData.isEmpty() || job.origVramCompressed == job.origVramUncompressed
                      ? writeRaw(out, vramData)
                      : (m_lzmaV3 ? writeLzmaV3(out, vramData) : writeZstd(out, vramData, false, order));
    } else {
        // m3mp, wem, bnk: stored as they are, no vram section.
        update.mainPart = appendFile(out, job.absolutePath);
        update.vram = writeRaw(out, QByteArray());
    }

    out.close();
    return update;
}

void ModifyWorker::writeHeaderV4(const QList<HeaderUpdate>& updates, quint32 filesCount) const {
    QFile header(m_headerFile.isEmpty() ? m_blobsetFile : m_headerFile);
    if (!header.open(QIODevice::ReadWrite))
        fail(u"Could not open the blobset header for writing: "_s + header.errorString());

    QDataStream out(&header);
    out.setByteOrder(streamOrder());

    // V4: the size table comes after the magic, the files count and one hash per file.
    // V3: every 24 byte entry starts with its sizes.
    const qint64 sizeTableOffset = m_lzmaV3 ? kV3PreambleSize
                                            : kV4PreambleSize + kV4HashSize * static_cast<qint64>(filesCount);
    const qint64 entrySize = m_lzmaV3 ? kV3EntrySize : kV4EntrySize;

    for (const HeaderUpdate& update : updates) {
        header.seek(sizeTableOffset + entrySize * static_cast<qint64>(update.index));

        out << update.mainPart.compressedSize << update.mainPart.uncompressedSize
            << update.vram.compressedSize << update.vram.uncompressedSize;
    }

    if (out.status() != QDataStream::Ok)
        fail(u"Failed writing the blobset header."_s);
}

void ModifyWorker::modifyV4() {
    QString currentFile;

    try {
        const QString label = m_lzmaV3 ? u"V3"_s : u"V4"_s;
        emit updateConsole(u"Validating structural mapping for %1 Modification..."_s.arg(label));

        const QHash<QString, quint64> mapping = loadMappingData();
        if (mapping.isEmpty()) {
            emit taskFinished(u"Mapping data is missing or corrupted. Try 'Update File Mapping Data' in Settings "
                                     u"or validate the game files."_s);
            return;
        }

        BlobsetFile blobset;
        {
            QFile headerFile(m_blobsetFile);
            if (!headerFile.open(QIODevice::ReadOnly)) {
                emit taskFinished(u"Failed to open %1 Blobset file."_s.arg(label));
                return;
            }

            QDataStream in(&headerFile);
            const auto layout = m_lzmaV3 ? CoreDefs::BlobsetVersion::V3 : CoreDefs::BlobsetVersion::V4;
            if (!blobset.read(in, layout, m_isBigEndian, 0))   // V3 / V4 have no SHA1.
                fail(u"Invalid BLOB Magic Signature."_s);
        }

        // V3 entries are addressed by 32 bit hashes (like V1), V4 by 64 bit ones.
        QList<ModJob> jobs = planJobs(kV4Extensions, mapping, blobset, !m_lzmaV3);
        const bool loaderMode = !m_modLoaderDir.isEmpty() && !m_lzmaV3;
        if (jobs.isEmpty()) {
            if (loaderMode) {
                // No mods left: an empty mods folder makes the game run untouched.
                QDir(QDir(m_modLoaderDir).filePath(u"data"_s)).removeRecursively();
                QFile::remove(QDir(m_modLoaderDir).filePath(u"blobset.base"_s));
                emit taskFinished(u"No mod files detected in workspace: the Mod Loader folder was emptied."_s);
                return;
            }
            emit taskFinished(u"No mod files detected in workspace."_s);
            return;
        }

        // The game's data folder; with the Mod Loader the files go into <game>\BlobsetMods\data instead.
        QString gameDir = QFileInfo(m_blobsetFile).absolutePath();
        m_headerFile = m_blobsetFile;

        currentFile = m_blobsetFile;
        if (loaderMode) {
            prepareModLoaderFolder(gameDir);
            gameDir = QDir(m_modLoaderDir).filePath(u"data"_s);
            m_headerFile = QDir(gameDir).filePath(QFileInfo(m_blobsetFile).fileName());
            emit updateConsole(u"Mod Loader: building the mods in %1 (the game's files aren't touched)."_s
                                   .arg(QDir::toNativeSeparators(m_modLoaderDir)));
        } else {
            // Keep the untouched header before the first modification ever happens.
            backupBlobsetHeader();
        }

        // New assets get header entries appended (V3 / V4 headers aren't sorted).
        const quint32 filesCount = appendHeaderEntriesV4(blobset, jobs);

        QList<HeaderUpdate> updates;
        updates.reserve(jobs.size());
        QByteArray modNames;   // Mod Loader: BlobsetMods\mods.txt, the loader logs the mods by these names.

        for (qsizetype i = 0; i < jobs.size(); ++i) {
            const ModJob& job = jobs[i];
            const QString physicalPath = QDir(gameDir).filePath(job.folderHash + u'/' + job.fileHash);
            if (loaderMode)
                modNames += (u"data\\"_s + job.folderHash + u'\\' + job.fileHash + u'\t'
                             + QString(job.relativePath).replace(u'/', u'\\') + u"\r\n"_s).toUtf8();
            currentFile = physicalPath;

            emit updateStatus(u"Modifying File: %1"_s.arg(job.relativePath));

            if (!loaderMode && !backupFilesV3V4(physicalPath, job.folderHash, job.fileHash))
                fail(u"Could not back up the original asset."_s);

            updates.append(writeJobV4(job, physicalPath));

            emitProgress(static_cast<int>(((i + 1) * 100) / jobs.size()));
            emit updateConsole(u"Injected and compressed: %1"_s.arg(job.relativePath));
        }

        currentFile = m_blobsetFile;
        writeHeaderV4(updates, filesCount);
        addNewFilesToMapping(jobs);

        if (loaderMode) {
            modNames.prepend((u"data\\"_s + QFileInfo(m_blobsetFile).fileName()
                              + u"\tBlobset header (with the mods' entries)\r\n"_s).toUtf8());
            QSaveFile names(QDir(m_modLoaderDir).filePath(u"mods.txt"_s));
            if (!names.open(QIODevice::WriteOnly) || names.write(modNames) != modNames.size() || !names.commit())
                emit updateConsole(u"Warning: could not write mods.txt (the loader's log shows file hashes only)."_s);
        }
    } catch (const std::exception& ex) {
        emit taskFinished(describeFailure(currentFile, ex));
        return;
    }

    if (!m_modLoaderDir.isEmpty() && !m_lzmaV3) {
        emit taskSucceeded();
        emit taskFinished(u"Mods built for the Blobset Mod Loader in %1. The game's files weren't changed."_s
                              .arg(QDir::toNativeSeparators(m_modLoaderDir)));
        return;
    }
    emit taskSucceeded();
    emit taskFinished(m_lzmaV3 ? u"V3 modification routine compiled successfully."_s
                               : u"V4 modification routine compiled successfully."_s);
}

/// Mod Loader mode: <game>\BlobsetMods gets a fresh copy of the game's header (the mods are always rebuilt from the
/// game's current version), blobset.base (so the loader can tell when the game was updated) and the loader DLL.
void ModifyWorker::prepareModLoaderFolder(const QString& gameDataDir) {
    // The game's own files have to be the originals: a header modified the classic way would end up in the copy.
    const QString backupHeader = QDir(m_backupDir).absoluteFilePath(QFileInfo(m_blobsetFile).fileName());
    if (QFile::exists(backupHeader) && readWholeFile(backupHeader) != readWholeFile(m_blobsetFile))
        fail(u"The game's files are still modified from an earlier (classic) Modify. Restore them first "
             u"(Steam: Verify integrity of game files), then delete %1."_s.arg(QDir::toNativeSeparators(backupHeader)));

    const QDir modsRoot(m_modLoaderDir);
    const QString modsData = modsRoot.filePath(u"data"_s);
    QDir(modsData).removeRecursively();   // Always rebuilt from scratch: removed mods disappear too.
    if (!QDir().mkpath(modsData))
        fail(u"Could not create the mods folder: "_s + QDir::toNativeSeparators(modsData));

    const QString headerCopy = QDir(modsData).filePath(QFileInfo(m_blobsetFile).fileName());
    if (!QFile::copy(m_blobsetFile, headerCopy))
        fail(u"Could not copy the blobset header to "_s + QDir::toNativeSeparators(headerCopy));

    // blobset.base: the game's header this build belongs to (the loader turns the mods off when it changes).
    const QFileInfo header(m_blobsetFile);
    QSaveFile base(modsRoot.filePath(u"blobset.base"_s));
    const QByteArray stamp = "size=" + QByteArray::number(header.size()) + "\r\nmtime=" + QByteArray::number(header.lastModified().toUTC().toMSecsSinceEpoch()) + "\r\n";

    if (!base.open(QIODevice::WriteOnly) || base.write(stamp) != stamp.size() || !base.commit())
        fail(u"Could not write blobset.base in "_s + QDir::toNativeSeparators(m_modLoaderDir));

    // The loader itself: <Blobset Tools>\modloader\dinput8.dll -> next to the game's executable.
    QDir gameRoot(gameDataDir);
    gameRoot.cdUp();
    const QString dllSource = QDir(QCoreApplication::applicationDirPath()).filePath(u"modloader/dinput8.dll"_s);
    const QString dllTarget = gameRoot.filePath(u"dinput8.dll"_s);
    if (QFile::exists(dllSource)) {
        if (!QFile::exists(dllTarget) || readWholeFile(dllSource) != readWholeFile(dllTarget)) {
            QFile::remove(dllTarget);
            if (QFile::copy(dllSource, dllTarget))
                emit updateConsole(u"Installed the Blobset Mod Loader: "_s + QDir::toNativeSeparators(dllTarget));
            else
                emit updateConsole(u"Warning: could not copy dinput8.dll to "_s + QDir::toNativeSeparators(dllTarget));
        }
    } else if (!QFile::exists(dllTarget)) {
        emit updateConsole(u"Warning: the Blobset Mod Loader isn't installed. Build BlobsetModLoader and put "
                           u"dinput8.dll in %1 (or next to the game's exe)."_s
                               .arg(QDir::toNativeSeparators(QFileInfo(dllSource).absolutePath())));
    }
}

// ===========================================================================
// New assets
// ===========================================================================

/// Rewrites the whole V1 / V2 header: the original entries (with the updates applied) plus the new ones, sorted by
/// path hash like the games' own headers. The SHA1 slot and magic stay as the game shipped them.
void ModifyWorker::writeFullHeaderV1(const QByteArray& originalHeader, const BlobsetFile& original,
                                     const QList<HeaderUpdate>& updates, const QList<NewEntry>& added,
                                     qsizetype hashSize, quint32 modBlobsetNumber) const {
    struct Row {
        quint32 mainOffset, mainCompressed, mainUncompressed;
        quint32 vramOffset, vramCompressed, vramUncompressed;
        quint32 hash, blob;
    };

    std::vector<Row> rows;
    rows.reserve(size_t(original.entries.size() + added.size()));
    for (const BlobsetEntry& e : original.entries)
        rows.push_back({e.mainFinalOffset, e.mainCompressedSize, e.mainUnCompressedSize, e.vramFinalOffset,
                        e.vramCompressedSize, e.vramUnCompressedSize, e.folderFileHash32, e.blobSetNumber});
    for (const HeaderUpdate& u : updates) {
        Row& r = rows[u.index];
        r = {u.mainPart.offset, u.mainPart.compressedSize, u.mainPart.uncompressedSize,
             u.vram.offset, u.vram.compressedSize, u.vram.uncompressedSize, r.hash, modBlobsetNumber};
    }
    for (const NewEntry& n : added)
        rows.push_back({n.mainPart.offset, n.mainPart.compressedSize, n.mainPart.uncompressedSize,
                        n.vram.offset, n.vram.compressedSize, n.vram.uncompressedSize, n.hash, modBlobsetNumber});
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.hash < b.hash; });

    if (originalHeader.size() < hashSize + 4)
        fail(u"The backed up blobset header is too small."_s);

    QByteArray header;
    {
        QDataStream out(&header, QIODevice::WriteOnly);
        out.setByteOrder(streamOrder());
        out.writeRawData(originalHeader.constData(), int(hashSize + 4));   // SHA1 slot + magic
        out << static_cast<quint32>(modBlobsetNumber + 1) << static_cast<quint32>(rows.size());
        for (const Row& r : rows)
            out << r.mainOffset << r.mainCompressed << r.mainUncompressed << r.vramOffset << r.vramCompressed
                << r.vramUncompressed << r.hash << r.blob;
        if (out.status() != QDataStream::Ok)
            fail(u"Failed building the blobset header."_s);
    }

    QFile file(m_blobsetFile);
    if (!file.open(QIODevice::ReadWrite))
        fail(u"Could not open the blobset header for writing: "_s + file.errorString());
    writeBytes(file, header);
    emit const_cast<ModifyWorker*>(this)->updateConsole(
        u"Header rewritten with %1 new file(s) (%2 files)."_s.arg(added.size()).arg(rows.size()));
}

/// V3 / V4: appends a header entry for every new asset (sizes are filled in by writeHeaderV4) and gives the jobs
/// their header index. Returns the new files count.
quint32 ModifyWorker::appendHeaderEntriesV4(const BlobsetFile& blobset, QList<ModJob>& jobs) const {
    const quint32 oldCount = blobset.filesCount;
    quint32 next = oldCount;
    for (ModJob& job : jobs)
        if (job.isNew)
            job.headerIndex = next++;
    if (next == oldCount)
        return oldCount;

    const QString headerPath = m_headerFile.isEmpty() ? m_blobsetFile : m_headerFile;
    QFile in(headerPath);
    if (!in.open(QIODevice::ReadOnly))
        fail(u"Could not open the blobset header: "_s + in.errorString());
    const QByteArray header = in.readAll();
    in.close();

    const qint64 expected = m_lzmaV3 ? kV3PreambleSize + kV3EntrySize * qint64(oldCount)
                                     : kV4PreambleSize + (kV4HashSize + kV4EntrySize) * qint64(oldCount);
    if (header.size() < expected)
        fail(u"The blobset header is smaller than its files count says."_s);

    QByteArray rebuilt;
    QDataStream out(&rebuilt, QIODevice::WriteOnly);
    out.setByteOrder(streamOrder());
    out.writeRawData(header.constData(), 4);   // Magic
    out << next;

    if (m_lzmaV3) {
        // Entries: main / vram compressed + uncompressed sizes, 32 bit hash, 0xEEEEEEEE.
        out.writeRawData(header.constData() + kV3PreambleSize, int(kV3EntrySize * qint64(oldCount)));
        for (const ModJob& job : std::as_const(jobs))
            if (job.isNew)
                out << quint32(0) << quint32(0) << quint32(0) << quint32(0) << static_cast<quint32>(job.newHash)
                    << quint32(0xEEEEEEEEu);
    } else {
        // All hashes first, then all size entries.
        out.writeRawData(header.constData() + kV4PreambleSize, int(kV4HashSize * qint64(oldCount)));
        for (const ModJob& job : std::as_const(jobs))
            if (job.isNew)
                out << job.newHash;
        out.writeRawData(header.constData() + kV4PreambleSize + kV4HashSize * qint64(oldCount),
                         int(kV4EntrySize * qint64(oldCount)));
        for (const ModJob& job : std::as_const(jobs))
            if (job.isNew)
                out << quint32(0) << quint32(0) << quint32(0) << quint32(0);
    }
    if (out.status() != QDataStream::Ok)
        fail(u"Failed building the blobset header."_s);

    QSaveFile save(headerPath);
    if (!save.open(QIODevice::WriteOnly) || save.write(rebuilt) != rebuilt.size() || !save.commit())
        fail(u"Could not write the blobset header: "_s + save.errorString());

    for (const ModJob& job : std::as_const(jobs))
        if (job.isNew)
            emit const_cast<ModifyWorker*>(this)->updateConsole(
                u"New asset: %1 (hash %2)"_s.arg(job.relativePath, BlobsetHash::toHex(job.newHash, m_lzmaV3 ? 8 : 16)));
    return next;
}

/// New assets go into the file mapping too, so the browser shows them and later runs find them by name.
void ModifyWorker::addNewFilesToMapping(const QList<ModJob>& jobs) {
    QList<std::pair<QString, quint64>> added;
    for (const ModJob& job : jobs)
        if (job.isNew)
            added.append({job.relativePath, job.newHash});
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
