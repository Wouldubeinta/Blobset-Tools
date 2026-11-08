#include "dialogue_editor_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "commentary_editor_manager.h"
#include "m3mp_tool_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QtEndian>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

struct ListKind {
    const char* key;
    const char* title;
    const char* hint;
    const char* prefixes;     // The commentary categories a name plays, comma separated.
};
constexpr ListKind kKinds[] = {
    {"surnames", "Surnames", "Players' surnames the commentators can say.", "surname_n_,surname_e_"},
    {"teamslocation", "Team Locations", "Teams' places (\"Brisbane\", \"Cronulla\") the commentators can say.",
     "team_mid_,team_and_,team_excited_"},
    {"teamsmascot", "Team Mascots", "Teams' mascots (\"Broncos\", \"Sharks\") the commentators can say.",
     "team_mid_,team_and_,team_excited_"},
    {"teams", "Teams", "Team names.", "team_mid_,team_and_,team_excited_"},
};
constexpr const char* kListPrefix = "sound/dialogue_wildcardhash_";
constexpr const char* kCommentaryHash = "99b3b6554a5750d3";   // sound\lines.commentary.cmtry
constexpr int kMaxName = 255;

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

QString hex32(quint32 v) {
    return u"0x"_s + u"%1"_s.arg(v, 8, 16, QChar(u'0')).toUpper();
}

// What a typed name becomes: lower case, spaces / punctuation dropped (the game's are a-z only: "A Hall" -> "ahall").
QByteArray cleanName(const QString& text) {
    QByteArray out;
    for (const QChar ch : text.trimmed().toLower()) {
        const char c = ch.toLatin1();
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out.append(c);
    }
    return out;
}

} // namespace

DialogueEditorManager::DialogueEditorManager(QObject* parent) : QObject(parent) {}

bool DialogueEditorManager::isDialogueHash(const QString& hexHash) {
    bool ok = false;
    const quint64 v = hexHash.trimmed().toULongLong(&ok, 16);
    return ok && v == QString::fromLatin1(kHash).toULongLong(nullptr, 16);
}

quint32 DialogueEditorManager::nameHash(const QByteArray& name) {
    quint32 h = 0x811c9dc5u;
    for (const char c : name) {
        h ^= static_cast<quint8>(c);
        h *= 0x01000193u;
    }
    return h;
}

bool DialogueEditorManager::modified() const {
    for (int i = 0; i < m_lists.size(); ++i)
        if (listData(i) != m_lists.at(i).originalData)
            return true;
    return false;
}

QString DialogueEditorManager::modsTarget() const {
    return m_m3mp ? m_m3mp->modsTarget() : QString();
}

void DialogueEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void DialogueEditorManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void DialogueEditorManager::edited() {
    ++m_revision;
    emit revisionChanged();
    emit modifiedChanged();
}

// --- Loading / saving ------------------------------------------------------------------------------------------

QList<DialogueEditorManager::Name> DialogueEditorManager::parseList(const QByteArray& data, bool* ok) {
    QList<Name> out;
    *ok = false;
    if (data.size() < 4)
        return out;
    const auto u32 = [&data](qsizetype at) {
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()) + at);
    };
    const quint32 count = u32(0);
    if (count > 1000000)
        return out;
    qsizetype p = 4;
    for (quint32 i = 0; i < count; ++i) {
        if (p + 8 > data.size())
            return {};
        const quint32 length = u32(p + 4);
        if (length > quint32(kMaxName) || p + 8 + qsizetype(length) > data.size())
            return {};
        const QByteArray name = data.mid(p + 8, length);
        out << Name{name, name};
        p += 8 + length;
    }
    *ok = p == data.size();
    return out;
}

QByteArray DialogueEditorManager::listData(int list) const {
    QByteArray out;
    if (list < 0 || list >= m_lists.size())
        return out;
    const auto put = [&out](quint32 v) {
        uchar b[4];
        qToLittleEndian(v, b);
        out.append(reinterpret_cast<const char*>(b), 4);
    };
    const List& l = m_lists.at(list);
    put(quint32(l.names.size()));
    for (const Name& n : l.names) {
        put(nameHash(n.name));
        put(quint32(n.name.size()));
        out += n.name;
    }
    return out;
}

