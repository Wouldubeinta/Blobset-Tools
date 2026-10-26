#ifndef ASSET_SIGNATURES_H
#define ASSET_SIGNATURES_H

#include "core_defs.h"

#include <QByteArray>
#include <QLatin1Char>
#include <QString>
#include <QtEndian>

#include <cstring>
#include <optional>

/// V4 asset formats that have no CoreDefs::FileType of their own, recognised by their first bytes.
///
/// FileMappingWorker and ExtractWorker both call identifyV4() / identifyV1(), so the path written to the mapping and the
/// path the file is extracted to can't drift apart. Found by scanning every entry of Rugby League 26 (PC):
///
///   78 56 34 12  skinned model (bone names like HJ_Jaw)               -> model\skinned\N.mdl
///   7F 13 80 24  skinned model data, always next to a skinned model   -> model\skinned\N.mdat
///   02 BA DC FE  static mesh (stadium geometry, "PosNrmTan_1UV")      -> model\static\N.mesh
///   99 CC 22 88  cinematic light / node animation                     -> anim\cinematic\N.anim
///   "LPXT"       texture path list (TXPL)                             -> txpl\N.txpl
///   24 00 00 00  head joint offset tables (hj_offsets_*.lua)          -> hj_offsets\N.hjo
///
/// Found later in the files the mapping still left unknown (RL26: 490 of them), checked after the formats above:
///
///   clip count, 5 ascending offsets, first table = 4 * count bytes   animation packs (bone names + clip lengths)
///                                                                     -> anim\packs\N.anims
///   data offset, count, data offset + 4 * count, ..., 0x18 at +24     collision meshes ("fence_collision", ...)
///                                                                     -> collision\N.col
///   40 00 00 00 + a "*.lua" name   character body shapes ("*_male.lua", "bodyShape" nodes)
///                                                                     -> body_shapes\N.bshp
///   "<stadium>_crowdnodes" in the first bytes   crowd seat layouts    -> crowd\nodes\N.crowd
///   "V:...|S:..." declarations     vertex format / stream tables      -> shader\vertex_formats\N.vfmt
///   "Commentary" at +8 / "subcategorydefault"   commentary database and event tables
///                                                                     -> sound\commentary\N.cmtry
///   "MIDI" + hash / junk / data chunks          MIDI sequences        -> sound\midi\N.midi
///   50 13 08 80 / 51 13 08 80 at +0 or +4       serialized objects (blend controllers, UI text styles)
///                                                                     -> objects\N.obj
///   count + length prefixed strings             string tables (localisation keys)
///                                                                     -> text\strings\N.str
///   short printable ASCII only                  version / text files  -> text\N.txt
///
/// Also in the V1 / V2 games (Rugby League Live 3 / 4), which share the rules above through identifyCommon():
///
///   "MOB!"                                      MOB files             -> mob\N.mob
///   count, then count * 92 byte records starting with a texture name ("xcalibur_col_1", "player_body_acne_01_col_1")
///                                                                     -> texture_names\N.names
///   f32 size, f32, u32 glyph count, u32 first character, 40 bytes per glyph (12 + 40 * count bytes in all)
///                                               bitmap font glyph tables -> font\glyphs\N.glyphs
///   "FLATLUADATA_IMPORT" / "script-config/"     flattened Lua script data -> scripts\N.luadata
///   "AnimU_" names                              animation sets        -> anim\sets\N.animset
///
/// And in AFL Live (PC, Blobset V1):
///
///   big endian clip count, then length prefixed clip names + f32 lengths, then joint names
///                                               animation clip packs  -> anim\clips\N.clips
///   "FSB4"                                      FMOD 4 sound banks    -> sound\fsb\N.fsb (CoreDefs::FileType::FSB4)
///
/// And in the big endian (PS3 / Xbox 360) builds of Rugby League Live 2 and Don Bradman Cricket 14 (identifyCommon()
/// with bigEndian = true, see detail::identifyConsole()):
///
///   count, count * (hash, offset, size), chunks back to back, each starting with FA DE
///                                               head / hair meshes    -> model\mesh_packs\N.meshpack
///   count, 5 ascending offsets, header size (8 * count + 32) & ~15
///                                               animation clip packs  -> anim\clip_packs\N.clippack
///   count, 6 ascending offsets, 0, first section starts with 0x20
///                                               keyframed animations  -> anim\keyframed\N.kfanim
///   collision / glyph tables / serialized objects / texture names, stored big endian (names: 60 byte records)
///   count, count ascending offsets              offset table databases (player names, ...) -> data\tables\N.tbl
///   exactly 1200 bytes                          20 x 60 byte grids (DBC14) -> data\grids\N.grid
///
/// identifyV4() reads little endian and is only used for V4 (PC) blobsets; V1 / V2 use identifyV1() further down.
namespace AssetSignatures {

/// How many leading bytes identifyV4() should be given for an uncompressed file (one V4 chunk).
/// For a compressed file, pass the whole decompressed first chunk.
constexpr qint64 kPeekBytes = 262144;

constexpr quint32 kSkinnedModel     = 0x12345678u;
constexpr quint32 kSkinnedModelData = 0x2480137Fu;
constexpr quint32 kStaticMesh       = 0xFEDCBA02u;
constexpr quint32 kCinematicAnim    = 0x8822CC99u;
constexpr quint32 kTextureList      = 0x5458504Cu; // "LPXT" on disk.
constexpr quint32 kHjOffsets        = 0x00000024u; // Just a header size, so it's only trusted with a ".lua" name.

struct Match {
    QString folder;    // '\' separated, relative to the platform folder.
    QString extension;
};

namespace detail {

inline quint32 u32At(const QByteArray& data, qsizetype offset) {
    return data.size() >= offset + 4 ? qFromLittleEndian<quint32>(data.constData() + offset) : 0;
}

/// Clip count, then 5 ascending section offsets; the first section is one u32 per clip.
inline bool isAnimationPack(const QByteArray& data, qint64 sectionSize) {
    if (data.size() < 24)
        return false;
    const quint32 count = u32At(data, 0);
    quint32 offsets[5];
    for (int k = 0; k < 5; ++k)
        offsets[k] = u32At(data, 4 + 4 * k);
    if (count == 0 || count > 100000 || offsets[0] < 24 || offsets[1] - offsets[0] != 4 * count)
        return false;
    for (int k = 0; k < 4; ++k)
        if (offsets[k] >= offsets[k + 1])
            return false;
    return offsets[4] <= sectionSize;
}

/// Data offset, mesh count, then offsets that start right after one u32 per mesh; 0x18 at +24.
inline bool isCollision(const QByteArray& data, qint64 sectionSize) {
    if (data.size() < 28)
        return false;
    const quint32 dataOffset = u32At(data, 0), count = u32At(data, 4);
    const quint32 a = u32At(data, 8), b = u32At(data, 12), c = u32At(data, 16);
    return count >= 1 && count <= 4096 && a == dataOffset + 4 * count && dataOffset < a && a < b && b < c
           && c <= sectionSize && u32At(data, 24) == 0x18;
}

/// Count, then at least two length prefixed printable strings.
inline bool isStringTable(const QByteArray& data) {
    const quint32 count = u32At(data, 0);
    if (count < 2 || count > 1000000)
        return false;
    qsizetype pos = 4;
    for (int n = 0; n < 2; ++n) {
        const quint32 length = u32At(data, pos);
        if (length == 0 || length > 256 || pos + 4 + length > data.size())
            return false;
        for (qsizetype k = pos + 4; k < pos + 4 + length; ++k) {
            const uchar ch = static_cast<uchar>(data[k]);
            if (ch < 0x20 || ch == 0x7F)   // Accented (UTF-8 / Latin-1) text is fine: localised tables have it.
                return false;
        }
        pos += 4 + length;
    }
    return true;
}

/// Count, then count * 92 byte records, each starting with a (zero padded) texture name.
inline bool isTextureNameList(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = u32At(data, 0);
    if (count == 0 || count > 100000 || sectionSize != 4 + 92 * static_cast<qint64>(count) || data.size() < 5)
        return false;
    const uchar ch = static_cast<uchar>(data[4]);
    return ch > 0x20 && ch < 0x7F;
}

/// f32 size, f32, u32 glyph count, u32 first character, then 40 bytes per glyph.
inline bool isGlyphTable(const QByteArray& data, qint64 sectionSize) {
    if (data.size() < 16)
        return false;
    const quint32 count = u32At(data, 8);
    if (count == 0 || count > 65536 || sectionSize != 12 + 40 * static_cast<qint64>(count))
        return false;
    float size = 0, second = 0;
    const quint32 a = u32At(data, 0), b = u32At(data, 4);
    std::memcpy(&size, &a, 4);
    std::memcpy(&second, &b, 4);
    return size > 0 && size < 1024 && second > 0 && second < 1024;
}

/// AFL Live (PC, stored big endian): clip count, then per clip a length prefixed name and its f32 length in seconds,
/// followed by the joint names ("Crowd_Std_05A", "breakthrough", "flogger_distractIdleSource", "Rn_KickDPLong90_L_01").
inline bool isBigEndianClipPack(const QByteArray& data) {
    if (data.size() < 16)
        return false;
    const quint32 count = qFromBigEndian<quint32>(data.constData());
    const quint32 nameLength = qFromBigEndian<quint32>(data.constData() + 4);
    if (count == 0 || count > 100000 || nameLength < 2 || nameLength > 255 || data.size() < 8 + qint64(nameLength) + 4)
        return false;
    for (qsizetype k = 8; k < 8 + qsizetype(nameLength); ++k) {
        const uchar ch = static_cast<uchar>(data[k]);
        if (ch < 0x20 || ch > 0x7E)
            return false;
    }
    float seconds = 0;
    const quint32 bits = qFromBigEndian<quint32>(data.constData() + 8 + nameLength);
    std::memcpy(&seconds, &bits, 4);
    return seconds > 0.0f && seconds < 100000.0f;
}

// ---- Big endian (console) formats -------------------------------------------------------------------------------

inline quint32 be32At(const QByteArray& data, qint64 offset) {
    return offset >= 0 && data.size() >= offset + 4 ? qFromBigEndian<quint32>(data.constData() + offset) : 0;
}

/// count, count * (hash, offset, size); the chunks follow the table back to back and start with FA DE.
inline bool isMeshPack(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = be32At(data, 0);
    if (count == 0 || count > 100000 || data.size() < 4 + 12 * qint64(count))
        return false;
    qint64 next = 4 + 12 * qint64(count);
    for (quint32 k = 0; k < count; ++k) {
        if (be32At(data, 8 + 12 * qint64(k)) != next)
            return false;
        next += be32At(data, 12 + 12 * qint64(k));
    }
    const qint64 first = 4 + 12 * qint64(count);
    return next <= sectionSize && data.size() >= first + 2 && static_cast<uchar>(data[first]) == 0xFA
           && static_cast<uchar>(data[first + 1]) == 0xDE;
}

/// count, 5 ascending section offsets, then the header size (8 * count + 32, rounded down to 16).
inline bool isClipPack(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = be32At(data, 0);
    if (count == 0 || count > 100000 || data.size() < 28)
        return false;
    for (int k = 0; k < 4; ++k)
        if (be32At(data, 4 + 4 * k) >= be32At(data, 8 + 4 * k))
            return false;
    return be32At(data, 20) <= sectionSize && be32At(data, 24) == ((8 * count + 32) & ~15u);
}

/// count, 6 ascending section offsets, 0; the first section starts with 0x20.
inline bool isKeyframedAnim(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = be32At(data, 0);
    if (count == 0 || count > 100000 || data.size() < 32 || be32At(data, 28) != 0)
        return false;
    for (int k = 0; k < 5; ++k)
        if (be32At(data, 4 + 4 * k) >= be32At(data, 8 + 4 * k))
            return false;
    const quint32 first = be32At(data, 4);
    if (be32At(data, 24) >= sectionSize)
        return false;
    return first + 4 > quint32(data.size()) || be32At(data, first) == 0x20;   // Checked when it was read.
}

inline bool isCollisionBE(const QByteArray& data, qint64 sectionSize) {
    if (data.size() < 28)
        return false;
    const quint32 dataOffset = be32At(data, 0), count = be32At(data, 4);
    const quint32 a = be32At(data, 8), b = be32At(data, 12), c = be32At(data, 16);
    return count >= 1 && count <= 4096 && a == dataOffset + 4 * count && dataOffset < a && a < b && b < c
           && c <= sectionSize && be32At(data, 24) == 0x18;
}

/// Same layout as isGlyphTable(); the second value (an offset) can be negative here.
inline bool isGlyphTableBE(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = be32At(data, 8);
    if (count == 0 || count > 65536 || sectionSize != 12 + 40 * qint64(count))
        return false;
    float size = 0, second = 0;
    const quint32 a = be32At(data, 0), b = be32At(data, 4);
    std::memcpy(&size, &a, 4);
    std::memcpy(&second, &b, 4);
    return size > 0 && size < 1024 && second > -1024 && second < 1024;
}

/// count, then count * 60 (DBC14) or 92 (RLL3) byte records, each starting with a texture name.
inline bool isTextureNameListBE(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = be32At(data, 0);
    if (count == 0 || count > 100000 || data.size() < 5)
        return false;
    if (sectionSize != 4 + 60 * qint64(count) && sectionSize != 4 + 92 * qint64(count))
        return false;
    const uchar ch = static_cast<uchar>(data[4]);
    return ch > 0x20 && ch < 0x7F;
}

/// count, then count ascending offsets that point past the table (RLL2 player names, DBC14 databases).
inline bool isOffsetTable(const QByteArray& data, qint64 sectionSize) {
    const quint32 count = be32At(data, 0);
    if (count < 2 || count > 100000 || data.size() < 4 + 4 * qint64(count))
        return false;
    if (be32At(data, 4) < 4 + 4 * count)
        return false;
    for (quint32 k = 1; k < count; ++k)
        if (be32At(data, 4 * qint64(k)) >= be32At(data, 4 + 4 * qint64(k)))
            return false;
    return be32At(data, 4 * qint64(count)) < sectionSize;
}

inline bool isShortText(const QByteArray& data, qint64 sectionSize) {
    if (data.isEmpty() || sectionSize > 256 || data.size() < sectionSize)
        return false;
    for (qsizetype k = 0; k < sectionSize; ++k) {
        const uchar ch = static_cast<uchar>(data[k]);
        if ((ch < 0x20 || ch > 0x7E) && ch != '\n' && ch != '\r' && ch != '\t')
            return false;
    }
    return true;
}

} // namespace detail

namespace detail {

/// Big endian (console) layouts, tried after every little endian rule.
inline std::optional<Match> identifyConsole(const QByteArray& data, qint64 sectionSize) {
    if (isMeshPack(data, sectionSize))
        return Match{QStringLiteral("model\\mesh_packs"), QStringLiteral("meshpack")};
    if (isClipPack(data, sectionSize))
        return Match{QStringLiteral("anim\\clip_packs"), QStringLiteral("clippack")};
    if (isKeyframedAnim(data, sectionSize))
        return Match{QStringLiteral("anim\\keyframed"), QStringLiteral("kfanim")};
    if (isCollisionBE(data, sectionSize))
        return Match{QStringLiteral("collision"), QStringLiteral("col")};
    if (isGlyphTableBE(data, sectionSize))
        return Match{QStringLiteral("font\\glyphs"), QStringLiteral("glyphs")};
    for (const qsizetype at : {qsizetype(0), qsizetype(4)}) {
        const quint32 tag = be32At(data, at);
        if (tag == 0x80081350u || tag == 0x80081351u)
            return Match{QStringLiteral("objects"), QStringLiteral("obj")};
    }
    if (isTextureNameListBE(data, sectionSize))
        return Match{QStringLiteral("texture_names"), QStringLiteral("names")};
    if (isOffsetTable(data, sectionSize))
        return Match{QStringLiteral("data\\tables"), QStringLiteral("tbl")};
    if (sectionSize == 1200)
        return Match{QStringLiteral("data\\grids"), QStringLiteral("grid")};
    return std::nullopt;
}

} // namespace detail

/// Formats without a magic number of their own, recognised by their content (see the tables at the top).
/// V4 runs these right after its signatures; V1 / V2 only after their own signatures and the index buffer check,
/// so they never take over a file that already has a type. bigEndian: console builds, which also get
/// detail::identifyConsole() once every little endian rule failed.
inline std::optional<Match> identifyCommon(const QByteArray& data, qint64 sectionSize, bool bigEndian = false) {
    if (data.size() < 4)
        return std::nullopt;
    if (sectionSize < 0)
        sectionSize = data.size();

    const QByteArray head = data.left(16384);
    const quint32 first = detail::u32At(data, 0);

    if (data.startsWith("MIDI"))
        return Match{QStringLiteral("sound\\midi"), QStringLiteral("midi")};
    if (first == 0x40 && head.contains(".lua"))
        return Match{QStringLiteral("body_shapes"), QStringLiteral("bshp")};
    if (data.left(256).contains("_crowdnodes"))
        return Match{QStringLiteral("crowd\\nodes"), QStringLiteral("crowd")};
    if (data.mid(8, 10) == "Commentary" || head.contains("subcategorydefault"))
        return Match{QStringLiteral("sound\\commentary"), QStringLiteral("cmtry")};
    if (head.contains("|S:") && head.contains("V:"))
        return Match{QStringLiteral("shader\\vertex_formats"), QStringLiteral("vfmt")};
    for (const qsizetype at : {qsizetype(0), qsizetype(4)}) {
        const quint32 tag = detail::u32At(data, at);
        if (tag == 0x80081350u || tag == 0x80081351u)
            return Match{QStringLiteral("objects"), QStringLiteral("obj")};
    }
    if (detail::isAnimationPack(data, sectionSize))
        return Match{QStringLiteral("anim\\packs"), QStringLiteral("anims")};
    if (detail::isCollision(data, sectionSize))
        return Match{QStringLiteral("collision"), QStringLiteral("col")};
    if (detail::isStringTable(data))
        return Match{QStringLiteral("text\\strings"), QStringLiteral("str")};
    if (detail::isShortText(data, sectionSize))
        return Match{QStringLiteral("text"), QStringLiteral("txt")};

    // Rugby League Live 3 / 4 era formats.
    if (data.startsWith("MOB!"))
        return Match{QStringLiteral("mob"), QStringLiteral("mob")};
    if (detail::isTextureNameList(data, sectionSize))
        return Match{QStringLiteral("texture_names"), QStringLiteral("names")};
    if (detail::isGlyphTable(data, sectionSize))
        return Match{QStringLiteral("font\\glyphs"), QStringLiteral("glyphs")};
    if (data.left(65536).contains("FLATLUADATA_IMPORT") || data.left(65536).contains("script-config/"))
        return Match{QStringLiteral("scripts"), QStringLiteral("luadata")};
    if (data.left(4096).contains("AnimU_"))
        return Match{QStringLiteral("anim\\sets"), QStringLiteral("animset")};

    // AFL Live era formats.
    if (detail::isBigEndianClipPack(data))
        return Match{QStringLiteral("anim\\clips"), QStringLiteral("clips")};

    if (bigEndian)
        return detail::identifyConsole(data, sectionSize);
    return std::nullopt;
}

/// `sectionSize`: the main section's uncompressed size (offsets in the header are checked against it);
/// -1 when unknown, then the data size is used.
inline std::optional<Match> identifyV4(const QByteArray& data, qint64 sectionSize = -1) {
    if (data.size() < 4)
        return std::nullopt;
    if (sectionSize < 0)
        sectionSize = data.size();

    switch (qFromLittleEndian<quint32>(data.constData())) {
    case kSkinnedModel:
        return Match{QStringLiteral("model\\skinned"), QStringLiteral("mdl")};
    case kSkinnedModelData:
        return Match{QStringLiteral("model\\skinned"), QStringLiteral("mdat")};
    case kStaticMesh:
        return Match{QStringLiteral("model\\static"), QStringLiteral("mesh")};
    case kCinematicAnim:
        return Match{QStringLiteral("anim\\cinematic"), QStringLiteral("anim")};
    case kTextureList:
        return Match{QStringLiteral("txpl"), QStringLiteral("txpl")};
    case kHjOffsets:
        if (data.contains(".lua"))
            return Match{QStringLiteral("hj_offsets"), QStringLiteral("hjo")};
        break;
    default:
        break;
    }

    return identifyCommon(data, sectionSize);
}

// ---------------------------------------------------------------------------
// V1 / V2 (found by scanning Rugby League Live 3, PS3 and PC)
//
//   78 56 34 12  skinned model                                         -> model\skinned\N.mdl
//   7F 13 80 24  skinned model data                                    -> model\skinned\N.mdat
//   FE DC BA 01  static mesh (crowd sprites, stadium parts); on PS3 the vram section is the vertex data
//                                                                      -> model\static\N.mesh
//   23 45 67 89  material (shader name + col_map / nrm_map texture paths) -> material\N.mat
//   99 CC 22 88  cinematic camera / light animation                    -> anim\cinematic\N.anim
//   33 EE 44 AA  the same with a small header in front (TableTop Cricket) -> anim\cinematic\N.anim
//   "\x89PNG" / " fMB" compressed: PNG images / BMF fonts (console magic read through canonicalMagic())
//   (no header)  16 bit triangle index buffer, see looksLikeIndexBuffer() -> model\index\N.idx
//
// The signatures are checked in both byte orders, because console (big endian) and PC (little endian)
// builds store them differently.
// ---------------------------------------------------------------------------

constexpr quint32 kStaticMeshV1 = 0xFEDCBA01u; // Older layout of kStaticMesh.
constexpr quint32 kMaterialV1   = 0x23456789u;
constexpr quint32 kNodeAnimV1   = 0x33EE44AAu; // Wraps a kCinematicAnim (at +4): camera / node channels (TTC).

/// The legacy (C#) rule reads the magic of a compressed section little endian, so a console file's big endian
/// magic comes out byte swapped (TTC PS3 fonts: " fMB"). Returns the FileType value in either byte order.
inline quint32 canonicalMagic(quint32 magic) {
    using FT = CoreDefs::FileType;
    const auto known = [](quint32 value) {
        switch (static_cast<FT>(value)) {
        case FT::BMF: case FT::BSB: case FT::BSB_BE: case FT::BANK: case FT::FEV1: case FT::FEV1_BE:
        case FT::FSB: case FT::FSB5_LE: case FT::FSB4: case FT::FSB4_BE: case FT::PM3M: case FT::M3MP:
        case FT::MINI: case FT::MOB: case FT::PNG: case FT::KPXT: case FT::TXPK: case FT::WiseBNK: case FT::WAV:
            return true;
        default:
            return false;
        }
    };
    if (known(magic))
        return magic;
    const quint32 swapped = qbswap(magic);
    return known(swapped) ? swapped : magic;
}

inline std::optional<Match> identifyV1(const QByteArray& data) {
    if (data.size() < 4)
        return std::nullopt;

    const quint32 magics[] = {qFromBigEndian<quint32>(data.constData()), qFromLittleEndian<quint32>(data.constData())};
    for (const quint32 magic : magics) {
        switch (magic) {
        case kSkinnedModel:
            return Match{QStringLiteral("model\\skinned"), QStringLiteral("mdl")};
        case kSkinnedModelData:
            return Match{QStringLiteral("model\\skinned"), QStringLiteral("mdat")};
        case kStaticMeshV1:
            return Match{QStringLiteral("model\\static"), QStringLiteral("mesh")};
        case kMaterialV1:
            return Match{QStringLiteral("material"), QStringLiteral("mat")};
        case kCinematicAnim:
        case kNodeAnimV1:
            return Match{QStringLiteral("anim\\cinematic"), QStringLiteral("anim")};
        default:
            break;
        }
    }
    return std::nullopt;
}

/// Largest section looksLikeIndexBuffer() is asked about (the whole section has to be read for it).
constexpr qint64 kIndexBufferMaxBytes = 4 * 1024 * 1024;

/// V1 / V2 index buffers are bare arrays of 16 bit triangle indices, so they're recognised by their shape:
/// a whole number of triangles, every index smaller than the number of indices, and under 5 % degenerate
/// triangles. Only used as a last resort, after every format with a real signature was ruled out.
/// On RLL3 (PC) this matched 1504 of the 1507 headerless files; the 3 it rejected were a version string and two
/// unrelated blobs.
inline bool looksLikeIndexBuffer(const QByteArray& data, bool bigEndian) {
    if (data.size() < 6 || data.size() % 6 != 0 || data.size() > kIndexBufferMaxBytes)
        return false;

    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype count = data.size() / 2;
    qsizetype degenerate = 0;

    for (qsizetype t = 0; t < count; t += 3) {
        quint16 v[3];
        for (int k = 0; k < 3; ++k) {
            const uchar* at = p + 2 * (t + k);
            v[k] = bigEndian ? qFromBigEndian<quint16>(at) : qFromLittleEndian<quint16>(at);
            if (v[k] >= count)
                return false;
        }
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
            ++degenerate;
    }
    return degenerate * 20 < count / 3;
}

inline Match indexBufferMatch() {
    return Match{QStringLiteral("model\\index"), QStringLiteral("idx")};
}

/// Mapping path for a match: <folder>\<index>.<extension>
inline QString mappedPath(const Match& match, int index) {
    return match.folder + QLatin1Char('\\') + QString::number(index) + QLatin1Char('.') + match.extension;
}

} // namespace AssetSignatures

#endif // ASSET_SIGNATURES_H
