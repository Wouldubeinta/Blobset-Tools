#ifndef APP_ORCHESTRATOR_H
#define APP_ORCHESTRATOR_H

#include "tree_model.h"
#include "xml_models.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <utility>

class ModLibrary;

class AppOrchestrator : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(TreeModel* treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QStringList currentFolderFiles READ currentFolderFiles NOTIFY currentFolderFilesChanged)
    Q_PROPERTY(int maxFileItemWidth READ maxFileItemWidth NOTIFY currentFolderFilesChanged)

    // Game location prompt (MainForm.qml opens a FileDialog while this is true).
    Q_PROPERTY(bool gameLocationRequired READ gameLocationRequired NOTIFY gameLocationRequiredChanged)
    Q_PROPERTY(QString expectedBlobsetName READ expectedBlobsetName NOTIFY gameInfoChanged)
    Q_PROPERTY(QString currentGameName READ currentGameName NOTIFY gameInfoChanged)

    // True while Extract / Modify / Create / Update File Mapping runs (MainForm.qml disables those buttons).
    Q_PROPERTY(bool workerRunning READ workerRunning NOTIFY workerRunningChanged)
    // The selected game has no usable file mapping (none yet, empty or unreadable): MainForm.qml offers to run
    // Update File Mapping Data.
    Q_PROPERTY(bool fileMappingMissing READ fileMappingMissing NOTIFY fileMappingMissingChanged)
    // Goes up whenever Replace File adds / removes a file in the mods folder (MainForm's "MOD" badges re-check).
    Q_PROPERTY(int modFilesRevision READ modFilesRevision NOTIFY modFilesChanged)
    // From the selected game's GameInfo.ini: CoreDefs::BlobsetVersion value (0 = V1) and Steam app id (0 = none).
    Q_PROPERTY(int blobsetVersion READ blobsetVersion NOTIFY gameInfoChanged)
    Q_PROPERTY(uint steamGameId READ steamGameId NOTIFY gameInfoChanged)
    // V4: Modify builds the mods into <game>\BlobsetMods for the Blobset Mod Loader (dinput8.dll) instead of changing
    // the game's files. Saved in Settings.ini (ModLoader).
    Q_PROPERTY(bool modLoaderMode READ modLoaderMode WRITE setModLoaderMode NOTIFY modLoaderModeChanged)
    // The workspace shows the Mod Loader's build: <game>\BlobsetMods\data's header, the modded files from there and
    // the others from the game (assetLocation()). Mod Loader on, V4 on PC, built for the game's current header.
    Q_PROPERTY(bool modLoaderView READ modLoaderView NOTIFY modLoaderViewChanged)
    // Steam games: start the game through Steam after Modify / Create succeeded. Saved in Settings.ini (LoadGame).
    Q_PROPERTY(bool loadGameMode READ loadGameMode WRITE setLoadGameMode NOTIFY loadGameModeChanged)
    // file:/// URL of the selected game's picture (GameSelectionInfo.xml <Image>, from assets/games), empty if none.
    Q_PROPERTY(QString currentGameImage READ currentGameImage NOTIFY gameInfoChanged)

