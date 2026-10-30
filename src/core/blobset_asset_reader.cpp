#include "blobset_asset_reader.h"

#include "compression_io.h"
#include "core_defs.h"
#include "package_stream.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QtEndian>

#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

constexpr quint32 kZstdFrameMagic = 0xFD2FB528u;

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

qint32 readInt32(PackageStream& in) {
    return static_cast<qint32>(in.readUInt32());
}

bool isZstdFrame(const QByteArray& chunk) {
    return chunk.size() >= 4 && qFromLittleEndian<quint32>(chunk.constData()) == kZstdFrameMagic;
}

// V1/V2: chunk count, [compressed size + 4] * count, then per chunk: uncompressed size + data.
QByteArray readLzmaBlock(PackageStream& in) {
    const int chunkCount = readInt32(in);
    if (chunkCount <= 0)
        fail(u"Invalid chunk count in blobset entry"_s);

    QList<int> compressedSizes;
    compressedSizes.reserve(chunkCount);
    for (int i = 0; i < chunkCount; ++i) {
        const int size = readInt32(in) - 4; // Stored sizes include 4 extra bytes.
        if (size < 0)
            fail(u"Invalid chunk size in blobset entry"_s);
        compressedSizes.append(size);
    }

    QByteArray out;
    for (int i = 0; i < chunkCount; ++i) {
        const int uncompressedSize = readInt32(in);
        const QByteArray chunk = in.readBytes(compressedSizes[i]);

        // Stored when the sizes match, unless it is LZMA that happens to be as big as its data.
        out.append(CompressionIO::decodeV1Chunk(chunk, static_cast<quint32>(uncompressedSize)));
    }
    return out;
}

// V4: ZSTD chunks (size + 4, data) until `maxBytes` were consumed (maxBytes < 0: until the end of the file).
QByteArray readZstdChunks(PackageStream& in, qint64 maxBytes) {
    QByteArray out;
    const qint64 start = in.position();

    while (!in.atEnd() && (maxBytes < 0 || in.position() - start < maxBytes)) {
        const int dataSize = readInt32(in) - 4;
        if (dataSize < 0)
            fail(u"Invalid ZSTD chunk size in blobset entry"_s);

        const QByteArray chunk = in.readBytes(dataSize);
        out.append(CompressionIO::decompressZstd(chunk, isZstdFrame(chunk)));
    }
    return out;
}

} // namespace

QString BlobsetAssetReader::dataFilePath(const QString& headerPath, const BlobsetEntry& entry) {
    const QString relative = entry.folderHashName + u'/' + entry.fileHashName;
    const QDir headerDir = QFileInfo(headerPath).absoluteDir();
    const QString path = headerDir.filePath(relative);
    if (QFile::exists(path))
        return path;

    // <game>\BlobsetMods\data\<header>: files that aren't modded come from <game>\data.
    QDir modsRoot = headerDir;
    if (headerDir.dirName().compare(u"data"_s, Qt::CaseInsensitive) == 0 && modsRoot.cdUp()
        && modsRoot.dirName().compare(u"BlobsetMods"_s, Qt::CaseInsensitive) == 0) {
        QDir gameRoot = modsRoot;
        if (gameRoot.cdUp()) {
            const QString original = QDir(gameRoot.filePath(u"data"_s)).filePath(relative);
            if (QFile::exists(original))
                return original;
        }
    }
    return path;
}

QByteArray BlobsetAssetReader::read(const BlobsetEntry& entry, const QString& gameLocation, int version,
                                    int platform, qint64* mainSizeOut, bool includeVram) {
    const bool bigEndian = platform != static_cast<int>(CoreDefs::Platforms::Windows);
    const PackageStream::Endianness endian = bigEndian ? PackageStream::Big : PackageStream::Little;

    QByteArray mainPart;
    QByteArray vram;

    // V3 (Cricket 19) and V4 keep every asset in its own file; V3 uses XORed LZMA chunks instead of ZSTD.
    const bool v3 = version == static_cast<int>(CoreDefs::BlobsetVersion::V3);
    if (v3 || version == static_cast<int>(CoreDefs::BlobsetVersion::V4)) {
        const QString path = dataFilePath(gameLocation, entry);
        if (!QFile::exists(path))
            fail(u"Asset file not found: "_s + path);

        PackageStream in(path, endian, QIODevice::ReadOnly);

        in.seek(0);
        mainPart = (entry.mainCompressedSize == entry.mainUnCompressedSize)
                   ? in.readBytes(std::min<qint64>(entry.mainUnCompressedSize, in.length()))
                   : (v3 ? CompressionIO::readLzmaSectionV3(in, entry.mainUnCompressedSize)
                         : readZstdChunks(in, entry.mainCompressedSize));

        in.seek(entry.mainCompressedSize); // The vram section follows the main section.
        if (!includeVram) {
            // Main section only.
        } else if (entry.vramCompressedSize == entry.vramUnCompressedSize) {
            const qint64 count = std::min<qint64>(entry.vramUnCompressedSize,
                                                  std::max<qint64>(0, in.length() - in.position()));
            if (count > 0)
                vram = in.readBytes(count);
        } else {
            vram = v3 ? CompressionIO::readLzmaSectionV3(in, entry.vramUnCompressedSize) : readZstdChunks(in, -1);
        }
    }
    else if (version == static_cast<int>(CoreDefs::BlobsetVersion::V1) ||
             version == static_cast<int>(CoreDefs::BlobsetVersion::V2)) {
        QString path = gameLocation;
        const qsizetype pos = path.lastIndexOf(u"0.blob"_s);
        if (pos >= 0)
            path.replace(pos, 6, QString::number(entry.blobSetNumber) + u".blob"_s);
        if (!QFile::exists(path))
            fail(u"Blob container not found: "_s + path);

        PackageStream in(path, endian, QIODevice::ReadOnly);

        // V1: chunk table + chunks. V2 (RLL4): self describing 32 KB chunks, no table.
        const bool v2 = version == static_cast<int>(CoreDefs::BlobsetVersion::V2);

        // V1: a section whose sizes match can still be an LZMA block (see CompressionIO::isStoredV1Section).
        const bool mainStored = v2 ? entry.mainCompressedSize == entry.mainUnCompressedSize
                                   : CompressionIO::isStoredV1Section(in, entry.mainFinalOffset, entry.mainCompressedSize,
                                                                      entry.mainUnCompressedSize);
        const bool vramStored = v2 ? entry.vramCompressedSize == entry.vramUnCompressedSize
                                   : CompressionIO::isStoredV1Section(in, entry.vramFinalOffset, entry.vramCompressedSize,
                                                                      entry.vramUnCompressedSize);

        in.seek(entry.mainFinalOffset);
        mainPart = mainStored
                   ? in.readBytes(entry.mainUnCompressedSize)
                   : (v2 ? CompressionIO::readLzmaSectionV2(in, entry.mainUnCompressedSize) : readLzmaBlock(in));

        if (includeVram && entry.vramUnCompressedSize > 0) {
            in.seek(entry.vramFinalOffset);
            vram = vramStored
                       ? in.readBytes(entry.vramUnCompressedSize)
                       : (v2 ? CompressionIO::readLzmaSectionV2(in, entry.vramUnCompressedSize) : readLzmaBlock(in));
        }
    }
    else {
        fail(u"Blobset version %1 is not supported yet."_s.arg(version + 1));
    }

    if (mainSizeOut)
        *mainSizeOut = mainPart.size();
    return mainPart + vram;
}
