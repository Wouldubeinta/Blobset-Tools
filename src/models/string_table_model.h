#ifndef STRING_TABLE_MODEL_H
#define STRING_TABLE_MODEL_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QAbstractListModel>
#include <QByteArray>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

#include <vector>

/// The game's string tables (text\strings\*.str, one file per language):
///
///   u32 count
///   count x { u32 keyLength, key bytes, u32 textLength, text bytes (UTF-8) }   - no terminators, no padding
///
/// Little endian on PC (big endian tables are detected and written back the same way). The entries are not sorted
/// (they're grouped the way the game's source files were), so the order is kept and new entries go at the end.
class StringTableModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged FINAL)
    Q_PROPERTY(int modifiedCount READ modifiedCount NOTIFY modifiedChanged FINAL)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged FINAL)        // Shown in the header.
    Q_PROPERTY(QString workspacePath READ workspacePath NOTIFY sourceChanged FINAL)  // Blobset path, empty for a file.
    Q_PROPERTY(QString filePath READ filePath NOTIFY sourceChanged FINAL)            // Last file loaded / saved.
    Q_PROPERTY(bool bigEndian READ bigEndian NOTIFY sourceChanged FINAL)

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,   // "key"
        TextRole,                     // "text"
        PreviewRole,                  // "preview": the text on one line (line breaks shown as ⏎)
        ModifiedRole,                 // "modified": changed since it was loaded
        AddedRole,                    // "added": not in the loaded table
    };

    explicit StringTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_entries.size()); }
    bool isModified() const { return m_structureChanged || modifiedCount() > 0; }
    int modifiedCount() const;
    QString sourceName() const { return m_sourceName; }
    QString workspacePath() const { return m_workspacePath; }
    QString filePath() const { return m_filePath; }
    bool bigEndian() const { return m_bigEndian; }

    // --- Loading -------------------------------------------------------------------------------------------
    Q_INVOKABLE bool loadFromBlobset(const QString& hexHash, const QString& workspacePath);
    Q_INVOKABLE bool loadFile(const QString& path);   // Path or file:// URL.

    // --- Saving --------------------------------------------------------------------------------------------
    // Into the game's mods folder at the table's blobset path (what Modify / Create pick up). Returns the path.
    Q_INVOKABLE QString saveToMods();
    Q_INVOKABLE bool saveAs(const QString& path);

    // Key,Text CSV (UTF-8 with BOM, quoted, so Excel and translation tools keep line breaks and accents).
    Q_INVOKABLE bool exportCsv(const QString& path) const;
    // Updates the texts of keys the table has and adds the others. Returns false when nothing could be read.
    Q_INVOKABLE bool importCsv(const QString& path);

    // --- Editing -------------------------------------------------------------------------------------------
    Q_INVOKABLE QString keyAt(int row) const;
    Q_INVOKABLE QString textAt(int row) const;
    Q_INVOKABLE QString originalTextAt(int row) const;          // As loaded (empty for an added entry).
    Q_INVOKABLE bool setText(int row, const QString& text);
    Q_INVOKABLE bool setKey(int row, const QString& key);        // Refused when empty or already used.
    Q_INVOKABLE int addEntry(const QString& key, const QString& text = QString());   // Row, -1 when refused.
    Q_INVOKABLE bool removeEntry(int row);
    Q_INVOKABLE void revert(int row);                            // Back to the loaded text.
    Q_INVOKABLE QString uniqueKey(const QString& base) const;    // "base", "base_2", ... not used yet.

    // --- Search (keys and texts, case insensitive, wrapping) ------------------------------------------------
    // scope: 0 = keys and texts, 1 = keys only, 2 = texts only. Returns the row or -1.
    Q_INVOKABLE int search(const QString& query, int startRow, bool backward = false, int scope = 0) const;
    Q_INVOKABLE QVariantMap searchInfo(const QString& query, int row, int scope = 0) const;   // { count, position }

signals:
    void countChanged();
    void modifiedChanged();
    void sourceChanged();
    void actionCompleted(const QString& message);
    void errorOccurred(const QString& message);

private:
    struct Entry {
        QString key;
        QString text;
        QString originalKey;
        QString originalText;
        bool added = false;

        bool changed() const { return added || key != originalKey || text != originalText; }
    };

    bool parse(const QByteArray& data, const QString& sourceName, const QString& workspacePath, const QString& filePath);
    QByteArray serialize() const;
    bool writeFile(const QString& path, const QByteArray& data);
    bool matches(const Entry& entry, const QString& query, int scope) const;
    void notifyRow(int row);
    void setSource(const QString& name, const QString& workspacePath, const QString& filePath);

    std::vector<Entry> m_entries;
    QHash<QString, int> m_keyRows;     // key -> row (rebuilt after removals)
    QString m_sourceName;
    QString m_workspacePath;
    QString m_filePath;
    bool m_bigEndian = false;
    bool m_structureChanged = false;   // Entries removed (added / renamed ones show up as changed rows).

    void rebuildKeyIndex();
    void markSaved();                  // What was just written becomes the new "loaded" state.
};

#endif // STRING_TABLE_MODEL_H
