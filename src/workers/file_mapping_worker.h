#ifndef FILE_MAPPING_WORKER_H
#define FILE_MAPPING_WORKER_H

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
 * The author may be contacted at:
 * Discord: Wouldubeinta
 *
 * History:
 * [Wouldubeinta]  01/07/2025  Created (C# FileMapping)
 *                             Ported to Qt 6 (File Mapping Worker), engine aligned with the Extract Worker
 */

#include "asset_signatures.h"
#include "core_defs.h"

#include <QByteArray>
#include <QObject>
#include <QDir>
#include <QSet>
#include <QString>

#include <optional>
#include <vector>

class BlobsetFile;
class PackageStream;
struct BlobsetEntry;
struct MiniTxpk;

class FileMappingWorker : public QObject {
    Q_OBJECT

public:
    // gameId   : GameInfo/GameId from the game INI (SHA1 header skip, .badds -> .dds, per game audio / fev names).
    // platform : static_cast<int>(CoreDefs::Platforms)
    explicit FileMappingWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& outputMappingFile,
                               int gameId = 0, int platform = 0, QObject* parent = nullptr);
    ~FileMappingWorker() override = default;

    struct MappingEntry {
        QString path;
        quint64 hash = 0;
    };

    // ------------------------------------------------------------------------------------------
    // Mapping table codec (shared with FileMappingEditorModel)
    //
    //   [u32 count] then, per entry: [u8 pathLength][latin1 path][u64 hash]   - always little endian.
    //
    // The hash is the blobset path hash: 64 bit for V4, the zero extended 32 bit hash for V1.
    // Both functions throw std::runtime_error with a readable message on failure.
    // ------------------------------------------------------------------------------------------
    static constexpr int kMaxPathBytes = 255; // The length prefix is a single byte.

    static std::vector<MappingEntry> readMappingFile(const QString& file);
    static void writeMappingFile(const QString& file, const std::vector<MappingEntry>& entries); // Atomic (QSaveFile).
    static void sortEntries(std::vector<MappingEntry>& entries);                                 // Alphabetical, like the C# OrderBy.
    static QString normalizePath(const QString& path);                                           // Trimmed, '\' separators.
    static QString validatePath(const QString& path);                                            // Empty when it can be stored.

    // Kept for existing callers: returns an empty list when the file can't be read.
    std::vector<MappingEntry> deserialize(const QString& inputMappingFile);

public slots:
    void generate_mapping();

signals:
    void progressUpdated(int value);
    void updateConsole(const QString& result);
    void updateStatus(const QString& result);
    void taskFinished(const QString& result);
    void taskSucceeded();   // Right before taskFinished() when the task worked.

private:
    struct FileLists; // TXPK_File_List.csv / M3MP_File_List.csv content collected while mapping.

    QString m_blobsetFile;
    int m_version;
    bool m_isBigEndian;
    QString m_outputMappingFile;
    int m_gameId;
    int m_platform;
    bool m_lzmaV2 = false;
    bool m_lzmaV3 = false;          // V3 blobset (Cricket 19): V4 layout, XORed LZMA chunks instead of ZSTD.          // V2 blobset (RLL4): LZMA sections are self describing 32 KB chunks.
    QString m_currentFile; // Reported when something goes wrong.

    void mapV1();
    void mapV2();
    void mapV3();
    void mapV4();

    // Shared pipeline
    bool readHeader(BlobsetFile& blobset, CoreDefs::BlobsetVersion version);
    // skipHashes: V3 / V4 entries that are already mapped (incremental update) and aren't read again.
    std::vector<MappingEntry> collectEntries(const BlobsetFile& blobset, CoreDefs::BlobsetVersion version, FileLists& lists,
                                             const QSet<quint64>* skipHashes = nullptr);
    void finish(std::vector<MappingEntry>& entries, const FileLists& lists, const QString& message);
    void serialize(std::vector<MappingEntry>& entries);
    void createModFolders(const std::vector<MappingEntry>& entries);
    void writeFileLists(const FileLists& lists, bool append = false) const;
    QDir platformDirectory() const;   // games/<game>/<platform> (above the mapping's data folder).

    // Per entry classification (returns the mapped path, empty when the entry is skipped)
    QString resolveV1(PackageStream& in, const BlobsetEntry& entry, int index, FileLists& lists);
    QString resolveV4(PackageStream& in, const BlobsetEntry& entry, int index, FileLists& lists);
    std::optional<AssetSignatures::Match> identifyAsset(const QByteArray& data, qint64 mainSize) const;
    std::optional<AssetSignatures::Match> identifyCompressedV1(PackageStream& in, const BlobsetEntry& entry,
                                                               const QByteArray& firstChunk) const;

    // Mini TXPK / TXPK / M3MP helpers
    MiniTxpk readMini(PackageStream& in) const;
    MiniTxpk parseMini(const QByteArray& header) const;
    void addTxpkList(FileLists& lists, const QByteArray& header, int index) const;
    void addM3mpList(FileLists& lists, PackageStream& in, int index) const;
    void addM3mpList(FileLists& lists, const QByteArray& header, int index) const;

    QString blobPathFor(quint32 blobSetNumber) const;
};

#endif // FILE_MAPPING_WORKER_H
