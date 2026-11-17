#include "rl26_skin_compositor.h"

#include "hash_utility.h"

#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Qt::StringLiterals;

namespace Rl26SkinCompositor {

namespace {

// A layer's placement: by its path (RL26), else by its file name (Rugby League Live 4's atlases).
Placements::const_iterator findPlacement(const Placements& placements, const QString& path) {
    const auto pl = placements.constFind(BlobsetHash::fnv1a32(path.toStdString()));
    if (pl != placements.constEnd())
        return pl;
    return placements.constFind(BlobsetHash::fnv1a32(path.section(u'/', -1).toStdString()));
}

// The path the game uses: its "_2" size level when it has levels.
QString levelled(const QString& path, const std::function<bool(const QString&)>& exists) {
    const QString two = path + u"_2"_s;
    return exists(two) ? two.toLower() : path.toLower();
}

float overlay(float a, float b) {
    return a < 0.5f ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b);
}

enum class Purpose { Colour, Normal, Gloss };

// Scaled with the colour and the alpha apart. A smooth scale premultiplies by alpha, and the games' alpha is often
// not transparency (a real player's head keeps its subsurface thickness there, about 0 around the eyes and the neck):
// those colours came back black, or as yellow / red noise where they were divided back out.
QImage scaledStraight(const QImage& img, int w, int h) {
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.width() == w && rgba.height() == h)
        return rgba;
    const QImage rgb = rgba.convertToFormat(QImage::Format_RGBX8888).scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage alpha = rgba.convertToFormat(QImage::Format_Alpha8).scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage out = rgb.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < out.height() && y < alpha.height(); ++y) {
        uchar* d = out.scanLine(y);
        const uchar* a = alpha.constScanLine(y);
        for (int x = 0; x < out.width() && x < alpha.width(); ++x)
            d[x * 4 + 3] = a[x];
    }
    return out;
}

// The creator blend modes the layers use (TextureLayer::blend), as an enum for the per-pixel loops.
enum class Blend { Other, Copy, CopyRgb, CopySkinMat, Overlay, OverlayMat, AlphaWeighted, Weighted, MultiplyAlpha,
                   Multiply, AdditiveAlpha, Subtract, Hair, TwoTint, CopyHairCap, FourTint };

Blend blendOf(const QString& name) {
    static const QHash<QString, Blend> modes = {
        {u"creator_copy"_s, Blend::Copy}, {u"creator_copy_rgb"_s, Blend::CopyRgb},
        {u"creator_copy_skin_mat"_s, Blend::CopySkinMat}, {u"creator_overlay"_s, Blend::Overlay},
        {u"creator_overlay_mat"_s, Blend::OverlayMat}, {u"creator_alphaweighted"_s, Blend::AlphaWeighted},
        {u"creator_weighted"_s, Blend::Weighted}, {u"creator_multiplyalpha"_s, Blend::MultiplyAlpha},
        {u"creator_multiply"_s, Blend::Multiply}, {u"creator_additivealpha"_s, Blend::AdditiveAlpha},
        {u"creator_subtract"_s, Blend::Subtract}, {u"creator_hair"_s, Blend::Hair}, {u"creator_twotint"_s, Blend::TwoTint},
        {u"creator_copy_haircap"_s, Blend::CopyHairCap}, {u"creator_fourtint"_s, Blend::FourTint}};
    return modes.value(name, Blend::Other);
}

// A layer group drawn into a float RGBA canvas, like the game's creator.
struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<float> px;
    bool any = false;
};

