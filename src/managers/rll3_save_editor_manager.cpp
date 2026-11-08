#include "rll3_save_editor_manager.h"

#include "app_orchestrator.h"
#include "dds_image_provider.h"
#include "rll3_lists.h"
#include "rll3_save_lists.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

enum class Type { U8, U16, I16, I32, Bool, Str, Palette };

enum Flags { kReadOnly = 1, kSlider = 2, kLook = 4 };

struct FieldDef {
    const char* key;
    const char* label;
    const char* group;
    Type type;
    int size;
    bool big;
    int off;
    int min;
    int max;
    const char* list;   // Drop down ("@..." = worked out here).
    int flags;
};

#include "rll3_save_fields.inc"

constexpr int kVersionAt = 0;
constexpr int kCrcAt = 4;
constexpr int kCrcFrom = 132;
constexpr int kTeamCountAt = 241;
constexpr qsizetype kTeamTable[2] = {752, 764};
constexpr qsizetype kPlayerCountAt[2] = {823188, 824096};
constexpr qsizetype kPlayerTable[2] = {827408, 828328};
constexpr qsizetype kLeaguesAt[2] = {752 + 232 * 2577, 764 + 232 * 2578};   // The end of the 232 team records.
constexpr int kMaxTeams = 232;
constexpr int kMaxJerseys = 8;
constexpr int kNoCommentary = 65535;   // The surname list's last entry.
const char* const kResourceDir = ":/qt/qml/com/wouldymods/blobset/assets/rll3_save/";

const QStringList kRoleNames = {u"Fullback"_s, u"Winger"_s, u"Centre"_s, u"Five-Eighth"_s, u"Halfback"_s, u"Prop"_s,
                                u"Hooker"_s, u"Second Row"_s, u"Lock"_s};
const QStringList kRoleShort = {u"FB"_s, u"WG"_s, u"C"_s, u"FE"_s, u"HB"_s, u"FR"_s, u"HK"_s, u"SR"_s, u"L"_s};
// The C# editor's lineup: the game keeps it in this order (the numbers are the shirts).
const QStringList kPositions = {u"1 - Fullback"_s, u"5 - Left Wing"_s, u"4 - Left Centre"_s, u"3 - Right Centre"_s,
                                u"2 - Right Wing"_s, u"6 - Five-Eighth"_s, u"7 - Halfback"_s, u"8 - Prop"_s, u"9 - Hooker"_s,
                                u"10 - Prop"_s, u"11 - Second Row"_s, u"12 - Second Row"_s, u"13 - Lock"_s,
                                u"14 - Interchange 1"_s, u"15 - Interchange 2"_s, u"16 - Interchange 3"_s,
                                u"17 - Interchange 4"_s};
const QStringList kTeamRoles = {u"Captain"_s, u"Goal Kicker"_s, u"Play Maker 1"_s, u"Play Maker 2"_s};
const char* const kStatKeys[7] = {"technicalAbility.speed", "technicalAbility.strength", "technicalAbility.technique",
                                  "technicalAbility.footwork", "technicalAbility.agility", "technicalAbility.fitness",
                                  "technicalAbility.strategy"};

// The C# editor's Rating.cs: 30 + (stat - 30) / divisor, per stat (speed .. strategy), by primary role.
double ratingFor(int role, const int stats[7]) {
    static const double backs[7] = {3.5, 6.5, 9.5, 5, 18.5, 9.5, 9.5};
    static const double halves[7] = {9.5, 18.5, 6.5, 6.5, 9.5, 9.5, 3};
    static const double hooker[7] = {1000, 18.5, 3.5, 6.5, 18.5, 18.5, 2.3};
    static const double forwards[7] = {9.5, 2.3, 9.5, 9.5, 18.5, 5, 1000};
    const double* d = role <= 2 ? backs : role <= 4 ? halves : role == 6 ? hooker : forwards;
    double r = 30;
    for (int i = 0; i < 7; ++i)
        r += (stats[i] - 30) / d[i];
    return r;
}
int roundRating(double r) {
    return static_cast<int>(std::round(r));   // Away from zero, as the C# editor shows it.
}

template <size_t N>
QList<const FieldDef*> toList(const FieldDef (&defs)[N]) {
    QList<const FieldDef*> out;
    for (const FieldDef& d : defs)
        out.append(&d);
    return out;
}

const QList<const FieldDef*>& defsFor(const QString& kind) {
    static const QList<const FieldDef*> team = toList(kTeamFields);
    static const QList<const FieldDef*> player = toList(kPlayerFields);
    return kind == u"team"_s ? team : player;
}

const QList<const FieldDef*>& jerseyDefs() {
    static const QList<const FieldDef*> list = toList(kJerseyFields);
    return list;
}

const FieldDef* findDef(const QList<const FieldDef*>& defs, const QString& key) {
    for (const FieldDef* d : defs)
        if (key == QLatin1StringView(d->key))
            return d;
    return nullptr;
}

const FieldDef* defFor(const QString& kind, const QString& key) {
    return findDef(defsFor(kind), key);
}

int offOf(const QString& kind, const char* key) {
    const FieldDef* d = defFor(kind, QString::fromLatin1(key));
    return d ? d->off : -1;
}

QString localPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString hexColour(unsigned int rgb) {
    return u"#%1"_s.arg(rgb & 0xFFFFFF, 6, 16, QChar(u'0'));
}

quint32 crc32(const char* data, qsizetype size) {
    static const QList<quint32> table = [] {
        QList<quint32> t(256);
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (qsizetype i = 0; i < size; ++i)
        crc = (crc >> 8) ^ table[(crc ^ static_cast<quint8>(data[i])) & 0xFF];
    return ~crc;
}

QVariantList namedChoices(const QStringList& names, int base = 0) {
    QVariantList out;
    for (int i = 0; i < names.size(); ++i)
        out << QVariantMap{{u"name"_s, names.at(i)}, {u"value"_s, base + i}};
    return out;
}

template <typename C>
QVariantList choiceList(const QList<C>& list) {
    QVariantList out;
    for (const C& c : list)
        out << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
    return out;
}

// A palette as choices: {name, value, colour}.
QVariantList paletteChoices(const QList<unsigned int>& palette) {
    QVariantList out;
    for (int i = 0; i < palette.size(); ++i)
        out << QVariantMap{{u"name"_s, u"%1  %2"_s.arg(i).arg(hexColour(palette.at(i)).toUpper())}, {u"value"_s, i},
                           {u"colour"_s, hexColour(palette.at(i))}};
    return out;
}

const QList<unsigned int>& paletteFor(const QString& list) {
    if (list == u"teamPalette"_s) return Rll3SaveLists::teamPalette();
    if (list == u"skinPalette"_s) return Rll3SaveLists::skinPalette();
    return Rll3SaveLists::jerseyPalette();
}

QString fileSafe(QString name) {
    static const QString bad = u"<>:\"/\\|?*"_s;
    for (QChar& c : name)
        if (bad.contains(c) || c.unicode() < 32)
            c = u'_';
    name = name.trimmed();
    return name.isEmpty() ? u"record"_s : name;
}

} // namespace

// =================================================================================================================
// Load / save
// =================================================================================================================

Rll3SaveEditorManager::Rll3SaveEditorManager(QObject* parent) : QObject(parent) {}

QString Rll3SaveEditorManager::fileName() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString Rll3SaveEditorManager::defaultFolder() const {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(u"My Games/Rugby League Live 3"_s);
}

QString Rll3SaveEditorManager::defaultFile() const {
    return QDir(defaultFolder()).filePath(u"FED000.SAV"_s);
}

void Rll3SaveEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void Rll3SaveEditorManager::edited() {
    ++m_revision;
    emit revisionChanged();
    if (!m_unsaved) {
        m_unsaved = true;
        emit unsavedChangesChanged();
    }
}

int Rll3SaveEditorManager::teamCount() const {
    return m_loaded ? static_cast<quint8>(m_data.at(kTeamCountAt)) : 0;
}

int Rll3SaveEditorManager::playerCount() const {
    return m_loaded ? static_cast<int>(readNum(kPlayerCountAt[m_variant], 2, true, false)) : 0;
}

qsizetype Rll3SaveEditorManager::teamTable() const { return kTeamTable[m_variant]; }
qsizetype Rll3SaveEditorManager::playerTable() const { return kPlayerTable[m_variant]; }
int Rll3SaveEditorManager::teamSize() const { return kTeamSize[m_variant]; }
int Rll3SaveEditorManager::playerSize() const { return kPlayerSize[m_variant]; }

int Rll3SaveEditorManager::maxTeams() const {
    return static_cast<int>(qMin<qsizetype>(kMaxTeams, (kLeaguesAt[m_variant] - teamTable()) / teamSize()));
}

int Rll3SaveEditorManager::maxPlayers() const {
    return static_cast<int>(qMin<qsizetype>(kMaxTeams * 64, (m_data.size() - playerTable()) / playerSize()));
}

