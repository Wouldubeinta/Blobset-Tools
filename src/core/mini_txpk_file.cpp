#include "mini_txpk_file.h"
#include "package_stream.h"

#include <algorithm>

void MiniTxpk::deserialize(PackageStream& in, bool isBigEndian, int gameId) {
    in.setEndianness(isBigEndian ? PackageStream::Big : PackageStream::Little);

    const qint64 start = in.position();

    ddsWidth = in.readUInt32();
    ddsHeight = in.readUInt32();
    ddsImageType = in.readUInt32();
    ddsFilePathOffset = in.readUInt32();
    consoleDDSHeaderOffset = in.readUInt32();

    ddsFilePath = in.readNullTerminatedString();
    const qint64 storedPathLength = ddsFilePath.length(); // Before the replacements below change it.
    ddsFilePath.replace("/", "\\");

    if (gameId > 15) { // Newer BigAnt games map .badds to .dds
        ddsFilePath.replace(".badds", ".dds");
    }

    if (isBigEndian) {
        // Zero padding up to the console header, then the texture descriptor (PS3 CellGcmTexture or Xbox 360 header).
        bufferSize = static_cast<quint32>(std::max<qint64>(
            0, static_cast<qint64>(consoleDDSHeaderOffset) - ddsFilePathOffset - (storedPathLength + 1)));
        in.seek(start + consoleDDSHeaderOffset);
        isXbox360 = X360::looksLikeHeader(in.readUInt32());
        in.seek(start + consoleDDSHeaderOffset);
        if (isXbox360)
            x360.read(in);
        else
            gcm.read(in);
    }
}

void MiniTxpk::layoutConsoleHeader(bool alignTo16) {
    const quint32 unpadded = ddsFilePathOffset + static_cast<quint32>(ddsFilePath.toLatin1().size() + 1);
    consoleDDSHeaderOffset = alignTo16 ? (unpadded + 15u) & ~15u : unpadded;
    bufferSize = consoleDDSHeaderOffset - unpadded;
}

void MiniTxpk::serialize(PackageStream& out, bool isBigEndian) const {
    out.setEndianness(isBigEndian ? PackageStream::Big : PackageStream::Little);

    out.writeUInt32(ddsWidth);
    out.writeUInt32(ddsHeight);
    out.writeUInt32(ddsImageType);
    out.writeUInt32(ddsFilePathOffset);
    out.writeUInt32(consoleDDSHeaderOffset);
    out.writeNullTerminatedString(ddsFilePath);

    if (isBigEndian) {
        QByteArray emptyBuffer(bufferSize, 0);
        out.writeBytes(emptyBuffer);
        if (isXbox360)
            x360.write(out);
        else
            gcm.write(out);
    }
}

QByteArray MiniTxpk::writeAndReadHeader(bool isBigEndian) const {
    QByteArray headerData;
    QBuffer buffer(&headerData);
    buffer.open(QIODevice::WriteOnly);

    PackageStream out(&buffer);
    serialize(out, isBigEndian);

    return headerData;
}
