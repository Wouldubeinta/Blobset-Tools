#ifndef BSB_FILE_H
#define BSB_FILE_H

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
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

#include <algorithm>
#include <optional>

/// BSB: a Flash user interface screen of the Blobset V1 games (Rugby League Live 2, Don Bradman Cricket 14, ...).
///
/// main section:
///   u32 magic           " BSB" (bytes 20 42 53 42; byte swapped on PS3 / Xbox 360)
///   u32 movieOffset     24
///   u32 movieSize       the SWF ("FWS" / "CWS" / "GFX" / "CFX")
///   u32 txpkOffset      right after the movie
///   u32 txpkSize        the TXPK main section (header + info records) of the movie's textures
///   u32 reserved        0
///   movie, then the TXPK main section
/// vram section: the TXPK's texture data (PC: whole .dds files at 128 byte aligned offsets).
///
/// The movie refers to its textures by the paths stored in the TXPK
/// (e.g. "data/blobs/ui/flash/profiles/ui_aswallow_profile.bsf/aswallow.tga").
namespace Bsb {

constexpr quint32 kMagic = 0x42534220u;   // " BSB" read little endian
constexpr qint64 kHeaderSize = 24;

struct Header {
    bool bigEndian = false;
    quint32 movieOffset = 0;
    quint32 movieSize = 0;
    quint32 txpkOffset = 0;
    quint32 txpkSize = 0;

    qint64 mainSize() const {
        return std::max<qint64>(qint64(movieOffset) + movieSize, qint64(txpkOffset) + txpkSize);
    }
};

/// The header of a BSB main section (or main + vram), when `data` is one and its sections fit.
std::optional<Header> readHeader(const QByteArray& data);

/// Size of the main section of a BSB that is stored as main + vram in one file (the rest is vram).
/// -1 when `data` isn't a BSB.
qint64 mainSectionSize(const QByteArray& data);

/// Builds a BSB main section from the movie and the TXPK main section.
QByteArray build(const QByteArray& movie, const QByteArray& txpkMain, bool bigEndian);

/// { signature, compressed, version, fileLength, width, height, frameRate, frameCount } of a SWF / GFX movie.
/// Empty map when the bytes aren't one.
QVariantMap movieInfo(const QByteArray& movie);

} // namespace Bsb

#endif // BSB_FILE_H