bool Rll3SaveEditorManager::load(const QString& pathOrUrl) {
    const QString path = QDir::toNativeSeparators(pathOrUrl.isEmpty() ? defaultFile() : localPath(pathOrUrl));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Couldn't open %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    if (data.size() < kPlayerTable[1] + 2) {
        setStatus(false, u"%1 is too small to be Rugby League Live 3's FED000.SAV."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    const qint32 version = qFromLittleEndian<qint32>(data.constData() + kVersionAt);
    if (version == 0x204E4F43) {
        setStatus(false, u"%1 is an Xbox 360 save (a CON package): only the PC saves can be edited here."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    if (version != 1 && version != 2) {
        setStatus(false, u"%1 isn't a Rugby League Live 3 FED save (version %2)."_s.arg(QFileInfo(path).fileName()).arg(version));
        return false;
    }
    const int variant = version - 1;
    const int teams = static_cast<quint8>(data.at(kTeamCountAt));
    const int players = qFromBigEndian<quint16>(data.constData() + kPlayerCountAt[variant]);
    if (kTeamTable[variant] + qint64(teams) * kTeamSize[variant] > kLeaguesAt[variant]
        || kPlayerTable[variant] + qint64(players) * kPlayerSize[variant] > data.size()) {
        setStatus(false, u"%1's team / player counts don't fit: is it Rugby League Live 3's FED000.SAV?"_s.arg(QFileInfo(path).fileName()));
        return false;
    }

    m_data = data;
    m_path = path;
    m_variant = variant;
    m_checksumOk = qFromLittleEndian<quint32>(m_data.constData() + kCrcAt) == crc32(m_data.constData() + kCrcFrom, m_data.size() - kCrcFrom);
    m_backedUp = false;
    m_loaded = true;
    m_logoUrls.clear();
    loadOriginals();
    if (m_unsaved) {
        m_unsaved = false;
        emit unsavedChangesChanged();
    }
    ++m_revision;
    emit loadedChanged();
    emit revisionChanged();
    QString text = u"Loaded %1 (save version %2): %3 teams, %4 players."_s.arg(fileName()).arg(version).arg(teams).arg(players);
    if (!m_checksumOk)
        text += u" The file's checksum doesn't match (it'll be right once saved)."_s;
    setStatus(true, text);
    return true;
}

// The game's own players (the C# editor's OriginalPlayers.bin): s32 id, names, roles, 7 stats.
void Rll3SaveEditorManager::loadOriginals() {
    if (!m_originals.isEmpty())
        return;
    QString path = QString::fromLatin1(kResourceDir) + u"OriginalPlayers.bin"_s;
    if (const QByteArray dir = qgetenv("RLL3_SAVE_RESOURCES"); !dir.isEmpty())
        path = QDir(QString::fromLocal8Bit(dir)).filePath(u"OriginalPlayers.bin"_s);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray b = f.readAll();
    qsizetype p = 0;
    const auto u8 = [&b, &p]() { return p < b.size() ? static_cast<quint8>(b.at(p++)) : 0; };
    while (p + 4 < b.size()) {
        Original o;
        o.id = qFromLittleEndian<qint32>(b.constData() + p);
        p += 4;
        int n = u8();
        o.first = QString::fromLatin1(b.mid(p, n));
        p += n;
        n = u8();
        o.last = QString::fromLatin1(b.mid(p, n));
        p += n;
        for (int& r : o.roles)
            r = u8();
        int stats[7];
        for (int& s : stats)
            s = u8();
        if (p > b.size())
            break;
        o.footwork = stats[3];
        o.rating = roundRating(ratingFor(o.roles[0], stats));
        m_originalIndex.insert(o.id, static_cast<int>(m_originals.size()));
        m_originals.append(o);
    }
}

bool Rll3SaveEditorManager::save() {
    if (!m_loaded)
        return false;
    QString backupNote;
    if (!m_backedUp) {
        const QString backup = m_path + u".bak"_s;
        if (!QFile::exists(backup)) {
            if (!QFile::copy(m_path, backup)) {
                setStatus(false, u"Couldn't back up %1 to %2; nothing was written."_s.arg(fileName(), backup));
                return false;
            }
            backupNote = u" The original is kept as %1."_s.arg(QFileInfo(backup).fileName());
        }
        m_backedUp = true;
    }
    if (!saveAs(m_path))
        return false;
    setStatus(true, u"Saved %1.%2"_s.arg(fileName(), backupNote));
    return true;
}

bool Rll3SaveEditorManager::saveAs(const QString& pathOrUrl) {
    if (!m_loaded)
        return false;
    const QString path = QDir::toNativeSeparators(localPath(pathOrUrl));
    qToLittleEndian<quint32>(crc32(m_data.constData() + kCrcFrom, m_data.size() - kCrcFrom), m_data.data() + kCrcAt);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data) != m_data.size() || !f.commit()) {
        setStatus(false, u"Couldn't write %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    if (path != m_path) {
        m_path = path;
        m_backedUp = true;
    }
    m_checksumOk = true;
    m_unsaved = false;
    emit unsavedChangesChanged();
    emit loadedChanged();
    setStatus(true, u"Saved as %1."_s.arg(path));
    return true;
}

// =================================================================================================================
// Data access
// =================================================================================================================

qsizetype Rll3SaveEditorManager::recordAt(const QString& kind, int index) const {
    if (!m_loaded || index < 0)
        return -1;
    if (kind == u"team"_s)
        return index < teamCount() ? teamTable() + qsizetype(index) * teamSize() : -1;
    return index < playerCount() ? playerTable() + qsizetype(index) * playerSize() : -1;
}

int Rll3SaveEditorManager::recordSize(const QString& kind) const {
    return kind == u"team"_s ? teamSize() : playerSize();
}

QString Rll3SaveEditorManager::recordOffset(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    return at < 0 ? QString() : u"0x"_s + QString::number(at, 16).toUpper();
}

qint64 Rll3SaveEditorManager::readNum(qsizetype at, int size, bool big, bool isSigned) const {
    if (at < 0 || at + size > m_data.size())
        return 0;
    const char* p = m_data.constData() + at;
    switch (size) {
    case 1: return isSigned ? qint64(static_cast<qint8>(*p)) : qint64(static_cast<quint8>(*p));
    case 2: {
        const quint16 v = big ? qFromBigEndian<quint16>(p) : qFromLittleEndian<quint16>(p);
        return isSigned ? qint64(static_cast<qint16>(v)) : qint64(v);
    }
    case 4: {
        const quint32 v = big ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
        return isSigned ? qint64(static_cast<qint32>(v)) : qint64(v);
    }
    }
    return 0;
}

void Rll3SaveEditorManager::writeNum(qsizetype at, int size, bool big, qint64 value) {
    if (at < 0 || at + size > m_data.size())
        return;
    char* p = m_data.data() + at;
    switch (size) {
    case 1: *p = static_cast<char>(value); break;
    case 2:
        if (big) qToBigEndian<quint16>(static_cast<quint16>(value), p);
        else qToLittleEndian<quint16>(static_cast<quint16>(value), p);
        break;
    case 4:
        if (big) qToBigEndian<quint32>(static_cast<quint32>(value), p);
        else qToLittleEndian<quint32>(static_cast<quint32>(value), p);
        break;
    }
}

QString Rll3SaveEditorManager::readText(qsizetype at, int size) const {
    QByteArray b = m_data.mid(at, size);
    if (b.indexOf('\0') >= 0)
        b.truncate(b.indexOf('\0'));
    return QString::fromLatin1(b);
}

void Rll3SaveEditorManager::writeText(qsizetype at, int size, const QString& text) {
    if (at < 0 || at + size > m_data.size())
        return;
    QByteArray b = text.toLatin1().left(size - 1);
    b.resize(size, '\0');
    m_data.replace(at, size, b);
}

// =================================================================================================================
// Players
// =================================================================================================================

int Rll3SaveEditorManager::playerIndexById(int id) const {
    if (id <= 0)
        return -1;
    const int count = playerCount();
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"player"_s, i), 4, true, true) == id)
            return i;
    return -1;
}

int Rll3SaveEditorManager::playerRating(int player) const {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return 0;
    int stats[7];
    for (int i = 0; i < 7; ++i)
        stats[i] = static_cast<quint8>(m_data.at(at + offOf(u"player"_s, kStatKeys[i])));
    return roundRating(ratingFor(static_cast<quint8>(m_data.at(at + offOf(u"player"_s, "primaryRole"))), stats));
}

int Rll3SaveEditorManager::footworkOfId(int id) const {
    const int p = playerIndexById(id);
    if (p >= 0)
        return static_cast<quint8>(m_data.at(recordAt(u"player"_s, p) + offOf(u"player"_s, "technicalAbility.footwork")));
    const auto it = m_originalIndex.constFind(id);
    return it == m_originalIndex.constEnd() ? 0 : m_originals.at(it.value()).footwork;
}

int Rll3SaveEditorManager::ratingOfId(int id) const {
    const int p = playerIndexById(id);
    if (p >= 0)
        return playerRating(p);
    const auto it = m_originalIndex.constFind(id);
    return it == m_originalIndex.constEnd() ? 0 : m_originals.at(it.value()).rating;
}

