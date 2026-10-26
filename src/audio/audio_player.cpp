#include "audio_player.h"

#include "audio_decoder.h"
#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "core_defs.h"
#include "utilities.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QtConcurrent>
#include <QtEndian>

#include <cstring>
#include <stdexcept>

namespace {

// Runs on a worker thread: decodes WEM bytes entirely in memory.
WemDecodeResult decodeWemBytes(const QByteArray& raw, const QString& name) {
    WemDecodeResult r;

    if (!raw.startsWith("RIFF") && !raw.startsWith("RIFX")) {
        r.error = QStringLiteral("WEM preview: Extracted stream mismatch (missing uncompressed RIFF descriptors).");
        return r;
    }

    // vgmstream picks its parser from the extension, so the in-memory stream must be named "*.wem".
    QString virtualName = QFileInfo(name).fileName();
    if (virtualName.isEmpty())
        virtualName = QStringLiteral("preview");
    if (!virtualName.endsWith(QStringLiteral(".wem"), Qt::CaseInsensitive))
        virtualName += QStringLiteral(".wem");

    // Only the first subsong is played, so don't decode the rest.
    const QVector<DecodedTrack> tracks = AudioProcessor::convertAudioBufferToWav(raw, virtualName, 1);
    if (tracks.isEmpty() || tracks.first().wavData.size() <= static_cast<qsizetype>(sizeof(WAVHeader))) {
        r.error = QStringLiteral("WEM preview: Underlying vgmstream dynamic link libraries failed decoding sequence.");
        return r;
    }

    const DecodedTrack& track = tracks.first();
    r.pcm        = track.wavData.mid(sizeof(WAVHeader)); // Strip the WAV header, QAudioSink wants raw PCM.
    r.sampleRate = track.metadata.sampleRate;
    r.channels   = track.metadata.channels;
    return r;
}

WemDecodeResult decodeWemEntry(const BlobsetEntry& entry, const QString& gameLocation, int version, int platform) {
    QByteArray data;
    try {
        data = BlobsetAssetReader::read(entry, gameLocation, version, platform);
    } catch (const std::exception& ex) {
        WemDecodeResult r;
        r.error = QStringLiteral("WEM preview: %1").arg(QString::fromUtf8(ex.what()));
        return r;
    }
    return decodeWemBytes(data, entry.fileHashName);
}

WemDecodeResult decodeAudioSubsong(const QByteArray& data, const QString& virtualName, int subsong) {
    WemDecodeResult r;

    DecodedTrack track;
    if (!AudioProcessor::decodeSubsongToWav(data, virtualName, subsong, track)) {
        r.error = QStringLiteral("Audio preview: vgmstream couldn't decode subsong %1 of %2.")
                      .arg(subsong).arg(QFileInfo(virtualName).fileName());
        return r;
    }

    r.pcm        = track.wavData.mid(sizeof(WAVHeader)); // QAudioSink wants raw PCM.
    r.sampleRate = track.metadata.sampleRate;
    r.channels   = track.metadata.channels;
    return r;
}

// Runs on a worker thread: plays a RIFF (little endian) / RIFX (big endian) WAV.
// Plain PCM (8 / 16 / 24 / 32 bit integer) is converted here; any other codec (MS / IMA ADPCM, XMA, ...) is
// handed to vgmstream.
WemDecodeResult decodeWavBytes(const QByteArray& raw, const QString& name) {
    WemDecodeResult r;

    if (raw.size() < 12 || (!raw.startsWith("RIFF") && !raw.startsWith("RIFX")) || raw.mid(8, 4) != "WAVE") {
        r.error = QStringLiteral("WAV preview: %1 isn't a RIFF / WAVE stream.").arg(name);
        return r;
    }

    const bool bigEndian = raw.startsWith("RIFX");
    const uchar* p = reinterpret_cast<const uchar*>(raw.constData());
    const auto u16 = [&](qsizetype at) { return bigEndian ? qFromBigEndian<quint16>(p + at) : qFromLittleEndian<quint16>(p + at); };
    const auto u32 = [&](qsizetype at) { return bigEndian ? qFromBigEndian<quint32>(p + at) : qFromLittleEndian<quint32>(p + at); };

    quint16 formatTag = 0, channels = 0, bits = 0;
    quint32 sampleRate = 0;
    qsizetype dataOffset = -1, dataSize = 0;
    for (qsizetype pos = 12; pos + 8 <= raw.size();) {
        const quint32 size = u32(pos + 4);
        if (std::memcmp(p + pos, "fmt ", 4) == 0 && size >= 16 && pos + 24 <= raw.size()) {
            formatTag = u16(pos + 8);
            channels = u16(pos + 10);
            sampleRate = u32(pos + 12);
            bits = u16(pos + 22);
            if (formatTag == 0xFFFE && size >= 40 && pos + 34 <= raw.size())
                formatTag = u16(pos + 32);   // WAVE_FORMAT_EXTENSIBLE: the sub format GUID starts with the tag.
        } else if (std::memcmp(p + pos, "data", 4) == 0) {
            dataOffset = pos + 8;
            dataSize = std::min<qsizetype>(size, raw.size() - dataOffset);   // Truncated files: play what's there.
        }
        pos += 8 + static_cast<qsizetype>(size) + (size & 1);
    }

    const bool plainPcm = formatTag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    if (plainPcm && dataOffset >= 0 && channels > 0 && sampleRate > 0) {
        const int bytesPerSample = bits / 8;
        const qsizetype samples = dataSize / bytesPerSample;
        const uchar* src = p + dataOffset;

        QByteArray pcm(samples * 2, Qt::Uninitialized);
        auto* dst = reinterpret_cast<qint16*>(pcm.data());
        for (qsizetype i = 0; i < samples; ++i, src += bytesPerSample) {
            qint16 value = 0;
            switch (bytesPerSample) {
            case 1: value = static_cast<qint16>((static_cast<int>(src[0]) - 128) << 8); break;   // Unsigned 8 bit.
            case 2: value = bigEndian ? qFromBigEndian<qint16>(src) : qFromLittleEndian<qint16>(src); break;
            case 3: value = static_cast<qint16>(bigEndian ? (src[0] << 8) | src[1] : (src[2] << 8) | src[1]); break;
            case 4: value = static_cast<qint16>((bigEndian ? qFromBigEndian<qint32>(src) : qFromLittleEndian<qint32>(src)) >> 16); break;
            }
            dst[i] = value;
        }

        r.pcm = pcm;
        r.sampleRate = static_cast<int>(sampleRate);
        r.channels = channels;
        return r;
    }

    // Compressed WAV: vgmstream picks its parser from the extension, so the stream is named "*.wav".
    QString virtualName = QFileInfo(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    if (virtualName.isEmpty())
        virtualName = QStringLiteral("preview");
    if (!virtualName.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive))
        virtualName += QStringLiteral(".wav");

    DecodedTrack track;
    if (!AudioProcessor::decodeSubsongToWav(raw, virtualName, 0, track)) {
        r.error = QStringLiteral("WAV preview: Can't decode %1 (format 0x%2).")
                      .arg(name).arg(static_cast<uint>(formatTag), 4, 16, QLatin1Char('0'));
        return r;
    }

    r.pcm        = track.wavData.mid(sizeof(WAVHeader)); // QAudioSink wants raw PCM.
    r.sampleRate = track.metadata.sampleRate;
    r.channels   = track.metadata.channels;
    return r;
}

// Runs on a worker thread: reads the asset out of the blobset (any version), then decodes it.
WemDecodeResult decodeWavEntry(const BlobsetEntry& entry, const QString& gameLocation, int version, int platform,
                               const QString& name) {
    QByteArray data;
    try {
        data = BlobsetAssetReader::read(entry, gameLocation, version, platform);
    } catch (const std::exception& ex) {
        WemDecodeResult r;
        r.error = QStringLiteral("WAV preview: %1").arg(QString::fromUtf8(ex.what()));
        return r;
    }
    return decodeWavBytes(data, name);
}

} // namespace

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {}

