#include "x360_texture.h"

#include "package_stream.h"

#include <QBuffer>
#include <QList>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace X360 {

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Formats
// ---------------------------------------------------------------------------------------------------------------

enum : quint32 {
    k8 = 2,
    k1_5_5_5 = 3,
    k5_6_5 = 4,
    k8_8_8_8 = 6,
    k8_8 = 10,
    k4_4_4_4 = 15,
    kDxt1 = 18,
    kDxt2_3 = 19,
    kDxt4_5 = 20,
    kDxn = 49,
    kDxt5A = 59,
};

enum : quint32 { kEndianNone = 0, kEndian8in16 = 1, kEndian8in32 = 2, kEndian16in32 = 3 };

struct FormatInfo {
    quint32 format;
    const char* name;
    quint32 blockDim;       // 4 for block compressed formats, 1 otherwise.
    quint32 bytesPerBlock;
    // DDS pixel format: a FourCC, or flags + bit count + masks.
    const char* fourCC;
    quint32 ddsFlags;
    quint32 bitCount;
    quint32 masks[4];       // R, G, B, A
};

constexpr quint32 kDdpfAlphaPixels = 0x1;
constexpr quint32 kDdpfFourCC = 0x4;
constexpr quint32 kDdpfRgb = 0x40;
constexpr quint32 kDdpfLuminance = 0x20000;

const FormatInfo kFormats[] = {
    {kDxt1,    "DXT1",     4, 8,  "DXT1", kDdpfFourCC, 0, {}},
    {kDxt2_3,  "DXT3",     4, 16, "DXT3", kDdpfFourCC, 0, {}},
    {kDxt4_5,  "DXT5",     4, 16, "DXT5", kDdpfFourCC, 0, {}},
    {kDxt5A,   "ATI1",     4, 8,  "ATI1", kDdpfFourCC, 0, {}},
    {kDxn,     "ATI2",     4, 16, "ATI2", kDdpfFourCC, 0, {}},
    {k8_8_8_8, "A8R8G8B8", 1, 4,  nullptr, kDdpfRgb | kDdpfAlphaPixels, 32,
     {0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u}},
    {k5_6_5,   "R5G6B5",   1, 2,  nullptr, kDdpfRgb, 16, {0xF800u, 0x07E0u, 0x001Fu, 0}},
    {k1_5_5_5, "A1R5G5B5", 1, 2,  nullptr, kDdpfRgb | kDdpfAlphaPixels, 16, {0x7C00u, 0x03E0u, 0x001Fu, 0x8000u}},
    {k4_4_4_4, "A4R4G4B4", 1, 2,  nullptr, kDdpfRgb | kDdpfAlphaPixels, 16, {0x0F00u, 0x00F0u, 0x000Fu, 0xF000u}},
    {k8,       "L8",       1, 1,  nullptr, kDdpfLuminance, 8, {0xFFu, 0, 0, 0}},
    {k8_8,     "A8L8",     1, 2,  nullptr, kDdpfLuminance | kDdpfAlphaPixels, 16, {0x00FFu, 0, 0, 0xFF00u}},
};