QVariantMap Rll3SaveEditorManager::playerInfo(int id) const {
    QVariantMap m{{u"id"_s, id}, {u"custom"_s, false}, {u"role"_s, QString()}, {u"rating"_s, 0}, {u"age"_s, 0},
                  {u"club"_s, QString()}};
    const int p = playerIndexById(id);
    if (p >= 0) {
        const qsizetype at = recordAt(u"player"_s, p);
        const auto byte = [this, at](const char* key) { return static_cast<int>(static_cast<quint8>(m_data.at(at + offOf(u"player"_s, key)))); };
        m[u"first"_s] = readText(at + offOf(u"player"_s, "firstName"), 20);
        m[u"last"_s] = readText(at + offOf(u"player"_s, "lastName"), 20);
        m[u"name"_s] = (m[u"first"_s].toString() + u' ' + m[u"last"_s].toString()).trimmed();
        m[u"known"_s] = true;
        m[u"role"_s] = kRoleShort.value(byte("primaryRole"));
        m[u"roles"_s] = QVariantList{byte("primaryRole"), byte("secondaryRole"), byte("tertiaryRole")};
        m[u"rating"_s] = playerRating(p);
        m[u"custom"_s] = true;
        m[u"club"_s] = u"Custom player"_s;
        m[u"playerIndex"_s] = p;
        return m;
    }
    const auto it = m_originalIndex.constFind(id);
    if (it == m_originalIndex.constEnd()) {
        m[u"name"_s] = id == 0 ? u"(empty)"_s : u"(unknown player id %1)"_s.arg(id);
        m[u"first"_s] = QString();
        m[u"last"_s] = m[u"name"_s];
        m[u"known"_s] = false;
        return m;
    }
    const Original& o = m_originals.at(it.value());
    m[u"name"_s] = (o.first + u' ' + o.last).trimmed();
    m[u"first"_s] = o.first;
    m[u"last"_s] = o.last;
    m[u"known"_s] = true;
    m[u"role"_s] = kRoleShort.value(o.roles[0]);
    m[u"roles"_s] = QVariantList{o.roles[0], o.roles[1], o.roles[2]};
    m[u"rating"_s] = o.rating;
    m[u"club"_s] = u"Game player"_s;
    return m;
}

QVariantList Rll3SaveEditorManager::playerRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const int count = playerCount();
    for (int i = 0; i < count; ++i) {
        const qsizetype at = recordAt(u"player"_s, i);
        const int id = static_cast<int>(readNum(at, 4, true, true));
        const QString first = readText(at + offOf(u"player"_s, "firstName"), 20);
        const QString last = readText(at + offOf(u"player"_s, "lastName"), 20);
        if (!f.isEmpty() && !(first + u' ' + last).contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        out.append(QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"originalId"_s, 0}, {u"first"_s, first}, {u"last"_s, last},
                               {u"shirt"_s, readText(at + offOf(u"player"_s, "jerseyName"), 16)}, {u"age"_s, 0},
                               {u"female"_s, false},
                               {u"role"_s, kRoleNames.value(static_cast<quint8>(m_data.at(at + offOf(u"player"_s, "primaryRole"))))},
                               {u"rating"_s, playerRating(i)}});
    }
    return out;
}

// =================================================================================================================
// Teams
// =================================================================================================================

QVariantList Rll3SaveEditorManager::teamRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const int count = teamCount();
    for (int i = 0; i < count; ++i) {
        const qsizetype at = recordAt(u"team"_s, i);
        const int id = static_cast<int>(readNum(at, 4, true, true));
        const QString location = readText(at + offOf(u"team"_s, "locationName"), 22);
        const QString club = readText(at + offOf(u"team"_s, "clubName"), 16);
        const QString full = (location + u' ' + club).trimmed();
        if (!f.isEmpty() && !full.contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        const int colourSet = static_cast<quint8>(m_data.at(at + offOf(u"team"_s, "colourSet")));
        out.append(QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"originalId"_s, 0}, {u"fullName"_s, full},
                               {u"shortName"_s, club}, {u"abbreviation"_s, readText(at + offOf(u"team"_s, "abbreviatedName"), 4)},
                               {u"primaryColour"_s, hexColour(Rll3SaveLists::teamPalette().value(colourSet))},
                               {u"secondaryColour"_s, u"#ffffff"_s}, {u"female"_s, false},
                               {u"logo"_s, teamLogo(i)}, {u"logoUrl"_s, teamLogoUrl(i)}});
    }
    return out;
}

QString Rll3SaveEditorManager::teamLogo(int team) const {
    const qsizetype at = recordAt(u"team"_s, team);
    return at < 0 ? QString() : readText(at + offOf(u"team"_s, "logo"), 16);
}

QString Rll3SaveEditorManager::teamLogoUrl(int team) const {
    const QString logo = teamLogo(team).toLower();
    if (logo.isEmpty())
        return {};
    if (const auto it = m_logoUrls.constFind(logo); it != m_logoUrls.constEnd())
        return it.value();
    QString url;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        for (const char* platform : {".pc", "", ".ps3", ".x360"}) {
            const QString hash = orchestrator->getHashFromPath(
                u"ui/club_logos/large_mirrored/%1%2.dds"_s.arg(logo, QLatin1StringView(platform)));
            if (!hash.isEmpty() && hash.toULongLong(nullptr, 16) != 0) {
                url = u"image://dds_provider/"_s + DdsImageProvider::makeId(hash, 0, {});
                break;
            }
        }
    }
    m_logoUrls.insert(logo, url);
    return url;
}

int Rll3SaveEditorManager::teamRating(int team) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return 0;
    const int count = qMin<int>(17, static_cast<int>(readNum(at + kLineupAmount, 2, true, false)));
    if (count <= 0)
        return 0;
    int sum = 0;
    for (int slot = 0; slot < count; ++slot)
        sum += ratingOfId(static_cast<int>(readNum(at + kLineups + 4 * slot, 4, true, true)));
    return roundRating(sum / 17.0);
}

// =================================================================================================================
// Fields
// =================================================================================================================

QStringList Rll3SaveEditorManager::groups(const QString& kind) const {
    QStringList out;
    for (const FieldDef* d : defsFor(kind))
        if (!out.contains(QString::fromLatin1(d->group)))
            out << QString::fromLatin1(d->group);
    return out;
}

QStringList Rll3SaveEditorManager::pageGroups(const QString& kind, const QString& page) const {
    if (kind == u"team"_s) {
        if (page == u"Details"_s)
            return {u"Names"_s, u"Details"_s, u"Colours"_s, u"Record"_s};
        if (page == u"Stadiums"_s)
            return {u"Stadiums"_s, u"Stadium 1"_s, u"Stadium 2"_s, u"Stadium 3"_s, u"Grand Final Stadium"_s};
        return {};
    }
    if (page == u"Identity"_s)
        return {u"Name"_s, u"Details"_s, u"Roles"_s, u"Representative"_s, u"Record"_s};
    if (page == u"Ratings"_s)
        return {u"Attributes"_s};
    return {};
}

QVariantList Rll3SaveEditorManager::fields(const QString& kind, int index) const {
    return recordFields(kind, index, 0);
}

QVariantList Rll3SaveEditorManager::appearanceFields(int player) const {
    return recordFields(u"player"_s, player, kLook);
}

bool Rll3SaveEditorManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    return setField(u"player"_s, player, key, value);
}

QVariantList Rll3SaveEditorManager::recordFields(const QString& kind, int index, int wantPage) const {
    QVariantList out;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return out;
    for (const FieldDef* d : defsFor(kind)) {
        if ((d->flags & kLook) != (wantPage & kLook))
            continue;
        QVariantMap m;
        m[u"key"_s] = QString::fromLatin1(d->key);
        m[u"label"_s] = QString::fromLatin1(d->label);
        m[u"group"_s] = QString::fromLatin1(d->group);
        m[u"readOnly"_s] = (d->flags & kReadOnly) != 0;
        m[u"slider"_s] = (d->flags & kSlider) != 0;
        m[u"offset"_s] = u"+0x"_s + QString::number(d->off, 16).toUpper();
        m[u"min"_s] = d->min;
        QVariant value;
        QString kindName = u"number"_s;
        qint64 max = d->max;
        switch (d->type) {
        case Type::Str:
            value = readText(at + d->off, d->size);
            kindName = u"text"_s;
            max = d->size - 1;
            break;
        case Type::Bool: value = m_data.at(at + d->off) != 0; kindName = u"bool"_s; break;
        case Type::Palette: value = static_cast<int>(static_cast<quint8>(m_data.at(at + d->off))); break;
        default: {
            const bool isSigned = d->type == Type::I16 || d->type == Type::I32;
            value = static_cast<double>(readNum(at + d->off, d->size, d->big, isSigned));
            if (max <= 0)
                max = d->type == Type::U8 ? 255 : d->type == Type::U16 ? 65535 : d->type == Type::I16 ? 32767 : 2147483647LL;
            if (d->type == Type::I32)
                kindName = u"big"_s;
        }
        }
        m[u"value"_s] = value;
        m[u"max"_s] = static_cast<double>(max);
        if (d->list) {
            const QVariantList choices = choicesFor(kind, at, QString::fromLatin1(d->list), value);
            if (!choices.isEmpty()) {
                kindName = d->type == Type::Palette ? u"palette"_s : u"choice"_s;
                m[u"choices"_s] = choices;
            }
        }
        m[u"kind"_s] = kindName;
        out.append(m);
    }
    return out;
}