Canvas layered(const QList<Rl26Character::TextureLayer>& layers, Purpose purpose, bool cap,
               const QHash<QString, QImage>& images, const Placements& placements,
               const std::function<bool(const QString&)>& exists, int maxSize) {
    Canvas c;
    // The target's size: the first layer's placement's full size, else its image's.
    int width = 0, height = 0;
    for (const Rl26Character::TextureLayer& layer : layers) {
        const QString path = levelled(layer.path, exists);
        const auto pl = findPlacement(placements, path);
        if (pl != placements.constEnd() && pl->fullWidth > 0) {
            width = pl->fullWidth;
            height = pl->fullHeight;
            break;
        }
        const QImage& img = images.value(path);
        if (!img.isNull()) {
            width = img.width();
            height = img.height();
            break;
        }
    }
    if (width <= 0 || height <= 0)
        return c;
    const float scale = maxSize > 0 ? std::min(1.0f, float(maxSize) / float(std::max(width, height))) : 1.0f;
    const int W = std::max(1, int(std::lround(width * scale))), H = std::max(1, int(std::lround(height * scale)));
    c.width = W;
    c.height = H;
    c.px.assign(size_t(W) * H * 4, 0.0f);
    const std::array<float, 4> start = purpose == Purpose::Normal ? std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f}
                                     : purpose == Purpose::Gloss  ? std::array<float, 4>{0.0f, 0.4f, 1.0f, 1.0f}
                                                                  : std::array<float, 4>{0.0f, 0.0f, 0.0f, cap ? 0.0f : 1.0f};
    for (size_t i = 0; i < c.px.size(); i += 4)
        std::copy(start.begin(), start.end(), c.px.begin() + qsizetype(i));

    for (const Rl26Character::TextureLayer& layer : layers) {
        const QString path = levelled(layer.path, exists);
        const QImage& img = images.value(path);
        if (img.isNull())
            continue;
        // Where it goes (top-down rectangle -> the bottom-up rows), scaled to the target.
        int x0 = 0, y0 = 0, x1 = W, y1 = H;
        const auto pl = findPlacement(placements, path);
        if (pl != placements.constEnd() && pl->fullWidth > 0 && pl->fullHeight > 0) {
            const float sx = float(W) / pl->fullWidth, sy = float(H) / pl->fullHeight;
            const int yb = pl->fullHeight - (pl->y + pl->height);
            x0 = int(std::lround(pl->x * sx));
            y0 = int(std::lround(yb * sy));
            x1 = int(std::lround((pl->x + pl->width) * sx));
            y1 = int(std::lround((yb + pl->height) * sy));
        }
        x0 = std::clamp(x0, 0, W);
        x1 = std::clamp(x1, 0, W);
        y0 = std::clamp(y0, 0, H);
        y1 = std::clamp(y1, 0, H);
        if (x1 <= x0 || y1 <= y0)
            continue;
        const QImage src = scaledStraight(img, x1 - x0, y1 - y0);
        // The blend mode, worked out once per layer (not per pixel).
        const Blend mode = blendOf(layer.blend);
        const float k = layer.intensity;
        const float tr = layer.tint.r / 255.f, tg = layer.tint.g / 255.f, tb = layer.tint.b / 255.f;
        const bool copyKind = mode == Blend::Copy || mode == Blend::CopyRgb || mode == Blend::CopySkinMat;
        const bool overlayKind = mode == Blend::Overlay || mode == Blend::OverlayMat;
        const bool fourTint = mode == Blend::FourTint && !layer.tints.isEmpty();
        // creator_fourtint: colour 1, with colours 2 - n where the R, G and B masks are.
        std::array<std::array<float, 3>, 4> tints{};
        const int tintCount = int(std::min<qsizetype>(4, layer.tints.size()));
        for (int i = 0; i < tintCount; ++i)
            tints[i] = {float(layer.tints[i].r), float(layer.tints[i].g), float(layer.tints[i].b)};
        const float hr = layer.tinted ? tr : 0.1f, hg = layer.tinted ? tg : 0.08f, hb = layer.tinted ? tb : 0.07f;
        const int rows = y1 - y0;
#pragma omp parallel for schedule(static) if (rows > 64)
        for (int y = y0; y < y1; ++y) {
            const uchar* s = src.constScanLine(y - y0);
            float* d = c.px.data() + (size_t(y) * W + x0) * 4;
            for (int x = x0; x < x1; ++x, s += 4, d += 4) {
                float r = s[0] / 255.f, g = s[1] / 255.f, b = s[2] / 255.f, a = s[3] / 255.f;
                if (purpose == Purpose::Normal && s[0] > 250 && s[2] > 250) {
                    // A swizzled ("DXT5nm") normal map, Rugby League Live 4's kits: X is in alpha, Y in green. Z is
                    // made from them (composeNormal), so B is left at 0.
                    r = a;
                    b = 0.0f;
                    a = 1.0f;
                }
                if (purpose != Purpose::Colour) {
                    // Normals and "mat" maps: copied, overlaid or drawn over by their alpha.
                    if (copyKind) {
                        d[0] = r; d[1] = g; d[2] = b;
                        if (purpose == Purpose::Gloss)
                            d[3] = a;   // RLL4's "mat" maps keep the occlusion in alpha.
                    } else if (overlayKind) {
                        const float w = a * k;
                        d[0] += (overlay(d[0], r) - d[0]) * w;
                        d[1] += (overlay(d[1], g) - d[1]) * w;
                        d[2] += (overlay(d[2], b) - d[2]) * w;
                    } else {
                        d[0] += (r - d[0]) * a;
                        d[1] += (g - d[1]) * a;
                        d[2] += (b - d[2]) * a;
                    }
                    continue;
                }
                switch (mode) {
                case Blend::Copy:
                    d[0] = r; d[1] = g; d[2] = b; d[3] = a;
                    break;
                case Blend::CopyRgb:
                    d[0] = r; d[1] = g; d[2] = b;
                    break;
                case Blend::Overlay: {
                    const float w = a * k;
                    d[0] += (overlay(d[0], r) - d[0]) * w;
                    d[1] += (overlay(d[1], g) - d[1]) * w;
                    d[2] += (overlay(d[2], b) - d[2]) * w;
                    break;
                }
                case Blend::AlphaWeighted: {
                    // Rugby League Live 4's eyes: the eye colour textures, each drawn over by its alpha and weight.
                    const float w = a * k;
                    d[0] += (r - d[0]) * w;
                    d[1] += (g - d[1]) * w;
                    d[2] += (b - d[2]) * w;
                    break;
                }
                case Blend::Weighted:
                    // Rugby League Live 4's skin: its tone textures summed by their weights (the "fourWay" layers).
                    d[0] += r * k;
                    d[1] += g * k;
                    d[2] += b * k;
                    break;
                case Blend::MultiplyAlpha: {
                    const float w = a * k;
                    d[0] += (d[0] * r - d[0]) * w;
                    d[1] += (d[1] * g - d[1]) * w;
                    d[2] += (d[2] * b - d[2]) * w;
                    break;
                }
                case Blend::Multiply:
                    d[0] *= 1 - k + k * r;
                    d[1] *= 1 - k + k * g;
                    d[2] *= 1 - k + k * b;
                    break;
                case Blend::AdditiveAlpha:
                    d[0] = std::min(1.f, d[0] + r * a * k);
                    d[1] = std::min(1.f, d[1] + g * a * k);
                    d[2] = std::min(1.f, d[2] + b * a * k);
                    break;
                case Blend::Subtract: {
                    // Freckles: the texture is how much darker the skin gets, taken off the skin.
                    const float w = a * k;
                    d[0] = std::max(0.f, d[0] - r * w);
                    d[1] = std::max(0.f, d[1] - g * w);
                    d[2] = std::max(0.f, d[2] - b * w);
                    break;
                }
                case Blend::Hair: {
                    // Eyebrows / body hair: green is the hair mask, coloured with the body hair colour.
                    const float m = g * a;
                    d[0] += (hr - d[0]) * m;
                    d[1] += (hg - d[1]) * m;
                    d[2] += (hb - d[2]) * m;
                    break;
                }
                case Blend::TwoTint: {
                    // Rugby League Live 4's eyebrows and stubble: green is the mask, coloured with the tint, by amount.
                    const float m = g * a * k;
                    d[0] += (hr - d[0]) * m;
                    d[1] += (hg - d[1]) * m;
                    d[2] += (hb - d[2]) * m;
                    break;
                }
                case Blend::CopyHairCap:
                    d[3] = std::max(d[3], a);
                    break;
                case Blend::FourTint:
                    if (fourTint) {
                        float cr = tints[0][0], cg = tints[0][1], cb = tints[0][2];
                        const float m[3] = {r, g, b};
                        for (int i = 0; i < 3 && i + 1 < tintCount; ++i) {
                            cr += (tints[i + 1][0] - cr) * m[i];
                            cg += (tints[i + 1][1] - cg) * m[i];
                            cb += (tints[i + 1][2] - cb) * m[i];
                        }
                        d[0] = cr / 255.f;
                        d[1] = cg / 255.f;
                        d[2] = cb / 255.f;
                        break;
                    }
                    [[fallthrough]];
                default:
                    // creator_skinlayer_col and the rest: drawn over by their alpha.
                    d[0] += (r - d[0]) * a;
                    d[1] += (g - d[1]) * a;
                    d[2] += (b - d[2]) * a;
                    break;
                }
            }
        }
        c.any = true;
    }
    return c;
}

