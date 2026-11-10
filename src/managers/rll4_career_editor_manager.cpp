#include "rll4_career_editor_manager.h"

#include "app_orchestrator.h"
#include "dds_image_provider.h"
#include "rll4_lists.h"
#include "rll4_save_lists.h"
#include "career_match_stats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

enum class Type { Virtual, U8, U16, I16, I32, Bool, Bool255, Str, Rgb };

enum Flags { kReadOnly = 1, kSlider = 2, kLook = 4, kStats = 8 };

struct FieldDef {
    const char* key;
    const char* label;
    const char* group;
    Type type;
    int size;
    bool big;
    int off[3];         // PC World Cup Edition: the field (colours: r, g, b). -1 = not in this version.
    int offDemo[3];     // The demo.
    int min;
    int max;
    const char* list;   // Drop down ("@..." = worked out here).
    int flags;
};

#include "rll4_career_fields.inc"

constexpr qsizetype kTeamTable[2] = {4892860, 4892860};
constexpr qsizetype kPlayerTable[2] = {5863136, 5863136};
constexpr int kMaxTeams = 200;
constexpr int kMaxPlayers = 10000;

// The career data (the C# editor's Global.cs).
constexpr qsizetype kStatusAt = 5851870;            // u16 count, then {i32 player, u8 reason, u16 time, u16 a, u16 b}.
constexpr int kStatusSize = 11;
constexpr int kStatusMax = 1024;                    // The room before the players.
constexpr qsizetype kContractsAt = 4706984;         // u16 count, then {i32 player, u16, i32 team, i32 new team, u16, i32}.
constexpr int kContractSize = 20;
constexpr qsizetype kRetirementAt = 4747946;        // i16 year, u8 month, u8 day, i32 count, then {i32 player, u8 reason}.
constexpr qsizetype kCareerModeAt = 4685671;        // bool set, bool player, i32 career player.
constexpr qsizetype kCareerTeamAt = 4685759;        // i32 team.
constexpr qsizetype kRepTeamsAt = 4686364;          // i32 international, state of origin, city v country, all stars.
constexpr qsizetype kMoneyAt = 4686381;             // i32 spending money, i32 earnt as a player.
constexpr qsizetype kSettingsAt = 4892853;          // salary cap, half, Nines half, match, recruiting, injuries, currency.
constexpr qsizetype kLeagueAt[21] = {4753095, 4760487, 4767465, 4774443, 4781421, 4788172, 4795337, 4802590, 4809515,
                                     4816552, 4823589, 4830702, 4843735, 4837107, 4849622, 4855525, 4861412, 4867068,
                                     4874069, 4880697, 4886425};
constexpr int kMaxJerseys = 8;
constexpr int kNinesSlots = 14;
constexpr int kCustomLogo = 261;    // Logo list: "custom".

const QStringList kRoleNames = {u"Fullback"_s, u"Winger"_s, u"Centre"_s, u"Five-Eighth"_s, u"Halfback"_s, u"Prop"_s,
                                u"Hooker"_s, u"Second Row"_s, u"Lock"_s};
const QStringList kRoleShort = {u"FB"_s, u"WG"_s, u"C"_s, u"FE"_s, u"HB"_s, u"FR"_s, u"HK"_s, u"SR"_s, u"L"_s};
const QStringList kPositions = {u"1 - Fullback"_s, u"2 - Right Wing"_s, u"3 - Right Centre"_s, u"4 - Left Centre"_s,
                                u"5 - Left Wing"_s, u"6 - Five-Eighth"_s, u"7 - Halfback"_s, u"8 - Prop"_s, u"9 - Hooker"_s,
                                u"10 - Prop"_s, u"11 - Right Second Row"_s, u"12 - Left Second Row"_s, u"13 - Lock"_s,
                                u"14 - Interchange 1"_s, u"15 - Interchange 2"_s, u"16 - Interchange 3"_s,
                                u"17 - Interchange 4"_s};
const QStringList kNinesPositions = {u"1"_s, u"2"_s, u"3"_s, u"4"_s, u"5"_s, u"6"_s, u"7"_s, u"8"_s, u"9"_s,
                                     u"10 - Interchange 1"_s, u"11 - Interchange 2"_s, u"12 - Interchange 3"_s,
                                     u"13 - Interchange 4"_s, u"14 - Interchange 5"_s};
const QStringList kTeamRoles = {u"Captain"_s, u"Goal Kicker"_s, u"Play Maker 1"_s, u"Play Maker 2"_s};

struct RatingTerm {
    QString field;
    int mul;
    int div;
};

// The C# editor's Rating.cs (and OrigRating.cs, the same sums): per primary role, the stats that count.
const QList<QList<RatingTerm>>& ratingTerms() {
    static const QList<QList<RatingTerm>> terms = {
        {{u"technicalAbility.strength"_s, 100, 90}, {u"technicalAbility.agility"_s, 1, 1}, {u"technicalAbility.fitness"_s, 1, 1}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 85}, {u"technicalAbility.sprintSpeed"_s, 1, 1}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.longPass"_s, 1, 1}, {u"skills.evasionSkills.sideStep"_s, 1, 1}, {u"skills.miscSkills.contestedCollect"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 100, 90}, {u"technicalAbility.agility"_s, 1, 1}, {u"technicalAbility.fitness"_s, 1, 1}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 85}, {u"technicalAbility.sprintSpeed"_s, 1, 1}, {u"skills.passingSkills.offload"_s, 1, 1}, {u"skills.evasionSkills.dummyPass"_s, 1, 1}, {u"skills.evasionSkills.fend"_s, 1, 1}, {u"skills.evasionSkills.sideStep"_s, 1, 1}, {u"skills.evasionSkills.breakTackle"_s, 1, 1}, {u"skills.tackleSkills.tackle"_s, 1, 1}, {u"skills.tackleSkills.driveTackle"_s, 1, 1}, {u"skills.tackleSkills.diveTackle"_s, 1, 1}, {u"skills.miscSkills.contestedCollect"_s, 1, 1}, {u"skills.miscSkills.diveCollect"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 100, 90}, {u"technicalAbility.agility"_s, 1, 1}, {u"technicalAbility.fitness"_s, 1, 1}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 85}, {u"technicalAbility.sprintSpeed"_s, 1, 1}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.offload"_s, 1, 1}, {u"skills.evasionSkills.dummyPass"_s, 1, 1}, {u"skills.evasionSkills.sideStep"_s, 1, 1}, {u"skills.tackleSkills.tackle"_s, 1, 1}, {u"skills.miscSkills.contestedCollect"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 100, 95}, {u"technicalAbility.agility"_s, 1, 1}, {u"technicalAbility.fitness"_s, 1, 1}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 85}, {u"technicalAbility.sprintSpeed"_s, 100, 95}, {u"skills.evasionSkills.dummyPass"_s, 1, 1}, {u"skills.kickingSkills.grubberKick"_s, 1, 1}, {u"skills.kickingSkills.puntKick"_s, 1, 1}, {u"skills.kickingSkills.bombKick"_s, 1, 1}, {u"skills.kickingSkills.fieldgoalKick"_s, 1, 1}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.longPass"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 100, 95}, {u"technicalAbility.agility"_s, 1, 1}, {u"technicalAbility.fitness"_s, 1, 1}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 85}, {u"technicalAbility.sprintSpeed"_s, 100, 95}, {u"skills.kickingSkills.grubberKick"_s, 1, 1}, {u"skills.kickingSkills.puntKick"_s, 1, 1}, {u"skills.kickingSkills.bombKick"_s, 1, 1}, {u"skills.kickingSkills.fieldgoalKick"_s, 1, 1}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.longPass"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 1, 1}, {u"technicalAbility.agility"_s, 100, 90}, {u"technicalAbility.fitness"_s, 100, 85}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 90}, {u"technicalAbility.sprintSpeed"_s, 100, 80}, {u"skills.evasionSkills.fend"_s, 1, 1}, {u"skills.evasionSkills.breakTackle"_s, 1, 1}, {u"skills.tackleSkills.tackle"_s, 1, 1}, {u"skills.tackleSkills.driveTackle"_s, 1, 1}, {u"skills.tackleSkills.impactTackle"_s, 1, 1}, {u"skills.passingSkills.offload"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 100, 90}, {u"technicalAbility.agility"_s, 100, 95}, {u"technicalAbility.fitness"_s, 1, 1}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 95}, {u"technicalAbility.durability"_s, 100, 95}, {u"technicalAbility.sprintSpeed"_s, 100, 90}, {u"skills.evasionSkills.dummyPass"_s, 1, 1}, {u"skills.evasionSkills.sideStep"_s, 1, 1}, {u"skills.tackleSkills.tackle"_s, 1, 1}, {u"skills.kickingSkills.grubberKick"_s, 1, 1}, {u"skills.kickingSkills.puntKick"_s, 1, 1}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.longPass"_s, 1, 1}, {u"skills.passingSkills.offload"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 1, 1}, {u"technicalAbility.agility"_s, 100, 90}, {u"technicalAbility.fitness"_s, 100, 95}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 90}, {u"technicalAbility.sprintSpeed"_s, 100, 85}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.offload"_s, 1, 1}, {u"skills.evasionSkills.fend"_s, 1, 1}, {u"skills.evasionSkills.sideStep"_s, 1, 1}, {u"skills.evasionSkills.breakTackle"_s, 1, 1}, {u"skills.tackleSkills.tackle"_s, 1, 1}, {u"skills.tackleSkills.driveTackle"_s, 1, 1}, {u"skills.tackleSkills.diveTackle"_s, 1, 1}},
        {{u"technicalAbility.strength"_s, 1, 1}, {u"technicalAbility.agility"_s, 100, 90}, {u"technicalAbility.fitness"_s, 100, 95}, {u"technicalAbility.acceleration"_s, 1, 1}, {u"technicalAbility.discipline"_s, 100, 90}, {u"technicalAbility.durability"_s, 100, 90}, {u"technicalAbility.sprintSpeed"_s, 100, 85}, {u"skills.passingSkills.basicPass"_s, 1, 1}, {u"skills.passingSkills.offload"_s, 1, 1}, {u"skills.evasionSkills.fend"_s, 1, 1}, {u"skills.evasionSkills.breakTackle"_s, 1, 1}, {u"skills.tackleSkills.tackle"_s, 1, 1}, {u"skills.tackleSkills.driveTackle"_s, 1, 1}},
    };
    return terms;
}

template <size_t N>
QList<const FieldDef*> toList(const FieldDef (&defs)[N]) {
    QList<const FieldDef*> out;
    for (const FieldDef& d : std::as_const(defs))
        out.append(&d);
    return out;
}

QList<const FieldDef*> defsFor(const QString& kind) {
    static const QList<const FieldDef*> team = toList(kTeamFields);
    static const QList<const FieldDef*> player = toList(kPlayerFields);
    return kind == u"team"_s ? team : player;
}

const FieldDef* findDef(const QList<const FieldDef*>& defs, const QString& key) {
    for (const FieldDef* d : std::as_const(defs))
        if (key == QLatin1StringView(d->key))
            return d;
    return nullptr;
}

const FieldDef* defFor(const QString& kind, const QString& key) {
    return findDef(defsFor(kind), key);
}

const QList<const FieldDef*>& jerseyDefs() {
    static const QList<const FieldDef*> list = toList(kJerseyFields);
    return list;
}

