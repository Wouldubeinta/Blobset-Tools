/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include "rl26_career_editor_manager.h"

#include "app_orchestrator.h"
#include "app_paths.h"
#include "blobset_entry_registry.h"
#include "career_match_stats.h"
#include "dds_image_provider.h"
#include "m3mp_tool_manager.h"
#include "rl26_lists.h"
#include "rl26_save.h"
#include "rl26_user_content.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

constexpr quint32 kMagic = 0x1234EDBCu;

struct NameEntry {
    quint32 hash;
    const char* name;
};
constexpr NameEntry kNames[] = {
#include "rl26_career_names.inc"
};

const QHash<quint32, QString>& nameTable() {
    static const QHash<quint32, QString> table = [] {
        QHash<quint32, QString> t;
        for (const NameEntry& e : std::as_const(kNames))
            t.insert(e.hash, QString::fromLatin1(e.name));
        return t;
    }();
    return table;
}

int builtinSize(quint16 kind) {
    switch (kind) {
    case 0xFDE8: case 0xFDE9: case 0xFDEA: case 0xFDEB: case 0xFFFF: return 1;
    case 0xFDEC: case 0xFDED: return 2;
    case 0xFDEE: case 0xFDEF: case 0xFDF2: return 4;
    case 0xFDF0: case 0xFDF1: case 0xFDF3: return 8;
    default: return 0;
    }
}
bool builtinSigned(quint16 kind) {
    return kind == 0xFDE9 || kind == 0xFDEA || kind == 0xFDEC || kind == 0xFDEE || kind == 0xFDF0;
}

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

// "m_averageMatchAttendanceTotal" -> "Average Match Attendance Total"
QString pretty(QString name) {
    if (name.startsWith(u"m_"_s))
        name = name.mid(2);
    QString out;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (c == u'_' || c == u'.') {
            out += u' ';
            continue;
        }
        if (i > 0 && c.isUpper() && !name.at(i - 1).isUpper() && name.at(i - 1) != u'_')
            out += u' ';
        else if (i > 0 && c.isDigit() && !name.at(i - 1).isDigit())
            out += u' ';
        out += i == 0 || (out.endsWith(u' ')) ? c.toUpper() : c;
    }
    return out.simplified();
}

const QStringList kRoles = {u"Fullback"_s, u"Winger"_s, u"Centre"_s, u"Five-Eighth"_s, u"Halfback"_s, u"Prop"_s,
                            u"Hooker"_s, u"Second Row"_s, u"Lock"_s};
const QStringList kRoleShort = {u"FB"_s, u"WG"_s, u"C"_s, u"FE"_s, u"HB"_s, u"FR"_s, u"HK"_s, u"SR"_s, u"L"_s};
const QStringList kStatusReasons = {u"Available"_s, u"Injured"_s, u"Suspended"_s, u"Retired"_s, u"NSW Origin Camp"_s,
                                    u"QLD Origin Camp"_s};
const QStringList kLineupRoles = {u"Captain"_s, u"Goal Kicker"_s, u"Play Maker 1"_s, u"Play Maker 2"_s, u"Role 5"_s};

// The competitions' logos (ui\logos_competition\large\<name>.pc.dds), by the save's competition names.
QString competitionLogoName(const QString& comp) {
    static const QList<QPair<QString, QString>> map = {
        {u"StateOfOriginWomens"_s, u"stateoforigin_w"_s}, {u"StateOfOrigin"_s, u"stateoforigin"_s},
        {u"WorldClubChallenge"_s, u"world_club_challenge"_s}, {u"WomensChallengeCup"_s, u"womens_challenge_cup"_s},
        {u"ChallengeCup"_s, u"challengecup"_s}, {u"SuperLeague"_s, u"superleague"_s}, {u"WSL"_s, u"superleague_w"_s},
        {u"ChampionshipQualifiers"_s, u"championship_qualifiers"_s}, {u"BetfredLeague1"_s, u"betfred_league1"_s},
        {u"Championship"_s, u"betfred_championship"_s}, {u"QLDCup"_s, u"qld_cup"_s}, {u"NRLW"_s, u"nrlw"_s},
        {u"NRL"_s, u"nrl"_s}, {u"Cup"_s, u"1895_cup"_s}, {u"IndigenousVsMaori"_s, u"all_stars"_s},
        {u"Ashes"_s, u"ashes"_s}};
    for (const auto& [key, logo] : std::as_const(map))
        if (comp.contains(key))
            return logo;
    return u"custom"_s;
}

} // namespace

Rl26CareerEditorManager::Rl26CareerEditorManager(QObject* parent) : QObject(parent) {}

quint32 Rl26CareerEditorManager::fnv(const QByteArray& text) {
    quint32 h = 0x811c9dc5u;
    for (const char c : std::as_const(text)) {
        h ^= static_cast<quint8>(c);
        h *= 0x01000193u;
    }
    return h;
}

QString Rl26CareerEditorManager::fileName() const {
    return QFileInfo(m_path).fileName();
}

QString Rl26CareerEditorManager::careerInfo() const {
    if (!m_loaded)
        return {};
    return u"%1 season  ·  %2 clubs  ·  %3 players"_s.arg(m_careerYear).arg(m_clubs.size()).arg(m_players.size());
}

void Rl26CareerEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void Rl26CareerEditorManager::edited() {
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

// =================================================================================================================
// Loading / saving
// =================================================================================================================

QString Rl26CareerEditorManager::defaultFolder() const {
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(home).filePath(u"Saved Games/Rugby League 26/Career000"_s);
}

QString Rl26CareerEditorManager::defaultFile() const {
    return QDir(defaultFolder()).filePath(u"CareerData.SAV"_s);
}

bool Rl26CareerEditorManager::load(const QString& pathOrUrl) {
    QString path = toLocalPath(pathOrUrl);
    if (path.isEmpty())
        path = defaultFile();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    const Rl26Save::File f = Rl26Save::decode(file.readAll());
    if (!f.ok) {
        setStatus(false, u"%1 isn't a Rugby League 26 save (%2)."_s.arg(QFileInfo(path).fileName(), f.error));
        return false;
    }
    const QByteArray before = m_data;
    const QString pathBefore = m_path;
    m_version = f.version;
    m_sizeBias = f.sizeBias;
    m_checksumOk = f.checksumOk;
    m_path = path;
    if (!loadDecoded(f.data)) {
        m_path = pathBefore;
        if (!before.isEmpty())
            loadDecoded(before);
        return false;
    }
    setStatus(true, u"Loaded %1: %2"_s.arg(QFileInfo(path).fileName(), careerInfo()));
    return true;
}

bool Rl26CareerEditorManager::loadDecoded(const QByteArray& decoded) {
    m_data = decoded;
    if (!parse()) {
        m_loaded = false;
        emit loadedChanged();
        return false;
    }
    index();
    buildDefs();
    buildStages();
    m_loaded = true;
    m_unsaved = false;
    ++m_revision;
    emit loadedChanged();
    emit unsavedChangesChanged();
    emit revisionChanged();
    if (m_path.isEmpty())
        setStatus(true, careerInfo());
    return true;
}

bool Rl26CareerEditorManager::save() {
    return m_loaded && saveAs(m_path);
}

bool Rl26CareerEditorManager::saveAs(const QString& pathOrUrl) {
    if (!m_loaded)
        return false;
    const QString path = toLocalPath(pathOrUrl);
    // A backup of the game's file the first time it's overwritten (CareerData.SAV.bak).
    if (QFileInfo(path).isFile() && !QFileInfo::exists(path + u".bak"_s))
        QFile::copy(path, path + u".bak"_s);
    const QByteArray bytes = Rl26Save::encode(m_data, m_version, m_sizeBias);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return false;
    }
    m_path = path;
    m_unsaved = false;
    emit unsavedChangesChanged();
    emit loadedChanged();
    setStatus(true, u"Saved %1."_s.arg(QDir::toNativeSeparators(path)));
    return true;
}

QString Rl26CareerEditorManager::exportDecoded(const QString& pathOrUrl) const {
    QFile out(toLocalPath(pathOrUrl));
    if (!out.open(QIODevice::WriteOnly) || out.write(m_data) != m_data.size())
        return out.errorString();
    return {};
}

// The type table: u32 magic, u16 version, u16 types; per type u32 hash + u16 index; per type u16 members, u32 size,
// per member u32 hash, u32 offset, u16 kind, u16 count. The root object's bytes follow.
bool Rl26CareerEditorManager::parse() {
    m_types.clear();
    const auto u16 = [this](qsizetype at) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(m_data.constData()) + at); };
    const auto u32 = [this](qsizetype at) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(m_data.constData()) + at); };
    if (m_data.size() < 8 || u32(0) != kMagic) {
        setStatus(false, u"This isn't a Rugby League 26 career save (no paSerial data)."_s);
        return false;
    }
    const int count = u16(6);
    qsizetype p = 8 + 6 * qsizetype(count);
    for (int t = 0; t < count; ++t) {
        if (p + 6 > m_data.size())
            return false;
        Type type;
        const int members = u16(p);
        type.size = u32(p + 2);
        p += 6;
        if (p + 12 * qsizetype(members) > m_data.size())
            return false;
        for (int m = 0; m < members; ++m, p += 12) {
            Member mem;
            mem.hash = u32(p);
            mem.off = u32(p + 4);
            mem.kind = u16(p + 8);
            mem.count = u16(p + 10);
            mem.name = nameTable().value(mem.hash);
            if (mem.kind < count) {
                mem.type = mem.kind;
                mem.size = 0;   // Set below (the type may come later).
            } else {
                mem.size = builtinSize(mem.kind);
            }
            type.byHash.insert(mem.hash, int(type.members.size()));
            type.members << mem;
        }
        m_types << type;
    }
    for (Type& t : m_types)
        for (Member& m : t.members)
            if (m.type >= 0)
                m.size = int(m_types.at(m.type).size);
    m_rootAt = p;
    m_root = -1;
    for (int t = 0; t < m_types.size(); ++t)
        if (qsizetype(m_types.at(t).size) == m_data.size() - p) {
            m_root = t;
            break;
        }
    if (m_root < 0) {
        setStatus(false, u"The career save's data doesn't match its type table."_s);
        return false;
    }
    return true;
}

const Rl26CareerEditorManager::Member* Rl26CareerEditorManager::member(int type, quint32 hash) const {
    if (type < 0 || type >= m_types.size())
        return nullptr;
    const auto it = m_types.at(type).byHash.constFind(hash);
    return it == m_types.at(type).byHash.constEnd() ? nullptr : &m_types.at(type).members.at(it.value());
}

const Rl26CareerEditorManager::Member* Rl26CareerEditorManager::member(int type, const QString& name) const {
    if (const Member* m = member(type, fnv(name.toLatin1())))
        return m;
    // Members named from the game's database (not by their hash).
    if (type >= 0 && type < m_types.size())
        for (const auto& mList = m_types.at(type).members; const Member& m : mList)
            if (m.name == name)
                return &m;
    return nullptr;
}

Rl26CareerEditorManager::Ref Rl26CareerEditorManager::child(const Ref& parent, const QString& token) const {
    if (!parent.ok() || parent.type < 0)
        return {};
    QString name = token;
    int index = 0;
    if (const int hashAt = int(name.indexOf(u'#')); hashAt > 0) {
        index = QStringView(name).mid(hashAt + 1).toInt();
        name = name.left(hashAt);
    }
    const Member* m = name.startsWith(u'@') ? member(parent.type, QStringView(name).mid(1).toUInt(nullptr, 16)) : member(parent.type, name);
    if (!m || index < 0 || index >= qMax<int>(1, m->count))
        return {};
    Ref r;
    r.type = m->type;
    r.size = m->size;
    r.count = m->type >= 0 ? m->count : m->count;
    r.kind = m->kind;
    r.at = parent.at + m->off + qsizetype(index) * (m->type >= 0 ? m->size : 0);
    if (m->type >= 0 && token.contains(u'#'))
        r.count = 1;   // One element of an array of objects; without "#n", the array (its first element, its length).
    return r;
}

Rl26CareerEditorManager::Ref Rl26CareerEditorManager::resolve(const Ref& from, const QString& path) const {
    // Some members' names have a dot ("primaryRGBColour.r"): a token that isn't a member is joined with the next.
    Ref r = from;
    const QStringList tokens = path.split(u'.', Qt::SkipEmptyParts);
    for (int i = 0; i < tokens.size(); ++i) {
        QString token = tokens.at(i);
        Ref next = child(r, token);
        while (!next.ok() && i + 1 < tokens.size()) {
            token += u'.' + tokens.at(++i);
            next = child(r, token);
        }
        if (!next.ok())
            return {};
        r = next;
    }
    return r;
}

qint64 Rl26CareerEditorManager::readInt(qsizetype at, int size, bool isSigned) const {
    if (at < 0 || at + size > m_data.size())
        return 0;
    quint64 v = 0;
    for (int i = size - 1; i >= 0; --i)
        v = (v << 8) | static_cast<quint8>(m_data.at(at + i));
    if (isSigned && size < 8 && (v & (quint64(1) << (size * 8 - 1))))
        v |= ~quint64(0) << (size * 8);
    return static_cast<qint64>(v);
}

void Rl26CareerEditorManager::writeInt(qsizetype at, int size, qint64 value) {
    if (at < 0 || at + size > m_data.size())
        return;
    for (int i = 0; i < size; ++i)
        m_data[at + i] = static_cast<char>((quint64(value) >> (8 * i)) & 0xFF);
}

