#ifndef MESH_TEXTURE_EXPORT_H
#define MESH_TEXTURE_EXPORT_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include "mesh_file.h"

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// The Mesh Viewer's textures: what it draws (resolveLook) and its OBJ export, which writes <name>.obj, <name>.mtl
// and the textures the mesh's surfaces use as PNGs in <name>_textures\, so the model opens textured (Blender, ...).
//
// Where the materials are:
//   - Rugby League Live 3 / 4 (and the other 01 BA DC FE games): "<model>.material.mat" next to the mesh.
//   - Rugby League 26 and the other V4 games: a material block inside the mesh.
// Each submesh names its surface; the surface's material lists its textures by path ("col_map" ->
// "props/goalposts/postpads_col"). A texture is found as its own blobset file ("<path>.texture", the V4 games'
// loose .pc.dds files), in the model's "<model>.texpack" TXPK, or in any other TXPK of the game (the older games
// keep most textures in shared packs; the index of every TXPK's textures is built once per loaded blobset).
//
// The UVs are the mesh's own; OBJ's V runs bottom up, so MeshFile::toObj() writes 1 - v and the PNGs keep the
// textures' own top-down row order.
class MeshTextureExport {
public:
    // What the export needs from the loaded game (read on the GUI thread, the export then runs on a worker).
    struct Context {
        QString assetLocation;
        int version = 0;
        int platform = 0;
        QString gameKey;
        QStringList txpkKeys;   // Blobset keys of every mapped .txpk.
        bool valid = false;
    };
    static Context currentContext();

    struct Result {
        bool ok = false;
        QString message;
    };

    // Progress (0 - 1, what it's doing): called from worker threads. Cancelled: polled, true = stop.
    using Progress = std::function<void(double fraction, const QString& stage)>;
    using Cancelled = std::function<bool()>;

    // What the Mesh Viewer draws: each submesh's material (-1 = none) and each material's colour texture as a PNG
    // in `pngDir` ("" when it has none or it wasn't found), scaled down to `maxSize`. Textures are read once and
    // decoded on every core; their PNGs are reused for the rest of the session (meshes share textures).
    struct Look {
        QList<int> submeshMaterial;
        QStringList files;
        QList<bool> transparent;   // The colour texture has see-through pixels (drawn alpha tested).
        QStringList missing;
        int found = 0;
    };
    static Look resolveLook(const MeshFile& mesh, const QString& meshPath, const Context& context,
                            const QString& pngDir, int maxSize,
                            const Progress& progress = nullptr, const Cancelled& cancelled = nullptr);

    // `meshPath`: the mesh's workspace path ("props/balls/models/ball.model.mesh"); it names the material file
    // of the older games and the model's texpack. Writes into `outDir`.
    static Result exportObj(const MeshFile& mesh, const QString& meshPath, const QString& outDir,
                            const QString& baseName, const Context& context, const Progress& progress = nullptr);
};

#endif // MESH_TEXTURE_EXPORT_H
