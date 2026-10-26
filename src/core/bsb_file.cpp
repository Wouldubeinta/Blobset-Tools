#include "bsb_file.h"

#include <QtEndian>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Bsb {

namespace {

quint32 u32(const QByteArray& data, qsizetype at, bool bigEndian) {
    const uchar* p = reinterpret_cast<const uchar*>(data.constData()) + at;
    return bigEndian ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
}

void put32(QByteArray& out, qsizetype at, quint32 value, bool bigEndian) {
    uchar* p = reinterpret_cast<uchar*>(out.data()) + at;
    if (bigEndian)
        qToBigEndian(value, p);
    else
        qToLittleEndian(value, p);
}

} // namespace

std::optional<Header> readHeader(const QByteArray& data) {
    if (data.size() < kHeaderSize)
        return std::nullopt;

    for (const bool bigEndian : {false, true}) {
        if (u32(data, 0, bigEndian) != kMagic)
            continue;
        Header header;
        header.bigEndian = bigEndian;
        header.movieOffset = u32(data, 4, bigEndian);
        header.movieSize = u32(data, 8, bigEndian);
        header.txpkOffset = u32(data, 12, bigEndian);
        header.txpkSize = u32(data, 16, bigEndian);
        if (header.movieOffset < kHeaderSize || qint64(header.movieOffset) + header.movieSize > data.size())
            return std::nullopt;
        if (header.txpkSize > 0 && qint64(header.txpkOffset) + header.txpkSize > data.size())
            return std::nullopt;
        return header;
    }
    return std::nullopt;
}

qint64 mainSectionSize(const QByteArray& data) {
    const std::optional<Header> header = readHeader(data);
    return header ? header->mainSize() : -1;
}

QByteArray build(const QByteArray& movie, const QByteArray& txpkMain, bool bigEndian) {
    QByteArray out(kHeaderSize, '\0');
    put32(out, 0, kMagic, bigEndian);
    put32(out, 4, quint32(kHeaderSize), bigEndian);
    put32(out, 8, quint32(movie.size()), bigEndian);
    put32(out, 12, quint32(kHeaderSize + movie.size()), bigEndian);
    put32(out, 16, quint32(txpkMain.size()), bigEndian);
    out += movie;
    out += txpkMain;
    return out;
}

QVariantMap movieInfo(const QByteArray& movie) {
    QVariantMap info;
    if (movie.size() < 8)
        return info;

    const QByteArray signature = movie.left(3);
    if (signature != "FWS" && signature != "CWS" && signature != "GFX" && signature != "CFX")
        return info;

    info[u"signature"_s] = QString::fromLatin1(signature);
    info[u"compressed"_s] = signature.startsWith('C');
    info[u"version"_s] = int(uchar(movie[3]));
    info[u"fileLength"_s] = qint64(qFromLittleEndian<quint32>(movie.constData() + 4));

    // Compressed movies (zlib after the 8 byte header) aren't inflated here: the rest is only known for FWS / GFX.
    if (signature.startsWith('C') || movie.size() < 9)
        return info;

    // Stage RECT: 5 bit field size, then xMin, xMax, yMin, yMax in twips.
    const auto bits = [&](qint64 start, int count) {
        quint32 value = 0;
        for (int i = 0; i < count; ++i) {
            const qint64 bit = start + i;
            const qsizetype byte = 8 + bit / 8;
            if (byte >= movie.size())
                return value;
            value = (value << 1) | ((uchar(movie[byte]) >> (7 - bit % 8)) & 1u);
        }
        return value;
    };
    const int fieldBits = int(bits(0, 5));
    const auto signedField = [&](int index) {
        const quint32 raw = bits(5 + qint64(index) * fieldBits, fieldBits);
        if (fieldBits > 0 && (raw & (1u << (fieldBits - 1))))
            return qint64(raw) - (qint64(1) << fieldBits);
        return qint64(raw);
    };
    const qint64 xMin = signedField(0), xMax = signedField(1), yMin = signedField(2), yMax = signedField(3);
    info[u"width"_s] = int((xMax - xMin) / 20);
    info[u"height"_s] = int((yMax - yMin) / 20);

    const qsizetype afterRect = 8 + (5 + 4 * fieldBits + 7) / 8;
    if (afterRect + 4 <= movie.size()) {
        info[u"frameRate"_s] = qFromLittleEndian<quint16>(movie.constData() + afterRect) / 256.0;
        info[u"frameCount"_s] = int(qFromLittleEndian<quint16>(movie.constData() + afterRect + 2));
    }
    return info;
}

} // namespace Bsb