uchar byte(float v) {
    return uchar(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

} // namespace

Placements parsePlacements(const QByteArray& data) {
    Placements out;
    if (data.size() < 4)
        return out;
    const quint32 count = qFromLittleEndian<quint32>(data.constData());
    if (qint64(count) * 32 + 4 > data.size())
        return out;
    for (quint32 i = 0; i < count; ++i) {
        const char* r = data.constData() + 4 + 32 * qint64(i);
        Placement p;
        p.x = int(qFromLittleEndian<quint32>(r + 8));
        p.y = int(qFromLittleEndian<quint32>(r + 12));
        p.width = int(qFromLittleEndian<quint32>(r + 16));
        p.height = int(qFromLittleEndian<quint32>(r + 20));
        p.fullWidth = int(qFromLittleEndian<quint32>(r + 24));
        p.fullHeight = int(qFromLittleEndian<quint32>(r + 28));
        out.insert(qFromLittleEndian<quint32>(r), p);
    }
    return out;
}

Placements parsePlacementsRll4(const QByteArray& data) {
    Placements out;
    if (data.size() < 4)
        return out;
    const quint32 count = qFromLittleEndian<quint32>(data.constData());
    if (qint64(count) * 92 + 4 > data.size())
        return out;
    for (quint32 i = 0; i < count; ++i) {
        const char* r = data.constData() + 4 + 92 * qint64(i);
        const QByteArray name = QByteArray(r, int(qstrnlen(r, 64))).toLower();
        Placement p;
        p.x = int(qFromLittleEndian<quint32>(r + 68));
        p.y = int(qFromLittleEndian<quint32>(r + 72));
        p.width = int(qFromLittleEndian<quint32>(r + 76));
        p.height = int(qFromLittleEndian<quint32>(r + 80));
        p.fullWidth = int(qFromLittleEndian<quint32>(r + 84));
        p.fullHeight = int(qFromLittleEndian<quint32>(r + 88));
        out.insert(BlobsetHash::fnv1a32(name.toStdString()), p);
    }
    return out;
}

QStringList texturePaths(const Rl26Character::MaterialTexture& texture, const std::function<bool(const QString&)>& exists) {
    QStringList out;
    if (texture.kind == Rl26Character::MaterialTexture::Kind::Single)
        out.append(levelled(texture.path, exists));
    for (const QList<Rl26Character::TextureLayer>* list : {&texture.layers, &texture.normalLayers, &texture.glossLayers})
        for (const Rl26Character::TextureLayer& layer : *list)
            out.append(levelled(layer.path, exists));
    return out;
}

// RLL4's strand textures are thin separate hairs (about a third of the clump covered). The game layers dozens of
// cards so they read solid; here, mip-mapped and blended once, they read as see-through. Fill the gaps between the
// hairs inside each clump (twice a local mean of the alpha, about 1/128 of the texture across) and keep the outer
// edges of the clumps soft.
static void fillStrandGaps(QImage& img) {
    const int w = img.width(), h = img.height(), r = std::max(2, w / 128);
    std::vector<uint32_t> sat(size_t(w + 1) * (h + 1), 0);   // Summed-area table of the alpha.
    for (int y = 0; y < h; ++y) {
        const uchar* s = img.constScanLine(y);
        uint32_t row = 0;
        for (int x = 0; x < w; ++x) {
            row += s[x * 4 + 3];
            sat[size_t(y + 1) * (w + 1) + x + 1] = sat[size_t(y) * (w + 1) + x + 1] + row;
        }
    }
    for (int y = 0; y < h; ++y) {
        uchar* d = img.scanLine(y);
        const int y0 = std::max(0, y - r), y1 = std::min(h, y + r + 1);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - r), x1 = std::min(w, x + r + 1);
            const uint32_t sum = sat[size_t(y1) * (w + 1) + x1] - sat[size_t(y0) * (w + 1) + x1]
                               - sat[size_t(y1) * (w + 1) + x0] + sat[size_t(y0) * (w + 1) + x0];
            const int fill = int(std::min<uint32_t>(255, 2 * sum / uint32_t((x1 - x0) * (y1 - y0))));
            d[x * 4 + 3] = uchar(std::max<int>(d[x * 4 + 3], fill));
        }
    }
}

