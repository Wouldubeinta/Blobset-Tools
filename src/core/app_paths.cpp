#include "app_paths.h"
#include "file_mapping_worker.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace AppPaths {

namespace {

const QString kPortableMarker = u"portable.txt"_s;
const QString kDataFolderName = u"Blobset Tools"_s;

/// games\<game>\<platform>\data-0.blobset.pc (Extract's output): too big to carry over, and Extract makes it again.
bool isExtractedBlobset(const QString& relativeToGames) {
    const QStringList parts = relativeToGames.split(u'/');
    if (parts.size() < 4)
        return false;
    const QString name = parts[2].toLower();
    return name.startsWith(u"data-"_s) && (name.contains(u".blobset."_s) || name.contains(u".dat."_s));
}

/// True when `newer` is a later version of `older`: written later (by more than the few seconds file systems round
/// to) and different.
bool isNewerCopy(const QString& newer, const QString& older) {
    const QFileInfo n(newer), o(older);
    if (!n.isFile() || !o.isFile() || n.lastModified() <= o.lastModified().addSecs(2))
        return false;
    if (n.size() != o.size())
        return true;
    QFile a(newer), b(older);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly))
        return false;
    while (!a.atEnd()) {
        if (a.read(1 << 20) != b.read(1 << 20))
            return true;
    }
    return false;
}

/// Copies the files of `from` into `to` that `to` doesn't have yet. Returns how many were copied.
/// `updated` (optional): file mappings (*.mapping) `to` has an older copy of are updated too (the newer mapping with
/// the user's own names kept, FileMappingWorker::mergeMappings), and counted there.
/// games\<game>\... of a game that isn't in `games` (empty `games`: every game is wanted).
bool isUnwantedGame(const QString& relative, const QSet<QString>& games) {
    if (games.isEmpty() || !relative.startsWith(u"games/"_s, Qt::CaseInsensitive))
        return false;
    const QString game = relative.section(u'/', 1, 1);
    return !game.isEmpty() && !games.contains(game.toLower());
}

int copyMissing(const QString& from, const QString& to, bool skipExtracted, QStringList* failures,
                const QSet<QString>& games = {}, int* updated = nullptr) {
    int copied = 0;
    const QDir source(from);
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString relative = source.relativeFilePath(file);
        if ((skipExtracted && isExtractedBlobset(relative)) || isUnwantedGame(relative, games))
            continue;
        const QString target = QDir(to).filePath(relative);
        if (QFileInfo::exists(target)) {
            // A newer file mapping shipped with Blobset Tools (made for a newer game build) is merged in: its entries,
            // with the names the user gave (File Mapping Editor, Resolve File Names) kept where it only has a made up
            // one, and the user's extra entries (their mods' files). One the user made since is newer, so it stays.
            if (updated && relative.endsWith(u".mapping"_s, Qt::CaseInsensitive) && isNewerCopy(file, target)) {
                try {
                    const auto merged = FileMappingWorker::mergeMappings(FileMappingWorker::readMappingFile(file),
                                                                         FileMappingWorker::readMappingFile(target));
                    FileMappingWorker::writeMappingFile(target, merged);   // Atomic; dated now, so it's done once.
                    ++*updated;
                } catch (const std::exception&) {
                    if (failures)
                        failures->append(QDir::toNativeSeparators(relative));
                }
            }
            continue;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (QFile::copy(file, target))
            ++copied;
        else if (failures)
            failures->append(QDir::toNativeSeparators(relative));
    }
    // Empty folders (mods\<category>, ...) matter too: the workspace layout is made of them.
    QDirIterator dirs(from, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirs.hasNext()) {
        const QString relative = source.relativeFilePath(dirs.next());
        if (!(skipExtracted && isExtractedBlobset(relative + u"/x"_s)) && !isUnwantedGame(relative, games))
            QDir().mkpath(QDir(to).filePath(relative));
    }
    return copied;
}

} // namespace

QString installDir() {
    return QCoreApplication::applicationDirPath();
}

bool isPortable() {
    return QFileInfo::exists(QDir(installDir()).filePath(kPortableMarker));
}

