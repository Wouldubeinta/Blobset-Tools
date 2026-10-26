#include "audio_decoder.h"

#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>

// vgmstream internal tracking structs are written in pure C
extern "C" {
#include <include/vgmstream/vgmstream.h>
#include <include/vgmstream/libvgmstream.h>
#include <include/vgmstream/streamfile.h>
#include <include/vgmstream/vgmstream_init.h>

#include <include/vgmstream/base/render.h>
#include <include/vgmstream/base/sbuf.h>
#include <include/vgmstream/base/info.h>
#include <include/vgmstream/meta/meta.h>
}

namespace {

// ==============================================================================================================
// In-memory libstreamfile_t
// ==============================================================================================================

struct MemoryStream {
    QByteArray data;  // Implicitly shared: every re-open shares the same bytes, nothing is copied.
    QByteArray name;  // vgmstream picks the parser from the extension of this name.
};

libstreamfile_t* openMemoryStream(const QByteArray& data, const QByteArray& name);

MemoryStream* memoryStream(void* userData) {
    return static_cast<MemoryStream*>(userData);
}

// Positional read: vgmstream passes the absolute offset on every call. Returns the bytes copied.
int memRead(void* userData, uint8_t* dst, int64_t offset, int length) {
    const MemoryStream* ms = memoryStream(userData);
    if (!dst || length <= 0 || offset < 0 || offset >= ms->data.size())
        return 0;

    const qint64 count = std::min<qint64>(length, ms->data.size() - offset);
    std::memcpy(dst, ms->data.constData() + offset, static_cast<size_t>(count));
    return static_cast<int>(count);
}

int64_t memGetSize(void* userData) {
    return memoryStream(userData)->data.size();
}

const char* memGetName(void* userData) {
    return memoryStream(userData)->name.constData();
}

// vgmstream re-opens the current file by name (and may probe for companion files like .txth).
// Only the buffer itself exists in memory, everything else is reported as missing.
libstreamfile_t* memOpen(void* userData, const char* filename) {
    const MemoryStream* ms = memoryStream(userData);
    if (!filename)
        return nullptr;

    const QString requested = QFileInfo(QString::fromUtf8(filename)).fileName();
    const QString own = QFileInfo(QString::fromUtf8(ms->name)).fileName();
    if (requested.compare(own, Qt::CaseInsensitive) != 0)
        return nullptr;

    return openMemoryStream(ms->data, ms->name);
}

void memClose(libstreamfile_t* libsf) {
    if (!libsf)
        return;
    delete memoryStream(libsf->user_data);
    delete libsf;
}

libstreamfile_t* openMemoryStream(const QByteArray& data, const QByteArray& name) {
    auto* libsf = new libstreamfile_t{};
    libsf->user_data = new MemoryStream{data, name};
    libsf->read = memRead;
    libsf->get_size = memGetSize;
    libsf->get_name = memGetName;
    libsf->open = memOpen;
    libsf->close = memClose;
    return libsf;
}

// ==============================================================================================================
// Decoding
// ==============================================================================================================

using StreamFactory = std::function<libstreamfile_t*()>;

// Maps the internal vgmstream codec names to friendlier labels; anything else is shown as vgmstream names it.
QString codecLabel(const char* codecName) {
    const QString name = QString::fromUtf8(codecName ? codecName : "");

    if (name == u"Custom Vorbis")            return QStringLiteral("Vorbis");
    if (name == u"16-bit Little Endian PCM") return QStringLiteral("Uncompressed PCM");
    if (name == u"Platinum 4-bit ADPCM")     return QStringLiteral("IMA ADPCM");
    if (name == u"libopus Opus")             return QStringLiteral("Opus");
    return name.isEmpty() ? QStringLiteral("Unknown") : name;
}

libvgmstream_config_t pcm16Config() {
    libvgmstream_config_t config;
    std::memset(&config, 0, sizeof(config));
    config.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
    return config;
}

// libvgmstream_create() re-opens the data through libsf->open() and owns that copy;
// the handle passed in stays ours, so it is closed right away.
libvgmstream_t* openSubsong(const StreamFactory& openStream, int subsong, libvgmstream_config_t* config) {
    libstreamfile_t* sf = openStream();
    if (!sf)
        return nullptr;

    libvgmstream_t* lib = libvgmstream_create(sf, subsong, config);
    libstreamfile_close(sf);
    return lib;
}

Metadata readMetadata(libvgmstream_t* lib, const QString& codec) {
    Metadata meta;
    meta.codecName = codec;
    meta.channels = lib->format->channels;
    meta.sampleRate = lib->format->sample_rate;
    meta.totalSamples = static_cast<qint32>(lib->format->stream_samples);
    meta.hasLoopPoints = lib->format->loop_flag != 0;
    meta.loopStartSample = meta.hasLoopPoints ? static_cast<qint32>(lib->format->loop_start) : 0;
    meta.loopEndSample = meta.hasLoopPoints ? static_cast<qint32>(lib->format->loop_end) : 0;
    return meta;
}

DecodedTrack renderTrack(libvgmstream_t* lib, const QString& codec) {
    DecodedTrack track;
    track.metadata = readMetadata(lib, codec);
    const Metadata& meta = track.metadata;

    const qint64 bytesPerFrame = static_cast<qint64>(meta.channels) * static_cast<qint64>(sizeof(int16_t));

    WAVHeader header;
    header.numChannels = static_cast<quint16>(meta.channels);
    header.sampleRate = static_cast<quint32>(meta.sampleRate);
    header.byteRate = static_cast<quint32>(meta.sampleRate * bytesPerFrame);
    header.blockAlign = static_cast<quint16>(bytesPerFrame);

    track.wavData.reserve(static_cast<qsizetype>(sizeof(WAVHeader) + qMax<qint64>(0, meta.totalSamples) * bytesPerFrame));
    track.wavData.append(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));

