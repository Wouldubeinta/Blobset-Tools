#ifndef CREATOR_SUPPORT_H
#define CREATOR_SUPPORT_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

struct BlobsetEntry;

/// Shared by the TXPK / M3MP viewers (Extract All writes a creator list) and the TXPK / M3MP Creators (which read
/// it back and write the rebuilt file plus the <name>.xml ModifyFileInfo that Modify / Create need).
namespace CreatorSupport {

/// Where a TXPK / M3MP came from: the blobset header entry it gets written back to.
struct SourceInfo {
    qint64 index = -1;          // Blobset header index, -1 = unknown.
    QString folderHash;
    QString fileHash;
    bool isCompressed = false;
    int version = -1;           // CoreDefs::BlobsetVersion
    int platform = 0;           // CoreDefs::Platforms
    QString outputName;         // Suggested file name of the rebuilt file, e.g. "12815.txpk".
};

/// Opened from the loaded blobset.
SourceInfo fromBlobset(const BlobsetEntry& entry, int version, int platform, const QString& extension);

/// Opened from a file: index / hashes come from a <name>.xml ModifyFileInfo next to it, or from a numeric file
/// name ("12815.txpk", how Extract names them) looked up in the loaded blobset.
SourceInfo fromFile(const QString& path, int version, int platform);

QString platformName(int platform);                         // "PC" / "PS3" / "Xbox360"
int platformFromName(const QString& name, int fallback);

/// QML hands paths over as file:/// URLs.
QString toLocalPath(const QString& path);

/// Alignment of a run of blocks {offset, size}: 1 when they follow each other without gaps, else the largest
/// power of two (up to `maxAlignment`) dividing every offset. `fallback` when there's nothing to go on.
quint32 detectAlignment(const QList<QPair<qint64, qint64>>& blocks, quint32 fallback, quint32 maxAlignment = 4096);

inline qint64 alignUp(qint64 value, quint32 alignment) {
    return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
}

/// The extracted file of a creator list entry inside `sourceDir` (its File attribute, or the stored path the way
/// the viewers name it on extraction). Empty when it can't be found.
QString resolveListFile(const QString& sourceDir, const QString& file, const QString& storedPath);

/// Reads a creator list for the creator pages: { ok, error, type, count, platform, version, index, folderHash,
/// fileNameHash, outputName, sourceDir, missing, totalSize, entries: [{ path, file, found, size }] }.
/// `sourceDir` empty = the list's own folder. `expectedType` "TXPK" / "M3MP".
QVariantMap inspectList(const QString& xmlPath, const QString& sourceDir, const QString& expectedType);

/// The name a creator list is saved under in the extraction folder.
inline QString txpkListName() { return QStringLiteral("TXPK_List.xml"); }
inline QString m3mpListName() { return QStringLiteral("M3MP_List.xml"); }
inline QString bsbListName() { return QStringLiteral("BSB_List.xml"); }
inline QString listNameFor(const QString& type) {
    return type == QLatin1String("TXPK") ? txpkListName() : type == QLatin1String("BSB") ? bsbListName() : m3mpListName();
}

} // namespace CreatorSupport

#endif // CREATOR_SUPPORT_H
