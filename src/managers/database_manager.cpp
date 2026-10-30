#include "database_manager.h"

#include "app_orchestrator.h"
#include "blobset_entry_registry.h"
#include "dds_image_provider.h"
#include "m3mp_tool_manager.h"
#include "pa_serial.h"
#include "rl26_lists.h"
#include "rll3_lists.h"
#include "rll4_lists.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QtEndian>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;
using BigAntDb::Table;
using BigAntDb::ValueType;

namespace {

const QStringList kDatabaseHashes = {u"2BE30C8187CABAF6"_s, u"CE54601F"_s};

const QStringList kRoleNames = {u"Fullback"_s, u"Winger"_s, u"Centre"_s, u"Five-Eighth"_s, u"Halfback"_s,
                                u"Prop"_s, u"Hooker"_s, u"Second Row"_s, u"Lock"_s};
const QStringList kRoleShort = {u"FB"_s, u"WG"_s, u"C"_s, u"FE"_s, u"HB"_s, u"FR"_s, u"HK"_s, u"SR"_s, u"L"_s};
const QStringList kTeamRoleKeys = {u"captain"_s, u"goalKicker"_s, u"playMaker1"_s, u"playMaker2"_s};

// Not ratings: the identity, looks and other numbers of a player.
const QStringList kNotSkills = {
    u"id"_s, u"club"_s, u"sex"_s, u"shirtNumber"_s, u"birthDay"_s, u"birthMonth"_s, u"birthYear"_s, u"age"_s,
    u"countryOfBirth"_s, u"representativeCountry"_s, u"stateOfOrigin"_s, u"originRepNumber"_s,
    u"originOtherNumber"_s, u"cityVsCountry"_s, u"allStars"_s, u"contractExpiry"_s, u"preferredRolePrimary"_s,
    u"preferredRoleSecondary"_s, u"preferredRoleTertiary"_s, u"height"_s, u"weight"_s, u"perk"_s, u"head"_s,
    u"hairTop"_s, u"photogrammetryStatus"_s, u"movementProfile"_s, u"temperament"_s, u"conversionStyle"_s,
    // Rugby League Live 3.
    u"unknown1"_s, u"preferredHand"_s, u"preferredFoot"_s, u"weakFootRating"_s, u"commentarySurname"_s,
    u"commentaryFullName"_s};

QString toLocalPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString titleCase(const QString& text) {
    QString out = text.toLower();
    if (!out.isEmpty())
        out[0] = out[0].toUpper();
    return out;
}

QString csvField(const QVariant& value) {
    QString text = value.toString();
    if (text.contains(u',') || text.contains(u'"') || text.contains(u'\n'))
        text = u'"' + text.replace(u"\""_s, u"\"\""_s) + u'"';
    return text;
}

QString typeName(ValueType type) {
    switch (type) {
    case ValueType::Bool: return u"bool"_s;
    case ValueType::Hash: return u"hash"_s;
    case ValueType::String: return u"string"_s;
    case ValueType::Colour: return u"colour"_s;
    case ValueType::Lineup: return u"lineup"_s;
    case ValueType::Derived: return u"derived"_s;
    default: return u"int"_s;
    }
}

QVariantMap fieldTypes(const Table& table) {
    QVariantMap map;
    for (const BigAntDb::Field& f : table.schema().fields)
        if (f.type != ValueType::Derived)
            map.insert(f.key, typeName(f.type));
    for (const BigAntDb::List& l : table.schema().lists)
        map.insert(l.name + u'!', u"list"_s);
    return map;
}

QString skillGroup(const QString& key) {
    const QString k = key.toLower();
    static const QStringList main = {u"speed"_s, u"strength"_s, u"aggression"_s, u"injurytolerance"_s, u"agility"_s, u"stamina"_s,
                                     u"fitness"_s, u"acceleration"_s, u"sprintspeed"_s, u"balance"_s};
    static const QStringList mind = {u"reputation"_s, u"ego"_s, u"loyalty"_s, u"focus"_s, u"leadership"_s,
                                     u"determination"_s, u"discipline"_s};
    if (main.contains(k))
        return u"Main Stats"_s;
    if (mind.contains(k))
        return u"Personality"_s;
    if (k.contains(u"kick"_s) || k == u"toepoke"_s || k == u"kickingaccuracy"_s || k == u"kickingpower"_s)
        return u"Kicking"_s;
    if (k.contains(u"pass"_s) || k == u"offload"_s)
        return u"Passing"_s;
    if (k == u"fend"_s || k == u"fending"_s || k == u"sidestep"_s || k == u"breaktackle"_s || k == u"evasion"_s)
        return u"Evasion"_s;
    if (k.contains(u"tackl"_s) || k == u"jackal"_s || k == u"rucking"_s || k == u"scrummaging"_s
        || k == u"intercepting"_s)
        return u"Tackling"_s;
    return u"Misc"_s;
}

QString niceLabel(const QString& key) {
    static const QHash<QString, QString> special = {
        {u"aggression"_s, u"Discipline"_s},  {u"injuryTolerance"_s, u"Durability"_s},
        {u"groundKick"_s, u"Grubber Kick"_s}, {u"fieldgoalKick"_s, u"Field Goal Kick"_s},
        {u"shortName"_s, u"Location Name"_s}, {u"nickname"_s, u"Club Name"_s}, {u"hudName"_s, u"Abbreviation"_s},
        {u"icon"_s, u"Logo"_s}, {u"worldCupIcon"_s, u"World Cup Logo"_s}, {u"shirtName"_s, u"Jersey Name"_s},
        {u"shirtNumber"_s, u"Jersey Number"_s}, {u"firstname"_s, u"First Name"_s}, {u"surname"_s, u"Last Name"_s},
        {u"sex"_s, u"Gender"_s},
        // Lineup positions.
        {u"fullBack"_s, u"Fullback"_s}, {u"wingLeft"_s, u"Left Wing"_s}, {u"wingRight"_s, u"Right Wing"_s},
        {u"centreLeft"_s, u"Left Centre"_s}, {u"centreRight"_s, u"Right Centre"_s}, {u"fiveEighth"_s, u"Five-Eighth"_s},
        {u"halfBack"_s, u"Halfback"_s}, {u"propLeft"_s, u"Left Prop"_s}, {u"propRight"_s, u"Right Prop"_s},
        {u"secondRowLeft"_s, u"Left Second Row"_s}, {u"secondRowRight"_s, u"Right Second Row"_s},
        {u"lockLeft"_s, u"Left Lock"_s}, {u"lockRight"_s, u"Right Lock"_s}, {u"blindFlank"_s, u"Blindside Flanker"_s},
        {u"openFlank"_s, u"Openside Flanker"_s}, {u"numberEight"_s, u"Number Eight"_s}, {u"scrumHalf"_s, u"Scrum-Half"_s},
        {u"flyHalf"_s, u"Fly-Half"_s}, {u"insideCentre"_s, u"Inside Centre"_s}, {u"outsideCentre"_s, u"Outside Centre"_s}};
    const QString field = key.mid(key.lastIndexOf(u'.') + 1);
    if (special.contains(field))
        return special.value(field);
    static const QRegularExpression substitute(u"^Substitute([0-9]*)$"_s);
    if (const QRegularExpressionMatch sub = substitute.match(field); sub.hasMatch())
        return u"Interchange %1"_s.arg(sub.captured(1).isEmpty() ? u"1"_s : sub.captured(1));
    static const QRegularExpression career(u"^(nrl|sl)career(.*)$"_s, QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = career.match(field);
    if (m.hasMatch()) {
        static const QHash<QString, QString> what = {{u"appearances"_s, u"Appearances"_s}, {u"triesscored"_s, u"Tries"_s},
                                                     {u"goals"_s, u"Goals"_s}, {u"goalsattempted"_s, u"Goals Attempted"_s},
                                                     {u"points"_s, u"Points"_s}};
        return (m.captured(1).toLower() == u"nrl"_s ? u"NRL "_s : u"Super League "_s)
               + what.value(m.captured(2).toLower(), m.captured(2));
    }
    QString label = field;
    label.replace(u'#', u' ').replace(u':', u' ');
    return PaSerial::prettyName(label);
}

QString listTitle(const QString& list) {
    if (list == u"NRL2026Match"_s) return u"NRL 2026"_s;
    if (list == u"LeagueMatch"_s) return u"League"_s;
    if (list == u"NinesMatch"_s || list == u"Nines"_s) return u"Nines"_s;
    if (list == u"StandardMatch"_s) return u"Standard"_s;
    if (list == u"UnionMatch"_s) return u"Union"_s;
    return PaSerial::prettyName(list);
}

// Jersey blob layout: ConvantClub = {u16 count, paUniform[N], u16 slot count, u8 slots[...]}.
struct KitLayout {
    int uniformType = -1;
    quint32 uniformSize = 0;
    qint64 countOffset = -1;
    int countSize = 2;
    qint64 arrayOffset = -1;
    int arraySize = 0;
    qint64 slotsOffset = -1;
    int slotCount = 0;
    qint64 nameOffset = -1;
    int nameLength = 0;
    qint64 hiddenOffset = -1;
    qint64 ninesOffset = -1;
    bool ok = false;
};

KitLayout kitLayout(const PaSerial::Blob& blob) {
    KitLayout k;
    if (!blob.isValid())
        return k;
    const PaSerial::Type& root = blob.types()[blob.root()];
    const QList<PaSerial::Member>& m = root.members;
    int arrayIndex = -1;
    for (int i = 0; i < m.size(); ++i)
        if (m[i].type >= 0 && m[i].count > 1)
            arrayIndex = i;
    if (arrayIndex < 1)
        return k;
    k.uniformType = m[arrayIndex].type;
    k.uniformSize = blob.types()[k.uniformType].size;
    k.arrayOffset = m[arrayIndex].offset;
    k.arraySize = m[arrayIndex].count;
    k.countOffset = m[arrayIndex - 1].offset;
    k.countSize = m[arrayIndex - 1].elementSize * m[arrayIndex - 1].count;
    if (k.countSize != 1 && k.countSize != 2 && k.countSize != 4)
        return k;
    // After the uniforms: the slot count, then the slots (one byte each: a uniform index).
    if (arrayIndex + 2 < m.size()) {
        k.slotsOffset = m[arrayIndex + 2].offset;
        k.slotCount = m[arrayIndex + 2].count * m[arrayIndex + 2].elementSize;
    }
    for (const PaSerial::Member& u : blob.types()[k.uniformType].members) {
        if (u.type >= 0)
            continue;
        if ((u.name == u"name"_s || (u.offset == 0 && u.count >= 8)) && k.nameOffset < 0) {
            k.nameOffset = u.offset;
            k.nameLength = u.count;
        } else if (u.name == u"isHidden"_s) {
            k.hiddenOffset = u.offset;
        } else if (u.name == u"m_isNines"_s) {
            k.ninesOffset = u.offset;
        }
    }
    k.ok = true;
    return k;
}

// Rugby League Live 3: the uniforms are inside the team's blob (Wouldy's RLL3 Database Editor): the count is at 139,
// then 6 fixed slots of 100 bytes from 1058 (1010 in the 1814 byte blobs):
//   name[25], ?, ?[29], u8 manufacturer, u16 licensed jersey, ?[29], u8 show leading zero, u8 name font,
//   u8 name colour, ?, u8 number font, u8 number font size, u8 keyline size, u8 keyline offset, u8 number colour,
//   u8 keyline colour, ?[3]   (colours are palette numbers)
struct Rll3Kit {
    static constexpr qint64 kCountAt = 139;
    static constexpr int kSlots = 6;
    static constexpr int kSize = 100;
    qint64 start = -1;
    bool ok() const { return start >= 0; }
};

Rll3Kit rll3Kit(const QByteArray& blob) {
    Rll3Kit k;
    const qint64 size = blob.size();
    k.start = size == 1887 || size == 1885 ? 1058 : size == 1814 ? 1010 : -1;
    if (k.start >= 0 && k.start + Rll3Kit::kSlots * Rll3Kit::kSize > size)
        k.start = -1;
    return k;
}

QString rawText(const QByteArray& data, qint64 at, int length) {
    QByteArray text = data.mid(at, length);
    if (const qsizetype zero = text.indexOf('\0'); zero >= 0)
        text.truncate(zero);
    return QString::fromLatin1(text);
}

// The RLL3 jersey fields for the editor, in PaSerial::Blob::fields() form.
QVariantList rll3KitFields(const QByteArray& blob, qint64 base) {
    QVariantList out;
    const auto byteAt = [&blob](qint64 at) { return static_cast<int>(static_cast<uchar>(blob.at(at))); };
    out << QVariantMap{{u"key"_s, u"t:%1:25"_s.arg(base)}, {u"label"_s, u"Name"_s}, {u"group"_s, u"Uniform"_s},
                       {u"kind"_s, u"text"_s}, {u"value"_s, rawText(blob, base, 25)}, {u"max"_s, 24}};
    // Manufacturer, then that manufacturer's licensed jerseys (0 = none); the jersey list changes with the manufacturer.
    const int manufacturer = byteAt(base + 55);
    QVariantList makers, jerseys{QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}}};
    for (const Rll3Lists::Choice& c : Rll3Lists::manufacturers())
        makers << QVariantMap{{u"name"_s, QString::fromLatin1(c.name)}, {u"value"_s, c.value}};
    for (const Rll3Lists::Choice& c : Rll3Lists::licensedJerseys(manufacturer))
        jerseys << QVariantMap{{u"name"_s, QString::fromLatin1(c.name)}, {u"value"_s, c.value}};
    out << QVariantMap{{u"key"_s, u"n:%1:1"_s.arg(base + 55)}, {u"label"_s, u"Manufacturer"_s}, {u"group"_s, u"Uniform"_s},
                       {u"kind"_s, u"choice"_s}, {u"value"_s, manufacturer}, {u"choices"_s, makers}, {u"max"_s, 255},
                       {u"refresh"_s, true}};
    out << QVariantMap{{u"key"_s, u"n:%1:2"_s.arg(base + 56)}, {u"label"_s, u"Licensed Jersey"_s}, {u"group"_s, u"Uniform"_s},
                       {u"kind"_s, u"choice"_s}, {u"value"_s, qFromLittleEndian<quint16>(blob.constData() + base + 56)},
                       {u"choices"_s, jerseys}, {u"max"_s, 65535}};
    out << QVariantMap{{u"key"_s, u"b:%1"_s.arg(base + 87)}, {u"label"_s, u"Show Leading Zero"_s}, {u"group"_s, u"Name & Number"_s},
                       {u"kind"_s, u"bool"_s}, {u"value"_s, byteAt(base + 87) != 0}};
    static const QList<QPair<int, QString>> bytes = {
        {88, u"Name Font"_s}, {89, u"Name Colour (palette)"_s}, {91, u"Number Font"_s}, {92, u"Number Font Size"_s},
        {93, u"Keyline Size"_s}, {94, u"Keyline Offset"_s}, {95, u"Number Colour (palette)"_s},
        {96, u"Keyline Colour (palette)"_s}};
    for (const auto& [offset, label] : bytes)
        out << QVariantMap{{u"key"_s, u"n:%1:1"_s.arg(base + offset)}, {u"label"_s, label}, {u"group"_s, u"Name & Number"_s},
                           {u"kind"_s, u"number"_s}, {u"value"_s, byteAt(base + offset)}, {u"max"_s, 255}};
    return out;
}

