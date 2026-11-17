#ifndef RL26_SKIN_COMPOSITOR_H
#define RL26_SKIN_COMPOSITOR_H

#include "rl26_character.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

/// Builds the colour texture of a Rugby League 26 character part the way the game's character creator does
/// (Rl26Character::materialTexture says which textures and blends): the skin layer groups of the head, eyes and
/// body, and the hair, beard, eyelash and cap textures coloured with the hair colour swatches.
///
/// Layer textures smaller than the target are placed by characters/player/global.texsrc: u32 count, then
/// 32 byte records { u32 FNV-1a 32 of the texture path with its size level ("..._col_2"), u32 ?, u32 x, y, width,
/// height, target width, height } (top-down rectangles; the textures are stored bottom-up). The textures come in
/// size levels "_1" (2048) to "_4" (256); "_2" is used.
namespace Rl26SkinCompositor {

struct Placement {
    int x = 0, y = 0, width = 0, height = 0, fullWidth = 0, fullHeight = 0;
};
using Placements = QHash<quint32, Placement>;
Placements parsePlacements(const QByteArray& data);
/// Rugby League Live 4's atlases (characters/player/head.texsrc, body.texsrc): u32 count, then 92 byte records
/// { char[64] texture file name with its size level ("player_eye_base_01_col_2"), u32 ?, u32 x, y, width, height,
/// target width, height } (top-down, like RL26's). Keyed by the FNV-1a 32 of the file name.
Placements parsePlacementsRll4(const QByteArray& data);

/// The texture paths (with the size level suffix where there is one) that `texture` needs; `exists` says whether a
/// path is in the blobset.
QStringList texturePaths(const Rl26Character::MaterialTexture& texture, const std::function<bool(const QString&)>& exists);

/// How the result is drawn.
enum class Alpha { Opaque, Mask, Blend };
Alpha alphaFor(const Rl26Character::MaterialTexture& texture);

/// The part's colour texture (stored row order, like the game's), null when none of its textures was found.
/// `images`: the decoded textures by lower case path (as texturePaths named them).
QImage compose(const Rl26Character::MaterialTexture& texture, const QHash<QString, QImage>& images,
               const Placements& placements, const std::function<bool(const QString&)>& exists, int maxSize = 1024);
/// The part's normal map (its normalLayers), null when it has none.
QImage composeNormal(const Rl26Character::MaterialTexture& texture, const QHash<QString, QImage>& images,
                     const Placements& placements, const std::function<bool(const QString&)>& exists, int maxSize = 1024);
/// The part's roughness (R, from the "mat" map's gloss) and occlusion (G), from its glossLayers; null without them.
QImage composeRoughness(const Rl26Character::MaterialTexture& texture, const QHash<QString, QImage>& images,
                        const Placements& placements, const std::function<bool(const QString&)>& exists, int maxSize = 1024);

} // namespace Rl26SkinCompositor

#endif // RL26_SKIN_COMPOSITOR_H
