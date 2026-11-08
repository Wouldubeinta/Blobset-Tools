#ifndef DIALOGUE_EDITOR_MANAGER_H
#define DIALOGUE_EDITOR_MANAGER_H

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
#include <QHash>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>

class M3mpToolManager;

// ==========================================
// QML Bridge Manager
// ==========================================
// DialogueEditor.qml: the names the commentary can say, in Rugby League 26's / Rugby 25's
// sound\dialogue.mempack.m3mp (0x83AF3201ED1AE751). The M3MP holds one list per kind:
//
//   sound/dialogue_wildcardhash_surnames       player surnames (commentary: surname_n_<name>, surname_e_<name>)
//   sound/dialogue_wildcardhash_teamslocation  team locations  (team_mid_<name>, team_and_<name>, team_excited_<name>)
//   sound/dialogue_wildcardhash_teamsmascot    team mascots     (the same team_ categories)
//
// A list: u32 count, then per name u32 FNV-1a 32 hash of the name, u32 length, the name (lower case a-z, no
// terminator). The first is "none". The database stores a player's / team's commentary name as the hash, and the
// commentary plays the line of the category named after it. Saving writes the M3MP into the mods folder (like the
// Database Editor's save), ready for Modify.
class DialogueEditorManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY loadedChanged)
    Q_PROPERTY(QString modsTarget READ modsTarget NOTIFY loadedChanged)
    Q_PROPERTY(bool hasCommentary READ hasCommentary NOTIFY loadedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)

public:
    explicit DialogueEditorManager(QObject* parent = nullptr);

    static constexpr const char* kHash = "83af3201ed1ae751";
    static bool isDialogueHash(const QString& hexHash);
    static quint32 nameHash(const QByteArray& name);   // FNV-1a 32.

    bool loaded() const { return !m_lists.isEmpty(); }
    bool busy() const { return m_busy; }
    bool modified() const;
    int revision() const { return m_revision; }
    QString sourceText() const { return m_sourceText; }
    QString modsTarget() const;
    bool hasCommentary() const { return !m_speakers.isEmpty(); }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // The M3MP from the blobset (or your edited copy in the mods folder), and lines.commentary for the coverage.
    Q_INVOKABLE bool load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void revertAll();

    // [{index, key, title, path, count, modified, hint}]
    Q_INVOKABLE QVariantList lists() const;
    // [{index, name, hash, voices, voiceNames, missing, modified, added, renamedFrom}]
    //   show: "all", "with" (some commentator says it), "without" (none does).
    Q_INVOKABLE QVariantList rows(int list, const QString& filter, const QString& show) const;
    // [{name, voices, voiceNames}]: names the commentary has lines for that aren't in the list.
    Q_INVOKABLE QVariantList unlisted(int list) const;
    // The commentators (with any category of this list's kind), for the column of ticks.
    Q_INVOKABLE QStringList voices(int list) const;

    // Names are added (and renamed ones moved) in alphabetical order, "none" first, and saved in that order.
    Q_INVOKABLE int addName(int list, const QString& name);       // Index or -1 (the status says why).
    Q_INVOKABLE int indexOfName(int list, const QString& name) const;   // -1 when it isn't in the list.
    Q_INVOKABLE int addNames(int list, const QStringList& names); // How many were added.
    Q_INVOKABLE bool renameName(int list, int index, const QString& name);
    Q_INVOKABLE bool removeName(int list, int index);
    Q_INVOKABLE bool revertName(int list, int index);
    Q_INVOKABLE QString hashOf(const QString& name) const;        // "0x...", for the add box.
    Q_INVOKABLE QString checkName(int list, const QString& name, int except = -1) const;   // "" when it can be added.

    // One name per line (UTF-8). Import adds the names that aren't in the list yet.
    Q_INVOKABLE bool exportList(int list, const QString& pathOrUrl);
    Q_INVOKABLE bool importList(int list, const QString& pathOrUrl);

    // For tests / other loaders: the lists by M3MP path, and lines.commentary's data (may be empty).
    bool loadFromEntries(const QList<QPair<QString, QByteArray>>& entries, const QByteArray& commentary);
    QByteArray listData(int list) const;

signals:
    void loadedChanged();
    void busyChanged();
    void modifiedChanged();
    void revisionChanged();
    void statusChanged();
    void saved(bool ok, const QString& message);

private:
    struct Name {
        QByteArray name;
        QByteArray original;       // Empty for an added name.
    };
    struct List {
        QString path;
        QString key;               // "surnames", "teamslocation", "teamsmascot", ...
        QList<Name> names;
        QByteArray originalData;
    };
    struct Speaker {
        QString name;
        QSet<QString> categories;
    };

    M3mpToolManager* m_m3mp = nullptr;
    QList<List> m_lists;
    QList<Speaker> m_speakers;
    QString m_sourceText;
    QString m_saveTemp;
    bool m_busy = false;
    bool m_saving = false;
    int m_revision = 0;
    QString m_statusText;
    bool m_statusOk = true;

    void setStatus(bool ok, const QString& text);
    void setBusy(bool busy);
    void edited();
    static QList<Name> parseList(const QByteArray& data, bool* ok);
    int sortedPosition(int list, const QByteArray& name) const;
    QStringList prefixes(int list) const;          // The commentary categories a name of this list plays.
    QStringList voicesFor(int list, const QString& name) const;
    int voiceCount(int list) const;
};

#endif // DIALOGUE_EDITOR_MANAGER_H
