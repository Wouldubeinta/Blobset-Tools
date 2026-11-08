#include "rll3_career_editor_manager.h"

#include "career_match_stats.h"

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

enum class Type { Virtual, U8, U16, I16, I32, Bool, Str, Palette };

enum Flags { kReadOnly = 1, kSlider = 2, kLook = 4, kStats = 8 };

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

#include "rll3_career_fields.inc"

// Where things are in the data (after the header; the C# editor's offsets).
constexpr int kCrcAt = 4;                           // In the header: CRC-32 of the data.
constexpr qsizetype kDataSize = 11000000;
constexpr qsizetype kTeamCountAt = 1460;            // u8
constexpr qsizetype kTeamTable = 2007;
constexpr qsizetype kPlayerCountAt = 518012;        // u16
constexpr qsizetype kPlayerTable = 522724;
constexpr int kMaxTeams = 200;
constexpr int kMaxPlayers = 5000;
constexpr qsizetype kStatusAt = 3092724;            // u16 count; the entries at 3092792:
constexpr qsizetype kStatusEntries = 3092792;       // {i32 player, u8 reason, u16 time, u16 a, u16 b}
constexpr int kStatusSize = 11;
constexpr int kStatusMax = 1024;
constexpr qsizetype kContractsAt = 9904688;         // u16 count; the entries at 9904768:
constexpr qsizetype kContractEntries = 9904768;     // {i32 player, u16, i32 team, i32 new team, u16, i32 salary}
constexpr int kContractSize = 20;
constexpr int kContractMax = 2048;
constexpr qsizetype kSettingsAt = 1216;             // salary cap, half, Nines half, match, recruiting, injuries, currency.
constexpr qsizetype kCareerModeAt = 9882800;        // bool set, bool player, i32 career player.
constexpr qsizetype kCareerTeamAt = 9882928;        // i32 team.
constexpr qsizetype kRepTeamsAt = 9883553;          // i32 state of origin, city v country, all stars, international.
constexpr qsizetype kMoneyAt = 9883570;             // i32 spending money, i32 earnt as a player.
constexpr qsizetype kLeagueAt[21] = {9951157, 9957186, 9965773, 9974360, 9982947, 9991582, 9997659, 10003616,
                                     10009737, 10015750, 10021447, 10027436, 10033511, 10039166, 10044767,
                                     10050368, 10055953, 10061616, 10067343, 10072974, 10078649};
constexpr int kMatchSize = 4076;                    // A fixture (fixed size).
constexpr int kNone = 65535;                        // No team / contract.
constexpr int kMaxJerseys = 8;
constexpr int kNoCommentary = 65535;   // The surname list's last entry.

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

Rll3CareerEditorManager::Rll3CareerEditorManager(QObject* parent) : QObject(parent) {}

QString Rll3CareerEditorManager::fileName() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString Rll3CareerEditorManager::defaultFolder() const {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(u"My Games/Rugby League Live 3"_s);
}

QString Rll3CareerEditorManager::defaultFile() const {
    return QDir(defaultFolder()).filePath(u"CAR000.SAV"_s);
}

void Rll3CareerEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void Rll3CareerEditorManager::edited() {
    ++m_revision;
    emit revisionChanged();
    if (!m_unsaved) {
        m_unsaved = true;
        emit unsavedChangesChanged();
    }
}

int Rll3CareerEditorManager::teamCount() const {
    return m_loaded ? static_cast<quint8>(m_data.at(m_base + kTeamCountAt)) : 0;
}

int Rll3CareerEditorManager::playerCount() const {
    return m_loaded ? static_cast<int>(readNum(m_base + kPlayerCountAt, 2, true, false)) : 0;
}

qsizetype Rll3CareerEditorManager::teamTable() const { return m_base + kTeamTable; }
qsizetype Rll3CareerEditorManager::playerTable() const { return m_base + kPlayerTable; }
int Rll3CareerEditorManager::teamSize() const { return kTeamSize; }
int Rll3CareerEditorManager::playerSize() const { return kPlayerSize; }

int Rll3CareerEditorManager::maxTeams() const {
    return kMaxTeams;
}

int Rll3CareerEditorManager::maxPlayers() const {
    return kMaxPlayers;
}

bool Rll3CareerEditorManager::load(const QString& pathOrUrl) {
    const QString path = QDir::toNativeSeparators(pathOrUrl.isEmpty() ? defaultFile() : localPath(pathOrUrl));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Couldn't open %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    // The header: 12 bytes, then three (i32 size, text), 8 bytes.
    qsizetype p = 12;
    QStringList texts;
    for (int i = 0; i < 3 && p + 4 <= data.size(); ++i) {
        const qint32 n = qFromLittleEndian<qint32>(data.constData() + p);
        if (n < 0 || n > 4096 || p + 4 + n > data.size())
            break;
        texts << QString::fromLatin1(data.mid(p + 4, n));
        p += 4 + n;
    }
    const qsizetype base = p + 8;
    if (texts.size() != 3 || texts.at(0) != u"Career Data"_s || data.size() < base + kDataSize) {
        setStatus(false, u"%1 isn't a Rugby League Live 3 career save (CAR000.SAV)."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    const int teams = static_cast<quint8>(data.at(base + kTeamCountAt));
    const int players = qFromBigEndian<quint16>(data.constData() + base + kPlayerCountAt);
    if (teams > kMaxTeams || players > kMaxPlayers) {
        setStatus(false, u"%1's team / player counts don't fit: is it a Rugby League Live 3 career save?"_s.arg(QFileInfo(path).fileName()));
        return false;
    }

    m_data = data;
    m_path = path;
    m_base = base;
    m_checksumOk = qFromLittleEndian<quint32>(m_data.constData() + kCrcAt) == crc32(m_data.constData() + m_base, m_data.size() - m_base);
    m_backedUp = false;
    m_loaded = true;
    m_logoUrls.clear();
    // "CLUB: RAIDERS - 2043 National Rugby League SEASON, ROUND 4 ..." (the year: the start of the state, unless the
    // career hasn't begun).
    const QString state = texts.at(2).simplified();
    m_careerInfo = texts.at(1).trimmed() + u" - "_s + state;
    m_careerYear = 2015;
    bool ok = false;
    const int year = state.left(4).toInt(&ok);
    if (ok && year > 1900 && !state.contains(u"NOT YET BEGAN"_s))
        m_careerYear = year;
    loadCareer();
    if (m_unsaved) {
        m_unsaved = false;
        emit unsavedChangesChanged();
    }
    ++m_revision;
    emit loadedChanged();
    emit revisionChanged();
    QString text = u"Loaded %1: %2 teams, %3 players. %4."_s.arg(fileName()).arg(teams).arg(players).arg(m_careerInfo);
    if (!m_checksumOk)
        text += u" The file's checksum doesn't match (it'll be right once saved)."_s;
    setStatus(true, text);
    return true;
}


bool Rll3CareerEditorManager::save() {
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

bool Rll3CareerEditorManager::saveAs(const QString& pathOrUrl) {
    if (!m_loaded)
        return false;
    const QString path = QDir::toNativeSeparators(localPath(pathOrUrl));
    qToLittleEndian<quint32>(crc32(m_data.constData() + m_base, m_data.size() - m_base), m_data.data() + kCrcAt);
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

qsizetype Rll3CareerEditorManager::recordAt(const QString& kind, int index) const {
    if (!m_loaded || index < 0)
        return -1;
    if (kind == u"team"_s)
        return index < teamCount() ? teamTable() + qsizetype(index) * teamSize() : -1;
    return index < playerCount() ? playerTable() + qsizetype(index) * playerSize() : -1;
}

int Rll3CareerEditorManager::recordSize(const QString& kind) const {
    return kind == u"team"_s ? teamSize() : playerSize();
}

QString Rll3CareerEditorManager::recordOffset(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    return at < 0 ? QString() : u"0x"_s + QString::number(at, 16).toUpper();
}

qint64 Rll3CareerEditorManager::readNum(qsizetype at, int size, bool big, bool isSigned) const {
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

void Rll3CareerEditorManager::writeNum(qsizetype at, int size, bool big, qint64 value) {
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

QString Rll3CareerEditorManager::readText(qsizetype at, int size) const {
    QByteArray b = m_data.mid(at, size);
    if (b.indexOf('\0') >= 0)
        b.truncate(b.indexOf('\0'));
    return QString::fromLatin1(b);
}

void Rll3CareerEditorManager::writeText(qsizetype at, int size, const QString& text) {
    if (at < 0 || at + size > m_data.size())
        return;
    QByteArray b = text.toLatin1().left(size - 1);
    b.resize(size, '\0');
    m_data.replace(at, size, b);
}

// =================================================================================================================
// Players
// =================================================================================================================

int Rll3CareerEditorManager::playerIndexById(int id) const {
    if (id < 0)
        return -1;
    const int count = playerCount();
    if (id < count && readNum(recordAt(u"player"_s, id), 4, true, true) == id)
        return id;   // Most careers keep player n at position n.
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"player"_s, i), 4, true, true) == id)
            return i;
    return -1;
}

int Rll3CareerEditorManager::playerRating(int player) const {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return 0;
    int stats[7];
    for (int i = 0; i < 7; ++i)
        stats[i] = static_cast<quint8>(m_data.at(at + offOf(u"player"_s, kStatKeys[i])));
    return roundRating(ratingFor(static_cast<quint8>(m_data.at(at + offOf(u"player"_s, "primaryRole"))), stats));
}

int Rll3CareerEditorManager::footworkOfId(int id) const {
    const int p = playerIndexById(id);
    return p < 0 ? 0 : static_cast<quint8>(m_data.at(recordAt(u"player"_s, p) + offOf(u"player"_s, "technicalAbility.footwork")));
}

int Rll3CareerEditorManager::ratingOfId(int id) const {
    const int p = playerIndexById(id);
    return p < 0 ? 0 : playerRating(p);
}

QVariantMap Rll3CareerEditorManager::playerInfo(int id) const {
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
        const int feeder = static_cast<int>(readNum(at + offOf(u"player"_s, "feederClubId"), 4, true, true));
        m[u"club"_s] = feeder >= 0 ? teamNameById(feeder) : QString();
        m[u"age"_s] = qMax(0, m_careerYear - static_cast<int>(readNum(at + offOf(u"player"_s, "dob.year"), 2, true, true)));
        m[u"playerIndex"_s] = p;
        return m;
    }
    m[u"name"_s] = u"(unknown player id %1)"_s.arg(id);
    m[u"first"_s] = QString();
    m[u"last"_s] = m[u"name"_s];
    m[u"known"_s] = false;
    return m;
}

QVariantList Rll3CareerEditorManager::playerRows(const QString& filter) const {
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
                               {u"shirt"_s, readText(at + offOf(u"player"_s, "jerseyName"), 16)}, {u"age"_s, qMax(0, m_careerYear - static_cast<int>(readNum(at + offOf(u"player"_s, "dob.year"), 2, true, true)))},
                               {u"female"_s, false},
                               {u"role"_s, kRoleNames.value(static_cast<quint8>(m_data.at(at + offOf(u"player"_s, "primaryRole"))))},
                               {u"rating"_s, playerRating(i)}});
    }
    return out;
}