QString localPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString hexColour(int r, int g, int b) {
    return u"#%1%2%3"_s.arg(r, 2, 16, QChar(u'0')).arg(g, 2, 16, QChar(u'0')).arg(b, 2, 16, QChar(u'0'));
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
    out.reserve(names.size());
    for (int i = 0; i < names.size(); ++i)
        out << QVariantMap{{u"name"_s, names.at(i)}, {u"value"_s, base + i}};
    return out;
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

Rll4CareerEditorManager::Rll4CareerEditorManager(QObject* parent) : QObject(parent) {}

QString Rll4CareerEditorManager::fileName() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString Rll4CareerEditorManager::defaultFolder() const {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(u"My Games/Rugby League Live 4/Career000"_s);
}

QString Rll4CareerEditorManager::defaultFile() const {
    return QDir(defaultFolder()).filePath(u"CareerData.SAV"_s);
}

void Rll4CareerEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void Rll4CareerEditorManager::edited() {
    ++m_revision;
    emit revisionChanged();
    if (!m_unsaved) {
        m_unsaved = true;
        emit unsavedChangesChanged();
    }
}

int Rll4CareerEditorManager::teamCount() const {
    return m_loaded ? static_cast<int>(readNum(teamTable(), 2, true, false)) : 0;
}

int Rll4CareerEditorManager::playerCount() const {
    return m_loaded ? static_cast<int>(readNum(playerTable(), 2, true, false)) : 0;
}

qsizetype Rll4CareerEditorManager::teamTable() const { return kTeamTable[m_variant]; }
qsizetype Rll4CareerEditorManager::playerTable() const { return kPlayerTable[m_variant]; }
int Rll4CareerEditorManager::teamSize() const { return kTeamSize[m_variant]; }
int Rll4CareerEditorManager::playerSize() const { return kPlayerSize[m_variant]; }

int Rll4CareerEditorManager::maxTeams() const {
    return static_cast<int>(qMin<qsizetype>(kMaxTeams, (playerTable() - teamTable() - 2) / teamSize()));
}

int Rll4CareerEditorManager::maxPlayers() const {
    return static_cast<int>(qMin<qsizetype>(kMaxPlayers, (m_data.size() - playerTable() - 2) / playerSize()));
}

bool Rll4CareerEditorManager::load(const QString& pathOrUrl) {
    const QString path = QDir::toNativeSeparators(pathOrUrl.isEmpty() ? defaultFile() : localPath(pathOrUrl));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Couldn't open %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    const int variant = 0;
    if (data.size() < kPlayerTable[variant] + 2) {
        setStatus(false, u"%1 is too small to be a Rugby League Live 4 career save (CareerData.SAV)."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    const int teams = qFromBigEndian<quint16>(data.constData() + kTeamTable[variant]);
    const int players = qFromBigEndian<quint16>(data.constData() + kPlayerTable[variant]);
    if (teams > kMaxTeams || kTeamTable[variant] + 2 + qint64(teams) * kTeamSize[variant] > kPlayerTable[variant]
        || kPlayerTable[variant] + 2 + qint64(players) * kPlayerSize[variant] > data.size()) {
        setStatus(false, u"%1 isn't a Rugby League Live 4 career save (CareerData.SAV)."_s.arg(QFileInfo(path).fileName()));
        return false;
    }

    m_data = data;
    m_path = path;
    m_variant = variant;
    m_checksumOk = qFromLittleEndian<quint32>(m_data.constData() + 4) == crc32(m_data.constData() + 8, m_data.size() - 8);
    m_backedUp = false;
    m_loaded = true;
    m_logoUrls.clear();
    loadCustomFiles();
    loadCareer();
    if (m_unsaved) {
        m_unsaved = false;
        emit unsavedChangesChanged();
    }
    ++m_revision;
    emit loadedChanged();
    emit revisionChanged();

    QString text = u"Loaded %1: %2 teams, %3 players."_s.arg(fileName()).arg(teams).arg(players);
    if (!m_careerInfo.isEmpty())
        text += u" "_s + m_careerInfo + u"."_s;
    if (!m_checksumOk)
        text += u" The file's checksum doesn't match (it'll be right once saved)."_s;
    setStatus(true, text);
    return true;
}

void Rll4CareerEditorManager::loadOriginals() {}   // Not used: the career save has every player.

// The custom stadiums (Levels\LevelData.SAV) and logos (Decals\DecalData.SAV) next to the Career000 folder.
void Rll4CareerEditorManager::loadCustomFiles() {
    m_stadiums.clear();
    m_decals.clear();
    QDir root = QFileInfo(m_path).absoluteDir();
    root.cdUp();
    const auto readList = [](const QString& path, int recordSize, int nameSize, int minId, int maxId, int maxCount,
                             int valueAt, QList<Custom>& out) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return;
        // Only the start is needed (the lists come first).
        const QByteArray b = f.read(8 + qint64(recordSize) * maxCount + 4);
        if (b.size() < 12)
            return;
        const int first = qFromLittleEndian<qint32>(b.constData() + 8);
        if (first < minId || first > maxId)
            return;
        for (int i = 0; i < maxCount; ++i) {
            const qsizetype at = 8 + qsizetype(i) * recordSize;
            if (at + recordSize > b.size())
                break;
            const quint32 id = qFromLittleEndian<quint32>(b.constData() + at);
            if (id == 0)
                break;
            QByteArray name = b.mid(at + 4, nameSize);
            if (name.indexOf('\0') >= 0)
                name.truncate(name.indexOf('\0'));
            Custom c;
            c.id = static_cast<int>(id);
            c.name = QString::fromLatin1(name).trimmed();
            if (valueAt >= 0)
                c.value = QString::number(qFromLittleEndian<quint32>(b.constData() + at + valueAt));
            out.append(c);
        }
    };
    // LevelData: u32 id, name[128], 64 bytes, u32 logo id, 25 bytes. DecalData: u32 id, name[64], 29 bytes.
    readList(root.filePath(u"Levels/LevelData.SAV"_s), 225, 128, 1024, 1280, 256, 196, m_stadiums);
    readList(root.filePath(u"Decals/DecalData.SAV"_s), 97, 64, 1024, 6024, 5000, -1, m_decals);
}

bool Rll4CareerEditorManager::save() {
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

bool Rll4CareerEditorManager::saveAs(const QString& pathOrUrl) {
    if (!m_loaded)
        return false;
    const QString path = QDir::toNativeSeparators(localPath(pathOrUrl));
    qToLittleEndian<quint32>(crc32(m_data.constData() + 8, m_data.size() - 8), m_data.data() + 4);
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

qsizetype Rll4CareerEditorManager::recordAt(const QString& kind, int index) const {
    if (!m_loaded || index < 0)
        return -1;
    if (kind == u"team"_s)
        return index < teamCount() ? teamTable() + 2 + qsizetype(index) * teamSize() : -1;
    return index < playerCount() ? playerTable() + 2 + qsizetype(index) * playerSize() : -1;
}

int Rll4CareerEditorManager::recordSize(const QString& kind) const {
    return kind == u"team"_s ? teamSize() : playerSize();
}

QString Rll4CareerEditorManager::recordOffset(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    return at < 0 ? QString() : u"0x"_s + QString::number(at, 16).toUpper();
}

qint64 Rll4CareerEditorManager::readNum(qsizetype at, int size, bool big, bool isSigned) const {
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

void Rll4CareerEditorManager::writeNum(qsizetype at, int size, bool big, qint64 value) {
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

QString Rll4CareerEditorManager::readText(qsizetype at, int size) const {
    QByteArray b = m_data.mid(at, size);
    if (b.indexOf('\0') >= 0)
        b.truncate(b.indexOf('\0'));
    return QString::fromLatin1(b);
}

// The C# editor's write: the text, zero padded (the last byte always 0).
void Rll4CareerEditorManager::writeText(qsizetype at, int size, const QString& text) {
    if (at < 0 || at + size > m_data.size())
        return;
    QByteArray b = text.toLatin1().left(size - 1);
    b.resize(size, '\0');
    m_data.replace(at, size, b);
}

// =================================================================================================================
// Players
// =================================================================================================================

namespace {
QString keyOf(const FieldDef* d) { return QString::fromLatin1(d->key); }
} // namespace

int Rll4CareerEditorManager::playerIndexById(int id) const {
    if (id <= 0)
        return -1;
    const int count = playerCount();
    // Most saves keep player n at position n - 1.
    if (id <= count && readNum(recordAt(u"player"_s, id - 1) + kPlayerId, 4, true, true) == id)
        return id - 1;
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"player"_s, i) + kPlayerId, 4, true, true) == id)
            return i;
    return -1;
}

int Rll4CareerEditorManager::playerRating(int player) const {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return 0;
    const auto& terms = ratingTerms();
    const FieldDef* roleDef = defFor(u"player"_s, u"primaryRole"_s);
    const int role = static_cast<int>(readNum(at + (m_variant ? roleDef->offDemo[0] : roleDef->off[0]), 1, false, false));
    if (role < 0 || role >= terms.size())
        return 0;
    int sum = 0;
    for (const RatingTerm& t : std::as_const(terms[role])) {
        const FieldDef* d = defFor(u"player"_s, t.field);
        if (!d)
            return 0;
        sum += static_cast<int>(readNum(at + (m_variant ? d->offDemo[0] : d->off[0]), 1, false, false)) * t.mul / t.div;
    }
    return sum / static_cast<int>(terms[role].size());
}

int Rll4CareerEditorManager::ratingOfId(int id) const {
    const int p = playerIndexById(id);
    if (p >= 0)
        return playerRating(p);
    const auto it = m_originalIndex.constFind(id);
    return it == m_originalIndex.constEnd() ? 0 : m_originals.at(it.value()).rating;
}

// {id, name, role, rating, age, club, custom, playerIndex, placeKick, puntKick, roles}
QVariantMap Rll4CareerEditorManager::playerInfo(int id) const {
    QVariantMap m{{u"id"_s, id}, {u"custom"_s, false}, {u"role"_s, QString()}, {u"rating"_s, 0}, {u"age"_s, 0},
                  {u"club"_s, QString()}};
    const auto off = [this](const char* key) {
        const FieldDef* d = defFor(u"player"_s, QString::fromLatin1(key));
        return d ? (m_variant ? d->offDemo[0] : d->off[0]) : -1;
    };
    const int p = playerIndexById(id);
    if (p >= 0) {
        const qsizetype at = recordAt(u"player"_s, p);
        const int role = static_cast<int>(readNum(at + off("primaryRole"), 1, false, false));
        m[u"first"_s] = readText(at + off("firstName"), 20);
        m[u"last"_s] = readText(at + off("lastName"), 20);
        m[u"name"_s] = (m[u"first"_s].toString() + u' ' + m[u"last"_s].toString()).trimmed();
        m[u"known"_s] = true;
        m[u"role"_s] = kRoleShort.value(role);
        m[u"roles"_s] = QVariantList{role, static_cast<int>(readNum(at + off("secondaryRole"), 1, false, false)),
                                     static_cast<int>(readNum(at + off("tertiaryRole"), 1, false, false))};
        m[u"rating"_s] = playerRating(p);
        m[u"placeKick"_s] = static_cast<int>(readNum(at + off("skills.kickingSkills.placeKick"), 1, false, false));
        m[u"puntKick"_s] = static_cast<int>(readNum(at + off("skills.kickingSkills.puntKick"), 1, false, false));
        m[u"custom"_s] = true;
        m[u"club"_s] = teamNameById(static_cast<int>(readNum(at + off("feederClubId"), 4, true, true)));
        m[u"age"_s] = qMax(0, m_careerYear - static_cast<int>(readNum(at + off("dob.year"), 2, false, true)));
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
    m[u"placeKick"_s] = o.stats.value(u"skills.kickingSkills.placeKick"_s);
    m[u"puntKick"_s] = o.stats.value(u"skills.kickingSkills.puntKick"_s);
    m[u"club"_s] = u"Game player"_s;
    return m;
}

QVariantList Rll4CareerEditorManager::playerRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const int count = playerCount();
    const auto off = [this](const char* key) {
        const FieldDef* d = defFor(u"player"_s, QString::fromLatin1(key));
        return m_variant ? d->offDemo[0] : d->off[0];
    };
    for (int i = 0; i < count; ++i) {
        const qsizetype at = recordAt(u"player"_s, i);
        const int id = static_cast<int>(readNum(at + kPlayerId, 4, true, true));
        const QString first = readText(at + off("firstName"), 20);
        const QString last = readText(at + off("lastName"), 20);
        if (!f.isEmpty() && !(first + u' ' + last).contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        QVariantMap row;
        row[u"index"_s] = i;
        row[u"id"_s] = id;
        row[u"originalId"_s] = 0;
        row[u"first"_s] = first;
        row[u"last"_s] = last;
        row[u"shirt"_s] = readText(at + off("jerseyName"), 16);
        row[u"age"_s] = qMax(0, m_careerYear - static_cast<int>(readNum(at + off("dob.year"), 2, false, true)));
        row[u"female"_s] = readNum(at + off("gender"), 1, false, false) == 1;
        row[u"role"_s] = kRoleNames.value(static_cast<int>(readNum(at + off("primaryRole"), 1, false, false)));
        row[u"rating"_s] = playerRating(i);
        out.append(row);
    }
    return out;
}

// =================================================================================================================
// Teams
// =================================================================================================================

QVariantList Rll4CareerEditorManager::teamRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const int count = teamCount();
    const auto off = [this](const char* key) {
        const FieldDef* d = defFor(u"team"_s, QString::fromLatin1(key));
        return m_variant ? d->offDemo[0] : d->off[0];
    };
    const auto colour = [this](qsizetype at, const char* key) {
        const FieldDef* d = defFor(u"team"_s, QString::fromLatin1(key));
        const int* o = m_variant ? d->offDemo : d->off;
        return hexColour(static_cast<quint8>(m_data.at(at + o[0])), static_cast<quint8>(m_data.at(at + o[1])),
                         static_cast<quint8>(m_data.at(at + o[2])));
    };
    for (int i = 0; i < count; ++i) {
        const qsizetype at = recordAt(u"team"_s, i);
        const int id = static_cast<int>(readNum(at, 4, true, true));
        const QString full = readText(at + off("fullName"), 44);
        if (!f.isEmpty() && !full.contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        QVariantMap row;
        row[u"index"_s] = i;
        row[u"id"_s] = id;
        row[u"originalId"_s] = static_cast<int>(readNum(at + 4, 4, true, true));
        row[u"fullName"_s] = full;
        row[u"shortName"_s] = readText(at + off("clubName"), 19);
        row[u"abbreviation"_s] = readText(at + off("abbreviatedName"), 4);
        row[u"primaryColour"_s] = colour(at, "primaryColour");
        row[u"secondaryColour"_s] = colour(at, "secondaryColour");
        row[u"female"_s] = readNum(at + off("gender"), 1, false, false) == 1;
        row[u"logo"_s] = teamLogo(i);
        row[u"logoUrl"_s] = teamLogoUrl(i);
        out.append(row);
    }
    return out;
}

QString Rll4CareerEditorManager::teamLogo(int team) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || static_cast<quint8>(m_data.at(at + kLogoEnabled[m_variant])) == 255)
        return {};
    const int logo = static_cast<int>(readNum(at + kLogoId[m_variant], 2, true, false));
    if (logo >= kCustomLogo)
        return {};
    QString file = Rll4SaveLists::logoFiles().value(logo);
    return file.left(file.indexOf(u'.'));
}

// The logo from the loaded blobset (ui/club_logos/large_mirrored/<logo>[.pc].dds), like the Database Editor's.
QString Rll4CareerEditorManager::teamLogoUrl(int team) const {
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

int Rll4CareerEditorManager::teamRating(int team) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return 0;
    const int count = qMin<int>(17, static_cast<int>(readNum(at + kLineupAmount[m_variant], 2, true, false)));
    if (count <= 0)
        return 0;
    int sum = 0;
    for (int slot = 0; slot < count; ++slot)
        sum += ratingOfId(static_cast<int>(readNum(at + kLineups[m_variant] + 4 * slot, 4, true, true)));
    return sum / 17;
}

// =================================================================================================================
// Fields
// =================================================================================================================

QStringList Rll4CareerEditorManager::groups(const QString& kind) const {
    QStringList out;
    for (const auto& dList = defsFor(kind); const FieldDef* d : dList)
        if (!out.contains(QString::fromLatin1(d->group)))
            out << QString::fromLatin1(d->group);
    return out;
}

QStringList Rll4CareerEditorManager::pageGroups(const QString& kind, const QString& page) const {
    if (kind == u"team"_s) {
        if (page == u"Details"_s)
            return {u"Names"_s, u"Details"_s, u"Colours"_s, u"Record"_s};
        if (page == u"Stadiums"_s)
            return {u"Stadiums"_s, u"Stadium 1"_s, u"Stadium 2"_s, u"Stadium 3"_s};
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
        return {u"Attributes"_s, u"Kicking"_s, u"Passing"_s, u"Evasion"_s, u"Tackling"_s, u"Misc Skills"_s};
    if (page == u"Stats"_s)
        return {u"Career Stats"_s};
    return {};
}

QVariantList Rll4CareerEditorManager::fields(const QString& kind, int index) const {
    if (kind == u"career"_s)
        return careerFields(index);
    if (kind == u"contract"_s)
        return contractFields(index);
    return recordFields(kind, index, 0);
}

QVariantList Rll4CareerEditorManager::appearanceFields(int player) const {
    return recordFields(u"player"_s, player, kLook);
}

bool Rll4CareerEditorManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    return setField(u"player"_s, player, key, value);
}

// wantPage 0: everything but the appearance; kLook: the appearance.
QVariantList Rll4CareerEditorManager::recordFields(const QString& kind, int index, int wantPage) const {
    QVariantList out;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return out;
    for (const auto& dList = defsFor(kind); const FieldDef* d : dList) {
        if ((d->flags & kLook) != (wantPage & kLook))
            continue;
        const int* o = m_variant ? d->offDemo : d->off;
        if (d->type != Type::Virtual && o[0] < 0)
            continue;   // Not in this version of the game.
        QVariantMap m;
        const QString key = keyOf(d);
        m[u"key"_s] = key;
        m[u"label"_s] = QString::fromLatin1(d->label);
        m[u"group"_s] = QString::fromLatin1(d->group);
        m[u"readOnly"_s] = (d->flags & kReadOnly) != 0;
        m[u"slider"_s] = (d->flags & kSlider) != 0;
        m[u"offset"_s] = o[0] >= 0 ? u"+0x"_s + QString::number(o[0], 16).toUpper() : QString();
        m[u"min"_s] = d->min;
        QVariant value;
        QString kindName = u"number"_s;
        qint64 max = d->max;
        switch (d->type) {
        case Type::Virtual: value = virtualValue(kind, at, key); break;
        case Type::Str:
            value = readText(at + o[0], d->size);
            kindName = u"text"_s;
            max = d->size - 1;
            break;
        case Type::Bool: value = m_data.at(at + o[0]) != 0; kindName = u"bool"_s; break;
        case Type::Bool255: value = static_cast<quint8>(m_data.at(at + o[0])) == 255; kindName = u"bool"_s; break;
        case Type::Rgb:
            value = hexColour(static_cast<quint8>(m_data.at(at + o[0])), static_cast<quint8>(m_data.at(at + o[1])),
                              static_cast<quint8>(m_data.at(at + o[2])));
            kindName = u"colour"_s;
            break;
        default: {
            const bool isSigned = d->type == Type::I16 || d->type == Type::I32;
            value = static_cast<double>(readNum(at + o[0], d->size, d->big, isSigned));
            if (max <= 0)
                max = d->type == Type::U8 ? 255 : d->type == Type::U16 ? 65535 : d->type == Type::I16 ? 32767 : 2147483647LL;
            if (d->type == Type::I32)
                kindName = u"big"_s;
        }
        }
        if (d->type == Type::Bool && (d->flags & kReadOnly))
            value = m_data.at(at + o[0]) == 1;
        m[u"value"_s] = value;
        m[u"max"_s] = static_cast<double>(max);
        if (d->list) {
            const QVariantList choices = choicesFor(kind, at, QString::fromLatin1(d->list), value);
            if (!choices.isEmpty()) {
                kindName = u"choice"_s;
                m[u"choices"_s] = choices;
            }
        }
        m[u"kind"_s] = kindName;
        out.append(m);
    }
    return out;
}

QVariant Rll4CareerEditorManager::virtualValue(const QString& kind, qsizetype at, const QString& key) const {
    if (kind == u"player"_s) {
        // The player's entry in the status list (none = available).
        const int s = statusIndexOf(static_cast<int>(readNum(at + kPlayerId, 4, true, true)));
        const qsizetype e = kStatusAt + 2 + qsizetype(s) * kStatusSize;
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
    if (kind != u"team"_s)
        return {};
    if (key == u"@logo"_s) {
        if (static_cast<quint8>(m_data.at(at + kLogoEnabled[m_variant])) == 255)
            return kCustomLogo;
        return static_cast<int>(readNum(at + kLogoId[m_variant], 2, true, false));
    }
    if (key == u"@customLogo"_s)
        return static_cast<int>(readNum(at + kLogoCustomId[m_variant], 2, true, false));
    if (key.startsWith(u"@stadium"_s)) {
        // The C# editor's stadium list: its 50 names (stored number - 5; 0-2 as custom slot numbers), then the
        // custom stadiums from LevelData.SAV (50 + their position).
        const qsizetype s = at + kStadium0[m_variant] + kStadiumStride * QStringView(key).mid(8).toInt();
        const int id = static_cast<quint8>(m_data.at(s + kStadium_id));
        const int sharing = static_cast<int>(readNum(s + kStadium_sharingId, 4, false, true));
        if (id == 1 && sharing >= 0 && sharing < 3)
            return sharing;
        if (id == 1) {
            for (int i = 0; i < m_stadiums.size(); ++i)
                if (m_stadiums.at(i).id == sharing)
                    return 50 + i;
            return 3;
        }
        return id - 5;
    }
    return {};
}

QVariantList Rll4CareerEditorManager::choicesFor(const QString& kind, qsizetype at, const QString& list,
                                               const QVariant& value) const {
    Q_UNUSED(kind)
    Q_UNUSED(at)
    QVariantList out;
    if (list == u"@logos"_s) {
        out = namedChoices(Rll4SaveLists::logoNames());
        if (!out.isEmpty())
            out[kCustomLogo] = QVariantMap{{u"name"_s, u"Custom Logo (below)"_s}, {u"value"_s, kCustomLogo}};
    } else if (list == u"@customLogos"_s) {
        const QStringList& builtIn = Rll4SaveLists::customLogos();
        // The C# editor: 0 none, 2 Big Ant, 21 + position for the sponsors, else a DecalData.SAV logo id.
        for (int i = 0; i < builtIn.size(); ++i)
            out << QVariantMap{{u"name"_s, builtIn.at(i)}, {u"value"_s, i == 0 ? 0 : i == 1 ? 2 : 21 + i}};
        for (const Custom& c : std::as_const(m_decals))
            out << QVariantMap{{u"name"_s, c.name}, {u"value"_s, c.id}};
    } else if (list == u"@stadiums"_s) {
        out = namedChoices(Rll4SaveLists::stadiums());
        for (int i = 0; i < m_stadiums.size(); ++i)
            out << QVariantMap{{u"name"_s, m_stadiums.at(i).name + u" (custom)"_s}, {u"value"_s, 50 + i}};
    } else if (list == u"@contract"_s) {
        // The C# editor's: the career year and the 19 after it, or none.
        for (int y = m_careerYear; y < m_careerYear + 20; ++y)
            out << QVariantMap{{u"name"_s, QString::number(y)}, {u"value"_s, y}};
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, value.toInt() == 0 ? 0 : 65535}};
    } else if (list == u"@feeder"_s) {
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}};
        const int count = teamCount();
        for (int i = 0; i < count && i < 98; ++i) {
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
    } else if (list == u"@soo"_s || list == u"@cvc"_s || list == u"@allStars"_s) {
        const int base = list == u"@soo"_s ? 196 : list == u"@cvc"_s ? 199 : 202;
        const QStringList names = list == u"@soo"_s ? QStringList{u"None"_s, u"Queensland"_s, u"New South Wales"_s}
                                : list == u"@cvc"_s ? QStringList{u"None"_s, u"City"_s, u"Country"_s}
                                                    : QStringList{u"None"_s, u"World All Stars"_s, u"Indigenous All Stars"_s};
        out = namedChoices(names, base);
        if (value.toInt() == 0)
            out[0] = QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}};
    } else if (list == u"@faces"_s) {
        for (const auto& cList = Rll4SaveLists::faceMeshes(); const Rll4SaveLists::Choice& c : cList)
            out << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
    } else if (list == u"countriesOfBirth"_s) {
        out = namedChoices(Rll4SaveLists::countriesOfBirth());
    } else if (list == u"repCountries"_s) {
        out = namedChoices(Rll4SaveLists::repCountries());
    } else if (list == u"commentarySurnames"_s) {
        out = namedChoices(Rll4SaveLists::commentarySurnames());
    } else if (list == u"commentaryTeams"_s) {
        out = namedChoices(Rll4SaveLists::commentaryTeams());
    } else if (list == u"supporters"_s) {
        out = namedChoices(Rll4SaveLists::supporters());
    } else if (list == u"headgear"_s) {
        out = namedChoices(Rll4SaveLists::headgear());
    } else if (list == u"roles"_s) {
        out = namedChoices(kRoleNames);
    } else if (list == u"perks"_s) {
        out = namedChoices({u"None"_s, u"Speed"_s, u"Stamina"_s, u"Contests"_s, u"Place Kicking"_s, u"In Play Kicking"_s});
    } else if (list == u"teamTypes"_s) {
        out = namedChoices({u"Standard"_s, u"International"_s, u"State Of Origin"_s, u"City V's Country"_s, u"All Stars"_s});
    } else if (list == u"leftRight"_s) {
        out = namedChoices({u"Left"_s, u"Right"_s});
    } else if (list == u"compression"_s) {
        out = namedChoices({u"Blank"_s, u"Short 1"_s, u"Short 2"_s, u"Long 1"_s, u"Long 2"_s});
    }
    if (out.isEmpty())
        return out;
    // A value the list doesn't have: shown as its number so nothing changes unless picked.
    const int v = value.toInt();
    const bool known = std::any_of(out.cbegin(), out.cend(), [v](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == v; });
    if (!known)
        out.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(v)}, {u"value"_s, v}});
    return out;
}

