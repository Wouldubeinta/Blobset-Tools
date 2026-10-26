#include "command_line.h"

#include "app_orchestrator.h"
#include "app_paths.h"
#include "mod_library.h"
#include "xml_models.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <cstdio>
#include <functional>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Qt::StringLiterals;

namespace CommandLine {

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitBusy = 3;

// ---------------------------------------------------------------------------------------------------------------
// Output: the console that started us (Blobset Tools is a GUI program, so it has none of its own), pipes
// (Vortex reads them) and cli.log in the data folder (Documents\Blobset Tools).
// ---------------------------------------------------------------------------------------------------------------

class Output {
public:
    Output() {
        m_log.setFileName(QDir(AppPaths::dataDir()).filePath(u"cli.log"_s));
        // cli.log is only a copy of what's printed: when it can't be written (read-only folder) the output still works.
        m_logOpen = m_log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
#ifdef Q_OS_WIN
        m_handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (m_handle == nullptr || m_handle == INVALID_HANDLE_VALUE) {
            // Started from a console window: write to it.
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                m_handle = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                m_ownsHandle = m_handle != INVALID_HANDLE_VALUE;
                SetConsoleOutputCP(CP_UTF8);
                m_console = true;
            }
        }
#endif
    }
    ~Output() {
#ifdef Q_OS_WIN
        if (m_console) {
            // The console's prompt was printed when we started (GUI programs don't make it wait): start a fresh line.
            DWORD written = 0;
            WriteFile(m_handle, "\r\n", 2, &written, nullptr);
        }
        if (m_ownsHandle)
            CloseHandle(m_handle);
#endif
    }

    void line(const QString& text) {
        const QByteArray bytes = (text + u'\n').toUtf8();
        if (m_logOpen) {
            m_log.write(bytes);
            m_log.flush();
        }
#ifdef Q_OS_WIN
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(m_handle, bytes.constData(), DWORD(bytes.size()), &written, nullptr);
            return;
        }
#endif
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        std::fflush(stdout);
    }

private:
    QFile m_log;
    bool m_logOpen = false;
#ifdef Q_OS_WIN
    HANDLE m_handle = nullptr;
    bool m_ownsHandle = false;
    bool m_console = false;
#endif
};

const char* kHelp = R"(Blobset Tools - command line

Usage:
  Blobset-Tools.exe --list-games
  Blobset-Tools.exe --game <name> [--platform pc|ps3|xbox360] [mod commands] [one task]
  Blobset-Tools.exe --install-vortex-extensions [--vortex-plugins <folder>]

Game:
  --game <name>             The game: its name, folder name or number from --list-games.
  --platform <platform>     pc (default), ps3 or xbox360.
  --game-location <path>    The game's blobset or folder (saved like the location set in the window).
  --mod-loader <on|off>     V4 games: build into <game>\BlobsetMods for the Blobset Mod Loader (saved).

Mod Manager (run before the task, in this order):
  --install <archive|folder>   Install a mod (.zip / .7z / .rar or a folder). Can be given more than once.
  --uninstall <mod>            Remove a mod (moved to the Recycle Bin).
  --enable <mod>               Enable a mod (by name or id).
  --disable <mod>              Disable a mod.
  --list-mods                  Print the load order (after the changes above).

Tasks (one per run):
  --build          Put the workspace mods folder and every enabled mod into the game (the Modify button).
                   Restores the original files first where needed; with no mods it restores them.
                   Runs --update-mapping first when the game has no file mapping yet.
  --modify         The same as --build.
  --restore        Put the original game files back.
  --extract        Extract the blobset into games\<game>\<platform>.
  --update-mapping Update the file mapping.

Other:
  --install-vortex-extensions   Copy the Vortex game extensions (vortex folder next to the exe) into Vortex.
  --vortex-plugins <folder>     Vortex's plugins folder (default %APPDATA%\Vortex\plugins).
  --quiet                       Only print errors and the result.
  --help, --version