// Applies a field key ("t:at:length", "n:at:size", "b:at") to raw little endian bytes. True when it changed.
bool setRawField(QByteArray& data, const QString& key, const QVariant& value) {
    const QStringList parts = key.split(u':');
    if (parts.size() < 2)
        return false;
    const qint64 at = parts[1].toLongLong();
    if (parts[0] == u"t"_s && parts.size() == 3) {
        const int length = parts[2].toInt();
        if (at < 0 || length <= 0 || at + length > data.size())
            return false;
        QByteArray text = value.toString().toLatin1().left(length - 1);
        if (rawText(data, at, length).toLatin1() == text)
            return false;
        text.resize(length, '\0');
        data.replace(at, length, text);
        return true;
    }
    const int size = parts[0] == u"b"_s ? 1 : parts.value(2).toInt();
    if (at < 0 || (size != 1 && size != 2) || at + size > data.size())
        return false;
    const quint32 v = parts[0] == u"b"_s ? (value.toBool() ? 1 : 0) : std::min<quint32>(value.toUInt(), size == 1 ? 255 : 65535);
    const quint32 old = size == 1 ? static_cast<uchar>(data.at(at)) : qFromLittleEndian<quint16>(data.constData() + at);
    if (old == v)
        return false;
    if (size == 1)
        data[at] = static_cast<char>(v);
    else
        qToLittleEndian<quint16>(static_cast<quint16>(v), data.data() + at);
    return true;
}

} // namespace

bool DatabaseManager::isDatabaseHash(const QString& hexHash) {
    bool ok = false;
    const quint64 value = hexHash.trimmed().toULongLong(&ok, 16);
    if (!ok)
        return false;
    for (const QString& h : kDatabaseHashes)
        if (h.toULongLong(nullptr, 16) == value)
            return true;
    return false;
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
    m_m3mp = new M3mpToolManager(this);
    connect(m_m3mp, &M3mpToolManager::statusChanged, this, [this]() {
        if (m_saving)
            setStatus(m_m3mp->statusOk(), m_m3mp->statusText());
    });
    connect(m_m3mp, &M3mpToolManager::processComplete, this, [this](bool ok, const QString& message) {
        if (!m_saving)
            return;
        m_saving = false;
        setBusy(false);
        if (ok) {
            m_unsaved = false;
            emit unsavedChangesChanged();
            m_sourceText = u"Your edited copy in the mods folder"_s;
            emit loadedChanged();
            setStatus(true, u"Saved to %1 - click Modify in the workspace to put it into the game."_s.arg(modsTarget()));
        } else {
            setStatus(false, message);
        }
        emit finished(ok, ok ? m_statusText : message);
    });
}

DatabaseManager::~DatabaseManager() {
    if (!m_saveTemp.isEmpty() && !m_saving)
        QDir(m_saveTemp).removeRecursively();
}

QString DatabaseManager::gameName() const {
    if (m_game == u"rl26"_s) return u"Rugby League 26"_s;
    if (m_game == u"rll3"_s) return u"Rugby League Live 3"_s;
    if (m_game == u"rll4"_s) return u"Rugby League Live 4"_s;
    if (m_game == u"r25"_s) return u"Rugby 25"_s;
    return {};
}

QString DatabaseManager::modsTarget() const {
    return m_loaded ? m_m3mp->modsTarget() : QString();
}

void DatabaseManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void DatabaseManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void DatabaseManager::edited() {
    ++m_revision;
    emit revisionChanged();
    if (!m_unsaved) {
        m_unsaved = true;
        emit unsavedChangesChanged();
    }
}