AudioPlayer::~AudioPlayer() {
    stop();
}

void AudioPlayer::setPlaying(bool playing) {
    if (m_playing == playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void AudioPlayer::releaseSink() {
    if (m_sink) {
        m_sink->disconnect(this);
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }

    if (m_buffer.isOpen())
        m_buffer.close();
    m_buffer.setBuffer(nullptr);

    setPlaying(false);
}

void AudioPlayer::stop() {
    ++m_generation; // Invalidates any decode that is still running
    releaseSink();
}

void AudioPlayer::playWemByHash(const QString& hexHash) {
    stop();

    AppOrchestrator* orch = AppOrchestrator::instance();
    if (!orch) {
        emit message(QStringLiteral("WEM preview: The application orchestrator is unavailable."));
        return;
    }

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        emit message(QStringLiteral("WEM preview: Target data hash mapping lookup failure: 0x%1").arg(hexHash.toUpper()));
        return;
    }

    // Any blobset version (Cricket 19's V3 has WEMs too): BlobsetAssetReader finds and decompresses the asset.
    startDecode(QtConcurrent::run(decodeWemEntry, entry, orch->gameLocation(), orch->blobsetVersion(), orch->platform()));
}

void AudioPlayer::playWavByHash(const QString& hexHash, const QString& name) {
    stop();

    AppOrchestrator* orch = AppOrchestrator::instance();
    if (!orch) {
        emit message(QStringLiteral("WAV preview: The application orchestrator is unavailable."));
        return;
    }

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        emit message(QStringLiteral("WAV preview: Target data hash mapping lookup failure: 0x%1").arg(hexHash.toUpper()));
        return;
    }

    const QString displayName = name.isEmpty() ? QStringLiteral("0x") + hexHash.toUpper() : name;
    startDecode(QtConcurrent::run(decodeWavEntry, entry, orch->gameLocation(), orch->blobsetVersion(),
                                  orch->platform(), displayName));
}

