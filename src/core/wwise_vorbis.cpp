/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include "wwise_vorbis.h"

#include <vorbis/vorbisenc.h>

#include <QHash>
#include <QList>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;

namespace WwiseVorbis {

namespace {

constexpr unsigned char kLibrary[] = {
#include "wwise_vorbis_codebooks.inc"
};

constexpr quint32 kRate = 44100;
constexpr float kQuality = 0.4f;
constexpr double kLowpassKHz = 18.0;
constexpr int kSeekSpacing = 16384;
constexpr double kPi = 3.14159265358979323846;   // Samples between the seek table's entries (at least).

// The decoder memory sizes Wwise stores for the game's setups (by the setup's hash): mono / stereo.
struct KnownSetup {
    quint32 hash;
    quint32 alloc;
    quint32 alloc64;
};
constexpr KnownSetup kKnownSetups[] = {{0xB3DEA448u, 18180, 18636}, {0xD54BA8E8u, 16080, 16560}};

// ---- Bits (Vorbis packs them from the lowest bit of each byte) -----------------------------------------------------

struct BitReader {
    const uchar* data = nullptr;
    qsizetype bits = 0;
    qsizetype pos = 0;
    bool overrun = false;

    BitReader(const QByteArray& bytes, qsizetype startByte = 0)
        : data(reinterpret_cast<const uchar*>(bytes.constData())), bits(bytes.size() * 8), pos(startByte * 8) {}
    BitReader(const uchar* d, qsizetype size) : data(d), bits(size * 8) {}

    quint32 get(int n) {
        quint32 v = 0;
        for (int i = 0; i < n; ++i, ++pos) {
            if (pos >= bits) {
                overrun = true;
                continue;
            }
            v |= quint32((data[pos >> 3] >> (pos & 7)) & 1) << i;
        }
        return v;
    }
};

struct BitWriter {
    QByteArray bytes;
    qsizetype pos = 0;

