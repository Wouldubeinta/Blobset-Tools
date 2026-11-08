#include "save_editor_manager.h"

#include "app_orchestrator.h"
#include "app_paths.h"
#include "blobset_entry_registry.h"
#include "dds_image_provider.h"
#include "m3mp_tool_manager.h"
#include "pa_serial.h"
#include "bigant_database.h"
#include "rl26_appearance.h"
#include "rl26_kit.h"
#include "rl26_lists.h"
#include "rl26_save.h"
#include "rl26_user_content.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <cstring>

using namespace Qt::StringLiterals;

namespace {

// --- Where the records are (AcademyData000.SAV, decoded: 20,236,153 bytes) -------------------------------------------
// Custom players: a u16 count, then 0x444 byte records (u32 id 20000, 20001, ... first).
constexpr qsizetype kPlayerCount = 0xDE3DC2;
constexpr qsizetype kPlayerTable = 0xDE3DC4;
constexpr qsizetype kPlayerSize = 0x444;
constexpr int kPlayerMaxCount = 2000;
constexpr int kAppearanceOffset = 0xA7;   // paCharacterVisualConfig (251 bytes), after PlayerSkills and a u16.
// Custom teams: 0x2BAD byte records from 0x2D3A (u16 id 1000, 1001, ... first). An unused slot's id is 0.
constexpr qsizetype kTeamTable = 0x2D3A;
constexpr qsizetype kTeamSize = 0x2BAD;
constexpr int kTeamMaxCount = 100;
// A team's roster: u16 count, then u32 player ids (the game's MIN_ROSTER_SIZE / MAX_ROSTER_SIZE: 19 / 50).
constexpr int kRosterOffset = 0x2668;
constexpr int kRosterMax = 50;
constexpr int kRosterMin = 19;
constexpr int kFirstCustomPlayerId = 20000;
// A team's lineups: u16 count, then 8 lineups of 139 bytes: u16 player count, u32 ids[19], u16 role count,
// u32 roles[5] (captain, goal kicker, play makers 1 and 2, ?), name[16], u8 1, u8 rules (0 League, 1 Nines,
// 2 NRL 2026), u16 shirt count, u8 shirts[19].
// (The same layout as the game's teams' lineups further on in the save.)
constexpr int kLineupsOffset = 0x2732;
constexpr int kLineupSize = 139;
constexpr int kLineupMax = 8;
constexpr int kLineupSlots = 19;
constexpr int kLineupIds = 2, kLineupRoleCount = 78, kLineupRoles = 80, kLineupName = 100, kLineupShirtCount = 118,
              kLineupShirts = 120;
// The game's team / player database M3MP (defaultdata/defaultdata_players), for the roster's player names.
const char* const kDatabaseHash = "2BE30C8187CABAF6";
// Which uniform the game uses where (the Database Editor's names).
const QStringList kKitSlotNames = {u"Home"_s, u"Away"_s, u"Heritage Round"_s, u"Anzac Round"_s, u"Nines"_s,
                                   u"Alternate / Special Round"_s, u"Indigenous Round"_s};
const QStringList kRoleShort = {u"FB"_s, u"WG"_s, u"C"_s, u"FE"_s, u"HB"_s, u"FR"_s, u"HK"_s, u"SR"_s, u"L"_s};

enum class Type { U8, U16, U32, Str, Bool, Rgb };

struct FieldDef {
    const char* key;
    const char* label;
    const char* group;
    int offset;          // In the record.
    Type type;
    int size = 0;        // Strings: the buffer (with the terminating zero).
    int min = 0;
    int max = 0;         // 0 = the type's range.
    bool readOnly = false;
    bool probable = false;   // Mapped from too few saves to be sure: shown with a "?".
};

// The fields, generated from the game's own record layouts (rl26_schema_dump.py: the DataConfigMaps the game builds
// from design/characters/*.lua). The save writes each map's fields in the order of the game's hash table, packed:
//   player (0x444):  +0x00 u32 id, +0x04 u32 linked player id, +0x08 u8 record type,
//                    +0x09 PlayerIdentity (123), +0x84 PlayerAttributes (13), +0x91 PlayerSkills (20),
//                    +0xA5 appearance and the rest (not mapped yet: kept as they are)
//   team (0x2BAD):   +0x00 u16 id, +0x15 ClubIdentity (345: the nested ClubHomeGroundInfo maps without their 3 byte
//                    in-memory header), +0x176 the jerseys (ConvantClub, 8851 bytes, see rl26_kit.h),
//                    +0x2668 the roster (u16 count, u32 player ids[50]), then the rest
// Limits are the game's (min / max in the maps).
const FieldDef kPlayerFields[] = {
    {"id", "Player ID", "Record", 0x000, Type::U32, 0, 0, 0, true},
    {"linkedId", "Linked Player ID", "Record", 0x004, Type::U32},
    {"recordType", "Record Type", "Record", 0x008, Type::U8},
    {"firstname", "First Name", "Name", 0x00F, Type::Str, 32, 0, 0},
    {"surname", "Last Name", "Name", 0x035, Type::Str, 32, 0, 0},
    {"shirtName", "Shirt Name", "Name", 0x056, Type::Str, 32, 0, 0},
    {"shirtNumber", "Shirt Number", "Name", 0x00B, Type::U8, 0, 0, 99},
    {"commentaryNameHash", "Commentary Name Hash", "Name", 0x07A, Type::U32, 0, 0, 0},
    {"voice", "Voice", "Name", 0x034, Type::U8, 0, 1, 15},
    {"sex", "Female", "Details", 0x07F, Type::Bool, 0, 0, 0},
    {"age", "Age", "Details", 0x032, Type::U8, 0, 1, 99},
    {"birthDay", "Birth Day", "Details", 0x033, Type::U8, 0, 1, 31},
    {"birthMonth", "Birth Month", "Details", 0x077, Type::U8, 0, 1, 12},
    {"birthYear", "Birth Year", "Details", 0x030, Type::U16, 0, 1800, 2500},
    {"rightHanded", "Right Handed (Preferred Hand)", "Details", 0x07E, Type::Bool, 0, 0, 0},
    {"rightFooted", "Right Footed (Preferred Foot)", "Details", 0x02F, Type::Bool, 0, 0, 0},
    {"contractExpiry", "Contract Expiry", "Details", 0x009, Type::U16, 0, 0, 2500},
    {"isWorldCup", "World Cup", "Details", 0x081, Type::Bool, 0, 0, 0},
    {"conversionStyle", "Conversion Style", "Details", 0x00D, Type::U8, 0, 0, 14},
    {"photogrammetryStatus", "Photogrammetry Status", "Details", 0x082, Type::U8, 0, 0, 0},
    {"preferredRolePrimary", "Primary Role", "Roles", 0x055, Type::U8, 0, 0, 9},
    {"preferredRoleSecondary", "Secondary Role", "Roles", 0x080, Type::U8, 0, 0, 9},
    {"preferredRoleTertiary", "Tertiary Role", "Roles", 0x083, Type::U8, 0, 0, 9},
    {"countryOfBirth", "Country Of Birth", "Representative", 0x076, Type::U8, 0, 0, 195},
    {"representativeCountry", "Representative Country", "Representative", 0x079, Type::U8, 0, 0, 43},
    {"stateOfOrigin", "State Of Origin", "Representative", 0x078, Type::U8, 0, 196, 198},
    {"cityVsCountry", "City vs Country", "Representative", 0x00C, Type::U8, 0, 199, 201},
    {"allStars", "All Stars", "Representative", 0x00E, Type::U8, 0, 202, 204},
    {"height", "Height", "Attributes", 0x085, Type::U8, 0, 155, 220},
    {"weight", "Weight", "Attributes", 0x088, Type::U8, 0, 50, 200},
    {"strength", "Strength", "Attributes", 0x08F, Type::U8, 0, 0, 99},
    {"agility", "Agility", "Attributes", 0x08D, Type::U8, 0, 0, 99},
    {"acceleration", "Acceleration", "Attributes", 0x08E, Type::U8, 0, 0, 99},
    {"sprintSpeed", "Sprint Speed", "Attributes", 0x089, Type::U8, 0, 0, 99},
    {"stamina", "Stamina", "Attributes", 0x087, Type::U8, 0, 0, 99},
    {"injuryTolerance", "Durability", "Attributes", 0x084, Type::U8, 0, 0, 99},
    {"aggression", "Discipline", "Attributes", 0x08B, Type::U8, 0, 0, 99},
    {"reputation", "Reputation", "Attributes", 0x08C, Type::U8, 0, 0, 99},
    {"loyalty", "Loyalty", "Attributes", 0x08A, Type::U8, 0, 0, 99},
    {"ego", "Ego", "Attributes", 0x090, Type::U8, 0, 0, 99},
    {"perk", "Career Perk", "Attributes", 0x086, Type::U8, 0, 0, 5},
    {"tackle", "Tackle", "Skills", 0x0A1, Type::U8, 0, 0, 99},
    {"driveTackle", "Drive Tackle", "Skills", 0x091, Type::U8, 0, 0, 99},
    {"diveTackle", "Dive Tackle", "Skills", 0x097, Type::U8, 0, 0, 99},
    {"impactTackle", "Impact Tackle", "Skills", 0x095, Type::U8, 0, 0, 99},
    {"basicPass", "Basic Pass", "Skills", 0x0A4, Type::U8, 0, 0, 99},
    {"longPass", "Long Pass", "Skills", 0x09C, Type::U8, 0, 0, 99},
    {"offload", "Offload", "Skills", 0x09B, Type::U8, 0, 0, 99},
    {"dummyPass", "Dummy Pass", "Skills", 0x09F, Type::U8, 0, 0, 99},
    {"fend", "Fend", "Skills", 0x09A, Type::U8, 0, 0, 99},
    {"sideStep", "Side Step", "Skills", 0x094, Type::U8, 0, 0, 99},
    {"breakTackle", "Break Tackle", "Skills", 0x0A2, Type::U8, 0, 0, 99},
    {"contestedCollect", "Contested Collect", "Skills", 0x093, Type::U8, 0, 0, 99},
    {"diveCollect", "Dive Collect", "Skills", 0x0A3, Type::U8, 0, 0, 99},
    {"ballStrip", "Ball Strip", "Skills", 0x09D, Type::U8, 0, 0, 99},
    {"groundKick", "Ground Kick", "Skills", 0x0A0, Type::U8, 0, 0, 99},
    {"puntKick", "Punt Kick", "Skills", 0x092, Type::U8, 0, 0, 99},
    {"chipKick", "Chip Kick", "Skills", 0x09E, Type::U8, 0, 0, 99},
    {"bombKick", "Bomb Kick", "Skills", 0x099, Type::U8, 0, 0, 99},
    {"fieldgoalKick", "Field Goal Kick", "Skills", 0x098, Type::U8, 0, 0, 99},
    {"placeKick", "Place Kick", "Skills", 0x096, Type::U8, 0, 0, 99},
};

const FieldDef kTeamFields[] = {
    {"id", "Team ID", "Record", 0x000, Type::U16, 0, 0, 0, true},
    // The emblem: m_decalIdOrIcon (u32 m_decalId, u8 m_iconCategory, u16 m_iconIndex). A custom logo (from
    // Decals\\DecalData.SAV) when m_decalId isn't 0, otherwise m_iconIndex in logos.txt. (m_iconIndex is a u16: the
    // two bytes after it are m_commentaryColour and m_showUniformEmblemDecals.)
    {"logoNumber", "Logo", "Details", 0x011, Type::U16},
    {"decalId", "Custom Logo", "Details", 0x00C, Type::U32},
    {"fullName", "Full Name", "Names", 0x0E1, Type::Str, 54, 0, 0},
    {"shortName", "Short Name", "Names", 0x158, Type::Str, 22, 0, 0},
    {"nickname", "Nickname", "Names", 0x018, Type::Str, 19, 0, 0},
    {"hudName", "Abbreviation (HUD)", "Names", 0x154, Type::Str, 4, 0, 0},
    {"shirtName", "Shirt Name", "Names", 0x0CF, Type::Str, 16, 0, 0},
    {"sex", "Female", "Details", 0x144, Type::Bool, 0, 0, 0},
    {"clubType", "Club Type", "Details", 0x0B5, Type::U8, 0, 0, 5},
    {"affiliations", "Affiliations", "Details", 0x0DF, Type::U16, 0, 0, 206},
    {"supporters", "Supporters", "Details", 0x0B6, Type::U8, 0, 0, 0},
    {"isWorldCup", "World Cup", "Details", 0x153, Type::Bool, 0, 0, 0},
    {"alternateNumbering", "Alternate Numbering", "Details", 0x0CE, Type::Bool, 0, 0, 0},
    {"alternativeNumberingSystem", "Alternative Numbering System", "Details", 0x0BF, Type::U8, 0, 0, 18},
    {"homeGroundCount", "Home Ground Count", "Details", 0x117, Type::U8, 0, 0, 0},
    {"primaryRGBColour", "Primary Colour", "Colours", 0x02B, Type::Rgb, 0, 0, 0},
    {"secondaryRGBColour", "Secondary Colour", "Colours", 0x015, Type::Rgb, 0, 0, 0},
    {"hudTextRGBColour", "HUD Text Colour", "Colours", 0x0B2, Type::Rgb, 0, 0, 0},
    {"commentaryTeamLocationHash", "Commentary Location Hash", "Commentary", 0x0B7, Type::U32, 0, 0, 0},
    {"commentaryTeamMascotHash", "Commentary Mascot Hash", "Commentary", 0x0BB, Type::U32, 0, 0, 0},
    {"homeGrounds#0:wantCustomName", "Want Custom Name", "Home Ground 1", 0x02E, Type::Bool, 0, 0, 0},
    {"homeGrounds#0:customName", "Custom Name", "Home Ground 1", 0x02F, Type::Str, 32, 0, 0},
    {"homeGrounds#0:stadium", "Stadium", "Home Ground 1", 0x04F, Type::U8, 0, 0, 0},
    {"homeGrounds#0:stadiumId", "Custom Stadium", "Home Ground 1", 0x051, Type::U32, 0, 0, 0},
    {"homeGrounds#0:stadiumSharingId", "Stadium Sharing ID", "Home Ground 1", 0x056, Type::U32, 0, 0, 0},
    {"homeGrounds#0:weatherZone", "Weather Zone", "Home Ground 1", 0x055, Type::U8, 0, 0, 0},
    {"homeGrounds#0:isOnDisk", "Is On Disk", "Home Ground 1", 0x050, Type::Bool, 0, 0, 0},
    {"homeGrounds#1:wantCustomName", "Want Custom Name", "Home Ground 2", 0x05A, Type::Bool, 0, 0, 0},
    {"homeGrounds#1:customName", "Custom Name", "Home Ground 2", 0x05B, Type::Str, 32, 0, 0},
    {"homeGrounds#1:stadium", "Stadium", "Home Ground 2", 0x07B, Type::U8, 0, 0, 0},
    {"homeGrounds#1:stadiumId", "Custom Stadium", "Home Ground 2", 0x07D, Type::U32, 0, 0, 0},
    {"homeGrounds#1:stadiumSharingId", "Stadium Sharing ID", "Home Ground 2", 0x082, Type::U32, 0, 0, 0},
    {"homeGrounds#1:weatherZone", "Weather Zone", "Home Ground 2", 0x081, Type::U8, 0, 0, 0},
    {"homeGrounds#1:isOnDisk", "Is On Disk", "Home Ground 2", 0x07C, Type::Bool, 0, 0, 0},
    {"homeGrounds#2:wantCustomName", "Want Custom Name", "Home Ground 3", 0x086, Type::Bool, 0, 0, 0},
    {"homeGrounds#2:customName", "Custom Name", "Home Ground 3", 0x087, Type::Str, 32, 0, 0},
    {"homeGrounds#2:stadium", "Stadium", "Home Ground 3", 0x0A7, Type::U8, 0, 0, 0},
    {"homeGrounds#2:stadiumId", "Custom Stadium", "Home Ground 3", 0x0A9, Type::U32, 0, 0, 0},
    {"homeGrounds#2:stadiumSharingId", "Stadium Sharing ID", "Home Ground 3", 0x0AE, Type::U32, 0, 0, 0},
    {"homeGrounds#2:weatherZone", "Weather Zone", "Home Ground 3", 0x0AD, Type::U8, 0, 0, 0},
    {"homeGrounds#2:isOnDisk", "Is On Disk", "Home Ground 3", 0x0A8, Type::Bool, 0, 0, 0},
    {"homeGroundFinal:wantCustomName", "Want Custom Name", "Grand Final Ground", 0x118, Type::Bool, 0, 0, 0},
    {"homeGroundFinal:customName", "Custom Name", "Grand Final Ground", 0x119, Type::Str, 32, 0, 0},
    {"homeGroundFinal:stadium", "Stadium", "Grand Final Ground", 0x139, Type::U8, 0, 0, 0},
    {"homeGroundFinal:stadiumId", "Custom Stadium", "Grand Final Ground", 0x13B, Type::U32, 0, 0, 0},
    {"homeGroundFinal:stadiumSharingId", "Stadium Sharing ID", "Grand Final Ground", 0x140, Type::U32, 0, 0, 0},
    {"homeGroundFinal:weatherZone", "Weather Zone", "Grand Final Ground", 0x13F, Type::U8, 0, 0, 0},
    {"homeGroundFinal:isOnDisk", "Is On Disk", "Grand Final Ground", 0x13A, Type::Bool, 0, 0, 0},
    {"feederClubs#0", "Feeder Club 1", "Feeder Clubs", 0x0C0, Type::U16, 0, 0, 0},
    {"feederClubs#1", "Feeder Club 2", "Feeder Clubs", 0x0C2, Type::U16, 0, 0, 0},
    {"feederClubs#2", "Feeder Club 3", "Feeder Clubs", 0x0C4, Type::U16, 0, 0, 0},
    {"feederClubs#3", "Feeder Club 4", "Feeder Clubs", 0x0C6, Type::U16, 0, 0, 0},
    {"feederClubs#4", "Feeder Club 5", "Feeder Clubs", 0x0C8, Type::U16, 0, 0, 0},
    {"feederClubs#5", "Feeder Club 6", "Feeder Clubs", 0x0CA, Type::U16, 0, 0, 0},
    {"feederClubs#6", "Feeder Club 7", "Feeder Clubs", 0x0CC, Type::U16, 0, 0, 0},
    {"fedFromClubs#0", "Fed From Club 1", "Feeder Clubs", 0x145, Type::U16, 0, 0, 0},
    {"fedFromClubs#1", "Fed From Club 2", "Feeder Clubs", 0x147, Type::U16, 0, 0, 0},
    {"fedFromClubs#2", "Fed From Club 3", "Feeder Clubs", 0x149, Type::U16, 0, 0, 0},
    {"fedFromClubs#3", "Fed From Club 4", "Feeder Clubs", 0x14B, Type::U16, 0, 0, 0},
    {"fedFromClubs#4", "Fed From Club 5", "Feeder Clubs", 0x14D, Type::U16, 0, 0, 0},
    {"fedFromClubs#5", "Fed From Club 6", "Feeder Clubs", 0x14F, Type::U16, 0, 0, 0},
    {"fedFromClubs#6", "Fed From Club 7", "Feeder Clubs", 0x151, Type::U16, 0, 0, 0},
};

// --- Drop downs -------------------------------------------------------------------------------------------------
// A field's list: the stored value is base + the name's position (the game's enums, checked against the Academy:
// Austria = 52 (country of birth), Canada = 2 (represents), Queensland = 197, Maori = 203, Winger = 1, Speed perk = 1).
struct ListDef {
    const char* key;
    const char* list;
    int base;
};
const ListDef kPlayerLists[] = {
    {"countryOfBirth", "countriesOfBirth", 0}, {"representativeCountry", "repCountries", 0},
    {"stateOfOrigin", "soo", 196}, {"cityVsCountry", "cvc", 199}, {"allStars", "allStars", 202},
    {"preferredRolePrimary", "roles", 0}, {"preferredRoleSecondary", "roles", 0}, {"preferredRoleTertiary", "roles", 0},
    {"perk", "perks", 0}, {"commentaryNameHash", "#surnames", 0},
};
const ListDef kTeamLists[] = {
    {"affiliations", "affiliations", 0}, {"supporters", "supporters", 0}, {"clubType", "clubTypes", 0},
    {"homeGrounds#0:stadium", "stadiums", 5}, {"homeGrounds#1:stadium", "stadiums", 5},
    {"homeGrounds#2:stadium", "stadiums", 5}, {"homeGroundFinal:stadium", "stadiums", 5},
    {"commentaryTeamLocationHash", "#location", 0}, {"commentaryTeamMascotHash", "#mascot", 0},
    {"logoNumber", "@logos", 0}, {"decalId", "@decals", 0},
    // A home ground's custom stadium (Levels\\LevelData.SAV): its level id, 0xFFFFFFFF = none.
    {"homeGrounds#0:stadiumId", "@levels", 0}, {"homeGrounds#1:stadiumId", "@levels", 0},
    {"homeGrounds#2:stadiumId", "@levels", 0}, {"homeGroundFinal:stadiumId", "@levels", 0},
};

QStringList namedList(const QString& name) {
    if (name == u"countriesOfBirth"_s) return Rl26Lists::countriesOfBirth();
    if (name == u"repCountries"_s) return Rl26Lists::repCountries();
    if (name == u"affiliations"_s) return Rl26Lists::affiliations();
    if (name == u"supporters"_s) return Rl26Lists::supporters();
    if (name == u"stadiums"_s) return Rl26Lists::stadiums();
    if (name == u"soo"_s) return {u"None"_s, u"Queensland"_s, u"New South Wales"_s};
    if (name == u"cvc"_s) return {u"None"_s, u"City"_s, u"Country"_s};
    if (name == u"allStars"_s) return {u"None"_s, u"Maori"_s, u"Indigenous"_s};
    if (name == u"roles"_s)
        return {u"Fullback"_s, u"Winger"_s, u"Centre"_s, u"Five-Eighth"_s, u"Halfback"_s, u"Prop"_s, u"Hooker"_s,
                u"Second Row"_s, u"Lock"_s};
    if (name == u"perks"_s)
        return {u"None"_s, u"Speed"_s, u"Stamina"_s, u"Contests"_s, u"Place Kicking"_s, u"In Play Kicking"_s};
    if (name == u"clubTypes"_s)
        return {u"League"_s, u"International"_s, u"State Of Origin"_s, u"City V's Country"_s, u"All Stars"_s};
    return {};
}

const ListDef* listFor(const QString& kind, const QString& key) {
    const auto find = [&key](const auto& defs) -> const ListDef* {
        for (const ListDef& d : defs)
            if (key == QLatin1StringView(d.key))
                return &d;
        return nullptr;
    };
    return kind == u"team"_s ? find(kTeamLists) : find(kPlayerLists);
}

QString titleCase(const QString& text) {
    QString out = text.toLower();
    if (!out.isEmpty())
        out[0] = out[0].toUpper();
    return out;
}

template <size_t N>
QList<const FieldDef*> toList(const FieldDef (&defs)[N]) {
    QList<const FieldDef*> out;
    for (const FieldDef& d : defs)
        out.append(&d);
    return out;
}

QList<const FieldDef*> defsFor(const QString& kind) {
    return kind == u"team"_s ? toList(kTeamFields) : toList(kPlayerFields);
}

const FieldDef* defFor(const QString& kind, const QString& key) {
    for (const FieldDef* d : defsFor(kind))
        if (key == QLatin1StringView(d->key))
            return d;
    return nullptr;
}

int typeSize(const FieldDef& d) {
    switch (d.type) {
    case Type::U8: case Type::Bool: return 1;
    case Type::U16: return 2;
    case Type::U32: return 4;
    case Type::Rgb: return 3;
    case Type::Str: return d.size;
    }
    return 0;
}

qint64 typeMax(const FieldDef& d) {
    if (d.max > 0)
        return d.max;
    switch (d.type) {
    case Type::U8: return 255;
    case Type::U16: return 65535;
    case Type::U32: return 4294967295LL;
    case Type::Bool: return 1;
    case Type::Rgb: return 0xFFFFFF;
    case Type::Str: return d.size - 1;
    }
    return 0;
}

QString localPath(const QString& pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

quint32 readU32(const QByteArray& b, qsizetype at) {
    return qFromLittleEndian<quint32>(b.constData() + at);
}
quint16 readU16(const QByteArray& b, qsizetype at) {
    return qFromLittleEndian<quint16>(b.constData() + at);
}

} // namespace

SaveEditorManager::SaveEditorManager(QObject* parent) : QObject(parent) {}

QString SaveEditorManager::fileName() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString SaveEditorManager::defaultFolder() const {
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(home).filePath(u"Saved Games/Rugby League 26/Profile"_s);
}

QString SaveEditorManager::defaultFile() const {
    return QDir(defaultFolder()).filePath(u"AcademyData000.SAV"_s);
}

void SaveEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void SaveEditorManager::edited() {
    if (m_rawBatch) {   // Raw Databases: once, after the whole table.
        m_rawEdited = true;
        return;
    }
    ++m_revision;
    emit revisionChanged();
    if (!m_unsaved) {
        m_unsaved = true;
        emit unsavedChangesChanged();
    }
}

bool SaveEditorManager::load(const QString& pathOrUrl) {
    const QString path = QDir::toNativeSeparators(pathOrUrl.isEmpty() ? defaultFile() : localPath(pathOrUrl));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Couldn't open %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    const Rl26Save::File file = Rl26Save::decode(f.readAll());
    if (!file.ok) {
        setStatus(false, file.error);
        return false;
    }

    m_data = file.data;
    m_path = path;
    m_version = file.version;
    m_sizeBias = file.sizeBias;
    m_checksumOk = file.checksumOk;
    m_backedUp = false;
    m_loaded = true;
    findRecords();
    if (m_unsaved) {
        m_unsaved = false;
        emit unsavedChangesChanged();
    }
    ++m_revision;
    emit loadedChanged();
    emit revisionChanged();

    QString text = u"Loaded %1: %2 custom players, %3 custom teams."_s.arg(fileName()).arg(m_players.size()).arg(m_teams.size());
    if (!m_checksumOk)
        text += u" The file's checksum doesn't match (it'll be right once saved)."_s;
    if (m_players.isEmpty() && m_teams.isEmpty())
        text += u" No custom players or teams found: is this AcademyData000.SAV?"_s;
    setStatus(true, text);
    return true;
}

void SaveEditorManager::findRecords() {
    m_players.clear();
    m_teams.clear();
    const QByteArray& b = m_data;

    // Players: the count, then the records. Check the ids look like custom player ids.
    auto playerOk = [&b](qsizetype at) {
        if (at < 0 || at + kPlayerSize > b.size())
            return false;
        const quint32 id = readU32(b, at);
        return id >= 20000 && id < 1000000;
    };
    qsizetype table = -1;
    int count = 0;
    if (b.size() >= kPlayerTable) {
        count = readU16(b, kPlayerCount);
        if (count == 0 || (count <= kPlayerMaxCount && playerOk(kPlayerTable)))
            table = kPlayerTable;
    }
    if (table < 0) {
        // Another layout (a different game version): the first record whose id and the count before it fit.
        for (qsizetype at = 2; at + kPlayerSize <= b.size(); ++at) {
            if (static_cast<quint8>(b.at(at + 0x0C)) != 0xC7 || !playerOk(at))
                continue;
            const int n = readU16(b, at - 2);
            if (n >= 1 && n <= kPlayerMaxCount && playerOk(at + (n - 1) * kPlayerSize)) {
                table = at;
                count = n;
                break;
            }
        }
    }
    if (table >= 0) {
        for (int i = 0; i < count; ++i) {
            const qsizetype at = table + i * kPlayerSize;
            if (!playerOk(at))
                break;
            m_players.append(at);
        }
    }

    // Teams: the used slots from the first.
    for (int i = 0; i < kTeamMaxCount; ++i) {
        const qsizetype at = kTeamTable + i * kTeamSize;
        if (at + kTeamSize > b.size())
            break;
        const quint16 id = readU16(b, at);
        if (id < 1000 || id >= 2000)
            break;
        m_teams.append(at);
    }
}

qsizetype SaveEditorManager::recordAt(const QString& kind, int index) const {
    const QList<qsizetype>& list = kind == u"team"_s ? m_teams : m_players;
    return index >= 0 && index < list.size() ? list.at(index) : -1;
}

int SaveEditorManager::recordSize(const QString& kind) const {
    return static_cast<int>(kind == u"team"_s ? kTeamSize : kPlayerSize);
}

QString SaveEditorManager::recordOffset(const QString& kind, int index) const {
    const qsizetype at = recordAt(kind, index);
    return at < 0 ? QString() : u"0x"_s + QString::number(at, 16).toUpper();
}

QStringList SaveEditorManager::groups(const QString& kind) const {
    QStringList out;
    for (const FieldDef* d : defsFor(kind)) {
        const QString g = QString::fromLatin1(d->group);
        if (!out.contains(g))
            out.append(g);
    }
    return out;
}

static QVariant readField(const QByteArray& b, qsizetype at, const FieldDef& d) {
    const qsizetype p = at + d.offset;
    switch (d.type) {
    case Type::U8: return static_cast<int>(static_cast<quint8>(b.at(p)));
    case Type::Bool: return b.at(p) != 0;
    case Type::U16: return static_cast<int>(readU16(b, p));
    case Type::U32: return static_cast<double>(readU32(b, p));
    case Type::Rgb:
        return u"#%1%2%3"_s.arg(static_cast<quint8>(b.at(p)), 2, 16, QChar(u'0'))
            .arg(static_cast<quint8>(b.at(p + 1)), 2, 16, QChar(u'0'))
            .arg(static_cast<quint8>(b.at(p + 2)), 2, 16, QChar(u'0'));
    case Type::Str: {
        const QByteArray raw = b.mid(p, d.size);
        const qsizetype end = raw.indexOf('\0');
        return QString::fromLatin1(end < 0 ? raw : raw.left(end));
    }
    }
    return {};
}

QVariantList SaveEditorManager::fields(const QString& kind, int index) const {
    QVariantList out;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return out;
    for (const FieldDef* d : defsFor(kind)) {
        QVariantMap m;
        m[u"key"_s] = QString::fromLatin1(d->key);
        m[u"label"_s] = QString::fromLatin1(d->label) + (d->probable ? u" (?)"_s : QString());
        m[u"group"_s] = QString::fromLatin1(d->group);
        m[u"kind"_s] = d->type == Type::Str ? u"text"_s : d->type == Type::Bool ? u"bool"_s
                     : d->type == Type::Rgb ? u"colour"_s : d->type == Type::U32 ? u"big"_s : u"number"_s;
        m[u"value"_s] = readField(m_data, at, *d);
        m[u"min"_s] = d->min;
        m[u"max"_s] = static_cast<double>(typeMax(*d));
        m[u"offset"_s] = u"+0x"_s + QString::number(d->offset, 16).toUpper();
        m[u"readOnly"_s] = d->readOnly;
        m[u"probable"_s] = d->probable;
        const QVariantList choices = choicesFor(kind, m[u"key"_s].toString(), m[u"value"_s]);
        if (!choices.isEmpty()) {
            m[u"kind"_s] = u"choice"_s;
            m[u"choices"_s] = choices;
        }
        out.append(m);
    }
    return out;
}

// The commentary name lists in the game's sound M3MP (sound\\dialogue.mempack.m3mp, 0x83AF3201ED1AE751):
// dialogue_wildcardhash_<list>: u32 count, then per name u32 hash, u32 length, the name. Empty when the blobset
// loaded in Blobset Tools isn't Rugby League 26's.
const SaveEditorManager::Commentary& SaveEditorManager::commentary(const QString& list) const {
    Commentary& c = m_commentary[list];
    if (c.loaded)
        return c;
    c.loaded = true;
    const QString hash = u"83AF3201ED1AE751"_s;
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hash, entry))
        return c;
    M3mpToolManager sound;
    sound.loadM3mpFromBlobset(hash);
    const QString file = list == u"location"_s ? u"teamslocation"_s : list == u"mascot"_s ? u"teamsmascot"_s : list;
    const QByteArray data = sound.entryDataByPath(u"sound/dialogue_wildcardhash_"_s + file);
    qsizetype pos = 0;
    const auto u32 = [&data, &pos](quint32& value) {
        if (pos + 4 > data.size())
            return false;
        value = qFromLittleEndian<quint32>(data.constData() + pos);
        pos += 4;
        return true;
    };
    quint32 count = 0;
    if (!u32(count))
        return c;
    for (quint32 i = 0; i < count; ++i) {
        quint32 h = 0, length = 0;
        if (!u32(h) || !u32(length) || pos + length > data.size())
            break;
        c.hashes << h;
        c.names << titleCase(QString::fromLatin1(data.mid(pos, length)));
        pos += length;
    }
    return c;
}

