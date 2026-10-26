#ifndef DDS_PROCESSOR_H
#define DDS_PROCESSOR_H

#include <QByteArray>
#include <QImage>

class DdsProcessor {
public:
    // The top mip level of the first image as RGBA8888 (null image when the DDS can't be read or converted).
    static QImage decodeDdsToQImage(const QByteArray &ddsFileData);
    // Xbox 360 textures: see x360_texture.h (X360::toDds / X360::fromDds).
};

#endif // DDS_PROCESSOR_H