const FormatInfo* findFormat(quint32 format) {
    for (const FormatInfo& info : kFormats)
        if (info.format == format)
            return &info;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Layout (Xenia texture_util.cc / texture_address.h)
// ---------------------------------------------------------------------------------------------------------------

constexpr quint32 kTile = 32;              // Tiles are 32 x 32 blocks.
constexpr quint32 kLevelAlignment = 4096;  // Every stored level starts on a 4 KB boundary.

quint32 log2Ceil(quint32 value) {
    quint32 result = 0;
    while ((quint32(1) << result) < value)
        ++result;
    return result;
}
quint32 nextPow2(quint32 value) { return quint32(1) << log2Ceil(value); }
quint32 alignUp(quint32 value, quint32 alignment) { return (value + alignment - 1) / alignment * alignment; }
quint32 blocks(quint32 texels, quint32 blockDim) { return std::max<quint32>(1, (texels + blockDim - 1) / blockDim); }

/// Level from which on the mips sit together in one tile (only when the header has packed mips).
quint32 packedMipLevel(quint32 width, quint32 height) {
    const quint32 log2Size = log2Ceil(std::min(width, height));
    return log2Size > 4 ? log2Size - 4 : 0;
}

/// Block position of a mip inside the packed mip tail (GetPackedMipOffset, 2D only).
void packedMipOffset(quint32 width, quint32 height, quint32 mip, quint32 blockDim, quint32& x, quint32& y) {
    const quint32 log2Width = log2Ceil(width), log2Height = log2Ceil(height);
    const quint32 log2Size = std::min(log2Width, log2Height);
    const quint32 packedBase = log2Size > 4 ? log2Size - 4 : 0;
    const quint32 packedMip = mip - packedBase;

    if (packedMip < 3) {
        x = log2Width > log2Height ? 0 : (16u >> packedMip);
        y = log2Width > log2Height ? (16u >> packedMip) : 0;
    } else if (log2Width > log2Height) {
        x = (quint32(1) << (log2Width - packedBase)) >> (packedMip - 2);
        y = 0;
    } else {
        x = 0;
        y = (quint32(1) << (log2Height - packedBase)) >> (packedMip - 2);
    }
    x /= blockDim;
    y /= blockDim;
}

/// Byte offset of block (x, y) in a tiled 2D surface whose row pitch is `pitch` blocks (a multiple of 32).
quint32 tiledOffset(quint32 x, quint32 y, quint32 pitch, quint32 bytesPerBlockLog2) {
    const quint32 outer = ((y >> 5) * (pitch >> 5) + (x >> 5)) << 6;
    const quint32 inner = (((y >> 1) & 7) << 3) | (x & 7);
    const quint32 outerInner = (outer | inner) << bytesPerBlockLog2;
    const quint32 bank = (y >> 4) & 1;
    const quint32 pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
    return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (outerInner & 0xF) | (((outerInner >> 4) & 1) << 5)
           | (((outerInner >> 5) & 7) << 8) | ((outerInner >> 8) << 12);
}

/// Where one mip level's blocks are stored.
struct Placement {
    quint32 offset = 0;       // Bytes from the start of the texture data.
    quint32 pitchBlocks = 0;  // Row pitch of the surface the level is addressed in (multiple of 32).
    quint32 x = 0, y = 0;     // Block position inside that surface (packed mips).
    quint32 widthBlocks = 0, heightBlocks = 0;
};

struct Layout {
    QList<Placement> levels;
    quint32 mipOffset = 0;    // Where the mips start (header mip address).
    quint32 totalSize = 0;    // Bytes the texture data takes (for building it).
};

/// basePitchTexels: the header pitch; mipOffset: the header mip address (0 = right after the base level).
/// A texture whose smallest side is 16 texels or less keeps every level in its base tile (the whole mip chain is
/// packed): the games store those with mip address 0 and no data after the base tile.
/// faces: 6 for cube maps. Every level stores the faces one after the other (each face padded to 4 KB);
/// the placements come out face by face (all levels of face 0, then face 1, ...), the order a .dds wants.
Layout layoutFor(const FormatInfo& info, quint32 width, quint32 height, quint32 levelCount, bool packed,
                 quint32 basePitchTexels, quint32 mipOffset, quint32 faces = 1) {
    Layout layout;
    const quint32 bd = info.blockDim, bpb = info.bytesPerBlock;
    const quint32 packedLevel = packed ? packedMipLevel(width, height) : 0xFFFFFFFFu;

    // Stored size of mip k (always from the power of two sizes, padded to whole tiles).
    const auto mipStride = [&](quint32 k) {
        const quint32 w = std::max<quint32>(1, nextPow2(width) >> k), h = std::max<quint32>(1, nextPow2(height) >> k);
        return alignUp(alignUp(blocks(w, bd), kTile) * alignUp(blocks(h, bd), kTile) * bpb, kLevelAlignment);
    };

    Placement base;
    base.pitchBlocks = alignUp(blocks(std::max(basePitchTexels, width), bd), kTile);
    base.widthBlocks = blocks(width, bd);
    base.heightBlocks = blocks(height, bd);
    if (packedLevel == 0)
        packedMipOffset(width, height, 0, bd, base.x, base.y);

    const quint32 baseSize = alignUp(base.pitchBlocks * alignUp(base.heightBlocks, kTile) * bpb, kLevelAlignment);
    const bool mipsInBaseTile = packedLevel == 0;
    layout.mipOffset = levelCount > 1 && !mipsInBaseTile ? (mipOffset ? mipOffset : baseSize * faces) : 0;
    layout.totalSize = baseSize * faces;

    // Mip levels of face 0 (the other faces follow each level's face 0 at `stride` steps).
    QList<Placement> mips;
    QList<quint32> mipStrides;
    quint32 mipEnd = layout.mipOffset;
    for (quint32 level = 1; level < levelCount; ++level) {
        const quint32 stored = std::min(level, packedLevel);   // Packed mips all live in the tail's level.
        Placement p;
        p.offset = layout.mipOffset;
        for (quint32 k = 1; k < stored; ++k)
            p.offset += mipStride(k) * faces;
        p.pitchBlocks = alignUp(blocks(std::max<quint32>(1, nextPow2(width) >> stored), bd), kTile);
        p.widthBlocks = blocks(std::max<quint32>(1, width >> level), bd);
        p.heightBlocks = blocks(std::max<quint32>(1, height >> level), bd);
        if (level >= packedLevel)
            packedMipOffset(width, height, level, bd, p.x, p.y);
        mips.append(p);
        mipStrides.append(mipsInBaseTile ? baseSize : mipStride(stored));
        if (!mipsInBaseTile)
            mipEnd = std::max(mipEnd, p.offset + mipStride(stored) * faces);
    }

    for (quint32 face = 0; face < faces; ++face) {
        Placement b = base;
        b.offset += face * baseSize;
        layout.levels.append(b);
        for (qsizetype k = 0; k < mips.size(); ++k) {
            Placement p = mips[k];
            p.offset += face * mipStrides[k];
            layout.levels.append(p);
        }
    }
    layout.totalSize = std::max(layout.totalSize, mipEnd);
    return layout;
}

quint32 blockAddress(const Placement& p, quint32 x, quint32 y, quint32 bytesPerBlock, bool tiled) {
    const quint32 bx = p.x + x, by = p.y + y;
    if (!tiled)
        return p.offset + by * p.pitchBlocks * bytesPerBlock + bx * bytesPerBlock;
    quint32 log2 = 0;
    while ((quint32(1) << log2) < bytesPerBlock)
        ++log2;
    return p.offset + tiledOffset(bx, by, p.pitchBlocks, log2);
}

/// Xbox 360 <-> PC byte order (the swap is its own inverse).
void swapEndian(QByteArray& data, quint32 endian) {
    char* p = data.data();
    const qsizetype n = data.size();
    switch (endian) {
    case kEndian8in16:
        for (qsizetype i = 0; i + 1 < n; i += 2)
            std::swap(p[i], p[i + 1]);
        break;
    case kEndian8in32:
        for (qsizetype i = 0; i + 3 < n; i += 4) {
            std::swap(p[i], p[i + 3]);
            std::swap(p[i + 1], p[i + 2]);
        }
        break;
    case kEndian16in32:
        for (qsizetype i = 0; i + 3 < n; i += 4) {
            std::swap(p[i], p[i + 2]);
            std::swap(p[i + 1], p[i + 3]);
        }
        break;
    default:
        break;
    }
}

QByteArray ddsHeader(const FormatInfo& info, quint32 width, quint32 height, quint32 levels, bool cube) {
    QByteArray header(128, '\0');
    uchar* p = reinterpret_cast<uchar*>(header.data());
    const auto put = [p](int at, quint32 value) { qToLittleEndian(value, p + at); };

    const quint32 topSize = blocks(width, info.blockDim) * blocks(height, info.blockDim) * info.bytesPerBlock;
    put(0, 0x20534444);                                                  // "DDS "
    put(4, 124);
    put(8, 0x1007 | (info.fourCC ? 0x80000 : 0x8) | (levels > 1 ? 0x20000 : 0));
    put(12, height);
    put(16, width);
    put(20, info.fourCC ? topSize : blocks(width, 1) * info.bytesPerBlock);   // Linear size / pitch.
    put(28, levels);
    put(76, 32);
    put(80, info.ddsFlags);
    if (info.fourCC) {
        std::memcpy(p + 84, info.fourCC, 4);
    } else {
        put(88, info.bitCount);
        for (int k = 0; k < 4; ++k)
            put(92 + 4 * k, info.masks[k]);
    }
    put(108, 0x1000 | (levels > 1 ? 0x400008 : 0) | (cube ? 0x8 : 0));    // TEXTURE (| COMPLEX | MIPMAP)
    if (cube)
        put(112, 0xFE00);                                                // CUBEMAP + all six faces.
    return header;
}

void setError(QString* error, const QString& message) {
    if (error)
        *error = message;
}

// ---------------------------------------------------------------------------------------------------------------
// DDS input
// ---------------------------------------------------------------------------------------------------------------

struct DdsInput {
    const FormatInfo* format = nullptr;
    quint32 width = 0, height = 0, levels = 1, faces = 1;
    QByteArray data;   // Every level, PC byte order (32 bit: B G R A).
};

std::optional<DdsInput> parseDds(const QByteArray& dds, QString* error) {
    if (dds.size() < 128 || !dds.startsWith("DDS ")) {
        setError(error, QStringLiteral("not a valid DDS file."));
        return std::nullopt;
    }
    const auto u32 = [&](qsizetype at) { return qFromLittleEndian<quint32>(dds.constData() + at); };

    DdsInput in;
    in.height = u32(12);
    in.width = u32(16);
    in.levels = (u32(8) & 0x20000) && u32(28) > 0 ? u32(28) : 1;
    const quint32 pfFlags = u32(80);
    const QByteArray fourCC = dds.mid(84, 4);
    qsizetype dataStart = 128;
    bool swapRedBlue = false, forceAlpha = false;
    if ((u32(112) & 0xFE00) == 0xFE00)
        in.faces = 6;                                    // Cube map with all six faces.

    if (pfFlags & kDdpfFourCC) {
        if (fourCC == "DX10") {
            if (dds.size() < 148) {
                setError(error, QStringLiteral("the DDS DX10 header is truncated."));
                return std::nullopt;
            }
            dataStart = 148;
            if (u32(136) & 0x4)
                in.faces = 6;                            // D3D10_RESOURCE_MISC_TEXTURECUBE.
            switch (u32(128)) {
            case 70: case 71: case 72: in.format = findFormat(kDxt1); break;
            case 73: case 74: case 75: in.format = findFormat(kDxt2_3); break;
            case 76: case 77: case 78: in.format = findFormat(kDxt4_5); break;
            case 79: case 80: in.format = findFormat(kDxt5A); break;
            case 82: case 83: in.format = findFormat(kDxn); break;
            case 87: case 91: in.format = findFormat(k8_8_8_8); break;
            case 88: case 93: in.format = findFormat(k8_8_8_8); forceAlpha = true; break;
            case 28: case 29: in.format = findFormat(k8_8_8_8); swapRedBlue = true; break;
            default: break;
            }
        } else if (fourCC == "DXT1") {
            in.format = findFormat(kDxt1);
        } else if (fourCC == "DXT2" || fourCC == "DXT3") {
            in.format = findFormat(kDxt2_3);
        } else if (fourCC == "DXT4" || fourCC == "DXT5") {
            in.format = findFormat(kDxt4_5);
        } else if (fourCC == "ATI1" || fourCC == "BC4U") {
            in.format = findFormat(kDxt5A);
        } else if (fourCC == "ATI2" || fourCC == "BC5U") {
            in.format = findFormat(kDxn);
        }
    } else if ((pfFlags & kDdpfRgb) && u32(88) == 32) {
        in.format = findFormat(k8_8_8_8);
        swapRedBlue = u32(92) == 0x000000FFu;           // R in the low byte: R G B A in memory.
        forceAlpha = !(pfFlags & kDdpfAlphaPixels);     // X8R8G8B8.
    }

    if (!in.format) {
        setError(error, QStringLiteral("the DDS format can't be imported for Xbox 360 (save it as DXT1, DXT3, DXT5, "
                                       "ATI1, ATI2 or 32 bit A8R8G8B8)."));
        return std::nullopt;
    }
    if (in.width == 0 || in.height == 0 || in.width > 8192 || in.height > 8192) {
        setError(error, QStringLiteral("the texture size %1 x %2 isn't supported (1 - 8192).").arg(in.width).arg(in.height));
        return std::nullopt;
    }

    // Keep the levels the file really holds (a short file loses its smallest mips, never the top level).
    const FormatInfo& f = *in.format;
    in.levels = std::clamp<quint32>(in.levels, 1, log2Ceil(std::max(nextPow2(in.width), nextPow2(in.height))) + 1);
    // Bytes of one face's mip chain with `levels` levels.
    const auto chainSize = [&](quint32 levels) {
        qint64 size = 0;
        for (quint32 level = 0; level < levels; ++level)
            size += qint64(blocks(std::max<quint32>(1, in.width >> level), f.blockDim))
                    * blocks(std::max<quint32>(1, in.height >> level), f.blockDim) * f.bytesPerBlock;
        return size;
    };
    if (dataStart + chainSize(in.levels) * in.faces > dds.size()) {
        // A short file keeps the levels it really holds (a cube map needs all of its faces).
        quint32 complete = 0;
        while (complete < in.levels && dataStart + chainSize(complete + 1) * in.faces <= dds.size())
            ++complete;
        if (complete == 0 || in.faces > 1) {
            setError(error, QStringLiteral("the DDS file is truncated."));
            return std::nullopt;
        }
        in.levels = complete;
    }
    in.data = dds.mid(dataStart, chainSize(in.levels) * in.faces);

    if (swapRedBlue || forceAlpha) {
        char* p = in.data.data();
        for (qsizetype i = 0; i + 3 < in.data.size(); i += 4) {
            if (swapRedBlue)
                std::swap(p[i], p[i + 2]);
            if (forceAlpha)
                p[i + 3] = char(0xFF);
        }
    }
    return in;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------------------------------------------

void TextureHeader::read(PackageStream& in) {
    in.setEndianness(PackageStream::Big);
    for (quint32& dword : dwords)
        dword = in.readUInt32();
}

void TextureHeader::write(PackageStream& out) const {
    out.setEndianness(PackageStream::Big);
    for (const quint32 dword : dwords)
        out.writeUInt32(dword);
}

QByteArray TextureHeader::toBytes() const {
    QByteArray bytes(kSize, '\0');
    for (int k = 0; k < 13; ++k)
        qToBigEndian(dwords[k], bytes.data() + 4 * k);
    return bytes;
}

QString formatName(quint32 format) {
    if (const FormatInfo* info = findFormat(format))
        return QString::fromLatin1(info->name);
    return QStringLiteral("Xbox 360 format %1").arg(format);
}

// ---------------------------------------------------------------------------------------------------------------
// Xbox 360 -> DDS
// ---------------------------------------------------------------------------------------------------------------

std::optional<QByteArray> toDds(const TextureHeader& header, const QByteArray& data, QString* error) {
    const FormatInfo* info = findFormat(header.format());
    if (!info) {
        setError(error, QStringLiteral("%1 can't be saved as a .dds.").arg(formatName(header.format())));
        return std::nullopt;
    }

    const quint32 width = header.width(), height = header.height();
    const quint32 maxLevels = log2Ceil(std::max(nextPow2(width), nextPow2(height))) + 1;
    quint32 levels = std::min(header.levelCount(), maxLevels);
    // Mip address 0 only makes sense when every level fits in the base tile; otherwise there are no mips.
    if (levels > 1 && header.mipOffset() == 0 && !(header.hasPackedMips() && packedMipLevel(width, height) == 0))
        levels = 1;
    if (header.dimension() == 2) {
        setError(error, QStringLiteral("3D (volume) textures aren't supported."));
        return std::nullopt;
    }
    const quint32 faces = header.faceCount();
    const Layout layout = layoutFor(*info, width, height, levels, header.hasPackedMips(), header.pitchTexels(),
                                    header.mipOffset(), faces);

    QByteArray source = data;
    swapEndian(source, header.endian());

    const quint32 bpb = info->bytesPerBlock;
    QByteArray out = ddsHeader(*info, width, height, levels, faces == 6);
    const qsizetype levelsStart = out.size();
    qsizetype total = 0;
    for (const Placement& p : layout.levels)
        total += qsizetype(p.widthBlocks) * p.heightBlocks * bpb;
    out.resize(levelsStart + total);   // Blocks outside the stored data stay zero.
    std::memset(out.data() + levelsStart, 0, total);

    char* dst = out.data() + levelsStart;
    for (const Placement& p : layout.levels) {
        for (quint32 y = 0; y < p.heightBlocks; ++y) {
            for (quint32 x = 0; x < p.widthBlocks; ++x, dst += bpb) {
                const quint32 at = blockAddress(p, x, y, bpb, header.isTiled());
                if (qsizetype(at) + bpb <= source.size())
                    std::memcpy(dst, source.constData() + at, bpb);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// DDS -> Xbox 360
// ---------------------------------------------------------------------------------------------------------------

std::optional<ImportedTexture> fromDds(const QByteArray& dds, QString* error) {
    const std::optional<DdsInput> in = parseDds(dds, error);
    if (!in)
        return std::nullopt;

    const FormatInfo& info = *in->format;
    const bool packed = in->levels > 1;
    const quint32 pitchTexels = alignUp(blocks(in->width, info.blockDim), kTile) * info.blockDim;
    const Layout layout = layoutFor(info, in->width, in->height, in->levels, packed, pitchTexels, 0, in->faces);

    ImportedTexture texture;
    texture.data = QByteArray(layout.totalSize, '\0');

    const quint32 bpb = info.bytesPerBlock;
    const char* src = in->data.constData();
    for (const Placement& p : layout.levels) {
        for (quint32 y = 0; y < p.heightBlocks; ++y) {
            for (quint32 x = 0; x < p.widthBlocks; ++x, src += bpb) {
                const quint32 at = blockAddress(p, x, y, bpb, true);
                if (qsizetype(at) + bpb <= texture.data.size())
                    std::memcpy(texture.data.data() + at, src, bpb);
            }
        }
    }

    const quint32 endian = info.bytesPerBlock == 4 && info.blockDim == 1 ? kEndian8in32 : kEndian8in16;
    swapEndian(texture.data, endian);

    // Same values the games' own Xbox 360 textures carry.
    auto& d = texture.header.dwords;
    d = {};
    d[0] = TextureHeader::kCommonTexture;
    d[1] = 1;                                                         // Reference count.
    d[5] = 0xFFFF0000u;                                               // Base flush.
    d[6] = 0xFFFF0000u;                                               // Mip flush.
    d[7] = 0x80000000u | ((pitchTexels / 32) << 22) | 2;              // Tiled, pitch, texture fetch.
    d[8] = info.format | (endian << 6);
    d[9] = (in->width - 1) | ((in->height - 1) << 13) | (in->faces == 6 ? 5u << 26 : 0);   // Cube: stack depth 5.
    d[10] = info.format == k8_8_8_8 ? 0x00000C14u : 0x00000D10u;      // Channel swizzle (ZYXW / XYZW).
    d[11] = (in->levels - 1) << 6;
    d[12] = ((in->faces == 6 ? 3u : 1u) << 9) | (packed ? (1u << 11) | layout.mipOffset : 0);   // 2D / cube, packed
                                                                                                 // mips, mip address.
    return texture;
}

} // namespace X360
