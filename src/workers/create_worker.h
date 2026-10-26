#ifndef CREATE_WORKER_H
#define CREATE_WORKER_H

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
 * [Wouldubeinta]  13/12/2025  Created (C#)
 * [Wouldubeinta]  09/10/2026  Ported to Qt 6 (Create Worker, uses PackageStream)
 */

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class BlobsetFile;
class PackageStream;

/// Builds a brand new V1 blobset (header + data in a single container) out of the files in the mods folder.
///
/// Layout written (same as the original C# Create.BlobsetV1):
///   [sha1 (20 bytes, zeroed, optional)] magic, blobset count (1), files count,
///   one 32 byte entry per file (main offset/sizes, vram offset/sizes, path hash, blobset number = 0),
///   followed by the packed data (main section, then vram section, file after file).
class CreateWorker : public QObject {
    Q_OBJECT

public:
    // gameId       : GameInfo/GameId from the game INI (AFLL has no SHA1 slot, TableTop Cricket has none on big endian).
    // platform     : static_cast<int>(CoreDefs::Platforms) - PS3 builds its texture descriptor from the imported .dds.
    // sourceBlobset: the original blobset header. Only needed for ".dat" files (and xml infos without hashes), which carry
    //                their hash / sizes / compression in the original entry. It is read before the output is touched,
    //                so it may be the same file as outputBlobset.
    explicit CreateWorker(const QString& outputBlobset, int version, bool isBigEndian,
                          const QString& modsDir, const QString& mappingFile, const QString& sourceBlobset = QString(),
                          int gameId = 0, int platform = 0,
                          QObject* parent = nullptr);
    ~CreateWorker() override = default;

public slots:
    void create_blobset();

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

    QString m_outputBlobset;
    int m_version;
    bool m_isBigEndian;
    QString m_modsDir;
    QString m_mappingFile;
    int m_gameId;
    int m_platform;
    QString m_sourceBlobset;

    // A file found in the mods folder.
    struct ModFile {
        QString absolutePath;
        QString relativePath;   // Relative to the mods folder, '/' separated.
        QString extension;      // Lower case, without the dot.
    };

    // A mod file that has been validated and matched to its path hash.
    struct ModJob {
        QString absolutePath;
        QString relativePath;
        QString extension;
        quint32 pathHash = 0;
        bool isNew = false;             // Not in the file mapping: the hash comes from the file name.
        quint32 mainUncompressed = 0;   // TXPK: split point of the file (from its xml info).
        quint32 vramUncompressed = 0;
        // DAT only: the original entry decides how the data is stored.
        quint32 origMainCompressed = 0;
        quint32 origVramCompressed = 0;
        quint32 origMainUncompressed = 0;
        quint32 origVramUncompressed = 0;
    };

    // Where a section (main / vram) ended up inside the container and how big it is.
    struct Section {
        quint32 offset = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
    };

    struct EntryRecord {
        quint32 pathHash = 0;
        Section mainPart;
        Section vram;
    };

    void createV1();
    void addNewFilesToMapping(const QList<ModJob>& jobs);

    // Planning (read only, nothing is written until every mod file has been validated)
    QHash<QString, quint64> loadMappingData() const;
    QList<ModFile> collectModFiles(const QStringList& extensions) const;
    QList<ModJob> planJobs(const QStringList& extensions, const QHash<QString, quint64>& mapping) const;
    BlobsetFile loadSourceHeader() const;

    // Mini TXPK (DDS) header
    QString miniPathFor(const QString& relativePath) const;
    // Mini TXPK header for an imported .dds; `vram` receives the matching vram section
    // (PC: the whole .dds, PS3: pixels converted for the CellGcmTexture built from the .dds).
    QByteArray buildMiniHeader(const ModJob& job, const QByteArray& dds, QByteArray& vram) const;

    // Writing
    EntryRecord writeJobV1(PackageStream& out, const ModJob& job) const;
    void writeHeaderV1(PackageStream& out, const QList<EntryRecord>& entries, qsizetype hashSize) const;
    qsizetype hashSizeV1() const;

    // Section writers - each one returns where the data ended up and its sizes
    static Section writeRaw(PackageStream& out, const QByteArray& data);
    static Section appendFile(PackageStream& out, const QString& path);
    static Section writeLzma(PackageStream& out, const QByteArray& data, qint64 chunkSize, bool isBigEndian,
                             bool lzmaV2 = false);
    bool m_lzmaV2 = false;  // V2 blobset (RLL4): writeLzma() writes V2 sections.
};

#endif // CREATE_WORKER_H