public:
    explicit AppOrchestrator(QObject* parent = nullptr);
    ~AppOrchestrator() override;

    // Singleton Instance Getter
    static AppOrchestrator* instance();

    // Thread-safe configuration getters (image provider, asset readers, ...)
    QString gameLocation() const;
    // Where the workspace reads assets from: the Mod Loader's header (<game>\BlobsetMods\data\<header>) while the
    // Mod Loader view is on, else the game's header (gameLocation()). Readers take it as their "game location";
    // BlobsetAssetReader::dataFilePath() falls back to the game's own data folder for files that aren't modded.
    QString assetLocation() const;
    bool modLoaderView() const { return m_modLoaderView; }
    int blobsetVersion() const;
    int platform() const;
    int gameId() const;
    uint steamGameId() const;
    bool modLoaderMode() const { return m_modLoaderMode; }
    void setModLoaderMode(bool enabled);
    bool loadGameMode() const { return m_loadGameMode; }
    void setLoadGameMode(bool enabled);
    QString currentGameImage();
    bool workerRunning() const { return m_workerRunning; }

    TreeModel* treeModel() const { return m_treeModel; }
    QStringList currentFolderFiles() const { return m_currentFolderFiles; }
    int maxFileItemWidth() const { return m_maxFileItemWidth; }

    bool gameLocationRequired() const { return m_gameLocationRequired; }
    bool fileMappingMissing() const { return m_fileMappingMissing; }
    int modFilesRevision() const { return m_modFilesRevision; }

    // ---- Replace File (MainForm drag & drop / right click, the TXPK / M3MP viewers' Save Changes) ----
    // workspacePath: the file's path in the blobset ('/' or '\\' separated), as the Files panel shows it.
    // games\<game>\<platform>\mods\<workspacePath>: where Modify / Build pick the replacement up.
    Q_INVOKABLE QString modFilePath(const QString& workspacePath) const;
    Q_INVOKABLE bool hasModFile(const QString& workspacePath) const;
    // Copies sourcePathOrUrl to modFilePath(workspacePath). The extension has to match. { ok, message, target }
    Q_INVOKABLE QVariantMap replaceAsset(const QString& workspacePath, const QString& sourcePathOrUrl);
    // Files / folders dropped on MainForm's Files panel. Each file (folders: every file inside, recursively)
    // replaces the blobset file it matches:
    //   by its path - the longest tail of "<dropped folder>/<sub folders>/<name>" that a mapped path ends with
    //                 (the open folder's match wins when there are several);
    //   by its name - in the open folder, else a name that's unique in the blobset.
    // A .wav matches the .wem of the same name. One file that matches nothing replaces `fallbackPath` (the file
    // it was dropped on) when given. { replaced, failed, unmatched: [names], message }
    Q_INVOKABLE QVariantMap replaceDropped(const QStringList& urls, const QString& currentFolder,
                                           const QString& fallbackPath = QString());
    // Writes bytes as the replacement (the TXPK / M3MP viewers' rebuilt packs).
    QVariantMap writeModFile(const QString& workspacePath, const QByteArray& data);
    Q_INVOKABLE bool removeModFile(const QString& workspacePath);
    // A file was written into the mods folder elsewhere (the TXPK / M3MP viewers): refresh the "MOD" badges.
    void notifyModFilesChanged();
    // The blobset path ('/' separated) of a path hash (the TXPK / M3MP viewers only get the hash), empty if unknown.
    Q_INVOKABLE QString pathFromHash(const QString& hexHash) const;
    // Opens the mods folder that holds the file (or the mods folder itself).
    Q_INVOKABLE void showModFile(const QString& workspacePath) const;
    QString expectedBlobsetName() const { return archiveName(); }
    QString currentGameName() const;

    Q_INVOKABLE void selectFolderIndex(const QModelIndex& index);
    Q_INVOKABLE QString getHashFromPath(const QString& filePath) const;
    // The loaded game's .mapping file (File Mapping Editor), empty when there is none yet.
    Q_INVOKABLE QString mappingFilePath() const { return findMappingFile(); }
    // The mapping in the data folder (a shipped one is copied there first), for the File Mapping Editor to save.
    Q_INVOKABLE QString writableMappingFilePath();
    // Reads the selected game's mapping and blobset again and shows MainForm (after the mapping was edited).
    Q_INVOKABLE void reloadWorkspace();
    // games/<game>/<platform>/mods: what Modify / Create read. Empty when no game is loaded.
    Q_INVOKABLE QString modsFolderPath() const;

    // MainForm.qml right click > Save As: writes one asset of the loaded blobset to outPath (path or file:// URL),
    // on a background thread. format:
    //   "raw" - decompressed; the main section to outPath, a vram section (if any) to "<outPath>.vram"
    //   "dds" - a standard PC .dds (console data converted)
    //   "png" - the decoded texture (flipped / alpha as given)   "wem" - the stream as stored
    //   "wav" - the WEM decoded to 16 bit PCM
    // Answers with assetSaved().
    Q_INVOKABLE void saveAssetAs(const QString& hexHash, const QString& workspacePath, const QString& outPath,
                                 const QString& format, bool flipped = false, bool alphaEnabled = true);
    // Assets / folders whose name, path or hash contains `query` (case insensitive), best matches first:
    // [{ folder: "a/b", file: "c.dds" (empty for a folder), path: "a/b/c.dds" }, ...]
    Q_INVOKABLE QVariantList searchAssets(const QString& query, int maxResults = 2000) const;

    // Switches to the viewer that matches the extension and hands it the asset.
    // displayName (e.g. the workspace path) is shown by viewers that have no name of their own (Hex Inspector).
    // Extensions without a dedicated viewer open in the Hex Inspector.
    Q_INVOKABLE void openWorkspaceFileByHash(const QString& hexHash, const QString& fileExtension,
                                             const QString& displayName = QString());

    // Builds the metadata of the selected workspace file (location, blobset entry, and per type details such as
    // the texture list of a TXPK or the format of a DDS) on a worker thread and hands it to assetInfoReady() as
    // HTML for the Console Log. Only the most recent request is answered.
    Q_INVOKABLE void requestAssetInfo(const QString& hexHash, const QString& workspacePath);

    // Read-only summary of games/<modFolder>/<platform>/GameInfo.ini for the game selection screen
    // (does not change the loaded game).
    Q_INVOKABLE QVariantMap gameInfoPreview(const QString& modGameFolderName, int platformId) const;
    Q_INVOKABLE void selectGameAndLoad(int gameId, int platformId, const QString& modGameFolderName, const QString& gameFolderName);
    Q_INVOKABLE void loadGameProfile();
    Q_INVOKABLE void gameSelectionInfo();
    Q_INVOKABLE void readGameIni(const QString& iniPath);
    Q_INVOKABLE void updateFileMapping();
    // Names numbered / unknown mapping entries by hashing candidate names (see NameResolverWorker).
    Q_INVOKABLE void resolveFileNames();
    // Puts the game's original files back from games/<game>/<platform>/backup (see RestoreWorker).
    Q_INVOKABLE void restoreBackupFiles();
    // Steam games only (SteamGameId in GameInfo.ini): start the game / let Steam verify the game's files.
    Q_INVOKABLE void launchGame();
    Q_INVOKABLE void validateSteamFiles();
    Q_INVOKABLE void extractBlobset();
    // Modify: puts the workspace mods folder and every enabled Mod Manager mod (in load order) into the game.
    // V3 / V4 games modified in place restore the original files first, so mods that were removed or disabled
    // since the last run disappear too; with nothing to apply the game's files are restored.
    Q_INVOKABLE void modifyBlobset();
    Q_INVOKABLE void createBlobset();

    // ---- Mod Manager / command line -------------------------------------------------------------------------
    // games/<game>/<platform> and the game's own folder (where the exe lives), empty when unknown.
    QString platformDirPath() const;
    QString gameRootPath() const;
    // <game>\BlobsetToolsMods: the folder Vortex deploys into (an extra, highest priority mod source).
    QString externalModsPath() const;
    // The Mod Manager's library for the loaded game (load order, enabled mods).
    ModLibrary modLibrary() const;
    // Lower case, '/' separated paths of the loaded game's mapping (finding a mod's root folder on install).
    QSet<QString> mappedPaths() const;

    // Command line: selects a game without loading the workspace or showing any view. False (and a console
    // message) when the game has no file mapping / GameInfo.ini yet.
    bool selectGameHeadless(int gameId, int platformId, const QString& modGameFolderName, const QString& gameFolderName);

    // Messages from startup (data folder set up / moved), handed to the first Console Log that asks (MainForm.qml).
    void setStartupMessages(const QStringList& messages) { m_startupMessages = messages; }
    Q_INVOKABLE QStringList takeStartupMessages() { return std::exchange(m_startupMessages, {}); }
    // The writable data folder (Documents\Blobset Tools) for "Open Data Folder".
    Q_INVOKABLE QString dataFolderPath() const;
    // A text file next to the exe (changelog.txt for About > Changelog), empty when it's missing.
    Q_INVOKABLE QString readAppTextFile(const QString& fileName) const;
    // file:/// URL of Documents\Blobset Tools\games\<game>\<platform>\<subFolder> (made if missing): the
    // default folder of the viewers' Extract dialogs (txpk, m3mp, bsb, ...). Empty when no game is loaded.
    Q_INVOKABLE QString workFolderUrl(const QString& subFolder) const;
    void setHeadless(bool headless) { m_headless = headless; }

    // Accepts the blobset file (or the folder that holds it) as a path or file:// URL, saves it to GameInfo.ini
    // and reloads the workspace. Returns false (and logs why) when it isn't the expected blobset.
    Q_INVOKABLE bool setGameLocation(const QString& path);

    Q_INVOKABLE void saveBrowserState(const QString& folderPath, const QString& fileName) {
        m_savedFolderPath = folderPath;
        m_savedFileName = fileName;
    }
    Q_INVOKABLE void clearBrowserState() { m_savedFolderPath.clear(); m_savedFileName.clear(); }
    Q_INVOKABLE QString savedFolderPath() const { return m_savedFolderPath; }
    Q_INVOKABLE QString savedFileName() const { return m_savedFileName; }

    QVector<GameEntry> loadedGames;

