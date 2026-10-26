#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <QString>
#include <QStringList>

/// Where Blobset Tools keeps its files.
///
///   Install folder (read only, e.g. C:\Program Files\Blobset Tools):
///       Blobset-Tools.exe, Qt / codec DLLs, GameSelectionInfo.xml (the games picked in setup), assets\games,
///       modloader, vortex,
///       defaults\  - the starting files of the data folder (defaults\games\<game>\<platform>\GameInfo.ini, ...)
///
///   Data folder (writable): Documents\Blobset Tools (Windows' Documents folder, OneDrive redirection included)
///       Settings.ini, games\<game>\<platform>\ (GameInfo.ini, file mapping, mods, mod library, backups,
///       extracted files), cli.log
///
/// Portable mode: a file called portable.txt next to the exe keeps everything next to the exe (the old layout).
namespace AppPaths {

/// The exe's folder.
QString installDir();
/// The writable data folder (created by prepare()).
QString dataDir();
bool isPortable();

/// GameSelectionInfo.xml next to the exe. The installer writes it with only the games picked during setup
/// (run setup again to change them); a build from source has the full list.
QString gameSelectionInfoFile();

/// The <ModFolder>s (games\\<game> folder names) of the games in GameSelectionInfo.xml (the installer writes only the games picked during setup).
QStringList selectedGameFolders();

/// Call once at startup, before anything reads Settings.ini or the games folder:
///   - creates the data folder,
///   - first run after the move: copies an old portable layout (games folder and Settings.ini next to the exe)
///     into the data folder, leaving extracted blobsets (games\<game>\<platform>\data-0.blobset.*) behind,
///   - copies every file of <install>\defaults that the data folder doesn't have yet (new games after an update),
///     for the games in GameSelectionInfo.xml only.
/// Returns what it did, for the Console Log.
QStringList prepare();

} // namespace AppPaths

#endif // APP_PATHS_H
