#include "rl26_save.h"

#include <QtEndian>

#include <array>

using namespace Qt::StringLiterals;

namespace Rl26Save {
namespace {

constexpr quint64 kKey = 0xBABABABABABABABAull;
constexpr int kKeyBytes = 64;

void xorHead(QByteArray& bytes) {
    const int n = qMin(kKeyBytes, static_cast<int>(bytes.size()));
    char* p = bytes.data();
    for (int i = 0; i < n; ++i)
        p[i] = static_cast<char>(static_cast<quint8>(p[i]) ^ static_cast<quint8>(kKey >> (8 * (i & 7))));
}

const std::array<quint32, 256>& crcTable() {
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

} // namespace

quint32 crc32(const QByteArray& data) {
    const auto& table = crcTable();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data)
        c = table[(c ^ static_cast<quint8>(ch)) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

File decode(const QByteArray& file) {
    File out;
    if (file.size() < kHeaderSize) {
        out.error = u"The file is too small to be a save (%1 bytes)."_s.arg(file.size());
        return out;
    }
    const auto* h = reinterpret_cast<const uchar*>(file.constData());
    const quint32 magic = qFromLittleEndian<quint32>(h);
    const quint32 version = qFromLittleEndian<quint32>(h + 4);
    const quint32 checksum = qFromLittleEndian<quint32>(h + 8);
    const quint32 size = qFromLittleEndian<quint32>(h + 12);
    if (magic != kMagic) {
        out.error = u"This isn't a Rugby League 26 save (magic 0x%1)."_s.arg(magic, 8, 16, QChar(u'0'));
        return out;
    }
    QByteArray payload = file.mid(kHeaderSize);
    out.version = version & ~kCompressedFlags;
    out.compressed = (version & kCompressedFlags) == kCompressedFlags;
    out.sizeBias = size - static_cast<quint32>(payload.size());

    if (!out.compressed) {
        out.checksumOk = crc32(payload) == checksum;
        out.data = payload;
        out.ok = true;
        return out;
    }

    xorHead(payload);
    out.checksumOk = (crc32(payload) ^ kChecksumXor) == checksum;
    // qUncompress takes Qt's 4 byte (big endian) size hint first; 0 = unknown, it grows the buffer as it goes.
    QByteArray withHint(4, '\0');
    withHint.append(payload);
    out.data = qUncompress(withHint);
    if (out.data.isEmpty()) {
        out.error = u"The save's data didn't decompress (the file may be damaged)."_s;
        return out;
    }
    out.ok = true;
    return out;
}

QByteArray encode(const QByteArray& data, quint32 version, quint32 sizeBias) {
    QByteArray stream = qCompress(data, 9).mid(4);   // Drop Qt's size hint: a plain zlib stream.
    const quint32 checksum = crc32(stream) ^ kChecksumXor;
    xorHead(stream);

    QByteArray out(kHeaderSize, '\0');
    auto* h = reinterpret_cast<uchar*>(out.data());
    qToLittleEndian<quint32>(kMagic, h);
    qToLittleEndian<quint32>(version | kCompressedFlags, h + 4);
    qToLittleEndian<quint32>(checksum, h + 8);
    qToLittleEndian<quint32>(static_cast<quint32>(stream.size()) + sizeBias, h + 12);
    out.append(stream);
    return out;
}

} // namespace Rl26Save
