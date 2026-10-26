#include "bnk_file.h"

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
