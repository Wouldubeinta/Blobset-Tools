#include "gcm_texture.h"
#include "package_stream.h"

#include <QDataStream>
#include <QIODevice>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace Gcm {

// ===========================================================================
// CellGcmTexture
// ===========================================================================

void CellGcmTexture::read(PackageStream& in) {
    format = in.readByte();
    mipmap = in.readByte();
    dimension = in.readByte();
    cubemap = in.readByte();
    remap = in.readUInt32();
    width = in.readUInt16();
    height = in.readUInt16();
    depth = in.readUInt16();
    location = in.readByte();
    padding = in.readByte();
    pitch = in.readUInt32();
    offset = in.readUInt32();
}

void CellGcmTexture::write(PackageStream& out) const {
    out.writeByte(format);
    out.writeByte(mipmap);
    out.writeByte(dimension);
    out.writeByte(cubemap);
    out.writeUInt32(remap);
    out.writeUInt16(width);
    out.writeUInt16(height);
    out.writeUInt16(depth);
    out.writeByte(location);
    out.writeByte(padding);
    out.writeUInt32(pitch);
    out.writeUInt32(offset);
}

bool CellGcmTexture::isCompressed() const {
    switch (baseFormat()) {
    case CompressedDxt1:
    case CompressedDxt23:
    case CompressedDxt45:
        return true;
    default:
        return false;
    }
}

// ===========================================================================
// Format info
// ===========================================================================

QString formatName(quint8 format) {
    QString name;
    switch (static_cast<quint8>(format & kFormatMask)) {
    case B8:                 name = u"B8"_s; break;
    case A1R5G5B5:           name = u"A1R5G5B5"_s; break;
    case A4R4G4B4:           name = u"A4R4G4B4"_s; break;
    case R5G6B5:             name = u"R5G6B5"_s; break;
    case A8R8G8B8:           name = u"A8R8G8B8"_s; break;
    case CompressedDxt1:     name = u"DXT1"_s; break;
    case CompressedDxt23:    name = u"DXT3"_s; break;
    case CompressedDxt45:    name = u"DXT5"_s; break;
    case G8B8:               name = u"G8B8"_s; break;
    case CompressedB8R8G8R8: name = u"B8R8_G8R8"_s; break;
    case CompressedR8B8R8G8: name = u"R8B8_R8G8"_s; break;
    case R6G5B5:             name = u"R6G5B5"_s; break;
    case Depth24D8:          name = u"DEPTH24_D8"_s; break;
    case Depth24D8Float:     name = u"DEPTH24_D8_FLOAT"_s; break;
    case Depth16:            name = u"DEPTH16"_s; break;
    case Depth16Float:       name = u"DEPTH16_FLOAT"_s; break;
    case X16:                name = u"X16"_s; break;
    case Y16X16:             name = u"Y16_X16"_s; break;
    case R5G5B5A1:           name = u"R5G5B5A1"_s; break;
    case CompressedHilo8:    name = u"HILO8"_s; break;
    case CompressedHiloS8:   name = u"HILO_S8"_s; break;
    case W16Z16Y16X16Float:  name = u"RGBA16F"_s; break;
    case W32Z32Y32X32Float:  name = u"RGBA32F"_s; break;
    case X32Float:           name = u"R32F"_s; break;
    case D1R5G5B5:           name = u"D1R5G5B5"_s; break;
    case D8R8G8B8:           name = u"D8R8G8B8"_s; break;
    case Y16X16Float:        name = u"RG16F"_s; break;
    default:
        return u"Format 0x%1"_s.arg(static_cast<uint>(format), 2, 16, QLatin1Char('0'));
    }

    // DXT data is always stored in block row order, so "swizzled" would only confuse.
    const bool compressed = (format & kFormatMask) >= CompressedDxt1 && (format & kFormatMask) <= CompressedDxt45;
    if (!compressed)
        name += (format & kFlagLinear) ? u" (linear)"_s : u" (swizzled)"_s;
    return name;
}