bool DialogueEditorManager::loadFromEntries(const QList<QPair<QString, QByteArray>>& entries, const QByteArray& commentary) {
    QList<List> parsed;
    for (const auto& [path, data] : entries) {
        bool ok = false;
        const QList<Name> names = parseList(data, &ok);
        if (!ok) {
            setStatus(false, u"%1 isn't a name list this editor knows."_s.arg(path));
            return false;
        }
        List l;
        l.path = path;
        l.key = path.mid(QString::fromLatin1(kListPrefix).size());
        l.names = names;
        l.originalData = data;
        parsed << l;
    }
    if (parsed.isEmpty()) {
        setStatus(false, u"The M3MP has no dialogue_wildcardhash lists."_s);
        return false;
    }
    m_lists = parsed;
    m_speakers.clear();
    for (const auto& sp : CommentaryEditorManager::speakerCategories(commentary))
        m_speakers << Speaker{sp.name, sp.categories};
    ++m_revision;
    emit loadedChanged();
    emit modifiedChanged();
    emit revisionChanged();
    QStringList counts;
    for (int i = 0; i < m_lists.size(); ++i)
        counts << u"%1 %2"_s.arg(m_lists.at(i).names.size()).arg(lists().at(i).toMap().value(u"title"_s).toString().toLower());
    setStatus(true, u"Loaded %1%2."_s.arg(counts.join(u", "_s),
                                          m_speakers.isEmpty() ? u" (lines.commentary couldn't be read, so who says each name isn't shown)"_s : QString()));
    return true;
}

bool DialogueEditorManager::load() {
    if (m_busy)
        return false;
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(QString::fromLatin1(kHash), entry)) {
        setStatus(false, u"sound\\dialogue.mempack.m3mp (0x83AF3201ED1AE751) isn't in the loaded blobset: load Rugby League 26 "
                         "(or Rugby 25) first."_s);
        return false;
    }
    if (!m_m3mp) {
        m_m3mp = new M3mpToolManager(this);
        connect(m_m3mp, &M3mpToolManager::processComplete, this, [this](bool ok, const QString& message) {
            if (!m_saving)
                return;
            m_saving = false;
            setBusy(false);
            if (ok) {
                // What's saved is the new "original".
                for (int i = 0; i < m_lists.size(); ++i) {
                    m_lists[i].originalData = listData(i);
                    for (Name& n : m_lists[i].names)
                        n.original = n.name;
                }
                m_sourceText = u"Your edited copy in the mods folder"_s;
                emit loadedChanged();
                edited();
                setStatus(true, u"Saved to %1. Run Modify to put it in the game."_s.arg(m_m3mp->modsTarget()));
            } else {
                setStatus(false, message);
            }
            emit saved(ok, m_statusText);
        });
    }
    m_m3mp->loadM3mpFromBlobset(QString::fromLatin1(kHash));
    if (m_m3mp->entryCount() == 0) {
        setStatus(false, m_m3mp->statusText().isEmpty() ? u"sound\\dialogue.mempack.m3mp couldn't be opened."_s : m_m3mp->statusText());
        return false;
    }
    QList<QPair<QString, QByteArray>> entries;
    for (const ListKind& k : kKinds) {
        const QString path = QString::fromLatin1(kListPrefix) + QString::fromLatin1(k.key);
        const QByteArray data = m_m3mp->entryDataByPath(path);
        if (!data.isEmpty())
            entries << qMakePair(path, data);
    }

    // lines.commentary (your edited copy in the mods folder first) for who says each name.
    QByteArray commentary;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        const QString workspace = orchestrator->pathFromHash(QString::fromLatin1(kCommentaryHash));
        const QString mods = workspace.isEmpty() ? QString() : orchestrator->modFilePath(workspace);
        QFile file(mods);
        if (!mods.isEmpty() && QFileInfo(mods).isFile() && file.open(QIODevice::ReadOnly))
            commentary = file.readAll();
        BlobsetEntry lines;
        if (commentary.isEmpty() && BlobsetEntryRegistry::findEntry(QString::fromLatin1(kCommentaryHash), lines)) {
            try {
                commentary = BlobsetAssetReader::read(lines, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                                      orchestrator->platform());
            } catch (const std::exception&) {
                commentary.clear();
            }
        }
    }
    m_sourceText = m_m3mp->loadedFromMods() ? u"Your edited copy in the mods folder"_s : u"The game's sound\\dialogue.mempack"_s;
    return loadFromEntries(entries, commentary);
}

