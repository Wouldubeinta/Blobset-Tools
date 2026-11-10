#ifndef COMMENTARY_EDITOR_MANAGER_H
#define COMMENTARY_EDITOR_MANAGER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// ==========================================
// QML Bridge Manager
// ==========================================
// CommentaryEditor.qml: Rugby League 26's commentary and referee dialogue.
//
//   sound\lines.commentary.cmtry     (0x99B3B6554A5750D3)  the commentators' lines
//   sound\lines.referee.dat          (0x01D4046906CCE436)  the referees' lines (same layout)
//   sound\categoryinfo.commentary.cmtry  (0xEAB8A37C7D0EBC40)  per category settings
//   sound\categoryinfo.referee.cmtry     (0x33B0057FD1FBD803)
//
// All little endian. A name table is: u32 count, count FNV-1a 32 hashes of the names, count u32 offsets of the
// names (from the count), the names (zero terminated).
//
// Lines file: u32 8 + name length, u32 name length, name ("Commentary" / "RefereeDialogue"), u32 speakers,
// speakers x u32 file offset, speakers x u32 speaker id; padded to 16. Per speaker (16 aligned):
//   u32 offset of its category table, u32 offset of the bank name, u32 + folder ("commentary/id1_andrewvoss/"),
//   u32 + bank ("Extern_Commentator_AndrewVoss"), the category table: u32 end of the table, then a name table;
//   per category { u32 offset of its first line, u32 lines, u32 offset of its sub-category name table or 0 };
//   padded to 16; the lines; the sub-category tables (not padded).
// A line (16 aligned): 8 u64 condition bit masks (the 5th, at +32, is the category's sub-categories the line is
//   for: bit 0 = the first), the sound's id (10 digits, zero terminated), u8 parts, u8 pronoun (0 they / them,
//   1 he / him, 2 she / her: the game's ELinePronoun), u8 0, u8 phrase (1 on whole sentences, 0 on the names / teams spliced into them), u8 0, then the
//   parts (zero terminated: a sound name, or "c:<category>" to play a line of that category) and the subtitle
//   text (zero terminated), padded to 16. Every offset is from the start of the file, so the file is rebuilt.
//
// Category info file: u32 count, u32 offset of the records, then a name table from offset 8 (offsets from 8);
//   the records (28 bytes, in name order): 6 floats, u8 type (0 sequence, 1 event, 2 reply, 3 sub-category),
//   u8 flag, 2 bytes.
class CommentaryEditorManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY loadedChanged)               // "lines", "categories" or "".
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY loadedChanged)
    Q_PROPERTY(QString workspacePath READ workspacePath NOTIFY loadedChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loadedChanged)
    Q_PROPERTY(QString title READ title NOTIFY loadedChanged)             // "Commentary" / "RefereeDialogue".
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)

