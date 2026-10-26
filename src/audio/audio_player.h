#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <QAudio>
#include <QBuffer>
#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QString>

class QAudioSink;

struct WemDecodeResult {
    QByteArray pcm;
    int sampleRate = 0;
    int channels = 0;
    QString error;
};

class AudioPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    bool isPlaying() const { return m_playing; }

    // Stops any active stream, then decodes and plays the WEM asset registered under that hash.
    Q_INVOKABLE void playWemByHash(const QString& hexHash);

    // Stops any active stream, then plays the WAV (any RIFF / RIFX audio) asset registered under that hash.
    // Works for every blobset version. name: the workspace path (only used for messages and vgmstream).
    Q_INVOKABLE void playWavByHash(const QString& hexHash, const QString& name = QString());

    // Plays WEM bytes that are already in memory (e.g. a WEM embedded in a sound bank).
    Q_INVOKABLE void playWemData(const QByteArray& data, const QString& name);
	
	// Plays subsong `subsong` (1-based, 0 = default) of audio already in memory (FSB, bank, ...).
    // virtualName must carry the real extension (e.g. "x.fsb"): vgmstream picks its parser from it.
    Q_INVOKABLE void playAudioData(const QByteArray& data, const QString& virtualName, int subsong = 0);

    Q_INVOKABLE void stop();

signals:
    void message(const QString& text); // Wired to the workspace console log
    void playingChanged();

private:
    void startDecode(const QFuture<WemDecodeResult>& future);
    void startPlayback(const QByteArray& pcm, int sampleRate, int channels);
    void releaseSink();
    void setPlaying(bool playing);
    void handleStateChanged(QAudio::State state);

    QAudioSink* m_sink       = nullptr;
    QByteArray  m_pcm;
    QBuffer     m_buffer;
    quint64     m_generation = 0; // Incremented on every change; isolates stale async decodes
    bool        m_playing    = false;
};

#endif // AUDIO_PLAYER_H
