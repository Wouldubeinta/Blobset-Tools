#include "rl26_appearance_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "hash_utility.h"
#include "mesh_texture_export.h"
#include "rl26_skin_compositor.h"
#include "rll4_character.h"
#include "rll4_lists.h"

#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QMutex>
#include <QRegularExpression>
#include <QUrl>
#include <QMetaObject>
#include <QSet>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

using namespace Qt::StringLiterals;

// ==============================================================================================================
// AppearanceGeometry
// ==============================================================================================================

AppearanceGeometry::AppearanceGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {}

void AppearanceGeometry::setData(const QByteArray& vertices, const QByteArray& indices, const QVector3D& min,
                                 const QVector3D& max, const QByteArray& colours) {
    clear();
    m_colours = colours;
    const bool coloured = !m_colours.isEmpty();
    setVertexData(interleaved(vertices));
    setIndexData(indices);
    setStride(MeshFile::kDisplayStride + (coloured ? 16 : 0));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 24, QQuick3DGeometry::Attribute::F32Type);
    if (coloured)
        addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, MeshFile::kDisplayStride, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
    setBounds(min, max);
    update();
}

void AppearanceGeometry::setVertices(const QByteArray& vertices) {
    setVertexData(interleaved(vertices));
    update();
}

// The display vertices (8 floats each) with the vertex colours (4 floats each) after each, when there are colours.
QByteArray AppearanceGeometry::interleaved(const QByteArray& vertices) const {
    if (m_colours.isEmpty())
        return vertices;
    const qsizetype count = vertices.size() / MeshFile::kDisplayStride;
    if (m_colours.size() < count * 16)
        return vertices;
    QByteArray out(count * (MeshFile::kDisplayStride + 16), Qt::Uninitialized);
    for (qsizetype i = 0; i < count; ++i) {
        std::memcpy(out.data() + i * 48, vertices.constData() + i * 32, 32);
        std::memcpy(out.data() + i * 48 + 32, m_colours.constData() + i * 16, 16);
    }
    return out;
}

// ==============================================================================================================
// Rl26AppearanceManager
// ==============================================================================================================

namespace {

// Rugby League Live 4's heads: the eyeballs are moved by the head's bones, not by its blend shapes,
// so the face shapes (ethnicity, eye depth, ...) move the eyelids over them. Each eye follows the head around its
// socket instead: moved by the weighted mean of how far the head's own vertices near it moved.
void followEyes(const MeshFile& mesh, QByteArray& vertices) {
    // A real player's (tier 1) head: its eyes are placed for its "bindMesh" shape (the head the skeleton was bound
    // to), not for the scan, so they also follow the head from that shape to the scan.
    QByteArray bind;
    if (mesh.morphs.contains(u"bindMesh"_s))
        bind = mesh.morphedVertices({{u"bindMesh"_s, 1.0f}});
    const auto* bound = reinterpret_cast<const float*>(bind.constData());
    const bool hasBind = bind.size() >= mesh.displayVertices.size();
    static const QSet<QString> eyeSurfaces = {u"eyes_mat"_s, u"eyes_clear_mat"_s};   // The lashes have their shapes.
    const auto* rest = reinterpret_cast<const float*>(mesh.displayVertices.constData());
    auto* cur = reinterpret_cast<float*>(vertices.data());
    const qsizetype count = std::min(mesh.displayVertices.size(), vertices.size()) / MeshFile::kDisplayStride;
    const auto* indices = reinterpret_cast<const quint32*>(mesh.displayIndices.constData());
    const qsizetype indexTotal = mesh.displayIndices.size() / 4;

    // Which vertices are the eyes' (by side) and which the head's.
    std::vector<qint8> role(size_t(count), -1);   // -1 unused, 0 head, 1 eye
    bool anyEye = false;
    for (const MeshFile::Submesh& s : mesh.submeshes) {
        const bool eye = eyeSurfaces.contains(mesh.surfaces.value(s.surface));
        anyEye = anyEye || eye;
        for (quint32 i = 0; i < s.indexCount && qsizetype(s.indexStart) + i < indexTotal; ++i) {
            const quint32 k = indices[s.indexStart + i];
            if (k < quint32(count) && role[k] != 1)
                role[k] = eye ? 1 : 0;
        }
    }
    if (!anyEye)
        return;
    for (int side = 0; side < 2; ++side) {
        // The eye's centre (rest pose).
        double c[3] = {0, 0, 0};
        int n = 0;
        for (qsizetype k = 0; k < count; ++k)
            if (role[k] == 1 && (rest[k * 8] > 0) == (side == 1)) {
                for (int j = 0; j < 3; ++j)
                    c[j] += rest[k * 8 + j];
                ++n;
            }
        if (n == 0)
            continue;
        for (double& v : c)
            v /= n;
        // How far the head moved around the socket.
        constexpr double kRadius = 0.03;   // Metres: the lids, brow and cheek around the eye.
        double moved[3] = {0, 0, 0}, total = 0;
        for (qsizetype k = 0; k < count; ++k) {
            if (role[k] != 0)
                continue;
            const float* from = hasBind ? bound : rest;   // The head the eyes were placed for.
            const double dx = from[k * 8] - c[0], dy = from[k * 8 + 1] - c[1], dz = from[k * 8 + 2] - c[2];
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d >= kRadius)
                continue;
            const double w = 1.0 - d / kRadius;
            for (int j = 0; j < 3; ++j)
                moved[j] += w * (cur[k * 8 + j] - (hasBind ? bound[k * 8 + j] : rest[k * 8 + j]));
            total += w;
        }
        if (total <= 0)
            continue;
        for (qsizetype k = 0; k < count; ++k)
            if (role[k] == 1 && (rest[k * 8] > 0) == (side == 1))
                for (int j = 0; j < 3; ++j)
                    cur[k * 8 + j] += float(moved[j] / total);
    }
}


