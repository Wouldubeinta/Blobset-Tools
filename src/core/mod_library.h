#ifndef MOD_LIBRARY_H
#define MOD_LIBRARY_H

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

/// The installed mods of one game / platform and their load order (Mod Manager and the command line).
///
///   games/<game>/<platform>/mod_library/
///       modlist.json            load order (lowest priority first) and which mods are enabled
///       <mod id>/               one folder per mod: its files at the paths the mapping uses
///           blobsetmod.json     name, author, version, description, url, image (optional; mod authors can ship it)
///           preview.png         the mod's picture ("image" in blobsetmod.json, else preview / image / screenshot /
///                               cover / thumbnail .png / .jpg, else the only picture in the folder); never built
///                               into the game
///
/// Two built in entries take part in the load order as well:
///   "@workspace"  games/<game>/<platform>/mods - the folder Modify always used (lowest priority by default)
///   "@external"   <game>\BlobsetToolsMods      - where Vortex deploys (highest priority by default; only listed
///                                                when the folder exists)
///
/// Modify takes the enabled entries' folders in this order; when two have the same file, the later one wins.
class ModLibrary {
public:
    static constexpr const char* kWorkspaceId = "@workspace";
    static constexpr const char* kExternalId = "@external";
    static constexpr const char* kExternalFolder = "BlobsetToolsMods";

    struct Mod {
        QString id;            // Folder name in the library, or one of the built in ids.
        QString name;
        QString author;
        QString version;
        QString description;
        QString url;
        QString path;          // The folder that holds the mod's files.
        bool enabled = true;
        bool builtIn = false;  // Workspace / external: can't be removed or edited.
        int fileCount = 0;     // Files Modify would use (supported types).
        QString image;         // Preview picture (absolute path, .png / .jpg), empty when there's none.
        QDateTime installed;
    };

    struct Conflict {
        QString path;          // '/' separated, as in the mod.
        QStringList ids;       // Every enabled mod that has the file, in load order: the last one wins.
    };

    /// platformDir: games/<game>/<platform>. externalDir: <game>\BlobsetToolsMods (empty when unknown).
    ModLibrary(const QString& platformDir, const QString& externalDir, const QStringList& supportedExtensions);

    QString libraryDir() const { return m_libraryDir; }
    QString workspaceDir() const { return m_workspaceDir; }
    QString externalDir() const { return m_externalDir; }

    /// Reads modlist.json and the mod folders (new folders are added enabled, at the end of the load order).
    void load();
    bool save(QString* error = nullptr) const;

    const QList<Mod>& mods() const { return m_mods; }
    const Mod* find(const QString& idOrName) const;   // By id, then by name (case insensitive).

    bool setEnabled(const QString& id, bool enabled);
    bool move(int from, int to);
    bool setInfo(const QString& id, const QString& name, const QString& author, const QString& version,
                 const QString& description, QString* error = nullptr);

    /// Enabled folders in load order (lowest priority first), for ModifyWorker::setModSources().
    QStringList enabledSources() const;
    /// Number of files the enabled entries hand to Modify (duplicates counted once).
    int enabledFileCount() const;
    /// Files that more than one enabled entry has.
    QList<Conflict> conflicts() const;

    /// Installs a mod from a folder or an archive (.zip / .7z / .rar / ...) and adds it, enabled, at the end of the
    /// load order. The folder inside the source whose files best match `mappedPaths` (lower case, '/' separated) is
    /// taken as the mod's root, so wrapper folders in archives don't matter. A mod with the same name is replaced.
    std::optional<Mod> install(const QString& source, const QSet<QString>& mappedPaths, QString* error,
                               const std::function<void(const QString&)>& log = {});
    bool uninstall(const QString& id, QString* error = nullptr);
    /// Copies a .png / .jpg into the mod's folder as its picture (preview.<ext>), replacing the old one.
    bool setImage(const QString& id, const QString& imageFile, QString* error = nullptr);
    /// Removes the mod's picture.
    bool clearImage(const QString& id, QString* error = nullptr);
    /// The picture of a mod folder ("image" in its blobsetmod.json, else a preview.png or the like), or empty.
    static QString findImage(const QString& folder);

    /// The supported files of a folder, relative ('/' separated).
    QStringList modFiles(const QString& folder) const;

    /// Unpacks an archive with 7-Zip when it's installed, otherwise with Windows' tar (zip / tar / gz).
    static bool extractArchive(const QString& archive, const QString& destination, QString* error);
    static bool isArchive(const QString& path);

private:
    QString m_platformDir;
    QString m_libraryDir;
    QString m_workspaceDir;
    QString m_externalDir;
    QStringList m_extensions;
    QList<Mod> m_mods;

    Mod readMod(const QString& id, const QString& folder) const;
    QString findRoot(const QString& folder, const QSet<QString>& mappedPaths) const;
    QString uniqueId(const QString& name) const;
};

#endif // MOD_LIBRARY_H
