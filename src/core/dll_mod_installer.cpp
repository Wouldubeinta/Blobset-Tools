#include "dll_mod_installer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr auto kManifest = "blobsettools_installed.txt";

// Relative paths ('/' separated) of the files under `dir`, sorted (case insensitive).
QStringList filesUnder(const QString& dir) {
    QStringList files;
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return files;
    const QDir root(dir);
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(root.relativeFilePath(it.next()));
    files.sort(Qt::CaseInsensitive);
    return files;
}

QByteArray fileHash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return hash.result();
}

bool sameFile(const QString& a, const QString& b) {
    const QFileInfo fa(a), fb(b);
    return fa.exists() && fb.exists() && fa.size() == fb.size() && fileHash(a) == fileHash(b);
}

bool loads(const QString& relativePath) {
    if (relativePath.contains(u'/'))
        return false;   // The loader only loads the files directly in DLL_Mods.
    const QString lower = relativePath.toLower();
    return (lower.endsWith(u".dll"_s) || lower.endsWith(u".asi"_s)) && lower != u"dinput8.dll"_s;
}

} // namespace

QString DllModInstaller::libraryDir(const QString& platformDir, bool create) {
    if (platformDir.isEmpty())
        return {};
    const QString dir = QDir(platformDir).filePath(u"mods/"_s + QLatin1String(kLibraryFolder));
    if (create)
        QDir().mkpath(dir);
    return dir;
}

QString DllModInstaller::gameDir(const QString& blobsetFile) {
    if (blobsetFile.isEmpty())
        return {};
    QDir game = QFileInfo(blobsetFile).absoluteDir();   // <game>\data
    if (game.dirName().compare(u"data"_s, Qt::CaseInsensitive) != 0 || !game.cdUp())
        return {};
    return game.filePath(QLatin1String(kGameFolder));
}

bool DllModInstaller::isLibraryPath(const QString& relativePath) {
    QString path = QString(relativePath).replace(u'\\', u'/');
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    return path.section(u'/', 0, 0).compare(QLatin1String(kLibraryFolder), Qt::CaseInsensitive) == 0
           && path.contains(u'/');
}

QVariantList DllModInstaller::list(const QString& libraryDir, const QString& gameDir) {
    QVariantList out;
    const QDir library(libraryDir);
    const QDir game(gameDir);
    for (const QString& name : filesUnder(libraryDir)) {
        const QString source = library.filePath(name);
        out.append(QVariantMap{
            {u"name"_s, name},
            {u"size"_s, QLocale().formattedDataSize(QFileInfo(source).size())},
            {u"loads"_s, loads(name)},
            {u"installed"_s, !gameDir.isEmpty() && sameFile(source, game.filePath(name))},
        });
    }
    return out;
}

QStringList DllModInstaller::sync(const QString& libraryDir, const QString& blobsetFile, bool* ok) {
    QStringList log;
    bool good = true;
    const QString target = gameDir(blobsetFile);
    if (target.isEmpty()) {
        if (ok)
            *ok = false;
        return {u"DLL mods: the game's folder isn't known (set the game location)."_s};
    }
    const QDir game(target);
    const QDir library(libraryDir);
    const QStringList wanted = filesUnder(libraryDir);

    // What was installed before (only those are ever removed).
    QStringList previous;
    {
        QFile manifest(game.filePath(QLatin1String(kManifest)));
        if (manifest.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const QByteArray& line : manifest.readAll().split('\n'))
                if (const QString name = QString::fromUtf8(line).trimmed(); !name.isEmpty())
                    previous.append(name);
    }
    if (wanted.isEmpty() && previous.isEmpty()) {
        if (ok)
            *ok = true;
        return log;   // Nothing to do.
    }

    // Take out what was removed from the library.
    QSet<QString> keep;
    for (const QString& name : wanted)
        keep.insert(name.toLower());
    int removed = 0;
    for (const QString& name : std::as_const(previous)) {
        if (keep.contains(name.toLower()))
            continue;
        const QString path = game.filePath(name);
        if (QFile::exists(path) && !QFile::remove(path)) {
            log << u"DLL mods: could not remove %1 (is the game running?)"_s.arg(QDir::toNativeSeparators(path));
            good = false;
        } else {
            ++removed;
        }
    }

    // Copy what's new or changed.
    int copied = 0, unchanged = 0;
    if (!wanted.isEmpty() && !QDir().mkpath(target)) {
        if (ok)
            *ok = false;
        return {u"DLL mods: could not create %1"_s.arg(QDir::toNativeSeparators(target))};
    }
    for (const QString& name : wanted) {
        const QString source = library.filePath(name);
        const QString destination = game.filePath(name);
        if (sameFile(source, destination)) {
            ++unchanged;
            continue;
        }
        QDir().mkpath(QFileInfo(destination).absolutePath());
        if ((QFile::exists(destination) && !QFile::remove(destination)) || !QFile::copy(source, destination)) {
            log << u"DLL mods: could not copy %1 to %2 (is the game running?)"_s.arg(name, QDir::toNativeSeparators(target));
            good = false;
            continue;
        }
        ++copied;
    }

    // The manifest (or no folder at all when nothing is left in it).
    if (wanted.isEmpty()) {
        QFile::remove(game.filePath(QLatin1String(kManifest)));
        QDir().rmdir(target);   // Only when empty.
    } else {
        QSaveFile manifest(game.filePath(QLatin1String(kManifest)));
        const QByteArray text = wanted.join(u"\r\n"_s).toUtf8() + "\r\n";
        if (!manifest.open(QIODevice::WriteOnly) || manifest.write(text) != text.size() || !manifest.commit()) {
            log << u"DLL mods: could not write %1"_s.arg(QLatin1String(kManifest));
            good = false;
        }
    }

    // The loader that loads them: <Blobset Tools>\modloader\dinput8.dll next to the game's exe.
    if (!wanted.isEmpty()) {
        QDir root(target);
        root.cdUp();
        const QString loaderSource = QDir(QCoreApplication::applicationDirPath()).filePath(u"modloader/dinput8.dll"_s);
        const QString loaderTarget = root.filePath(u"dinput8.dll"_s);
        if (QFile::exists(loaderSource)) {
            if (!sameFile(loaderSource, loaderTarget)) {
                QFile::remove(loaderTarget);
                if (QFile::copy(loaderSource, loaderTarget))
                    log << u"Installed the Blobset Mod Loader: %1"_s.arg(QDir::toNativeSeparators(loaderTarget));
                else
                    log << u"Warning: could not copy dinput8.dll to %1"_s.arg(QDir::toNativeSeparators(loaderTarget));
            }
        } else if (!QFile::exists(loaderTarget)) {
            log << u"Warning: the Blobset Mod Loader (dinput8.dll) isn't installed, so the game won't load the DLL "
                   u"mods. Build BlobsetModLoader and put dinput8.dll in %1."_s
                       .arg(QDir::toNativeSeparators(QFileInfo(loaderSource).absolutePath()));
        }
    }

    log.prepend(u"DLL mods: %1 file(s) in %2 - %3 copied, %4 unchanged, %5 removed."_s
                    .arg(wanted.size()).arg(QDir::toNativeSeparators(target)).arg(copied).arg(unchanged).arg(removed));
    if (ok)
        *ok = good;
    return log;
}
