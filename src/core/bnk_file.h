#ifndef BNK_FILE_H
#define BNK_FILE_H

#include <QByteArray>
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