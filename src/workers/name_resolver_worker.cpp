#include "name_resolver_worker.h"

#include "blobset_asset_reader.h"
#include "blobset_file.h"
#include "core_defs.h"
#include "file_mapping_worker.h"
#include "hash_utility.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <exception>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kMinStringLength = 4;
constexpr qsizetype kMaxStringLength = 240;
constexpr qint64 kMaxExecutableBytes = 256LL * 1024 * 1024;
constexpr quint32 kMaxAssetMainBytes = 1024 * 1024;   // Larger assets are geometry / pixels, not names.
constexpr int kMaxHarvestedAssets = 40000;

/// Which mapped file kinds a type can be. A guessed type is only accepted when the file's current extension is listed;
/// a type spelt out in a string ("x.mempack") is also accepted for types that aren't in this table.
const QHash<QString, QStringList>& compatibleExtensions() {
    static const QHash<QString, QStringList> table = {
        {u"texture"_s, {u"dds"_s, u"txpk"_s}},
        {u"texpack"_s, {u"txpk"_s}},
        {u"model"_s, {u"mesh"_s, u"mdl"_s, u"mdat"_s, u"idx"_s}},
        {u"mempack"_s, {u"m3mp"_s}},
        {u"material"_s, {u"mat"_s}},
        {u"anim"_s, {u"anim"_s, u"anims"_s}},
        {u"animset"_s, {u"animset"_s}},
        {u"bank"_s, {u"bank"_s, u"bnk"_s}},
        {u"soundbank"_s, {u"bank"_s, u"bnk"_s}},
        {u"wem"_s, {u"wem"_s}},
        {u"bnk"_s, {u"bnk"_s}},
        {u"fsb"_s, {u"fsb"_s}},
        {u"wav"_s, {u"wav"_s}},
    };
    return table;
}

/// Mapped kinds whose main section is worth scanning for names (materials name their textures, meshes their
/// materials and model folders, scripts their data files, ...).
bool worthScanning(const QString& extension) {
    static const QStringList kinds = {u"mat"_s, u"mesh"_s, u"mdl"_s, u"mdat"_s, u"luadata"_s, u"str"_s,
                                      u"names"_s, u"txt"_s, u"xml"_s, u"animset"_s, u"obj"_s, u"dat"_s,
                                      u"anims"_s, u"mob"_s, u"col"_s, u"glyphs"_s};
    return kinds.contains(extension);
}

QString extensionOf(const QString& mappedPath) {
    const qsizetype slash = mappedPath.lastIndexOf(u'\\');
    const qsizetype dot = mappedPath.lastIndexOf(u'.');
    return dot > slash ? mappedPath.mid(dot + 1).toLower() : QString();
}

/// Numbered names the file mapping invents for files it can't name ("model\static\123.mesh", "unknown\...").
bool isUnnamed(const QString& mappedPath) {
    if (mappedPath.startsWith(u"unknown\\"_s, Qt::CaseInsensitive))
        return true;
    QString name = mappedPath.mid(mappedPath.lastIndexOf(u'\\') + 1);
    name = name.left(name.indexOf(u'.'));
    if (name.isEmpty())
        return false;
    for (const QChar c : std::as_const(name))
        if (!c.isDigit())
            return false;
    return true;
}

bool isTypeWord(const QString& word) {
    if (word.size() < 2 || word.size() > 12)
        return false;
    for (const QChar c : word)
        if (c < u'a' || c > u'z')
            return false;
    return true;
}

} // namespace

NameResolverWorker::NameResolverWorker(const QString& blobsetFile, int version, bool isBigEndian,
                                       const QString& mappingFile, const QString& userNames, const QString& gameKey,
                                       int gameId, int platform, QObject* parent)
    : QObject(parent),
      m_blobsetFile(blobsetFile),
      m_version(version),
      m_isBigEndian(isBigEndian),
      m_mappingFile(mappingFile),
      m_userNames(userNames),
      m_gameKey(gameKey),
      m_gameId(gameId),
      m_platform(platform) {}

void NameResolverWorker::reportProgress(int percent, const QString& status) {
    percent = std::clamp(percent, 0, 100);
    if (percent == m_lastPercent)
        return;
    m_lastPercent = percent;
    emit progressUpdated(percent);
    if (!status.isEmpty())
        emit updateStatus(status);
}

void NameResolverWorker::reportRange(int from, int to, qint64 done, qint64 total, const QString& status) {
    const qint64 span = std::max<qint64>(1, total);
    reportProgress(from + int((to - from) * std::min(done, span) / span), status);
}