QVariantList SaveEditorManager::choicesFor(const QString& kind, const QString& key, const QVariant& value) const {
    QVariantList out;
    const ListDef* l = listFor(kind, key);
    if (!l)
        return out;
    const QString list = QString::fromLatin1(l->list);
    if (list.startsWith(u'#')) {
        const Commentary& c = commentary(list.mid(1));
        if (c.names.isEmpty())
            return out;   // Not available: the field stays a number.
        out.reserve(c.names.size() + 1);
        if (!c.hashes.contains(static_cast<quint32>(value.toDouble())))
            out << QVariantMap{{u"name"_s, u"(0x%1)"_s.arg(static_cast<quint32>(value.toDouble()), 8, 16, QChar(u'0')).toUpper()},
                               {u"value"_s, value.toDouble()}};
        for (int i = 0; i < c.names.size(); ++i)
            out << QVariantMap{{u"name"_s, c.names.at(i)}, {u"value"_s, static_cast<double>(c.hashes.at(i))}};
        return out;
    }
    if (list == u"@levels"_s || list == u"@decals"_s) {
        const bool levels = list == u"@levels"_s;
        const quint32 v = static_cast<quint32>(value.toDouble());
        const quint32 none = levels ? Rl26UserContent::kNoLevel : 0;
        const QList<Rl26UserContent::Item> items = levels ? Rl26UserContent::customStadiums(m_path)
                                                          : Rl26UserContent::decals(m_path);
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, static_cast<double>(none)}};
        bool found = v == none;
        QHash<QString, int> uses;
        for (const Rl26UserContent::Item& i : items)
            ++uses[i.name];
        for (const Rl26UserContent::Item& i : items) {
            found = found || i.id == v;
            out << QVariantMap{{u"name"_s, uses.value(i.name) > 1 ? u"%1 (%2)"_s.arg(i.name).arg(i.id) : i.name},
                               {u"value"_s, static_cast<double>(i.id)}};
        }
        if (!found)
            out.insert(1, QVariantMap{{u"name"_s, levels ? u"Custom Stadium"_s : u"Custom Logo"_s},
                                      {u"value"_s, static_cast<double>(v)}});
        return out;
    }
    const QStringList names = list == u"@logos"_s ? logoNames() : namedList(list);
    out.reserve(names.size() + 1);
    const int v = value.toInt();
    if (v < l->base || v >= l->base + names.size())
        out << QVariantMap{{u"name"_s, list == u"@logos"_s && v == 0xFFFF ? u"Custom Logo"_s : u"(%1)"_s.arg(v)}, {u"value"_s, v}};
    for (int i = 0; i < names.size(); ++i)
        out << QVariantMap{{u"name"_s, names.at(i)}, {u"value"_s, l->base + i}};
    return out;
}

