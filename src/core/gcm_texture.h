#ifndef GCM_TEXTURE_H
#define GCM_TEXTURE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <optional>

class PackageStream;

/// PS3 RSX texture descriptor (libgcm CellGcmTexture) and the conversion of PS3 texture data to a PC .dds.
///
/// Every console mini DDS / TXPK texture header ends with this 24 byte struct (big endian), copied
/// straight from the game's libgcm texture setup:
///
///   offset  size  field
///        0     1  format     base format | flags (see below)
///        1     1  mipmap     mip level count
///        2     1  dimension  1 = 1D, 2 = 2D, 3 = 3D
///        3     1  cubemap    0 = no, 1 = yes
///        4     4  remap      channel remap (low byte: input order, high byte: output mode)
///        8     2  width
///       10     2  height
///       12     2  depth      1 for 2D textures
///       14     1  location   0 = local memory (VRAM), 1 = main memory
///       15     1  padding
///       16     4  pitch      bytes per row for linear textures, 0 for swizzled / compressed ones
///       20     4  offset     GPU address, filled in at runtime (0 in the files)
namespace Gcm {

// ---------------------------------------------------------------------------
// format: base format | flags
// ---------------------------------------------------------------------------

constexpr quint8 kFlagLinear       = 0x20; // CELL_GCM_TEXTURE_LN - pixels stored row by row (not swizzled).
constexpr quint8 kFlagUnnormalized = 0x40; // CELL_GCM_TEXTURE_UN - unnormalised texture coordinates.
constexpr quint8 kFormatMask       = static_cast<quint8>(~(kFlagLinear | kFlagUnnormalized));

enum BaseFormat : quint8 {
    B8                    = 0x81,
    A1R5G5B5              = 0x82,
    A4R4G4B4              = 0x83,
    R5G6B5                = 0x84,
    A8R8G8B8              = 0x85,
    CompressedDxt1        = 0x86,
    CompressedDxt23       = 0x87,
    CompressedDxt45       = 0x88,
    G8B8                  = 0x8B,
    CompressedB8R8G8R8    = 0x8D, // 0xAD in the SDK headers (stored with the linear bit set).
    CompressedR8B8R8G8    = 0x8E, // 0xAE in the SDK headers.
    R6G5B5                = 0x8F,
    Depth24D8             = 0x90,
    Depth24D8Float        = 0x91,
    Depth16               = 0x92,
    Depth16Float          = 0x93,
    X16                   = 0x94,
    Y16X16                = 0x95,
    R5G5B5A1              = 0x97,
    CompressedHilo8       = 0x98,
    CompressedHiloS8      = 0x99,
    W16Z16Y16X16Float     = 0x9A,
    W32Z32Y32X32Float     = 0x9B,
    X32Float              = 0x9C,
    D1R5G5B5              = 0x9D,
    D8R8G8B8              = 0x9E,
    Y16X16Float           = 0x9F,
};

// ---------------------------------------------------------------------------
// remap
// ---------------------------------------------------------------------------

constexpr quint32 kRemapDefault  = 0xAAE4; // All channels passed through, A R G B order.
constexpr quint32 kRemapReversed = 0xAA1B; // All channels passed through, order reversed (B G R A in memory).

struct CellGcmTexture {
    static constexpr qint64 kSize = 24;

    quint8  format    = A8R8G8B8 | kFlagLinear;
    quint8  mipmap    = 1;
    quint8  dimension = 2;
    quint8  cubemap   = 0;
    quint32 remap     = kRemapDefault;
    quint16 width     = 0;
    quint16 height    = 0;
    quint16 depth     = 1;
    quint8  location  = 0;
    quint8  padding   = 0;
    quint32 pitch     = 0;
    quint32 offset    = 0;

    void read(PackageStream& in);          // Reads 24 bytes in the stream's current byte order.
    void write(PackageStream& out) const;  // Writes 24 bytes in the stream's current byte order.

    quint8 baseFormat() const { return static_cast<quint8>(format & kFormatMask); }
    bool isLinear() const { return (format & kFlagLinear) != 0; }
    bool isCompressed() const;             // DXT / BC formats (never swizzled on the RSX).
    bool isSwizzled() const { return !isLinear() && !isCompressed(); }
};

/// "A8R8G8B8 (linear)", "DXT5", ... for the UI.
QString formatName(quint8 format);

/// Bytes per pixel of an uncompressed base format, 0 for block compressed or unknown formats.
int bytesPerPixel(quint8 baseFormat);

/// Converts PS3 texture data (all mip levels, as stored) into a complete PC .dds file:
///   - swizzled formats are un-swizzled level by level (linear and DXT data is already in row order),
///   - big endian texels are byte swapped and the channel order is fixed up from the remap,
///   - a DDS header with the right format, pitch and mip count is put in front.
/// Returns nothing for formats a .dds can't describe (depth, HILO, ...).
std::optional<QByteArray> toDds(const CellGcmTexture& texture, const QByteArray& pixels);

/// A PC .dds turned into what a PS3 texture holds: the descriptor and the vram data that matches it.
struct ImportedTexture {
    CellGcmTexture texture;
    QByteArray pixels;  // vram section (no DDS header).
    QString note;       // Set when something had to be dropped (e.g. mips of a non power of two texture).
};

/// Inverse of toDds(), used when a .dds is imported into a PS3 blobset, so no per-texture header dump is needed.
/// The layout follows what the games themselves store:
///   - DXT1 / DXT3 / DXT5: data as it is, remap 0xAAE4; power of two sizes get pitch 0, other sizes the linear
///     flag and a pitch of one block row,
///   - 32 bit BGRA / BGRX: linear (LN flag), remap 0xAA1B so the PC byte order is kept, pitch = width * 4,
///   - other uncompressed formats: byte swapped to big endian, linear,
///   - uncompressed textures with mip levels are swizzled instead (linear textures can't carry mips reliably);
///     non power of two ones keep only the top level.
/// Accepts the classic DDS header and the DX10 extension (BC1-3, B8G8R8A8/X8, R8G8B8A8, float formats).
std::optional<ImportedTexture> fromDds(const QByteArray& dds, QString* error = nullptr);

} // namespace Gcm

#endif // GCM_TEXTURE_H