int bytesPerPixel(quint8 baseFormat) {
    switch (baseFormat) {
    case B8:
        return 1;
    case A1R5G5B5:
    case A4R4G4B4:
    case R5G6B5:
    case G8B8:
    case R6G5B5:
    case Depth16:
    case Depth16Float:
    case X16:
    case R5G5B5A1:
    case CompressedHilo8:
    case CompressedHiloS8:
    case D1R5G5B5:
        return 2;
    case A8R8G8B8:
    case Depth24D8:
    case Depth24D8Float:
    case Y16X16:
    case X32Float:
    case D8R8G8B8:
    case Y16X16Float:
        return 4;
    case W16Z16Y16X16Float:
        return 8;
    case W32Z32Y32X32Float:
        return 16;
    default:
        return 0;
    }
}

// ===========================================================================
// PS3 -> DDS
// ===========================================================================

namespace {

// DDS header flags.
constexpr quint32 DDSD_CAPS = 0x1, DDSD_HEIGHT = 0x2, DDSD_WIDTH = 0x4, DDSD_PITCH = 0x8;
constexpr quint32 DDSD_PIXELFORMAT = 0x1000, DDSD_MIPMAPCOUNT = 0x20000, DDSD_LINEARSIZE = 0x80000;
constexpr quint32 DDPF_ALPHAPIXELS = 0x1, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40, DDPF_LUMINANCE = 0x20000;
constexpr quint32 DDSCAPS_COMPLEX = 0x8, DDSCAPS_TEXTURE = 0x1000, DDSCAPS_MIPMAP = 0x400000;
constexpr quint32 DDSCAPS2_CUBEMAP_ALLFACES = 0xFE00;

constexpr quint32 fourCC(char a, char b, char c, char d) {
    return static_cast<quint32>(static_cast<quint8>(a)) | (static_cast<quint32>(static_cast<quint8>(b)) << 8)
         | (static_cast<quint32>(static_cast<quint8>(c)) << 16) | (static_cast<quint32>(static_cast<quint8>(d)) << 24);
}

// D3DFORMAT numbers that DirectXTex accepts as a FourCC.
constexpr quint32 kD3dG16R16 = 34, kD3dG16R16F = 112, kD3dA16B16G16R16F = 113, kD3dR32F = 114, kD3dA32B32G32R32F = 116;

struct PixelFormat {
    quint32 flags = 0;
    quint32 fourCC = 0;
    quint32 bitCount = 0;
    quint32 r = 0, g = 0, b = 0, a = 0;
};

/// How one texel is turned from PS3 memory order into the order the DDS pixel format describes.
enum class TexelFix {
    None,       // Already in DDS order.
    Reverse32,  // A R G B -> B G R A.
    Swap16,     // Every 16 bit value is big endian.
    Swap32,     // Every 32 bit value is big endian.
};

struct Layout {
    PixelFormat pf;
    int blockBytes = 0;      // DXT: bytes per 4x4 block. 0 = uncompressed.
    int bytesPerPixel = 0;
    TexelFix fix = TexelFix::None;
};

std::optional<Layout> layoutFor(const CellGcmTexture& tex) {
    Layout l;
    l.bytesPerPixel = bytesPerPixel(tex.baseFormat());

    switch (tex.baseFormat()) {
    case CompressedDxt1:
        l.pf = {DDPF_FOURCC, fourCC('D', 'X', 'T', '1')};
        l.blockBytes = 8;
        return l;
    case CompressedDxt23:
        l.pf = {DDPF_FOURCC, fourCC('D', 'X', 'T', '3')};
        l.blockBytes = 16;
        return l;
    case CompressedDxt45:
        l.pf = {DDPF_FOURCC, fourCC('D', 'X', 'T', '5')};
        l.blockBytes = 16;
        return l;

    case A8R8G8B8:
    case D8R8G8B8: {
        const bool alpha = tex.baseFormat() == A8R8G8B8;
        l.pf = {DDPF_RGB | (alpha ? DDPF_ALPHAPIXELS : 0u), 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, alpha ? 0xFF000000 : 0u};
        // With the reversed remap the bytes are already B G R A (what the DDS masks describe on a little endian PC).
        l.fix = (tex.remap & 0xFF) == (kRemapReversed & 0xFF) ? TexelFix::None : TexelFix::Reverse32;
        return l;
    }

    case R5G6B5:
        l.pf = {DDPF_RGB, 0, 16, 0xF800, 0x07E0, 0x001F, 0};
        l.fix = TexelFix::Swap16;
        return l;
    case A1R5G5B5:
        l.pf = {DDPF_RGB | DDPF_ALPHAPIXELS, 0, 16, 0x7C00, 0x03E0, 0x001F, 0x8000};
        l.fix = TexelFix::Swap16;
        return l;
    case D1R5G5B5:
        l.pf = {DDPF_RGB, 0, 16, 0x7C00, 0x03E0, 0x001F, 0};
        l.fix = TexelFix::Swap16;
        return l;
    case A4R4G4B4:
        l.pf = {DDPF_RGB | DDPF_ALPHAPIXELS, 0, 16, 0x0F00, 0x00F0, 0x000F, 0xF000};
        l.fix = TexelFix::Swap16;
        return l;
    case R5G5B5A1:
        l.pf = {DDPF_RGB | DDPF_ALPHAPIXELS, 0, 16, 0xF800, 0x07C0, 0x003E, 0x0001};
        l.fix = TexelFix::Swap16;
        return l;
    case G8B8:
        // Two 8 bit channels, G in the first byte: read little endian that is G = 0x00FF, B = 0xFF00.
        l.pf = {DDPF_RGB, 0, 16, 0, 0x00FF, 0xFF00, 0};
        return l;
    case B8:
        l.pf = {DDPF_LUMINANCE, 0, 8, 0xFF, 0, 0, 0};
        return l;
    case X16:
        l.pf = {DDPF_LUMINANCE, 0, 16, 0xFFFF, 0, 0, 0};
        l.fix = TexelFix::Swap16;
        return l;

    case Y16X16:
        l.pf = {DDPF_FOURCC, kD3dG16R16};
        l.fix = TexelFix::Swap16;
        return l;
    case Y16X16Float:
        l.pf = {DDPF_FOURCC, kD3dG16R16F};
        l.fix = TexelFix::Swap16;
        return l;
    case W16Z16Y16X16Float:
        l.pf = {DDPF_FOURCC, kD3dA16B16G16R16F};
        l.fix = TexelFix::Swap16;
        return l;
    case X32Float:
        l.pf = {DDPF_FOURCC, kD3dR32F};
        l.fix = TexelFix::Swap32;
        return l;
    case W32Z32Y32X32Float:
        l.pf = {DDPF_FOURCC, kD3dA32B32G32R32F};
        l.fix = TexelFix::Swap32;
        return l;

    default:
        return std::nullopt; // Depth, HILO, R6G5B5, B8R8_G8R8 / R8B8_R8G8: no .dds equivalent.
    }
}

void fixTexels(QByteArray& data, TexelFix fix) {
    char* p = data.data();
    const qsizetype size = data.size();

    switch (fix) {
    case TexelFix::None:
        break;
    case TexelFix::Reverse32:
    case TexelFix::Swap32:
        for (qsizetype i = 0; i + 3 < size; i += 4) {
            std::swap(p[i], p[i + 3]);
            std::swap(p[i + 1], p[i + 2]);
        }
        break;
    case TexelFix::Swap16:
        for (qsizetype i = 0; i + 1 < size; i += 2)
            std::swap(p[i], p[i + 1]);
        break;
    }
}

/// RSX swizzle: x and y bits interleaved (x first) while both dimensions still have bits left.
qsizetype swizzledIndex(quint32 x, quint32 y, quint32 width, quint32 height) {
    qsizetype index = 0;
    int bit = 0;
    while (width > 1 || height > 1) {
        if (width > 1) {
            index |= static_cast<qsizetype>(x & 1) << bit++;
            x >>= 1;
            width >>= 1;
        }
        if (height > 1) {
            index |= static_cast<qsizetype>(y & 1) << bit++;
            y >>= 1;
            height >>= 1;
        }
    }
    return index;
}

QByteArray unswizzle(const QByteArray& level, quint32 width, quint32 height, int bpp) {
    QByteArray out(level.size(), 0);
    for (quint32 y = 0; y < height; ++y) {
        for (quint32 x = 0; x < width; ++x) {
            const qsizetype src = swizzledIndex(x, y, width, height) * bpp;
            const qsizetype dst = (static_cast<qsizetype>(y) * width + x) * bpp;
            if (src + bpp <= level.size() && dst + bpp <= out.size())
                std::copy_n(level.constData() + src, bpp, out.data() + dst);
        }
    }
    return out;
}

QByteArray swizzle(const QByteArray& level, quint32 width, quint32 height, int bpp) {
    QByteArray out(level.size(), 0);
    for (quint32 y = 0; y < height; ++y) {
        for (quint32 x = 0; x < width; ++x) {
            const qsizetype src = (static_cast<qsizetype>(y) * width + x) * bpp;
            const qsizetype dst = swizzledIndex(x, y, width, height) * bpp;
            if (src + bpp <= level.size() && dst + bpp <= out.size())
                std::copy_n(level.constData() + src, bpp, out.data() + dst);
        }
    }
    return out;
}

bool isPowerOfTwo(quint32 v) {
    return v != 0 && (v & (v - 1)) == 0;
}

QByteArray ddsHeader(const CellGcmTexture& tex, const Layout& layout) {
    const quint32 width = std::max<quint32>(1, tex.width);
    const quint32 height = std::max<quint32>(1, tex.height);
    const quint32 mips = std::max<quint32>(1, tex.mipmap);
    const bool compressed = layout.blockBytes > 0;

    quint32 flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    flags |= compressed ? DDSD_LINEARSIZE : DDSD_PITCH;
    if (mips > 1)
        flags |= DDSD_MIPMAPCOUNT;

    const quint32 pitchOrLinearSize = compressed
        ? std::max(1u, (width + 3) / 4) * std::max(1u, (height + 3) / 4) * static_cast<quint32>(layout.blockBytes)
        : width * static_cast<quint32>(layout.bytesPerPixel);

    quint32 caps = DDSCAPS_TEXTURE;
    if (mips > 1)
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
    if (tex.cubemap)
        caps |= DDSCAPS_COMPLEX;

    QByteArray header(128, 0);
    QDataStream out(&header, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);

    out << quint32(0x20534444) << quint32(124) << flags << height << width << pitchOrLinearSize
        << quint32(0) << mips;

    out.device()->seek(76); // dwReserved1[11]
    out << quint32(32) << layout.pf.flags << layout.pf.fourCC << layout.pf.bitCount
        << layout.pf.r << layout.pf.g << layout.pf.b << layout.pf.a;
    out << caps << quint32(tex.cubemap ? DDSCAPS2_CUBEMAP_ALLFACES : 0);

    return header;
}

} // namespace