bool Rll4CareerEditorManager::setField(const QString& kind, int index, const QString& key, const QVariant& value) {
    if (kind == u"career"_s)
        return setCareerField(index, key, value);
    if (kind == u"contract"_s)
        return setContractField(index, key, value);
    const qsizetype at = recordAt(kind, index);
    const FieldDef* d = defFor(kind, key);
    if (at < 0 || !d || (d->flags & kReadOnly))
        return false;
    const QByteArray before = m_data.mid(at, recordSize(kind));
    const int* o = m_variant ? d->offDemo : d->off;
    switch (d->type) {
    case Type::Virtual:
        if (!setVirtual(kind, at, key, value))
            return false;
        if (kind == u"player"_s) {   // The status list changed (not the record).
            edited();
            return true;
        }
        break;
    case Type::Str: writeText(at + o[0], d->size, value.toString()); break;
    case Type::Bool: m_data[at + o[0]] = value.toBool() ? 1 : 0; break;
    case Type::Bool255: m_data[at + o[0]] = static_cast<char>(value.toBool() ? 255 : 0); break;
    case Type::Rgb: {
        QString hex = value.toString().trimmed();
        if (hex.startsWith(u'#'))
            hex.remove(0, 1);
        bool ok = false;
        const uint rgb = hex.toUInt(&ok, 16);
        if (!ok || hex.size() != 6)
            return false;
        m_data[at + o[0]] = static_cast<char>((rgb >> 16) & 0xFF);
        m_data[at + o[1]] = static_cast<char>((rgb >> 8) & 0xFF);
        m_data[at + o[2]] = static_cast<char>(rgb & 0xFF);
        break;
    }
    default: {
        qint64 v = static_cast<qint64>(value.toDouble());
        if (d->max > 0)
            v = std::clamp<qint64>(v, d->min, d->max);
        writeNum(at + o[0], d->size, d->big, v);
    }
    }
    if (m_data.mid(at, recordSize(kind)) == before)
        return false;
    if (kind == u"team"_s && key == u"@logo"_s)
        m_logoUrls.clear();
    edited();
    return true;
}

bool Rll4CareerEditorManager::setVirtual(const QString& kind, qsizetype at, const QString& key, const QVariant& value) {
    if (kind == u"player"_s)
        return setPlayerStatus(static_cast<int>(readNum(at + kPlayerId, 4, true, true)), key, value.toInt());
    if (kind != u"team"_s)
        return false;
    const int v = value.toInt();
    if (key == u"@logo"_s) {
        if (v < 0 || v > kCustomLogo)
            return false;
        writeNum(at + kLogoId[m_variant], 2, true, v);
        if (v != kCustomLogo) {
            writeNum(at + kLogoCustomId[m_variant], 2, true, 0);
            m_data[at + kLogoEnabled[m_variant]] = 0;
        }
        return true;
    }
    if (key == u"@customLogo"_s) {
        writeNum(at + kLogoCustomId[m_variant], 2, true, v);
        if (v != 0) {
            m_data[at + kLogoEnabled[m_variant]] = static_cast<char>(255);
            writeNum(at + kLogoId[m_variant], 2, true, kCustomLogo);
        } else {
            m_data[at + kLogoEnabled[m_variant]] = 0;
        }
        return true;
    }
    if (key.startsWith(u"@stadium"_s)) {
        const qsizetype s = at + kStadium0[m_variant] + kStadiumStride * QStringView(key).mid(8).toInt();
        m_data[s + kStadium_isOnDisk] = 0;
        m_data[s + kStadium_key] = 0;
        if (v >= 0 && v < 3) {
            m_data[s + kStadium_id] = 1;
            writeNum(s + kStadium_sharingId, 4, false, v);
            writeNum(s + kStadium_logoID, 4, false, -1);
        } else if (v >= 3 && v < 50) {
            m_data[s + kStadium_id] = static_cast<char>(5 + v);
            writeNum(s + kStadium_sharingId, 4, false, qint64(INT32_MIN));
            writeNum(s + kStadium_logoID, 4, false, 0);
        } else if (v >= 50 && v - 50 < m_stadiums.size()) {
            const Custom& c = m_stadiums.at(v - 50);
            m_data[s + kStadium_id] = 1;
            writeNum(s + kStadium_sharingId, 4, false, c.id);
            writeNum(s + kStadium_logoID, 4, false, c.value.toLongLong());
        } else {
            return false;
        }
        return true;
    }
    return false;
}

// =================================================================================================================
// Roster and lineups
// =================================================================================================================

QList<int> Rll4CareerEditorManager::rosterIds(int team) const {
    QList<int> out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qMin<int>(rosterMax(), static_cast<int>(readNum(at + kPlayerAmount[m_variant], 2, true, false)));
    for (int i = 0; i < count; ++i)
        out << static_cast<int>(readNum(at + kPlayers[m_variant] + 4 * i, 4, true, true));
    return out;
}

QVariantList Rll4CareerEditorManager::rosterRows(int team) const {
    QVariantList out;
    const QList<int> ids = rosterIds(team);
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap m = playerInfo(ids.at(i));
        m[u"slot"_s] = i;
        out.append(m);
    }
    return out;
}

bool Rll4CareerEditorManager::addRosterPlayer(int team, int playerId) {
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
    writeNum(at + kPlayers[m_variant] + 4 * ids.size(), 4, true, playerId);
    writeNum(at + kPlayerAmount[m_variant], 2, true, ids.size() + 1);
    edited();
    return true;
}

bool Rll4CareerEditorManager::removeRosterPlayer(int team, int slot) {
    const qsizetype at = recordAt(u"team"_s, team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || slot < 0 || slot >= ids.size())
        return false;
    ids.removeAt(slot);
    for (int i = 0; i < rosterMax(); ++i)
        writeNum(at + kPlayers[m_variant] + 4 * i, 4, true, i < ids.size() ? ids.at(i) : 0);
    writeNum(at + kPlayerAmount[m_variant], 2, true, ids.size());
    edited();
    return true;
}

bool Rll4CareerEditorManager::moveRosterPlayer(int team, int slot, int delta) {
    const qsizetype at = recordAt(u"team"_s, team);
    QList<int> ids = rosterIds(team);
    const int to = slot + delta;
    if (at < 0 || slot < 0 || slot >= ids.size() || to < 0 || to >= ids.size())
        return false;
    ids.swapItemsAt(slot, to);
    writeNum(at + kPlayers[m_variant] + 4 * slot, 4, true, ids.at(slot));
    writeNum(at + kPlayers[m_variant] + 4 * to, 4, true, ids.at(to));
    edited();
    return true;
}

bool Rll4CareerEditorManager::sortRoster(int team) {
    const qsizetype at = recordAt(u"team"_s, team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || ids.size() < 2)
        return false;
    QHash<int, QString> lastNames;
    for (int id : std::as_const(ids)) {
        const QString name = playerInfo(id).value(u"name"_s).toString();
        lastNames.insert(id, name.section(u' ', -1) + u' ' + name);
    }
    const QList<int> before = ids;
    std::stable_sort(ids.begin(), ids.end(), [&lastNames](int a, int b) {
        return lastNames.value(a).compare(lastNames.value(b), Qt::CaseInsensitive) < 0;
    });
    if (ids == before)
        return false;
    for (int i = 0; i < ids.size(); ++i)
        writeNum(at + kPlayers[m_variant] + 4 * i, 4, true, ids.at(i));
    edited();
    return true;
}

bool Rll4CareerEditorManager::databasePlayersAvailable() const {
    return playerCount() > 0;   // Every player is in the career save.
}

QVariantList Rll4CareerEditorManager::pickablePlayers(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const auto matches = [&f](const QVariantMap& m) {
        return f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive)
            || m.value(u"id"_s).toString() == f;
    };
    const int count = playerCount();
    for (int i = 0; i < count; ++i) {
        const QVariantMap m = playerInfo(static_cast<int>(readNum(recordAt(u"player"_s, i) + kPlayerId, 4, true, true)));
        if (matches(m))
            out.append(m);
    }
    for (const Original& o : std::as_const(m_originals)) {
        if (out.size() >= 400)
            break;
        const QVariantMap m = playerInfo(o.id);
        if (m.value(u"custom"_s).toBool())
            continue;   // Edited in this save: listed above.
        if (matches(m))
            out.append(m);
    }
    return out;
}

QVariantMap Rll4CareerEditorManager::lineup(int team) const {
    QVariantMap out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const auto group = [this, at](const int* countOff, const int* idsOff, const QStringList& labels, bool order) {
        QVariantList rows;
        const int count = qMin<int>(labels.size(), static_cast<int>(readNum(at + countOff[m_variant], 2, true, false)));
        for (int i = 0; i < count; ++i) {
            const int slot = order ? kLineupSlot[i] : i;
            const int id = static_cast<int>(readNum(at + idsOff[m_variant] + 4 * slot, 4, true, true));
            QVariantMap m = playerInfo(id);
            m[u"slot"_s] = i;
            m[u"label"_s] = labels.at(i);
            rows.append(m);
        }
        return rows;
    };
    out[u"standard"_s] = group(kLineupAmount, kLineups, kPositions, true);
    out[u"roles"_s] = group(kRolesAmount, kRoles, kTeamRoles, false);
    out[u"nines"_s] = group(kNinesAmount, kNines, kNinesPositions, false);
    out[u"ninesRoles"_s] = group(kNinesRolesAmount, kNinesRoles, kTeamRoles, false);
    out[u"hasNines"_s] = readNum(at + kNinesAmount[m_variant], 2, true, false) > 0;
    out[u"rating"_s] = teamRating(team);
    // Who can be picked: the roster (and anyone already picked who isn't in it any more).
    QVariantList choices;
    QList<int> seen;
    const auto add = [&choices, &seen, this](int id) {
        if (seen.contains(id))
            return;
        seen << id;
        const QVariantMap m = playerInfo(id);
        choices << QVariantMap{{u"name"_s, u"%1  (%2, %3)"_s.arg(m.value(u"name"_s).toString(), m.value(u"role"_s).toString())
                                               .arg(m.value(u"rating"_s).toInt())},
                               {u"value"_s, id}};
    };
    for (const auto& idList = rosterIds(team); int id : idList)
        add(id);
    for (const char* g : {"standard", "roles", "nines", "ninesRoles"})
        for (const auto& rList = out.value(QString::fromLatin1(g)).toList(); const QVariant& r : rList)
            add(r.toMap().value(u"id"_s).toInt());
    out[u"choices"_s] = choices;
    return out;
}

bool Rll4CareerEditorManager::setLineupPlayer(int team, const QString& group, int slot, int playerId) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || slot < 0)
        return false;
    qsizetype p = -1;
    if (group == u"standard"_s && slot < 17)
        p = at + kLineups[m_variant] + 4 * kLineupSlot[slot];
    else if (group == u"roles"_s && slot < 4)
        p = at + kRoles[m_variant] + 4 * slot;
    else if (group == u"nines"_s && slot < kNinesSlots)
        p = at + kNines[m_variant] + 4 * slot;
    else if (group == u"ninesRoles"_s && slot < 4)
        p = at + kNinesRoles[m_variant] + 4 * slot;
    if (p < 0 || readNum(p, 4, true, true) == playerId)
        return false;
    writeNum(p, 4, true, playerId);
    edited();
    return true;
}

