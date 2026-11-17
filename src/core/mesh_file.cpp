#include "mesh_file.h"

#include <QFloat16>
#include <QHash>
#include <QStringList>
#include <QVector2D>
#include <QtEndian>

#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

constexpr quint32 kStaticMeshMagic = 0xFEDCBA02u;
constexpr quint32 kStaticMeshV1Magic = 0xFEDCBA01u;
constexpr quint32 kNoParent = 0xFFFFFFFFu;
constexpr quint32 kMaterialMagic = 0x23456789u;
constexpr quint32 kSkinnedModelMagic = 0x12345678u;   // RL26 .modelcomponent.mdl
constexpr quint32 kBlendshapesMagic = 0x2480137Fu;    // RL26 .blendshapes.mdat

class Reader {
public:
    explicit Reader(const QByteArray& data, bool bigEndian = false) : m_data(data), m_bigEndian(bigEndian) {}

    bool fits(qint64 offset, qint64 size) const { return offset >= 0 && size >= 0 && offset + size <= m_data.size(); }

    quint32 u32(qint64 offset) const {
        if (!fits(offset, 4))
            return 0;
        return m_bigEndian ? qFromBigEndian<quint32>(m_data.constData() + offset)
                           : qFromLittleEndian<quint32>(m_data.constData() + offset);
    }

    quint16 u16(qint64 offset) const {
        if (!fits(offset, 2))
            return 0;
        return m_bigEndian ? qFromBigEndian<quint16>(m_data.constData() + offset)
                           : qFromLittleEndian<quint16>(m_data.constData() + offset);
    }

    float f32(qint64 offset) const {
        const quint32 bits = u32(offset);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }

    float f16(qint64 offset) const {
        if (!fits(offset, 2))
            return 0.0f;
        const quint16 bits = u16(offset);
        return float(qFromLittleEndian<qfloat16>(&bits));
    }

    /// Self relative pointer: the target is the pointer's own position plus its value.
    qint64 pointer(qint64 at) const { return at + u32(at); }

    QString cString(qint64 offset, int maxLength = 256) const {
        if (!fits(offset, 1))
            return {};
        const qint64 end = std::min<qint64>(m_data.size(), offset + maxLength);
        qint64 p = offset;
        while (p < end && m_data[p] != '\0')
            ++p;
        return QString::fromLatin1(m_data.constData() + offset, p - offset);
    }

private:
    const QByteArray& m_data;
    bool m_bigEndian = false;
};

bool failWith(QString* error, const QString& message) {
    if (error)
        *error = message;
    return false;
}

void appendFloat(QByteArray& out, float value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(float));
}

} // namespace

