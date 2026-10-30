#ifndef WEM_FILE_H
#define WEM_FILE_H

#include <QByteArray>
#include <QList>
#include <QString>

/// Wwise .wem helpers for Replace File (MainForm, BNK viewer).
///
/// A WEM is a RIFF / WAVE file. The V4 games' WEMs carry Wwise's own chunks next to "fmt " and "data", e.g.
///     fmt  (0x42)   Vorbis (0xFFFF) with Wwise's extra fields
///     hash (0x10)   16 bytes Wwise uses to identify the source
///     data
/// A replacement keeps the original's id (its file name / the bank's media id) and those extra chunks.
namespace WemFile {

struct Chunk {
    QByteArray tag;     // 4 characters.
    qint64 body = 0;    // Offset of the chunk's data in the file.
    quint32 size = 0;
};

/// The chunks of a little endian RIFF / WAVE file (WEM or WAV). Empty + error when it isn't one.
QList<Chunk> readChunks(const QByteArray& riff, QString* error = nullptr);

/// The format tag of a WEM / WAV ("fmt " +0x00): 0xFFFF Vorbis, 0xFFFE / 0x0001 PCM, ... 0 when unknown.
quint16 formatTag(const QByteArray& riff);

/// A WAV as a Wwise PCM WEM (16 bit, WAVE_FORMAT_EXTENSIBLE 0xFFFE, the layout Wwise writes), with the original
/// WEM's extra chunks (hash, ...) copied in. Chunks tied to the old sound's sample positions (smpl, cue, LIST,
/// vorb, seek) are left out. 8 / 16 / 24 / 32 bit and float WAVs are converted to 16 bit.
/// Empty + error when the WAV can't be used.
QByteArray fromWav(const QByteArray& wav, const QByteArray& originalWem, QString* error = nullptr);

/// A WEM made elsewhere (e.g. with Wwise) with the original's "hash" chunk put in (replaced, or added after
/// "fmt "). Unchanged when the original has no hash chunk. Empty + error when it isn't a WEM.
QByteArray withOriginalHash(const QByteArray& wem, const QByteArray& originalWem, QString* error = nullptr);

/// A short description for the status bar: "PCM 16 bit, 2 ch, 44100 Hz, 1.23 s" / "Vorbis, 2 ch, 44100 Hz".
QString describe(const QByteArray& riff);

} // namespace WemFile

#endif // WEM_FILE_H
