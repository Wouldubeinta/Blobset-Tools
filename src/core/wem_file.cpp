#include "wem_file.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace WemFile {

namespace {

constexpr quint16 kFormatPcm = 0x0001;
constexpr quint16 kFormatFloat = 0x0003;
constexpr quint16 kFormatExtensible = 0xFFFE;
constexpr quint16 kFormatVorbis = 0xFFFF;

quint16 u16(const QByteArray& d, qint64 o) { return qFromLittleEndian<quint16>(d.constData() + o); }
quint32 u32(const QByteArray& d, qint64 o) { return qFromLittleEndian<quint32>(d.constData() + o); }

void put16(QByteArray& out, quint16 v) {
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}
void put32(QByteArray& out, quint32 v) {
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

// pad: RIFF chunks are word aligned, except the last one (Wwise doesn't pad an odd sized "data" at the end).
void appendChunk(QByteArray& out, const QByteArray& tag, const QByteArray& body, bool pad = true) {
    out.append(tag.left(4));
    put32(out, static_cast<quint32>(body.size()));
    out.append(body);
    if (pad && (body.size() & 1))
        out.append('\0');
}

QByteArray riffFile(const QByteArray& chunks) {
    QByteArray out;
    out.reserve(chunks.size() + 12);
    out.append("RIFF");
    put32(out, static_cast<quint32>(chunks.size() + 4));
    out.append("WAVE");
    out.append(chunks);
    return out;
}

const Chunk* findChunk(const QList<Chunk>& chunks, const char* tag) {
    for (const Chunk& c : chunks)
        if (c.tag == tag)
            return &c;
    return nullptr;
}

// Standard speaker masks for 1..8 channels (mono = front centre, as Wwise does).
quint32 speakerMask(int channels) {
    switch (channels) {
    case 1: return 0x4;
    case 2: return 0x3;
    case 3: return 0x7;
    case 4: return 0x33;
    case 5: return 0x37;
    case 6: return 0x3F;
    case 7: return 0x13F;
    case 8: return 0x63F;
    default: return channels > 0 && channels < 32 ? (1u << channels) - 1 : 0;
    }
}

// Converts the WAV's samples to interleaved 16 bit little endian.
bool toPcm16(const QByteArray& wav, const Chunk& data, quint16 format, int bits, int channels, QByteArray& out,
             QString* error) {
    const int bytes = bits / 8;
    if (bytes <= 0 || bits % 8 != 0) {
        if (error)
            *error = u"%1 bit WAVs aren't supported."_s.arg(bits);
        return false;
    }
    const qint64 frameSize = static_cast<qint64>(bytes) * channels;
    const qint64 frames = data.size / frameSize;
    const qint64 samples = frames * channels;
    const auto* in = reinterpret_cast<const uchar*>(wav.constData() + data.body);

    if (format == kFormatPcm && bits == 16) {
        out = wav.mid(data.body, samples * 2);
        return true;
    }

    out.resize(samples * 2);
    auto* o = reinterpret_cast<uchar*>(out.data());
    const auto store = [&o](qint64 i, double value) {
        const auto s = static_cast<qint16>(std::clamp(std::lround(value), -32768L, 32767L));
        qToLittleEndian(s, o + i * 2);
    };

    for (qint64 i = 0; i < samples; ++i) {
        const uchar* p = in + i * bytes;
        if (format == kFormatFloat) {
            double v = 0.0;
            if (bits == 32) {
                float f;
                const quint32 raw = qFromLittleEndian<quint32>(p);
                std::memcpy(&f, &raw, 4);
                v = f;
            } else if (bits == 64) {
                double d;
                const quint64 raw = qFromLittleEndian<quint64>(p);
                std::memcpy(&d, &raw, 8);
                v = d;
            } else {
                if (error)
                    *error = u"%1 bit float WAVs aren't supported."_s.arg(bits);
                return false;
            }
            store(i, std::clamp(v, -1.0, 1.0) * 32767.0);
        } else {
            switch (bits) {
            case 8:  store(i, (static_cast<int>(p[0]) - 128) * 256.0); break;   // 8 bit WAV is unsigned.
            case 24: {
                qint32 v = p[0] | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000)
                    v |= ~0xFFFFFF;
                store(i, v / 256.0);
                break;
            }
            case 32: store(i, qFromLittleEndian<qint32>(p) / 65536.0); break;
            default:
                if (error)
                    *error = u"%1 bit WAVs aren't supported."_s.arg(bits);
                return false;
            }
        }
    }
    return true;
}

} // namespace

