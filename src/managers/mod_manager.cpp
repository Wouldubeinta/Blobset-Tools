#include "mod_manager.h"

#include "app_orchestrator.h"
#include "app_paths.h"
#include "mod_library.h"

#include <QDesktopServices>
#include <QFile>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QPointer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;

namespace {

QString toLocal(const QString& pathOrUrl) {
    const QString trimmed = pathOrUrl.trimmed();
    if (trimmed.startsWith(u"file:"_s, Qt::CaseInsensitive))
        return QUrl(trimmed).toLocalFile();
    return trimmed;
}

} // namespace

ModManager::ModManager(AppOrchestrator* orchestrator, QObject* parent) : QObject(parent), m_orchestrator(orchestrator) {}

void ModManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ModManager::report(bool ok, const QString& text) {
    emit message(ok, text);
    if (m_orchestrator)
        emit m_orchestrator->uiConsoleUpdate(u"Mod Manager: "_s + text);
}

void ModManager::reload() {
    m_mods.clear();
    m_conflictCount = 0;
    m_enabledFileCount = 0;
    m_libraryPath.clear();

    if (!m_orchestrator || m_orchestrator->platformDirPath().isEmpty()) {
        emit modsChanged();
        return;
    }

    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    m_libraryPath = library.libraryDir();

    // Files each mod takes from / gives up to another enabled mod (the later one in the list wins).
    QHash<QString, int> wins, loses;
    const QList<ModLibrary::Conflict> conflicts = library.conflicts();
    m_conflictCount = static_cast<int>(conflicts.size());
    for (const ModLibrary::Conflict& conflict : conflicts) {
        wins[conflict.ids.last()] += 1;
        for (qsizetype i = 0; i + 1 < conflict.ids.size(); ++i)
            loses[conflict.ids[i]] += 1;
    }
    m_enabledFileCount = library.enabledFileCount();

    for (const ModLibrary::Mod& mod : library.mods()) {
        m_mods.append(QVariantMap{
            {u"id"_s, mod.id},
            {u"name"_s, mod.name},
            {u"author"_s, mod.author},
            {u"version"_s, mod.version},
            {u"description"_s, mod.description},
            {u"url"_s, mod.url},
            {u"path"_s, QDir::toNativeSeparators(mod.path)},
            {u"enabled"_s, mod.enabled},
            {u"builtIn"_s, mod.builtIn},
            {u"fileCount"_s, mod.fileCount},
            {u"installed"_s, mod.installed.isValid() ? QLocale::system().toString(mod.installed, QLocale::ShortFormat) : QString()},
            {u"wins"_s, wins.value(mod.id)},
            {u"loses"_s, loses.value(mod.id)},
        });
    }
    emit modsChanged();
}

void ModManager::install(const QString& pathOrUrl) {
    if (m_busy || !m_orchestrator)
        return;
    const QString source = toLocal(pathOrUrl);
    if (m_orchestrator->platformDirPath().isEmpty()) {
        report(false, u"Load a game first."_s);
        return;
    }

    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    const QSet<QString> mapped = m_orchestrator->mappedPaths();

    setBusy(true);
    report(true, u"Installing %1..."_s.arg(QFileInfo(source).fileName()));

    QPointer<ModManager> self(this);
    QPointer<AppOrchestrator> orchestrator(m_orchestrator);
    (void)QtConcurrent::run([self, orchestrator, library, mapped, source]() mutable {
        QString error;
        QStringList log;
        const std::optional<ModLibrary::Mod> mod =
            library.install(source, mapped, &error, [&log](const QString& line) { log.append(line); });

        QMetaObject::invokeMethod(qApp, [self, mod, error, log]() {
            if (!self)
                return;
            for (const QString& line : log)
                if (self->m_orchestrator)
                    emit self->m_orchestrator->uiConsoleUpdate(u"Mod Manager: "_s + line);
            self->setBusy(false);
            self->reload();
            if (mod)
                emit self->message(true, u"Installed %1 (%2 file(s)). Press Build to put it into the game."_s
                                             .arg(mod->name).arg(mod->fileCount));
            else
                self->report(false, error);
        });
    });
}

