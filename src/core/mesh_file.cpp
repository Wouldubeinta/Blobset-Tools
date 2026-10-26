#include "mesh_file.h"

#include <QFloat16>
#include <QHash>
#include <QStringList>
#include <QVector2D>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

constexpr quint32 kStaticMeshMagic = 0xFEDCBA02u;
constexpr quint32 kStaticMeshV1Magic = 0xFEDCBA01u;
constexpr quint32 kNoParent = 0xFFFFFFFFu;

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
// ones pack it, then the normals are computed). The last 4 bytes of the last stream are half2 UVs.
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
                           && f.ic == part.ic && f.lod == part.lod;
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
                vb.uvOffset = v4 ? 0 : int(s.stride) - 4;
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

    // Indices of every submesh, made absolute into the combined vertex buffer.
    QList<quint32> indices;
    indices.reserve(qsizetype(totalIndices));
    for (const Part& part : std::as_const(parts)) {
        const quint32 first = vbs[part.vb].base + part.vs;
        submeshes.append({first, part.vc, quint32(indices.size()), part.ic});
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

QByteArray MeshFile::toObj(const QString& name) const {
    QByteArray obj;
    obj += "# " + name.toUtf8() + " - exported by Blobset Tools\n";
    obj += "# format " + formatName.toUtf8() + ", " + QByteArray::number(vertexCount) + " vertices, "
           + QByteArray::number(triangleCount()) + " triangles\n";

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
