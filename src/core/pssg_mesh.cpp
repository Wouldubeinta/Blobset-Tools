// PSSG scene files (the "PhyreEngine" / Ego scene graph format of Rugby League Live 2 and AFL Live: stadiums,
// props, generated heads), decoded far enough for the Mesh Viewer. See MeshFile::parsePssg().
#include "mesh_file.h"

#include <QFloat16>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

/// Layout (big endian, every platform):
///   "PSSG", u32 data size, u32 attribute name count, u32 node name count
///   per node name: u32 id, u32 length, name, u32 attribute count, then per attribute u32 id, u32 length, name
///   then the node tree: u32 node id, u32 size (of what follows), u32 attribute bytes, the attributes
///   ({u32 id, u32 size, value}; strings are u32 length + characters), then children or the node's data.
/// A node's remainder is its children when it splits exactly into nodes, else its data.
class Pssg {
public:
    struct Node {
        std::string name;
        std::unordered_map<std::string, std::pair<qint64, qint64>> attrs;   // Value offset, size.
        std::vector<int> kids;
        qint64 dataStart = -1;
        qint64 dataEnd = -1;
    };

    explicit Pssg(const QByteArray& data) : m_data(data.constData()), m_size(data.size()) {}

    bool parse(std::string* error) {
        if (m_size < 16 || std::memcmp(m_data, "PSSG", 4) != 0) {
            *error = "Not a PSSG file.";
            return false;
        }
        const quint32 nodeNames = u32(12);
        qint64 p = 16;
        for (quint32 i = 0; i < nodeNames; ++i) {
            if (!fits(p, 8))
                return fail(error, "The PSSG name table is cut short.");
            const quint32 id = u32(p);
            const quint32 length = u32(p + 4);
            if (!fits(p + 8, length + 4))
                return fail(error, "The PSSG name table is cut short.");
            m_nodeNames[id] = std::string(m_data + p + 8, length);
            p += 8 + length;
            const quint32 attributes = u32(p);
            p += 4;
            for (quint32 a = 0; a < attributes; ++a) {
                if (!fits(p, 8))
                    return fail(error, "The PSSG name table is cut short.");
                const quint32 attributeId = u32(p);
                const quint32 attributeLength = u32(p + 4);
                if (!fits(p + 8, attributeLength))
                    return fail(error, "The PSSG name table is cut short.");
                m_attributeNames[attributeId] = std::string(m_data + p + 8, attributeLength);
                p += 8 + attributeLength;
            }
        }
        if (readNode(p, m_size, 0) < 0)
            return fail(error, "The PSSG node tree couldn't be read.");
        return true;
    }

    const std::vector<Node>& nodes() const { return m_nodes; }
    const Node& node(int index) const { return m_nodes[size_t(index)]; }

