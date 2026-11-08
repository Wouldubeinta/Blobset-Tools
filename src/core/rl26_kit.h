#ifndef RL26_KIT_H
#define RL26_KIT_H

#include <QByteArray>

// A Rugby League 26 team's jerseys (ConvantClub: u16 count, paUniform[20], u16 slot count, u8 slots[7]; 8851 bytes)
// as the Academy save keeps it: the root object's bytes only, at team record + kOffset. The database
// (defaultdata_clubs) keeps it as a whole paSerial blob; blobHeader() is that blob's type header (from the game's
// database), so header + the save's 8851 bytes parse with PaSerial::Blob (like Rl26Appearance).
namespace Rl26Kit {

constexpr int kOffset = 0x176;
constexpr int kObjectSize = 8851;
const QByteArray& blobHeader();

} // namespace Rl26Kit

#endif // RL26_KIT_H