bool Rll4CareerEditorManager::setHasNines(int team, bool on) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    if (on && rosterIds(team).size() < 17) {
        setStatus(false, u"A Nines lineup needs at least 17 players in the roster."_s);
        return false;
    }
    if ((readNum(at + kNinesAmount[m_variant], 2, true, false) > 0) == on)
        return false;
    writeNum(at + kNinesAmount[m_variant], 2, true, on ? kNinesSlots : 0);
    writeNum(at + kNinesRolesAmount[m_variant], 2, true, on ? 4 : 0);
    if (on) {
        // The lineup's name (after the Nines roles): "Nines".
        writeText(at + kNinesRoles[m_variant] + 16, 16, u"Nines"_s);
    }
    edited();
    return true;
}

// The C# editor's Generate Best Lineup: the roster by rating, each position from the players whose primary role
// (else secondary, else tertiary) fits; captain = best rated, goal kicker = best place kick, play makers = the two
// best punt kicks (of the lineup).
QString Rll4CareerEditorManager::generateBestLineup(int team) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return u"No team."_s;
    struct P {
        int id, rating, roles[3], place, punt;
    };
    QList<P> players;
    for (const auto& idList = rosterIds(team); int id : idList) {
        const QVariantMap m = playerInfo(id);
        const QVariantList roles = m.value(u"roles"_s).toList();
        P p{id, m.value(u"rating"_s).toInt(), {roles.value(0, -1).toInt(), roles.value(1, -1).toInt(), roles.value(2, -1).toInt()},
            m.value(u"placeKick"_s).toInt(), m.value(u"puntKick"_s).toInt()};
        players << p;
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
    const auto pick = [](const QList<int>& ids, int n) { return n < ids.size() ? ids.at(n) : 0; };

    // Standard: position <- (role, n-th best).
    int lineupIds[17] = {};
    const int map[17][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {1, 1}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {5, 1}, {7, 0},
                            {7, 1}, {8, 0}, {5, 2}, {5, 3}, {7, 2}, {7, 3}};
    static const QStringList needs = {u"fullbacks"_s, u"right wingers"_s, u"right centres"_s, u"left centres"_s,
                                      u"left wingers"_s, u"five-eighths"_s, u"halfbacks"_s, u"props"_s, u"hookers"_s,
                                      u"props (10)"_s, u"right second rowers"_s, u"left second rowers"_s, u"locks"_s,
                                      u"interchange props (14)"_s, u"interchange props (15)"_s,
                                      u"interchange second rowers (16)"_s, u"interchange second rowers (17)"_s};
    QStringList missing;
    for (int i = 0; i < 17; ++i) {
        lineupIds[i] = pick(byRole(map[i][0]), map[i][1]);
        if (lineupIds[i] != 0)
            writeNum(at + kLineups[m_variant] + 4 * kLineupSlot[i], 4, true, lineupIds[i]);
        else
            missing << needs.at(i);
    }
    const auto kickers = [&players](const QList<int>& ids, int which) {
        QList<P> list;
        for (const P& p : std::as_const(players))
            if (ids.contains(p.id))
                list << p;
        std::stable_sort(list.begin(), list.end(), [which](const P& a, const P& b) {
            const int x = which == 0 ? a.rating : which == 1 ? a.place : a.punt;
            const int y = which == 0 ? b.rating : which == 1 ? b.place : b.punt;
            return x > y;
        });
        return list;
    };
    const auto setRoles = [this, at, &kickers](const QList<int>& ids, const int* rolesOff) {
        if (ids.isEmpty())
            return;
        const QList<P> best = kickers(ids, 0), place = kickers(ids, 1), punt = kickers(ids, 2);
        writeNum(at + rolesOff[m_variant], 4, true, best.at(0).id);
        writeNum(at + rolesOff[m_variant] + 4, 4, true, place.at(0).id);
        writeNum(at + rolesOff[m_variant] + 8, 4, true, punt.at(0).id);
        writeNum(at + rolesOff[m_variant] + 12, 4, true, punt.value(1, punt.at(0)).id);
    };
    QList<int> standard;
    for (int id : std::as_const(lineupIds))
        if (id)
            standard << id;
    setRoles(standard, kRoles);

    // Nines (when the team has one).
    if (readNum(at + kNinesAmount[m_variant], 2, true, false) > 0) {
        int nines[kNinesSlots] = {};
        nines[0] = pick(byRole(0), 0);
        nines[1] = pick(byRole(1), 0);
        nines[2] = pick(byRole(2), 0);
        nines[13] = pick(byRole(2), 1);
        nines[3] = pick(byRole(3), 0);
        nines[11] = pick(byRole(3), 1);
        nines[4] = pick(byRole(4), 0);
        nines[5] = pick(byRole(5), 0);
        nines[12] = pick(byRole(5), 1);
        nines[6] = pick(byRole(6), 0);
        nines[7] = pick(byRole(7), 0);
        nines[9] = pick(byRole(7), 1);
        nines[8] = pick(byRole(8), 0);
        if (!nines[12])
            nines[12] = pick(byRole(8), 1);
        QList<int> ninesIds;
        for (int i = 0; i < kNinesSlots; ++i) {
            if (nines[i]) {
                writeNum(at + kNines[m_variant] + 4 * i, 4, true, nines[i]);
                ninesIds << nines[i];
            }
        }
        setRoles(ninesIds, kNinesRoles);
    }
    edited();
    if (missing.isEmpty())
        return {};
    return u"Not enough %1 in the roster: those places weren't changed."_s.arg(missing.join(u", "_s));
}

// --- The Database Editor's lineup page (matchLists / lineupRows / teamRoles) ---------------------------------------

QVariantList Rll4CareerEditorManager::matchLists(int team) const {
    Q_UNUSED(team)
    return {QVariantMap{{u"list"_s, u"standard"_s}, {u"title"_s, u"Standard"_s}, {u"shirts"_s, false}},
            QVariantMap{{u"list"_s, u"nines"_s}, {u"title"_s, u"Nines"_s}, {u"shirts"_s, false}}};
}

bool Rll4CareerEditorManager::listPresent(int team, const QString& list) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    const int* countOff = list == u"nines"_s ? kNinesAmount : kLineupAmount;
    return readNum(at + countOff[m_variant], 2, true, false) > 0;
}

bool Rll4CareerEditorManager::setListPresent(int team, const QString& list, bool on) {
    if (list == u"nines"_s)
        return setHasNines(team, on);
    if (!on)
        setStatus(false, u"Every team has a standard lineup."_s);
    return false;
}

// Rows like the Database Editor's: {key, position, id, first, last, roles, rating, present, known, playerIndex}.
QVariantMap Rll4CareerEditorManager::lineupRow(int id, bool present) const {
    QVariantMap m = playerInfo(present ? id : 0);
    QStringList roles;
    for (const auto& rList = m.value(u"roles"_s).toList(); const QVariant& r : rList)
        roles << kRoleShort.value(r.toInt());
    m[u"roles"_s] = roles.join(u" / "_s);
    m[u"present"_s] = present;
    return m;
}

QVariantList Rll4CareerEditorManager::lineupRows(int team, const QString& list) const {
    QVariantList rows;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return rows;
    const bool nines = list == u"nines"_s;
    const bool on = listPresent(team, list);
    const QStringList& labels = nines ? kNinesPositions : kPositions;
    for (int i = 0; i < labels.size(); ++i) {
        const qsizetype p = nines ? at + kNines[m_variant] + 4 * i : at + kLineups[m_variant] + 4 * kLineupSlot[i];
        QVariantMap row = lineupRow(static_cast<int>(readNum(p, 4, true, true)), on);
        row[u"key"_s] = u"p:%1"_s.arg(i);
        row[u"position"_s] = labels.at(i);
        rows << row;
    }
    return rows;
}

QVariantList Rll4CareerEditorManager::teamRoles(int team, const QString& list) const {
    QVariantList rows;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return rows;
    const bool on = listPresent(team, list);
    const int* rolesOff = list == u"nines"_s ? kNinesRoles : kRoles;
    for (int r = 0; r < kTeamRoles.size(); ++r) {
        QVariantMap row = lineupRow(static_cast<int>(readNum(at + rolesOff[m_variant] + 4 * r, 4, true, true)), on);
        row[u"key"_s] = u"r:%1"_s.arg(r);
        row[u"name"_s] = kTeamRoles.at(r);
        rows << row;
    }
    return rows;
}

bool Rll4CareerEditorManager::setLineupSlot(int team, const QString& list, const QString& key, int playerId) {
    const QStringList parts = key.split(u':');
    if (parts.size() != 2)
        return false;
    const bool nines = list == u"nines"_s;
    if (nines && !listPresent(team, list) && !setHasNines(team, true))
        return false;
    if (parts.at(0) == u"r"_s)
        return setLineupPlayer(team, nines ? u"ninesRoles"_s : u"roles"_s, parts.at(1).toInt(), playerId);
    return setLineupPlayer(team, nines ? u"nines"_s : u"standard"_s, parts.at(1).toInt(), playerId);
}

// The Database Editor's player picker: {id, first, last, clubName, roleName, age, rating}; squadOnly: the team's
// roster.
QVariantList Rll4CareerEditorManager::pickerRows(const QString& filter, int team, bool squadOnly) const {
    QVariantList out;
    const QList<int> squad = squadOnly ? rosterIds(team) : QList<int>();
    const auto toRow = [](const QVariantMap& m) {
        return QVariantMap{{u"id"_s, m.value(u"id"_s)}, {u"first"_s, m.value(u"first"_s)}, {u"last"_s, m.value(u"last"_s)},
                           {u"clubName"_s, m.value(u"club"_s)}, {u"roleName"_s, m.value(u"role"_s)},
                           {u"age"_s, m.value(u"age"_s)}, {u"rating"_s, m.value(u"rating"_s)}};
    };
    if (squadOnly) {
        const QString f = filter.trimmed();
        for (int id : std::as_const(squad)) {
            const QVariantMap m = playerInfo(id);
            if (f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive) || QString::number(id) == f)
                out << toRow(m);
        }
        return out;
    }
    for (const auto& vList = pickablePlayers(filter); const QVariant& v : vList)
        out << toRow(v.toMap());
    return out;
}

// =================================================================================================================
// Jerseys
// =================================================================================================================

qsizetype Rll4CareerEditorManager::jerseyAt(int team, int uniform) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || uniform < 0 || uniform >= kMaxJerseys)
        return -1;
    return at + kJerseyBase[m_variant] + qsizetype(uniform) * kJerseyStride;
}

QVariantMap Rll4CareerEditorManager::jerseys(int team) const {
    QVariantMap out{{u"ok"_s, false}};
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qBound(0, static_cast<int>(static_cast<quint8>(m_data.at(at + kJerseyAmount[m_variant]))), kMaxJerseys);
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

QVariantList Rll4CareerEditorManager::jerseyFields(int team, int uniform) const {
    QVariantList out;
    const qsizetype base = jerseyAt(team, uniform);
    if (base < 0)
        return out;
    // The manufacturer and licensed jersey bytes (the C# editor's names).
    const qsizetype makerAt = base + 313;    // jerseys[0].manufactureId - jerseys[0].name
    const qsizetype licensedAt = base + 333; // jerseys[0].licensedId - jerseys[0].name (u16)
    const int maker = static_cast<quint8>(m_data.at(makerAt));
    const int licensed = static_cast<int>(readNum(licensedAt, 2, false, false));
    for (const auto& dList = jerseyDefs(); const FieldDef* d : dList) {
        const QString key = keyOf(d);
        QVariantMap m;
        m[u"key"_s] = u"j:%1:%2"_s.arg(uniform).arg(key);
        m[u"label"_s] = QString::fromLatin1(d->label);
        m[u"group"_s] = QString::fromLatin1(d->group);
        m[u"min"_s] = 0;
        m[u"offset"_s] = d->off[0] >= 0 ? u"+0x"_s + QString::number(base - recordAt(u"team"_s, team) + d->off[0], 16).toUpper() : QString();
        if (key == u"@maker"_s) {
            QVariantList makers;
            for (const auto& cList = Rll4Lists::manufacturers(); const Rll4Lists::Choice& c : cList)
                makers << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
            if (std::none_of(makers.cbegin(), makers.cend(), [maker](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == maker; }))
                makers.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(maker)}, {u"value"_s, maker}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = makers;
            m[u"value"_s] = maker;
            m[u"refresh"_s] = true;
        } else if (key == u"@licensed"_s) {
            QVariantList jerseys{QVariantMap{{u"name"_s, u"Custom Jersey"_s}, {u"value"_s, 0}}};
            for (const auto& cList = Rll4Lists::licensedJerseys(maker); const Rll4Lists::Choice& c : cList)
                jerseys << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
            if (std::none_of(jerseys.cbegin(), jerseys.cend(), [licensed](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == licensed; }))
                jerseys.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(licensed)}, {u"value"_s, licensed}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = jerseys;
            m[u"value"_s] = licensed;
        } else if (d->type == Type::Str) {
            m[u"kind"_s] = u"text"_s;
            m[u"value"_s] = readText(base + d->off[0], d->size);
            m[u"max"_s] = d->size - 1;
        } else if (d->type == Type::Bool) {
            m[u"kind"_s] = u"bool"_s;
            m[u"value"_s] = m_data.at(base + d->off[0]) != 0;
        } else if (d->type == Type::Rgb) {
            m[u"kind"_s] = u"colour"_s;
            m[u"value"_s] = hexColour(static_cast<quint8>(m_data.at(base + d->off[0])), static_cast<quint8>(m_data.at(base + d->off[1])),
                                      static_cast<quint8>(m_data.at(base + d->off[2])));
        } else {
            m[u"kind"_s] = u"number"_s;
            m[u"value"_s] = static_cast<int>(readNum(base + d->off[0], d->size, d->big, false));
            m[u"max"_s] = d->max > 0 ? d->max : 255;
        }
        out.append(m);
    }
    return out;
}

bool Rll4CareerEditorManager::setJerseyField(int team, const QString& key, const QVariant& value) {
    const QStringList parts = key.split(u':');
    if (parts.size() != 3 || parts.at(0) != u"j"_s)
        return false;
    const int uniform = parts.at(1).toInt();
    const qsizetype base = jerseyAt(team, uniform);
    const FieldDef* d = findDef(jerseyDefs(), parts.at(2));
    if (base < 0 || !d)
        return false;
    const QByteArray before = m_data.mid(base, kJerseyStride);
    const QString& name = parts.at(2);
    if (name == u"@maker"_s) {
        m_data[base + 313] = static_cast<char>(value.toInt());
        writeNum(base + 333, 2, false, 0);   // A new manufacturer: its custom jersey.
    } else if (name == u"@licensed"_s) {
        writeNum(base + 333, 2, false, value.toInt());
    } else if (d->type == Type::Str) {
        writeText(base + d->off[0], d->size, value.toString());
    } else if (d->type == Type::Bool) {
        m_data[base + d->off[0]] = value.toBool() ? 1 : 0;
    } else if (d->type == Type::Rgb) {
        QString hex = value.toString().trimmed();
        if (hex.startsWith(u'#'))
            hex.remove(0, 1);
        bool ok = false;
        const uint rgb = hex.toUInt(&ok, 16);
        if (!ok || hex.size() != 6)
            return false;
        m_data[base + d->off[0]] = static_cast<char>((rgb >> 16) & 0xFF);
        m_data[base + d->off[1]] = static_cast<char>((rgb >> 8) & 0xFF);
        m_data[base + d->off[2]] = static_cast<char>(rgb & 0xFF);
    } else {
        writeNum(base + d->off[0], d->size, d->big, std::clamp<qint64>(static_cast<qint64>(value.toDouble()), 0, d->max > 0 ? d->max : 255));
    }
    if (m_data.mid(base, kJerseyStride) == before)
        return false;
    edited();
    return true;
}

int Rll4CareerEditorManager::addJersey(int team, int from) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return -1;
    const int count = static_cast<quint8>(m_data.at(at + kJerseyAmount[m_variant]));
    if (count >= kMaxJerseys) {
        setStatus(false, u"This team already has %1 jerseys, the most it can have."_s.arg(kMaxJerseys));
        return -1;
    }
    if (from < 0 || from >= count)
        from = 0;
    m_data.replace(jerseyAt(team, count), kJerseyStride, m_data.mid(jerseyAt(team, from), kJerseyStride));
    writeText(jerseyAt(team, count), 25, u"JERSEY %1"_s.arg(count + 1));
    m_data[at + kJerseyAmount[m_variant]] = static_cast<char>(count + 1);
    edited();
    setStatus(true, u"Added a jersey (a copy of jersey %1)."_s.arg(from + 1));
    return count;
}

