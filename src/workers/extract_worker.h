#ifndef EXTRACT_WORKER_H
#define EXTRACT_WORKER_H

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
 * [Wouldubeinta]  07/10/2026  Ported to Qt 6 (Extract Worker)
 */
#include "x360_texture.h"
#include "blobset_file.h"
#include "asset_signatures.h"
#include "core_defs.h"
#include "mini_txpk_file.h"

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTemporaryDir>

#include <memory>
#include <optional>

class PackageStream;
class QIODevice;

class ExtractWorker : public QObject {
    Q_OBJECT

public:
    // gameId   : GameInfo/GameId from the game INI (controls SHA1 header skip + .badds -> .dds mapping).
    // platform : static_cast<int>(CoreDefs::Platforms) - only used to pick PS3 vs Xbox 360 unswizzling.
    explicit ExtractWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& outputDir,
                           const QString& gameName, int gameId = 0, int platform = 0, QObject* parent = nullptr);
    ~ExtractWorker() override = default;

public slots:
    void extract_blobset();

signals:
    void progressUpdated(int value);
    void updateConsole(const QString& result);
    void updateStatus(const QString& result);
    void taskFinished(const QString& result);
    void taskSucceeded();   // Right before taskFinished() when the task worked.

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
    QString m_outDir;
    int m_gameId;
    QString m_gameName;
    int m_platform;

    std::unique_ptr<QTemporaryDir> m_tempDir;

    void extractV1();
    void extractV2();
    //void extractV3();
    void extractV4();

    // V1 / V2 share the entry layout. Every entry follows the same rules as FileMappingWorker::resolveV1,
    // so each file lands at the path the mapping lists for it.
    void extractLegacy(CoreDefs::BlobsetVersion version);
    QString extractLegacyEntry(PackageStream& in, const BlobsetEntry& entry, int index);
    QString dumpLegacyEntry(PackageStream& in, const BlobsetEntry& entry, int index);

    // V4: same rules as FileMappingWorker::resolveV4.
    QString extractV4Entry(PackageStream& in, const QString& filePath, const BlobsetEntry& entry, int index);

    // Paths / temp files
    QString blobPathFor(quint32 blobSetNumber) const;
    QString makeTarget(const QString& relativePath, QString* normalizedRelative = nullptr) const;
    QString freshTempPath(int index, const QString& extension);

    // Mini TXPK / DDS helpers
    MiniTxpk readMini(PackageStream& in) const;
    MiniTxpk parseMini(const QByteArray& header, qint64* headerEnd = nullptr) const;
    // Console texture data -> PC .dds (PS3: from the CellGcmTexture descriptor; Xbox 360: from its D3D texture
    // header, untiled). `name` is only used for the warning when the format can't be stored in a .dds.
    QByteArray toConsoleDds(const QByteArray& pixels, const Gcm::CellGcmTexture& gcm, bool isXbox360,
                            const X360::TextureHeader& x360, const QString& name);
    QString writeMiniDds(const MiniTxpk& mini, const QByteArray& pixelData);
    QString extractTxpkDds(const QString& txpkFile, int mainSize, int index);

    // LZMA (V1 / V2) - chunk table: count, [size + 4] * count, then per chunk: uncompressed size + data
    // V2 (RLL4, v2Uncompressed = the section's uncompressed size): no chunk table, 32 KB self describing chunks.
    static void lzmaBlockToDevice(PackageStream& in, QIODevice& out, qint64 v2Uncompressed = -1);
    static QByteArray lzmaBlockToBytes(PackageStream& in, qint64 v2Uncompressed = -1);

    // ZSTD (V4) - chunk: int32 (size + 4), then the data.
    // V3 (Cricket 19, m_lzmaV3): XORed LZMA chunks instead (CompressionIO::readLzmaSectionV3). Those need the
    // section's uncompressed size, taken from the entry being extracted: a read starting before
    // m_v3MainCompressed is the main section, anything after it the vram section.
    void zstdChunkToDevice(PackageStream& in, QIODevice& out);
    void zstdChunksToDevice(PackageStream& in, QIODevice& out, qint64 maxBytes);
    QByteArray zstdChunksToBytes(PackageStream& in, qint64 maxBytes);

    // V4 signatures; V3 also checks the V1 era model / material layouts.
    std::optional<AssetSignatures::Match> identifyAsset(const QByteArray& data, qint64 mainSize) const;
    std::optional<AssetSignatures::Match> identifyCompressedV1(PackageStream& in, qint64 mainOffset, qint64 mainSize,
                                                               qint64 mainV2, const QByteArray& firstChunk) const;

    bool m_lzmaV3 = false;
    qint64 m_v3MainCompressed = 0;
    qint64 m_v3MainSize = 0;
    qint64 m_v3VramSize = 0;
};

#endif // EXTRACT_WORKER_H
