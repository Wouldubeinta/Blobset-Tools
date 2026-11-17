#include "rl26_character.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

#include "rl26_character_data.inc"

// One of the game's item maps (AssetMapItems.<name>): how its model paths are made, and its items.
struct ItemMap {
    QJsonObject def;                 // basePath, namePrefix, permutationOrder, permutationMap, backupPermutations
    QList<QJsonObject> items;
    QHash<int, qsizetype> byGuid;    // Last one wins, as the game's lookup tables.
    QHash<QString, qsizetype> byName;
};

struct Data {
    QHash<QString, ItemMap> maps;
    QJsonObject options;
    QList<QPair<QString, QString>> shapes;   // option, blend shape name
    // Textures: the skin layer groups, the texture item maps, the hair colour swatches (guid -> 0 - 255 RGB).
    QJsonObject textureGroups;
    QHash<QString, ItemMap> textureMaps;
    QHash<int, Rl26Character::Rgb> hairColours;
    QList<int> hairColourOrder;              // The swatches in the game's order.
    int firstHairColour = 0;
};

ItemMap readItemMap(const QJsonObject& def) {
    ItemMap map;
    map.def = def;
    const QJsonArray items = map.def.value(u"items"_s).toArray();
    map.def.remove(u"items"_s);
    for (const QJsonValue& v : items) {
        const QJsonObject item = v.toObject();
        const qsizetype index = map.items.size();
        map.items.append(item);
        if (item.contains(u"guid"_s))
            map.byGuid.insert(item.value(u"guid"_s).toInt(), index);
        const QString name = item.value(u"name"_s).toString();
        if (!name.isEmpty() && !map.byName.contains(name))
            map.byName.insert(name, index);   // First one wins (ipairs).
    }
    return map;
}