std::optional<QByteArray> toDds(const CellGcmTexture& tex, const QByteArray& pixels) {
    const std::optional<Layout> layout = layoutFor(tex);
    if (!layout || tex.depth > 1)
        return std::nullopt; // 3D textures aren't used by these games.

    const quint32 width = std::max<quint32>(1, tex.width);
    const quint32 height = std::max<quint32>(1, tex.height);
    const int mips = std::max<int>(1, tex.mipmap);
    const int faces = tex.cubemap ? 6 : 1;

    QByteArray body;

    if (layout->blockBytes > 0) {
        // DXT: blocks are stored in row order on the RSX (never swizzled), little endian like on PC.
        body = pixels;
    } else {
        const int bpp = layout->bytesPerPixel;
        qsizetype pos = 0;

        for (int face = 0; face < faces; ++face) {
            for (int mip = 0; mip < mips; ++mip) {
                const quint32 w = std::max<quint32>(1, width >> mip);
                const quint32 h = std::max<quint32>(1, height >> mip);
                const qsizetype tightRow = static_cast<qsizetype>(w) * bpp;

                QByteArray level;
                if (tex.isLinear() && mip == 0 && tex.pitch > tightRow) {
                    // Linear rows can be padded up to the pitch; a .dds wants them tight.
                    for (quint32 y = 0; y < h; ++y)
                        level += pixels.mid(pos + static_cast<qsizetype>(y) * tex.pitch, tightRow);
                    pos += static_cast<qsizetype>(h) * tex.pitch;
                } else {
                    level = pixels.mid(pos, tightRow * h);
                    pos += tightRow * h;
                }
                level.resize(tightRow * h, '\0'); // Short data: pad rather than hand the decoder a truncated file.

                if (tex.isSwizzled())
                    level = unswizzle(level, w, h, bpp);

                fixTexels(level, layout->fix);
                body += level;
            }
        }
    }

    return ddsHeader(tex, *layout) + body;
}

