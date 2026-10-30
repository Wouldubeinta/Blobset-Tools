#ifndef BNK_FILE_H
#define BNK_FILE_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

/// Minimal Wwise SoundBank (.bnk) reader.
///
/// A bank is a list of chunks: 4 character tag, u32 size, data. The ones read here:
///   BKHD  bank header      - version, bank id
///   DIDX  media index      - 12 bytes per embedded WEM: id, offset (relative to DATA), size
///   DATA  media data       - the embedded WEM files back to back
///   HIRC  hierarchy        - u32 count, then per object: u8 type, u32 size, u32 id, ...
///   STID  string table     - bank names
class BnkFile {
public:
    struct Media {
        quint32 id = 0;
        quint32 offset = 0; // Absolute offset inside the bank file.
        quint32 size = 0;
    };

    bool parse(const QByteArray& data, QString* error = nullptr);

    /// The bank `data` (parsed into `bank`) with embedded WEMs replaced (media index -> new WEM bytes):
    ///   DATA  rebuilt, keeping the bank's alignment (16 bytes in Wwise banks);
    ///   DIDX  new offsets / sizes (same order and ids);
    ///   HIRC  every source of a replaced media ([plugin u32][stream type u8][media id u32][media size u32], as in
    ///         Sound and Music Track objects) gets the new size, and the codec of the new WEM (PCM 0x00010001 /
    ///         Vorbis 0x00040001) when it changes. hircPatched counts them.
    /// Every other chunk is copied as is. Empty + error on failure.
    static QByteArray rebuild(const QByteArray& data, const BnkFile& bank, const QHash<int, QByteArray>& replacements,
                              int* hircPatched = nullptr, QString* error = nullptr);

    bool isBigEndian = false;
    bool truncated = false;     // The file ended in the middle of a chunk.
    quint32 version = 0;
    quint32 bankId = 0;
    QString bankName;           // From STID, when the bank has one.
    quint32 dataOffset = 0;     // DATA payload (absolute).
    quint32 dataSize = 0;
    quint32 hircObjects = 0;
    quint32 hircSounds = 0;
    quint32 hircEvents = 0;
    QStringList chunks;         // Chunk tags in file order.
    QList<Media> media;

private:
    quint32 readU32(const QByteArray& data, qint64 offset) const;
    void readHirc(const QByteArray& data, qint64 body, quint32 size);
    void readStid(const QByteArray& data, qint64 body, quint32 size);
};

#endif // BNK_FILE_H