Alpha alphaFor(const Rl26Character::MaterialTexture& texture) {
    if (texture.style == u"hair"_s || texture.style == u"lash"_s)
        return Alpha::Mask;
    if (texture.style == u"cap"_s)
        return Alpha::Blend;
    return Alpha::Opaque;
}

QImage compose(const Rl26Character::MaterialTexture& texture, const QHash<QString, QImage>& images,
               const Placements& placements, const std::function<bool(const QString&)>& exists, int maxSize) {
    using Kind = Rl26Character::MaterialTexture::Kind;
    const auto colourAt = [&](float v) {
        // Hair: the swatches' two colours mixed by the strand variation in the texture.
        const Rl26Character::Rgb& a = texture.colour1;
        const Rl26Character::Rgb& b = texture.colour2;
        const float shade = 0.85f + 0.3f * v;
        return std::array<float, 3>{std::min(255.f, (a.r + (b.r - a.r) * v) * shade),
                                    std::min(255.f, (a.g + (b.g - a.g) * v) * shade),
                                    std::min(255.f, (a.b + (b.b - a.b) * v) * shade)};
    };

    // ---- One texture (hair, beard, eyelashes, cap, hair tie).
    if (texture.kind == Kind::Single) {
        QImage src = images.value(levelled(texture.path, exists));
        if (src.isNull())
            return {};
        if (maxSize > 0 && (src.width() > maxSize || src.height() > maxSize))
            src = scaledStraight(src, src.width() >= src.height() ? maxSize : src.width() * maxSize / src.height(),
                                 src.height() >= src.width() ? maxSize : src.height() * maxSize / src.width());
        src = src.convertToFormat(QImage::Format_RGBA8888);
        if (!texture.coloured && texture.style != u"lash"_s)
            return src;
        QImage out(src.size(), QImage::Format_RGBA8888);
        const bool lash = texture.style == u"lash"_s, capStyle = texture.style == u"cap"_s;   // Once, not per pixel.
        for (int y = 0; y < src.height(); ++y) {
            const uchar* s = src.constScanLine(y);
            uchar* d = out.scanLine(y);
            for (int x = 0; x < src.width(); ++x, s += 4, d += 4) {
                std::array<float, 3> c;
                int alpha = s[3];
                if (texture.strandShading && !lash) {
                    // The cap texture is grey (its light / dark strands); the strand texture keeps the strands'
                    // variation in green and root (dark) to tip (light) in red.
                    const float shade = capStyle ? 0.8f + 0.35f * (s[0] / 255.f)   // Subtle: bright strands read as scalp.
                                                 : (0.5f + 0.8f * (s[1] / 255.f)) * (0.8f + 0.4f * (s[0] / 255.f));
                    const float base[3] = {std::max(texture.colour1.r, 34.f), std::max(texture.colour1.g, 29.f),
                                           std::max(texture.colour1.b, 25.f)};
                    c = {std::min(255.f, base[0] * shade), std::min(255.f, base[1] * shade), std::min(255.f, base[2] * shade)};
                    // RLL4's cap shader reads its alpha as a hairline mask: solid inside, a short fade at the edge.
                    alpha = std::min(255, capStyle ? alpha * 5 / 2 : alpha * 4 / 3);
                } else if (lash) {
                    c = {14, 11, 10};
                    alpha = std::min(255, alpha * 3 / 2);   // Thin cards: a fuller lash line.
                } else if (capStyle) {
                    c = {texture.colour1.r, texture.colour1.g, texture.colour1.b};
                    alpha = std::min(255, alpha * 3 / 2);   // The game's cap shader reads denser than the raw alpha.
                } else {
                    c = colourAt((s[0] + s[1] + s[2]) / (3.f * 255.f));
                    alpha = std::min(255, alpha * 4 / 3);   // Blended strands: as dense as the game's.
                }
                d[0] = uchar(c[0]);
                d[1] = uchar(c[1]);
                d[2] = uchar(c[2]);
                d[3] = uchar(alpha);
            }
        }
        if (texture.strandShading && !lash && !capStyle)
            fillStrandGaps(out);
        return out;
    }
    if (texture.kind != Kind::Layers || texture.layers.isEmpty())
        return {};

    const bool cap = texture.style == u"cap"_s;
    const Canvas c = layered(texture.layers, Purpose::Colour, cap, images, placements, exists, maxSize);
    if (!c.any)
        return {};
    QImage out(c.width, c.height, QImage::Format_RGBA8888);
    for (int y = 0; y < c.height; ++y) {
        const float* d = c.px.data() + size_t(y) * c.width * 4;
        uchar* o = out.scanLine(y);
        for (int x = 0; x < c.width; ++x, d += 4, o += 4) {
            if (cap) {
                // The beard stubble: the beard colour where the layers cover.
                o[0] = uchar(texture.colour1.r);
                o[1] = uchar(texture.colour1.g);
                o[2] = uchar(texture.colour1.b);
                o[3] = byte(d[3]);
                continue;
            }
            o[0] = byte(d[0]);
            o[1] = byte(d[1]);
            o[2] = byte(d[2]);
            o[3] = 255;   // The skin's alpha is its thickness (subsurface), not see-through.
        }
    }
    return out;
}