QVariantList Rll3SaveEditorManager::choicesFor(const QString& kind, qsizetype at, const QString& list,
                                               const QVariant& value) const {
    Q_UNUSED(kind)
    Q_UNUSED(at)
    QVariantList out;
    if (list == u"emblems"_s) {
        for (const Rll3Lists::TextChoice& c : Rll3Lists::emblems())
            out << QVariantMap{{u"name"_s, QString::fromLatin1(c.name)}, {u"value"_s, QString::fromLatin1(c.value)}};
        const QString v = value.toString();
        if (std::none_of(out.cbegin(), out.cend(), [&v](const QVariant& c) { return c.toMap().value(u"value"_s).toString() == v; }))
            out.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(v)}, {u"value"_s, v}});
        return out;
    }
    if (list.endsWith(u"Palette"_s))
        return paletteChoices(paletteFor(list));
    if (list == u"@surnames"_s) {
        const QStringList names = Rll3SaveLists::commentarySurnames();
        out = namedChoices(names);
        if (!out.isEmpty())
            out.last() = QVariantMap{{u"name"_s, names.last()}, {u"value"_s, kNoCommentary}};
    } else if (list == u"@contract"_s) {
        for (int y = 2015; y <= 2035; ++y)
            out << QVariantMap{{u"name"_s, QString::number(y)}, {u"value"_s, y}};
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}};
        if (value.toInt() == 65535)
            out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 65535}};
    } else if (list == u"countries"_s) {
        out = choiceList(Rll3Lists::countries());
    } else if (list == u"supporters"_s) {
        out = choiceList(Rll3Lists::supporters());
    } else if (list == u"stadiums"_s) {
        out = choiceList(Rll3SaveLists::stadiums());
    } else if (list == u"teamCommentary"_s) {
        out = choiceList(Rll3SaveLists::teamCommentary());
    } else if (list == u"roles"_s) {
        out = namedChoices(kRoleNames);
    } else if (list == u"perks"_s) {
        out = namedChoices({u"Speed"_s, u"Tackle"_s, u"Place Kicking"_s});
    } else if (list == u"teamTypes"_s) {
        out = namedChoices({u"Standard"_s, u"State Of Origin"_s, u"City V's Country"_s, u"Allstars"_s, u"International"_s});
    } else if (list == u"leftRight"_s) {
        out = namedChoices({u"Left"_s, u"Right"_s});
    } else if (list == u"soo"_s) {
        out = namedChoices({u"None"_s, u"Queensland"_s, u"New South Wales"_s});
    } else if (list == u"cvc"_s) {
        out = namedChoices({u"None"_s, u"City"_s, u"Country"_s});
    } else if (list == u"allStars"_s) {
        out = namedChoices({u"NRL All Stars"_s, u"Indigenous All Stars"_s});
    } else if (list == u"headgear"_s) {
        out = namedChoices({u"No Headgear"_s, u"Headband"_s, u"Headgear"_s});
    } else if (list == u"tucked"_s) {
        out = namedChoices({u"Out"_s, u"In"_s});
    } else if (list == u"socks"_s) {
        out = namedChoices({u"Up"_s, u"Down"_s});
    }
    if (out.isEmpty())
        return out;
    const int v = value.toInt();
    if (std::none_of(out.cbegin(), out.cend(), [v](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == v; }))
        out.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(v)}, {u"value"_s, v}});
    return out;
}

bool Rll3SaveEditorManager::setField(const QString& kind, int index, const QString& key, const QVariant& value) {
    const qsizetype at = recordAt(kind, index);
    const FieldDef* d = defFor(kind, key);
    if (at < 0 || !d || (d->flags & kReadOnly))
        return false;
    const QByteArray before = m_data.mid(at, recordSize(kind));
    switch (d->type) {
    case Type::Str: writeText(at + d->off, d->size, value.toString()); break;
    case Type::Bool: m_data[at + d->off] = value.toBool() ? 1 : 0; break;
    default: {
        qint64 v = static_cast<qint64>(value.toDouble());
        if (d->max > 0 && !(d->list && QLatin1StringView(d->list) == "@surnames"_L1) && !(d->list && QLatin1StringView(d->list) == "@contract"_L1))
            v = std::clamp<qint64>(v, d->min, d->max);
        writeNum(at + d->off, d->size, d->big, v);
    }
    }
    if (m_data.mid(at, recordSize(kind)) == before)
        return false;
    if (key == u"logo"_s)
        m_logoUrls.clear();
    edited();
    return true;
}

// =================================================================================================================
// Roster and lineup
// =================================================================================================================

QList<int> Rll3SaveEditorManager::rosterIds(int team) const {
    QList<int> out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qMin<int>(rosterMax(), static_cast<int>(readNum(at + kPlayerAmount, 2, true, false)));
    for (int i = 0; i < count; ++i)
        out << static_cast<int>(readNum(at + kPlayers + 4 * i, 4, true, true));
    return out;
}

QVariantList Rll3SaveEditorManager::rosterRows(int team) const {
    QVariantList out;
    const QList<int> ids = rosterIds(team);
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap m = playerInfo(ids.at(i));
        m[u"slot"_s] = i;
        out.append(m);
    }
    return out;
}

bool Rll3SaveEditorManager::addRosterPlayer(int team, int playerId) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || playerId <= 0)
        return false;
    const QList<int> ids = rosterIds(team);
    if (ids.size() >= rosterMax()) {
        setStatus(false, u"The roster is full (%1 players, the most a team can have)."_s.arg(rosterMax()));
        return false;
    }
    if (ids.contains(playerId)) {
        setStatus(false, u"That player is already in the roster."_s);
        return false;
    }
    writeNum(at + kPlayers + 4 * ids.size(), 4, true, playerId);
    writeNum(at + kPlayerAmount, 2, true, ids.size() + 1);
    edited();
    return true;
}

bool Rll3SaveEditorManager::removeRosterPlayer(int team, int slot) {
    const qsizetype at = recordAt(u"team"_s, team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || slot < 0 || slot >= ids.size())
        return false;
    ids.removeAt(slot);
    for (int i = 0; i < rosterMax(); ++i)
        writeNum(at + kPlayers + 4 * i, 4, true, i < ids.size() ? ids.at(i) : 0);
    writeNum(at + kPlayerAmount, 2, true, ids.size());
    edited();
    return true;
}

bool Rll3SaveEditorManager::moveRosterPlayer(int team, int slot, int delta) {
    const qsizetype at = recordAt(u"team"_s, team);
    QList<int> ids = rosterIds(team);
    const int to = slot + delta;
    if (at < 0 || slot < 0 || slot >= ids.size() || to < 0 || to >= ids.size())
        return false;
    ids.swapItemsAt(slot, to);
    writeNum(at + kPlayers + 4 * slot, 4, true, ids.at(slot));
    writeNum(at + kPlayers + 4 * to, 4, true, ids.at(to));
    edited();
    return true;
}

bool Rll3SaveEditorManager::sortRoster(int team) {
    const qsizetype at = recordAt(u"team"_s, team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || ids.size() < 2)
        return false;
    QHash<int, QString> keys;
    for (int id : std::as_const(ids)) {
        const QVariantMap m = playerInfo(id);
        keys.insert(id, m.value(u"last"_s).toString() + u' ' + m.value(u"first"_s).toString());
    }
    const QList<int> before = ids;
    std::stable_sort(ids.begin(), ids.end(), [&keys](int a, int b) { return keys.value(a).compare(keys.value(b), Qt::CaseInsensitive) < 0; });
    if (ids == before)
        return false;
    for (int i = 0; i < ids.size(); ++i)
        writeNum(at + kPlayers + 4 * i, 4, true, ids.at(i));
    edited();
    return true;
}

bool Rll3SaveEditorManager::databasePlayersAvailable() const {
    return !m_originals.isEmpty();
}

QVariantList Rll3SaveEditorManager::pickablePlayers(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const auto matches = [&f](const QVariantMap& m) {
        return f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive) || m.value(u"id"_s).toString() == f;
    };
    const int count = playerCount();
    for (int i = 0; i < count; ++i) {
        const QVariantMap m = playerInfo(static_cast<int>(readNum(recordAt(u"player"_s, i), 4, true, true)));
        if (matches(m))
            out.append(m);
    }
    for (const Original& o : m_originals) {
        if (out.size() >= 400)
            break;
        const QVariantMap m = playerInfo(o.id);
        if (m.value(u"custom"_s).toBool())
            continue;
        if (matches(m))
            out.append(m);
    }
    return out;
}

QVariantList Rll3SaveEditorManager::matchLists(int team) const {
    Q_UNUSED(team)
    return {QVariantMap{{u"list"_s, u"standard"_s}, {u"title"_s, u"Lineup"_s}, {u"shirts"_s, false}}};
}

bool Rll3SaveEditorManager::listPresent(int team, const QString& list) const {
    Q_UNUSED(list)
    return recordAt(u"team"_s, team) >= 0;
}

bool Rll3SaveEditorManager::setListPresent(int team, const QString& list, bool on) {
    Q_UNUSED(team)
    Q_UNUSED(list)
    if (!on)
        setStatus(false, u"Every team has its lineup."_s);
    return false;
}

QVariantMap Rll3SaveEditorManager::lineupRow(int id, bool present) const {
    QVariantMap m = playerInfo(present ? id : 0);
    QStringList roles;
    for (const QVariant& r : m.value(u"roles"_s).toList())
        roles << kRoleShort.value(r.toInt());
    m[u"roles"_s] = roles.join(u" / "_s);
    m[u"present"_s] = present;
    return m;
}