// =================================================================================================================
// Teams
// =================================================================================================================

QVariantList Rll3CareerEditorManager::teamRows(const QString& filter) const {
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

QString Rll3CareerEditorManager::teamLogo(int team) const {
    const qsizetype at = recordAt(u"team"_s, team);
    return at < 0 ? QString() : readText(at + offOf(u"team"_s, "logo"), 16);
}

QString Rll3CareerEditorManager::teamLogoUrl(int team) const {
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

int Rll3CareerEditorManager::teamRating(int team) const {
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

QStringList Rll3CareerEditorManager::groups(const QString& kind) const {
    QStringList out;
    for (const FieldDef* d : defsFor(kind))
        if (!out.contains(QString::fromLatin1(d->group)))
            out << QString::fromLatin1(d->group);
    return out;
}

QStringList Rll3CareerEditorManager::pageGroups(const QString& kind, const QString& page) const {
    if (kind == u"team"_s) {
        if (page == u"Details"_s)
            return {u"Names"_s, u"Details"_s, u"Colours"_s, u"Record"_s};
        if (page == u"Stadiums"_s)
            return {u"Stadiums"_s, u"Stadium 1"_s, u"Stadium 2"_s, u"Stadium 3"_s, u"Grand Final Stadium"_s};
        return {};
    }
    if (kind == u"career"_s) {
        if (page == u"Settings"_s)
            return {u"Career"_s, u"Match Settings"_s, u"Money"_s};
        if (page == u"Selection"_s)
            return {u"Career Team"_s, u"Career Player"_s, u"Representative Teams"_s};
        return {};
    }
    if (kind == u"contract"_s)
        return {u"Contract"_s};
    if (page == u"Identity"_s)
        return {u"Name"_s, u"Details"_s, u"Career"_s, u"Status"_s, u"Roles"_s, u"Representative"_s, u"Record"_s};
    if (page == u"Ratings"_s)
        return {u"Attributes"_s};
    if (page == u"Stats"_s)
        return {u"Career Stats"_s};
    return {};
}

QVariantList Rll3CareerEditorManager::fields(const QString& kind, int index) const {
    if (kind == u"career"_s)
        return careerFields(index);
    if (kind == u"contract"_s)
        return contractFields(index);
    return recordFields(kind, index, 0);
}

QVariantList Rll3CareerEditorManager::appearanceFields(int player) const {
    return recordFields(u"player"_s, player, kLook);
}

bool Rll3CareerEditorManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    return setField(u"player"_s, player, key, value);
}

QVariantList Rll3CareerEditorManager::recordFields(const QString& kind, int index, int wantPage) const {
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
        m[u"offset"_s] = d->off >= 0 ? u"+0x"_s + QString::number(d->off, 16).toUpper() : QString();
        m[u"min"_s] = d->min;
        QVariant value;
        QString kindName = u"number"_s;
        qint64 max = d->max;
        switch (d->type) {
        case Type::Virtual: value = virtualValue(at, QString::fromLatin1(d->key)); break;
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

QVariantList Rll3CareerEditorManager::choicesFor(const QString& kind, qsizetype at, const QString& list,
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
        // The C# editor's: the career year and the 19 after it, or none.
        for (int y = m_careerYear; y < m_careerYear + 20; ++y)
            out << QVariantMap{{u"name"_s, QString::number(y)}, {u"value"_s, y}};
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, value.toInt() == 0 ? 0 : kNone}};
    } else if (list == u"@feeder"_s) {
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, -1}};
        const int count = teamCount();
        for (int i = 0; i < count; ++i) {
            const int id = static_cast<int>(readNum(recordAt(u"team"_s, i), 4, true, true));
            out << QVariantMap{{u"name"_s, u"%1 - %2"_s.arg(id).arg(teamNameById(id))}, {u"value"_s, id}};
        }
    } else if (list == u"@statuses"_s) {
        out = namedChoices({u"Available"_s, u"Injured"_s, u"Suspended"_s, u"Retired"_s, u"NSW Origin Camp"_s, u"QLD Origin Camp"_s});
    } else if (list == u"@injuries"_s) {
        out = namedChoices({u"Concussion"_s, u"Neck Sprain"_s, u"Ankle Sprain"_s, u"Corked Thigh"_s, u"Thigh Strain"_s,
                            u"Hamstring"_s, u"AC Joint"_s, u"Wrist Sprain"_s, u"Back Muscle Strain"_s, u"Torn Calf Muscle"_s,
                            u"Adductor Tendinopathy"_s, u"Knee Bursitis"_s, u"Labral Tear"_s, u"Meniscus Tear"_s,
                            u"Broken Arm"_s, u"Fractured Wrist"_s, u"Rotator Cuff Tear"_s, u"Shoulder Bursitis"_s,
                            u"Bulging Disc"_s, u"Dislocated Elbow"_s, u"Dislocated Shoulder"_s,
                            u"Posterior Cruciate Ligament"_s, u"Fractured Skull"_s, u"Avulsion Fracture"_s,
                            u"Metatarsal Fracture"_s, u"Achilles Rupture"_s, u"Anterior Cruciate Ligament"_s});
    } else if (list == u"@severity"_s) {
        out = namedChoices({u"None"_s, u"Light"_s, u"Minor"_s, u"Medium"_s, u"Large"_s, u"Serious"_s});
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

bool Rll3CareerEditorManager::setField(const QString& kind, int index, const QString& key, const QVariant& value) {
    if (kind == u"career"_s)
        return setCareerField(index, key, value);
    if (kind == u"contract"_s)
        return setContractField(index, key, value);
    const qsizetype at = recordAt(kind, index);
    const FieldDef* d = defFor(kind, key);
    if (at < 0 || !d || (d->flags & kReadOnly))
        return false;
    if (d->type == Type::Virtual) {   // The player's status (its list, not the record).
        if (kind != u"player"_s || !setPlayerStatus(static_cast<int>(readNum(at, 4, true, true)), key, value.toInt()))
            return false;
        edited();
        return true;
    }
    const QByteArray before = m_data.mid(at, recordSize(kind));
    switch (d->type) {
    case Type::Str: writeText(at + d->off, d->size, value.toString()); break;
    case Type::Bool: m_data[at + d->off] = value.toBool() ? 1 : 0; break;
    default: {
        qint64 v = static_cast<qint64>(value.toDouble());
        if (d->max > 0 && !d->list)
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

QList<int> Rll3CareerEditorManager::rosterIds(int team) const {
    QList<int> out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qMin<int>(rosterMax(), static_cast<int>(readNum(at + kPlayerAmount, 2, true, false)));
    for (int i = 0; i < count; ++i)
        out << static_cast<int>(readNum(at + kPlayers + 4 * i, 4, true, true));
    return out;
}

QVariantList Rll3CareerEditorManager::rosterRows(int team) const {
    QVariantList out;
    const QList<int> ids = rosterIds(team);
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap m = playerInfo(ids.at(i));
        m[u"slot"_s] = i;
        out.append(m);
    }
    return out;
}

bool Rll3CareerEditorManager::addRosterPlayer(int team, int playerId) {
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

bool Rll3CareerEditorManager::removeRosterPlayer(int team, int slot) {
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

bool Rll3CareerEditorManager::moveRosterPlayer(int team, int slot, int delta) {
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

bool Rll3CareerEditorManager::sortRoster(int team) {
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

bool Rll3CareerEditorManager::databasePlayersAvailable() const {
    return playerCount() > 0;   // Every player is in the career save.
}

QVariantList Rll3CareerEditorManager::pickablePlayers(const QString& filter) const {
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
    return out;
}

QVariantList Rll3CareerEditorManager::matchLists(int team) const {
    Q_UNUSED(team)
    return {QVariantMap{{u"list"_s, u"standard"_s}, {u"title"_s, u"Lineup"_s}, {u"shirts"_s, false}}};
}

bool Rll3CareerEditorManager::listPresent(int team, const QString& list) const {
    Q_UNUSED(list)
    return recordAt(u"team"_s, team) >= 0;
}

bool Rll3CareerEditorManager::setListPresent(int team, const QString& list, bool on) {
    Q_UNUSED(team)
    Q_UNUSED(list)
    if (!on)
        setStatus(false, u"Every team has its lineup."_s);
    return false;
}

QVariantMap Rll3CareerEditorManager::lineupRow(int id, bool present) const {
    QVariantMap m = playerInfo(present ? id : 0);
    QStringList roles;
    for (const QVariant& r : m.value(u"roles"_s).toList())
        roles << kRoleShort.value(r.toInt());
    m[u"roles"_s] = roles.join(u" / "_s);
    m[u"present"_s] = present;
    return m;
}

QVariantList Rll3CareerEditorManager::lineupRows(int team, const QString& list) const {
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

QVariantList Rll3CareerEditorManager::teamRoles(int team, const QString& list) const {
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

bool Rll3CareerEditorManager::setLineupSlot(int team, const QString& list, const QString& key, int playerId) {
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

QVariantList Rll3CareerEditorManager::pickerRows(const QString& filter, int team, bool squadOnly) const {
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
QString Rll3CareerEditorManager::generateBestLineup(int team) {
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

qsizetype Rll3CareerEditorManager::jerseyAt(int team, int uniform) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || uniform < 0 || uniform >= kMaxJerseys)
        return -1;
    return at + kJerseyBase + qsizetype(uniform) * kJerseyStride;
}

QVariantMap Rll3CareerEditorManager::jerseys(int team) const {
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

QVariantList Rll3CareerEditorManager::jerseyFields(int team, int uniform) const {
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

bool Rll3CareerEditorManager::setJerseyField(int team, const QString& key, const QVariant& value) {
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

int Rll3CareerEditorManager::addJersey(int team, int from) {
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

bool Rll3CareerEditorManager::removeJersey(int team, int uniform) {
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

int Rll3CareerEditorManager::addRecord(const QString& kind, bool female) {
    Q_UNUSED(kind)
    Q_UNUSED(female)
    setStatus(false, u"A career save has a fixed set of teams and players: import over one instead."_s);
    return -1;
}

bool Rll3CareerEditorManager::deleteRecord(const QString& kind, int index) {
    Q_UNUSED(kind)
    Q_UNUSED(index)
    setStatus(false, u"A career save has a fixed set of teams and players."_s);
    return false;
}

QString Rll3CareerEditorManager::importRecord(const QString& kind, int index, const QString& pathOrUrl) {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Pick a %1 first."_s.arg(kind);
    QFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::ReadOnly))
        return u"Couldn't open %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    QByteArray record = f.readAll();
    const int size = recordSize(kind);
    // The Save Editor's files (FED000.SAV records, either version): the career's extra bytes are kept from the record
    // being replaced.
    const int fed = kind == u"team"_s ? kFedTeamSize : kFedPlayerSize;
    if (record.size() == fed || record.size() == fed - 1)
        record += m_data.mid(at + record.size(), size - record.size());
    if (record.size() != size)
        return u"%1 is %2 bytes: not a Rugby League Live 3 %3 (%4 bytes, or %5 from the Save Editor)."_s
            .arg(QFileInfo(f).fileName()).arg(f.size()).arg(kind).arg(size).arg(fed);
    const qint64 id = readNum(at, 4, true, true);
    m_data.replace(at, size, record);
    writeNum(at, 4, true, id);
    m_logoUrls.clear();
    edited();
    setStatus(true, u"Imported %1."_s.arg(QFileInfo(f).fileName()));
    return {};
}

QString Rll3CareerEditorManager::exportRecord(const QString& kind, int index, const QString& pathOrUrl) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Nothing to export."_s;
    // The Save Editor's format (a FED000.SAV version 2 record), so the files go both ways.
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data.mid(at, kind == u"team"_s ? kFedTeamSize : kFedPlayerSize)) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QString Rll3CareerEditorManager::recordFileName(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return {};
    if (kind == u"team"_s)
        return fileSafe((readText(at + offOf(kind, "locationName"), 22) + u' ' + readText(at + offOf(kind, "clubName"), 16)).trimmed()) + u".tem"_s;
    return fileSafe(playerInfo(static_cast<int>(readNum(at, 4, true, true))).value(u"name"_s).toString()) + u".ply"_s;
}

QString Rll3CareerEditorManager::exportDecoded(const QString& pathOrUrl) const {
    if (!m_loaded)
        return u"Nothing loaded."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QVariantList Rll3CareerEditorManager::rawRows(const QString& kind, int index) const {
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

bool Rll3CareerEditorManager::setRawByte(const QString& kind, int index, int offset, int value) {
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
// Career
// =================================================================================================================

namespace {

// "NATIONAL RUGBY LEAGUE" -> "National Rugby League" (the C# editor's ToTitleCase).
QString titleCase(const QString& text) {
    QString out = text.toLower();
    bool start = true;
    for (QChar& c : out) {
        if (start && c.isLetter())
            c = c.toUpper();
        start = !c.isLetterOrNumber() && c != u'\'';
    }
    return out;
}

// The C# editor's fixture lists (Fixture Results.cs): league, name suffix, then games, rounds and where (in the data)
// in a year with a Four Nations (league 14 has teams), and in one without.
struct Stage {
    int league;
    const char* suffix;
    int games;
    int rounds;
    qsizetype at;
    int gamesNoFn;
    int roundsNoFn;
    qsizetype atNoFn;
};
constexpr Stage kStages[] = {
    {0, " Trials", 16, 3, 3104411, 16, 3, 3104411},
    {0, "", 192, 26, 3169627, 192, 26, 3169627},
    {0, " Finals", 9, 4, 9071675, 9, 4, 8896407},
    {1, "", 1, 1, 3952219, 1, 1, 3952219},
    {2, "", 1, 1, 3956295, 1, 1, 3956295},
    {3, "", 1, 1, 3960371, 1, 1, 3960371},
    {4, "", 3, 3, 3964447, 3, 3, 3964447},
    {5, " Trials", 16, 3, 3976675, 16, 3, 3976675},
    {5, "", 192, 26, 4041891, 192, 26, 4041891},
    {5, " Finals", 9, 4, 9034991, 9, 4, 8859723},
    {7, " Trials", 21, 3, 4824483, 21, 3, 4824483},
    {7, "", 182, 26, 4910079, 182, 26, 4910079},
    {7, " Finals", 6, 4, 9002383, 6, 4, 8798583},
    {6, " Trials", 18, 3, 5566315, 18, 3, 5566315},
    {6, "", 132, 25, 5639683, 132, 25, 5639683},
    {6, " Finals", 9, 4, 8998307, 9, 4, 8823039},
    {8, " Trials", 18, 3, 6177715, 18, 3, 6177715},
    {8, "", 138, 23, 6251083, 138, 23, 6251083},
    {8, " Super 8s", 28, 7, 8733367, 28, 7, 8558099},
    {8, " Play-Offs", 3, 2, 9108359, 3, 2, 8933091},
    {9, " Super 8s", 28, 7, 8847495, 28, 7, 8672227},
    {9, " Million Pound Game", 1, 1, 9120587, 1, 1, 8945319},
    {10, " Trials", 12, 2, 6813571, 12, 2, 6813571},
    {10, "", 126, 21, 6862483, 126, 21, 6862483},
    {10, " Champ Shield", 28, 7, 8619239, 28, 7, 8443971},
    {10, " Play-Offs", 3, 2, 8961623, 3, 2, 8786355},
    {11, " Trials", 21, 3, 7376059, 21, 3, 7376059},
    {11, "", 154, 22, 7461655, 154, 22, 7461655},
    {11, " Promo Match", 1, 1, 9124663, 1, 1, 8949395},
    {11, " Promo Play-Offs", 3, 2, 9128739, 3, 2, 8953471},
    {12, " Rangitoto", 6, 3, 8089359, 6, 3, 8089359},
    {12, " Waiheke", 6, 3, 8113815, 6, 3, 8113815},
    {12, " Hunua", 6, 3, 8138271, 6, 3, 8138271},
    {12, " Piha", 6, 3, 8162727, 6, 3, 8162727},
    {12, " Finals", 7, 3, 8496959, 7, 3, 8321691},
    {14, "", 6, 3, 8187183, 0, 3, 8187183},
    {14, " Final", 1, 1, 9169499, 0, 1, 9169499},
    {15, "", 10, 5, 8211639, 0, 5, 8211639},
    {15, " Final", 1, 1, 9177651, 0, 1, 9177651},
    {13, "", 6, 3, 8252399, 0, 3, 8252399},
    {13, " Final", 1, 1, 9173575, 0, 1, 9173575},
    {16, "", 3, 1, 8276855, 3, 1, 8187183},
    {17, "", 0, 3, 8199411, 18, 3, 8199411},
    {17, " Finals", 0, 5, 8965699, 7, 5, 8965699},
    {18, "", 24, 3, 8289083, 0, 3, 8289083},
    {18, " Finals", 7, 5, 9140967, 0, 5, 9140967},
    {19, "", 15, 5, 8386907, 0, 5, 8386907},
    {20, " - Round 4", 12, 1, 8448047, 12, 1, 8272779},
    {20, " - Round 5", 8, 1, 8525491, 8, 1, 8350223},
    {20, " - Round 6", 8, 1, 8558099, 8, 1, 8382831},
    {20, " Finals", 7, 5, 8590707, 7, 5, 8415439}};
constexpr int kStageCount = sizeof(kStages) / sizeof(kStages[0]);

// The C# editor's competition ladders: league, its season's fixture list (stage), how many make the finals, and the
// rounds that count (Queensland Cup: 25).
struct LadderComp {
    int league;
    int stage;
    int top;
    int rounds;
};
constexpr LadderComp kLadders[] = {{0, 1, 8, 26}, {5, 8, 8, 26}, {7, 11, 5, 25}, {6, 14, 8, 25}, {8, 17, 8, 23},
                                   {10, 23, 4, 21}, {11, 27, 5, 22}};

enum class CType { U8, Bool, I32 };

struct CareerDef {
    const char* key;
    const char* label;
    const char* group;
    CType type;
    qsizetype at;       // In the data.
    const char* list;
    int page;           // 0 settings, 1 selection
};
constexpr CareerDef kCareerDefs[] = {
    {"isSet", "Career Started", "Career", CType::Bool, kCareerModeAt, nullptr, 0},
    {"isPlayer", "Career Mode", "Career", CType::U8, kCareerModeAt + 1, "@mode", 0},
    {"salaryCap", "Salary Cap Enforced", "Match Settings", CType::Bool, kSettingsAt, nullptr, 0},
    {"halfLength", "Half Length", "Match Settings", CType::U8, kSettingsAt + 1, "@half", 0},
    {"ninesHalfLength", "Half Length (Nines)", "Match Settings", CType::U8, kSettingsAt + 2, "@ninesHalf", 0},
    {"matchDifficulty", "Match Difficulty", "Match Settings", CType::U8, kSettingsAt + 3, "@difficulty", 0},
    {"recruiting", "Recruiting Difficulty", "Match Settings", CType::U8, kSettingsAt + 4, "@recruiting", 0},
    {"injuries", "Injuries", "Match Settings", CType::U8, kSettingsAt + 5, "@injuryLevel", 0},
    {"currency", "Currency", "Match Settings", CType::U8, kSettingsAt + 6, "@currency", 0},
    {"spendingMoney", "Spending Money", "Money", CType::I32, kMoneyAt, nullptr, 0},
    {"moneyEarnt", "Money Earnt As A Player", "Money", CType::I32, kMoneyAt + 4, nullptr, 0},
    {"careerTeam", "Team", "Career Team", CType::I32, kCareerTeamAt, "@careerTeam", 1},
    {"careerPlayer", "Player", "Career Player", CType::I32, kCareerModeAt + 2, "@careerPlayer", 1},
    {"stateOfOrigin", "State Of Origin", "Representative Teams", CType::I32, kRepTeamsAt, "@repOrigin", 1},
    {"cityVsCountry", "City vs Country", "Representative Teams", CType::I32, kRepTeamsAt + 4, "@repCityCountry", 1},
    {"allStars", "All Stars", "Representative Teams", CType::I32, kRepTeamsAt + 8, "@repAllStars", 1},
    {"international", "International", "Representative Teams", CType::I32, kRepTeamsAt + 12, "@repInternational", 1}};

QVariantList valueChoices(const QList<int>& values, const QString& unit) {
    QVariantList out;
    for (int v : values)
        out << QVariantMap{{u"name"_s, u"%1 %2"_s.arg(v).arg(unit)}, {u"value"_s, v}};
    return out;
}

} // namespace

// The competitions (the C# editor's 21): u8 id, name[52], short name[8], kind[15], i32 count, i32 team ids.
void Rll3CareerEditorManager::loadCareer() {
    m_leagues.clear();
    for (const qsizetype rel : kLeagueAt) {
        const qsizetype at = m_base + rel;
        League l;
        l.name = readText(at + 1, 52);
        l.shortName = readText(at + 53, 8);
        l.kind = readText(at + 61, 15);
        const int count = static_cast<int>(readNum(at + 76, 4, false, true));
        for (int i = 0; i < count && i < 64; ++i)
            l.teams << static_cast<int>(readNum(at + 80 + 4 * i, 4, false, true));
        m_leagues << l;
    }
}

int Rll3CareerEditorManager::teamIndexById(int id) const {
    const int count = teamCount();
    if (id < 0 || id == kNone)
        return -1;
    if (id < count && readNum(recordAt(u"team"_s, id), 4, true, true) == id)
        return id;
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"team"_s, i), 4, true, true) == id)
            return i;
    return -1;
}

QString Rll3CareerEditorManager::teamNameById(int id) const {
    const int t = teamIndexById(id);
    if (t < 0)
        return id < 0 || id == kNone ? QString() : u"(team %1)"_s.arg(id);
    const qsizetype at = recordAt(u"team"_s, t);
    return titleCase((readText(at + offOf(u"team"_s, "locationName"), 22) + u' ' + readText(at + offOf(u"team"_s, "clubName"), 16)).trimmed());
}

QVariantList Rll3CareerEditorManager::teamChoices(const QList<int>& leagues, bool none) const {
    QVariantList out;
    for (int l : leagues) {
        if (l < 0 || l >= m_leagues.size())
            continue;
        for (int id : m_leagues.at(l).teams) {
            QString name = teamNameById(id);
            if (leagues.size() > 1)
                name += u"  ("_s + titleCase(m_leagues.at(l).name) + u")"_s;
            out << QVariantMap{{u"name"_s, name}, {u"value"_s, id}};
        }
    }
    if (none)
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, kNone}};
    return out;
}

QVariantList Rll3CareerEditorManager::careerItems() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const bool player = m_data.at(m_base + kCareerModeAt + 1) != 0;
    const int team = static_cast<int>(readNum(m_base + kCareerTeamAt, 4, true, true));
    const int statuses = static_cast<int>(readNum(m_base + kStatusAt, 2, true, false));
    const int contracts = static_cast<int>(readNum(m_base + kContractsAt, 2, true, false));
    const auto item = [](int i, const QString& key, const QString& title, const QString& subtitle, const QStringList& tabs) {
        return QVariantMap{{u"index"_s, i}, {u"key"_s, key}, {u"title"_s, title}, {u"subtitle"_s, subtitle}, {u"tabs"_s, tabs}};
    };
    out << item(0, u"settings"_s, u"Career Settings"_s,
                (player ? u"Player career"_s : u"Coach career"_s) + u"  ·  %1 season"_s.arg(m_careerYear), {u"Settings"_s});
    out << item(1, u"selection"_s, u"Team & Player Selection"_s, teamNameById(team), {u"Selection"_s});
    out << item(2, u"status"_s, u"Player Status"_s, u"%1 injured, suspended or away"_s.arg(statuses), {u"Player Status"_s});
    out << item(3, u"contracts"_s, u"Player Contracts"_s, u"%1 contract changes"_s.arg(contracts), {u"Contracts"_s});
    out << item(4, u"competitions"_s, u"Competitions"_s, u"Ladders and fixtures"_s, {u"Ladder"_s, u"Fixtures"_s});
    return out;
}

QVariantList Rll3CareerEditorManager::careerFields(int page) const {
    QVariantList out;
    if (!m_loaded)
        return out;
    for (const CareerDef& d : kCareerDefs) {
        if (d.page != page)
            continue;
        const qsizetype at = m_base + d.at;
        QVariantMap m;
        m[u"key"_s] = QString::fromLatin1(d.key);
        m[u"label"_s] = QString::fromLatin1(d.label);
        m[u"group"_s] = QString::fromLatin1(d.group);
        m[u"readOnly"_s] = false;
        m[u"slider"_s] = false;
        m[u"offset"_s] = u"0x"_s + QString::number(at, 16).toUpper();
        m[u"min"_s] = 0;
        int value = 0;
        switch (d.type) {
        case CType::Bool:
            m[u"kind"_s] = u"bool"_s;
            m[u"value"_s] = m_data.at(at) != 0;
            break;
        case CType::U8:
            value = static_cast<quint8>(m_data.at(at));
            m[u"kind"_s] = u"number"_s;
            m[u"value"_s] = value;
            m[u"max"_s] = 255;
            break;
        case CType::I32:
            value = static_cast<int>(readNum(at, 4, true, true));
            m[u"kind"_s] = u"big"_s;
            m[u"value"_s] = static_cast<double>(value);
            m[u"max"_s] = 2147483647.0;
            break;
        }
        if (d.list) {
            const QString list = QString::fromLatin1(d.list);
            QVariantList choices;
            if (list == u"@mode"_s)
                choices = namedChoices({u"Coach"_s, u"Player"_s});
            else if (list == u"@half"_s)
                choices = valueChoices({3, 5, 7, 9, 10, 15, 20, 30, 40}, u"Minutes"_s);
            else if (list == u"@ninesHalf"_s)
                choices = valueChoices({3, 5, 7, 9}, u"Minutes"_s);
            else if (list == u"@difficulty"_s)
                choices = namedChoices({u"Rookie"_s, u"Amateur"_s, u"Pro"_s, u"Veteran"_s, u"Legend"_s});
            else if (list == u"@recruiting"_s)
                choices = {QVariantMap{{u"name"_s, u"Always Get Your Man"_s}, {u"value"_s, 0}},
                           QVariantMap{{u"name"_s, u"Realistic"_s}, {u"value"_s, 3}}};
            else if (list == u"@injuryLevel"_s)
                choices = namedChoices({u"None"_s, u"Reduced"_s, u"Realistic"_s, u"Extreme"_s});
            else if (list == u"@currency"_s)
                choices = namedChoices({u"Pounds"_s, u"Dollars"_s});
            else if (list == u"@careerTeam"_s)
                choices = teamChoices({0, 5, 6, 7, 8, 10, 11}, false);
            else if (list == u"@repInternational"_s)
                choices = teamChoices({13, 14, 15, 17, 18, 19}, true);
            else if (list == u"@repOrigin"_s)
                choices = teamChoices({4}, true);
            else if (list == u"@repCityCountry"_s)
                choices = teamChoices({3}, true);
            else if (list == u"@repAllStars"_s)
                choices = teamChoices({1}, true);
            else if (list == u"@careerPlayer"_s) {
                // The career team's players (the C# editor's list), and whoever it is now.
                QList<int> ids = rosterIds(teamIndexById(static_cast<int>(readNum(m_base + kCareerTeamAt, 4, true, true))));
                if (!ids.contains(value))
                    ids.prepend(value);
                for (int id : std::as_const(ids)) {
                    const QVariantMap info = playerInfo(id);
                    choices << QVariantMap{{u"name"_s, id < 0 ? u"None"_s : u"%1  (%2)"_s.arg(info.value(u"name"_s).toString(),
                                                                                             info.value(u"role"_s).toString())},
                                           {u"value"_s, id}};
                }
            }
            if (std::none_of(choices.cbegin(), choices.cend(), [value](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == value; }))
                choices.prepend(QVariantMap{{u"name"_s, value == -1 || value == kNone ? u"None"_s : u"(%1)"_s.arg(value)}, {u"value"_s, value}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = choices;
            m[u"value"_s] = value;
        }
        out << m;
    }
    return out;
}

bool Rll3CareerEditorManager::setCareerField(int page, const QString& key, const QVariant& value) {
    for (const CareerDef& d : kCareerDefs) {
        if (d.page != page || key != QLatin1StringView(d.key))
            continue;
        const qsizetype at = m_base + d.at;
        const QByteArray before = m_data.mid(at, 4);
        switch (d.type) {
        case CType::Bool: m_data[at] = value.toBool() ? 1 : 0; break;
        case CType::U8: m_data[at] = static_cast<char>(std::clamp(value.toInt(), 0, 255)); break;
        case CType::I32: writeNum(at, 4, true, static_cast<qint64>(value.toDouble())); break;
        }
        if (m_data.mid(at, 4) == before)
            return false;
        edited();
        return true;
    }
    return false;
}

// --- Player status --------------------------------------------------------------------------------------------------

int Rll3CareerEditorManager::statusIndexOf(int playerId) const {
    const int count = qMin(kStatusMax, static_cast<int>(readNum(m_base + kStatusAt, 2, true, false)));
    for (int i = 0; i < count; ++i)
        if (readNum(m_base + kStatusEntries + qsizetype(i) * kStatusSize, 4, true, true) == playerId)
            return i;
    return -1;
}

QVariant Rll3CareerEditorManager::virtualValue(qsizetype at, const QString& key) const {
    const int s = statusIndexOf(static_cast<int>(readNum(at, 4, true, true)));
    const qsizetype e = m_base + kStatusEntries + qsizetype(s) * kStatusSize;
    if (key == u"@status"_s)
        return s < 0 ? 0 : static_cast<int>(static_cast<quint8>(m_data.at(e + 4)));
    if (key == u"@statusTime"_s)
        return s < 0 ? 0 : static_cast<int>(readNum(e + 5, 2, true, false));
    if (key == u"@injury"_s)
        return s < 0 || m_data.at(e + 4) != 1 ? 0 : static_cast<int>(readNum(e + 9, 2, true, false));
    if (key == u"@injurySeverity"_s)
        return s < 0 || m_data.at(e + 4) != 1 ? 0 : static_cast<int>(readNum(e + 7, 2, true, false));
    return {};
}

// The C# editor's Player Editor: an entry per player who isn't available; +7 is the injury's severity (the editor
// writes 2), the team for a suspension, 96 / 95 for the NSW / QLD Origin camps; +9 the injury.
bool Rll3CareerEditorManager::setPlayerStatus(int playerId, const QString& key, int value) {
    if (playerId < 0)
        return false;
    int s = statusIndexOf(playerId);
    const auto entry = [this](int i) { return m_base + kStatusEntries + qsizetype(i) * kStatusSize; };
    if (key == u"@status"_s) {
        if (value < 0 || value > 5)
            return false;
        if (value == 0) {
            if (s < 0)
                return false;
            const int count = static_cast<int>(readNum(m_base + kStatusAt, 2, true, false));
            const qsizetype e = entry(s), end = entry(count);
            m_data.replace(e, end - e - kStatusSize, m_data.mid(e + kStatusSize, end - e - kStatusSize));
            m_data.replace(end - kStatusSize, kStatusSize, QByteArray(kStatusSize, '\0'));
            writeNum(m_base + kStatusAt, 2, true, count - 1);
            return true;
        }
        const int count = static_cast<int>(readNum(m_base + kStatusAt, 2, true, false));
        const int was = s >= 0 ? static_cast<quint8>(m_data.at(entry(s) + 4)) : 0;
        if (was == value)
            return false;
        if (s < 0) {
            if (count >= kStatusMax) {
                setStatus(false, u"The status list is full (%1 players)."_s.arg(kStatusMax));
                return false;
            }
            s = count;
            m_data.replace(entry(s), kStatusSize, QByteArray(kStatusSize, '\0'));
            writeNum(entry(s), 4, true, playerId);
            writeNum(entry(s) + 5, 2, true, value == 3 ? 0 : 1);
            writeNum(m_base + kStatusAt, 2, true, count + 1);
        }
        const qsizetype e = entry(s);
        m_data[e + 4] = static_cast<char>(value);
        int a = 0;
        if (value == 1) {
            a = was == 1 ? static_cast<int>(readNum(e + 7, 2, true, false)) : 2;
        } else if (value == 2) {
            const int teams = teamCount();
            for (int t = 0; t < teams && a == 0; ++t)
                if (rosterIds(t).contains(playerId))
                    a = static_cast<int>(readNum(recordAt(u"team"_s, t), 4, true, true));
        } else if (value == 4 || value == 5) {
            a = value == 4 ? 96 : 95;
        }
        writeNum(e + 7, 2, true, a);
        if (value != 1)
            writeNum(e + 9, 2, true, 0);
        if (value == 3)
            writeNum(e + 5, 2, true, 0);
        return true;
    }
    if (s < 0)
        return false;
    const qsizetype e = entry(s);
    const int reason = static_cast<quint8>(m_data.at(e + 4));
    if (key == u"@statusTime"_s && reason != 3)
        writeNum(e + 5, 2, true, std::clamp(value, 0, 65535));
    else if (key == u"@injury"_s && reason == 1)
        writeNum(e + 9, 2, true, std::clamp(value, 0, 26));
    else if (key == u"@injurySeverity"_s && reason == 1)
        writeNum(e + 7, 2, true, std::clamp(value, 0, 5));
    else
        return false;
    return true;
}

QVariantList Rll3CareerEditorManager::statusRows() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    static const QStringList reasons = {u"Available"_s, u"Injured"_s, u"Suspended"_s, u"Retired"_s, u"NSW Origin Camp"_s,
                                        u"QLD Origin Camp"_s};
    static const QStringList severity = {u""_s, u"Light"_s, u"Minor"_s, u"Medium"_s, u"Large"_s, u"Serious"_s};
    const QVariantList injuries = choicesFor(u"player"_s, 0, u"@injuries"_s, 0);
    const int count = qMin(kStatusMax, static_cast<int>(readNum(m_base + kStatusAt, 2, true, false)));
    for (int i = 0; i < count; ++i) {
        const qsizetype e = m_base + kStatusEntries + qsizetype(i) * kStatusSize;
        const int id = static_cast<int>(readNum(e, 4, true, true));
        const int reason = static_cast<quint8>(m_data.at(e + 4));
        const int time = static_cast<int>(readNum(e + 5, 2, true, false));
        const int a = static_cast<int>(readNum(e + 7, 2, true, false));
        const int b = static_cast<int>(readNum(e + 9, 2, true, false));
        const QVariantMap info = playerInfo(id);
        QString detail;
        QString unit = u"days"_s;
        if (reason == 1)
            detail = (severity.value(a) + u" "_s + injuries.value(b).toMap().value(u"name"_s).toString()).trimmed();
        else if (reason == 2) {
            detail = u"Suspended"_s;
            unit = u"matches"_s;
        } else if (reason == 4 || reason == 5)
            detail = u"Origin camp"_s;
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"name"_s, info.value(u"name"_s)}, {u"role"_s, info.value(u"role"_s)},
                           {u"rating"_s, info.value(u"rating"_s)}, {u"reason"_s, reason},
                           {u"reasonName"_s, reasons.value(reason, u"(%1)"_s.arg(reason))}, {u"duration"_s, time},
                           {u"unit"_s, reason == 3 ? QString() : unit}, {u"detail"_s, detail},
                           {u"playerIndex"_s, info.value(u"playerIndex"_s, -1)}};
    }
    return out;
}

bool Rll3CareerEditorManager::removeStatus(int index) {
    const int count = qMin(kStatusMax, static_cast<int>(readNum(m_base + kStatusAt, 2, true, false)));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = m_base + kStatusEntries + qsizetype(index) * kStatusSize;
    const qsizetype end = m_base + kStatusEntries + qsizetype(count) * kStatusSize;
    m_data.replace(e, end - e - kStatusSize, m_data.mid(e + kStatusSize, end - e - kStatusSize));
    m_data.replace(end - kStatusSize, kStatusSize, QByteArray(kStatusSize, '\0'));
    writeNum(m_base + kStatusAt, 2, true, count - 1);
    edited();
    return true;
}

// --- Contracts ------------------------------------------------------------------------------------------------------

namespace {
struct ContractDef {
    const char* key;
    const char* label;
    int off;
    int size;
    bool team;
};
constexpr ContractDef kContractDefs[] = {{"playerId", "Player ID", 0, 4, false},
                                         {"currentContract", "Current Contract", 4, 2, false},
                                         {"currentTeam", "Current Team", 6, 4, true},
                                         {"newTeam", "New Team", 10, 4, true},
                                         {"newContract", "New Contract", 14, 2, false},
                                         {"newSalary", "New Salary", 16, 4, false}};
} // namespace

QVariantList Rll3CareerEditorManager::contractRows() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const int count = qMin(kContractMax, static_cast<int>(readNum(m_base + kContractsAt, 2, true, false)));
    for (int i = 0; i < count; ++i) {
        const qsizetype e = m_base + kContractEntries + qsizetype(i) * kContractSize;
        const int id = static_cast<int>(readNum(e, 4, true, true));
        const int team = static_cast<int>(readNum(e + 6, 4, true, true));
        const int newTeam = static_cast<int>(readNum(e + 10, 4, true, true));
        const QVariantMap info = playerInfo(id);
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"name"_s, info.value(u"name"_s)}, {u"role"_s, info.value(u"role"_s)},
                           {u"rating"_s, info.value(u"rating"_s)},
                           {u"currentContract"_s, static_cast<int>(readNum(e + 4, 2, true, false))},
                           {u"currentTeam"_s, team}, {u"currentTeamName"_s, teamNameById(team)},
                           {u"newTeam"_s, newTeam}, {u"newTeamName"_s, teamNameById(newTeam)},
                           {u"newContract"_s, static_cast<int>(readNum(e + 14, 2, true, false))},
                           {u"newSalary"_s, static_cast<int>(readNum(e + 16, 4, true, true))},
                           {u"playerIndex"_s, info.value(u"playerIndex"_s, -1)}};
    }
    return out;
}

QVariantList Rll3CareerEditorManager::contractFields(int index) const {
    QVariantList out;
    const int count = static_cast<int>(readNum(m_base + kContractsAt, 2, true, false));
    if (!m_loaded || index < 0 || index >= count)
        return out;
    const qsizetype e = m_base + kContractEntries + qsizetype(index) * kContractSize;
    for (const ContractDef& d : kContractDefs) {
        const int value = static_cast<int>(readNum(e + d.off, d.size, true, d.size == 4));
        QVariantMap m{{u"key"_s, QString::fromLatin1(d.key)}, {u"label"_s, QString::fromLatin1(d.label)}, {u"group"_s, u"Contract"_s},
                      {u"readOnly"_s, false}, {u"slider"_s, false}, {u"min"_s, 0},
                      {u"offset"_s, u"0x"_s + QString::number(e + d.off, 16).toUpper()}};
        if (d.team) {
            QVariantList choices = teamChoices({0, 5, 6, 7, 8, 10, 11}, false);
            if (std::none_of(choices.cbegin(), choices.cend(), [value](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == value; }))
                choices.prepend(QVariantMap{{u"name"_s, teamNameById(value).isEmpty() ? u"None"_s : teamNameById(value)}, {u"value"_s, value}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = choices;
            m[u"value"_s] = value;
        } else {
            m[u"kind"_s] = d.size == 4 ? u"big"_s : u"number"_s;
            m[u"value"_s] = static_cast<double>(value);
            m[u"max"_s] = d.size == 4 ? 2147483647.0 : 65535.0;
        }
        out << m;
    }
    return out;
}

bool Rll3CareerEditorManager::setContractField(int index, const QString& key, const QVariant& value) {
    const int count = static_cast<int>(readNum(m_base + kContractsAt, 2, true, false));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = m_base + kContractEntries + qsizetype(index) * kContractSize;
    for (const ContractDef& d : kContractDefs) {
        if (key != QLatin1StringView(d.key))
            continue;
        const qint64 v = static_cast<qint64>(value.toDouble());
        if (readNum(e + d.off, d.size, true, d.size == 4) == v)
            return false;
        writeNum(e + d.off, d.size, true, v);
        edited();
        return true;
    }
    return false;
}

bool Rll3CareerEditorManager::removeContract(int index) {
    const int count = static_cast<int>(readNum(m_base + kContractsAt, 2, true, false));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = m_base + kContractEntries + qsizetype(index) * kContractSize;
    const qsizetype end = m_base + kContractEntries + qsizetype(count) * kContractSize;
    m_data.replace(e, end - e - kContractSize, m_data.mid(e + kContractSize, end - e - kContractSize));
    m_data.replace(end - kContractSize, kContractSize, QByteArray(kContractSize, '\0'));
    writeNum(m_base + kContractsAt, 2, true, count - 1);
    edited();
    return true;
}

// --- Competitions ---------------------------------------------------------------------------------------------------

// A fixture (4076 bytes, the C# editor's ReadFixtureStats): u8, u8 round, u8 match, u16 home, u16 away, u8 stadium,
// i16 year, u8 month, day, hour, minute, u8 event, bool played; 392 bytes; the home players' stats (17 x 36), the
// home team's (21: captain, then i16 completion attempts, errors, field goals, goals, line breaks, tackles, missed,
// possession, tries, completions); 554 bytes; the away players' and team's; 1848 bytes. A score: 4 a try, 2 a goal,
// 1 a field goal.
QList<Rll3CareerEditorManager::Match> Rll3CareerEditorManager::readStage(int stage) const {
    QList<Match> out;
    if (!m_loaded || stage < 0 || stage >= kStageCount)
        return out;
    const Stage& st = kStages[stage];
    const bool fourNations = !m_leagues.value(14).teams.isEmpty();
    const int games = fourNations ? st.games : st.gamesNoFn;
    const qsizetype first = m_base + (fourNations ? st.at : st.atNoFn);
    const auto score = [this](qsizetype t) {
        return 4 * static_cast<int>(readNum(t + 17, 2, true, true)) + 2 * static_cast<int>(readNum(t + 7, 2, true, true))
             + static_cast<int>(readNum(t + 5, 2, true, true));
    };
    for (int i = 0; i < games; ++i) {
        const qsizetype p = first + qsizetype(i) * kMatchSize;
        if (p + kMatchSize > m_data.size())
            break;
        Match m;
        m.round = static_cast<quint8>(m_data.at(p + 1));
        m.home = static_cast<int>(readNum(p + 3, 2, true, false));
        m.away = static_cast<int>(readNum(p + 5, 2, true, false));
        m.stadium = static_cast<quint8>(m_data.at(p + 7));
        m.year = static_cast<int>(readNum(p + 8, 2, true, true));
        m.month = static_cast<quint8>(m_data.at(p + 10));
        m.day = static_cast<quint8>(m_data.at(p + 11));
        m.hour = static_cast<quint8>(m_data.at(p + 12));
        m.minute = static_cast<quint8>(m_data.at(p + 13));
        m.played = m_data.at(p + 15) != 0;
        m.at = p;
        m.homeScore = score(p + 16 + 392 + 17 * 36);
        m.awayScore = score(p + 16 + 392 + 17 * 36 + 21 + 554 + 17 * 36);
        out << m;
    }
    return out;
}

QVariantList Rll3CareerEditorManager::ladderComps() const {
    QVariantList out;
    for (int i = 0; i < int(sizeof(kLadders) / sizeof(kLadders[0])); ++i)
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, titleCase(m_leagues.value(kLadders[i].league).name)},
                           {u"logoUrl"_s, competitionLogoUrl(m_leagues.value(kLadders[i].league).kind)}};
    return out;
}

// The C# editor's Competition Ladder: the season's results up to the first match not played yet; a round a team
// didn't play (before then) is a bye, worth 2 points.
QVariantList Rll3CareerEditorManager::ladderRows(int comp) const {
    QVariantList out;
    if (!m_loaded || comp < 0 || comp >= int(sizeof(kLadders) / sizeof(kLadders[0])))
        return out;
    const LadderComp& lc = kLadders[comp];
    const QList<int> teams = m_leagues.value(lc.league).teams;
    const int rounds = lc.rounds;
    struct Row {
        int id = 0, played = 0, points = 0, wins = 0, draws = 0, lost = 0, byes = 0, pf = 0, pa = 0, hw = 0, hl = 0, aw = 0, al = 0;
    };
    QList<Row> rows(teams.size());
    for (int i = 0; i < teams.size(); ++i)
        rows[i].id = teams.at(i);
    QList<QList<bool>> playing(rounds, QList<bool>(teams.size(), false));
    int stopRound = rounds;
    for (const Match& m : readStage(lc.stage)) {
        if (m.round >= rounds)
            continue;
        if (!m.played) {
            stopRound = m.round;
            break;
        }
        const int h = teams.indexOf(m.home), a = teams.indexOf(m.away);
        if (h < 0 || a < 0)
            continue;
        playing[m.round][h] = playing[m.round][a] = true;
        Row& home = rows[h];
        Row& away = rows[a];
        ++home.played;
        ++away.played;
        home.pf += m.homeScore;
        home.pa += m.awayScore;
        away.pf += m.awayScore;
        away.pa += m.homeScore;
        if (m.homeScore > m.awayScore) {
            ++home.wins, ++home.hw, home.points += 2, ++away.lost, ++away.al;
        } else if (m.awayScore > m.homeScore) {
            ++away.wins, ++away.aw, away.points += 2, ++home.lost, ++home.hl;
        } else {
            ++home.draws, ++away.draws, ++home.points, ++away.points;
        }
    }
    for (int r = 0; r < stopRound; ++r)
        for (int t = 0; t < teams.size(); ++t)
            if (!playing[r][t])
                ++rows[t].byes, rows[t].points += 2;
    QList<QPair<Row, QString>> sorted;
    for (const Row& r : std::as_const(rows))
        sorted << qMakePair(r, teamNameById(r.id));
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) {
        if (x.first.points != y.first.points)
            return x.first.points > y.first.points;
        const int dx = x.first.pf - x.first.pa, dy = y.first.pf - y.first.pa;
        if (dx != dy)
            return dx > dy;
        return x.second.compare(y.second, Qt::CaseInsensitive) < 0;
    });
    const QVariantList all = teamRows(QString());   // Every team, by index.
    for (int i = 0; i < sorted.size(); ++i) {
        const Row& r = sorted.at(i).first;
        const int t = teamIndexById(r.id);
        out << QVariantMap{{u"pos"_s, i + 1}, {u"id"_s, r.id}, {u"teamIndex"_s, t}, {u"name"_s, sorted.at(i).second},
                           {u"logoUrl"_s, all.value(t).toMap().value(u"logoUrl"_s)},
                           {u"abbreviation"_s, all.value(t).toMap().value(u"abbreviation"_s)},
                           {u"colour"_s, all.value(t).toMap().value(u"primaryColour"_s)},
                           {u"played"_s, r.played}, {u"points"_s, r.points}, {u"wins"_s, r.wins}, {u"draws"_s, r.draws},
                           {u"lost"_s, r.lost}, {u"byes"_s, r.byes}, {u"for"_s, r.pf}, {u"against"_s, r.pa},
                           {u"diff"_s, r.pf - r.pa}, {u"home"_s, u"%1-%2"_s.arg(r.hw).arg(r.hl)},
                           {u"away"_s, u"%1-%2"_s.arg(r.aw).arg(r.al)}, {u"finals"_s, i < lc.top}};
    }
    return out;
}

QVariantList Rll3CareerEditorManager::fixtureStages() const {
    QVariantList out;
    for (int i = 0; i < kStageCount; ++i)
        out << QVariantMap{{u"index"_s, i},
                           {u"name"_s, titleCase(m_leagues.value(kStages[i].league).name) + QString::fromLatin1(kStages[i].suffix)},
                           {u"logoUrl"_s, competitionLogoUrl(m_leagues.value(kStages[i].league).kind)}};
    return out;
}

QStringList Rll3CareerEditorManager::fixtureRounds(int stage) const {
    if (stage < 0 || stage >= kStageCount)
        return {};
    switch (stage) {
    case 2: case 9: case 12: case 15: return {u"Week 1"_s, u"Semi Finals"_s, u"Preliminary Final"_s, u"Grand Final"_s};
    case 19: case 25: case 29: return {u"Semi Finals"_s, u"Final"_s};
    case 34: case 43: case 45: case 50: return {u"Quarter Finals"_s, u"Semi Finals"_s, u"Final"_s};
    case 36: case 38: case 40: return {u"Final"_s};
    default: break;
    }
    const Stage& st = kStages[stage];
    const int rounds = m_leagues.value(14).teams.isEmpty() ? st.roundsNoFn : st.rounds;
    QStringList out;
    for (int r = 0; r < rounds; ++r)
        out << u"Round %1"_s.arg(r + 1);
    return out;
}

QVariantList Rll3CareerEditorManager::fixtureRows(int stage, int round) const {
    QVariantList out;
    QVariantList all;   // Every team, by index (made when needed).
    const QList<Match> matches = readStage(stage);
    for (int n = 0; n < matches.size(); ++n) {
        const Match& m = matches.at(n);
        if (m.round != round || m.year == 0)
            continue;
        if (all.isEmpty())
            all = teamRows(QString());
        const int h = teamIndexById(m.home), a = teamIndexById(m.away);
        const QVariantMap hr = all.value(h).toMap(), ar = all.value(a).toMap();
        QString stadium = u"Stadium %1"_s.arg(m.stadium);
        for (const Rll3SaveLists::Choice& c : Rll3SaveLists::stadiums())
            if (c.value == m.stadium)
                stadium = QString::fromUtf8(c.name);
        out << QVariantMap{{u"home"_s, teamNameById(m.home)}, {u"away"_s, teamNameById(m.away)},
                           {u"homeIndex"_s, h}, {u"awayIndex"_s, a},
                           {u"homeLogoUrl"_s, hr.value(u"logoUrl"_s)}, {u"awayLogoUrl"_s, ar.value(u"logoUrl"_s)},
                           {u"homeAbbreviation"_s, hr.value(u"abbreviation"_s)}, {u"awayAbbreviation"_s, ar.value(u"abbreviation"_s)},
                           {u"homeColour"_s, hr.value(u"primaryColour"_s)}, {u"awayColour"_s, ar.value(u"primaryColour"_s)},
                           {u"played"_s, m.played}, {u"homeScore"_s, m.homeScore}, {u"awayScore"_s, m.awayScore},
                           {u"stadium"_s, stadium}, {u"match"_s, n},
                           {u"date"_s, u"%1/%2/%3"_s.arg(m.day, 2, 10, QChar(u'0')).arg(m.month, 2, 10, QChar(u'0')).arg(m.year)},
                           {u"time"_s, u"%1:%2"_s.arg(m.hour, 2, 10, QChar(u'0')).arg(m.minute, 2, 10, QChar(u'0'))}};
    }
    return out;
}

QString Rll3CareerEditorManager::competitionLogoUrl(const QString& name) const {
    if (name.isEmpty())
        return {};
    const QString key = u"comp:"_s + name.toLower();
    if (const auto it = m_logoUrls.constFind(key); it != m_logoUrls.constEnd())
        return it.value();
    QString url;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        for (const char* size : {"large", "small"}) {
            for (const char* platform : {".pc", "", ".ps3", ".x360"}) {
                const QString hash = orchestrator->getHashFromPath(
                    u"ui/logos_competition/%1/%2%3.dds"_s.arg(QLatin1StringView(size), name.toLower(), QLatin1StringView(platform)));
                if (!hash.isEmpty() && hash.toULongLong(nullptr, 16) != 0) {
                    url = u"image://dds_provider/"_s + DdsImageProvider::makeId(hash, 0, {});
                    break;
                }
            }
            if (!url.isEmpty())
                break;
        }
    }
    m_logoUrls.insert(key, url);
    return url;
}

// A fixture's full stats (see readStage): per side 17 players {i32 id, u8 position, u8 ball strips, conversions,
// conversion attempts, fends, field goal attempts, field goals, 40/20s, handling errors, hit ups, intercepted passes,
// interceptions, kick metres, kicks in play, knock ons, line break assists, line breaks, i16 run metres, u8 minutes,
// missed tackles, offloads, one on one tackles, passes, penalties conceded, penalty goals, penalty goal attempts,
// runs, tackle breaks, tackles, tries, try assists}, then the team's stats.
QVariantMap Rll3CareerEditorManager::matchStats(int stage, int match) const {
    const QList<Match> matches = readStage(stage);
    if (match < 0 || match >= matches.size())
        return {{u"ok"_s, false}};
    const Match& m = matches.at(match);
    static const char* const bytesA[] = {"ballStrips", "conversions", "conversionAttempts", "fends", "fieldGoalsAttempts",
                                         "fieldGoals", "fortyTwenties", "handlingErrors", "hitups", "interceptedPasses",
                                         "interceptions", "kickMetresGained", "kicksInPlay", "knockOns", "lineBreakAssists",
                                         "lineBreaks"};
    static const char* const bytesB[] = {"minutesPlayed", "missedTackles", "offloads", "oneOnOneTackles", "passes",
                                         "penaltiesConceded", "penaltyGoals", "penaltyGoalAttempts", "allRuns",
                                         "tackleBreaks", "tackles", "tries", "tryAssists"};
    static const char* const teamStats[] = {"completionAttempts", "errors", "fieldgoal", "goals", "lineBreaks", "tackles",
                                            "missTackles", "timeInPossession", "tries", "completions"};
    const QVariantList all = teamRows(QString());
    CareerMatch::Team teams[2];
    teams[0].id = m.home;
    teams[1].id = m.away;
    qsizetype p = m.at + 16 + 392;
    for (int side = 0; side < 2; ++side) {
        CareerMatch::Team& t = teams[side];
        for (int k = 0; k < 17; ++k, p += 36) {
            CareerMatch::Player pl;
            pl.id = static_cast<int>(readNum(p, 4, true, true));
            pl.position = static_cast<quint8>(m_data.at(p + 4));
            for (int s = 0; s < 16; ++s)
                pl.s.insert(QString::fromLatin1(bytesA[s]), static_cast<quint8>(m_data.at(p + 5 + s)));
            pl.s.insert(u"allRunMetres"_s, static_cast<int>(readNum(p + 21, 2, true, true)));
            for (int s = 0; s < 13; ++s)
                pl.s.insert(QString::fromLatin1(bytesB[s]), static_cast<quint8>(m_data.at(p + 23 + s)));
            const QVariantMap info = playerInfo(pl.id);
            pl.name = info.value(u"name"_s).toString();
            pl.role = info.value(u"role"_s).toString();
            pl.playerIndex = info.value(u"playerIndex"_s, -1).toInt();
            t.players << pl;
        }
        for (int s = 0; s < 10; ++s)
            t.t.insert(QString::fromLatin1(teamStats[s]), static_cast<int>(readNum(p + 1 + 2 * s, 2, true, true)));
        t.score = 4 * t.t.value(u"tries"_s) + 2 * t.t.value(u"goals"_s) + t.t.value(u"fieldgoal"_s);
        p += 21 + 554;
        t.teamIndex = teamIndexById(t.id);
        const QVariantMap row = all.value(t.teamIndex).toMap();
        t.name = teamNameById(t.id);
        if (t.teamIndex >= 0) {
            const qsizetype tr = recordAt(u"team"_s, t.teamIndex);
            t.location = titleCase(readText(tr + offOf(u"team"_s, "locationName"), 22));
            t.club = titleCase(readText(tr + offOf(u"team"_s, "clubName"), 16));
        }
        t.logoUrl = row.value(u"logoUrl"_s).toString();
        t.abbreviation = row.value(u"abbreviation"_s).toString();
        t.colour = row.value(u"primaryColour"_s).toString();
    }
    QString stadium = u"Stadium %1"_s.arg(m.stadium);
    for (const Rll3SaveLists::Choice& c : Rll3SaveLists::stadiums())
        if (c.value == m.stadium)
            stadium = QString::fromUtf8(c.name);
    QVariantMap out = CareerMatch::build(
        teams[0], teams[1], stadium,
        u"%1/%2/%3"_s.arg(m.day, 2, 10, QChar(u'0')).arg(m.month, 2, 10, QChar(u'0')).arg(m.year),
        u"%1:%2"_s.arg(m.hour, 2, 10, QChar(u'0')).arg(m.minute, 2, 10, QChar(u'0')), true);
    out[u"played"_s] = m.played;
    const Stage& st = kStages[stage];
    out[u"competition"_s] = titleCase(m_leagues.value(st.league).name) + QString::fromLatin1(st.suffix);
    out[u"competitionLogoUrl"_s] = competitionLogoUrl(m_leagues.value(st.league).kind);
    out[u"round"_s] = fixtureRounds(stage).value(m.round);
    return out;
}

// =================================================================================================================
// Raw Databases (the C# editor's Raw ... windows: a table of every record, edited as text, CSV in / out)
// =================================================================================================================

namespace {

struct RawCol {
    const char* name;
    const char* key;    // A field key, "#index" or "#<list cell>".
};

constexpr int kRawLeagueTeams = 36;    // The C# editor's columns.

const QList<RawCol> kRawPlayerCols = {
    {"Player Index", "#index"}, {"Player ID", "id"}, {"Player Original ID", "originalId"},
    {"First Name", "firstName"}, {"Last Name", "lastName"},
    {"DOB - Day", "dob.day"}, {"DOB - Month", "dob.month"}, {"DOB - Year", "dob.year"},
    {"Jersey Number", "jerseyNumber"}, {"Jersey Name", "jerseyName"}, {"Commentary Surname", "commentarySurname"},
    {"Commentary FullName", "commentaryFullName"}, {"Primary Role", "primaryRole"},
    {"Secondary Role", "secondaryRole"}, {"Tertiary Role", "tertiaryRole"}, {"State Of Origin", "stateOfOrigin"},
    {"City V's Country", "cityVsCountry"}, {"All Stars", "allStars"}, {"Preferred Hand", "preferredHand"},
    {"Preferred Foot", "preferredFoot"}, {"Weak Foot Rating", "weakFootRating"},
    {"Representative Country", "repCountry"}, {"Country Of Birth", "countryOfBirth"},
    {"Height", "appearance.height"}, {"Weight", "appearance.weight"}, {"Contract Expiry", "contractExpiry"},
    {"Player Salary", "salary"},
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
        cols = rawColumns({{"League Index", "#index"}, {"League ID", "#id"}, {"League Name", "#name"},
                           {"League Short Name", "#shortName"}, {"League Type", "#typeId"},
                           {"League Team Amount", "#teamAmount"}});
        for (int j = 0; j < kRawLeagueTeams; ++j)
            cols << RawColumn{u"Team %1"_s.arg(j + 1), u"#team%1"_s.arg(j)};
    } else if (id == u"status"_s) {
        cols = rawColumns({{"Player Index", "#index"}, {"Player ID", "#id"}, {"First Name", "#firstName"},
                           {"Last Name", "#lastName"}, {"Reason", "#reason"}, {"Duration", "#duration"},
                           {"AdditionalA", "#additionalA"}, {"AdditionalB", "#additionalB"}});
        for (RawColumn& c : cols)
            c.readOnly = c.key == u"#index"_s || c.key.endsWith(u"Name"_s);
        return cols;
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
    if (id == u"status"_s)
        return u"status"_s;
    return u"team"_s;
}

} // namespace

QVariantList Rll3CareerEditorManager::rawTables() const {
    const auto t = [](const char* id, const char* title) {
        return QVariantMap{{u"id"_s, QString::fromLatin1(id)}, {u"title"_s, QString::fromLatin1(title)}, {u"repair"_s, false}};
    };
    return {t("leagues", "Raw League Database"), t("players", "Raw Player Database"),
            t("appearance", "Raw Player Appearance Database"), t("status", "Raw Player Status Database"),
            t("teams", "Raw Team Database"), t("lineup", "Raw Team Lineup"), t("teamPlayers", "Raw Team Players")};
}

int Rll3CareerEditorManager::rawCount(const QString& kind) const {
    if (!m_loaded)
        return 0;
    if (kind == u"league"_s)
        return static_cast<int>(std::size(kLeagueAt));
    if (kind == u"status"_s)
        return qMin(kStatusMax, static_cast<int>(readNum(m_base + kStatusAt, 2, true, false)));
    return kind == u"team"_s ? teamCount() : playerCount();
}

qsizetype Rll3CareerEditorManager::rawAt(const QString& kind, int index) const {
    if (kind == u"team"_s || kind == u"player"_s)
        return recordAt(kind, index);
    if (index < 0 || index >= rawCount(kind))
        return -1;
    if (kind == u"league"_s)
        return m_base + kLeagueAt[index];
    return m_base + kStatusEntries + qsizetype(index) * kStatusSize;
}

// A cell that isn't a record field: where it is, its size (0 = text of `*max` bytes) and the biggest number.
bool Rll3CareerEditorManager::rawCellAt(const QString& kind, const QString& key, qsizetype* off, int* size, qint64* max) const {
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
        if (key == u"#name"_s) { *off = 1; *size = 0; *max = 52; return true; }
        if (key == u"#shortName"_s) { *off = 53; *size = 0; *max = 8; return true; }
        if (key == u"#typeId"_s) { *off = 61; *size = 0; *max = 15; return true; }
        if (key == u"#teamAmount"_s) { *off = 76; *size = 4; *max = kRawLeagueTeams; return true; }
        if (numbered(u"#team"_s, kRawLeagueTeams, &j)) { *off = 80 + 4 * j; *size = 4; *max = INT32_MAX; return true; }
        return false;
    }
    if (kind == u"status"_s) {
        if (key == u"#id"_s) { *off = 0; *size = 4; *max = INT32_MAX; return true; }
        if (key == u"#reason"_s) { *off = 4; *size = 1; *max = 255; return true; }
        if (key == u"#duration"_s) { *off = 5; *size = 2; *max = 65535; return true; }
        if (key == u"#additionalA"_s) { *off = 7; *size = 2; *max = 65535; return true; }
        if (key == u"#additionalB"_s) { *off = 9; *size = 2; *max = 65535; return true; }
    }
    return false;
}

QString Rll3CareerEditorManager::rawGet(const QString& kind, int index, const QString& key) const {
    const qsizetype at = rawAt(kind, index);
    if (at < 0)
        return {};
    if (key == u"#index"_s)
        return QString::number(index);
    qsizetype off = 0;
    int size = 0;
    qint64 max = 0;
    if (kind == u"status"_s && (key == u"#firstName"_s || key == u"#lastName"_s)) {
        const int p = playerIndexById(static_cast<int>(readNum(at, 4, true, true)));
        return p < 0 ? QString() : rawGet(u"player"_s, p, key == u"#firstName"_s ? u"firstName"_s : u"lastName"_s);
    }
    if (rawCellAt(kind, key, &off, &size, &max)) {
        if (size == 0)
            return readText(at + off, static_cast<int>(max));
        if (kind == u"league"_s && key.startsWith(u"#team"_s) && key != u"#teamAmount"_s
            && key.mid(5).toInt() >= readNum(at + 76, 4, false, true))
            return {};   // Past the league's teams.
        return QString::number(readNum(at + off, size, kind != u"league"_s, size == 4));
    }
    const FieldDef* d = defFor(kind, key);
    if (!d)
        return {};
    switch (d->type) {
    case Type::Virtual: return virtualValue(at, key).toString();
    case Type::Str: return readText(at + d->off, d->size);
    case Type::Bool: return RawTableModel::boolText(m_data.at(at + d->off) != 0);
    case Type::Palette: return QString::number(static_cast<quint8>(m_data.at(at + d->off)));
    default: return QString::number(readNum(at + d->off, d->size, d->big, d->type == Type::I16 || d->type == Type::I32));
    }
}

bool Rll3CareerEditorManager::rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error) {
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
        if (kind == u"league"_s && key.startsWith(u"#team"_s) && key != u"#teamAmount"_s
            && key.mid(5).toInt() >= readNum(at + 76, 4, false, true))
            return false;
        writeNum(at + off, size, kind != u"league"_s, v);
        return true;
    }
    const FieldDef* d = defFor(kind, key);
    if (!d)
        return false;
    switch (d->type) {
    case Type::Virtual: return false;
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

bool Rll3CareerEditorManager::fillRawTable(const QString& id, RawTableModel* model) const {
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

QString Rll3CareerEditorManager::applyRawTable(RawTableModel* model) {
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
        if (kind == u"league"_s)
            loadCareer();
        edited();
    }
    fillRawTable(id, model);
    if (!errors.isEmpty())
        return errors;
    setStatus(true, changed > 0 ? u"Saved %1 changes from %2."_s.arg(changed).arg(model->title())
                                : u"Nothing changed in %1."_s.arg(model->title()));
    return {};
}

int Rll3CareerEditorManager::repairRawIds(RawTableModel*) const { return 0; }