public:
    explicit CommentaryEditorManager(QObject* parent = nullptr);

    // The four files' blobset hashes (lowercase hex), for routing a double click here.
    static bool isCommentaryHash(const QString& hexHash);
    // The names the game loads a lines file's sounds by: "sound/" + speaker folder + sound name, e.g.
    // "sound/commentary/id1_andrewvoss/id1_try_27" (the blobset hash is FNV-1a 64 of that whole string: no type).
    // Resolve File Names uses them to name the commentary .wem files. Empty when the data isn't a lines file.
    static QStringList soundNames(const QByteArray& linesFile);
    // Each speaker's name ("Andrew Voss") and category names, for the Dialogue Editor's "who can say it".
    struct SpeakerCategories {
        QString name;
        QSet<QString> categories;
    };
    static QList<SpeakerCategories> speakerCategories(const QByteArray& linesFile);

    bool loaded() const { return !m_kind.isEmpty(); }
    QString kind() const { return m_kind; }
    QString sourceName() const { return m_sourceName; }
    QString workspacePath() const { return m_workspacePath; }
    QString fileName() const;
    QString title() const { return QString::fromUtf8(m_title); }
    bool modified() const { return m_modified; }
    int revision() const { return m_revision; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // [{name, path, hash, available}] the four files, and whether the loaded blobset has each.
    Q_INVOKABLE QVariantList blobsetFiles() const;
    Q_INVOKABLE bool loadFromBlobset(const QString& hexHash, const QString& workspacePath);
    Q_INVOKABLE bool loadFile(const QString& pathOrUrl);
    Q_INVOKABLE QString saveToMods();
    Q_INVOKABLE bool saveAs(const QString& pathOrUrl);

    // --- Lines ----------------------------------------------------------------------------------------------
    // [{index, id, name, folder, bank, categories, lines}]
    Q_INVOKABLE QVariantList speakers() const;
    // [{index, name, lines, subcategories, match}] (filter: the name, or any of its lines' texts / parts).
    Q_INVOKABLE QVariantList categories(int speaker, const QString& filter) const;
    // [{index, soundId, text, parts, gender, subNames, flag, modified, added}]
    Q_INVOKABLE QVariantList lines(int speaker, int category) const;
    // {id, text, originalText, parts: [], gender, flag, subcategories: [names], subFlags: [bool],
    //  conditions: ["0x0000000000000000" x 8]}
    Q_INVOKABLE QVariantMap line(int speaker, int category, int index) const;
    Q_INVOKABLE QStringList subcategories(int speaker, int category) const;

    Q_INVOKABLE bool setLineText(int speaker, int category, int index, const QString& text);
    Q_INVOKABLE bool setLineParts(int speaker, int category, int index, const QStringList& parts);
    Q_INVOKABLE bool setLineId(int speaker, int category, int index, const QString& id);
    Q_INVOKABLE bool setLineGender(int speaker, int category, int index, int gender);
    Q_INVOKABLE bool setLineSubcategory(int speaker, int category, int index, int subcategory, bool on);
    Q_INVOKABLE bool setLineFlag(int speaker, int category, int index, bool on);
    Q_INVOKABLE bool setLineCondition(int speaker, int category, int index, int field, const QString& value);   // 0-7
    Q_INVOKABLE bool revertLineText(int speaker, int category, int index);
    // A copy of a line (or a blank one when from < 0) at the end of the category. Returns its index or -1.
    Q_INVOKABLE int addLine(int speaker, int category, int from);
    Q_INVOKABLE bool removeLine(int speaker, int category, int index);
    Q_INVOKABLE bool moveLine(int speaker, int category, int index, int delta);

    Q_INVOKABLE int addCategory(int speaker, const QString& name);     // Index or -1 (the status says why).
    Q_INVOKABLE bool renameCategory(int speaker, int category, const QString& name);
    Q_INVOKABLE bool removeCategory(int speaker, int category);
    Q_INVOKABLE bool setSubcategories(int speaker, int category, const QStringList& names);

    // The next / previous line whose text, parts or id contain the query, from (and not including) a line:
    // {speaker, category, line} or {} when none.
    Q_INVOKABLE QVariantMap find(const QString& query, int speaker, int category, int index, bool backward) const;
    Q_INVOKABLE int countMatches(const QString& query) const;

    // Speaker, Category, Line, Id, Sub-category, Parts, Text (UTF-8 with BOM).
    Q_INVOKABLE bool exportCsv(const QString& pathOrUrl);
    // Updates the texts (and parts) of the lines it names (by speaker folder, category and line number).
    Q_INVOKABLE bool importCsv(const QString& pathOrUrl);

    // --- New line from WAV (MainForm: WAVs dropped into a sound\\commentary\\<speaker> folder) ----------------
    // The game loads a line's sound by "sound/" + the speaker's folder + the part's name and hashes that as it is
    // (FNV-1a 64, no type), so a new sound only needs a WEM in the mods folder under that name and a line that
    // plays it: no Wwise bank changes.
    // The speaker whose folder that is ("sound/commentary/id1_andrewvoss" or "commentary/id1_andrewvoss/"), or -1.
    Q_INVOKABLE int speakerForFolder(const QString& folder) const;
    // A sound name for the WAV: its file name in lower case (a-z, 0-9, _) with the speaker's prefix ("id1_"),
    // made unique.
    Q_INVOKABLE QString suggestSoundName(int speaker, const QString& wavPathOrUrl) const;
    // "" when the name can be used, else why not (characters, a line of the speaker plays it, the game has it).
    Q_INVOKABLE QString checkSoundName(int speaker, const QString& name) const;
    // {path: "sound\\commentary\\id1_andrewvoss\\name.wem", sound: "sound/commentary/id1_andrewvoss/name",
    //  pathHash: "0x..."}
    Q_INVOKABLE QVariantMap newSoundInfo(int speaker, const QString& name) const;
    // The WEM's "hash" chunk a WAV gets by default (32 hex digits: MD5 of the WAV), "" when it can't be read.
    Q_INVOKABLE QString defaultWemHash(const QString& wavPathOrUrl) const;
    // Makes the WEM (PCM, the speaker's sounds' extra chunks, its hash chunk = wemHash, 32 hex digits or "" for
    // the default) in the mods folder, adds a line playing it to the end of the category (its conditions copied
    // from the category's first line, a new sound id, a whole sentence), and saves the lines file to the mods
    // folder. Returns the new line's index or -1 (the status says why).
    // Whether a category holds spliced names (surname_..., team_...: the line isn't a whole sentence).
    Q_INVOKABLE bool isNameCategory(const QString& category) const;
    // phrase: a whole sentence (the game's flag 1); off for the names / teams spliced into sentences (surname_n_,
    // surname_e_, team_... categories: the game's own name lines all have it off).
    Q_INVOKABLE int addLineFromWav(int speaker, int category, const QString& wavPathOrUrl, const QString& name,
                                   const QString& text, int gender, bool phrase, const QString& wemHash);

    // --- Category info ----------------------------------------------------------------------------------------
    // [{index, name, hash, values: [6 floats], type, flag, extra: [2 bytes]}]
    Q_INVOKABLE QVariantList infoRows(const QString& filter) const;
    Q_INVOKABLE bool setInfoValue(int index, int field, double value);   // field 0-5.
    Q_INVOKABLE bool setInfoByte(int index, int field, int value);       // field 0 type, 1 flag, 2-3 the rest.
    Q_INVOKABLE int addInfoCategory(const QString& name, int copyFrom);  // Index or -1.
    Q_INVOKABLE bool removeInfoCategory(int index);

signals:
    void loadedChanged();
    void modifiedChanged();
    void revisionChanged();
    void statusChanged();

private:
    struct Line {
        QByteArray conditions;   // 64 bytes (8 u64 masks; [4] = the sub-categories).
        QByteArray idBlock;      // 16 bytes (id, parts count, gender, phrase flag).
        QList<QByteArray> parts;
        QByteArray text;
        QByteArray originalText;
        bool added = false;
    };
    struct Category {
        QByteArray name;
        QList<QByteArray> subs;
        QList<Line> lines;
    };
    struct Speaker {
        quint32 id = 0;
        QByteArray folder;
        QByteArray bank;
        QList<Category> cats;
    };
    struct InfoCategory {
        QByteArray name;
        QByteArray record;       // 28 bytes.
    };

    QString m_kind;
    QString m_sourceName;
    QString m_workspacePath;
    QString m_filePath;
    QByteArray m_title;
    QList<Speaker> m_speakers;
    QList<InfoCategory> m_info;
    bool m_packed = false;     // Loaded as ZSTD chunks (how the game stores them): saved the same way.
    bool m_modified = false;
    int m_revision = 0;
    QString m_statusText;
    bool m_statusOk = true;

    void setStatus(bool ok, const QString& text);
    void edited();
    bool parse(const QByteArray& data, const QString& sourceName, const QString& workspacePath, const QString& filePath);
    bool parseLines(const QByteArray& data);
    bool parseInfo(const QByteArray& data);
    QByteArray serialize() const;
    QByteArray serializeLines() const;
    QByteArray serializeInfo() const;
    bool writeFile(const QString& path, const QByteArray& data);

    Category* categoryAt(int speaker, int category);
    const Category* categoryAt(int speaker, int category) const;
    Line* lineAt(int speaker, int category, int index);
    const Line* lineAt(int speaker, int category, int index) const;
    static QString lineId(const Line& line);
    static quint64 condition(const Line& line, int field);
    static void setCondition(Line& line, int field, quint64 value);
    static bool lineMatches(const Line& line, const QString& query);
};

#endif // COMMENTARY_EDITOR_MANAGER_H