    while (!lib->decoder->done) {
        if (libvgmstream_render(lib) < 0)
            break;

        qint64 bytes = lib->decoder->buf_bytes;
        if (bytes <= 0)
            bytes = static_cast<qint64>(lib->decoder->buf_samples) * bytesPerFrame;
        if (bytes <= 0)
            break;

        track.wavData.append(static_cast<const char*>(lib->decoder->buf), static_cast<qsizetype>(bytes));
    }

    // Size the header from what was actually rendered (loops can make it longer than stream_samples).
    const auto pcmBytes = static_cast<quint32>(track.wavData.size() - static_cast<qsizetype>(sizeof(WAVHeader)));
    char* raw = track.wavData.data();
    qToLittleEndian<quint32>(36 + pcmBytes, raw + offsetof(WAVHeader, fileSize));
    qToLittleEndian<quint32>(pcmBytes, raw + offsetof(WAVHeader, dataSize));

    return track;
}

QVector<DecodedTrack> decodeTracks(const StreamFactory& openStream, int maxTracks) {
    QVector<DecodedTrack> tracks;
    if (maxTracks == 0)
        return tracks;

    libvgmstream_config_t config = pcm16Config();

    // Subsong 0 = the default (first) subsong; it also reports how many subsongs there are.
    libvgmstream_t* first = openSubsong(openStream, 0, &config);
    if (!first)
        return tracks;

    const QString codec = codecLabel(first->format->codec_name);
    int subsongCount = std::max(1, static_cast<int>(first->format->subsong_count));
    if (maxTracks > 0)
        subsongCount = std::min(subsongCount, maxTracks);

    tracks.reserve(subsongCount);
    tracks.append(renderTrack(first, codec));
    libvgmstream_free(first);

    for (int subsong = 2; subsong <= subsongCount; ++subsong) {
        libvgmstream_t* lib = openSubsong(openStream, subsong, &config);
        if (!lib)
            continue;

        tracks.append(renderTrack(lib, codec));
        libvgmstream_free(lib);
    }

    return tracks;
}

// Opens one subsong of in-memory audio (1-based, 0 = the default one).
libvgmstream_t* openMemorySubsong(const QByteArray& data, const QByteArray& name, int subsong,
                                  libvgmstream_config_t* config) {
    return openSubsong([&data, &name]() { return openMemoryStream(data, name); }, subsong, config);
}

} // namespace

// ==============================================================================================================
// AudioProcessor
// ==============================================================================================================

QVector<DecodedTrack> AudioProcessor::convertAudioBufferToWav(const QByteArray& data, const QString& virtualName,
                                                              int maxTracks) {
    if (data.isEmpty() || virtualName.isEmpty())
        return {};

    const QByteArray name = virtualName.toUtf8();
    return decodeTracks([&data, &name]() { return openMemoryStream(data, name); }, maxTracks);
}

bool AudioProcessor::probeAudioBuffer(const QByteArray& data, const QString& virtualName, Metadata& out, int subsong) {
    if (data.isEmpty() || virtualName.isEmpty() || subsong < 0)
        return false;

    const QByteArray name = virtualName.toUtf8();
    libvgmstream_config_t config = pcm16Config();

    libvgmstream_t* lib = openMemorySubsong(data, name, subsong, &config);
    if (!lib)
        return false;

    out = readMetadata(lib, codecLabel(lib->format->codec_name));
    libvgmstream_free(lib);
    return true;
}

bool AudioProcessor::decodeSubsongToWav(const QByteArray& data, const QString& virtualName, int subsong,
                                        DecodedTrack& out) {
    if (data.isEmpty() || virtualName.isEmpty() || subsong < 0)
        return false;

    const QByteArray name = virtualName.toUtf8();
    libvgmstream_config_t config = pcm16Config();

    libvgmstream_t* lib = openMemorySubsong(data, name, subsong, &config);
    if (!lib)
        return false;

    out = renderTrack(lib, codecLabel(lib->format->codec_name));
    libvgmstream_free(lib);
    return out.wavData.size() > static_cast<qsizetype>(sizeof(WAVHeader));
}