    void put(int n, quint32 v) {
        for (int i = 0; i < n; ++i, ++pos) {
            if ((pos >> 3) >= bytes.size())
                bytes.append('\0');
            if ((v >> i) & 1)
                bytes[pos >> 3] = char(bytes.at(pos >> 3) | (1 << (pos & 7)));
        }
    }
    // The bits as a key (the bytes + the bit count).
    QByteArray key() const {
        QByteArray k = bytes;
        k.append(reinterpret_cast<const char*>(&pos), sizeof(pos));
        return k;
    }
};

int ilog(quint32 v) {
    int r = 0;
    while (v) {
        ++r;
        v >>= 1;
    }
    return r;
}

quint32 quantvals(quint32 entries, quint32 dimensions) {
    const int bits = ilog(entries);
    quint32 vals = entries >> ((bits - 1) * (dimensions - 1) / dimensions);
    for (int guard = 0; guard < 100000; ++guard) {
        quint64 acc = 1, acc1 = 1;
        for (quint32 i = 0; i < dimensions; ++i) {
            acc *= vals;
            acc1 *= vals + 1;
        }
        if (acc <= entries && acc1 > entries)
            return vals;
        if (acc > entries)
            --vals;
        else
            ++vals;
    }
    return 0;
}

// ---- The codebook library ------------------------------------------------------------------------------------------

// A packed (Wwise) codebook as a standard Vorbis one (ww2ogg's rebuild).
bool unpackCodebook(BitReader& in, BitWriter& out) {
    out.put(24, 0x564342);
    const quint32 dimensions = in.get(4);
    out.put(16, dimensions);
    const quint32 entries = in.get(14);
    out.put(24, entries);
    const quint32 ordered = in.get(1);
    out.put(1, ordered);
    if (ordered) {
        out.put(5, in.get(5));
        quint32 current = 0;
        while (current < entries && !in.overrun) {
            const int bits = ilog(entries - current);
            const quint32 number = in.get(bits);
            out.put(bits, number);
            current += number;
        }
    } else {
        const quint32 lengthBits = in.get(3);
        const quint32 sparse = in.get(1);
        out.put(1, sparse);
        if (lengthBits == 0 || lengthBits > 5)
            return false;
        for (quint32 i = 0; i < entries && !in.overrun; ++i) {
            bool present = true;
            if (sparse) {
                const quint32 p = in.get(1);
                out.put(1, p);
                present = p != 0;
            }
            if (present)
                out.put(5, in.get(int(lengthBits)));
        }
    }
    const quint32 lookup = in.get(1);
    out.put(4, lookup);
    if (lookup == 1) {
        out.put(32, in.get(32));
        out.put(32, in.get(32));
        const quint32 valueLength = in.get(4);
        out.put(4, valueLength);
        out.put(1, in.get(1));
        const quint32 count = quantvals(entries, dimensions);
        for (quint32 i = 0; i < count; ++i)
            out.put(int(valueLength) + 1, in.get(int(valueLength) + 1));
    }
    return !in.overrun;
}

struct Library {
    QList<QByteArray> standard;              // By id: the codebook as standard Vorbis bits (key()).
    QHash<QByteArray, QList<int>> idsOf;     // Standard bits -> the ids that are that codebook (a few are twice).
};

const Library& library() {
    static const Library lib = [] {
        Library l;
        const qsizetype size = sizeof(kLibrary);
        const quint32 tableStart = qFromLittleEndian<quint32>(kLibrary + size - 4);
        const int count = int((size - tableStart) / 4) - 1;
        for (int id = 0; id < count; ++id) {
            const quint32 from = qFromLittleEndian<quint32>(kLibrary + tableStart + 4 * id);
            const quint32 to = qFromLittleEndian<quint32>(kLibrary + tableStart + 4 * (id + 1));
            BitReader in(kLibrary + from, to - from);
            BitWriter out;
            unpackCodebook(in, out);
            l.standard << out.key();
            l.idsOf[out.key()] << id;
        }
        return l;
    }();
    return lib;
}

// Copies one standard codebook (its bits) from the setup.
bool copyStandardCodebook(BitReader& in, BitWriter& out) {
    const auto copy = [&](int n) {
        const quint32 v = in.get(n);
        out.put(n, v);
        return v;
    };
    if (copy(24) != 0x564342)
        return false;
    const quint32 dimensions = copy(16);
    const quint32 entries = copy(24);
    if (copy(1)) {
        copy(5);
        quint32 current = 0;
        while (current < entries && !in.overrun)
            current += copy(ilog(entries - current));
    } else {
        const quint32 sparse = copy(1);
        for (quint32 i = 0; i < entries && !in.overrun; ++i)
            if (!sparse || copy(1))
                copy(5);
    }
    const quint32 lookup = copy(4);
    if (lookup == 1) {
        copy(32);
        copy(32);
        const quint32 valueLength = copy(4);
        copy(1);
        const quint32 count = quantvals(entries, dimensions);
        for (quint32 i = 0; i < count; ++i)
            copy(int(valueLength) + 1);
    } else if (lookup != 0) {
        return false;
    }
    return !in.overrun;
}

// ---- Setup: standard Vorbis -> Wwise -------------------------------------------------------------------------------

bool toWwiseSetup(const QByteArray& setup, int channels, QByteArray* wwise, QVector<bool>* blockFlags, QString* error) {
    const auto fail = [error](const QString& why) {
        if (error)
            *error = u"Vorbis setup: "_s + why;
        return false;
    };
    if (setup.size() < 8 || setup.at(0) != 5 || setup.mid(1, 6) != "vorbis")
        return fail(u"not a Vorbis setup packet."_s);
    BitReader in(setup, 7);
    BitWriter out;
    const auto copy = [&](int n) {
        const quint32 v = in.get(n);
        out.put(n, v);
        return v;
    };

    // Codebooks: by their index in the library. A few are in it twice: the one nearest the last picked (Wwise keeps
    // a setup's codebooks together).
    const Library& lib = library();
    const int codebooks = int(copy(8)) + 1;
    int previous = -1;
    for (int i = 0; i < codebooks; ++i) {
        BitWriter book;
        if (!copyStandardCodebook(in, book))
            return fail(u"codebook %1 can't be read."_s.arg(i));
        const auto it = lib.idsOf.constFind(book.key());
        if (it == lib.idsOf.constEnd())
            return fail(u"codebook %1 isn't one of Wwise's (the encoder's codebooks don't match its library)."_s.arg(i));
        int id = it.value().first();
        for (const int candidate : it.value())
            if (previous >= 0 && std::abs(candidate - previous) < std::abs(id - previous))
                id = candidate;
        out.put(10, quint32(id));
        previous = id;
    }

    // Time domain transforms: left out (always one, 0).
    const int times = int(in.get(6)) + 1;
    for (int i = 0; i < times; ++i)
        if (in.get(16) != 0)
            return fail(u"unexpected time domain transform."_s);

    // Floors (always type 1: the type is left out).
    const int floors = int(copy(6)) + 1;
    for (int i = 0; i < floors; ++i) {
        if (in.get(16) != 1)
            return fail(u"floor type 0 isn't supported."_s);
        const int partitions = int(copy(5));
        QVector<int> classes(partitions);
        int maximum = -1;
        for (int j = 0; j < partitions; ++j) {
            classes[j] = int(copy(4));
            maximum = std::max(maximum, classes[j]);
        }
        QVector<int> dimensions(maximum + 1);
        for (int j = 0; j <= maximum; ++j) {
            dimensions[j] = int(copy(3)) + 1;
            const int subclasses = int(copy(2));
            if (subclasses)
                copy(8);
            for (int k = 0; k < (1 << subclasses); ++k)
                copy(8);
        }
        copy(2);
        const int rangeBits = int(copy(4));
        for (int j = 0; j < partitions; ++j)
            for (int k = 0; k < dimensions[classes[j]]; ++k)
                copy(rangeBits);
    }

    // Residues: the type in 2 bits (16 in Vorbis).
    const int residues = int(in.get(6)) + 1;
    out.put(6, quint32(residues - 1));
    for (int i = 0; i < residues; ++i) {
        const quint32 type = in.get(16);
        if (type > 2)
            return fail(u"bad residue type."_s);
        out.put(2, type);
        copy(24);
        copy(24);
        copy(24);
        const int classifications = int(copy(6)) + 1;
        copy(8);
        QVector<quint32> cascade(classifications);
        for (int j = 0; j < classifications; ++j) {
            const quint32 low = copy(3);
            const quint32 high = copy(1) ? copy(5) : 0;
            cascade[j] = high * 8 + low;
        }
        for (int j = 0; j < classifications; ++j)
            for (int k = 0; k < 8; ++k)
                if (cascade[j] & (1u << k))
                    copy(8);
    }

    // Mappings (always type 0: left out).
    const int mappings = int(copy(6)) + 1;
    for (int i = 0; i < mappings; ++i) {
        if (in.get(16) != 0)
            return fail(u"bad mapping type."_s);
        const int submaps = copy(1) ? int(copy(4)) + 1 : 1;
        if (copy(1)) {
            const int steps = int(copy(8)) + 1;
            for (int j = 0; j < steps; ++j) {
                copy(ilog(quint32(channels - 1)));
                copy(ilog(quint32(channels - 1)));
            }
        }
        if (copy(2) != 0)
            return fail(u"bad mapping reserved bits."_s);
        if (submaps > 1)
            for (int j = 0; j < channels; ++j)
                copy(4);
        for (int j = 0; j < submaps; ++j) {
            copy(8);
            copy(8);
            copy(8);
        }
    }

    // Modes: the window and transform types are left out (always 0).
    const int modes = int(copy(6)) + 1;
    blockFlags->clear();
    for (int i = 0; i < modes; ++i) {
        *blockFlags << (copy(1) != 0);
        if (in.get(16) != 0 || in.get(16) != 0)
            return fail(u"bad mode."_s);
        copy(8);
    }
    if (in.get(1) != 1 || in.overrun)
        return fail(u"bad framing."_s);
    *wwise = out.bytes;
    return true;
}

// An audio packet without its packet type bit and (on long blocks) its previous / next window flags.
QByteArray toWwisePacket(const unsigned char* packet, long bytes, const QVector<bool>& blockFlags, int modeBits) {
    BitReader in(packet, bytes);
    BitWriter out;
    in.get(1);   // Packet type (0: audio).
    const quint32 mode = in.get(modeBits);
    out.put(modeBits, mode);
    if (mode < quint32(blockFlags.size()) && blockFlags[int(mode)])
        in.get(2);
    while (in.pos < in.bits) {
        const int n = int(std::min<qsizetype>(24, in.bits - in.pos));
        out.put(n, in.get(n));
    }
    if (out.bytes.isEmpty())
        out.bytes.append('\0');
    return out.bytes;
}

// ---- Audio -----------------------------------------------------------------------------------------------------------

// Windowed sinc resampling (Blackman, 32 taps a side), one channel.
std::vector<float> resample(const std::vector<float>& in, quint32 from, quint32 to) {
    if (from == to || in.empty())
        return in;
    const double ratio = double(from) / double(to);
    const double cutoff = std::min(1.0, double(to) / double(from)) * 0.97;
    const int half = 32;
    const qint64 outCount = qint64(std::floor(double(in.size()) / ratio));
    std::vector<float> out(size_t(std::max<qint64>(0, outCount)));
    for (qint64 n = 0; n < outCount; ++n) {
        const double t = double(n) * ratio;
        const qint64 centre = qint64(std::floor(t));
        double sum = 0, weight = 0;
        for (qint64 k = centre - half + 1; k <= centre + half; ++k) {
            const double x = t - double(k);
            const double w = 0.42 + 0.5 * std::cos(kPi * x / half) + 0.08 * std::cos(2 * kPi * x / half);
            if (std::abs(x) >= half)
                continue;
            const double s = x == 0 ? cutoff : std::sin(kPi * cutoff * x) / (kPi * x);
            const double c = s * w;
            weight += c;
            if (k >= 0 && k < qint64(in.size()))
                sum += c * in[size_t(k)];
        }
        out[size_t(n)] = float(weight != 0 ? sum / weight : 0);
    }
    return out;
}

} // namespace

bool encode(const QByteArray& pcm, int channels, quint32 sampleRate, int targetChannels, quint32 layout, Stream* result,
            QString* error) {
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    if (channels < 1 || targetChannels < 1 || targetChannels > 2 || sampleRate < 1000)
        return fail(u"Vorbis: unsupported audio (%1 channels, %2 Hz)."_s.arg(channels).arg(sampleRate));
    const qsizetype frames = pcm.size() / (2 * channels);
    if (frames <= 0)
        return fail(u"Vorbis: the WAV has no audio."_s);

    // The channels (mixed down to mono, a mono one doubled for stereo, the first two of more), at 44100 Hz.
    const auto sample = [&](qsizetype frame, int ch) {
        return float(qFromLittleEndian<qint16>(pcm.constData() + 2 * (frame * channels + ch))) / 32768.0f;
    };
    std::vector<std::vector<float>> audio(static_cast<size_t>(targetChannels), std::vector<float>(static_cast<size_t>(frames)));
    for (qsizetype f = 0; f < frames; ++f) {
        if (targetChannels == 1) {
            float sum = 0;
            for (int c = 0; c < channels; ++c)
                sum += sample(f, c);
            audio[0][size_t(f)] = sum / float(channels);
        } else {
            audio[0][size_t(f)] = sample(f, 0);
            audio[1][size_t(f)] = sample(f, channels > 1 ? 1 : 0);
        }
    }
    for (std::vector<float>& ch : audio)
        ch = resample(ch, sampleRate, kRate);
    const qsizetype samples = qsizetype(audio[0].size());
    if (samples <= 0)
        return fail(u"Vorbis: the WAV is too short."_s);

    // libvorbis: quality 0.4, 18 kHz lowpass (the game's setup).
    vorbis_info vi;
    vorbis_info_init(&vi);
    double lowpass = kLowpassKHz;
    if (vorbis_encode_setup_vbr(&vi, targetChannels, long(kRate), kQuality) != 0
        || vorbis_encode_ctl(&vi, OV_ECTL_LOWPASS_SET, &lowpass) != 0 || vorbis_encode_setup_init(&vi) != 0) {
        vorbis_info_clear(&vi);
        return fail(u"Vorbis: the encoder couldn't be set up."_s);
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_packet identification, comment, setupPacket;
    vorbis_analysis_headerout(&vd, &vc, &identification, &comment, &setupPacket);
    const QByteArray setup(reinterpret_cast<const char*>(setupPacket.packet), setupPacket.bytes);
    const int blockShort = int(vorbis_info_blocksize(&vi, 0));
    const int blockLong = int(vorbis_info_blocksize(&vi, 1));

    QList<QByteArray> packets;
    const auto drain = [&] {
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while (vorbis_bitrate_flushpacket(&vd, &op) == 1)
                packets << QByteArray(reinterpret_cast<const char*>(op.packet), op.bytes);
        }
    };
    constexpr qsizetype kChunk = 4096;
    for (qsizetype at = 0; at < samples; at += kChunk) {
        const int n = int(std::min(kChunk, samples - at));
        float** buffer = vorbis_analysis_buffer(&vd, n);
        for (int c = 0; c < targetChannels; ++c)
            std::copy(audio[size_t(c)].begin() + at, audio[size_t(c)].begin() + at + n, buffer[c]);
        vorbis_analysis_wrote(&vd, n);
        drain();
    }
    vorbis_analysis_wrote(&vd, 0);
    drain();
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);