float Rl26CareerEditorManager::readFloat(qsizetype at) const {
    const quint32 bits = static_cast<quint32>(readInt(at, 4));
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

QString Rl26CareerEditorManager::readText(qsizetype at, int size) const {
    if (at < 0 || at + size > m_data.size())
        return {};
    const QByteArray raw = m_data.mid(at, size);
    const int end = int(raw.indexOf('\0'));
    return QString::fromLatin1(end < 0 ? raw : raw.left(end));
}

void Rl26CareerEditorManager::writeText(qsizetype at, int size, const QString& text) {
    if (at < 0 || at + size > m_data.size() || size <= 0)
        return;
    const QByteArray bytes = text.toLatin1().left(size - 1);
    for (int i = 0; i < size; ++i)
        m_data[at + i] = i < bytes.size() ? bytes.at(i) : '\0';
}

// The records: root.matches, baseSchema.m_clubs (338 slots, the used count first), baseSchema.m_players.
void Rl26CareerEditorManager::index() {
    const Ref root = rootRef();
    m_clubs.clear();
    m_players.clear();
    m_clubById.clear();
    m_playerById.clear();

    m_matchArray = child(root, u"@cd149852"_s);
    if (const Member* matches = member(root.type, 0xcd149852u))
        m_matchArray.count = matches->count;   // child() gives one element; the array's length.
    m_matchCount = int(readInt(child(root, u"@ba432ce3"_s).at, 2));
    m_matchSize = m_matchArray.size;

    const Ref clubs = resolve(root, u"baseSchema.m_clubs"_s);
    const Ref clubArray = child(clubs, u"@6568e3a6"_s);
    const Member* clubMember = member(clubs.type, 0x6568e3a6u);
    const int clubCount = qMin<int>(int(readInt(child(clubs, u"@57e27cef"_s).at, 2)), clubMember ? clubMember->count : 0);
    m_clubArray = clubArray;
    m_clubSize = clubArray.size;
    for (int i = 0; i < clubCount; ++i) {
        const qsizetype at = clubArray.at + qsizetype(i) * m_clubSize;
        m_clubById.insert(int(readInt(resolve({at, clubArray.type, 0, 1, 0}, u"paClub.m_overrideId"_s).at, 4)), int(m_clubs.size()));
        m_clubs << at;
    }

    const Ref players = resolve(root, u"baseSchema.m_players"_s);
    const Ref playerArray = child(players, u"@b9c6b413"_s);
    const Member* playerMember = member(players.type, 0xb9c6b413u);
    const int playerCount = qMin<int>(int(readInt(child(players, u"@b62f14ac"_s).at, 2)), playerMember ? playerMember->count : 0);
    m_playerArray = playerArray;
    m_playerSize = playerArray.size;
    for (int i = 0; i < playerCount; ++i) {
        const qsizetype at = playerArray.at + qsizetype(i) * m_playerSize;
        m_playerById.insert(int(readInt(resolve({at, playerArray.type, 0, 1, 0}, u"paPlayer.m_overrideId"_s).at, 4)), int(m_players.size()));
        m_players << at;
    }
    m_careerYear = int(readInt(resolve(root, u"m_date.year"_s).at, 2));
    if (m_careerYear < 1900)
        m_careerYear = 2026;
}

// =================================================================================================================
// Field definitions (paths from the record, resolved once)
// =================================================================================================================

void Rl26CareerEditorManager::buildDefs() {
    m_playerDefs.clear();
    m_teamDefs.clear();
    m_careerDefs.clear();
    const auto add = [this](QList<FieldDef>& list, const Ref& base, const QString& key, const QString& label, const QString& group,
                            const QString& path, const QString& kind, const QString& choiceList = {}, int min = 0, int max = 0,
                            bool slider = false, bool readOnly = false) {
        const Ref r = resolve(base, path);
        if (!r.ok())
            return;
        FieldDef d;
        d.key = key;
        d.label = label;
        d.group = group;
        d.path = path;
        d.kind = kind;
        d.list = choiceList;
        d.min = min;
        d.max = max;
        d.slider = slider;
        d.readOnly = readOnly;
        d.rel = r.at - base.at;
        d.size = r.type >= 0 ? r.size : r.size * r.count;
        d.elemKind = r.kind;
        if (kind == u"text"_s && max == 0)
            d.max = qMax(0, d.size - 1);   // The text box's maximum length (the save keeps a terminator).
        if (kind == u"number"_s && max == 0)
            d.max = d.size >= 4 ? 2147483647 : int((qint64(1) << (8 * d.size)) - 1);
        list << d;
    };

    // --- Players ------------------------------------------------------------------------------------------------
    if (!m_players.isEmpty()) {
        const Ref p{m_players.first(), m_playerArray.type, 0, 1, 0};
        const QString id = u"paPlayer.m_identity.base."_s;
        add(m_playerDefs, p, u"firstname"_s, u"First Name"_s, u"Name"_s, id + u"firstname"_s, u"text"_s);
        add(m_playerDefs, p, u"surname"_s, u"Surname"_s, u"Name"_s, id + u"surname"_s, u"text"_s);
        add(m_playerDefs, p, u"shirtName"_s, u"Shirt Name"_s, u"Name"_s, id + u"shirtName"_s, u"text"_s);
        add(m_playerDefs, p, u"sex"_s, u"Sex"_s, u"Details"_s, id + u"sex"_s, u"choice"_s, u"sex"_s);
        add(m_playerDefs, p, u"age"_s, u"Age"_s, u"Details"_s, id + u"age"_s, u"number"_s, {}, 0, 99);
        add(m_playerDefs, p, u"birthDay"_s, u"Birth Day"_s, u"Details"_s, id + u"birthDay"_s, u"number"_s, {}, 1, 31);
        add(m_playerDefs, p, u"birthMonth"_s, u"Birth Month"_s, u"Details"_s, id + u"birthMonth"_s, u"number"_s, {}, 1, 12);
        add(m_playerDefs, p, u"birthYear"_s, u"Birth Year"_s, u"Details"_s, id + u"birthYear"_s, u"number"_s, {}, 1900, 2100);
        add(m_playerDefs, p, u"shirtNumber"_s, u"Shirt Number"_s, u"Details"_s, id + u"shirtNumber"_s, u"number"_s, {}, 0, 99);
        add(m_playerDefs, p, u"rightHanded"_s, u"Right Handed"_s, u"Details"_s, id + u"rightHanded"_s, u"bool"_s);
        add(m_playerDefs, p, u"rightFooted"_s, u"Right Footed"_s, u"Details"_s, id + u"rightFooted"_s, u"bool"_s);

        add(m_playerDefs, p, u"attributes.form"_s, u"Form"_s, u"Career"_s, u"form"_s, u"number"_s, {}, 0, 100, true);
        add(m_playerDefs, p, u"popularity"_s, u"Popularity"_s, u"Career"_s, u"popularity"_s, u"number"_s, {}, 0, 65535);
        add(m_playerDefs, p, u"morale"_s, u"Morale"_s, u"Career"_s, u"morale"_s, u"float"_s, {}, 0, 100);
        add(m_playerDefs, p, u"fatigue"_s, u"Fatigue"_s, u"Career"_s, u"m_fatigue"_s, u"float"_s, {}, 0, 100);
        add(m_playerDefs, p, u"points"_s, u"Points"_s, u"Career"_s, u"points"_s, u"number"_s);
        add(m_playerDefs, p, u"baseRating"_s, u"Base Rating"_s, u"Career"_s, u"m_baseRating"_s, u"number"_s, {}, 0, 99);
        add(m_playerDefs, p, u"potentialRating"_s, u"Potential Rating"_s, u"Career"_s, u"m_potentialRating"_s, u"number"_s, {}, 0, 99);
        add(m_playerDefs, p, u"potentialAdjustment"_s, u"Potential Adjustment"_s, u"Career"_s, u"m_potentialAdjustment"_s, u"float"_s, {}, -100, 100);
        add(m_playerDefs, p, u"contractExpires"_s, u"Contract Expires (end of)"_s, u"Career"_s, u"m_contract.expiresAtEndOfYear"_s, u"number"_s, {}, 2000, 2100);
        add(m_playerDefs, p, u"contractAmount"_s, u"Contract (per year)"_s, u"Career"_s, u"m_contract.amountPerYear"_s, u"number"_s);
        add(m_playerDefs, p, u"feederClubId"_s, u"Feeder Club"_s, u"Career"_s, u"feederClubId"_s, u"choice"_s, u"@clubs"_s);
        add(m_playerDefs, p, u"firstYearInCountry"_s, u"First Year In Country"_s, u"Career"_s, u"m_firstYearInCountry"_s, u"number"_s, {}, 0, 2100);
        add(m_playerDefs, p, u"firstYearInLeague"_s, u"First Year In League"_s, u"Career"_s, u"m_firstYearInLeague"_s, u"number"_s, {}, 0, 2100);
        add(m_playerDefs, p, u"firstYearAtClub"_s, u"First Year At Club"_s, u"Career"_s, u"m_firstYearAtClub"_s, u"number"_s, {}, 0, 2100);
        add(m_playerDefs, p, u"peakYear"_s, u"Peak (year)"_s, u"Career"_s, u"m_peakDate.year"_s, u"number"_s, {}, 0, 2200);
        add(m_playerDefs, p, u"declineYear"_s, u"Decline (year)"_s, u"Career"_s, u"m_declineDate.year"_s, u"number"_s, {}, 0, 2200);
        add(m_playerDefs, p, u"endYear"_s, u"Career End (year)"_s, u"Career"_s, u"m_endDate.year"_s, u"number"_s, {}, 0, 2200);

        add(m_playerDefs, p, u"preferredRolePrimary"_s, u"Primary Role"_s, u"Roles"_s, id + u"preferredRolePrimary"_s, u"choice"_s, u"roles"_s);
        add(m_playerDefs, p, u"preferredRoleSecondary"_s, u"Secondary Role"_s, u"Roles"_s, id + u"preferredRoleSecondary"_s, u"choice"_s, u"roles"_s);
        add(m_playerDefs, p, u"preferredRoleTertiary"_s, u"Tertiary Role"_s, u"Roles"_s, id + u"preferredRoleTertiary"_s, u"choice"_s, u"roles"_s);

        add(m_playerDefs, p, u"countryOfBirth"_s, u"Country Of Birth"_s, u"Representative"_s, id + u"countryOfBirth"_s, u"choice"_s, u"countriesOfBirth"_s);
        add(m_playerDefs, p, u"representativeCountry"_s, u"Representative Country"_s, u"Representative"_s, id + u"representativeCountry"_s, u"choice"_s, u"repCountries"_s);
        add(m_playerDefs, p, u"stateOfOrigin"_s, u"State Of Origin"_s, u"Representative"_s, id + u"stateOfOrigin"_s, u"choice"_s, u"soo"_s);
        add(m_playerDefs, p, u"cityVsCountry"_s, u"City Vs Country"_s, u"Representative"_s, id + u"cityVsCountry"_s, u"choice"_s, u"cvc"_s);
        add(m_playerDefs, p, u"allStars"_s, u"All Stars"_s, u"Representative"_s, id + u"allStars"_s, u"choice"_s, u"allStars"_s);
        add(m_playerDefs, p, u"isWorldCup"_s, u"World Cup Player"_s, u"Representative"_s, id + u"isWorldCup"_s, u"bool"_s);

        add(m_playerDefs, p, u"id"_s, u"Player Id"_s, u"Record"_s, u"paPlayer.m_overrideId"_s, u"number"_s, {}, 0, 0, false, true);
        add(m_playerDefs, p, u"academyId"_s, u"Academy / Database Id"_s, u"Record"_s, u"paPlayer.m_academyId"_s, u"number"_s, {}, 0, 0, false, true);
        add(m_playerDefs, p, u"licensed"_s, u"Licensed"_s, u"Record"_s, id + u"licensed"_s, u"bool"_s);
        add(m_playerDefs, p, u"hidden"_s, u"Hidden"_s, u"Record"_s, id + u"hidden"_s, u"bool"_s);
        add(m_playerDefs, p, u"commentaryNameHash"_s, u"Commentary Name"_s, u"Name"_s, id + u"commentaryNameHash"_s, u"choice"_s, u"#surnames"_s);
        add(m_playerDefs, p, u"photogrammetryStatus"_s, u"Photogrammetry Status"_s, u"Record"_s, id + u"photogrammetryStatus"_s, u"number"_s);
        add(m_playerDefs, p, u"contractExpiry"_s, u"Database Contract Expiry"_s, u"Record"_s, id + u"contractExpiry"_s, u"number"_s, {}, 0, 2100);

        const QString at = u"paPlayer.m_attributes.base."_s;
        add(m_playerDefs, p, u"height"_s, u"Height (cm)"_s, u"Attributes"_s, at + u"height"_s, u"number"_s, {}, 100, 230);
        add(m_playerDefs, p, u"weight"_s, u"Weight (kg)"_s, u"Attributes"_s, at + u"weight"_s, u"number"_s, {}, 40, 200);
        add(m_playerDefs, p, u"perk"_s, u"Perk"_s, u"Attributes"_s, at + u"perk"_s, u"choice"_s, u"perks"_s);
        for (const char* a : {"reputation", "ego", "loyalty", "strength", "aggression", "injuryTolerance", "agility",
                              "stamina", "acceleration", "sprintSpeed"})
            add(m_playerDefs, p, QString::fromLatin1(a), pretty(QString::fromLatin1(a)), u"Attributes"_s,
                at + QString::fromLatin1(a), u"number"_s, {}, 0, 99, true);
        const QList<QPair<QString, QStringList>> skills = {
            {u"Kicking"_s, {u"groundKick"_s, u"puntKick"_s, u"chipKick"_s, u"bombKick"_s, u"fieldgoalKick"_s, u"placeKick"_s}},
            {u"Passing"_s, {u"basicPass"_s, u"longPass"_s, u"offload"_s, u"dummyPass"_s}},
            {u"Evasion"_s, {u"fend"_s, u"sideStep"_s, u"breakTackle"_s}},
            {u"Tackling"_s, {u"tackle"_s, u"driveTackle"_s, u"diveTackle"_s, u"impactTackle"_s}},
            {u"Misc Skills"_s, {u"contestedCollect"_s, u"diveCollect"_s, u"ballStrip"_s}}};
        for (const auto& [group, names] : std::as_const(skills))
            for (const QString& s : std::as_const(names))
                add(m_playerDefs, p, s, pretty(s), group, u"paPlayer.m_skills."_s + s, u"number"_s, {}, 0, 99, true);
        // Lifetime statistics.
        const Ref stats = resolve(p, u"m_lifetimeStats.base"_s);
        QList<Member> statMembers = stats.ok() ? m_types.at(stats.type).members : QList<Member>();
        std::sort(statMembers.begin(), statMembers.end(), [](const Member& a, const Member& b) { return a.off < b.off; });
        if (stats.ok())
            for (const Member& m : std::as_const(statMembers)) {
                if (m.name.isEmpty())
                    continue;
                add(m_playerDefs, p, u"stat."_s + m.name,
                    m.name.startsWith(u"otherStat"_s) ? u"Other Stat %1 (not identified)"_s.arg(m.name.mid(9)) : pretty(m.name),
                    u"Career Stats"_s, u"m_lifetimeStats.base."_s + m.name, u"number"_s);
            }
    }

    // --- Teams -------------------------------------------------------------------------------------------------
    if (!m_clubs.isEmpty()) {
        const Ref c{m_clubs.first(), m_clubArray.type, 0, 1, 0};
        const QString b = u"paClub.m_identity.base."_s;
        add(m_teamDefs, c, u"fullName"_s, u"Full Name"_s, u"Names"_s, b + u"fullName"_s, u"text"_s);
        add(m_teamDefs, c, u"shortName"_s, u"Short Name"_s, u"Names"_s, b + u"shortName"_s, u"text"_s);
        add(m_teamDefs, c, u"nickname"_s, u"Nickname"_s, u"Names"_s, b + u"nickname"_s, u"text"_s);
        add(m_teamDefs, c, u"hudName"_s, u"Abbreviation"_s, u"Names"_s, b + u"hudName"_s, u"text"_s);
        add(m_teamDefs, c, u"sex"_s, u"Women's Team"_s, u"Details"_s, b + u"sex"_s, u"bool"_s);
        add(m_teamDefs, c, u"clubType"_s, u"Club Type"_s, u"Details"_s, b + u"clubType"_s, u"choice"_s, u"clubTypes"_s);
        add(m_teamDefs, c, u"supporters"_s, u"Supporters"_s, u"Details"_s, b + u"supporters"_s, u"choice"_s, u"supporters"_s);
        add(m_teamDefs, c, u"affiliations"_s, u"Affiliations"_s, u"Details"_s, b + u"affiliations"_s, u"number"_s);
        add(m_teamDefs, c, u"alternateNumbering"_s, u"Alternate Numbering"_s, u"Details"_s, b + u"alternateNumbering"_s, u"bool"_s);
        add(m_teamDefs, c, u"alternativeNumberingSystem"_s, u"Numbering System"_s, u"Details"_s, b + u"alternativeNumberingSystem"_s, u"number"_s);
        // The emblem: its position in logos.txt (the Save Editor's list).
        add(m_teamDefs, c, u"logoNumber"_s, u"Logo"_s, u"Details"_s, u"paClub.m_identity.m_decalIdOrIcon.m_iconIndex"_s, u"choice"_s, u"@logos"_s);
        // A custom logo (Decals\\DecalData.SAV) when it isn't 0.
        add(m_teamDefs, c, u"decalId"_s, u"Custom Logo"_s, u"Details"_s, u"paClub.m_identity.m_decalIdOrIcon.m_decalId"_s, u"choice"_s, u"@decals"_s);
        add(m_teamDefs, c, u"commentaryTeamLocationHash"_s, u"Commentary Location"_s, u"Commentary"_s, b + u"commentaryTeamLocationHash"_s, u"choice"_s, u"#location"_s);
        add(m_teamDefs, c, u"commentaryTeamMascotHash"_s, u"Commentary Mascot"_s, u"Commentary"_s, b + u"commentaryTeamMascotHash"_s, u"choice"_s, u"#mascot"_s);
        add(m_teamDefs, c, u"primaryRGBColour"_s, u"Primary Colour"_s, u"Colours"_s, b + u"primaryRGBColour.r"_s, u"colour"_s);
        add(m_teamDefs, c, u"secondaryRGBColour"_s, u"Secondary Colour"_s, u"Colours"_s, b + u"secondaryRGBColour.r"_s, u"colour"_s);
        add(m_teamDefs, c, u"hudTextRGBColour"_s, u"HUD Text Colour"_s, u"Colours"_s, b + u"hudTextRGBColour.r"_s, u"colour"_s);
        add(m_teamDefs, c, u"id"_s, u"Club Id"_s, u"Record"_s, u"paClub.m_overrideId"_s, u"number"_s, {}, 0, 0, false, true);
        add(m_teamDefs, c, u"academyId"_s, u"Academy / Database Id"_s, u"Record"_s, u"paClub.m_academyId"_s, u"number"_s, {}, 0, 0, false, true);
        add(m_teamDefs, c, u"hidden"_s, u"Hidden"_s, u"Record"_s, b + u"hidden"_s, u"bool"_s);
        add(m_teamDefs, c, u"coachId"_s, u"Coach Id"_s, u"Record"_s, u"paClub.coachId"_s, u"number"_s);
        add(m_teamDefs, c, u"memberCount"_s, u"Members"_s, u"Record"_s, u"m_memberCount"_s, u"number"_s);
        add(m_teamDefs, c, u"averageAttendance"_s, u"Match Attendance Total"_s, u"Record"_s, u"m_averageMatchAttendanceTotal"_s, u"number"_s);
        add(m_teamDefs, c, u"matchesPlayedThisYear"_s, u"Matches Played This Year"_s, u"Record"_s, u"m_matchesPlayedThisYear"_s, u"number"_s);
        add(m_teamDefs, c, u"membershipChange"_s, u"Membership Change"_s, u"Record"_s, u"m_membershipChange"_s, u"float"_s, {}, -100000, 100000);
        add(m_teamDefs, c, u"gamesWithoutCharge"_s, u"Games Without A Charge"_s, u"Record"_s, u"m_gamesWithoutCharge"_s, u"number"_s);
        add(m_teamDefs, c, u"gamesWithoutInjury"_s, u"Games Without An Injury"_s, u"Record"_s, u"m_gamesWithoutInjury"_s, u"number"_s);
        add(m_teamDefs, c, u"homeGroundCount"_s, u"Home Grounds"_s, u"Stadiums"_s, b + u"homeGroundCount"_s, u"number"_s, {}, 0, 3);
        for (int g = 0; g < 4; ++g) {
            const QString group = g < 3 ? u"Stadium %1"_s.arg(g + 1) : u"Grand Final Ground"_s;
            const QString base = g < 3 ? b + u"homeGrounds#%1."_s.arg(g) : b + u"homeGroundFinal."_s;
            const QString key = g < 3 ? u"homeGrounds#%1:"_s.arg(g) : u"homeGroundFinal:"_s;
            add(m_teamDefs, c, key + u"stadium"_s, u"Stadium"_s, group, base + u"stadium"_s, u"choice"_s, u"stadiums"_s);
            add(m_teamDefs, c, key + u"wantCustomName"_s, u"Custom Name"_s, group, base + u"wantCustomName"_s, u"bool"_s);
            add(m_teamDefs, c, key + u"customName"_s, u"Name"_s, group, base + u"customName"_s, u"text"_s);
            add(m_teamDefs, c, key + u"isOnDisk"_s, u"On Disk"_s, group, base + u"isOnDisk"_s, u"bool"_s);
            // The custom stadium (Levels\\LevelData.SAV) it uses: its level id, 0xFFFFFFFF = none.
            add(m_teamDefs, c, key + u"stadiumId"_s, u"Custom Stadium"_s, group, base + u"@c8d3d6be"_s, u"choice"_s, u"@levels"_s);
        }
    }

    // --- Career settings (from the root) -------------------------------------------------------------------------
    const Ref root = rootRef();
    const auto addAll = [&](const QString& path, const QString& group, const QString& prefix) {
        const Ref r = resolve(root, path);
        if (!r.ok() || r.type < 0)
            return;
        for (const auto& mList = m_types.at(r.type).members; const Member& m : mList) {
            if (m.name.isEmpty() || m.type >= 0 || (m.count > 1 && m.kind != 0xFDEB))
                continue;
            const QString kind = m.kind == 0xFDE8 ? u"bool"_s : m.kind == 0xFDF2 ? u"float"_s
                               : (m.count > 1 ? u"text"_s : u"number"_s);
            add(m_careerDefs, root, prefix + m.name, pretty(m.name), group, path + u'.' + m.name, kind);
        }
    };
    add(m_careerDefs, root, u"date.year"_s, u"Year"_s, u"Career"_s, u"m_date.year"_s, u"number"_s, {}, 1900, 2200);
    add(m_careerDefs, root, u"date.month"_s, u"Month"_s, u"Career"_s, u"m_date.month"_s, u"number"_s, {}, 1, 12);
    add(m_careerDefs, root, u"date.day"_s, u"Day"_s, u"Career"_s, u"m_date.day"_s, u"number"_s, {}, 1, 31);
    add(m_careerDefs, root, u"matchesThisYear"_s, u"Career Matches This Year"_s, u"Career"_s, u"m_completedCareerMatchesThisYear"_s, u"number"_s);
    add(m_careerDefs, root, u"haveStartedYear"_s, u"Year Started"_s, u"Career"_s, u"m_haveStartedYear"_s, u"bool"_s);
    add(m_careerDefs, root, u"haveEndedYear"_s, u"Year Ended"_s, u"Career"_s, u"m_haveEndedYear"_s, u"bool"_s);
    addAll(u"baseSchema.m_settings"_s, u"Match Settings"_s, u"settings."_s);
    add(m_careerDefs, root, u"user.spendingMoney"_s, u"Spending Money"_s, u"Money"_s, u"baseSchema.m_user.m_spendingMoney"_s, u"number"_s);
    add(m_careerDefs, root, u"user.earnt"_s, u"Money Earnt As A Player"_s, u"Money"_s, u"baseSchema.m_user.m_spendingMoneyEarntAsPlayerTotal"_s, u"number"_s);
    add(m_careerDefs, root, u"user.fanSatisfaction"_s, u"Fan Satisfaction"_s, u"Money"_s, u"baseSchema.m_user.m_fanSatisfaction"_s, u"float"_s, {}, 0, 100);
    const int settingsEnd = int(m_careerDefs.size());
    add(m_careerDefs, root, u"user.isPlayer"_s, u"Player Career"_s, u"Career Team"_s, u"baseSchema.m_user.m_isPlayer"_s, u"bool"_s);
    add(m_careerDefs, root, u"user.coachClubId"_s, u"Club"_s, u"Career Team"_s, u"baseSchema.m_user.m_coachClubId"_s, u"choice"_s, u"@clubs"_s);
    add(m_careerDefs, root, u"user.lossesThisSeason"_s, u"Losses This Season"_s, u"Career Team"_s, u"baseSchema.m_user.m_lossesThisSeason"_s, u"number"_s);
    add(m_careerDefs, root, u"user.championshipWins"_s, u"Championship Wins"_s, u"Career Team"_s, u"baseSchema.m_user.m_championshipWinsCount"_s, u"number"_s);
    add(m_careerDefs, root, u"user.playerId"_s, u"Player"_s, u"Career Player"_s, u"baseSchema.m_user.m_playerId"_s, u"choice"_s, u"@players"_s);
    add(m_careerDefs, root, u"user.motm"_s, u"Man Of The Match"_s, u"Career Player"_s, u"baseSchema.m_user.m_playerTimesManOfTheMatch"_s, u"number"_s);
    add(m_careerDefs, root, u"user.motmSeason"_s, u"Man Of The Match This Season"_s, u"Career Player"_s, u"baseSchema.m_user.m_playerTimesManOfTheMatchThisSeason"_s, u"number"_s);
    add(m_careerDefs, root, u"user.requestedTransfer"_s, u"Requested A Transfer"_s, u"Career Player"_s, u"baseSchema.m_user.m_playerRequestedTransfer"_s, u"bool"_s);
    for (int i = settingsEnd; i < m_careerDefs.size(); ++i)
        m_careerDefs[i].path.prepend(u"#1|"_s);   // Page 1 (Selection).
}

const Rl26CareerEditorManager::FieldDef* Rl26CareerEditorManager::playerDef(const QString& key) const {
    for (const FieldDef& d : std::as_const(m_playerDefs))
        if (d.key == key)
            return &d;
    return nullptr;
}

const Rl26CareerEditorManager::FieldDef* Rl26CareerEditorManager::teamDef(const QString& key) const {
    for (const FieldDef& d : std::as_const(m_teamDefs))
        if (d.key == key)
            return &d;
    return nullptr;
}

namespace {
const Rl26CareerEditorManager::FieldDef& safe(const Rl26CareerEditorManager::FieldDef* d) {
    static const Rl26CareerEditorManager::FieldDef none = [] { Rl26CareerEditorManager::FieldDef f; f.kind = u"none"_s; return f; }();
    return d ? *d : none;
}
} // namespace

QVariant Rl26CareerEditorManager::fieldValue(qsizetype record, const FieldDef& d) const {
    if (d.kind == u"none"_s || record < 0)
        return {};
    const qsizetype at = record + d.rel;
    if (d.kind == u"text"_s)
        return readText(at, d.size);
    if (d.kind == u"bool"_s)
        return m_data.at(at) != 0;
    if (d.kind == u"float"_s)
        return double(std::round(readFloat(at) * 100.0f) / 100.0f);
    if (d.kind == u"colour"_s)
        return u"#%1%2%3"_s.arg(quint8(m_data.at(at)), 2, 16, QChar(u'0')).arg(quint8(m_data.at(at + 1)), 2, 16, QChar(u'0'))
            .arg(quint8(m_data.at(at + 2)), 2, 16, QChar(u'0'));
    return double(readInt(at, qMin(d.size, 8), builtinSigned(d.elemKind)));
}

// =================================================================================================================
// Players / teams
// =================================================================================================================

int Rl26CareerEditorManager::playerId(int index) const {
    const FieldDef* d = playerDef(u"id"_s);
    return d && playerAt(index) >= 0 ? int(readInt(playerAt(index) + d->rel, 4)) : 0;
}

int Rl26CareerEditorManager::clubId(int index) const {
    const FieldDef* d = teamDef(u"id"_s);
    return d && clubAt(index) >= 0 ? int(readInt(clubAt(index) + d->rel, 4)) : 0;
}

QString Rl26CareerEditorManager::playerName(int id) const {
    const int i = m_playerById.value(id, -1);
    if (i < 0)
        return id == 0 ? QString() : u"(player %1)"_s.arg(id);
    const qsizetype at = playerAt(i);
    return (fieldValue(at, safe(playerDef(u"firstname"_s))).toString() + u' ' + fieldValue(at, safe(playerDef(u"surname"_s))).toString()).trimmed();
}

QString Rl26CareerEditorManager::clubName(int id) const {
    const int i = m_clubById.value(id, -1);
    if (i < 0)
        return id == 0 ? QString() : u"(club %1)"_s.arg(id);
    return fieldValue(clubAt(i), safe(teamDef(u"fullName"_s))).toString();
}

QVariantMap Rl26CareerEditorManager::playerInfo(int id) const {
    QVariantMap m{{u"id"_s, id}, {u"playerIndex"_s, -1}, {u"known"_s, false}, {u"rating"_s, 0}, {u"role"_s, QString()},
                  {u"roles"_s, QVariantList()}, {u"age"_s, 0}, {u"club"_s, QString()}};
    const int p = m_playerById.value(id, -1);
    if (p < 0) {
        m[u"name"_s] = id == 0 ? u"(empty)"_s : u"(unknown player id %1)"_s.arg(id);
        m[u"first"_s] = QString();
        m[u"last"_s] = m[u"name"_s];
        return m;
    }
    const qsizetype at = playerAt(p);
    const auto num = [&](const char* key) { const FieldDef* d = playerDef(QString::fromLatin1(key)); return d ? fieldValue(at, *d).toInt() : 0; };
    m[u"first"_s] = fieldValue(at, safe(playerDef(u"firstname"_s)));
    m[u"last"_s] = fieldValue(at, safe(playerDef(u"surname"_s)));
    m[u"name"_s] = playerName(id);
    m[u"known"_s] = true;
    const int role = num("preferredRolePrimary");
    m[u"role"_s] = kRoleShort.value(role);
    m[u"roles"_s] = QVariantList{role, num("preferredRoleSecondary"), num("preferredRoleTertiary")};
    m[u"rating"_s] = playerRating(p);
    m[u"placeKick"_s] = num("placeKick");
    m[u"puntKick"_s] = num("puntKick");
    m[u"age"_s] = num("age");
    m[u"playerIndex"_s] = p;
    return m;
}

int Rl26CareerEditorManager::playerRating(int player) const {
    const qsizetype at = playerAt(player);
    if (at < 0)
        return 0;
    static const QHash<QString, QString> alias = {{u"durability"_s, u"injuryTolerance"_s}, {u"discipline"_s, u"aggression"_s},
                                                  {u"grubberKick"_s, u"groundKick"_s}};
    const auto& terms = Rl26Lists::ratingTerms();
    const FieldDef* roleDef = playerDef(u"preferredRolePrimary"_s);
    const int role = roleDef ? fieldValue(at, *roleDef).toInt() : -1;
    if (role < 0 || role >= terms.size() || terms[role].isEmpty())
        return 0;
    int sum = 0;
    for (const Rl26Lists::RatingTerm& term : std::as_const(terms[role])) {
        const FieldDef* d = playerDef(alias.value(term.field, term.field));
        if (!d)
            return 0;
        sum += fieldValue(at, *d).toInt() * term.mul / term.div;
    }
    return sum / int(terms[role].size());
}

int Rl26CareerEditorManager::teamRating(int team) const {
    const QList<int> ids = rosterIds(team);
    QList<int> ratings;
    for (int id : std::as_const(ids))
        if (const int p = m_playerById.value(id, -1); p >= 0)
            ratings << playerRating(p);
    std::sort(ratings.begin(), ratings.end(), std::greater<int>());
    ratings = ratings.mid(0, 17);
    if (ratings.isEmpty())
        return 0;
    int sum = 0;
    for (int r : std::as_const(ratings))
        sum += r;
    return sum / int(ratings.size());
}

QVariantList Rl26CareerEditorManager::playerRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    for (int i = 0; i < m_players.size(); ++i) {
        const qsizetype at = m_players.at(i);
        const QString first = fieldValue(at, safe(playerDef(u"firstname"_s))).toString();
        const QString last = fieldValue(at, safe(playerDef(u"surname"_s))).toString();
        const int id = playerId(i);
        if (!f.isEmpty() && !(first + u' ' + last).contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"originalId"_s, fieldValue(at, safe(playerDef(u"academyId"_s)))},
                           {u"first"_s, first}, {u"last"_s, last}, {u"shirt"_s, fieldValue(at, safe(playerDef(u"shirtName"_s)))},
                           {u"age"_s, fieldValue(at, safe(playerDef(u"age"_s)))},
                           {u"female"_s, fieldValue(at, safe(playerDef(u"sex"_s))).toInt() == 1},
                           {u"role"_s, kRoles.value(fieldValue(at, safe(playerDef(u"preferredRolePrimary"_s))).toInt())},
                           {u"rating"_s, playerRating(i)}};
    }
    return out;
}