QString dataDir() {
    static const QString dir = []() {
        if (isPortable())
            return installDir();
        QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (documents.isEmpty())
            documents = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return QDir::cleanPath(QDir(documents).filePath(kDataFolderName));
    }();
    return dir;
}

QString gameSelectionInfoFile() {
    return QDir(installDir()).filePath(u"GameSelectionInfo.xml"_s);
}

QStringList selectedGameFolders() {
    // <ModFolder> names games\<game> (and the Vortex extensions' blobset_game.json); <FolderName> is the game's own
    // install folder, which can differ (Rugby League 26: ModFolder "Rugby League 26", FolderName "Rugby League").
    QStringList folders;
    QFile file(gameSelectionInfoFile());
    if (!file.open(QIODevice::ReadOnly))
        return folders;
    QXmlStreamReader xml(&file);
    QString modFolder, folderName;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == u"Game") {
                modFolder.clear();
                folderName.clear();
            } else if (xml.name() == u"ModFolder") {
                modFolder = xml.readElementText().trimmed();
            } else if (xml.name() == u"FolderName") {
                folderName = xml.readElementText().trimmed();
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == u"Game") {
            const QString folder = modFolder.isEmpty() ? folderName : modFolder;
            if (!folder.isEmpty())
                folders.append(folder);
        }
    }
    return folders;
}

QStringList prepare() {
    QStringList log;
    const QString data = dataDir();
    if (!QDir().mkpath(data)) {
        log.append(u"Error: could not create the data folder %1."_s.arg(QDir::toNativeSeparators(data)));
        return log;
    }
    if (isPortable())
        return log;   // Everything stays next to the exe.

    const QDir install(installDir());
    const bool sameFolder = QDir::cleanPath(install.absolutePath()).compare(QDir::cleanPath(data), Qt::CaseInsensitive) == 0;
    QStringList failures;

    // The old layout kept everything next to the exe: carry it over once (backups, mappings, mods, settings).
    const QString oldGames = install.filePath(u"games"_s);
    const QString newGames = QDir(data).filePath(u"games"_s);
    if (!sameFolder && QFileInfo(oldGames).isDir() && !QFileInfo::exists(newGames)) {
        const int copied = copyMissing(oldGames, newGames, true, &failures);
        log.append(u"Moved the game profiles, mappings, mods and backups (%1 file(s)) from %2 to %3. "
                   u"Extracted blobsets were left behind (run Extract again if you need them)."_s
                       .arg(copied).arg(QDir::toNativeSeparators(oldGames), QDir::toNativeSeparators(newGames)));
    }
    const QString oldSettings = install.filePath(u"Settings.ini"_s);
    const QString newSettings = QDir(data).filePath(u"Settings.ini"_s);
    if (!sameFolder && QFileInfo::exists(oldSettings) && !QFileInfo::exists(newSettings) && QFile::copy(oldSettings, newSettings))
        log.append(u"Moved Settings.ini to %1."_s.arg(QDir::toNativeSeparators(data)));

    // The installed starting files (game profiles of new games after an update, ...); the user's never get replaced.
    const QString defaults = install.filePath(u"defaults"_s);
    if (QFileInfo(defaults).isDir()) {
        QSet<QString> games;
        for (const auto& folderList = selectedGameFolders(); const QString& folder : folderList)
            games.insert(folder.toLower());
        int updated = 0;
        const int copied = copyMissing(defaults, data, false, &failures, games, &updated);
        if (copied > 0)
            log.append(u"Added %1 default file(s) to %2."_s.arg(copied).arg(QDir::toNativeSeparators(data)));
        if (updated > 0)
            log.append(u"Updated %1 file mapping(s) in %2 from the newer one(s) installed with Blobset Tools (your own names kept)."_s
                           .arg(updated).arg(QDir::toNativeSeparators(data)));
    }

    if (!failures.isEmpty())
        log.append(u"Warning: %1 file(s) couldn't be copied into the data folder, e.g. %2."_s
                       .arg(failures.size()).arg(failures.first()));
    return log;
}

} // namespace AppPaths
