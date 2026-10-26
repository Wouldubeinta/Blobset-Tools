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

public:
    explicit ModManager(AppOrchestrator* orchestrator, QObject* parent = nullptr);

    QVariantList mods() const { return m_mods; }
    int conflictCount() const { return m_conflictCount; }
    int enabledFileCount() const { return m_enabledFileCount; }
    QString libraryPath() const { return m_libraryPath; }
    bool busy() const { return m_busy; }

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
    // The first `max` files of a mod ('/' separated).
    Q_INVOKABLE QStringList filesOf(const QString& id, int max = 2000) const;
    Q_INVOKABLE void openFolder(const QString& id) const;
    Q_INVOKABLE void openLibraryFolder() const;
    // Applies the workspace + every enabled mod (same as the Modify button).
    Q_INVOKABLE void build();
    // Copies the Vortex game extensions (vortex folder next to the exe) into Vortex (Blobset-Tools.exe
    // --install-vortex-extensions) and reports the result with message().
    Q_INVOKABLE void installVortexExtensions();

signals:
    void modsChanged();
    void busyChanged();
    void message(bool ok, const QString& text);

private:
    AppOrchestrator* m_orchestrator;
    QVariantList m_mods;
    int m_conflictCount = 0;
    int m_enabledFileCount = 0;
    QString m_libraryPath;
    bool m_busy = false;

    void setBusy(bool busy);
    void report(bool ok, const QString& text);
};

#endif // MOD_MANAGER_H