QVariantList Rl26CareerEditorManager::teamRows(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    for (int i = 0; i < m_clubs.size(); ++i) {
        const qsizetype at = m_clubs.at(i);
        const QString full = fieldValue(at, safe(teamDef(u"fullName"_s))).toString();
        const int id = clubId(i);
        if (!f.isEmpty() && !full.contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"originalId"_s, fieldValue(at, safe(teamDef(u"academyId"_s)))},
                           {u"fullName"_s, full}, {u"shortName"_s, fieldValue(at, safe(teamDef(u"shortName"_s)))},
                           {u"abbreviation"_s, fieldValue(at, safe(teamDef(u"hudName"_s)))},
                           {u"primaryColour"_s, fieldValue(at, safe(teamDef(u"primaryRGBColour"_s)))},
                           {u"secondaryColour"_s, fieldValue(at, safe(teamDef(u"secondaryRGBColour"_s)))},
                           {u"female"_s, fieldValue(at, safe(teamDef(u"sex"_s))).toBool()},
                           {u"logo"_s, teamLogo(i)}, {u"logoUrl"_s, teamLogoUrl(i)}};
    }
    return out;
}

// The team logo names: games/Rugby League 26/pc/save_editor/logos.txt in Blobset Tools' folder, the Save Editor's
// list (the emblem value is the line's position, first line = 0; blank lines and "#" comments are skipped). Made with
// the built in list when it doesn't exist; read again when it changes.
QString Rl26CareerEditorManager::logosFile() const {
    return QDir(AppPaths::dataDir()).filePath(u"games/Rugby League 26/pc/save_editor/logos.txt"_s);
}

QStringList Rl26CareerEditorManager::logoNames() const {
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
    m_logoUrls.clear();
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logos = Rl26Lists::clubIcons();
        return m_logos;
    }
    m_logos.clear();
    for (const auto& rawList = in.readAll().split('\n'); const QByteArray& raw : rawList) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty() && !line.startsWith(u'#'))
            m_logos << line;
    }
    return m_logos;
}

QString Rl26CareerEditorManager::teamLogo(int team) const {
    const qsizetype at = clubAt(team);
    const FieldDef* d = teamDef(u"logoNumber"_s);
    if (at < 0 || !d)
        return {};
    if (const quint32 decal = decalOf(team); decal != 0) {
        const QString name = Rl26UserContent::decalName(m_path, decal);
        return name.isEmpty() ? u"Custom Logo"_s : name;
    }
    return logoNames().value(fieldValue(at, *d).toInt());
}