bool MeshFile::parse(const QByteArray& input, QString* error, qint64 mainSize) {
    *this = MeshFile();
    const bool split = mainSize >= 0 && mainSize <= input.size();
    const QByteArray data = split ? input.left(mainSize) : input;
    const Reader in(data);

    if (data.size() < 0x60)
        return failWith(error, u"The mesh is too small to hold a header."_s);

    // 01 BA DC FE little endian (PC), FE DC BA 01 big endian (PS3 / Xbox 360).
    if (in.u32(0) == kStaticMeshV1Magic || qFromBigEndian<quint32>(data.constData()) == kStaticMeshV1Magic)
        return parseLists(data, split ? input.mid(mainSize) : QByteArray(), 0x30, false, error);

    const quint32 magic = in.u32(0);
    if (magic != kStaticMeshMagic)
        return failWith(error, u"Not a static mesh (magic 0x%1)."_s.arg(magic, 8, 16, QLatin1Char('0')));

    // The full description (several vertex buffers, crowd / skinned formats, LODs). The simple single buffer
    // decoder below stays as a fallback.
    {
        const qint64 vertexData = in.pointer(0x30);
        if (in.fits(vertexData, 0)) {
            MeshFile full;
            QString fullError;
            if (full.parseLists(data, data.mid(vertexData), 0x38, true, &fullError)) {
                *this = std::move(full);
                return true;
            }
        }
        *this = MeshFile();
    }

    const quint32 sectionCount = in.u32(4);
    if (sectionCount < 6 || sectionCount > 64 || !in.fits(8, 4 * qint64(sectionCount)))
        return failWith(error, u"Unsupported mesh header (%1 sections)."_s.arg(sectionCount));

    QList<qint64> sections;
    for (quint32 k = 0; k < sectionCount; ++k)
        sections.append(in.u32(8 + 4 * k));

    // Vertex data start and index data end.
    const qint64 vertexStart = in.pointer(0x30);
    const qint64 indexEnd = in.pointer(0x34);
    if (!in.fits(vertexStart, 0) || !in.fits(indexEnd, 0) || indexEnd <= vertexStart)
        return failWith(error, u"The mesh's data pointers lie outside of the file."_s);

    // Vertex format and streams.
    const qint64 s1 = sections[1];
    formatName = in.cString(in.pointer(s1 + 8));
    const quint32 streamCount = in.u32(s1 + 20);
    if (streamCount == 0 || streamCount > 8)
        return failWith(error, u"Unsupported vertex layout (%1 streams)."_s.arg(streamCount));
    for (quint32 k = 0; k < streamCount; ++k) {
        const qint64 at = s1 + 24 + 20 * qint64(k);
        streams.append({in.u32(at), in.u32(at + 4), in.u32(at + 8)});
    }

    // Buffer sizes.
    const qint64 s4 = sections[4];
    const quint32 vertexBytes = in.u32(s4 + 8);
    indexSize = in.u32(s4 + 12);
    indexCount = in.u32(s4 + 16);
    if ((indexSize != 2 && indexSize != 4) || vertexStart + vertexBytes + qint64(indexCount) * indexSize != indexEnd)
        return failWith(error, u"The mesh's buffer sizes don't add up (format %1)."_s.arg(formatName));

    const Stream& positions = streams[0];
    if (positions.stride < 12 || positions.size % positions.stride != 0)
        return failWith(error, u"Unsupported vertex layout \"%1\" (stride %2)."_s.arg(formatName).arg(positions.stride));
    vertexCount = positions.size / positions.stride;
    for (const Stream& s : std::as_const(streams))
        if (qint64(s.offset) + s.size > vertexBytes)
            return failWith(error, u"A vertex stream lies outside of the vertex data."_s);

    // Submeshes: { vertexStart, vertexCount, -1, indexStart, indexCount } records that chain up to the totals.
    const qint64 searchFrom = sections.size() > 5 ? sections[5] : 0x38;
    for (qint64 p = searchFrom; p + 20 <= vertexStart && submeshes.isEmpty(); p += 4) {
        if (in.u32(p) != 0 || in.u32(p + 8) != kNoParent || in.u32(p + 12) != 0)
            continue;

        QList<Submesh> chain;
        quint32 nextVertex = 0, nextIndex = 0;
        qint64 q = p;
        while (q >= 0) {
            const Submesh sub{nextVertex, in.u32(q + 4), nextIndex, in.u32(q + 16)};
            if (sub.vertexCount == 0 || sub.indexCount == 0)
                break;
            chain.append(sub);
            nextVertex += sub.vertexCount;
            nextIndex += sub.indexCount;
            if (nextVertex == vertexCount && nextIndex == indexCount) {
                submeshes = chain;
                break;
            }
            if (nextVertex > vertexCount || nextIndex > indexCount)
                break;

            // The next record follows somewhere after this one.
            qint64 next = -1;
            for (qint64 r = q + 20; r + 20 <= vertexStart && r < q + 4096; r += 4) {
                if (in.u32(r) == nextVertex && in.u32(r + 8) == kNoParent && in.u32(r + 12) == nextIndex) {
                    next = r;
                    break;
                }
            }
            q = next;
        }
    }

    // Indices.
    const qint64 indexStart = vertexStart + vertexBytes;
    QList<quint32> indices(indexCount);
    for (quint32 k = 0; k < indexCount; ++k) {
        const qint64 at = indexStart + qint64(k) * indexSize;
        indices[k] = indexSize == 2 ? qFromLittleEndian<quint16>(data.constData() + at) : in.u32(at);
    }

    if (submeshes.isEmpty()) {
        // No submesh table found: fine when the indices address the whole vertex buffer.
        quint32 maxIndex = 0;
        for (quint32 v : std::as_const(indices))
            maxIndex = std::max(maxIndex, v);
        if (maxIndex >= vertexCount)
            return failWith(error, u"Couldn't find the mesh's submesh table (format %1)."_s.arg(formatName));
        submeshes.append({0, vertexCount, 0, indexCount});
    }

    displayIndices.resize(qsizetype(indexCount) * 4);
    auto* outIndex = reinterpret_cast<quint32*>(displayIndices.data());
    for (const Submesh& sub : std::as_const(submeshes)) {
        for (quint32 k = sub.indexStart; k < sub.indexStart + sub.indexCount; ++k) {
            if (indices[k] >= sub.vertexCount)
                return failWith(error, u"A submesh index is out of range."_s);
            outIndex[k] = indices[k] + sub.vertexStart;
        }
    }

    // Vertices: positions always, normals / UVs when the format is known.
    storedNormals = formatName.startsWith(u"PosNrm"_s) && positions.stride >= 24;
    hasUv = formatName.contains(u"UV"_s) && streams.size() > 1 && streams[1].stride >= 4;

    QList<QVector3D> pos(vertexCount), nrm(vertexCount);
    boundsMin = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    boundsMax = -boundsMin;
    for (quint32 v = 0; v < vertexCount; ++v) {
        const qint64 at = vertexStart + positions.offset + qint64(v) * positions.stride;
        const QVector3D p(in.f32(at), in.f32(at + 4), in.f32(at + 8));
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) || p.length() > 1e6f)
            return failWith(error, u"The vertex format \"%1\" isn't supported yet (its positions aren't plain floats)."_s
                                       .arg(formatName));
        pos[v] = p;
        boundsMin = QVector3D(std::min(boundsMin.x(), p.x()), std::min(boundsMin.y(), p.y()), std::min(boundsMin.z(), p.z()));
        boundsMax = QVector3D(std::max(boundsMax.x(), p.x()), std::max(boundsMax.y(), p.y()), std::max(boundsMax.z(), p.z()));
        if (storedNormals)
            nrm[v] = QVector3D(in.f32(at + 12), in.f32(at + 16), in.f32(at + 20));
    }

    if (!storedNormals) {
        // Area weighted face normals, summed per vertex.
        for (quint32 t = 0; t + 2 < indexCount; t += 3) {
            const quint32 a = outIndex[t], b = outIndex[t + 1], c = outIndex[t + 2];
            const QVector3D n = QVector3D::crossProduct(pos[b] - pos[a], pos[c] - pos[a]);
            nrm[a] += n;
            nrm[b] += n;
            nrm[c] += n;
        }
    }

    displayVertices.reserve(qsizetype(vertexCount) * kDisplayStride);
    for (quint32 v = 0; v < vertexCount; ++v) {
        const QVector3D n = nrm[v].length() > 0 ? nrm[v].normalized() : QVector3D(0, 1, 0);
        float u = 0, w = 0;
        if (hasUv) {
            const qint64 at = vertexStart + streams[1].offset + qint64(v) * streams[1].stride;
            u = in.f16(at);
            w = in.f16(at + 2);
        }
        for (float value : {pos[v].x(), pos[v].y(), pos[v].z(), n.x(), n.y(), n.z(), u, w})
            appendFloat(displayVertices, value);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Older layout (01 BA DC FE): Rugby League Live 3 (PC little endian, PS3 big endian) and Rugby League Live 4.
//
// The main section is a serialized structure with self relative pointers (target = pointer position + value);
// all buffer offsets are into the vram section:
//
//   0x00  magic, u32 section count + section offsets
//   0x30  shader list:        count, count pointers -> { pointer -> name ("daeSF_lite", ...) }
//   then  vertex buffer list: count, count pointers -> { stream count, streams { offset, size, stride, 1, 4 } }
//   then  index buffer list:  count, count pointers -> { offset, index size (2 / 4), index count }
//   later submesh list:       count, count pointers -> records holding the vertex buffer index (first u32) and
//                             { vertexStart, vertexCount, ?, indexStart, indexCount } at +24 (RLL4) / +28 (RLL3).
//                             Indices are relative to the submesh's vertexStart. With one index buffer per vertex
//                             buffer, a submesh uses the index buffer of its vertex buffer, else the only one.
//
// Stream 0 starts with a float3 position. A 32 byte stream 0 has a float3 normal at +12 for static meshes (skinned
// ones pack it, then the normals are computed). The last 4 bytes of the last stream are half2 UVs, except in a 24
// byte last stream (RLL3 lightmapped "daeSF": colour UVs, then lightmap UVs), where they're the 4 bytes before.
// Checked with a Python copy against every RLL4 PC mesh (2,569 of 2,570; the other one is empty) and 473 RLL3 PC +
// 400 RLL3 PS3 meshes (all of them).
// ---------------------------------------------------------------------------------------------------------------
bool MeshFile::parseLists(const QByteArray& mainPart, const QByteArray& vram, qint64 listStart, bool v4, QString* error) {
    legacyLayout = !v4;
    const bool bigEndian = !v4 && static_cast<uchar>(mainPart[0]) == 0xFE;
    const Reader in(mainPart, bigEndian);
    const Reader vr(vram, bigEndian);

    if (vram.isEmpty())
        return failWith(error, u"This mesh has no vertex data (its vram section is empty)."_s);

    // Shader names (V1 / V2) or vertex format names (V4, one per vertex buffer).
    qint64 p = listStart;
    // Counts are only sanity checked here; fits() makes sure every list really is inside the file. The big RLL3
    // stadium meshes (stadium_t1) have 260 - 465 shaders.
    constexpr quint32 kMaxListCount = 65536;
    const quint32 shaderCount = in.u32(p);
    if (shaderCount > kMaxListCount || !in.fits(p, 4 + 8 * qint64(shaderCount)))
        return failWith(error, u"Unsupported mesh header (%1 shaders)."_s.arg(shaderCount));
    QStringList shaders;
    QStringList listNames;
    for (quint32 k = 0; k < shaderCount; ++k) {
        const QString name = in.cString(in.pointer(in.pointer(p + 4 + 4 * qint64(k))));
        listNames.append(name);
        if (!name.isEmpty() && !shaders.contains(name))
            shaders.append(name);
    }
    p += 4 + 8 * qint64(shaderCount);

    // Vertex buffers.
    struct VertexBuffer {
        QList<Stream> streams;
        quint32 count = 0;
        quint32 base = 0;          // First vertex in the combined display buffer.
        bool storedNormals = false;
        int uvOffset = -1;         // Byte offset of the half2 UV in the last stream, -1: none.
    };
    const quint32 vbCount = in.u32(p);
    if (vbCount == 0 || vbCount > kMaxListCount || !in.fits(p, 4 + 4 * qint64(vbCount)))
        return failWith(error, u"Unsupported mesh header (%1 vertex buffers)."_s.arg(vbCount));

    QList<VertexBuffer> vbs;
    QStringList layouts;
    qint64 end = -1;
    for (quint32 k = 0; k < vbCount; ++k) {
        const qint64 d = in.pointer(p + 4 + 4 * qint64(k));
        const quint32 streamCount = in.u32(d);
        if (streamCount == 0 || streamCount > 8 || !in.fits(d, 4 + 20 * qint64(streamCount)))
            return failWith(error, u"Unsupported vertex layout (%1 streams)."_s.arg(streamCount));

        VertexBuffer vb;
        QStringList strides;
        for (quint32 j = 0; j < streamCount; ++j) {
            const qint64 at = d + 4 + 20 * qint64(j);
            const Stream s{in.u32(at), in.u32(at + 4), in.u32(at + 8)};
            if (s.stride == 0 || !vr.fits(s.offset, s.size))
                return failWith(error, u"A vertex stream lies outside of the vertex data."_s);
            vb.streams.append(s);
            strides.append(QString::number(s.stride));
        }
        const Stream& s0 = vb.streams[0];
        if (s0.stride < 12 || s0.size % s0.stride != 0)
            return failWith(error, u"Unsupported vertex layout (stride %1)."_s.arg(s0.stride));
        vb.count = s0.size / s0.stride;
        for (const Stream& s : std::as_const(vb.streams))
            if (qint64(s.size) < qint64(vb.count) * s.stride)
                return failWith(error, u"The mesh's vertex streams don't match in size."_s);

        layouts.append(strides.join(u'+'));
        streams.append(vb.streams);
        vbs.append(vb);
        end = d + 4 + 20 * qint64(streamCount);
    }

    // Index buffers.
    struct IndexBuffer {
        quint32 offset = 0;
        quint32 size = 2;
        quint32 count = 0;
    };
    const quint32 ibCount = in.u32(end);
    if (ibCount == 0 || ibCount > kMaxListCount || !in.fits(end, 4 + 4 * qint64(ibCount)))
        return failWith(error, u"Unsupported mesh header (%1 index buffers)."_s.arg(ibCount));
    QList<IndexBuffer> ibs;
    quint64 totalIndices = 0;
    for (quint32 k = 0; k < ibCount; ++k) {
        const qint64 d = in.pointer(end + 4 + 4 * qint64(k));
        const IndexBuffer ib{in.u32(d), in.u32(d + 4), in.u32(d + 8)};
        if ((ib.size != 2 && ib.size != 4) || !vr.fits(ib.offset, qint64(ib.count) * ib.size))
            return failWith(error, u"An index buffer lies outside of the vertex data."_s);
        ibs.append(ib);
        totalIndices += ib.count;
    }
    const auto indexAt = [&](const IndexBuffer& ib, quint32 k) -> quint32 {
        const qint64 at = ib.offset + qint64(k) * ib.size;
        return ib.size == 2 ? vr.u16(at) : vr.u32(at);
    };

    // Submesh list: the pointer array whose records are all valid and together cover every index. Large stadium
    // meshes list some pieces more than once (instances); each piece is drawn once.
    struct Part {
        int vb = 0;
        int ib = 0;
        quint32 vs = 0, vc = 0, is = 0, ic = 0;
        quint32 lod = 0;   // V4: detail level (0 = most detailed).
        quint32 surface = 0;
    };
    QList<Part> parts;
    for (qint64 a = end; a + 8 <= mainPart.size() && parts.isEmpty(); a += 4) {
        const quint32 n = in.u32(a);
        if (n == 0 || n > kMaxListCount || !in.fits(a, 4 + 4 * qint64(n)) || in.pointer(a + 4) != a + 4 + 4 * qint64(n))
            continue;

        for (const int vsOffset : {24, 28}) {
            QList<Part> found;
            bool ok = true;
            for (quint32 k = 0; k < n && ok; ++k) {
                const qint64 t = in.pointer(a + 4 + 4 * qint64(k));
                if (!in.fits(t, vsOffset + 20)) {
                    ok = false;
                    break;
                }
                Part part;
                part.vb = vbs.size() > 1 ? int(in.u32(t)) : 0;
                part.ib = ibs.size() == vbs.size() ? part.vb : 0;
                part.vs = in.u32(t + vsOffset);
                part.vc = in.u32(t + vsOffset + 4);
                part.is = in.u32(t + vsOffset + 12);
                part.ic = in.u32(t + vsOffset + 16);
                part.lod = v4 ? in.u32(t + vsOffset - 8) : 0;
                part.surface = in.u32(t + 8);   // Index into the surface list (checked against every RLL3,
                                                // RLL4 and RL26 sample: always a surface the material names).
                if (part.vb < 0 || part.vb >= vbs.size() || part.ib >= ibs.size() || part.vc == 0 || part.ic == 0
                    || part.ic % 3 != 0 || quint64(part.vs) + part.vc > vbs[part.vb].count
                    || quint64(part.is) + part.ic > ibs[part.ib].count) {
                    ok = false;
                    break;
                }
                for (quint32 i = part.is; i < part.is + part.ic; ++i) {
                    if (indexAt(ibs[part.ib], i) >= part.vc) {
                        ok = false;
                        break;
                    }
                }
                const bool duplicate = std::any_of(found.cbegin(), found.cend(), [&](const Part& f) {
                    return f.vb == part.vb && f.ib == part.ib && f.vs == part.vs && f.vc == part.vc && f.is == part.is
                           && f.ic == part.ic && f.lod == part.lod && f.surface == part.surface;
                });
                if (!duplicate)
                    found.append(part);
            }
            if (!ok)
                continue;

            std::vector<std::vector<char>> covered;
            for (const IndexBuffer& ib : std::as_const(ibs))
                covered.emplace_back(ib.count, char(0));
            for (const Part& part : std::as_const(found))
                std::fill_n(covered[part.ib].begin() + part.is, part.ic, char(1));
            const bool all = std::all_of(covered.cbegin(), covered.cend(), [](const std::vector<char>& c) {
                return std::find(c.cbegin(), c.cend(), char(0)) == c.cend();
            });
            if (all) {
                parts = found;
                break;
            }
        }
    }
    if (parts.isEmpty())
        return failWith(error, u"Couldn't find the mesh's submesh table."_s);

    // V4 crowd / prop meshes carry every LOD of the model; drawing them all stacks them on top of each other.
    if (v4) {
        QList<quint32> lods;
        for (const Part& part : std::as_const(parts))
            if (!lods.contains(part.lod))
                lods.append(part.lod);
        lodCount = int(lods.size());
        const quint32 best = *std::min_element(lods.cbegin(), lods.cend());
        parts.removeIf([best](const Part& part) { return part.lod != best; });
    }

    // Positions, stored normals and UVs, one vertex buffer after the other.
    QList<QVector3D> pos, nrm;
    QList<QVector2D> uv;
    for (qsizetype k = 0; k < vbs.size(); ++k) {
        VertexBuffer& vb = vbs[k];
        vb.base = quint32(pos.size());
        const Stream& s0 = vb.streams[0];
        const QString format = listNames.value(k, listNames.value(0));

        // Crowd meshes (V4 "crowdStream", "crowdStreamSkinned") store half float positions.
        const bool halfPositions = v4 && format.startsWith(u"crowdStream"_s);

        // Normals: the first candidate that is unit length for (nearly) every vertex.
        //   float3 at +12 (PosNrmTan, RLL4 static), float3 at +16 (daeSFskinned5), half3 at +8 (crowd),
        //   float3 in stream 1 (daeSFcpuskinned_vertCol).
        struct NormalSource { int stream; int offset; bool half; };
        NormalSource normals{-1, 0, false};
        for (const NormalSource& c : {NormalSource{0, 12, false}, NormalSource{0, 16, false}, NormalSource{0, 8, true},
                                      NormalSource{1, 0, false}}) {
            if (c.stream >= vb.streams.size() || (!v4 && (c.half || c.offset == 16)))
                continue;   // The V1 / V2 layouts only ever store float3 normals at +12 or in stream 1.
            const Stream& s = vb.streams[c.stream];
            if (s.stride < quint32(c.offset + (c.half ? 6 : 12)))
                continue;
            quint32 unit = 0;
            for (quint32 v = 0; v < vb.count; ++v) {
                const qint64 at = s.offset + qint64(v) * s.stride + c.offset;
                const QVector3D n = c.half ? QVector3D(vr.f16(at), vr.f16(at + 2), vr.f16(at + 4))
                                           : QVector3D(vr.f32(at), vr.f32(at + 4), vr.f32(at + 8));
                const float len = n.length();
                if (std::isfinite(len) && std::abs(len - 1.0f) < 0.05f)
                    ++unit;
            }
            if (vb.count > 0 && unit >= vb.count * 9 / 10) {
                normals = c;
                break;
            }
        }
        vb.storedNormals = normals.stream >= 0;

        // UVs: half2. V4: the first pair of stream 1 (PosNrmTan_1UV / _2UV); V1 / V2: the last 4 bytes of the
        // last stream.
        int uvStream = -1;
        if (vb.streams.size() > 1) {
            const Stream& s = v4 ? vb.streams[1] : vb.streams.last();
            const bool fits = v4 ? s.stride >= 4 : (s.stride == 4 || s.stride == 8 || s.stride == 16 || s.stride == 24);
            if (fits) {
                uvStream = v4 ? 1 : int(vb.streams.size()) - 1;
                // A 24 byte last stream (RLL3 "daeSF", lightmapped) ends with two half2 UV sets: the colour
                // texture's, then the lightmap's (a small area of the stadium's lightmap atlas).
                vb.uvOffset = v4 ? 0 : int(s.stride) - (s.stride == 24 ? 8 : 4);
                for (quint32 v = 0; v < vb.count && vb.uvOffset >= 0; ++v) {
                    const qint64 at = s.offset + qint64(v) * s.stride + vb.uvOffset;
                    const float u = vr.f16(at), w = vr.f16(at + 2);
                    if (!std::isfinite(u) || !std::isfinite(w) || std::abs(u) > 256 || std::abs(w) > 256)
                        vb.uvOffset = -1;
                }
            }
        }

        for (quint32 v = 0; v < vb.count; ++v) {
            const qint64 at = s0.offset + qint64(v) * s0.stride;
            const QVector3D p3 = halfPositions ? QVector3D(vr.f16(at), vr.f16(at + 2), vr.f16(at + 4))
                                               : QVector3D(vr.f32(at), vr.f32(at + 4), vr.f32(at + 8));
            if (!std::isfinite(p3.x()) || !std::isfinite(p3.y()) || !std::isfinite(p3.z()) || p3.length() > 1e6f)
                return failWith(error, u"The vertex positions of \"%1\" aren't plain floats."_s.arg(format));
            pos.append(p3);

            if (vb.storedNormals) {
                const Stream& s = vb.streams[normals.stream];
                const qint64 nAt = s.offset + qint64(v) * s.stride + normals.offset;
                nrm.append(normals.half ? QVector3D(vr.f16(nAt), vr.f16(nAt + 2), vr.f16(nAt + 4))
                                        : QVector3D(vr.f32(nAt), vr.f32(nAt + 4), vr.f32(nAt + 8)));
            } else {
                nrm.append(QVector3D());
            }

            if (vb.uvOffset >= 0) {
                const Stream& s = vb.streams[uvStream];
                const qint64 uvAt = s.offset + qint64(v) * s.stride + vb.uvOffset;
                uv.append(QVector2D(vr.f16(uvAt), vr.f16(uvAt + 2)));
            } else {
                uv.append(QVector2D());
            }
        }
        storedNormals = storedNormals || vb.storedNormals;
        hasUv = hasUv || vb.uvOffset >= 0;
    }

    // Surface names: the header's pointer at 0x24 -> count, count pointers -> { pointer -> name }.
    {
        const qint64 list = in.pointer(0x24);
        const quint32 count = in.u32(list);
        if (count > 0 && count <= kMaxListCount && in.fits(list, 4 + 4 * qint64(count))) {
            for (quint32 k = 0; k < count; ++k)
                surfaces.append(in.cString(in.pointer(in.pointer(list + 4 + 4 * qint64(k)))));
        }
    }
    // V4: the material block follows the mesh description (u32 at 0x34: its offset from 0x38).
    if (v4) {
        const auto isMaterial = [&](qint64 at) { return at >= 0 && in.fits(at, 16) && in.u32(at) == kMaterialMagic; };
        qint64 at = 0x38 + qint64(in.u32(0x34));
        if (!isMaterial(at)) {
            char magic[4];
            qToLittleEndian(kMaterialMagic, magic);
            at = mainPart.indexOf(QByteArrayView(magic, 4), 0x38);
        }
        if (isMaterial(at))
            materialBlock = mainPart.mid(at);
    }

    // Indices of every submesh, made absolute into the combined vertex buffer.
    QList<quint32> indices;
    indices.reserve(qsizetype(totalIndices));
    for (const Part& part : std::as_const(parts)) {
        const quint32 first = vbs[part.vb].base + part.vs;
        submeshes.append({first, part.vc, quint32(indices.size()), part.ic,
                          part.surface < quint32(surfaces.size()) ? int(part.surface) : -1});
        for (quint32 i = part.is; i < part.is + part.ic; ++i)
            indices.append(first + indexAt(ibs[part.ib], i));
    }

    // Normals for vertex buffers that don't store plain ones: area weighted face normals.
    for (qsizetype t = 0; t + 2 < indices.size(); t += 3) {
        const quint32 a = indices[t], b = indices[t + 1], c = indices[t + 2];
        const QVector3D n = QVector3D::crossProduct(pos[b] - pos[a], pos[c] - pos[a]);
        for (const quint32 v : {a, b, c}) {
            int owner = 0;
            while (owner + 1 < vbs.size() && v >= vbs[owner + 1].base)
                ++owner;
            if (!vbs[owner].storedNormals)
                nrm[v] += n;
        }
    }

    vertexCount = quint32(pos.size());
    indexCount = quint32(indices.size());
    indexSize = ibs[0].size;
    vertexBufferCount = int(vbs.size());
    // Vertex layouts, repeats grouped: big stadium meshes have hundreds of buffers ("12+24 x290 | 12+8 x40").
    QStringList layoutSummary;
    {
        QStringList distinct;
        QHash<QString, int> uses;
        for (const QString& layout : std::as_const(layouts)) {
            if (!uses.contains(layout))
                distinct.append(layout);
            uses[layout] += 1;
        }
        for (const QString& layout : std::as_const(distinct))
            layoutSummary.append(uses.value(layout) > 1 ? u"%1 x%2"_s.arg(layout).arg(uses.value(layout)) : layout);
    }
    formatName = (shaders.isEmpty() ? (v4 ? u"Mesh"_s : u"Legacy mesh"_s) : shaders.join(u", "_s)) + u" ("_s
                 + layoutSummary.join(u" | "_s) + u")"_s;
    if (lodCount > 1)
        formatName += u", LOD 0 of %1"_s.arg(lodCount);

    // Bounds of what is drawn (other LODs' vertices aren't).
    boundsMin = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    boundsMax = -boundsMin;
    for (const quint32 i : std::as_const(indices)) {
        const QVector3D& p3 = pos[i];
        boundsMin = QVector3D(std::min(boundsMin.x(), p3.x()), std::min(boundsMin.y(), p3.y()), std::min(boundsMin.z(), p3.z()));
        boundsMax = QVector3D(std::max(boundsMax.x(), p3.x()), std::max(boundsMax.y(), p3.y()), std::max(boundsMax.z(), p3.z()));
    }

    displayVertices.reserve(qsizetype(vertexCount) * kDisplayStride);
    for (quint32 v = 0; v < vertexCount; ++v) {
        const QVector3D& p3 = pos[v];
        const QVector3D n = nrm[v].length() > 0 ? nrm[v].normalized() : QVector3D(0, 1, 0);
        for (float value : {p3.x(), p3.y(), p3.z(), n.x(), n.y(), n.z(), uv[v].x(), uv[v].y()})
            appendFloat(displayVertices, value);
    }

    displayIndices.resize(qsizetype(indexCount) * 4);
    std::memcpy(displayIndices.data(), indices.constData(), displayIndices.size());
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// RL26 skinned model: "<part>.modelcomponent.mdl" (magic 0x12345678, model\skinned\N.mdl when unnamed) and its
// "<part>.blendshapes.mdat" (magic 0x2480137F). Little endian (PC).
//
// .modelcomponent.mdl
//   0x04  u32 version (19)
//   0x08  u32 offset of the mesh records (0x24), 0x0C u32 record count (one per mesh and LOD)
//   0x1C  pointer -> index data (self relative)
//   0x20  pointer -> vertex data (self relative), runs to the end of the file
//   record (0x90 bytes)
//     +0x08 u32 LOD (0 = most detailed)          +0x0C u32 vertex offset   +0x10 u32 vertex count
//     +0x18 u32 index offset                       +0x1C u32 index count     +0x20 u32 material index
//     +0x24 float3 min, float3 max (cm)            +0x3C u32[8] vertices by bone count (1 - 5 bones)
//     +0x70 char[32] vertex format ("daeSFskinned5", ...)
//   0x14  pointer -> the materials, 0x18 u32 their count: 88 bytes each, char[32] name ("head_mat", "hairMaterial",
//         ...), char[32] shader ("skin", "hair", ...), then their parameters.
//   A record's vertices are stored stream after stream (not interleaved): 48 bytes per vertex left empty (filled
//   from the .mdat), 10 bytes of bone indices, then the rest (half2 UV first; "_vertCol" / "_decals" add 4 bytes).
//   The vertex size is where the next record starts (62 or 66 bytes). Indices are 16 bit and relative to the record.
//
// .blendshapes.mdat
//   0x08  pointer -> the base vertices (self relative), 0x0C u32 their size: 48 bytes per vertex of every record,
//         in record order: float3 position (metres) + 4, float3 normal + 4, float3 tangent + 4.
//   0x10  pointer -> the blend shapes, 0x14 u32 their count; per shape (16 bytes): pointer -> name, pointer -> runs,
//         pointer -> offsets, u32 run count. A run is { u32 byte offset into the base vertices, u32 byte length,
//         u32 48 }: the vertices it moves; the offsets are float4 (x, y, z, 1) per moved vertex, run after run. A
//         shape is listed once per mesh / LOD it touches. The game sets the weights from the appearance options
//         (Rl26Character::shapeWeights).
//
// Checked with a Python copy against 750 random RL26 models (body, head, hair, beard, decal meshes): all decode.
// ---------------------------------------------------------------------------------------------------------------
bool MeshFile::isSkinnedModel(const QByteArray& data) {
    return data.size() >= 0x24 && qFromLittleEndian<quint32>(data.constData()) == kSkinnedModelMagic;
}

bool MeshFile::parseSkinned(const QByteArray& model, const QByteArray& blendshapes, QString* error) {
    *this = MeshFile();
    if (!isSkinnedModel(model))
        return failWith(error, u"Not a skinned model."_s);
    const Reader in(model);
    const Reader bs(blendshapes);

    constexpr qint64 kRecordSize = 0x90;
    constexpr quint32 kBaseVertexSize = 48;   // Position, normal, tangent from the .mdat.
    constexpr quint32 kSkinBytes = 10;        // Bone indices.
    const qint64 recordStart = in.u32(8);
    const quint32 recordCount = in.u32(12);
    if (recordCount == 0 || recordCount > 1024 || !in.fits(recordStart, kRecordSize * recordCount))
        return failWith(error, u"Unsupported skinned model header (%1 meshes)."_s.arg(recordCount));
    const qint64 indexData = in.pointer(0x1C);
    const qint64 vertexData = in.pointer(0x20);
    if (indexData <= 0 || vertexData < indexData || vertexData > model.size())
        return failWith(error, u"The skinned model's data pointers lie outside of the file."_s);
    const qint64 indexBytes = vertexData - indexData;
    const qint64 vertexBytes = model.size() - vertexData;

    struct Record {
        quint32 lod = 0;
        qint64 vertexOffset = 0;
        quint32 vertexCount = 0;
        qint64 indexOffset = 0;
        quint32 indexCount = 0;
        quint32 stride = 0;
        quint32 indexSize = 2;
        quint32 base = 0;   // First vertex in the .mdat.
        quint32 material = 0;
        QString format;
    };
    QList<Record> records;
    for (quint32 k = 0; k < recordCount; ++k) {
        const qint64 r = recordStart + kRecordSize * k;
        Record rec;
        rec.lod = in.u32(r + 0x08);
        rec.vertexOffset = in.u32(r + 0x0C);
        rec.vertexCount = in.u32(r + 0x10);
        rec.indexOffset = in.u32(r + 0x18);
        rec.indexCount = in.u32(r + 0x1C);
        rec.material = in.u32(r + 0x20);
        rec.format = in.cString(r + 0x70, 32);
        records.append(rec);
    }

    // Vertex sizes and index sizes: up to where the next record's data starts.
    quint32 totalVertices = 0;
    for (qsizetype k = 0; k < records.size(); ++k) {
        Record& rec = records[k];
        const bool last = k + 1 == records.size();
        const qint64 nextVertex = last ? vertexBytes : records[k + 1].vertexOffset;
        const qint64 nextIndex = last ? indexBytes : records[k + 1].indexOffset;
        const qint64 span = nextVertex - rec.vertexOffset;
        if (rec.vertexCount == 0 || span <= 0 || span % rec.vertexCount != 0
            || span / rec.vertexCount < kBaseVertexSize + kSkinBytes + 4 || span / rec.vertexCount > 256)
            return failWith(error, u"Unsupported skinned vertex layout (%1)."_s.arg(rec.format));
        rec.stride = quint32(span / rec.vertexCount);
        rec.indexSize = nextIndex - rec.indexOffset >= qint64(rec.indexCount) * 4 ? 4 : 2;
        if (rec.indexCount % 3 != 0 || rec.indexOffset + qint64(rec.indexCount) * rec.indexSize > indexBytes)
            return failWith(error, u"The skinned model's index data doesn't add up."_s);
        rec.base = totalVertices;
        totalVertices += rec.vertexCount;
    }

    // The base vertices (.blendshapes.mdat).
    if (blendshapes.size() < 16 || bs.u32(0) != kBlendshapesMagic)
        return failWith(error, u"This skinned model's vertex positions are in its .blendshapes.mdat, which wasn't found "
                               u"in the loaded blobset."_s);
    const qint64 baseVertices = bs.pointer(8);
    if (bs.u32(12) != quint64(totalVertices) * kBaseVertexSize
        || !bs.fits(baseVertices, qint64(totalVertices) * kBaseVertexSize))
        return failWith(error, u"The .blendshapes.mdat found doesn't belong to this model (vertex count differs)."_s);

    // Only the most detailed LOD is drawn.
    QList<quint32> lods;
    for (const Record& rec : std::as_const(records))
        if (!lods.contains(rec.lod))
            lods.append(rec.lod);
    lodCount = int(lods.size());
    const quint32 best = *std::min_element(lods.cbegin(), lods.cend());

    // Material names: the submeshes' surfaces.
    {
        const qint64 list = in.pointer(0x14);
        const quint32 count = in.u32(0x18);
        if (count <= 256 && in.fits(list, 88 * qint64(count)))
            for (quint32 k = 0; k < count; ++k)
                surfaces.append(in.cString(list + 88 * qint64(k), 32));
    }

    QList<QVector3D> pos, nrm;
    QList<QVector2D> uv;
    QList<quint32> indices;
    QStringList formats;
    int drawn = 0;
    std::vector<qint64> displayOf(totalVertices, -1);   // .mdat vertex -> display vertex (drawn LOD only).
    QByteArray colours;
    bool hasColours = false;
    for (const Record& rec : std::as_const(records)) {
        if (rec.lod != best)
            continue;
        ++drawn;
        if (!rec.format.isEmpty() && !formats.contains(rec.format))
            formats.append(rec.format);
        const quint32 first = quint32(pos.size());
        const qint64 uvStream = vertexData + rec.vertexOffset + qint64(kBaseVertexSize + kSkinBytes) * rec.vertexCount;
        const quint32 uvStride = rec.stride - kBaseVertexSize - kSkinBytes;
        for (quint32 v = 0; v < rec.vertexCount; ++v) {
            const qint64 at = baseVertices + qint64(rec.base + v) * kBaseVertexSize;
            const QVector3D p(bs.f32(at), bs.f32(at + 4), bs.f32(at + 8));
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) || p.length() > 1e6f)
                return failWith(error, u"The skinned model's vertex positions aren't plain floats."_s);
            displayOf[rec.base + v] = pos.size();
            pos.append(p);
            nrm.append(QVector3D(bs.f32(at + 16), bs.f32(at + 20), bs.f32(at + 24)));
            const qint64 uvAt = uvStream + qint64(v) * uvStride;
            const float u = in.f16(uvAt), w = in.f16(uvAt + 2);
            uv.append(std::isfinite(u) && std::isfinite(w) ? QVector2D(u, w) : QVector2D());
            // "_vertCol" / "_decals": a colour after the UV.
            if (uvStride >= 8 && in.fits(uvAt + 4, 4)) {
                colours.append(model.constData() + uvAt + 4, 4);
                hasColours = true;
            } else {
                colours.append("\xff\xff\xff\xff", 4);
            }
        }
        const quint32 indexStart = quint32(indices.size());
        for (quint32 i = 0; i < rec.indexCount; ++i) {
            const qint64 at = indexData + rec.indexOffset + qint64(i) * rec.indexSize;
            const quint32 index = rec.indexSize == 2 ? in.u16(at) : in.u32(at);
            if (index >= rec.vertexCount)
                return failWith(error, u"A skinned model index is out of range."_s);
            indices.append(first + index);
        }
        submeshes.append({first, rec.vertexCount, indexStart, rec.indexCount,
                          rec.material < quint32(surfaces.size()) ? int(rec.material) : -1});
    }

    // Blend shapes (of the drawn vertices).
    {
        const qint64 list = bs.pointer(0x10);
        const quint32 count = bs.u32(0x14);
        if (count <= 4096 && bs.fits(list, 16 * qint64(count))) {
            for (quint32 k = 0; k < count; ++k) {
                const qint64 e = list + 16 * qint64(k);
                const QString name = bs.cString(bs.pointer(e), 128);
                const qint64 runs = bs.pointer(e + 4);
                qint64 offsets = bs.pointer(e + 8);
                const quint32 runCount = bs.u32(e + 12);
                if (name.isEmpty() || runCount > 1000000 || !bs.fits(runs, 12 * qint64(runCount)))
                    continue;
                Morph& morph = morphs[name];
                for (quint32 r = 0; r < runCount; ++r) {
                    const quint32 offset = bs.u32(runs + 12 * qint64(r));
                    const quint32 length = bs.u32(runs + 12 * qint64(r) + 4);
                    for (quint32 g = offset / kBaseVertexSize; g < (offset + length) / kBaseVertexSize; ++g) {
                        if (!bs.fits(offsets, 16))
                            break;
                        if (g < totalVertices && displayOf[g] >= 0) {
                            morph.vertices.append(quint32(displayOf[g]));
                            morph.deltas.append(QVector3D(bs.f32(offsets), bs.f32(offsets + 4), bs.f32(offsets + 8)));
                        }
                        offsets += 16;
                    }
                }
                if (morph.vertices.isEmpty())
                    morphs.remove(name);
            }
        }
    }

    if (hasColours)
        vertexColours = colours;
    skinned = true;
    storedNormals = true;
    hasUv = true;
    indexSize = records.first().indexSize;
    vertexBufferCount = drawn;
    vertexCount = quint32(pos.size());
    indexCount = quint32(indices.size());
    formatName = (formats.isEmpty() ? u"Skinned"_s : formats.join(u", "_s)) + u" (skinned)"_s;
    if (lodCount > 1)
        formatName += u", LOD 0 of %1"_s.arg(lodCount);

    boundsMin = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    boundsMax = -boundsMin;
    for (const QVector3D& p : std::as_const(pos)) {
        boundsMin = QVector3D(std::min(boundsMin.x(), p.x()), std::min(boundsMin.y(), p.y()), std::min(boundsMin.z(), p.z()));
        boundsMax = QVector3D(std::max(boundsMax.x(), p.x()), std::max(boundsMax.y(), p.y()), std::max(boundsMax.z(), p.z()));
    }

    displayVertices.reserve(qsizetype(vertexCount) * kDisplayStride);
    for (quint32 v = 0; v < vertexCount; ++v) {
        const QVector3D n = nrm[v].length() > 0 ? nrm[v].normalized() : QVector3D(0, 1, 0);
        for (float value : {pos[v].x(), pos[v].y(), pos[v].z(), n.x(), n.y(), n.z(), uv[v].x(), uv[v].y()})
            appendFloat(displayVertices, value);
    }
    displayIndices.resize(qsizetype(indexCount) * 4);
    std::memcpy(displayIndices.data(), indices.constData(), displayIndices.size());
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Rugby League Live 3 / 4 skinned parts (version 16 of the skinned model). Three files per part, little endian:
//
// .modelcomponent.mdl (magic 0x12345678, u32 version 16). Pointers are self relative.
//   0x08  pointer -> vertex declaration: u32, u32 element count, then { char[16] name, u16 type, u16 count } per
//         element ("Position", "Normal", ..., "TexCoord"; one "TexCoord" per UV set)
//   0x10  pointer -> mesh records, 0x14 u32 record count; record (0x4C bytes):
//     +0x00 u32 LOD (0 = most detailed)    +0x04 u32 vertex byte offset (.vbo vram)   +0x08 u32 vertex count
//     +0x0C u32 index byte offset (.vbo main)                                         +0x10 u32 index count
//     +0x18 u32 material                    +0x20 float3 min, float3 max (cm)          +0x38 u32[5] vertices by bones
//   0x1C  pointer -> materials, 0x20 u32 their count: 88 bytes each, char[32] name first ("asics_body", ...).
//
// .vbo.idx: main section = 16 bit indices, relative to their record. Vram section = each record's vertices, stream
//   after stream; the last stream holds the UV sets (half2 each, interleaved: set 0, set 1, ...), starting at
//   offset + (vertex size - 4 * UV sets) * vertex count. The vertex size is where the next record (in offset order)
//   starts.
//
// .blendshapes.mdat (magic 0x2480137F, u32 version 4)
//   0x10  pointer -> the base vertices, 0x14 u32 their size: one vertex per vertex of every record, the records in
//         vertex offset order. 0x20 pointer -> { u32 vertex size, u32 count, ... } per record (0x24 u32 count).
//         RLL4 (48 bytes): float3 position (m) + 4, float3 normal + 4, float3 tangent + 4.
//         RLL3 (32 bytes): float3 position (m), then packed normal / tangent / skinning (normals are computed).
//   0x18  pointer -> the blend shapes, 0x1C u32 their count. A shape moves runs of base vertices { u32 first,
//         u32 count }:
//         RLL3: 16 byte entries { pointer name, pointer deltas, pointer runs, u32 run count }, 28 bytes per moved
//               vertex (float3 position offset first).
//         RLL4: 24 byte entries { pointer name, pointer runs, pointer deltas, pointer deltas end, u32 moved count,
//               u32 }, float4 (x, y, z, 1) per moved vertex.
//
// RLL3's hair and beards are built 1.5 m lower (fbxprocessing-hair.lua: offsetYValue = -1.5); their records' bounds
// say where they belong, so a record whose bounds sit 1.5 m above its vertices is lifted.
// Checked against RLL3 and RLL4 heads, bodies, hair, beards, kits and boots (Python copy, rendered).
// ---------------------------------------------------------------------------------------------------------------
bool MeshFile::isLegacySkinnedModel(const QByteArray& data) {
    return isSkinnedModel(data) && qFromLittleEndian<quint32>(data.constData() + 4) == 16;
}

bool MeshFile::parseSkinned(const QByteArray& model, const QByteArray& blendshapes, const QByteArray& vbo,
                            qint64 vboMainSize, QString* error) {
    if (isLegacySkinnedModel(model))
        return parseSkinnedV16(model, blendshapes, vbo, vboMainSize, error);
    return parseSkinned(model, blendshapes, error);
}

bool MeshFile::parseSkinnedV16(const QByteArray& model, const QByteArray& blendshapes, const QByteArray& vbo,
                               qint64 vboMainSize, QString* error) {
    *this = MeshFile();
    const Reader in(model);
    const Reader bs(blendshapes);
    const Reader vb(vbo);
    if (blendshapes.size() < 0x28 || bs.u32(0) != kBlendshapesMagic)
        return failWith(error, u"This skinned model's vertex positions are in its .blendshapes.mdat, which wasn't found "
                               u"in the loaded blobset."_s);
    if (vbo.isEmpty() || vboMainSize <= 0 || vboMainSize > vbo.size())
        return failWith(error, u"This skinned model's indices and UVs are in its .vbo.idx, which wasn't found in the "
                               u"loaded blobset."_s);
    const qint64 vram = vboMainSize;               // Where the vram section starts in `vbo`.
    const qint64 vramSize = vbo.size() - vboMainSize;

    // UV sets in the vertex declaration.
    int uvSets = 0;
    {
        const qint64 decl = in.pointer(0x08);
        const quint32 count = in.u32(decl + 4);
        if (count == 0 || count > 32 || !in.fits(decl + 8, 20 * qint64(count)))
            return failWith(error, u"Unsupported skinned model vertex declaration."_s);
        for (quint32 k = 0; k < count; ++k)
            if (in.cString(decl + 8 + 20 * qint64(k), 16) == u"TexCoord"_s)
                ++uvSets;
    }

    constexpr qint64 kRecordSize = 0x4C;
    const qint64 recordStart = in.pointer(0x10);
    const quint32 recordCount = in.u32(0x14);
    if (recordCount == 0 || recordCount > 1024 || !in.fits(recordStart, kRecordSize * recordCount))
        return failWith(error, u"Unsupported skinned model header (%1 meshes)."_s.arg(recordCount));

    struct Record {
        quint32 lod = 0;
        qint64 vertexOffset = 0;
        quint32 vertexCount = 0;
        qint64 indexOffset = 0;
        quint32 indexCount = 0;
        quint32 material = 0;
        float minY = 0;
        quint32 stride = 0;
        quint32 indexSize = 2;
        quint32 base = 0;   // First vertex in the .mdat.
    };
    QList<Record> records;
    for (quint32 k = 0; k < recordCount; ++k) {
        const qint64 r = recordStart + kRecordSize * k;
        Record rec;
        rec.lod = in.u32(r);
        rec.vertexOffset = in.u32(r + 0x04);
        rec.vertexCount = in.u32(r + 0x08);
        rec.indexOffset = in.u32(r + 0x0C);
        rec.indexCount = in.u32(r + 0x10);
        rec.material = in.u32(r + 0x18);
        rec.minY = in.f32(r + 0x24);
        if (rec.vertexCount == 0 || rec.indexCount % 3 != 0)
            return failWith(error, u"The skinned model's mesh records don't add up."_s);
        records.append(rec);
    }

    // Vertex sizes, index sizes and .mdat positions: the records in vertex (and index) data order.
    QList<qsizetype> byVertex(records.size()), byIndex(records.size());
    std::iota(byVertex.begin(), byVertex.end(), 0);
    std::iota(byIndex.begin(), byIndex.end(), 0);
    std::sort(byVertex.begin(), byVertex.end(),
              [&](qsizetype a, qsizetype b) { return records[a].vertexOffset < records[b].vertexOffset; });
    std::sort(byIndex.begin(), byIndex.end(),
              [&](qsizetype a, qsizetype b) { return records[a].indexOffset < records[b].indexOffset; });
    quint32 totalVertices = 0;
    for (qsizetype k = 0; k < byVertex.size(); ++k) {
        Record& rec = records[byVertex[k]];
        const qint64 next = k + 1 < byVertex.size() ? records[byVertex[k + 1]].vertexOffset : vramSize;
        const qint64 span = next - rec.vertexOffset;
        if (span <= 0 || span % rec.vertexCount != 0 || span / rec.vertexCount < 4 * qint64(std::max(uvSets, 1)))
            return failWith(error, u"Unsupported skinned vertex layout."_s);
        rec.stride = quint32(span / rec.vertexCount);
        rec.base = totalVertices;
        totalVertices += rec.vertexCount;
    }
    for (qsizetype k = 0; k < byIndex.size(); ++k) {
        Record& rec = records[byIndex[k]];
        const qint64 next = k + 1 < byIndex.size() ? records[byIndex[k + 1]].indexOffset : vboMainSize;
        rec.indexSize = next - rec.indexOffset >= qint64(rec.indexCount) * 4 ? 4 : 2;
        if (rec.indexOffset + qint64(rec.indexCount) * rec.indexSize > vboMainSize)
            return failWith(error, u"The skinned model's index data doesn't add up."_s);
    }

    // The base vertices.
    const qint64 baseVertices = bs.pointer(0x10);
    const quint32 baseBytes = bs.u32(0x14);
    const quint32 baseSize = bs.u32(bs.pointer(0x20));
    if ((baseSize != 32 && baseSize != 48) || baseBytes != quint64(totalVertices) * baseSize
        || !bs.fits(baseVertices, baseBytes))
        return failWith(error, u"The .blendshapes.mdat found doesn't belong to this model (vertex count differs)."_s);
    const bool packedNormals = baseSize == 32;   // RLL3

    QList<quint32> lods;
    for (const Record& rec : std::as_const(records))
        if (!lods.contains(rec.lod))
            lods.append(rec.lod);
    lodCount = int(lods.size());
    const quint32 best = *std::min_element(lods.cbegin(), lods.cend());

    {
        const qint64 list = in.pointer(0x1C);
        const quint32 count = in.u32(0x20);
        if (count <= 256 && in.fits(list, 88 * qint64(count)))
            for (quint32 k = 0; k < count; ++k)
                surfaces.append(in.cString(list + 88 * qint64(k), 32));
    }

    QList<QVector3D> pos, nrm;
    QList<QVector2D> uv;
    QList<quint32> indices;
    int drawn = 0;
    std::vector<qint64> displayOf(totalVertices, -1);   // .mdat vertex -> display vertex (drawn LOD only).
    for (const Record& rec : std::as_const(records)) {
        if (rec.lod != best)
            continue;
        ++drawn;
        const quint32 first = quint32(pos.size());
        float lowest = std::numeric_limits<float>::max();
        for (quint32 v = 0; v < rec.vertexCount; ++v) {
            const qint64 at = baseVertices + qint64(rec.base + v) * baseSize;
            const QVector3D p(bs.f32(at), bs.f32(at + 4), bs.f32(at + 8));
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) || p.length() > 1e4f)
                return failWith(error, u"The skinned model's vertex positions aren't plain floats."_s);
            lowest = std::min(lowest, p.y());
            displayOf[rec.base + v] = pos.size();
            pos.append(p);
            nrm.append(packedNormals ? QVector3D() : QVector3D(bs.f32(at + 16), bs.f32(at + 20), bs.f32(at + 24)));
            const qint64 uvAt = vram + rec.vertexOffset
                                + (qint64(rec.stride) - 4 * qint64(uvSets)) * rec.vertexCount
                                + 4 * qint64(uvSets) * v;   // The UV sets share one stream (set 0 first).
            const float u = uvSets > 0 ? vb.f16(uvAt) : 0.0f, w = uvSets > 0 ? vb.f16(uvAt + 2) : 0.0f;
            uv.append(std::isfinite(u) && std::isfinite(w) ? QVector2D(u, w) : QVector2D());
        }
        // RLL3 hair / beards: built 1.5 m lower than where they're drawn.
        if (std::abs(rec.minY / 100.0f - lowest - 1.5f) < 0.05f)
            for (qsizetype v = first; v < pos.size(); ++v)
                pos[v].setY(pos[v].y() + 1.5f);

        const quint32 indexStart = quint32(indices.size());
        for (quint32 i = 0; i < rec.indexCount; ++i) {
            const qint64 at = rec.indexOffset + qint64(i) * rec.indexSize;
            const quint32 index = rec.indexSize == 2 ? vb.u16(at) : vb.u32(at);
            if (index >= rec.vertexCount)
                return failWith(error, u"A skinned model index is out of range."_s);
            indices.append(first + index);
        }
        submeshes.append({first, rec.vertexCount, indexStart, rec.indexCount,
                          rec.material < quint32(surfaces.size()) ? int(rec.material) : -1});
    }

    // Normals from the faces (RLL3 packs its normals).
    if (packedNormals) {
        for (qsizetype t = 0; t + 2 < indices.size(); t += 3) {
            const quint32 a = indices[t], b = indices[t + 1], c = indices[t + 2];
            const QVector3D n = QVector3D::crossProduct(pos[b] - pos[a], pos[c] - pos[a]);
            nrm[a] += n;
            nrm[b] += n;
            nrm[c] += n;
        }
    }

    // Blend shapes (of the drawn vertices).
    {
        const qint64 list = bs.pointer(0x18);
        const quint32 count = bs.u32(0x1C);
        const qint64 entrySize = packedNormals ? 16 : 24;
        const qint64 deltaSize = packedNormals ? 28 : 16;
        if (count <= 4096 && bs.fits(list, entrySize * qint64(count))) {
            for (quint32 k = 0; k < count; ++k) {
                const qint64 e = list + entrySize * qint64(k);
                const QString name = bs.cString(bs.pointer(e), 128);
                qint64 runs, deltas;
                quint32 runCount, moved;
                if (packedNormals) {
                    deltas = bs.pointer(e + 4);
                    runs = bs.pointer(e + 8);
                    runCount = bs.u32(e + 12);
                    moved = std::numeric_limits<quint32>::max();
                } else {
                    runs = bs.pointer(e + 4);
                    deltas = bs.pointer(e + 8);
                    moved = bs.u32(e + 16);
                    runCount = moved;   // At most one run per moved vertex.
                }
                if (name.isEmpty() || runCount > 1000000)
                    continue;
                Morph& morph = morphs[name];
                quint32 done = 0;
                for (quint32 r = 0; r < runCount && done < moved; ++r) {
                    if (!bs.fits(runs + 8 * qint64(r), 8))
                        break;
                    const quint32 start = bs.u32(runs + 8 * qint64(r));
                    const quint32 length = bs.u32(runs + 8 * qint64(r) + 4);
                    for (quint32 g = start; g < start + length && done < moved; ++g, ++done) {
                        const qint64 d = deltas + deltaSize * qint64(done);
                        if (!bs.fits(d, 12))
                            break;
                        if (g < totalVertices && displayOf[g] >= 0) {
                            morph.vertices.append(quint32(displayOf[g]));
                            morph.deltas.append(QVector3D(bs.f32(d), bs.f32(d + 4), bs.f32(d + 8)));
                        }
                    }
                }
                if (morph.vertices.isEmpty())
                    morphs.remove(name);
            }
        }
    }

    skinned = true;
    storedNormals = !packedNormals;
    hasUv = uvSets > 0;
    indexSize = records.first().indexSize;
    vertexBufferCount = drawn;
    vertexCount = quint32(pos.size());
    indexCount = quint32(indices.size());
    formatName = u"Skinned (RLL, %1 bytes per vertex)"_s.arg(baseSize);
    if (lodCount > 1)
        formatName += u", LOD 0 of %1"_s.arg(lodCount);

    boundsMin = QVector3D(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    boundsMax = -boundsMin;
    for (const QVector3D& p : std::as_const(pos)) {
        boundsMin = QVector3D(std::min(boundsMin.x(), p.x()), std::min(boundsMin.y(), p.y()), std::min(boundsMin.z(), p.z()));
        boundsMax = QVector3D(std::max(boundsMax.x(), p.x()), std::max(boundsMax.y(), p.y()), std::max(boundsMax.z(), p.z()));
    }

    displayVertices.reserve(qsizetype(vertexCount) * kDisplayStride);
    for (quint32 v = 0; v < vertexCount; ++v) {
        const QVector3D n = nrm[v].length() > 0 ? nrm[v].normalized() : QVector3D(0, 1, 0);
        for (float value : {pos[v].x(), pos[v].y(), pos[v].z(), n.x(), n.y(), n.z(), uv[v].x(), uv[v].y()})
            appendFloat(displayVertices, value);
    }
    displayIndices.resize(qsizetype(indexCount) * 4);
    std::memcpy(displayIndices.data(), indices.constData(), displayIndices.size());
    return true;
}