QImage composeNormal(const Rl26Character::MaterialTexture& texture, const QHash<QString, QImage>& images,
                     const Placements& placements, const std::function<bool(const QString&)>& exists, int maxSize) {
    if (texture.normalLayers.isEmpty())
        return {};
    const Canvas c = layered(texture.normalLayers, Purpose::Normal, false, images, placements, exists, maxSize);
    if (!c.any)
        return {};
    QImage out(c.width, c.height, QImage::Format_RGBA8888);
    for (int y = 0; y < c.height; ++y) {
        const float* d = c.px.data() + size_t(y) * c.width * 4;
        uchar* o = out.scanLine(y);
        for (int x = 0; x < c.width; ++x, d += 4, o += 4) {
            const float nx = d[0] * 2 - 1, ny = d[1] * 2 - 1;
            // Two channel normal maps (the eyes'): Z from X and Y.
            const float nz = d[2] < 0.05f ? std::sqrt(std::max(0.f, 1 - nx * nx - ny * ny)) : d[2] * 2 - 1;
            o[0] = byte(d[0]);
            o[1] = byte(d[1]);
            o[2] = byte(nz * 0.5f + 0.5f);
            o[3] = 255;
        }
    }
    return out;
}

QImage composeRoughness(const Rl26Character::MaterialTexture& texture, const QHash<QString, QImage>& images,
                        const Placements& placements, const std::function<bool(const QString&)>& exists, int maxSize) {
    if (texture.glossLayers.isEmpty())
        return {};
    const Canvas c = layered(texture.glossLayers, Purpose::Gloss, false, images, placements, exists, maxSize);
    if (!c.any)
        return {};
    // "mat": G is gloss. Qt wants roughness (R), and B (occlusion) goes in G.
    QImage out(c.width, c.height, QImage::Format_RGBA8888);
    for (int y = 0; y < c.height; ++y) {
        const float* d = c.px.data() + size_t(y) * c.width * 4;
        uchar* o = out.scanLine(y);
        for (int x = 0; x < c.width; ++x, d += 4, o += 4) {
            using Layout = Rl26Character::MaterialTexture::MatLayout;
            if (texture.matLayout == Layout::Rll4Cloth) {
                // RLL4's cloth: G is its roughness (about 0.75: matte); B the occlusion.
                o[0] = byte(std::clamp(d[1], 0.55f, 1.0f));
                o[1] = byte(d[2]);
            } else if (texture.matLayout == Layout::Rll4Skin) {
                // RLL4's skin: G gloss (about 0.34 on the face) gives a soft sheen, rougher in the pores and creases
                // (B, darker there); the occlusion is in alpha (B would darken those speckles like dirt).
                const float rough = 0.95f - 1.0f * d[1] + 0.35f * (1.0f - d[2]);
                o[0] = byte(std::clamp(rough, 0.35f, 1.0f));
                o[1] = byte(d[3]);
            } else {
                // Skin's gloss (G about 0.4 - 0.5) gives a satin roughness of about 0.65, not a wet one.
                o[0] = byte(std::clamp(0.3f + 0.75f * (1.0f - d[1]), 0.15f, 1.0f));
                o[1] = byte(d[2]);
            }
            o[2] = 0;
            o[3] = 255;
        }
    }
    return out;
}

} // namespace Rl26SkinCompositor
