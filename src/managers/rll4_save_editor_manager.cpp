#include "rll4_save_editor_manager.h"

#include "app_orchestrator.h"
#include "dds_image_provider.h"
#include "rll4_lists.h"
#include "rll4_save_lists.h"

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

#include "rll4_save_fields.inc"

constexpr qsizetype kTeamTable[2] = {15916, 15868};
constexpr qsizetype kPlayerTable[2] = {3611012, 3609480};
constexpr qint64 kDemoSaveSize = 11451158;
constexpr int kMaxTeams = 300;
constexpr int kMaxPlayers = 15000;
constexpr int kMaxJerseys = 8;
constexpr int kNinesSlots = 14;
constexpr int kCustomLogo = 261;    // Logo list: "custom".
const char* const kResourceDir = ":/qt/qml/com/wouldymods/blobset/assets/rll4_save/";

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
    for (const FieldDef& d : defs)
        out.append(&d);
    return out;
}

QList<const FieldDef*> defsFor(const QString& kind) {
    static const QList<const FieldDef*> team = toList(kTeamFields);
    static const QList<const FieldDef*> player = toList(kPlayerFields);
    return kind == u"team"_s ? team : player;
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

Rll4SaveEditorManager::Rll4SaveEditorManager(QObject* parent) : QObject(parent) {}

QString Rll4SaveEditorManager::fileName() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString Rll4SaveEditorManager::defaultFolder() const {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(u"My Games/Rugby League Live 4/Profile"_s);
}

QString Rll4SaveEditorManager::defaultFile() const {
    return QDir(defaultFolder()).filePath(u"AcademyData000.SAV"_s);
}

void Rll4SaveEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void Rll4SaveEditorManager::edited() {
    ++m_revision;
    emit revisionChanged();
    if (!m_unsaved) {
        m_unsaved = true;
        emit unsavedChangesChanged();
    }
}

int Rll4SaveEditorManager::teamCount() const {
    return m_loaded ? static_cast<int>(readNum(teamTable(), 2, true, false)) : 0;
}

int Rll4SaveEditorManager::playerCount() const {
    return m_loaded ? static_cast<int>(readNum(playerTable(), 2, true, false)) : 0;
}

qsizetype Rll4SaveEditorManager::teamTable() const { return kTeamTable[m_variant]; }
qsizetype Rll4SaveEditorManager::playerTable() const { return kPlayerTable[m_variant]; }
int Rll4SaveEditorManager::teamSize() const { return kTeamSize[m_variant]; }
int Rll4SaveEditorManager::playerSize() const { return kPlayerSize[m_variant]; }

int Rll4SaveEditorManager::maxTeams() const {
    return static_cast<int>(qMin<qsizetype>(kMaxTeams, (playerTable() - teamTable() - 2) / teamSize()));
}

int Rll4SaveEditorManager::maxPlayers() const {
    return static_cast<int>(qMin<qsizetype>(kMaxPlayers, (m_data.size() - playerTable() - 2) / playerSize()));
}

bool Rll4SaveEditorManager::load(const QString& pathOrUrl) {
    const QString path = QDir::toNativeSeparators(pathOrUrl.isEmpty() ? defaultFile() : localPath(pathOrUrl));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Couldn't open %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    const int variant = data.size() == kDemoSaveSize ? 1 : 0;
    if (data.size() < kPlayerTable[variant] + 2) {
        setStatus(false, u"%1 is too small to be Rugby League Live 4's AcademyData000.SAV."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    const int teams = qFromBigEndian<quint16>(data.constData() + kTeamTable[variant]);
    const int players = qFromBigEndian<quint16>(data.constData() + kPlayerTable[variant]);
    if (teams > kMaxTeams || kTeamTable[variant] + 2 + qint64(teams) * kTeamSize[variant] > kPlayerTable[variant]
        || kPlayerTable[variant] + 2 + qint64(players) * kPlayerSize[variant] > data.size()) {
        setStatus(false, u"%1 isn't a Rugby League Live 4 Academy save (AcademyData000.SAV)."_s.arg(QFileInfo(path).fileName()));
        return false;
    }

    m_data = data;
    m_path = path;
    m_variant = variant;
    m_checksumOk = qFromLittleEndian<quint32>(m_data.constData() + 4) == crc32(m_data.constData() + 8, m_data.size() - 8);
    m_backedUp = false;
    m_loaded = true;
    m_logoUrls.clear();
    loadOriginals();
    loadCustomFiles();
    if (m_unsaved) {
        m_unsaved = false;
        emit unsavedChangesChanged();
    }
    ++m_revision;
    emit loadedChanged();
    emit revisionChanged();

    QString text = u"Loaded %1 (%2): %3 teams, %4 players."_s.arg(fileName(), variant ? u"demo"_s : u"PC World Cup Edition"_s)
                       .arg(teams).arg(players);
    if (!m_checksumOk)
        text += u" The file's checksum doesn't match (it'll be right once saved)."_s;
    setStatus(true, text);
    return true;
}

// The game's own players (the C# editor's OriginalPlayers.bin): names, roles and the stats the ratings use.
void Rll4SaveEditorManager::loadOriginals() {
    if (!m_originals.isEmpty())
        return;
    QString path = QString::fromLatin1(kResourceDir) + u"OriginalPlayers.bin"_s;
    if (const QByteArray dir = qgetenv("RLL4_SAVE_RESOURCES"); !dir.isEmpty())
        path = QDir(QString::fromLocal8Bit(dir)).filePath(u"OriginalPlayers.bin"_s);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray b = f.readAll();
    static const char* const statKeys[] = {
        "technicalAbility.strength", "technicalAbility.agility", "technicalAbility.fitness",
        "technicalAbility.acceleration", "technicalAbility.discipline", "technicalAbility.durability",
        "technicalAbility.sprintSpeed", "skills.kickingSkills.grubberKick", "skills.kickingSkills.puntKick",
        "skills.kickingSkills.chipKick", "skills.kickingSkills.bombKick", "skills.kickingSkills.fieldgoalKick",
        "skills.kickingSkills.placeKick", "skills.kickingSkills.dropKick", "skills.kickingSkills.kickForTouch",
        "skills.passingSkills.basicPass", "skills.passingSkills.longPass", "skills.passingSkills.offload",
        "skills.evasionSkills.dummyPass", "skills.evasionSkills.fend", "skills.evasionSkills.sideStep",
        "skills.evasionSkills.breakTackle", "skills.tackleSkills.tackle", "skills.tackleSkills.driveTackle",
        "skills.tackleSkills.diveTackle", "skills.tackleSkills.impactTackle", "skills.miscSkills.contestedCollect",
        "skills.miscSkills.diveCollect", "skills.miscSkills.toePoke"};
    constexpr int statCount = sizeof(statKeys) / sizeof(statKeys[0]);
    qsizetype p = 0;
    const auto u8 = [&b, &p]() { return p < b.size() ? static_cast<quint8>(b.at(p++)) : 0; };
    while (p + 5 < b.size()) {
        Original o;
        o.female = u8() == 1;
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
        if (p + statCount > b.size())
            break;
        for (int s = 0; s < statCount; ++s)
            o.stats.insert(QString::fromLatin1(statKeys[s]), u8());
        const auto& terms = ratingTerms();
        if (o.roles[0] >= 0 && o.roles[0] < terms.size()) {
            int sum = 0;
            for (const RatingTerm& t : terms[o.roles[0]])
                sum += o.stats.value(t.field) * t.mul / t.div;
            o.rating = sum / static_cast<int>(terms[o.roles[0]].size());
        }
        m_originalIndex.insert(o.id, static_cast<int>(m_originals.size()));
        m_originals.append(o);
    }
}

// The custom stadiums (Levels\LevelData.SAV) and logos (Decals\DecalData.SAV) next to the Profile folder.
void Rll4SaveEditorManager::loadCustomFiles() {
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

bool Rll4SaveEditorManager::save() {
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

bool Rll4SaveEditorManager::saveAs(const QString& pathOrUrl) {
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

qsizetype Rll4SaveEditorManager::recordAt(const QString& kind, int index) const {
    if (!m_loaded || index < 0)
        return -1;
    if (kind == u"team"_s)
        return index < teamCount() ? teamTable() + 2 + qsizetype(index) * teamSize() : -1;
    return index < playerCount() ? playerTable() + 2 + qsizetype(index) * playerSize() : -1;
}

int Rll4SaveEditorManager::recordSize(const QString& kind) const {
    return kind == u"team"_s ? teamSize() : playerSize();
}

QString Rll4SaveEditorManager::recordOffset(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    return at < 0 ? QString() : u"0x"_s + QString::number(at, 16).toUpper();
}

qint64 Rll4SaveEditorManager::readNum(qsizetype at, int size, bool big, bool isSigned) const {
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

void Rll4SaveEditorManager::writeNum(qsizetype at, int size, bool big, qint64 value) {
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

QString Rll4SaveEditorManager::readText(qsizetype at, int size) const {
    QByteArray b = m_data.mid(at, size);
    if (b.indexOf('\0') >= 0)
        b.truncate(b.indexOf('\0'));
    return QString::fromLatin1(b);
}

// The C# editor's write: the text, zero padded (the last byte always 0).
void Rll4SaveEditorManager::writeText(qsizetype at, int size, const QString& text) {
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

int Rll4SaveEditorManager::playerIndexById(int id) const {
    if (id <= 0)
        return -1;
    const int count = playerCount();
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"player"_s, i), 4, true, true) == id)
            return i;
    // An edited game player: the custom player whose original id it is.
    for (int i = 0; i < count; ++i)
        if (readNum(recordAt(u"player"_s, i) + kPlayerOriginalId, 4, true, true) == id)
            return i;
    return -1;
}

int Rll4SaveEditorManager::playerRating(int player) const {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return 0;
    const auto& terms = ratingTerms();
    const FieldDef* roleDef = defFor(u"player"_s, u"primaryRole"_s);
    const int role = static_cast<int>(readNum(at + (m_variant ? roleDef->offDemo[0] : roleDef->off[0]), 1, false, false));
    if (role < 0 || role >= terms.size())
        return 0;
    int sum = 0;
    for (const RatingTerm& t : terms[role]) {
        const FieldDef* d = defFor(u"player"_s, t.field);
        if (!d)
            return 0;
        sum += static_cast<int>(readNum(at + (m_variant ? d->offDemo[0] : d->off[0]), 1, false, false)) * t.mul / t.div;
    }
    return sum / static_cast<int>(terms[role].size());
}

int Rll4SaveEditorManager::ratingOfId(int id) const {
    const int p = playerIndexById(id);
    if (p >= 0)
        return playerRating(p);
    const auto it = m_originalIndex.constFind(id);
    return it == m_originalIndex.constEnd() ? 0 : m_originals.at(it.value()).rating;
}

// {id, name, role, rating, age, club, custom, playerIndex, placeKick, puntKick, roles}
QVariantMap Rll4SaveEditorManager::playerInfo(int id) const {
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
        m[u"club"_s] = readNum(at + kPlayerOriginalId, 4, true, true) == id ? u"Edited game player"_s : u"Custom player"_s;
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

QVariantList Rll4SaveEditorManager::playerRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const int count = playerCount();
    const auto off = [this](const char* key) {
        const FieldDef* d = defFor(u"player"_s, QString::fromLatin1(key));
        return m_variant ? d->offDemo[0] : d->off[0];
    };
    for (int i = 0; i < count; ++i) {
        const qsizetype at = recordAt(u"player"_s, i);
        const int id = static_cast<int>(readNum(at, 4, true, true));
        const QString first = readText(at + off("firstName"), 20);
        const QString last = readText(at + off("lastName"), 20);
        if (!f.isEmpty() && !(first + u' ' + last).contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        QVariantMap row;
        row[u"index"_s] = i;
        row[u"id"_s] = id;
        row[u"originalId"_s] = static_cast<int>(readNum(at + kPlayerOriginalId, 4, true, true));
        row[u"first"_s] = first;
        row[u"last"_s] = last;
        row[u"shirt"_s] = readText(at + off("jerseyName"), 16);
        row[u"age"_s] = 0;
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

QVariantList Rll4SaveEditorManager::teamRows(const QString& filter) const {
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

QString Rll4SaveEditorManager::teamLogo(int team) const {
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
QString Rll4SaveEditorManager::teamLogoUrl(int team) const {
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

int Rll4SaveEditorManager::teamRating(int team) const {
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

QStringList Rll4SaveEditorManager::groups(const QString& kind) const {
    QStringList out;
    for (const FieldDef* d : defsFor(kind))
        if (!out.contains(QString::fromLatin1(d->group)))
            out << QString::fromLatin1(d->group);
    return out;
}

QStringList Rll4SaveEditorManager::pageGroups(const QString& kind, const QString& page) const {
    if (kind == u"team"_s) {
        if (page == u"Details"_s)
            return {u"Names"_s, u"Details"_s, u"Colours"_s, u"Record"_s};
        if (page == u"Stadiums"_s)
            return {u"Stadiums"_s, u"Stadium 1"_s, u"Stadium 2"_s, u"Stadium 3"_s};
        return {};
    }
    if (page == u"Identity"_s)
        return {u"Name"_s, u"Details"_s, u"Roles"_s, u"Representative"_s, u"Record"_s};
    if (page == u"Ratings"_s)
        return {u"Attributes"_s, u"Kicking"_s, u"Passing"_s, u"Evasion"_s, u"Tackling"_s, u"Misc Skills"_s};
    if (page == u"Stats"_s)
        return {u"Standard Stats"_s, u"Nines Stats"_s};
    return {};
}

QVariantList Rll4SaveEditorManager::fields(const QString& kind, int index) const {
    return recordFields(kind, index, 0);
}

QVariantList Rll4SaveEditorManager::appearanceFields(int player) const {
    return recordFields(u"player"_s, player, kLook);
}

bool Rll4SaveEditorManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    return setField(u"player"_s, player, key, value);
}

// wantPage 0: everything but the appearance; kLook: the appearance.
QVariantList Rll4SaveEditorManager::recordFields(const QString& kind, int index, int wantPage) const {
    QVariantList out;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return out;
    for (const FieldDef* d : defsFor(kind)) {
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

QVariant Rll4SaveEditorManager::virtualValue(const QString& kind, qsizetype at, const QString& key) const {
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
        const qsizetype s = at + kStadium0[m_variant] + kStadiumStride * key.mid(8).toInt();
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

QVariantList Rll4SaveEditorManager::choicesFor(const QString& kind, qsizetype at, const QString& list,
                                               const QVariant& value) const {
    Q_UNUSED(kind)
    Q_UNUSED(at)
    QVariantList out;
    if (list == u"@logos"_s) {
        out = namedChoices(Rll4SaveLists::logoNames());
        if (!out.isEmpty())
            out[kCustomLogo] = QVariantMap{{u"name"_s, u"Custom Logo (below)"_s}, {u"value"_s, kCustomLogo}};
    } else if (list == u"@customLogos"_s) {
        const QStringList builtIn = Rll4SaveLists::customLogos();
        // The C# editor: 0 none, 2 Big Ant, 21 + position for the sponsors, else a DecalData.SAV logo id.
        for (int i = 0; i < builtIn.size(); ++i)
            out << QVariantMap{{u"name"_s, builtIn.at(i)}, {u"value"_s, i == 0 ? 0 : i == 1 ? 2 : 21 + i}};
        for (const Custom& c : m_decals)
            out << QVariantMap{{u"name"_s, c.name}, {u"value"_s, c.id}};
    } else if (list == u"@stadiums"_s) {
        out = namedChoices(Rll4SaveLists::stadiums());
        for (int i = 0; i < m_stadiums.size(); ++i)
            out << QVariantMap{{u"name"_s, m_stadiums.at(i).name + u" (custom)"_s}, {u"value"_s, 50 + i}};
    } else if (list == u"@contract"_s) {
        for (int y = 2017; y <= 2042; ++y)
            out << QVariantMap{{u"name"_s, QString::number(y)}, {u"value"_s, y}};
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}};
    } else if (list == u"@soo"_s || list == u"@cvc"_s || list == u"@allStars"_s) {
        const int base = list == u"@soo"_s ? 196 : list == u"@cvc"_s ? 199 : 202;
        const QStringList names = list == u"@soo"_s ? QStringList{u"None"_s, u"Queensland"_s, u"New South Wales"_s}
                                : list == u"@cvc"_s ? QStringList{u"None"_s, u"City"_s, u"Country"_s}
                                                    : QStringList{u"None"_s, u"World All Stars"_s, u"Indigenous All Stars"_s};
        out = namedChoices(names, base);
        if (value.toInt() == 0)
            out[0] = QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}};
    } else if (list == u"@faces"_s) {
        for (const Rll4SaveLists::Choice& c : Rll4SaveLists::faceMeshes())
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

bool Rll4SaveEditorManager::setField(const QString& kind, int index, const QString& key, const QVariant& value) {
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

bool Rll4SaveEditorManager::setVirtual(const QString& kind, qsizetype at, const QString& key, const QVariant& value) {
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
        const qsizetype s = at + kStadium0[m_variant] + kStadiumStride * key.mid(8).toInt();
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

QList<int> Rll4SaveEditorManager::rosterIds(int team) const {
    QList<int> out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qMin<int>(rosterMax(), static_cast<int>(readNum(at + kPlayerAmount[m_variant], 2, true, false)));
    for (int i = 0; i < count; ++i)
        out << static_cast<int>(readNum(at + kPlayers[m_variant] + 4 * i, 4, true, true));
    return out;
}

QVariantList Rll4SaveEditorManager::rosterRows(int team) const {
    QVariantList out;
    const QList<int> ids = rosterIds(team);
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap m = playerInfo(ids.at(i));
        m[u"slot"_s] = i;
        out.append(m);
    }
    return out;
}

bool Rll4SaveEditorManager::addRosterPlayer(int team, int playerId) {
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

bool Rll4SaveEditorManager::removeRosterPlayer(int team, int slot) {
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

bool Rll4SaveEditorManager::moveRosterPlayer(int team, int slot, int delta) {
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

bool Rll4SaveEditorManager::sortRoster(int team) {
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

bool Rll4SaveEditorManager::databasePlayersAvailable() const {
    return !m_originals.isEmpty();
}

QVariantList Rll4SaveEditorManager::pickablePlayers(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const auto matches = [&f](const QVariantMap& m) {
        return f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive)
            || m.value(u"id"_s).toString() == f;
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
            continue;   // Edited in this save: listed above.
        if (matches(m))
            out.append(m);
    }
    return out;
}

QVariantMap Rll4SaveEditorManager::lineup(int team) const {
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
    for (int id : rosterIds(team))
        add(id);
    for (const char* g : {"standard", "roles", "nines", "ninesRoles"})
        for (const QVariant& r : out.value(QString::fromLatin1(g)).toList())
            add(r.toMap().value(u"id"_s).toInt());
    out[u"choices"_s] = choices;
    return out;
}

bool Rll4SaveEditorManager::setLineupPlayer(int team, const QString& group, int slot, int playerId) {
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

bool Rll4SaveEditorManager::setHasNines(int team, bool on) {
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
QString Rll4SaveEditorManager::generateBestLineup(int team) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return u"No team."_s;
    struct P {
        int id, rating, roles[3], place, punt;
    };
    QList<P> players;
    for (int id : rosterIds(team)) {
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
    for (int id : lineupIds)
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

QVariantList Rll4SaveEditorManager::matchLists(int team) const {
    Q_UNUSED(team)
    return {QVariantMap{{u"list"_s, u"standard"_s}, {u"title"_s, u"Standard"_s}, {u"shirts"_s, false}},
            QVariantMap{{u"list"_s, u"nines"_s}, {u"title"_s, u"Nines"_s}, {u"shirts"_s, false}}};
}

bool Rll4SaveEditorManager::listPresent(int team, const QString& list) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    const int* countOff = list == u"nines"_s ? kNinesAmount : kLineupAmount;
    return readNum(at + countOff[m_variant], 2, true, false) > 0;
}

bool Rll4SaveEditorManager::setListPresent(int team, const QString& list, bool on) {
    if (list == u"nines"_s)
        return setHasNines(team, on);
    if (!on)
        setStatus(false, u"Every team has a standard lineup."_s);
    return false;
}

// Rows like the Database Editor's: {key, position, id, first, last, roles, rating, present, known, playerIndex}.
QVariantMap Rll4SaveEditorManager::lineupRow(int id, bool present) const {
    QVariantMap m = playerInfo(present ? id : 0);
    QStringList roles;
    for (const QVariant& r : m.value(u"roles"_s).toList())
        roles << kRoleShort.value(r.toInt());
    m[u"roles"_s] = roles.join(u" / "_s);
    m[u"present"_s] = present;
    return m;
}

QVariantList Rll4SaveEditorManager::lineupRows(int team, const QString& list) const {
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

QVariantList Rll4SaveEditorManager::teamRoles(int team, const QString& list) const {
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

bool Rll4SaveEditorManager::setLineupSlot(int team, const QString& list, const QString& key, int playerId) {
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
QVariantList Rll4SaveEditorManager::pickerRows(const QString& filter, int team, bool squadOnly) const {
    QVariantList out;
    const QList<int> squad = squadOnly ? rosterIds(team) : QList<int>();
    const auto toRow = [](const QVariantMap& m) {
        return QVariantMap{{u"id"_s, m.value(u"id"_s)}, {u"first"_s, m.value(u"first"_s)}, {u"last"_s, m.value(u"last"_s)},
                           {u"clubName"_s, m.value(u"club"_s)}, {u"roleName"_s, m.value(u"role"_s)},
                           {u"age"_s, m.value(u"age"_s)}, {u"rating"_s, m.value(u"rating"_s)}};
    };
    if (squadOnly) {
        const QString f = filter.trimmed();
        for (int id : squad) {
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

// =================================================================================================================
// Jerseys
// =================================================================================================================

qsizetype Rll4SaveEditorManager::jerseyAt(int team, int uniform) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || uniform < 0 || uniform >= kMaxJerseys)
        return -1;
    return at + kJerseyBase[m_variant] + qsizetype(uniform) * kJerseyStride;
}

QVariantMap Rll4SaveEditorManager::jerseys(int team) const {
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

QVariantList Rll4SaveEditorManager::jerseyFields(int team, int uniform) const {
    QVariantList out;
    const qsizetype base = jerseyAt(team, uniform);
    if (base < 0)
        return out;
    // The manufacturer and licensed jersey bytes (the C# editor's names).
    const qsizetype makerAt = base + 313;    // jerseys[0].manufactureId - jerseys[0].name
    const qsizetype licensedAt = base + 333; // jerseys[0].licensedId - jerseys[0].name (u16)
    const int maker = static_cast<quint8>(m_data.at(makerAt));
    const int licensed = static_cast<int>(readNum(licensedAt, 2, false, false));
    for (const FieldDef* d : jerseyDefs()) {
        const QString key = keyOf(d);
        QVariantMap m;
        m[u"key"_s] = u"j:%1:%2"_s.arg(uniform).arg(key);
        m[u"label"_s] = QString::fromLatin1(d->label);
        m[u"group"_s] = QString::fromLatin1(d->group);
        m[u"min"_s] = 0;
        m[u"offset"_s] = d->off[0] >= 0 ? u"+0x"_s + QString::number(base - recordAt(u"team"_s, team) + d->off[0], 16).toUpper() : QString();
        if (key == u"@maker"_s) {
            QVariantList makers;
            for (const Rll4Lists::Choice& c : Rll4Lists::manufacturers())
                makers << QVariantMap{{u"name"_s, QString::fromUtf8(c.name)}, {u"value"_s, c.value}};
            if (std::none_of(makers.cbegin(), makers.cend(), [maker](const QVariant& c) { return c.toMap().value(u"value"_s).toInt() == maker; }))
                makers.prepend(QVariantMap{{u"name"_s, u"(%1)"_s.arg(maker)}, {u"value"_s, maker}});
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = makers;
            m[u"value"_s] = maker;
            m[u"refresh"_s] = true;
        } else if (key == u"@licensed"_s) {
            QVariantList jerseys{QVariantMap{{u"name"_s, u"Custom Jersey"_s}, {u"value"_s, 0}}};
            for (const Rll4Lists::Choice& c : Rll4Lists::licensedJerseys(maker))
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

bool Rll4SaveEditorManager::setJerseyField(int team, const QString& key, const QVariant& value) {
    const QStringList parts = key.split(u':');
    if (parts.size() != 3 || parts.at(0) != u"j"_s)
        return false;
    const int uniform = parts.at(1).toInt();
    const qsizetype base = jerseyAt(team, uniform);
    const FieldDef* d = findDef(jerseyDefs(), parts.at(2));
    if (base < 0 || !d)
        return false;
    const QByteArray before = m_data.mid(base, kJerseyStride);
    const QString name = parts.at(2);
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

int Rll4SaveEditorManager::addJersey(int team, int from) {
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

bool Rll4SaveEditorManager::removeJersey(int team, int uniform) {
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

int Rll4SaveEditorManager::addRecord(const QString& kind, bool female) {
    if (!m_loaded)
        return -1;
    const bool team = kind == u"team"_s;
    const int count = team ? teamCount() : playerCount();
    const int max = team ? maxTeams() : maxPlayers();
    if (count >= max) {
        setStatus(false, u"The save already has %1 %2, the most it can hold."_s.arg(count).arg(team ? u"teams"_s : u"players"_s));
        return -1;
    }
    const QString name = m_variant ? (team ? u"Demo_NewTeam.tem"_s : u"Demo_NewPlayer.ply"_s)
                                   : team ? (female ? u"NewFemaleTeam.tem"_s : u"NewMaleTeam.tem"_s)
                                          : (female ? u"NewFemalePlayer.ply"_s : u"NewMalePlayer.ply"_s);
    QString path = QString::fromLatin1(kResourceDir) + name;
    if (const QByteArray dir = qgetenv("RLL4_SAVE_RESOURCES"); !dir.isEmpty())
        path = QDir(QString::fromLocal8Bit(dir)).filePath(name);
    QFile f(path);
    const int size = recordSize(kind);
    const QByteArray record = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    if (record.size() != size) {
        setStatus(false, u"The new %1 template (%2) is missing."_s.arg(team ? u"team"_s : u"player"_s, name));
        return -1;
    }
    const qsizetype table = team ? teamTable() : playerTable();
    const qsizetype at = table + 2 + qsizetype(count) * size;
    m_data.replace(at, size, record);
    // The C# editor's ids: 1000 + position for teams, 10000 + position for players (one not used yet).
    int id = (team ? 1000 : 10000) + count;
    const auto used = [&](int candidate) {
        for (int i = 0; i < count; ++i)
            if (readNum(table + 2 + qsizetype(i) * size, 4, true, true) == candidate)
                return true;
        return false;
    };
    while (used(id))
        ++id;
    writeNum(at, 4, true, id);
    writeNum(table, 2, true, count + 1);
    edited();
    setStatus(true, u"Added a new %1 (id %2)."_s.arg(team ? u"team"_s : u"player"_s).arg(id));
    return count;
}

bool Rll4SaveEditorManager::deleteRecord(const QString& kind, int index) {
    const bool team = kind == u"team"_s;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return false;
    const int count = team ? teamCount() : playerCount();
    const int size = recordSize(kind);
    const qsizetype end = (team ? teamTable() : playerTable()) + 2 + qsizetype(count) * size;
    // The later records move up; the last slot keeps its old bytes (as the C# editor leaves it).
    m_data.replace(at, end - size - at, m_data.mid(at + size, end - at - size));
    writeNum(team ? teamTable() : playerTable(), 2, true, count - 1);
    edited();
    setStatus(true, u"Deleted the %1. Teams that had them in their roster or lineups still list the id."_s.arg(team ? u"team"_s : u"player"_s));
    return true;
}

QString Rll4SaveEditorManager::importRecord(const QString& kind, int index, const QString& pathOrUrl) {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Pick a %1 first."_s.arg(kind);
    QFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::ReadOnly))
        return u"Couldn't open %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    const QByteArray record = f.readAll();
    const int size = recordSize(kind);
    if (record.size() != size)
        return u"%1 is %2 bytes; a %3 here is %4 (%5)."_s.arg(QFileInfo(f).fileName()).arg(record.size())
            .arg(kind).arg(size).arg(m_variant ? u"demo"_s : u"PC World Cup Edition"_s);
    const qint64 id = readNum(at, 4, true, true);
    m_data.replace(at, size, record);
    writeNum(at, 4, true, id);
    m_logoUrls.clear();
    edited();
    setStatus(true, u"Imported %1."_s.arg(QFileInfo(f).fileName()));
    return {};
}

QString Rll4SaveEditorManager::exportRecord(const QString& kind, int index, const QString& pathOrUrl) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Nothing to export."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data.mid(at, recordSize(kind))) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QString Rll4SaveEditorManager::recordFileName(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return {};
    if (kind == u"team"_s) {
        const FieldDef* d = defFor(kind, u"fullName"_s);
        return fileSafe(readText(at + (m_variant ? d->offDemo[0] : d->off[0]), d->size)) + u".tem"_s;
    }
    const QVariantMap info = playerInfo(static_cast<int>(readNum(at, 4, true, true)));
    return fileSafe(info.value(u"name"_s).toString()) + u".ply"_s;
}

QString Rll4SaveEditorManager::exportDecoded(const QString& pathOrUrl) const {
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

QVariantList Rll4SaveEditorManager::rawRows(const QString& kind, int index) const {
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
    for (const FieldDef* d : defsFor(kind)) {
        const int* o = m_variant ? d->offDemo : d->off;
        if (d->type == Type::Rgb)
            for (int c = 0; c < 3; ++c)
                mark(o[c], 1);
        else if (d->type != Type::Virtual)
            mark(o[0], d->size);
    }
    if (kind == u"team"_s) {
        for (int u = 0; u < kMaxJerseys; ++u) {
            const int base = kJerseyBase[m_variant] + u * kJerseyStride;
            for (const FieldDef* d : jerseyDefs()) {
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

bool Rll4SaveEditorManager::setRawByte(const QString& kind, int index, int offset, int value) {
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
    const char* key;    // A field key (".r" / ".g" / ".b": a colour's part), "#index" or "#<team list>".
};

const QList<RawCol> kRawPlayerCols = {
    {"Player Index", "#index"}, {"Custom Player ID", "id"}, {"Original Player ID", "originalId"},
    {"First Name", "firstName"}, {"Last Name", "lastName"}, {"DOB - Day", "dob.day"}, {"DOB - Month", "dob.month"},
    {"DOB - Year", "dob.year"}, {"Jersey Number", "jerseyNumber"}, {"Jersey Name", "jerseyName"},
    {"Primary Role", "primaryRole"}, {"Secondary Role", "secondaryRole"}, {"Tertiary Role", "tertiaryRole"},
    {"State Of Origin", "stateOfOrigin"}, {"City V's Country", "cityVsCountry"}, {"All Stars", "allStars"},
    {"Preferred Hand", "preferredHand"}, {"Preferred Foot", "preferredFoot"}, {"Representative Country", "repCountry"},
    {"Country Of Birth", "countryOfBirth"}, {"Commentary Surname", "commentarySurname"},
    {"Commentary Nickname", "commentaryNickname"}, {"Contract Expiry", "contractExpiry"},
    {"Height", "appearance.height"}, {"Weight", "appearance.weight"}, {"Reputation", "attributes.reputation"},
    {"Ego", "attributes.ego"}, {"Loyalty", "attributes.loyalty"}, {"Perks", "attributes.perk"},
    {"Strength", "technicalAbility.strength"}, {"Agility", "technicalAbility.agility"},
    {"Fitness", "technicalAbility.fitness"}, {"Acceleration", "technicalAbility.acceleration"},
    {"Discipline", "technicalAbility.discipline"}, {"Durability", "technicalAbility.durability"},
    {"Sprint Speed", "technicalAbility.sprintSpeed"},
};

const QList<RawCol> kRawSkillCols = {
    {"Player Index", "#index"}, {"Custom Player ID", "id"}, {"Original Player ID", "originalId"},
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
};

const QList<RawCol> kRawTeamCols = {
    {"Team Index", "#index"}, {"Custom Team ID", "id"}, {"Original Team ID", "originalId"}, {"Team Type", "type"},
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

QList<RawColumn> rawColumns(const QList<RawCol>& cols) {
    QList<RawColumn> out;
    for (const RawCol& c : cols)
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

QVariantList Rll4SaveEditorManager::rawTables() const {
    return {
        QVariantMap{{u"id"_s, u"players"_s}, {u"title"_s, u"Raw Player Database"_s}, {u"repair"_s, false}},
        QVariantMap{{u"id"_s, u"skills"_s}, {u"title"_s, u"Raw Player Skills"_s}, {u"repair"_s, false}},
        QVariantMap{{u"id"_s, u"teams"_s}, {u"title"_s, u"Raw Team Database"_s}, {u"repair"_s, false}},
        QVariantMap{{u"id"_s, u"lineup"_s}, {u"title"_s, u"Raw Team Lineup"_s}, {u"repair"_s, true}},
        QVariantMap{{u"id"_s, u"nines"_s}, {u"title"_s, u"Raw Team Nines Lineup"_s}, {u"repair"_s, true}},
        QVariantMap{{u"id"_s, u"teamPlayers"_s}, {u"title"_s, u"Raw Team Players"_s}, {u"repair"_s, true}},
    };
}

namespace {
QList<RawColumn> rll4SaveRawColumns(const QString& id) {
    QList<RawColumn> cols;
    if (id == u"players"_s)
        return rawColumns(kRawPlayerCols);
    if (id == u"skills"_s)
        return rawColumns(kRawSkillCols);
    if (id == u"teams"_s)
        return rawColumns(kRawTeamCols);
    if (id == u"lineup"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Custom Team ID", "id"}, {"Original Team ID", "originalId"},
                           {"Location Name", "locationName"}, {"Club Name", "clubName"},
                           {"Team Role Amount", "#rolesAmount"}, {"Team Captain ID", "#role0"},
                           {"Team GoalKicker ID", "#role1"}, {"Team PlayMaker1 ID", "#role2"},
                           {"Team PlayMaker2 ID", "#role3"}, {"Team Lineup Amount", "#lineupAmount"}});
        for (int j = 0; j < 17; ++j)
            cols << RawColumn{kRawLineupNames.at(j), u"#lineup%1"_s.arg(j)};
    } else if (id == u"nines"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Custom Team ID", "id"}, {"Original Team ID", "originalId"},
                           {"Full Name", "fullName"}, {"Location Name", "locationName"}, {"Club Name", "clubName"},
                           {"Nines Roles Amount", "#ninesRolesAmount"}, {"Team Nines Captain ID", "#ninesRole0"},
                           {"Team Nines GoalKicker ID", "#ninesRole1"}, {"Team Nines PlayMaker1 ID", "#ninesRole2"},
                           {"Team Nines PlayMaker2 ID", "#ninesRole3"}, {"Nines Lineup Amount", "#ninesAmount"}});
        for (int j = 0; j < 14; ++j)
            cols << RawColumn{kRawNinesNames.at(j), u"#nines%1"_s.arg(j)};
    } else if (id == u"teamPlayers"_s) {
        cols = rawColumns({{"Team Index", "#index"}, {"Custom Team ID", "id"}, {"Original Team ID", "originalId"},
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
bool Rll4SaveEditorManager::rawListCell(const QString& key, qsizetype* at, int* size, int* max) const {
    static const QList<std::pair<QString, int>> lists = {
        {u"#playerAmount"_s, 0}, {u"#player"_s, 1}, {u"#lineupAmount"_s, 2}, {u"#lineup"_s, 3},
        {u"#rolesAmount"_s, 4}, {u"#role"_s, 5}, {u"#ninesRolesAmount"_s, 8}, {u"#ninesRole"_s, 9},
        {u"#ninesAmount"_s, 6}, {u"#nines"_s, 7}};
    for (const auto& [name, kind] : lists) {
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

QString Rll4SaveEditorManager::rawGet(const QString& kind, int index, const QString& key) const {
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

bool Rll4SaveEditorManager::rawSet(const QString& kind, int index, const QString& key, const QString& text,
                                   QString* error) {
    const qsizetype at = recordAt(kind, index);
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

bool Rll4SaveEditorManager::fillRawTable(const QString& id, RawTableModel* model) const {
    if (!model || !m_loaded)
        return false;
    const QList<RawColumn> cols = rll4SaveRawColumns(id);
    if (cols.isEmpty())
        return false;
    const QString kind = (id == u"players"_s || id == u"skills"_s) ? u"player"_s : u"team"_s;
    const int count = kind == u"team"_s ? teamCount() : playerCount();
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

QString Rll4SaveEditorManager::applyRawTable(RawTableModel* model) {
    if (!model || !m_loaded)
        return u"Nothing is loaded."_s;
    const QString id = model->tableId();
    const QString kind = (id == u"players"_s || id == u"skills"_s) ? u"player"_s : u"team"_s;
    const int count = kind == u"team"_s ? teamCount() : playerCount();
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

// The C# editor's Repair Id's: a player id in the team lists that's a custom player's own id is changed to the id of
// the game player they replace (their original id), as the game links them by that.
int Rll4SaveEditorManager::repairRawIds(RawTableModel* model) const {
    if (!model || !m_loaded)
        return 0;
    QHash<int, int> originalOf;
    for (int i = 0; i < playerCount(); ++i) {
        const qsizetype at = recordAt(u"player"_s, i);
        const int id = static_cast<int>(readNum(at, 4, true, true));
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