void DatabaseManager::rebuildIndexes() {
    m_playerById.clear();
    m_teamById.clear();
    for (int i = 0; i < m_players.count(); ++i) {
        const int id = m_players.num(i, u"id"_s);
        if (!m_playerById.contains(id))
            m_playerById.insert(id, i);
    }
    for (int i = 0; i < m_teams.count(); ++i) {
        const int id = m_teams.num(i, u"id"_s);
        if (!m_teamById.contains(id))
            m_teamById.insert(id, i);
    }
    // Games without a player "club" field (RLL3): the first squad a player is in.
    m_playerClub.clear();
    if (!m_players.schema().has(u"club"_s))
        for (int t = 0; t < m_teams.count(); ++t)
            for (int id : slotValues(t, m_rosterKeys))
                if (!m_playerClub.contains(id))
                    m_playerClub.insert(id, t);
}

QStringList DatabaseManager::listFieldKeys(const QString& list, bool includeDerived) const {
    QStringList keys;
    const BigAntDb::Schema& s = m_teams.schema();
    const int l = s.list(list);
    if (l < 0)
        return keys;
    for (int f : s.lists[l].fields)
        if (includeDerived || s.fields[f].type != ValueType::Derived)
            keys << s.fields[f].key;
    return keys;
}

QStringList DatabaseManager::lineupKeys(const QString& list) const {
    // The players of a match list, in the game's numbering: the files keep league teams as fullback, left wing,
    // left centre, right centre, right wing, ... but the game numbers them 1 fullback, 2 right wing, 3 right
    // centre, 4 left centre, 5 left wing, 6 five-eighth, 7 halfback, 8 prop, 9 hooker, 10 prop, 11 / 12 second row,
    // 13 lock (the C# editor's winger / centre swap). Union and Nines lists are already in order.
    QStringList keys;
    for (const QString& k : listFieldKeys(list))
        if (!kTeamRoleKeys.contains(k.mid(k.indexOf(u'.') + 1)))
            keys << k;
    static const QStringList league = {u"fullBack"_s, u"wingRight"_s, u"centreRight"_s, u"centreLeft"_s, u"wingLeft"_s,
                                       u"fiveEighth"_s, u"halfBack"_s, u"propLeft"_s, u"hooker"_s, u"propRight"_s,
                                       u"secondRowLeft"_s, u"secondRowRight"_s, u"Lock"_s};
    const QString prefix = list + u'.';
    for (const char* side : {"wingLeft", "wingRight", "centreLeft", "centreRight"})
        if (!keys.contains(prefix + QLatin1StringView(side)))
            return keys;   // Nines / Union: already in order.
    QStringList ordered;
    for (const QString& name : league)
        if (keys.contains(prefix + name))
            ordered << prefix + name;
    for (const QString& k : std::as_const(keys))
        if (!ordered.contains(k))
            ordered << k;   // The substitutes, in file order.
    return ordered;
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void DatabaseManager::load() {
    if (m_busy)
        return;
    const auto fail = [this](const QString& message) {
        m_loaded = false;
        m_game.clear();
        emit loadedChanged();
        setStatus(false, message);
        emit finished(false, message);
    };

    QString hash;
    BlobsetEntry entry;
    for (const QString& h : kDatabaseHashes) {
        if (BlobsetEntryRegistry::findEntry(h, entry)) {
            hash = h;
            break;
        }
    }
    if (hash.isEmpty()) {
        fail(u"This game has no team / player database the editor knows (Rugby League 26, Rugby League Live 4 and Rugby 25 are supported)."_s);
        return;
    }
    m_m3mp->loadM3mpFromBlobset(hash);
    if (m_m3mp->entryCount() == 0) {
        fail(m_m3mp->statusText().isEmpty() ? u"The database M3MP (0x%1) couldn't be opened."_s.arg(hash) : m_m3mp->statusText());
        return;
    }
    const QByteArray teamsData = m_m3mp->entryDataByPath(QString::fromLatin1(kTeamsPath));
    const QByteArray playersData = m_m3mp->entryDataByPath(QString::fromLatin1(kPlayersPath));
    if (teamsData.isEmpty() || playersData.isEmpty()) {
        fail(u"The M3MP 0x%1 has no defaultdata_clubs / defaultdata_players."_s.arg(hash));
        return;
    }

    Table teams;
    Table players;
    // RL26 / RLL4 / Rugby 25 describe their fields; Rugby League Live 3 has the older fixed layout.
    bool rll3 = false;
    QString error = teams.read(teamsData);
    if (!error.isEmpty()) {
        if (!teams.readRll3(teamsData, true).isEmpty()) {
            fail(u"defaultdata_clubs: "_s + error);
            return;
        }
        rll3 = true;
    }
    error = rll3 ? players.readRll3(playersData, false) : players.read(playersData);
    if (!error.isEmpty()) {
        fail(u"defaultdata_players: "_s + error);
        return;
    }

    m_teams = teams;
    m_players = players;
    m_hash = hash;
    m_game = rll3 ? u"rll3"_s
             : m_teams.schema().newTypeNumbers ? u"r25"_s
             : m_teams.schema().list(u"NRL2026Match"_s) >= 0 ? u"rl26"_s : u"rll4"_s;
    m_logoUrls.clear();
    loadCommentary();
    m_teamTypes = fieldTypes(m_teams);
    m_playerTypes = fieldTypes(m_players);
    m_rosterKeys = listFieldKeys(u"roster"_s);

    // The C# editor's rating / randomize stat names -> this database's fields.
    m_ratingFields.clear();
    static const QHash<QString, QStringList> candidates = {
        {u"durability"_s, {u"injuryTolerance"_s, u"durability"_s}},
        {u"discipline"_s, {u"aggression"_s, u"discipline"_s}},
        {u"stamina"_s, {u"stamina"_s, u"fitness"_s}},
        {u"grubberKick"_s, {u"grubberKick"_s, u"groundKick"_s}}};
    QStringList names;
    for (const auto& terms : Rl26Lists::ratingTerms())
        for (const Rl26Lists::RatingTerm& t : terms)
            names << t.field;
    for (const Rl26Lists::RandomRange& r : Rl26Lists::randomRanges())
        names << r.field;
    for (const QString& n : std::as_const(names)) {
        if (m_ratingFields.contains(n))
            continue;
        QString found;
        for (const QString& c : candidates.value(n, {n})) {
            if (m_players.schema().has(c)) {
                found = c;
                break;
            }
        }
        m_ratingFields.insert(n, found);
    }

    rebuildIndexes();
    m_loaded = true;
    m_sourceText = m_m3mp->loadedFromMods() ? u"Your edited copy in the mods folder"_s : u"The game's database"_s;
    if (m_unsaved) {
        m_unsaved = false;
        emit unsavedChangesChanged();
    }
    ++m_revision;
    emit revisionChanged();
    emit loadedChanged();
    setStatus(true, u"%1: loaded %2 teams and %3 players (%4)."_s.arg(gameName()).arg(m_teams.count())
                        .arg(m_players.count()).arg(m_sourceText.toLower()));
    emit finished(true, m_statusText);
}

void DatabaseManager::save() {
    if (!m_loaded || m_busy)
        return;
    if (m_m3mp->modsTarget().isEmpty()) {
        setStatus(false, u"The database's file name isn't in the file mapping, so it can't be saved to the mods folder "
                         "(Update File Mapping Data / Resolve File Names)."_s);
        emit finished(false, m_statusText);
        return;
    }
    if (m_saveTemp.isEmpty())
        m_saveTemp = QDir(QDir::tempPath()).filePath(u"BlobsetTools-DB-%1"_s.arg(QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(m_saveTemp);

    const auto writeFile = [this](const QString& name, const QByteArray& data) -> QString {
        QSaveFile file(QDir(m_saveTemp).filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            return QString();
        return file.fileName();
    };
    const QString teamsFile = writeFile(u"defaultdata_clubs"_s, m_teams.write());
    const QString playersFile = writeFile(u"defaultdata_players"_s, m_players.write());
    if (teamsFile.isEmpty() || playersFile.isEmpty()) {
        setStatus(false, u"Could not write the databases to %1."_s.arg(QDir::toNativeSeparators(m_saveTemp)));
        emit finished(false, m_statusText);
        return;
    }
    if (!m_m3mp->replaceEntryByPath(QString::fromLatin1(kTeamsPath), teamsFile)
        || !m_m3mp->replaceEntryByPath(QString::fromLatin1(kPlayersPath), playersFile)) {
        setStatus(false, m_m3mp->statusText());
        emit finished(false, m_statusText);
        return;
    }
    m_saving = true;
    setBusy(true);
    setStatus(true, u"Saving the database to the mods folder..."_s);
    m_m3mp->saveChanges();
}

QString DatabaseManager::exportCsv(const QString& kind, const QString& pathOrUrl) const {
    if (!m_loaded)
        return u"Nothing is loaded."_s;
    QStringList lines;
    const bool teams = kind == u"teams"_s;
    const QVariantList rows = teams ? teamRows(QString(), -1) : playerRows(QString(), -1, -1, -1);
    const QStringList keys = teams ? QStringList{u"index"_s, u"id"_s, u"gender"_s, u"fullName"_s, u"location"_s,
                                                 u"club"_s, u"abbreviation"_s, u"rating"_s, u"players"_s}
                                   : QStringList{u"index"_s, u"id"_s, u"gender"_s, u"first"_s, u"last"_s,
                                                 u"clubName"_s, u"roleName"_s, u"rating"_s, u"age"_s};
    lines << (teams ? u"Index,ID,Gender,Full Name,Location,Club,Abbreviation,Rating,Players"_s
                    : u"Index,ID,Gender,First Name,Last Name,Club,Primary Role,Rating,Age"_s);
    for (const QVariant& row : rows) {
        const QVariantMap r = row.toMap();
        QStringList cells;
        for (const QString& key : keys)
            cells << csvField(r.value(key));
        lines << cells.join(u',');
    }
    QSaveFile file(toLocalPath(pathOrUrl));
    if (!file.open(QIODevice::WriteOnly))
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    file.write(lines.join(u"\r\n"_s).toUtf8() + "\r\n");
    if (!file.commit())
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    return {};
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

QVariantList DatabaseManager::teamRows(const QString& filter, int gender) const {
    QVariantList rows;
    if (!m_loaded)
        return rows;
    const QString query = filter.trimmed();
    bool isNumber = false;
    const int queryId = query.toInt(&isNumber);
    for (int i = 0; i < m_teams.count(); ++i) {
        const int teamGender = m_teams.num(i, u"sex"_s);
        if (gender >= 0 && teamGender != gender)
            continue;
        const QString full = teamFullName(i);
        const QString location = m_teams.str(i, u"shortName"_s);
        const QString club = m_teams.str(i, u"nickname"_s);
        const int id = m_teams.num(i, u"id"_s);
        if (!query.isEmpty() && !(isNumber && id == queryId) && !full.contains(query, Qt::CaseInsensitive)
            && !(location + u' ' + club).contains(query, Qt::CaseInsensitive))
            continue;
        rows.append(QVariantMap{{u"index"_s, i},
                                {u"id"_s, id},
                                {u"gender"_s, teamGender},
                                {u"fullName"_s, full},
                                {u"location"_s, location},
                                {u"club"_s, club},
                                {u"abbreviation"_s, m_teams.str(i, u"hudName"_s)},
                                {u"logoUrl"_s, logoUrl(m_teams.str(i, u"icon"_s))},
                                {u"primaryColour"_s, m_teams.str(i, u"primaryRGBColour"_s)},
                                {u"rating"_s, teamRating(i)},
                                {u"players"_s, slotValues(i, m_rosterKeys).size()}});
    }
    return rows;
}

int DatabaseManager::age(int i) const {
    const QDate dob(m_players.num(i, u"birthYear"_s), m_players.num(i, u"birthMonth"_s), m_players.num(i, u"birthDay"_s));
    if (!dob.isValid())
        return m_players.num(i, u"age"_s);
    const QDate today = QDate::currentDate();
    int years = today.year() - dob.year();
    if (today < dob.addYears(years))
        --years;
    return years;
}

QVariantList DatabaseManager::playerRows(const QString& filter, int gender, int team, int role) const {
    QVariantList rows;
    if (!m_loaded)
        return rows;
    const QString query = filter.trimmed();
    bool isNumber = false;
    const int queryId = query.toInt(&isNumber);

    QList<int> indexes;
    if (team >= 0 && team < m_teams.count()) {
        for (int id : slotValues(team, m_rosterKeys))
            indexes << playerIndexById(id);
    } else {
        indexes.reserve(m_players.count());
        for (int i = 0; i < m_players.count(); ++i)
            indexes << i;
    }
    const QStringList clubs = teamNames();
    const bool rugbyLeague = m_game != u"r25"_s;
    const bool hasClub = m_players.schema().has(u"club"_s);
    for (int i : std::as_const(indexes)) {
        if (i < 0)
            continue;
        const int playerGender = m_players.num(i, u"sex"_s);
        const int primary = m_players.num(i, u"preferredRolePrimary"_s);
        if (gender >= 0 && playerGender != gender)
            continue;
        if (role >= 0 && primary != role)
            continue;
        const QString first = m_players.str(i, u"firstname"_s);
        const QString last = m_players.str(i, u"surname"_s);
        const int id = m_players.num(i, u"id"_s);
        if (!query.isEmpty() && !(isNumber && id == queryId) && !(first + u' ' + last).contains(query, Qt::CaseInsensitive))
            continue;
        const int club = hasClub ? m_players.num(i, u"club"_s) : m_playerClub.value(id, -1);
        rows.append(QVariantMap{{u"index"_s, i},
                                {u"id"_s, id},
                                {u"gender"_s, playerGender},
                                {u"first"_s, first},
                                {u"last"_s, last},
                                {u"club"_s, club},
                                {u"clubName"_s, club >= 0 && club < clubs.size() ? clubs[club] : QString()},
                                {u"role"_s, primary},
                                {u"roleName"_s, rugbyLeague ? kRoleShort.value(primary) : QString::number(primary)},
                                {u"rating"_s, playerRating(i)},
                                {u"age"_s, age(i)}});
    }
    return rows;
}

// ---------------------------------------------------------------------------
// One team / player
// ---------------------------------------------------------------------------

QVariantMap DatabaseManager::tableRecord(const Table& table, int index) const {
    QVariantMap map;
    if (index < 0 || index >= table.count())
        return map;
    const BigAntDb::Schema& s = table.schema();
    const BigAntDb::Record& rec = table.record(index);
    for (int f = 0; f < s.fields.size(); ++f) {
        if (s.fields[f].type == ValueType::Derived)
            continue;
        map.insert(s.fields[f].key, table.get(index, f));
        map.insert(s.fields[f].key + u'?', rec.listPresent[s.fields[f].list] && rec.values[f].flag);
    }
    for (int l = 0; l < s.lists.size(); ++l)
        map.insert(s.lists[l].name + u'!', rec.listPresent[l] != 0);
    map.insert(u"_index"_s, index);
    return map;
}

QVariantMap DatabaseManager::team(int index) const {
    QVariantMap map = tableRecord(m_teams, index);
    if (!map.isEmpty()) {
        map.insert(u"_rating"_s, teamRating(index));
        map.insert(u"_logoUrl"_s, logoUrl(m_teams.str(index, u"icon"_s)));
        map.insert(u"_players"_s, slotValues(index, m_rosterKeys).size());
    }
    return map;
}

QVariantMap DatabaseManager::player(int index) const {
    QVariantMap map = tableRecord(m_players, index);
    if (!map.isEmpty()) {
        map.insert(u"_rating"_s, playerRating(index));
        map.insert(u"_age"_s, age(index));
        const int id = m_players.num(index, u"id"_s);
        const int club = m_players.schema().has(u"club"_s) ? m_players.num(index, u"club"_s) : m_playerClub.value(id, -1);
        map.insert(u"_clubName"_s, club >= 0 && club < m_teams.count() ? teamFullName(club) : QString());
    }
    return map;
}

bool DatabaseManager::setTeamField(int index, const QString& key, const QVariant& value) {
    if (!m_loaded || m_busy)
        return false;
    const bool changed = m_teams.set(index, key, value);
    // The C# editor keeps the World Cup logo the same as the logo.
    if (changed && key == u"icon"_s && m_teams.isSet(index, u"worldCupIcon"_s))
        m_teams.set(index, u"worldCupIcon"_s, value);
    if (changed && key == u"id"_s)
        rebuildIndexes();
    if (changed)
        edited();
    return changed;
}

bool DatabaseManager::setPlayerField(int index, const QString& key, const QVariant& value) {
    if (!m_loaded || m_busy)
        return false;
    const bool changed = m_players.set(index, key, value);
    if (changed && key == u"id"_s)
        rebuildIndexes();
    if (changed)
        edited();
    return changed;
}

bool DatabaseManager::setTeamPresent(int index, const QString& key, bool on) {
    if (!m_loaded || m_busy || !m_teams.setPresent(index, key, on))
        return false;
    edited();
    return true;
}

bool DatabaseManager::setPlayerPresent(int index, const QString& key, bool on) {
    if (!m_loaded || m_busy || !m_players.setPresent(index, key, on))
        return false;
    edited();
    return true;
}

QVariantList DatabaseManager::playerSkillFields() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const BigAntDb::Schema& s = m_players.schema();
    static const QStringList order = {u"Main Stats"_s, u"Personality"_s, u"Kicking"_s, u"Passing"_s, u"Evasion"_s,
                                      u"Tackling"_s, u"Misc"_s};
    QList<QVariantMap> items;
    for (int f : s.lists.value(0).fields) {
        const BigAntDb::Field& field = s.fields[f];
        if (field.type != ValueType::Int || kNotSkills.contains(field.key)
            || field.key.contains(u"career"_s, Qt::CaseInsensitive))
            continue;
        items << QVariantMap{{u"key"_s, field.key}, {u"label"_s, niceLabel(field.key)}, {u"group"_s, skillGroup(field.key)}};
    }
    std::stable_sort(items.begin(), items.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return order.indexOf(a.value(u"group"_s).toString()) < order.indexOf(b.value(u"group"_s).toString());
    });
    for (const QVariantMap& item : std::as_const(items))
        out << item;
    return out;
}

QVariantList DatabaseManager::playerCareerFields() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const BigAntDb::Schema& s = m_players.schema();
    for (int f : s.lists.value(0).fields)
        if (s.fields[f].type == ValueType::Int && s.fields[f].key.contains(u"career"_s, Qt::CaseInsensitive))
            out << QVariantMap{{u"key"_s, s.fields[f].key}, {u"label"_s, niceLabel(s.fields[f].key)}};
    return out;
}

QVariantList DatabaseManager::playerStatLists() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const BigAntDb::Schema& s = m_players.schema();
    for (int l = 1; l < s.lists.size(); ++l) {
        QVariantList fields;
        for (int f : s.lists[l].fields)
            if (s.fields[f].type != ValueType::Derived)
                fields << QVariantMap{{u"key"_s, s.fields[f].key}, {u"label"_s, niceLabel(s.fields[f].key)}};
        out << QVariantMap{{u"list"_s, s.lists[l].name},
                           {u"title"_s, listTitle(s.lists[l].name) + u" Match Stats"_s},
                           {u"fields"_s, fields}};
    }
    return out;
}

int DatabaseManager::playerRating(int index) const {
    if (!m_loaded || index < 0 || index >= m_players.count())
        return 0;
    const auto& terms = Rl26Lists::ratingTerms();
    const int role = m_players.num(index, u"preferredRolePrimary"_s);
    if (m_game != u"r25"_s && role >= 0 && role < terms.size() && !terms[role].isEmpty()) {
        int sum = 0;
        bool complete = true;
        for (const Rl26Lists::RatingTerm& term : terms[role]) {
            const QString field = ratingField(term.field);
            if (field.isEmpty()) {
                complete = false;
                break;
            }
            sum += m_players.num(index, field) * term.mul / term.div;
        }
        if (complete)
            return sum / static_cast<int>(terms[role].size());
    }
    // Rugby 25 (and anything the formulas don't fit): the average of the ratings.
    int sum = 0, count = 0;
    const BigAntDb::Schema& s = m_players.schema();
    for (int f : s.lists.value(0).fields) {
        const BigAntDb::Field& field = s.fields[f];
        if (field.type != ValueType::Int || kNotSkills.contains(field.key)
            || field.key.contains(u"career"_s, Qt::CaseInsensitive) || skillGroup(field.key) == u"Personality"_s)
            continue;
        sum += m_players.num(index, field.key);
        ++count;
    }
    return count ? sum / count : 0;
}

int DatabaseManager::teamRating(int index) const {
    if (!m_loaded || index < 0 || index >= m_teams.count())
        return 0;
    // The first 17 of the main lineup (the C# editor's team rating), else the squad.
    QStringList keys;
    const BigAntDb::Schema& s = m_teams.schema();
    for (const QString& list : {u"NRL2026Match"_s, u"UnionMatch"_s, u"LeagueMatch"_s, u"StandardMatch"_s}) {
        if (s.list(list) < 0 || !m_teams.listPresent(index, list))
            continue;
        keys = lineupKeys(list);
        break;
    }
    QList<int> ids = slotValues(index, keys.mid(0, 17));
    if (ids.isEmpty())
        ids = slotValues(index, m_rosterKeys);
    int sum = 0, count = 0;
    for (int id : std::as_const(ids)) {
        const int p = playerIndexById(id);
        if (p >= 0) {
            sum += playerRating(p);
            ++count;
        }
    }
    return count ? sum / count : 0;
}

QString DatabaseManager::playerName(int id) const {
    const int i = playerIndexById(id);
    return i < 0 ? QString() : (m_players.str(i, u"firstname"_s) + u' ' + m_players.str(i, u"surname"_s)).trimmed();
}

QString DatabaseManager::teamName(int id) const {
    const int i = teamIndexById(id);
    return i < 0 ? QString() : teamFullName(i);
}

bool DatabaseManager::canRandomize() const {
    if (!m_loaded || m_game == u"r25"_s)
        return false;
    for (const Rl26Lists::RandomRange& r : Rl26Lists::randomRanges())
        if (ratingField(r.field).isEmpty())
            return false;
    return true;
}

void DatabaseManager::randomizeStats(int index, int level) {
    if (!canRandomize() || m_busy || index < 0 || index >= m_players.count())
        return;
    const int role = m_players.num(index, u"preferredRolePrimary"_s);
    bool changed = false;
    for (const Rl26Lists::RandomRange& range : Rl26Lists::randomRanges()) {
        if (range.level != level || !(range.roleMask & (1 << role)))
            continue;
        changed |= m_players.set(index, ratingField(range.field), QRandomGenerator::global()->bounded(range.min, range.max));
    }
    if (changed) {
        edited();
        setStatus(true, u"New %1 stats for %2 - rating %3."_s
                            .arg(list(u"randomLevels"_s).value(level).toLower(), playerName(m_players.num(index, u"id"_s)))
                            .arg(playerRating(index)));
    }
}

// ---------------------------------------------------------------------------
// Squad, lineups, roles, feeder clubs
// ---------------------------------------------------------------------------

QVariantMap DatabaseManager::playerSummary(int id) const {
    const int i = playerIndexById(id);
    QVariantMap row{{u"id"_s, id}, {u"known"_s, i >= 0}, {u"index"_s, i}};
    if (i < 0) {
        row.insert(u"first"_s, QString());
        row.insert(u"last"_s, id == 0 ? u"(empty)"_s : u"(unknown player id %1)"_s.arg(id));
        row.insert(u"roles"_s, QString());
        row.insert(u"rating"_s, 0);
        return row;
    }
    row.insert(u"first"_s, m_players.str(i, u"firstname"_s));
    row.insert(u"last"_s, m_players.str(i, u"surname"_s));
    if (m_game != u"r25"_s)
        row.insert(u"roles"_s, QStringList{kRoleShort.value(m_players.num(i, u"preferredRolePrimary"_s)),
                                           kRoleShort.value(m_players.num(i, u"preferredRoleSecondary"_s)),
                                           kRoleShort.value(m_players.num(i, u"preferredRoleTertiary"_s))}.join(u" / "_s));
    else
        row.insert(u"roles"_s, QString());
    row.insert(u"rating"_s, playerRating(i));
    return row;
}

QList<int> DatabaseManager::slotValues(int team, const QStringList& keys) const {
    QList<int> values;
    if (team < 0 || team >= m_teams.count())
        return values;
    for (const QString& key : keys)
        if (m_teams.isSet(team, key))
            values << m_teams.num(team, key);
    return values;
}

void DatabaseManager::writeSlotValues(int team, const QStringList& keys, const QList<int>& values) {
    for (int j = 0; j < keys.size(); ++j) {
        if (j < values.size()) {
            m_teams.set(team, keys[j], values[j]);
            m_teams.setPresent(team, keys[j], true);
        } else {
            m_teams.setPresent(team, keys[j], false);
        }
    }
    if (keys == m_rosterKeys && !m_players.schema().has(u"club"_s))
        rebuildIndexes();   // The players' clubs come from the squads.
    edited();
}

QVariantList DatabaseManager::roster(int team) const {
    QVariantList rows;
    const QList<int> ids = slotValues(team, m_rosterKeys);
    for (int slot = 0; slot < ids.size(); ++slot) {
        QVariantMap row = playerSummary(ids[slot]);
        row.insert(u"slot"_s, slot);
        rows << row;
    }
    return rows;
}

bool DatabaseManager::addRosterPlayer(int team, int playerId) {
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count() || m_rosterKeys.isEmpty())
        return false;
    QList<int> ids = slotValues(team, m_rosterKeys);
    if (ids.size() >= m_rosterKeys.size()) {
        setStatus(false, u"A squad can have %1 players at most."_s.arg(m_rosterKeys.size()));
        return false;
    }
    if (ids.contains(playerId)) {
        setStatus(false, u"%1 is already in the squad."_s.arg(playerName(playerId)));
        return false;
    }
    ids << playerId;
    writeSlotValues(team, m_rosterKeys, ids);
    setStatus(true, u"Added %1 to the squad (%2 players)."_s.arg(playerName(playerId)).arg(ids.size()));
    return true;
}

