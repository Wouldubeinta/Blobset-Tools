#ifndef RL26_CAREER_EDITOR_MANAGER_H
#define RL26_CAREER_EDITOR_MANAGER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include "raw_table_model.h"

#include <QByteArray>
#include <QDateTime>
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
// CareerEditor.qml (SaveEditor.qml in career mode) with Rugby League 26 loaded: the career save,
// Saved Games\Rugby League 26\Career000\CareerData.SAV.
//
// The file is Rl26Save (zlib + XOR + checksum, version 0x4F) around a paSerial object (magic 0x1234EDBC): a type
// table (member name hashes, offsets, kinds, counts) and the root object's bytes, 38 MB of fixed size arrays. Every
// record is found through the type table by member name (FNV-1a 32 hashes; the names are in rl26_career_names.inc,
// recovered from rugbyleague1.exe, the game's Lua scripts and its database), so nothing depends on fixed offsets:
//
//   root   matches (2284 fixture slots: round, home / away club ids, ground, date, result with both teams' players'
//          match stats), m_date, m_news, m_judiciary, m_recruiting (transfers, rookies), m_retirement,
//          m_competitions (23 slots, each one of the competition layouts: the competition, its stages' match lists),
//          baseSchema (m_settings, m_clubs (338 x paClub + the career's club state), m_players (player status list,
//          3080+ players: career state + paPlayer), m_npcs, m_user (the manager / player career)).
//
// The database fields kept as raw bytes (player identity / attributes / skills, club details) carry hashes of names
// nothing gives away; they're named by matching their values with the game's database (see the .inc).
// The API is the RLL4 / RLL3 Career Editors' (Rll4CareerEditorManager), so SaveEditor.qml shows it the same way.
class Rl26CareerEditorManager : public QObject {
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
    Q_PROPERTY(bool demo READ demo CONSTANT)
    Q_PROPERTY(QString gameName READ gameName CONSTANT)
    Q_PROPERTY(bool canAddRecords READ canAddRecords CONSTANT)
    Q_PROPERTY(bool hasLineups READ hasLineups CONSTANT)
    Q_PROPERTY(bool career READ career CONSTANT)
    Q_PROPERTY(QString careerInfo READ careerInfo NOTIFY loadedChanged)

public:
    explicit Rl26CareerEditorManager(QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    QString filePath() const { return m_path; }
    QString fileName() const;
    bool checksumOk() const { return m_checksumOk; }
    int playerCount() const { return int(m_players.size()); }
    int teamCount() const { return int(m_clubs.size()); }
    bool unsavedChanges() const { return m_unsaved; }
    int revision() const { return m_revision; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    bool demo() const { return false; }
    QString gameName() const { return QStringLiteral("Rugby League 26"); }
    bool canAddRecords() const { return false; }
    bool hasLineups() const { return true; }
    bool career() const { return true; }
    QString careerInfo() const;

    Q_INVOKABLE QString defaultFolder() const;
    Q_INVOKABLE QString defaultFile() const;
    Q_INVOKABLE bool load(const QString& pathOrUrl = QString());
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& pathOrUrl);
    // For tests: the decoded data (no file).
    bool loadDecoded(const QByteArray& decoded);

    // {index, id, originalId, first, last, shirt, age, female, role, rating}
    Q_INVOKABLE QVariantList playerRows(const QString& filter) const;
    // {index, id, originalId, fullName, shortName, abbreviation, primaryColour, secondaryColour, female, logo, logoUrl}
    Q_INVOKABLE QVariantList teamRows(const QString& filter) const;
    Q_INVOKABLE QString teamLogo(int team) const;
    Q_INVOKABLE QString teamLogoUrl(int team) const;
    // The logo list: games/Rugby League 26/pc/save_editor/logos.txt (the Save Editor's; emblem value = line position).
    Q_INVOKABLE QString logosFile() const;
    Q_INVOKABLE QStringList logoNames() const;

    // [{key, label, group, kind, value, min, max, offset, readOnly, slider, choices}]
    Q_INVOKABLE QVariantList fields(const QString& kind, int index) const;
    Q_INVOKABLE bool setField(const QString& kind, int index, const QString& key, const QVariant& value);
    Q_INVOKABLE QStringList groups(const QString& kind) const;
    Q_INVOKABLE QStringList pageGroups(const QString& kind, const QString& page) const;

    Q_INVOKABLE QVariantList appearanceFields(int player) const;
    Q_INVOKABLE bool setAppearanceField(int player, const QString& key, const QVariant& value);
    Q_INVOKABLE int playerRating(int player) const;
    Q_INVOKABLE int teamRating(int team) const;

    // --- Roster (50 player ids) -------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantList rosterRows(int team) const;
    Q_INVOKABLE int rosterMax() const { return 50; }
    Q_INVOKABLE int rosterMin() const { return 17; }
    Q_INVOKABLE bool addRosterPlayer(int team, int playerId);
    Q_INVOKABLE bool removeRosterPlayer(int team, int slot);
    Q_INVOKABLE bool moveRosterPlayer(int team, int slot, int delta);
    Q_INVOKABLE bool sortRoster(int team);
    Q_INVOKABLE QVariantList pickablePlayers(const QString& filter) const;
    Q_INVOKABLE bool databasePlayersAvailable() const { return true; }

    // --- Lineups (the club's 8: 19 players, 5 roles) ------------------------------------------------------------------
    Q_INVOKABLE QString generateBestLineup(int team);
    Q_INVOKABLE QVariantList matchLists(int team = -1) const;
    Q_INVOKABLE bool listPresent(int team, const QString& list) const;
    Q_INVOKABLE bool setListPresent(int team, const QString& list, bool on);   // Off: removes the lineup (not the last).
    Q_INVOKABLE QString lineupName(int team, const QString& list) const;
    Q_INVOKABLE bool setLineupName(int team, const QString& list, const QString& name);
    Q_INVOKABLE int addLineup(int team);    // A copy of the first (as the game's New Lineup does). Its index or -1.
    Q_INVOKABLE QVariantList lineupRows(int team, const QString& list) const;
    Q_INVOKABLE QVariantList teamRoles(int team, const QString& list) const;
    Q_INVOKABLE bool setLineupSlot(int team, const QString& list, const QString& key, int playerId);
    Q_INVOKABLE bool setLineupShirt(int, const QString&, const QString&, int) { return false; }
    Q_INVOKABLE QVariantList pickerRows(const QString& filter, int team, bool squadOnly) const;

    // --- Jerseys (paTeamVisualConfig, up to 20) ------------------------------------------------------------------------
    Q_INVOKABLE QVariantMap jerseys(int team) const;
    Q_INVOKABLE QVariantList jerseyFields(int team, int uniform) const;
    Q_INVOKABLE bool setJerseyField(int team, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setJerseySlot(int, int, int) { return false; }
    Q_INVOKABLE int addJersey(int, int) { return -1; }
    Q_INVOKABLE bool removeJersey(int, int) { return false; }

    // --- Career --------------------------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantList careerItems() const;
    Q_INVOKABLE QVariantList statusRows() const;
    Q_INVOKABLE bool removeStatus(int index);
    Q_INVOKABLE QVariantList contractRows() const;
    Q_INVOKABLE bool removeContract(int index);
    Q_INVOKABLE QVariantList retirementRows() const;
    Q_INVOKABLE QVariantMap retirementDate() const;
    Q_INVOKABLE bool setRetirementReason(int index, int reason);
    Q_INVOKABLE bool removeRetirement(int index);
    Q_INVOKABLE QVariantList ladderComps() const;           // [{index, name, logoUrl}]
    Q_INVOKABLE QVariantList ladderRows(int comp) const;
    Q_INVOKABLE QVariantList fixtureStages() const;         // [{index, name, logoUrl}]
    Q_INVOKABLE QStringList fixtureRounds(int stage) const;
    Q_INVOKABLE QVariantList fixtureRows(int stage, int round) const;
    Q_INVOKABLE QVariantMap matchStats(int stage, int match) const;

    // --- Records -------------------------------------------------------------------------------------------------------
    Q_INVOKABLE int addRecord(const QString&, bool) { return -1; }
    Q_INVOKABLE bool deleteRecord(const QString&, int) { return false; }
    Q_INVOKABLE QString importRecord(const QString&, int, const QString&) { return QStringLiteral("Not available for Rugby League 26 careers."); }
    Q_INVOKABLE QString exportRecord(const QString&, int, const QString&) const { return QStringLiteral("Not available for Rugby League 26 careers."); }
    Q_INVOKABLE QString recordFileName(const QString&, int) const { return {}; }
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
    // --- The paSerial schema --------------------------------------------------------------------------------------
    struct Member {
        quint32 hash = 0;
        QString name;
        quint32 off = 0;
        quint16 kind = 0;
        quint16 count = 1;
        int type = -1;           // A nested object, else built in.
        int size = 1;            // Bytes per element.
    };
    struct Type {
        quint32 size = 0;
        QList<Member> members;
        QHash<quint32, int> byHash;
    };
    // A place in the data: absolute offset and its type (-1: a built in value of `size` bytes).
    struct Ref {
        qsizetype at = -1;
        int type = -1;
        int size = 0;
        int count = 1;
        quint16 kind = 0;
        bool ok() const { return at >= 0; }
    };
public:
    // A field of a record type (resolved once), for the field lists.
    struct FieldDef {
        QString key;
        QString label;
        QString group;
        QString path;
        QString kind;            // "number", "float", "bool", "text", "colour", "choice", "hash", "date"
        QString list;            // Choice list name.
        int min = 0;
        int max = 0;
        bool slider = false;
        bool readOnly = false;
        qsizetype rel = -1;      // Offset in the record (colour: of r).
        int size = 0;
        quint16 elemKind = 0;
    };

private:
    struct Stage {
        QString name;            // "NRL - Season"
        QString comp;            // Competition key ("NRL")
        int compSlot = -1;
        QString stageName;       // "Season"
        QList<int> matches;      // Global match indices.
        qsizetype config = -1;   // The stage's points config (T38: win, bye, draw, loss, finals qualifiers...).
        bool finals = false;
    };

    QByteArray m_head;           // Rl26Save header bits.
    quint32 m_version = 0;
    quint32 m_sizeBias = 0;
    QByteArray m_data;           // Decoded.
    qsizetype m_rootAt = 0;
    QList<Type> m_types;
    int m_root = -1;
    QString m_path;
    bool m_checksumOk = false;
    bool m_loaded = false;
    bool m_unsaved = false;
    int m_revision = 0;
    QString m_statusText;
    bool m_statusOk = true;

    // Records.
    Ref m_matchArray;            // root.matches
    int m_matchCount = 0;
    int m_matchSize = 0;
    Ref m_clubArray;
    int m_clubSize = 0;
    Ref m_playerArray;
    int m_playerSize = 0;
    QList<qsizetype> m_clubs;    // Club records (used ones).
    QList<qsizetype> m_players;
    QHash<int, int> m_clubById;
    QHash<int, int> m_playerById;
    QList<FieldDef> m_playerDefs;
    QList<FieldDef> m_teamDefs;
    QList<FieldDef> m_careerDefs;   // Settings / Selection.
    QList<Stage> m_stages;
    int m_careerYear = 2026;
    mutable QHash<QString, QString> m_logoUrls;
    mutable QStringList m_logos;                    // logos.txt.
    mutable QDateTime m_logosStamp;
    mutable bool m_logosLoaded = false;
    struct Commentary {
        bool loaded = false;
        QList<quint32> hashes;
        QStringList names;
    };
    mutable QHash<QString, Commentary> m_commentary;   // "surnames", "location", "mascot".
    const Commentary& commentary(const QString& list) const;

    void setStatus(bool ok, const QString& text);
    void edited();
    bool parse();
    void index();
    void buildDefs();
    void buildStages();

    // Schema navigation.
    const Member* member(int type, quint32 hash) const;
    const Member* member(int type, const QString& name) const;
    Ref child(const Ref& parent, const QString& token) const;   // "name", "@<hex hash>", "name#<n>"
    Ref resolve(const Ref& from, const QString& path) const;    // "a.b.c"
    Ref rootRef() const { return {m_rootAt, m_root, 0, 1, 0}; }
    static quint32 fnv(const QByteArray& text);

    qint64 readInt(qsizetype at, int size, bool isSigned = false) const;
    void writeInt(qsizetype at, int size, qint64 value);
    float readFloat(qsizetype at) const;
    QString readText(qsizetype at, int size) const;
    void writeText(qsizetype at, int size, const QString& text);

    // Records and names.
    qsizetype playerAt(int index) const { return index >= 0 && index < m_players.size() ? m_players.at(index) : -1; }
    qsizetype clubAt(int index) const { return index >= 0 && index < m_clubs.size() ? m_clubs.at(index) : -1; }
    QVariant fieldValue(qsizetype record, const FieldDef& d) const;
    const FieldDef* playerDef(const QString& key) const;
    const FieldDef* teamDef(const QString& key) const;
    int playerId(int index) const;
    int clubId(int index) const;
    QString playerName(int id) const;
    QString clubName(int id) const;
    QVariantMap playerInfo(int id) const;
    QList<int> rosterIds(int team) const;
    Ref lineupRef(int team, int list) const;
    int lineupCount(int team) const;
    quint32 decalOf(int team) const;   // The club's custom logo (m_decalId), 0 = none.
    QVariantList choices(const QString& list, const QVariant& value) const;
    QStringList listNames(const QString& list) const;
    QString stadiumName(int ground, int homeClubId) const;
    QString competitionLogoUrl(const QString& comp) const;
    QVariantList objectFields(qsizetype at, int type, const QString& prefix, const QString& group) const;
    bool setObjectField(qsizetype at, int type, const QString& key, const QVariant& value);
    QVariantList careerFields(int page) const;
    QVariantMap readMatch(int match) const;
    QList<RawColumn> rawColumnsFor(const QString& id) const;
    QString rawGet(const QString& kind, int index, const QString& key) const;
    bool rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error);
    bool m_rawBatch = false;
    bool m_rawEdited = false;
};

#endif // RL26_CAREER_EDITOR_MANAGER_H
