#ifndef SAVE_EDITOR_MANAGER_H
#define SAVE_EDITOR_MANAGER_H

#include "raw_table_model.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PaSerial { class Blob; }

// ==========================================
// QML Bridge Manager
// ==========================================
// SaveEditor.qml: Rugby League 26's Academy save (Saved Games\Rugby League 26\Profile\AcademyData000.SAV), the
// custom players and teams made in the game's Academy.
//
// The file is decoded with Rl26Save (zlib + XOR + checksum, see rl26_save.h). The decoded data is fixed size: the
// custom players and teams are arrays of fixed size records at fixed places. A record is the game's in-memory
// layout: strings are fixed size buffers (the game leaves old text after the terminating zero; edits clear it).
//
// The fields are the ones mapped so far (kPlayerFields / kTeamFields in the .cpp: key, offset, type, size). Every
// other byte is kept as it is, and the Raw tab shows (and edits) the record's bytes, to map more.
class SaveEditorManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY loadedChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loadedChanged)
    Q_PROPERTY(bool checksumOk READ checksumOk NOTIFY loadedChanged)
    Q_PROPERTY(int playerCount READ playerCount NOTIFY loadedChanged)
    Q_PROPERTY(int teamCount READ teamCount NOTIFY loadedChanged)
    Q_PROPERTY(bool unsavedChanges READ unsavedChanges NOTIFY unsavedChangesChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)