    quint32 u32(qint64 at) const { return fits(at, 4) ? qFromBigEndian<quint32>(m_data + at) : 0; }
    quint16 u16(qint64 at) const { return fits(at, 2) ? qFromBigEndian<quint16>(m_data + at) : 0; }
    float f32(qint64 at) const {
        const quint32 bits = u32(at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    float f16(qint64 at) const {
        const quint16 bits = u16(at);
        qfloat16 value;
        std::memcpy(&value, &bits, 2);
        return float(value);
    }
    bool fits(qint64 at, qint64 size) const { return at >= 0 && size >= 0 && at + size <= m_size; }

    // Attribute values: strings (u32 length + characters, references start with '#'), or a u32.
    bool has(const Node& node, const char* name) const { return node.attrs.count(name) > 0; }
    std::string text(const Node& node, const char* name) const {
        const auto it = node.attrs.find(name);
        if (it == node.attrs.end() || it->second.second < 4)
            return {};
        const quint32 length = u32(it->second.first);
        if (qint64(length) + 4 != it->second.second)
            return {};
        return std::string(m_data + it->second.first + 4, length);
    }
    quint32 number(const Node& node, const char* name) const {
        const auto it = node.attrs.find(name);
        return it == node.attrs.end() || it->second.second != 4 ? 0 : u32(it->second.first);
    }

    std::vector<int> children(const Node& node, const char* name) const {
        std::vector<int> out;
        for (const int kid : node.kids)
            if (m_nodes[size_t(kid)].name == name)
                out.push_back(kid);
        return out;
    }
    int child(const Node& node, const char* name) const {
        for (const int kid : node.kids)
            if (m_nodes[size_t(kid)].name == name)
                return kid;
        return -1;
    }

private:
    static bool fail(std::string* error, const char* message) {
        *error = message;
        return false;
    }

    // Reads the node at `at` (and its subtree); returns its index, -1 when it doesn't fit.
    int readNode(qint64 at, qint64 limit, int depth) {
        if (depth > 64 || !fits(at, 12))
            return -1;
        const quint32 id = u32(at);
        const qint64 end = at + 8 + qint64(u32(at + 4));
        const qint64 attributeEnd = at + 12 + qint64(u32(at + 8));
        const auto name = m_nodeNames.find(id);
        if (name == m_nodeNames.end() || end > limit || attributeEnd > end)
            return -1;

        const int index = int(m_nodes.size());
        m_nodes.emplace_back();
        m_nodes.back().name = name->second;
        for (qint64 q = at + 12; q + 8 <= attributeEnd;) {
            const quint32 attributeId = u32(q);
            const qint64 size = u32(q + 4);
            if (q + 8 + size > attributeEnd)
                break;
            const auto attribute = m_attributeNames.find(attributeId);
            if (attribute != m_attributeNames.end())
                m_nodes[size_t(index)].attrs[attribute->second] = {q + 8, size};
            q += 8 + size;
        }

        // Children when the rest splits exactly into known nodes.
        std::vector<qint64> starts;
        bool nodes = attributeEnd < end;
        for (qint64 r = attributeEnd; nodes && r < end;) {
            if (!fits(r, 12) || m_nodeNames.find(u32(r)) == m_nodeNames.end()) {
                nodes = false;
                break;
            }
            starts.push_back(r);
            r += 8 + qint64(u32(r + 4));
            if (r > end)
                nodes = false;
        }
        if (nodes) {
            for (const qint64 start : starts) {
                const int kid = readNode(start, end, depth + 1);
                if (kid < 0)
                    return -1;
                m_nodes[size_t(index)].kids.push_back(kid);
            }
        } else if (attributeEnd < end) {
            m_nodes[size_t(index)].dataStart = attributeEnd;
            m_nodes[size_t(index)].dataEnd = end;
        }
        return index;
    }

    const char* m_data;
    qint64 m_size;
    std::unordered_map<quint32, std::string> m_nodeNames;
    std::unordered_map<quint32, std::string> m_attributeNames;
    std::vector<Node> m_nodes;
};

// Row vector 4x4 matrices (PSSG TRANSFORM: 16 floats, translation in the last row).
struct Matrix {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Matrix operator*(const Matrix& b) const {
        Matrix r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += m[i * 4 + k] * b.m[k * 4 + j];
                r.m[i * 4 + j] = sum;
            }
        return r;
    }
    void point(const float* in, float* out) const {
        for (int j = 0; j < 3; ++j)
            out[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + m[12 + j];
    }
    void direction(const float* in, float* out) const {
        for (int j = 0; j < 3; ++j)
            out[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j];
        const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
        if (length > 1e-12f)
            for (int j = 0; j < 3; ++j)
                out[j] /= length;
    }
};

std::string withoutHash(const std::string& reference) {
    return !reference.empty() && reference[0] == '#' ? reference.substr(1) : reference;
}

// One stream of a data block, decoded to floats (`components` per element; missing ones stay 0).
bool decodeStream(const Pssg& pssg, const Pssg::Node& block, int subStream, std::string* renderType,
                  std::vector<float>* out, int components) {
    const std::vector<int> streams = pssg.children(block, "DATABLOCKSTREAM");
    const int dataNode = pssg.child(block, "DATABLOCKDATA");
    if (subStream < 0 || size_t(subStream) >= streams.size() || dataNode < 0)
        return false;
    const Pssg::Node& stream = pssg.node(streams[size_t(subStream)]);
    const Pssg::Node& data = pssg.node(dataNode);
    *renderType = pssg.text(stream, "renderType");
    const std::string type = pssg.text(stream, "dataType");
    const quint32 count = pssg.number(block, "elementCount");
    const qint64 offset = pssg.number(stream, "offset");
    const qint64 stride = pssg.number(stream, "stride");
    if (data.dataStart < 0 || stride <= 0 || count == 0)
        return false;

    out->assign(size_t(count) * size_t(components), 0.0f);
    for (quint32 i = 0; i < count; ++i) {
        const qint64 at = data.dataStart + offset + qint64(i) * stride;
        float* o = out->data() + size_t(i) * size_t(components);
        int n = 0;
        if (type.rfind("float", 0) == 0 || type.rfind("half", 0) == 0) {
            const bool half = type[0] == 'h';
            n = type.back() >= '1' && type.back() <= '4' ? type.back() - '0' : 1;
            const int size = half ? 2 : 4;
            if (at + qint64(n) * size > data.dataEnd)
                return false;
            for (int c = 0; c < std::min(n, components); ++c)
                o[c] = half ? pssg.f16(at + c * size) : pssg.f32(at + c * size);
        } else if (type == "11_11_10") {   // Packed normal / tangent: x 11 bits, y 11 bits, z 10 bits, signed.
            if (at + 4 > data.dataEnd)
                return false;
            const quint32 v = pssg.u32(at);
            const auto signedBits = [](quint32 x, int bits) {
                const int value = int(x & ((1u << bits) - 1));
                return value & (1 << (bits - 1)) ? value - (1 << bits) : value;
            };
            const float packed[3] = {signedBits(v, 11) / 1023.0f, signedBits(v >> 11, 11) / 1023.0f,
                                     signedBits(v >> 22, 10) / 511.0f};
            for (int c = 0; c < std::min(3, components); ++c)
                o[c] = packed[c];
        } else if (type == "uchar4" || type == "uchar4n") {
            if (at + 4 > data.dataEnd)
                return false;
            const quint32 v = pssg.u32(at);
            for (int c = 0; c < std::min(4, components); ++c)
                o[c] = float((v >> (24 - 8 * c)) & 0xFFu) / 255.0f;
        } else {
            return false;   // A type this doesn't know: the stream is left out.
        }
    }
    return true;
}

} // namespace

bool MeshFile::isPssg(const QByteArray& data) {
    return data.size() >= 16 && std::memcmp(data.constData(), "PSSG", 4) == 0;
}

bool MeshFile::parsePssg(const QByteArray& data, QString* error) {
    *this = MeshFile();
    Pssg pssg(data);
    std::string message;
    if (!pssg.parse(&message)) {
        if (error)
            *error = QString::fromStdString(message);
        return false;
    }
    const std::vector<Pssg::Node>& nodes = pssg.nodes();

    // Everything with an id ("#source0", "#streamJBBAM", "#blockFKLAM", "#es2_concrete-fx" refer to them).
    std::unordered_map<std::string, int> byId;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const std::string id = pssg.text(nodes[i], "id");
        if (!id.empty())
            byId.emplace(id, int(i));
    }