// Rugby League Live 4's kits: the collar / yoke is a separate piece laid over the shirt around the neck (a mesh
// island of its own, entirely in the model's top tenth). Its triangles, to leave out when it's hidden.
QByteArray withoutCollar(const MeshFile& mesh, const QByteArray& indices) {
    const auto* rest = reinterpret_cast<const float*>(mesh.displayVertices.constData());
    const qsizetype count = mesh.displayVertices.size() / MeshFile::kDisplayStride;
    const auto* idx = reinterpret_cast<const quint32*>(indices.constData());
    const qsizetype tris = indices.size() / 12;
    if (count == 0 || tris == 0)
        return indices;
    // Islands: vertices joined by the triangles (index connectivity).
    std::vector<int> parent(static_cast<size_t>(count), 0);
    for (qsizetype i = 0; i < count; ++i)
        parent[size_t(i)] = int(i);
    const auto find = [&parent](int a) {
        while (parent[size_t(a)] != a) {
            parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
            a = parent[size_t(a)];
        }
        return a;
    };
    for (qsizetype t = 0; t < tris; ++t)
        for (int e = 0; e < 2; ++e) {
            const quint32 a = idx[t * 3 + e], b = idx[t * 3 + e + 1];
            if (a < quint32(count) && b < quint32(count))
                parent[size_t(find(int(a)))] = find(int(b));
        }
    float lo = std::numeric_limits<float>::max(), hi = -lo;
    for (qsizetype k = 0; k < count; ++k) {
        lo = std::min(lo, rest[k * 8 + 1]);
        hi = std::max(hi, rest[k * 8 + 1]);
    }
    const float limit = hi - (hi - lo) * 0.12f;
    QHash<int, float> islandMin;   // Root -> its lowest point.
    QHash<int, int> islandTris;
    for (qsizetype t = 0; t < tris; ++t) {
        const quint32 a = idx[t * 3];
        if (a >= quint32(count))
            continue;
        const int root = find(int(a));
        islandTris[root] += 1;
        float m = islandMin.value(root, std::numeric_limits<float>::max());
        for (int e = 0; e < 3; ++e)
            if (idx[t * 3 + e] < quint32(count))
                m = std::min(m, rest[idx[t * 3 + e] * 8 + 1]);
        islandMin[root] = m;
    }
    QSet<int> collar;
    for (auto it = islandMin.cbegin(); it != islandMin.cend(); ++it)
        if (it.value() > limit && islandTris.value(it.key()) >= 50)
            collar.insert(it.key());
    if (collar.isEmpty())
        return indices;
    QByteArray out;
    out.reserve(indices.size());
    for (qsizetype t = 0; t < tris; ++t)
        if (idx[t * 3] >= quint32(count) || !collar.contains(find(int(idx[t * 3]))))
            out.append(indices.constData() + t * 12, 12);
    return out;
}

struct Loaded {
    QHash<QString, std::shared_ptr<const MeshFile>> meshes;
    QHash<QString, QString> errors;
    QHash<QString, QStringList> packs;   // RLL3 / RLL4: the textures each model's texture pack lists.
};

// The models of `paths` (".modelcomponent" + ".blendshapes" each) from the loaded blobset. Runs on the pool.
// RL26 parts are ".modelcomponent" + ".blendshapes"; RLL3 / RLL4 parts add a ".vbo" (indices and UVs).
// A version 16 model's materials (88 bytes each, at 0x1C, count at 0x20): their texture parameters (+80: a list of
// {char[32] name, char[64] texture}, +84 its count), as "@<material>:<param>=<texture>", lower case.
QStringList materialTextures(const QByteArray& model) {
    QStringList out;
    const auto i32 = [&model](qsizetype at) {
        return at >= 0 && at + 4 <= model.size() ? qFromLittleEndian<qint32>(model.constData() + at) : 0;
    };
    const auto text = [&model](qsizetype at, int max) {
        if (at < 0 || at + max > model.size())
            return QString();
        const char* s = model.constData() + at;
        return QString::fromLatin1(s, qsizetype(qstrnlen(s, uint(max)))).toLower();
    };
    if (model.size() < 0x24)
        return out;
    const qsizetype table = 0x1C + i32(0x1C);
    const int count = std::clamp(i32(0x20), 0, 64);
    for (int i = 0; i < count; ++i) {
        const qsizetype rec = table + 88 * qsizetype(i);
        if (rec + 88 > model.size())
            break;
        const QString material = text(rec, 32);
        const qint32 rel = i32(rec + 80);
        const int params = i32(rec + 84);
        if (material.isEmpty() || rel == 0 || params <= 0 || params > 32)
            continue;
        for (int j = 0; j < params; ++j) {
            const qsizetype e = rec + 80 + rel + 0x60 * qsizetype(j);
            const QString param = text(e, 32), texture = text(e + 32, 64);
            if (!param.isEmpty() && texture.contains(u'/'))
                out.append(u"@%1:%2=%3"_s.arg(material, param, texture));
        }
    }
    return out;
}

Loaded readModels(const QStringList& paths, const QString& location, int version, int platform, const QString& gameKey) {
    Loaded out;
    const auto read = [&](const QString& path, const QString& type, QByteArray* data, qint64* mainSize) -> QString {
        BlobsetEntry entry;
        const quint64 hash = BlobsetHash::hashForAsset(version, {path.toLower(), type}, platform, gameKey);
        if (!BlobsetEntryRegistry::findEntry(BlobsetEntryRegistry::makeKey(hash), entry))
            return u"%1.%2 isn't in the loaded blobset"_s.arg(path, type);
        try {
            *mainSize = -1;
            *data = BlobsetAssetReader::read(entry, location, version, platform, mainSize, true);
        } catch (const std::exception& ex) {
            return QString::fromUtf8(ex.what());
        }
        return {};
    };
    for (const QString& path : paths) {
        QByteArray model, shapes, vbo;
        qint64 mainSize = -1, vboMain = -1;
        QString error = read(path, u"modelcomponent"_s, &model, &mainSize);
        if (error.isEmpty())
            error = read(path, u"blendshapes"_s, &shapes, &mainSize);
        if (error.isEmpty() && MeshFile::isLegacySkinnedModel(model)) {
            error = read(path, u"vbo"_s, &vbo, &vboMain);
            // Its texture pack's header names its textures ("characters/uniforms/textures/nrl_dragons_home_col").
            QByteArray pack;
            qint64 packMain = -1;
            if (read(path, u"texpack"_s, &pack, &packMain).isEmpty()) {
                static const QRegularExpression name(u"characters/[A-Za-z0-9_/]+"_s);
                const QString header = QString::fromLatin1(pack.left(packMain > 0 ? packMain : pack.size()));
                QStringList names;
                for (auto it = name.globalMatch(header); it.hasNext();)
                    names.append(it.next().captured(0).toLower());
                out.packs.insert(path, names);
            }
            // Its materials' texture parameters, as "@<material>:<param>=<texture>".
            out.packs[path] += materialTextures(model);
        }
        if (error.isEmpty()) {
            auto mesh = std::make_shared<MeshFile>();
            if (mesh->parseSkinned(model, shapes, vbo, vboMain, &error))
                out.meshes.insert(path, mesh);
        }
        if (!error.isEmpty())
            out.errors.insert(path, error);
    }
    return out;
}

