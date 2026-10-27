#include "mod_library.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int kMaxRootDepth = 6;
const QString kModListFile = u"modlist.json"_s;
const QString kModInfoFile = u"blobsetmod.json"_s;
const QStringList kImageSuffixes = {u"png"_s, u"jpg"_s, u"jpeg"_s};
const QStringList kImageNames = {u"preview"_s, u"image"_s, u"screenshot"_s, u"cover"_s, u"thumbnail"_s, u"thumb"_s};

bool isImageFile(const QString& path) {
    return kImageSuffixes.contains(QFileInfo(path).suffix().toLower()) && QFileInfo(path).isFile();
}

QString normalizedKey(QString path) {
    path.replace(u'\\', u'/');
    return path.toLower();
}

/// Top level folders the games' mappings use: a mod folder holding these is a mod root even without a mapping.
bool isKnownTopFolder(const QString& name) {
    static const QSet<QString> known = {
        u"anim"_s, u"body_shapes"_s, u"characters"_s, u"collision"_s, u"crowd"_s, u"dds_txpk"_s, u"editor"_s,
        u"fields"_s, u"font"_s, u"fonts"_s, u"grass"_s, u"heads"_s, u"hj_offsets"_s, u"m3mp"_s, u"material"_s,
        u"model"_s, u"noesis"_s, u"objects"_s, u"players"_s, u"png"_s, u"props"_s, u"sound"_s, u"stadiumcreator"_s,
        u"stadiums"_s, u"text"_s, u"textures"_s, u"txpl"_s, u"ui"_s, u"unknown"_s};
    return known.contains(name.toLower());
}

bool copyTree(const QString& from, const QString& to, QString* error) {
    const QDir source(from);
    if (!QDir().mkpath(to)) {
        if (error)
            *error = u"Could not create %1."_s.arg(QDir::toNativeSeparators(to));
        return false;
    }
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = QDir(to).filePath(source.relativeFilePath(file));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(file, target)) {
            if (error)
                *error = u"Could not copy %1."_s.arg(QDir::toNativeSeparators(file));
            return false;
        }
    }
    return true;
}

bool removeTree(const QString& path) {
    if (!QFileInfo(path).isDir())
        return true;
    return QFile::moveToTrash(path) || QDir(path).removeRecursively();
}

QJsonObject readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJson(const QString& path, const QJsonObject& object, QString* error) {
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

} // namespace

ModLibrary::ModLibrary(const QString& platformDir, const QString& externalDir, const QStringList& supportedExtensions)
    : m_platformDir(platformDir),
      m_libraryDir(QDir(platformDir).filePath(u"mod_library"_s)),
      m_workspaceDir(QDir(platformDir).filePath(u"mods"_s)),
      m_externalDir(externalDir),
      m_extensions(supportedExtensions) {}

// ---------------------------------------------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------------------------------------------

