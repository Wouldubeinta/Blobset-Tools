#ifndef RLL4_CAREER_EDITOR_MANAGER_H
#define RLL4_CAREER_EDITOR_MANAGER_H

#include "raw_table_model.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// ==========================================
// QML Bridge Manager
// ==========================================
// CareerEditor.qml (SaveEditor.qml in career mode) with Rugby League Live 4 loaded: a career save (Documents\My Games\
// Rugby League Live 4\Career000\CareerData.SAV). A port of Wouldy's RLL4 Career Save Editor (C#).
//
// The file is laid out like the Academy save (u32, u32 CRC-32 of everything after the first 8 bytes, then the data),
// but bigger: all the game's teams (a big endian u16 count, then 6850 byte records at 4892860) and players (u16 count,
// 1377 byte records at 5863136), plus the career: settings, the career team / player, player status (injuries,
// suspensions, ...), contracts, retirements, competitions and their fixtures. The team record is the Academy one
// (6 bytes longer); a player is the Academy one after 91 bytes of career data (form, popularity, career stats,
// contract, salary, XP, feeder club). Record layouts: the C# editor's ReadSave.cs (generated into
// rll4_career_fields.inc); every byte the editor doesn't know is kept as it is.
//
// The API is Rll4SaveEditorManager's (so SaveEditor.qml shows it the same way) plus the career pages.
class Rll4CareerEditorManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY loadedChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loadedChanged)
    Q_PROPERTY(bool checksumOk READ checksumOk NOTIFY loadedChanged)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY revisionChanged)
    Q_PROPERTY(int teamCount READ teamCount NOTIFY revisionChanged)
    Q_PROPERTY(bool unsavedChanges READ unsavedChanges NOTIFY unsavedChangesChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY loadedChanged)
    Q_PROPERTY(QString gameName READ gameName CONSTANT)
    Q_PROPERTY(bool canAddRecords READ canAddRecords CONSTANT)
    Q_PROPERTY(bool hasLineups READ hasLineups CONSTANT)
    Q_PROPERTY(bool career READ career CONSTANT)
    Q_PROPERTY(QString careerInfo READ careerInfo NOTIFY loadedChanged)

