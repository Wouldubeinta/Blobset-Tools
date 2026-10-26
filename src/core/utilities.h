#ifndef UTILITIES_H
#define UTILITIES_H

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

namespace Utilities {
    // Number of chunks `uncompressedSize` bytes are split into (at least 1), and the size of each.
    qint64 ChunkAmount(qint64 uncompressedSize, qint64 chunkSize = 262144);
    QList<qint64> ChunkSizes(qint64 uncompressedSize, qint64 chunkAmount, qint64 chunkSize = 262144);

    // Steam library folder that holds `steamGameId` (Windows registry + libraryfolders.vdf), empty if unknown.
    QString getSteamGamesLocation(quint32 steamGameId);

    // Whole file, empty on failure.
    QByteArray readBlobsetHeader(const QString& backupFilePath);
    // Writes a V1 / V2 header backup over the blobset (and the data a grown header covered), removes the mod container.
    bool resetBlobset(const QString& headerFilePath, const QString& targetBlobsetPath);

    // { folder, file } hash names: V1 - V3 "ff" / "ffffff", V4 "ff" / "ffffffffffffff".
    QPair<QString, QString> getHashString32(quint32 hash);
    QPair<QString, QString> getHashString64(quint64 hash);
}

#endif // UTILITIES_H
