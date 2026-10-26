#ifndef MESH_TOOL_MANAGER_H
#define MESH_TOOL_MANAGER_H

#include "mesh_file.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVector3D>
#include <QtQuick3D/QQuick3DGeometry>

/// The decoded mesh as Qt Quick 3D geometry: one triangle list, float position / normal / uv per vertex.
class MeshGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit MeshGeometry(QQuick3DObject* parent = nullptr);

    void setMesh(const MeshFile& mesh);
    void clearMesh();
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

public:
    explicit MeshToolManager(QObject* parent = nullptr);

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

    /// Loads the mesh registered under that path hash. displayName (the workspace path) names extracted files.
    Q_INVOKABLE void loadMeshFromBlobset(const QString& hexHash, const QString& displayName = QString());

    /// format: "mesh" (the original main section) or "obj" (Wavefront OBJ of what is shown).
    Q_INVOKABLE void extract(const QString& outDirPath, const QString& format);

signals:
    void meshChanged();
    void statusChanged();

private:
    void setStatus(bool ok, const QString& message);

    MeshGeometry* m_geometry = nullptr;
    MeshFile m_mesh;
    QByteArray m_raw;          // Main section as stored (for "extract original").
    bool m_loaded = false;
    QString m_sourceName;
    QString m_statusText;
    bool m_statusOk = true;
};

#endif // MESH_TOOL_MANAGER_H