void NameResolverWorker::resolve_names() {
    m_lastPercent = -1;
    try {
        run();
    } catch (const std::exception& ex) {
        emit taskFinished(u"Resolving file names failed.\n\n"_s + QString::fromUtf8(ex.what()));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Candidate strings
// ---------------------------------------------------------------------------------------------------------------

void NameResolverWorker::addString(QString s) {
    s = s.trimmed().toLower();
    s.replace(u'\\', u'/');
    if (s.size() < kMinStringLength || s.size() > kMaxStringLength || s.contains(u'%') || s.contains(u':'))
        return;

    // Build paths some tools leave in front of the real name.
    for (const QString& prefix : {u"convant2-temp-intermediate/"_s, u"ant2-temp-intermediate/"_s, u"data/"_s}) {
        if (s.startsWith(prefix))
            s.remove(0, prefix.size());
    }
    while (s.startsWith(u"./"_s))
        s.remove(0, 2);
    while (s.startsWith(u'/'))
        s.remove(0, 1);
    while (s.endsWith(u'/') || s.endsWith(u'.'))
        s.chop(1);

    if (s.size() >= 2)
        m_strings.insert(s);
}

void NameResolverWorker::addStrings(const QByteArray& data) {
    const auto isNameChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'
               || c == '.' || c == '/' || c == '\\';
    };

    qsizetype start = -1;
    for (qsizetype i = 0; i <= data.size(); ++i) {
        const bool inName = i < data.size() && isNameChar(data[i]);
        if (inName && start < 0) {
            start = i;
        } else if (!inName && start >= 0) {
            const qsizetype length = i - start;
            if (length >= kMinStringLength && length <= kMaxStringLength)
                addString(QString::fromLatin1(data.constData() + start, length));
            start = -1;
        }
    }
}

void NameResolverWorker::harvestExecutables() {
    // The game folder (or the folders above it: RL26 / Cricket keep the blobset in a "data" sub folder).
    QDir dir = QFileInfo(m_blobsetFile).absoluteDir();
    QFileInfoList executables;
    for (int level = 0; level < 3; ++level) {
        const QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo& file : files) {
            const QString suffix = file.suffix().toLower();
            const bool executable = suffix == u"exe"_s || suffix == u"xex"_s || suffix == u"elf"_s
                                    || suffix == u"self"_s || file.fileName().compare(u"EBOOT.BIN"_s, Qt::CaseInsensitive) == 0;
            if (executable && file.size() <= kMaxExecutableBytes)
                executables.append(file);
        }
        if (!dir.cdUp())
            break;
    }

    QStringList scanned;
    for (qsizetype i = 0; i < executables.size(); ++i) {
        const QFileInfo& file = executables[i];
        emit updateStatus(u"Reading names from %1..."_s.arg(file.fileName()));
        reportRange(3, 8, i, executables.size());
        {
            QFile in(file.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly))
                continue;
            const qsizetype before = m_strings.size();
            addStrings(in.readAll());
            scanned.append(u"%1 (%2 strings)"_s.arg(file.fileName()).arg(m_strings.size() - before));
        }
    }
    reportProgress(8);

    emit updateConsole(scanned.isEmpty() ? u"No game executable found next to the blobset."_s
                                         : u"Scanned executables: "_s + scanned.join(u", "_s));
}

void NameResolverWorker::harvestUserNames() {
    QFile in(m_userNames);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit updateConsole(u"Tip: add your own names (one per line) to %1"_s.arg(QDir::toNativeSeparators(m_userNames)));
        return;
    }
    int count = 0;
    while (!in.atEnd()) {
        const QString line = QString::fromUtf8(in.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        addString(line);
        ++count;
    }
    emit updateConsole(u"Read %1 name(s) from %2"_s.arg(count).arg(QFileInfo(m_userNames).fileName()));
}