QString modelName(const QString& path) {
    return path.section(u'/', -1);
}

} // namespace

Rl26AppearanceManager::Rl26AppearanceManager(QObject* parent) : QObject(parent) {}

Rl26AppearanceManager::~Rl26AppearanceManager() {
    ++m_loadId;   // A load still running finds nobody to give its models to (it holds no pointer to this).
    delete m_textureDir;
}

void Rl26AppearanceManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void Rl26AppearanceManager::open(QObject* editor, int player) {
    m_editor = editor;
    m_player = player;
    // Rugby League Live 4's save / career editors: that game's rules (RL26's database / save / career otherwise).
    // Rugby League Live 4: its save / career editors, or the database editor with an RLL4 database loaded.
    m_rll4Database = editor && editor->property("game").toString() == u"rll4"_s;
    m_rll4 = editor && (QString::fromLatin1(editor->metaObject()->className()).startsWith(u"Rll4"_s) || m_rll4Database);
    m_models.clear();
    m_firstBuild = true;
    m_texturesDirty = true;
    m_kit = -2;
    m_teamKits.clear();
    if (m_rll4 && player >= 0 && editor->metaObject()->indexOfMethod("playerKits(int)") >= 0)
        QMetaObject::invokeMethod(editor, "playerKits", Qt::DirectConnection, Q_RETURN_ARG(QVariantList, m_teamKits),
                                  Q_ARG(int, player));
    if (!editor || player < 0) {
        m_fields.clear();
        m_groups.clear();
        emit fieldsChanged();
        setStatus(false, u"No player is selected."_s);
        return;
    }
    readFields();
    if (m_rawFields.isEmpty()) {
        setStatus(false, u"This player has no appearance data."_s);
        return;
    }
    rebuild(true);
}

void Rl26AppearanceManager::close() {
    ++m_loadId;
    m_editor = nullptr;
    m_player = -1;
    m_models.clear();
    ++m_textureId;
    const QList<AppearanceGeometry*> old = m_geometries.values();
    m_geometries.clear();
    m_geometryPath.clear();
    m_partList.clear();
    m_textureUrl.clear();
    m_textureAlpha.clear();
    m_textureNormal.clear();
    m_textureRough.clear();
    emit texturesChanged();
    m_parts.clear();
    if (m_texturing) {
        m_texturing = false;
        emit texturingChanged();
    }
    emit partsChanged();
    for (AppearanceGeometry* g : old)
        g->deleteLater();
    if (m_loading) {
        m_loading = false;
        emit loadingChanged();
    }
}

void Rl26AppearanceManager::refresh() {
    if (!m_editor || m_player < 0)
        return;
    readFields();
    rebuild(true);
}

void Rl26AppearanceManager::readFields() {
    QVariantList raw;
    if (m_editor)
        QMetaObject::invokeMethod(m_editor, "appearanceFields", Qt::DirectConnection, Q_RETURN_ARG(QVariantList, raw),
                                  Q_ARG(int, m_player));
    m_rawFields = raw;

    // The option values (what the blob stores) and the colours, by the fields' member names.
    m_options.clear();
    m_colours.clear();
    if (m_rll4) {
        // RLL4's save fields are keyed by the save's names: give each the script option it holds. The database's
        // appearance blob names its members after the options themselves.
        for (QVariant& v : m_rawFields) {
            QVariantMap f = v.toMap();
            if (!m_rll4Database || f.value(u"name"_s).toString().isEmpty())
                f.insert(u"name"_s, Rll4Character::saveOption(f.value(u"key"_s).toString()));
            // The database's two byte item ids (head, hairTop, ...) read byte swapped (Nathan Cleary's head is 3,
            // read as 768).
            const QString key = f.value(u"key"_s).toString(), name = f.value(u"name"_s).toString();
            if (m_rll4Database && key.startsWith(u"n:"_s) && key.endsWith(u":2"_s) && !name.isEmpty()) {
                const int value = f.value(u"value"_s).toInt();
                const int swappedValue = ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
                const auto known = [&name](int guid) {
                    for (int sex = 0; sex < 2; ++sex)
                        for (const Rll4Character::Choice& c : Rll4Character::choices(name, sex))
                            if (c.guid == guid)
                                return true;
                    return false;
                };
                // Big endian in the database (Bryce Cartwright's head is 0x0001, read 256: Corey Oates'), so swapped
                // whenever the field is an item list, unless only the stored order names an item.
                if (swappedValue != value && (known(swappedValue) || !known(value))
                    && (Rll4Character::choices(name, 0).size() + Rll4Character::choices(name, 1).size()) > 0) {
                    f.insert(u"value"_s, swappedValue);
                    f.insert(u"swap16"_s, true);
                }
            }
            v = f;
        }
    }
    for (const QVariant& v : std::as_const(m_rawFields)) {
        const QVariantMap f = v.toMap();
        const QString name = f.value(u"name"_s).toString();
        if (name.isEmpty())
            continue;
        const QString kind = f.value(u"kind"_s).toString();
        if (kind == u"number"_s || kind == u"choice"_s)
            m_options.insert(name, f.value(u"value"_s).toDouble());
        else if (kind == u"bool"_s)
            m_options.insert(name, f.value(u"value"_s).toBool() ? 1 : 0);
        else if (kind == u"colour"_s) {
            // "#rrggbb" of the _r / _g / _b bytes: the game's options are the three (the boots' colours, ...).
            const QColor colour(f.value(u"value"_s).toString());
            m_colours.insert(name, colour);
            if (colour.isValid()) {
                m_options.insert(name + u"_r"_s, colour.red());
                m_options.insert(name + u"_g"_s, colour.green());
                m_options.insert(name + u"_b"_s, colour.blue());
            }
        }
    }

    if (m_rll4)
        applyKit();

    // What the view shows: the editor's items, with the game's item lists as choices and sliders marked.
    const int sex = int(m_options.value(u"sex"_s, 0));
    m_fields.clear();
    m_groups.clear();
    for (const QVariant& v : std::as_const(m_rawFields)) {
        QVariantMap f = v.toMap();
        const QString name = f.value(u"name"_s).toString();
        const QString kind = f.value(u"kind"_s).toString();
        QString control = kind;
        if (kind == u"choice"_s) {
            // The editor's own list ({name, value}): as the view's {value, text}.
            QVariantList choices;
            for (const QVariant& c : f.value(u"choices"_s).toList()) {
                const QVariantMap m = c.toMap();
                choices.append(QVariantMap{{u"value"_s, m.value(u"value"_s)},
                                           {u"text"_s, m.contains(u"text"_s) ? m.value(u"text"_s) : m.value(u"name"_s)}});
            }
            f.insert(u"choices"_s, choices);
        } else if (kind == u"number"_s && !name.isEmpty() && m_rll4) {
            const QList<Rll4Character::Choice> list = Rll4Character::choices(name, sex);
            if (!list.isEmpty()) {
                QVariantList choices;
                const int current = f.value(u"value"_s).toInt();
                bool found = false;
                for (const Rll4Character::Choice& c : list) {
                    choices.append(QVariantMap{{u"value"_s, c.guid}, {u"text"_s, c.name}});
                    found = found || c.guid == current;
                }
                if (!found)
                    choices.prepend(QVariantMap{{u"value"_s, current}, {u"text"_s, u"#%1 (not in the game's list)"_s.arg(current)}});
                f.insert(u"choices"_s, choices);
                control = u"choice"_s;
            } else if (Rll4Character::isSlider(name)) {
                control = u"slider"_s;
                f.insert(u"max"_s, Rll4Character::sliderMax(name));
            }
        } else if (kind == u"number"_s && !name.isEmpty()) {
            const QList<Rl26Character::Choice> list = Rl26Character::choices(name, sex);
            if (!list.isEmpty()) {
                QVariantList choices;
                const int current = f.value(u"value"_s).toInt();
                bool found = false;
                for (const Rl26Character::Choice& c : list) {
                    choices.append(QVariantMap{{u"value"_s, c.guid}, {u"text"_s, c.name}});
                    found = found || c.guid == current;
                }
                if (!found)
                    choices.prepend(QVariantMap{{u"value"_s, current}, {u"text"_s, u"#%1 (not in the game's list)"_s.arg(current)}});
                f.insert(u"choices"_s, choices);
                control = u"choice"_s;
            } else if (Rl26Character::isSlider(name)) {
                control = u"slider"_s;
                f.insert(u"max"_s, Rl26Character::sliderMax(name));
            }
        }
        f.insert(u"control"_s, control);
        f.insert(u"model"_s, affectsModels(name));
        const QString group = f.value(u"group"_s).toString();
        if (!m_groups.contains(group))
            m_groups.append(group);
        m_fields.append(f);
    }
    if (m_rll4 && !m_fields.isEmpty()) {
        // The kit pick: first in the kit group (the boots' fields), else its own group at the end.
        const QVariantMap kit = kitField();
        const QString group = kit.value(u"group"_s).toString();
        qsizetype at = m_fields.size();
        for (qsizetype i = 0; i < m_fields.size(); ++i)
            if (m_fields.at(i).toMap().value(u"group"_s).toString() == group) {
                at = i;
                break;
            }
        m_fields.insert(at, kit);
        m_fields.insert(at + 1, QVariantMap{{u"key"_s, u"@collar"_s}, {u"name"_s, u"kitCollar"_s},
                                            {u"label"_s, u"Collar Piece"_s}, {u"group"_s, group}, {u"kind"_s, u"bool"_s},
                                            {u"control"_s, u"bool"_s}, {u"value"_s, m_showCollar}, {u"model"_s, true}});
        if (!m_groups.contains(group))
            m_groups.append(group);
    }
    emit fieldsChanged();
}