bool DatabaseManager::setRosterPlayer(int team, int slot, int playerId) {
    if (!m_loaded || m_busy)
        return false;
    QList<int> ids = slotValues(team, m_rosterKeys);
    if (slot < 0 || slot >= ids.size())
        return false;
    ids[slot] = playerId;
    writeSlotValues(team, m_rosterKeys, ids);
    return true;
}

bool DatabaseManager::removeRosterPlayer(int team, int slot) {
    if (!m_loaded || m_busy)
        return false;
    QList<int> ids = slotValues(team, m_rosterKeys);
    if (slot < 0 || slot >= ids.size())
        return false;
    const int id = ids.takeAt(slot);
    writeSlotValues(team, m_rosterKeys, ids);
    setStatus(true, u"Removed %1 from the squad (%2 players)."_s.arg(playerName(id)).arg(ids.size()));
    return true;
}

void DatabaseManager::sortRoster(int team) {
    if (!m_loaded || m_busy)
        return;
    QList<int> ids = slotValues(team, m_rosterKeys);
    std::stable_sort(ids.begin(), ids.end(), [this](int a, int b) {
        const int ia = playerIndexById(a), ib = playerIndexById(b);
        const QString la = ia < 0 ? QString() : m_players.str(ia, u"surname"_s) + u' ' + m_players.str(ia, u"firstname"_s);
        const QString lb = ib < 0 ? QString() : m_players.str(ib, u"surname"_s) + u' ' + m_players.str(ib, u"firstname"_s);
        return la.compare(lb, Qt::CaseInsensitive) < 0;
    });
    writeSlotValues(team, m_rosterKeys, ids);
}