Exit codes: 0 done, 1 a task failed, 2 bad arguments, 3 Blobset Tools is open or another command is running.
Your files (Settings.ini, the games folder) are in Documents\Blobset Tools; everything printed is also written to
cli.log there.)";

QString simplifiedName(QString name) {
    // "Cricket 24 (PC)" -> "cricket 24"
    static const QRegularExpression platforms(u"\\((pc|ps3|360)\\)"_s, QRegularExpression::CaseInsensitiveOption);
    name.remove(platforms);
    return name.simplified().toLower();
}

int platformFrom(const QString& text) {
    const QString p = text.trimmed().toLower();
    if (p.isEmpty() || p == u"pc"_s || p == u"windows"_s || p == u"win"_s)
        return 0;
    if (p == u"ps3"_s || p == u"playstation3"_s || p == u"playstation 3"_s)
        return 1;
    if (p == u"xbox360"_s || p == u"x360"_s || p == u"360"_s || p == u"xbox 360"_s)
        return 2;
    return -1;
}

std::optional<int> findGame(const QVector<GameEntry>& games, const QString& query) {
    bool isNumber = false;
    const int number = query.toInt(&isNumber);
    if (isNumber && number >= 0 && number < games.size())
        return number;

    const QString wanted = simplifiedName(query);
    for (int i = 0; i < games.size(); ++i) {
        const GameEntry& g = games[i];
        if (simplifiedName(g.name) == wanted || g.folderName.compare(query.trimmed(), Qt::CaseInsensitive) == 0
            || g.modFolder.compare(query.trimmed(), Qt::CaseInsensitive) == 0)
            return i;
    }
    return std::nullopt;
}

QString platformsOf(const GameEntry& g) {
    QStringList list;
    if (g.pc)
        list << u"pc"_s;
    if (g.ps3)
        list << u"ps3"_s;
    if (g.xbox360)
        list << u"xbox360"_s;
    return list.join(u", "_s);
}

/// Copies <exe>\vortex\* into Vortex's plugins folder and tells every extension where Blobset Tools is.
int installVortexExtensions(Output& out, const QString& pluginsIn) {
    const QString source = QDir(QCoreApplication::applicationDirPath()).filePath(u"vortex"_s);
    if (!QFileInfo(source).isDir()) {
        out.line(u"Error: %1 doesn't exist (the Vortex extensions are deployed there with Blobset Tools)."_s
                     .arg(QDir::toNativeSeparators(source)));
        return kExitFailed;
    }
    QString plugins = pluginsIn;
    if (plugins.isEmpty()) {
        const QString appData = qEnvironmentVariable("APPDATA");
        if (appData.isEmpty()) {
            out.line(u"Error: %APPDATA% isn't set; pass --vortex-plugins <folder>."_s);
            return kExitUsage;
        }
        plugins = QDir(appData).filePath(u"Vortex/plugins"_s);
    }
    if (!QDir().mkpath(plugins)) {
        out.line(u"Error: could not create %1."_s.arg(QDir::toNativeSeparators(plugins)));
        return kExitFailed;
    }

    const QByteArray config = QJsonDocument(QJsonObject{
        {u"blobsetToolsPath"_s, QDir::toNativeSeparators(QCoreApplication::applicationFilePath())}}).toJson();

    // Only the games picked in setup (GameSelectionInfo.xml); each extension says its game in blobset_game.json.
    QSet<QString> games;
    for (const QString& folder : AppPaths::selectedGameFolders())
        games.insert(folder.toLower());

    int installed = 0, skipped = 0;
    for (const QString& extension : QDir(source).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString from = QDir(source).filePath(extension);
        QFile gameFile(QDir(from).filePath(u"blobset_game.json"_s));
        if (!games.isEmpty() && gameFile.open(QIODevice::ReadOnly)) {
            const QString game = QJsonDocument::fromJson(gameFile.readAll()).object().value(u"game"_s).toString();
            if (!game.isEmpty() && !games.contains(game.toLower())) {
                ++skipped;
                continue;
            }
        }
        const QString to = QDir(plugins).filePath(extension);
        QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
        bool ok = true;
        while (it.hasNext()) {
            const QString file = it.next();
            const QString target = QDir(to).filePath(QDir(from).relativeFilePath(file));
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::remove(target);
            ok = QFile::copy(file, target) && ok;
        }
        QSaveFile configFile(QDir(to).filePath(u"config.json"_s));
        ok = ok && configFile.open(QIODevice::WriteOnly) && configFile.write(config) == config.size() && configFile.commit();
        if (ok)
            ++installed;
        else
            out.line(u"Warning: %1 could not be copied completely."_s.arg(extension));
    }
    out.line(u"Installed %1 Vortex extension(s) into %2. Restart Vortex to see them."_s
                 .arg(installed).arg(QDir::toNativeSeparators(plugins)));
    if (skipped > 0)
        out.line(u"Skipped %1 extension(s) of games that aren't in GameSelectionInfo.xml (pick them in setup to add them)."_s
                     .arg(skipped));
    return installed > 0 ? kExitOk : kExitFailed;
}

} // namespace