namespace {
constexpr int kKitDefault = -2;
constexpr int kKitTeam = 1000000;
} // namespace

void Rl26AppearanceManager::applyKit() {
    for (const QString& k : {u"kitUniform"_s, u"kitPattern"_s, u"kitColour_r"_s, u"kitColour_g"_s, u"kitColour_b"_s})
        m_options.remove(k);
    int kit = m_kit;
    if (kit == kKitDefault)
        kit = m_teamKits.isEmpty() ? -1 : kKitTeam;
    if (kit < 0)
        return;
    if (kit >= kKitTeam) {
        const QVariantMap k = m_teamKits.value(kit - kKitTeam).toMap();
        if (k.isEmpty())
            return;
        m_options.insert(u"kitUniform"_s, k.value(u"maker"_s).toInt());
        m_options.insert(u"kitPattern"_s, k.value(u"licensed"_s).toInt());
        const QColor colour(k.value(u"colour"_s).toString());
        if (colour.isValid()) {
            m_options.insert(u"kitColour_r"_s, colour.red());
            m_options.insert(u"kitColour_g"_s, colour.green());
            m_options.insert(u"kitColour_b"_s, colour.blue());
        }
        return;
    }
    m_options.insert(u"kitUniform"_s, kit / 1000);
    m_options.insert(u"kitPattern"_s, kit % 1000);
}

QVariantMap Rl26AppearanceManager::kitField() const {
    QString group = u"Kit & Boots"_s;
    for (const QVariant& v : std::as_const(m_fields)) {
        const QVariantMap f = v.toMap();
        if (f.value(u"name"_s).toString() == u"shoes"_s || f.value(u"name"_s).toString() == u"shirtTucked"_s) {
            group = f.value(u"group"_s).toString();
            break;
        }
    }
    QVariantList choices;
    for (qsizetype i = 0; i < m_teamKits.size(); ++i) {
        const QVariantMap k = m_teamKits.at(i).toMap();
        const bool custom = k.value(u"licensed"_s).toInt() == 0;
        choices.append(QVariantMap{{u"value"_s, kKitTeam + int(i)},
                                   {u"text"_s, k.value(u"name"_s).toString() + (custom ? u" (custom design: team colour)"_s : QString())}});
    }
    choices.append(QVariantMap{{u"value"_s, -1}, {u"text"_s, u"Plain (the kit model's own)"_s}});
    for (const Rll4Lists::Choice& maker : Rll4Lists::manufacturers()) {
        const QString makerName = QString::fromUtf8(maker.name);
        for (const Rll4Lists::Choice& j : Rll4Lists::licensedJerseys(maker.value))
            choices.append(QVariantMap{{u"value"_s, maker.value * 1000 + j.value},
                                       {u"text"_s, u"%1: %2"_s.arg(makerName, QString::fromUtf8(j.name))}});
    }
    const int value = m_kit == kKitDefault ? (m_teamKits.isEmpty() ? -1 : kKitTeam) : m_kit;
    return QVariantMap{{u"key"_s, u"@kit"_s},
                       {u"name"_s, u"kitUniform"_s},
                       {u"label"_s, u"Kit"_s},
                       {u"group"_s, group},
                       {u"kind"_s, u"choice"_s},
                       {u"control"_s, u"choice"_s},
                       {u"value"_s, value},
                       {u"choices"_s, choices},
                       {u"model"_s, true}};
}

