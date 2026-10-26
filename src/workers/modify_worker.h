#ifndef MODIFY_WORKER_H
#define MODIFY_WORKER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The author may be contacted at:
 * Discord: Wouldubeinta
 *
 * History:
 * [Wouldubeinta]  08/10/2026  Ported to Qt 6 (Modify Worker)
 */

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class BlobsetFile;
class QFile;
class QIODevice;

/// Injects modified assets into a blobset.
///
///  - V1: the mods are appended to the "-1" container (LZMA chunks) and the original header is patched to point at them.
///  - V4: every modified asset replaces its physical file (the original is moved to the backup folder first)
///        and the sizes in the blobset header are patched (ZSTD chunks).
class ModifyWorker : public QObject {
    Q_OBJECT

public:
    // gameId   : GameInfo/GameId from the game INI (newer games store DDS paths as ".badds").
    // platform : static_cast<int>(CoreDefs::Platforms) - PS3 builds its texture descriptor from the imported .dds.
    explicit ModifyWorker(const QString& blobsetFile, int version, bool isBigEndian,
                          const QString& modsDir, const QString& backupDir, const QString& mappingFile,
                          int gameId = 0, int platform = 0, QObject* parent = nullptr);
    ~ModifyWorker() override = default;

    // V3 only: the game key its hashes start from (see BlobsetHash::gameKeyFor()), needed to add new assets.
    void setGameKey(const QString& gameKey) { m_gameKey = gameKey; }

    // V4 only: build the mods into this folder (<game>\BlobsetMods) for the Blobset Mod Loader (dinput8.dll)
    // instead of changing the game's files. Empty: the classic in place modification.
    void setModLoaderDir(const QString& modsRoot) { m_modLoaderDir = modsRoot; }

    // The folders the mod files come from, lowest priority first: when two of them have a file with the same
    // path, the later one wins (Mod Manager load order). Empty: only the workspace mods folder.
    void setModSources(const QStringList& folders) { m_modSources = folders; }

    // The file types Modify puts into a blobset of this CoreDefs::BlobsetVersion (lower case, no dot).
    static QStringList supportedExtensions(int version);

public slots:
    void modify_blobset();

signals:
    void progressUpdated(int percentage);
    void updateConsole(const QString& message);
    void updateStatus(const QString& result);
    void taskFinished(const QString& message);
    void taskSucceeded();   // Right before taskFinished() when the blobset was written (Load Game starts the game).

private:
    // Progress only changes 100 times a task: emitting it per file would queue thousands of UI events.
    void emitProgress(int percent) {
        if (percent == m_lastProgress)
            return;
        m_lastProgress = percent;
        emit progressUpdated(percent);
    }
    int m_lastProgress = -1;

    QString m_blobsetFile;
    int m_version;
    bool m_isBigEndian;
    QString m_modsDir;
    QString m_backupDir;
    QString m_mappingFile;
    int m_gameId;
    int m_platform;
    QString m_gameKey;
    QString m_modLoaderDir;
    QStringList m_modSources;
    QString m_headerFile;   // The V3 / V4 header modifyV4() writes (the game's, or the copy in the mods folder).
    void prepareModLoaderFolder(const QString& gameDataDir);

    // A file found in the mods folder.
    struct ModFile {
        QString absolutePath;
        QString relativePath;   // Relative to the mods folder, '/' separated.
        QString extension;      // Lower case, without the dot.
    };

    // A mod file that has been matched to a blobset header entry.
    struct ModJob {
        QString absolutePath;
        QString relativePath;
        QString extension;
        quint32 headerIndex = 0;
        bool isNew = false;             // Not in the blobset yet: gets a new header entry.
        quint64 newHash = 0;            // Its hash, from its file name (see hash_utility.h).
        QString folderHash;             // V4: physical location of the asset.
        QString fileHash;
        quint32 mainUncompressed = 0;   // TXPK only: split point of the file (from its xml info).
        quint32 vramUncompressed = 0;
    };

    // Where a section (main / vram) ended up inside the container and how big it is.
    struct Section {
        quint32 offset = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
    };

    struct HeaderUpdate {
        quint32 index = 0;
        Section mainPart;
        Section vram;
    };

    // A new V1 / V2 header entry.
    struct NewEntry {
        quint32 hash = 0;
        Section mainPart;
        Section vram;
    };

    using HashIndex = QHash<quint64, quint32>;   // Path hash -> blobset header index.

    void modifyV1();
    //void modifyV2();
    //void modifyV3();
    void modifyV4();

    // Planning (read only, nothing on disk is touched until every mod file has been validated)
    QHash<QString, quint64> loadMappingData() const;
    QList<ModFile> collectModFiles(const QStringList& extensions) const;
    ModJob resolveJob(const ModFile& file, const QHash<QString, quint64>& mapping,
                      const HashIndex& indexByHash, const BlobsetFile& blobset) const;
    QList<ModJob> planJobs(const QStringList& extensions, const QHash<QString, quint64>& mapping,
                           const BlobsetFile& blobset, bool use64BitHash) const;
    static HashIndex buildHashIndex(const BlobsetFile& blobset, bool use64BitHash);

    // Mini TXPK (DDS) header
    // Mini TXPK header for an imported .dds; `vram` receives the matching vram section
    // (PC: the whole .dds, PS3: pixels converted for the CellGcmTexture built from the .dds).
    QByteArray buildMiniHeader(const ModJob& job, const QByteArray& dds, QByteArray& vram) const;
    QString miniPathFor(const QString& relativePath) const;

    // Writing
    HeaderUpdate writeJobV1(QFile& out, const ModJob& job, const BlobsetFile& original) const;
    HeaderUpdate writeJobV4(const ModJob& job, const QString& physicalPath) const;
    void writeHeaderV1(const QList<HeaderUpdate>& updates, qsizetype hashSize, quint32 modBlobsetNumber) const;
    void writeHeaderV4(const QList<HeaderUpdate>& updates, quint32 filesCount) const;
    void writeFullHeaderV1(const QByteArray& originalHeader, const BlobsetFile& original,
                           const QList<HeaderUpdate>& updates, const QList<NewEntry>& added, qsizetype hashSize,
                           quint32 modBlobsetNumber) const;
    quint32 appendHeaderEntriesV4(const BlobsetFile& blobset, QList<ModJob>& jobs) const;
    void addNewFilesToMapping(const QList<ModJob>& jobs);

    // Backups / paths
    bool backupFilesV3V4(const QString& sourcePath, const QString& folderName, const QString& fileName);
    void backupBlobsetHeader();
    QString blobPathFor(quint32 blobSetNumber) const;
    QDataStream::ByteOrder streamOrder() const {
        return m_isBigEndian ? QDataStream::BigEndian : QDataStream::LittleEndian;
    }

    // Section writers - each one returns where the data ended up and its sizes
    static Section writeRaw(QIODevice& out, const QByteArray& data);
    static Section appendFile(QIODevice& out, const QString& path);
    static Section writeLzma(QIODevice& out, const QByteArray& data, qint64 chunkSize, QDataStream::ByteOrder order,
                             bool lzmaV2 = false);
    bool m_lzmaV2 = false;  // V2 blobset (RLL4): writeLzma() writes V2 sections.
    static Section writeLzmaV3(QIODevice& out, const QByteArray& data);
    bool m_lzmaV3 = false;  // V3 blobset (Cricket 19): modifyV4() writes V3 sections and V3 header entries.
    static Section writeZstd(QIODevice& out, const QByteArray& data, bool compressPartialChunks, QDataStream::ByteOrder order);
};

#endif // MODIFY_WORKER_H

