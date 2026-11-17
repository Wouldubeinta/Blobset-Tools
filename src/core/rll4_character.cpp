#include "rll4_character.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

#include "rll4_character_data.inc"

// One of the game's item maps (AssetMapItems.<name>): how its model paths are made, and its items.
struct ItemMap {
    QJsonObject def;                 // basePath, namePrefix, permutationOrder, permutationMap, backupPermutations
    QList<QJsonObject> items;
    QHash<int, qsizetype> byGuid;    // Last one wins, as the game's lookup tables.
};

struct Data {
    QHash<QString, ItemMap> maps;
    QJsonObject options;
    struct Shape {
        QString option, name;
        QChar sex;   // 'm' / 'f': only for that sex (the body types, ethnicities), else for both.
    };
    QList<Shape> shapes;
};

ItemMap readItemMap(const QJsonObject& def) {
    ItemMap map;
    map.def = def;
    const QJsonArray items = map.def.value(u"items"_s).toArray();
    map.def.remove(u"items"_s);
    for (const QJsonValue& v : items) {
        const QJsonObject item = v.toObject();
        if (item.contains(u"guid"_s))
            map.byGuid.insert(item.value(u"guid"_s).toInt(), map.items.size());
        map.items.append(item);
    }
    return map;
}

const Data& data() {
    static const Data d = [] {
        Data out;
        const QByteArray json = qUncompress(kRll4CharacterData, qsizetype(sizeof(kRll4CharacterData)));
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        const QJsonObject maps = root.value(u"maps"_s).toObject();
        for (auto it = maps.begin(); it != maps.end(); ++it)
            out.maps.insert(it.key(), readItemMap(it.value().toObject()));
        out.options = root.value(u"options"_s).toObject();
        for (const QJsonValue& v : root.value(u"blendShapes"_s).toArray()) {
            const QJsonObject s = v.toObject();
            const QString sex = s.value(u"sex"_s).toString();
            out.shapes.append({s.value(u"option"_s).toString(), s.value(u"name"_s).toString(),
                               sex.isEmpty() ? QChar() : sex.at(0)});
        }
        return out;
    }();
    return d;
}

const ItemMap* itemMap(const QString& name) {
    const auto it = data().maps.constFind(name);
    return it == data().maps.constEnd() ? nullptr : &it.value();
}

const QJsonObject* item(const QString& mapName, std::optional<int> guid) {
    const ItemMap* map = itemMap(mapName);
    if (!map || !guid)
        return nullptr;
    const auto it = map->byGuid.constFind(*guid);
    return it == map->byGuid.constEnd() ? nullptr : &map->items.at(it.value());
}

// Lua truthiness of an item field: present and not false.
bool truthy(const QJsonObject& o, const QString& key) {
    const auto it = o.constFind(key);
    return it != o.constEnd() && !it->isNull() && !(it->isBool() && !it->toBool());
}

// A stored option's value: what the player has, else its default when that's a number.
int opt(const Rll4Character::Options& options, const QString& name) {
    const auto it = options.constFind(name);
    if (it != options.constEnd())
        return int(it.value());
    const QJsonValue d = data().options.value(name).toObject().value(u"default"_s);
    return d.isDouble() ? d.toInt() : 0;
}

// getPermutationByGuid: the permutation's item ("Male", "geara", "long", ...), {} = no suffix, nullopt = no model.
std::optional<QJsonObject> permutationItem(const ItemMap& map, int value, const QJsonObject& it, const QString& key) {
    const QJsonObject pm = map.def.value(u"permutationMap"_s).toObject().value(key).toObject();
    const QString allowed = it.value(u"permutations"_s).toObject().value(key).toString();
    if (it.value(u"permutations"_s).toObject().contains(key) && allowed.isEmpty())
        return QJsonObject();
    const auto valid = [&](std::optional<int> v) {
        if (!v)
            return false;
        for (auto p = pm.begin(); p != pm.end(); ++p)
            if (p.value().toInt() == *v && allowed.contains(p.key()))
                return true;
        return false;
    };
    std::optional<int> v = value;
    bool ok = valid(v);
    if (!ok) {
        QJsonObject backup = it.value(u"backupPermutations"_s).toObject();
        if (backup.isEmpty())
            backup = map.def.value(u"backupPermutations"_s).toObject();
        const QJsonObject b = backup.value(key).toObject();
        if (!b.isEmpty()) {
            const QJsonValue letter = b.value(QString::number(value));
            v = letter.isString() && pm.contains(letter.toString()) ? std::optional<int>(pm.value(letter.toString()).toInt())
                                                                      : std::nullopt;
            ok = valid(v);
        }
    }
    if (!ok) {
        if (truthy(it, u"noModelMissingPermutations"_s))
            return std::nullopt;
        const QString first = allowed.left(1);
        v = pm.contains(first) ? std::optional<int>(pm.value(first).toInt()) : std::nullopt;
    }
    const QJsonObject* p = item(key, v);
    return p ? *p : QJsonObject();
}