void Rl26AppearanceManager::setField(const QString& key, const QVariant& value) {
    if (!m_editor || m_player < 0)
        return;
    if (key == u"@collar"_s) {   // The view's own: show the kit's collar piece.
        m_showCollar = value.toBool();
        readFields();
        rebuild(true);
        return;
    }
    if (key == u"@kit"_s) {   // The view's own kit pick: nothing to save.
        m_kit = value.toInt();
        readFields();
        m_texturesDirty = true;
        rebuild(true);
        return;
    }
    QString name;
    bool swapped = false;
    for (const QVariant& v : std::as_const(m_rawFields)) {
        const QVariantMap f = v.toMap();
        if (f.value(u"key"_s).toString() == key) {
            name = f.value(u"name"_s).toString();
            swapped = f.value(u"swap16"_s).toBool();
            break;
        }
    }
    QVariant stored = value;
    if (swapped) {   // Back to the blob's byte order (see readFields).
        const int v = value.toInt();
        stored = ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);
    }
    bool ok = false;
    QMetaObject::invokeMethod(m_editor, "setAppearanceField", Qt::DirectConnection, Q_RETURN_ARG(bool, ok),
                              Q_ARG(int, m_player), Q_ARG(QString, key), Q_ARG(QVariant, stored));
    bool models = affectsModels(name);
    // A beard / hair preset: its parts (moustache, chin hair, sideburns / hair top, sides, cap) are set too, as the
    // game's appearance screen does.
    const QList<QPair<QString, int>> preset = m_rll4 ? QList<QPair<QString, int>>() : Rl26Character::presetOptions(name, value.toInt());
    for (const auto& [part, guid] : preset) {
        for (const QVariant& v : std::as_const(m_rawFields)) {
            const QVariantMap f = v.toMap();
            if (f.value(u"name"_s).toString() != part)
                continue;
            QMetaObject::invokeMethod(m_editor, "setAppearanceField", Qt::DirectConnection, Q_RETURN_ARG(bool, ok),
                                      Q_ARG(int, m_player), Q_ARG(QString, f.value(u"key"_s).toString()),
                                      Q_ARG(QVariant, QVariant(guid)));
            models = models || Rl26Character::affectsModels(part);
            m_texturesDirty = m_texturesDirty || !Rl26Character::onlyShapes(part);
            break;
        }
    }
    readFields();
    m_texturesDirty = m_texturesDirty || m_rll4 || !Rl26Character::onlyShapes(name);
    rebuild(models);
}

void Rl26AppearanceManager::previewField(const QString& name, double value) {
    if (name.isEmpty() || m_models.isEmpty() || affectsModels(name))
        return;
    m_options.insert(name, value);
    updateGeometry(true);
}

void Rl26AppearanceManager::rebuild(bool modelsMayChange) {
    if (modelsMayChange || m_models.isEmpty())
        m_models = m_rll4 ? Rll4Character::models(m_options) : Rl26Character::models(m_options);

    QStringList missing;
    for (const auto& [slot, path] : std::as_const(m_models))
        if (!m_cache.contains(path) && !m_failed.contains(path))
            missing.append(path);
    if (!missing.isEmpty()) {
        loadMissing(missing);   // Keeps showing what there is until the new models are in.
        return;
    }
    updateGeometry(!modelsMayChange);
}

void Rl26AppearanceManager::loadMissing(const QStringList& paths) {
    const int loadId = ++m_loadId;
    if (!m_loading) {
        m_loading = true;
        emit loadingChanged();
    }
    setStatus(true, u"Loading %1 model(s)..."_s.arg(paths.size()));

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        m_loading = false;
        emit loadingChanged();
        setStatus(false, u"The application orchestrator is unavailable."_s);
        return;
    }
    const QString location = orchestrator->assetLocation();
    const int version = orchestrator->blobsetVersion();
    const int platform = orchestrator->platform();
    const QString gameKey = BlobsetHash::gameKeyFor(orchestrator->currentGameName());
    const QPointer<Rl26AppearanceManager> self(this);
    (void)QtConcurrent::run([self, loadId, paths, location, version, platform, gameKey]() {
        const Loaded loaded = readModels(paths, location, version, platform, gameKey);
        QMetaObject::invokeMethod(qApp, [self, loadId, loaded]() {
            if (!self)
                return;
            // The models are kept whatever happened meanwhile; only the latest load draws.
            for (auto it = loaded.meshes.cbegin(); it != loaded.meshes.cend(); ++it)
                self->m_cache.insert(it.key(), it.value());
            for (auto it = loaded.errors.cbegin(); it != loaded.errors.cend(); ++it)
                self->m_failed.insert(it.key(), it.value());
            for (auto it = loaded.packs.cbegin(); it != loaded.packs.cend(); ++it)
                self->m_packTextures.insert(it.key(), it.value());
            if (loadId != self->m_loadId)
                return;
            self->m_loading = false;
            emit self->loadingChanged();
            self->updateGeometry(false);
        }, Qt::QueuedConnection);
    });
}

bool Rl26AppearanceManager::affectsModels(const QString& option) const {
    return m_rll4 ? Rll4Character::affectsModels(option) : Rl26Character::affectsModels(option);
}

QColor Rl26AppearanceManager::colourFor(const QString& slot) const {
    const auto pick = [this](const QString& name, const QColor& fallback) {
        const QColor c = m_colours.value(name);
        return c.isValid() ? c : fallback;
    };
    const QColor hair = pick(u"hair_colour"_s, QColor(0x3b, 0x2a, 0x1e));
    if (slot == u"head"_s || slot == u"body"_s)
        return QColor(0xc6, 0x95, 0x74);
    if (slot.startsWith(u"hair"_s))
        return slot == u"hairCap"_s ? hair.darker(115) : hair;
    if (slot == u"moustache"_s || slot == u"chinhair"_s || slot == u"sideburns"_s || slot == u"beard"_s)
        return pick(u"beard_colour"_s, hair);
    if (slot == u"top"_s || slot == u"socks"_s)
        return QColor(0x2f, 0x5f, 0xa8);
    if (slot == u"bottom"_s)
        return QColor(0xe8, 0xe8, 0xe8);
    if (slot == u"shoes"_s)
        return QColor(0x1e, 0x1e, 0x1e);
    return QColor(0x8a, 0x8a, 0x8a);
}

