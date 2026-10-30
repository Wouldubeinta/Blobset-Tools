#include "bigant_database.h"

#include <QtEndian>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace BigAntDb {
namespace {

struct Reader {
    const QByteArray& data;
    qint64 pos = 0;
    QString error;

    bool need(qint64 n) {
        if (!error.isEmpty())
            return false;
        if (n < 0 || pos + n > data.size()) {
            error = u"the file ends early (offset %1, %2 more bytes needed)"_s.arg(pos).arg(n);
            return false;
        }
        return true;
    }
    quint8 u8() { return need(1) ? static_cast<quint8>(data[pos++]) : 0; }
    quint32 u32() {
        if (!need(4))
            return 0;
        const quint32 v = qFromLittleEndian<quint32>(data.constData() + pos);
        pos += 4;
        return v;
    }
    QByteArray bytes(qint64 n) {
        if (!need(n))
            return {};
        const QByteArray v = data.mid(pos, n);
        pos += n;
        return v;
    }
    QString str8() { return QString::fromLatin1(bytes(u8())); }
};

void putU32(QByteArray& out, quint32 v) {
    char b[4];
    qToLittleEndian<quint32>(v, b);
    out.append(b, 4);
}

ValueType typeOf(quint8 raw, bool newNumbers, bool* known) {
    *known = true;
    if (raw == 0)
        return ValueType::Derived;
    if (newNumbers) {
        switch (raw) {
        case 1: return ValueType::Bool;
        case 4: case 8: return ValueType::Int;
        case 6: return ValueType::String;
        case 11: return ValueType::Colour;
        case 12: return ValueType::Hash;
        default: break;
        }
    } else {
        switch (raw) {
        case 1: return ValueType::Bool;
        case 2: case 5: return ValueType::Int;
        case 4: return ValueType::String;
        case 6: return ValueType::Colour;
        case 7: return ValueType::Hash;
        case 8: return ValueType::Lineup;
        default: break;
        }
    }
    *known = false;
    return ValueType::Int;
}

bool isBlobMagic(const QByteArray& data, qint64 at) {
    if (at < 0 || at + 4 > data.size())
        return false;
    const quint32 v = qFromLittleEndian<quint32>(data.constData() + at);
    return v == 0x1234EDBCu || v == 0xBCED3412u;
}

} // namespace

QString Table::read(const QByteArray& data) {
    m_schema = Schema();
    m_records.clear();
    m_tail.clear();
    Reader r{data};

    // --- Header ----------------------------------------------------------------------------------------------
    r.u8();   // Revision.
    const int listCount = r.u8();
    if (listCount <= 0 || listCount > 64)
        return u"this isn't a database this editor knows (%1 lists)"_s.arg(listCount);
    struct RawField { quint8 type; QString name; QString group; };
    QList<QList<RawField>> rawFields;
    for (int l = 0; l < listCount && r.error.isEmpty(); ++l) {
        List list;
        list.kind = r.u8();
        list.name = r.str8();
        const quint32 count = r.u32();
        if (count > 4096)
            return u"list %1 has %2 fields: this isn't a database this editor knows"_s.arg(list.name).arg(count);
        QList<RawField> fields;
        for (quint32 f = 0; f < count && r.error.isEmpty(); ++f) {
            RawField field;
            field.type = r.u8();
            field.name = r.str8();
            field.group = r.str8();
            fields << field;
        }
        m_schema.lists << list;
        rawFields << fields;
    }
    if (!r.error.isEmpty())
        return u"header: "_s + r.error;
    if (rawFields.isEmpty() || rawFields[0].isEmpty())
        return u"the database has no fields"_s;
    m_schema.header = data.left(r.pos);
    m_schema.newTypeNumbers = rawFields[0][0].type == 4;   // "id": int is 2 in the older numbering, 4 in the newer.

    for (int l = 0; l < rawFields.size(); ++l) {
        List& list = m_schema.lists[l];
        m_schema.listByName.insert(list.name, l);
        QHash<QString, int> seen;
        for (int f = 0; f < rawFields[l].size(); ++f) {
            const RawField& raw = rawFields[l][f];
            Field field;
            field.name = raw.name;
            field.group = raw.group;
            field.list = l;
            field.indexInList = f;
            field.rawType = raw.type;
            bool known = false;
            field.type = typeOf(raw.type, m_schema.newTypeNumbers, &known);
            if (!known)
                return u"field %1.%2 has a type (%3) this editor doesn't know"_s.arg(list.name, raw.name).arg(raw.type);
            const int n = ++seen[raw.name];
            const QString base = l == 0 ? raw.name : list.name + u'.' + raw.name;
            field.key = n == 1 ? base : base + QString::number(n);
            m_schema.byKey.insert(field.key, static_cast<int>(m_schema.fields.size()));
            list.fields << static_cast<int>(m_schema.fields.size());
            m_schema.fields << field;
        }
    }

    // --- Records ---------------------------------------------------------------------------------------------
    const qint32 count = static_cast<qint32>(r.u32());
    if (!r.error.isEmpty() || count < 0 || count > 200000)
        return u"the record count (%1) doesn't look right"_s.arg(count);
    // Players carry their appearance blob before the lists: u32 size, then the blob's magic.
    m_schema.blobBeforeLists = count > 0 && isBlobMagic(data, r.pos + 8);

    m_records.reserve(count);
    for (int i = 0; i < count; ++i) {
        Record rec;
        rec.identifier = r.u32();
        if (m_schema.blobBeforeLists)
            rec.blob = r.bytes(static_cast<qint32>(r.u32()));
        rec.values.resize(m_schema.fields.size());
        rec.listPresent.resize(m_schema.lists.size());
        for (int l = 0; l < m_schema.lists.size() && r.error.isEmpty(); ++l) {
            rec.listPresent[l] = r.u8();
            if (!rec.listPresent[l])
                continue;
            for (int f : m_schema.lists[l].fields) {
                const Field& field = m_schema.fields[f];
                if (field.type == ValueType::Derived)
                    continue;
                Value& v = rec.values[f];
                v.flag = r.u8();
                if (!v.flag)
                    continue;
                switch (field.type) {
                case ValueType::Bool: v.num = r.u8(); break;
                case ValueType::Int: v.num = static_cast<qint32>(r.u32()); break;
                case ValueType::Hash: v.num = r.u32(); break;
                case ValueType::String: v.bytes = r.bytes(r.u8()); break;
                case ValueType::Colour: {
                    const quint8 cr = r.u8(), cg = r.u8(), cb = r.u8();
                    v.num = (cr << 16) | (cg << 8) | cb;
                    break;
                }
                case ValueType::Lineup:
                    v.num = static_cast<qint32>(r.u32());
                    v.aux = r.u8();
                    break;
                default: break;
                }
            }
        }
        if (!m_schema.blobBeforeLists)
            rec.blob = r.bytes(static_cast<qint32>(r.u32()));
        if (!r.error.isEmpty())
            return u"record %1 of %2: %3"_s.arg(i).arg(count).arg(r.error);
        m_records << rec;
    }
    m_tail = data.mid(r.pos);
    return {};
}