signals:
    void viewChanged(const QString& viewName);
    void uiConsoleUpdate(const QString& message);
    void assetInfoReady(const QString& html);   // Answer to requestAssetInfo(): replaces the Console Log.
    void uiStatusUpdate(const QString& message);
    void assetSaved(bool success, const QString& message);   // Answer to saveAssetAs().
    void uiProgressUpdate(int percentage);
    void profileLoaded(int gameId, int platformId, const QString& modGameFolderName, const QString& gameFolderName);
    void gameSelectionInfoLoaded(const QVariantList& gameSelectedInfo);
    void currentFolderFilesChanged();
    void missingMetadataError(const QString& title, const QString& message);
    void gameLocationRequiredChanged();
    void fileMappingMissingChanged();
    void modFilesChanged();
    // Steam game (PC) only. pending: Steam has an update waiting to install (Restore Backup Files first);
    // otherwise the game was updated since its file mapping was made (Update File Mapping Data).
    void steamUpdateDetected(bool pending, const QString& gameName, const QString& details);
    void workerRunningChanged();
    void modLoaderModeChanged();
    void modLoaderViewChanged();
    void loadGameModeChanged();
    void gameInfoChanged();
    void gameLocationRequested();   // Raised every time the UI should show the game location dialog.
    // A task (or a chain of tasks, e.g. Restore + Modify) is over: success says whether every step worked.
    void taskCompleted(bool success, const QString& message);
    void openBnkViewerByHash(QString pathHash);
    void openDdsViewerByHash(QString pathHash, QString displayName);
    void openFmodViewerByHash(QString pathHash);
    void openTxpkViewerByHash(QString pathHash);
    void openM3mpViewerByHash(QString pathHash);
    void openMeshViewerByHash(QString pathHash, QString displayName);
    void openHexViewerByHash(QString pathHash, QString displayName);
    void openStringEditorByHash(QString pathHash, QString displayName);
    void openBsbViewerByHash(QString pathHash, QString displayName);

