#ifndef RL26_SAVE_H
#define RL26_SAVE_H

#include <QByteArray>
#include <QString>

// Rugby League 26 save files (Saved Games\Rugby League 26\...\*.SAV), as the game writes them (rugbyleague1.exe,
// the save writer at 0x140440390 / 0x140444420):
//
//   16 byte header, four little endian u32s
//      0  magic      0xABCDABCD
//      4  version    format version | 0xC0000000 (0xC0000006 Academy / Profile, 0xC0000001 Levels). Metadata.SAV
//                    files don't have the 0xC0000000 bits: their data follows as it is.
//      8  checksum   crc32(zlib stream before the XOR below) ^ 0xDEADBABA   (Metadata.SAV: crc32 of the data)
//     12  size       payload size (+ 0x09934FA0 on version 6 files)
//   then the payload: a zlib stream whose first 64 bytes are XORed with the 8 byte key 0xBABABABABABABABA.
namespace Rl26Save {

constexpr quint32 kMagic = 0xABCDABCDu;
constexpr quint32 kCompressedFlags = 0xC0000000u;
constexpr quint32 kChecksumXor = 0xDEADBABAu;
constexpr int kHeaderSize = 16;

struct File {
    bool ok = false;
    QString error;
    quint32 version = 0;        // Without the 0xC0000000 bits.
    bool compressed = false;
    bool checksumOk = false;
    quint32 sizeBias = 0;       // The size field minus the payload size (kept when writing back).
    QByteArray data;            // Decoded.
};

quint32 crc32(const QByteArray& data);

// A .SAV file's bytes to its decoded data.
File decode(const QByteArray& file);

// Decoded data back to a .SAV file (compressed, checksum, XOR, header), with the version and size bias of the file
// it came from.
QByteArray encode(const QByteArray& data, quint32 version, quint32 sizeBias);

} // namespace Rl26Save

#endif // RL26_SAVE_H
