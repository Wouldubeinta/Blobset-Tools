#include "bnk_file.h"

#include "wem_file.h"

#include <QtEndian>

using namespace Qt::StringLiterals;

namespace {
constexpr qint64 kChunkHeaderSize = 8;
constexpr qint64 kDidxEntrySize = 12;
constexpr quint8 kHircSound = 2;
constexpr quint8 kHircEvent = 4;
} // namespace

quint32 BnkFile::readU32(const QByteArray& data, qint64 offset) const {
    const auto* p = reinterpret_cast<const uchar*>(data.constData()) + offset;
    return isBigEndian ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
}

bool BnkFile::parse(const QByteArray& data, QString* error) {
    *this = BnkFile();

    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    const qint64 total = data.size();
    if (total < 16 || !data.startsWith("BKHD"))
        return fail(u"This isn't a Wwise SoundBank (no BKHD header). The asset may be compressed or a different format."_s);

    // The bank version is a small number, so a huge little endian value means a big endian (console) bank.
    isBigEndian = qFromLittleEndian<quint32>(data.constData() + 8) > 0xFFFF;

    qint64 didxOffset = -1;
    quint32 didxSize = 0;
    bool hasData = false;

    qint64 pos = 0;
    while (pos + kChunkHeaderSize <= total) {
        const QByteArray tag = data.mid(pos, 4);
        const quint32 size = readU32(data, pos + 4);
        const qint64 body = pos + kChunkHeaderSize;

        if (body + static_cast<qint64>(size) > total) {
            truncated = true;
            break;
        }

        chunks.append(QString::fromLatin1(tag));

        if (tag == "BKHD") {
            if (size >= 8) {
                version = readU32(data, body);
                bankId = readU32(data, body + 4);
            }
        } else if (tag == "DIDX") {
            didxOffset = body;
            didxSize = size;
        } else if (tag == "DATA") {
            dataOffset = static_cast<quint32>(body);
            dataSize = size;
            hasData = true;
        } else if (tag == "HIRC") {
            readHirc(data, body, size);
        } else if (tag == "STID") {
            readStid(data, body, size);
        }

        pos = body + size;
    }

    if (didxOffset >= 0 && didxSize >= kDidxEntrySize) {
        if (!hasData)
            return fail(u"The bank has a media index (DIDX) but no DATA chunk."_s);

        media.reserve(static_cast<qsizetype>(didxSize / kDidxEntrySize));
        const qint64 end = didxOffset + didxSize;

        for (qint64 e = didxOffset; e + kDidxEntrySize <= end; e += kDidxEntrySize) {
            Media m;
            m.id = readU32(data, e);
            const quint32 relative = readU32(data, e + 4);
            m.size = readU32(data, e + 8);

            if (static_cast<quint64>(relative) + m.size > dataSize)
                return fail(u"WEM %1 points outside of the DATA chunk."_s.arg(m.id));

            m.offset = dataOffset + relative;
            media.append(m);
        }
    }

    return true;
}

void BnkFile::readHirc(const QByteArray& data, qint64 body, quint32 size) {
    if (size < 4)
        return;

    const qint64 end = body + size;
    hircObjects = readU32(data, body);

    qint64 pos = body + 4;
    for (quint32 i = 0; i < hircObjects && pos + 5 <= end; ++i) {
        const auto type = static_cast<quint8>(data.at(pos));
        const quint32 objectSize = readU32(data, pos + 1);

        if (type == kHircSound)
            ++hircSounds;
        else if (type == kHircEvent)
            ++hircEvents;

        pos += 5 + static_cast<qint64>(objectSize);
    }
}

void BnkFile::readStid(const QByteArray& data, qint64 body, quint32 size) {
    if (size < 8)
        return;

    const qint64 end = body + size;
    const quint32 count = readU32(data, body + 4);

    qint64 pos = body + 8;
    for (quint32 i = 0; i < count && pos + 5 <= end; ++i) {
        const quint32 id = readU32(data, pos);
        const auto length = static_cast<quint8>(data.at(pos + 4));
        pos += 5;
        if (pos + length > end)
            break;

        const QString name = QString::fromLatin1(data.constData() + pos, length);
        pos += length;

        if (bankName.isEmpty() || id == bankId)
            bankName = name;
    }
}


// ---------------------------------------------------------------------------
// Rebuild (BNK viewer: Save Changes)
// ---------------------------------------------------------------------------

namespace {

// Wwise codec plugin ids (company 0, type 1 = codec): PCM 1, ADPCM 2, Vorbis 4.
constexpr quint32 kPluginPcm = 0x00010001;
constexpr quint32 kPluginVorbis = 0x00040001;

quint32 pluginFor(const QByteArray& wem) {
    switch (WemFile::formatTag(wem)) {
    case 0x0001:
    case 0xFFFE: return kPluginPcm;
    case 0xFFFF: return kPluginVorbis;
    default:     return 0;   // Unknown: keep the bank's.
    }
}

} // namespace