bool SaveEditorManager::setField(const QString& kind, int index, const QString& key, const QVariant& value) {
    const qsizetype at = recordAt(kind, index);
    const FieldDef* d = defFor(kind, key);
    if (at < 0 || !d || d->readOnly)
        return false;
    const qsizetype p = at + d->offset;
    char* data = m_data.data();
    switch (d->type) {
    case Type::Str: {
        QByteArray text = value.toString().toLatin1().left(d->size - 1);
        // The game leaves old text after the terminating zero: clear the whole buffer.
        memset(data + p, 0, static_cast<size_t>(d->size));
        memcpy(data + p, text.constData(), static_cast<size_t>(text.size()));
        break;
    }
    case Type::Bool:
        data[p] = value.toBool() ? 1 : 0;
        break;
    case Type::Rgb: {
        QString hex = value.toString().trimmed();
        if (hex.startsWith(u'#'))
            hex.remove(0, 1);
        bool ok = false;
        const uint rgb = hex.left(6).toUInt(&ok, 16);
        if (!ok || hex.size() < 6)
            return false;
        data[p] = static_cast<char>(rgb >> 16);
        data[p + 1] = static_cast<char>(rgb >> 8);
        data[p + 2] = static_cast<char>(rgb);
        break;
    }
    default: {
        const qint64 v = qBound<qint64>(d->min, value.toLongLong(), typeMax(*d));
        if (d->type == Type::U8)
            data[p] = static_cast<char>(v);
        else if (d->type == Type::U16)
            qToLittleEndian<quint16>(static_cast<quint16>(v), data + p);
        else
            qToLittleEndian<quint32>(static_cast<quint32>(v), data + p);
        // The emblem is a custom logo or an icon: m_iconCategory / m_iconIndex 255 / 65535 with a custom logo (as
        // the game saves them), and picking an icon drops the custom logo.
        if (kind == u"team"_s && key == u"decalId"_s) {
            data[at + 0x10] = static_cast<char>(v != 0 ? 0xFF : 0);
            if (v != 0)
                qToLittleEndian<quint16>(0xFFFF, data + at + 0x11);
            else if (readU16(m_data, at + 0x11) == 0xFFFF)
                qToLittleEndian<quint16>(0, data + at + 0x11);
        } else if (kind == u"team"_s && key == u"logoNumber"_s) {
            qToLittleEndian<quint32>(0, data + at + 0x0C);
            data[at + 0x10] = 0;
        }
        break;
    }
    }
    edited();
    return true;
}

