#include "bigant_database.h"

#include <QtEndian>

using namespace Qt::Literals::StringLiterals;

// Rugby League Live 3's databases (from Wouldy's RLL3 Database Editor, ReadDatabase.cs):
//
//   u8 version, i32 record count, then fixed size records with a few counted arrays:
//
//   Team:   licensed, deleted (bools), i32 id, u8 type, u16 typeAdditional, location[22], club[16], abbreviation[4],
//           logo[16], u8 colourSet, u8 supporters, commentary[40], u16 formation,
//           i32 stadiumAmount + (stadiumAmount + 1) * stadium[20],
//           i32 n + n * i32 feeder clubs, i32 n + n * i32 fed from clubs, i32 n + n * i32 squad,
//           i32 n + n * i32 lineup, [4 * i32 roles: only with a squad],
//           i32 n + n * i32 Nines lineup, [4 * i32 Nines roles: only with a squad and a Nines lineup],
//           i32 size + blob (the uniforms; jerseys at a fixed place inside it, see the database manager)
//   Player: u8 ?, i32 id, u8 hand, u8 foot, u8 weak foot rating, u8 day, u8 month, i16 year, u16 commentary surname,
//           u16 commentary full name, first[20], last[20], jersey name[16], u8 jersey number, u8 roles * 3,
//           u8 State of Origin, City v Country, All Stars, rep country, country of birth, u16 contract expiry,
//           u8 perk, height, weight, reputation, ego, loyalty, speed, strength, technique, footwork, agility, fitness,
//           strategy, i32 size + blob (the appearance and more)
//
// Strings are zero terminated in their fixed size. Little endian.