QList<Chunk> readChunks(const QByteArray& riff, QString* error) {
    QList<Chunk> chunks;
    if (riff.size() < 12 || riff.mid(8, 4) != "WAVE" || !riff.startsWith("RIFF")) {
        if (error)
            *error = riff.startsWith("RIFX") ? u"Big endian (console) WEMs aren't supported."_s
                                             : u"This isn't a RIFF / WAVE file."_s;
        return {};
    }
    qint64 pos = 12;
    const qint64 total = riff.size();
    while (pos + 8 <= total) {
        Chunk c;
        c.tag = riff.mid(pos, 4);
        c.size = u32(riff, pos + 4);
        c.body = pos + 8;
        if (c.body + static_cast<qint64>(c.size) > total)
            c.size = static_cast<quint32>(total - c.body);   // A truncated last chunk (often "data"): keep what's there.
        chunks.append(c);
        pos = c.body + c.size + (c.size & 1);
    }
    if (!findChunk(chunks, "fmt ") || !findChunk(chunks, "data")) {
        if (error)
            *error = u"The file has no fmt / data chunk."_s;
        return {};
    }
    return chunks;
}

quint16 formatTag(const QByteArray& riff) {
    const QList<Chunk> chunks = readChunks(riff);
    const Chunk* fmt = findChunk(chunks, "fmt ");
    return fmt && fmt->size >= 2 ? u16(riff, fmt->body) : 0;
}

QByteArray fromWav(const QByteArray& wav, const QByteArray& originalWem, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QByteArray();
    };

    QString why;
    const QList<Chunk> wavChunks = readChunks(wav, &why);
    if (wavChunks.isEmpty())
        return fail(u"The WAV can't be read: "_s + why);
    const QList<Chunk> wemChunks = readChunks(originalWem, &why);
    if (wemChunks.isEmpty())
        return fail(u"The WEM it replaces can't be read: "_s + why);

    // The WAV's format.
    const Chunk& fmt = *findChunk(wavChunks, "fmt ");
    if (fmt.size < 16)
        return fail(u"The WAV's fmt chunk is too short."_s);
    quint16 format = u16(wav, fmt.body);
    const int channels = u16(wav, fmt.body + 2);
    const quint32 sampleRate = u32(wav, fmt.body + 4);
    const int bits = u16(wav, fmt.body + 14);
    if (format == kFormatExtensible && fmt.size >= 40)
        format = u16(wav, fmt.body + 24);   // SubFormat GUID: its first two bytes are the format.
    if (format != kFormatPcm && format != kFormatFloat)
        return fail(u"The WAV isn't PCM (format 0x%1); save it as a 16 bit PCM WAV."_s.arg(format, 4, 16, QChar(u'0')));
    if (channels < 1 || channels > 8)
        return fail(u"The WAV has %1 channels (1 - 8 are supported)."_s.arg(channels));
    if (sampleRate < 1000 || sampleRate > 192000)
        return fail(u"The WAV's sample rate (%1 Hz) isn't usable."_s.arg(sampleRate));

    QByteArray pcm;
    if (!toPcm16(wav, *findChunk(wavChunks, "data"), format, bits, channels, pcm, &why))
        return fail(why);
    if (pcm.isEmpty())
        return fail(u"The WAV has no audio."_s);

    // Channel layout: the original's when the channel count is the same (newer Wwise stores an AkChannelConfig:
    // mask << 12 | type << 8 | channels; older ones a plain speaker mask), else the standard one in its style.
    const Chunk& wemFmt = *findChunk(wemChunks, "fmt ");
    const int wemChannels = wemFmt.size >= 4 ? u16(originalWem, wemFmt.body + 2) : 0;
    const quint32 originalLayout = wemFmt.size >= 0x18 ? u32(originalWem, wemFmt.body + 0x14) : 0;
    const bool configStyle = originalLayout != 0 && (originalLayout & 0xFF) == static_cast<quint32>(wemChannels)
                             && ((originalLayout >> 8) & 0xF) <= 3;
    quint32 layout;
    if (originalLayout != 0 && wemChannels == channels)
        layout = originalLayout;
    else if (configStyle || originalLayout == 0)
        layout = (speakerMask(channels) << 12) | (1u << 8) | static_cast<quint32>(channels);
    else
        layout = speakerMask(channels);

    // Wwise's PCM "fmt ": WAVE_FORMAT_EXTENSIBLE, 16 bit, cbSize 6, then 2 bytes + the channel layout.
    QByteArray newFmt;
    put16(newFmt, kFormatExtensible);
    put16(newFmt, static_cast<quint16>(channels));
    put32(newFmt, sampleRate);
    put32(newFmt, sampleRate * static_cast<quint32>(channels) * 2);
    put16(newFmt, static_cast<quint16>(channels * 2));
    put16(newFmt, 16);
    put16(newFmt, 6);
    put16(newFmt, 0);
    put32(newFmt, layout);

    QByteArray body;
    appendChunk(body, "fmt ", newFmt);
    // The original's extra chunks (hash, akd, JUNK, ...), in their order. Left out: the format / audio itself and
    // what points into the old audio (Vorbis setup / seek table, loop and cue points, markers).
    static const QList<QByteArray> dropped = {"fmt ", "data", "vorb", "seek", "smpl", "cue ", "LIST", "fact"};
    for (const Chunk& c : wemChunks)
        if (!dropped.contains(c.tag))
            appendChunk(body, c.tag, originalWem.mid(c.body, c.size));
    appendChunk(body, "data", pcm, false);
    return riffFile(body);
}