QVariantList SaveEditorManager::playerRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    for (int i = 0; i < m_players.size(); ++i) {
        const qsizetype at = m_players.at(i);
        QVariantMap row;
        const quint32 id = readU32(m_data, at);
        const QString first = readField(m_data, at, *defFor(u"player"_s, u"firstname"_s)).toString();
        const QString last = readField(m_data, at, *defFor(u"player"_s, u"surname"_s)).toString();
        if (!f.isEmpty() && !(first + u' ' + last).contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        row[u"index"_s] = i;
        row[u"id"_s] = static_cast<double>(id);
        row[u"first"_s] = first;
        row[u"last"_s] = last;
        row[u"shirt"_s] = readField(m_data, at, *defFor(u"player"_s, u"shirtName"_s)).toString();
        row[u"age"_s] = readField(m_data, at, *defFor(u"player"_s, u"age"_s)).toInt();
        row[u"female"_s] = readField(m_data, at, *defFor(u"player"_s, u"sex"_s)).toBool();
        row[u"role"_s] = namedList(u"roles"_s).value(readField(m_data, at, *defFor(u"player"_s, u"preferredRolePrimary"_s)).toInt());
        row[u"rating"_s] = playerRating(i);
        out.append(row);
    }
    return out;
}

QVariantList SaveEditorManager::teamRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    for (int i = 0; i < m_teams.size(); ++i) {
        const qsizetype at = m_teams.at(i);
        const int id = readU16(m_data, at);
        const QString full = readField(m_data, at, *defFor(u"team"_s, u"fullName"_s)).toString();
        if (!f.isEmpty() && !full.contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        QVariantMap row;
        row[u"index"_s] = i;
        row[u"id"_s] = id;
        row[u"fullName"_s] = full;
        row[u"shortName"_s] = readField(m_data, at, *defFor(u"team"_s, u"shortName"_s)).toString();
        row[u"abbreviation"_s] = readField(m_data, at, *defFor(u"team"_s, u"hudName"_s)).toString();
        row[u"primaryColour"_s] = readField(m_data, at, *defFor(u"team"_s, u"primaryRGBColour"_s)).toString();
        row[u"secondaryColour"_s] = readField(m_data, at, *defFor(u"team"_s, u"secondaryRGBColour"_s)).toString();
        row[u"female"_s] = readField(m_data, at, *defFor(u"team"_s, u"sex"_s)).toBool();
        row[u"logo"_s] = teamLogo(i);
        row[u"logoUrl"_s] = teamLogoUrl(i);
        out.append(row);
    }
    return out;
}

// The team logo names: games/Rugby League 26/pc/save_editor/logos.txt in Blobset Tools' folder, one name per line
// (the emblem value is the line's position, first line = 0; blank lines and "#" comments are skipped). Made with
// the built in list when it doesn't exist; read again when it changes.
QString SaveEditorManager::logosFile() const {
    return QDir(AppPaths::dataDir()).filePath(u"games/Rugby League 26/pc/save_editor/logos.txt"_s);
}