QVariantList DatabaseManager::matchLists() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const BigAntDb::Schema& s = m_teams.schema();
    // The game's own lineup first (NRL 2026 / Union), then the rest, Nines last.
    QList<int> order;
    for (int l = 1; l < s.lists.size(); ++l)
        if (s.lists[l].name != u"roster"_s)
            order << l;
    const auto rank = [&s](int l) {
        const QString& n = s.lists[l].name;
        return n == u"NRL2026Match"_s || n == u"UnionMatch"_s ? 0 : (n.contains(u"Nine"_s) ? 2 : 1);
    };
    std::stable_sort(order.begin(), order.end(), [&rank](int a, int b) { return rank(a) < rank(b); });
    for (int l : std::as_const(order)) {
        bool shirts = false;
        for (int f : s.lists[l].fields)
            shirts = shirts || s.fields[f].type == ValueType::Lineup;
        out << QVariantMap{{u"list"_s, s.lists[l].name}, {u"title"_s, listTitle(s.lists[l].name)}, {u"shirts"_s, shirts}};
    }
    return out;
}

QVariantList DatabaseManager::lineup(int team, const QString& list) const {
    QVariantList rows;
    if (!m_loaded || team < 0 || team >= m_teams.count())
        return rows;
    const bool on = m_teams.listPresent(team, list);
    int number = 0;
    for (const QString& key : lineupKeys(list)) {
        const QString field = key.mid(key.indexOf(u'.') + 1);
        ++number;
        const bool present = on && m_teams.isSet(team, key);
        QVariantMap row = playerSummary(present ? m_teams.num(team, key) : 0);
        row.insert(u"key"_s, key);
        row.insert(u"position"_s, u"%1 - %2"_s.arg(number).arg(niceLabel(field)));
        row.insert(u"present"_s, present);
        row.insert(u"shirt"_s, std::max(0, m_teams.shirt(team, key)));
        rows << row;
    }
    return rows;
}