void AudioPlayer::playWemData(const QByteArray& data, const QString& name) {
    stop();

    if (data.isEmpty()) {
        emit message(QStringLiteral("WEM preview: There is no audio data to play."));
        return;
    }

    startDecode(QtConcurrent::run(decodeWemBytes, data, name));
}

void AudioPlayer::playAudioData(const QByteArray& data, const QString& virtualName, int subsong) {
    stop();

    if (data.isEmpty() || virtualName.isEmpty()) {
        emit message(QStringLiteral("FMOD preview: There is no audio data to play."));
        return;
    }

    startDecode(QtConcurrent::run(decodeAudioSubsong, data, virtualName, subsong));
}

void AudioPlayer::startDecode(const QFuture<WemDecodeResult>& future) {
    const quint64 generation = m_generation;

    auto* watcher = new QFutureWatcher<WemDecodeResult>(this);
    connect(watcher, &QFutureWatcher<WemDecodeResult>::finished, this, [this, watcher, generation]() {
        const WemDecodeResult result = watcher->result();
        watcher->deleteLater();

        // The user moved on (another file, stop, view change) while this was decoding.
        if (generation != m_generation)
            return;

        if (!result.error.isEmpty()) {
            emit message(result.error);
            return;
        }

        startPlayback(result.pcm, result.sampleRate, result.channels);
    });

    watcher->setFuture(future);
}

void AudioPlayer::startPlayback(const QByteArray& pcm, int sampleRate, int channels) {
    if (pcm.isEmpty()) {
        emit message(QStringLiteral("WEM preview error: Attempted to process empty PCM stream block bounds."));
        return;
    }

    QAudioFormat fmt;
    fmt.setSampleRate(sampleRate);
    fmt.setChannelCount(channels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (defaultDevice.isNull() || !defaultDevice.isFormatSupported(fmt)) {
        emit message(QStringLiteral("WEM preview: Local hardware subsystem architecture lacks support for %1 Hz / %2 ch.")
                         .arg(sampleRate).arg(channels));
        return;
    }

    m_pcm = pcm;
    m_buffer.setBuffer(&m_pcm);

    if (!m_buffer.open(QIODevice::ReadOnly)) {
        emit message(QStringLiteral("WEM preview error: Critical failure opening internal linear tracking buffer handle."));
        return;
    }

    m_sink = new QAudioSink(defaultDevice, fmt, this);
    connect(m_sink, &QAudioSink::stateChanged, this, &AudioPlayer::handleStateChanged);

    m_sink->start(&m_buffer);
    setPlaying(true);
}

void AudioPlayer::handleStateChanged(QAudio::State state) {
    if (!m_sink)
        return;

    switch (state) {
    case QAudio::IdleState:
        if (m_buffer.isOpen() && m_buffer.atEnd())
            stop(); // Reached the end of the data
        break;

    case QAudio::StoppedState:
        if (m_sink->error() != QAudio::NoError) {
            emit message(QStringLiteral("WEM playback engine encountered hardware error code context: %1")
                             .arg(static_cast<int>(m_sink->error())));
            stop();
        }
        break;

    default:
        break;
    }
}
