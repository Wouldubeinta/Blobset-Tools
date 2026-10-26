#ifndef M3MP_H
#define M3MP_H

#include <QString>
#include <QVector>
#include "package_stream.h"

class M3MP {
public:
    struct UncompressedDataInfo {
        quint32 offset = 0;
        quint32 size = 0;
        quint32 filePathOffset = 0;
        quint32 reserved = 0; // Always 0
    };

    struct UnCompressedEntry {
        UncompressedDataInfo info;
        QString filePath = "";
    };

    struct ChunkCompressedDataInfo {
        quint32 offset = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
    };

    struct CompressedEntry {
        ChunkCompressedDataInfo info;
    };

    const QVector<UnCompressedEntry>& getUnCompressedEntries() const { return uncompressedEntries; }
    const QVector<CompressedEntry>& getCompressedEntries() const { return compressedEntries; }

    // Reads the file table, the paths and the chunk table. Throws std::runtime_error on a bad file.
    void deserialize(PackageStream& input, bool isBigEndian);

private:
    QVector<UnCompressedEntry> uncompressedEntries;
    QVector<CompressedEntry> compressedEntries;
};

#endif // M3MP_H