QVariantList DatabaseManager::teamRoles(int team, const QString& list) const {
    QVariantList rows;
    if (!m_loaded || team < 0 || team >= m_teams.count())
        return rows;
    static const QStringList names = {u"Captain"_s, u"Goal Kicker"_s, u"Play Maker 1"_s, u"Play Maker 2"_s};
    const bool on = m_teams.listPresent(team, list);
    for (int r = 0; r < kTeamRoleKeys.size(); ++r) {
        const QString key = list + u'.' + kTeamRoleKeys[r];
        if (!m_teams.schema().has(key))
            continue;
        const bool present = on && m_teams.isSet(team, key);
        QVariantMap row = playerSummary(present ? m_teams.num(team, key) : 0);
        row.insert(u"key"_s, key);
        row.insert(u"name"_s, names[r]);
        row.insert(u"present"_s, present);
        rows << row;
    }
    return rows;
}

bool DatabaseManager::setLineupPlayer(int team, const QString& list, const QString& key, int playerId) {
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count() || !m_teams.schema().has(key))
        return false;
    const auto apply = [this, team](const QString& k, int id) {
        const bool wasSet = m_teams.isSet(team, k);
        m_teams.set(team, k, id);
        m_teams.setPresent(team, k, true);
        if (!wasSet && m_teams.shirt(team, k) == 0) {
            // A new slot gets its position's number on the shirt.
            const int number = static_cast<int>(lineupKeys(k.left(k.indexOf(u'.'))).indexOf(k)) + 1;
            if (number > 0)
                m_teams.setShirt(team, k, number);
        }
    };
    apply(key, playerId);
    // Like the C# editor: the older League lineup / roles follow the NRL 2026 ones.
    if (list == u"NRL2026Match"_s && m_teams.listPresent(team, u"LeagueMatch"_s)) {
        const QString mirror = u"LeagueMatch."_s + key.mid(key.indexOf(u'.') + 1);
        if (m_teams.schema().has(mirror))
            apply(mirror, playerId);
    }
    edited();
    return true;
}

bool DatabaseManager::setLineupShirt(int team, const QString& list, const QString& key, int shirt) {
    if (!m_loaded || m_busy)
        return false;
    bool changed = m_teams.setShirt(team, key, shirt);
    if (list == u"NRL2026Match"_s && m_teams.listPresent(team, u"LeagueMatch"_s))
        changed |= m_teams.setShirt(team, u"LeagueMatch."_s + key.mid(key.indexOf(u'.') + 1), shirt);
    if (changed)
        edited();
    return changed;
}

QStringList DatabaseManager::clubLinkKeys(const QString& kind) const {
    QStringList keys;
    const QString base = kind == u"feeder"_s ? u"feederClubs#"_s : u"fedFromClubs#"_s;
    for (int j = 0; j < 16; ++j)
        if (m_teams.schema().has(base + QString::number(j)))
            keys << base + QString::number(j);
    return keys;
}

QVariantList DatabaseManager::clubLinks(int team, const QString& kind) const {
    QVariantList rows;
    const QList<int> ids = slotValues(team, clubLinkKeys(kind));
    for (int slot = 0; slot < ids.size(); ++slot) {
        const int t = teamIndexById(ids[slot]);
        rows << QVariantMap{{u"slot"_s, slot},
                            {u"id"_s, ids[slot]},
                            {u"known"_s, t >= 0},
                            {u"fullName"_s, t < 0 ? u"(unknown team id %1)"_s.arg(ids[slot]) : teamFullName(t)},
                            {u"logoUrl"_s, t < 0 ? QString() : logoUrl(m_teams.str(t, u"icon"_s))}};
    }
    return rows;
}

bool DatabaseManager::addClubLink(int team, const QString& kind, int teamId) {
    if (!m_loaded || m_busy)
        return false;
    const QStringList keys = clubLinkKeys(kind);
    QList<int> ids = slotValues(team, keys);
    if (ids.size() >= keys.size()) {
        setStatus(false, u"A team can have %1 at most."_s.arg(keys.size()));
        return false;
    }
    if (ids.contains(teamId))
        return false;
    ids << teamId;
    writeSlotValues(team, keys, ids);
    return true;
}

bool DatabaseManager::setClubLink(int team, const QString& kind, int slot, int teamId) {
    if (!m_loaded || m_busy)
        return false;
    const QStringList keys = clubLinkKeys(kind);
    QList<int> ids = slotValues(team, keys);
    if (slot < 0 || slot >= ids.size())
        return false;
    ids[slot] = teamId;
    writeSlotValues(team, keys, ids);
    return true;
}

bool DatabaseManager::removeClubLink(int team, const QString& kind, int slot) {
    if (!m_loaded || m_busy)
        return false;
    const QStringList keys = clubLinkKeys(kind);
    QList<int> ids = slotValues(team, keys);
    if (slot < 0 || slot >= ids.size())
        return false;
    ids.removeAt(slot);
    writeSlotValues(team, keys, ids);
    return true;
}

// ---------------------------------------------------------------------------
// Jerseys
// ---------------------------------------------------------------------------

QVariantMap DatabaseManager::jerseys(int team) const {
    QVariantMap out{{u"ok"_s, false}};
    if (!m_loaded || team < 0 || team >= m_teams.count())
        return out;
    if (m_game == u"rll3"_s) {
        const QByteArray& data = m_teams.record(team).blob;
        const Rll3Kit k = rll3Kit(data);
        if (!k.ok()) {
            out.insert(u"error"_s, u"This team's jersey data (%1 bytes) has a layout the editor doesn't know."_s.arg(data.size()));
            return out;
        }
        const int count = std::min<int>(Rll3Kit::kSlots, static_cast<uchar>(data.at(Rll3Kit::kCountAt)));
        QVariantList uniforms;
        for (int u = 0; u < count; ++u)
            uniforms << QVariantMap{{u"index"_s, u}, {u"name"_s, rawText(data, k.start + u * Rll3Kit::kSize, 25)},
                                    {u"hidden"_s, false}, {u"nines"_s, false}};
        out.insert(u"ok"_s, true);
        out.insert(u"count"_s, count);
        out.insert(u"max"_s, Rll3Kit::kSlots);
        out.insert(u"uniforms"_s, uniforms);
        out.insert(u"slots"_s, QVariantList());
        return out;
    }
    PaSerial::Blob blob;
    QString error;
    if (!blob.parse(m_teams.record(team).blob, &error)) {
        out.insert(u"error"_s, m_teams.record(team).blob.isEmpty() ? u"This team has no jerseys."_s
                                                                    : u"The jersey data can't be read: "_s + error);
        return out;
    }
    const KitLayout k = kitLayout(blob);
    if (!k.ok) {
        out.insert(u"error"_s, u"The jersey data has a layout this editor doesn't know."_s);
        return out;
    }
    const int count = std::min<int>(k.arraySize, static_cast<int>(blob.readUInt(k.countOffset, k.countSize)));
    QVariantList uniforms;
    for (int u = 0; u < count; ++u) {
        const qint64 base = k.arrayOffset + qint64(u) * k.uniformSize;
        QByteArray name = k.nameOffset >= 0 ? blob.data().mid(base + k.nameOffset, k.nameLength) : QByteArray();
        if (name.indexOf('\0') >= 0)
            name.truncate(name.indexOf('\0'));
        uniforms << QVariantMap{{u"index"_s, u},
                                {u"name"_s, QString::fromLatin1(name)},
                                {u"hidden"_s, k.hiddenOffset >= 0 && blob.readUInt(base + k.hiddenOffset, 1) != 0},
                                {u"nines"_s, k.ninesOffset >= 0 && blob.readUInt(base + k.ninesOffset, 1) != 0}};
    }
    // Which uniform the game uses where (worked out from RLL4's teams, where they differ).
    static const QStringList slotNames = {u"Home"_s, u"Away"_s, u"Heritage Round"_s, u"Anzac Round"_s, u"Nines"_s,
                                          u"Alternate / Special Round"_s, u"Indigenous Round"_s};
    QVariantList kitSlots;
    for (int s = 0; k.slotsOffset >= 0 && s < k.slotCount; ++s)
        kitSlots << QVariantMap{{u"slot"_s, s},
                                {u"label"_s, m_game == u"r25"_s ? u"Kit Slot %1"_s.arg(s + 1) : slotNames.value(s, u"Slot %1"_s.arg(s + 1))},
                                {u"uniform"_s, static_cast<int>(blob.readUInt(k.slotsOffset + s, 1))}};
    out.insert(u"ok"_s, true);
    out.insert(u"count"_s, count);
    out.insert(u"max"_s, k.arraySize);
    out.insert(u"uniforms"_s, uniforms);
    out.insert(u"slots"_s, kitSlots);
    return out;
}