// getModelFileNameByGuid.
QString modelByItem(const Rll4Character::Options& options, const QJsonObject* it, const QString& mapName,
                    const QHash<QString, int>& overrides = {}) {
    if (!it || truthy(*it, u"noModel"_s))
        return {};
    const ItemMap* map = itemMap(mapName);
    if (!map)
        return {};
    const QString sex = it->value(u"sex"_s).toString();
    if (!sex.isEmpty()) {
        const bool male = opt(options, u"sex"_s) == 0;
        if ((male && !sex.contains(u'm')) || (!male && !sex.contains(u'f')))
            return {};
    }
    QString name = map->def.value(u"namePrefix"_s).toString();
    name += it->contains(u"modelName"_s) ? it->value(u"modelName"_s).toString() : it->value(u"name"_s).toString();
    for (const QJsonValue& k : map->def.value(u"permutationOrder"_s).toArray()) {
        const QString key = k.toString();
        const int value = overrides.contains(key) ? overrides.value(key) : opt(options, key);
        const std::optional<QJsonObject> p = permutationItem(*map, value, *it, key);
        if (!p)
            return {};
        const QString suffix = p->value(u"name"_s).toString();
        if (!suffix.isEmpty())
            name += u'_' + suffix;
    }
    return map->def.value(u"basePath"_s).toString() + u"/models/"_s + name + u'/' + name;
}

QString modelByGuid(const Rll4Character::Options& options, std::optional<int> guid, const QString& mapName,
                    const QHash<QString, int>& overrides = {}) {
    return modelByItem(options, item(mapName, guid), mapName, overrides);
}

double lerp(double t, double a, double b) {
    return a + (b - a) * t;
}

// An option's value as the game's scripts see it (getOption): sliders 0 - 1 mapped through their range, derived
// options computed.
double scriptValue(const QString& name, const Rll4Character::Options& options, int depth = 0) {
    const QJsonObject o = data().options.value(name).toObject();
    if (o.isEmpty() || depth > 6)
        return 0;
    const QString kind = o.value(u"kind"_s).toString();
    if (kind == u"clamp"_s) {
        const double v = scriptValue(o.value(u"option"_s).toString(), options, depth + 1);
        const QJsonArray in = o.value(u"in"_s).toArray(), out = o.value(u"out"_s).toArray();
        const double i1 = in.at(0).toDouble(), i2 = in.at(1).toDouble();
        const double x = (std::min(i2, std::max(i1, v)) - i1) / (i2 - i1);
        return lerp(x, out.at(0).toDouble(), out.at(1).toDouble());
    }
    if (kind == u"match"_s) {
        const QJsonObject* it = item(o.value(u"list"_s).toString(),
                                     int(scriptValue(o.value(u"option"_s).toString(), options, depth + 1)));
        return it && it->value(u"name"_s).toString() == o.value(u"name"_s).toString() ? 1 : 0;
    }
    if (kind == u"other"_s) {
        // The body types: getFiveWayBlendFn("fitness", "weight", n), and their shirt tucked / socks down copies.
        for (const QString& flag : {u"_shirt_tucked"_s, u"_socks_down"_s}) {
            if (name.endsWith(flag)) {
                const QString option = flag == u"_shirt_tucked"_s ? u"shirtTucked"_s : u"socksDown"_s;
                return opt(options, option) == 1 ? scriptValue(name.chopped(flag.size()), options, depth + 1) : 0;
            }
        }
        const double f = scriptValue(u"fitness"_s, options, depth + 1);
        const double w = scriptValue(u"weight"_s, options, depth + 1);
        if (name == u"smallUnfit_bodyShape"_s)
            return std::max(1 - (f + w), 0.0);
        if (name == u"largeUnfit_bodyShape"_s)
            return std::max(w - f, 0.0);
        if (name == u"smallFit_bodyShape"_s)
            return std::max(f - w, 0.0);
        if (name == u"largeFit_bodyShape"_s)
            return std::max(f + w - 1, 0.0);
        if (name == u"base_bodyShape"_s)
            return 1 - 2 * std::max(std::abs(f - 0.5), std::abs(w - 0.5));
        return 0;
    }

    // A stored option. Missing: its default (given as the value the scripts see).
    const auto stored = options.constFind(name);
    if (stored == options.constEnd()) {
        const QJsonValue d = o.value(u"default"_s);
        return d.isDouble() ? d.toDouble() : 0;
    }
    if (o.value(u"type"_s).toString() == u"slider"_s) {
        const double raw = stored.value() / double(Rll4Character::sliderMax(name));
        const QJsonArray range = o.value(u"range"_s).toArray();
        return range.size() == 2 ? lerp(raw, range.at(0).toDouble(), range.at(1).toDouble()) : raw;
    }
    return stored.value();
}