    // The Wwise setup and packets.
    QByteArray wwSetup;
    QVector<bool> blockFlags;
    QString why;
    if (!toWwiseSetup(setup, targetChannels, &wwSetup, &blockFlags, &why))
        return fail(why);
    const int modeBits = ilog(quint32(blockFlags.size() - 1));
    QList<QByteArray> wwPackets;
    for (const QByteArray& p : std::as_const(packets))
        wwPackets << toWwisePacket(reinterpret_cast<const uchar*>(p.constData()), long(p.size()), blockFlags, modeBits);
    if (wwPackets.isEmpty() || wwSetup.size() > 0xFFFF)
        return fail(u"Vorbis: nothing was encoded."_s);

    // Samples each packet adds (a Vorbis packet gives its window's overlap with the last one), the seek table.
    QByteArray seek;
    qint64 decoded = 0, lastEntrySamples = 0;
    qsizetype offset = 2 + wwSetup.size(), lastEntryOffset = 0;   // From the setup's size (after the seek table).
    int previousBlock = -1;
    int maxPacket = 0;
    for (int i = 0; i < wwPackets.size(); ++i) {
        const QByteArray& p = wwPackets.at(i);
        BitReader modeReader(p);
        const quint32 mode = modeReader.get(modeBits);
        const int block = mode < quint32(blockFlags.size()) && blockFlags[int(mode)] ? blockLong : blockShort;
        if (previousBlock >= 0)
            decoded += previousBlock / 4 + block / 4;
        previousBlock = block;
        if (decoded - lastEntrySamples >= kSeekSpacing) {
            QByteArray entry(4, '\0');
            qToLittleEndian<quint16>(quint16(decoded - lastEntrySamples), entry.data());
            qToLittleEndian<quint16>(quint16(offset - lastEntryOffset), entry.data() + 2);
            seek += entry;
            lastEntrySamples = decoded;
            lastEntryOffset = offset;
        }
        if (p.size() > 0xFFFF)
            return fail(u"Vorbis: a packet is too big."_s);
        maxPacket = std::max(maxPacket, int(p.size()));
        offset += 2 + p.size();
    }

