#include "utilities.h"

#include <algorithm>
#include <QtEndian>
#include <QSettings>
#include <QFile>
#include <QRegularExpression>
#include <QDir>

namespace Utilities {

    qint64 ChunkAmount(qint64 uncompressedSize, qint64 chunkSize) {
        if (uncompressedSize <= 0) return 1;
        if (chunkSize <= 0) return 1;
        
        // Accurate ceiling division resolving chunk fragmentation targets
        return (uncompressedSize + chunkSize - 1) / chunkSize;
    }

    QList<qint64> ChunkSizes(qint64 uncompressedSize, qint64 chunkAmount, qint64 chunkSize) {
        QList<qint64> chunkSizes;
        if (chunkAmount <= 0 || chunkSize <= 0) return chunkSizes;
        chunkSizes.reserve(chunkAmount);

        qint64 fullChunkSize = chunkSize;
        qint64 totalFullChunks = uncompressedSize / fullChunkSize;
        qint64 remainder = uncompressedSize % fullChunkSize;

        for (qint64 i = 0; i < chunkAmount; ++i) {
            if (i < totalFullChunks) {
                chunkSizes.append(fullChunkSize);
            } else if (i == totalFullChunks && remainder > 0) {
                chunkSizes.append(remainder);
            } else {
                chunkSizes.append(0);
            }
        }
        
        return chunkSizes;
    }

    QString getSteamGamesLocation(quint32 steamGameId) {
    #ifdef Q_OS_WIN
        QSettings reg("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Valve\\Steam", QSettings::NativeFormat);
        QString steamPath = reg.value("InstallPath").toString();

        if (steamPath.isEmpty()) return "";

        QString vdfPath = steamPath + "\\steamapps\\libraryfolders.vdf";
        QFile vdfFile(vdfPath);
        if (!vdfFile.open(QIODevice::ReadOnly | QIODevice::Text)) return "";

        QString vdfContent = vdfFile.readAll();

        // Matches "path", captures the directory string, then matches
        // forward through "apps" to make sure the targeted game ID exists inside this block.
        // [^}]* ensures we don't accidentally leak into a different library folder block.
        QString pattern = QString(R"raw("path"\s+"([^"]+)"[^}]+"apps"\s*\{[^}]*"%1")raw").arg(steamGameId);

        QRegularExpression re(pattern, QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch match = re.match(vdfContent);

        if (match.hasMatch()) {
            return QDir::fromNativeSeparators(match.captured(1));
        }
        return "";
    #else
        Q_UNUSED(steamGameId);
        return "";
    #endif
    }

    QByteArray readBlobsetHeader(const QString& backupFilePath) {
        QFile file(backupFilePath);
        if (file.open(QIODevice::ReadOnly)) {
            return file.readAll();
        }
        return QByteArray();
    }

    bool resetBlobset(const QString& headerFilePath, const QString& targetBlobsetPath) {
        const QByteArray header = readBlobsetHeader(headerFilePath);
        if (header.isEmpty()) return false;

        // Legacy (V1 / V2) header: [sha1 (optional, 20 bytes)] magic, blobset count, files count, entries.
        // The magic tells the byte order; the blobset count is how many containers the game itself ships.
        constexpr quint32 kBlobLittle = 1112493122; // "BLOB" read little endian
        constexpr quint32 kBlobBig = 1112297282;    // "BLOB" read big endian
        qsizetype magicAt = -1;
        bool bigEndian = false;
        for (const qsizetype offset : {qsizetype(0), qsizetype(20)}) {
            if (header.size() < offset + 8) continue;
            const quint32 value = qFromLittleEndian<quint32>(header.constData() + offset);
            if (value == kBlobLittle || value == kBlobBig) {
                magicAt = offset;
                bigEndian = (value == kBlobBig);
                break;
            }
        }
        if (magicAt < 0) return false; // Not a legacy blobset header: touch nothing.

        const quint32 gameContainers = bigEndian ? qFromBigEndian<quint32>(header.constData() + magicAt + 4)
                                                 : qFromLittleEndian<quint32>(header.constData() + magicAt + 4);

        // Overwrite the header in place. ReadWrite, never WriteOnly: WriteOnly truncates the file and would
        // throw away every asset stored after the header.
        QFile targetFile(targetBlobsetPath);
        if (!targetFile.open(QIODevice::ReadWrite)) return false;
        if (targetFile.write(header) != header.size()) return false;

        // Modify keeps the game data a grown header (new assets) covered in "<blobset>.spill": put it back.
        QString spillPath = headerFilePath;
        if (spillPath.endsWith(QStringLiteral(".header"))) {
            spillPath.chop(7);
            QFile spillFile(spillPath + QStringLiteral(".spill"));
            if (spillFile.open(QIODevice::ReadOnly)) {
                const QByteArray spill = spillFile.readAll();
                const quint32 files = bigEndian ? qFromBigEndian<quint32>(header.constData() + magicAt + 8)
                                                : qFromLittleEndian<quint32>(header.constData() + magicAt + 8);
                const qint64 headerEnd = magicAt + 12 + 32 * qint64(files);
                if (!targetFile.seek(headerEnd) || targetFile.write(spill) != spill.size()) return false;
            }
        }
        targetFile.close();

        // Remove only the mod container: the one right after the game's own containers (data-1 when the game
        // ships data-0 only, data-4 for RLL4's data-0 .. data-3). A game container is never deleted.
        const qsizetype pos = targetBlobsetPath.lastIndexOf(QStringLiteral("0.blob"));
        if (pos < 0) return true;

        const quint32 modContainer = std::max<quint32>(1, gameContainers);
        QString modBlob = targetBlobsetPath;
        modBlob.replace(pos, 6, QString::number(modContainer) + QStringLiteral(".blob"));
        if (modBlob != targetBlobsetPath && QFile::exists(modBlob)) {
            QFile::remove(modBlob);
        }

        return true;
    }

    QPair<QString, QString> getHashString32(quint32 hash) {
        // Folder: the top 8 bits as 2 hex digits. File: the lower 24 bits as 6 hex digits.
        return {QString::number(hash >> 24, 16).rightJustified(2, u'0'),
                QString::number(hash & 0x00FFFFFFu, 16).rightJustified(6, u'0')};
    }

    QPair<QString, QString> getHashString64(quint64 hash) {
        // Folder: the top byte as 2 hex digits. File: the other 7 bytes as 14 hex digits.
        // (Called for every entry of a V4 header, so no QString::arg chains here.)
        return {QString::number(hash >> 56, 16).rightJustified(2, u'0'),
                QString::number(hash & 0x00FFFFFFFFFFFFFFull, 16).rightJustified(14, u'0')};
    }
}