QString prettyName(const QString& name) {
    if (name.isEmpty())
        return u"None"_s;
    QString s = name;
    s.replace(u'_', u' ');
    static const QRegularExpression camel(u"([a-z])([A-Z0-9])"_s);
    s.replace(camel, u"\\1 \\2"_s);
    QStringList words = s.split(u' ', Qt::SkipEmptyParts);
    for (QString& w : words)
        w[0] = w[0].toUpper();
    return words.join(u' ');
}

} // namespace

namespace Rll4Character {

const QStringList& slotNames() {
    static const QStringList s = {u"body"_s, u"head"_s, u"top"_s, u"shoes"_s, u"hairTop"_s, u"beard"_s,
                                  u"moustache"_s, u"chinhair"_s, u"sideburns"_s, u"headWear"_s};
    return s;
}

QList<QPair<QString, QString>> models(const Options& options) {
    QHash<QString, QString> found;
    const int head = opt(options, u"head"_s);
    const bool male = opt(options, u"sex"_s) == 0;
    const bool hidden = opt(options, u"hideBody"_s) != 0;

    // Headwear: its item's "gear" picks the hair / beard models made to fit under it.
    const int helmet = opt(options, u"helmet"_s);
    const QJsonObject* helmetItem = item(u"helmet"_s, helmet);
    const int gear = helmetItem ? helmetItem->value(u"gear"_s).toInt(0) : 0;
    const QHash<QString, int> gearOverride{{u"hairGear"_s, gear}};

    found[u"head"_s] = head != 0 ? modelByGuid(options, head, u"tier1head"_s) : QString();
    if (found.value(u"head"_s).isEmpty())   // The created players' head, or a real player's not in the game's list.
        found[u"head"_s] = modelByGuid(options, 0, u"head"_s);
    if (opt(options, u"body"_s) == 0 && !hidden)
        found[u"body"_s] = modelByGuid(options, 0, u"body"_s);

    // The kit: the player's team decides it in the game. "kitUniform" (not a game option: the 3D view's kit pick, a
    // team jersey's manufacturer) picks the model; else the first kit for the player's sex.
    if (const auto it = options.constFind(u"kitUniform"_s); it != options.constEnd()) {
        if (const QJsonObject* kit = item(u"uniformTop_player"_s, int(it.value())))
            found[u"top"_s] = modelByItem(options, kit, u"uniformTop_player"_s);
    }
    if (const ItemMap* kits = itemMap(u"uniformTop_player"_s); kits && found.value(u"top"_s).isEmpty()) {
        const QString suffix = male ? u"_male"_s : u"_female"_s;
        for (const QJsonObject& it : kits->items) {
            if (!it.value(u"name"_s).toString().endsWith(suffix))
                continue;
            const QString path = modelByItem(options, &it, u"uniformTop_player"_s);
            if (!path.isEmpty()) {
                found[u"top"_s] = path;
                break;
            }
        }
    }
    found[u"shoes"_s] = modelByGuid(options, opt(options, u"shoes"_s), u"shoes"_s);

    if (!hidden)
        found[u"hairTop"_s] = modelByGuid(options, opt(options, u"hairTop"_s), u"hairTop"_s, gearOverride);
    // A full beard, or else its parts.
    const QJsonObject* beard = item(u"beard"_s, opt(options, u"beard"_s));
    if (beard && !truthy(*beard, u"noModel"_s)) {
        found[u"beard"_s] = modelByItem(options, beard, u"beard"_s, gearOverride);
    } else {
        found[u"moustache"_s] = modelByGuid(options, opt(options, u"moustache"_s), u"moustache"_s);
        found[u"sideburns"_s] = modelByGuid(options, opt(options, u"sideburns"_s), u"sideburns"_s, gearOverride);
        found[u"chinhair"_s] = modelByGuid(options, opt(options, u"chinhair"_s), u"chinhair"_s, gearOverride);
    }
    found[u"headWear"_s] = modelByItem(options, helmetItem, u"helmet"_s);

    QList<QPair<QString, QString>> out;
    for (const QString& slot : slotNames())
        if (!found.value(slot).isEmpty())
            out.append({slot, found.value(slot)});
    return out;
}

QHash<QString, float> shapeWeights(const Options& options) {
    // The male and female meshes both carry the other sex's body shapes: only the player's are applied.
    const QChar sex = opt(options, u"sex"_s) == 1 ? u'f' : u'm';
    QHash<QString, float> out;
    for (const auto& shape : data().shapes) {
        if (!shape.sex.isNull() && shape.sex != sex)
            continue;
        const double w = scriptValue(shape.option, options);
        if (w != 0)
            out.insert(shape.name, float(w));
    }
    return out;
}

bool isSlider(const QString& option) {
    const QJsonObject o = data().options.value(option).toObject();
    return !o.contains(u"kind"_s) && o.value(u"type"_s).toString() == u"slider"_s;
}

int sliderMax(const QString& option) {
    return data().options.value(option).toObject().value(u"byteSize"_s).toInt(1) == 2 ? 65535 : 255;
}

QList<Choice> choices(const QString& option, int sex) {
    const QString list = option == u"head"_s ? u"tier1head"_s : option;
    const ItemMap* map = itemMap(list);
    if (!map || !map->def.contains(u"basePath"_s))
        return {};
    QList<Choice> out;
    if (option == u"head"_s)
        out.append({0, u"Created player"_s});
    QSet<int> seen;
    for (const QJsonObject& it : map->items) {
        if (!it.contains(u"guid"_s))
            continue;
        const int guid = it.value(u"guid"_s).toInt();
        if (seen.contains(guid))
            continue;
        const QString itemSex = it.value(u"sex"_s).toString();
        if (!itemSex.isEmpty() && !itemSex.contains(sex == 1 ? u'f' : u'm'))
            continue;
        seen.insert(guid);
        const QString ui = it.value(u"uiName"_s).toString();
        out.append({guid, ui.isEmpty() ? prettyName(it.value(u"name"_s).toString()) : ui});
    }
    return out;
}

bool affectsModels(const QString& option) {
    static const QSet<QString> keys = {u"sex"_s, u"head"_s, u"hideBody"_s, u"body"_s, u"hairTop"_s, u"beard"_s,
                                       u"moustache"_s, u"chinhair"_s, u"sideburns"_s, u"helmet"_s, u"shoes"_s};
    return keys.contains(option);
}

double optionValue(const QString& option, const Options& options) {
    return scriptValue(option, options);
}

QString saveOption(const QString& fieldKey) {
    // The RLL4 save / career editors' appearance fields (from the RLL4 Save Editor's names) -> the script's options.
    static const QHash<QString, QString> map = {
        {u"appearance.headMeshId"_s, u"head"_s},
        {u"appearance.skinTone1"_s, u"baseColourX"_s},
        {u"appearance.skinTone2"_s, u"baseColourY"_s},
        {u"appearance.buildHead"_s, u"headSize"_s},
        {u"appearance.buildNeck1"_s, u"neckWidth_bodyShape"_s},
        {u"appearance.buildNeck2"_s, u"neckLength_bodyShape"_s},
        {u"appearance.buildTraps"_s, u"traps_bodyShape"_s},
        {u"appearance.buildChest"_s, u"chest_bodyShape"_s},
        {u"appearance.buildShoulders"_s, u"shoulders_bodyShape"_s},
        {u"appearance.buildArms"_s, u"arms_bodyShape"_s},
        {u"appearance.buildStomach"_s, u"gut_bodyShape"_s},
        {u"appearance.buildLegs"_s, u"legs_bodyShape"_s},
        {u"appearance.hairStyle"_s, u"hairTop"_s},
        {u"appearance.hairColour"_s, u"hair_colour"_s},
        {u"appearance.bodyHairId"_s, u"bodyHair_type"_s},
        {u"appearance.bodyHairColour"_s, u"bodyHair_colour"_s},
        {u"appearance.beardStubbleType"_s, u"beardShadow_type"_s},
        {u"appearance.beardStubbleIntensity"_s, u"beardShadow_amount"_s},
        {u"appearance.beardId"_s, u"beard"_s},
        {u"appearance.beardMoustache"_s, u"moustache"_s},
        {u"appearance.beardSideburns"_s, u"sideburns"_s},
        {u"appearance.beardChinId"_s, u"chinhair"_s},
        {u"appearance.beardColour"_s, u"beard_colour"_s},
        {u"appearance.headgearId"_s, u"helmet"_s},
        {u"appearance.headgearColour1"_s, u"helmetColour1"_s},
        {u"appearance.headgearColour2"_s, u"helmetColour2"_s},
        {u"appearance.headgearColour3"_s, u"helmetColour3"_s},
        {u"appearance.headgearColour4"_s, u"helmetColour4"_s},
        {u"appearance.shirtTucked"_s, u"shirtTucked"_s},
        {u"appearance.socksDown"_s, u"socksDown"_s},
        {u"appearance.bootsModel"_s, u"shoes"_s},
        {u"appearance.bootsColour1"_s, u"shoeColour1"_s},
        {u"appearance.bootsColour2"_s, u"shoeColour2"_s},
        {u"appearance.bootsColour3"_s, u"shoeColour3"_s},
        {u"appearance.bootsColour4"_s, u"shoeColour4"_s},
    };
    return map.value(fieldKey);
}

namespace {

// An RGB option ("hair_colour" -> its _r / _g / _b sliders), 0 - 255.
Rl26Character::Rgb colourOption(const Options& options, const QString& name, Rl26Character::Rgb fallback) {
    const auto r = options.constFind(name + u"_r"_s);
    if (r == options.constEnd())
        return fallback;
    return {float(r.value()), float(options.value(name + u"_g"_s)), float(options.value(name + u"_b"_s))};
}

Rl26Character::TextureLayer layer(const QString& path, const QString& blend, float intensity = 1.0f) {
    Rl26Character::TextureLayer l;
    l.path = path.toLower();   // Texture lookups are by lower case path.
    l.blend = blend;
    l.intensity = intensity;
    return l;
}

// The texture pack's texture ending in `suffix` ("_col", "_nrm", "_mat"), preferring one that contains `prefer`.
QString packTexture(const QStringList& pack, const QString& suffix, const QString& prefer = {}) {
    QString found;
    for (const QString& t : pack) {
        if (!t.endsWith(suffix) || t.startsWith(u'@'))   // "@<material>:<param>=<texture>": the model's materials.
            continue;
        if (!prefer.isEmpty() && t.contains(prefer))
            return t;
        if (found.isEmpty())
            found = t;
    }
    return found;
}

} // namespace

Rl26Character::MaterialTexture materialTexture(const QString& slot, const QString& material, const QString& modelPath,
                                               const QStringList& packTextures, const Options& options) {
    using Kind = Rl26Character::MaterialTexture::Kind;
    Rl26Character::MaterialTexture t;
    const bool female = opt(options, u"sex"_s) == 1;
    const QString sx = female ? u"f"_s : u"m"_s;

    if (material == u"eyes_clear_mat"_s) {   // The clear cornea over the eyes.
        t.kind = Kind::Hidden;
        return t;
    }
    if (material == u"eyelashes_mat"_s) {
        t.kind = Kind::Single;
        t.style = u"lash"_s;
        t.path = u"characters/hair/textures/eyelashstrand_straight_col"_s;
        return t;
    }

    if (slot == u"head"_s || slot == u"body"_s) {
        t.kind = Kind::Layers;
        t.style = u"skin"_s;
        t.matLayout = Rl26Character::MaterialTexture::MatLayout::Rll4Skin;
        t.gloss = 0.45f;
        const bool body = slot == u"body"_s;
        if (!body && opt(options, u"head"_s) != 0 && !modelPath.contains(u"player_master_head"_s, Qt::CaseInsensitive)) {
            // A real player's scanned head: its own textures (characters/heads/textures/<name>_col, ...).
            const QString col = packTexture(packTextures, u"_col"_s);
            if (col.isEmpty()) {
                t.kind = Kind::None;
                return t;
            }
            t.layers.append(layer(col, u"creator_copy_rgb"_s));
            if (const QString n = packTexture(packTextures, u"_nrm"_s); !n.isEmpty())
                t.normalLayers.append(layer(n, u"creator_copy_rgb"_s));
            if (const QString m = packTexture(packTextures, u"_mat"_s); !m.isEmpty())
                t.glossLayers.append(layer(m, u"creator_copy_rgb"_s));
            return t;
        }
        // The created players' skin: miscSkinSpecific_head / _body's "fourWay" item, its four tones weighted by the
        // skin tone options (getFourWayBlendFn("baseColourX", "baseColourY", n)).
        const double x = scriptValue(u"baseColourX"_s, options), y = scriptValue(u"baseColourY"_s, options);
        const double weights[4] = {(1 - x) * (1 - y), (1 - x) * y, x * (1 - y), x * y};
        const QString base = u"characters/player/textures/base/%1_player_%2_"_s.arg(sx, body ? u"body"_s : u"head"_s);
        for (int n = 0; n < 4; ++n)
            if (weights[n] > 0.001)
                t.layers.append(layer(base + u"0%1_col"_s.arg(n + 1), u"creator_weighted"_s, float(weights[n])));
        // A skin layer item of headColourLayers: its four tone versions ("<prefix>01_col" ...) weighted by the skin
        // tone ("fourWay") times the layer's amount, each drawn over by its alpha.
        const auto toned = [&](const QString& prefix, double amount) {
            if (amount <= 0.001)
                return;
            for (int n = 0; n < 4; ++n)
                if (weights[n] * amount > 0.001)
                    t.layers.append(layer(prefix + u"0%1_col"_s.arg(n + 1), u"creator_alphaweighted"_s,
                                          float(weights[n] * amount)));
        };
        const QString skinLayers = u"characters/player/textures/skinlayers/"_s;
        if (!body) {
            // Redness (the game's defaults: cheeks 0.5, face 0.2, nose 0.1) and the eye socket tint.
            toned(skinLayers + u"redness_cheeks_"_s, scriptValue(u"redness_cheeks_amount"_s, options));
            toned(skinLayers + u"redness_face_"_s, scriptValue(u"redness_face_amount"_s, options));
            toned(skinLayers + u"redness_nose_"_s, scriptValue(u"redness_nose_amount"_s, options));
            static const char* const sockets[] = {"eyesocket_small_", "eyesocket_mid_", "eyesocket_large_"};
            const int socket = std::clamp(opt(options, u"eyeSocketTint_type"_s), 0, 2);
            toned(skinLayers + QString::fromLatin1(sockets[socket]), 1.0);

            // headColourPostDecalLayers: the eyebrows and the beard shadow (stubble), green masks coloured with the
            // eyebrow / beard colour ("creator_twotint"). The save editors don't have the eyebrow's own options:
            // the default style, in the beard's colour.
            const Rl26Character::Rgb beardColour = colourOption(options, u"beard_colour"_s, {60, 40, 25});
            // AssetMapItems.eyebrows_head_col (guids 0 - 6 the men's, 7 - 13 the women's).
            static const char* const brows[] = {"straight", "thickEnds", "curved", "monobrow", "thick", "short", "thinEnds"};
            const int browGuid = opt(options, u"eyebrows_type"_s);
            const int browStyle = std::clamp(browGuid >= 7 ? browGuid - 7 : browGuid, 0, 6);
            {
                Rl26Character::TextureLayer l = layer(u"characters/player/textures/hair/eyebrow_%1%2_col"_s
                                                          .arg(female ? u"f_"_s : QString(), QString::fromLatin1(brows[browStyle])),
                                                      u"creator_twotint"_s);
                l.tinted = true;
                l.tint = colourOption(options, u"eyebrow_colour"_s, beardColour);
                t.layers.append(l);
            }
            const int stubble = opt(options, u"beardShadow_type"_s);
            const double stubbleAmount = scriptValue(u"beardShadow_amount"_s, options);
            if (stubble > 0 && stubbleAmount > 0.001) {
                Rl26Character::TextureLayer l = layer(u"characters/player/textures/hair/player_head_beardShadow_%1_col"_s
                                                          .arg(stubble, 2, 10, QChar(u'0')),
                                                      u"creator_twotint"_s, float(stubbleAmount));
                l.tinted = true;
                l.tint = beardColour;
                t.layers.append(l);
            }
        }
        if (!body) {
            // The eyes (headColourPostDecalLayers): miscEyeSpecific's five eye colour textures weighted by the eye
            // colour options (getFiveWayBlendFn("eyeColourX", "eyeColourY", n)), then eyeComposite's pupil and
            // highlights. Placed in the head's atlas by characters/player/head.texsrc.
            const double ex = scriptValue(u"eyeColourX"_s, options), ey = scriptValue(u"eyeColourY"_s, options);
            const double eyes[5] = {std::max(1 - (ex + ey), 0.0), std::max(ey - ex, 0.0), std::max(ex - ey, 0.0),
                                    std::max(ex + ey - 1, 0.0),
                                    1 - 2 * std::max(std::abs(ex - 0.5), std::abs(ey - 0.5))};
            const QString eye = u"characters/player/textures/base/player_eye_"_s;
            for (int n = 0; n < 5; ++n)
                if (eyes[n] > 0.001)
                    t.layers.append(layer(eye + u"base_0%1_col"_s.arg(n + 1), u"creator_alphaweighted"_s, float(eyes[n])));
            t.layers.append(layer(eye + u"pupil_col"_s, u"creator_multiply"_s));
            t.layers.append(layer(eye + u"highlights_col"_s, u"creator_overlay"_s));
        }
        t.normalLayers.append(layer(base + u"base_nrm"_s, u"creator_copy_rgb"_s));
        t.glossLayers.append(layer(base + u"base_mat"_s, u"creator_copy_rgb"_s));
        return t;
    }

    if (slot == u"top"_s) {
        // The kit: a licensed jersey's pattern texture ("kitPattern", the 3D view's pick: the uniform item's
        // "variants" map, characters/uniforms/textures/<name>), a custom jersey's base (miscUniform topBase_col)
        // in the team's colour ("kitColour"; its pattern layers aren't read from the save yet), else the
        // model's own texture.
        QString col;
        Rl26Character::TextureLayer colour;
        const int pattern = options.contains(u"kitPattern"_s) ? opt(options, u"kitPattern"_s) : -1;
        const QJsonObject* kit = item(u"uniformTop_player"_s, options.contains(u"kitUniform"_s) ? std::optional<int>(opt(options, u"kitUniform"_s)) : std::nullopt);
        if (pattern > 0 && kit) {
            // uniformTop_player's "variants" (the pattern map of each manufacturer's kit).
            static const QHash<int, QString> patternMaps = {
                {0, u"isc_patterns"_s}, {2, u"asics_patterns"_s}, {3, u"xblades_patterns"_s},
                {4, u"canterbury_patterns"_s}, {6, u"classic_patterns"_s}, {7, u"classicSans_patterns"_s},
                {8, u"errea_patterns"_s}, {14, u"canterbury_female_patterns"_s}};
            QString variants = kit->value(u"variants"_s).toString();
            if (variants.isEmpty())
                variants = patternMaps.value(kit->value(u"guid"_s).toInt());
            const ItemMap* map = itemMap(variants);
            if (const QJsonObject* p = item(variants, pattern); map && p)
                col = map->def.value(u"basePath"_s).toString() + u'/' + p->value(u"name"_s).toString();
            if (!col.isEmpty())
                colour = layer(col, u"creator_copy_rgb"_s);
        } else if (pattern == 0 && options.contains(u"kitColour_r"_s)) {
            col = u"characters/tops/textures/topBase_col"_s;
            colour = layer(col, u"creator_fourtint"_s);
            colour.tints.append(colourOption(options, u"kitColour"_s, {40, 40, 40}));
        }
        if (col.isEmpty()) {
            col = packTexture(packTextures, u"_col"_s);
            if (col.isEmpty())
                return t;
            colour = layer(col, u"creator_copy_rgb"_s);
        }
        t.kind = Kind::Layers;
        t.style = u"kit"_s;
        t.matLayout = Rl26Character::MaterialTexture::MatLayout::Rll4Cloth;
        t.layers.append(colour);
        // The kit's own normal map ("<maker>_nrm"), not its animated wrinkles ("<maker>_twist_nrm").
        QStringList normals;
        for (const QString& p : packTextures)
            if (p.endsWith(u"_nrm"_s) && !p.contains(u"twist"_s))
                normals.append(p);
        if (const QString n = packTexture(normals, u"_nrm"_s); !n.isEmpty())
            t.normalLayers.append(layer(n, u"creator_copy_rgb"_s));
        if (const QString m = packTexture(packTextures, u"_mat"_s); !m.isEmpty())
            t.glossLayers.append(layer(m, u"creator_copy_rgb"_s));
        return t;
    }

    if (slot == u"shoes"_s || slot == u"headWear"_s) {
        // Boots and headgear: a mask texture, coloured with the item's four colours (creator_fourtint).
        const QString col = packTexture(packTextures, u"_col"_s, u"pattern"_s);
        if (col.isEmpty())
            return t;
        const QString colours = slot == u"shoes"_s ? u"shoeColour"_s : u"helmetColour"_s;
        Rl26Character::TextureLayer l = layer(col, u"creator_fourtint"_s);
        for (int i = 1; i <= 4; ++i)
            l.tints.append(colourOption(options, colours + QString::number(i), {40, 40, 40}));
        t.kind = Kind::Layers;
        t.style = u"kit"_s;
        t.matLayout = Rl26Character::MaterialTexture::MatLayout::Rll4Cloth;
        t.layers.append(l);
        if (const QString n = packTexture(packTextures, u"_nrm"_s); !n.isEmpty())
            t.normalLayers.append(layer(n, u"creator_copy_rgb"_s));
        if (const QString m = packTexture(packTextures, u"_mat"_s); !m.isEmpty())
            t.glossLayers.append(layer(m, u"creator_copy_rgb"_s));
        return t;
    }

    // Hair and beards: the model's own material says its colour texture ("col_map", e.g. the strands'
    // characters/hair/textures/hair_wavyclumps_col and the cap's medium_spiky01_cap_hl0_col).
    if (slot == u"hairTop"_s || slot == u"beard"_s || slot == u"moustache"_s || slot == u"sideburns"_s
        || slot == u"chinhair"_s) {
        const QString key = u"@"_s + material.toLower() + u":col_map="_s;
        QString col;
        for (const QString& p : packTextures)
            if (p.startsWith(key)) {
                col = p.mid(key.size());
                break;
            }
        if (!col.isEmpty()) {
            const Rl26Character::Rgb colour = slot == u"hairTop"_s ? colourOption(options, u"hair_colour"_s, {59, 42, 30})
                                                                    : colourOption(options, u"beard_colour"_s, {60, 40, 25});
            t.kind = Kind::Single;
            t.style = material.endsWith(u"_cap"_s, Qt::CaseInsensitive) ? u"cap"_s : u"hair"_s;
            t.strandShading = true;
            t.path = col;
            t.coloured = true;
            t.colour1 = colour;
            t.colour2 = colour;
            return t;
        }
    }
    if (slot == u"hairTop"_s) {
        // The hair cap: characters/hair/textures/shared's "<length>_<style>_cap_hl<hairline>_col" for the model
        // "top_<style>_<length>_mhl<hairline>_<sex>".
        const QString name = modelPath.section(u'/', -1).toLower();
        static const QRegularExpression parts(u"^top_([a-z0-9]+?)_(short|medium|long)(?:_[a-z]+)*?_[mf]hl(\\d)"_s);
        const QRegularExpressionMatch m = parts.match(name);
        if (!m.hasMatch())
            return t;
        const Rl26Character::Rgb hair = colourOption(options, u"hair_colour"_s, {59, 42, 30});
        t.kind = Kind::Single;
        t.style = material.endsWith(u"_cap"_s) ? u"cap"_s : u"hair"_s;
        t.path = u"characters/hair/textures/%1_%2_cap_hl%3_col"_s.arg(m.captured(2), m.captured(1), m.captured(3));
        t.coloured = true;
        t.colour1 = hair;
        t.colour2 = hair;
        return t;
    }
    return t;   // Plain colour (beards, the players' own hair: their textures aren't worked out yet).
}

} // namespace Rll4Character