bool Rll4CareerEditorManager::removeJersey(int team, int uniform) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    const int count = static_cast<quint8>(m_data.at(at + kJerseyAmount[m_variant]));
    if (count <= 1 || uniform < 0 || uniform >= count) {
        setStatus(false, u"A team needs at least one jersey."_s);
        return false;
    }
    for (int u = uniform; u + 1 < count; ++u)
        m_data.replace(jerseyAt(team, u), kJerseyStride, m_data.mid(jerseyAt(team, u + 1), kJerseyStride));
    m_data[at + kJerseyAmount[m_variant]] = static_cast<char>(count - 1);
    edited();
    return true;
}

// =================================================================================================================
// Records: add, delete, import, export
// =================================================================================================================

int Rll4CareerEditorManager::addRecord(const QString& kind, bool female) {
    Q_UNUSED(kind)
    Q_UNUSED(female)
    setStatus(false, u"A career save has a fixed set of teams and players: import over one instead."_s);
    return -1;
}

bool Rll4CareerEditorManager::deleteRecord(const QString& kind, int index) {
    Q_UNUSED(kind)
    Q_UNUSED(index)
    setStatus(false, u"A career save has a fixed set of teams and players."_s);
    return false;
}

QString Rll4CareerEditorManager::importRecord(const QString& kind, int index, const QString& pathOrUrl) {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Pick a %1 first."_s.arg(kind);
    QFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::ReadOnly))
        return u"Couldn't open %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    QByteArray record = f.readAll();
    const int size = recordSize(kind);
    const bool team = kind == u"team"_s;
    // The Academy editors' files: a team is the career one less its last 6 bytes, a player the career one less its
    // first 91 (the career data). Those are kept from the record being replaced.
    const int academy = team ? size - 6 : size - kPlayerId;
    if (record.size() == academy)
        record = team ? record + m_data.mid(at + academy, 6) : m_data.mid(at, kPlayerId) + record;
    if (record.size() != size)
        return u"%1 is %2 bytes: not a Rugby League Live 4 %3 (%4 bytes, or %5 from the Academy editor)."_s
            .arg(QFileInfo(f).fileName()).arg(record.size()).arg(kind).arg(size).arg(academy);
    const int idAt = team ? 0 : kPlayerId;
    const qint64 id = readNum(at + idAt, 4, true, true);
    m_data.replace(at, size, record);
    writeNum(at + idAt, 4, true, id);
    m_logoUrls.clear();
    edited();
    setStatus(true, u"Imported %1."_s.arg(QFileInfo(f).fileName()));
    return {};
}

QString Rll4CareerEditorManager::exportRecord(const QString& kind, int index, const QString& pathOrUrl) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Nothing to export."_s;
    // The Academy format (what the Academy editors read).
    const QByteArray record = kind == u"team"_s ? m_data.mid(at, recordSize(kind) - 6)
                                                : m_data.mid(at + kPlayerId, recordSize(kind) - kPlayerId);
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(record) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QString Rll4CareerEditorManager::recordFileName(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return {};
    if (kind == u"team"_s) {
        const FieldDef* d = defFor(kind, u"fullName"_s);
        return fileSafe(readText(at + (m_variant ? d->offDemo[0] : d->off[0]), d->size)) + u".tem"_s;
    }
    const QVariantMap info = playerInfo(static_cast<int>(readNum(at + kPlayerId, 4, true, true)));
    return fileSafe(info.value(u"name"_s).toString()) + u".ply"_s;
}

QString Rll4CareerEditorManager::exportDecoded(const QString& pathOrUrl) const {
    if (!m_loaded)
        return u"Nothing loaded."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

// =================================================================================================================
// Raw
// =================================================================================================================

QVariantList Rll4CareerEditorManager::rawRows(const QString& kind, int index) const {
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
    for (const auto& dList = defsFor(kind); const FieldDef* d : dList) {
        const int* o = m_variant ? d->offDemo : d->off;
        if (d->type == Type::Rgb)
            for (int c = 0; c < 3; ++c)
                mark(o[c], 1);
        else if (d->type != Type::Virtual)
            mark(o[0], d->size);
    }
    if (kind == u"player"_s)
        mark(kPlayerId, 8);
    if (kind == u"team"_s) {
        for (int u = 0; u < kMaxJerseys; ++u) {
            const int base = kJerseyBase[m_variant] + u * kJerseyStride;
            for (const auto& dList = jerseyDefs(); const FieldDef* d : dList) {
                if (d->type == Type::Rgb)
                    for (int c = 0; c < 3; ++c)
                        mark(base + d->off[c], 1);
                else if (d->type != Type::Virtual)
                    mark(base + d->off[0], d->size);
            }
            mark(base + 313, 1);
            mark(base + 333, 2);
        }
        mark(kLogoCustomId[m_variant], 5);
        mark(kJerseyAmount[m_variant], 1);
        mark(kPlayerAmount[m_variant], 2 + 4 * rosterMax());
        mark(kLineupAmount[m_variant], 2 + 4 * 17);
        mark(kRolesAmount[m_variant], 2 + 4 * 4);
        mark(kNinesAmount[m_variant], 2 + 4 * 17);
        mark(kNinesRolesAmount[m_variant], 2 + 4 * 4);
        for (int s = 0; s < 3; ++s)
            mark(kStadium0[m_variant] + s * kStadiumStride, kStadiumStride);
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

bool Rll4CareerEditorManager::setRawByte(const QString& kind, int index, int offset, int value) {
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

// The C# editor's competition ladders: league, its season's fixture list (stage) and how many make the finals.
struct LadderComp {
    int league;
    int stage;
    int top;
};
constexpr LadderComp kLadders[] = {{0, 1, 8}, {5, 8, 8}, {6, 11, 5}, {7, 14, 8}, {8, 17, 4}, {9, 21, 4}, {10, 25, 5}};

// The C# editor's fixture lists (Fixture Results.cs): league, name suffix, games, rounds, where. Queensland Cup's
// trials and season are smaller when it's the "Elite" one (eliteGames / eliteRounds).
struct Stage {
    int league;
    const char* suffix;
    int games;
    int rounds;
    qsizetype at;
    int eliteGames;
    int eliteRounds;
};
constexpr Stage kStages[] = {
    {0, " Trials", 16, 2, 34196, 0, 0}, {0, "", 192, 26, 84182, 0, 0}, {0, " Finals", 9, 4, 617366, 0, 0},
    {1, "", 1, 1, 642359, 0, 0}, {2, "", 1, 1, 650690, 0, 0}, {3, "", 1, 1, 659021, 0, 0}, {4, "", 3, 3, 667352, 0, 0},
    {5, " Trials", 16, 2, 675683, 0, 0}, {5, "", 192, 26, 725669, 0, 0}, {5, " Finals", 9, 4, 1258853, 0, 0},
    {6, " Trials", 14, 2, 1283846, 5, 1}, {6, "", 161, 25, 1342163, 90, 18}, {6, " Finals", 6, 4, 1875347, 0, 0},
    {7, " Trials", 12, 2, 1953103, 0, 0}, {7, "", 132, 25, 2003089, 0, 0}, {7, " Finals", 9, 4, 2369653, 0, 0},
    {8, " Trials", 12, 2, 2394646, 0, 0}, {8, "", 138, 23, 2444632, 0, 0}, {8, " Super 8s", 28, 7, 2827858, 0, 0},
    {8, " Play-Offs", 3, 2, 2905614, 0, 0}, {9, " Trials", 6, 1, 2913945, 0, 0}, {9, "", 138, 23, 2963931, 0, 0},
    {9, " Champ Shield", 28, 7, 3347157, 0, 0}, {9, " Play-Offs", 3, 2, 3424913, 0, 0},
    {10, " Trials", 16, 2, 3433244, 0, 0}, {10, "", 120, 15, 3491561, 0, 0}, {10, " Promo Match", 1, 1, 3919219, 0, 0},
    {10, " Promo Play-Offs", 3, 2, 3996975, 0, 0}, {11, "", 28, 7, 4005306, 0, 0}, {11, " Final", 1, 1, 4083062, 0, 0},
    {13, " Rangitoto", 6, 3, 4085839, 0, 0}, {13, " Waiheke", 6, 3, 4102501, 0, 0}, {13, " Hunua", 6, 3, 4119163, 0, 0},
    {13, " Piha", 6, 3, 4135825, 0, 0}, {13, " Finals", 7, 3, 4152487, 0, 0}, {12, "", 6, 3, 4171926, 0, 0},
    {12, " Final", 1, 1, 4188588, 0, 0}, {14, "", 10, 5, 4191365, 0, 0}, {14, " Final", 1, 1, 4219135, 0, 0},
    {15, "", 6, 3, 4221912, 0, 0}, {15, " Final", 1, 1, 4238574, 0, 0}, {16, "", 3, 1, 4241351, 0, 0},
    {17, "", 21, 3, 4249682, 0, 0}, {17, " Finals", 7, 5, 4346877, 0, 0}, {18, "", 24, 3, 4366316, 0, 0},
    {18, " Finals", 7, 5, 4432964, 0, 0}, {19, "", 15, 5, 4452403, 0, 0}, {20, " - Round 4", 12, 1, 4510720, 0, 0},
    {20, " - Round 5", 8, 1, 4544044, 0, 0}, {20, " - Round 6", 8, 1, 4566260, 0, 0}, {20, " Finals", 7, 5, 4588476, 0, 0}};
constexpr int kStageCount = sizeof(kStages) / sizeof(kStages[0]);

// Each fixture list's competition logo (the C# editor's CompetitionLogos; ui/logos_competition in the blobset).
const char* const kStageLogos[kStageCount] = {
    "nrl", "nrl", "nrl", "allstarsmatch", "anzactest", "cityvscountry", "stateoforigin", "holdencup", "holdencup",
    "holdencup", "qldcup", "qldcup", "qldcup", "nswcup", "nswcup", "nswcup", "superleague", "superleague",
    "superleague", "superleague", "kingstonec", "kingstonec", "kingstonec", "kingstonec", "kingstonel1",
    "kingstonel1", "kingstonel1", "kingstonel1", "superleague", "superleague", "aucklandnines", "aucklandnines",
    "aucklandnines", "aucklandnines", "aucklandnines", "fournations", "fournations", "pacificcup", "pacificcup",
    "europeancup", "europeancup", "worldclub", "worldcup", "worldcup", "emergee", "emergee", "emergerotw",
    "challengecup", "challengecup", "challengecup", "challengecup"};

// A fixture's player stats (after i32 id, u8 position: 31 i16) and team stats (after u8 captain: 11 i16).
const char* const kMatchPlayerStats[] = {
    "ballStrips", "bonusTries", "conversions", "conversionAttempts", "fends", "fieldGoalsAttempts", "fieldGoals",
    "fortyTwenties", "handlingErrors", "hitups", "interceptedPasses", "interceptions", "kickMetresGained", "kicksInPlay",
    "knockOns", "lineBreakAssists", "lineBreaks", "allRunMetres", "minutesPlayed", "missedTackles", "offloads",
    "oneOnOneTackles", "passes", "penaltiesConceded", "penaltyGoals", "penaltyGoalAttempts", "allRuns", "tackleBreaks",
    "tackles", "tries", "tryAssists"};
const char* const kMatchTeamStats[] = {"completionAttempts", "errors", "fieldgoal", "goals", "lineBreaks", "tackles",
                                       "missTackles", "timeInPossession", "tries", "score", "completions"};

enum class CType { U8, Bool, I32 };

// The career settings / selection: where, what, and the drop down list.
struct CareerDef {
    const char* key;
    const char* label;
    const char* group;
    CType type;
    qsizetype at;
    const char* list;
    int page;   // 0 settings, 1 selection
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
    {"international", "International", "Representative Teams", CType::I32, kRepTeamsAt, "@repInternational", 1},
    {"stateOfOrigin", "State Of Origin", "Representative Teams", CType::I32, kRepTeamsAt + 4, "@repOrigin", 1},
    {"cityVsCountry", "City vs Country", "Representative Teams", CType::I32, kRepTeamsAt + 8, "@repCityCountry", 1},
    {"allStars", "All Stars", "Representative Teams", CType::I32, kRepTeamsAt + 12, "@repAllStars", 1}};

QVariantList valueChoices(const QList<int>& values, const QString& unit) {
    QVariantList out;
    for (int v : std::as_const(values))
        out << QVariantMap{{u"name"_s, u"%1 %2"_s.arg(v).arg(unit)}, {u"value"_s, v}};
    return out;
}

} // namespace

void Rll4CareerEditorManager::loadCareer() {
    // Metadata.SAV (next to the save): i32 version, i32 hash, i32, then three (i32 size, text): "CAREER DATA", the
    // club, the career's state ("2017 NATIONAL RUGBY LEAGUE\nSEASON, ROUND 13\n...").
    m_careerInfo.clear();
    m_careerYear = 2017;
    QFile meta(QFileInfo(m_path).absoluteDir().filePath(u"Metadata.SAV"_s));
    if (meta.open(QIODevice::ReadOnly)) {
        const QByteArray b = meta.readAll();
        qsizetype p = 12;
        QStringList parts;
        for (int i = 0; i < 3 && p + 4 <= b.size(); ++i) {
            const int n = qFromLittleEndian<qint32>(b.constData() + p);
            p += 4;
            if (n < 0 || p + n > b.size())
                break;
            parts << QString::fromLatin1(b.mid(p, n));
            p += n;
        }
        if (parts.size() == 3) {
            QString info = parts.at(2).simplified();
            m_careerInfo = parts.at(1).trimmed() + u" - "_s + info;
            bool ok = false;
            const int year = QStringView(info).left(4).toInt(&ok);
            if (ok && year > 1900)
                m_careerYear = year;
        }
    }
    // The competitions (the C# editor's 21): name[40], short name[9], i32 count, i32 team ids.
    m_leagues.clear();
    for (const qsizetype at : std::as_const(kLeagueAt)) {
        League l;
        l.name = readText(at, 40);
        l.shortName = readText(at + 40, 9);
        const int count = static_cast<int>(readNum(at + 49, 4, false, true));
        for (int i = 0; i < count && i < 64; ++i)
            l.teams << static_cast<int>(readNum(at + 53 + 4 * i, 4, false, true));
        m_leagues << l;
    }
}

int Rll4CareerEditorManager::teamIndexById(int id) const {
    const int count = teamCount();
    if (id >= 1 && id <= count && readNum(recordAt(u"team"_s, id - 1), 4, true, true) == id)
        return id - 1;
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"team"_s, i), 4, true, true) == id)
            return i;
    return -1;
}

QString Rll4CareerEditorManager::teamNameById(int id) const {
    const int t = teamIndexById(id);
    if (t < 0)
        return id <= 0 || id == 65535 ? QString() : u"(team %1)"_s.arg(id);
    const FieldDef* d = defFor(u"team"_s, u"fullName"_s);
    return readText(recordAt(u"team"_s, t) + d->off[0], d->size);
}

QVariantList Rll4CareerEditorManager::teamChoices(const QList<int>& leagues, bool none) const {
    QVariantList out;
    for (int l : std::as_const(leagues)) {
        if (l < 0 || l >= m_leagues.size())
            continue;
        for (const auto& idList = m_leagues.at(l).teams; int id : idList) {
            QString name = teamNameById(id);
            if (leagues.size() > 1)
                name += u"  ("_s + titleCase(m_leagues.at(l).name) + u")"_s;
            out << QVariantMap{{u"name"_s, name}, {u"value"_s, id}};
        }
    }
    if (none)
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 65535}};
    return out;
}