QByteArray MeshFile::morphedVertices(const QHash<QString, float>& weights) const {
    QByteArray out = displayVertices;
    auto* v = reinterpret_cast<float*>(out.data());
    const qsizetype count = out.size() / kDisplayStride;
    for (auto it = weights.cbegin(); it != weights.cend(); ++it) {
        const auto morph = morphs.constFind(it.key());
        if (morph == morphs.constEnd() || it.value() == 0)
            continue;
        const float w = it.value();
        for (qsizetype i = 0; i < morph->vertices.size(); ++i) {
            const qsizetype at = qsizetype(morph->vertices.at(i));
            if (at >= count)
                continue;
            const QVector3D& d = morph->deltas.at(i);
            v[at * 8] += w * d.x();
            v[at * 8 + 1] += w * d.y();
            v[at * 8 + 2] += w * d.z();
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Material block (magic 0x23456789): RLL3 / RLL4 "<model>.material.mat", embedded in the V4 (RL26) meshes.
//
//   0x00  magic, u32 version (2)
//   0x08  pointer -> surface list: count, count pointers -> surface
//   surface  +0 pointer -> name ("props/goalposts/.../cornerPostPadding_Top", the mesh's surface name)
//            +4 pointer -> shader ("rugby/es2_goalposts")
//            +8 u32 group count, then a pointer per group -> count, count { pointer -> key, pointer -> value }
//   The texture group's keys end in "_map" ("col_map", "nrm_map", "mat_map", ...) and its values are texture
//   paths without an extension; the other groups hold numbers and options ("opt_punchthrough": the colour
//   texture's alpha cuts holes - fences, light towers, roof atlases).
// ---------------------------------------------------------------------------------------------------------------
QString MeshFile::Material::map(const QString& slot) const {
    for (const auto& entry : maps)
        if (entry.first.compare(slot, Qt::CaseInsensitive) == 0)
            return entry.second;
    return {};
}

QList<MeshFile::Material> MeshFile::parseMaterials(const QByteArray& data) {
    QList<Material> out;
    if (data.size() < 16)
        return out;
    const bool bigEndian = qFromBigEndian<quint32>(data.constData()) == kMaterialMagic;
    if (!bigEndian && qFromLittleEndian<quint32>(data.constData()) != kMaterialMagic)
        return out;
    const Reader in(data, bigEndian);
    const auto text = [&](qint64 pointerAt) -> QString {
        if (!in.fits(pointerAt, 4))
            return {};
        const QString s = in.cString(in.pointer(pointerAt), 512);
        for (const QChar c : s)
            if (c.unicode() < 0x20 || c.unicode() > 0x7E)
                return {};
        return s;
    };

    constexpr quint32 kMaxCount = 4096;
    const qint64 list = in.pointer(8);
    const quint32 count = in.u32(list);
    if (count == 0 || count > kMaxCount || !in.fits(list, 4 + 4 * qint64(count)))
        return out;
    for (quint32 k = 0; k < count; ++k) {
        const qint64 surface = in.pointer(list + 4 + 4 * qint64(k));
        if (!in.fits(surface, 12))
            continue;
        Material material;
        material.name = text(surface);
        material.shader = text(surface + 4);
        const quint32 groups = in.u32(surface + 8);
        for (quint32 g = 0; g < groups && g < 64 && in.fits(surface + 12 + 4 * qint64(g), 4); ++g) {
            const qint64 group = in.pointer(surface + 12 + 4 * qint64(g));
            const quint32 entries = in.u32(group);
            if (entries > kMaxCount || !in.fits(group, 4 + 8 * qint64(entries)))
                continue;
            for (quint32 e = 0; e < entries; ++e) {
                const QString key = text(group + 4 + 8 * qint64(e));
                if (key.compare(u"opt_punchthrough"_s, Qt::CaseInsensitive) == 0) {
                    // An option: its value is 0 when off (1 in the first byte on the PC when on).
                    const qint64 value = in.pointer(group + 8 + 8 * qint64(e));
                    material.punchthrough = in.fits(value, 4) && in.u32(value) != 0;
                    continue;
                }
                if (!key.endsWith(u"_map"_s, Qt::CaseInsensitive))
                    continue;
                const QString value = text(group + 8 + 8 * qint64(e));
                if (!value.isEmpty())
                    material.maps.append({key, value});
            }
        }
        if (!material.name.isEmpty())
            out.append(material);
    }
    return out;
}

QByteArray MeshFile::toObj(const QString& name, const QString& mtlFile, const QStringList& submeshMaterials) const {
    QByteArray obj;
    obj += "# " + name.toUtf8() + " - exported by Blobset Tools\n";
    obj += "# format " + formatName.toUtf8() + ", " + QByteArray::number(vertexCount) + " vertices, "
           + QByteArray::number(triangleCount()) + " triangles\n";
    if (!mtlFile.isEmpty())
        obj += "mtllib " + mtlFile.toUtf8() + '\n';

    const auto* v = reinterpret_cast<const float*>(displayVertices.constData());
    for (quint32 k = 0; k < vertexCount; ++k)
        obj += "v " + QByteArray::number(v[k * 8], 'g', 7) + ' ' + QByteArray::number(v[k * 8 + 1], 'g', 7) + ' '
               + QByteArray::number(v[k * 8 + 2], 'g', 7) + '\n';
    for (quint32 k = 0; k < vertexCount; ++k)
        obj += "vn " + QByteArray::number(v[k * 8 + 3], 'g', 6) + ' ' + QByteArray::number(v[k * 8 + 4], 'g', 6) + ' '
               + QByteArray::number(v[k * 8 + 5], 'g', 6) + '\n';
    if (hasUv) {
        for (quint32 k = 0; k < vertexCount; ++k)  // OBJ's V runs bottom up.
            obj += "vt " + QByteArray::number(v[k * 8 + 6], 'g', 6) + ' ' + QByteArray::number(1.0f - v[k * 8 + 7], 'g', 6)
                   + '\n';
    }

    const auto* idx = reinterpret_cast<const quint32*>(displayIndices.constData());
    for (int s = 0; s < submeshes.size(); ++s) {
        const Submesh& sub = submeshes[s];
        obj += "g submesh_" + QByteArray::number(s) + '\n';
        if (!mtlFile.isEmpty() && !submeshMaterials.value(s).isEmpty())
            obj += "usemtl " + submeshMaterials.value(s).toUtf8() + '\n';
        for (quint32 t = sub.indexStart; t + 2 < sub.indexStart + sub.indexCount; t += 3) {
            obj += 'f';
            for (int c = 0; c < 3; ++c) {
                const QByteArray i = QByteArray::number(idx[t + c] + 1);
                obj += ' ' + i + (hasUv ? "/" + i + "/" + i : "//" + i);
            }
            obj += '\n';
        }
    }
    return obj;
}