void Rl26AppearanceManager::updateGeometry(bool reshapeOnly) {
    Q_UNUSED(reshapeOnly)   // Whether the parts stay is decided from what they are (below).
    const QHash<QString, float> weights = m_rll4 ? Rll4Character::shapeWeights(m_options) : Rl26Character::shapeWeights(m_options);

    // The parts: one per material of each model.
    struct Spec {
        Part part;
        QString path;
        QByteArray vertices;
        QByteArray indices;
        QByteArray colours;   // Float RGBA per vertex, or empty.
        QVector3D min, max;
    };
    QList<Spec> specs;
    QStringList names, problems;

    // Head size: the game scales the head bone. The head and what sits on it grow or shrink about the jaw, eased
    // into the neck below it (0.5 = as made; about +-10% at the ends, an estimate of the game's range).
    const double headSize = m_rll4 ? Rll4Character::optionValue(u"headSize"_s, m_options)
                                   : Rl26Character::optionValue(u"headSize"_s, m_options);
    const float headScale = 1.0f + float(headSize - 0.5) * 0.2f;
    QVector3D pivot;
    bool scaleHead = false;
    for (const auto& [slot, path] : std::as_const(m_models)) {
        const std::shared_ptr<const MeshFile> head = slot == u"head"_s ? m_cache.value(path) : nullptr;
        if (head) {
            pivot = QVector3D((head->boundsMin.x() + head->boundsMax.x()) / 2, head->boundsMax.y() - 0.19f,
                              (head->boundsMin.z() + head->boundsMax.z()) / 2);
            scaleHead = std::fabs(headScale - 1.0f) > 1e-4f;
        }
    }
    static const QSet<QString> onHead = {u"head"_s, u"hairTop"_s, u"hairSide"_s, u"hairSide2"_s, u"hairCap"_s,
                                         u"moustache"_s, u"chinhair"_s, u"sideburns"_s, u"headWear"_s, u"beard"_s};
    QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D hi = -lo;
    bool any = false;
    for (const auto& [slot, path] : std::as_const(m_models)) {
        const std::shared_ptr<const MeshFile> mesh = m_cache.value(path);
        if (!mesh) {
            problems.append(u"%1: %2"_s.arg(slot, m_failed.value(path, u"not loaded"_s)));
            continue;
        }
        // Qt Quick 3D's UVs are bottom-left based (the meshes' top-left).
        QByteArray vertices = mesh->morphedVertices(weights);
        if (m_rll4 && slot == u"head"_s)
            followEyes(*mesh, vertices);
        if (m_rll4 && slot != u"head"_s && onHead.contains(slot)) {
            // RLL4's hair / beard caps lie right on the scalp / skin (the game draws them with a depth bias): lifted
            // 1.5 mm off it, or the head shows through them in patches.
            auto* p = reinterpret_cast<float*>(vertices.data());
            const auto* idx = reinterpret_cast<const quint32*>(mesh->displayIndices.constData());
            const qsizetype total = vertices.size() / MeshFile::kDisplayStride;
            std::vector<char> lifted(size_t(total), 0);
            for (const MeshFile::Submesh& sub : mesh->submeshes) {
                if (!mesh->surfaces.value(sub.surface).endsWith(u"_cap"_s, Qt::CaseInsensitive))
                    continue;
                for (quint32 i = 0; i < sub.indexCount; ++i) {
                    const qsizetype at = qsizetype(sub.indexStart) + i;
                    if (at >= mesh->displayIndices.size() / 4)
                        break;
                    const quint32 k = idx[at];
                    if (k >= quint32(total) || lifted[k])
                        continue;
                    lifted[k] = 1;
                    for (int j = 0; j < 3; ++j)
                        p[k * 8 + j] += p[k * 8 + 3 + j] * 0.0015f;
                }
            }
        }
        auto* v = reinterpret_cast<float*>(vertices.data());
        const qsizetype count = vertices.size() / MeshFile::kDisplayStride;
        for (qsizetype k = 0; k < count; ++k)
            v[k * 8 + 7] = 1.0f - v[k * 8 + 7];
        if (scaleHead && onHead.contains(slot)) {
            const float from = pivot.y() - 0.09f;
            for (qsizetype k = 0; k < count; ++k) {
                float* p = v + k * 8;
                const float t = std::clamp((p[1] - from) / (pivot.y() - from), 0.0f, 1.0f);
                const float f = 1.0f + (headScale - 1.0f) * t * t * (3 - 2 * t);
                p[0] = pivot.x() + (p[0] - pivot.x()) * f;
                p[1] = pivot.y() + (p[1] - pivot.y()) * f;
                p[2] = pivot.z() + (p[2] - pivot.z()) * f;
            }
        }

        QMap<int, QList<int>> bySurface;
        for (qsizetype s = 0; s < mesh->submeshes.size(); ++s)
            bySurface[mesh->submeshes.at(s).surface].append(int(s));
        for (auto group = bySurface.cbegin(); group != bySurface.cend(); ++group) {
            const QString material = mesh->surfaces.value(group.key());
            const Rl26Character::MaterialTexture texture =
                    m_rll4 ? Rll4Character::materialTexture(slot, material, path, m_packTextures.value(path), m_options)
                           : Rl26Character::materialTexture(slot, material, m_options);
            if (texture.kind == Rl26Character::MaterialTexture::Kind::Hidden)
                continue;
            Spec spec;
            spec.part = {u"%1|%2|%3"_s.arg(slot).arg(group.key()).arg(material), slot, material, nullptr, colourFor(slot), texture};
            spec.path = path;
            spec.vertices = vertices;
            // Hair strands: the vertex colours' occlusion (R) darkens the roots, as the game's hair shader does.
            if (texture.style == u"hair"_s && mesh->vertexColours.size() == count * 4) {
                spec.colours.resize(count * 16);
                auto* c = reinterpret_cast<float*>(spec.colours.data());
                for (qsizetype k = 0; k < count; ++k) {
                    const float ao = 0.25f + 0.75f * quint8(mesh->vertexColours.at(k * 4)) / 255.0f;
                    c[k * 4] = c[k * 4 + 1] = c[k * 4 + 2] = ao;
                    c[k * 4 + 3] = 1.0f;
                }
            }
            for (const int s : group.value()) {
                const MeshFile::Submesh& sub = mesh->submeshes.at(s);
                spec.indices += mesh->displayIndices.mid(qsizetype(sub.indexStart) * 4, qsizetype(sub.indexCount) * 4);
            }
            if (m_rll4 && slot == u"top"_s && !m_showCollar) {
                spec.indices = withoutCollar(*mesh, spec.indices);
                spec.path += u"#nocollar"_s;   // A new geometry when it's shown / hidden.
            }
            spec.min = mesh->boundsMin;
            spec.max = mesh->boundsMax;
            specs.append(spec);
        }

        names.append(modelName(path));
        lo = QVector3D(std::min(lo.x(), mesh->boundsMin.x()), std::min(lo.y(), mesh->boundsMin.y()), std::min(lo.z(), mesh->boundsMin.z()));
        hi = QVector3D(std::max(hi.x(), mesh->boundsMax.x()), std::max(hi.y(), mesh->boundsMax.y()), std::max(hi.z(), mesh->boundsMax.z()));
        any = true;
        if (slot == u"head"_s) {
            // The face: the top of the head mesh (it runs down to the shoulders).
            const QVector3D size = mesh->boundsMax - mesh->boundsMin;
            m_headCenter = QVector3D((mesh->boundsMin.x() + mesh->boundsMax.x()) / 2, mesh->boundsMax.y() - size.y() * 0.3f,
                                     (mesh->boundsMin.z() + mesh->boundsMax.z()) / 2);
            m_headRadius = std::max(0.08f, size.y() * 0.35f);
        }
    }

    // The same parts of the same models: only their vertices change (the view keeps its models). Anything else:
    // new geometries for a new list. Qt Quick 3D's models don't take a geometry over from a model that's gone, so
    // geometries are never reused across lists.
    bool same = specs.size() == m_partList.size();
    for (qsizetype i = 0; same && i < specs.size(); ++i)
        same = specs[i].part.key == m_partList[i].key && m_geometryPath.value(specs[i].part.key) == specs[i].path;
    const bool changedModel = !same;
    if (same) {
        for (qsizetype i = 0; i < specs.size(); ++i) {
            m_partList[i].geometry->setVertices(specs[i].vertices);
            m_partList[i].colour = specs[i].part.colour;
            m_partList[i].texture = specs[i].part.texture;
        }
    } else {
        const QList<AppearanceGeometry*> old = m_geometries.values();
        m_geometries.clear();
        m_geometryPath.clear();
        QList<Part> parts;
        for (Spec& spec : specs) {
            auto* geometry = new AppearanceGeometry();
            geometry->setParent(this);
            geometry->setData(spec.vertices, spec.indices, spec.min, spec.max, spec.colours);
            spec.part.vertexColours = !spec.colours.isEmpty();
            spec.part.geometry = geometry;
            m_geometries.insert(spec.part.key, geometry);
            m_geometryPath.insert(spec.part.key, spec.path);
            parts.append(spec.part);
        }
        m_partList = parts;
        m_modelsInfo = names.join(u"  |  "_s);
        publishParts();
        for (AppearanceGeometry* g : old)
            g->deleteLater();
    }

    if (any) {
        m_center = (lo + hi) / 2;
        m_radius = std::max(0.1f, (hi - lo).length() / 2);
        emit boundsChanged();
        if (m_firstBuild) {
            m_firstBuild = false;
            emit viewReset();
        }
    }
    if (!problems.isEmpty())
        setStatus(false, u"Couldn't load: "_s + problems.join(u"; "_s));
    else if (!any)
        setStatus(false, u"No models: is Rugby League 26's blobset loaded?"_s);
    else
        setStatus(true, u"%1 part(s), %2 blend shape(s) active"_s.arg(m_partList.size()).arg(weights.size()));

    if (any && (m_texturesDirty || changedModel)) {
        m_texturesDirty = false;
        startTextures();
    }
}