// RLL4: the uniform's "Uniform" byte is the manufacturer and "Top Pattern" its licensed jersey (Wouldy's RLL4 Database
// Editor's lists): drop downs with names, at the top of the Uniform group.
static void rll4KitChoices(QVariantList& fields) {
    int makerAt = -1, patternAt = -1;
    for (int i = 0; i < fields.size(); ++i) {
        const QString label = fields[i].toMap().value(u"label"_s).toString();
        if (label == u"Uniform"_s) makerAt = i;
        else if (label == u"Top Pattern"_s) patternAt = i;
    }
    if (makerAt < 0 || patternAt < 0)
        return;
    QVariantMap maker = fields[makerAt].toMap(), pattern = fields[patternAt].toMap();
    QVariantList makers, jerseys{QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}}};
    for (const Rll4Lists::Choice& c : Rll4Lists::manufacturers())
        makers << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
    for (const Rll4Lists::Choice& c : Rll4Lists::licensedJerseys(maker.value(u"value"_s).toInt()))
        jerseys << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
    maker.insert({{u"label"_s, u"Manufacturer"_s}, {u"group"_s, u"Uniform"_s}, {u"kind"_s, u"choice"_s},
                  {u"choices"_s, makers}, {u"refresh"_s, true}});
    pattern.insert({{u"label"_s, u"Licensed Jersey"_s}, {u"group"_s, u"Uniform"_s}, {u"kind"_s, u"choice"_s},
                    {u"choices"_s, jerseys}});
    fields.removeAt(std::max(makerAt, patternAt));
    fields.removeAt(std::min(makerAt, patternAt));
    int at = 0;   // After the uniform's name.
    while (at < fields.size() && fields[at].toMap().value(u"group"_s).toString() == u"Uniform"_s)
        ++at;
    fields.insert(at, pattern);
    fields.insert(at, maker);
}

QVariantList DatabaseManager::jerseyFields(int team, int uniform) const {
    if (!m_loaded || team < 0 || team >= m_teams.count())
        return {};
    if (m_game == u"rll3"_s) {
        const QByteArray& data = m_teams.record(team).blob;
        const Rll3Kit k = rll3Kit(data);
        if (!k.ok() || uniform < 0 || uniform >= Rll3Kit::kSlots)
            return {};
        return rll3KitFields(data, k.start + uniform * Rll3Kit::kSize);
    }
    PaSerial::Blob blob;
    if (!blob.parse(m_teams.record(team).blob))
        return {};
    const KitLayout k = kitLayout(blob);
    if (!k.ok || uniform < 0 || uniform >= k.arraySize)
        return {};
    QVariantList fields = blob.fields(k.uniformType, k.arrayOffset + qint64(uniform) * k.uniformSize);
    if (m_game == u"rll4"_s)
        rll4KitChoices(fields);
    return fields;
}

bool DatabaseManager::setJerseyField(int team, int uniform, const QString& key, const QVariant& value) {
    Q_UNUSED(uniform)   // The key carries the absolute offset.
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count())
        return false;
    if (m_game == u"rll3"_s) {
        if (!setRawField(m_teams.record(team).blob, key, value))
            return false;
        edited();
        return true;
    }
    PaSerial::Blob blob;
    if (!blob.parse(m_teams.record(team).blob) || !blob.setField(key, value))
        return false;
    m_teams.record(team).blob = blob.bytes();
    edited();
    return true;
}

bool DatabaseManager::setJerseySlot(int team, int slot, int uniform) {
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count())
        return false;
    PaSerial::Blob blob;
    if (!blob.parse(m_teams.record(team).blob))
        return false;
    const KitLayout k = kitLayout(blob);
    if (!k.ok || k.slotsOffset < 0 || slot < 0 || slot >= k.slotCount || uniform < 0 || uniform > 255)
        return false;
    if (blob.readUInt(k.slotsOffset + slot, 1) == static_cast<quint64>(uniform))
        return false;
    blob.writeUInt(k.slotsOffset + slot, 1, uniform);
    m_teams.record(team).blob = blob.bytes();
    edited();
    return true;
}

int DatabaseManager::addJersey(int team, int from) {
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count())
        return -1;
    if (m_game == u"rll3"_s) {
        QByteArray& data = m_teams.record(team).blob;
        const Rll3Kit k = rll3Kit(data);
        const int count = k.ok() ? static_cast<uchar>(data.at(Rll3Kit::kCountAt)) : Rll3Kit::kSlots;
        if (count >= Rll3Kit::kSlots) {
            setStatus(false, u"This team already has %1 jerseys, the most it can have."_s.arg(Rll3Kit::kSlots));
            return -1;
        }
        if (from < 0 || from >= count)
            from = 0;
        data.replace(k.start + count * Rll3Kit::kSize, Rll3Kit::kSize, data.mid(k.start + from * Rll3Kit::kSize, Rll3Kit::kSize));
        setRawField(data, u"t:%1:25"_s.arg(k.start + count * Rll3Kit::kSize), u"NEW JERSEY %1"_s.arg(count + 1));
        data[Rll3Kit::kCountAt] = static_cast<char>(count + 1);
        edited();
        setStatus(true, u"Added a jersey (a copy of jersey %1)."_s.arg(from + 1));
        return count;
    }
    PaSerial::Blob blob;
    if (!blob.parse(m_teams.record(team).blob))
        return -1;
    const KitLayout k = kitLayout(blob);
    if (!k.ok)
        return -1;
    const int count = static_cast<int>(blob.readUInt(k.countOffset, k.countSize));
    if (count >= k.arraySize) {
        setStatus(false, u"This team already has %1 jerseys, the most it can have."_s.arg(k.arraySize));
        return -1;
    }
    if (from < 0 || from >= count)
        from = 0;
    const QByteArray source = blob.data().mid(k.arrayOffset + qint64(from) * k.uniformSize, k.uniformSize);
    blob.data().replace(k.arrayOffset + qint64(count) * k.uniformSize, k.uniformSize, source);
    if (k.nameOffset >= 0) {
        QByteArray name = (u"NEW JERSEY %1"_s.arg(count + 1)).toLatin1().left(k.nameLength - 1);
        name.resize(k.nameLength, '\0');
        blob.data().replace(k.arrayOffset + qint64(count) * k.uniformSize + k.nameOffset, k.nameLength, name);
    }
    blob.writeUInt(k.countOffset, k.countSize, count + 1);
    m_teams.record(team).blob = blob.bytes();
    edited();
    setStatus(true, u"Added a jersey (a copy of jersey %1)."_s.arg(from + 1));
    return count;
}

bool DatabaseManager::removeJersey(int team, int uniform) {
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count())
        return false;
    if (m_game == u"rll3"_s) {
        QByteArray& data = m_teams.record(team).blob;
        const Rll3Kit k = rll3Kit(data);
        const int count = k.ok() ? static_cast<uchar>(data.at(Rll3Kit::kCountAt)) : 0;
        if (count <= 1 || uniform < 0 || uniform >= count) {
            setStatus(false, u"A team needs at least one jersey."_s);
            return false;
        }
        for (int u = uniform; u + 1 < count; ++u)
            data.replace(k.start + u * Rll3Kit::kSize, Rll3Kit::kSize, data.mid(k.start + (u + 1) * Rll3Kit::kSize, Rll3Kit::kSize));
        data.replace(k.start + (count - 1) * Rll3Kit::kSize, Rll3Kit::kSize, QByteArray(Rll3Kit::kSize, '\0'));
        data[Rll3Kit::kCountAt] = static_cast<char>(count - 1);
        edited();
        return true;
    }
    PaSerial::Blob blob;
    if (!blob.parse(m_teams.record(team).blob))
        return false;
    const KitLayout k = kitLayout(blob);
    const int count = k.ok ? static_cast<int>(blob.readUInt(k.countOffset, k.countSize)) : 0;
    if (count <= 1 || uniform < 0 || uniform >= count) {
        setStatus(false, u"A team needs at least one jersey."_s);
        return false;
    }
    QByteArray& data = blob.data();
    for (int u = uniform; u + 1 < count; ++u)
        data.replace(k.arrayOffset + qint64(u) * k.uniformSize, k.uniformSize,
                     data.mid(k.arrayOffset + qint64(u + 1) * k.uniformSize, k.uniformSize));
    data.replace(k.arrayOffset + qint64(count - 1) * k.uniformSize, k.uniformSize, QByteArray(k.uniformSize, '\0'));
    blob.writeUInt(k.countOffset, k.countSize, count - 1);
    // The slots: the removed jersey's slots go back to the first one, later ones move down.
    for (int s = 0; k.slotsOffset >= 0 && s < k.slotCount; ++s) {
        const int v = static_cast<int>(blob.readUInt(k.slotsOffset + s, 1));
        if (v == uniform)
            blob.writeUInt(k.slotsOffset + s, 1, 0);
        else if (v > uniform)
            blob.writeUInt(k.slotsOffset + s, 1, v - 1);
    }
    m_teams.record(team).blob = blob.bytes();
    edited();
    return true;
}

QString DatabaseManager::exportJerseys(int team, const QString& pathOrUrl) const {
    if (!m_loaded || team < 0 || team >= m_teams.count())
        return u"Nothing is loaded."_s;
    QSaveFile file(toLocalPath(pathOrUrl));
    if (!file.open(QIODevice::WriteOnly) || file.write(m_teams.record(team).blob) < 0 || !file.commit())
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    return {};
}

