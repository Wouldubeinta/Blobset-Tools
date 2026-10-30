#ifndef PA_SERIAL_H
#define PA_SERIAL_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

/// BigAnt's self describing binary objects ("paSerial", magic 0x1234EDBC): a team's jerseys (ConvantClub with up
/// to 20 paUniform / paTeamVisualConfig) and a player's appearance (paCharacterVisualConfig) in the databases.
///
///     u32 magic, u16 version, u16 type count
///     per type:  u32 name hash, u16 index
///     per type:  u16 member count, u32 size, then per member: u32 name hash, u32 offset, u16 kind, u16 count
///     the root object's bytes (the type whose size is what's left)
///
/// A member's kind is a type index (a nested object) or a built in type (0xFDE8 bool, 0xFDEB u8, 0xFDED u16,
/// 0xFDEF u32, 0xFDF1 u64; RLL4 only has 0xFFFF = byte, arrays of it for wider values). Little endian, except the
/// RLL4 blobs (magic stored the other way round), which are big endian. Names are FNV-1a 32 hashes (RLL4:
/// MurmurHash2, seed 0xDEADBEEF); the ones the games' files give away are in knownNames().
namespace PaSerial {

struct Member {
    quint32 hash = 0;
    QString name;           // Known name, else empty.
    quint32 offset = 0;
    quint16 kind = 0;
    quint16 count = 1;
    int type = -1;          // >= 0: a nested object of that type.
    int elementSize = 1;    // Bytes per element (built in types).
};

struct Type {
    quint32 hash = 0;
    QString name;
    quint32 size = 0;
    QList<Member> members;  // By offset.
};

class Blob {
public:
    bool parse(const QByteArray& data, QString* error = nullptr);
    QByteArray bytes() const { return m_head + m_data; }
    bool isValid() const { return m_root >= 0; }
    bool bigEndian() const { return m_bigEndian; }

    const QList<Type>& types() const { return m_types; }
    int root() const { return m_root; }
    int typeByName(const QString& name) const;
    const Member* member(int type, const QString& name) const;
    const Member* memberByHash(int type, quint32 hash) const;

    QByteArray& data() { return m_data; }
    const QByteArray& data() const { return m_data; }
    quint64 readUInt(qint64 offset, int size) const;
    bool writeUInt(qint64 offset, int size, quint64 value);

    /// The fields of an object for an editor, flattened (nested objects included, "_r/_g/_b" bytes as colours):
    /// {key, label, group, kind ("number" / "bool" / "colour" / "text"), value, max}. key goes to setField().
    QVariantList fields(int type, qint64 base) const;
    bool setField(const QString& key, const QVariant& value);

private:
    void collect(int type, qint64 base, const QString& prefix, QVariantList& out, int depth) const;

    QByteArray m_head;      // Everything before the root object's bytes.
    QByteArray m_data;      // The root object.
    QList<Type> m_types;
    int m_root = -1;
    bool m_bigEndian = false;
};

/// The recovered names (pa_serial_names.cpp).
const QStringList& knownNames();
/// The name for a hash (FNV-1a or MurmurHash2), empty when unknown.
QString nameForHash(quint32 hash);
/// "uniformTop_layer1_colour" -> "Top Layer 1 Colour".
QString prettyName(const QString& name);

} // namespace PaSerial

#endif // PA_SERIAL_H
