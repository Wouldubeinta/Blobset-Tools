#include "m3mp_file.h"

void M3MP::deserialize(PackageStream& input, bool isBigEndian) {
    input.setEndianness(isBigEndian ? PackageStream::Big : PackageStream::Little);

    if (input.readUInt32() != 1347236685) { // "M3MP"
        throw std::runtime_error("Invalid file format: This isn't a valid M3MP File (Wrong Magic Number).");
    }

    const quint32 filesCount = input.readUInt32();
    const quint32 chunksCount = input.readUInt32();
    input.readUInt32();   // Chunk table offset (the chunk table follows the paths anyway).

    // 16 bytes per file and 12 per chunk must fit, so a corrupt count can't ask for a huge allocation.
    if (qint64(filesCount) * 16 + qint64(chunksCount) * 12 > input.length() - input.position())
        throw std::runtime_error("Invalid M3MP header (bad file or chunk count).");

    uncompressedEntries.resize(filesCount);
    for (UnCompressedEntry& entry : uncompressedEntries) {
        entry.info.offset = input.readUInt32();
        entry.info.size = input.readUInt32();
        entry.info.filePathOffset = input.readUInt32();
        entry.info.reserved = input.readUInt32();
    }
    for (UnCompressedEntry& entry : uncompressedEntries)
        entry.filePath = input.readNullTerminatedString();

    compressedEntries.resize(chunksCount);
    for (CompressedEntry& entry : compressedEntries) {
        entry.info.offset = input.readUInt32();
        entry.info.compressedSize = input.readUInt32();
        entry.info.uncompressedSize = input.readUInt32();
    }
}