QStringList SaveEditorManager::logoNames() const {
    const QString path = logosFile();
    QFileInfo info(path);
    if (!info.exists()) {
        QDir().mkpath(info.absolutePath());
        QFile out(path);
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write(Rl26Lists::clubIcons().join(u'\n').toLatin1() + '\n');
        info.refresh();
    }
    const QDateTime stamp = info.exists() ? info.lastModified() : QDateTime();
    if (m_logosLoaded && stamp == m_logosStamp)
        return m_logos;
    m_logosLoaded = true;
    m_logosStamp = stamp;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logos = Rl26Lists::clubIcons();
        return m_logos;
    }
    m_logos.clear();
    for (const QByteArray& raw : in.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty() && !line.startsWith(u'#'))
            m_logos << line;
    }
    return m_logos;
}

QString SaveEditorManager::teamLogo(int team) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return {};
    if (const quint32 decal = readU32(m_data, at + 0x0C); decal != 0) {
        const QString name = Rl26UserContent::decalName(m_path, decal);
        return name.isEmpty() ? u"Custom Logo"_s : name;
    }
    const int v = readField(m_data, at, *defFor(u"team"_s, u"logoNumber"_s)).toInt();
    return logoNames().value(v);
}

// The logo from the loaded blobset (ui/club_logos/large_mirrored/<logo>.pc.dds), like the Database Editor's.
QString SaveEditorManager::teamLogoUrl(int team) const {
    // A custom logo (Decals\\DecalData.SAV), drawn.
    if (const qsizetype at = recordAt(u"team"_s, team); at >= 0)
        if (const quint32 decal = readU32(m_data, at + 0x0C); decal != 0)
            return Rl26UserContent::decalImageUrl(m_path, decal);
    const QString logo = teamLogo(team).toLower();
    if (logo.isEmpty())
        return {};
    if (const auto it = m_logoUrls.constFind(logo); it != m_logoUrls.constEnd())
        return it.value();
    QString url;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        const QString hash = orchestrator->getHashFromPath(u"ui/club_logos/large_mirrored/%1.pc.dds"_s.arg(logo));
        if (!hash.isEmpty() && hash.toULongLong(nullptr, 16) != 0)
            url = u"image://dds_provider/"_s + DdsImageProvider::makeId(hash, 0, {});
    }
    m_logoUrls.insert(logo, url);
    return url;
}

QVariantList SaveEditorManager::appearanceFields(int player) const {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return {};
    PaSerial::Blob blob;
    if (!blob.parse(Rl26Appearance::blobHeader() + m_data.mid(at + kAppearanceOffset, Rl26Appearance::kObjectSize)))
        return {};
    return blob.fields(blob.root(), 0);
}

bool SaveEditorManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return false;
    PaSerial::Blob blob;
    if (!blob.parse(Rl26Appearance::blobHeader() + m_data.mid(at + kAppearanceOffset, Rl26Appearance::kObjectSize))
        || !blob.setField(key, value) || blob.data().size() != Rl26Appearance::kObjectSize)
        return false;
    m_data.replace(at + kAppearanceOffset, Rl26Appearance::kObjectSize, blob.data());
    edited();
    return true;
}

int SaveEditorManager::playerRating(int player) const {
    const qsizetype at = recordAt(u"player"_s, player);
    if (at < 0)
        return 0;
    // The C# editor's names -> the save's fields.
    static const QHash<QString, QString> alias = {{u"durability"_s, u"injuryTolerance"_s},
                                                  {u"discipline"_s, u"aggression"_s},
                                                  {u"grubberKick"_s, u"groundKick"_s}};
    const auto& terms = Rl26Lists::ratingTerms();
    const int role = readField(m_data, at, *defFor(u"player"_s, u"preferredRolePrimary"_s)).toInt();
    if (role < 0 || role >= terms.size() || terms[role].isEmpty())
        return 0;
    int sum = 0;
    for (const Rl26Lists::RatingTerm& term : terms[role]) {
        const FieldDef* d = defFor(u"player"_s, alias.value(term.field, term.field));
        if (!d)
            return 0;
        sum += readField(m_data, at, *d).toInt() * term.mul / term.div;
    }
    return sum / static_cast<int>(terms[role].size());
}

// =================================================================================================================
// Roster
// =================================================================================================================

// The game's database players, for names / roles / ratings (Rugby League 26's blobset must be the one loaded).
void SaveEditorManager::loadDatabasePlayers() const {
    if (m_dbLoaded)
        return;
    m_dbLoaded = true;
    const QString hash = QString::fromLatin1(kDatabaseHash);
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hash, entry))
        return;
    M3mpToolManager m3mp;
    m3mp.loadM3mpFromBlobset(hash);
    BigAntDb::Table players;
    BigAntDb::Table clubs;
    if (!players.read(m3mp.entryDataByPath(u"defaultdata/defaultdata_players"_s)).isEmpty())
        return;
    QHash<int, QString> clubNames;
    if (clubs.read(m3mp.entryDataByPath(u"defaultdata/defaultdata_clubs"_s)).isEmpty())
        for (int r = 0; r < clubs.count(); ++r)
            clubNames.insert(clubs.num(r, u"id"_s, -1), clubs.str(r, u"fullName"_s));
    static const QHash<QString, QString> alias = {{u"durability"_s, u"injuryTolerance"_s},
                                                  {u"discipline"_s, u"aggression"_s},
                                                  {u"grubberKick"_s, u"groundKick"_s}};
    const auto& terms = Rl26Lists::ratingTerms();
    m_dbPlayers.reserve(players.count());
    for (int r = 0; r < players.count(); ++r) {
        DbPlayer p;
        p.id = players.num(r, u"id"_s, -1);
        if (p.id < 0)
            continue;
        p.name = (players.str(r, u"firstname"_s) + u' ' + players.str(r, u"surname"_s)).trimmed();
        p.role = players.num(r, u"preferredRolePrimary"_s, -1);
        p.first = players.str(r, u"firstname"_s);
        p.last = players.str(r, u"surname"_s);
        p.roles = QStringList{kRoleShort.value(p.role), kRoleShort.value(players.num(r, u"preferredRoleSecondary"_s, -1)),
                              kRoleShort.value(players.num(r, u"preferredRoleTertiary"_s, -1))}.join(u" / "_s);
        p.age = players.num(r, u"age"_s);
        p.club = clubNames.value(players.num(r, u"club"_s, -1));
        if (p.role >= 0 && p.role < terms.size() && !terms[p.role].isEmpty()) {
            int sum = 0;
            for (const Rl26Lists::RatingTerm& t : terms[p.role])
                sum += players.num(r, alias.value(t.field, t.field)) * t.mul / t.div;
            p.rating = sum / static_cast<int>(terms[p.role].size());
        }
        m_dbPlayerIndex.insert(p.id, static_cast<int>(m_dbPlayers.size()));
        m_dbPlayers.append(p);
    }
}

bool SaveEditorManager::databasePlayersAvailable() const {
    loadDatabasePlayers();
    return !m_dbPlayers.isEmpty();
}

// {id, name, role, rating, age, club, custom} for a player id (the save's own players or the game's).
QVariantMap SaveEditorManager::playerInfo(int id) const {
    QVariantMap m{{u"id"_s, id}, {u"custom"_s, id >= kFirstCustomPlayerId}, {u"role"_s, QString()}, {u"rating"_s, 0},
                  {u"age"_s, 0}, {u"club"_s, QString()}};
    if (id >= kFirstCustomPlayerId) {
        for (int i = 0; i < m_players.size(); ++i) {
            const qsizetype at = m_players.at(i);
            if (static_cast<int>(readU32(m_data, at)) != id)
                continue;
            m[u"first"_s] = readField(m_data, at, *defFor(u"player"_s, u"firstname"_s)).toString();
            m[u"last"_s] = readField(m_data, at, *defFor(u"player"_s, u"surname"_s)).toString();
            m[u"name"_s] = (m[u"first"_s].toString() + u' ' + m[u"last"_s].toString()).trimmed();
            m[u"roles"_s] = QStringList{kRoleShort.value(readField(m_data, at, *defFor(u"player"_s, u"preferredRolePrimary"_s)).toInt()),
                                        kRoleShort.value(readField(m_data, at, *defFor(u"player"_s, u"preferredRoleSecondary"_s)).toInt()),
                                        kRoleShort.value(readField(m_data, at, *defFor(u"player"_s, u"preferredRoleTertiary"_s)).toInt())}
                                .join(u" / "_s);
            m[u"known"_s] = true;
            m[u"role"_s] = kRoleShort.value(readField(m_data, at, *defFor(u"player"_s, u"preferredRolePrimary"_s)).toInt());
            m[u"rating"_s] = playerRating(i);
            m[u"age"_s] = readField(m_data, at, *defFor(u"player"_s, u"age"_s));
            m[u"club"_s] = u"Academy player"_s;
            m[u"playerIndex"_s] = i;
            return m;
        }
        m[u"name"_s] = u"(Academy player %1, not in this save)"_s.arg(id);
        m[u"last"_s] = m[u"name"_s];
        m[u"known"_s] = false;
        return m;
    }
    loadDatabasePlayers();
    const auto it = m_dbPlayerIndex.constFind(id);
    if (it == m_dbPlayerIndex.constEnd()) {
        m[u"name"_s] = id == 0 ? u"(empty)"_s : m_dbPlayers.isEmpty() ? u"Player %1"_s.arg(id) : u"(unknown player id %1)"_s.arg(id);
        m[u"last"_s] = m[u"name"_s];
        m[u"known"_s] = false;
        return m;
    }
    const DbPlayer& p = m_dbPlayers.at(it.value());
    m[u"name"_s] = p.name;
    m[u"first"_s] = p.first;
    m[u"last"_s] = p.last;
    m[u"roles"_s] = p.roles;
    m[u"known"_s] = true;
    m[u"role"_s] = kRoleShort.value(p.role);
    m[u"rating"_s] = p.rating;
    m[u"age"_s] = p.age;
    m[u"club"_s] = p.club;
    return m;
}

int SaveEditorManager::rosterMax() const { return kRosterMax; }
int SaveEditorManager::rosterMin() const { return kRosterMin; }

QVariantList SaveEditorManager::rosterRows(int team) const {
    QVariantList out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qMin<int>(readU16(m_data, at + kRosterOffset), kRosterMax);
    for (int i = 0; i < count; ++i) {
        QVariantMap m = playerInfo(static_cast<int>(readU32(m_data, at + kRosterOffset + 2 + 4 * i)));
        m[u"slot"_s] = i;
        out.append(m);
    }
    return out;
}

