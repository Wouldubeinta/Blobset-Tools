#ifndef HASH_UTILITY_H
#define HASH_UTILITY_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#ifndef BLOBSET_HASH_NO_QT
#include <QString>
#include <QStringList>
#endif

/// How BigAnt's games turn an asset name into the hash stored in the blobset, worked out from the game executables
/// (rll4.exe, ashes.exe) and checked against every named texture in the existing mappings:
///
///   V1 / V2  RLL2, RLL3, RLL4         MurmurHash2, seed 0xDEADBEEF, of  "<path>.<type>.<platform>"
///                                     e.g. "characters/hair/textures/eyelashstrand_straight_col.texture.pc"
///                                     PC reads the 4 byte blocks little endian, PS3 big endian ("....texture.ps3").
///   V3       Cricket 19               FNV-1a 32 of "<game key><path>.<type>", i.e. FNV-1a started on "cricket19"
///                                     e.g. "cricket19" + "characters/armwear/textures/armwear_01_mat_1.texture"
///   V4       RL26, Cricket 24, ...    FNV-1a 64 of "<path>.<type>"
///                                     e.g. "characters/flag/textures/flag_col.texture"
///
/// The name is lower case ASCII with '/' separators. <type> is the asset type, not the file extension: textures are
/// "texture" (a .dds), texture packs "texpack", memory packs "mempack", ...
/// 32 bit hashes collide now and then, so a name found by guessing should be checked against the asset's contents.
namespace BlobsetHash {

/// Lower case + '/' separators, as the engine does before hashing (ASCII only).
inline std::string normalise(std::string_view name) {
    std::string out(name);
    for (char& c : out) {
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
    }
    return out;
}

/// MurmurHash2 (Austin Appleby) as compiled into the V1 / V2 games. `bigEndian`: the 4 byte blocks are read in
/// big endian order (PS3 / Xbox 360 builds).
inline std::uint32_t murmur2(std::string_view data, std::uint32_t seed = 0xDEADBEEFu, bool bigEndian = false) {
    constexpr std::uint32_t m = 0x5BD1E995u;
    const auto* p = reinterpret_cast<const unsigned char*>(data.data());
    std::size_t len = data.size();
    std::uint32_t h = seed ^ std::uint32_t(len);

    while (len >= 4) {
        std::uint32_t k = bigEndian ? (std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3])
                                    : (std::uint32_t(p[3]) << 24 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[1]) << 8 | p[0]);
        k *= m;
        k ^= k >> 24;
        k *= m;
        h *= m;
        h ^= k;
        p += 4;
        len -= 4;
    }
    switch (len) {
    case 3: h ^= std::uint32_t(p[2]) << 16; [[fallthrough]];
    case 2: h ^= std::uint32_t(p[1]) << 8;  [[fallthrough]];
    case 1: h ^= p[0]; h *= m;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
}

/// FNV-1a 32. `state` lets a hash continue from another (V3 starts from the hash of the game key).
inline std::uint32_t fnv1a32(std::string_view data, std::uint32_t state = 0x811C9DC5u) {
    for (const char c : data) {
        state ^= std::uint32_t(std::int32_t(static_cast<signed char>(c)));   // The engine sign extends the bytes.
        state *= 0x01000193u;
    }
    return state;
}

/// FNV-1a 64.
inline std::uint64_t fnv1a64(std::string_view data, std::uint64_t state = 0xCBF29CE484222325ull) {
    for (const char c : data) {
        state ^= static_cast<unsigned char>(c);
        state *= 0x100000001B3ull;
    }
    return state;
}

/// "pc", "ps3" or "xbox360" (the blobset file extension).
inline std::uint32_t hashV1V2(std::string_view path, std::string_view type, std::string_view platform) {
    const std::string key = normalise(std::string(path) + '.' + std::string(type) + '.' + std::string(platform));
    return murmur2(key, 0xDEADBEEFu, platform != "pc");
}

/// `gameKey`: "cricket19" for Cricket 19.
inline std::uint32_t hashV3(std::string_view path, std::string_view type, std::string_view gameKey = "cricket19") {
    return fnv1a32(normalise(std::string(path) + '.' + std::string(type)), fnv1a32(normalise(gameKey)));
}

inline std::uint64_t hashV4(std::string_view path, std::string_view type) {
    return fnv1a64(normalise(std::string(path) + '.' + std::string(type)));
}

