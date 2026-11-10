#include "mesh_tool_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "mesh_texture_export.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <memory>

using namespace Qt::StringLiterals;

// ==============================================================================================================
// MeshGeometry
// ==============================================================================================================

MeshGeometry::MeshGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {}

QByteArray MeshGeometry::quick3DVertices(const MeshFile& mesh) {
    QByteArray vertices = mesh.displayVertices;
    auto* v = reinterpret_cast<float*>(vertices.data());
    const qsizetype count = vertices.size() / MeshFile::kDisplayStride;
    for (qsizetype k = 0; k < count; ++k)
        v[k * 8 + 7] = 1.0f - v[k * 8 + 7];
    return vertices;
}

void MeshGeometry::setPart(const MeshFile& mesh, const QByteArray& vertices, const QList<int>& submeshes) {
    QByteArray indices;
    for (const int s : submeshes) {
        const MeshFile::Submesh& sub = mesh.submeshes.at(s);
        indices += mesh.displayIndices.mid(qsizetype(sub.indexStart) * 4, qsizetype(sub.indexCount) * 4);
    }
    clear();
    setVertexData(vertices);
    setIndexData(indices);
    setStride(MeshFile::kDisplayStride);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 24, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    setBounds(mesh.boundsMin, mesh.boundsMax);
    update();
}

void MeshGeometry::setMesh(const MeshFile& mesh) {
    clear();
    setVertexData(quick3DVertices(mesh));
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

MeshToolManager::~MeshToolManager() {
    m_export.waitForFinished();   // The export reads its own copies, but writes files the user expects whole.
    m_textureJob.waitForFinished();
    delete m_textureDir;   // Removes the decoded textures (kept for the session: meshes share them).
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
    ++m_loadId;   // Textures still loading for the previous mesh stop and are dropped.
    m_currentLoad->store(m_loadId);
    clearParts();

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
        m_raw = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
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
    setParts({{-1, {}}}, {}, {}, m_mesh);   // Untextured until the textures are in.
    finish(true, infoLine());
    loadTextures();
}

void MeshToolManager::setParts(const QMap<int, QList<int>>& groups, const QStringList& files,
                               const QList<bool>& transparent, const MeshFile& mesh) {
    // The models showing the old parts go when QML sees the new list; their geometries after that.
    const QList<MeshGeometry*> old = m_partGeometries;
    m_partGeometries.clear();
    QVariantList parts;
    const QByteArray vertices = MeshGeometry::quick3DVertices(mesh);
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        QList<int> submeshes = it.value();
        if (submeshes.isEmpty())   // The whole mesh.
            for (qsizetype s = 0; s < mesh.submeshes.size(); ++s)
                submeshes.append(int(s));
        auto* geometry = new MeshGeometry();
        geometry->setParent(this);
        geometry->setPart(mesh, vertices, submeshes);
        m_partGeometries.append(geometry);
        const QString file = it.key() >= 0 ? files.value(it.key()) : QString();
        parts.append(QVariantMap{{u"geometry"_s, QVariant::fromValue(geometry)},
                                 {u"texture"_s, file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString()},
                                 {u"transparent"_s, it.key() >= 0 && transparent.value(it.key())}});
    }
    m_parts = parts;
    emit partsChanged();
    for (MeshGeometry* geometry : old)
        geometry->deleteLater();
}

void MeshToolManager::clearParts() {
    m_parts.clear();
    m_textureInfo.clear();
    emit partsChanged();
    for (MeshGeometry* geometry : std::as_const(m_partGeometries))
        geometry->deleteLater();   // QML may still hold them until it sees the new parts.
    m_partGeometries.clear();
    setTextureProgress(false, 0, QString());
}

void MeshToolManager::setTextureProgress(bool loading, double progress, const QString& stage) {
    if (m_texturesLoading == loading && qFuzzyCompare(m_textureProgress + 1, progress + 1) && m_textureStage == stage)
        return;
    m_texturesLoading = loading;
    m_textureProgress = progress;
    m_textureStage = stage;
    emit textureProgressChanged();
}

