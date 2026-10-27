#ifndef FILE_MAPPING_EDITOR_MODEL_H
#define FILE_MAPPING_EDITOR_MODEL_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The author may be contacted at:
 * Discord: Wouldubeinta
 *
 * History:
 * [Wouldubeinta]  01/07/2025  Created (C# FileMapping)
 *                             Ported to Qt 6 (File Mapping Editor Model)
 */

#include "file_mapping_worker.h"

#include <QAbstractTableModel>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

#include <vector>

/// Editable view of a .mapping table. Rows are FileMappingWorker::MappingEntry (path + hash);
/// the file format itself lives in FileMappingWorker so the generator and the editor can't drift apart.
class FileMappingEditorModel : public QAbstractTableModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged FINAL)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged FINAL)

public:
    enum MappingRoles {
        PathRole = Qt::UserRole + 1, // "pathVal"
        HashRole                     // "hashVal" - 16 digit lowercase hex, the same key BlobsetEntryRegistry uses
    };

    explicit FileMappingEditorModel(QObject* parent = nullptr);
    ~FileMappingEditorModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    int count() const { return static_cast<int>(m_entries.size()); }
    bool isModified() const { return m_modified; }
    QString filePath() const { return m_filePath; }

    Q_INVOKABLE void loadMapping(const QString& filePath);
    Q_INVOKABLE void saveMapping();

    /// Case insensitive search of the paths ('/' finds '\') and hashes, wrapping around. Returns the row or -1.
    Q_INVOKABLE int search(const QString& query, int startIndex, bool backward = false);

    /// How many rows match `query`, and which match `row` is (1 based, 0 when it isn't one): { count, position }.
    Q_INVOKABLE QVariantMap searchInfo(const QString& query, int row) const;

    /// Reads the current file again (edits are dropped).
    Q_INVOKABLE void reload();

signals:
    void actionCompleted(const QString& message);
    void errorOccurred(const QString& message);
    void saved();   // saveMapping() wrote the file.
    void countChanged();
    void modifiedChanged();
    void filePathChanged();

private:
    void setModified(bool modified);
    void setFilePath(const QString& path);
    bool matches(qsizetype row, const QString& path, const QString& hash) const;

    std::vector<FileMappingWorker::MappingEntry> m_entries;
    QString m_filePath;
    bool m_modified = false;
};

#endif // FILE_MAPPING_EDITOR_MODEL_H
