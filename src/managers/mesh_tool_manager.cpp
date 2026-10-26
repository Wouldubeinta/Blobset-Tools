#include "mesh_tool_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <stdexcept>

using namespace Qt::StringLiterals;

// ==============================================================================================================
// MeshGeometry
// ==============================================================================================================

MeshGeometry::MeshGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {}

void MeshGeometry::setMesh(const MeshFile& mesh) {
    clear();
    setVertexData(mesh.displayVertices);
    setIndexData(mesh.displayIndices);
    setStride(MeshFile::kDisplayStride);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 24, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    setBounds(mesh.boundsMin, mesh.boundsMax);
    update();
}

void MeshGeometry::clearMesh() {
    clear();
    update();
}

// ==============================================================================================================
// MeshToolManager
// ==============================================================================================================

MeshToolManager::MeshToolManager(QObject* parent) : QObject(parent), m_geometry(new MeshGeometry()) {
    m_geometry->setParent(this);
}

QString MeshToolManager::appVersion() const {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QString() : u"v"_s + version;
}

float MeshToolManager::radius() const {
    return m_loaded ? std::max(0.001f, size().length() / 2) : 1.0f;
}

QString MeshToolManager::infoLine() const {
    if (!m_loaded)
        return {};
    const QVector3D s = size();
    return u"%1  |  %2  |  %3 vertices  |  %4 triangles  |  %5 submesh(es)  |  %6 x %7 x %8"_s
        .arg(m_sourceName, m_mesh.formatName)
        .arg(m_mesh.vertexCount)
        .arg(m_mesh.triangleCount())
        .arg(m_mesh.submeshes.size())
        .arg(s.x(), 0, 'f', 2).arg(s.y(), 0, 'f', 2).arg(s.z(), 0, 'f', 2);
}

void MeshToolManager::setStatus(bool ok, const QString& message) {
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}

void MeshToolManager::loadMeshFromBlobset(const QString& hexHash, const QString& displayName) {
    m_loaded = false;
    m_mesh = MeshFile();
    m_raw.clear();
    m_sourceName = displayName.isEmpty() ? u"0x"_s + hexHash.toUpper() : QString(displayName).replace(u'\\', u'/');
    m_geometry->clearMesh();

    const auto finish = [this](bool ok, const QString& message) {
        emit meshChanged();
        setStatus(ok, message);
    };

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry))
        return finish(false, u"This mesh (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator)
        return finish(false, u"The application orchestrator is unavailable."_s);

    qint64 mainSize = -1;
    try {
        // V4 meshes live in the main section; the older layout (RLL3 / RLL4) keeps its buffers in vram.
        m_raw = BlobsetAssetReader::read(entry, orchestrator->gameLocation(), orchestrator->blobsetVersion(),
                                         orchestrator->platform(), &mainSize, true);
    } catch (const std::exception& ex) {
        return finish(false, QString::fromUtf8(ex.what()));
    }

    QString error;
    if (!m_mesh.parse(m_raw, &error, mainSize)) {
        m_mesh = MeshFile();
        return finish(false, error);
    }

    m_loaded = true;
    m_geometry->setMesh(m_mesh);
    finish(true, infoLine());
}

void MeshToolManager::extract(const QString& outDirPath, const QString& format) {
    if (!m_loaded) {
        setStatus(false, u"No mesh is loaded."_s);
        return;
    }

    const QString outDir = outDirPath.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(outDirPath).toLocalFile()
                                                                                  : outDirPath;
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        setStatus(false, u"Could not create the output folder: "_s + outDir);
        return;
    }

    const bool asObj = format.compare(u"obj"_s, Qt::CaseInsensitive) == 0;
    QString baseName = QFileInfo(m_sourceName).completeBaseName();
    if (baseName.isEmpty())
        baseName = u"mesh"_s;
    const QString path = QDir(outDir).filePath(baseName + (asObj ? u".obj"_s : u".mesh"_s));
    const QByteArray data = asObj ? m_mesh.toObj(m_sourceName) : m_raw;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return;
    }
    setStatus(true, u"Extracted: "_s + QDir::toNativeSeparators(path));
}
