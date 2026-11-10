#ifndef DLL_MOD_INSTALLER_H
#define DLL_MOD_INSTALLER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QString>
#include <QStringList>
#include <QVariantList>

// DLL mods for the V4 PC games (Rugby League 26, Rugby 25, Cricket 24, AFL 23, Tiebreak).
//
// The Blobset Mod Loader (dinput8.dll next to the game's exe) loads every .dll / .asi in <game>\DLL_Mods into the
// game before its own code runs. Blobset Tools keeps the DLL mods in games\<game>\pc\mods\dll_mods (the Mod
// Manager's DLL Mods) and installs them: sync() copies that folder into <game>\DLL_Mods (only what changed) together
// with the loader, and takes out the files it installed earlier that are gone from the library. Files it didn't
// put there are left alone; <game>\DLL_Mods\blobsettools_installed.txt lists the ones it did.
//
// The dll_mods folder is not part of the blobset mods: Modify / Create skip it (isLibraryPath()).
class DllModInstaller {
public:
    static constexpr const char* kLibraryFolder = "dll_mods";   // Inside the workspace mods folder.
    static constexpr const char* kGameFolder = "DLL_Mods";      // Next to the game's exe.

    // <platformDir>\mods\dll_mods (made when `create`).
    static QString libraryDir(const QString& platformDir, bool create = false);
    // <game>\DLL_Mods for the game's blobset header (<game>\data\data-0.blobset.pc); "" when it isn't one.
    static QString gameDir(const QString& blobsetFile);
    // Whether a path relative to a mods folder is inside dll_mods (not a blobset file).
    static bool isLibraryPath(const QString& relativePath);

    // The library's files: [{ name (relative, '/' separated), size (text), loads (a .dll / .asi the loader loads),
    // installed (the game has the same file) }], sorted by name.
    static QVariantList list(const QString& libraryDir, const QString& gameDir);

    // Copies the library into the game (see above). Returns what it did, one line each; *ok = false on a failure.
    static QStringList sync(const QString& libraryDir, const QString& blobsetFile, bool* ok);
};

#endif // DLL_MOD_INSTALLER_H
