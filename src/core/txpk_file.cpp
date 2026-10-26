#include "txpk_file.h"

#include "core_defs.h"

#include <algorithm>

void TXPK::deserialize(PackageStream& input, bool isBigEndian) {
    PackageStream::Endianness endian = isBigEndian ? PackageStream::Big : PackageStream::Little;
    input.setEndianness(endian);

    const quint32 magic = input.readUInt32();

    // The file always starts with the bytes "KPXT": read little endian that's TXPK, big endian (console) it's KPXT.
    if (magic != static_cast<quint32>(CoreDefs::FileType::TXPK) &&
        magic != static_cast<quint32>(CoreDefs::FileType::KPXT)) {
        throw std::runtime_error("Invalid file format: This isn't a valid TXPK File (Wrong Magic Number).");
    }

    const quint32 filesCount = input.readUInt32();
    // 16 bytes per file must fit, so a corrupt count can't ask for a huge allocation.
    if (qint64(filesCount) * 16 > input.length() - input.position())
        throw std::runtime_error("Invalid TXPK header (bad file count).");
    entries.resize(filesCount);

    for (quint32 i = 0; i < filesCount; i++) {
        entries[i].DDSPathOffset = input.readUInt32();
        entries[i].DDSPathSize = input.readUInt32();
        entries[i].DDSDataOffset = input.readUInt32();
        entries[i].DDSDataSize = input.readUInt32();
    }

    for (quint32 i = 0; i < filesCount; i++) {
        const qint64 recordStart = entries[i].DDSPathOffset;
        input.seek(recordStart);
        entries[i].DDSWidth = input.readUInt32();
        entries[i].DDSHeight = input.readUInt32();
        entries[i].DDSImageType = input.readUInt32();
        entries[i].DDSFilePathOffset = input.readUInt32();
        entries[i].ConsoleDDSHeaderOffset = input.readUInt32();
        entries[i].DDSFilePath = input.readNullTerminatedString();

        if (isBigEndian) {
            // Zero padding up to the console header, then the 24 byte CellGcmTexture. Seek instead of skipping
            // bytes so a header offset that doesn't match the path length can't throw or misalign the read.
            const qint64 padding = static_cast<qint64>(entries[i].ConsoleDDSHeaderOffset) - entries[i].DDSFilePathOffset
                                   - (entries[i].DDSFilePath.toUtf8().size() + 1);
            entries[i].BufferSize = static_cast<quint32>(std::max<qint64>(0, padding));

            input.seek(recordStart + entries[i].ConsoleDDSHeaderOffset);
            entries[i].IsXbox360 = X360::looksLikeHeader(input.readUInt32());
            input.seek(recordStart + entries[i].ConsoleDDSHeaderOffset);
            if (entries[i].IsXbox360)
                entries[i].X360Header.read(input);
            else
                entries[i].GcmTexture.read(input);
        }
    }
}
