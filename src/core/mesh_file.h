#ifndef MESH_FILE_H
#define MESH_FILE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVector3D>

/// BigAnt static mesh, decoded far enough to display and export it. Two layouts:
///
///  - 02 BA DC FE  (V4, Rugby League 26): everything in the main section, described below.
///  - 01 BA DC FE  (V1 / V2: Rugby League Live 3 PC + PS3, Rugby League Live 4): the main section only holds the
///    description, the vertex and index buffers are in the vram section. See parseLists() in mesh_file.cpp.
///
/// Both layouts describe the mesh with the same lists (vertex formats / shaders, vertex buffers, index buffers,
/// submesh records); V4 meshes may have several vertex buffers, other vertex formats and LODs, which parseLists()
/// handles (only the most detailed LOD is shown).
///
/// The header is a serialized structure whose pointers are self relative (target = pointer position + value):
///
///   0x00  u32 magic 0xFEDCBA02
///   0x04  u32 section count, then one u32 offset per section (absolute)
///   0x30  pointer -> vertex data          (vertex streams, then the index buffer right behind them)
///   0x34  pointer -> end of the index data
///   section 1   +8 pointer -> vertex format name ("PosNrmTan_1UV", ...)
///               +20 u32 stream count, then per stream { offset, size, stride, 1, 4 }
///   section 4   +8 u32 vertex data bytes, +12 u32 index size (2 / 4), +16 u32 index count
///   submeshes   { vertexStart, vertexCount, 0xFFFFFFFF, indexStart, indexCount } records, one after the other;
///               the indices of a submesh are relative to its vertexStart
///
/// "PosNrmTan_*" stream 0 (32 bytes): float3 position, float3 normal, packed tangent.
/// Stream 1: half2 UVs (one pair per UV set). Other vertex formats only have a known float3 position.
/// Found by decoding Rugby League 26 (PC): 95 % of its static meshes check out against every invariant.
class MeshFile {
public:
    struct Stream {
        quint32 offset = 0;
        quint32 size = 0;
        quint32 stride = 0;
    };

    struct Submesh {
        quint32 vertexStart = 0;
        quint32 vertexCount = 0;
        quint32 indexStart = 0;
        quint32 indexCount = 0;
    };

    /// Parses a .mesh. `data` is the main section, followed by the vram section when `mainSize` >= 0 (the older
    /// layout keeps its buffers there). Returns false (and the reason in `error`) when it can't be decoded.
    bool parse(const QByteArray& data, QString* error = nullptr, qint64 mainSize = -1);

    QString formatName;
    QList<Stream> streams;
    QList<Submesh> submeshes;
    quint32 vertexCount = 0;
    quint32 indexCount = 0;
    quint32 indexSize = 2;
    bool storedNormals = false;   // Normals come from the file (otherwise they are computed from the faces).
    bool hasUv = false;

    // Display / export data: interleaved float position (3), normal (3), uv (2) per vertex, and 32 bit indices with
    // each submesh's vertexStart already added, so the whole mesh draws as one triangle list.
    static constexpr int kDisplayStride = 8 * int(sizeof(float));
    QByteArray displayVertices;
    QByteArray displayIndices;
    QVector3D boundsMin;
    QVector3D boundsMax;

    quint32 triangleCount() const { return indexCount / 3; }
    bool legacyLayout = false;     // 01 BA DC FE (V1 / V2 era)
    int lodCount = 1;              // Detail levels in the file (only the most detailed one is displayed).
    int vertexBufferCount = 1;

    /// Wavefront OBJ of the display data (one group per submesh).
    QByteArray toObj(const QString& name) const;

private:
    // main: the mesh description; data: where its buffer offsets point (V1/V2: the vram section, V4: the main
    // section from the vertex data on); listStart: where the format / shader list starts (V1/V2 0x30, V4 0x38).
    bool parseLists(const QByteArray& mainPart, const QByteArray& data, qint64 listStart, bool v4, QString* error);
};

#endif // MESH_FILE_H