QByteArray BnkFile::rebuild(const QByteArray& data, const BnkFile& bank, const QHash<int, QByteArray>& replacements,
                            int* hircPatched, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QByteArray();
    };
    if (hircPatched)
        *hircPatched = 0;
    if (bank.truncated)
        return fail(u"The bank ends in the middle of a chunk; it can't be rebuilt."_s);
    if (bank.media.isEmpty())
        return fail(u"The bank has no embedded audio."_s);

    const bool big = bank.isBigEndian;
    const auto rd = [&](const QByteArray& d, qint64 o) {
        const auto* p = reinterpret_cast<const uchar*>(d.constData()) + o;
        return big ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
    };
    const auto wr = [&](QByteArray& d, qint64 o, quint32 v) {
        auto* p = reinterpret_cast<uchar*>(d.data()) + o;
        if (big)
            qToBigEndian(v, p);
        else
            qToLittleEndian(v, p);
    };
    const auto append = [&](QByteArray& d, quint32 v) {
        const qint64 o = d.size();
        d.resize(o + 4);
        wr(d, o, v);
    };

    // Alignment of the media in DATA: 16 when every original offset is a multiple of 16 (Wwise), else none.
    quint32 alignment = 16;
    for (const Media& m : bank.media)
        if ((m.offset - bank.dataOffset) % 16 != 0)
            alignment = 1;

    // New DATA and the new offsets / sizes, in DIDX order.
    QByteArray newData;
    QList<quint32> offsets, sizes;
    struct Change { quint32 id, oldSize, newSize, plugin; };
    QList<Change> changes;
    for (int i = 0; i < bank.media.size(); ++i) {
        const Media& m = bank.media[i];
        const bool replaced = replacements.contains(i);
        const QByteArray bytes = replaced ? replacements.value(i) : data.mid(m.offset, m.size);
        while (newData.size() % alignment != 0)
            newData.append('\0');
        offsets.append(static_cast<quint32>(newData.size()));
        sizes.append(static_cast<quint32>(bytes.size()));
        newData.append(bytes);
        if (replaced) {
            const quint32 oldPlugin = pluginFor(data.mid(m.offset, m.size));
            const quint32 newPlugin = pluginFor(bytes);
            changes.append({m.id, m.size, static_cast<quint32>(bytes.size()),
                            newPlugin != 0 && newPlugin != oldPlugin ? newPlugin : 0});
        }
    }

    QByteArray out;
    out.reserve(data.size() + newData.size() - static_cast<qint64>(bank.dataSize) + 64);
    qint64 pos = 0;
    const qint64 total = data.size();
    while (pos + 8 <= total) {
        const QByteArray tag = data.mid(pos, 4);
        const quint32 size = rd(data, pos + 4);
        const qint64 body = pos + 8;
        if (body + static_cast<qint64>(size) > total)
            return fail(u"The bank ends in the middle of the %1 chunk."_s.arg(QString::fromLatin1(tag)));

        QByteArray chunk;
        if (tag == "DIDX") {
            for (int i = 0; i < bank.media.size(); ++i) {
                append(chunk, bank.media[i].id);
                append(chunk, offsets[i]);
                append(chunk, sizes[i]);
            }
        } else if (tag == "DATA") {
            chunk = newData;
        } else if (tag == "HIRC") {
            chunk = data.mid(body, size);
            // Objects: u8 type, u32 size, body. Look for the replaced media's source records inside each.
            if (chunk.size() >= 4) {
                const quint32 count = rd(chunk, 0);
                qint64 p = 4;
                for (quint32 o = 0; o < count && p + 5 <= chunk.size(); ++o) {
                    const qint64 objectSize = rd(chunk, p + 1);
                    const qint64 objectBody = p + 5;
                    const qint64 objectEnd = std::min<qint64>(objectBody + objectSize, chunk.size());
                    for (qint64 k = objectBody + 5; k + 8 <= objectEnd; ++k) {
                        for (const Change& change : std::as_const(changes)) {
                            if (rd(chunk, k) != change.id || rd(chunk, k + 4) != change.oldSize)
                                continue;
                            const quint8 streamType = static_cast<quint8>(chunk.at(k - 1));
                            const quint32 plugin = rd(chunk, k - 5);
                            if (streamType > 1 || (plugin & 0xF) != 1 || plugin > 0x00FFFFFF)
                                continue;   // Not a source record (in-memory / prefetch media of a codec).
                            wr(chunk, k + 4, change.newSize);
                            if (change.plugin != 0)
                                wr(chunk, k - 5, change.plugin);
                            if (hircPatched)
                                ++*hircPatched;
                        }
                    }
                    p = objectBody + objectSize;
                }
            }
        } else {
            chunk = data.mid(body, size);
        }

        out.append(tag);
        append(out, static_cast<quint32>(chunk.size()));
        out.append(chunk);
        pos = body + size;
    }
    if (pos < total)
        out.append(data.mid(pos));   // Trailing bytes after the last chunk.
    return out;
}