    // Surfaces: one per shader instance used, with its colour texture (the first "_col" texture; the others are
    // the stadium's lighting / environment maps and masks).
    std::unordered_map<std::string, int> surfaceIndex;
    const auto surfaceOf = [&](const std::string& shaderId) {
        const auto found = surfaceIndex.find(shaderId);
        if (found != surfaceIndex.end())
            return found->second;
        Material material;
        material.name = QString::fromStdString(shaderId);
        const auto shader = byId.find(shaderId);
        if (shader != byId.end()) {
            const Pssg::Node& instance = nodes[size_t(shader->second)];
            material.shader = QString::fromStdString(pssg.text(instance, "shaderGroup"));
            // The colour texture: the first "_col" / "_dif" one. The others are overlays and helpers: the uniform
            // and body decals (drawn over the colour with the second UV set), normal / mask / bump maps, mud, the
            // stadium's light maps and environment map.
            QString colour, first, decal;
            for (const int input : pssg.children(instance, "SHADERINPUT")) {
                const Pssg::Node& node = nodes[size_t(input)];
                for (const auto& attributeValue : node.attrs) {
                    const std::string& attribute = attributeValue.first;
                    if (attribute.rfind("texture", 0) != 0)
                        continue;
                    const QString path = QString::fromStdString(pssg.text(node, attribute.c_str())).toLower();
                    if (!path.contains(u'/'))
                        continue;
                    bool helper = path.contains(u"decal"_s) || path.contains(u"/mud/"_s) || path.contains(u"/lighting/"_s)
                                  || path.contains(u"env_map"_s);
                    for (const QString& suffix : {u"_nrm"_s, u"_msk"_s, u"_bmp"_s, u"_spc"_s, u"_clm"_s})
                        helper = helper || path.endsWith(suffix);
                    if (helper) {
                        // The decal layer (uniform logos, numbers, the body's): drawn over the colour texture.
                        if (decal.isEmpty() && path.contains(u"decal"_s) && !path.endsWith(u"_nrm"_s)
                            && !path.endsWith(u"_msk"_s) && !path.endsWith(u"_bmp"_s))
                            decal = path;
                        continue;
                    }
                    if (first.isEmpty())
                        first = path;
                    if (colour.isEmpty() && (path.endsWith(u"_col"_s) || path.endsWith(u"_dif"_s)))
                        colour = path;
                }
            }
            if (colour.isEmpty())
                colour = first;
            if (!colour.isEmpty())
                material.maps.append(qMakePair(u"col_map"_s, colour));
            // The back panel (as2_uniformback) draws the player's name and number there (made at run time); its
            // second UVs stretch over the whole decal sheet, so the sheet isn't drawn on it.
            if (!colour.isEmpty() && !decal.isEmpty() && !material.shader.contains(u"uniformback"_s))
                material.maps.append(qMakePair(u"decal_map"_s, decal));
            // "_pt" (punch through) shaders: seats, railings, fences, foliage; the crowd's sprites; hair.
            material.punchthrough = material.shader.contains(u"_pt."_s) || material.shader.contains(u"_pt_"_s)
                                    || material.shader.contains(u"crowdsprite"_s) || material.shader.contains(u"_hair"_s);
        }
        const int index = int(surfaces.size());
        surfaces.append(material.name);
        pssgMaterials.append(material);
        surfaceIndex.emplace(shaderId, index);
        return index;
    };