public:
    explicit Rll4CareerEditorManager(QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    QString filePath() const { return m_path; }
    QString fileName() const;
    bool checksumOk() const { return m_checksumOk; }
    int playerCount() const;
    int teamCount() const;
    bool unsavedChanges() const { return m_unsaved; }
    int revision() const { return m_revision; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    bool demo() const { return false; }
    QString gameName() const { return QStringLiteral("Rugby League Live 4"); }
    bool canAddRecords() const { return false; }   // The career has every team and player already.
    bool career() const { return true; }
    QString careerInfo() const { return m_careerInfo; }
    bool hasLineups() const { return true; }

    Q_INVOKABLE QString defaultFolder() const;
    Q_INVOKABLE QString defaultFile() const;
    Q_INVOKABLE bool load(const QString& pathOrUrl = QString());
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& pathOrUrl);

    // {index, id, originalId, first, last, shirt, age, female, role, rating}
    Q_INVOKABLE QVariantList playerRows(const QString& filter) const;
    // {index, id, originalId, fullName, shortName, abbreviation, primaryColour, secondaryColour, female, logo, logoUrl}
    Q_INVOKABLE QVariantList teamRows(const QString& filter) const;
    Q_INVOKABLE QString teamLogo(int team) const;
    Q_INVOKABLE QString teamLogoUrl(int team) const;

    // Like SaveEditorManager: [{key, label, group, kind, value, min, max, offset, readOnly, slider, choices}].
    Q_INVOKABLE QVariantList fields(const QString& kind, int index) const;
    Q_INVOKABLE bool setField(const QString& kind, int index, const QString& key, const QVariant& value);
    Q_INVOKABLE QStringList groups(const QString& kind) const;
    // The field groups a detail tab shows ("Details", "Stadiums", "Identity", "Ratings", "Stats").
    Q_INVOKABLE QStringList pageGroups(const QString& kind, const QString& page) const;

    Q_INVOKABLE QVariantList appearanceFields(int player) const;
    Q_INVOKABLE bool setAppearanceField(int player, const QString& key, const QVariant& value);
    Q_INVOKABLE int playerRating(int player) const;
    Q_INVOKABLE int teamRating(int team) const;   // The standard lineup's average OVR (as the C# editor shows it).

    // --- Roster (50 player ids) -------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantList rosterRows(int team) const;
    Q_INVOKABLE int rosterMax() const { return 50; }
    Q_INVOKABLE int rosterMin() const { return 17; }
    Q_INVOKABLE bool addRosterPlayer(int team, int playerId);
    Q_INVOKABLE bool removeRosterPlayer(int team, int slot);
    Q_INVOKABLE bool moveRosterPlayer(int team, int slot, int delta);
    Q_INVOKABLE bool sortRoster(int team);        // A-Z by last name.
    Q_INVOKABLE QVariantList pickablePlayers(const QString& filter) const;
    Q_INVOKABLE bool databasePlayersAvailable() const;

    // --- Lineups ----------------------------------------------------------------------------------------------------
    // {standard, roles, nines, ninesRoles: [{slot, label, id, name, role, rating}], hasNines, choices: [{name, value}]}
    Q_INVOKABLE QVariantMap lineup(int team) const;
    // group: "standard", "roles", "nines", "ninesRoles".
    Q_INVOKABLE bool setLineupPlayer(int team, const QString& group, int slot, int playerId);
    Q_INVOKABLE bool setHasNines(int team, bool on);
    // The C# editor's Generate Best Lineup (by rating and roles; kickers by place / punt kick). Returns warnings.
    Q_INVOKABLE QString generateBestLineup(int team);

    // The Database Editor's lineup page: [{list, title, shirts}], rows {key, position, id, first, last, roles, rating,
    // present, known}, roles {key, name, ...}.
    Q_INVOKABLE QVariantList matchLists(int team = -1) const;
    Q_INVOKABLE bool listPresent(int team, const QString& list) const;
    Q_INVOKABLE bool setListPresent(int team, const QString& list, bool on);
    Q_INVOKABLE QVariantList lineupRows(int team, const QString& list) const;
    Q_INVOKABLE QVariantList teamRoles(int team, const QString& list) const;
    Q_INVOKABLE bool setLineupSlot(int team, const QString& list, const QString& key, int playerId);
    Q_INVOKABLE bool setLineupShirt(int, const QString&, const QString&, int) { return false; }
    Q_INVOKABLE QVariantList pickerRows(const QString& filter, int team, bool squadOnly) const;

    // --- Jerseys (8 per team) ---------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantMap jerseys(int team) const;
    Q_INVOKABLE QVariantList jerseyFields(int team, int uniform) const;
    Q_INVOKABLE bool setJerseyField(int team, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setJerseySlot(int, int, int) { return false; }
    Q_INVOKABLE int addJersey(int team, int from);
    Q_INVOKABLE bool removeJersey(int team, int uniform);

    // --- Career ------------------------------------------------------------------------------------------------------
    // The career pages: [{index, key, title, subtitle, tabs}] (kind "career", index = the page: settings, selection,
    // status, contracts, retirements, competitions). fields("career", 0 / 1) are the settings / selection.
    Q_INVOKABLE QVariantList careerItems() const;
    // Player status: [{index, id, name, reason, reasonName, duration, unit, detail, playerIndex}].
    Q_INVOKABLE QVariantList statusRows() const;
    Q_INVOKABLE bool removeStatus(int index);
    // Contracts (the off-season transfers): [{index, id, name, currentTeam, newTeam, ..., playerIndex}], and the
    // fields of one (kind "contract").
    Q_INVOKABLE QVariantList contractRows() const;
    Q_INVOKABLE bool removeContract(int index);
    // Retirements (this season's): [{index, id, name, reason, playerIndex}], {year, month, day}.
    Q_INVOKABLE QVariantList retirementRows() const;
    Q_INVOKABLE QVariantMap retirementDate() const;
    Q_INVOKABLE bool setRetirementReason(int index, int reason);
    Q_INVOKABLE bool removeRetirement(int index);
    // Competitions: the ladders (the C# editor's 7) and the fixture lists (its 51 stages).
    Q_INVOKABLE QVariantList ladderComps() const;           // [{index, name}]
    Q_INVOKABLE QVariantList ladderRows(int comp) const;    // [{pos, id, name, logoUrl, colour, played, ...}]
    Q_INVOKABLE QVariantList fixtureStages() const;         // [{index, name}]
    Q_INVOKABLE QStringList fixtureRounds(int stage) const;
    Q_INVOKABLE QVariantList fixtureRows(int stage, int round) const;
    // A fixture's team v team stats, players and man of the match (CareerMatch::build); match = its "match" in
    // fixtureRows.
    Q_INVOKABLE QVariantMap matchStats(int stage, int match) const;

    // --- Records ----------------------------------------------------------------------------------------------------
    // Not in a career (returns -1 / false): the save has a fixed set.
    Q_INVOKABLE int addRecord(const QString& kind, bool female);
    Q_INVOKABLE bool deleteRecord(const QString& kind, int index);
    // A .tem / .ply over a record, keeping its id: the Academy editors' files (converted) or a career record. "" or an
    // error. Export writes the Academy format, so the files go both ways.
    Q_INVOKABLE QString importRecord(const QString& kind, int index, const QString& pathOrUrl);
    Q_INVOKABLE QString exportRecord(const QString& kind, int index, const QString& pathOrUrl) const;
    Q_INVOKABLE QString recordFileName(const QString& kind, int index) const;   // "<name>.tem" / ".ply"
    Q_INVOKABLE QString exportDecoded(const QString& pathOrUrl) const;

    Q_INVOKABLE QVariantList rawRows(const QString& kind, int index) const;
    Q_INVOKABLE int recordSize(const QString& kind) const;
    Q_INVOKABLE QString recordOffset(const QString& kind, int index) const;
    Q_INVOKABLE bool setRawByte(const QString& kind, int index, int offset, int value);

    // --- Raw Databases (the C# editor's Raw ... windows) ---------------------------------------------------------
    // [{id, title, repair}]: the tables, as the C# editor's menu has them.
    Q_INVOKABLE QVariantList rawTables() const;
    // Fills `model` with table `id` (a row per record / list entry, the C# editor's columns).
    Q_INVOKABLE bool fillRawTable(const QString& id, RawTableModel* model) const;
    // Writes `model`'s changed cells back into the save (the C# editor's Save Changes); refills it. "" or errors.
    Q_INVOKABLE QString applyRawTable(RawTableModel* model);
    Q_INVOKABLE int repairRawIds(RawTableModel* model) const;

signals:
    void loadedChanged();
    void unsavedChangesChanged();
    void revisionChanged();
    void statusChanged();

private:
    struct Original {
        int id = 0;
        QString first;
        QString last;
        bool female = false;
        int roles[3] = {0, 0, 0};
        QHash<QString, int> stats;      // By the record's field key ("technicalAbility.strength").
        int rating = 0;
    };
    struct Custom {
        int id = 0;
        QString name;
        QString value;
    };

    QByteArray m_data;
    QString m_path;
    int m_variant = 0;          // Always 0 (the field tables keep the Academy editor's two columns).
    bool m_checksumOk = false;
    bool m_loaded = false;
    bool m_unsaved = false;
    bool m_backedUp = false;
    int m_revision = 0;
    QString m_statusText;
    bool m_statusOk = true;
    QList<Original> m_originals;
    QHash<int, int> m_originalIndex;
    QList<Custom> m_stadiums;   // LevelData.SAV: custom stadiums (id, name, logo id in value).
    QList<Custom> m_decals;     // DecalData.SAV: custom logos.
    QString m_careerInfo;       // Metadata.SAV: "CLUB: Raiders - 2017 NATIONAL RUGBY LEAGUE SEASON, ROUND 13 ..."
    int m_careerYear = 2017;
    struct League {
        QString name;
        QString shortName;
        QList<int> teams;
    };
    QList<League> m_leagues;
    mutable QHash<QString, QString> m_logoUrls;

    void setStatus(bool ok, const QString& text);
    void edited();
    void loadOriginals();
    void loadCustomFiles();
    void loadCareer();
    QString teamNameById(int id) const;
    int teamIndexById(int id) const;
    QVariantList careerFields(int page) const;
    bool setCareerField(int page, const QString& key, const QVariant& value);
    QVariantList contractFields(int index) const;
    bool setContractField(int index, const QString& key, const QVariant& value);
    int statusIndexOf(int playerId) const;
    bool setPlayerStatus(int playerId, const QString& key, int value);
    QVariantList teamChoices(const QList<int>& leagues, bool none) const;
    struct Match {
        int round = 0, home = 0, away = 0, stadium = 0, homeScore = 0, awayScore = 0;
        bool played = false;
        int year = 0, month = 0, day = 0, hour = 0, minute = 0;
        qsizetype at = 0;       // Where it is in the save.
    };
    QList<Match> readStage(int stage) const;
    QString competitionLogoUrl(const QString& name) const;   // ui/logos_competition/large|small/<name>.dds

    qsizetype teamTable() const;
    qsizetype playerTable() const;
    int teamSize() const;
    int playerSize() const;
    int maxTeams() const;
    int maxPlayers() const;
    qsizetype recordAt(const QString& kind, int index) const;

    // Reading / writing numbers in the data.
    qint64 readNum(qsizetype at, int size, bool big, bool isSigned) const;
    void writeNum(qsizetype at, int size, bool big, qint64 value);
    QString readText(qsizetype at, int size) const;
    void writeText(qsizetype at, int size, const QString& text);

    int playerIndexById(int id) const;      // A custom player with this id (or replacing this original id).
    QVariantMap playerInfo(int id) const;
    QVariantMap lineupRow(int id, bool present) const;
    int ratingOfId(int id) const;
    QList<int> rosterIds(int team) const;

    QVariantList recordFields(const QString& kind, int index, int wantPage) const;
    QVariantList choicesFor(const QString& kind, qsizetype at, const QString& list, const QVariant& value) const;
    QVariant virtualValue(const QString& kind, qsizetype at, const QString& key) const;
    bool setVirtual(const QString& kind, qsizetype at, const QString& key, const QVariant& value);
    qsizetype jerseyAt(int team, int uniform) const;
    static QString rawKind(const QString& id);
    int rawCount(const QString& kind) const;
    qsizetype rawListAt(const QString& kind, int index) const;
    bool rawListCell(const QString& key, qsizetype* at, int* size, int* max) const;
    QString rawGet(const QString& kind, int index, const QString& key) const;
    bool rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error);
};

#endif // RLL4_CAREER_EDITOR_MANAGER_H
