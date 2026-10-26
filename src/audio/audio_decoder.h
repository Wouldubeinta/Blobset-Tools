#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

struct Metadata {
    bool hasLoopPoints = false;
    qint32 loopStartSample = 0;
    qint32 loopEndSample = 0;
    qint32 totalSamples = 0;
    int channels = 0;
    int sampleRate = 0;
    QString codecName;
};

#pragma pack(push, 1)
struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    quint32 fileSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    quint32 fmtSize = 16;
    quint16 audioFormat = 1; // 1 = Uncompressed Integer PCM
    quint16 numChannels = 0;
    quint32 sampleRate = 0;
    quint32 byteRate = 0;
    quint16 blockAlign = 0;
    quint16 bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    quint32 dataSize = 0;
};
#pragma pack(pop)

struct DecodedTrack {
    QByteArray wavData; // Complete WAV file: WAVHeader followed by 16 bit PCM.
    Metadata metadata;
};

class AudioProcessor {
public:
    // Decodes audio that is already in memory - nothing touches the disk.
    // virtualName must carry the real extension (e.g. "1234.wem"): vgmstream picks its parser from it.
    static QVector<DecodedTrack> convertAudioBufferToWav(const QByteArray& data, const QString& virtualName,
                                                         int maxTracks = -1);

    // Reads only the stream format (codec, channels, rate, length, loops) - nothing is decoded.
    // subsong: 1-based, 0 = the default (first) one.
    static bool probeAudioBuffer(const QByteArray& data, const QString& virtualName, Metadata& out, int subsong = 0);

    // Decodes a single subsong (1-based, 0 = the default one) of in-memory audio to a WAV.
    static bool decodeSubsongToWav(const QByteArray& data, const QString& virtualName, int subsong, DecodedTrack& out);
};

#endif // AUDIO_DECODER_H