    std::vector<float> vertices;   // 8 floats each: position, normal, uv.
    std::vector<float> secondUv;   // 2 floats each: the second UV set (decals), 0 when an instance has none.
    std::vector<quint32> indices;
    bool anyNormals = false, anyUv = false;

    // Walks the scene from its root nodes, applying the node transforms.
    const auto drawInstance = [&](const Pssg::Node& instance, const Matrix& world) {
        const std::string shaderId = withoutHash(pssg.text(instance, "shader"));
        const auto shader = byId.find(shaderId);
        bool crowd = false;
        if (shader != byId.end()) {
            const std::string group = pssg.text(nodes[size_t(shader->second)], "shaderGroup");
            crowd = group.find("crowdsprite") != std::string::npos;
            // Sun flares / glows: sprites placed far out in the sky.
            if (group.find("lenseflare") != std::string::npos || group.find("spriteglow") != std::string::npos)
                return;
        }
        const auto source = byId.find(withoutHash(pssg.text(instance, "indices")));
        if (source == byId.end())
            return;
        const int indexSource = pssg.child(nodes[size_t(source->second)], "RENDERINDEXSOURCE");
        if (indexSource < 0)
            return;
        const Pssg::Node& indexNode = nodes[size_t(indexSource)];
        const int indexData = pssg.child(indexNode, "INDEXSOURCEDATA");
        if (indexData < 0 || nodes[size_t(indexData)].dataStart < 0)
            return;

        // The instance's streams, then its data source's own (skinned meshes, MODIFIERNETWORKINSTANCE, only list
        // their UVs; their skinnable vertices / normals are the source's streams the skinning network reads).
        std::vector<int> streamNodes;
        for (const int ri : pssg.children(instance, "RISTREAM")) {
            const auto stream = byId.find(withoutHash(pssg.text(nodes[size_t(ri)], "stream")));
            if (stream != byId.end())
                streamNodes.push_back(stream->second);
        }
        for (const int rs : pssg.children(nodes[size_t(source->second)], "RENDERSTREAM"))
            if (std::find(streamNodes.begin(), streamNodes.end(), rs) == streamNodes.end())
                streamNodes.push_back(rs);

        std::vector<float> positions, normals, uvs, uvs2;
        for (const int streamNode : streamNodes) {
            const Pssg::Node& renderStream = nodes[size_t(streamNode)];
            const auto block = byId.find(withoutHash(pssg.text(renderStream, "dataBlock")));
            if (block == byId.end())
                continue;
            const int subStream = int(pssg.number(renderStream, "subStream"));
            std::string renderType;
            std::vector<float> values;
            if (!decodeStream(pssg, nodes[size_t(block->second)], subStream, &renderType, &values, 3))
                continue;
            if ((renderType == "Vertex" || renderType == "SkinnableVertex") && positions.empty())
                positions = std::move(values);
            else if ((renderType == "Normal" || renderType == "SkinnableNormal") && normals.empty())
                normals = std::move(values);
            else if (renderType == "ST" && uvs.empty())
                uvs = std::move(values);   // The first UV set (the second is the stadium's light map).
            else if (renderType == "ST" && uvs2.empty())
                uvs2 = std::move(values);
        }
        if (positions.empty())
            return;
        const size_t count = positions.size() / 3;
        if (normals.size() != count * 3)
            normals.clear();
        if (uvs.size() != count * 3)
            uvs.clear();

        // The crowd (es2_crowdsprite): one quad per person, its 4 corners at (almost) the same spot; the shader
        // grows it into a billboard facing the way the normal points. The second UV set holds 2 x person + corner x
        // and the corner's y. Drawn here as a person sized quad showing one of the 8 x 4 people of the crowd atlas
        // (et00_atlas_col, 64 x 128 cells, stored upside down).
        if (crowd && uvs2.size() == count * 3 && uvs.size() == count * 3 && normals.size() == count * 3) {
            constexpr float kWidth = 0.55f, kHeight = 1.1f;
            // The corner stream is the one counting people (values past 1); the other is the light map's.
            const auto counts = [&](const std::vector<float>& set) {
                for (size_t v = 0; v < set.size(); v += 3)
                    if (set[v] > 1.5f)
                        return true;
                return false;
            };
            if (counts(uvs) && !counts(uvs2))
                std::swap(uvs, uvs2);
            uvs.assign(count * 3, 0.0f);
            for (size_t v = 0; v < count; ++v) {
                const float code = uvs2[v * 3];
                const int person = int(std::floor(code / 2));
                const float cornerX = code - 2.0f * float(person) >= 0.5f ? 1.0f : 0.0f;
                const float cornerY = uvs2[v * 3 + 1] >= 0.5f ? 1.0f : 0.0f;
                const float* n = &normals[v * 3];
                float right[3] = {n[2], 0.0f, -n[0]};   // up x facing
                const float length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
                if (length > 1e-6f) {
                    right[0] /= length;
                    right[2] /= length;
                } else {
                    right[0] = 1.0f;
                }
                float* p = &positions[v * 3];
                p[0] += (cornerX - 0.5f) * kWidth * right[0];
                p[1] += cornerY * kHeight;
                p[2] += (cornerX - 0.5f) * kWidth * right[2];
                const int cell = ((person % 32) + 32) % 32;
                uvs[v * 3] = (float(cell % 8) + cornerX) / 8.0f;
                uvs[v * 3 + 1] = (float(cell / 8) + cornerY) / 4.0f;   // Head at the cell's bottom (upside down).
            }
        }

        // Indices: triangles, or strips (0xFFFF / 0xFFFFFFFF restart the strip).
        const std::string format = pssg.text(indexNode, "format");
        const std::string primitive = pssg.text(indexNode, "primitive");
        const bool wide = format == "uint";
        const Pssg::Node& raw = nodes[size_t(indexData)];
        const qint64 available = (raw.dataEnd - raw.dataStart) / (wide ? 4 : 2);
        const qint64 total = std::min<qint64>(pssg.number(indexNode, "count"), available);
        std::vector<quint32> local;
        const quint32 restart = wide ? 0xFFFFFFFFu : 0xFFFFu;
        const auto at = [&](qint64 i) {
            return wide ? pssg.u32(raw.dataStart + i * 4) : quint32(pssg.u16(raw.dataStart + i * 2));
        };
        if (primitive == "triangles") {
            for (qint64 i = 0; i + 2 < total; i += 3)
                local.insert(local.end(), {at(i), at(i + 1), at(i + 2)});
        } else if (primitive == "tristrip" || primitive == "tristrips" || primitive == "triangleStrip") {
            qint64 start = 0;
            for (qint64 i = 0; i < total; ++i) {
                if (at(i) == restart) {
                    start = i + 1;
                    continue;
                }
                if (i - start < 2)
                    continue;
                const quint32 a = at(i - 2), b = at(i - 1), c = at(i);
                if (a == b || b == c || a == c)
                    continue;
                if ((i - start) % 2 == 0)
                    local.insert(local.end(), {a, b, c});
                else
                    local.insert(local.end(), {b, a, c});
            }
        } else {
            return;
        }
        for (size_t t = 0; t + 2 < local.size(); t += 3)
            if (local[t] >= count || local[t + 1] >= count || local[t + 2] >= count)
                return;   // Indices out of range: not something this understands.
        if (local.empty())
            return;

        // Normals from the faces when the instance has none.
        std::vector<float> computed;
        if (normals.empty()) {
            computed.assign(count * 3, 0.0f);
            for (size_t t = 0; t + 2 < local.size(); t += 3) {
                const float* a = &positions[local[t] * 3];
                const float* b = &positions[local[t + 1] * 3];
                const float* c = &positions[local[t + 2] * 3];
                const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 3; ++j)
                        computed[local[t + k] * 3 + j] += n[j];
            }
        } else {
            anyNormals = true;
        }
        const std::vector<float>& normalSource = normals.empty() ? computed : normals;
        anyUv = anyUv || !uvs.empty();