bool SaveEditorManager::addRosterPlayer(int team, int playerId) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || playerId <= 0)
        return false;
    const int count = readU16(m_data, at + kRosterOffset);
    if (count >= kRosterMax) {
        setStatus(false, u"The roster is full (%1 players, the game's most)."_s.arg(kRosterMax));
        return false;
    }
    for (int i = 0; i < count; ++i)
        if (static_cast<int>(readU32(m_data, at + kRosterOffset + 2 + 4 * i)) == playerId) {
            setStatus(false, u"That player is already in the roster."_s);
            return false;
        }
    qToLittleEndian<quint32>(static_cast<quint32>(playerId), m_data.data() + at + kRosterOffset + 2 + 4 * count);
    qToLittleEndian<quint16>(static_cast<quint16>(count + 1), m_data.data() + at + kRosterOffset);
    edited();
    return true;
}

bool SaveEditorManager::removeRosterPlayer(int team, int slot) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    const int count = readU16(m_data, at + kRosterOffset);
    if (slot < 0 || slot >= count || count > kRosterMax)
        return false;
    char* ids = m_data.data() + at + kRosterOffset + 2;
    memmove(ids + 4 * slot, ids + 4 * (slot + 1), static_cast<size_t>(4 * (count - slot - 1)));
    memset(ids + 4 * (count - 1), 0, 4);
    qToLittleEndian<quint16>(static_cast<quint16>(count - 1), m_data.data() + at + kRosterOffset);
    edited();
    return true;
}

bool SaveEditorManager::moveRosterPlayer(int team, int slot, int delta) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return false;
    const int count = readU16(m_data, at + kRosterOffset);
    const int to = slot + delta;
    if (slot < 0 || slot >= count || to < 0 || to >= count || count > kRosterMax)
        return false;
    char* ids = m_data.data() + at + kRosterOffset + 2;
    char tmp[4];
    memcpy(tmp, ids + 4 * slot, 4);
    memcpy(ids + 4 * slot, ids + 4 * to, 4);
    memcpy(ids + 4 * to, tmp, 4);
    edited();
    return true;
}

QVariantList SaveEditorManager::pickablePlayers(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    const auto matches = [&f](const QVariantMap& m) {
        return f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive)
            || m.value(u"club"_s).toString().contains(f, Qt::CaseInsensitive) || m.value(u"id"_s).toString() == f;
    };
    for (int i = 0; i < m_players.size(); ++i) {
        const QVariantMap m = playerInfo(static_cast<int>(readU32(m_data, m_players.at(i))));
        if (matches(m))
            out.append(m);
    }
    loadDatabasePlayers();
    for (const DbPlayer& p : std::as_const(m_dbPlayers)) {
        const QVariantMap m{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"first"_s, p.first}, {u"last"_s, p.last},
                            {u"role"_s, kRoleShort.value(p.role)}, {u"rating"_s, p.rating}, {u"age"_s, p.age},
                            {u"club"_s, p.club}, {u"custom"_s, false}};
        if (matches(m))
            out.append(m);
        if (out.size() >= 400)
            break;
    }
    return out;
}

// =================================================================================================================
// Lineups
// =================================================================================================================

namespace {
// The Database Editor's position names, by how many players the lineup has (its NRL 2026, League and Nines lists).
QStringList lineupPositions(int count) {
    static const QStringList league = {u"Fullback"_s, u"Left Wing"_s, u"Left Centre"_s, u"Right Centre"_s, u"Right Wing"_s,
                                       u"Five-Eighth"_s, u"Halfback"_s, u"Left Prop"_s, u"Hooker"_s, u"Right Prop"_s,
                                       u"Left Second Row"_s, u"Right Second Row"_s, u"Lock"_s};
    static const QStringList nines = {u"Fullback"_s, u"Left Wing"_s, u"Right Wing"_s, u"Halfback"_s, u"Five-Eighth"_s,
                                      u"Left Prop"_s, u"Hooker"_s, u"Left Second Row"_s, u"Right Second Row"_s};
    const QStringList& base = count == 14 ? nines : league;
    QStringList out;
    for (int i = 0; i < count; ++i)
        out << u"%1 - %2"_s.arg(i + 1).arg(i < base.size() ? base.at(i) : u"Interchange %1"_s.arg(i - base.size() + 1));
    return out;
}
// The byte after the name's flag: the lineup's rules (the game's own teams: League 17, Nines 14, NRL 2026 19).
QString lineupKind(int type) {
    return type == 0 ? u"League"_s : type == 1 ? u"Nines"_s : type == 2 ? u"NRL 2026"_s : u"type %1"_s.arg(type);
}
const QStringList kLineupRoleNames = {u"Captain"_s, u"Goal Kicker"_s, u"Play Maker 1"_s, u"Play Maker 2"_s};
} // namespace

qsizetype SaveEditorManager::lineupAt(int team, const QString& list) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || !list.startsWith(u'L'))
        return -1;
    const int i = list.mid(1).toInt();
    const int count = qMin<int>(readU16(m_data, at + kLineupsOffset), kLineupMax);
    return i >= 0 && i < count ? at + kLineupsOffset + 2 + qsizetype(i) * kLineupSize : -1;
}

QVariantList SaveEditorManager::matchLists(int team) const {
    QVariantList out;
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return out;
    const int count = qMin<int>(readU16(m_data, at + kLineupsOffset), kLineupMax);
    for (int i = 0; i < count; ++i) {
        const qsizetype l = at + kLineupsOffset + 2 + qsizetype(i) * kLineupSize;
        QByteArray name = m_data.mid(l + kLineupName, 16);
        if (name.indexOf('\0') >= 0)
            name.truncate(name.indexOf('\0'));
        out << QVariantMap{{u"list"_s, u"L%1"_s.arg(i)},
                           {u"title"_s, u"%1. %2  (%3)"_s.arg(i + 1).arg(QString::fromLatin1(name), lineupKind(static_cast<quint8>(m_data.at(l + kLineupName + 17))))},
                           {u"shirts"_s, true}};
    }
    return out;
}

bool SaveEditorManager::listPresent(int team, const QString& list) const {
    return lineupAt(team, list) >= 0;
}

bool SaveEditorManager::setListPresent(int team, const QString& list, bool on) {
    const qsizetype at = recordAt(u"team"_s, team);
    const qsizetype l = lineupAt(team, list);
    if (at < 0 || l < 0 || on)
        return false;
    const int count = readU16(m_data, at + kLineupsOffset);
    if (count <= 1) {
        setStatus(false, u"A team needs at least one lineup."_s);
        return false;
    }
    // Delete it: the later lineups move up, the last slot is cleared.
    const qsizetype end = at + kLineupsOffset + 2 + qsizetype(count) * kLineupSize;
    m_data.replace(l, end - kLineupSize - l, m_data.mid(l + kLineupSize, end - l - kLineupSize));
    m_data.replace(end - kLineupSize, kLineupSize, QByteArray(kLineupSize, '\0'));
    qToLittleEndian<quint16>(static_cast<quint16>(count - 1), m_data.data() + at + kLineupsOffset);
    edited();
    setStatus(true, u"Deleted the lineup."_s);
    return true;
}

QVariantList SaveEditorManager::lineupRows(int team, const QString& list) const {
    QVariantList rows;
    const qsizetype l = lineupAt(team, list);
    if (l < 0)
        return rows;
    const int count = qMin<int>(readU16(m_data, l), kLineupSlots);
    const QStringList positions = lineupPositions(count);
    for (int i = 0; i < count; ++i) {
        QVariantMap row = playerInfo(static_cast<int>(readU32(m_data, l + kLineupIds + 4 * i)));
        row[u"key"_s] = u"p:%1"_s.arg(i);
        row[u"position"_s] = positions.at(i);
        row[u"present"_s] = true;
        row[u"shirt"_s] = static_cast<int>(static_cast<quint8>(m_data.at(l + kLineupShirts + i)));
        rows << row;
    }
    return rows;
}

QVariantList SaveEditorManager::teamRoles(int team, const QString& list) const {
    QVariantList rows;
    const qsizetype l = lineupAt(team, list);
    if (l < 0)
        return rows;
    for (int r = 0; r < kLineupRoleNames.size(); ++r) {
        QVariantMap row = playerInfo(static_cast<int>(readU32(m_data, l + kLineupRoles + 4 * r)));
        row[u"key"_s] = u"r:%1"_s.arg(r);
        row[u"name"_s] = kLineupRoleNames.at(r);
        row[u"present"_s] = true;
        rows << row;
    }
    return rows;
}

bool SaveEditorManager::setLineupSlot(int team, const QString& list, const QString& key, int playerId) {
    const qsizetype l = lineupAt(team, list);
    const QStringList parts = key.split(u':');
    if (l < 0 || parts.size() != 2 || playerId < 0)
        return false;
    const int i = parts.at(1).toInt();
    qsizetype p = -1;
    if (parts.at(0) == u"p"_s && i >= 0 && i < qMin<int>(readU16(m_data, l), kLineupSlots))
        p = l + kLineupIds + 4 * i;
    else if (parts.at(0) == u"r"_s && i >= 0 && i < kLineupRoleNames.size())
        p = l + kLineupRoles + 4 * i;
    if (p < 0 || readU32(m_data, p) == static_cast<quint32>(playerId))
        return false;
    qToLittleEndian<quint32>(static_cast<quint32>(playerId), m_data.data() + p);
    edited();
    return true;
}

bool SaveEditorManager::setLineupShirt(int team, const QString& list, const QString& key, int shirt) {
    const qsizetype l = lineupAt(team, list);
    const QStringList parts = key.split(u':');
    if (l < 0 || parts.size() != 2 || parts.at(0) != u"p"_s || shirt < 0 || shirt > 255)
        return false;
    const int i = parts.at(1).toInt();
    if (i < 0 || i >= kLineupSlots || static_cast<quint8>(m_data.at(l + kLineupShirts + i)) == shirt)
        return false;
    m_data[l + kLineupShirts + i] = static_cast<char>(shirt);
    edited();
    return true;
}

QString SaveEditorManager::lineupName(int team, const QString& list) const {
    const qsizetype l = lineupAt(team, list);
    if (l < 0)
        return {};
    QByteArray name = m_data.mid(l + kLineupName, 16);
    if (name.indexOf('\0') >= 0)
        name.truncate(name.indexOf('\0'));
    return QString::fromLatin1(name);
}

