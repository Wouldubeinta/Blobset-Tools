#ifndef COMPRESSION_IO_H
#define COMPRESSION_IO_H

#include <QByteArray>

class PackageStream;

namespace CompressionIO {
    
    // ZStandard Compression API
    QByteArray compressZstd(const QByteArray& input, int compressionLevel = 4);
    QByteArray decompressZstd(const QByteArray& input, bool isCompressed = true);

    // LZMA (7-zip) Compression API mapped for v1/v2 '.lzma' backwards compatibility
    QByteArray compressLzma(const QByteArray& inputData);
    QByteArray decompressLzma(const QByteArray& compressedData, quint32 uncompressedSize);

    // --- V2 LZMA sections (Rugby League Live 4) ---------------------------------------------------------------
    // Unlike V1 there is no chunk table: a section is a run of chunks, each an int32 (chunk size including these
    // 4 bytes) followed by the chunk data. Every chunk holds kLzmaV2ChunkSize uncompressed bytes (the last one the
    // rest); data of exactly that size is stored as it is, anything else is LZMA (5 byte properties first).
    constexpr qint64 kLzmaV2ChunkSize = 32768;

    // Reads a section from the stream's current position until `uncompressedSize` bytes were produced, or stops
    // early once `maxBytes` were (maxBytes >= 0). Throws std::runtime_error on corrupt data.
    QByteArray readLzmaSectionV2(PackageStream& in, qint64 uncompressedSize, qint64 maxBytes = -1);

    // --- V1 LZMA blocks (chunk count, [size + 4] * count, then per chunk: uncompressed size + data) ------------
    // A chunk whose size equals its uncompressed size is normally stored as it is, but a small chunk can compress
    // to exactly its own size (AFL Live's ~80 byte mini texture headers do): such a chunk is recognised by its
    // LZMA properties and decoded. Use this instead of comparing the sizes.
    QByteArray decodeV1Chunk(const QByteArray& chunk, quint32 uncompressedSize);

    // The same at the section level: a section whose compressed size equals its uncompressed size is normally
    // stored, unless it is a whole V1 LZMA block (its chunk table adds up to exactly the section size and the first
    // chunk starts with LZMA properties). Leaves the stream where it was.
    bool isStoredV1Section(PackageStream& in, qint64 offset, quint32 compressedSize, quint32 uncompressedSize);

    // Builds the section readLzmaSectionV2() reads, sizes in the given byte order.
    QByteArray buildLzmaSectionV2(const QByteArray& data, bool bigEndian);

    // --- V3 LZMA sections (Cricket 19) ------------------------------------------------------------------------
    // V2's chunking (32 KB uncompressed per chunk, int32 size incl. these 4 bytes, a full size chunk is stored),
    // but every size is XORed with kLzmaV3SizeKey and the LZMA data has no properties header: it's always
    // lc 3 / lp 0 / pb 2 with a 2 MB dictionary. Sizes are little endian (PC only).
    constexpr quint32 kLzmaV3SizeKey = 0x62676E74;

    QByteArray readLzmaSectionV3(PackageStream& in, qint64 uncompressedSize, qint64 maxBytes = -1);
    QByteArray buildLzmaSectionV3(const QByteArray& data);
}

#endif // COMPRESSION_IO_H
