#ifndef RL26_CHARACTER_H
#define RL26_CHARACTER_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

/// Rugby League 26's character builder, from the game's own scripts (design/characters: assetmapitems.lua,
/// assetmap.lua, blendshapes.lua; the data is in rl26_character_data.inc):
///
///  - which models make up a player (head, body, kit, hair pieces, beard parts, headwear), from the appearance
///    options (paCharacterVisualConfig). A port of assetmap.lua's modelSlots[...].filename and
///    getModelFileNameByItem; checked against the game's Lua on 42,000 random lookups and every database player.
///  - the blend shape weights (face sliders, body shapes) the options give: blendshapes.lua's list, each weight the
///    value of an option - a stored slider (mapped through its range), or a derived one (clampScale /
///    optionMatchesName / a few special cases).
///
/// Option values are what the appearance blob stores: guids as they are, 8 bit sliders 0 - 255.
namespace Rl26Character {

using Options = QHash<QString, double>;

/// The model slots, in drawing order.
const QStringList& slotNames();

/// Slot -> model asset path without its type ("characters/heads/models/x/x"; + ".modelcomponent" /
/// ".blendshapes" are the files), for each slot that has a model. `previewKit`: when the player's own kit (the
/// team's, in the game) has no model for their sex, the first one that does is used.
QList<QPair<QString, QString>> models(const Options& options, bool previewKit = true);

/// Blend shape name -> weight, the non-zero ones.
QHash<QString, float> shapeWeights(const Options& options);

/// An option's description: "slider" (with its range), "guid", ... Empty when the game has no such option.
QString optionType(const QString& option);
bool isSlider(const QString& option);
int sliderMax(const QString& option);   // 255, or 65535 for 2 byte ones.

/// The item list a guid option picks from ("hairTop" -> the hair styles), empty when there isn't one.
struct Choice {
    int guid = 0;
    QString name;
};
QList<Choice> choices(const QString& option, int sex);

/// The beard / hair presets ("beard", "hair"): the game's appearance screen sets their parts when one is picked
/// (moustache, chinhair, sideburns / hairTop, hairSide, hairSide2, hairTexture_cap). Option -> guid for preset
/// `guid` of `option`; empty for other options.
QList<QPair<QString, int>> presetOptions(const QString& option, int guid);

/// A readable name for an item ("caesar_curly01" -> "Caesar Curly 01").
QString prettyName(const QString& name);

/// The options that decide which models are used (changing one reloads models; the others only reshape).
bool affectsModels(const QString& option);

/// An option's value as the game's scripts see it (getOption): sliders 0 - 1 through their range, derived ones
/// computed, a default when the player hasn't got it.
double optionValue(const QString& option, const Options& options);

/// True when the option only reshapes (a blend shape slider): no model or texture depends on it.
bool onlyShapes(const QString& option);

// ---- Textures --------------------------------------------------------------------------------------------------
//
// The game builds the head, eyes and body colour textures at run time from layers (assetmap.lua's
// headColourLayers, tier1_headColourLayers, eyesColourLayers, bodyColourLayers, beardCapColourLayers): a base
// skin texture, then redness, highlights, lip tint, freckles, tattoos, eyebrows, strapping ... each placed in a
// rectangle of the target (characters/player/global.texsrc) and blended ("creator_copy_rgb", "creator_overlay",
// ...). Hair, beard and eyelash textures are single textures, coloured by the hair colour swatches.

struct Rgb {
    float r = 0, g = 0, b = 0;
};

struct TextureLayer {
    QString path;            // Texture asset path without size level / type ("characters/player/textures/base/x_col").
    QString blend;           // The item's blend mode ("creator_copy_rgb", "creator_overlay", "creator_hair", ...).
    float intensity = 1.0f;  // The layer's amount option (redness, highlights, lip tint), else 1.
    bool tinted = false;
    Rgb tint;                // creator_hair: the body hair colour.
    QList<Rgb> tints;        // creator_fourtint (boots): the four colours the texture's masks pick.
};

struct MaterialTexture {
    enum class Kind { None, Hidden, Single, Layers };
    Kind kind = Kind::None;
    QString style;           // "skin", "eyes", "hair", "cap", "lash".
    QString path;            // Single: the texture.
    QList<TextureLayer> layers;
    bool coloured = false;   // Hair-like: tinted with colour1 / colour2.
    Rgb colour1, colour2;
    // The surface detail the game's materials use: their normal map ("_nrm") and "mat" map (R metalness,
    // G gloss, B occlusion) layers, built like the colour. Parts without them have a fixed gloss.
    QList<TextureLayer> normalLayers;
    QList<TextureLayer> glossLayers;
    float gloss = 0.4f;
    // How the "mat" map's channels are read: RL26 (R metalness, G gloss, B occlusion), RLL4's cloth (kits, boots,
    // headgear: G is the cloth's roughness) or RLL4's skin (G gloss, B the pores / cavities, A occlusion).
    enum class MatLayout { Rl26, Rll4Cloth, Rll4Skin };
    MatLayout matLayout = MatLayout::Rl26;
    // RLL4's hair and beards: the texture's own light and dark strands shade the colour (the cap included), and very
    // dark colours are lifted a little so the strands still read.
    bool strandShading = false;
};

/// What a part's material ("head_mat", "hairMaterial", "beardMaterial", ...) of `slot` is drawn with.
MaterialTexture materialTexture(const QString& slot, const QString& material, const Options& options);

} // namespace Rl26Character

#endif // RL26_CHARACTER_H
