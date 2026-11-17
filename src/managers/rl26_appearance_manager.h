#ifndef RL26_APPEARANCE_MANAGER_H
#define RL26_APPEARANCE_MANAGER_H

#include "mesh_file.h"
#include "rl26_character.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>
#include <QtQuick3D/QQuick3DGeometry>

#include <memory>

/// One part of the player (head, hair, top, ...) as Qt Quick 3D geometry: float position / normal / uv per vertex.
class AppearanceGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit AppearanceGeometry(QQuick3DObject* parent = nullptr);
    // `colours`: float RGBA per vertex (the hair's occlusion), or empty.
    void setData(const QByteArray& vertices, const QByteArray& indices, const QVector3D& min, const QVector3D& max,
                 const QByteArray& colours = {});
    void setVertices(const QByteArray& vertices);

private:
    QByteArray interleaved(const QByteArray& vertices) const;
    QByteArray m_colours;
};

/// AppearanceEditor.qml: a Rugby League 26 player built in 3D from their appearance, edited live.
///
/// The fields come from the editor that opened it (DatabaseManager, SaveEditorManager or Rl26CareerEditorManager:
/// all three have appearanceFields(player) / setAppearanceField(player, key, value)), so edits go through the
/// editor and are saved with it. Which models make up the player and the blend shape weights are the game's own
/// rules (Rl26Character); the models (".modelcomponent" + ".blendshapes" pairs) are read from the loaded blobset in
/// the background and kept for the session.
class Rl26AppearanceManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList parts READ parts NOTIFY partsChanged)         // {slot, geometry, colour}
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)      // The editor's items + choices / slider
    Q_PROPERTY(QStringList groups READ groups NOTIFY fieldsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasModel READ hasModel NOTIFY partsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(QVector3D center READ center NOTIFY boundsChanged)
    Q_PROPERTY(float radius READ radius NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D headCenter READ headCenter NOTIFY boundsChanged)
    Q_PROPERTY(float headRadius READ headRadius NOTIFY boundsChanged)
    Q_PROPERTY(QString modelsInfo READ modelsInfo NOTIFY partsChanged)    // The model names, for the info strip.
    Q_PROPERTY(bool texturing READ texturing NOTIFY texturingChanged)    // Textures being built in the background.
    // The parts' colour textures by part key: {url, alpha (0 opaque, 1 cut out, 2 blended)}. Apart from the parts so
    // new textures don't rebuild the view's models.
    Q_PROPERTY(QVariantMap textures READ textures NOTIFY texturesChanged)
    // A studio picture (equirectangular) the view lights and reflects the player with.
    Q_PROPERTY(QString environmentUrl READ environmentUrl CONSTANT)

public:
    explicit Rl26AppearanceManager(QObject* parent = nullptr);
    ~Rl26AppearanceManager() override;

    QVariantList parts() const { return m_parts; }
    QVariantList fields() const { return m_fields; }
    QStringList groups() const { return m_groups; }
    bool loading() const { return m_loading; }
    bool hasModel() const { return !m_parts.isEmpty(); }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    QVector3D center() const { return m_center; }
    float radius() const { return m_radius; }
    QVector3D headCenter() const { return m_headCenter; }
    float headRadius() const { return m_headRadius; }
    QString modelsInfo() const { return m_modelsInfo; }
    bool texturing() const { return m_texturing; }
    QVariantMap textures() const;
    QString environmentUrl();

    /// Shows `player` of `editor` (an object with appearanceFields(int) / setAppearanceField(int, QString, QVariant)).
    Q_INVOKABLE void open(QObject* editor, int player);
    /// An edit from the view: written through the editor, then the player is rebuilt.
    Q_INVOKABLE void setField(const QString& key, const QVariant& value);
    /// A slider being dragged: reshapes the player with `value` for option `name`, nothing is written yet (setField
    /// does that on release).
    Q_INVOKABLE void previewField(const QString& name, double value);
    /// Reads the fields again (the editor changed them).
    Q_INVOKABLE void refresh();
    /// Lets go of the editor and the drawn parts (the models stay cached).
    Q_INVOKABLE void close();

signals:
    void partsChanged();
    void fieldsChanged();
    void loadingChanged();
    void statusChanged();
    void boundsChanged();
    void viewReset();   // The first build of a newly opened player: the view puts its camera back.
    void texturingChanged();
    void texturesChanged();

private:
    void readFields();
    void rebuild(bool modelsMayChange);
    void loadMissing(const QStringList& paths);
    void updateGeometry(bool reshapeOnly);
    void setStatus(bool ok, const QString& text);
    QColor colourFor(const QString& slot) const;
    bool affectsModels(const QString& option) const;
    void startTextures();
    void publishParts();

    QPointer<QObject> m_editor;
    int m_player = -1;
    bool m_rll4 = false;   // The editor is Rugby League Live 4's (its rules and textures).
    QHash<QString, QStringList> m_packTextures;   // RLL4: model path -> the textures its texture pack lists.
    // RLL4's kit pick (the view's own: a player's kit is their team's): kKitDefault = the first of the player's team
    // kits (m_teamKits, from the editor's playerKits()), kKitTeam + n = team kit n, maker * 1000 + licensed
    // jersey = that jersey, -1 = the kit model's own texture.
    int m_kit = -2;
    bool m_showCollar = true;   // RLL4: the kit's separate collar / yoke piece (the "Collar Piece" tick box).
    bool m_rll4Database = false;   // RLL4 in the database editor: the fields are named after the game's options.   // RLL4: the kit's separate collar / yoke piece (hidden by default).
    QVariantList m_teamKits;
    void applyKit();
    QVariantMap kitField() const;
    QVariantList m_rawFields;                 // As the editor gives them.
    QVariantList m_fields;
    QStringList m_groups;
    Rl26Character::Options m_options;
    QHash<QString, QColor> m_colours;         // Colour fields, by name ("hair_colour").

    QList<QPair<QString, QString>> m_models;  // slot, path
    QHash<QString, std::shared_ptr<const MeshFile>> m_cache;   // path -> model
    QHash<QString, QString> m_failed;         // path -> why it couldn't be read
    // One part per material of each model: "slot|surface" -> geometry, and the model its geometry shows.
    QHash<QString, AppearanceGeometry*> m_geometries;
    QHash<QString, QString> m_geometryPath;
    struct Part {
        QString key, slot, material;
        AppearanceGeometry* geometry = nullptr;
        QColor colour;
        Rl26Character::MaterialTexture texture;
        bool vertexColours = false;
    };
    QList<Part> m_partList;
    QVariantList m_parts;

    // The parts' composed colour textures (PNG files in m_textureDir), by part key.
    struct TextureCache;
    std::shared_ptr<TextureCache> m_textureCache;
    QTemporaryDir* m_textureDir = nullptr;
    QHash<QString, QString> m_textureUrl;
    QHash<QString, int> m_textureAlpha;       // Rl26SkinCompositor::Alpha
    QHash<QString, QString> m_textureNormal;  // Normal maps (skin, eyes).
    QHash<QString, QString> m_textureRough;   // Roughness (R) + occlusion (G), from the "mat" maps.
    QString m_environmentUrl;
    bool m_texturesDirty = true;
    int m_textureId = 0;
    bool m_texturing = false;
    int m_loadId = 0;
    bool m_loading = false;
    bool m_firstBuild = true;

    QString m_statusText;
    bool m_statusOk = true;
    QVector3D m_center;
    float m_radius = 1.0f;
    QVector3D m_headCenter;
    float m_headRadius = 0.2f;
    QString m_modelsInfo;
};

#endif // RL26_APPEARANCE_MANAGER_H
