#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "bigant_database.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class M3mpToolManager;

// ==========================================
// QML Bridge Manager
// ==========================================
// Rl26DatabaseEditor.qml: the team and player databases of Rugby League 26, Rugby League Live 4, Rugby League Live 3
// and Rugby 25 (started as a port of Wouldy's RL26 and RLL3 Database Editors).
//
// The databases (defaultdata/defaultdata_clubs and defaultdata/defaultdata_players) are in one M3MP:
// licenseddata.mempack.m3mp, path hash 0x2BE30C8187CABAF6 (V4 games) / 0xCE54601F (RLL3, RLL4). load() opens it from
// the blobset, or the edited copy in the mods folder when there is one; save() writes both files back into the
// M3MP (M3MP Creator) as games\<game>\<platform>\mods\<path>, so Modify / Create put it into the game.
//
// The files describe their own fields (BigAntDb), so each game shows what its database has. Field keys are the
// game's names ("fullName", "homeGrounds#0:stadium", "NRL2026Match.fullBack", ...). Every edit sets the field's
// flag, like the in-game editor. RLL3's older fixed layout is read into the same form (BigAntDb::Table::readRll3()),
// with its jerseys at fixed places in each team's blob.
//
// Jerseys (a team's blob) and a player's appearance (a player's blob) are paSerial objects (PaSerial): shown as
// grouped fields, named where the games' files give the names away.
//
// From the game's blobset: the team logos (ui\club_logos\large_mirrored\<logo>.pc.dds, through the DDS image provider)
// and the commentary name lists (the sound M3MP, see loadCommentary()). The drop down lists (countries, stadiums, ...)
// are RL26's.
class DatabaseManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString game READ game NOTIFY loadedChanged)          // "rl26", "rll4", "r25" or "".
    Q_PROPERTY(QString gameName READ gameName NOTIFY loadedChanged)
    Q_PROPERTY(QString databaseHash READ databaseHash NOTIFY loadedChanged)   // The M3MP's path hash (hex).
    Q_PROPERTY(int teamCount READ teamCount NOTIFY loadedChanged)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY loadedChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY loadedChanged)
    Q_PROPERTY(QString modsTarget READ modsTarget NOTIFY loadedChanged)
    // Field key -> type ("bool", "int", "hash", "string", "colour", "lineup") of what each database has.
    Q_PROPERTY(QVariantMap teamFieldTypes READ teamFieldTypes NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap playerFieldTypes READ playerFieldTypes NOTIFY loadedChanged)
    Q_PROPERTY(bool unsavedChanges READ unsavedChanges NOTIFY unsavedChangesChanged)
    // Goes up on every edit: bind to it to refresh what's shown.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)