private slots:
    void handleProgress(int percentage);
    void handleConsoleUpdate(const QString& message);
    void handleStatusUpdate(const QString& message);
    void handleFinished(const QString& finalStatus);

private:
    static AppOrchestrator* s_instance;
    mutable QMutex m_mutex; // Protects configuration variables across threads

    int m_gameId;               // The real game id (GameInfo.ini GameId: 0 AFL Live ... 23 Cricket 26).
    int m_selectionIndex = 0;   // The game's place in GameSelectionInfo.xml (only the games picked in setup).
    QString m_gameName;
    bool m_launchAfterTask = false;   // Set by a successful Modify / Create, handled in handleFinished().
    uint m_steamGameId;
    int m_blobsetVersion;
    QString m_gameLocation;

    int m_platformId;
    QString m_modGameFolderName;
    QString m_gameFolderName;
    QString m_relativeBlobsetPath;  // "data-0.blobset.pc" or "data/data-0.blobset.pc", fixed per selected game.

    bool m_isBigEndian;
    bool m_gameLocationRequired = false;
    bool m_fileMappingMissing = false;
    int m_modFilesRevision = 0;
    bool m_quietReplace = false;   // replaceDropped(): one summary instead of a line per replaced file.
    bool m_updateMappingAfterLocation = false;
    bool m_recordSteamBuildAfterTask = false;   // The running task is Update File Mapping Data.
    QSet<QString> m_steamNoticesShown;          // Each Steam update popup once per session.   // Update File Mapping waits for the game location dialog.
    quint64 m_loadGeneration = 0;   // Results of an older workspace load are dropped.
    bool m_workerRunning = false;   // Only one Extract / Modify / Create / mapping worker at a time.
    bool m_reloadAfterTask = true;  // The running task changes the blobset / mapping: reload the workspace after it.
    bool m_taskSucceeded = false;   // The running worker said taskSucceeded().
    std::function<void()> m_nextTask;   // Runs when the current task succeeded (Restore -> Modify).
    bool m_headless = false;        // Command line: no workspace reloads, no game launch.
    QStringList m_startupMessages;
    void startModify(const QStringList& sources);
    void restoreBackupFilesUnchecked();   // restoreBackupFiles() without the worker / location checks.
    // Settings.ini values, read once (QML reads the properties often).
    bool m_modLoaderMode = false;
    bool m_modLoaderView = false;   // The loaded workspace reads the Mod Loader's build (assetLocation()).
    QString m_modLoaderHeaderPath;  // That build's header while the view is on (m_mutex).
    QString modLoaderHeader(QString* whyNot = nullptr) const;   // The build's header when it can be shown, else empty.
    void setModLoaderView(bool on);
    bool m_loadGameMode = false;
    QString settingsPath() const;
    bool startWorkerAllowed();      // False (and a console message) while a worker is running.
    void setWorkerRunning(bool running);
    std::atomic<quint64> m_assetInfoTicket{0};   // requestAssetInfo(): older requests are skipped / dropped.

    // Paths
    static QString appPath();
    QString platformFolder() const;
    QString archiveName() const;
    QString gameInfoIniPath() const;
    QString findMappingFile() const;

    // Game location
    bool resolveGameLocation();               // INI value, then Steam (only when SteamGameId > 0).
    bool ensureGameLocation();                // For Extract / Modify / Create / Update mapping.
    void storeGameLocation(const QString& blobsetFile);
    void setGameLocationRequired(bool required);
    void setFileMappingMissing(bool missing);
    // The real game id (GameInfo.ini GameId, else fallbackId) and where the blobset sits in the game folder.
    void applyGameId(int fallbackId);
    // Steam's appmanifest against the build the mapping was made from (GameInfo.ini SteamBuildId).
    void checkSteamUpdate();
    // Remembers the installed Steam build as the one the mapping matches (after Update File Mapping Data).
    void recordSteamBuild();
    // Steam games (PC): the first line of <game>\version.txt becomes GameInfo.ini Version (Update File Mapping).
    void readGameVersionFile();

    void startWorkspaceLoad(const QString& mappingPath);

    // reloadWorkspace: false for tasks that leave the blobset and the mapping alone (Extract).
    template <typename Worker>
    void runWorker(Worker* worker, void (Worker::*start)(), bool reloadWorkspace = true);

    TreeModel* m_treeModel;
    QStringList m_currentFolderFiles;
    int m_maxFileItemWidth = 180;
    // Memory map tracking container correlating a normalized file path to its pathHash
    QHash<QString, quint64> m_pathHashLookup;

    QString m_savedFolderPath;
    QString m_savedFileName;
};

#endif // APP_ORCHESTRATOR_H