QString Rl26AppearanceManager::environmentUrl() {
    if (!m_environmentUrl.isEmpty())
        return m_environmentUrl;
    if (!m_textureDir)
        m_textureDir = new QTemporaryDir();
    if (!m_textureDir->isValid())
        return {};
    // A dark studio with soft boxes: a key front left, a fill front right, two rims behind and one above. What the
    // skin, eyes and hair reflect (their gloss), like the game's light probes.
    const int W = 1024, H = 512;
    QImage image(W, H, QImage::Format_RGBA8888);
    struct Box {
        float azimuth, elevation, width, height, r, g, b;
    };
    const Box boxes[] = {{-40, 25, 55, 40, 1.00f, 0.96f, 0.90f}, {45, 15, 45, 35, 0.45f, 0.47f, 0.50f},
                         {150, 20, 25, 50, 0.85f, 0.88f, 0.95f}, {-150, 20, 25, 50, 0.85f, 0.88f, 0.95f},
                         {0, 75, 120, 25, 0.35f, 0.35f, 0.35f}};
    for (int y = 0; y < H; ++y) {
        const float elevation = 90.0f - 180.0f * (y + 0.5f) / H;
        uchar* o = image.scanLine(y);
        for (int x = 0; x < W; ++x, o += 4) {
            const float azimuth = 360.0f * (x + 0.5f) / W - 180.0f;
            // Background: a dim gradient, darker below the horizon.
            float v = elevation > 0 ? 0.06f + 0.06f * elevation / 90.0f : 0.05f + 0.02f * elevation / 90.0f;
            float r = v, g = v, b = v * 1.05f;
            for (const Box& box : boxes) {
                float da = std::fabs(azimuth - box.azimuth);
                da = std::min(da, 360.0f - da);
                const float fx = std::clamp(1.0f - (da - box.width / 2) / 12.0f, 0.0f, 1.0f);
                const float fy = std::clamp(1.0f - (std::fabs(elevation - box.elevation) - box.height / 2) / 12.0f, 0.0f, 1.0f);
                const float f = fx * fy;
                r += box.r * f;
                g += box.g * f;
                b += box.b * f;
            }
            o[0] = uchar(std::clamp(r, 0.f, 1.f) * 255);
            o[1] = uchar(std::clamp(g, 0.f, 1.f) * 255);
            o[2] = uchar(std::clamp(b, 0.f, 1.f) * 255);
            o[3] = 255;
        }
    }
    const QString file = QDir(m_textureDir->path()).filePath(u"studio.png"_s);
    if (image.save(file, "PNG", 100))
        m_environmentUrl = QUrl::fromLocalFile(file).toString();
    return m_environmentUrl;
}

QVariantMap Rl26AppearanceManager::textures() const {
    QVariantMap out;
    for (auto it = m_textureUrl.cbegin(); it != m_textureUrl.cend(); ++it)
        out.insert(it.key(), QVariantMap{{u"url"_s, it.value()}, {u"alpha"_s, m_textureAlpha.value(it.key(), 0)},
                                         {u"normal"_s, m_textureNormal.value(it.key())},
                                         {u"rough"_s, m_textureRough.value(it.key())}});
    return out;
}

