#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

/// Blobset Tools without its window, for scripts and mod managers (the Vortex extensions call --build).
///
///   Blobset-Tools.exe --list-games
///   Blobset-Tools.exe --game "Cricket 24" [--platform pc] --build
///   Blobset-Tools.exe --game "Cricket 24" --install "C:\Downloads\Better Bats.zip" --build
///   Blobset-Tools.exe --game "Cricket 24" --disable "Better Bats" --list-mods
///   Blobset-Tools.exe --install-vortex-extensions
///
/// Exit codes: 0 = done, 1 = a task failed, 2 = bad arguments, 3 = Blobset Tools is open / another command runs.
/// Everything printed also goes to cli.log in the data folder (Documents\Blobset Tools, see app_paths.h).
#include <QStringList>

namespace CommandLine {

/// True when the arguments ask for the command line (any argument starting with "--").
bool requested(int argc, char* argv[]);

/// Runs the command line. `guiRunning`: the Blobset Tools window is open (tasks that change the game refuse).
/// Needs the QApplication to exist.
/// startupLog: what AppPaths::prepare() did (printed first).
int run(bool guiRunning, const QStringList& startupLog = {});

} // namespace CommandLine

#endif // COMMAND_LINE_H