// The commentary name lists in the game's sound M3MP (sound\\dialogue.mempack.m3mp, 0x83AF3201ED1AE751), as the
// Save Editor reads them: dialogue_wildcardhash_<list>: u32 count, then per name u32 hash, u32 length, the name.
// Empty when the blobset loaded in Blobset Tools isn't Rugby League 26's (the fields are then numbers).
const Rl26CareerEditorManager::Commentary& Rl26CareerEditorManager::commentary(const QString& list) const {
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
        QString name = QString::fromLatin1(data.mid(pos, length)).toLower();
        if (!name.isEmpty())
            name[0] = name[0].toUpper();
        c.names << name;
        pos += length;
    }
    return c;
}

quint32 Rl26CareerEditorManager::decalOf(int team) const {
    const qsizetype at = clubAt(team);
    const FieldDef* d = teamDef(u"decalId"_s);
    return at >= 0 && d ? static_cast<quint32>(readInt(at + d->rel, 4)) : 0;
}

QString Rl26CareerEditorManager::teamLogoUrl(int team) const {
    // A custom logo (Decals\\DecalData.SAV), drawn.
    if (const quint32 decal = decalOf(team); decal != 0)
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

QString Rl26CareerEditorManager::competitionLogoUrl(const QString& comp) const {
    const QString logo = u"comp:"_s + competitionLogoName(comp);
    if (const auto it = m_logoUrls.constFind(logo); it != m_logoUrls.constEnd())
        return it.value();
    QString url;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        const QString hash = orchestrator->getHashFromPath(u"ui/logos_competition/large/%1.pc.dds"_s.arg(competitionLogoName(comp)));
        if (!hash.isEmpty() && hash.toULongLong(nullptr, 16) != 0)
            url = u"image://dds_provider/"_s + DdsImageProvider::makeId(hash, 0, {});
    }
    m_logoUrls.insert(logo, url);
    return url;
}

QStringList Rl26CareerEditorManager::listNames(const QString& list) const {
    if (list == u"countriesOfBirth"_s) return Rl26Lists::countriesOfBirth();
    if (list == u"repCountries"_s) return Rl26Lists::repCountries();
    if (list == u"supporters"_s) return Rl26Lists::supporters();
    if (list == u"stadiums"_s) return Rl26Lists::stadiums();
    if (list == u"roles"_s) return kRoles;
    if (list == u"soo"_s) return {u"None"_s, u"Queensland"_s, u"New South Wales"_s};
    if (list == u"cvc"_s) return {u"None"_s, u"City"_s, u"Country"_s};
    if (list == u"allStars"_s) return {u"None"_s, u"Maori"_s, u"Indigenous"_s};
    if (list == u"perks"_s) return {u"None"_s, u"Speed"_s, u"Stamina"_s, u"Contests"_s, u"Place Kicking"_s, u"In Play Kicking"_s};
    if (list == u"clubTypes"_s) return {u"League"_s, u"International"_s, u"State Of Origin"_s, u"City V's Country"_s, u"All Stars"_s};
    if (list == u"sex"_s) return {u"Male"_s, u"Female"_s};
    if (list == u"status"_s) return kStatusReasons;
    return {};
}

QVariantList Rl26CareerEditorManager::choices(const QString& list, const QVariant& value) const {
    QVariantList out;
    const int v = value.toInt();
    if (list == u"@clubs"_s || list == u"@players"_s) {
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, 0}};
        if (list == u"@clubs"_s) {
            QList<QPair<QString, int>> clubs;
            for (int i = 0; i < m_clubs.size(); ++i)
                clubs << qMakePair(fieldValue(m_clubs.at(i), safe(teamDef(u"fullName"_s))).toString(), clubId(i));
            std::sort(clubs.begin(), clubs.end());
            for (const auto& [name, id] : std::as_const(clubs))
                out << QVariantMap{{u"name"_s, name}, {u"value"_s, id}};
        } else {
            for (int i = 0; i < m_players.size(); ++i)
                out << QVariantMap{{u"name"_s, playerName(playerId(i))}, {u"value"_s, playerId(i)}};
        }
        if (v != 0 && !(list == u"@clubs"_s ? m_clubById.contains(v) : m_playerById.contains(v)))
            out.insert(1, QVariantMap{{u"name"_s, u"(%1)"_s.arg(v)}, {u"value"_s, v}});
        return out;
    }
    if (list.startsWith(u'#')) {
        const Commentary& c = commentary(list.mid(1));
        if (c.names.isEmpty())
            return out;   // Not available: the field is a number.
        const quint32 h = static_cast<quint32>(value.toDouble());
        if (!c.hashes.contains(h))
            out << QVariantMap{{u"name"_s, u"(0x%1)"_s.arg(h, 8, 16, QChar(u'0')).toUpper()}, {u"value"_s, double(h)}};
        for (int i = 0; i < c.names.size(); ++i)
            out << QVariantMap{{u"name"_s, c.names.at(i)}, {u"value"_s, double(c.hashes.at(i))}};
        return out;
    }
    if (list == u"@levels"_s || list == u"@decals"_s) {
        const bool levels = list == u"@levels"_s;
        const quint32 id = static_cast<quint32>(value.toDouble());
        const quint32 none = levels ? Rl26UserContent::kNoLevel : 0;
        const QList<Rl26UserContent::Item> items = levels ? Rl26UserContent::customStadiums(m_path)
                                                          : Rl26UserContent::decals(m_path);
        out << QVariantMap{{u"name"_s, u"None"_s}, {u"value"_s, double(none)}};
        bool found = id == none;
        QHash<QString, int> uses;
        for (const Rl26UserContent::Item& i : std::as_const(items))
            ++uses[i.name];
        for (const Rl26UserContent::Item& i : std::as_const(items)) {
            found = found || i.id == id;
            out << QVariantMap{{u"name"_s, uses.value(i.name) > 1 ? u"%1 (%2)"_s.arg(i.name).arg(i.id) : i.name}, {u"value"_s, double(i.id)}};
        }
        if (!found)
            out.insert(1, QVariantMap{{u"name"_s, levels ? u"Custom Stadium"_s : u"Custom Logo"_s}, {u"value"_s, double(id)}});
        return out;
    }
    const QStringList names = list == u"@logos"_s ? logoNames() : listNames(list);
    // Stadium ids start at 5; the representative teams' are club ids (Rl26Lists / the Academy editor).
    const int base = list == u"stadiums"_s ? 5 : list == u"soo"_s ? 196 : list == u"cvc"_s ? 199 : list == u"allStars"_s ? 202 : 0;
    if (v < base || v >= base + names.size())
        out << QVariantMap{{u"name"_s, list == u"@logos"_s && v == 0xFFFF ? u"Custom Logo"_s : u"(%1)"_s.arg(v)}, {u"value"_s, v}};
    for (int i = 0; i < names.size(); ++i)
        out << QVariantMap{{u"name"_s, names.at(i)}, {u"value"_s, base + i}};
    return out;
}

// =================================================================================================================
// Fields
// =================================================================================================================

QStringList Rl26CareerEditorManager::groups(const QString& kind) const {
    QStringList out;
    const QList<FieldDef>& defs = kind == u"team"_s ? m_teamDefs : m_playerDefs;
    for (const FieldDef& d : std::as_const(defs))
        if (!out.contains(d.group))
            out << d.group;
    return out;
}

QStringList Rl26CareerEditorManager::pageGroups(const QString& kind, const QString& page) const {
    if (kind == u"team"_s) {
        if (page == u"Details"_s)
            return {u"Names"_s, u"Details"_s, u"Colours"_s, u"Commentary"_s, u"Record"_s};
        if (page == u"Stadiums"_s)
            return {u"Stadiums"_s, u"Stadium 1"_s, u"Stadium 2"_s, u"Stadium 3"_s, u"Grand Final Ground"_s};
        return {};
    }
    if (kind == u"career"_s) {
        if (page == u"Settings"_s)
            return {u"Career"_s, u"Match Settings"_s, u"Money"_s};
        if (page == u"Selection"_s)
            return {u"Career Team"_s, u"Career Player"_s};
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

QVariantList Rl26CareerEditorManager::fields(const QString& kind, int index) const {
    if (kind == u"career"_s)
        return careerFields(index);
    QVariantList out;
    qsizetype record = -1;
    const QList<FieldDef>* defs = nullptr;
    if (kind == u"player"_s) {
        record = playerAt(index);
        defs = &m_playerDefs;
    } else if (kind == u"team"_s) {
        record = clubAt(index);
        defs = &m_teamDefs;
    } else if (kind == u"contract"_s) {
        const Ref rec = resolve(rootRef(), u"m_recruiting"_s);
        const Ref arr = child(rec, u"@c41b7a94"_s);
        const int count = int(readInt(child(rec, u"@5a12360d"_s).at, 2));
        if (!arr.ok() || index < 0 || index >= count)
            return out;
        const qsizetype e = arr.at + qsizetype(index) * arr.size;
        const Ref r{e, arr.type, 0, 1, 0};
        const auto row = [&](const QString& key, const QString& label, const QString& path, const QString& list) {
            const Ref f = resolve(r, path);
            if (!f.ok())
                return;
            const int v = int(readInt(f.at, f.size));
            QVariantMap m{{u"key"_s, key}, {u"label"_s, label}, {u"group"_s, u"Contract"_s}, {u"readOnly"_s, false},
                          {u"slider"_s, false}, {u"min"_s, 0}, {u"offset"_s, u"0x"_s + QString::number(f.at - m_rootAt, 16).toUpper()},
                          {u"value"_s, v}};
            if (!list.isEmpty()) {
                m[u"kind"_s] = u"choice"_s;
                m[u"choices"_s] = choices(list, v);
            } else {
                m[u"kind"_s] = u"number"_s;
                m[u"max"_s] = f.size >= 4 ? 2147483647 : 65535;
            }
            out << m;
        };
        row(u"playerId"_s, u"Player"_s, u"playerId"_s, u"@players"_s);
        row(u"fromClubId"_s, u"From Club"_s, u"fromClubId"_s, u"@clubs"_s);
        row(u"toClubId"_s, u"To Club"_s, u"toClubId"_s, u"@clubs"_s);
        row(u"enactAtStartOfYear"_s, u"Moves At The Start Of"_s, u"enactAtStartOfYear"_s, {});
        row(u"expiresAtEndOfYear"_s, u"Contract Expires (end of)"_s, u"contract.expiresAtEndOfYear"_s, {});
        row(u"amountPerYear"_s, u"Contract (per year)"_s, u"contract.amountPerYear"_s, {});
        return out;
    }
    if (record < 0 || !defs)
        return out;
    for (const FieldDef& d : std::as_const(*defs)) {
        QVariantMap m{{u"key"_s, d.key}, {u"label"_s, d.label}, {u"group"_s, d.group}, {u"kind"_s, d.kind},
                      {u"value"_s, fieldValue(record, d)}, {u"min"_s, d.min}, {u"max"_s, d.max},
                      {u"offset"_s, u"+0x"_s + QString::number(d.rel, 16).toUpper()}, {u"readOnly"_s, d.readOnly},
                      {u"slider"_s, d.slider}};
        if (d.kind == u"float"_s)
            m[u"kind"_s] = u"number"_s;
        if (d.kind == u"choice"_s) {
            const QVariantList c = choices(d.list, m.value(u"value"_s));
            if (c.isEmpty())   // The commentary names aren't available: the hash as a number.
                m[u"kind"_s] = d.list.startsWith(u'#') ? u"big"_s : u"number"_s;
            else
                m[u"choices"_s] = c;
        }
        out << m;
    }
    if (kind == u"player"_s) {
        // The player's status (Player Status page).
        const int id = playerId(index);
        int reason = 0;
        for (const auto& vList = statusRows(); const QVariant& v : vList)
            if (v.toMap().value(u"id"_s).toInt() == id)
                reason = v.toMap().value(u"reason"_s).toInt();
        out << QVariantMap{{u"key"_s, u"@status"_s}, {u"label"_s, u"Status"_s}, {u"group"_s, u"Status"_s}, {u"kind"_s, u"choice"_s},
                           {u"value"_s, reason}, {u"choices"_s, choices(u"status"_s, reason)}, {u"readOnly"_s, true},
                           {u"offset"_s, QString()}};
    }
    return out;
}

bool Rl26CareerEditorManager::setField(const QString& kind, int index, const QString& key, const QVariant& value) {
    qsizetype record = -1;
    const FieldDef* def = nullptr;
    if (kind == u"player"_s) {
        record = playerAt(index);
        def = playerDef(key);
    } else if (kind == u"team"_s) {
        record = clubAt(index);
        def = teamDef(key);
    } else if (kind == u"career"_s) {
        record = m_rootAt;
        for (const FieldDef& d : std::as_const(m_careerDefs))
            if (d.key == key)
                def = &d;
    } else if (kind == u"contract"_s) {
        const Ref rec = resolve(rootRef(), u"m_recruiting"_s);
        const Ref arr = child(rec, u"@c41b7a94"_s);
        if (!arr.ok() || index < 0)
            return false;
        const QHash<QString, QString> paths = {{u"playerId"_s, u"playerId"_s}, {u"fromClubId"_s, u"fromClubId"_s},
                                               {u"toClubId"_s, u"toClubId"_s}, {u"enactAtStartOfYear"_s, u"enactAtStartOfYear"_s},
                                               {u"expiresAtEndOfYear"_s, u"contract.expiresAtEndOfYear"_s},
                                               {u"amountPerYear"_s, u"contract.amountPerYear"_s}};
        const Ref f = resolve({arr.at + qsizetype(index) * arr.size, arr.type, 0, 1, 0}, paths.value(key));
        if (!f.ok())
            return false;
        writeInt(f.at, f.size, value.toLongLong());
        edited();
        return true;
    }
    if (record < 0 || !def || def->readOnly)
        return false;
    const qsizetype at = record + def->rel;
    if (def->kind == u"text"_s) {
        writeText(at, def->size, value.toString());
    } else if (def->kind == u"bool"_s) {
        m_data[at] = value.toBool() ? 1 : 0;
    } else if (def->kind == u"float"_s) {
        const float f = float(value.toDouble());
        quint32 bits;
        std::memcpy(&bits, &f, 4);
        writeInt(at, 4, bits);
    } else if (def->kind == u"colour"_s) {
        QString s = value.toString();
        if (s.startsWith(u'#'))
            s = s.mid(1);
        const uint rgb = QStringView(s).left(6).toUInt(nullptr, 16);
        m_data[at] = char((rgb >> 16) & 0xFF);
        m_data[at + 1] = char((rgb >> 8) & 0xFF);
        m_data[at + 2] = char(rgb & 0xFF);
    } else {
        qint64 v = value.toLongLong();
        if (def->max > def->min)
            v = qBound<qint64>(qint64(def->min), v, qint64(def->max));
        writeInt(at, qMin(def->size, 8), v);
    }
    // The emblem is a custom logo or an icon: m_iconCategory / m_iconIndex 255 / 65535 with a custom logo (as the
    // game saves them), and picking an icon drops the custom logo.
    if (kind == u"team"_s && (key == u"decalId"_s || key == u"logoNumber"_s)) {
        const Ref icon = resolve({record, m_clubArray.type, 0, 1, 0}, u"paClub.m_identity.m_decalIdOrIcon"_s);
        const Member* category = icon.ok() ? member(icon.type, u"m_iconCategory"_s) : nullptr;
        const Member* index = icon.ok() ? member(icon.type, u"m_iconIndex"_s) : nullptr;
        const Member* decal = icon.ok() ? member(icon.type, u"m_decalId"_s) : nullptr;
        if (category && index && decal) {
            if (key == u"decalId"_s) {
                const bool custom = value.toLongLong() != 0;
                writeInt(icon.at + category->off, 1, custom ? 0xFF : 0);
                if (custom)
                    writeInt(icon.at + index->off, 2, 0xFFFF);
                else if (readInt(icon.at + index->off, 2) == 0xFFFF)
                    writeInt(icon.at + index->off, 2, 0);
            } else {
                writeInt(icon.at + decal->off, 4, 0);
                writeInt(icon.at + category->off, 1, 0);
            }
        }
    }
    edited();
    return true;
}

QVariantList Rl26CareerEditorManager::careerFields(int page) const {
    QVariantList out;
    for (const FieldDef& d : std::as_const(m_careerDefs)) {
        const bool selection = d.path.startsWith(u"#1|"_s);
        if ((page == 1) != selection)
            continue;
        QVariantMap m{{u"key"_s, d.key}, {u"label"_s, d.label}, {u"group"_s, d.group}, {u"kind"_s, d.kind == u"float"_s ? u"number"_s : d.kind},
                      {u"value"_s, fieldValue(m_rootAt, d)}, {u"min"_s, d.min}, {u"max"_s, d.max},
                      {u"offset"_s, u"0x"_s + QString::number(d.rel, 16).toUpper()}, {u"readOnly"_s, d.readOnly}, {u"slider"_s, false}};
        if (d.kind == u"choice"_s)
            m[u"choices"_s] = choices(d.list, m.value(u"value"_s));
        out << m;
    }
    return out;
}

// paTeamVisualConfig / paCharacterVisualConfig objects as fields: named members, colours from their _r/_g/_b bytes.
QVariantList Rl26CareerEditorManager::objectFields(qsizetype at, int type, const QString& prefix, const QString& group) const {
    QVariantList out;
    if (type < 0)
        return out;
    QList<Member> members = m_types.at(type).members;
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.name < b.name; });
    QSet<QString> colours;
    for (const Member& m : std::as_const(members)) {
        if (m.name.isEmpty() || m.type >= 0)
            continue;
        if (m.name.endsWith(u"_r"_s) || m.name.endsWith(u"_g"_s) || m.name.endsWith(u"_b"_s)) {
            const QString base = m.name.left(m.name.size() - 2);
            if (colours.contains(base))
                continue;
            const Member* r = member(type, base + u"_r"_s);
            const Member* g = member(type, base + u"_g"_s);
            const Member* b = member(type, base + u"_b"_s);
            if (r && g && b) {
                colours.insert(base);
                out << QVariantMap{{u"key"_s, prefix + base}, {u"label"_s, pretty(base)}, {u"group"_s, group},
                                   {u"kind"_s, u"colour"_s}, {u"readOnly"_s, false}, {u"offset"_s, QString()},
                                   {u"value"_s, u"#%1%2%3"_s.arg(quint8(m_data.at(at + r->off)), 2, 16, QChar(u'0'))
                                                    .arg(quint8(m_data.at(at + g->off)), 2, 16, QChar(u'0'))
                                                    .arg(quint8(m_data.at(at + b->off)), 2, 16, QChar(u'0'))}};
                continue;
            }
        }
        const int size = m.size * m.count;
        if (size > 8)
            continue;
        const qint64 v = readInt(at + m.off, size, builtinSigned(m.kind));
        out << QVariantMap{{u"key"_s, prefix + m.name}, {u"label"_s, pretty(m.name)}, {u"group"_s, group},
                           {u"kind"_s, m.kind == 0xFDE8 ? u"bool"_s : u"number"_s},
                           {u"value"_s, m.kind == 0xFDE8 ? QVariant(v != 0) : QVariant(double(v))}, {u"min"_s, 0},
                           {u"max"_s, size >= 4 ? 2147483647 : int((qint64(1) << (8 * size)) - 1)},
                           {u"readOnly"_s, false}, {u"slider"_s, false}, {u"offset"_s, u"+0x"_s + QString::number(m.off, 16).toUpper()}};
    }
    return out;
}