namespace BigAntDb {
namespace {

struct Builder {
    Schema& s;
    int list = -1;
    void beginList(const QString& name, quint8 kind) {
        List l;
        l.name = name;
        l.kind = kind;
        list = static_cast<int>(s.lists.size());
        s.listByName.insert(name, list);
        s.lists << l;
    }
    void add(const QString& name, ValueType type, int width) {
        Field f;
        f.name = name;
        f.key = list == 0 ? name : s.lists[list].name + u'.' + name;
        f.list = list;
        f.indexInList = static_cast<int>(s.lists[list].fields.size());
        f.type = type;
        f.width = width;
        s.byKey.insert(f.key, static_cast<int>(s.fields.size()));
        s.lists[list].fields << static_cast<int>(s.fields.size());
        s.fields << f;
    }
};

const QStringList kLineup = {u"fullBack"_s, u"wingLeft"_s, u"centreLeft"_s, u"centreRight"_s, u"wingRight"_s,
                             u"fiveEighth"_s, u"halfBack"_s, u"propLeft"_s, u"hooker"_s, u"propRight"_s,
                             u"secondRowLeft"_s, u"secondRowRight"_s, u"Lock"_s, u"Substitute"_s, u"Substitute2"_s,
                             u"Substitute3"_s, u"Substitute4"_s};
// Worked out from the players' positions in RLL3's Nines lineups.
const QStringList kNines = {u"fullBack"_s, u"wingLeft"_s, u"centreLeft"_s, u"centreRight"_s, u"fiveEighth"_s,
                            u"halfBack"_s, u"hooker"_s, u"secondRowLeft"_s, u"secondRowRight"_s, u"Substitute"_s,
                            u"Substitute2"_s, u"Substitute3"_s, u"Substitute4"_s, u"Substitute5"_s};
const QStringList kRoles = {u"captain"_s, u"goalKicker"_s, u"playMaker1"_s, u"playMaker2"_s};

void teamSchema(Schema& s) {
    Builder b{s};
    b.beginList(u"clubList"_s, 3);
    b.add(u"licensed"_s, ValueType::Bool, 1);
    b.add(u"deleted"_s, ValueType::Bool, 1);
    b.add(u"id"_s, ValueType::Int, 4);
    b.add(u"clubType"_s, ValueType::Int, 1);
    b.add(u"clubTypeAdditional"_s, ValueType::Int, 2);
    b.add(u"shortName"_s, ValueType::String, 22);
    b.add(u"nickname"_s, ValueType::String, 16);
    b.add(u"hudName"_s, ValueType::String, 4);
    b.add(u"icon"_s, ValueType::String, 16);
    b.add(u"colourSet"_s, ValueType::Int, 1);
    b.add(u"supporters"_s, ValueType::Int, 1);
    b.add(u"commentary"_s, ValueType::String, 40);
    b.add(u"formation"_s, ValueType::Int, 2);
    for (int j = 0; j < 4; ++j)
        b.add(u"homeGrounds#%1:stadium"_s.arg(j), ValueType::String, 20);
    for (int j = 0; j < 7; ++j)
        b.add(u"feederClubs#%1"_s.arg(j), ValueType::Int, 4);
    for (int j = 0; j < 7; ++j)
        b.add(u"fedFromClubs#%1"_s.arg(j), ValueType::Int, 4);
    b.beginList(u"roster"_s, 4);
    for (int j = 1; j <= 64; ++j)
        b.add(u"player%1"_s.arg(j), ValueType::Int, 4);
    b.beginList(u"LeagueMatch"_s, 5);
    for (const QString& r : kRoles)
        b.add(r, ValueType::Int, 4);
    for (const QString& p : kLineup)
        b.add(p, ValueType::Int, 4);
    b.beginList(u"NinesMatch"_s, 5);
    for (const QString& r : kRoles)
        b.add(r, ValueType::Int, 4);
    for (const QString& p : kNines)
        b.add(p, ValueType::Int, 4);
}

void playerSchema(Schema& s) {
    Builder b{s};
    b.beginList(u"players"_s, 1);
    const auto byte = [&b](const char* name) { b.add(QString::fromLatin1(name), ValueType::Int, 1); };
    const auto word = [&b](const char* name) { b.add(QString::fromLatin1(name), ValueType::Int, 2); };
    byte("unknown1");
    b.add(u"id"_s, ValueType::Int, 4);
    byte("preferredHand");
    byte("preferredFoot");
    byte("weakFootRating");
    byte("birthDay");
    byte("birthMonth");
    word("birthYear");
    word("commentarySurname");
    word("commentaryFullName");
    b.add(u"firstname"_s, ValueType::String, 20);
    b.add(u"surname"_s, ValueType::String, 20);
    b.add(u"shirtName"_s, ValueType::String, 16);
    for (const char* name : {"shirtNumber", "preferredRolePrimary", "preferredRoleSecondary", "preferredRoleTertiary",
                             "stateOfOrigin", "cityVsCountry", "allStars", "representativeCountry", "countryOfBirth"})
        byte(name);
    word("contractExpiry");
    for (const char* name : {"perk", "height", "weight", "reputation", "ego", "loyalty", "speed", "strength",
                             "technique", "footwork", "agility", "fitness", "strategy"})
        byte(name);
}

struct Reader {
    const QByteArray& data;
    qint64 pos = 0;
    QString error;
    bool need(qint64 n) {
        if (!error.isEmpty())
            return false;
        if (n < 0 || pos + n > data.size()) {
            error = u"the file ends early (offset %1)"_s.arg(pos);
            return false;
        }
        return true;
    }
    quint64 uint(int width) {
        if (!need(width))
            return 0;
        const char* p = data.constData() + pos;
        pos += width;
        switch (width) {
        case 1: return static_cast<uchar>(*p);
        case 2: return qFromLittleEndian<quint16>(p);
        default: return qFromLittleEndian<quint32>(p);
        }
    }
    qint32 i32() { return static_cast<qint32>(uint(4)); }
    QByteArray bytes(qint64 n) {
        if (!need(n))
            return {};
        pos += n;
        return data.mid(pos - n, n);
    }
};

void readValue(Reader& r, const Field& f, Value& v) {
    v.flag = 1;
    if (f.type == ValueType::String) {
        v.raw = r.bytes(f.width);
        const qsizetype zero = v.raw.indexOf('\0');
        v.bytes = zero < 0 ? v.raw : v.raw.left(zero);
    } else {
        const quint64 n = r.uint(f.width);
        v.num = f.width == 4 ? static_cast<qint64>(static_cast<qint32>(n)) : static_cast<qint64>(n);
    }
}

void putUInt(QByteArray& out, quint64 v, int width) {
    char b[4];
    switch (width) {
    case 1: out.append(static_cast<char>(v)); return;
    case 2: qToLittleEndian<quint16>(static_cast<quint16>(v), b); out.append(b, 2); return;
    default: qToLittleEndian<quint32>(static_cast<quint32>(v), b); out.append(b, 4); return;
    }
}

void writeValue(QByteArray& out, const Field& f, const Value& v) {
    if (f.type == ValueType::String) {
        const qsizetype zero = v.raw.indexOf('\0');
        if (v.raw.size() == f.width && (zero < 0 ? v.raw : v.raw.left(zero)) == v.bytes) {
            out.append(v.raw);   // Unchanged: as stored.
            return;
        }
        QByteArray text = v.bytes.left(f.width - 1);
        text.resize(f.width, '\0');
        out.append(text);
        return;
    }
    putUInt(out, static_cast<quint64>(v.num), f.width);
}

} // namespace

QString Table::readRll3(const QByteArray& data, bool teams) {
    m_schema = Schema();
    m_records.clear();
    m_tail.clear();
    m_schema.format = teams ? Format::Rll3Teams : Format::Rll3Players;
    if (teams)
        teamSchema(m_schema);
    else
        playerSchema(m_schema);

    Reader r{data};
    m_schema.rll3Version = static_cast<quint8>(r.uint(1));
    const qint32 count = r.i32();
    if (!r.error.isEmpty() || count <= 0 || count > 100000)
        return u"this isn't a Rugby League Live 3 database (%1 records)"_s.arg(count);

    const Schema& s = m_schema;
    const auto key = [&s](const QString& k) { return s.fields[s.byKey.value(k)]; };
    for (int i = 0; i < count && r.error.isEmpty(); ++i) {
        Record rec;
        rec.values.resize(s.fields.size());
        rec.listPresent.fill(1, s.lists.size());
        const auto read = [&](const QString& k) { readValue(r, s.fields[s.byKey.value(k)], rec.values[s.byKey.value(k)]); };
        const auto readArray = [&](const QStringList& keys, int max, const char* what) -> int {
            const qint32 n = r.i32();
            if (n < 0 || n > max) {
                if (r.error.isEmpty())
                    r.error = u"%1 count %2 at offset %3"_s.arg(QLatin1StringView(what)).arg(n).arg(r.pos - 4);
                return 0;
            }
            for (int j = 0; j < n; ++j)
                read(keys[j]);
            return n;
        };
        if (teams) {
            for (const char* k : {"licensed", "deleted", "id", "clubType", "clubTypeAdditional", "shortName", "nickname",
                                  "hudName", "icon", "colourSet", "supporters", "commentary", "formation"})
                read(QString::fromLatin1(k));
            const qint32 stadiums = r.i32() + 1;
            if (stadiums < 0 || stadiums > 4) {
                r.error = u"stadium count %1"_s.arg(stadiums - 1);
                break;
            }
            for (int j = 0; j < stadiums; ++j)
                read(u"homeGrounds#%1:stadium"_s.arg(j));
            QStringList feeder, fedFrom, roster, lineup, nines, roles, ninesRoles;
            for (int j = 0; j < 7; ++j) {
                feeder << u"feederClubs#%1"_s.arg(j);
                fedFrom << u"fedFromClubs#%1"_s.arg(j);
            }
            for (int j = 1; j <= 64; ++j)
                roster << u"roster.player%1"_s.arg(j);
            for (const QString& p : kLineup)
                lineup << u"LeagueMatch."_s + p;
            for (const QString& p : kNines)
                nines << u"NinesMatch."_s + p;
            for (const QString& p : kRoles) {
                roles << u"LeagueMatch."_s + p;
                ninesRoles << u"NinesMatch."_s + p;
            }
            readArray(feeder, 7, "feeder club");
            readArray(fedFrom, 7, "fed from club");
            const int squad = readArray(roster, 64, "squad");
            readArray(lineup, 17, "lineup");
            if (squad != 0)
                for (const QString& k : roles)
                    read(k);
            const int ninesCount = readArray(nines, 14, "Nines lineup");
            if (squad != 0 && ninesCount != 0)
                for (const QString& k : ninesRoles)
                    read(k);
        } else {
            for (int f = 0; f < s.fields.size(); ++f)
                readValue(r, s.fields[f], rec.values[f]);
        }
        const qint32 size = r.i32();
        if (size < 0 || size > 16 * 1024 * 1024) {
            if (r.error.isEmpty())
                r.error = u"blob size %1"_s.arg(size);
            break;
        }
        rec.blob = r.bytes(size);
        if (r.error.isEmpty())
            m_records << rec;
    }
    Q_UNUSED(key)
    if (!r.error.isEmpty())
        return u"record %1 of %2: %3"_s.arg(m_records.size()).arg(count).arg(r.error);
    m_tail = data.mid(r.pos);
    return {};
}

QByteArray Table::writeRll3() const {
    QByteArray out;
    const Schema& s = m_schema;
    out.append(static_cast<char>(s.rll3Version));
    putUInt(out, static_cast<quint32>(m_records.size()), 4);
    for (const Record& rec : m_records) {
        const auto field = [&s](const QString& k) { return s.byKey.value(k, -1); };
        const auto write = [&](const QString& k) {
            const int f = field(k);
            writeValue(out, s.fields[f], rec.values[f]);
        };
        // The set entries of an array, in order (an array is stored as a count and the entries).
        const auto present = [&](const QString& prefix, int from, int to, const QString& suffix = QString()) {
            QStringList keys;
            for (int j = from; j <= to; ++j) {
                const int f = field(prefix + QString::number(j) + suffix);
                if (f >= 0 && rec.values[f].flag)
                    keys << s.fields[f].key;
            }
            return keys;
        };
        const auto writeArray = [&](const QStringList& keys) {
            putUInt(out, static_cast<quint32>(keys.size()), 4);
            for (const QString& k : keys)
                write(k);
        };
        if (s.format == Format::Rll3Teams) {
            for (const char* k : {"licensed", "deleted", "id", "clubType", "clubTypeAdditional", "shortName", "nickname",
                                  "hudName", "icon", "colourSet", "supporters", "commentary", "formation"})
                write(QString::fromLatin1(k));
            QStringList stadiums = present(u"homeGrounds#"_s, 0, 3, u":stadium"_s);
            if (stadiums.isEmpty())
                stadiums << u"homeGrounds#0:stadium"_s;   // There's always one (the count is one less).
            putUInt(out, static_cast<quint32>(stadiums.size() - 1), 4);
            for (const QString& k : stadiums)
                write(k);
            writeArray(present(u"feederClubs#"_s, 0, 6));
            writeArray(present(u"fedFromClubs#"_s, 0, 6));
            const QStringList squad = present(u"roster.player"_s, 1, 64);
            writeArray(squad);
            const auto listKeys = [&](const QString& list, const QStringList& names) {
                QStringList keys;
                for (const QString& n : names) {
                    const int f = field(list + u'.' + n);
                    if (rec.values[f].flag)
                        keys << s.fields[f].key;
                }
                return keys;
            };
            writeArray(listKeys(u"LeagueMatch"_s, kLineup));
            if (!squad.isEmpty())
                for (const QString& r : kRoles)
                    write(u"LeagueMatch."_s + r);
            const QStringList nines = listKeys(u"NinesMatch"_s, kNines);
            writeArray(nines);
            if (!squad.isEmpty() && !nines.isEmpty())
                for (const QString& r : kRoles)
                    write(u"NinesMatch."_s + r);
        } else {
            for (int f = 0; f < s.fields.size(); ++f)
                writeValue(out, s.fields[f], rec.values[f]);
        }
        putUInt(out, static_cast<quint32>(rec.blob.size()), 4);
        out.append(rec.blob);
    }
    out.append(m_tail);
    return out;
}

} // namespace BigAntDb