bool requested(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (argv[i] && argv[i][0] == '-' && argv[i][1] == '-')
            return true;
    return false;
}

int run(bool guiRunning, const QStringList& startupLog) {
    Output out;
    for (const QString& line : startupLog)
        out.line(line);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Blobset Tools"_s);
    const QCommandLineOption help(QStringList{u"help"_s, u"h"_s, u"?"_s});
    const QCommandLineOption version(u"version"_s);
    const QCommandLineOption listGames(u"list-games"_s);
    const QCommandLineOption game(u"game"_s, QString(), u"name"_s);
    const QCommandLineOption platform(u"platform"_s, QString(), u"platform"_s, u"pc"_s);
    const QCommandLineOption location(u"game-location"_s, QString(), u"path"_s);
    const QCommandLineOption modLoader(u"mod-loader"_s, QString(), u"on|off"_s);
    const QCommandLineOption install(u"install"_s, QString(), u"path"_s);
    const QCommandLineOption uninstall(u"uninstall"_s, QString(), u"mod"_s);
    const QCommandLineOption enable(u"enable"_s, QString(), u"mod"_s);
    const QCommandLineOption disable(u"disable"_s, QString(), u"mod"_s);
    const QCommandLineOption listMods(u"list-mods"_s);
    const QCommandLineOption build(u"build"_s);
    const QCommandLineOption modify(u"modify"_s);
    const QCommandLineOption restore(u"restore"_s);
    const QCommandLineOption extract(u"extract"_s);
    const QCommandLineOption updateMapping(u"update-mapping"_s);
    const QCommandLineOption vortex(u"install-vortex-extensions"_s);
    const QCommandLineOption vortexPlugins(u"vortex-plugins"_s, QString(), u"folder"_s);
    const QCommandLineOption quiet(u"quiet"_s);
    parser.addOptions({help, version, listGames, game, platform, location, modLoader, install, uninstall, enable, disable,
                       listMods, build, modify, restore, extract, updateMapping, vortex, vortexPlugins, quiet});

    if (!parser.parse(QCoreApplication::arguments())) {
        out.line(u"Error: "_s + parser.errorText() + u"\nRun with --help for the commands."_s);
        return kExitUsage;
    }
    if (parser.isSet(help)) {
        out.line(QString::fromUtf8(kHelp));
        return kExitOk;
    }
    if (parser.isSet(version)) {
        out.line(u"Blobset Tools "_s + QCoreApplication::applicationVersion());
        return kExitOk;
    }
    if (parser.isSet(vortex))
        return installVortexExtensions(out, parser.value(vortexPlugins));

    AppOrchestrator orchestrator;
    orchestrator.setHeadless(true);
    const bool verbose = !parser.isSet(quiet);
    QObject::connect(&orchestrator, &AppOrchestrator::uiConsoleUpdate, [&out, verbose](const QString& message) {
        if (verbose || message.startsWith(u"Error"_s) || message.startsWith(u"Warning"_s))
            out.line(message);
    });
    int lastTenth = -1;
    QObject::connect(&orchestrator, &AppOrchestrator::uiProgressUpdate, [&out, &lastTenth, verbose](int percent) {
        if (!verbose || percent <= 0 || percent / 10 == lastTenth)
            return;
        lastTenth = percent / 10;
        out.line(u"[%1%]"_s.arg(percent, 3));
    });

    orchestrator.gameSelectionInfo();
    const QVector<GameEntry>& games = orchestrator.loadedGames;
    if (games.isEmpty()) {
        out.line(u"Error: %1 couldn't be read."_s.arg(QDir::toNativeSeparators(AppPaths::gameSelectionInfoFile())));
        return kExitFailed;
    }

    if (parser.isSet(listGames)) {
        for (int i = 0; i < games.size(); ++i)
            out.line(u"%1  %2  [%3]"_s.arg(i, 2).arg(games[i].modFolder, platformsOf(games[i])));
        return kExitOk;
    }

    if (!parser.isSet(game)) {
        out.line(u"Error: say which game with --game <name> (see --list-games), or run --help."_s);
        return kExitUsage;
    }
    const std::optional<int> gameIndex = findGame(games, parser.value(game));
    if (!gameIndex) {
        out.line(u"Error: there's no game called \"%1\" (see --list-games)."_s.arg(parser.value(game)));
        return kExitUsage;
    }
    const GameEntry& entry = games[*gameIndex];
    const int platformId = platformFrom(parser.value(platform));
    if (platformId < 0) {
        out.line(u"Error: unknown platform \"%1\" (pc, ps3 or xbox360)."_s.arg(parser.value(platform)));
        return kExitUsage;
    }
    if ((platformId == 0 && !entry.pc) || (platformId == 1 && !entry.ps3) || (platformId == 2 && !entry.xbox360)) {
        out.line(u"Error: %1 isn't on %2 (it's on %3)."_s.arg(entry.modFolder, parser.value(platform), platformsOf(entry)));
        return kExitUsage;
    }

    const QList<QCommandLineOption> tasks = {build, modify, restore, extract, updateMapping};
    int taskCount = 0;
    for (const QCommandLineOption& task : tasks)
        taskCount += parser.isSet(task) ? 1 : 0;
    if (taskCount > 1) {
        out.line(u"Error: give one task per run (--build, --restore, --extract or --update-mapping)."_s);
        return kExitUsage;
    }
    const bool changesFiles = taskCount > 0 || parser.isSet(install) || parser.isSet(uninstall) || parser.isSet(enable)
                              || parser.isSet(disable) || parser.isSet(location) || parser.isSet(modLoader);
    if (changesFiles && guiRunning) {
        out.line(u"Error: Blobset Tools is open. Close it and run the command again."_s);
        return kExitBusy;
    }

    // One command at a time (Vortex can deploy twice in a row).
    QLockFile lock(QDir(AppPaths::dataDir()).filePath(u"cli.lock"_s));
    if (changesFiles && !lock.tryLock(2 * 60 * 1000)) {
        out.line(u"Error: another Blobset Tools command is still running."_s);
        return kExitBusy;
    }

    if (!orchestrator.selectGameHeadless(*gameIndex, platformId, entry.modFolder, entry.folderName))
        return kExitFailed;
    out.line(u"Game: %1 (%2)"_s.arg(entry.modFolder, parser.value(platform).toLower()));

    if (parser.isSet(location) && !orchestrator.setGameLocation(parser.value(location)))
        return kExitFailed;
    if (parser.isSet(modLoader)) {
        const QString value = parser.value(modLoader).toLower();
        if (value != u"on"_s && value != u"off"_s) {
            out.line(u"Error: --mod-loader takes on or off."_s);
            return kExitUsage;
        }
        orchestrator.setModLoaderMode(value == u"on"_s);
    }

    // ---- Mod Manager changes ----
    ModLibrary library = orchestrator.modLibrary();
    library.load();
    bool libraryChanged = false;
    QString error;

    if (parser.isSet(install)) {
        const QSet<QString> mapped = orchestrator.mappedPaths();
        for (const QString& source : parser.values(install)) {
            if (!library.install(source, mapped, &error, [&out](const QString& line) { out.line(line); })) {
                out.line(u"Error: "_s + error);
                return kExitFailed;
            }
        }
    }
    for (const QString& name : parser.values(uninstall)) {
        const ModLibrary::Mod* mod = library.find(name);
        if (!mod || !library.uninstall(mod->id, &error)) {
            out.line(u"Error: "_s + (mod ? error : u"there's no mod called \"%1\"."_s.arg(name)));
            return kExitFailed;
        }
        out.line(u"Removed %1."_s.arg(name));
    }
    for (const auto& [option, value] : {std::pair{&enable, true}, std::pair{&disable, false}}) {
        for (const QString& name : parser.values(*option)) {
            const ModLibrary::Mod* mod = library.find(name);
            if (!mod) {
                out.line(u"Error: there's no mod called \"%1\" (see --list-mods)."_s.arg(name));
                return kExitFailed;
            }
            library.setEnabled(mod->id, value);
            libraryChanged = true;
            out.line(u"%1 %2."_s.arg(value ? u"Enabled"_s : u"Disabled"_s, mod->name));
        }
    }
    if (libraryChanged && !library.save(&error)) {
        out.line(u"Error: "_s + error);
        return kExitFailed;
    }

    if (parser.isSet(listMods)) {
        library.load();
        out.line(u"Load order (later entries win when two mods have the same file):"_s);
        int position = 1;
        for (const ModLibrary::Mod& mod : library.mods()) {
            out.line(u"%1. [%2] %3%4 - %5 file(s)%6"_s.arg(position++, 2)
                         .arg(mod.enabled ? u"x"_s : u" "_s, mod.name,
                              mod.version.isEmpty() ? QString() : u" "_s + mod.version)
                         .arg(mod.fileCount)
                         .arg(mod.builtIn ? QString() : u"  (id: %1)"_s.arg(mod.id)));
        }
        const int conflicts = static_cast<int>(library.conflicts().size());
        if (conflicts > 0)
            out.line(u"%1 file(s) are in more than one enabled mod."_s.arg(conflicts));
    }

    if (taskCount == 0)
        return kExitOk;

    // ---- The task ----
    const auto runTask = [&](const std::function<void()>& start) -> int {
        bool success = false;
        QEventLoop loop;
        const QMetaObject::Connection done = QObject::connect(&orchestrator, &AppOrchestrator::taskCompleted,
                                                              &loop, [&](bool ok, const QString&) {
                                                                  success = ok;
                                                                  loop.quit();
                                                              });
        start();
        if (orchestrator.workerRunning())
            loop.exec();
        QObject::disconnect(done);
        return success ? kExitOk : kExitFailed;
    };

    const bool building = parser.isSet(build) || parser.isSet(modify);
    if (building && orchestrator.mappedPaths().isEmpty()) {
        out.line(u"The game has no file mapping yet: updating it first (this takes a while once)."_s);
        if (runTask([&]() { orchestrator.updateFileMapping(); }) != kExitOk) {
            out.line(u"Result: the file mapping couldn't be updated."_s);
            return kExitFailed;
        }
    }

    int result = kExitFailed;
    if (building)
        result = runTask([&]() { orchestrator.modifyBlobset(); });
    else if (parser.isSet(restore))
        result = runTask([&]() { orchestrator.restoreBackupFiles(); });
    else if (parser.isSet(extract))
        result = runTask([&]() { orchestrator.extractBlobset(); });
    else if (parser.isSet(updateMapping))
        result = runTask([&]() { orchestrator.updateFileMapping(); });

    out.line(result == kExitOk ? u"Result: done."_s : u"Result: failed (see the messages above)."_s);
    return result;
}

} // namespace CommandLine