QVariantList Rll4CareerEditorManager::careerItems() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const bool player = m_data.at(kCareerModeAt + 1) != 0;
    const int team = static_cast<int>(readNum(kCareerTeamAt, 4, true, true));
    const int statuses = static_cast<int>(readNum(kStatusAt, 2, true, false));
    const int contracts = static_cast<int>(readNum(kContractsAt, 2, true, false));
    const int retired = static_cast<int>(readNum(kRetirementAt + 4, 4, true, true));
    const auto item = [](int i, const QString& key, const QString& title, const QString& subtitle, const QStringList& tabs) {
        return QVariantMap{{u"index"_s, i}, {u"key"_s, key}, {u"title"_s, title}, {u"subtitle"_s, subtitle}, {u"tabs"_s, tabs}};
    };
    out << item(0, u"settings"_s, u"Career Settings"_s,
                (player ? u"Player career"_s : u"Coach career"_s) + u"  ·  %1 season"_s.arg(m_careerYear), {u"Settings"_s});
    out << item(1, u"selection"_s, u"Team & Player Selection"_s, teamNameById(team), {u"Selection"_s});
    out << item(2, u"status"_s, u"Player Status"_s, u"%1 injured, suspended or away"_s.arg(statuses), {u"Player Status"_s});
    out << item(3, u"contracts"_s, u"Player Contracts"_s, u"%1 contract changes"_s.arg(contracts), {u"Contracts"_s});
    out << item(4, u"retirements"_s, u"Retirements"_s, u"%1 players"_s.arg(retired), {u"Retirements"_s});
    out << item(5, u"competitions"_s, u"Competitions"_s, u"Ladders and fixtures"_s, {u"Ladder"_s, u"Fixtures"_s});
    return out;
}