QStringList ModLibrary::modFiles(const QString& folder) const {
    QStringList files;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return files;

    QStringList filters;
    for (const QString& extension : m_extensions)
        filters.append(u"*."_s + extension);

    const QDir root(folder);
    QDirIterator it(folder, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(root.relativeFilePath(it.next()));
    return files;
}

ModLibrary::Mod ModLibrary::readMod(const QString& id, const QString& folder) const {
    Mod mod;
    mod.id = id;
    mod.path = folder;

    if (id == QLatin1String(kWorkspaceId)) {
        mod.name = u"Workspace (mods folder)"_s;
        mod.description = u"The files in the workspace mods folder, the ones Modify always used."_s;
        mod.builtIn = true;
    } else if (id == QLatin1String(kExternalId)) {
        mod.name = u"Vortex / external mods (BlobsetToolsMods)"_s;
        mod.description = u"Files deployed into the game's BlobsetToolsMods folder by Vortex or by hand."_s;
        mod.builtIn = true;
    } else {
        const QJsonObject info = readJson(QDir(folder).filePath(kModInfoFile));
        mod.name = info.value(u"name"_s).toString(id);
        mod.author = info.value(u"author"_s).toString();
        mod.version = info.value(u"version"_s).toString();
        mod.description = info.value(u"description"_s).toString();
        mod.url = info.value(u"url"_s).toString();
        mod.installed = QDateTime::fromString(info.value(u"installed"_s).toString(), Qt::ISODate);
        mod.image = findImage(folder);
    }
    mod.fileCount = static_cast<int>(modFiles(folder).size());
    return mod;
}

void ModLibrary::load() {
    m_mods.clear();
    QSet<QString> seen;

    const QJsonArray order = readJson(QDir(m_libraryDir).filePath(kModListFile)).value(u"order"_s).toArray();
    for (const QJsonValue& value : order) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(u"id"_s).toString();
        if (id.isEmpty() || seen.contains(id))
            continue;

        QString folder;
        if (id == QLatin1String(kWorkspaceId))
            folder = m_workspaceDir;
        else if (id == QLatin1String(kExternalId))
            folder = m_externalDir;
        else
            folder = QDir(m_libraryDir).filePath(id);

        // Library mods whose folder is gone and the external folder when it doesn't exist drop out.
        if (id != QLatin1String(kWorkspaceId) && (folder.isEmpty() || !QFileInfo(folder).isDir()))
            continue;

        Mod mod = readMod(id, folder);
        mod.enabled = entry.value(u"enabled"_s).toBool(true);
        m_mods.append(mod);
        seen.insert(id);
    }

    // The workspace always takes part (lowest priority unless it was moved).
    if (!seen.contains(QLatin1String(kWorkspaceId)))
        m_mods.prepend(readMod(QLatin1String(kWorkspaceId), m_workspaceDir));

    // Folders dropped into the library by hand: added at the end, enabled.
    QStringList folders = QDir(m_libraryDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QString& id : std::as_const(folders)) {
        if (!seen.contains(id) && !id.startsWith(u'.'))
            m_mods.append(readMod(id, QDir(m_libraryDir).filePath(id)));
    }

    if (!seen.contains(QLatin1String(kExternalId)) && !m_externalDir.isEmpty() && QFileInfo(m_externalDir).isDir())
        m_mods.append(readMod(QLatin1String(kExternalId), m_externalDir));
}

bool ModLibrary::save(QString* error) const {
    if (!QDir().mkpath(m_libraryDir)) {
        if (error)
            *error = u"Could not create %1."_s.arg(QDir::toNativeSeparators(m_libraryDir));
        return false;
    }
    QJsonArray order;
    for (const Mod& mod : m_mods)
        order.append(QJsonObject{{u"id"_s, mod.id}, {u"enabled"_s, mod.enabled}});
    return writeJson(QDir(m_libraryDir).filePath(kModListFile), QJsonObject{{u"order"_s, order}}, error);
}