QVariantList Rll3SaveEditorManager::lineupRows(int team, const QString& list) const {
    Q_UNUSED(list)
    QVariantList rows;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return rows;
    for (int i = 0; i < kPositions.size(); ++i) {
        QVariantMap row = lineupRow(static_cast<int>(readNum(at + kLineups + 4 * i, 4, true, true)), true);
        row[u"key"_s] = u"p:%1"_s.arg(i);
        row[u"position"_s] = kPositions.at(i);
        rows << row;
    }
    return rows;
}

QVariantList Rll3SaveEditorManager::teamRoles(int team, const QString& list) const {
    Q_UNUSED(list)
    QVariantList rows;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return rows;
    for (int r = 0; r < kTeamRoles.size(); ++r) {
        QVariantMap row = lineupRow(static_cast<int>(readNum(at + kRoles + 4 * r, 4, true, true)), true);
        row[u"key"_s] = u"r:%1"_s.arg(r);
        row[u"name"_s] = kTeamRoles.at(r);
        rows << row;
    }
    return rows;
}

bool Rll3SaveEditorManager::setLineupSlot(int team, const QString& list, const QString& key, int playerId) {
    Q_UNUSED(list)
    const qsizetype at = recordAt(u"team"_s, team);
    const QStringList parts = key.split(u':');
    if (at < 0 || parts.size() != 2 || playerId < 0)
        return false;
    const int i = parts.at(1).toInt();
    qsizetype p = -1;
    if (parts.at(0) == u"p"_s && i >= 0 && i < 17)
        p = at + kLineups + 4 * i;
    else if (parts.at(0) == u"r"_s && i >= 0 && i < 4)
        p = at + kRoles + 4 * i;
    if (p < 0 || readNum(p, 4, true, true) == playerId)
        return false;
    writeNum(p, 4, true, playerId);
    edited();
    return true;
}

QVariantList Rll3SaveEditorManager::pickerRows(const QString& filter, int team, bool squadOnly) const {
    QVariantList out;
    const auto toRow = [](const QVariantMap& m) {
        return QVariantMap{{u"id"_s, m.value(u"id"_s)}, {u"first"_s, m.value(u"first"_s)}, {u"last"_s, m.value(u"last"_s)},
                           {u"clubName"_s, m.value(u"club"_s)}, {u"roleName"_s, m.value(u"role"_s)},
                           {u"age"_s, m.value(u"age"_s)}, {u"rating"_s, m.value(u"rating"_s)}};
    };
    if (squadOnly) {
        const QString f = filter.trimmed();
        for (int id : rosterIds(team)) {
            const QVariantMap m = playerInfo(id);
            if (f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive) || QString::number(id) == f)
                out << toRow(m);
        }
        return out;
    }
    for (const QVariant& v : pickablePlayers(filter))
        out << toRow(v.toMap());
    return out;
}

// The C# editor's Generate Best Lineup: the roster by rating, each position from the players whose primary role
// (else secondary, else tertiary) fits; captain = best rated, goal kicker / play makers = the next best halves.
QString Rll3SaveEditorManager::generateBestLineup(int team) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return u"No team."_s;
    struct P {
        int id, rating, roles[3];
    };
    QList<P> players;
    for (int id : rosterIds(team)) {
        const QVariantMap m = playerInfo(id);
        const QVariantList roles = m.value(u"roles"_s).toList();
        players << P{id, m.value(u"rating"_s).toInt(), {roles.value(0, -1).toInt(), roles.value(1, -1).toInt(), roles.value(2, -1).toInt()}};
    }
    if (players.isEmpty())
        return u"The roster is empty."_s;
    std::stable_sort(players.begin(), players.end(), [](const P& a, const P& b) { return a.rating > b.rating; });
    const auto byRole = [&players](int role) {
        for (int which = 0; which < 3; ++which) {
            QList<int> ids;
            for (const P& p : std::as_const(players))
                if (p.roles[which] == role)
                    ids << p.id;
            if (!ids.isEmpty())
                return ids;
        }
        return QList<int>();
    };
    // Slot <- (role, n-th best), as the C# editor fills them (interchanges: props 3 and 4, second rowers 3 and 4).
    const int map[17][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {1, 1}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {5, 1}, {7, 0},
                            {7, 1}, {8, 0}, {5, 2}, {5, 3}, {7, 2}, {7, 3}};
    QStringList missing;
    QList<int> chosen;
    for (int i = 0; i < 17; ++i) {
        const QList<int> ids = byRole(map[i][0]);
        const int id = map[i][1] < ids.size() ? ids.at(map[i][1]) : 0;
        if (id) {
            writeNum(at + kLineups + 4 * i, 4, true, id);
            chosen << id;
        } else {
            missing << kPositions.at(i).section(u" - "_s, 1);
        }
    }
    // Roles (the C# editor's): captain the best rated of the lineup, goal kicker and play maker 1 the best footwork,
    // play maker 2 the next best footwork.
    if (!chosen.isEmpty()) {
        QList<int> byRating = chosen;
        std::stable_sort(byRating.begin(), byRating.end(), [this](int a, int b) { return ratingOfId(a) > ratingOfId(b); });
        QList<int> byFootwork = byRating;
        std::stable_sort(byFootwork.begin(), byFootwork.end(), [this](int a, int b) { return footworkOfId(a) > footworkOfId(b); });
        writeNum(at + kRoles, 4, true, byRating.first());
        writeNum(at + kRoles + 4, 4, true, byFootwork.first());
        writeNum(at + kRoles + 8, 4, true, byFootwork.first());
        writeNum(at + kRoles + 12, 4, true, byFootwork.value(1, byFootwork.first()));
    }
    edited();
    return missing.isEmpty() ? QString() : u"Not enough players for: %1 (those places weren't changed)."_s.arg(missing.join(u", "_s));
}

// =================================================================================================================
// Jerseys
// =================================================================================================================

qsizetype Rll3SaveEditorManager::jerseyAt(int team, int uniform) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || uniform < 0 || uniform >= kMaxJerseys)
        return -1;
    return at + kJerseyBase + qsizetype(uniform) * kJerseyStride;
}

QVariantMap Rll3SaveEditorManager::jerseys(int team) const {
    QVariantMap out{{u"ok"_s, false}};
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qBound(0, static_cast<int>(static_cast<quint8>(m_data.at(at + kJerseyAmount))), kMaxJerseys);
    QVariantList uniforms;
    for (int u = 0; u < count; ++u)
        uniforms << QVariantMap{{u"index"_s, u}, {u"name"_s, readText(jerseyAt(team, u), 25)}, {u"hidden"_s, false}};
    out.insert(u"ok"_s, true);
    out.insert(u"count"_s, count);
    out.insert(u"max"_s, kMaxJerseys);
    out.insert(u"uniforms"_s, uniforms);
    out.insert(u"slots"_s, QVariantList());
    return out;
}

QVariantList Rll3SaveEditorManager::jerseyFields(int team, int uniform) const {
    QVariantList out;
    const qsizetype base = jerseyAt(team, uniform);
    if (base < 0)
        return out;
    const int maker = static_cast<quint8>(m_data.at(base + findDef(jerseyDefs(), u"manufactureId"_s)->off));
    for (const FieldDef* d : jerseyDefs()) {
        const QString key = QString::fromLatin1(d->key);
        QVariantMap m;
        m[u"key"_s] = u"j:%1:%2"_s.arg(uniform).arg(key);
        m[u"label"_s] = QString::fromLatin1(d->label);
        m[u"group"_s] = QString::fromLatin1(d->group);
        m[u"min"_s] = 0;
        m[u"offset"_s] = u"+0x"_s + QString::number(base - recordAt(u"team"_s, team) + d->off, 16).toUpper();
        const int number = d->type == Type::Str ? 0 : static_cast<int>(readNum(base + d->off, d->size, d->big, false));
        if (key == u"manufactureId"_s) {
            QVariantList makers = choiceList(Rll3Lists::manufacturers());
            if (std::none_of(makers.cbegin(), makers.cend(), [maker](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == maker; }))
                makers.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(maker)}, {u"value"_s, maker}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = makers;
            m[u"value"_s] = maker;
            m[u"refresh"_s] = true;
        } else if (key == u"licensedId"_s) {
            QVariantList jerseys{QVariantMap{{u"name"_s, u"Custom Jersey"_s}, {u"value"_s, 0}}};
            jerseys << choiceList(Rll3Lists::licensedJerseys(maker));
            if (std::none_of(jerseys.cbegin(), jerseys.cend(), [number](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == number; }))
                jerseys.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(number)}, {u"value"_s, number}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = jerseys;
            m[u"value"_s] = number;
        } else if (d->type == Type::Str) {
            m[u"kind"_s] = u"text"_s;
            m[u"value"_s] = readText(base + d->off, d->size);
            m[u"max"_s] = d->size - 1;
        } else if (d->type == Type::Bool) {
            m[u"kind"_s] = u"bool"_s;
            m[u"value"_s] = number != 0;
        } else if (d->type == Type::Palette) {
            m[u"kind"_s] = u"palette"_s;
            m[u"choices"_s] = paletteChoices(paletteFor(QString::fromLatin1(d->list)));
            m[u"value"_s] = number;
        } else {
            m[u"kind"_s] = u"number"_s;
            m[u"value"_s] = number;
            m[u"max"_s] = d->max > 0 ? d->max : 255;
        }
        out.append(m);
    }
    return out;
}