bool Rl26CareerEditorManager::setObjectField(qsizetype at, int type, const QString& key, const QVariant& value) {
    if (type < 0)
        return false;
    const Member* r = member(type, key + u"_r"_s);
    if (r && value.toString().startsWith(u'#')) {
        const Member* g = member(type, key + u"_g"_s);
        const Member* b = member(type, key + u"_b"_s);
        if (!g || !b)
            return false;
        const uint rgb = QStringView(value.toString()).mid(1, 6).toUInt(nullptr, 16);
        m_data[at + r->off] = char((rgb >> 16) & 0xFF);
        m_data[at + g->off] = char((rgb >> 8) & 0xFF);
        m_data[at + b->off] = char(rgb & 0xFF);
        edited();
        return true;
    }
    const Member* m = member(type, key);
    if (!m || m->type >= 0 || m->size * m->count > 8)
        return false;
    writeInt(at + m->off, m->size * m->count, m->kind == 0xFDE8 ? (value.toBool() ? 1 : 0) : value.toLongLong());
    edited();
    return true;
}

QVariantList Rl26CareerEditorManager::appearanceFields(int player) const {
    const qsizetype at = playerAt(player);
    if (at < 0)
        return {};
    // paPlayer's visual config: the type of the unnamed member after its count (0x860d1a1b).
    const Ref v = resolve({at, m_playerArray.type, 0, 1, 0}, u"paPlayer.@860d1a1b"_s);
    return v.ok() ? objectFields(v.at, v.type, QString(), u"Appearance"_s) : QVariantList();
}

bool Rl26CareerEditorManager::setAppearanceField(int player, const QString& key, const QVariant& value) {
    const qsizetype at = playerAt(player);
    const Ref v = at >= 0 ? resolve({at, m_playerArray.type, 0, 1, 0}, u"paPlayer.@860d1a1b"_s) : Ref();
    return v.ok() && setObjectField(v.at, v.type, key, value);
}

// =================================================================================================================
// Roster / lineups / jerseys
// =================================================================================================================

QList<int> Rl26CareerEditorManager::rosterIds(int team) const {
    QList<int> out;
    const qsizetype at = clubAt(team);
    if (at < 0)
        return out;
    const Ref club{at, m_clubArray.type, 0, 1, 0};
    const Ref count = resolve(club, u"paClub.@dba90fa9"_s);
    const Ref ids = resolve(club, u"paClub.@d72f45a8"_s);
    if (!count.ok() || !ids.ok())
        return out;
    const int n = qMin(int(readInt(count.at, 2)), ids.count);
    for (int i = 0; i < n; ++i)
        out << int(readInt(ids.at + 4 * i, 4));
    return out;
}

QVariantList Rl26CareerEditorManager::rosterRows(int team) const {
    QVariantList out;
    const QList<int> ids = rosterIds(team);
    for (int i = 0; i < ids.size(); ++i) {
        QVariantMap m = playerInfo(ids.at(i));
        m[u"slot"_s] = i;
        out << m;
    }
    return out;
}

namespace {
void writeRoster(QByteArray& data, qsizetype countAt, qsizetype idsAt, int max, const QList<int>& ids) {
    for (int i = 0; i < max; ++i) {
        const quint32 v = i < ids.size() ? quint32(ids.at(i)) : 0;
        qToLittleEndian(v, reinterpret_cast<uchar*>(data.data()) + idsAt + 4 * i);
    }
    qToLittleEndian(quint16(ids.size()), reinterpret_cast<uchar*>(data.data()) + countAt);
}
} // namespace

bool Rl26CareerEditorManager::addRosterPlayer(int team, int playerId) {
    const qsizetype at = clubAt(team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || playerId <= 0)
        return false;
    if (ids.size() >= rosterMax()) {
        setStatus(false, u"The roster is full (%1 players)."_s.arg(rosterMax()));
        return false;
    }
    if (ids.contains(playerId)) {
        setStatus(false, u"That player is already in the roster."_s);
        return false;
    }
    ids << playerId;
    const Ref club{at, m_clubArray.type, 0, 1, 0};
    writeRoster(m_data, resolve(club, u"paClub.@dba90fa9"_s).at, resolve(club, u"paClub.@d72f45a8"_s).at, rosterMax(), ids);
    edited();
    return true;
}

bool Rl26CareerEditorManager::removeRosterPlayer(int team, int slot) {
    const qsizetype at = clubAt(team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || slot < 0 || slot >= ids.size())
        return false;
    ids.removeAt(slot);
    const Ref club{at, m_clubArray.type, 0, 1, 0};
    writeRoster(m_data, resolve(club, u"paClub.@dba90fa9"_s).at, resolve(club, u"paClub.@d72f45a8"_s).at, rosterMax(), ids);
    edited();
    return true;
}

bool Rl26CareerEditorManager::moveRosterPlayer(int team, int slot, int delta) {
    const qsizetype at = clubAt(team);
    QList<int> ids = rosterIds(team);
    const int to = slot + delta;
    if (at < 0 || slot < 0 || slot >= ids.size() || to < 0 || to >= ids.size())
        return false;
    ids.move(slot, to);
    const Ref club{at, m_clubArray.type, 0, 1, 0};
    writeRoster(m_data, resolve(club, u"paClub.@dba90fa9"_s).at, resolve(club, u"paClub.@d72f45a8"_s).at, rosterMax(), ids);
    edited();
    return true;
}

bool Rl26CareerEditorManager::sortRoster(int team) {
    const qsizetype at = clubAt(team);
    QList<int> ids = rosterIds(team);
    if (at < 0 || ids.isEmpty())
        return false;
    std::stable_sort(ids.begin(), ids.end(), [this](int a, int b) {
        const QVariantMap pa = playerInfo(a), pb = playerInfo(b);
        return pa.value(u"last"_s).toString().compare(pb.value(u"last"_s).toString(), Qt::CaseInsensitive) < 0;
    });
    const Ref club{at, m_clubArray.type, 0, 1, 0};
    writeRoster(m_data, resolve(club, u"paClub.@dba90fa9"_s).at, resolve(club, u"paClub.@d72f45a8"_s).at, rosterMax(), ids);
    edited();
    return true;
}

QVariantList Rl26CareerEditorManager::pickablePlayers(const QString& filter) const {
    QVariantList out;
    const QString f = filter.trimmed();
    // Who's in which club's roster.
    QHash<int, QString> clubOf;
    for (int c = 0; c < m_clubs.size(); ++c) {
        const QString name = fieldValue(m_clubs.at(c), safe(teamDef(u"fullName"_s))).toString();
        for (const auto& idList = rosterIds(c); int id : idList)
            clubOf.insert(id, name);
    }
    for (int i = 0; i < m_players.size(); ++i) {
        const int id = playerId(i);
        const QString name = playerName(id);
        if (!f.isEmpty() && !name.contains(f, Qt::CaseInsensitive) && QString::number(id) != f)
            continue;
        QVariantMap m = playerInfo(id);
        m[u"club"_s] = clubOf.value(id, u"No club"_s);
        m[u"custom"_s] = true;
        out << m;
    }
    return out;
}

Rl26CareerEditorManager::Ref Rl26CareerEditorManager::lineupRef(int team, int list) const {
    const qsizetype at = clubAt(team);
    if (at < 0 || list < 0)
        return {};
    return resolve({at, m_clubArray.type, 0, 1, 0}, u"paClub.@480bed19#%1"_s.arg(list));
}

QVariantList Rl26CareerEditorManager::matchLists(int team) const {
    QVariantList out;
    const qsizetype at = clubAt(team < 0 ? 0 : team);
    if (at < 0)
        return out;
    const Ref count = resolve({at, m_clubArray.type, 0, 1, 0}, u"paClub.@871eab72"_s);
    const int n = qMin(8, int(readInt(count.at, 2)));
    for (int i = 0; i < n; ++i) {
        const Ref l = lineupRef(team < 0 ? 0 : team, i);
        const QString name = readText(resolve(l, u"m_name"_s).at, 16);
        out << QVariantMap{{u"list"_s, u"L%1"_s.arg(i)}, {u"title"_s, u"%1. %2"_s.arg(i + 1).arg(name.isEmpty() ? u"Lineup"_s : name)},
                           {u"shirts"_s, false}};
    }
    return out;
}

bool Rl26CareerEditorManager::listPresent(int team, const QString& list) const {
    const int i = QStringView(list).mid(1).toInt();
    const qsizetype at = clubAt(team);
    if (at < 0)
        return false;
    return i < int(readInt(resolve({at, m_clubArray.type, 0, 1, 0}, u"paClub.@871eab72"_s).at, 2));
}

int Rl26CareerEditorManager::lineupCount(int team) const {
    const qsizetype at = clubAt(team);
    return at < 0 ? 0 : qMin(8, int(readInt(resolve({at, m_clubArray.type, 0, 1, 0}, u"paClub.@871eab72"_s).at, 2)));
}

bool Rl26CareerEditorManager::setListPresent(int team, const QString& list, bool on) {
    const int i = QStringView(list).mid(1).toInt();
    const int count = lineupCount(team);
    if (on || i < 0 || i >= count)
        return on && i < count;
    if (count <= 1) {
        setStatus(false, u"A club keeps at least one lineup."_s);
        return false;
    }
    // Remove it: the later lineups move up, the last slot is cleared.
    const Ref first = lineupRef(team, 0);
    const qsizetype at = first.at + qsizetype(i) * first.size;
    const qsizetype end = first.at + qsizetype(count) * first.size;
    std::memmove(m_data.data() + at, m_data.constData() + at + first.size, size_t(end - at - first.size));
    std::memset(m_data.data() + end - first.size, 0, size_t(first.size));
    writeInt(resolve({clubAt(team), m_clubArray.type, 0, 1, 0}, u"paClub.@871eab72"_s).at, 2, count - 1);
    edited();
    return true;
}

QString Rl26CareerEditorManager::lineupName(int team, const QString& list) const {
    const Ref l = lineupRef(team, QStringView(list).mid(1).toInt());
    const Ref name = resolve(l, u"m_name"_s);
    return name.ok() ? readText(name.at, name.size * name.count) : QString();
}

bool Rl26CareerEditorManager::setLineupName(int team, const QString& list, const QString& name) {
    const int i = QStringView(list).mid(1).toInt();
    if (i < 0 || i >= lineupCount(team) || name == lineupName(team, list))
        return false;
    const Ref n = resolve(lineupRef(team, i), u"m_name"_s);
    if (!n.ok())
        return false;
    writeText(n.at, n.size * n.count, name);
    edited();
    return true;
}

int Rl26CareerEditorManager::addLineup(int team) {
    const int count = lineupCount(team);
    if (count <= 0 || count >= 8) {
        setStatus(false, count >= 8 ? u"A club has at most 8 lineups."_s : u"The club has no lineup to copy."_s);
        return -1;
    }
    const Ref first = lineupRef(team, 0);
    std::memmove(m_data.data() + first.at + qsizetype(count) * first.size, m_data.constData() + first.at, size_t(first.size));
    writeInt(resolve({clubAt(team), m_clubArray.type, 0, 1, 0}, u"paClub.@871eab72"_s).at, 2, count + 1);
    const Ref n = resolve(lineupRef(team, count), u"m_name"_s);
    if (n.ok())
        writeText(n.at, n.size * n.count, u"Lineup %1"_s.arg(count + 1));
    edited();
    return count;
}

QVariantList Rl26CareerEditorManager::lineupRows(int team, const QString& list) const {
    QVariantList rows;
    const Ref l = lineupRef(team, QStringView(list).mid(1).toInt());
    if (!l.ok())
        return rows;
    const Ref players = resolve(l, u"@c179eadf"_s);
    const int count = int(readInt(resolve(l, u"m_playersCount"_s).at, 2));
    static const QStringList league = {u"Fullback"_s, u"Left Wing"_s, u"Left Centre"_s, u"Right Centre"_s, u"Right Wing"_s,
                                       u"Five-Eighth"_s, u"Halfback"_s, u"Left Prop"_s, u"Hooker"_s, u"Right Prop"_s,
                                       u"Left Second Row"_s, u"Right Second Row"_s, u"Lock"_s};
    // The lineup's players (17 standard, 14 nines); its names as the game shows them.
    const bool nines = count < 17;
    for (int i = 0; i < qMin(qMax(count, 1), players.count); ++i) {
        const int id = int(readInt(players.at + 4 * i, 4));
        QVariantMap row = playerInfo(id);
        QStringList roles;
        for (const auto& rList = row.value(u"roles"_s).toList(); const QVariant& r : rList)
            roles << kRoleShort.value(r.toInt());
        row[u"roles"_s] = roles.join(u" / "_s);
        row[u"present"_s] = true;
        row[u"key"_s] = u"p:%1"_s.arg(i);
        row[u"position"_s] = nines ? u"%1 - %2"_s.arg(i + 1).arg(i < 9 ? u"Nines"_s : u"Interchange %1"_s.arg(i - 8))
                                   : u"%1 - %2"_s.arg(i + 1).arg(i < league.size() ? league.at(i) : u"Interchange %1"_s.arg(i - league.size() + 1));
        rows << row;
    }
    return rows;
}

QVariantList Rl26CareerEditorManager::teamRoles(int team, const QString& list) const {
    QVariantList rows;
    const Ref l = lineupRef(team, QStringView(list).mid(1).toInt());
    if (!l.ok())
        return rows;
    const Ref roles = resolve(l, u"@1da459df"_s);
    for (int r = 0; r < roles.count; ++r) {
        QVariantMap row = playerInfo(int(readInt(roles.at + 4 * r, 4)));
        QStringList rs;
        for (const auto& xList = row.value(u"roles"_s).toList(); const QVariant& x : xList)
            rs << kRoleShort.value(x.toInt());
        row[u"roles"_s] = rs.join(u" / "_s);
        row[u"present"_s] = true;
        row[u"key"_s] = u"r:%1"_s.arg(r);
        row[u"name"_s] = kLineupRoles.value(r, u"Role %1"_s.arg(r + 1));
        rows << row;
    }
    return rows;
}