public:
    static constexpr const char* kTeamsPath = "defaultdata/defaultdata_clubs";
    static constexpr const char* kPlayersPath = "defaultdata/defaultdata_players";
    // The M3MP holding them: V4 (RL26, Rugby 25) and RLL4 (32 bit hash) names.
    static bool isDatabaseHash(const QString& hexHash);

    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    bool loaded() const { return m_loaded; }
    bool busy() const { return m_busy; }
    QString game() const { return m_game; }
    QString gameName() const;
    QString databaseHash() const { return m_hash; }
    int teamCount() const { return m_loaded ? m_teams.count() : 0; }
    int playerCount() const { return m_loaded ? m_players.count() : 0; }
    QString sourceText() const { return m_sourceText; }
    QString modsTarget() const;
    QVariantMap teamFieldTypes() const { return m_teamTypes; }
    QVariantMap playerFieldTypes() const { return m_playerTypes; }
    bool unsavedChanges() const { return m_unsaved; }
    int revision() const { return m_revision; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // --- Load / save -------------------------------------------------------------------------------------
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE QString exportCsv(const QString& kind, const QString& pathOrUrl) const;

    // --- Lists ---------------------------------------------------------------------------------------------
    // {index, id, gender, fullName, location, club, abbreviation, logoUrl, primaryColour, rating, players}.
    Q_INVOKABLE QVariantList teamRows(const QString& filter, int gender) const;
    // {index, id, gender, first, last, club, clubName, role, roleName, rating, age}.
    Q_INVOKABLE QVariantList playerRows(const QString& filter, int gender, int team, int role) const;

    // --- One team / player ---------------------------------------------------------------------------------
    // Every field by key ("<key>?" = its flag), every list's present flag ("<list>!"), plus "_index", "_rating",
    // "_logoUrl", "_players".
    Q_INVOKABLE QVariantMap team(int index) const;
    Q_INVOKABLE QVariantMap player(int index) const;
    Q_INVOKABLE bool setTeamField(int index, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setPlayerField(int index, const QString& key, const QVariant& value);
    // A field's flag or a list ("roster", "StandardMatch", ...) on / off.
    Q_INVOKABLE bool setTeamPresent(int index, const QString& key, bool on);
    Q_INVOKABLE bool setPlayerPresent(int index, const QString& key, bool on);

    // The player editor's sections from the database: [{key, label, group}].
    Q_INVOKABLE QVariantList playerSkillFields() const;     // Ratings (sliders).
    Q_INVOKABLE QVariantList playerCareerFields() const;    // Career numbers.
    // Match stats lists: [{list, title, fields: [{key, label}]}].
    Q_INVOKABLE QVariantList playerStatLists() const;

    Q_INVOKABLE int playerRating(int index) const;
    Q_INVOKABLE int teamRating(int index) const;
    Q_INVOKABLE int playerIndexById(int id) const { return m_playerById.value(id, -1); }
    Q_INVOKABLE int teamIndexById(int id) const { return m_teamById.value(id, -1); }
    Q_INVOKABLE QString playerName(int id) const;
    Q_INVOKABLE QString teamName(int id) const;
    Q_INVOKABLE bool canRandomize() const;
    Q_INVOKABLE void randomizeStats(int index, int level);

    // --- Squad, lineups, roles, feeder clubs ------------------------------------------------------------------
    Q_INVOKABLE QVariantList roster(int team) const;   // {slot, id, first, last, roles, rating, known}.
    Q_INVOKABLE bool addRosterPlayer(int team, int playerId);
    Q_INVOKABLE bool setRosterPlayer(int team, int slot, int playerId);
    Q_INVOKABLE bool removeRosterPlayer(int team, int slot);
    Q_INVOKABLE void sortRoster(int team);

    // The match lists (lineups) the database has: [{list, title, shirts}].
    Q_INVOKABLE QVariantList matchLists() const;
    // A lineup: [{key, position, id, shirt, first, last, roles, rating, present, known}].
    Q_INVOKABLE QVariantList lineup(int team, const QString& list) const;
    Q_INVOKABLE QVariantList teamRoles(int team, const QString& list) const;   // Captain, goal kicker, ...
    Q_INVOKABLE bool setLineupPlayer(int team, const QString& list, const QString& key, int playerId);
    Q_INVOKABLE bool setLineupShirt(int team, const QString& list, const QString& key, int shirt);

    // kind "feeder" / "fedFrom". {slot, id, fullName, logoUrl, known}.
    Q_INVOKABLE QVariantList clubLinks(int team, const QString& kind) const;
    Q_INVOKABLE bool addClubLink(int team, const QString& kind, int teamId);
    Q_INVOKABLE bool setClubLink(int team, const QString& kind, int slot, int teamId);
    Q_INVOKABLE bool removeClubLink(int team, const QString& kind, int slot);

    // --- Jerseys (a team's blob) ----------------------------------------------------------------------------
    // {ok, error, count, max, uniforms: [{index, name, hidden, nines}], slots: [{slot, label, uniform}]}.
    Q_INVOKABLE QVariantMap jerseys(int team) const;
    // A uniform's fields: [{key, label, group, kind, value, max}] (PaSerial::Blob::fields()).
    Q_INVOKABLE QVariantList jerseyFields(int team, int uniform) const;
    Q_INVOKABLE bool setJerseyField(int team, int uniform, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setJerseySlot(int team, int slot, int uniform);
    // Adds a uniform (a copy of `from`) at the end. Returns its index or -1.
    Q_INVOKABLE int addJersey(int team, int from);
    Q_INVOKABLE bool removeJersey(int team, int uniform);
    // A team's jerseys from / to a file (.bajersey: the team's whole jersey blob, same game only).
    Q_INVOKABLE QString exportJerseys(int team, const QString& pathOrUrl) const;
    Q_INVOKABLE QString importJerseys(int team, const QString& pathOrUrl);

    // --- A player's appearance (a player's blob) -------------------------------------------------------------
    Q_INVOKABLE QVariantList appearanceFields(int player) const;
    Q_INVOKABLE bool setAppearanceField(int player, const QString& key, const QVariant& value);

    // --- Drop down lists (RL26) --------------------------------------------------------------------------------
    Q_INVOKABLE QStringList list(const QString& name) const;
    // Lists where the stored value isn't the position: [{name, value}] (RLL3: "countries", "supporters", "logos";
    // RLL4: "logos"). Empty for other games.
    Q_INVOKABLE QVariantList choices(const QString& name) const;
    Q_INVOKABLE QStringList teamNames() const;
    Q_INVOKABLE QStringList commentaryNames(const QString& kind) const;
    Q_INVOKABLE int commentaryIndex(const QString& kind, uint hash) const;
    Q_INVOKABLE uint commentaryHash(const QString& kind, int index) const;
    // The name as the game stores it (RLL3 keeps a team's commentary name as text), and its index.
    Q_INVOKABLE QString commentaryValue(const QString& kind, int index) const;
    Q_INVOKABLE int commentaryIndexOf(const QString& kind, const QString& value) const;
    Q_INVOKABLE QString logoUrl(const QString& logo) const;
    Q_INVOKABLE QString prettyLabel(const QString& key) const;

signals:
    void loadedChanged();
    void busyChanged();
    void unsavedChangesChanged();
    void revisionChanged();
    void statusChanged();
    void finished(bool ok, const QString& message);

private:
    struct Commentary {
        QList<uint> hashes;
        QStringList names;      // Title case, to show.
        QStringList values;     // As stored.
    };

    M3mpToolManager* m_m3mp = nullptr;
    BigAntDb::Table m_teams;
    BigAntDb::Table m_players;
    QHash<int, int> m_playerById;
    QHash<int, int> m_teamById;
    QVariantMap m_teamTypes;
    QVariantMap m_playerTypes;
    QStringList m_rosterKeys;      // roster.player1 .. 64.
    QHash<QString, Commentary> m_commentary;
    mutable QHash<QString, QString> m_logoUrls;   // Logo name (lower case) -> image URL ("" = not in the blobset).
    QHash<int, int> m_playerClub;                 // RLL3: player id -> the first team whose squad has them.
    QHash<QString, QString> m_ratingFields;   // C# name -> this database's field (empty: not available).

    QString m_game;
    QString m_hash;
    bool m_loaded = false;
    bool m_busy = false;
    bool m_unsaved = false;
    bool m_saving = false;
    int m_revision = 0;
    QString m_sourceText;
    QString m_statusText;
    bool m_statusOk = true;
    QString m_saveTemp;

    void loadCommentary();
    QString teamFullName(int team) const;
    void rebuildIndexes();
    void edited();
    void setStatus(bool ok, const QString& text);
    void setBusy(bool busy);
    QVariantMap playerSummary(int id) const;
    QVariantMap tableRecord(const BigAntDb::Table& table, int index) const;
    QList<int> slotValues(int team, const QStringList& keys) const;
    void writeSlotValues(int team, const QStringList& keys, const QList<int>& values);
    QStringList clubLinkKeys(const QString& kind) const;
    QStringList listFieldKeys(const QString& list, bool includeDerived = false) const;
    QStringList lineupKeys(const QString& list) const;   // A match list's players in the game's numbering.
    QString ratingField(const QString& csharpName) const { return m_ratingFields.value(csharpName); }
    int age(int player) const;
};

#endif // DATABASE_MANAGER_H
