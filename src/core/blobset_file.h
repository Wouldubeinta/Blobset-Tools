#ifndef BLOBSET_FILE_H
#define BLOBSET_FILE_H

#include "core_defs.h"

#include <QString>
#include <QVector>
#include <QByteArray>
#include <QDataStream>

struct BlobsetEntry {
    quint32 fileIndex = 0;
    QString folderHashName;
    QString fileHashName;
    quint32 mainFinalOffset = 0;
    quint32 mainCompressedSize = 0;
    quint32 mainUnCompressedSize = 0;
    quint32 vramFinalOffset = 0;
    quint32 vramCompressedSize = 0;
    quint32 vramUnCompressedSize = 0;
    quint32 folderFileHash32 = 0;
    quint64 folderFileHash64 = 0;   // quint64, not uint64_t: QDataStream only reads the Qt integer types everywhere.
    quint32 blobSetNumber = 0;
};

class BlobsetFile {
public:
    QByteArray sha1Hash; // 20 bytes
    quint32 magic = 0;
    quint32 blobsetCount = 0;
    quint32 filesCount = 0;
    QVector<BlobsetEntry> entries;

    // Parses standard Blobset (v1 - v4)
    bool read(QDataStream& in, CoreDefs::BlobsetVersion version, bool isBigEndian, int gameId);
};

#endif // BLOBSET_FILE_H
