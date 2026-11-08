#ifndef RL26_USER_CONTENT_H
#define RL26_USER_CONTENT_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

// Rugby League 26's user made content, next to the saves (Saved Games\Rugby League 26\...):
//
//   Decals\DecalData.SAV   the logo editor's vector logos (custom and downloaded from the sharing server). Decoded:
//                          a paSerial root (u16 count, u32 ids[3072], u32 [3072], u32 next id), then 5000 headers
//                          of 121 bytes (u32 id, name[64], the sharing info), 5000 index entries (u32 id, u32 offset)
//                          and the logos' data from there. A logo: u32 size, u32 size, then u8 0, u16 BE width,
//                          u16 BE height (5000 x 5000) and its drawing, one byte commands:
//                            01 start / end, 02 begin a shape, 08 x y move to, 0C x y x1 y1 x2 y2 cubic curve to
//                            (end point first, then the control points), 0D / 0E a sub path's direction, 03 end the
//                            shape, 06 r g b a fill it, 07 09 end of the shape's layer.
//                          Points are u16 BE, 0..65535 across the canvas. A club uses one when its m_decalId isn't 0
//                          (m_iconCategory 255, m_iconIndex 65535).
//   Levels\LevelData.SAV   the custom stadiums. Decoded: u32 BE count, then per stadium u32 BE id (from 1024),
//                          name[64] and its layout. A home ground uses one when its level id isn't 0xFFFFFFFF.
namespace Rl26UserContent {

constexpr quint32 kNoLevel = 0xFFFFFFFFu;

// The "Saved Games\Rugby League 26" folder of a save (its folder's parent when that has Decals / Levels, otherwise
// the default one).
QString gameFolder(const QString& savePath);

struct Item {
    quint32 id = 0;
    QString name;
};

// The custom logos, in the file's order (empty when there's no DecalData.SAV). Read again when the file changes.
QList<Item> decals(const QString& savePath);
QString decalName(const QString& savePath, quint32 id);   // "" when it isn't there.
// The logo drawn on a transparent square, fitted to what it covers (a null image when it isn't there).
QImage renderDecal(const QString& savePath, quint32 id, int size);
// A file:/// URL of the drawn logo (a PNG in the temp folder), for an Image; "" when it isn't there.
QString decalImageUrl(const QString& savePath, quint32 id, int size = 256);
// For tests: draw a logo's data (from its u32 size on).
QImage renderDecalData(const QByteArray& data, int size);

// The custom stadiums (empty when there's no LevelData.SAV or none have been made).
QList<Item> customStadiums(const QString& savePath);
// A home ground's level id as a name: "" for none (0xFFFFFFFF), the stadium's name, or "Custom Stadium" when it
// isn't in LevelData.SAV.
QString customStadiumName(const QString& savePath, quint32 id);

} // namespace Rl26UserContent

#endif // RL26_USER_CONTENT_H