void MeshToolManager::loadTextures() {
    const MeshTextureExport::Context context = MeshTextureExport::currentContext();
    if (!context.valid)
        return;
    if (!m_textureDir)
        m_textureDir = new QTemporaryDir();
    if (!m_textureDir->isValid())
        return;
    m_textureInfo = u"Loading textures..."_s;
    emit partsChanged();
    setTextureProgress(true, 0, u"Loading textures..."_s);

    // Everything runs on the thread pool: finding, reading and decoding the textures (on every core) and writing
    // the PNGs Qt Quick 3D loads. The UI thread only gets the progress and, at the end, the finished parts.
    const int loadId = m_loadId;
    const std::shared_ptr<std::atomic<int>> current = m_currentLoad;
    current->store(loadId);
    const MeshFile mesh = m_mesh;
    const QString source = m_sourceName;
    const QString dir = m_textureDir->path();
    const QPointer<MeshToolManager> self(this);
    m_textureJob = QtConcurrent::run([self, loadId, current, mesh, source, context, dir]() {
        const auto lastPermille = std::make_shared<std::atomic<int>>(-1);
        const MeshTextureExport::Progress progress = [self, loadId, lastPermille](double fraction, const QString& stage) {
            const int permille = int(std::clamp(fraction, 0.0, 1.0) * 1000);
            if (permille / 10 == lastPermille->load() / 10 && permille != 1000)
                return;   // Only whole percents reach the UI.
            lastPermille->store(permille);
            QMetaObject::invokeMethod(qApp, [self, loadId, permille, stage]() {
                if (self && self->m_loadId == loadId)
                    self->setTextureProgress(true, permille / 1000.0, stage);
            }, Qt::QueuedConnection);
        };
        const MeshTextureExport::Cancelled cancelled = [current, loadId]() { return current->load() != loadId; };
        // 1024 px is plenty on screen and keeps decoding, writing and uploading quick (Extract keeps full size).
        const MeshTextureExport::Look look = MeshTextureExport::resolveLook(
            mesh, source, context, dir, 1024, progress, cancelled);
        QMetaObject::invokeMethod(qApp, [self, loadId, mesh, look]() {
            if (!self || self->m_loadId != loadId)
                return;   // Another mesh was loaded meanwhile (its PNGs stay for later).

            // One part per material (and one for the submeshes without), when there is something to texture.
            if (look.found > 0) {
                QMap<int, QList<int>> groups;
                for (qsizetype s = 0; s < mesh.submeshes.size(); ++s)
                    groups[look.submeshMaterial.value(s, -1)].append(int(s));
                self->setParts(groups, look.files, look.transparent, mesh);
            }
            self->m_textureInfo = look.found == 0 && look.missing.isEmpty()
                                      ? u"No textures (no material found)"_s
                                      : u"%1 texture(s)"_s.arg(look.found)
                                            + (look.missing.isEmpty() ? QString()
                                                                      : u", %1 not found"_s.arg(look.missing.size()));
            emit self->partsChanged();
            self->setTextureProgress(false, 1, QString());
            if (!look.missing.isEmpty())
                self->setStatus(true, u"Textures not found: "_s + look.missing.mid(0, 6).join(u", "_s)
                                          + (look.missing.size() > 6 ? u", ..."_s : QString()));
        }, Qt::QueuedConnection);
    });
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
    if (asObj && baseName.endsWith(u".model"_s, Qt::CaseInsensitive))
        baseName.chop(6);   // "ball.model.mesh" -> ball.obj, ball.mtl, ball_textures.
    if (baseName.isEmpty())
        baseName = u"mesh"_s;

    if (asObj) {
        // OBJ + MTL + textures: finding the textures can read many texture packs, so it runs in the background.
        if (m_busy)
            return;
        const MeshTextureExport::Context context = MeshTextureExport::currentContext();
        const MeshFile mesh = m_mesh;
        const QString source = m_sourceName;
        m_busy = true;
        emit busyChanged();
        setStatus(true, u"Exporting %1.obj with its textures..."_s.arg(baseName));
        const QPointer<MeshToolManager> self(this);
        m_export = QtConcurrent::run([self, mesh, source, outDir, baseName, context]() {
            const auto lastPercent = std::make_shared<std::atomic<int>>(-1);
            const MeshTextureExport::Progress progress = [self, lastPercent](double fraction, const QString& stage) {
                const int percent = int(std::clamp(fraction, 0.0, 1.0) * 100);
                if (lastPercent->exchange(percent) == percent)
                    return;
                QMetaObject::invokeMethod(qApp, [self, percent, stage]() {
                    if (self && self->m_busy)
                        self->setStatus(true, u"Exporting the OBJ: %1 (%2%)"_s.arg(stage).arg(percent));
                }, Qt::QueuedConnection);
            };
            const MeshTextureExport::Result result =
                MeshTextureExport::exportObj(mesh, source, outDir, baseName, context, progress);
            QMetaObject::invokeMethod(qApp, [self, result]() {
                if (!self)
                    return;
                self->m_busy = false;
                emit self->busyChanged();
                self->setStatus(result.ok, result.message);
                if (AppOrchestrator* orchestrator = AppOrchestrator::instance())
                    emit orchestrator->uiConsoleUpdate(u"Mesh Viewer: "_s + result.message);
            }, Qt::QueuedConnection);
        });
        return;
    }

    const QString path = QDir(outDir).filePath(baseName + u".mesh"_s);
    const QByteArray& data = m_raw;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return;
    }
    setStatus(true, u"Extracted: "_s + QDir::toNativeSeparators(path));
}
