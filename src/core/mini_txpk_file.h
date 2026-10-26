#ifndef MINI_TXPK_FILE_H
#define MINI_TXPK_FILE_H

#include "gcm_texture.h"
#include "x360_texture.h"
#include "package_stream.h"
#include <QString>
#include <QDataStream>
#include <QByteArray>

struct MiniTxpk {
    quint32 ddsWidth = 0;
    quint32 ddsHeight = 0;
    quint32 ddsImageType = 1;
    quint32 ddsFilePathOffset = 20; // SizeOf uint(DDSWidth + DDSHeight + DDSImageType + DDSFilePathOffset + ConsoleDDSHeaderOffset)
    quint32 consoleDDSHeaderOffset = 0;
    QString ddsFilePath;

    // Console only (PS3 / Xbox 360): zero padding after the path, then the texture descriptor:
    // PS3 the 24 byte RSX CellGcmTexture, Xbox 360 the 52 byte D3D texture header (isXbox360).
    quint32 bufferSize = 0;
    Gcm::CellGcmTexture gcm;
    bool isXbox360 = false;
    X360::TextureHeader x360;

    /// Console mini header built from scratch (import): sets consoleDDSHeaderOffset / bufferSize for the current
    /// ddsFilePath. `alignTo16` puts the descriptor on the next 16 byte boundary, like every PS3 mini header in the
    /// and Xbox 360 mini header in the games does; without it there is no padding.
    void layoutConsoleHeader(bool alignTo16);

    void deserialize(PackageStream& in, bool isBigEndian, int gameId);
    void serialize(PackageStream& out, bool isBigEndian) const;
    QByteArray writeAndReadHeader(bool isBigEndian) const;
};

#endif // MINI_TXPK_FILE_H
