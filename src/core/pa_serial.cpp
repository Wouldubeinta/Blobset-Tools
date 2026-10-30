#include "pa_serial.h"

#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtEndian>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace PaSerial {
namespace {

quint32 fnv1a(const QByteArray& s) {
    quint32 h = 0x811C9DC5u;
    for (char c : s) {
        h ^= static_cast<quint32>(static_cast<qint32>(static_cast<signed char>(c)));
        h *= 0x01000193u;
    }
    return h;
}

quint32 murmur2(const QByteArray& s, quint32 seed = 0xDEADBEEFu) {
    const quint32 m = 0x5BD1E995u;
    const auto* p = reinterpret_cast<const uchar*>(s.constData());
    qsizetype len = s.size();
    quint32 h = seed ^ static_cast<quint32>(len);
    while (len >= 4) {
        quint32 k = quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24;
        k *= m;
        k ^= k >> 24;
        k *= m;
        h *= m;
        h ^= k;
        p += 4;
        len -= 4;
    }
    switch (len) {
    case 3: h ^= quint32(p[2]) << 16; [[fallthrough]];
    case 2: h ^= quint32(p[1]) << 8; [[fallthrough]];
    case 1: h ^= p[0]; h *= m;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
}

const QHash<quint32, QString>& dictionary() {
    static const QHash<quint32, QString> dict = [] {
        QHash<quint32, QString> d;
        for (const QString& name : knownNames()) {
            const QByteArray utf8 = name.toUtf8();
            d.insert(fnv1a(utf8), name);
            d.insert(murmur2(utf8), name);
        }
        return d;
    }();
    return dict;
}

int builtinSize(quint16 kind) {
    switch (kind) {
    case 0xFDE8: case 0xFDEB: case 0xFFFF: return 1;
    case 0xFDED: return 2;
    case 0xFDEF: return 4;
    case 0xFDF1: return 8;
    default: return 0;
    }
}

QString groupFor(const QString& name) {
    const QString n = name.toLower();
    if (n.isEmpty())
        return u"Other"_s;
    if (n == u"name"_s)
        return u"Uniform"_s;
    if (n.startsWith(u"uniformtop"_s) || n == u"toptype"_s)
        return u"Jersey"_s;
    if (n.startsWith(u"uniformbottom"_s) || n.startsWith(u"pantsdecal"_s) || n.startsWith(u"pants"_s))
        return u"Shorts"_s;
    if (n.startsWith(u"uniformsocks"_s) || n.startsWith(u"sock"_s))
        return u"Socks"_s;
    if (n.startsWith(u"number"_s) || n.startsWith(u"name"_s) || n.startsWith(u"playername"_s) || n == u"hasnumber"_s
        || n == u"hasplayername"_s || n == u"showleadingzero"_s || n == u"hasteamname"_s)
        return u"Name & Number"_s;
    if (n.startsWith(u"decal"_s))
        return u"Jersey Decals"_s;
    if (n.startsWith(u"hat"_s) || n.startsWith(u"helmet"_s) || n.startsWith(u"headwear"_s) || n.startsWith(u"headstrapping"_s))
        return u"Headwear"_s;
    if (n.startsWith(u"shoe"_s))
        return u"Boots"_s;
    if (n.startsWith(u"show"_s) || n.startsWith(u"uniform"_s) || n == u"version"_s || n == u"sex"_s)
        return u"General"_s;
    // Player appearance.
    if (n.contains(u"tattoo"_s))
        return u"Tattoos"_s;
    if (n.contains(u"hair"_s) || n.contains(u"beard"_s) || n.contains(u"moustache"_s) || n.contains(u"sideburns"_s)
        || n.contains(u"chinhair"_s) || n.contains(u"eyebrow"_s))
        return u"Hair"_s;
    if (n.contains(u"bodyshape"_s) || n.contains(u"morph"_s) || n.contains(u"thickness"_s) || n.contains(u"torso"_s)
        || n.contains(u"neck"_s) || n == u"height"_s || n == u"weight"_s || n == u"traps_bodyshape"_s)
        return u"Body"_s;
    if (n.startsWith(u"strapping"_s) || n.startsWith(u"compression"_s) || n.startsWith(u"skins"_s)
        || n.startsWith(u"mouthguard"_s) || n.startsWith(u"armwear"_s) || n.startsWith(u"mouthwear"_s))
        return u"Gear"_s;
    static const QStringList face = {u"eye"_s, u"nose"_s, u"nostril"_s, u"mouth"_s, u"lip"_s, u"jaw"_s, u"chin"_s,
                                     u"cheek"_s, u"brow"_s, u"ear"_s, u"forehead"_s, u"skull"_s, u"head"_s,
                                     u"temple"_s, u"malar"_s, u"philtrum"_s, u"septum"_s, u"oris"_s, u"temporalis"_s,
                                     u"skin"_s, u"freckle"_s, u"mole"_s, u"scar"_s, u"redness"_s, u"highlight"_s,
                                     u"crowsfeet"_s, u"pockmark"_s, u"limbal"_s, u"age"_s, u"pimple"_s, u"facepaint"_s};
    for (const QString& part : face)
        if (n.contains(part))
            return u"Face"_s;
    return u"Other"_s;
}

QString hexName(quint32 hash) {
    return u"Unknown 0x%1"_s.arg(QString::number(hash, 16).toUpper().rightJustified(8, u'0'));
}

} // namespace

QString nameForHash(quint32 hash) {
    return dictionary().value(hash);
}

QString prettyName(const QString& name) {
    QString n = name;
    if (n.startsWith(u"m_"_s))
        n.remove(0, 2);
    for (const char* prefix : {"uniformTop_", "uniformBottom_", "uniformSocks_"})
        if (n.startsWith(QLatin1StringView(prefix)))
            n.remove(0, static_cast<qsizetype>(qstrlen(prefix)));
    n.replace(u'_', u' ');
    static const QRegularExpression camel(u"([a-z])([A-Z0-9])"_s);
    static const QRegularExpression digitsWord(u"([0-9])([A-Za-z])"_s);
    n.replace(camel, u"\\1 \\2"_s);
    n.replace(digitsWord, u"\\1 \\2"_s);
    QStringList words = n.split(u' ', Qt::SkipEmptyParts);
    for (QString& w : words) {
        if (w == u"col"_s)
            w = u"Colour"_s;
        else if (w == u"r"_s || w == u"g"_s || w == u"b"_s)
            w = w.toUpper();
        else if (!w.isEmpty())
            w[0] = w[0].toUpper();
    }
    return words.join(u' ');
}

bool Blob::parse(const QByteArray& data, QString* error) {
    const auto fail = [error](const QString& text) {
        if (error)
            *error = text;
        return false;
    };
    m_types.clear();
    m_root = -1;
    m_head.clear();
    m_data.clear();
    if (data.size() < 8)
        return fail(u"too small"_s);
    const quint32 magic = qFromLittleEndian<quint32>(data.constData());
    if (magic == 0x1234EDBCu)
        m_bigEndian = false;
    else if (magic == 0xBCED3412u)
        m_bigEndian = true;
    else
        return fail(u"not a paSerial blob"_s);

    qint64 pos = 4;
    const auto u16 = [&](quint16& out) {
        if (pos + 2 > data.size())
            return false;
        out = m_bigEndian ? qFromBigEndian<quint16>(data.constData() + pos) : qFromLittleEndian<quint16>(data.constData() + pos);
        pos += 2;
        return true;
    };
    const auto u32 = [&](quint32& out) {
        if (pos + 4 > data.size())
            return false;
        out = m_bigEndian ? qFromBigEndian<quint32>(data.constData() + pos) : qFromLittleEndian<quint32>(data.constData() + pos);
        pos += 4;
        return true;
    };

    quint16 version = 0, typeCount = 0;
    if (!u16(version) || !u16(typeCount) || typeCount == 0 || typeCount > 256)
        return fail(u"bad header"_s);
    for (int t = 0; t < typeCount; ++t) {
        Type type;
        quint16 index = 0;
        if (!u32(type.hash) || !u16(index))
            return fail(u"the type table is cut short"_s);
        type.name = nameForHash(type.hash);
        m_types << type;
    }
    for (int t = 0; t < typeCount; ++t) {
        quint16 memberCount = 0;
        if (!u16(memberCount) || !u32(m_types[t].size))
            return fail(u"the type definitions are cut short"_s);
        for (int m = 0; m < memberCount; ++m) {
            Member member;
            if (!u32(member.hash) || !u32(member.offset) || !u16(member.kind) || !u16(member.count))
                return fail(u"the type definitions are cut short"_s);
            member.name = nameForHash(member.hash);
            if (member.kind < typeCount)
                member.type = member.kind;
            else
                member.elementSize = std::max(1, builtinSize(member.kind));
            m_types[t].members << member;
        }
        std::sort(m_types[t].members.begin(), m_types[t].members.end(),
                  [](const Member& a, const Member& b) { return a.offset < b.offset; });
    }
    const qint64 left = data.size() - pos;
    for (int t = static_cast<int>(m_types.size()) - 1; t >= 0 && m_root < 0; --t)
        if (m_types[t].size == left)
            m_root = t;
    if (m_root < 0)
        return fail(u"no object fits the data (%1 bytes)"_s.arg(left));
    m_head = data.left(pos);
    m_data = data.mid(pos);
    return true;
}

int Blob::typeByName(const QString& name) const {
    for (int t = 0; t < m_types.size(); ++t)
        if (m_types[t].name == name)
            return t;
    return -1;
}

const Member* Blob::member(int type, const QString& name) const {
    if (type < 0 || type >= m_types.size())
        return nullptr;
    for (const Member& m : m_types[type].members)
        if (m.name == name)
            return &m;
    return nullptr;
}

const Member* Blob::memberByHash(int type, quint32 hash) const {
    if (type < 0 || type >= m_types.size())
        return nullptr;
    for (const Member& m : m_types[type].members)
        if (m.hash == hash)
            return &m;
    return nullptr;
}

quint64 Blob::readUInt(qint64 offset, int size) const {
    if (offset < 0 || offset + size > m_data.size())
        return 0;
    const char* p = m_data.constData() + offset;
    switch (size) {
    case 1: return static_cast<uchar>(*p);
    case 2: return m_bigEndian ? qFromBigEndian<quint16>(p) : qFromLittleEndian<quint16>(p);
    case 4: return m_bigEndian ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
    case 8: return m_bigEndian ? qFromBigEndian<quint64>(p) : qFromLittleEndian<quint64>(p);
    default: return 0;
    }
}

bool Blob::writeUInt(qint64 offset, int size, quint64 value) {
    if (offset < 0 || offset + size > m_data.size())
        return false;
    char* p = m_data.data() + offset;
    switch (size) {
    case 1: *p = static_cast<char>(value); break;
    case 2: m_bigEndian ? qToBigEndian<quint16>(static_cast<quint16>(value), p) : qToLittleEndian<quint16>(static_cast<quint16>(value), p); break;
    case 4: m_bigEndian ? qToBigEndian<quint32>(static_cast<quint32>(value), p) : qToLittleEndian<quint32>(static_cast<quint32>(value), p); break;
    case 8: m_bigEndian ? qToBigEndian<quint64>(value, p) : qToLittleEndian<quint64>(value, p); break;
    default: return false;
    }
    return true;
}

void Blob::collect(int type, qint64 base, const QString& prefix, QVariantList& out, int depth) const {
    if (type < 0 || type >= m_types.size() || depth > 6)
        return;
    const Type& t = m_types[type];

    // Colour triples: "<stem>_r", "<stem>_g", "<stem>_b" bytes.
    QHash<QString, const Member*> bytesByName;
    for (const Member& m : t.members)
        if (m.type < 0 && m.elementSize == 1 && m.count == 1 && !m.name.isEmpty())
            bytesByName.insert(m.name, &m);
    QSet<quint32> used;

    for (int i = 0; i < t.members.size(); ++i) {
        const Member& m = t.members[i];
        if (used.contains(m.hash))
            continue;
        const qint64 at = base + m.offset;
        const QString label = m.name.isEmpty() ? hexName(m.hash) : prettyName(m.name);
        QString group = prefix.isEmpty() ? groupFor(m.name) : prefix;
        if (depth == 0 && group == u"Other"_s)
            group = u"Uniform"_s;   // The object's own fields (a uniform's isHidden / m_isNines, ...).

        if (m.type >= 0) {
            if (m.count == 1 && m_types[m.type].name != u"paUuid"_s)
                collect(m.type, at, QString(), out, depth + 1);   // Flattened (m_config, ...).
            continue;
        }
        // Next member's offset (for RLL4's byte arrays).
        const qint64 end = i + 1 < t.members.size() ? t.members[i + 1].offset : t.size;
        Q_UNUSED(end)

        if (m.name.endsWith(u"_r"_s) && m.elementSize == 1 && m.count == 1) {
            const QString stem = m.name.chopped(2);
            const Member* g = bytesByName.value(stem + u"_g"_s);
            const Member* b = bytesByName.value(stem + u"_b"_s);
            if (g && b) {
                used.insert(g->hash);
                used.insert(b->hash);
                const int r = static_cast<int>(readUInt(at, 1));
                const int gv = static_cast<int>(readUInt(base + g->offset, 1));
                const int bv = static_cast<int>(readUInt(base + b->offset, 1));
                out << QVariantMap{{u"key"_s, u"c:%1:%2:%3"_s.arg(at).arg(base + g->offset).arg(base + b->offset)},
                                   {u"label"_s, prettyName(stem)},
                                   {u"group"_s, group},
                                   {u"kind"_s, u"colour"_s},
                                   {u"value"_s, u"#%1%2%3"_s.arg(r, 2, 16, QChar(u'0')).arg(gv, 2, 16, QChar(u'0')).arg(bv, 2, 16, QChar(u'0'))}};
                continue;
            }
        }
        if (m.name.endsWith(u"_g"_s) || m.name.endsWith(u"_b"_s)) {
            // Part of a triple listed later: picked up with its "_r".
            const QString stem = m.name.chopped(2);
            if (bytesByName.contains(stem + u"_r"_s) && bytesByName.contains(stem + u"_g"_s) && bytesByName.contains(stem + u"_b"_s))
                continue;
        }

        const int total = m.elementSize * m.count;
        const bool textLike = m.elementSize == 1 && m.count >= 8
                              && (m.name.contains(u"name"_s, Qt::CaseInsensitive) || m.name.isEmpty());
        if (textLike) {
            const QByteArray raw = m_data.mid(at, m.count);
            const QByteArray text = raw.left(raw.indexOf('\0') < 0 ? raw.size() : raw.indexOf('\0'));
            bool printable = true;
            for (char c : text)
                printable = printable && (static_cast<uchar>(c) >= 0x20);
            if (printable) {
                out << QVariantMap{{u"key"_s, u"t:%1:%2"_s.arg(at).arg(m.count)},
                                   {u"label"_s, label},
                                   {u"group"_s, group},
                                   {u"kind"_s, u"text"_s},
                                   {u"value"_s, QString::fromLatin1(text)},
                                   {u"max"_s, m.count - 1}};
                continue;
            }
        }
        // A value: built in size, or an RLL4 byte array of 2 / 4 / 8.
        int size = m.elementSize;
        int count = m.count;
        if (m.elementSize == 1 && (m.count == 2 || m.count == 4 || m.count == 8) && m.kind == 0xFFFF) {
            size = m.count;
            count = 1;
        }
        Q_UNUSED(total)
        for (int e = 0; e < count; ++e) {
            const qint64 o = at + qint64(e) * size;
            const bool isBool = m.kind == 0xFDE8;
            const quint64 v = readUInt(o, size);
            const QString key = isBool ? u"b:%1"_s.arg(o) : u"n:%1:%2"_s.arg(o).arg(size);
            QVariantMap item{{u"key"_s, key},
                             {u"label"_s, count > 1 ? u"%1 [%2]"_s.arg(label).arg(e + 1) : label},
                             {u"group"_s, group},
                             {u"kind"_s, isBool ? u"bool"_s : u"number"_s},
                             {u"value"_s, isBool ? QVariant(v != 0) : QVariant(static_cast<double>(v))},
                             {u"max"_s, size >= 4 ? 2147483647.0 : static_cast<double>((quint64(1) << (8 * size)) - 1)}};
            out << item;
        }
    }
}

namespace {
// "Decal 2 Scale" before "Decal 10 Scale".
bool naturalLess(const QString& a, const QString& b) {
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype ei = i, ej = j;
            while (ei < a.size() && a[ei].isDigit()) ++ei;
            while (ej < b.size() && b[ej].isDigit()) ++ej;
            const qlonglong na = a.mid(i, ei - i).toLongLong(), nb = b.mid(j, ej - j).toLongLong();
            if (na != nb)
                return na < nb;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a[i].toLower(), cb = b[j].toLower();
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}
} // namespace

QVariantList Blob::fields(int type, qint64 base) const {
    QVariantList out;
    collect(type, base, QString(), out, 0);
    static const QStringList order = {u"Uniform"_s, u"General"_s, u"Jersey"_s, u"Jersey Decals"_s, u"Shorts"_s,
                                      u"Socks"_s, u"Name & Number"_s, u"Headwear"_s, u"Boots"_s, u"Face"_s, u"Hair"_s,
                                      u"Body"_s, u"Tattoos"_s, u"Gear"_s, u"Other"_s};
    std::stable_sort(out.begin(), out.end(), [](const QVariant& x, const QVariant& y) {
        const QVariantMap a = x.toMap(), b = y.toMap();
        const qsizetype ga = order.indexOf(a.value(u"group"_s).toString()), gb = order.indexOf(b.value(u"group"_s).toString());
        if (ga != gb)
            return ga < gb;
        const QString la = a.value(u"label"_s).toString(), lb = b.value(u"label"_s).toString();
        const bool ua = la.startsWith(u"Unknown"_s), ub = lb.startsWith(u"Unknown"_s);
        if (ua != ub)
            return ub;   // Named fields first.
        return naturalLess(la, lb);
    });
    return out;
}

bool Blob::setField(const QString& key, const QVariant& value) {
    const QStringList parts = key.split(u':');
    if (parts.size() < 2)
        return false;
    const QString kind = parts[0];
    if (kind == u"n"_s && parts.size() == 3) {
        const int size = parts[2].toInt();
        const quint64 v = static_cast<quint64>(value.toDouble());
        if (readUInt(parts[1].toLongLong(), size) == v)
            return false;
        return writeUInt(parts[1].toLongLong(), size, v);
    }
    if (kind == u"b"_s) {
        const quint64 v = value.toBool() ? 1 : 0;
        if (readUInt(parts[1].toLongLong(), 1) == v)
            return false;
        return writeUInt(parts[1].toLongLong(), 1, v);
    }
    if (kind == u"c"_s && parts.size() == 4) {
        QString text = value.toString().trimmed();
        if (text.startsWith(u'#'))
            text.remove(0, 1);
        if (text.size() == 8)
            text.remove(0, 2);
        bool ok = false;
        const uint rgb = text.toUInt(&ok, 16);
        if (!ok || text.size() != 6)
            return false;
        bool changed = false;
        const uint channels[3] = {(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF};
        for (int c = 0; c < 3; ++c) {
            const qint64 at = parts[c + 1].toLongLong();
            if (readUInt(at, 1) != channels[c]) {
                writeUInt(at, 1, channels[c]);
                changed = true;
            }
        }
        return changed;
    }
    if (kind == u"t"_s && parts.size() == 3) {
        const qint64 at = parts[1].toLongLong();
        const int count = parts[2].toInt();
        if (at < 0 || at + count > m_data.size() || count <= 0)
            return false;
        QByteArray text = value.toString().toLatin1().left(count - 1);
        const QByteArray old = m_data.mid(at, count);
        if (old.left(old.indexOf('\0') < 0 ? old.size() : old.indexOf('\0')) == text)
            return false;
        text.resize(count, '\0');
        m_data.replace(at, count, text);
        return true;
    }
    return false;
}

} // namespace PaSerial