const ModLibrary::Mod* ModLibrary::find(const QString& idOrName) const {
    for (const Mod& mod : m_mods)
        if (mod.id.compare(idOrName, Qt::CaseInsensitive) == 0)
            return &mod;
    for (const Mod& mod : m_mods)
        if (mod.name.compare(idOrName, Qt::CaseInsensitive) == 0)
            return &mod;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Load order
// ---------------------------------------------------------------------------------------------------------------

bool ModLibrary::setEnabled(const QString& id, bool enabled) {
    for (Mod& mod : m_mods) {
        if (mod.id == id) {
            mod.enabled = enabled;
            return true;
        }
    }
    return false;
}

bool ModLibrary::move(int from, int to) {
    if (from < 0 || from >= m_mods.size() || to < 0 || to >= m_mods.size())
        return false;
    m_mods.move(from, to);
    return true;
}

QString ModLibrary::findImage(const QString& folder) {
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return {};
    const QDir dir(folder);
    // 1. blobsetmod.json "image": a path relative to the mod folder.
    const QString named = readJson(dir.filePath(kModInfoFile)).value(u"image"_s).toString().trimmed();
    if (!named.isEmpty()) {
        const QString path = QDir::cleanPath(dir.filePath(named));
        if (isImageFile(path) && !dir.relativeFilePath(path).startsWith(u".."_s))
            return path;
    }
    // 2. preview.png / image.png / ... in the mod's top folder.
    QStringList filters;
    for (const QString& suffix : kImageSuffixes)
        filters << u"*."_s + suffix;
    const QStringList pictures = dir.entryList(filters, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& name : kImageNames)
        for (const QString& picture : pictures)
            if (QFileInfo(picture).completeBaseName().compare(name, Qt::CaseInsensitive) == 0)
                return dir.filePath(picture);
    // 3. The only picture there.
    return pictures.size() == 1 ? dir.filePath(pictures.first()) : QString();
}

bool ModLibrary::setImage(const QString& id, const QString& imageFile, QString* error) {
    const auto failWith = [error](const QString& reason) {
        if (error)
            *error = reason;
        return false;
    };
    const Mod* mod = find(id);
    if (!mod || mod->id != id)
        return failWith(u"%1 isn't installed."_s.arg(id));
    if (mod->builtIn)
        return failWith(u"%1 can't have a picture."_s.arg(mod->name));
    if (!isImageFile(imageFile))
        return failWith(u"%1 isn't a .png or .jpg picture."_s.arg(QFileInfo(imageFile).fileName()));

    const QDir dir(mod->path);
    const QString target = dir.filePath(u"preview."_s + QFileInfo(imageFile).suffix().toLower());
    if (QDir::cleanPath(QFileInfo(imageFile).absoluteFilePath()).compare(QDir::cleanPath(target), Qt::CaseInsensitive) != 0) {
        for (const QString& suffix : kImageSuffixes)
            QFile::remove(dir.filePath(u"preview."_s + suffix));
        if (!QFile::copy(imageFile, target))
            return failWith(u"Could not copy the picture into %1."_s.arg(QDir::toNativeSeparators(mod->path)));
    }
    QJsonObject info = readJson(dir.filePath(kModInfoFile));
    info[u"image"_s] = QFileInfo(target).fileName();
    if (!writeJson(dir.filePath(kModInfoFile), info, error))
        return false;
    for (Mod& entry : m_mods)
        if (entry.id == id)
            entry.image = target;
    return true;
}

bool ModLibrary::clearImage(const QString& id, QString* error) {
    const Mod* mod = find(id);
    if (!mod || mod->id != id || mod->builtIn) {
        if (error)
            *error = u"%1 isn't an installed mod."_s.arg(id);
        return false;
    }
    const QString image = findImage(mod->path);
    const QDir dir(mod->path);
    if (!image.isEmpty())
        QFile::remove(image);
    QJsonObject info = readJson(dir.filePath(kModInfoFile));
    info.remove(u"image"_s);
    if (!writeJson(dir.filePath(kModInfoFile), info, error))
        return false;
    for (Mod& entry : m_mods)
        if (entry.id == id)
            entry.image = findImage(entry.path);   // Another picture may still be there.
    return true;
}

bool ModLibrary::setInfo(const QString& id, const QString& name, const QString& author, const QString& version,
                         const QString& description, QString* error) {
    for (Mod& mod : m_mods) {
        if (mod.id != id)
            continue;
        if (mod.builtIn) {
            if (error)
                *error = u"%1 can't be renamed."_s.arg(mod.name);
            return false;
        }
        const QString infoPath = QDir(mod.path).filePath(kModInfoFile);
        QJsonObject info = readJson(infoPath);
        info[u"name"_s] = name.trimmed().isEmpty() ? mod.id : name.trimmed();
        info[u"author"_s] = author.trimmed();
        info[u"version"_s] = version.trimmed();
        info[u"description"_s] = description.trimmed();
        if (!writeJson(infoPath, info, error))
            return false;
        mod.name = info.value(u"name"_s).toString();
        mod.author = info.value(u"author"_s).toString();
        mod.version = info.value(u"version"_s).toString();
        mod.description = info.value(u"description"_s).toString();
        return true;
    }
    return false;
}

QStringList ModLibrary::enabledSources() const {
    QStringList sources;
    for (const Mod& mod : m_mods)
        if (mod.enabled && !mod.path.isEmpty() && QFileInfo(mod.path).isDir())
            sources.append(mod.path);
    return sources;
}

int ModLibrary::enabledFileCount() const {
    QSet<QString> keys;
    for (const Mod& mod : m_mods) {
        if (!mod.enabled)
            continue;
        for (const QString& file : modFiles(mod.path))
            keys.insert(normalizedKey(file));
    }
    return static_cast<int>(keys.size());
}

QList<ModLibrary::Conflict> ModLibrary::conflicts() const {
    QHash<QString, Conflict> byKey;
    QStringList keys;   // First seen order.
    for (const Mod& mod : m_mods) {
        if (!mod.enabled)
            continue;
        for (const QString& file : modFiles(mod.path)) {
            const QString key = normalizedKey(file);
            auto it = byKey.find(key);
            if (it == byKey.end()) {
                it = byKey.insert(key, Conflict{QString(file).replace(u'\\', u'/'), {}});
                keys.append(key);
            }
            it->ids.append(mod.id);
        }
    }

    QList<Conflict> result;
    for (const QString& key : std::as_const(keys))
        if (byKey.value(key).ids.size() > 1)
            result.append(byKey.value(key));
    std::sort(result.begin(), result.end(), [](const Conflict& a, const Conflict& b) {
        return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------------------------------------------

bool ModLibrary::isArchive(const QString& path) {
    static const QStringList extensions = {u"zip"_s, u"7z"_s, u"rar"_s, u"tar"_s, u"gz"_s, u"tgz"_s, u"bz2"_s, u"xz"_s};
    return QFileInfo(path).isFile() && extensions.contains(QFileInfo(path).suffix().toLower());
}

bool ModLibrary::extractArchive(const QString& archive, const QString& destination, QString* error) {
    const auto failWith = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!QDir().mkpath(destination))
        return failWith(u"Could not create %1."_s.arg(QDir::toNativeSeparators(destination)));

    QString program = QStandardPaths::findExecutable(u"7z"_s);
    for (const QString& candidate : {u"C:/Program Files/7-Zip/7z.exe"_s, u"C:/Program Files (x86)/7-Zip/7z.exe"_s})
        if (program.isEmpty() && QFileInfo::exists(candidate))
            program = candidate;

    QStringList arguments;
    if (!program.isEmpty()) {
        arguments = {u"x"_s, u"-y"_s, u"-o"_s + QDir::toNativeSeparators(destination), QDir::toNativeSeparators(archive)};
    } else {
        // Windows 10 / 11 ship bsdtar, which reads zip (and on newer builds 7z / rar) archives.
        program = QStandardPaths::findExecutable(u"tar"_s);
        if (program.isEmpty())
            return failWith(u"Can't unpack %1: install 7-Zip, or unpack it yourself and install the folder."_s
                                .arg(QFileInfo(archive).fileName()));
        arguments = {u"-xf"_s, QDir::toNativeSeparators(archive), u"-C"_s, QDir::toNativeSeparators(destination)};
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(15000))
        return failWith(u"Could not start %1."_s.arg(QDir::toNativeSeparators(program)));
    if (!process.waitForFinished(10 * 60 * 1000)) {
        process.kill();
        return failWith(u"Unpacking %1 took too long."_s.arg(QFileInfo(archive).fileName()));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString output = QString::fromLocal8Bit(process.readAll()).trimmed().right(400);
        return failWith(u"Could not unpack %1 (%2). %3"_s.arg(QFileInfo(archive).fileName(),
                                                              QFileInfo(program).fileName(), output));
    }
    return true;
}

QString ModLibrary::findRoot(const QString& folder, const QSet<QString>& mappedPaths) const {
    // Every folder (breadth first, so the shallowest wins a tie) scored by how many of its files are mapped paths.
    QStringList queue{folder};
    QHash<QString, int> depth{{folder, 0}};
    QString best;
    int bestMapped = 0, bestKnownTop = 0;

    while (!queue.isEmpty()) {
        const QString current = queue.takeFirst();
        const int d = depth.value(current);

        int mapped = 0;
        int knownTop = 0;
        for (const QString& file : modFiles(current)) {
            if (mappedPaths.contains(normalizedKey(file)))
                ++mapped;
            const qsizetype slash = file.indexOf(u'/');
            if (slash > 0 && isKnownTopFolder(file.left(slash)))
                ++knownTop;
        }
        if (mapped > bestMapped || (bestMapped == 0 && knownTop > bestKnownTop)) {
            best = current;
            bestMapped = mapped;
            bestKnownTop = knownTop;
        }

        if (d >= kMaxRootDepth)
            continue;
        for (const QString& child : QDir(current).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (child == u"__MACOSX"_s || child.startsWith(u'.'))
                continue;
            const QString path = QDir(current).filePath(child);
            depth.insert(path, d + 1);
            queue.append(path);
        }
    }
    if (!best.isEmpty())
        return best;

    // Nothing recognisable: skip wrapper folders (a single folder and nothing else).
    QString current = folder;
    for (int d = 0; d < kMaxRootDepth; ++d) {
        const QStringList dirs = QDir(current).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const QStringList files = QDir(current).entryList(QDir::Files);
        if (dirs.size() != 1 || !files.isEmpty())
            break;
        current = QDir(current).filePath(dirs.first());
    }
    return current;
}

QString ModLibrary::uniqueId(const QString& name) const {
    QString id;
    for (const QChar ch : name.trimmed()) {
        if (ch.isLetterOrNumber() || QStringLiteral(" _-.()[]").contains(ch))
            id += ch;
        else
            id += u'_';
    }
    id = id.trimmed().left(80);
    while (id.endsWith(u'.'))
        id.chop(1);
    if (id.isEmpty() || id.startsWith(u'@') || id.startsWith(u'.'))
        id = u"mod"_s + (id.isEmpty() ? QString() : u"_"_s + id);
    return id;
}

std::optional<ModLibrary::Mod> ModLibrary::install(const QString& sourceIn, const QSet<QString>& mappedPaths,
                                                    QString* error, const std::function<void(const QString&)>& log) {
    const auto failWith = [error](const QString& message) -> std::optional<Mod> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const auto say = [&log](const QString& message) {
        if (log)
            log(message);
    };

    const QFileInfo source(sourceIn);
    if (!source.exists())
        return failWith(u"%1 doesn't exist."_s.arg(QDir::toNativeSeparators(sourceIn)));
    if (!QDir().mkpath(m_libraryDir))
        return failWith(u"Could not create %1."_s.arg(QDir::toNativeSeparators(m_libraryDir)));

    // Archives are unpacked into a temporary folder inside the library (same drive: the files are moved, not copied).
    const QString temp = QDir(m_libraryDir).filePath(u".installing-%1"_s.arg(QRandomGenerator::global()->generate(), 8, 16, QChar(u'0')));
    struct Cleanup {
        QString path;
        ~Cleanup() { QDir(path).removeRecursively(); }
    } cleanup{temp};

    QString unpacked;
    if (source.isDir()) {
        unpacked = source.absoluteFilePath();
    } else if (isArchive(sourceIn)) {
        say(u"Unpacking %1..."_s.arg(source.fileName()));
        unpacked = QDir(temp).filePath(u"x"_s);
        QString reason;
        if (!extractArchive(source.absoluteFilePath(), unpacked, &reason))
            return failWith(reason);
    } else {
        return failWith(u"%1 isn't a folder or an archive (.zip, .7z, .rar)."_s.arg(source.fileName()));
    }

    const QString root = findRoot(unpacked, mappedPaths);
    const int files = static_cast<int>(modFiles(root).size());
    if (files == 0)
        return failWith(u"%1 has no files Modify can use (supported: .%2)."_s.arg(source.fileName(), m_extensions.join(u", ."_s)));

    // Name: the mod's own blobsetmod.json (in its root or the top of the archive), else the archive / folder name.
    QJsonObject info = readJson(QDir(root).filePath(kModInfoFile));
    if (info.isEmpty())
        info = readJson(QDir(unpacked).filePath(kModInfoFile));
    QString name = info.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        name = source.isDir() ? source.fileName() : source.completeBaseName();
    if (name.endsWith(u".tar"_s, Qt::CaseInsensitive))
        name.chop(4);

    const QString id = uniqueId(name);
    const QString target = QDir(m_libraryDir).filePath(id);

    // Same mod again (an update): it takes the old one's place in the load order.
    int position = -1;
    bool enabled = true;
    for (int i = 0; i < m_mods.size(); ++i) {
        if (m_mods[i].id == id) {
            position = i;
            enabled = m_mods[i].enabled;
            m_mods.removeAt(i);
            break;
        }
    }
    if (QFileInfo(target).exists() && !removeTree(target))
        return failWith(u"Could not replace the installed %1 (is a file in it open?)."_s.arg(name));

    QString reason;
    const bool moved = !source.isDir() && QDir().rename(root, target);
    if (!moved && !copyTree(root, target, &reason)) {
        QDir(target).removeRecursively();
        return failWith(reason);
    }

    // A picture next to the mod's root in the archive (e.g. Mod\preview.png beside Mod\data\...) comes along.
    if (findImage(target).isEmpty() && QDir::cleanPath(root) != QDir::cleanPath(unpacked)) {
        QString picture;
        for (QString dir = QFileInfo(root).absolutePath(); picture.isEmpty(); dir = QFileInfo(dir).absolutePath()) {
            picture = findImage(dir);
            if (QDir::cleanPath(dir) == QDir::cleanPath(unpacked) || QDir(dir).isRoot())
                break;
        }
        if (!picture.isEmpty()) {
            const QString copied = QDir(target).filePath(u"preview."_s + QFileInfo(picture).suffix().toLower());
            if (QFile::copy(picture, copied))
                info[u"image"_s] = QFileInfo(copied).fileName();
        }
    }

    info[u"name"_s] = name;
    info[u"installed"_s] = QDateTime::currentDateTime().toString(Qt::ISODate);
    info[u"source"_s] = source.fileName();
    if (!writeJson(QDir(target).filePath(kModInfoFile), info, &reason))
        say(u"Warning: "_s + reason);

    Mod mod = readMod(id, target);
    mod.enabled = enabled;
    if (position >= 0)
        m_mods.insert(position, mod);
    else
        m_mods.append(mod);

    if (QDir::cleanPath(root) != QDir::cleanPath(unpacked))
        say(u"Mod root: %1"_s.arg(QDir(unpacked).relativeFilePath(root)));
    say(u"Installed %1 (%2 file(s))%3."_s.arg(name).arg(mod.fileCount).arg(position >= 0 ? u", replacing the older copy"_s : QString()));

    if (!save(&reason))
        return failWith(reason);
    return mod;
}

bool ModLibrary::uninstall(const QString& id, QString* error) {
    for (int i = 0; i < m_mods.size(); ++i) {
        if (m_mods[i].id != id)
            continue;
        if (m_mods[i].builtIn) {
            if (error)
                *error = u"%1 can't be removed."_s.arg(m_mods[i].name);
            return false;
        }
        if (!removeTree(m_mods[i].path)) {
            if (error)
                *error = u"Could not remove %1 (is a file in it open?)."_s.arg(QDir::toNativeSeparators(m_mods[i].path));
            return false;
        }
        m_mods.removeAt(i);
        return save(error);
    }
    if (error)
        *error = u"There's no mod called %1."_s.arg(id);
    return false;
}