QByteArray withOriginalHash(const QByteArray& wem, const QByteArray& originalWem, QString* error) {
    QString why;
    const QList<Chunk> chunks = readChunks(wem, &why);
    if (chunks.isEmpty()) {
        if (error)
            *error = u"The WEM can't be read: "_s + why;
        return {};
    }
    const QList<Chunk> originalChunks = readChunks(originalWem);
    const Chunk* hash = findChunk(originalChunks, "hash");
    if (!hash)
        return wem;   // Nothing to carry over.

    const QByteArray hashBody = originalWem.mid(hash->body, hash->size);
    const bool hasHash = findChunk(chunks, "hash") != nullptr;
    QByteArray body;
    for (qsizetype i = 0; i < chunks.size(); ++i) {
        const Chunk& c = chunks[i];
        const bool last = i + 1 == chunks.size();
        if (c.tag == "hash") {
            appendChunk(body, c.tag, hashBody, !last);
            continue;
        }
        appendChunk(body, c.tag, wem.mid(c.body, c.size), !last);
        if (c.tag == "fmt " && !hasHash)
            appendChunk(body, "hash", hashBody);
    }
    return riffFile(body);
}

QString describe(const QByteArray& riff) {
    const QList<Chunk> chunks = readChunks(riff);
    const Chunk* fmt = findChunk(chunks, "fmt ");
    if (!fmt || fmt->size < 16)
        return u"unknown format"_s;
    const quint16 format = u16(riff, fmt->body);
    const int channels = u16(riff, fmt->body + 2);
    const quint32 rate = u32(riff, fmt->body + 4);
    const int bits = u16(riff, fmt->body + 14);
    if (format == kFormatVorbis)
        return u"Vorbis, %1 ch, %2 Hz"_s.arg(channels).arg(rate);
    if (format == kFormatPcm || format == kFormatExtensible) {
        const Chunk* data = findChunk(chunks, "data");
        const double seconds = data && channels > 0 && rate > 0 && bits > 0
                                   ? double(data->size) / (double(channels) * (bits / 8) * rate) : 0.0;
        return u"PCM %1 bit, %2 ch, %3 Hz, %4 s"_s.arg(bits).arg(channels).arg(rate).arg(seconds, 0, 'f', 2);
    }
    return u"format 0x%1, %2 ch, %3 Hz"_s.arg(format, 4, 16, QChar(u'0')).arg(channels).arg(rate);
}

} // namespace WemFile