        Submesh sub;
        sub.vertexStart = quint32(vertices.size() / 8);
        sub.vertexCount = quint32(count);
        sub.indexStart = quint32(indices.size());
        sub.indexCount = quint32(local.size());
        sub.surface = surfaceOf(shaderId);
        for (size_t v = 0; v < count; ++v) {
            float out[8] = {};
            world.point(&positions[v * 3], out);
            world.direction(&normalSource[v * 3], out + 3);
            if (!uvs.empty()) {
                out[6] = uvs[v * 3];
                out[7] = uvs[v * 3 + 1];
            }
            vertices.insert(vertices.end(), out, out + 8);
            const bool second = !crowd && uvs2.size() == count * 3;
            secondUv.push_back(second ? uvs2[v * 3] : 0.0f);
            secondUv.push_back(second ? uvs2[v * 3 + 1] : 0.0f);
        }
        for (const quint32 index : local)
            indices.push_back(index + sub.vertexStart);
        submeshes.append(sub);
    };

    std::vector<std::pair<int, Matrix>> stack;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].name == "ROOTNODE")
            stack.push_back({int(i), Matrix()});
    while (!stack.empty()) {
        const auto [index, parent] = stack.back();
        stack.pop_back();
        const Pssg::Node& node = nodes[size_t(index)];
        Matrix world = parent;
        const int transform = pssg.child(node, "TRANSFORM");
        if (transform >= 0 && nodes[size_t(transform)].dataEnd - nodes[size_t(transform)].dataStart >= 64) {
            Matrix local;
            for (int k = 0; k < 16; ++k)
                local.m[k] = pssg.f32(nodes[size_t(transform)].dataStart + k * 4);
            world = local * parent;
        }
        for (const int kid : node.kids) {
            const std::string& name = nodes[size_t(kid)].name;
            // Skinned meshes (SKINNODE's MODIFIERNETWORKINSTANCE) are drawn in their bind pose.
            if (name == "RENDERSTREAMINSTANCE" || name == "MODIFIERNETWORKINSTANCE")
                drawInstance(nodes[size_t(kid)], world);
            else if (name == "NODE" || name == "RENDERNODE" || name == "JOINTNODE" || name == "LODVISIBLERENDERNODE"
                     || name == "SKINNODE")
                stack.push_back({kid, world});
        }
    }

    if (indices.empty()) {
        if (error)
            *error = u"The PSSG file has no mesh this can display."_s;
        *this = MeshFile();
        return false;
    }

    formatName = u"PSSG"_s;
    vertexCount = quint32(vertices.size() / 8);
    indexCount = quint32(indices.size());
    indexSize = 4;
    storedNormals = anyNormals;
    hasUv = anyUv;
    displayVertices = QByteArray(reinterpret_cast<const char*>(vertices.data()), qsizetype(vertices.size() * sizeof(float)));
    displayIndices = QByteArray(reinterpret_cast<const char*>(indices.data()), qsizetype(indices.size() * sizeof(quint32)));
    displayUv2 = QByteArray(reinterpret_cast<const char*>(secondUv.data()), qsizetype(secondUv.size() * sizeof(float)));
    float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (size_t v = 0; v < vertices.size(); v += 8)
        for (int j = 0; j < 3; ++j) {
            lo[j] = std::min(lo[j], vertices[v + j]);
            hi[j] = std::max(hi[j], vertices[v + j]);
        }
    boundsMin = QVector3D(lo[0], lo[1], lo[2]);
    boundsMax = QVector3D(hi[0], hi[1], hi[2]);
    return true;
}