void DialogueEditorManager::save() {
    if (!loaded() || m_busy || !m_m3mp)
        return;
    if (m_m3mp->modsTarget().isEmpty()) {
        setStatus(false, u"The M3MP's file name isn't in the file mapping, so it can't be saved to the mods folder "
                         "(Update File Mapping Data / Resolve File Names)."_s);
        emit saved(false, m_statusText);
        return;
    }
    if (!modified()) {
        setStatus(true, u"Nothing has changed since the last save."_s);
        emit saved(true, m_statusText);
        return;
    }
    if (m_saveTemp.isEmpty())
        m_saveTemp = QDir(QDir::tempPath()).filePath(u"BlobsetTools-Dialogue-%1"_s.arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(m_saveTemp);
    for (int i = 0; i < m_lists.size(); ++i) {
        const QByteArray data = listData(i);
        if (data == m_lists.at(i).originalData)
            continue;
        QSaveFile file(QDir(m_saveTemp).filePath(u"dialogue_wildcardhash_"_s + m_lists.at(i).key));
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
            emit saved(false, m_statusText);
            return;
        }
        if (!m_m3mp->replaceEntryByPath(m_lists.at(i).path, file.fileName())) {
            setStatus(false, m_m3mp->statusText());
            emit saved(false, m_statusText);
            return;
        }
    }
    m_saving = true;
    setBusy(true);
    setStatus(true, u"Saving sound\\dialogue.mempack to the mods folder..."_s);
    m_m3mp->saveChanges();
}

void DialogueEditorManager::revertAll() {
    for (List& l : m_lists) {
        bool ok = false;
        l.names = parseList(l.originalData, &ok);
    }
    edited();
    setStatus(true, u"Reverted every change since the last save."_s);
}

// --- Lists ---------------------------------------------------------------------------------------------------

QStringList DialogueEditorManager::prefixes(int list) const {
    if (list < 0 || list >= m_lists.size())
        return {};
    for (const ListKind& k : kKinds)
        if (m_lists.at(list).key == QString::fromLatin1(k.key))
            return QString::fromLatin1(k.prefixes).split(u',');
    return {};
}

QStringList DialogueEditorManager::voicesFor(int list, const QString& name) const {
    QStringList out;
    const QStringList pre = prefixes(list);
    const QStringList real = voices(list);
    for (const Speaker& sp : m_speakers) {
        if (!real.contains(sp.name))
            continue;
        for (const QString& p : pre)
            if (sp.categories.contains(p + name)) {
                out << sp.name;
                break;
            }
    }
    return out;
}

// The commentators with a real set of this list's categories (not the placeholder voices with a handful).
QStringList DialogueEditorManager::voices(int list) const {
    QStringList out;
    const QStringList pre = prefixes(list);
    for (const Speaker& sp : m_speakers) {
        int count = 0;
        for (const QString& c : sp.categories)
            for (const QString& p : pre)
                if (c.startsWith(p)) {
                    ++count;
                    break;
                }
        if (count >= 20)
            out << sp.name;
    }
    return out;
}

int DialogueEditorManager::voiceCount(int list) const {
    return int(voices(list).size());
}

QVariantList DialogueEditorManager::lists() const {
    QVariantList out;
    for (int i = 0; i < m_lists.size(); ++i) {
        const List& l = m_lists.at(i);
        QString title = l.key, hint;
        for (const ListKind& k : kKinds)
            if (l.key == QString::fromLatin1(k.key)) {
                title = QString::fromLatin1(k.title);
                hint = QString::fromLatin1(k.hint);
            }
        out << QVariantMap{{u"index"_s, i}, {u"key"_s, l.key}, {u"title"_s, title}, {u"path"_s, l.path},
                           {u"count"_s, int(l.names.size())}, {u"modified"_s, listData(i) != l.originalData}, {u"hint"_s, hint}};
    }
    return out;
}

QVariantList DialogueEditorManager::rows(int list, const QString& filterIn, const QString& show) const {
    QVariantList out;
    if (list < 0 || list >= m_lists.size())
        return out;
    const QString filter = QString::fromLatin1(cleanName(filterIn));
    const int all = voiceCount(list);
    const List& l = m_lists.at(list);
    for (int i = 0; i < l.names.size(); ++i) {
        const Name& n = l.names.at(i);
        const QString name = QString::fromLatin1(n.name);
        if (!filter.isEmpty() && !name.contains(filter) && !hex32(nameHash(n.name)).contains(filterIn.trimmed(), Qt::CaseInsensitive))
            continue;
        const QStringList v = voicesFor(list, name);
        if (show == u"with"_s && v.isEmpty())
            continue;
        if (show == u"without"_s && (!v.isEmpty() || i == 0))
            continue;
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, name}, {u"hash"_s, hex32(nameHash(n.name))},
                           {u"voices"_s, int(v.size())}, {u"of"_s, all}, {u"voiceNames"_s, v.join(u", "_s)},
                           {u"added"_s, n.original.isEmpty()},
                           {u"modified"_s, n.original.isEmpty() || n.original != n.name},
                           {u"renamedFrom"_s, !n.original.isEmpty() && n.original != n.name ? QString::fromLatin1(n.original) : QString()},
                           {u"isNone"_s, i == 0 && n.name == "none"}};
    }
    return out;
}