bool Rl26CareerEditorManager::setLineupSlot(int team, const QString& list, const QString& key, int playerId) {
    const Ref l = lineupRef(team, QStringView(list).mid(1).toInt());
    const QStringList parts = key.split(u':');
    if (!l.ok() || parts.size() != 2)
        return false;
    const int slot = parts.at(1).toInt();
    if (parts.at(0) == u"r"_s) {
        const Ref roles = resolve(l, u"@1da459df"_s);
        if (slot < 0 || slot >= roles.count)
            return false;
        writeInt(roles.at + 4 * slot, 4, playerId);
    } else {
        const Ref players = resolve(l, u"@c179eadf"_s);
        if (slot < 0 || slot >= players.count)
            return false;
        writeInt(players.at + 4 * slot, 4, playerId);
        const Ref count = resolve(l, u"m_playersCount"_s);
        if (readInt(count.at, 2) <= slot)
            writeInt(count.at, 2, slot + 1);
    }
    edited();
    return true;
}

QVariantList Rl26CareerEditorManager::pickerRows(const QString& filter, int team, bool squadOnly) const {
    QVariantList out;
    const auto toRow = [](const QVariantMap& m) {
        return QVariantMap{{u"id"_s, m.value(u"id"_s)}, {u"first"_s, m.value(u"first"_s)}, {u"last"_s, m.value(u"last"_s)},
                           {u"clubName"_s, m.value(u"club"_s)}, {u"roleName"_s, m.value(u"role"_s)},
                           {u"age"_s, m.value(u"age"_s)}, {u"rating"_s, m.value(u"rating"_s)}};
    };
    if (squadOnly) {
        const QString f = filter.trimmed();
        for (const auto& idList = rosterIds(team); int id : idList) {
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

// The C# editors' Generate Best Lineup, for each of the club's lineups: positions by preferred role (the primary role
// first, then the secondary / tertiary), the best rated first; captain the best rated, goal kicker the best place kick,
// play makers the best punt kicks. Nines lineups (fewer than 17 players) in the order of the game's own.
QString Rl26CareerEditorManager::generateBestLineup(int team) {
    if (clubAt(team) < 0)
        return u"No team."_s;
    struct P {
        int id, rating, roles[3], place, punt;
    };
    QList<P> players;
    for (const auto& idList = rosterIds(team); int id : idList) {
        const QVariantMap m = playerInfo(id);
        if (!m.value(u"known"_s).toBool())
            continue;
        const QVariantList roles = m.value(u"roles"_s).toList();
        players << P{id, m.value(u"rating"_s).toInt(), {roles.value(0, -1).toInt(), roles.value(1, -1).toInt(), roles.value(2, -1).toInt()},
                     m.value(u"placeKick"_s).toInt(), m.value(u"puntKick"_s).toInt()};
    }
    if (players.isEmpty())
        return u"The roster is empty."_s;
    std::stable_sort(players.begin(), players.end(), [](const P& a, const P& b) { return a.rating > b.rating; });
    // Roles: 0 fullback, 1 winger, 2 centre, 3 five-eighth, 4 halfback, 5 prop, 6 hooker, 7 second row, 8 lock.
    static const int standard[19] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 5, 7, 7, 8, 5, 5, 7, 7, 6, 2};
    static const int nines[14] = {0, 1, 2, 3, 1, 3, 4, 7, 7, 5, 8, 2, 5, 5};   // The game's own nines lineups' order.
    QStringList missing;
    for (int list = 0; list < lineupCount(team); ++list) {
        const Ref l = lineupRef(team, list);
        const Ref ids = resolve(l, u"@c179eadf"_s);
        const Ref roles = resolve(l, u"@1da459df"_s);
        const int count = qMin(int(readInt(resolve(l, u"m_playersCount"_s).at, 2)), ids.count);
        if (count <= 0)
            continue;
        const int* order = count >= 17 ? standard : nines;
        const int orderSize = count >= 17 ? 19 : 14;
        QList<int> used;
        for (int slot = 0; slot < count; ++slot) {
            const int role = order[qMin(slot, orderSize - 1)];
            int pick = 0;
            for (int which = 0; which < 3 && pick == 0; ++which)
                for (const P& p : std::as_const(players))
                    if (p.roles[which] == role && !used.contains(p.id)) {
                        pick = p.id;
                        break;
                    }
            if (pick == 0)   // Anyone left, the best rated.
                for (const P& p : std::as_const(players))
                    if (!used.contains(p.id)) {
                        pick = p.id;
                        break;
                    }
            if (pick == 0) {
                missing << u"slot %1"_s.arg(slot + 1);
                continue;
            }
            used << pick;
            writeInt(ids.at + 4 * slot, 4, pick);
        }
        QList<P> in;
        for (const P& p : std::as_const(players))
            if (used.contains(p.id))
                in << p;
        if (in.isEmpty() || !roles.ok())
            continue;
        QList<P> place = in, punt = in;
        std::stable_sort(place.begin(), place.end(), [](const P& a, const P& b) { return a.place > b.place; });
        std::stable_sort(punt.begin(), punt.end(), [](const P& a, const P& b) { return a.punt > b.punt; });
        writeInt(roles.at, 4, in.at(0).id);
        writeInt(roles.at + 4, 4, place.at(0).id);
        writeInt(roles.at + 8, 4, punt.at(0).id);
        writeInt(roles.at + 12, 4, punt.value(1, punt.at(0)).id);
    }
    edited();
    missing.removeDuplicates();
    return missing.isEmpty() ? QString() : u"Not enough players in the roster for: "_s + missing.join(u", "_s) + u'.';
}

QVariantMap Rl26CareerEditorManager::jerseys(int team) const {
    QVariantMap out{{u"ok"_s, false}};
    const qsizetype at = clubAt(team);
    if (at < 0)
        return out;
    const Ref club{at, m_clubArray.type, 0, 1, 0};
    const int count = qMin(20, int(readInt(resolve(club, u"paClub.@ede692af"_s).at, 2)));
    QVariantList uniforms;
    for (int u = 0; u < count; ++u) {
        const Ref j = resolve(club, u"paClub.@cb5d8166#%1"_s.arg(u));
        uniforms << QVariantMap{{u"index"_s, u}, {u"name"_s, readText(resolve(j, u"name"_s).at, 25)},
                                {u"hidden"_s, readInt(resolve(j, u"isHidden"_s).at, 1) != 0}};
    }
    out.insert(u"ok"_s, true);
    out.insert(u"count"_s, count);
    out.insert(u"max"_s, 20);
    out.insert(u"uniforms"_s, uniforms);
    out.insert(u"slots"_s, QVariantList());
    return out;
}

QVariantList Rl26CareerEditorManager::jerseyFields(int team, int uniform) const {
    const qsizetype at = clubAt(team);
    if (at < 0)
        return {};
    const Ref j = resolve({at, m_clubArray.type, 0, 1, 0}, u"paClub.@cb5d8166#%1"_s.arg(uniform));
    const Ref config = resolve(j, u"m_config"_s);
    if (!config.ok())
        return {};
    QVariantList out = objectFields(config.at, config.type, u"j:%1:"_s.arg(uniform), u"Jersey"_s);
    // Group them like the Database Editor (decals, pants, socks, ...).
    for (QVariant& v : out) {
        QVariantMap m = v.toMap();
        const QString key = m.value(u"key"_s).toString().section(u':', 2);
        m[u"group"_s] = key.startsWith(u"pantsDecal"_s) ? u"Pants Decals"_s
                      : key.startsWith(u"decal"_s) ? u"Decals"_s
                      : key.startsWith(u"uniformTop"_s) ? u"Top"_s
                      : key.startsWith(u"uniformBottom"_s) ? u"Shorts"_s
                      : key.startsWith(u"uniformSocks"_s) || key.startsWith(u"sock"_s) ? u"Socks"_s
                      : key.startsWith(u"number"_s) || key.startsWith(u"name"_s) || key.startsWith(u"playerName"_s) ? u"Numbers & Names"_s
                      : u"Jersey"_s;
        v = m;
    }
    return out;
}

bool Rl26CareerEditorManager::setJerseyField(int team, const QString& key, const QVariant& value) {
    const QStringList parts = key.split(u':');
    const qsizetype at = clubAt(team);
    if (at < 0 || parts.size() != 3 || parts.at(0) != u"j"_s)
        return false;
    const Ref j = resolve({at, m_clubArray.type, 0, 1, 0}, u"paClub.@cb5d8166#%1"_s.arg(parts.at(1).toInt()));
    const Ref config = resolve(j, u"m_config"_s);
    return config.ok() && setObjectField(config.at, config.type, parts.at(2), value);
}

// =================================================================================================================
// Career pages
// =================================================================================================================

QVariantList Rl26CareerEditorManager::careerItems() const {
    QVariantList out;
    if (!m_loaded)
        return out;
    const Ref root = rootRef();
    const bool player = readInt(resolve(root, u"baseSchema.m_user.m_isPlayer"_s).at, 1) != 0;
    const int club = int(readInt(resolve(root, u"baseSchema.m_user.m_coachClubId"_s).at, 4));
    const auto item = [](int i, const QString& key, const QString& title, const QString& subtitle, const QStringList& tabs) {
        return QVariantMap{{u"index"_s, i}, {u"key"_s, key}, {u"title"_s, title}, {u"subtitle"_s, subtitle}, {u"tabs"_s, tabs}};
    };
    const int retired = int(readInt(resolve(root, u"m_retirement.@e702b91b"_s).at, 2));
    out << item(0, u"settings"_s, u"Career Settings"_s,
                (player ? u"Player career"_s : u"Coach career"_s) + u"  ·  %1 season"_s.arg(m_careerYear), {u"Settings"_s});
    out << item(1, u"selection"_s, u"Team & Player Selection"_s, clubName(club), {u"Selection"_s});
    out << item(2, u"status"_s, u"Player Status"_s, u"%1 injured, suspended or away"_s.arg(statusRows().size()), {u"Player Status"_s});
    out << item(3, u"contracts"_s, u"Player Contracts"_s, u"%1 contract changes"_s.arg(contractRows().size()), {u"Contracts"_s});
    out << item(4, u"retirements"_s, u"Retirements"_s, u"%1 players"_s.arg(retired), {u"Retirements"_s});
    out << item(5, u"competitions"_s, u"Competitions"_s, u"Ladders and fixtures"_s, {u"Ladder"_s, u"Fixtures"_s});
    return out;
}

QVariantList Rl26CareerEditorManager::statusRows() const {
    QVariantList out;
    const Ref players = resolve(rootRef(), u"baseSchema.m_players"_s);
    const Ref arr = child(players, u"@7adc0911"_s);
    const Member* am = member(players.type, 0x7adc0911u);
    if (!arr.ok() || !am)
        return out;
    const int count = qMin<int>(int(readInt(child(players, u"@3289eeea"_s).at, 2)), am->count);
    static const QStringList severity = {QString(), u"Light"_s, u"Minor"_s, u"Medium"_s, u"Large"_s, u"Serious"_s};
    for (int i = 0; i < count; ++i) {
        const Ref e{arr.at + qsizetype(i) * arr.size, arr.type, 0, 1, 0};
        const int id = int(readInt(resolve(e, u"id"_s).at, 4));
        const int reason = int(readInt(resolve(e, u"reason"_s).at, 1));
        const int duration = int(readInt(resolve(e, u"duration"_s).at, 2));
        const int a = int(readInt(resolve(e, u"additionalA"_s).at, 2));
        const int b = int(readInt(resolve(e, u"additionalB"_s).at, 2));
        const QVariantMap info = playerInfo(id);
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"name"_s, info.value(u"name"_s)}, {u"role"_s, info.value(u"role"_s)},
                           {u"rating"_s, info.value(u"rating"_s)}, {u"reason"_s, reason},
                           {u"reasonName"_s, kStatusReasons.value(reason, u"(%1)"_s.arg(reason))},
                           {u"duration"_s, duration}, {u"unit"_s, reason == 3 ? QString() : u"days"_s},
                           {u"detail"_s, reason == 1 ? severity.value(b, u"Severity %1"_s.arg(b)) + u" (type %1)"_s.arg(a) : QString()},
                           {u"playerIndex"_s, info.value(u"playerIndex"_s, -1)}};
    }
    return out;
}

bool Rl26CareerEditorManager::removeStatus(int index) {
    const Ref players = resolve(rootRef(), u"baseSchema.m_players"_s);
    const Ref arr = child(players, u"@7adc0911"_s);
    const Ref countRef = child(players, u"@3289eeea"_s);
    const int count = int(readInt(countRef.at, 2));
    if (!arr.ok() || index < 0 || index >= count)
        return false;
    const qsizetype from = arr.at + qsizetype(index + 1) * arr.size, to = arr.at + qsizetype(index) * arr.size;
    std::memmove(m_data.data() + to, m_data.constData() + from, size_t(qsizetype(count - index - 1) * arr.size));
    std::memset(m_data.data() + arr.at + qsizetype(count - 1) * arr.size, 0, size_t(arr.size));
    writeInt(countRef.at, 2, count - 1);
    edited();
    return true;
}

QVariantList Rl26CareerEditorManager::contractRows() const {
    QVariantList out;
    const Ref rec = resolve(rootRef(), u"m_recruiting"_s);
    const Ref arr = child(rec, u"@c41b7a94"_s);
    if (!arr.ok())
        return out;
    const int count = int(readInt(child(rec, u"@5a12360d"_s).at, 2));
    for (int i = 0; i < count; ++i) {
        const Ref e{arr.at + qsizetype(i) * arr.size, arr.type, 0, 1, 0};
        const int id = int(readInt(resolve(e, u"playerId"_s).at, 4));
        const int from = int(readInt(resolve(e, u"fromClubId"_s).at, 4));
        const int to = int(readInt(resolve(e, u"toClubId"_s).at, 4));
        const QVariantMap info = playerInfo(id);
        const int p = info.value(u"playerIndex"_s).toInt();
        const FieldDef* cur = playerDef(u"contractExpires"_s);
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"name"_s, info.value(u"name"_s)}, {u"role"_s, info.value(u"role"_s)},
                           {u"rating"_s, info.value(u"rating"_s)},
                           {u"currentContract"_s, p >= 0 && cur ? fieldValue(playerAt(p), *cur).toInt() : 0},
                           {u"currentTeam"_s, from}, {u"currentTeamName"_s, clubName(from)}, {u"newTeam"_s, to},
                           {u"newTeamName"_s, clubName(to)},
                           {u"newContract"_s, int(readInt(resolve(e, u"contract.expiresAtEndOfYear"_s).at, 2))},
                           {u"newSalary"_s, int(readInt(resolve(e, u"contract.amountPerYear"_s).at, 4))},
                           {u"playerIndex"_s, p}};
    }
    return out;
}

bool Rl26CareerEditorManager::removeContract(int index) {
    const Ref rec = resolve(rootRef(), u"m_recruiting"_s);
    const Ref arr = child(rec, u"@c41b7a94"_s);
    const Ref countRef = child(rec, u"@5a12360d"_s);
    const int count = int(readInt(countRef.at, 2));
    if (!arr.ok() || index < 0 || index >= count)
        return false;
    const qsizetype from = arr.at + qsizetype(index + 1) * arr.size, to = arr.at + qsizetype(index) * arr.size;
    std::memmove(m_data.data() + to, m_data.constData() + from, size_t(qsizetype(count - index - 1) * arr.size));
    std::memset(m_data.data() + arr.at + qsizetype(count - 1) * arr.size, 0, size_t(arr.size));
    writeInt(countRef.at, 2, count - 1);
    edited();
    return true;
}

QVariantList Rl26CareerEditorManager::retirementRows() const {
    QVariantList out;
    const Ref ret = resolve(rootRef(), u"m_retirement"_s);
    const Ref arr = child(ret, u"@0fc6596a"_s);
    if (!arr.ok())
        return out;
    const int count = qMin(1024, int(readInt(child(ret, u"@e702b91b"_s).at, 2)));
    for (int i = 0; i < count; ++i) {
        const Ref e{arr.at + qsizetype(i) * arr.size, arr.type, 0, 1, 0};
        const int id = int(readInt(resolve(e, u"playerId"_s).at, 4));
        const QVariantMap info = playerInfo(id);
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, id}, {u"name"_s, info.value(u"name"_s)}, {u"role"_s, info.value(u"role"_s)},
                           {u"rating"_s, info.value(u"rating"_s)}, {u"age"_s, info.value(u"age"_s)},
                           {u"reason"_s, int(readInt(resolve(e, u"reason"_s).at, 1))},
                           {u"playerIndex"_s, info.value(u"playerIndex"_s, -1)}};
    }
    return out;
}

