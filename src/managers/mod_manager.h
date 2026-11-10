#ifndef MOD_MANAGER_H
#define MOD_MANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class AppOrchestrator;

/// Mod Manager view (ModManager.qml): the loaded game's mod library (see ModLibrary) - install, enable / disable,
/// load order, conflicts - and Build (AppOrchestrator::modifyBlobset() with every enabled mod).
class ModManager : public QObject {
    Q_OBJECT

    // [{ id, name, author, version, description, url, path, enabled, builtIn, fileCount, installed,
    //    wins, loses }]  in load order (lowest priority first). wins / loses: files this mod takes from a mod above /
    // gives up to a mod below it in the list.
    Q_PROPERTY(QVariantList mods READ mods NOTIFY modsChanged)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY modsChanged)   // Files more than one enabled mod has.
    Q_PROPERTY(int enabledFileCount READ enabledFileCount NOTIFY modsChanged)
    Q_PROPERTY(QString libraryPath READ libraryPath NOTIFY modsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)                     // An install is running.
    // DLL mods (V4 PC games, see DllModInstaller): the library's files [{ name, size, loads, installed }].
    Q_PROPERTY(QVariantList dllMods READ dllMods NOTIFY dllModsChanged)
    Q_PROPERTY(bool dllModsSupported READ dllModsSupported NOTIFY dllModsChanged)
    Q_PROPERTY(QString dllModsFolder READ dllModsFolder NOTIFY dllModsChanged)   // The library folder.
    Q_PROPERTY(QString dllModsGameFolder READ dllModsGameFolder NOTIFY dllModsChanged)   // <game>\DLL_Mods

public:
    explicit ModManager(AppOrchestrator* orchestrator, QObject* parent = nullptr);

    QVariantList mods() const { return m_mods; }
    int conflictCount() const { return m_conflictCount; }
    int enabledFileCount() const { return m_enabledFileCount; }
    QString libraryPath() const { return m_libraryPath; }
    bool busy() const { return m_busy; }
    QVariantList dllMods() const { return m_dllMods; }
    bool dllModsSupported() const { return m_dllModsSupported; }
    QString dllModsFolder() const { return m_dllModsFolder; }
    QString dllModsGameFolder() const { return m_dllModsGameFolder; }

    Q_INVOKABLE void reload();
    // A folder, an archive or a file:// URL (drag and drop / file dialog). Runs in the background.
    Q_INVOKABLE void install(const QString& pathOrUrl);
    Q_INVOKABLE void uninstall(const QString& id);
    Q_INVOKABLE void setEnabled(const QString& id, bool enabled);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void saveInfo(const QString& id, const QString& name, const QString& author, const QString& version,
                              const QString& description);
    // The files of `id` another enabled mod also has: [{ path, winner (name), others: "A, B" }].
    Q_INVOKABLE QVariantList conflictsFor(const QString& id) const;
    // The mod's picture (.png / .jpg, file path or file:// URL), copied into its folder as preview.<ext>.
    Q_INVOKABLE void setImage(const QString& id, const QString& pathOrUrl);
    Q_INVOKABLE void clearImage(const QString& id);
    // The first `max` files of a mod ('/' separated).
    Q_INVOKABLE QStringList filesOf(const QString& id, int max = 2000) const;
    Q_INVOKABLE void openFolder(const QString& id) const;
    Q_INVOKABLE void openLibraryFolder() const;
    // Applies the workspace + every enabled mod (same as the Modify button).
    Q_INVOKABLE void build();
    // Copies the Vortex game extensions (vortex folder next to the exe) into Vortex (Blobset-Tools.exe
    // --install-vortex-extensions) and reports the result with message().
    Q_INVOKABLE void installVortexExtensions();

    // ---- DLL mods ----
    Q_INVOKABLE void reloadDllMods();
    // Files (.dll / .asi and anything they need, e.g. .ini) or folders, as paths or file:// URLs: copied into the
    // library. Build / installDllMods() puts them into the game.
    Q_INVOKABLE void addDllMods(const QStringList& pathsOrUrls);
    Q_INVOKABLE void removeDllMod(const QString& name);   // To the Recycle Bin; taken out of the game on install.
    Q_INVOKABLE void openDllModsFolder() const;
    Q_INVOKABLE void installDllMods();                    // Library -> <game>\DLL_Mods now (no Build needed).

signals:
    void modsChanged();
    void busyChanged();
    void dllModsChanged();
    void message(bool ok, const QString& text);

private:
    AppOrchestrator* m_orchestrator;
    QVariantList m_mods;
    int m_conflictCount = 0;
    int m_enabledFileCount = 0;
    QString m_libraryPath;
    bool m_busy = false;
    QVariantList m_dllMods;
    bool m_dllModsSupported = false;
    QString m_dllModsFolder;
    QString m_dllModsGameFolder;

    void setBusy(bool busy);
    void report(bool ok, const QString& text);
};

#endif // MOD_MANAGER_H