QVariantList DialogueEditorManager::unlisted(int list) const {
    QVariantList out;
    if (list < 0 || list >= m_lists.size())
        return out;
    QSet<QString> listed;
    for (const Name& n : m_lists.at(list).names)
        listed.insert(QString::fromLatin1(n.name));
    // Locations and mascots share the team_ categories: one isn't "unlisted" because it's in the other list.
    const QStringList pre = prefixes(list);
    if (pre.contains(u"team_mid_"_s))
        for (int other = 0; other < m_lists.size(); ++other)
            if (other != list && prefixes(other) == pre)
                for (const Name& n : m_lists.at(other).names)
                    listed.insert(QString::fromLatin1(n.name));
    QSet<QString> found;
    for (const Speaker& sp : m_speakers)
        for (const QString& c : sp.categories)
            for (const QString& p : pre)
                if (c.startsWith(p)) {
                    const QString name = c.mid(p.size());
                    if (!name.isEmpty() && !listed.contains(name))
                        found.insert(name);
                }
    QStringList names(found.begin(), found.end());
    names.sort();
    for (const QString& name : names) {
        const QStringList v = voicesFor(list, name);
        out << QVariantMap{{u"name"_s, name}, {u"voices"_s, int(v.size())}, {u"voiceNames"_s, v.join(u", "_s)},
                           {u"hash"_s, hex32(nameHash(name.toLatin1()))}};
    }
    return out;
}

QString DialogueEditorManager::hashOf(const QString& name) const {
    const QByteArray n = cleanName(name);
    return n.isEmpty() ? QString() : hex32(nameHash(n));
}

QString DialogueEditorManager::checkName(int list, const QString& text, int except) const {
    if (list < 0 || list >= m_lists.size())
        return u"Pick a list."_s;
    const QByteArray name = cleanName(text);
    if (name.isEmpty())
        return u"Type a name (letters a-z; spaces and punctuation are dropped)."_s;
    if (name.size() > kMaxName)
        return u"That's too long."_s;
    const quint32 h = nameHash(name);
    const QList<Name>& names = m_lists.at(list).names;
    for (int i = 0; i < names.size(); ++i) {
        if (i == except)
            continue;
        if (names.at(i).name == name)
            return u"\"%1\" is already in the list."_s.arg(QString::fromLatin1(name));
        if (nameHash(names.at(i).name) == h)
            return u"\"%1\" has the same hash as \"%2\" (%3): the game couldn't tell them apart."_s
                .arg(QString::fromLatin1(name), QString::fromLatin1(names.at(i).name), hex32(h));
    }
    return {};
}

// Where a name goes to keep the list in alphabetical order: "none" stays first, and the name goes before the
// first name after it (the game's lists are in order, bar a few, which stay where they are).
int DialogueEditorManager::sortedPosition(int list, const QByteArray& name) const {
    const QList<Name>& names = m_lists.at(list).names;
    const int first = !names.isEmpty() && names.first().name == "none" ? 1 : 0;
    for (int i = first; i < names.size(); ++i)
        if (names.at(i).name > name)
            return i;
    return int(names.size());
}

int DialogueEditorManager::indexOfName(int list, const QString& text) const {
    if (list < 0 || list >= m_lists.size())
        return -1;
    const QByteArray name = cleanName(text);
    for (int i = 0; i < m_lists.at(list).names.size(); ++i)
        if (m_lists.at(list).names.at(i).name == name)
            return i;
    return -1;
}