void ModManager::uninstall(const QString& id) {
    if (!m_orchestrator)
        return;
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    const ModLibrary::Mod* mod = library.find(id);
    const QString name = mod ? mod->name : id;
    QString error;
    if (library.uninstall(id, &error))
        report(true, u"Removed %1 (moved to the Recycle Bin). Press Build to take it out of the game."_s.arg(name));
    else
        report(false, error);
    reload();
}

void ModManager::setEnabled(const QString& id, bool enabled) {
    if (!m_orchestrator)
        return;
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    QString error;
    if (!library.setEnabled(id, enabled) || !library.save(&error))
        report(false, error.isEmpty() ? u"There's no mod called %1."_s.arg(id) : error);
    reload();
}

void ModManager::move(int from, int to) {
    if (!m_orchestrator || from == to)
        return;
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    QString error;
    if (!library.move(from, to) || !library.save(&error)) {
        if (!error.isEmpty())
            report(false, error);
    }
    reload();
}

void ModManager::saveInfo(const QString& id, const QString& name, const QString& author, const QString& version,
                          const QString& description) {
    if (!m_orchestrator)
        return;
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    QString error;
    if (library.setInfo(id, name, author, version, description, &error))
        report(true, u"Saved the details of %1."_s.arg(name.trimmed().isEmpty() ? id : name.trimmed()));
    else
        report(false, error);
    reload();
}

QVariantList ModManager::conflictsFor(const QString& id) const {
    QVariantList result;
    if (!m_orchestrator)
        return result;
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();

    const auto nameOf = [&library](const QString& modId) {
        const ModLibrary::Mod* mod = library.find(modId);
        return mod ? mod->name : modId;
    };
    for (const ModLibrary::Conflict& conflict : library.conflicts()) {
        if (!conflict.ids.contains(id))
            continue;
        QStringList others;
        for (const QString& other : conflict.ids)
            if (other != id)
                others.append(nameOf(other));
        result.append(QVariantMap{{u"path"_s, conflict.path},
                                  {u"winner"_s, nameOf(conflict.ids.last())},
                                  {u"wins"_s, conflict.ids.last() == id},
                                  {u"others"_s, others.join(u", "_s)}});
    }
    return result;
}

QStringList ModManager::filesOf(const QString& id, int max) const {
    if (!m_orchestrator)
        return {};
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    const ModLibrary::Mod* mod = library.find(id);
    if (!mod)
        return {};
    QStringList files = library.modFiles(mod->path);
    files.sort(Qt::CaseInsensitive);
    if (max > 0 && files.size() > max)
        files = files.mid(0, max);
    return files;
}

void ModManager::openFolder(const QString& id) const {
    if (!m_orchestrator)
        return;
    ModLibrary library = m_orchestrator->modLibrary();
    library.load();
    if (const ModLibrary::Mod* mod = library.find(id)) {
        QDir().mkpath(mod->path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(mod->path));
    }
}

void ModManager::openLibraryFolder() const {
    if (m_libraryPath.isEmpty())
        return;
    QDir().mkpath(m_libraryPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_libraryPath));
}

void ModManager::build() {
    if (m_orchestrator)
        m_orchestrator->modifyBlobset();
}

void ModManager::installVortexExtensions() {
    // The command line does the copying (it's what a script would run too); its last line says what happened.
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QCoreApplication::applicationFilePath(), {u"--install-vortex-extensions"_s});
    if (!process.waitForFinished(60000)) {
        process.kill();
        report(false, u"Installing the Vortex extensions took too long."_s);
        return;
    }
    QString output = QString::fromUtf8(process.readAll()).trimmed();
    if (output.isEmpty()) {
        QFile log(QDir(AppPaths::dataDir()).filePath(u"cli.log"_s));
        if (log.open(QIODevice::ReadOnly | QIODevice::Text))
            output = QString::fromUtf8(log.readAll()).trimmed();
    }
    const QString last = output.section(u'\n', -1).trimmed();
    report(process.exitCode() == 0, last.isEmpty() ? u"Installing the Vortex extensions failed."_s : last);
}