bool Rll3SaveEditorManager::setJerseyField(int team, const QString& key, const QVariant& value) {
    const QStringList parts = key.split(u':');
    if (parts.size() != 3 || parts.at(0) != u"j"_s)
        return false;
    const qsizetype base = jerseyAt(team, parts.at(1).toInt());
    const FieldDef* d = findDef(jerseyDefs(), parts.at(2));
    if (base < 0 || !d)
        return false;
    const QByteArray before = m_data.mid(base, kJerseyStride);
    if (d->type == Type::Str) {
        writeText(base + d->off, d->size, value.toString());
    } else if (d->type == Type::Bool) {
        m_data[base + d->off] = value.toBool() ? 1 : 0;
    } else {
        writeNum(base + d->off, d->size, d->big, static_cast<qint64>(value.toDouble()));
        if (parts.at(2) == u"manufactureId"_s)
            writeNum(base + findDef(jerseyDefs(), u"licensedId"_s)->off, 2, false, 0);   // Its custom jersey.
    }
    if (m_data.mid(base, kJerseyStride) == before)
        return false;
    edited();
    return true;
}

int Rll3SaveEditorManager::addJersey(int team, int from) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return -1;
    const int count = static_cast<quint8>(m_data.at(at + kJerseyAmount));
    if (count >= kMaxJerseys) {
        setStatus(false, u"This team already has %1 jerseys, the most it can have."_s.arg(kMaxJerseys));
        return -1;
    }
    if (from < 0 || from >= count)
        from = 0;
    m_data.replace(jerseyAt(team, count), kJerseyStride, m_data.mid(jerseyAt(team, from), kJerseyStride));
    writeText(jerseyAt(team, count), 25, u"JERSEY %1"_s.arg(count + 1));
    m_data[at + kJerseyAmount] = static_cast<char>(count + 1);
    edited();
    setStatus(true, u"Added a jersey (a copy of jersey %1)."_s.arg(from + 1));
    return count;
}

bool Rll3SaveEditorManager::removeJersey(int team, int uniform) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    const int count = static_cast<quint8>(m_data.at(at + kJerseyAmount));
    if (count <= 1 || uniform < 0 || uniform >= count) {
        setStatus(false, u"A team needs at least one jersey."_s);
        return false;
    }
    for (int u = uniform; u + 1 < count; ++u)
        m_data.replace(jerseyAt(team, u), kJerseyStride, m_data.mid(jerseyAt(team, u + 1), kJerseyStride));
    m_data[at + kJerseyAmount] = static_cast<char>(count - 1);
    edited();
    return true;
}

// =================================================================================================================
// Records
// =================================================================================================================

// A record from a file (a .tem / .ply, or the editor's templates): either save version's size, made this one's
// (a version 2 record has one more byte at the end).
static QByteArray fitRecord(QByteArray record, int size, bool team) {
    if (record.size() == size + 1)
        record.chop(1);
    else if (record.size() == size - 1)
        record.append(team ? '\x01' : '\0');
    return record;
}

int Rll3SaveEditorManager::addRecord(const QString& kind, bool female) {
    Q_UNUSED(female)
    if (!m_loaded)
        return -1;
    const bool team = kind == u"team"_s;
    const int count = team ? teamCount() : playerCount();
    const int max = team ? maxTeams() : maxPlayers();
    if (count >= max) {
        setStatus(false, u"The save already has %1 %2, the most it can hold."_s.arg(count).arg(team ? u"teams"_s : u"players"_s));
        return -1;
    }
    const QString name = team ? u"NewTeam.tem"_s : u"NewPlayer.ply"_s;
    QString path = QString::fromLatin1(kResourceDir) + name;
    if (const QByteArray dir = qgetenv("RLL3_SAVE_RESOURCES"); !dir.isEmpty())
        path = QDir(QString::fromLocal8Bit(dir)).filePath(name);
    QFile f(path);
    const int size = recordSize(kind);
    const QByteArray record = fitRecord(f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(), size, team);
    if (record.size() != size) {
        setStatus(false, u"The new %1 template (%2) is missing."_s.arg(team ? u"team"_s : u"player"_s, name));
        return -1;
    }
    const qsizetype table = team ? teamTable() : playerTable();
    const qsizetype at = table + qsizetype(count) * size;
    m_data.replace(at, size, record);
    int id = (team ? 1000 : 4000) + count;
    const auto used = [&](int candidate) {
        for (int i = 0; i < count; ++i)
            if (readNum(table + qsizetype(i) * size, 4, true, true) == candidate)
                return true;
        return !team && m_originalIndex.contains(candidate);
    };
    while (used(id))
        ++id;
    writeNum(at, 4, true, id);
    if (team)
        m_data[kTeamCountAt] = static_cast<char>(count + 1);
    else
        writeNum(kPlayerCountAt[m_variant], 2, true, count + 1);
    edited();
    setStatus(true, u"Added a new %1 (id %2)."_s.arg(team ? u"team"_s : u"player"_s).arg(id));
    return count;
}

bool Rll3SaveEditorManager::deleteRecord(const QString& kind, int index) {
    const bool team = kind == u"team"_s;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return false;
    const int count = team ? teamCount() : playerCount();
    const int size = recordSize(kind);
    const qsizetype end = (team ? teamTable() : playerTable()) + qsizetype(count) * size;
    m_data.replace(at, end - size - at, m_data.mid(at + size, end - at - size));
    if (team)
        m_data[kTeamCountAt] = static_cast<char>(count - 1);
    else
        writeNum(kPlayerCountAt[m_variant], 2, true, count - 1);
    edited();
    setStatus(true, u"Deleted the %1. Teams that had them in their roster or lineup still list the id."_s.arg(team ? u"team"_s : u"player"_s));
    return true;
}

QString Rll3SaveEditorManager::importRecord(const QString& kind, int index, const QString& pathOrUrl) {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Pick a %1 first."_s.arg(kind);
    QFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::ReadOnly))
        return u"Couldn't open %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    const int size = recordSize(kind);
    const QByteArray record = fitRecord(f.readAll(), size, kind == u"team"_s);
    if (record.size() != size)
        return u"%1 isn't a Rugby League Live 3 %2 (%3 bytes)."_s.arg(QFileInfo(f).fileName(), kind).arg(record.size());
    const qint64 id = readNum(at, 4, true, true);
    m_data.replace(at, size, record);
    writeNum(at, 4, true, id);
    m_logoUrls.clear();
    edited();
    setStatus(true, u"Imported %1."_s.arg(QFileInfo(f).fileName()));
    return {};
}

QString Rll3SaveEditorManager::exportRecord(const QString& kind, int index, const QString& pathOrUrl) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Nothing to export."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data.mid(at, recordSize(kind))) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QString Rll3SaveEditorManager::recordFileName(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return {};
    if (kind == u"team"_s)
        return fileSafe((readText(at + offOf(kind, "locationName"), 22) + u' ' + readText(at + offOf(kind, "clubName"), 16)).trimmed()) + u".tem"_s;
    return fileSafe(playerInfo(static_cast<int>(readNum(at, 4, true, true))).value(u"name"_s).toString()) + u".ply"_s;
}