void Rl26AppearanceManager::publishParts() {
    QVariantList parts;
    for (const Part& p : std::as_const(m_partList))
        parts.append(QVariantMap{{u"key"_s, p.key}, {u"slot"_s, p.slot}, {u"material"_s, p.material},
                                 {u"geometry"_s, QVariant::fromValue(p.geometry)},
                                 {u"colour"_s, QVariant::fromValue(p.colour)},
                                 {u"style"_s, p.texture.style},
                                 // The material's own roughness where no "mat" map is built.
                                 {u"roughness"_s, std::clamp(0.3f + 0.75f * (1.0f - p.texture.gloss), 0.15f, 1.0f)},
                                 {u"vertexColours"_s, p.vertexColours}});
    m_parts = parts;
    emit partsChanged();
}

// ---- Textures ------------------------------------------------------------------------------------------------

struct Rl26AppearanceManager::TextureCache {
    QMutex mutex;
    QHash<QString, QImage> images;   // Decoded textures by lower case path (kept for the session).
    Rl26SkinCompositor::Placements placements;
    bool placementsRead = false;
    bool placementsRll4 = false;     // Which game's atlases they are.
};

void Rl26AppearanceManager::startTextures() {
    if (!m_textureDir)
        m_textureDir = new QTemporaryDir();
    if (!m_textureDir->isValid())
        return;
    if (!m_textureCache)
        m_textureCache = std::make_shared<TextureCache>();
    const MeshTextureExport::Context context = MeshTextureExport::currentContext();
    if (!context.valid)
        return;

    struct Job {
        QString key;
        Rl26Character::MaterialTexture texture;
    };
    QList<Job> jobs;
    for (const Part& p : std::as_const(m_partList))
        if (p.texture.kind == Rl26Character::MaterialTexture::Kind::Single
            || p.texture.kind == Rl26Character::MaterialTexture::Kind::Layers)
            jobs.append({p.key, p.texture});
    const int id = ++m_textureId;
    if (!m_texturing) {
        m_texturing = true;
        emit texturingChanged();
    }
    const QString dir = m_textureDir->path();
    const std::shared_ptr<TextureCache> cache = m_textureCache;
    const QPointer<Rl26AppearanceManager> self(this);
    const bool packs = m_rll4;
    (void)QtConcurrent::run([self, id, jobs, context, dir, cache, packs]() {
        const auto exists = [&context](const QString& path) { return MeshTextureExport::hasTexture(context, path); };
        QHash<QString, QString> urls, normals, roughs;
        QHash<QString, int> alphas;
        {
            QMutexLocker locker(&cache->mutex);
            if (!cache->placementsRead || cache->placementsRll4 != packs) {
                cache->placementsRead = true;
                cache->placementsRll4 = packs;
                if (packs) {
                    // RLL4: an atlas per part (the eyes, eyebrows, ... in the head's).
                    cache->placements.clear();
                    for (const QString& atlas : {u"characters/player/head"_s, u"characters/player/body"_s})
                        cache->placements.insert(Rl26SkinCompositor::parsePlacementsRll4(
                            MeshTextureExport::readAssetData(context, atlas, u"texsrc"_s)));
                } else {
                    cache->placements = Rl26SkinCompositor::parsePlacements(
                        MeshTextureExport::readAssetData(context, u"characters/player/global"_s, u"texsrc"_s));
                }
            }
            // Every texture the parts need, read once.
            QStringList wanted;
            for (const Job& job : jobs)
                for (const QString& path : Rl26SkinCompositor::texturePaths(job.texture, exists))
                    if (!cache->images.contains(path) && !wanted.contains(path))
                        wanted.append(path);
            if (cache->images.size() > 400)
                cache->images.clear();   // Many players later: start again rather than grow without end.
            // RLL4 keeps most textures in texture packs (the kits', the real players' heads, the hair caps).
            const QHash<QString, QImage> fetched = packs ? MeshTextureExport::fetchPackImages(wanted, context, 0)
                                                         : MeshTextureExport::fetchImages(wanted, context, 0);
            for (const QString& path : std::as_const(wanted)) {
                cache->images.insert(path, fetched.value(path));   // Null: not found (not asked again).
                if (fetched.value(path).isNull())
                    qWarning().noquote() << u"Appearance: texture not found or not decoded: %1"_s.arg(path);
            }
            if (packs) {
                // Where each layer goes in its atlas (RLL4): reported once per texture, to check the eyes etc.
                for (const Job& job : jobs)
                    for (const Rl26Character::TextureLayer& l : job.texture.layers) {
                        const QString name = l.path.section(u'/', -1);
                        static QSet<QString> told;
                        if (told.contains(name))
                            continue;
                        told.insert(name);
                        const bool placed = cache->placements.contains(BlobsetHash::fnv1a32((name + u"_2"_s).toStdString()))
                                            || cache->placements.contains(BlobsetHash::fnv1a32(name.toStdString()));
                        qInfo().noquote() << u"Appearance: %1 layer %2 (%3, %4), %5 in the atlas (%6 placements)"_s
                                                 .arg(job.key, l.path, l.blend).arg(l.intensity)
                                                 .arg(placed ? u"placed"_s : u"not placed"_s).arg(cache->placements.size());
                    }
            }
        }
        int n = 0;
        const auto save = [&](const QImage& image) -> QString {
            if (image.isNull())
                return {};
            const QString file = QDir(dir).filePath(u"t%1_%2.png"_s.arg(id).arg(++n));
            return image.save(file, "PNG", 100) ? QUrl::fromLocalFile(file).toString() : QString();   // 100: fastest.
        };
        for (const Job& job : jobs) {
            QImage colour, normal, rough;
            {
                QMutexLocker locker(&cache->mutex);
                colour = Rl26SkinCompositor::compose(job.texture, cache->images, cache->placements, exists, 1024);
                normal = Rl26SkinCompositor::composeNormal(job.texture, cache->images, cache->placements, exists, 1024);
                rough = Rl26SkinCompositor::composeRoughness(job.texture, cache->images, cache->placements, exists, 1024);
            }
            const QString url = save(colour);
            if (url.isEmpty())
                continue;
            urls.insert(job.key, url);
            alphas.insert(job.key, int(Rl26SkinCompositor::alphaFor(job.texture)));
            if (const QString u = save(normal); !u.isEmpty())
                normals.insert(job.key, u);
            if (const QString u = save(rough); !u.isEmpty())
                roughs.insert(job.key, u);
        }
        QMetaObject::invokeMethod(qApp, [self, id, urls, alphas, normals, roughs]() {
            if (!self || id != self->m_textureId)
                return;
            self->m_textureUrl = urls;
            self->m_textureAlpha = alphas;
            self->m_textureNormal = normals;
            self->m_textureRough = roughs;
            self->m_texturing = false;
            emit self->texturingChanged();
            emit self->texturesChanged();   // The parts stay: their materials pick the new textures up.
        }, Qt::QueuedConnection);
    });
}