    QByteArray data = seek;
    const auto appendSized = [&data](const QByteArray& bytes) {
        QByteArray size(2, '\0');
        qToLittleEndian<quint16>(quint16(bytes.size()), size.data());
        data += size;
        data += bytes;
    };
    appendSized(wwSetup);
    for (const QByteArray& p : std::as_const(wwPackets))
        appendSized(p);

    // FNV-1 32 of the setup: Wwise's id for it (the decoder shares decoded setups by it).
    quint32 setupHash = 0x811C9DC5u;
    for (const char c : wwSetup) {
        setupHash *= 0x01000193u;
        setupHash ^= quint8(c);
    }
    quint32 alloc = 20480, alloc64 = 21000;   // Not one of the game's setups: room to spare.
    for (const KnownSetup& known : kKnownSetups)
        if (known.hash == setupHash) {
            alloc = known.alloc;
            alloc64 = known.alloc64;
        }

    const quint32 seekSize = quint32(seek.size());
    const quint32 afterSeek = quint32(data.size()) - seekSize;
    const quint32 lastExtra = quint32(std::max<qint64>(0, decoded - samples));
    QByteArray fmt;
    const auto put16 = [&fmt](quint32 v) {
        char b[2];
        qToLittleEndian<quint16>(quint16(v), b);
        fmt.append(b, 2);
    };
    const auto put32 = [&fmt](quint32 v) {
        char b[4];
        qToLittleEndian<quint32>(v, b);
        fmt.append(b, 4);
    };
    put16(0xFFFF);
    put16(quint32(targetChannels));
    put32(kRate);
    put32(quint32(quint64(afterSeek) * kRate / quint64(samples)));
    put16(0);
    put16(0);
    put16(0x30);
    put16(0);
    put32(layout);
    put32(quint32(samples));
    put32(quint32(2 + wwSetup.size()));     // The first audio packet (loop start), after the seek table.
    put32(afterSeek);                       // The data's end (loop end).
    put16(0);
    put16(lastExtra);
    put32(seekSize);
    put32(seekSize + 2 + quint32(wwSetup.size()));
    put16(quint32(maxPacket));
    put16(lastExtra);
    put32(alloc);
    put32(alloc64);
    put32(setupHash);
    fmt.append(char(ilog(quint32(blockShort)) - 1));
    fmt.append(char(ilog(quint32(blockLong)) - 1));