public:
    explicit SaveEditorManager(QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    QString filePath() const { return m_path; }
    QString fileName() const;
    bool checksumOk() const { return m_checksumOk; }
    int playerCount() const { return m_players.size(); }
    int teamCount() const { return m_teams.size(); }
    bool unsavedChanges() const { return m_unsaved; }
    int revision() const { return m_revision; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // The game's save folder (Saved Games\Rugby League 26\Profile) and Academy save in it.
    Q_INVOKABLE QString defaultFolder() const;
    Q_INVOKABLE QString defaultFile() const;

    // "" = the default file. A path or a file:// URL.
    Q_INVOKABLE bool load(const QString& pathOrUrl = QString());
    // Writes the file back (the first save of a file keeps the original as <name>.bak).
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& pathOrUrl);

    // {index, id, first, last, shirt, age, female, role, rating}
    Q_INVOKABLE QVariantList playerRows(const QString& filter) const;
    // {index, id, fullName, shortName, abbreviation, primaryColour, secondaryColour, female, logo, logoUrl}
    Q_INVOKABLE QVariantList teamRows(const QString& filter) const;
    // The team's emblem: its club icon name ("cus_89") and image URL ("" when not a club icon / not in the blobset).
    Q_INVOKABLE QString teamLogo(int team) const;
    Q_INVOKABLE QString teamLogoUrl(int team) const;
    // The logo list: games/Rugby League 26/pc/save_editor/logos.txt (emblem value = line position, from 0).
    Q_INVOKABLE QString logosFile() const;
    Q_INVOKABLE QStringList logoNames() const;

    // A record's fields: [{key, label, group, kind ("text", "number", "big", "bool", "colour", "choice"), value, min,
    // max, offset, readOnly, choices: [{name, value}] (drop downs)}]. kind "player" / "team".
    Q_INVOKABLE QVariantList fields(const QString& kind, int index) const;
    Q_INVOKABLE bool setField(const QString& kind, int index, const QString& key, const QVariant& value);
    // The groups the fields are in, in order.
    Q_INVOKABLE QStringList groups(const QString& kind) const;

    // A player's appearance (the paCharacterVisualConfig at +0xA7): PaSerial::Blob::fields() items
    // {key, label, group, kind, value, max}, like the Database Editor's Appearance tab.
    Q_INVOKABLE QVariantList appearanceFields(int player) const;
    Q_INVOKABLE bool setAppearanceField(int player, const QString& key, const QVariant& value);

    // --- Roster (team record + 0x2668: u16 count, u32 player ids[50]) ---------------------------------------
    // The team's players: [{slot, id, name, role, rating, age, club, custom}]. Ids below 20000 are the game's
    // database players (names from the blobset's defaultdata_players), 20000+ the save's own (custom) players.
    Q_INVOKABLE QVariantList rosterRows(int team) const;
    Q_INVOKABLE int rosterMax() const;      // The game's MAX_ROSTER_SIZE (50).
    Q_INVOKABLE int rosterMin() const;      // MIN_ROSTER_SIZE (19): fewer and the game won't play the team.
    Q_INVOKABLE bool addRosterPlayer(int team, int playerId);
    Q_INVOKABLE bool removeRosterPlayer(int team, int slot);
    Q_INVOKABLE bool moveRosterPlayer(int team, int slot, int delta);
    // Players to add: [{id, name, role, rating, age, club, custom}], the save's players first, then the game's
    // (needs Rugby League 26's blobset loaded). filter: name / id / club.
    Q_INVOKABLE QVariantList pickablePlayers(const QString& filter) const;
    Q_INVOKABLE bool databasePlayersAvailable() const;

    // --- Lineups (team record + 0x2732: u16 count, 8 x 139 byte lineups), like the Database Editor's page ---------
    // [{list ("L0".."L7"), title, shirts}]
    Q_INVOKABLE QVariantList matchLists(int team = -1) const;
    Q_INVOKABLE bool listPresent(int team, const QString& list) const;
    Q_INVOKABLE bool setListPresent(int team, const QString& list, bool on);   // Off: deletes the lineup.
    // {key, position, id, first, last, roles, rating, present, known, shirt, playerIndex}
    Q_INVOKABLE QVariantList lineupRows(int team, const QString& list) const;
    Q_INVOKABLE QVariantList teamRoles(int team, const QString& list) const;
    Q_INVOKABLE bool setLineupSlot(int team, const QString& list, const QString& key, int playerId);
    Q_INVOKABLE bool setLineupShirt(int team, const QString& list, const QString& key, int shirt);
    Q_INVOKABLE QString lineupName(int team, const QString& list) const;
    Q_INVOKABLE bool setLineupName(int team, const QString& list, const QString& name);
    Q_INVOKABLE int addLineup(int team);     // A copy of the first (as the game's New Lineup does). Its index or -1.
    // The Database Editor's player picker: {id, first, last, clubName, roleName, age, rating}.
    Q_INVOKABLE QVariantList pickerRows(const QString& filter, int team, bool squadOnly) const;

    // --- Jerseys (ConvantClub at team record + 0x176, like the Database Editor's Jerseys tab) -------------------
    // {ok, error, count, max, uniforms: [{index, name, hidden}], slots: [{slot, label, uniform}]}
    Q_INVOKABLE QVariantMap jerseys(int team) const;
    Q_INVOKABLE QVariantList jerseyFields(int team, int uniform) const;
    Q_INVOKABLE bool setJerseyField(int team, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setJerseySlot(int team, int slot, int uniform);
    Q_INVOKABLE int addJersey(int team, int from);       // A copy of `from` at the end. Its index or -1.
    Q_INVOKABLE bool removeJersey(int team, int uniform);

    // OVR as the game shows it (Rl26Lists::ratingTerms() for the primary role).
    Q_INVOKABLE int playerRating(int player) const;

    // The record's bytes as hex rows: [{offset, hex: [..16 "xx"], text, known: [..16 bool]}].
    Q_INVOKABLE QVariantList rawRows(const QString& kind, int index) const;
    Q_INVOKABLE int recordSize(const QString& kind) const;
    Q_INVOKABLE QString recordOffset(const QString& kind, int index) const;   // In the decoded data (hex).
    Q_INVOKABLE bool setRawByte(const QString& kind, int index, int offset, int value);

    // --- Raw Databases (the C# editor's Raw ... windows) ---------------------------------------------------------
    // [{id, title, repair}]: the tables, as the C# editor's menu has them.
    Q_INVOKABLE QVariantList rawTables() const;
    // Fills `model` with table `id` (a row per record / list entry, the C# editor's columns).
    Q_INVOKABLE bool fillRawTable(const QString& id, RawTableModel* model) const;
    // Writes `model`'s changed cells back into the save (the C# editor's Save Changes); refills it. "" or errors.
    Q_INVOKABLE QString applyRawTable(RawTableModel* model);
    Q_INVOKABLE int repairRawIds(RawTableModel* model) const;
    // The record's bytes / the whole decoded data to a file (to compare in a hex editor).
    Q_INVOKABLE QString exportRecord(const QString& kind, int index, const QString& pathOrUrl) const;
    Q_INVOKABLE QString exportDecoded(const QString& pathOrUrl) const;

signals:
    void loadedChanged();
    void unsavedChangesChanged();
    void revisionChanged();
    void statusChanged();

private:
    struct Commentary {
        QList<quint32> hashes;
        QStringList names;
        bool loaded = false;
    };
    mutable QHash<QString, Commentary> m_commentary;
    mutable QHash<QString, QString> m_logoUrls;         // Logo name -> image URL.
    mutable QStringList m_logos;                        // logos.txt.
    mutable QDateTime m_logosStamp;
    mutable bool m_logosLoaded = false;

    struct DbPlayer {
        int id = 0;
        QString name;
        QString first;
        QString last;
        QString roles;      // "FB / WG / C"
        QString club;
        int role = -1;
        int rating = 0;
        int age = 0;
    };
    mutable QList<DbPlayer> m_dbPlayers;                // The game's database players (by id order of the file).
    mutable QHash<int, int> m_dbPlayerIndex;            // id -> index in m_dbPlayers.
    mutable bool m_dbLoaded = false;
    void loadDatabasePlayers() const;
    QVariantMap playerInfo(int id) const;
    qsizetype lineupAt(int team, const QString& list) const;   // -1 when the team doesn't have it.
    bool kitBlob(int team, PaSerial::Blob& out) const;
    bool writeKit(int team, const QByteArray& object);   // "surnames", "location", "mascot" (from the game's blobset).

    QByteArray m_data;          // Decoded.
    QString m_path;
    quint32 m_version = 0;
    quint32 m_sizeBias = 0;
    bool m_checksumOk = false;
    bool m_loaded = false;
    bool m_unsaved = false;
    bool m_backedUp = false;
    int m_revision = 0;
    QString m_statusText;
    bool m_statusOk = true;
    QList<qsizetype> m_players;     // Record offsets in m_data.
    QList<qsizetype> m_teams;

    void findRecords();
    const Commentary& commentary(const QString& list) const;
    QVariantList choicesFor(const QString& kind, const QString& key, const QVariant& value) const;
    qsizetype recordAt(const QString& kind, int index) const;
    void edited();
    void setStatus(bool ok, const QString& text);
    bool writeTo(const QString& path);
    QString rawGet(const QString& kind, int index, const QString& key) const;
    bool rawSet(const QString& kind, int index, const QString& key, const QString& text, QString* error);
    bool m_rawBatch = false;
    bool m_rawEdited = false;
};

#endif // SAVE_EDITOR_MANAGER_H