QByteArray Table::write() const {
    if (m_schema.format != Format::SelfDescribing)
        return writeRll3();
    QByteArray out;
    out.reserve(m_schema.header.size() + m_records.size() * 1024);
    out.append(m_schema.header);
    putU32(out, static_cast<quint32>(m_records.size()));
    for (const Record& rec : m_records) {
        putU32(out, rec.identifier);
        if (m_schema.blobBeforeLists) {
            putU32(out, static_cast<quint32>(rec.blob.size()));
            out.append(rec.blob);
        }
        for (int l = 0; l < m_schema.lists.size(); ++l) {
            out.append(static_cast<char>(rec.listPresent[l]));
            if (!rec.listPresent[l])
                continue;
            for (int f : m_schema.lists[l].fields) {
                const Field& field = m_schema.fields[f];
                if (field.type == ValueType::Derived)
                    continue;
                const Value& v = rec.values[f];
                out.append(static_cast<char>(v.flag));
                if (!v.flag)
                    continue;
                switch (field.type) {
                case ValueType::Bool: out.append(static_cast<char>(v.num)); break;
                case ValueType::Int: case ValueType::Hash: putU32(out, static_cast<quint32>(v.num)); break;
                case ValueType::String:
                    out.append(static_cast<char>(v.bytes.size()));
                    out.append(v.bytes);
                    break;
                case ValueType::Colour:
                    out.append(static_cast<char>((v.num >> 16) & 0xFF));
                    out.append(static_cast<char>((v.num >> 8) & 0xFF));
                    out.append(static_cast<char>(v.num & 0xFF));
                    break;
                case ValueType::Lineup:
                    putU32(out, static_cast<quint32>(v.num));
                    out.append(static_cast<char>(v.aux));
                    break;
                default: break;
                }
            }
        }
        if (!m_schema.blobBeforeLists) {
            putU32(out, static_cast<quint32>(rec.blob.size()));
            out.append(rec.blob);
        }
    }
    out.append(m_tail);
    return out;
}

QVariant Table::get(int row, const QString& key) const {
    return get(row, m_schema.field(key));
}