bool SaveEditorManager::setLineupName(int team, const QString& list, const QString& name) {
    const qsizetype l = lineupAt(team, list);
    if (l < 0)
        return false;
    QByteArray text = name.trimmed().toLatin1().left(15);
    if (text.isEmpty())
        return false;
    text.resize(16, '\0');
    if (m_data.mid(l + kLineupName, 16) == text)
        return false;
    m_data.replace(l + kLineupName, 16, text);
    edited();
    return true;
}

int SaveEditorManager::addLineup(int team) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0)
        return -1;
    const int count = readU16(m_data, at + kLineupsOffset);
    if (count >= kLineupMax || count < 1) {
        setStatus(false, count < 1 ? u"This team has no lineup to copy (make one in the game first)."_s
                                   : u"This team already has %1 lineups, the most it can have."_s.arg(kLineupMax));
        return -1;
    }
    const qsizetype first = at + kLineupsOffset + 2;
    const qsizetype l = first + qsizetype(count) * kLineupSize;
    m_data.replace(l, kLineupSize, m_data.mid(first, kLineupSize));
    QByteArray name = (u"Lineup %1"_s.arg(count + 1)).toLatin1();
    name.resize(16, '\0');
    m_data.replace(l + kLineupName, 16, name);
    qToLittleEndian<quint16>(static_cast<quint16>(count + 1), m_data.data() + at + kLineupsOffset);
    edited();
    setStatus(true, u"Added a lineup (a copy of the first)."_s);
    return count;
}

QVariantList SaveEditorManager::pickerRows(const QString& filter, int team, bool squadOnly) const {
    QVariantList out;
    const auto toRow = [](const QVariantMap& m) {
        return QVariantMap{{u"id"_s, m.value(u"id"_s)}, {u"first"_s, m.value(u"first"_s)}, {u"last"_s, m.value(u"last"_s)},
                           {u"clubName"_s, m.value(u"club"_s)}, {u"roleName"_s, m.value(u"role"_s)},
                           {u"age"_s, m.value(u"age"_s)}, {u"rating"_s, m.value(u"rating"_s)}};
    };
    if (squadOnly) {
        const QString f = filter.trimmed();
        for (const QVariant& v : rosterRows(team)) {
            const QVariantMap m = v.toMap();
            if (f.isEmpty() || m.value(u"name"_s).toString().contains(f, Qt::CaseInsensitive) || m.value(u"id"_s).toString() == f)
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

namespace {
// ConvantClub = {u16 count, paUniform[20], u16 slot count, u8 slots[7]} (paUniform = name[25], config, isHidden).
struct KitLayout {
    int uniformType = -1;
    qint64 uniformSize = 0;
    qint64 countOffset = -1;
    qint64 arrayOffset = -1;
    int arraySize = 0;
    qint64 slotsOffset = -1;
    int slotCount = 0;
    qint64 nameOffset = -1;
    int nameLength = 0;
    qint64 hiddenOffset = -1;
    bool ok = false;
};

KitLayout kitLayout(const PaSerial::Blob& blob) {
    KitLayout k;
    if (!blob.isValid())
        return k;
    const QList<PaSerial::Member>& m = blob.types()[blob.root()].members;
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
        }
    }
    k.ok = true;
    return k;
}
} // namespace

bool SaveEditorManager::kitBlob(int team, PaSerial::Blob& out) const {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || at + Rl26Kit::kOffset + Rl26Kit::kObjectSize > m_data.size())
        return false;
    return out.parse(Rl26Kit::blobHeader() + m_data.mid(at + Rl26Kit::kOffset, Rl26Kit::kObjectSize))
        && out.data().size() == Rl26Kit::kObjectSize;
}

bool SaveEditorManager::writeKit(int team, const QByteArray& object) {
    const qsizetype at = recordAt(u"team"_s, team);
    if (at < 0 || object.size() != Rl26Kit::kObjectSize)
        return false;
    if (m_data.mid(at + Rl26Kit::kOffset, Rl26Kit::kObjectSize) == object)
        return false;
    m_data.replace(at + Rl26Kit::kOffset, Rl26Kit::kObjectSize, object);
    edited();
    return true;
}

QVariantMap SaveEditorManager::jerseys(int team) const {
    QVariantMap out{{u"ok"_s, false}};
    PaSerial::Blob blob;
    if (!kitBlob(team, blob)) {
        out.insert(u"error"_s, u"This team's jerseys can't be read."_s);
        return out;
    }
    const KitLayout k = kitLayout(blob);
    if (!k.ok) {
        out.insert(u"error"_s, u"The jersey data has a layout this editor doesn't know."_s);
        return out;
    }
    const int count = qMin<int>(k.arraySize, static_cast<int>(blob.readUInt(k.countOffset, 2)));
    QVariantList uniforms;
    for (int u = 0; u < count; ++u) {
        const qint64 base = k.arrayOffset + qint64(u) * k.uniformSize;
        QByteArray name = k.nameOffset >= 0 ? blob.data().mid(base + k.nameOffset, k.nameLength) : QByteArray();
        if (name.indexOf('\0') >= 0)
            name.truncate(name.indexOf('\0'));
        uniforms << QVariantMap{{u"index"_s, u}, {u"name"_s, QString::fromLatin1(name)},
                                {u"hidden"_s, k.hiddenOffset >= 0 && blob.readUInt(base + k.hiddenOffset, 1) != 0}};
    }
    QVariantList kitSlots;
    for (int s = 0; k.slotsOffset >= 0 && s < k.slotCount; ++s)
        kitSlots << QVariantMap{{u"slot"_s, s}, {u"label"_s, kKitSlotNames.value(s, u"Slot %1"_s.arg(s + 1))},
                                {u"uniform"_s, static_cast<int>(blob.readUInt(k.slotsOffset + s, 1))}};
    out.insert(u"ok"_s, true);
    out.insert(u"count"_s, count);
    out.insert(u"max"_s, k.arraySize);
    out.insert(u"uniforms"_s, uniforms);
    out.insert(u"slots"_s, kitSlots);
    return out;
}

QVariantList SaveEditorManager::jerseyFields(int team, int uniform) const {
    PaSerial::Blob blob;
    if (!kitBlob(team, blob))
        return {};
    const KitLayout k = kitLayout(blob);
    if (!k.ok || uniform < 0 || uniform >= k.arraySize)
        return {};
    return blob.fields(k.uniformType, k.arrayOffset + qint64(uniform) * k.uniformSize);
}

bool SaveEditorManager::setJerseyField(int team, const QString& key, const QVariant& value) {
    PaSerial::Blob blob;   // The key carries the field's offset in the whole jersey object.
    if (!kitBlob(team, blob) || !blob.setField(key, value))
        return false;
    return writeKit(team, blob.data());
}

bool SaveEditorManager::setJerseySlot(int team, int slot, int uniform) {
    PaSerial::Blob blob;
    if (!kitBlob(team, blob))
        return false;
    const KitLayout k = kitLayout(blob);
    if (!k.ok || k.slotsOffset < 0 || slot < 0 || slot >= k.slotCount || uniform < 0 || uniform > 255)
        return false;
    blob.writeUInt(k.slotsOffset + slot, 1, uniform);
    return writeKit(team, blob.data());
}

int SaveEditorManager::addJersey(int team, int from) {
    PaSerial::Blob blob;
    if (!kitBlob(team, blob))
        return -1;
    const KitLayout k = kitLayout(blob);
    if (!k.ok)
        return -1;
    const int count = static_cast<int>(blob.readUInt(k.countOffset, 2));
    if (count >= k.arraySize) {
        setStatus(false, u"This team already has %1 jerseys, the most it can have."_s.arg(k.arraySize));
        return -1;
    }
    if (from < 0 || from >= count)
        from = 0;
    const QByteArray source = blob.data().mid(k.arrayOffset + qint64(from) * k.uniformSize, k.uniformSize);
    blob.data().replace(k.arrayOffset + qint64(count) * k.uniformSize, k.uniformSize, source);
    if (k.nameOffset >= 0) {
        QByteArray name = (u"Jersey %1"_s.arg(count + 1)).toLatin1().left(k.nameLength - 1);
        name.resize(k.nameLength, '\0');
        blob.data().replace(k.arrayOffset + qint64(count) * k.uniformSize + k.nameOffset, k.nameLength, name);
    }
    blob.writeUInt(k.countOffset, 2, count + 1);
    writeKit(team, blob.data());
    setStatus(true, u"Added a jersey (a copy of jersey %1)."_s.arg(from + 1));
    return count;
}

bool SaveEditorManager::removeJersey(int team, int uniform) {
    PaSerial::Blob blob;
    if (!kitBlob(team, blob))
        return false;
    const KitLayout k = kitLayout(blob);
    const int count = k.ok ? static_cast<int>(blob.readUInt(k.countOffset, 2)) : 0;
    if (count <= 1 || uniform < 0 || uniform >= count) {
        setStatus(false, u"A team needs at least one jersey."_s);
        return false;
    }
    for (int u = uniform; u + 1 < count; ++u)
        blob.data().replace(k.arrayOffset + qint64(u) * k.uniformSize, k.uniformSize,
                            blob.data().mid(k.arrayOffset + qint64(u + 1) * k.uniformSize, k.uniformSize));
    blob.data().replace(k.arrayOffset + qint64(count - 1) * k.uniformSize, k.uniformSize, QByteArray(k.uniformSize, '\0'));
    blob.writeUInt(k.countOffset, 2, count - 1);
    // Slots that used the removed jersey go to the first; later ones move down.
    for (int s = 0; k.slotsOffset >= 0 && s < k.slotCount; ++s) {
        const int v = static_cast<int>(blob.readUInt(k.slotsOffset + s, 1));
        if (v == uniform)
            blob.writeUInt(k.slotsOffset + s, 1, 0);
        else if (v > uniform)
            blob.writeUInt(k.slotsOffset + s, 1, v - 1);
    }
    return writeKit(team, blob.data());
}

