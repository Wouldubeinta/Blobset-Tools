#ifndef RLL4_CHARACTER_H
#define RLL4_CHARACTER_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "rl26_character.h"

/// Rugby League Live 4's character builder, from the game's own scripts (design/characters: assetmapitems.lua,
/// assetmap.lua; the data is in rll4_character_data.inc):
///
///  - which models make up a player (head, body, kit, boots, hair, beard parts, headgear) from the appearance
///    options: a port of assetmap.lua's modelSlots[...].filename and getModelFileNameByGuid (the same scheme as
///    Rugby League 26: "<basePath>/models/<name>_<permutations>/<name>_<permutations>").
///  - the blend shape weights (body shapes, neck, ...) the options give: assetmap.lua's blendShapes list, each weight
///    an option's value (a stored slider, or a derived one: clampScale, optionMatchesName, the body type blends).
///
/// Options are the script's names ("hairTop", "beard", "traps_bodyShape", ...); RLL4's save and career files keep a
/// smaller record of them, saveOption() names the option a save editor field holds. Values are what the files
/// store: guids as they are, 8 bit sliders 0 - 255.
namespace Rll4Character {

using Options = QHash<QString, double>;

/// The model slots, in drawing order.
const QStringList& slotNames();

/// Slot -> model asset path without its type ("characters/hair/models/x/x"; + ".modelcomponent", ".vbo",
/// ".blendshapes" are the files). The kit is the first one for the player's sex (a team's kit isn't in a player).
QList<QPair<QString, QString>> models(const Options& options);

/// Blend shape name -> weight, the non-zero ones.
QHash<QString, float> shapeWeights(const Options& options);

bool isSlider(const QString& option);
int sliderMax(const QString& option);

struct Choice {
    int guid = 0;
    QString name;
};
/// The item list a guid option picks from ("hairTop" -> the hair styles, "head" -> the real players' faces).
QList<Choice> choices(const QString& option, int sex);

/// The options that decide which models are used (changing one reloads models; the others only reshape).
bool affectsModels(const QString& option);
/// An option's value as the game's scripts see it: sliders 0 - 1 through their range, derived ones computed.
double optionValue(const QString& option, const Options& options);

/// The script option a save / career editor field holds ("appearance.hairStyle" -> "hairTop"), empty when none. RGB
/// colour fields name the option without its "_r" / "_g" / "_b".
QString saveOption(const QString& fieldKey);

/// What material `material` of `slot`'s model (`modelPath`) is drawn with, in Rl26SkinCompositor's terms (its
/// textures are composed the same way): the generic head and body mix the game's four skin tone textures by the
/// skin tone options (assetmap.lua's "fourWay" colour layers), real players' heads, kits, boots and headgear use
/// the textures of their model's texture pack (`packTextures`, the names it lists; boots and headgear tinted with
/// their four colours), hair its cap texture from characters/hair/textures/shared, coloured with the hair colour.
Rl26Character::MaterialTexture materialTexture(const QString& slot, const QString& material, const QString& modelPath,
                                               const QStringList& packTextures, const Options& options);

} // namespace Rll4Character

#endif // RLL4_CHARACTER_H