    result->fmt = fmt;
    result->data = data;
    result->samples = quint32(samples);
    result->channels = targetChannels;
    return true;
}

QByteArray standardSetup(const QByteArray& wwiseSetup, int channels) {
    BitReader in(wwiseSetup);
    BitWriter out;
    for (const char c : QByteArray("\x05vorbis", 7))
        out.put(8, quint8(c));
    const auto copy = [&](int n) {
        const quint32 v = in.get(n);
        out.put(n, v);
        return v;
    };
    const int codebooks = int(copy(8)) + 1;
    const qsizetype size = sizeof(kLibrary);
    const quint32 tableStart = qFromLittleEndian<quint32>(kLibrary + size - 4);
    for (int i = 0; i < codebooks; ++i) {
        const quint32 id = in.get(10);
        if (int(id) >= int((size - tableStart) / 4) - 1)
            return {};
        const quint32 from = qFromLittleEndian<quint32>(kLibrary + tableStart + 4 * id);
        const quint32 to = qFromLittleEndian<quint32>(kLibrary + tableStart + 4 * (id + 1));
        BitReader book(kLibrary + from, to - from);
        if (!unpackCodebook(book, out))
            return {};
    }
    out.put(6, 0);
    out.put(16, 0);
    const int floors = int(copy(6)) + 1;
    for (int i = 0; i < floors; ++i) {
        out.put(16, 1);
        const int partitions = int(copy(5));
        QVector<int> classes(partitions);
        int maximum = -1;
        for (int j = 0; j < partitions; ++j) {
            classes[j] = int(copy(4));
            maximum = std::max(maximum, classes[j]);
        }
        QVector<int> dimensions(maximum + 1);
        for (int j = 0; j <= maximum; ++j) {
            dimensions[j] = int(copy(3)) + 1;
            const int subclasses = int(copy(2));
            if (subclasses)
                copy(8);
            for (int k = 0; k < (1 << subclasses); ++k)
                copy(8);
        }
        copy(2);
        const int rangeBits = int(copy(4));
        for (int j = 0; j < partitions; ++j)
            for (int k = 0; k < dimensions[classes[j]]; ++k)
                copy(rangeBits);
    }
    const int residues = int(copy(6)) + 1;
    for (int i = 0; i < residues; ++i) {
        out.put(16, in.get(2));
        copy(24);
        copy(24);
        copy(24);
        const int classifications = int(copy(6)) + 1;
        copy(8);
        QVector<quint32> cascade(classifications);
        for (int j = 0; j < classifications; ++j) {
            const quint32 low = copy(3);
            const quint32 high = copy(1) ? copy(5) : 0;
            cascade[j] = high * 8 + low;
        }
        for (int j = 0; j < classifications; ++j)
            for (int k = 0; k < 8; ++k)
                if (cascade[j] & (1u << k))
                    copy(8);
    }
    const int mappings = int(copy(6)) + 1;
    for (int i = 0; i < mappings; ++i) {
        out.put(16, 0);
        const int submaps = copy(1) ? int(copy(4)) + 1 : 1;
        if (copy(1)) {
            const int steps = int(copy(8)) + 1;
            for (int j = 0; j < steps; ++j) {
                copy(ilog(quint32(channels - 1)));
                copy(ilog(quint32(channels - 1)));
            }
        }
        copy(2);
        if (submaps > 1)
            for (int j = 0; j < channels; ++j)
                copy(4);
        for (int j = 0; j < submaps; ++j) {
            copy(8);
            copy(8);
            copy(8);
        }
    }
    const int modes = int(copy(6)) + 1;
    for (int i = 0; i < modes; ++i) {
        copy(1);
        out.put(16, 0);
        out.put(16, 0);
        copy(8);
    }
    out.put(1, 1);
    return in.overrun ? QByteArray() : out.bytes;
}

} // namespace WwiseVorbis