QVariantList SaveEditorManager::rawRows(const QString& kind, int index) const {
    QVariantList out;
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return out;
    const int size = recordSize(kind);
    QVector<bool> known(size, false);
    for (const FieldDef* d : defsFor(kind))
        for (int i = 0; i < typeSize(*d) && d->offset + i < size; ++i)
            known[d->offset + i] = true;
    if (kind != u"team"_s) {
        for (int i = 0; i < Rl26Appearance::kObjectSize; ++i)
            known[kAppearanceOffset + i] = true;
    } else {
        for (int i = 0; i < Rl26Kit::kObjectSize; ++i)
            known[Rl26Kit::kOffset + i] = true;
        for (int i = 0; i < 2 + 4 * kRosterMax; ++i)
            known[kRosterOffset + i] = true;
        for (int i = 0; i < 2 + kLineupSize * kLineupMax; ++i)
            known[kLineupsOffset + i] = true;
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
        QVariantMap m;
        m[u"offset"_s] = row;
        m[u"hex"_s] = hex;
        m[u"known"_s] = marks;
        m[u"text"_s] = text;
        out.append(m);
    }
    return out;
}

bool SaveEditorManager::setRawByte(const QString& kind, int index, int offset, int value) {
    const qsizetype at = recordAt(kind, index);
    if (at < 0 || offset < 0 || offset >= recordSize(kind) || value < 0 || value > 255)
        return false;
    if (static_cast<quint8>(m_data.at(at + offset)) == value)
        return true;
    m_data[at + offset] = static_cast<char>(value);
    edited();
    return true;
}

QString SaveEditorManager::exportRecord(const QString& kind, int index, const QString& pathOrUrl) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return u"Nothing to export."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data.mid(at, recordSize(kind))) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

QString SaveEditorManager::exportDecoded(const QString& pathOrUrl) const {
    if (!m_loaded)
        return u"Nothing loaded."_s;
    QSaveFile f(localPath(pathOrUrl));
    if (!f.open(QIODevice::WriteOnly) || f.write(m_data) < 0 || !f.commit())
        return u"Couldn't write %1: %2"_s.arg(localPath(pathOrUrl), f.errorString());
    return {};
}

bool SaveEditorManager::writeTo(const QString& path) {
    const QByteArray bytes = Rl26Save::encode(m_data, m_version, m_sizeBias);
    // Check it reads back as written before replacing anything.
    const Rl26Save::File check = Rl26Save::decode(bytes);
    if (!check.ok || !check.checksumOk || check.data != m_data) {
        setStatus(false, u"The save didn't encode correctly; nothing was written."_s);
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        setStatus(false, u"Couldn't write %1: %2"_s.arg(path, f.errorString()));
        return false;
    }
    return true;
}

bool SaveEditorManager::save() {
    if (!m_loaded)
        return false;
    // Keep the original the first time this file is saved (once: a later save doesn't replace the .bak).
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
    if (!writeTo(m_path))
        return false;
    m_checksumOk = true;
    m_unsaved = false;
    emit unsavedChangesChanged();
    emit loadedChanged();
    setStatus(true, u"Saved %1.%2"_s.arg(fileName(), backupNote));
    return true;
}

bool SaveEditorManager::saveAs(const QString& pathOrUrl) {
    if (!m_loaded)
        return false;
    const QString path = QDir::toNativeSeparators(localPath(pathOrUrl));
    if (!writeTo(path))
        return false;
    m_path = path;
    m_backedUp = true;   // A new file: nothing to keep.
    m_checksumOk = true;
    m_unsaved = false;
    emit unsavedChangesChanged();
    emit loadedChanged();
    setStatus(true, u"Saved as %1."_s.arg(path));
    return true;
}

// =================================================================================================================
// Raw Databases (like the C# editors' Raw ... windows: every record a row, edited as text, CSV in / out)
// =================================================================================================================
//
// Rugby League 26's own editors had none; these are the mapped fields (players, teams) and the rosters. A column's
// key is the field's ("|r" / "|g" / "|b": a colour's part), "#index", "#rosterCount" or "#roster<n>".

namespace {

QList<RawColumn> rl26SaveRawColumns(const QString& id) {
    QList<RawColumn> cols;
    cols << RawColumn{id == u"players"_s ? u"Player Index"_s : u"Team Index"_s, u"#index"_s, true};
    const auto add = [&cols](const FieldDef* d, bool readOnly) {
        const QString label = QString::fromLatin1(d->label);
        if (d->type == Type::Rgb) {
            for (const char* part : {"R", "G", "B"})
                cols << RawColumn{label + u' ' + QLatin1StringView(part), QString::fromLatin1(d->key) + u'|'
                                                                             + QString::fromLatin1(part).toLower(), readOnly};
            return;
        }
        cols << RawColumn{label, QString::fromLatin1(d->key), readOnly};
    };
    if (id == u"players"_s || id == u"teams"_s) {
        for (const FieldDef* d : defsFor(id == u"players"_s ? u"player"_s : u"team"_s))
            add(d, d->readOnly);
    } else if (id == u"teamPlayers"_s) {
        for (const char* key : {"id", "fullName"})
            if (const FieldDef* d = defFor(u"team"_s, QString::fromLatin1(key)))
                add(d, true);
        cols << RawColumn{u"Team Player Amount"_s, u"#rosterCount"_s};
        for (int j = 0; j < kRosterMax; ++j)
            cols << RawColumn{u"Player %1"_s.arg(j + 1), u"#roster%1"_s.arg(j)};
    } else {
        return {};
    }
    QHash<QString, int> seen;
    for (RawColumn& c : cols)
        if (seen[c.name.toLower()]++ > 0)
            c.name += u" ("_s + c.key + u')';
    return cols;
}

} // namespace

QVariantList SaveEditorManager::rawTables() const {
    const auto t = [](const char* id, const char* title) {
        return QVariantMap{{u"id"_s, QString::fromLatin1(id)}, {u"title"_s, QString::fromLatin1(title)}, {u"repair"_s, false}};
    };
    return {t("players", "Raw Player Database"), t("teams", "Raw Team Database"), t("teamPlayers", "Raw Team Players")};
}

QString SaveEditorManager::rawGet(const QString& kind, int index, const QString& key) const {
    const qsizetype at = recordAt(kind, index);
    if (at < 0)
        return {};
    if (key == u"#index"_s)
        return QString::number(index);
    if (key == u"#rosterCount"_s)
        return QString::number(readU16(m_data, at + kRosterOffset));
    if (key.startsWith(u"#roster"_s))
        return QString::number(readU32(m_data, at + kRosterOffset + 2 + 4 * key.mid(7).toInt()));
    const qsizetype bar = key.indexOf(u'|');
    const FieldDef* d = defFor(kind, bar < 0 ? key : key.left(bar));
    if (!d)
        return {};
    if (bar >= 0) {
        const int part = QStringLiteral("rgb").indexOf(key.back());
        return QString::number(static_cast<quint8>(m_data.at(at + d->offset + qMax(0, part))));
    }
    const QVariant v = readField(m_data, at, *d);
    if (d->type == Type::Bool)
        return RawTableModel::boolText(v.toBool());
    if (d->type == Type::U32)
        return QString::number(static_cast<quint32>(v.toDouble()));
    return v.toString();
}

bool SaveEditorManager::rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error) {
    const qsizetype at = recordAt(kind, index);
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
    if (key == u"#rosterCount"_s) {
        if (!number(0, kRosterMax))
            return false;
        qToLittleEndian<quint16>(static_cast<quint16>(v), m_data.data() + at + kRosterOffset);
        edited();
        return true;
    }
    if (key.startsWith(u"#roster"_s)) {
        if (!number(0, 0xFFFFFFFFLL))
            return false;
        qToLittleEndian<quint32>(static_cast<quint32>(v), m_data.data() + at + kRosterOffset + 2 + 4 * key.mid(7).toInt());
        edited();
        return true;
    }
    const qsizetype bar = key.indexOf(u'|');
    const QString name = bar < 0 ? key : key.left(bar);
    const FieldDef* d = defFor(kind, name);
    if (!d || d->readOnly)
        return false;
    if (bar >= 0) {
        if (!number(0, 255))
            return false;
        const int part = QStringLiteral("rgb").indexOf(key.back());
        QString hex = readField(m_data, at, *d).toString();
        bool okHex = false;
        uint rgb = hex.mid(1).toUInt(&okHex, 16);
        const int shift = part == 0 ? 16 : part == 1 ? 8 : 0;
        rgb = (rgb & ~(0xFFu << shift)) | (uint(v) << shift);
        return setField(kind, index, name, u"#%1"_s.arg(rgb, 6, 16, QChar(u'0')));
    }
    switch (d->type) {
    case Type::Str: return setField(kind, index, name, text);
    case Type::Bool: {
        const bool on = RawTableModel::parseBool(text, &ok);
        if (!ok) {
            *error = u"\"%1\" isn't True or False"_s.arg(text);
            return false;
        }
        return setField(kind, index, name, on);
    }
    default:
        if (!number(d->min, typeMax(*d)))
            return false;
        return setField(kind, index, name, v);
    }
}

bool SaveEditorManager::fillRawTable(const QString& id, RawTableModel* model) const {
    if (!model || !m_loaded)
        return false;
    const QList<RawColumn> cols = rl26SaveRawColumns(id);
    if (cols.isEmpty())
        return false;
    const QString kind = id == u"players"_s ? u"player"_s : u"team"_s;
    const int count = kind == u"team"_s ? teamCount() : playerCount();
    QList<QStringList> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        QStringList row;
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

QString SaveEditorManager::applyRawTable(RawTableModel* model) {
    if (!model || !m_loaded)
        return u"Nothing is loaded."_s;
    const QString id = model->tableId();
    const QString kind = id == u"players"_s ? u"player"_s : u"team"_s;
    const int count = kind == u"team"_s ? teamCount() : playerCount();
    int changed = 0;
    // One refresh at the end, not one per cell.
    m_rawBatch = true;
    m_rawEdited = false;
    const QString errors = model->apply(
        [model, count](int r) { return model->recordOfRow(r, count); },
        [this, &kind](int rec, const QString& key) { return rawGet(kind, rec, key); },
        [this, &kind](int rec, const QString& key, const QString& text, QString* error) {
            return rawSet(kind, rec, key, text, error);
        },
        &changed);
    m_rawBatch = false;
    if (m_rawEdited)
        edited();
    fillRawTable(id, model);
    if (!errors.isEmpty())
        return errors;
    setStatus(true, changed > 0 ? u"Saved %1 changes from %2."_s.arg(changed).arg(model->title())
                                : u"Nothing changed in %1."_s.arg(model->title()));
    return {};
}

int SaveEditorManager::repairRawIds(RawTableModel*) const { return 0; }
