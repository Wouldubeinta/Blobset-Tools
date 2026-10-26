#ifndef TXPK_H
#define TXPK_H

#include <QString>
#include <QVector>
#include "gcm_texture.h"
#include "x360_texture.h"
#include "package_stream.h"

class TXPK {
public:
    struct Entry {
        quint32 DDSPathOffset = 0;
        quint32 DDSPathSize = 0;
        quint32 DDSDataOffset = 0;
        quint32 DDSDataSize = 0;

        quint32 DDSWidth = 0;
        quint32 DDSHeight = 0;
        quint32 DDSImageType = 1;
        quint32 DDSFilePathOffset = 20;
        quint32 ConsoleDDSHeaderOffset = 0;
        QString DDSFilePath = "";

        // ** For PS3 and Xbox 360 only **: zero padding after the path, then the texture descriptor
        // (PS3: RSX CellGcmTexture, Xbox 360: D3D texture header, IsXbox360).
        quint32 BufferSize = 0;
        Gcm::CellGcmTexture GcmTexture;
        bool IsXbox360 = false;
        X360::TextureHeader X360Header;
    };

    const QVector<Entry>& getEntries() const { return entries; }

    // Reads the header and every info record. Throws std::runtime_error on a bad file.
    void deserialize(PackageStream& input, bool isBigEndian);

private:
    QVector<Entry> entries;
};

#endif // TXPK_H