int DialogueEditorManager::addName(int list, const QString& text) {
    const QString problem = checkName(list, text);
    if (!problem.isEmpty()) {
        setStatus(false, problem);
        return -1;
    }
    const QByteArray name = cleanName(text);
    const int at = sortedPosition(list, name);
    m_lists[list].names.insert(at, Name{name, QByteArray()});
    edited();
    const QStringList v = voicesFor(list, QString::fromLatin1(name));
    setStatus(v.isEmpty() ? false : true,
              v.isEmpty() ? u"Added \"%1\" (%2), but no commentator has lines for it: the commentary needs %3%1 categories "
                             "(Commentary Editor) for it to be said."_s
                                .arg(QString::fromLatin1(name), hex32(nameHash(name)), prefixes(list).value(0))
                          : u"Added \"%1\" (%2): said by %3."_s.arg(QString::fromLatin1(name), hex32(nameHash(name)), v.join(u", "_s)));
    return at;
}

int DialogueEditorManager::addNames(int list, const QStringList& names) {
    if (list < 0 || list >= m_lists.size())
        return 0;
    int added = 0;
    for (const QString& n : names) {
        if (!checkName(list, n).isEmpty())
            continue;
        const QByteArray name = cleanName(n);
        m_lists[list].names.insert(sortedPosition(list, name), Name{name, QByteArray()});
        ++added;
    }
    if (added)
        edited();
    setStatus(true, u"Added %1 name(s)."_s.arg(added));
    return added;
}

bool DialogueEditorManager::renameName(int list, int index, const QString& text) {
    if (list < 0 || list >= m_lists.size() || index < 0 || index >= m_lists.at(list).names.size())
        return false;
    const QByteArray name = cleanName(text);
    if (name == m_lists.at(list).names.at(index).name)
        return false;
    const QString problem = checkName(list, text, index);
    if (!problem.isEmpty()) {
        setStatus(false, problem);
        return false;
    }
    // Renamed, it moves to its place in alphabetical order (indexOfName() finds it).
    Name n = m_lists[list].names.takeAt(index);
    const QByteArray before = n.name;
    n.name = name;
    m_lists[list].names.insert(sortedPosition(list, name), n);
    edited();
    setStatus(true, u"Renamed \"%1\" to \"%2\": its hash is now %3. Players / teams in the database with the old one (%4) "
                    "need it changed too."_s.arg(QString::fromLatin1(before), QString::fromLatin1(name), hex32(nameHash(name)),
                                                hex32(nameHash(before))));
    return true;
}

bool DialogueEditorManager::removeName(int list, int index) {
    if (list < 0 || list >= m_lists.size() || index < 0 || index >= m_lists.at(list).names.size())
        return false;
    if (index == 0 && m_lists.at(list).names.at(0).name == "none") {
        setStatus(false, u"\"none\" is what a player / team without a commentary name uses: it stays."_s);
        return false;
    }
    const QByteArray name = m_lists.at(list).names.at(index).name;
    m_lists[list].names.removeAt(index);
    edited();
    setStatus(true, u"Removed \"%1\" (%2). Players / teams in the database that use it won't have their name said."_s
                        .arg(QString::fromLatin1(name), hex32(nameHash(name))));
    return true;
}

bool DialogueEditorManager::revertName(int list, int index) {
    if (list < 0 || list >= m_lists.size() || index < 0 || index >= m_lists.at(list).names.size())
        return false;
    if (m_lists.at(list).names.at(index).original.isEmpty() || m_lists.at(list).names.at(index).original == m_lists.at(list).names.at(index).name)
        return false;
    Name n = m_lists[list].names.takeAt(index);
    n.name = n.original;
    m_lists[list].names.insert(sortedPosition(list, n.name), n);
    edited();
    return true;
}

bool DialogueEditorManager::exportList(int list, const QString& pathOrUrl) {
    if (list < 0 || list >= m_lists.size())
        return false;
    QByteArray text;
    for (const Name& n : m_lists.at(list).names)
        text += n.name + "\r\n";
    const QString path = toLocalPath(pathOrUrl);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return false;
    }
    setStatus(true, u"Exported %1 names to %2."_s.arg(m_lists.at(list).names.size()).arg(QDir::toNativeSeparators(path)));
    return true;
}

bool DialogueEditorManager::importList(int list, const QString& pathOrUrl) {
    if (list < 0 || list >= m_lists.size())
        return false;
    const QString path = toLocalPath(pathOrUrl);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    const QStringList names = QString::fromUtf8(file.readAll()).split(QRegularExpression(u"[\\r\\n,;\\t]+"_s), Qt::SkipEmptyParts);
    const int added = addNames(list, names);
    setStatus(true, u"Imported %1: %2 new name(s) added (names already in the list are skipped)."_s
                        .arg(QFileInfo(path).fileName()).arg(added));
    return true;
}
