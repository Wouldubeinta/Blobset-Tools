#ifndef MESH_TOOL_MANAGER_H
#define MESH_TOOL_MANAGER_H

#include "mesh_file.h"

#include <QFuture>
#include <QList>
#include <QMap>
#include <QObject>
#include <QTemporaryDir>
#include <QVariantList>
#include <QQmlEngine>
#include <QString>
#include <QVector3D>
#include <QtQuick3D/QQuick3DGeometry>

#include <atomic>
#include <memory>

/// The decoded mesh as Qt Quick 3D geometry: one triangle list, float position / normal / uv per vertex.
class MeshGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit MeshGeometry(QQuick3DObject* parent = nullptr);

    void setMesh(const MeshFile& mesh);
    // Some submeshes of the mesh only (the ones drawn with one material).
    void setPart(const MeshFile& mesh, const QByteArray& vertices, const QList<int>& submeshes);
    void clearMesh();

    // The mesh's display vertices with V flipped: the meshes' UVs are top-left based (Direct3D), Qt Quick 3D's
    // bottom-left based (OpenGL).
    static QByteArray quick3DVertices(const MeshFile& mesh);
};

/// MeshViewer.qml: loads a .mesh out of the loaded blobset, shows it in 3D and extracts it (original or OBJ).
class MeshToolManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(MeshGeometry* geometry READ geometry CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY meshChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY meshChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY meshChanged)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY meshChanged)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY meshChanged)
    Q_PROPERTY(int submeshCount READ submeshCount NOTIFY meshChanged)
    Q_PROPERTY(QVector3D center READ center NOTIFY meshChanged)
    Q_PROPERTY(QVector3D size READ size NOTIFY meshChanged)
    Q_PROPERTY(float radius READ radius NOTIFY meshChanged)    // Bounding sphere, for the camera distance.
    Q_PROPERTY(QString infoLine READ infoLine NOTIFY meshChanged)

    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)          // An OBJ export (with textures) is running.
    // What is drawn: { geometry, texture (file URL, "" = none), transparent }. The whole mesh untextured until the
    // textures are in, then one part per material.
    Q_PROPERTY(QVariantList parts READ parts NOTIFY partsChanged)
    Q_PROPERTY(QString textureInfo READ textureInfo NOTIFY partsChanged)   // "12 textures", "Loading textures..."
    // The textures loading in the background: progress 0 - 1 and what it's doing.
    Q_PROPERTY(bool texturesLoading READ texturesLoading NOTIFY textureProgressChanged)
    Q_PROPERTY(double textureProgress READ textureProgress NOTIFY textureProgressChanged)
    Q_PROPERTY(QString textureStage READ textureStage NOTIFY textureProgressChanged)

public:
    explicit MeshToolManager(QObject* parent = nullptr);
    ~MeshToolManager() override;

    MeshGeometry* geometry() const { return m_geometry; }
    bool loaded() const { return m_loaded; }
    QString sourceName() const { return m_sourceName; }
    QString formatName() const { return m_mesh.formatName; }
    int vertexCount() const { return int(m_mesh.vertexCount); }
    int triangleCount() const { return int(m_mesh.triangleCount()); }
    int submeshCount() const { return int(m_mesh.submeshes.size()); }
    QVector3D center() const { return (m_mesh.boundsMin + m_mesh.boundsMax) / 2; }
    QVector3D size() const { return m_mesh.boundsMax - m_mesh.boundsMin; }
    float radius() const;
    QString infoLine() const;

    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    QString appVersion() const;
    bool busy() const { return m_busy; }
    QVariantList parts() const { return m_parts; }
    QString textureInfo() const { return m_textureInfo; }
    bool texturesLoading() const { return m_texturesLoading; }
    double textureProgress() const { return m_textureProgress; }
    QString textureStage() const { return m_textureStage; }

    /// Loads the mesh registered under that path hash. displayName (the workspace path) names extracted files.
    Q_INVOKABLE void loadMeshFromBlobset(const QString& hexHash, const QString& displayName = QString());

    /// format: "mesh" (the original main section) or "obj" (Wavefront OBJ of what is shown, with an .mtl and the
    /// textures its surfaces use as PNGs in <name>_textures - see MeshTextureExport). The OBJ export runs in the
    /// background (busy); the status says what was written.
    Q_INVOKABLE void extract(const QString& outDirPath, const QString& format);

signals:
    void meshChanged();
    void statusChanged();
    void busyChanged();
    void partsChanged();
    void textureProgressChanged();

private:
    void setStatus(bool ok, const QString& message);

    MeshGeometry* m_geometry = nullptr;
    MeshFile m_mesh;
    QByteArray m_raw;          // Main section as stored (for "extract original").
    bool m_loaded = false;
    QString m_sourceName;
    QString m_statusText;
    bool m_statusOk = true;
    bool m_busy = false;
    QFuture<void> m_export;

    // Textures: found in the background after a mesh loads (m_loadId tells a stale result apart).
    void loadTextures();
    void clearParts();
    // groups: material index (-1 = none) -> submeshes ({-1, {}}: the whole mesh, untextured); files: each
    // material's texture PNG.
    void setParts(const QMap<int, QList<int>>& groups, const QStringList& files, const QList<bool>& transparent,
                  const MeshFile& mesh);
    void setTextureProgress(bool loading, double progress, const QString& stage);
    std::shared_ptr<std::atomic<int>> m_currentLoad = std::make_shared<std::atomic<int>>(0);   // Read by the job.
    bool m_texturesLoading = false;
    double m_textureProgress = 0;
    QString m_textureStage;
    int m_loadId = 0;
    QFuture<void> m_textureJob;
    QVariantList m_parts;
    QList<MeshGeometry*> m_partGeometries;
    QString m_textureInfo;
    QTemporaryDir* m_textureDir = nullptr;   // The decoded textures as PNGs (Texture.source takes files).
};

#endif // MESH_TOOL_MANAGER_H