QVariantList Rll4CareerEditorManager::careerFields(int page) const {
    QVariantList out;
    if (!m_loaded)
        return out;
    for (const CareerDef& d : std::as_const(kCareerDefs)) {
        if (d.page != page)
            continue;
        QVariantMap m;
        m[u"key"_s] = QString::fromLatin1(d.key);
        m[u"label"_s] = QString::fromLatin1(d.label);
        m[u"group"_s] = QString::fromLatin1(d.group);
        m[u"readOnly"_s] = false;
        m[u"slider"_s] = false;
        m[u"offset"_s] = u"0x"_s + QString::number(d.at, 16).toUpper();
        m[u"min"_s] = 0;
        int value = 0;
        switch (d.type) {
        case CType::Bool:
            m[u"kind"_s] = u"bool"_s;
            m[u"value"_s] = m_data.at(d.at) != 0;
            break;
        case CType::U8:
            value = static_cast<quint8>(m_data.at(d.at));
            m[u"kind"_s] = u"number"_s;
            m[u"value"_s] = value;
            m[u"max"_s] = 255;
            break;
        case CType::I32:
            value = static_cast<int>(readNum(d.at, 4, true, true));
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
                choices = teamChoices({0, 5, 6, 7, 8, 9, 10}, false);
            else if (list == u"@repInternational"_s)
                choices = teamChoices({12, 14, 15, 17, 18, 19}, true);
            else if (list == u"@repOrigin"_s)
                choices = teamChoices({4}, true);
            else if (list == u"@repCityCountry"_s)
                choices = teamChoices({3}, true);
            else if (list == u"@repAllStars"_s)
                choices = teamChoices({1}, true);
            else if (list == u"@careerPlayer"_s) {
                // The career team's players (the C# editor's list), and whoever it is now.
                QList<int> ids = rosterIds(teamIndexById(static_cast<int>(readNum(kCareerTeamAt, 4, true, true))));
                if (!ids.contains(value))
                    ids.prepend(value);
                for (int id : std::as_const(ids)) {
                    const QVariantMap info = playerInfo(id);
                    choices << QVariantMap{{u"name"_s, id == 0 ? u"None"_s : u"%1  (%2)"_s.arg(info.value(u"name"_s).toString(),
                                                                                               info.value(u"role"_s).toString())},
                                           {u"value"_s, id}};
                }
            }
            if (std::none_of(choices.cbegin(), choices.cend(), [value](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == value; }))
                choices.prepend(QVariantMap{{u"name"_s, value == 0 ? u"None"_s : u"(%1)"_s.arg(value)}, {u"value"_s, value}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = choices;
            m[u"value"_s] = value;
        }
        out << m;
    }
    return out;
}

bool Rll4CareerEditorManager::setCareerField(int page, const QString& key, const QVariant& value) {
    for (const CareerDef& d : std::as_const(kCareerDefs)) {
        if (d.page != page || key != QLatin1StringView(d.key))
            continue;
        const QByteArray before = m_data.mid(d.at, 4);
        switch (d.type) {
        case CType::Bool: m_data[d.at] = value.toBool() ? 1 : 0; break;
        case CType::U8: m_data[d.at] = static_cast<char>(std::clamp(value.toInt(), 0, 255)); break;
        case CType::I32: writeNum(d.at, 4, true, static_cast<qint64>(value.toDouble())); break;
        }
        if (m_data.mid(d.at, 4) == before)
            return false;
        edited();
        return true;
    }
    return false;
}

// --- Player status --------------------------------------------------------------------------------------------------

int Rll4CareerEditorManager::statusIndexOf(int playerId) const {
    const int count = qMin(kStatusMax, static_cast<int>(readNum(kStatusAt, 2, true, false)));
    for (int i = 0; i < count; ++i)
        if (readNum(kStatusAt + 2 + qsizetype(i) * kStatusSize, 4, true, true) == playerId)
            return i;
    return -1;
}

// The C# editor's Player Editor: an entry per player who isn't available; "a" is the injury's severity (the editor
// writes 3), the team for a suspension, 96 / 95 for the NSW / QLD Origin camps; "b" the injury.
bool Rll4CareerEditorManager::setPlayerStatus(int playerId, const QString& key, int value) {
    if (playerId <= 0)
        return false;
    int s = statusIndexOf(playerId);
    const auto entry = [](int i) { return kStatusAt + 2 + qsizetype(i) * kStatusSize; };
    if (key == u"@status"_s) {
        if (value < 0 || value > 5)
            return false;
        if (value == 0)
            return s >= 0 && removeStatus(s);
        const int count = static_cast<int>(readNum(kStatusAt, 2, true, false));
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
            writeNum(kStatusAt, 2, true, count + 1);
        }
        const qsizetype e = entry(s);
        m_data[e + 4] = static_cast<char>(value);
        int a = 0;
        if (value == 1) {
            a = was == 1 ? static_cast<int>(readNum(e + 7, 2, true, false)) : 3;
        } else if (value == 2) {
            const int count2 = teamCount();
            for (int t = 0; t < count2 && a == 0; ++t)
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

QVariantList Rll4CareerEditorManager::statusRows() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    static const QStringList reasons = {u"Available"_s, u"Injured"_s, u"Suspended"_s, u"Retired"_s, u"NSW Origin Camp"_s,
                                        u"QLD Origin Camp"_s};
    static const QStringList severity = {u""_s, u"Light"_s, u"Minor"_s, u"Medium"_s, u"Large"_s, u"Serious"_s};
    const QVariantList injuries = choicesFor(u"player"_s, 0, u"@injuries"_s, 0);
    const int count = qMin(kStatusMax, static_cast<int>(readNum(kStatusAt, 2, true, false)));
    for (int i = 0; i < count; ++i) {
        const qsizetype e = kStatusAt + 2 + qsizetype(i) * kStatusSize;
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

bool Rll4CareerEditorManager::removeStatus(int index) {
    const int count = qMin(kStatusMax, static_cast<int>(readNum(kStatusAt, 2, true, false)));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = kStatusAt + 2 + qsizetype(index) * kStatusSize;
    const qsizetype end = kStatusAt + 2 + qsizetype(count) * kStatusSize;
    m_data.replace(e, end - e - kStatusSize, m_data.mid(e + kStatusSize, end - e - kStatusSize));
    m_data.replace(end - kStatusSize, kStatusSize, QByteArray(kStatusSize, '\0'));
    writeNum(kStatusAt, 2, true, count - 1);
    edited();
    return true;
}

// --- Contracts ------------------------------------------------------------------------------------------------------

QVariantList Rll4CareerEditorManager::contractRows() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const int count = qMin(2048, static_cast<int>(readNum(kContractsAt, 2, true, false)));
    for (int i = 0; i < count; ++i) {
        const qsizetype e = kContractsAt + 2 + qsizetype(i) * kContractSize;
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

QVariantList Rll4CareerEditorManager::contractFields(int index) const {
    QVariantList out;
    const int count = static_cast<int>(readNum(kContractsAt, 2, true, false));
    if (!m_loaded || index < 0 || index >= count)
        return out;
    const qsizetype e = kContractsAt + 2 + qsizetype(index) * kContractSize;
    for (const ContractDef& d : std::as_const(kContractDefs)) {
        const int value = static_cast<int>(readNum(e + d.off, d.size, true, d.size == 4));
        QVariantMap m{{u"key"_s, QString::fromLatin1(d.key)}, {u"label"_s, QString::fromLatin1(d.label)}, {u"group"_s, u"Contract"_s},
                      {u"readOnly"_s, false}, {u"slider"_s, false}, {u"min"_s, 0},
                      {u"offset"_s, u"0x"_s + QString::number(e + d.off, 16).toUpper()}};
        if (d.team) {
            QVariantList choices = teamChoices({0, 5, 6, 7, 8, 9, 10}, false);
            if (std::none_of(choices.cbegin(), choices.cend(), [value](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == value; }))
                choices.prepend(QVariantMap{{u"name"_s, value == 0 ? u"None"_s : teamNameById(value)}, {u"value"_s, value}});
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

bool Rll4CareerEditorManager::setContractField(int index, const QString& key, const QVariant& value) {
    const int count = static_cast<int>(readNum(kContractsAt, 2, true, false));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = kContractsAt + 2 + qsizetype(index) * kContractSize;
    for (const ContractDef& d : std::as_const(kContractDefs)) {
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

bool Rll4CareerEditorManager::removeContract(int index) {
    const int count = static_cast<int>(readNum(kContractsAt, 2, true, false));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = kContractsAt + 2 + qsizetype(index) * kContractSize;
    const qsizetype end = kContractsAt + 2 + qsizetype(count) * kContractSize;
    m_data.replace(e, end - e - kContractSize, m_data.mid(e + kContractSize, end - e - kContractSize));
    m_data.replace(end - kContractSize, kContractSize, QByteArray(kContractSize, '\0'));
    writeNum(kContractsAt, 2, true, count - 1);
    edited();
    return true;
}

// --- Retirements ----------------------------------------------------------------------------------------------------

QVariantMap Rll4CareerEditorManager::retirementDate() const {
    if (!m_loaded)
        return {};
    return {{u"year"_s, static_cast<int>(readNum(kRetirementAt, 2, true, true))},
            {u"month"_s, static_cast<int>(static_cast<quint8>(m_data.at(kRetirementAt + 2)))},
            {u"day"_s, static_cast<int>(static_cast<quint8>(m_data.at(kRetirementAt + 3)))}};
}

QVariantList Rll4CareerEditorManager::retirementRows() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const int count = qBound(0, static_cast<int>(readNum(kRetirementAt + 4, 4, true, true)), 1024);
    for (int i = 0; i < count; ++i) {
        const qsizetype e = kRetirementAt + 8 + qsizetype(i) * 5;
        const int id = static_cast<int>(readNum(e, 4, true, true));
        const QVariantMap info = playerInfo(id);
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"name"_s, info.value(u"name"_s)}, {u"role"_s, info.value(u"role"_s)},
                           {u"rating"_s, info.value(u"rating"_s)}, {u"age"_s, info.value(u"age"_s)},
                           {u"reason"_s, static_cast<int>(static_cast<quint8>(m_data.at(e + 4)))},
                           {u"playerIndex"_s, info.value(u"playerIndex"_s, -1)}};
    }
    return out;
}

bool Rll4CareerEditorManager::setRetirementReason(int index, int reason) {
    const int count = static_cast<int>(readNum(kRetirementAt + 4, 4, true, true));
    if (index < 0 || index >= count || reason < 0 || reason > 255)
        return false;
    const qsizetype e = kRetirementAt + 8 + qsizetype(index) * 5 + 4;
    if (static_cast<quint8>(m_data.at(e)) == reason)
        return false;
    m_data[e] = static_cast<char>(reason);
    edited();
    return true;
}

bool Rll4CareerEditorManager::removeRetirement(int index) {
    const int count = static_cast<int>(readNum(kRetirementAt + 4, 4, true, true));
    if (index < 0 || index >= count)
        return false;
    const qsizetype e = kRetirementAt + 8 + qsizetype(index) * 5;
    const qsizetype end = kRetirementAt + 8 + qsizetype(count) * 5;
    m_data.replace(e, end - e - 5, m_data.mid(e + 5, end - e - 5));
    m_data.replace(end - 5, 5, QByteArray(5, '\0'));
    writeNum(kRetirementAt + 4, 4, true, count - 1);
    edited();
    return true;
}

// --- Competitions ---------------------------------------------------------------------------------------------------

// A fixture list (the C# editor's ReadFixtureStats): per match u8 round, u16 match, u16 pool, u16 home, u16 away,
// u8 stadium, u8 event, bool played, i16 year, u8 month, day, hour, minute; the two lineups (u16 count, i32 ids);
// 3 bytes; per team the player stats (u16 count, 67 bytes each) and the team stats (23 bytes, the score at +19);
// 288 bytes. Every match has a fixed 2777 byte slot (room for 17 players a side) whatever its counts: a match not
// played yet (or one in progress: lineups but no stats) leaves most of it empty, so the walk goes slot by slot.
constexpr qsizetype kMatchSlot = 2777;
QList<Rll4CareerEditorManager::Match> Rll4CareerEditorManager::readStage(int stage) const {
    QList<Match> out;
    if (!m_loaded || stage < 0 || stage >= kStageCount)
        return out;
    const Stage& st = kStages[stage];
    const bool elite = st.eliteGames > 0 && m_leagues.value(st.league).name.contains(u"ELITE"_s, Qt::CaseInsensitive);
    const int games = elite ? st.eliteGames : st.games;
    const auto u16 = [this](qsizetype at) { return static_cast<int>(readNum(at, 2, true, false)); };
    for (int i = 0; i < games; ++i) {
        qsizetype p = st.at + qsizetype(i) * kMatchSlot;
        if (p + kMatchSlot > m_data.size())
            break;
        Match m;
        m.round = static_cast<quint8>(m_data.at(p));
        m.home = u16(p + 5);
        m.away = u16(p + 7);
        m.stadium = static_cast<quint8>(m_data.at(p + 9));
        m.played = m_data.at(p + 11) != 0;
        m.year = static_cast<int>(readNum(p + 12, 2, true, true));
        m.month = static_cast<quint8>(m_data.at(p + 14));
        m.day = static_cast<quint8>(m_data.at(p + 15));
        m.hour = static_cast<quint8>(m_data.at(p + 16));
        m.minute = static_cast<quint8>(m_data.at(p + 17));
        p += 18;
        p += 2 + 4 * qsizetype(u16(p));     // Home lineup.
        p += 2 + 4 * qsizetype(u16(p));     // Away lineup.
        p += 3;
        p += 2 + 67 * qsizetype(u16(p));    // Home player stats.
        m.homeScore = static_cast<int>(readNum(p + 19, 2, true, true));
        p += 23;
        p += 2 + 67 * qsizetype(u16(p));    // Away player stats.
        m.awayScore = static_cast<int>(readNum(p + 19, 2, true, true));
        if (!m.played)
            m.homeScore = m.awayScore = 0;
        out << m;
    }
    return out;
}

QVariantList Rll4CareerEditorManager::ladderComps() const {
    QVariantList out;
    for (int i = 0; i < int(sizeof(kLadders) / sizeof(kLadders[0])); ++i)
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, titleCase(m_leagues.value(kLadders[i].league).name)},
                           {u"logoUrl"_s, competitionLogoUrl(QString::fromLatin1(kStageLogos[kLadders[i].stage]))}};
    return out;
}

// The C# editor's Competition Ladder: the season's results up to the first match not played yet; a round a team
// didn't play (before then) is a bye, worth 2 points.
QVariantList Rll4CareerEditorManager::ladderRows(int comp) const {
    QVariantList out;
    if (!m_loaded || comp < 0 || comp >= int(sizeof(kLadders) / sizeof(kLadders[0])))
        return out;
    const LadderComp& lc = kLadders[comp];
    const QList<int> teams = m_leagues.value(lc.league).teams;
    const Stage& st = kStages[lc.stage];
    const bool elite = st.eliteGames > 0 && m_leagues.value(st.league).name.contains(u"ELITE"_s, Qt::CaseInsensitive);
    const int rounds = elite ? st.eliteRounds : st.rounds;
    struct Row {
        int id = 0, played = 0, points = 0, wins = 0, draws = 0, lost = 0, byes = 0, pf = 0, pa = 0, hw = 0, hl = 0, aw = 0, al = 0;
    };
    QList<Row> rows(teams.size());
    for (int i = 0; i < teams.size(); ++i)
        rows[i].id = teams.at(i);
    QList<QList<bool>> playing(rounds, QList<bool>(teams.size(), false));
    int stopRound = rounds;
    for (const auto& mList = readStage(lc.stage); const Match& m : mList) {
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
                           {u"logoUrl"_s, t >= 0 ? teamLogoUrl(t) : QString()},
                           {u"abbreviation"_s, all.value(t).toMap().value(u"abbreviation"_s)},
                           {u"colour"_s, all.value(t).toMap().value(u"primaryColour"_s)},
                           {u"played"_s, r.played}, {u"points"_s, r.points}, {u"wins"_s, r.wins}, {u"draws"_s, r.draws},
                           {u"lost"_s, r.lost}, {u"byes"_s, r.byes}, {u"for"_s, r.pf}, {u"against"_s, r.pa},
                           {u"diff"_s, r.pf - r.pa}, {u"home"_s, u"%1-%2"_s.arg(r.hw).arg(r.hl)},
                           {u"away"_s, u"%1-%2"_s.arg(r.aw).arg(r.al)}, {u"finals"_s, i < lc.top}};
    }
    return out;
}

QVariantList Rll4CareerEditorManager::fixtureStages() const {
    QVariantList out;
    for (int i = 0; i < kStageCount; ++i)
        out << QVariantMap{{u"index"_s, i},
                           {u"name"_s, titleCase(m_leagues.value(kStages[i].league).name) + QString::fromLatin1(kStages[i].suffix)},
                           {u"logoUrl"_s, competitionLogoUrl(QString::fromLatin1(kStageLogos[i]))}};
    return out;
}

QStringList Rll4CareerEditorManager::fixtureRounds(int stage) const {
    if (stage < 0 || stage >= kStageCount)
        return {};
    switch (stage) {
    case 2: case 9: case 12: case 15: return {u"Week 1"_s, u"Semi Finals"_s, u"Preliminary Final"_s, u"Grand Final"_s};
    case 19: case 23: case 27: return {u"Semi Finals"_s, u"Final"_s};
    case 29: return {u"Qualifier Match"_s};
    case 34: case 43: case 45: case 50: return {u"Quarter Finals"_s, u"Semi Finals"_s, u"Final"_s};
    case 36: case 38: case 40: return {u"Final"_s};
    default: break;
    }
    const Stage& st = kStages[stage];
    const bool elite = st.eliteGames > 0 && m_leagues.value(st.league).name.contains(u"ELITE"_s, Qt::CaseInsensitive);
    QStringList out;
    for (int r = 0; r < (elite ? st.eliteRounds : st.rounds); ++r)
        out << u"Round %1"_s.arg(r + 1);
    return out;
}

QVariantList Rll4CareerEditorManager::fixtureRows(int stage, int round) const {
    QVariantList out;
    const QStringList& stadiums = Rll4SaveLists::stadiums();
    QVariantList all;   // Every team, by index (made when needed).
    const QList<Match> matches = readStage(stage);
    for (int n = 0; n < matches.size(); ++n) {
        const Match& m = matches.at(n);
        if (m.round != round || (m.home == 0 && m.away == 0))
            continue;
        const int h = teamIndexById(m.home), a = teamIndexById(m.away);
        if (all.isEmpty())
            all = teamRows(QString());
        const QVariantMap hr = all.value(h).toMap(), ar = all.value(a).toMap();
        QString stadium = m.stadium >= 5 && m.stadium - 5 < stadiums.size() ? stadiums.at(m.stadium - 5) : u"Custom stadium"_s;
        out << QVariantMap{{u"home"_s, teamNameById(m.home)}, {u"away"_s, teamNameById(m.away)},
                           {u"homeIndex"_s, h}, {u"awayIndex"_s, a},
                           {u"homeLogoUrl"_s, hr.value(u"logoUrl"_s)}, {u"awayLogoUrl"_s, ar.value(u"logoUrl"_s)},
                           {u"homeAbbreviation"_s, hr.value(u"abbreviation"_s)}, {u"awayAbbreviation"_s, ar.value(u"abbreviation"_s)},
                           {u"homeColour"_s, hr.value(u"primaryColour"_s)}, {u"awayColour"_s, ar.value(u"primaryColour"_s)},
                           {u"played"_s, m.played}, {u"homeScore"_s, m.homeScore}, {u"awayScore"_s, m.awayScore},
                           {u"stadium"_s, stadium}, {u"match"_s, n},
                           {u"date"_s, m.year > 0 ? u"%1/%2/%3"_s.arg(m.day, 2, 10, QChar(u'0')).arg(m.month, 2, 10, QChar(u'0')).arg(m.year) : QString()},
                           {u"time"_s, u"%1:%2"_s.arg(m.hour, 2, 10, QChar(u'0')).arg(m.minute, 2, 10, QChar(u'0'))}};
    }
    return out;
}

QString Rll4CareerEditorManager::competitionLogoUrl(const QString& name) const {
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

// The match's full stats (readStage's walk, keeping the players' and teams' stats of the one asked for).
QVariantMap Rll4CareerEditorManager::matchStats(int stage, int match) const {
    if (!m_loaded || stage < 0 || stage >= kStageCount || match < 0)
        return {{u"ok"_s, false}};
    const Stage& st = kStages[stage];
    const bool elite = st.eliteGames > 0 && m_leagues.value(st.league).name.contains(u"ELITE"_s, Qt::CaseInsensitive);
    const int games = elite ? st.eliteGames : st.games;
    if (match >= games)
        return {{u"ok"_s, false}};
    const auto u16 = [this](qsizetype at) { return static_cast<int>(readNum(at, 2, true, false)); };
    for (int i = match; i < games; ++i) {
        qsizetype p = st.at + qsizetype(i) * kMatchSlot;
        if (p + kMatchSlot > m_data.size())
            break;
        const qsizetype start = p;
        p += 18;
        p += 2 + 4 * qsizetype(u16(p));
        p += 2 + 4 * qsizetype(u16(p));
        p += 3;
        if (i < match) {
            p += 2 + 67 * qsizetype(u16(p)) + 23;
            p += 2 + 67 * qsizetype(u16(p)) + 23 + 288;
            continue;
        }
        CareerMatch::Team teams[2];
        teams[0].id = u16(start + 5);
        teams[1].id = u16(start + 7);
        const QVariantList all = teamRows(QString());
        for (CareerMatch::Team& t : teams) {
            const int count = u16(p);
            p += 2;
            for (int k = 0; k < count && p + 67 <= m_data.size(); ++k, p += 67) {
                CareerMatch::Player pl;
                pl.id = static_cast<int>(readNum(p, 4, true, true));
                pl.position = static_cast<quint8>(m_data.at(p + 4));
                for (int s = 0; s < 31; ++s)
                    pl.s.insert(QString::fromLatin1(kMatchPlayerStats[s]), static_cast<int>(readNum(p + 5 + 2 * s, 2, true, true)));
                const QVariantMap info = playerInfo(pl.id);
                pl.name = info.value(u"name"_s).toString();
                pl.role = info.value(u"role"_s).toString();
                pl.playerIndex = info.value(u"playerIndex"_s, -1).toInt();
                t.players << pl;
            }
            for (int s = 0; s < 11; ++s)
                t.t.insert(QString::fromLatin1(kMatchTeamStats[s]), static_cast<int>(readNum(p + 1 + 2 * s, 2, true, true)));
            t.score = t.t.value(u"score"_s);
            p += 23;
            t.teamIndex = teamIndexById(t.id);
            const QVariantMap row = all.value(t.teamIndex).toMap();
            t.name = teamNameById(t.id);
            if (t.teamIndex >= 0) {
                const qsizetype tr = recordAt(u"team"_s, t.teamIndex);
                const FieldDef* loc = defFor(u"team"_s, u"locationName"_s);
                const FieldDef* club = defFor(u"team"_s, u"clubName"_s);
                t.location = readText(tr + loc->off[0], loc->size);
                t.club = readText(tr + club->off[0], club->size);
            }
            t.logoUrl = row.value(u"logoUrl"_s).toString();
            t.abbreviation = row.value(u"abbreviation"_s).toString();
            t.colour = row.value(u"primaryColour"_s).toString();
        }
        const int stadiumId = static_cast<quint8>(m_data.at(start + 9));
        const QStringList& stadiums = Rll4SaveLists::stadiums();
        const QString stadium = stadiumId >= 5 && stadiumId - 5 < stadiums.size() ? stadiums.at(stadiumId - 5) : u"Custom stadium"_s;
        const int year = static_cast<int>(readNum(start + 12, 2, true, true));
        QVariantMap out = CareerMatch::build(
            teams[0], teams[1], stadium,
            u"%1/%2/%3"_s.arg(static_cast<quint8>(m_data.at(start + 15)), 2, 10, QChar(u'0')).arg(static_cast<quint8>(m_data.at(start + 14)), 2, 10, QChar(u'0')).arg(year),
            u"%1:%2"_s.arg(static_cast<quint8>(m_data.at(start + 16)), 2, 10, QChar(u'0')).arg(static_cast<quint8>(m_data.at(start + 17)), 2, 10, QChar(u'0')),
            false);
        out[u"played"_s] = m_data.at(start + 11) != 0;
        out[u"competition"_s] = titleCase(m_leagues.value(st.league).name) + QString::fromLatin1(st.suffix);
        out[u"competitionLogoUrl"_s] = competitionLogoUrl(QString::fromLatin1(kStageLogos[stage]));
        out[u"round"_s] = fixtureRounds(stage).value(static_cast<quint8>(m_data.at(start)));
        return out;
    }
    return {{u"ok"_s, false}};
}
// =================================================================================================================
// Raw Databases (the C# editor's Raw ... windows: a table of every record, edited as text, CSV in / out)
// =================================================================================================================

namespace {

struct RawCol {
    const char* name;
    const char* key;    // A field key (".r" / ".g" / ".b": a colour's part), "#index" or "#<team list>".
};

const QList<RawCol> kRawPlayerCols = {
    {"Player Index", "#index"}, {"Player ID", "id"}, {"Original Player ID", "originalId"},
    {"First Name", "firstName"}, {"Last Name", "lastName"}, {"DOB - Day", "dob.day"}, {"DOB - Month", "dob.month"},
    {"DOB - Year", "dob.year"}, {"Jersey Number", "jerseyNumber"}, {"Jersey Name", "jerseyName"},
    {"Primary Role", "primaryRole"}, {"Secondary Role", "secondaryRole"}, {"Tertiary Role", "tertiaryRole"},
    {"State Of Origin", "stateOfOrigin"}, {"City V's Country", "cityVsCountry"}, {"All Stars", "allStars"},
    {"Preferred Hand", "preferredHand"}, {"Preferred Foot", "preferredFoot"}, {"Representative Country", "repCountry"},
    {"Country Of Birth", "countryOfBirth"}, {"Commentary", "commentarySurname"}, {"Original Contract", "contractStart"},
    {"Contract Expiry", "contractExpiry"}, {"Height", "appearance.height"}, {"Weight", "appearance.weight"},
    {"Reputation", "attributes.reputation"}, {"Ego", "attributes.ego"}, {"Loyalty", "attributes.loyalty"},
    {"Form", "attributes.form"}, {"Perks", "attributes.perk"},
    {"Strength", "technicalAbility.strength"}, {"Agility", "technicalAbility.agility"},
    {"Fitness", "technicalAbility.fitness"}, {"Acceleration", "technicalAbility.acceleration"},
    {"Discipline", "technicalAbility.discipline"}, {"Durability", "technicalAbility.durability"},
    {"Sprint Speed", "technicalAbility.sprintSpeed"}, {"Face ID", "appearance.headMeshId"},
    {"Hair ID", "appearance.hairStyle"}, {"Beard ID", "appearance.beardId"},
};

const QList<RawCol> kRawSkillCols = {
    {"Player Index", "#index"}, {"Player ID", "id"}, {"Original Player ID", "originalId"},
    {"First Name", "firstName"}, {"Last Name", "lastName"},
    {"Grubber Kick", "skills.kickingSkills.grubberKick"}, {"Punt Kick", "skills.kickingSkills.puntKick"},
    {"Chip Kick", "skills.kickingSkills.chipKick"}, {"Bomb Kick", "skills.kickingSkills.bombKick"},
    {"Field Goal Kick", "skills.kickingSkills.fieldgoalKick"}, {"Place Kick", "skills.kickingSkills.placeKick"},
    {"Drop Kick", "skills.kickingSkills.dropKick"}, {"Kick For Touch", "skills.kickingSkills.kickForTouch"},
    {"Basic Pass", "skills.passingSkills.basicPass"}, {"Long Pass", "skills.passingSkills.longPass"},
    {"Offload", "skills.passingSkills.offload"}, {"Dummy Pass", "skills.evasionSkills.dummyPass"},
    {"Fend", "skills.evasionSkills.fend"}, {"Side Step", "skills.evasionSkills.sideStep"},
    {"Break Tackle", "skills.evasionSkills.breakTackle"}, {"Tackle", "skills.tackleSkills.tackle"},
    {"Drive Tackle", "skills.tackleSkills.driveTackle"}, {"Dive Tackle", "skills.tackleSkills.diveTackle"},
    {"Impact Tackle", "skills.tackleSkills.impactTackle"}, {"Contested Collect", "skills.miscSkills.contestedCollect"},
    {"Dive Collect", "skills.miscSkills.diveCollect"}, {"Toe Poke", "skills.miscSkills.toePoke"},
    {"Face ID", "appearance.headMeshId"},
};

const QList<RawCol> kRawTeamCols = {
    {"Team Index", "#index"}, {"Team ID", "id"}, {"Original Team ID", "originalId"}, {"Team Type", "type"},
    {"Team Type Additions", "typeAdditional"}, {"Full Name", "fullName"}, {"Location Name", "locationName"},
    {"Club Name", "clubName"}, {"Abbreviated Name", "abbreviatedName"},
    {"Primary R Colour", "primaryColour.r"}, {"Primary G Colour", "primaryColour.g"}, {"Primary B Colour", "primaryColour.b"},
    {"Secondary R Colour", "secondaryColour.r"}, {"Secondary G Colour", "secondaryColour.g"},
    {"Secondary B Colour", "secondaryColour.b"}, {"Hud Text R Colour", "hudTextColour.r"},
    {"Hud Text G Colour", "hudTextColour.g"}, {"Hud Text B Colour", "hudTextColour.b"},
    {"Supporters", "supporters"}, {"Commentary", "commentary"}, {"Alternate Numbering", "alternateNumbering"},
};

const QStringList kRawLineupNames = {u"1 - Fullback"_s, u"2 - RWing"_s, u"3 - RCentre"_s, u"4 - LCentre"_s,
                                     u"5 - LWing"_s, u"6 - Five Eight"_s, u"7 - Halfback"_s, u" 8 - Prop"_s,
                                     u"9 - Hooker"_s, u"10 - FrontRow"_s, u"11 - Second Row 1"_s,
                                     u"12 - Second Row 2"_s, u"13 - Lock"_s, u"14 - Int1"_s, u"15 - Int2"_s,
                                     u"16 - Int3"_s, u"17 - Int4"_s};
const QStringList kRawNinesNames = {u"1"_s, u"2"_s, u"3"_s, u"4"_s, u"5"_s, u"6"_s, u"7"_s, u"8"_s, u"9"_s,
                                    u"10 - Int1"_s, u"11 - Int2"_s, u"12 - Int3"_s, u"13 - Int4"_s, u"14 - Int5"_s};

constexpr int kRawLeagueTeams = 36;   // The C# editor's columns.

QList<RawColumn> rawColumns(const QList<RawCol>& cols) {
    QList<RawColumn> out;
    for (const RawCol& c : std::as_const(cols))
        out << RawColumn{QString::fromLatin1(c.name), QString::fromLatin1(c.key), c.key[0] == '#'};
    return out;
}

// "primaryColour.g" -> {"primaryColour", 1}; else {key, -1}.
std::pair<QString, int> colourPart(const QString& key) {
    if (key.size() > 2 && key.at(key.size() - 2) == u'.') {
        const int part = QStringLiteral("rgb").indexOf(key.back());
        if (part >= 0)
            return {key.left(key.size() - 2), part};
    }
    return {key, -1};
}

} // namespace

QVariantList Rll4CareerEditorManager::rawTables() const {
    const auto t = [](const char* id, const char* title) {
        return QVariantMap{{u"id"_s, QString::fromLatin1(id)}, {u"title"_s, QString::fromLatin1(title)}, {u"repair"_s, false}};
    };
    return {t("leagues", "Raw League Database"), t("players", "Raw Player Database"), t("skills", "Raw Player Skills"),
            t("status", "Raw Player Status Database"), t("retirements", "Raw Retirement Database"),
            t("teams", "Raw Team Database"), t("lineup", "Raw Team Lineup"), t("nines", "Raw Team Nines Lineup"),
            t("teamPlayers", "Raw Team Players")};
}

namespace {
QList<RawColumn> rll4CareerRawColumns(const QString& id) {
    QList<RawColumn> cols;
    if (id == u"players"_s)
        return rawColumns(kRawPlayerCols);
    if (id == u"skills"_s)
        return rawColumns(kRawSkillCols);
    if (id == u"teams"_s)
        return rawColumns(kRawTeamCols);
    if (id == u"leagues"_s) {
        cols = rawColumns({{"League Index", "#index"}, {"League Name", "#name"}, {"League Short Name", "#shortName"},
                           {"League Team Amount", "#teamAmount"}});
        for (int j = 0; j < kRawLeagueTeams; ++j)
            cols << RawColumn{u"Team %1"_s.arg(j + 1), u"#team%1"_s.arg(j)};
        for (RawColumn& c : cols)
            c.readOnly = c.key == u"#index"_s;
        return cols;
    }
    if (id == u"status"_s) {
        cols = rawColumns({{"Player Index", "#index"}, {"Player ID", "#id"}, {"First Name", "#firstName"},
                           {"Last Name", "#lastName"}, {"Reason", "#reason"}, {"Player Time", "#duration"},
                           {"Condition", "#additionalA"}, {"Type", "#additionalB"}});
        for (RawColumn& c : cols)
            c.readOnly = c.key == u"#index"_s || c.key.endsWith(u"Name"_s);
        return cols;
    }
    if (id == u"retirements"_s) {
        cols = rawColumns({{"Index", "#index"}, {"Player ID", "#id"}, {"First Name", "#firstName"},
                           {"Last Name", "#lastName"}, {"Retirement Reason", "#reason"}});
        for (RawColumn& c : cols)
            c.readOnly = c.key == u"#index"_s || c.key.endsWith(u"Name"_s);
        return cols;
    }
    if (id == u"lineup"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Team ID", "id"}, {"Original Team ID", "originalId"},
                           {"Location Name", "locationName"}, {"Club Name", "clubName"},
                           {"Team Role Amount", "#rolesAmount"}, {"Team Captain ID", "#role0"},
                           {"Team GoalKicker ID", "#role1"}, {"Team PlayMaker1 ID", "#role2"},
                           {"Team PlayMaker2 ID", "#role3"}, {"Team Lineup Amount", "#lineupAmount"}});
        for (int j = 0; j < 17; ++j)
            cols << RawColumn{kRawLineupNames.at(j), u"#lineup%1"_s.arg(j)};
    } else if (id == u"nines"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Team ID", "id"}, {"Original Team ID", "originalId"},
                           {"Full Name", "fullName"}, {"Location Name", "locationName"}, {"Club Name", "clubName"},
                           {"Nines Roles Amount", "#ninesRolesAmount"}, {"Team Nines Captain ID", "#ninesRole0"},
                           {"Team Nines GoalKicker ID", "#ninesRole1"}, {"Team Nines PlayMaker1 ID", "#ninesRole2"},
                           {"Team Nines PlayMaker2 ID", "#ninesRole3"}, {"Nines Lineup Amount", "#ninesAmount"}});
        for (int j = 0; j < 14; ++j)
            cols << RawColumn{kRawNinesNames.at(j), u"#nines%1"_s.arg(j)};
    } else if (id == u"teamPlayers"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Team ID", "id"}, {"Original Team ID", "originalId"},
                           {"Location Name", "locationName"}, {"Club Name", "clubName"},
                           {"Team Player Amount", "#playerAmount"}});
        for (int j = 0; j < 50; ++j)
            cols << RawColumn{u"Player %1"_s.arg(j + 1), u"#player%1"_s.arg(j)};
    }
    // The team lists' cells are editable (only "#index" isn't).
    for (RawColumn& c : cols)
        c.readOnly = c.key == u"#index"_s;
    return cols;
}
} // namespace

