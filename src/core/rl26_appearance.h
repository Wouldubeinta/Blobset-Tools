#ifndef RL26_APPEARANCE_H
#define RL26_APPEARANCE_H

#include <QByteArray>

// A Rugby League 26 player's appearance (paCharacterVisualConfig, 251 bytes) as the Academy save keeps it: the root
// object's bytes only. The database (defaultdata_players) keeps it as a whole paSerial blob; blobHeader() is that
// blob's type header (from the game's database), so header + the save's 251 bytes parse with PaSerial::Blob.
namespace Rl26Appearance {

constexpr int kObjectSize = 251;
const QByteArray& blobHeader();

} // namespace Rl26Appearance

#endif // RL26_APPEARANCE_H