QString Rll3SaveEditorManager::exportDecoded(const QString& pathOrUrl) const {
    if (!m_loaded)
        return u"Nothing loaded."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QVariantList Rll3SaveEditorManager::rawRows(const QString& kind, int index) const {
    QVariantList out;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return out;
    const int size = recordSize(kind);
    QVector<bool> known(size, false);
    const auto mark = [&known, size](int off, int n) {
        for (int i = 0; i < n && off >= 0 && off + i < size; ++i)
            known[off + i] = true;
    };
    for (const FieldDef* d : defsFor(kind))
        mark(d->off, d->size);
    if (kind == u"team"_s) {
        for (int u = 0; u < kMaxJerseys; ++u)
            for (const FieldDef* d : jerseyDefs())
                mark(kJerseyBase + u * kJerseyStride + d->off, d->size);
        mark(kJerseyAmount, 1);
        mark(kPlayerAmount, 2 + 4 * rosterMax());
        mark(kLineupAmount, 2 + 4 * 17 + 4 * 4);
    }
    for (int row = 0; row < size; row += 16) {
        QVariantList hex;
        QVariantList marks;
        QString text;
        for (int i = row; i < qMin(row + 16, size); ++i) {
            const quint8 c = static_cast<quint8>(m_data.at(at + i));
            hex.append(QString::number(c, 16).rightJustified(2, u'0').toUpper());
            marks.append(known[i]);
            text += (c >= 32 && c < 127) ? QChar(c) : QChar(u'.');
        }
        out.append(QVariantMap{{u"offset"_s, row}, {u"hex"_s, hex}, {u"known"_s, marks}, {u"text"_s, text}});
    }
    return out;
}

bool Rll3SaveEditorManager::setRawByte(const QString& kind, int index, int offset, int value) {
    const qsizetype at = recordAt(kind, index);
    if (at < 0 || offset < 0 || offset >= recordSize(kind) || value < 0 || value > 255)
        return false;
    if (static_cast<quint8>(m_data.at(at + offset)) == value)
        return true;
    m_data[at + offset] = static_cast<char>(value);
    edited();
    return true;
}

// =================================================================================================================
// Raw Databases (the C# editor's Raw ... windows: a table of every record, edited as text, CSV in / out)
// =================================================================================================================

namespace {

struct RawCol {
    const char* name;
    const char* key;    // A field key, "#index" or "#<list cell>".
};

// The leagues and referees (the C# editor's ReadLeagueData / ReadRefData), save versions 1 and 2.
constexpr qsizetype kRawLeagueCountAt[2] = {598647, 598891};   // u8
constexpr qsizetype kRawLeaguesAt[2] = {598762, 599018};
constexpr int kRawLeagueSize = 228;     // u8 id, i32 reserve, name[52], short name[8], type id[14], i32 count, 36 i32, u8 type
constexpr int kRawLeagueTeams = 36;
constexpr qsizetype kRawRefCountAt[2] = {627848, 628232};      // u16
constexpr qsizetype kRawRefsAt[2] = {631696, 632092};
constexpr int kRawRefSize[2] = {374, 375};   // i32 id, first[20], last[20], u8, u8 commentary, the rest

const QList<RawCol> kRawPlayerCols = {
    {"Player Index", "#index"}, {"Player ID", "id"}, {"First Name", "firstName"}, {"Last Name", "lastName"},
    {"DOB - Day", "dob.day"}, {"DOB - Month", "dob.month"}, {"DOB - Year", "dob.year"},
    {"Jersey Number", "jerseyNumber"}, {"Jersey Name", "jerseyName"}, {"Commentary Surname", "commentarySurname"},
    {"Commentary FullName", "commentaryFullName"}, {"Primary Role", "primaryRole"},
    {"Secondary Role", "secondaryRole"}, {"Tertiary Role", "tertiaryRole"}, {"State Of Origin", "stateOfOrigin"},
    {"City V's Country", "cityVsCountry"}, {"All Stars", "allStars"}, {"Preferred Hand", "preferredHand"},
    {"Preferred Foot", "preferredFoot"}, {"Weak Foot Rating", "weakFootRating"},
    {"Representative Country", "repCountry"}, {"Country Of Birth", "countryOfBirth"},
    {"Contract Expiry", "contractExpiry"}, {"Height", "appearance.height"}, {"Weight", "appearance.weight"},
    {"Reputation", "attributes.reputation"}, {"Ego", "attributes.ego"}, {"Loyalty", "attributes.loyalty"},
    {"Speed", "technicalAbility.speed"}, {"Strength", "technicalAbility.strength"},
    {"Technique", "technicalAbility.technique"}, {"Footwork", "technicalAbility.footwork"},
    {"Agility", "technicalAbility.agility"}, {"Fitness", "technicalAbility.fitness"},
    {"Strategy", "technicalAbility.strategy"}, {"Perks", "attributes.perk"},
};

const QList<RawCol> kRawAppearanceCols = {
    {"Player Index", "#index"}, {"Player ID", "id"}, {"First Name", "firstName"}, {"Last Name", "lastName"},
    {"Head Accessory Model", "appearance.headAccessoryModelId"}, {"Headgear Pattern", "appearance.patternId"},
    {"Headgear Pattern Colour1", "appearance.patternColour1"}, {"Headgear Pattern Colour2", "appearance.patternColour2"},
    {"Headgear Pattern Colour3", "appearance.patternColour3"}, {"Headgear Pattern Colour4", "appearance.patternColour4"},
    {"Left Upper Arm Strapping", "appearance.strappingLeftUpperArm"},
    {"Left Lower Arm Strapping", "appearance.strappingLeftLowerArm"},
    {"Left Hand Strapping", "appearance.strappingLeftHand"},
    {"Right Upper Arm Strapping", "appearance.strappingRightUpperArm"},
    {"Right Lower Arm Strapping", "appearance.strappingRightLowerArm"},
    {"Right Hand Strapping", "appearance.strappingRightHand"}, {"Left Leg Strapping", "appearance.strappingLeftLeg"},
    {"Right Leg Strapping", "appearance.strappingRightLeg"}, {"Compression Shorts", "appearance.compressionShorts"},
    {"Compression Colour", "appearance.compressionShortsColour"}, {"Shirt Tucked", "appearance.shitTucked"},
    {"Socks Height", "appearance.socksHeight"}, {"Boots Model", "appearance.bootsModelId"},
    {"Boots Colour1", "appearance.bootsColour1"}, {"Boots Colour2", "appearance.bootsColour2"},
    {"Boots Colour3", "appearance.bootsColour3"}, {"Boots Colour4", "appearance.bootsColour4"},
    {"Skin Tone", "appearance.skinColour"},
};

const QList<RawCol> kRawTeamCols = {
    {"Team Index", "#index"}, {"Team ID", "id"}, {"Team Type", "type"}, {"Team Type Additions", "typeAdditional"},
    {"Location Name", "locationName"}, {"Club Name", "clubName"}, {"Abbreviated Name", "abbreviatedName"},
    {"Emblem", "logo"}, {"Colour Set", "colourSet"}, {"Supporters", "supporters"}, {"Formation Year", "formation"},
    {"Commentary", "commentary"},
    {"Primary Ground", "stadium[0].id"}, {"Primary Ground isCustomName", "stadium[0].isCustomName"},
    {"Primary Ground Custom Name", "stadium[0].customName"},
    {"Secondary Ground", "stadium[1].id"}, {"Secondary Ground isCustomName", "stadium[1].isCustomName"},
    {"Secondary Ground Custom Name", "stadium[1].customName"},
    {"Tertiary Ground", "stadium[2].id"}, {"Tertiary Ground isCustomName", "stadium[2].isCustomName"},
    {"Tertiary Ground Custom Name", "stadium[2].customName"},
    {"Grand Final Ground", "stadium[3].id"}, {"Grand Final Ground isCustomName", "stadium[3].isCustomName"},
    {"Grand Final Ground Custom Name", "stadium[3].customName"},
};

// The C# editor's names for the lineup's places, in the order the game keeps them.
const QStringList kRawLineupNames = {u"1 - Fullback"_s, u"5 - LWing"_s, u"4 - LCentre"_s, u"3 - RCentre"_s,
                                     u"2 - RWing"_s, u"6 - Five-Eighth"_s, u"7 - Halfback"_s, u" 8 - Prop"_s,
                                     u"9 - Hooker"_s, u"10 - FrontRow"_s, u"11 - Second Row 1"_s,
                                     u"12 - Second Row 2"_s, u"13 - Lock"_s, u"14 - Int1"_s, u"15 - Int2"_s,
                                     u"16 - Int3"_s, u"17 - Int4"_s};

QList<RawColumn> rawColumns(const QList<RawCol>& cols) {
    QList<RawColumn> out;
    for (const RawCol& c : cols)
        out << RawColumn{QString::fromLatin1(c.name), QString::fromLatin1(c.key), c.key[0] == '#'};
    return out;
}

QList<RawColumn> rawColumnsFor(const QString& id) {
    QList<RawColumn> cols;
    if (id == u"players"_s)
        return rawColumns(kRawPlayerCols);
    if (id == u"appearance"_s)
        return rawColumns(kRawAppearanceCols);
    if (id == u"teams"_s)
        return rawColumns(kRawTeamCols);
    if (id == u"lineup"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Team ID", "id"}, {"Location Name", "locationName"},
                           {"Club Name", "clubName"}, {"Team Captain ID", "#role0"}, {"Team GoalKicker ID", "#role1"},
                           {"Team PlayMaker1 ID", "#role2"}, {"Team PlayMaker2 ID", "#role3"},
                           {"Team Lineup Amount", "#lineupAmount"}});
        for (int j = 0; j < 17; ++j)
            cols << RawColumn{kRawLineupNames.at(j), u"#lineup%1"_s.arg(j)};
    } else if (id == u"teamPlayers"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Team ID", "id"}, {"Location Name", "locationName"},
                           {"Club Name", "clubName"}, {"Team Player Amount", "#playerAmount"}});
        for (int j = 0; j < 64; ++j)
            cols << RawColumn{u"Player %1"_s.arg(j + 1), u"#player%1"_s.arg(j)};
    } else if (id == u"leagues"_s) {
        cols = rawColumns({{"League Index", "#index"}, {"League ID", "#id"}, {"League Reserve", "#reserve"},
                           {"League Name", "#name"}, {"League Short Name", "#shortName"},
                           {"League Type ID", "#typeId"}, {"League Type", "#type"},
                           {"League Team Amount", "#teamAmount"}});
        for (int j = 0; j < kRawLeagueTeams; ++j)
            cols << RawColumn{u"Team %1"_s.arg(j + 1), u"#team%1"_s.arg(j)};
    } else if (id == u"referees"_s) {
        cols = rawColumns({{"Ref Index", "#index"}, {"Ref ID", "#id"}, {"Ref First Name", "#firstName"},
                           {"Ref Last Name", "#lastName"}, {"Ref Commentary", "#commentary"}});
    }
    for (RawColumn& c : cols)
        c.readOnly = c.key == u"#index"_s;
    return cols;
}

QString rawKindOf(const QString& id) {
    if (id == u"players"_s || id == u"appearance"_s)
        return u"player"_s;
    if (id == u"leagues"_s)
        return u"league"_s;
    if (id == u"referees"_s)
        return u"referee"_s;
    return u"team"_s;
}

} // namespace