QString DatabaseManager::importJerseys(int team, const QString& pathOrUrl) {
    if (!m_loaded || m_busy || team < 0 || team >= m_teams.count())
        return u"Nothing is loaded."_s;
    QFile file(toLocalPath(pathOrUrl));
    if (!file.open(QIODevice::ReadOnly))
        return u"Could not open %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    const QByteArray data = file.readAll();
    if (m_game == u"rll3"_s) {
        if (!rll3Kit(data).ok() || data.size() != m_teams.record(team).blob.size())
            return u"%1 isn't a Rugby League Live 3 team's jerseys (or is from a team with a different layout)."_s
                .arg(QFileInfo(file.fileName()).fileName());
        m_teams.record(team).blob = data;
        edited();
        return {};
    }
    PaSerial::Blob blob;
    QString error;
    if (!blob.parse(data, &error) || !kitLayout(blob).ok)
        return u"%1 isn't a team's jerseys %2."_s.arg(QFileInfo(file.fileName()).fileName(), error.isEmpty() ? QString() : u"("_s + error + u')');
    PaSerial::Blob current;
    if (current.parse(m_teams.record(team).blob) && current.bigEndian() != blob.bigEndian())
        return u"Those jerseys are from another game."_s;
    m_teams.record(team).blob = data;
    edited();
    return {};
}

// ---------------------------------------------------------------------------
// Player appearance
// ---------------------------------------------------------------------------

QVariantList DatabaseManager::appearanceFields(int player) const {
    if (!m_loaded || player < 0 || player >= m_players.count())
        return {};
    PaSerial::Blob blob;
    if (!blob.parse(m_players.record(player).blob))
        return {};
    return blob.fields(blob.root(), 0);
}

bool DatabaseManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    if (!m_loaded || m_busy || player < 0 || player >= m_players.count())
        return false;
    PaSerial::Blob blob;
    if (!blob.parse(m_players.record(player).blob) || !blob.setField(key, value))
        return false;
    m_players.record(player).blob = blob.bytes();
    edited();
    return true;
}

// ---------------------------------------------------------------------------
// Drop down lists
// ---------------------------------------------------------------------------

QStringList DatabaseManager::list(const QString& name) const {
    if (m_game == u"rll4"_s) {   // Wouldy's RLL4 Database Editor's lists.
        if (name == u"countryOfBirth"_s) return Rll4Lists::countries();
        if (name == u"repCountry"_s) return Rll4Lists::repCountries();
        if (name == u"stadiums"_s) return Rll4Lists::stadiums();
        if (name == u"supporters"_s) return Rll4Lists::supporters();
        if (name == u"commentaryTeams"_s) return Rll4Lists::commentaryTeams();
    }
    if (name == u"countryOfBirth"_s) return Rl26Lists::countriesOfBirth();
    if (name == u"repCountry"_s) return Rl26Lists::repCountries();
    if (name == u"affiliations"_s) return Rl26Lists::affiliations();
    if (name == u"stadiums"_s) return Rl26Lists::stadiums();
    if (name == u"supporters"_s) return Rl26Lists::supporters();
    if (name == u"logos"_s) return Rl26Lists::logos();
    if (name == u"roles"_s) return kRoleNames;
    if (name == u"roleShort"_s) return kRoleShort;
    if (name == u"perks"_s)
        return {u"None"_s, u"Speed"_s, u"Stamina"_s, u"Contests"_s, u"Place Kicking"_s, u"In Play Kicking"_s};
    if (name == u"contract"_s) {
        QStringList years{u"None"_s};
        for (int year = 2025; year <= 2050; ++year)
            years << QString::number(year);
        return years;
    }
    if (name == u"teamTypes"_s)
        return {u"League"_s, u"International"_s, u"State Of Origin"_s, u"City V's Country"_s, u"All Stars"_s};
    if (name == u"soo"_s) return {u"None"_s, u"Queensland"_s, u"New South Wales"_s};
    if (name == u"cvc"_s) return {u"None"_s, u"City"_s, u"Country"_s};
    if (name == u"allStars"_s) return {u"None"_s, u"Maori All Stars"_s, u"Indigenous All Stars"_s};
    if (name == u"hands"_s) return {u"Left"_s, u"Right"_s};
    if (name == u"randomLevels"_s) return {u"Star Player"_s, u"Average Player"_s, u"Rookie Player"_s};
    if (name == u"genders"_s) return {u"Male"_s, u"Female"_s};
    return {};
}

QVariantList DatabaseManager::choices(const QString& name) const {
    QVariantList out;
    const auto add = [&out](const QList<Rll3Lists::Choice>& list) {
        for (const Rll3Lists::Choice& c : list)
            out << QVariantMap{{u"name"_s, QString::fromLatin1(c.name)}, {u"value"_s, c.value}};
    };
    if (m_game == u"rll4"_s) {
        if (name == u"logos"_s)
            for (const Rll4Lists::TextChoice& c : Rll4Lists::logos())
                out << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, QString::fromUtf8(c.value)}};
        return out;
    }
    if (m_game != u"rll3"_s)
        return out;
    if (name == u"countries"_s)
        add(Rll3Lists::countries());
    else if (name == u"supporters"_s)
        add(Rll3Lists::supporters());
    else if (name == u"logos"_s)
        for (const Rll3Lists::TextChoice& c : Rll3Lists::emblems())
            out << QVariantMap{{u"name"_s, QString::fromLatin1(c.name)}, {u"value"_s, QString::fromLatin1(c.value)}};
    return out;
}

QStringList DatabaseManager::teamNames() const {
    QStringList names;
    if (!m_loaded)
        return names;
    names.reserve(m_teams.count());
    for (int i = 0; i < m_teams.count(); ++i)
        names << teamFullName(i);
    return names;
}

// The commentary name lists, from the sound M3MP in the blobset:
//   RL26 / Rugby 25: sound\dialogue.mempack.m3mp (0x83AF3201ED1AE751): dialogue_wildcardhash_<list>,
//                    u32 count, then per name: u32 hash, u32 length, the name. The databases store the hash.
//   RLL3 / RLL4:     sound\ui.mempack.m3mp (0x37D44B5B): ui_<list>, u32 count, then per name: u32 length, the name.
//                    The names have no hash: a name's "hash" here is its index.
void DatabaseManager::loadCommentary() {
    m_commentary.clear();
    const bool hashed = m_game == u"rl26"_s || m_game == u"r25"_s;
    BlobsetEntry entry;
    const QString hash = hashed ? u"83AF3201ED1AE751"_s : u"37D44B5B"_s;
    if (!BlobsetEntryRegistry::findEntry(hash, entry))
        return;
    M3mpToolManager sound;
    sound.loadM3mpFromBlobset(hash);
    static const QList<QPair<QString, QString>> lists = {
        {u"surnames"_s, u"surnames"_s}, {u"location"_s, u"teamslocation"_s},
        {u"mascot"_s, u"teamsmascot"_s}, {u"teams"_s, u"teams"_s}};
    for (const auto& [kind, file] : lists) {
        const QByteArray data = sound.entryDataByPath((hashed ? u"sound/dialogue_wildcardhash_"_s : u"sound/ui_"_s) + file);
        if (data.size() < 4)
            continue;
        Commentary& c = m_commentary[kind];
        qint64 pos = 0;
        const auto u32 = [&data, &pos](quint32& out) {
            if (pos + 4 > data.size())
                return false;
            out = qFromLittleEndian<quint32>(data.constData() + pos);
            pos += 4;
            return true;
        };
        quint32 count = 0;
        u32(count);
        for (quint32 i = 0; i < count; ++i) {
            quint32 h = i, length = 0;
            if ((hashed && !u32(h)) || !u32(length) || pos + length > data.size())
                break;
            const QString name = QString::fromLatin1(data.mid(pos, length));
            pos += length;
            c.hashes << h;
            c.values << name;
            c.names << titleCase(name);
        }
    }
}

QStringList DatabaseManager::commentaryNames(const QString& kind) const {
    return m_commentary.value(kind).names;
}

int DatabaseManager::commentaryIndex(const QString& kind, uint hash) const {
    return static_cast<int>(m_commentary.value(kind).hashes.indexOf(hash));
}

uint DatabaseManager::commentaryHash(const QString& kind, int index) const {
    return m_commentary.value(kind).hashes.value(index, 2913447899u);   // The "none" hash the C# editor uses.
}

QString DatabaseManager::commentaryValue(const QString& kind, int index) const {
    return m_commentary.value(kind).values.value(index);
}

int DatabaseManager::commentaryIndexOf(const QString& kind, const QString& value) const {
    const QStringList& values = m_commentary.value(kind).values;
    for (int i = 0; i < values.size(); ++i)
        if (values[i].compare(value, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

// Team logos from the blobset: ui\club_logos\large_mirrored\<logo>[.pc].dds, served by the DDS image provider.
QString DatabaseManager::logoUrl(const QString& logo) const {
    if (logo.isEmpty())
        return {};
    const QString key = logo.toLower();
    if (const auto it = m_logoUrls.constFind(key); it != m_logoUrls.constEnd())
        return it.value();
    QString url;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        // "<logo>.pc.dds" (RLL4 and newer), "<logo>.dds" (RLL3).
        for (const char* platform : {".pc", "", ".ps3", ".x360"}) {
            const QString hash = orchestrator->getHashFromPath(
                u"ui/club_logos/large_mirrored/%1%2.dds"_s.arg(key, QLatin1StringView(platform)));
            if (!hash.isEmpty() && hash.toULongLong(nullptr, 16) != 0) {
                url = u"image://dds_provider/"_s + DdsImageProvider::makeId(hash, 0, {});
                break;
            }
        }
    }
    m_logoUrls.insert(key, url);
    return url;
}

QString DatabaseManager::teamFullName(int team) const {
    // RLL3 has no full name: location + club ("BARROW RAIDERS").
    const QString full = m_teams.str(team, u"fullName"_s);
    if (!full.isEmpty())
        return full;
    return (m_teams.str(team, u"shortName"_s) + u' ' + m_teams.str(team, u"nickname"_s)).trimmed();
}

QString DatabaseManager::prettyLabel(const QString& key) const {
    return niceLabel(key);
}
