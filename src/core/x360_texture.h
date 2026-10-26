#ifndef X360_TEXTURE_H
#define X360_TEXTURE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <array>
#include <optional>

class PackageStream;

/// Xbox 360 texture header (Direct3D 9 for Xbox 360 "D3DBaseTexture") and the conversion of Xbox 360 texture data
/// to and from a PC .dds.
///
/// Every Xbox 360 mini DDS / TXPK texture record ends with this 52 byte struct (big endian dwords):
///
///   dwords 0 - 6   D3DResource: Common (3 = texture), ReferenceCount (1), Fence, ReadFence, Identifier,
///                  BaseFlush (0xFFFF0000), MipFlush (0xFFFF0000)
///   dwords 7 - 12  GPU texture fetch constant (Xenos):
///     f0  bits 0-1 type (2), bits 22-30 pitch (base row pitch in texels / 32), bit 31 tiled
///     f1  bits 0-5 format, bits 6-7 endian swap, bits 12-31 base address / 4096 (0 in the files)
///     f2  bits 0-12 width - 1, bits 13-25 height - 1
///     f3  channel swizzle / filters
///     f4  bits 6-9 last mip level
///     f5  bits 9-10 dimension (1 = 2D, 3 = cube map: 6 faces, f2 bits 26-31 = 5), bit 11 packed mips,
///         bits 12-31 mip address / 4096 (relative to the start of the texture data)
///
/// The texture data is tiled in 32 x 32 block tiles (blocks: 4 x 4 texels for DXT, single texels otherwise) and
/// stored in 16 bit (DXT, 16 bpp) or 32 bit (32 bpp) big endian units. Mip levels smaller than 16 texels are packed
/// together into one tile (the "mip tail"). The address math follows the Xenia emulator (texture_address.h /
/// texture_util.cc), checked against Rugby League Live 2 textures.
namespace X360 {

struct TextureHeader {
    static constexpr qint64 kSize = 52;
    static constexpr quint32 kCommonTexture = 3;   // D3DCOMMON_TYPE_TEXTURE, the first dword.

    std::array<quint32, 13> dwords{};

    void read(PackageStream& in);          // 52 bytes, big endian.
    void write(PackageStream& out) const;  // 52 bytes, big endian.
    QByteArray toBytes() const;            // The same 52 bytes.

    quint32 fetch(int k) const { return dwords[7 + k]; }

    bool isTiled() const { return (fetch(0) >> 31) != 0; }
    quint32 pitchTexels() const { return ((fetch(0) >> 22) & 0x1FF) * 32; }
    quint32 format() const { return fetch(1) & 0x3F; }
    quint32 endian() const { return (fetch(1) >> 6) & 3; }
    quint32 width() const { return (fetch(2) & 0x1FFF) + 1; }
    quint32 height() const { return ((fetch(2) >> 13) & 0x1FFF) + 1; }
    quint32 levelCount() const { return ((fetch(4) >> 6) & 0xF) + 1; }
    bool hasPackedMips() const { return ((fetch(5) >> 11) & 1) != 0; }
    quint32 dimension() const { return (fetch(5) >> 9) & 3; }   // 1 = 2D, 2 = 3D, 3 = cube map.
    quint32 faceCount() const { return dimension() == 3 ? 6 : 1; }
    quint32 mipOffset() const { return (fetch(5) >> 12) << 12; }
};

/// The first dword of a console texture header: Xbox 360 headers start with 3, PS3 ones with the RSX format byte.
inline bool looksLikeHeader(quint32 firstDword) { return firstDword == TextureHeader::kCommonTexture; }

/// "DXT5", "A8R8G8B8", ... for the UI.
QString formatName(quint32 format);

/// Converts Xbox 360 texture data (every mip level, as stored) into a complete PC .dds: untiled, byte swapped,
/// with a DDS header for the format. Returns nothing for formats a .dds can't describe (error says why).
std::optional<QByteArray> toDds(const TextureHeader& header, const QByteArray& data, QString* error = nullptr);

/// A PC .dds turned into what an Xbox 360 texture holds: the header and the (tiled, big endian) texture data.
struct ImportedTexture {
    TextureHeader header;
    QByteArray data;
};

/// Inverse of toDds(), used when a .dds is imported into an Xbox 360 blobset or TXPK.
/// Accepts DXT1 / DXT3 / DXT5, ATI1 / ATI2 (BC4 / BC5) and 32 bit BGRA / BGRX (classic header or DX10 extension).
std::optional<ImportedTexture> fromDds(const QByteArray& dds, QString* error = nullptr);

} // namespace X360

#endif // X360_TEXTURE_H