QVariantMap Rl26CareerEditorManager::retirementDate() const {
    const Ref d = resolve(rootRef(), u"m_retirement.m_nextRetirementTick"_s);
    return {{u"year"_s, int(readInt(resolve(d, u"year"_s).at, 2))}, {u"month"_s, int(readInt(resolve(d, u"month"_s).at, 1))},
            {u"day"_s, int(readInt(resolve(d, u"day"_s).at, 1))}};
}

bool Rl26CareerEditorManager::setRetirementReason(int index, int reason) {
    const Ref ret = resolve(rootRef(), u"m_retirement"_s);
    const Ref arr = child(ret, u"@0fc6596a"_s);
    const int count = int(readInt(child(ret, u"@e702b91b"_s).at, 2));
    if (!arr.ok() || index < 0 || index >= count)
        return false;
    writeInt(resolve({arr.at + qsizetype(index) * arr.size, arr.type, 0, 1, 0}, u"reason"_s).at, 1, reason);
    edited();
    return true;
}

bool Rl26CareerEditorManager::removeRetirement(int index) {
    const Ref ret = resolve(rootRef(), u"m_retirement"_s);
    const Ref arr = child(ret, u"@0fc6596a"_s);
    const Ref countRef = child(ret, u"@e702b91b"_s);
    const int count = int(readInt(countRef.at, 2));
    if (!arr.ok() || index < 0 || index >= count)
        return false;
    const qsizetype from = arr.at + qsizetype(index + 1) * arr.size, to = arr.at + qsizetype(index) * arr.size;
    std::memmove(m_data.data() + to, m_data.constData() + from, size_t(qsizetype(count - index - 1) * arr.size));
    std::memset(m_data.data() + arr.at + qsizetype(count - 1) * arr.size, 0, size_t(arr.size));
    writeInt(countRef.at, 2, count - 1);
    edited();
    return true;
}

// =================================================================================================================
// Competitions
// =================================================================================================================

// Each of root.m_competitions' slots is u8 competition id + a buffer holding one of the competition layouts (a type
// with a "competition" member). The layout is the one whose stages (members holding a fixture config) all read as
// stages: two u16 count + list pairs (the match list first), then the stage's name.
void Rl26CareerEditorManager::buildStages() {
    m_stages.clear();
    const Ref comps = resolve(rootRef(), u"m_competitions"_s);
    if (!comps.ok())
        return;
    int competitionType = -1, configType = -1;
    QList<QPair<int, quint32>> layouts;   // (type, offset of its competition)
    for (int t = 0; t < m_types.size(); ++t)
        for (const auto& mList = m_types.at(t).members; const Member& m : mList)
            if (m.name == u"competition"_s && m.type >= 0) {
                layouts << qMakePair(t, m.off);
                competitionType = m.type;
            }
    if (competitionType < 0)
        return;
    if (const Member* fc = member(competitionType, u"fixtureConfig"_s))
        configType = fc->type;
    const auto stageMembers = [&](int type) {
        QList<const Member*> out;
        for (const auto& mList = m_types.at(type).members; const Member& m : mList) {
            if (m.type < 0 || m.type == competitionType)
                continue;
            for (const auto& sList = m_types.at(m.type).members; const Member& s : sList)
                if (s.type == configType && configType >= 0) {
                    out << &m;
                    break;
                }
        }
        std::sort(out.begin(), out.end(), [](const Member* a, const Member* b) { return a->off < b->off; });
        return out;
    };
    // A stage's match list and name: members sorted by offset - u16 count, u16 list, u16 count, u16 list, bool, name.
    const auto readStage = [&](qsizetype at, int type, QList<int>* matches, QString* name, qsizetype* config) {
        QList<Member> ms = m_types.at(type).members;
        std::sort(ms.begin(), ms.end(), [](const Member& a, const Member& b) { return a.off < b.off; });
        if (ms.size() < 6 || ms.at(0).kind != 0xFDED || ms.at(1).kind != 0xFDED)
            return false;
        const int count = int(readInt(at + ms.at(0).off, 2));
        if (count > ms.at(1).count)
            return false;
        QList<int> list;
        for (int i = 0; i < count; ++i) {
            const int m = int(readInt(at + ms.at(1).off + 2 * i, 2));
            if (m >= m_matchArray.count && m_matchArray.count > 0)
                return false;
            list << m;
        }
        QString text;
        for (const Member& m : std::as_const(ms))
            if (m.kind == 0xFDEB && m.count == 64) {
                const QByteArray raw = m_data.mid(at + m.off, 64);
                const int end = int(raw.indexOf('\0'));
                if (end <= 0)
                    return false;
                for (int i = 0; i < 64; ++i)
                    if ((i < end && (uchar(raw.at(i)) < 32 || uchar(raw.at(i)) > 126)) || (i > end && raw.at(i) != 0))
                        return false;
                text = QString::fromLatin1(raw.left(end));
                break;
            }
        if (text.isEmpty())
            return false;
        *matches = list;
        *name = text;
        *config = -1;
        for (const Member& m : std::as_const(ms))
            if (m.type == configType)
                *config = at + m.off;
        return true;
    };

    // Slot order (the members by offset).
    QList<Member> compSlots = m_types.at(comps.type).members;
    std::sort(compSlots.begin(), compSlots.end(), [](const Member& a, const Member& b) { return a.off < b.off; });
    for (int s = 0; s < compSlots.size(); ++s) {
        const Member& slot = compSlots.at(s);
        const qsizetype at = comps.at + slot.off;
        const int compId = quint8(m_data.at(at));
        const qsizetype buffer = at + 1;
        const Member* idMember = member(competitionType, u"id"_s);
        const Member* nameMember = member(competitionType, u"m_competitionDisplayName"_s);
        const Member* keyMember = member(competitionType, u"name"_s);
        QList<Stage> best;
        for (const auto& [type, compOff] : std::as_const(layouts)) {
            const qsizetype c = buffer + compOff;
            if (!idMember || quint8(m_data.at(c + idMember->off)) != compId)
                continue;
            QList<Stage> stages;
            bool ok = true;
            for (const auto& mList = stageMembers(type); const Member* m : mList) {
                Stage st;
                if (!readStage(buffer + m->off, m->type, &st.matches, &st.stageName, &st.config)) {
                    ok = false;
                    break;
                }
                st.compSlot = s;
                st.comp = slot.name;
                QString display = nameMember ? readText(c + nameMember->off, nameMember->count) : QString();
                if (display.isEmpty() && keyMember)
                    display = pretty(readText(c + keyMember->off, keyMember->count));
                st.name = display + u" - "_s + st.stageName;
                st.finals = st.stageName.contains(u"Final"_s, Qt::CaseInsensitive);
                // A stage's own points are zero unless it overrides the competition's (competition.fixtureConfig).
                if (const Member* fc = member(competitionType, u"fixtureConfig"_s);
                    fc && (st.config < 0 || m_data.at(st.config) == 0))
                    st.config = c + fc->off;
                stages << st;
            }
            if (ok && stages.size() > best.size())
                best = stages;
        }
        for (const Stage& st : std::as_const(best))
            if (!st.matches.isEmpty())
                m_stages << st;
    }
}

QVariantMap Rl26CareerEditorManager::readMatch(int match) const {
    QVariantMap m;
    if (!m_matchArray.ok() || match < 0 || match >= m_matchArray.count)
        return m;
    const Ref r{m_matchArray.at + qsizetype(match) * m_matchSize, m_matchArray.type, 0, 1, 0};
    const auto num = [&](const QString& path, int size) { return int(readInt(resolve(r, path).at, size)); };
    m[u"round"_s] = num(u"roundIndex"_s, 1);
    m[u"home"_s] = num(u"home"_s, 2);
    m[u"away"_s] = num(u"away"_s, 2);
    m[u"ground"_s] = num(u"ground"_s, 1);
    m[u"year"_s] = num(u"m_date.year"_s, 2);
    m[u"month"_s] = num(u"m_date.month"_s, 1);
    m[u"day"_s] = num(u"m_date.day"_s, 1);
    m[u"hour"_s] = num(u"m_date.hour"_s, 1);
    m[u"minute"_s] = num(u"m_date.minute"_s, 1);
    // isComplete is uninitialised (0x7F) in a match that hasn't been played: resultSet says it has.
    m[u"played"_s] = num(u"resultSet"_s, 1) == 1 && num(u"m_result.isComplete"_s, 1) == 1;
    m[u"homeScore"_s] = num(u"m_result.m_home.statistics.score"_s, 2);
    m[u"awayScore"_s] = num(u"m_result.m_away.statistics.score"_s, 2);
    return m;
}

QString Rl26CareerEditorManager::stadiumName(int ground, int homeClubId) const {
    if (ground >= 5)
        return Rl26Lists::stadiums().value(ground - 5, u"Stadium %1"_s.arg(ground));
    // Below 5: the home club's ground (its home grounds by slot).
    const int c = m_clubById.value(homeClubId, -1);
    if (c >= 0) {
        const FieldDef* level = teamDef(u"homeGrounds#%1:stadiumId"_s.arg(qBound(0, ground, 2)));
        if (level) {
            const QString custom = Rl26UserContent::customStadiumName(m_path, quint32(readInt(clubAt(c) + level->rel, 4)));
            if (!custom.isEmpty())
                return custom;
        }
        const FieldDef* d = teamDef(u"homeGrounds#%1:stadium"_s.arg(qBound(0, ground, 2)));
        if (d) {
            const int s = fieldValue(clubAt(c), *d).toInt();
            if (s >= 5)
                return Rl26Lists::stadiums().value(s - 5);
        }
    }
    return u"Home ground"_s;
}

QVariantList Rl26CareerEditorManager::ladderComps() const {
    QVariantList out;
    for (int i = 0; i < m_stages.size(); ++i) {
        const Stage& st = m_stages.at(i);
        if (st.finals || st.stageName.contains(u"Pre"_s, Qt::CaseInsensitive))
            continue;
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, st.name}, {u"logoUrl"_s, competitionLogoUrl(st.comp)}};
    }
    return out;
}

QVariantList Rl26CareerEditorManager::ladderRows(int comp) const {
    QVariantList out;
    if (comp < 0 || comp >= m_stages.size())
        return out;
    const Stage& st = m_stages.at(comp);
    int win = 2, bye = 2, draw = 1, loss = 0, top = 8;
    if (st.config >= 0) {
        win = quint8(m_data.at(st.config));
        bye = quint8(m_data.at(st.config + 1));
        draw = quint8(m_data.at(st.config + 2));
        loss = quint8(m_data.at(st.config + 3));
        top = quint8(m_data.at(st.config + 4));
    }
    struct Row { int id = 0, played = 0, wins = 0, draws = 0, lost = 0, byes = 0, pf = 0, pa = 0, hw = 0, hl = 0, aw = 0, al = 0; };
    QHash<int, Row> rows;
    QSet<int> roundsPlayed;
    QHash<int, QSet<int>> teamRounds;
    for (int m : std::as_const(st.matches)) {
        const QVariantMap x = readMatch(m);
        const int h = x.value(u"home"_s).toInt(), a = x.value(u"away"_s).toInt();
        rows[h].id = h;
        rows[a].id = a;
        if (!x.value(u"played"_s).toBool())
            continue;
        const int round = x.value(u"round"_s).toInt();
        roundsPlayed.insert(round);
        teamRounds[h].insert(round);
        teamRounds[a].insert(round);
        const int hs = x.value(u"homeScore"_s).toInt(), as = x.value(u"awayScore"_s).toInt();
        Row& rh = rows[h];
        Row& ra = rows[a];
        ++rh.played;
        ++ra.played;
        rh.pf += hs; rh.pa += as;
        ra.pf += as; ra.pa += hs;
        if (hs > as) { ++rh.wins; ++ra.lost; ++rh.hw; ++ra.al; }
        else if (hs < as) { ++ra.wins; ++rh.lost; ++ra.aw; ++rh.hl; }
        else { ++rh.draws; ++ra.draws; }
    }
    QList<Row> list;
    for (Row r : std::as_const(rows)) {
        r.byes = qMax(0, int(roundsPlayed.size()) - int(teamRounds.value(r.id).size()));
        list << r;
    }
    const auto pts = [&](const Row& r) { return r.wins * win + r.draws * draw + r.lost * loss + r.byes * bye; };
    std::sort(list.begin(), list.end(), [&](const Row& a, const Row& b) {
        if (pts(a) != pts(b)) return pts(a) > pts(b);
        if (a.pf - a.pa != b.pf - b.pa) return a.pf - a.pa > b.pf - b.pa;
        return a.pf > b.pf;
    });
    for (int i = 0; i < list.size(); ++i) {
        const Row& r = list.at(i);
        const int t = m_clubById.value(r.id, -1);
        out << QVariantMap{{u"pos"_s, i + 1}, {u"id"_s, r.id}, {u"teamIndex"_s, t}, {u"name"_s, clubName(r.id)},
                           {u"logoUrl"_s, t >= 0 ? teamLogoUrl(t) : QString()},
                           {u"abbreviation"_s, t >= 0 ? fieldValue(clubAt(t), safe(teamDef(u"hudName"_s))) : QVariant()},
                           {u"colour"_s, t >= 0 ? fieldValue(clubAt(t), safe(teamDef(u"primaryRGBColour"_s))) : QVariant()},
                           {u"played"_s, r.played}, {u"points"_s, pts(r)}, {u"wins"_s, r.wins}, {u"draws"_s, r.draws},
                           {u"lost"_s, r.lost}, {u"byes"_s, r.byes}, {u"for"_s, r.pf}, {u"against"_s, r.pa},
                           {u"diff"_s, r.pf - r.pa}, {u"home"_s, u"%1-%2"_s.arg(r.hw).arg(r.hl)},
                           {u"away"_s, u"%1-%2"_s.arg(r.aw).arg(r.al)}, {u"finals"_s, i < top}};
    }
    return out;
}

QVariantList Rl26CareerEditorManager::fixtureStages() const {
    QVariantList out;
    for (int i = 0; i < m_stages.size(); ++i)
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, m_stages.at(i).name}, {u"logoUrl"_s, competitionLogoUrl(m_stages.at(i).comp)}};
    return out;
}

QStringList Rl26CareerEditorManager::fixtureRounds(int stage) const {
    QStringList out;
    if (stage < 0 || stage >= m_stages.size())
        return out;
    QList<int> rounds;
    for (const auto& mList = m_stages.at(stage).matches; int m : mList) {
        const int r = readMatch(m).value(u"round"_s).toInt();
        if (!rounds.contains(r))
            rounds << r;
    }
    std::sort(rounds.begin(), rounds.end());
    const bool finals = m_stages.at(stage).finals;
    for (int i = 0; i < rounds.size(); ++i)
        out << (finals ? u"Week %1"_s.arg(i + 1) : u"Round %1"_s.arg(i + 1));
    return out;
}

QVariantList Rl26CareerEditorManager::fixtureRows(int stage, int round) const {
    QVariantList out;
    if (stage < 0 || stage >= m_stages.size())
        return out;
    QList<int> rounds;
    for (const auto& mList = m_stages.at(stage).matches; int m : mList) {
        const int r = readMatch(m).value(u"round"_s).toInt();
        if (!rounds.contains(r))
            rounds << r;
    }
    std::sort(rounds.begin(), rounds.end());
    if (round < 0 || round >= rounds.size())
        return out;
    for (const auto& nList = m_stages.at(stage).matches; int n : nList) {
        const QVariantMap m = readMatch(n);
        if (m.value(u"round"_s).toInt() != rounds.at(round))
            continue;
        const int h = m_clubById.value(m.value(u"home"_s).toInt(), -1);
        const int a = m_clubById.value(m.value(u"away"_s).toInt(), -1);
        const auto team = [this](int t, const char* key) { return t >= 0 ? fieldValue(clubAt(t), safe(teamDef(QString::fromLatin1(key)))) : QVariant(); };
        out << QVariantMap{{u"home"_s, clubName(m.value(u"home"_s).toInt())}, {u"away"_s, clubName(m.value(u"away"_s).toInt())},
                           {u"homeIndex"_s, h}, {u"awayIndex"_s, a},
                           {u"homeLogoUrl"_s, h >= 0 ? teamLogoUrl(h) : QString()}, {u"awayLogoUrl"_s, a >= 0 ? teamLogoUrl(a) : QString()},
                           {u"homeAbbreviation"_s, team(h, "hudName")}, {u"awayAbbreviation"_s, team(a, "hudName")},
                           {u"homeColour"_s, team(h, "primaryRGBColour")}, {u"awayColour"_s, team(a, "primaryRGBColour")},
                           {u"played"_s, m.value(u"played"_s)}, {u"homeScore"_s, m.value(u"homeScore"_s)},
                           {u"awayScore"_s, m.value(u"awayScore"_s)},
                           {u"stadium"_s, stadiumName(m.value(u"ground"_s).toInt(), m.value(u"home"_s).toInt())},
                           {u"match"_s, n},
                           {u"date"_s, m.value(u"year"_s).toInt() > 0 ? u"%1/%2/%3"_s.arg(m.value(u"day"_s).toInt(), 2, 10, QChar(u'0'))
                                                                           .arg(m.value(u"month"_s).toInt(), 2, 10, QChar(u'0')).arg(m.value(u"year"_s).toInt()) : QString()},
                           {u"time"_s, u"%1:%2"_s.arg(m.value(u"hour"_s).toInt(), 2, 10, QChar(u'0')).arg(m.value(u"minute"_s).toInt(), 2, 10, QChar(u'0'))}};
    }
    return out;
}