// ===========================================================================
// DDS -> PS3
// ===========================================================================

namespace {

constexpr quint32 DDSCAPS2_CUBEMAP = 0x200;
constexpr quint32 kDx10FourCC = fourCC('D', 'X', '1', '0');
constexpr quint32 kDx10MiscCube = 0x4;
constexpr quint32 kMaxRsxSize = 4096;

// DXGI_FORMAT values accepted from a DX10 header.
enum : quint32 {
    DXGI_R32G32B32A32_FLOAT = 2, DXGI_R16G16B16A16_FLOAT = 10, DXGI_R8G8B8A8_UNORM = 28, DXGI_R8G8B8A8_UNORM_SRGB = 29,
    DXGI_R16G16_FLOAT = 34, DXGI_R32_FLOAT = 41, DXGI_BC1_UNORM = 71, DXGI_BC1_UNORM_SRGB = 72, DXGI_BC2_UNORM = 74,
    DXGI_BC2_UNORM_SRGB = 75, DXGI_BC3_UNORM = 77, DXGI_BC3_UNORM_SRGB = 78, DXGI_B8G8R8A8_UNORM = 87,
    DXGI_B8G8R8X8_UNORM = 88, DXGI_B8G8R8A8_UNORM_SRGB = 91, DXGI_B8G8R8X8_UNORM_SRGB = 93,
};

quint32 u32At(const QByteArray& data, qsizetype offset) {
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

/// What the imported pixels have to go through before they match the PS3 layout.
enum class ImportFix { None, RgbaToBgra, Swap16, Swap32 };

struct ImportFormat {
    quint8 baseFormat = 0;
    quint32 remap = kRemapDefault;
    ImportFix fix = ImportFix::None;
};

std::optional<ImportFormat> importFormatFor(const QByteArray& dds, quint32 dxgiFormat, QString* error) {
    const quint32 pfFlags = u32At(dds, 80);
    const quint32 pfFourCC = u32At(dds, 84);
    const quint32 bits = u32At(dds, 88);
    const quint32 r = u32At(dds, 92), g = u32At(dds, 96), b = u32At(dds, 100), a = u32At(dds, 104);

    if (dxgiFormat != 0) {
        switch (dxgiFormat) {
        case DXGI_BC1_UNORM: case DXGI_BC1_UNORM_SRGB: return ImportFormat{CompressedDxt1};
        case DXGI_BC2_UNORM: case DXGI_BC2_UNORM_SRGB: return ImportFormat{CompressedDxt23};
        case DXGI_BC3_UNORM: case DXGI_BC3_UNORM_SRGB: return ImportFormat{CompressedDxt45};
        case DXGI_B8G8R8A8_UNORM: case DXGI_B8G8R8A8_UNORM_SRGB: return ImportFormat{A8R8G8B8, kRemapReversed};
        case DXGI_B8G8R8X8_UNORM: case DXGI_B8G8R8X8_UNORM_SRGB: return ImportFormat{D8R8G8B8, kRemapReversed};
        case DXGI_R8G8B8A8_UNORM: case DXGI_R8G8B8A8_UNORM_SRGB:
            return ImportFormat{A8R8G8B8, kRemapReversed, ImportFix::RgbaToBgra};
        case DXGI_R16G16B16A16_FLOAT: return ImportFormat{W16Z16Y16X16Float, kRemapDefault, ImportFix::Swap16};
        case DXGI_R32G32B32A32_FLOAT: return ImportFormat{W32Z32Y32X32Float, kRemapDefault, ImportFix::Swap32};
        case DXGI_R16G16_FLOAT:       return ImportFormat{Y16X16Float, kRemapDefault, ImportFix::Swap16};
        case DXGI_R32_FLOAT:          return ImportFormat{X32Float, kRemapDefault, ImportFix::Swap32};
        default:
            if (error)
                *error = u"DX10 format %1 can't be stored on PS3 (use DXT1, DXT3, DXT5 or 32 bit BGRA)."_s.arg(dxgiFormat);
            return std::nullopt;
        }
    }

    if (pfFlags & DDPF_FOURCC) {
        switch (pfFourCC) {
        case fourCC('D', 'X', 'T', '1'): return ImportFormat{CompressedDxt1};
        case fourCC('D', 'X', 'T', '2'):
        case fourCC('D', 'X', 'T', '3'): return ImportFormat{CompressedDxt23};
        case fourCC('D', 'X', 'T', '4'):
        case fourCC('D', 'X', 'T', '5'): return ImportFormat{CompressedDxt45};
        case kD3dA16B16G16R16F: return ImportFormat{W16Z16Y16X16Float, kRemapDefault, ImportFix::Swap16};
        case kD3dA32B32G32R32F: return ImportFormat{W32Z32Y32X32Float, kRemapDefault, ImportFix::Swap32};
        case kD3dG16R16F:       return ImportFormat{Y16X16Float, kRemapDefault, ImportFix::Swap16};
        case kD3dR32F:          return ImportFormat{X32Float, kRemapDefault, ImportFix::Swap32};
        case kD3dG16R16:        return ImportFormat{Y16X16, kRemapDefault, ImportFix::Swap16};
        default:
            if (error)
                *error = u"DDS FourCC 0x%1 can't be stored on PS3 (use DXT1, DXT3, DXT5 or 32 bit BGRA)."_s
                             .arg(pfFourCC, 8, 16, QLatin1Char('0'));
            return std::nullopt;
        }
    }

    const bool hasAlpha = (pfFlags & DDPF_ALPHAPIXELS) && a != 0;

    if ((pfFlags & DDPF_RGB) && bits == 32) {
        if (r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF)
            return ImportFormat{static_cast<quint8>(hasAlpha ? A8R8G8B8 : D8R8G8B8), kRemapReversed};
        if (r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000)
            return ImportFormat{static_cast<quint8>(hasAlpha ? A8R8G8B8 : D8R8G8B8), kRemapReversed, ImportFix::RgbaToBgra};
    }
    if ((pfFlags & DDPF_RGB) && bits == 16) {
        if (r == 0xF800 && g == 0x07E0 && b == 0x001F)
            return ImportFormat{R5G6B5, kRemapDefault, ImportFix::Swap16};
        if (r == 0x7C00 && g == 0x03E0 && b == 0x001F)
            return ImportFormat{static_cast<quint8>(hasAlpha ? A1R5G5B5 : D1R5G5B5), kRemapDefault, ImportFix::Swap16};
        if (r == 0x0F00 && g == 0x00F0 && b == 0x000F && hasAlpha)
            return ImportFormat{A4R4G4B4, kRemapDefault, ImportFix::Swap16};
    }
    if ((pfFlags & DDPF_LUMINANCE) && bits == 8)
        return ImportFormat{B8};

    if (error)
        *error = u"This DDS pixel format (%1 bit, masks %2/%3/%4/%5) can't be stored on PS3 (use DXT1, DXT3, DXT5 or 32 bit BGRA)."_s
                     .arg(bits).arg(r, 0, 16).arg(g, 0, 16).arg(b, 0, 16).arg(a, 0, 16);
    return std::nullopt;
}

void applyImportFix(QByteArray& data, ImportFix fix) {
    char* p = data.data();
    const qsizetype size = data.size();

    switch (fix) {
    case ImportFix::None:
        break;
    case ImportFix::RgbaToBgra:
        for (qsizetype i = 0; i + 3 < size; i += 4)
            std::swap(p[i], p[i + 2]);
        break;
    case ImportFix::Swap16:
        fixTexels(data, TexelFix::Swap16);
        break;
    case ImportFix::Swap32:
        fixTexels(data, TexelFix::Swap32);
        break;
    }
}

} // namespace

std::optional<ImportedTexture> fromDds(const QByteArray& dds, QString* error) {
    const auto failWith = [error](const QString& message) -> std::optional<ImportedTexture> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    if (dds.size() < 128 || u32At(dds, 0) != 0x20534444 || u32At(dds, 4) != 124)
        return failWith(u"Not a valid DDS file."_s);

    const quint32 flags = u32At(dds, 8);
    const quint32 height = u32At(dds, 12);
    const quint32 width = u32At(dds, 16);
    const quint32 caps2 = u32At(dds, 112);
    quint32 mips = (flags & DDSD_MIPMAPCOUNT) ? std::max<quint32>(1, u32At(dds, 28)) : 1;

    if (width == 0 || height == 0 || width > kMaxRsxSize || height > kMaxRsxSize)
        return failWith(u"%1 x %2 is outside of what the PS3 supports (1 - 4096)."_s.arg(width).arg(height));
    if (u32At(dds, 24) > 1 && (flags & 0x800000)) // DDSD_DEPTH
        return failWith(u"Volume textures aren't supported."_s);

    qsizetype dataOffset = 128;
    quint32 dxgiFormat = 0;
    bool cube = (caps2 & DDSCAPS2_CUBEMAP) != 0;

    if ((u32At(dds, 80) & DDPF_FOURCC) && u32At(dds, 84) == kDx10FourCC) {
        if (dds.size() < 148)
            return failWith(u"The DDS DX10 header is truncated."_s);
        dxgiFormat = u32At(dds, 128);
        cube = cube || (u32At(dds, 136) & kDx10MiscCube);
        if (u32At(dds, 140) > 1 && !cube)
            return failWith(u"Texture arrays aren't supported."_s);
        dataOffset = 148;
    }

    const std::optional<ImportFormat> format = importFormatFor(dds, dxgiFormat, error);
    if (!format)
        return std::nullopt;

    // Mip chain can't go below 1 x 1.
    const quint32 maxMips = 1 + static_cast<quint32>(std::log2(std::max(width, height)));
    mips = std::min(mips, maxMips);

    ImportedTexture result;
    CellGcmTexture& tex = result.texture;
    tex.width = static_cast<quint16>(width);
    tex.height = static_cast<quint16>(height);
    tex.depth = 1;
    tex.dimension = 2;
    tex.cubemap = cube ? 1 : 0;
    tex.remap = format->remap;
    tex.location = 0;
    tex.offset = 0;

    const int faces = cube ? 6 : 1;
    const QByteArray source = dds.mid(dataOffset);

    // --- DXT: stored as it is ---
    const int blockBytes = format->baseFormat == CompressedDxt1 ? 8
                         : (format->baseFormat == CompressedDxt23 || format->baseFormat == CompressedDxt45) ? 16 : 0;
    if (blockBytes > 0) {
        qsizetype expected = 0;
        for (quint32 mip = 0; mip < mips; ++mip) {
            const quint32 w = std::max<quint32>(1, width >> mip), h = std::max<quint32>(1, height >> mip);
            expected += static_cast<qsizetype>(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * blockBytes;
        }
        expected *= faces;
        if (source.size() < expected)
            return failWith(u"The DDS pixel data is shorter than its header says (%1 of %2 bytes)."_s.arg(source.size()).arg(expected));

        // Same as the games: power of two DXT textures have no flags and pitch 0, the others carry the
        // linear flag and the byte size of one row of blocks.
        const bool potDxt = isPowerOfTwo(width) && isPowerOfTwo(height);
        tex.format = static_cast<quint8>(format->baseFormat | (potDxt ? 0 : kFlagLinear));
        tex.mipmap = static_cast<quint8>(mips);
        tex.pitch = potDxt ? 0 : std::max(1u, (width + 3) / 4) * static_cast<quint32>(blockBytes);
        result.pixels = source.left(expected);
        return result;
    }

    // --- Uncompressed ---
    const int bpp = bytesPerPixel(format->baseFormat);
    const bool pot = isPowerOfTwo(width) && isPowerOfTwo(height);

    if (mips > 1 && !pot) {
        result.note = u"Non power of two texture: only the top mip level was imported."_s;
    }
    const quint32 storedMips = (mips > 1 && pot) ? mips : 1;
    const bool swizzled = storedMips > 1;

    tex.format = static_cast<quint8>(format->baseFormat | (swizzled ? 0 : kFlagLinear));
    tex.mipmap = static_cast<quint8>(storedMips);
    tex.pitch = swizzled ? 0 : width * static_cast<quint32>(bpp);

    qsizetype pos = 0;
    for (int face = 0; face < faces; ++face) {
        for (quint32 mip = 0; mip < mips; ++mip) {
            const quint32 w = std::max<quint32>(1, width >> mip), h = std::max<quint32>(1, height >> mip);
            const qsizetype levelBytes = static_cast<qsizetype>(w) * h * bpp;
            if (pos + levelBytes > source.size())
                return failWith(u"The DDS pixel data is shorter than its header says."_s);

            if (mip < storedMips) {
                QByteArray level = source.mid(pos, levelBytes);
                applyImportFix(level, format->fix);
                if (swizzled)
                    level = swizzle(level, w, h, bpp);
                result.pixels += level;
            }
            pos += levelBytes;
        }
    }
    return result;
}

} // namespace Gcm