void NameResolverWorker::harvestAssets(const QHash<quint64, QString>& mappedPaths, const QStringList& allPaths) {
    // The names the mapping already has (their folders often name texture packs / models).
    for (const QString& path : allPaths)
        if (!isUnnamed(path))
            addString(path);

    BlobsetFile blobset;
    {
        QFile headerFile(m_blobsetFile);
        if (!headerFile.open(QIODevice::ReadOnly)) {
            emit updateConsole(u"Could not open the blobset, its assets weren't scanned for names."_s);
            return;
        }
        QDataStream in(&headerFile);
        if (!blobset.read(in, static_cast<CoreDefs::BlobsetVersion>(m_version), m_isBigEndian, m_gameId)) {
            emit updateConsole(u"Could not read the blobset header, its assets weren't scanned for names."_s);
            return;
        }
    }

    const bool hash64 = m_version == static_cast<int>(CoreDefs::BlobsetVersion::V4);
    QList<const BlobsetEntry*> targets;
    for (const BlobsetEntry& entry : std::as_const(blobset.entries)) {
        const quint64 hash = hash64 ? entry.folderFileHash64 : quint64(entry.folderFileHash32);
        const auto mapped = mappedPaths.constFind(hash);
        if (mapped == mappedPaths.constEnd() || !worthScanning(extensionOf(mapped.value())))
            continue;
        if (entry.mainUnCompressedSize == 0 || entry.mainUnCompressedSize > kMaxAssetMainBytes)
            continue;
        targets.append(&entry);
        if (targets.size() >= kMaxHarvestedAssets)
            break;
    }

    emit updateStatus(u"Scanning %1 assets for names..."_s.arg(targets.size()));
    reportProgress(10);
    int failures = 0;
    for (qsizetype i = 0; i < targets.size(); ++i) {
        try {
            qint64 mainSize = 0;
            addStrings(BlobsetAssetReader::read(*targets[i], m_blobsetFile, m_version, m_platform, &mainSize, false));
        } catch (const std::exception&) {
            ++failures;   // A damaged asset just doesn't contribute names.
        }
        reportRange(10, 60, i + 1, targets.size(),
                    u"Scanning assets for names (%1 of %2)..."_s.arg(i + 1).arg(targets.size()));
    }
    emit updateConsole(u"Scanned %1 assets for names%2."_s.arg(targets.size() - failures)
                           .arg(failures ? u" (%1 couldn't be read)"_s.arg(failures) : QString()));
}

// ---------------------------------------------------------------------------------------------------------------
// Resolving
// ---------------------------------------------------------------------------------------------------------------

