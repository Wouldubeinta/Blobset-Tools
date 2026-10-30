#ifndef BLOBSET_ASSET_READER_H
#define BLOBSET_ASSET_READER_H

#include "blobset_file.h"

#include <QByteArray>
#include <QString>

/// Reads one asset of the loaded blobset (main section followed by the vram section, decompressed).
///
///  - V1/V2: the asset lives in a numbered "N.blob" container at its final offsets (LZMA chunk blocks).
///  - V3   : the asset is its own physical file "<folder hash>/<file hash>" (XORed LZMA chunks, Cricket 19).
///  - V4   : the asset is its own physical file "<folder hash>/<file hash>" next to the header (ZSTD chunks).
///
/// includeVram = false reads the main section only (e.g. a TXPK header without its textures).
/// Throws std::runtime_error with a readable message on failure.
namespace BlobsetAssetReader {

QByteArray read(const BlobsetEntry& entry, const QString& gameLocation, int version, int platform,
                qint64* mainSizeOut = nullptr, bool includeVram = true);

/// V3 / V4: the physical file of an entry, "<folder hash>/<file hash>" next to the header. With the header in
/// <game>\BlobsetMods\data (the Blobset Mod Loader's build, see AppOrchestrator::assetLocation) only the modded files
/// are there: the others are read from the game's own data folder, the way the loader serves them to the game.
QString dataFilePath(const QString& headerPath, const BlobsetEntry& entry);

} // namespace BlobsetAssetReader

#endif // BLOBSET_ASSET_READER_H