const Data& data() {
    static const Data d = [] {
        Data out;
        const QByteArray json = qUncompress(kRl26CharacterData, qsizetype(sizeof(kRl26CharacterData)));
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        const QJsonObject maps = root.value(u"maps"_s).toObject();
        for (auto it = maps.begin(); it != maps.end(); ++it)
            out.maps.insert(it.key(), readItemMap(it.value().toObject()));
        const QJsonObject textures = root.value(u"textures"_s).toObject();
        out.textureGroups = textures.value(u"groups"_s).toObject();
        const QJsonObject textureMaps = textures.value(u"maps"_s).toObject();
        for (auto it = textureMaps.begin(); it != textureMaps.end(); ++it)
            out.textureMaps.insert(it.key(), readItemMap(it.value().toObject()));
        bool first = true;
        for (const QJsonValue& v : textures.value(u"hairColours"_s).toArray()) {
            const QJsonObject c = v.toObject();
            const QJsonArray rgb = c.value(u"colour"_s).toArray();
            const int guid = c.value(u"guid"_s).toInt();
            out.hairColours.insert(guid, {float(rgb.at(0).toDouble()), float(rgb.at(1).toDouble()), float(rgb.at(2).toDouble())});
            out.hairColourOrder.append(guid);
            if (first)
                out.firstHairColour = guid;
            first = false;
        }
        out.options = root.value(u"options"_s).toObject();
        for (const QJsonValue& v : root.value(u"blendShapes"_s).toArray()) {
            const QJsonObject s = v.toObject();
            out.shapes.append({s.value(u"option"_s).toString(), s.value(u"name"_s).toString()});
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

const QJsonObject* itemByName(const QString& mapName, const QString& name) {
    const ItemMap* map = itemMap(mapName);
    if (!map)
        return nullptr;
    const auto it = map->byName.constFind(name);
    return it == map->byName.constEnd() ? nullptr : &map->items.at(it.value());
}

// Lua truthiness of an item field: present and not false.
bool truthy(const QJsonObject& o, const QString& key) {
    const auto it = o.constFind(key);
    return it != o.constEnd() && !it->isNull() && !(it->isBool() && !it->toBool());
}

class Resolver {
public:
    explicit Resolver(const Rl26Character::Options& options) : m_options(options) {}

    int opt(const QString& name) const { return int(m_options.value(name, 0)); }

    // getPermutationItemByGuid: the permutation's item ("Male", "gearA", "long", ...), {} = no suffix,
    // nullopt = no model (noModelMissingPermutations).
    std::optional<QJsonObject> permutationItem(const ItemMap& map, int value, const QJsonObject& it, const QString& key) const {
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

    // getModelFileNameByItem.
    QString modelByItem(const QJsonObject* it, const QString& mapName, const QHash<QString, int>& overrides = {}) const {
        if (!it || truthy(*it, u"noModel"_s))
            return {};
        const ItemMap* map = itemMap(mapName);
        if (!map)
            return {};
        const QString sex = it->value(u"sex"_s).toString();
        if (!sex.isEmpty()) {
            const bool male = opt(u"sex"_s) == 0;
            if ((male && !sex.contains(u'm')) || (!male && !sex.contains(u'f')))
                return {};
        }
        QString name = map->def.value(u"namePrefix"_s).toString();
        name += it->contains(u"modelName"_s) ? it->value(u"modelName"_s).toString() : it->value(u"name"_s).toString();
        for (const QJsonValue& k : map->def.value(u"permutationOrder"_s).toArray()) {
            const QString key = k.toString();
            const int value = overrides.contains(key) ? overrides.value(key) : opt(key);
            const std::optional<QJsonObject> p = permutationItem(*map, value, *it, key);
            if (!p)
                return {};
            const QString suffix = p->value(u"name"_s).toString();
            if (!suffix.isEmpty())
                name += u'_' + suffix;
        }
        return map->def.value(u"basePath"_s).toString() + u"/models/"_s + name + u'/' + name;
    }

    QString modelByGuid(std::optional<int> guid, const QString& mapName, const QHash<QString, int>& overrides = {}) const {
        return modelByItem(item(mapName, guid), mapName, overrides);
    }

    // assetmap.lua's modelSlots[slot].filename.
    QHash<QString, QString> resolve() const {
        QHash<QString, QString> out;
        const int head = opt(u"head"_s);
        const bool tier1 = item(u"tier1head"_s, head) != nullptr;
        const QJsonObject* helmet = item(u"helmet"_s, opt(u"helmet"_s));
        const int gear = helmet ? helmet->value(u"gear"_s).toInt(0) : 0;
        const QJsonObject* shoes = item(u"shoes"_s, opt(u"shoes"_s));
        const bool noSocks = shoes && truthy(*shoes, u"noSocks"_s);
        const int hide = opt(u"hideBody"_s);

        if (hide != 1) {
            out[u"head"_s] = tier1 ? modelByGuid(head, u"tier1head"_s) : modelByGuid(0, u"head"_s);
            out[u"body"_s] = modelByGuid(noSocks ? 2 : 0, u"body"_s);
        }
        out[u"top"_s] = modelByGuid(opt(u"uniformTop"_s), u"uniformTop"_s);
        out[u"bottom"_s] = modelByGuid(opt(u"uniformBottom"_s), u"uniformBottom"_s);
        if (!noSocks)
            out[u"socks"_s] = modelByGuid(opt(u"uniformSocks"_s), u"uniformSocks"_s);
        out[u"shoes"_s] = modelByGuid(opt(u"shoes"_s), u"shoes"_s);

        if (hide == 0) {
            const QJsonObject* hairTop = item(u"hairTop"_s, opt(u"hairTop"_s));
            const QJsonObject* hairSide = item(u"hairSide"_s, opt(u"hairSide"_s));
            for (const QString& slot : {u"hairTop"_s, u"hairSide"_s, u"hairSide2"_s}) {
                const int guid = tier1 ? head + 1000 : opt(slot);
                if (gear == 1) {
                    if (slot == u"hairSide2"_s)
                        continue;
                    QString gearName;
                    QString gearMap;
                    if (slot == u"hairTop"_s) {
                        gearName = hairTop ? hairTop->value(u"gear_top"_s).toString() : QString();
                        gearMap = u"hairTop_gear"_s;
                    } else {
                        gearName = hairSide ? hairSide->value(u"gear_side"_s).toString() : QString();
                        if (gearName.isEmpty() && hairTop)
                            gearName = hairTop->value(u"gear_side"_s).toString();
                        gearMap = u"hairSide_gear"_s;
                    }
                    if (gearName.isEmpty())
                        continue;
                    out[slot] = modelByItem(tier1 ? item(gearMap, guid) : itemByName(gearMap, gearName), gearMap);
                    continue;
                }
                out[slot] = modelByGuid(guid, slot, {{u"hairGear"_s, gear}});
            }

            // The hair cap (the stubble / hairline mesh): with a layered cap texture and / or a beard.
            const QJsonObject* cap = item(u"hairCap_col"_s, opt(u"hairTexture_cap"_s));
            const bool layer = cap && !cap->contains(u"noLayer"_s);
            std::optional<int> capGuid;
            if (tier1) {
                if (layer)
                    capGuid = head + 1000;
            } else {
                bool beard = false;
                if (opt(u"sex"_s) != 1)
                    for (const QString& k : {u"moustache"_s, u"chinhair"_s, u"sideburns"_s}) {
                        const QJsonObject* b = item(k, opt(k));
                        beard = beard || (b && !truthy(*b, u"noModel"_s));
                    }
                if (layer && beard)
                    capGuid = 2;
                else if (layer)
                    capGuid = 0;
                else if (beard)
                    capGuid = 1;
            }
            out[u"hairCap"_s] = modelByGuid(capGuid, u"hairCap"_s);

            for (const QString& slot : {u"moustache"_s, u"chinhair"_s, u"sideburns"_s})
                out[slot] = modelByGuid(tier1 ? head + 1000 : opt(slot), slot);
        }
        if (hide != 1)
            out[u"headWear"_s] = modelByItem(tier1 ? item(u"helmet"_s, head + 1000) : helmet, u"helmet"_s);
        return out;
    }

private:
    const Rl26Character::Options& m_options;
};

double lerp(double t, double a, double b) {
    return a + (b - a) * t;
}

// An option's value as the game's scripts see it (getOption): sliders 0 - 1 mapped through their range, derived
// options computed.
double scriptValue(const QString& name, const Rl26Character::Options& options, int depth = 0) {
    const QJsonObject o = data().options.value(name).toObject();
    if (o.isEmpty() || depth > 4)
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
        if (name == u"ears_pinned"_s || name == u"ears_headstrapping"_s) {
            const QJsonObject* h = item(u"helmet"_s, int(options.value(u"helmet"_s, 0)));
            if (!h || !truthy(*h, name))
                return 0;
            return name == u"ears_pinned"_s ? 1.3 : 1.0;
        }
        if (name.startsWith(u"headwear_hair_"_s)) {
            const QJsonObject* a = item(u"hairTop"_s, int(options.value(u"hairTop"_s, 0)));
            const QJsonObject* b = item(u"hairSide"_s, int(options.value(u"hairSide"_s, 0)));
            return (a && a->value(u"headwearBlendshape"_s).toString() == name)
                           || (b && b->value(u"headwearBlendshape"_s).toString() == name) ? 1 : 0;
        }
        return 0;
    }

    // A stored option. Missing: its default (given as the value the scripts see).
    const auto stored = options.constFind(name);
    if (stored == options.constEnd()) {
        const QJsonValue d = o.value(u"default"_s);
        return d.isDouble() ? d.toDouble() : 0;
    }
    if (o.value(u"type"_s).toString() == u"slider"_s) {
        const double raw = stored.value() / double(Rl26Character::sliderMax(name));
        const QJsonArray range = o.value(u"range"_s).toArray();
        return range.size() == 2 ? lerp(raw, range.at(0).toDouble(), range.at(1).toDouble()) : raw;
    }
    return stored.value();
}

} // namespace

namespace Rl26Character {

const QStringList& slotNames() {
    static const QStringList s = {u"body"_s, u"head"_s, u"top"_s, u"bottom"_s, u"socks"_s, u"shoes"_s,
                                  u"hairCap"_s, u"hairTop"_s, u"hairSide"_s, u"hairSide2"_s, u"moustache"_s,
                                  u"chinhair"_s, u"sideburns"_s, u"headWear"_s};
    return s;
}

QList<QPair<QString, QString>> models(const Options& options, bool previewKit) {
    const Resolver resolver(options);
    QHash<QString, QString> found = resolver.resolve();

    if (previewKit) {
        const QJsonObject* shoes = item(u"shoes"_s, int(options.value(u"shoes"_s, 0)));
        const bool noSocks = shoes && truthy(*shoes, u"noSocks"_s);
        for (const auto& [slot, mapName] : {std::pair{u"top"_s, u"uniformTop"_s}, std::pair{u"bottom"_s, u"uniformBottom"_s},
                                            std::pair{u"socks"_s, u"uniformSocks"_s}}) {
            if (!found.value(slot).isEmpty() || (slot == u"socks"_s && noSocks))
                continue;
            const ItemMap* map = itemMap(mapName);
            if (!map)
                continue;
            for (const QJsonObject& it : map->items) {
                const QString path = resolver.modelByItem(&it, mapName);
                if (!path.isEmpty()) {
                    found[slot] = path;
                    break;
                }
            }
        }
    }

    QList<QPair<QString, QString>> out;
    for (const QString& slot : slotNames())
        if (!found.value(slot).isEmpty())
            out.append({slot, found.value(slot)});
    return out;
}

QHash<QString, float> shapeWeights(const Options& options) {
    QHash<QString, float> out;
    for (const auto& [option, name] : data().shapes) {
        const double w = scriptValue(option, options);
        if (w != 0)
            out.insert(name, float(w));
    }
    return out;
}

QString optionType(const QString& option) {
    const QJsonObject o = data().options.value(option).toObject();
    return o.contains(u"kind"_s) ? u"derived"_s : o.value(u"type"_s).toString();
}

bool isSlider(const QString& option) {
    return optionType(option) == u"slider"_s;
}

int sliderMax(const QString& option) {
    return data().options.value(option).toObject().value(u"byteSize"_s).toInt(1) == 2 ? 65535 : 255;
}

namespace {

// The preset lists' parts: option, and the map its item names are in (presetOptionList in assetmapitems.lua).
const QList<QPair<QString, QString>>& presetParts(const QString& option) {
    static const QList<QPair<QString, QString>> beard{
        {u"moustache"_s, u"moustache"_s}, {u"chinhair"_s, u"chinhair"_s}, {u"sideburns"_s, u"sideburns"_s}};
    static const QList<QPair<QString, QString>> hair{{u"hairTop"_s, u"hairTop"_s}, {u"hairSide"_s, u"hairSide"_s},
                                                     {u"hairSide2"_s, u"hairSide2"_s}, {u"hairTexture_cap"_s, u"hairCap_col"_s}};
    static const QList<QPair<QString, QString>> none;
    return option == u"beard"_s ? beard : option == u"hair"_s ? hair : none;
}

// A preset's name, from its parts ("Moustache Pencil + Chin Soul Patch 01", "Caesar Curly 01", "Cap: Shaved 01").
QString presetName(const QString& option, const QJsonObject& it) {
    QStringList parts;
    QString cap;
    for (const auto& [part, map] : presetParts(option)) {
        QString n = it.value(part).toString();
        if (n.isEmpty())
            continue;
        if (part == u"hairTexture_cap"_s) {
            n.remove(u"haircap_"_s);
            if (n.endsWith(u"_col"_s))
                n.chop(4);
            cap = prettyName(n);
        } else {
            parts.append(prettyName(n));
        }
    }
    if (parts.isEmpty())
        return cap.isEmpty() ? u"None"_s : u"Cap: "_s + cap;
    return parts.join(u" + "_s);
}

} // namespace

QList<QPair<QString, int>> presetOptions(const QString& option, int guid) {
    QList<QPair<QString, int>> out;
    const auto& parts = presetParts(option);
    if (parts.isEmpty())
        return out;
    const QJsonObject* preset = item(option, guid);
    if (!preset)
        return out;
    for (const auto& [part, mapName] : parts) {
        int g = 0;   // The parts' default.
        const QString name = preset->value(part).toString();
        const ItemMap* map = itemMap(mapName);
        if (!name.isEmpty() && map) {
            const auto found = map->byName.constFind(name);
            if (found != map->byName.constEnd())
                g = map->items.at(found.value()).value(u"guid"_s).toInt();
        }
        out.append({part, g});
    }
    return out;
}

QList<Choice> choices(const QString& option, int sex) {
    // The list an option picks from: the item map of the same name, the head list, the cap textures, or the
    // morph lists the derived options name.
    // The hair colour options (hair, beard, eyebrows / body hair): the game's colour swatches, named "#rrggbb".
    if (option == u"hairColour1"_s || option == u"hairColour2"_s || option == u"beardColour1"_s
        || option == u"beardColour2"_s || option == u"bodyHair_colour"_s) {
        const Data& d = data();
        QList<Choice> out;
        for (int guid : d.hairColourOrder) {
            const Rgb c = d.hairColours.value(guid);
            out.append({guid, u"#%1%2%3"_s.arg(int(c.r), 2, 16, QChar(u'0')).arg(int(c.g), 2, 16, QChar(u'0'))
                                  .arg(int(c.b), 2, 16, QChar(u'0'))});
        }
        return out;
    }
    QString list;
    if (option == u"head"_s)
        list = u"tier1head"_s;
    else if (option == u"hairTexture_cap"_s)
        list = u"hairCap_col"_s;
    else if (itemMap(option))
        list = option;
    else {
        const QJsonObject& all = data().options;
        for (auto it = all.begin(); it != all.end() && list.isEmpty(); ++it) {
            const QJsonObject o = it.value().toObject();
            if (o.value(u"kind"_s).toString() == u"match"_s && o.value(u"option"_s).toString() == option)
                list = o.value(u"list"_s).toString();
        }
    }
    const ItemMap* map = itemMap(list);
    if (!map)
        return {};

    QList<Choice> out;
    if (option == u"head"_s)
        out.append({0, u"Created player (face sliders)"_s});
    QSet<int> seen;
    for (const QJsonObject& it : map->items) {
        if (!it.contains(u"guid"_s))
            continue;
        const int guid = it.value(u"guid"_s).toInt();
        if (seen.contains(guid) || (option == u"head"_s && guid == 0))
            continue;
        const QString itemSex = it.value(u"sex"_s).toString();
        if (option != u"sex"_s && !itemSex.isEmpty() && !itemSex.contains(sex == 1 ? u'f' : u'm'))
            continue;
        seen.insert(guid);
        const QString ui = it.value(u"uiName"_s).toString();
        if (!presetParts(option).isEmpty())
            out.append({guid, presetName(option, it)});
        else
            out.append({guid, ui.isEmpty() ? prettyName(it.value(u"name"_s).toString()) : ui});
    }
    return out;
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

bool affectsModels(const QString& option) {
    static const QSet<QString> keys = {
        u"sex"_s, u"head"_s, u"hideBody"_s, u"hairTop"_s, u"hairSide"_s, u"hairSide2"_s, u"moustache"_s,
        u"chinhair"_s, u"sideburns"_s, u"helmet"_s, u"shoes"_s, u"uniformTop"_s, u"uniformBottom"_s,
        u"uniformSocks"_s, u"hairTexture_cap"_s, u"topType"_s, u"sockType"_s, u"sockHeight"_s};
    return keys.contains(option);
}

double optionValue(const QString& option, const Options& options) {
    return scriptValue(option, options);
}

bool onlyShapes(const QString& option) {
    static const QSet<QString> shapeOnly = [] {
        QSet<QString> used;   // Options the textures / models read.
        const Data& d = data();
        for (auto g = d.textureGroups.begin(); g != d.textureGroups.end(); ++g)
            for (const QJsonValue& v : g.value().toArray()) {
                const QJsonObject e = v.toObject();
                for (const QString& k : {u"option"_s, u"intensity"_s, u"tintHairColour"_s, u"guidNameFrom"_s})
                    if (e.contains(k))
                        used.insert(e.value(k).toString());
                if (e.contains(u"skipIfOption"_s))
                    used.insert(e.value(u"skipIfOption"_s).toArray().at(0).toString());
            }
        for (const QString& k : {u"hairColour1"_s, u"hairColour2"_s, u"beardColour1"_s, u"beardColour2"_s, u"sex"_s,
                                 u"hairTexture_cap"_s, u"helmet"_s, u"head"_s})
            used.insert(k);
        QSet<QString> out;
        for (const auto& [option, name] : d.shapes) {
            const QJsonObject o = d.options.value(option).toObject();
            const QString source = o.value(u"option"_s).toString();
            for (const QString& k : {option, source})
                if (!k.isEmpty() && !used.contains(k) && !affectsModels(k))
                    out.insert(k);
        }
        return out;
    }();
    return shapeOnly.contains(option);
}

namespace {

Rgb hairColour(const QString& option, const Options& options) {
    const Data& d = data();
    const int guid = int(options.value(option, 0));
    return d.hairColours.value(guid, d.hairColours.value(0, d.hairColours.value(d.firstHairColour)));
}

const QJsonObject* textureItem(const QString& mapName, std::optional<int> guid, const QString& name) {
    const auto map = data().textureMaps.constFind(mapName);
    if (map == data().textureMaps.constEnd())
        return nullptr;
    if (guid) {
        const auto it = map->byGuid.constFind(*guid);
        return it == map->byGuid.constEnd() ? nullptr : &map->items.at(it.value());
    }
    const auto it = map->byName.constFind(name);
    return it == map->byName.constEnd() ? nullptr : &map->items.at(it.value());
}

// assetmap.lua's var_0_52: "<basePath>/<name>" of a texture item map, empty without a name.
QString texturePath(const QString& mapName, const QString& name) {
    if (name.isEmpty())
        return {};
    const auto map = data().textureMaps.constFind(mapName);
    if (map == data().textureMaps.constEnd())
        return {};
    return map->def.value(u"basePath"_s).toString() + u'/' + name;
}

// A skin layer group, resolved for the player: the textures that are on, in order.
QList<TextureLayer> layerGroup(const QString& group, const Options& options) {
    QList<TextureLayer> out;
    const int sex = int(options.value(u"sex"_s, 0));
    for (const QJsonValue& v : data().textureGroups.value(group).toArray()) {
        const QJsonObject e = v.toObject();
        if (e.contains(u"skipIfOption"_s)) {
            const QJsonArray skip = e.value(u"skipIfOption"_s).toArray();
            if (int(options.value(skip.at(0).toString(), 0)) == skip.at(1).toInt())
                continue;
        }
        const QString mapName = e.contains(u"map"_s) ? e.value(u"map"_s).toString()
                                                     : e.value(sex == 1 ? u"mapF"_s : u"mapM"_s).toString();
        const QJsonObject* it = nullptr;
        if (e.contains(u"guid"_s))
            it = textureItem(mapName, e.value(u"guid"_s).toInt(), {});
        else if (e.contains(u"option"_s))
            it = textureItem(mapName, int(options.value(e.value(u"option"_s).toString(), 0)), {});
        else if (e.contains(u"guidName"_s))
            it = textureItem(mapName, std::nullopt, e.value(u"guidName"_s).toString());
        else if (e.contains(u"guidNameFrom"_s)) {
            // A field of the player's beard item ("colTexture_cap").
            const QString from = e.value(u"guidNameFrom"_s).toString();
            const QJsonObject* source = item(from, int(options.value(from, 0)));
            const QString name = source ? source->value(e.value(u"guidNameField"_s).toString()).toString() : QString();
            if (!name.isEmpty())
                it = textureItem(mapName, std::nullopt, name);
        }
        if (!it || it->value(u"name"_s).toString().isEmpty())
            continue;
        const auto map = data().textureMaps.constFind(mapName);
        TextureLayer layer;
        layer.path = map->def.value(u"basePath"_s).toString() + u'/' + map->def.value(u"namePrefix"_s).toString()
                     + it->value(u"name"_s).toString();
        layer.blend = it->value(u"blend"_s).toString(u"creator_copy"_s);
        if (e.contains(u"intensity"_s))
            layer.intensity = float(scriptValue(e.value(u"intensity"_s).toString(), options));
        if (e.contains(u"tintHairColour"_s)) {
            layer.tinted = true;
            layer.tint = hairColour(e.value(u"tintHairColour"_s).toString(), options);
        }
        out.append(layer);
    }
    return out;
}

// A hair item's texture field (var_0_53 / var_0_54): the gear version's when a helmet asks for hair gear.
QString hairField(const QString& slot, const QString& field, const Options& options) {
    const QJsonObject* top = item(u"hairTop"_s, int(options.value(u"hairTop"_s, 0)));
    const QJsonObject* side = item(u"hairSide"_s, int(options.value(u"hairSide"_s, 0)));
    const QJsonObject* helmet = item(u"helmet"_s, int(options.value(u"helmet"_s, 0)));
    const int gear = helmet ? helmet->value(u"gear"_s).toInt(0) : 0;
    if (slot == u"hairTop"_s) {
        const QString gearTop = top ? top->value(u"gear_top"_s).toString() : QString();
        if (gear == 1 && !gearTop.isEmpty()) {
            const QJsonObject* g = itemByName(u"hairTop_gear"_s, gearTop);
            return g ? g->value(field).toString() : QString();
        }
        return top ? top->value(field).toString() : QString();
    }
    QString gearSide = side ? side->value(u"gear_side"_s).toString() : QString();
    if (gearSide.isEmpty() && top)
        gearSide = top->value(u"gear_side"_s).toString();
    if (gear == 1 && !gearSide.isEmpty()) {
        const QJsonObject* g = itemByName(u"hairSide_gear"_s, gearSide);
        return g ? g->value(field).toString() : QString();
    }
    return side ? side->value(field).toString() : QString();
}

} // namespace

MaterialTexture materialTexture(const QString& slot, const QString& material, const Options& options) {
    MaterialTexture out;
    const auto single = [&](const QString& path, const QString& style) {
        out.kind = path.isEmpty() ? MaterialTexture::Kind::None : MaterialTexture::Kind::Single;
        out.path = path;
        out.style = style;
    };
    const auto glossy = [&](float g) { out.gloss = g; };
    const auto colours = [&](const QString& first, const QString& second) {
        out.coloured = true;
        out.colour1 = hairColour(first, options);
        out.colour2 = hairColour(second, options);
    };

    if (slot == u"head"_s) {
        if (material == u"eyes_clear_mat"_s) {
            out.kind = MaterialTexture::Kind::Hidden;   // The clear coat over the eyes.
            return out;
        }
        if (material == u"eyelashes_mat"_s) {
            single(texturePath(u"eyelashStrand_col"_s, u"eyelashStrand_straight_col"_s), u"lash"_s);
            out.coloured = true;   // RedColour: black.
            glossy(0.35f);
            return out;
        }
        const bool tier1 = item(u"tier1head"_s, int(options.value(u"head"_s, 0))) != nullptr;
        const QString group = material == u"eyes_mat"_s ? u"eyesColourLayers"_s
                              : tier1                    ? u"tier1_headColourLayers"_s
                                                         : u"headColourLayers"_s;
        out.kind = MaterialTexture::Kind::Layers;
        out.style = material == u"eyes_mat"_s ? u"eyes"_s : u"skin"_s;
        out.layers = layerGroup(group, options);
        if (material == u"eyes_mat"_s) {
            TextureLayer normal, gloss;
            normal.path = u"characters/player/textures/eyes/eye_nrm"_s;
            gloss.path = u"characters/player/textures/eyes/eye_mat"_s;
            normal.blend = gloss.blend = u"creator_copy"_s;
            out.normalLayers = {normal};
            out.glossLayers = {gloss};
            out.gloss = 0.85f;
        } else {
            out.normalLayers = layerGroup(tier1 ? u"tier1_headNormalLayers"_s : u"headNormalLayers"_s, options);
            out.glossLayers = layerGroup(tier1 ? u"tier1_headMaterialLayers"_s : u"headMaterialLayers"_s, options);
        }
        return out;
    }
    if (slot == u"body"_s) {
        out.kind = MaterialTexture::Kind::Layers;
        out.style = u"skin"_s;
        out.layers = layerGroup(u"bodyColourLayers"_s, options);
        out.normalLayers = layerGroup(u"bodyNormalLayers"_s, options);
        out.glossLayers = layerGroup(u"bodyMaterialLayers"_s, options);
        return out;
    }
    if (slot == u"hairTop"_s || slot == u"hairSide"_s) {
        if (material == u"hairMaterial_extra"_s) {
            const QJsonObject* top = item(u"hairTop"_s, int(options.value(u"hairTop"_s, 0)));
            if (slot == u"hairTop"_s && top && truthy(*top, u"hairBand"_s))
                single(texturePath(u"headBand_textures_col"_s, u"headBand01_matrix_col"_s), u"extra"_s);
            else
                single(texturePath(u"hairTie_textures_col"_s, u"hairtie_col"_s), u"extra"_s);
            return out;
        }
        const QString field = material == u"hairMaterial02"_s ? u"colTexture2"_s : u"colTexture"_s;
        single(texturePath(u"hairColMap_textures"_s, hairField(slot, field, options)), u"hair"_s);
        colours(u"hairColour1"_s, u"hairColour2"_s);
        glossy(0.52f);
        return out;
    }
    if (slot == u"hairSide2"_s) {
        if (material == u"hairMaterial_extra"_s) {
            single(texturePath(u"hairTie_textures_col"_s, u"hairtie_col"_s), u"extra"_s);
            return out;
        }
        const QJsonObject* side2 = item(u"hairSide2"_s, int(options.value(u"hairSide2"_s, 0)));
        single(texturePath(u"hairColMap_textures"_s, side2 ? side2->value(u"colTexture3"_s).toString() : QString()), u"hair"_s);
        colours(u"hairColour1"_s, u"hairColour2"_s);
        glossy(0.52f);
        return out;
    }
    if (slot == u"hairCap"_s) {
        if (material == u"beardCap_mat"_s) {
            out.kind = MaterialTexture::Kind::Layers;
            out.style = u"cap"_s;
            out.layers = layerGroup(u"beardCapColourLayers"_s, options);
            colours(u"beardColour1"_s, u"beardColour1"_s);
            if (out.layers.isEmpty())
                out.kind = MaterialTexture::Kind::Hidden;
            return out;
        }
        const QJsonObject* cap = item(u"hairCap_col"_s, int(options.value(u"hairTexture_cap"_s, 0)));
        const QString name = cap && !cap->contains(u"noLayer"_s) ? cap->value(u"name"_s).toString() : QString();
        single(texturePath(u"hairCap_col"_s, name), u"cap"_s);
        colours(u"hairColour1"_s, u"hairColour1"_s);
        glossy(0.35f);   // Stubble over the scalp: matt.
        if (out.kind == MaterialTexture::Kind::None)
            out.kind = MaterialTexture::Kind::Hidden;
        return out;
    }
    if (slot == u"moustache"_s || slot == u"chinhair"_s || slot == u"sideburns"_s) {
        const QJsonObject* beard = item(slot, int(options.value(slot, 0)));
        single(texturePath(u"beardColMap_textures"_s, beard ? beard->value(u"colTexture"_s).toString() : QString()), u"hair"_s);
        colours(u"beardColour1"_s, u"beardColour2"_s);
        glossy(0.45f);
        return out;
    }
    // An item coloured by tints (boots, headgear): its colTexture ("creator_fourtint": colour 1, with colours 2 - n
    // where the texture's R, G and B masks are), and its own normal and "mat" maps.
    const auto tintedItem = [&](const QJsonObject* it, const QString& prefix, const QStringList& tintOptions,
                                const QString& style) {
        if (!it)
            return;
        const QString colour = it->value(u"colTexture"_s).toString();
        const QJsonObject* colourItem = textureItem(prefix + u"_col"_s, std::nullopt, colour);
        TextureLayer layer;
        layer.path = texturePath(prefix + u"_col"_s, colour);
        layer.blend = colourItem ? colourItem->value(u"blend"_s).toString(u"creator_copy"_s) : u"creator_copy"_s;
        for (const QString& stem : tintOptions)
            layer.tints.append({float(options.value(stem + u"_r"_s, 255)), float(options.value(stem + u"_g"_s, 255)),
                                float(options.value(stem + u"_b"_s, 255))});
        if (layer.path.isEmpty())
            return;
        out.kind = MaterialTexture::Kind::Layers;
        out.style = style;
        out.layers = {layer};
        TextureLayer normal, gloss;
        normal.path = texturePath(prefix + u"_nrm"_s, it->value(u"nrmTexture"_s).toString());
        gloss.path = texturePath(prefix + u"_mat"_s, it->value(u"matTexture"_s).toString());
        normal.blend = gloss.blend = u"creator_copy"_s;
        if (!normal.path.isEmpty())
            out.normalLayers = {normal};
        if (!gloss.path.isEmpty())
            out.glossLayers = {gloss};
        out.gloss = 0.55f;
    };
    if (slot == u"shoes"_s) {
        // shoesColourLayers: the player's four boot colours.
        tintedItem(item(u"shoes"_s, int(options.value(u"shoes"_s, 0))), u"shoes"_s,
                   {u"shoeColour1"_s, u"shoeColour2"_s, u"shoeColour3"_s, u"shoeColour4"_s}, u"boots"_s);
        return out;
    }
    if (slot == u"headWear"_s) {
        // helmetColourLayers: the four headwear colours, or head strapping's two. A scanned head's own headwear
        // (guid head + 1000) like the model.
        const int head = int(options.value(u"head"_s, 0));
        const QJsonObject* helmet = item(u"tier1head"_s, head) ? item(u"helmet"_s, head + 1000)
                                                               : item(u"helmet"_s, int(options.value(u"helmet"_s, 0)));
        const QStringList tints = helmet && truthy(*helmet, u"headStrapping"_s)
                                      ? QStringList{u"headStrappingColour1"_s, u"headStrappingColour2"_s}
                                      : QStringList{u"headwearColour1"_s, u"headwearColour2"_s, u"headwearColour3"_s,
                                                    u"headwearColour4"_s};
        tintedItem(helmet, u"helmet"_s, tints, u"headwear"_s);
        return out;
    }
    return out;   // Kit: its textures come from the team's kit (not drawn yet).
}

} // namespace Rl26Character