#ifndef BLOBSET_HASH_NO_QT
inline quint32 hashV1V2(const QString& path, const QString& type, const QString& platform) {
    return hashV1V2(path.toStdString(), type.toStdString(), platform.toStdString());
}
inline quint32 hashV3(const QString& path, const QString& type, const QString& gameKey = QStringLiteral("cricket19")) {
    return hashV3(path.toStdString(), type.toStdString(), gameKey.toStdString());
}
inline quint64 hashV4(const QString& path, const QString& type) {
    return hashV4(path.toStdString(), type.toStdString());
}
inline QString toHex(quint64 hash, int digits) {
    return QStringLiteral("0x") + QString::number(hash, 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}

/// "pc", "ps3" or "xbox360" for CoreDefs::Platforms (Windows = 0, PS3 = 1, Xbox360 = 2).
inline QString platformName(int platform) {
    switch (platform) {
    case 1:  return QStringLiteral("ps3");
    case 2:  return QStringLiteral("xbox360");
    default: return QStringLiteral("pc");
    }
}

/// V3's game key: the game name in lower case without spaces ("Cricket 19" -> "cricket19").
inline QString gameKeyFor(const QString& gameName) {
    QString key = gameName.toLower();
    key.remove(QLatin1Char(' '));
    return key;
}

/// Asset types the engine hashes ("<path>.<type>"). A file name may spell its type out ("field_t1.model.mesh");
/// otherwise the type is worked out from the extension.
inline const QStringList& knownTypes() {
    static const QStringList types = {
        QStringLiteral("texture"), QStringLiteral("texpack"), QStringLiteral("model"), QStringLiteral("mempack"),
        QStringLiteral("material"), QStringLiteral("anim"), QStringLiteral("animset"), QStringLiteral("bank"),
        QStringLiteral("soundbank"), QStringLiteral("sound"), QStringLiteral("font"), QStringLiteral("script"),
        QStringLiteral("bin"), QStringLiteral("entity"), QStringLiteral("prefab"), QStringLiteral("data"),
        QStringLiteral("json"), QStringLiteral("shader"), QStringLiteral("wem"), QStringLiteral("bnk")};
    return types;
}

/// Type used when the file name doesn't spell one out.
inline QString defaultTypeForExtension(const QString& extension) {
    if (extension == QLatin1String("dds") || extension == QLatin1String("badds"))
        return QStringLiteral("texture");
    if (extension == QLatin1String("txpk"))
        return QStringLiteral("texpack");
    if (extension == QLatin1String("mesh"))
        return QStringLiteral("model");
    if (extension == QLatin1String("m3mp"))
        return QStringLiteral("mempack");
    return extension;
}

struct AssetKey {
    QString path;   // Lower case, '/' separated, no type / extension ("ui/textures/circles").
    QString type;   // "texture", "model", ...
};

/// The engine name of a mod / mapped file:
///   "ui\textures\circles.pc.dds"          -> ui/textures/circles   + texture
///   "stadiums/westpac/field_t1.model.mesh" -> stadiums/westpac/field_t1 + model
///   "mempacks/heatmaps.mempack.m3mp"      -> mempacks/heatmaps     + mempack
inline AssetKey assetKeyFromFile(const QString& relativePath) {
    QString rel = relativePath.trimmed().toLower();
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (rel.startsWith(QLatin1String("./")))
        rel.remove(0, 2);
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);

    const qsizetype slash = rel.lastIndexOf(QLatin1Char('/'));
    const QString dir = slash >= 0 ? rel.left(slash + 1) : QString();
    QStringList parts = rel.mid(slash + 1).split(QLatin1Char('.'));
    const QString extension = parts.size() > 1 ? parts.takeLast() : QString();

    if (parts.size() > 1 && (parts.last() == QLatin1String("pc") || parts.last() == QLatin1String("ps3")
                             || parts.last() == QLatin1String("xbox360")))
        parts.removeLast();

    QString type;
    if (parts.size() > 1 && knownTypes().contains(parts.last()))
        type = parts.takeLast();
    else
        type = defaultTypeForExtension(extension);

    return {dir + parts.join(QLatin1Char('.')), type};
}

/// The blobset hash of an asset for CoreDefs::BlobsetVersion `version` (0 = V1 ... 3 = V4).
inline quint64 hashForAsset(int version, const AssetKey& key, int platform, const QString& gameKey) {
    switch (version) {
    case 0:
    case 1:
        return hashV1V2(key.path, key.type, platformName(platform));
    case 2:
        return hashV3(key.path, key.type, gameKey);
    default:
        return hashV4(key.path, key.type);
    }
}

/// The blobset hash of a mod / mapped file name (see assetKeyFromFile()).
inline quint64 hashForFile(int version, const QString& relativePath, int platform, const QString& gameKey) {
    return hashForAsset(version, assetKeyFromFile(relativePath), platform, gameKey);
}
#endif

} // namespace BlobsetHash

#endif // HASH_UTILITY_H