void NameResolverWorker::run() {
    QElapsedTimer timer;
    timer.start();

    emit updateConsole(u"Resolving file names (blobset V%1, %2)..."_s.arg(m_version + 1).arg(BlobsetHash::platformName(m_platform)));
    reportProgress(0, u"Reading the file mapping..."_s);
    std::vector<FileMappingWorker::MappingEntry> entries = FileMappingWorker::readMappingFile(m_mappingFile);
    reportProgress(2);

    QHash<quint64, qsizetype> unnamed;          // Hash -> mapping entry index.
    QHash<quint64, QString> mappedPaths;        // Every hash -> its mapped path.
    QSet<QString> takenPaths;
    QStringList allPaths;
    for (qsizetype i = 0; i < qsizetype(entries.size()); ++i) {
        const auto& e = entries[size_t(i)];
        mappedPaths.insert(e.hash, e.path);
        takenPaths.insert(e.path.toLower());
        allPaths.append(e.path);
        if (isUnnamed(e.path))
            unnamed.insert(e.hash, i);
    }
    emit updateConsole(u"%1 of %2 mapped files have no real name yet."_s.arg(unnamed.size()).arg(entries.size()));
    if (unnamed.isEmpty()) {
        emit taskSucceeded();
        emit taskFinished(u"Every file already has a name."_s);
        return;
    }

    reportProgress(3, u"Collecting candidate names..."_s);
    emit updateConsole(u"Collecting candidate names from the executables, names.txt and the game's assets..."_s);
    harvestExecutables();
    harvestUserNames();
    reportProgress(10);
    harvestAssets(mappedPaths, allPaths);
    emit updateConsole(u"Collected %1 candidate strings."_s.arg(m_strings.size()));

    // Candidate bases (path without type / extension) plus every folder above them; explicit "<base>.<type>" pairs.
    QSet<QString> bases;
    QSet<QPair<QString, QString>> explicitPairs;
    emit updateStatus(u"Building candidate names from %1 strings..."_s.arg(m_strings.size()));
    qsizetype stringsDone = 0;
    for (const QString& s : std::as_const(m_strings)) {
        reportRange(60, 70, ++stringsDone, m_strings.size());
        const qsizetype slash = s.lastIndexOf(u'/');
        const QString last = s.mid(slash + 1);
        const qsizetype dot = last.indexOf(u'.');
        const QString base = dot >= 0 ? s.left(slash + 1 + dot) : s;
        if (base.isEmpty())
            continue;

        if (dot >= 0) {
            const QStringList suffixes = last.mid(dot + 1).split(u'.');
            if (!suffixes.isEmpty() && isTypeWord(suffixes.first()) && suffixes.first() != u"pc"_s
                && suffixes.first() != u"ps3"_s)
                explicitPairs.insert({base, suffixes.first()});
        }

        bases.insert(base);
        for (qsizetype p = base.indexOf(u'/'); p > 0; p = base.indexOf(u'/', p + 1))
            bases.insert(base.left(p));
    }

    const QStringList guessedTypes = compatibleExtensions().keys();
    const QString platform = BlobsetHash::platformName(m_platform);

    struct Hit {
        QString base;
        QString type;
    };
    QHash<quint64, Hit> hits;
    int rejected = 0;

    const auto tryName = [&](const QString& base, const QString& type, bool spelledOut) {
        const quint64 hash = BlobsetHash::hashForAsset(m_version, {base, type}, m_platform, m_gameKey);
        const auto target = unnamed.constFind(hash);
        if (target == unnamed.constEnd() || hits.contains(hash))
            return;

        const QString extension = extensionOf(entries[size_t(target.value())].path);
        const auto compatible = compatibleExtensions().constFind(type);
        const bool fits = compatible != compatibleExtensions().constEnd() ? compatible.value().contains(extension)
                                                                          : spelledOut;
        if (!fits) {
            ++rejected;   // Most likely two different names sharing a 32 bit hash.
            return;
        }
        hits.insert(hash, {base, type});
    };

    reportProgress(70, u"Hashing %1 candidate names..."_s.arg(bases.size()));
    for (const auto& pair : std::as_const(explicitPairs))
        tryName(pair.first, pair.second, true);
    qsizetype done = 0;
    for (const QString& base : std::as_const(bases)) {
        for (const QString& type : guessedTypes)
            tryName(base, type, false);
        ++done;
        const int percent = 70 + int(25 * done / std::max<qsizetype>(1, bases.size()));
        if (percent != m_lastPercent)   // Only build the status text when the bar moves.
            reportProgress(percent, u"Hashing candidate names (%1 of %2, %3 match(es) so far)..."_s
                                .arg(done).arg(bases.size()).arg(hits.size()));
    }

    reportProgress(95, u"Renaming %1 matched file(s)..."_s.arg(hits.size()));

    // Rename.
    QStringList report;
    int renamed = 0;
    for (auto it = hits.cbegin(); it != hits.cend(); ++it) {
        auto& entry = entries[size_t(unnamed.value(it.key()))];
        const QString extension = extensionOf(entry.path);
        QString path = it.value().base;
        path.replace(u'/', u'\\');
        if (it.value().type == u"texture"_s && extension == u"dds"_s)
            path += u'.' + platform + u".dds"_s;   // Like the textures the mapping already names.
        else
            path += u'.' + it.value().type + (extension.isEmpty() || extension == it.value().type ? QString()
                                                                                                    : u'.' + extension);

        if (!FileMappingWorker::validatePath(path).isEmpty() || takenPaths.contains(path.toLower()))
            continue;

        report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(it.key(), m_version == 3 ? 16 : 8), entry.path, path));
        takenPaths.insert(path.toLower());
        entry.path = path;
        ++renamed;
    }

    if (renamed == 0) {
        reportProgress(100, u"Done."_s);
        emit taskSucceeded();
        emit taskFinished(u"No new names found (%1 candidate names tried). Add names to names.txt and try again."_s
                              .arg(bases.size()));
        return;
    }

    reportProgress(97, u"Writing the file mapping..."_s);
    FileMappingWorker::sortEntries(entries);
    FileMappingWorker::writeMappingFile(m_mappingFile, entries);
    reportProgress(99, u"Writing resolved_names.txt..."_s);

    // What was renamed, newest run first.
    const QString reportPath = QFileInfo(m_mappingFile).absoluteDir().filePath(u"resolved_names.txt"_s);
    QByteArray previous;
    {
        QFile old(reportPath);
        if (old.open(QIODevice::ReadOnly))
            previous = old.readAll();
    }
    report.sort(Qt::CaseInsensitive);
    QSaveFile out(reportPath);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(u"# %1: %2 file(s) named\r\n"_s.arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(renamed).toUtf8());
        out.write(report.join(u"\r\n"_s).toUtf8() + "\r\n\r\n");
        out.write(previous);
        out.commit();
    }

    for (const QString& line : report.mid(0, 50))
        emit updateConsole(line);
    if (report.size() > 50)
        emit updateConsole(u"... and %1 more (see resolved_names.txt)."_s.arg(report.size() - 50));
    if (rejected > 0)
        emit updateConsole(u"%1 hash match(es) were skipped because the type didn't fit the file (chance collisions)."_s
                               .arg(rejected));

    reportProgress(100, u"Done."_s);
    emit taskSucceeded();
    emit taskFinished(u"Named %1 of %2 unknown files in %3 s (%4 still unknown). The list is in %5."_s
                          .arg(renamed).arg(unnamed.size()).arg(timer.elapsed() / 1000)
                          .arg(unnamed.size() - renamed).arg(QDir::toNativeSeparators(reportPath)));
}