QVariantMap Rl26CareerEditorManager::matchStats(int stage, int match) const {
    QVariantMap out{{u"ok"_s, false}};
    if (!m_matchArray.ok() || match < 0 || match >= m_matchArray.count)
        return out;
    const Ref r{m_matchArray.at + qsizetype(match) * m_matchSize, m_matchArray.type, 0, 1, 0};
    const QVariantMap m = readMatch(match);
    // The C# editors' stat names.
    static const QHash<QString, QString> playerKeys = {{u"metresRun"_s, u"allRunMetres"_s}, {u"runs"_s, u"allRuns"_s},
                                                       {u"fieldGoalAttempts"_s, u"fieldGoalsAttempts"_s}};
    const auto side = [&](const char* name, int clubIdValue, int score) {
        CareerMatch::Team t;
        t.id = clubIdValue;
        t.teamIndex = m_clubById.value(clubIdValue, -1);
        t.name = clubName(clubIdValue);
        if (t.teamIndex >= 0) {
            const qsizetype c = clubAt(t.teamIndex);
            t.logoUrl = teamLogoUrl(t.teamIndex);
            t.abbreviation = fieldValue(c, safe(teamDef(u"hudName"_s))).toString();
            t.colour = fieldValue(c, safe(teamDef(u"primaryRGBColour"_s))).toString();
            const QString full = t.name, nick = fieldValue(c, safe(teamDef(u"nickname"_s))).toString();
            t.club = nick;
            t.location = full.endsWith(nick) ? full.left(full.size() - nick.size()).trimmed() : full;
        }
        t.score = score;
        const Ref team = resolve(r, u"m_result."_s + QString::fromLatin1(name));
        const Ref stats = resolve(team, u"statistics"_s);
        if (stats.ok())
            for (const auto& memList = m_types.at(stats.type).members; const Member& mem : memList) {
                const int v = int(readInt(stats.at + mem.off, mem.size));
                if (mem.name == u"fieldGoals"_s) t.t.insert(u"fieldgoal"_s, v);
                else if (mem.name == u"missedTackles"_s) t.t.insert(u"missTackles"_s, v);
                else if (mem.name == u"sets"_s) t.t.insert(u"completions"_s, v);   // The C# names: completions = sets,
                else if (mem.name == u"completions"_s) t.t.insert(u"completionAttempts"_s, v);   // ... attempts = completed.
                else t.t.insert(mem.name, v);
            }
        const Ref players = resolve(team, u"@069ca1ff"_s);
        const int count = qMin(int(readInt(resolve(team, u"playersCount"_s).at, 2)), players.ok() ? players.count : 0);
        for (int k = 0; k < count; ++k) {
            const Ref p{players.at + qsizetype(k) * players.size, players.type, 0, 1, 0};
            CareerMatch::Player pl;
            pl.id = int(readInt(resolve(p, u"id"_s).at, 4));
            pl.position = k == 5 ? 6 : k == 6 ? 5 : k;   // build() swaps 6 / 7 (RLL4's order); RL26 keeps shirt order.
            const QVariantMap info = playerInfo(pl.id);
            pl.name = info.value(u"name"_s).toString();
            pl.role = info.value(u"role"_s).toString();
            pl.playerIndex = info.value(u"playerIndex"_s).toInt();
            const Ref s = resolve(p, u"statisticsStored"_s);
            if (s.ok())
                for (const auto& memList = m_types.at(s.type).members; const Member& mem : memList)
                    pl.s.insert(playerKeys.value(mem.name, mem.name), int(readInt(s.at + mem.off, mem.size)));
            t.players << pl;
        }
        return t;
    };
    const CareerMatch::Team home = side("m_home", m.value(u"home"_s).toInt(), m.value(u"homeScore"_s).toInt());
    const CareerMatch::Team away = side("m_away", m.value(u"away"_s).toInt(), m.value(u"awayScore"_s).toInt());
    out = CareerMatch::build(home, away, stadiumName(m.value(u"ground"_s).toInt(), m.value(u"home"_s).toInt()),
                             u"%1/%2/%3"_s.arg(m.value(u"day"_s).toInt(), 2, 10, QChar(u'0')).arg(m.value(u"month"_s).toInt(), 2, 10, QChar(u'0')).arg(m.value(u"year"_s).toInt()),
                             u"%1:%2"_s.arg(m.value(u"hour"_s).toInt(), 2, 10, QChar(u'0')).arg(m.value(u"minute"_s).toInt(), 2, 10, QChar(u'0')), false);
    out[u"played"_s] = m.value(u"played"_s);
    if (stage >= 0 && stage < m_stages.size()) {
        out[u"competition"_s] = m_stages.at(stage).name.section(u" - "_s, 0, 0);
        out[u"competitionLogoUrl"_s] = competitionLogoUrl(m_stages.at(stage).comp);
        const QStringList rounds = fixtureRounds(stage);
        QList<int> rs;
        for (const auto& nList = m_stages.at(stage).matches; int n : nList) {
            const int x = readMatch(n).value(u"round"_s).toInt();
            if (!rs.contains(x))
                rs << x;
        }
        std::sort(rs.begin(), rs.end());
        out[u"round"_s] = rounds.value(int(rs.indexOf(m.value(u"round"_s).toInt())));
    }
    return out;
}

// =================================================================================================================
// Raw
// =================================================================================================================

int Rl26CareerEditorManager::recordSize(const QString& kind) const {
    return kind == u"team"_s ? m_clubSize : kind == u"player"_s ? m_playerSize : 0;
}

QString Rl26CareerEditorManager::recordOffset(const QString& kind, int index) const {
    const qsizetype at = kind == u"team"_s ? clubAt(index) : playerAt(index);
    return at < 0 ? QString() : u"0x"_s + QString::number(at - m_rootAt, 16).toUpper() + u" (in the decoded data: 0x"_s
                                    + QString::number(at, 16).toUpper() + u')';
}

// The rows the Raw tab shows (the same as the other Save / Career Editors): {offset, hex: 16 "XX", known: the bytes
// of mapped fields, text}.
QVariantList Rl26CareerEditorManager::rawRows(const QString& kind, int index) const {
    QVariantList out;
    const qsizetype at = kind == u"team"_s ? clubAt(index) : playerAt(index);
    const int size = recordSize(kind);
    if (at < 0 || size <= 0 || at + size > m_data.size())
        return out;
    QList<bool> known(size, false);
    for (const FieldDef& d : kind == u"team"_s ? m_teamDefs : m_playerDefs) {
        if (d.rel < 0)
            continue;
        const int bytes = d.kind == u"colour"_s ? 3 : qMax(1, d.size);
        for (qsizetype i = d.rel; i < d.rel + bytes && i < size; ++i)
            known[i] = true;
    }
    out.reserve((size + 15) / 16);
    for (int row = 0; row < size; row += 16) {
        QVariantList hex;
        QVariantList marks;
        QString text;
        for (int i = row; i < qMin(row + 16, size); ++i) {
            const quint8 c = static_cast<quint8>(m_data.at(at + i));
            hex.append(QString::number(c, 16).rightJustified(2, u'0').toUpper());
            marks.append(known.at(i));
            text += (c >= 32 && c < 127) ? QChar(c) : QChar(u'.');
        }
        out.append(QVariantMap{{u"offset"_s, row}, {u"hex"_s, hex}, {u"known"_s, marks}, {u"text"_s, text}});
    }
    return out;
}

bool Rl26CareerEditorManager::setRawByte(const QString& kind, int index, int offset, int value) {
    const qsizetype at = kind == u"team"_s ? clubAt(index) : playerAt(index);
    if (at < 0 || offset < 0 || offset >= recordSize(kind) || value < 0 || value > 255)
        return false;
    m_data[at + offset] = char(value);
    edited();
    return true;
}

// =================================================================================================================
// Raw Databases (like the C# editors' Raw ... windows: every record a row, edited as text, CSV in / out)
// =================================================================================================================
//
// The mapped player / club fields and the squads. A column's key is the field's ("|r" / "|g" / "|b": a colour's
// part), "#index", "#rosterCount" or "#roster<n>".

QVariantList Rl26CareerEditorManager::rawTables() const {
    const auto t = [](const char* id, const char* title) {
        return QVariantMap{{u"id"_s, QString::fromLatin1(id)}, {u"title"_s, QString::fromLatin1(title)}, {u"repair"_s, false}};
    };
    return {t("players", "Raw Player Database"), t("teams", "Raw Team Database"), t("teamPlayers", "Raw Team Players")};
}

QList<RawColumn> Rl26CareerEditorManager::rawColumnsFor(const QString& id) const {
    QList<RawColumn> cols;
    cols << RawColumn{id == u"players"_s ? u"Player Index"_s : u"Team Index"_s, u"#index"_s, true};
    const auto add = [&cols](const FieldDef& d, bool readOnly) {
        if (d.kind == u"none"_s || d.rel < 0)
            return;
        if (d.kind == u"colour"_s) {
            for (const char* part : {"R", "G", "B"})
                cols << RawColumn{d.label + u' ' + QLatin1StringView(part), d.key + u'|' + QString::fromLatin1(part).toLower(), readOnly};
            return;
        }
        cols << RawColumn{d.label, d.key, readOnly};
    };
    if (id == u"players"_s || id == u"teams"_s) {
        for (const auto& dList = id == u"players"_s ? m_playerDefs : m_teamDefs; const FieldDef& d : dList)
            add(d, d.readOnly || d.kind == u"date"_s);
    } else if (id == u"teamPlayers"_s) {
        for (const char* key : {"id", "fullName"})
            if (const FieldDef* d = teamDef(QString::fromLatin1(key)))
                add(*d, true);
        cols << RawColumn{u"Team Player Amount"_s, u"#rosterCount"_s};
        for (int j = 0; j < rosterMax(); ++j)
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

QString Rl26CareerEditorManager::rawGet(const QString& kind, int index, const QString& key) const {
    const qsizetype at = kind == u"team"_s ? clubAt(index) : playerAt(index);
    if (at < 0)
        return {};
    if (key == u"#index"_s)
        return QString::number(index);
    if (key.startsWith(u"#roster"_s)) {
        const Ref club{at, m_clubArray.type, 0, 1, 0};
        const Ref count = resolve(club, u"paClub.@dba90fa9"_s);
        const Ref ids = resolve(club, u"paClub.@d72f45a8"_s);
        if (!count.ok() || !ids.ok())
            return {};
        if (key == u"#rosterCount"_s)
            return QString::number(readInt(count.at, 2));
        const int j = QStringView(key).mid(7).toInt();
        return j < ids.count ? QString::number(readInt(ids.at + 4 * j, 4)) : QString();
    }
    const qsizetype bar = key.indexOf(u'|');
    const FieldDef* d = kind == u"team"_s ? teamDef(bar < 0 ? key : key.left(bar)) : playerDef(bar < 0 ? key : key.left(bar));
    if (!d)
        return {};
    if (bar >= 0) {
        const int part = QStringLiteral("rgb").indexOf(key.back());
        return QString::number(quint8(m_data.at(at + d->rel + qMax(0, part))));
    }
    const QVariant v = fieldValue(at, *d);
    if (d->kind == u"bool"_s)
        return RawTableModel::boolText(v.toBool());
    if (d->kind == u"float"_s)
        return QString::number(v.toDouble(), 'g', 7);
    if (d->kind == u"text"_s || d->kind == u"colour"_s)
        return v.toString();
    return QString::number(static_cast<qint64>(v.toDouble()));
}

bool Rl26CareerEditorManager::rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error) {
    const qsizetype at = kind == u"team"_s ? clubAt(index) : playerAt(index);
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
    if (key.startsWith(u"#roster"_s)) {
        const Ref club{at, m_clubArray.type, 0, 1, 0};
        const Ref count = resolve(club, u"paClub.@dba90fa9"_s);
        const Ref ids = resolve(club, u"paClub.@d72f45a8"_s);
        if (!count.ok() || !ids.ok())
            return false;
        if (key == u"#rosterCount"_s) {
            if (!number(0, qMin(rosterMax(), ids.count)))
                return false;
            writeInt(count.at, 2, v);
        } else {
            const int j = QStringView(key).mid(7).toInt();
            if (j >= ids.count || !number(0, 0xFFFFFFFFLL))
                return false;
            writeInt(ids.at + 4 * j, 4, v);
        }
        edited();
        return true;
    }
    const qsizetype bar = key.indexOf(u'|');
    const QString name = bar < 0 ? key : key.left(bar);
    const FieldDef* d = kind == u"team"_s ? teamDef(name) : playerDef(name);
    if (!d || d->readOnly)
        return false;
    if (bar >= 0) {
        if (!number(0, 255))
            return false;
        const int part = QStringLiteral("rgb").indexOf(key.back());
        uint rgb = QStringView(fieldValue(at, *d).toString()).mid(1).toUInt(nullptr, 16);
        const int shift = part == 0 ? 16 : part == 1 ? 8 : 0;
        rgb = (rgb & ~(0xFFu << shift)) | (uint(v) << shift);
        return setField(kind, index, name, u"#%1"_s.arg(rgb, 6, 16, QChar(u'0')));
    }
    if (d->kind == u"text"_s)
        return setField(kind, index, name, text);
    if (d->kind == u"bool"_s) {
        const bool on = RawTableModel::parseBool(text, &ok);
        if (!ok) {
            *error = u"\"%1\" isn't True or False"_s.arg(text);
            return false;
        }
        return setField(kind, index, name, on);
    }
    if (d->kind == u"float"_s) {
        const double f = text.trimmed().toDouble(&ok);
        if (!ok) {
            *error = u"\"%1\" isn't a number"_s.arg(text);
            return false;
        }
        return setField(kind, index, name, f);
    }
    const int bytes = qMin(d->size, 8);
    const bool isSigned = builtinSigned(d->elemKind);
    const qint64 lo = d->max > d->min ? d->min : isSigned ? -(qint64(1) << (8 * bytes - 1)) : 0;
    const qint64 hi = d->max > d->min ? d->max : bytes >= 8 ? INT64_MAX
                                               : isSigned ? (qint64(1) << (8 * bytes - 1)) - 1 : (qint64(1) << (8 * bytes)) - 1;
    if (!number(lo, hi))
        return false;
    return setField(kind, index, name, v);
}

bool Rl26CareerEditorManager::fillRawTable(const QString& id, RawTableModel* model) const {
    if (!model || !m_loaded)
        return false;
    const QList<RawColumn> cols = rawColumnsFor(id);
    if (cols.isEmpty())
        return false;
    const QString kind = id == u"players"_s ? u"player"_s : u"team"_s;
    const int count = kind == u"team"_s ? teamCount() : playerCount();
    QList<QStringList> rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        QStringList row;
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

QString Rl26CareerEditorManager::applyRawTable(RawTableModel* model) {
    if (!model || !m_loaded)
        return u"Nothing is loaded."_s;
    const QString id = model->tableId();
    const QString kind = id == u"players"_s ? u"player"_s : u"team"_s;
    const int count = kind == u"team"_s ? teamCount() : playerCount();
    int changed = 0;
    m_rawBatch = true;   // One refresh at the end, not one per cell.
    m_rawEdited = false;
    QString errors = model->apply(
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

int Rl26CareerEditorManager::repairRawIds(RawTableModel*) const { return 0; }
