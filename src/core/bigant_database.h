#ifndef BIGANT_DATABASE_H
#define BIGANT_DATABASE_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

/// BigAnt's team / player databases (defaultdata/defaultdata_clubs and defaultdata_players) as Rugby League Live 4,
/// Rugby 25 and Rugby League 26 store them (and Rugby League Live 3's older fixed layout, see readRll3()).
///
/// The file describes itself. The header names the tables ("lists") and their fields:
///     u8 revision, u8 list count, then per list: u8 kind, str8 name, u32 field count,
///                                                per field: u8 type, str8 name, str8 group
/// then u32 record count and the records. A record is
///     u32 identifier, [u32 size + blob]   (players: the appearance, before the lists)
///     per list:  u8 present, then (when present) per field: u8 set, then (when set) the value
///     [u32 size + blob]                   (teams: the jerseys, after the lists)
/// Values: bool = 1 byte, int / enum / hash = 4 bytes, string = u8 length + bytes, colour = r g b,
/// lineup slot = int player id + u8 shirt number; "derived" fields (type 0) take no space.
///
/// The type numbers changed between games (RLL4 / RL26: 1 bool 2 int 4 string 5 enum 6 colour 7 hash 8 lineup;
/// Rugby 25: 1 bool 4 int 6 string 8 enum 11 colour 12 hash), told apart by the type of the first field (id).
///
/// Loading and saving without edits gives back the same bytes. Setting a value sets its flag.
namespace BigAntDb {

enum class ValueType : quint8 { Derived, Bool, Int, Hash, String, Colour, Lineup };

struct Field {
    QString key;        // Main list: the field's name. Other lists: "<list>.<name>" (+ 2, 3, ... for repeats).
    QString name;       // As the header has it.
    QString group;
    int list = 0;
    int indexInList = 0;
    quint8 rawType = 0;
    ValueType type = ValueType::Int;
    int width = 0;      // Rugby League Live 3: the field's size in bytes (strings: with the terminating zero).
};

struct List {
    QString name;
    quint8 kind = 0;
    QList<int> fields;  // Indexes into Schema::fields.
};

enum class Format { SelfDescribing, Rll3Teams, Rll3Players };

struct Schema {
    Format format = Format::SelfDescribing;
    quint8 rll3Version = 0;         // Rugby League Live 3: the file's version byte.
    QByteArray header;              // Kept as read.
    QList<List> lists;
    QList<Field> fields;
    QHash<QString, int> byKey;
    QHash<QString, int> listByName;
    bool blobBeforeLists = false;   // Players: appearance blob first. Teams: jersey blob last.
    bool newTypeNumbers = false;    // Rugby 25 numbering.

    int field(const QString& key) const { return byKey.value(key, -1); }
    int list(const QString& name) const { return listByName.value(name, -1); }
    bool has(const QString& key) const { return byKey.contains(key); }
};

struct Value {
    quint8 flag = 0;
    qint64 num = 0;
    QByteArray bytes;
    quint8 aux = 0;     // A lineup slot's shirt number.
    QByteArray raw;     // Rugby League Live 3: a fixed size string as stored (written back while unchanged).
};

struct Record {
    quint32 identifier = 0;
    QVector<quint8> listPresent;
    QVector<Value> values;      // By field index.
    QByteArray blob;
};

class Table {
public:
    /// Empty on success, else what went wrong.
    QString read(const QByteArray& data);
    QByteArray write() const;
    /// Rugby League Live 3's older fixed layout (bigant_database_rll3.cpp): the same records, keys named like the
    /// newer games' where they mean the same. teams: defaultdata_clubs, else defaultdata_players.
    QString readRll3(const QByteArray& data, bool teams);

    const Schema& schema() const { return m_schema; }
    int count() const { return static_cast<int>(m_records.size()); }
    Record& record(int i) { return m_records[i]; }
    const Record& record(int i) const { return m_records[i]; }
    bool isEmpty() const { return m_records.isEmpty(); }

    // A field by key: int / bool / QString ("#rrggbb" for colours); invalid when the schema has no such field.
    QVariant get(int row, const QString& key) const;
    QVariant get(int row, int field) const;
    int num(int row, const QString& key, int fallback = 0) const;
    QString str(int row, const QString& key) const { return get(row, key).toString(); }
    bool isSet(int row, const QString& key) const;      // The field's list is present and its flag is set.
    bool set(int row, const QString& key, const QVariant& value);
    bool set(int row, int field, const QVariant& value);
    bool setPresent(int row, const QString& key, bool on);   // A field's flag, or a list's present byte.
    bool listPresent(int row, const QString& list) const;

    int shirt(int row, const QString& key) const;
    bool setShirt(int row, const QString& key, int shirt);

private:
    QByteArray writeRll3() const;
    Schema m_schema;
    QVector<Record> m_records;
    QByteArray m_tail;
};

} // namespace BigAntDb

#endif // BIGANT_DATABASE_H