// A team's list cell ("#lineup3", "#rolesAmount", ...): where it is and its size.
bool Rll4CareerEditorManager::rawListCell(const QString& key, qsizetype* at, int* size, int* max) const {
    static const QList<std::pair<QString, int>> lists = {
        {u"#playerAmount"_s, 0}, {u"#player"_s, 1}, {u"#lineupAmount"_s, 2}, {u"#lineup"_s, 3},
        {u"#rolesAmount"_s, 4}, {u"#role"_s, 5}, {u"#ninesRolesAmount"_s, 8}, {u"#ninesRole"_s, 9},
        {u"#ninesAmount"_s, 6}, {u"#nines"_s, 7}};
    for (const auto& [name, kind] : std::as_const(lists)) {
        if (!key.startsWith(name))
            continue;
        const QString rest = key.mid(name.size());
        const bool amount = kind % 2 == 0;
        if (amount != rest.isEmpty())
            continue;
        bool ok = true;
        const int j = amount ? 0 : rest.toInt(&ok);
        const int slotCounts[] = {50, 17, 4, kNinesSlots, 4};
        if (!ok || j < 0 || j >= slotCounts[kind / 2])
            return false;
        const int* amountOff[] = {kPlayerAmount, kLineupAmount, kRolesAmount, kNinesAmount, kNinesRolesAmount};
        const int* idsOff[] = {kPlayers, kLineups, kRoles, kNines, kNinesRoles};
        *at = amount ? amountOff[kind / 2][m_variant] : idsOff[kind / 2][m_variant] + 4 * j;
        *size = amount ? 2 : 4;
        *max = amount ? slotCounts[kind / 2] : -1;
        return true;
    }
    return false;
}

QString Rll4CareerEditorManager::rawKind(const QString& id) {
    if (id == u"players"_s || id == u"skills"_s)
        return u"player"_s;
    if (id == u"leagues"_s)
        return u"league"_s;
    if (id == u"status"_s || id == u"retirements"_s)
        return id == u"status"_s ? u"status"_s : u"retirement"_s;
    return u"team"_s;
}

int Rll4CareerEditorManager::rawCount(const QString& kind) const {
    if (kind == u"league"_s)
        return static_cast<int>(std::size(kLeagueAt));
    if (kind == u"status"_s)
        return qMin(kStatusMax, static_cast<int>(readNum(kStatusAt, 2, true, false)));
    if (kind == u"retirement"_s)
        return qBound(0, static_cast<int>(readNum(kRetirementAt + 4, 4, true, true)), 1024);
    return kind == u"team"_s ? teamCount() : playerCount();
}

// A row of the league / status / retirement lists: where it is.
qsizetype Rll4CareerEditorManager::rawListAt(const QString& kind, int index) const {
    if (index < 0 || index >= rawCount(kind))
        return -1;
    if (kind == u"league"_s)
        return kLeagueAt[index];
    if (kind == u"status"_s)
        return kStatusAt + 2 + qsizetype(index) * kStatusSize;
    return kRetirementAt + 8 + qsizetype(index) * 5;
}

QString Rll4CareerEditorManager::rawGet(const QString& kind, int index, const QString& key) const {
    if (kind == u"league"_s || kind == u"status"_s || kind == u"retirement"_s) {
        const qsizetype e = rawListAt(kind, index);
        if (e < 0)
            return {};
        if (key == u"#index"_s)
            return QString::number(index);
        if (kind == u"league"_s) {
            if (key == u"#name"_s)
                return readText(e, 40);
            if (key == u"#shortName"_s)
                return readText(e + 40, 9);
            const int amount = static_cast<int>(readNum(e + 49, 4, false, true));
            if (key == u"#teamAmount"_s)
                return QString::number(amount);
            const int j = QStringView(key).mid(5).toInt();
            return j < amount ? QString::number(readNum(e + 53 + 4 * j, 4, false, true)) : QString();
        }
        const int id = static_cast<int>(readNum(e, 4, true, true));
        if (key == u"#id"_s)
            return QString::number(id);
        if (key == u"#firstName"_s || key == u"#lastName"_s) {
            const int p = playerIndexById(id);
            return p < 0 ? QString() : rawGet(u"player"_s, p, key == u"#firstName"_s ? u"firstName"_s : u"lastName"_s);
        }
        if (key == u"#reason"_s)
            return QString::number(static_cast<quint8>(m_data.at(e + 4)));
        const int part = key == u"#duration"_s ? 5 : key == u"#additionalA"_s ? 7 : 9;
        return QString::number(readNum(e + part, 2, true, false));
    }
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return {};
    if (key == u"#index"_s)
        return QString::number(index);
    qsizetype off = 0;
    int size = 0, max = 0;
    if (rawListCell(key, &off, &size, &max))
        return QString::number(readNum(at + off, size, true, size == 4));
    const auto [name, part] = colourPart(key);
    const FieldDef* d = defFor(kind, name);
    if (!d)
        return {};
    const int* o = m_variant ? d->offDemo : d->off;
    if (d->type != Type::Virtual && o[0] < 0)
        return {};
    switch (d->type) {
    case Type::Virtual: return virtualValue(kind, at, name).toString();
    case Type::Str: return readText(at + o[0], d->size);
    case Type::Bool: return RawTableModel::boolText(m_data.at(at + o[0]) != 0);
    case Type::Bool255: return RawTableModel::boolText(static_cast<quint8>(m_data.at(at + o[0])) == 255);
    case Type::Rgb: return QString::number(static_cast<quint8>(m_data.at(at + o[part < 0 ? 0 : part])));
    default: return QString::number(readNum(at + o[0], d->size, d->big, d->type == Type::I16 || d->type == Type::I32));
    }
}

bool Rll4CareerEditorManager::rawSet(const QString& kind, int index, const QString& key, const QString& text,
                                   QString* error) {
    const bool isList = kind == u"league"_s || kind == u"status"_s || kind == u"retirement"_s;
    const qsizetype at = isList ? rawListAt(kind, index) : recordAt(kind, index);
    if (at < 0)
        return false;
    bool ok = false;
    const auto number = [&](qint64 lo, qint64 hi, qint64* v) {
        *v = RawTableModel::parseInt(text, &ok);
        if (!ok) {
            *error = u"\"%1\" isn't a number"_s.arg(text);
            return false;
        }
        if (*v < lo || *v > hi) {
            *error = u"%1 is out of range (%2 to %3)"_s.arg(*v).arg(lo).arg(hi);
            return false;
        }
        return true;
    };
    qint64 v = 0;
    if (isList) {
        if (kind == u"league"_s) {
            if (key == u"#name"_s || key == u"#shortName"_s) {
                writeText(at + (key == u"#name"_s ? 0 : 40), key == u"#name"_s ? 40 : 9, text);
                return true;
            }
            if (key == u"#teamAmount"_s) {
                if (!number(0, kRawLeagueTeams, &v))
                    return false;
                writeNum(at + 49, 4, false, v);
                return true;
            }
            const int j = QStringView(key).mid(5).toInt();
            if (text.isEmpty() || j >= readNum(at + 49, 4, false, true))
                return false;   // Past the league's teams (the C# editor leaves those).
            if (!number(INT32_MIN, INT32_MAX, &v))
                return false;
            writeNum(at + 53 + 4 * j, 4, false, v);
            return true;
        }
        if (key == u"#id"_s) {
            if (!number(INT32_MIN, INT32_MAX, &v))
                return false;
            writeNum(at, 4, true, v);
        } else if (key == u"#reason"_s) {
            if (!number(0, 255, &v))
                return false;
            m_data[at + 4] = static_cast<char>(v);
        } else if (kind == u"status"_s) {
            if (!number(0, 65535, &v))
                return false;
            writeNum(at + (key == u"#duration"_s ? 5 : key == u"#additionalA"_s ? 7 : 9), 2, true, v);
        } else {
            return false;
        }
        return true;
    }
    qsizetype off = 0;
    int size = 0, max = 0;
    if (rawListCell(key, &off, &size, &max)) {
        if (!number(size == 4 ? INT32_MIN : 0, size == 4 ? INT32_MAX : max, &v))
            return false;
        writeNum(at + off, size, true, v);
        return true;
    }
    const auto [name, part] = colourPart(key);
    const FieldDef* d = defFor(kind, name);
    if (!d)
        return false;
    const int* o = m_variant ? d->offDemo : d->off;
    switch (d->type) {
    case Type::Virtual:
        if (!number(INT32_MIN, INT32_MAX, &v))
            return false;
        if (!setVirtual(kind, at, name, static_cast<int>(v))) {
            *error = u"%1 isn't one of the choices"_s.arg(v);
            return false;
        }
        return true;
    case Type::Str:
        writeText(at + o[0], d->size, text);
        return true;
    case Type::Bool:
    case Type::Bool255: {
        const bool on = RawTableModel::parseBool(text, &ok);
        if (!ok) {
            *error = u"\"%1\" isn't True or False"_s.arg(text);
            return false;
        }
        m_data[at + o[0]] = static_cast<char>(on ? (d->type == Type::Bool255 ? 255 : 1) : 0);
        return true;
    }
    case Type::Rgb:
        if (!number(0, 255, &v))
            return false;
        m_data[at + o[part < 0 ? 0 : part]] = static_cast<char>(v);
        return true;
    default: {
        const bool isSigned = d->type == Type::I16 || d->type == Type::I32;
        const qint64 lo = !isSigned ? 0 : d->size == 2 ? -32768 : INT32_MIN;
        const qint64 hi = d->size == 1 ? 255 : d->size == 2 ? (isSigned ? 32767 : 65535) : (isSigned ? INT32_MAX : UINT32_MAX);
        if (!number(lo, hi, &v))
            return false;
        writeNum(at + o[0], d->size, d->big, v);
        return true;
    }
    }
}

bool Rll4CareerEditorManager::fillRawTable(const QString& id, RawTableModel* model) const {
    if (!model || !m_loaded)
        return false;
    const QList<RawColumn> cols = rll4CareerRawColumns(id);
    if (cols.isEmpty())
        return false;
    const QString kind = rawKind(id);
    const int count = rawCount(kind);
    QList<QStringList> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        QStringList row;
        row.reserve(cols.size());
        for (const RawColumn& c : std::as_const(cols))
            row << rawGet(kind, i, c.key);
        rows << row;
    }
    QString title;
    for (const auto& tList = rawTables(); const QVariant& t : tList)
        if (t.toMap().value(u"id"_s).toString() == id)
            title = t.toMap().value(u"title"_s).toString();
    model->setTable(id, title, cols, rows);
    return true;
}

QString Rll4CareerEditorManager::applyRawTable(RawTableModel* model) {
    if (!model || !m_loaded)
        return u"Nothing is loaded."_s;
    const QString id = model->tableId();
    const QString kind = rawKind(id);
    const int count = rawCount(kind);
    int changed = 0;
    QString errors = model->apply(
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

// The C# editor's Repair Id's: a player id in the team lists that's a custom player's own id is changed to the id of
// the game player they replace (their original id), as the game links them by that.
int Rll4CareerEditorManager::repairRawIds(RawTableModel* model) const {
    if (!model || !m_loaded)
        return 0;
    QHash<int, int> originalOf;
    for (int i = 0; i < playerCount(); ++i) {
        const qsizetype at = recordAt(u"player"_s, i);
        const int id = static_cast<int>(readNum(at + kPlayerId, 4, true, true));
        const int original = static_cast<int>(readNum(at + kPlayerOriginalId, 4, true, true));
        if (original != 0 && original != id && !originalOf.contains(id))
            originalOf.insert(id, original);
    }
    int repaired = 0;
    for (int c = 0; c < model->columns(); ++c) {
        const QString key = model->columnList().at(c).key;
        static const QStringList idLists = {u"#player"_s, u"#lineup"_s, u"#role"_s, u"#nines"_s, u"#ninesRole"_s};
        const bool isId = std::any_of(idLists.cbegin(), idLists.cend(), [&key](const QString& p) {
            return key.startsWith(p) && !key.endsWith(u"Amount"_s);
        });
        if (!isId)
            continue;
        for (int r = 0; r < model->rows(); ++r) {
            bool ok = false;
            const int id = static_cast<int>(RawTableModel::parseInt(model->cell(r, c), &ok));
            if (ok && originalOf.contains(id) && model->setText(r, c, QString::number(originalOf.value(id))))
                ++repaired;
        }
    }
    return repaired;
}
