#include "fsb_file.h"

#include <QtEndian>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kFsb5HeaderSizeV0 = 0x40;
constexpr qint64 kFsb5HeaderSizeV1 = 0x3C;
constexpr quint32 kFsb5MaxVersion = 1;
constexpr quint32 kFsb5MaxCodec = 17;
constexpr qint64 kFsb4HeaderSize = 0x30;
constexpr qint64 kFsb3HeaderSize = 0x18;
constexpr qint64 kFsb34SampleHeaderMin = 0x40;
constexpr qint64 kFsb34BasicHeaderSize = 8;
constexpr int kMaxBankDepth = 8;

constexpr quint32 kFsb34BasicHeaders = 0x02;    // FMOD_FSB_SOURCE_BASICHEADERS
constexpr quint32 kFsb34LoopModes = 0x02 | 0x04; // FSOUND_LOOP_NORMAL | FSOUND_LOOP_BIDI

constexpr quint32 kFsb5Rates[] = {4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
constexpr quint32 kFsb5Channels[] = {1, 2, 6, 8};

quint16 u16(const QByteArray& data, qint64 offset) {
    return qFromLittleEndian<quint16>(data.constData() + offset);
}

quint32 u32(const QByteArray& data, qint64 offset) {
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

quint64 u64(const QByteArray& data, qint64 offset) {
    return qFromLittleEndian<quint64>(data.constData() + offset);
}

// Four printable ASCII characters - what a RIFF chunk tag looks like.
bool looksLikeTag(const QByteArray& data, qint64 pos) {
    if (pos < 0 || pos + 4 > data.size())
        return false;
    for (qint64 i = pos; i < pos + 4; ++i) {
        const auto c = static_cast<uchar>(data.at(i));
        if (c < 0x20 || c > 0x7E)
            return false;
    }
    return true;
}

QString fsb5CodecName(quint32 mode) {
    switch (mode) {
    case 1:  return u"PCM 8-bit"_s;
    case 2:  return u"PCM 16-bit"_s;
    case 3:  return u"PCM 24-bit"_s;
    case 4:  return u"PCM 32-bit"_s;
    case 5:  return u"PCM Float"_s;
    case 6:  return u"GameCube ADPCM"_s;
    case 7:  return u"IMA ADPCM"_s;
    case 8:  return u"PS VAG"_s;
    case 9:  return u"PS HEVAG"_s;
    case 10: return u"XMA"_s;
    case 11: return u"MPEG"_s;
    case 12: return u"CELT"_s;
    case 13: return u"ATRAC9"_s;
    case 14: return u"xWMA"_s;
    case 15: return u"Vorbis"_s;
    case 16: return u"FMOD ADPCM"_s;
    case 17: return u"Opus"_s;
    default: return u"Unknown (%1)"_s.arg(mode);
    }
}

QString defaultSampleName(int index) {
    return u"sample_%1"_s.arg(index + 1, 4, 10, QChar(u'0'));
}

// Fixed size, possibly not null terminated name field.
QString fixedName(const QByteArray& data, qint64 offset, int maxLength) {
    const char* start = data.constData() + offset;
    const auto* end = static_cast<const char*>(std::memchr(start, '\0', static_cast<size_t>(maxLength)));
    return QString::fromLatin1(start, end ? static_cast<qsizetype>(end - start) : maxLength).trimmed();
}

} // namespace

bool FsbFile::parse(const QByteArray& data, QString* error) {
    *this = FsbFile();

    if (data.size() < 16) {
        if (error)
            *error = u"The file is too small to be an FMOD sound bank."_s;
        return false;
    }

    if (data.startsWith("RIFF") && data.mid(8, 4) == "FEV ")
        return parseStudioBank(data);

    if (data.startsWith("FSB5"))
        return parseFsb5(data, 0, data.size(), error);
    if (data.startsWith("FSB4"))
        return parseFsb34(data, 0, 4, error);
    if (data.startsWith("FSB3"))
        return parseFsb34(data, 0, 3, error);

    if (error)
        *error = u"This isn't an FMOD sound bank or FSB file (unknown signature). It may be encrypted."_s;
    return false;
}

// ---------------------------------------------------------------------------
// FMOD Studio bank
// ---------------------------------------------------------------------------

// A bank always "loads": master / strings banks just have no samples, encrypted ones get a note.
bool FsbFile::parseStudioBank(const QByteArray& data) {
    container = Container::StudioBank;

    const qint64 riffEnd = std::min<qint64>(data.size(), 8 + static_cast<qint64>(u32(data, 4)));
    BankScan scan;
    walkBankChunks(data, 12, riffEnd, 0, scan);

    if (scan.sndBody < 0) {
        formatName = u"FMOD Studio bank (no audio)"_s;
        note = u"This bank has no audio samples. It only holds events, buses, strings or other project data "
               u"(e.g. a master or strings bank)."_s;
        return true;
    }

    // The FSB5 follows some alignment padding inside the SND chunk.
    const qint64 sndEnd = scan.sndBody + scan.sndSize;
    const qsizetype fsb = data.indexOf("FSB5", scan.sndBody);
    if (fsb < 0 || fsb + kFsb5HeaderSizeV1 > sndEnd) {
        formatName = u"FMOD Studio bank (unreadable audio)"_s;
        note = u"The bank has a sample data chunk, but no FSB5 inside it. The samples are probably encrypted."_s;
        return true;
    }

    QString fsbError;
    if (!parseFsb5(data, fsb, sndEnd, &fsbError)) {
        // Keep the bank info, drop whatever the failed FSB parse filled in.
        samples.clear();
        codecName.clear();
        sampleDataSize = 0;
        fsbVersion = 0;
        fsbOffset = -1;
        fsbSize = 0;

        container = Container::StudioBank;
        formatName = u"FMOD Studio bank (unreadable audio)"_s;
        note = u"The bank's sample data couldn't be read: "_s + fsbError;
        return true;
    }

    container = Container::StudioBank;
    formatName = u"FMOD Studio bank (FSB5 version %1)"_s.arg(fsbVersion);
    return true;
}

void FsbFile::walkBankChunks(const QByteArray& data, qint64 pos, qint64 end, int depth, BankScan& scan) {
    while (pos + 8 <= end) {
        const QByteArray tag = data.mid(pos, 4);
        const quint32 size = u32(data, pos + 4);
        const qint64 body = pos + 8;

        if (body + static_cast<qint64>(size) > end) {
            truncated = true;
            return;
        }

        QString label = QString::fromLatin1(tag).trimmed();

        if (tag == "LIST" && size >= 4) {
            label += u'(' + QString::fromLatin1(data.mid(body, 4)).trimmed() + u')';
            if (depth < kMaxBankDepth)
                walkBankChunks(data, body + 4, body + size, depth + 1, scan);
        } else if (tag == "FMT " && size >= 4 && bankVersion == 0) {
            bankVersion = u32(data, body);
        } else if (tag == "SND " && scan.sndBody < 0) {
            scan.sndBody = body;
            scan.sndSize = size;
        }

        if (!chunks.contains(label))
            chunks.append(label);

        // RIFF pads odd sized chunks to an even size; only skip the pad byte when that's clearly what it is.
        qint64 next = body + size;
        if ((size & 1) && !looksLikeTag(data, next) && looksLikeTag(data, next + 1))
            ++next;
        pos = next;
    }
}

// ---------------------------------------------------------------------------
// FSB5
// ---------------------------------------------------------------------------

bool FsbFile::parseFsb5(const QByteArray& data, qint64 base, qint64 limit, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    limit = std::min<qint64>(limit, data.size());
    if (limit - base < kFsb5HeaderSizeV1)
        return fail(u"The FSB5 header is truncated."_s);

    fsbVersion = u32(data, base + 0x04);
    const quint32 count = u32(data, base + 0x08);
    const quint32 sampleHeaderSize = u32(data, base + 0x0C);
    const quint32 nameTableSize = u32(data, base + 0x10);
    sampleDataSize = u32(data, base + 0x14);
    const quint32 codec = u32(data, base + 0x18);

    // Sanity checks first: nothing that fails these is handed to vgmstream.
    if (fsbVersion > kFsb5MaxVersion)
        return fail(u"Unsupported FSB5 version %1."_s.arg(fsbVersion));
    if (codec > kFsb5MaxCodec)
        return fail(u"The FSB5 header is invalid (unknown codec %1)."_s.arg(codec));
    if (static_cast<quint64>(count) * 8 > sampleHeaderSize)
        return fail(u"The FSB5 header is invalid (%1 samples don't fit into the sample headers)."_s.arg(count));

    const qint64 headerSize = fsbVersion == 0 ? kFsb5HeaderSizeV0 : kFsb5HeaderSizeV1;
    const qint64 sampleHeaders = base + headerSize;
    const qint64 nameTable = sampleHeaders + sampleHeaderSize;
    const qint64 sampleData = nameTable + nameTableSize;
    const qint64 end = sampleData + sampleDataSize;

    if (end > limit)
        return fail(u"The FSB5 data runs past the end of the file."_s);

    container = Container::Fsb5;
    formatName = u"FSB5 (version %1)"_s.arg(fsbVersion);
    codecName = fsb5CodecName(codec);
    fsbOffset = base;
    fsbSize = end - base;

    samples.reserve(static_cast<qsizetype>(count));

    // Sample headers: a 64 bit packed mode, followed by optional extra chunks.
    qint64 pos = sampleHeaders;
    for (quint32 i = 0; i < count; ++i) {
        if (pos + 8 > nameTable)
            return fail(u"FSB5 sample header %1 is truncated."_s.arg(i + 1));

        const quint64 mode = u64(data, pos);
        pos += 8;

        Sample s;
        s.name = defaultSampleName(static_cast<int>(i));
        const quint32 rateIndex = static_cast<quint32>((mode >> 1) & 0x0F);
        s.sampleRate = rateIndex < std::size(kFsb5Rates) ? kFsb5Rates[rateIndex] : 44100;
        s.channels = kFsb5Channels[(mode >> 5) & 0x03];
        s.dataOffset = static_cast<qint64>((mode >> 7) & 0x07FFFFFF) << 5;  // Relative to the sample data.
        s.sampleCount = static_cast<quint32>((mode >> 34) & 0x3FFFFFFF);

        bool hasChunk = (mode & 0x01) != 0;
        while (hasChunk) {
            if (pos + 4 > nameTable)
                return fail(u"FSB5 sample header %1 is truncated."_s.arg(i + 1));

            const quint32 chunk = u32(data, pos);
            pos += 4;

            hasChunk = (chunk & 0x01) != 0;
            const quint32 size = (chunk >> 1) & 0x00FFFFFF;
            const quint32 type = (chunk >> 25) & 0x7F;

            if (pos + size > nameTable)
                return fail(u"FSB5 sample header %1 is truncated."_s.arg(i + 1));

            switch (type) {
            case 0x01: // Channels
                if (size >= 1)
                    s.channels = static_cast<quint8>(data.at(pos));
                break;
            case 0x02: // Frequency
                if (size >= 4)
                    s.sampleRate = u32(data, pos);
                break;
            case 0x03: // Loop
                if (size >= 8) {
                    s.hasLoop = true;
                    s.loopStart = u32(data, pos);
                    s.loopEnd = u32(data, pos + 4);
                }
                break;
            default:
                break;
            }
            pos += size;
        }

        if (s.dataOffset > sampleDataSize)
            return fail(u"FSB5 sample %1 points outside of the sample data."_s.arg(i + 1));

        samples.append(s);
    }

    // Data sizes: from each sample's offset to the next higher offset (or the end of the sample data).
    QList<qint64> offsets;
    offsets.reserve(samples.size() + 1);
    for (const Sample& s : std::as_const(samples))
        offsets.append(s.dataOffset);
    offsets.append(sampleDataSize);
    std::sort(offsets.begin(), offsets.end());

    for (Sample& s : samples) {
        const auto next = std::upper_bound(offsets.cbegin(), offsets.cend(), s.dataOffset);
        const qint64 nextOffset = next != offsets.cend() ? *next : sampleDataSize;
        s.dataSize = static_cast<quint32>(nextOffset - s.dataOffset);
        s.dataOffset += sampleData - base; // Relative to the start of the FSB.
    }

    // Name table: one offset per sample (relative to the table), then null terminated names.
    if (nameTableSize >= static_cast<quint64>(count) * 4) {
        const qint64 tableEnd = nameTable + nameTableSize;
        for (int i = 0; i < samples.size(); ++i) {
            const qint64 start = nameTable + u32(data, nameTable + 4 * static_cast<qint64>(i));
            if (start >= tableEnd)
                continue;

            const auto* begin = data.constData() + start;
            const auto* terminator = static_cast<const char*>(std::memchr(begin, '\0', static_cast<size_t>(tableEnd - start)));
            const qsizetype length = terminator ? static_cast<qsizetype>(terminator - begin)
                                                : static_cast<qsizetype>(tableEnd - start);

            const QString name = QString::fromUtf8(begin, length).trimmed();
            if (!name.isEmpty())
                samples[i].name = name;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// FSB3 / FSB4
// ---------------------------------------------------------------------------

bool FsbFile::parseFsb34(const QByteArray& data, qint64 base, int fsbMajor, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    const qint64 headerSize = fsbMajor == 4 ? kFsb4HeaderSize : kFsb3HeaderSize;
    if (data.size() - base < headerSize)
        return fail(u"The FSB%1 header is truncated."_s.arg(fsbMajor));

    const quint32 count = u32(data, base + 0x04);
    const quint32 sampleHeaderSize = u32(data, base + 0x08);
    sampleDataSize = u32(data, base + 0x0C);
    fsbVersion = u32(data, base + 0x10);
    headerFlags = u32(data, base + 0x14);

    const qint64 sampleHeaders = base + headerSize;
    const qint64 sampleData = sampleHeaders + sampleHeaderSize;
    const qint64 end = sampleData + sampleDataSize;

    if (end > data.size())
        return fail(u"The FSB%1 data runs past the end of the file."_s.arg(fsbMajor));
    if (count > 0 && sampleHeaderSize < kFsb34SampleHeaderMin)
        return fail(u"The FSB%1 header is invalid (no room for the sample headers)."_s.arg(fsbMajor));

    container = fsbMajor == 4 ? Container::Fsb4 : Container::Fsb3;
    formatName = fsbMajor == 4 ? u"FSB4 (FMOD Ex)"_s : u"FSB3 (FMOD 3)"_s;
    fsbOffset = base;
    fsbSize = end - base;

    const bool basicHeaders = (headerFlags & kFsb34BasicHeaders) != 0;
    Sample first;

    qint64 pos = sampleHeaders;
    for (quint32 i = 0; i < count; ++i) {
        Sample s;

        if (i == 0 || !basicHeaders) {
            if (pos + kFsb34SampleHeaderMin > sampleData)
                return fail(u"FSB%1 sample header %2 is truncated."_s.arg(fsbMajor).arg(i + 1));

            const quint16 size = u16(data, pos);
            if (size < kFsb34SampleHeaderMin || pos + size > sampleData)
                return fail(u"FSB%1 sample header %2 is invalid."_s.arg(fsbMajor).arg(i + 1));

            s.name = fixedName(data, pos + 0x02, 30);
            s.sampleCount = u32(data, pos + 0x20);
            s.dataSize = u32(data, pos + 0x24);
            s.loopStart = u32(data, pos + 0x28);
            s.loopEnd = u32(data, pos + 0x2C);
            const quint32 mode = u32(data, pos + 0x30);
            s.sampleRate = u32(data, pos + 0x34);
            s.channels = u16(data, pos + 0x3E);
            s.hasLoop = (mode & kFsb34LoopModes) != 0 && s.loopEnd > s.loopStart;

            first = s;
            pos += size;
        } else {
            // Basic headers: only the lengths, everything else comes from the first sample.
            if (pos + kFsb34BasicHeaderSize > sampleData)
                return fail(u"FSB%1 sample header %2 is truncated."_s.arg(fsbMajor).arg(i + 1));

            s = first;
            s.name.clear();
            s.sampleCount = u32(data, pos);
            s.dataSize = u32(data, pos + 4);
            s.hasLoop = false;
            pos += kFsb34BasicHeaderSize;
        }

        if (s.name.isEmpty())
            s.name = defaultSampleName(static_cast<int>(i));
        samples.append(s);
    }

    return true;
}