QVariant Table::get(int row, int field) const {
    if (row < 0 || row >= m_records.size() || field < 0 || field >= m_schema.fields.size())
        return {};
    const Value& v = m_records[row].values[field];
    switch (m_schema.fields[field].type) {
    case ValueType::String:
        if (!v.flag)
            return QString();
        return m_schema.fields[field].width > 0 ? QString::fromLatin1(v.bytes) : QString::fromUtf8(v.bytes);
    case ValueType::Bool: return v.flag && v.num != 0;
    case ValueType::Colour: return u"#%1"_s.arg(v.flag ? v.num & 0xFFFFFF : 0, 6, 16, QChar(u'0'));
    case ValueType::Hash: return static_cast<uint>(v.flag ? v.num : 0);
    case ValueType::Derived: return {};
    default: return static_cast<int>(v.flag ? v.num : 0);
    }
}

int Table::num(int row, const QString& key, int fallback) const {
    const QVariant v = get(row, key);
    return v.isValid() ? v.toInt() : fallback;
}

bool Table::isSet(int row, const QString& key) const {
    const int f = m_schema.field(key);
    if (row < 0 || row >= m_records.size() || f < 0)
        return false;
    const Record& rec = m_records[row];
    return rec.listPresent[m_schema.fields[f].list] && rec.values[f].flag;
}

bool Table::listPresent(int row, const QString& list) const {
    const int l = m_schema.list(list);
    return row >= 0 && row < m_records.size() && l >= 0 && m_records[row].listPresent[l];
}

bool Table::set(int row, const QString& key, const QVariant& value) {
    return set(row, m_schema.field(key), value);
}

bool Table::set(int row, int field, const QVariant& value) {
    if (row < 0 || row >= m_records.size() || field < 0 || field >= m_schema.fields.size())
        return false;
    Record& rec = m_records[row];
    Value& v = rec.values[field];
    const Value before = v;
    switch (m_schema.fields[field].type) {
    case ValueType::Derived:
        return false;
    case ValueType::String: {
        const int width = m_schema.fields[field].width;
        // RLL3 keeps Latin-1 in fixed size fields (with room for the zero); the newer games UTF-8, up to 255.
        QByteArray bytes = width > 0 ? value.toString().toLatin1() : value.toString().toUtf8();
        const int max = width > 0 ? width - 1 : 255;
        if (bytes.size() > max)
            bytes.truncate(max);
        v.bytes = bytes;
        break;
    }
    case ValueType::Colour: {
        QString text = value.toString().trimmed();
        if (text.startsWith(u'#'))
            text.remove(0, 1);
        if (text.size() == 8)
            text.remove(0, 2);
        bool ok = false;
        const uint rgb = text.toUInt(&ok, 16);
        if (!ok || text.size() != 6)
            return false;
        v.num = rgb & 0xFFFFFF;
        break;
    }
    case ValueType::Bool: v.num = value.toBool() ? 1 : 0; break;
    case ValueType::Hash: v.num = value.toUInt(); break;
    default: {
        qint64 n = value.toLongLong();
        const int width = m_schema.fields[field].width;
        if (width == 1)
            n = std::clamp<qint64>(n, 0, 255);
        else if (width == 2)
            n = std::clamp<qint64>(n, 0, 65535);
        v.num = n;
        break;
    }
    }
    const bool changed = !before.flag || v.num != before.num || v.bytes != before.bytes;
    if (!changed) {
        v = before;
        return false;
    }
    v.flag = 1;
    const int list = m_schema.fields[field].list;
    rec.listPresent[list] = 1;
    return true;
}

bool Table::setPresent(int row, const QString& key, bool on) {
    if (row < 0 || row >= m_records.size())
        return false;
    Record& rec = m_records[row];
    const int l = m_schema.list(key);
    if (l >= 0) {
        if ((rec.listPresent[l] != 0) == on)
            return false;
        rec.listPresent[l] = on ? 1 : 0;
        return true;
    }
    const int f = m_schema.field(key);
    if (f < 0 || m_schema.fields[f].type == ValueType::Derived)
        return false;
    Value& v = rec.values[f];
    if ((v.flag != 0) == on && (!on || rec.listPresent[m_schema.fields[f].list]))
        return false;
    v.flag = on ? 1 : 0;
    if (on)
        rec.listPresent[m_schema.fields[f].list] = 1;
    return true;
}

int Table::shirt(int row, const QString& key) const {
    const int f = m_schema.field(key);
    if (row < 0 || row >= m_records.size() || f < 0 || m_schema.fields[f].type != ValueType::Lineup)
        return -1;
    return m_records[row].values[f].flag ? m_records[row].values[f].aux : -1;
}

bool Table::setShirt(int row, const QString& key, int shirt) {
    const int f = m_schema.field(key);
    if (row < 0 || row >= m_records.size() || f < 0 || m_schema.fields[f].type != ValueType::Lineup
        || shirt < 0 || shirt > 255)
        return false;
    Value& v = m_records[row].values[f];
    if (!v.flag || v.aux == shirt)
        return false;
    v.aux = static_cast<quint8>(shirt);
    return true;
}

} // namespace BigAntDb