QVariantList Rll3SaveEditorManager::rawTables() const {
    const auto t = [](const char* id, const char* title) {
        return QVariantMap{{u"id"_s, QString::fromLatin1(id)}, {u"title"_s, QString::fromLatin1(title)}, {u"repair"_s, false}};
    };
    return {t("leagues", "Raw League Database"), t("players", "Raw Player Database"),
            t("appearance", "Raw Player Appearance Database"), t("referees", "Raw Referee Database"),
            t("teams", "Raw Team Database"), t("lineup", "Raw Team Lineup"), t("teamPlayers", "Raw Team Players")};
}

int Rll3SaveEditorManager::rawCount(const QString& kind) const {
    if (!m_loaded)
        return 0;
    if (kind == u"league"_s)
        return static_cast<quint8>(m_data.at(kRawLeagueCountAt[m_variant]));
    if (kind == u"referee"_s)
        return static_cast<int>(readNum(kRawRefCountAt[m_variant], 2, true, false));
    return kind == u"team"_s ? teamCount() : playerCount();
}

qsizetype Rll3SaveEditorManager::rawAt(const QString& kind, int index) const {
    if (kind == u"team"_s || kind == u"player"_s)
        return recordAt(kind, index);
    if (index < 0 || index >= rawCount(kind))
        return -1;
    const qsizetype at = kind == u"league"_s ? kRawLeaguesAt[m_variant] + qsizetype(index) * kRawLeagueSize
                                             : kRawRefsAt[m_variant] + qsizetype(index) * kRawRefSize[m_variant];
    return at + (kind == u"league"_s ? kRawLeagueSize : kRawRefSize[m_variant]) <= m_data.size() ? at : -1;
}

// A cell that isn't a record field: where it is, its size (0 = text of `*max` bytes) and the biggest number.
bool Rll3SaveEditorManager::rawCellAt(const QString& kind, const QString& key, qsizetype* off, int* size, qint64* max) const {
    const auto numbered = [&key](const QString& name, int count, int* j) {
        if (!key.startsWith(name))
            return false;
        bool ok = false;
        *j = key.mid(name.size()).toInt(&ok);
        return ok && *j >= 0 && *j < count;
    };
    int j = 0;
    if (kind == u"team"_s) {
        if (key == u"#playerAmount"_s) { *off = kPlayerAmount; *size = 2; *max = 64; return true; }
        if (key == u"#lineupAmount"_s) { *off = kLineupAmount; *size = 2; *max = 17; return true; }
        if (numbered(u"#player"_s, 64, &j)) { *off = kPlayers + 4 * j; *size = 4; *max = INT32_MAX; return true; }
        if (numbered(u"#lineup"_s, 17, &j)) { *off = kLineups + 4 * j; *size = 4; *max = INT32_MAX; return true; }
        if (numbered(u"#role"_s, 4, &j)) { *off = kRoles + 4 * j; *size = 4; *max = INT32_MAX; return true; }
        return false;
    }
    if (kind == u"league"_s) {
        if (key == u"#id"_s) { *off = 0; *size = 1; *max = 255; return true; }
        if (key == u"#reserve"_s) { *off = 1; *size = 4; *max = INT32_MAX; return true; }
        if (key == u"#name"_s) { *off = 5; *size = 0; *max = 52; return true; }
        if (key == u"#shortName"_s) { *off = 57; *size = 0; *max = 8; return true; }
        if (key == u"#typeId"_s) { *off = 65; *size = 0; *max = 14; return true; }
        if (key == u"#teamAmount"_s) { *off = 79; *size = 4; *max = kRawLeagueTeams; return true; }
        if (numbered(u"#team"_s, kRawLeagueTeams, &j)) { *off = 83 + 4 * j; *size = 4; *max = INT32_MAX; return true; }
        if (key == u"#type"_s) { *off = 83 + 4 * kRawLeagueTeams; *size = 1; *max = 255; return true; }
        return false;
    }
    if (kind == u"referee"_s) {
        if (key == u"#id"_s) { *off = 0; *size = 4; *max = INT32_MAX; return true; }
        if (key == u"#firstName"_s) { *off = 4; *size = 0; *max = 20; return true; }
        if (key == u"#lastName"_s) { *off = 24; *size = 0; *max = 20; return true; }
        if (key == u"#commentary"_s) { *off = 45; *size = 1; *max = 255; return true; }
    }
    return false;
}

QString Rll3SaveEditorManager::rawGet(const QString& kind, int index, const QString& key) const {
    const qsizetype at = rawAt(kind, index);
    if (at < 0)
        return {};
    if (key == u"#index"_s)
        return QString::number(index);
    qsizetype off = 0;
    int size = 0;
    qint64 max = 0;
    if (rawCellAt(kind, key, &off, &size, &max)) {
        if (size == 0)
            return readText(at + off, static_cast<int>(max));
        return QString::number(readNum(at + off, size, true, size == 4));
    }
    const FieldDef* d = defFor(kind, key);
    if (!d)
        return {};
    switch (d->type) {
    case Type::Str: return readText(at + d->off, d->size);
    case Type::Bool: return RawTableModel::boolText(m_data.at(at + d->off) != 0);
    case Type::Palette: return QString::number(static_cast<quint8>(m_data.at(at + d->off)));
    default: return QString::number(readNum(at + d->off, d->size, d->big, d->type == Type::I16 || d->type == Type::I32));
    }
}

bool Rll3SaveEditorManager::rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error) {
    const qsizetype at = rawAt(kind, index);
    if (at < 0)
        return false;
    bool ok = false;
    qint64 v = 0;
    const auto number = [&](qint64 lo, qint64 hi) {
        v = RawTableModel::parseInt(text, &ok);
        if (!ok) {
            *error = u"\"%1\" isn't a number"_s.arg(text);
            return false;
        }
        if (v < lo || v > hi) {
            *error = u"%1 is out of range (%2 to %3)"_s.arg(v).arg(lo).arg(hi);
            return false;
        }
        return true;
    };
    qsizetype off = 0;
    int size = 0;
    qint64 max = 0;
    if (rawCellAt(kind, key, &off, &size, &max)) {
        if (size == 0) {
            writeText(at + off, static_cast<int>(max), text);
            return true;
        }
        if (kind == u"league"_s && key.startsWith(u"#team"_s) && key != u"#teamAmount"_s && text.isEmpty())
            return false;   // Past the league's teams.
        if (!number(size == 4 && max == INT32_MAX ? INT32_MIN : 0, max))
            return false;
        writeNum(at + off, size, true, v);
        return true;
    }
    const FieldDef* d = defFor(kind, key);
    if (!d)
        return false;
    switch (d->type) {
    case Type::Str:
        writeText(at + d->off, d->size, text);
        return true;
    case Type::Bool: {
        const bool on = RawTableModel::parseBool(text, &ok);
        if (!ok) {
            *error = u"\"%1\" isn't True or False"_s.arg(text);
            return false;
        }
        m_data[at + d->off] = on ? 1 : 0;
        return true;
    }
    default: {
        const bool isSigned = d->type == Type::I16 || d->type == Type::I32;
        const qint64 lo = !isSigned ? 0 : d->size == 2 ? -32768 : INT32_MIN;
        const qint64 hi = d->size == 1 ? 255 : d->size == 2 ? (isSigned ? 32767 : 65535) : (isSigned ? INT32_MAX : UINT32_MAX);
        if (!number(lo, hi))
            return false;
        writeNum(at + d->off, d->size, d->big, v);
        return true;
    }
    }
}

bool Rll3SaveEditorManager::fillRawTable(const QString& id, RawTableModel* model) const {
    if (!model || !m_loaded)
        return false;
    const QList<RawColumn> cols = rawColumnsFor(id);
    if (cols.isEmpty())
        return false;
    const QString kind = rawKindOf(id);
    const int count = rawCount(kind);
    QList<QStringList> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        QStringList row;
        row.reserve(cols.size());
        for (const RawColumn& c : cols)
            row << rawGet(kind, i, c.key);
        rows << row;
    }
    QString title;
    for (const QVariant& t : rawTables())
        if (t.toMap().value(u"id"_s).toString() == id)
            title = t.toMap().value(u"title"_s).toString();
    model->setTable(id, title, cols, rows);
    return true;
}

QString Rll3SaveEditorManager::applyRawTable(RawTableModel* model) {
    if (!model || !m_loaded)
        return u"Nothing is loaded."_s;
    const QString id = model->tableId();
    const QString kind = rawKindOf(id);
    const int count = rawCount(kind);
    int changed = 0;
    const QString errors = model->apply(
        [model, count](int r) { return model->recordOfRow(r, count); },
        [this, &kind](int rec, const QString& key) { return rawGet(kind, rec, key); },
        [this, &kind](int rec, const QString& key, const QString& text, QString* error) {
            return rawSet(kind, rec, key, text, error);
        },
        &changed);
    if (changed > 0) {
        m_logoUrls.clear();
        edited();
    }
    fillRawTable(id, model);
    if (!errors.isEmpty())
        return errors;
    setStatus(true, changed > 0 ? u"Saved %1 changes from %2."_s.arg(changed).arg(model->title())
                                : u"Nothing changed in %1."_s.arg(model->title()));
    return {};
}

int Rll3SaveEditorManager::repairRawIds(RawTableModel*) const { return 0; }
