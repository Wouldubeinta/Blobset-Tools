#ifndef RAW_TABLE_MODEL_H
#define RAW_TABLE_MODEL_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QAbstractTableModel>
#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>

// The Raw Databases grids (the C# editors' "Raw ... Database" windows): one table of a database / save (every
// player, team, lineup, ...) as text cells, edited in place, then written back in one go ("Save Changes"), and
// exported to / imported from CSV. RawDatabaseView.qml shows it; the editor managers fill it (fillRawTable()) and
// write it back (applyRawTable()).
//
// Each column has the manager's key for what it shows ("#index" = the record's index, read only). The CSV files are
// the C# editors': a header line of the column names, then a line a row, comma separated (a cell with a comma or a
// quote is quoted). An imported file's columns are matched by name (case, spaces ignored), so files exported by the
// C# editors import too; columns it doesn't have keep what the grid had.
struct RawColumn {
    QString name;
    QString key;
    bool readOnly = false;
};

class RawTableModel : public QAbstractTableModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString tableId READ tableId NOTIFY tableChanged)
    Q_PROPERTY(QString title READ title NOTIFY tableChanged)
    Q_PROPERTY(int rows READ rows NOTIFY tableChanged)
    Q_PROPERTY(int columns READ columns NOTIFY tableChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)   // Edited / imported since it was filled.

public:
    enum Roles { ReadOnlyRole = Qt::UserRole + 1, ChangedRole };

    explicit RawTableModel(QObject* parent = nullptr);

    // --- For the managers ---------------------------------------------------------------------------------------
    void setTable(const QString& id, const QString& title, const QList<RawColumn>& columns,
                  const QList<QStringList>& rows);
    const QList<RawColumn>& columnList() const { return m_columns; }
    const QStringList& row(int r) const { return m_rows.at(r); }
    QString cell(int r, int c) const;
    int columnOfKey(const QString& key) const;

    // Writes the rows back: `set(row, key, text)` for each cell that isn't read only and differs from
    // `get(row, key)` (the record's current value as the grid shows it). `record(row)` gives the record a row is
    // (-1 = skip it). Returns "" or the errors (`set` gives one by returning false and setting *error).
    using Getter = std::function<QString(int record, const QString& key)>;
    using Setter = std::function<bool(int record, const QString& key, const QString& text, QString* error)>;
    QString apply(const std::function<int(int row)>& record, const Getter& get, const Setter& set, int* changed);
    // The record a row is: its "#index" cell (or the row number when there isn't one); -1 when it's not a number or
    // not below `count`.
    int recordOfRow(int r, int count) const;

    // A bool as the C# editors show it.
    static QString boolText(bool on) { return on ? QStringLiteral("True") : QStringLiteral("False"); }
    // "True" / "1" / "yes" / "on" (any case) -> true; "False" / "0" / "no" / "off" / "" -> false; else *ok = false.
    static bool parseBool(const QString& text, bool* ok);
    // A whole number ("12", "-3", "0x1F", "12.0"); *ok = false if it isn't one.
    static qint64 parseInt(const QString& text, bool* ok);

    // --- QML ------------------------------------------------------------------------------------------------------
    QString tableId() const { return m_id; }
    QString title() const { return m_title; }
    int rows() const { return static_cast<int>(m_rows.size()); }
    int columns() const { return static_cast<int>(m_columns.size()); }
    bool dirty() const { return m_dirty; }

    Q_INVOKABLE QString headerName(int column) const;
    Q_INVOKABLE bool columnReadOnly(int column) const;
    Q_INVOKABLE int columnWidth(int column) const;      // A width (pixels) that fits the column's name and cells.
    Q_INVOKABLE QString text(int row, int column) const { return cell(row, column); }
    Q_INVOKABLE bool setText(int row, int column, const QString& text);
    // The first row at or after `from` (wrapping) with a cell containing `query` (any case); -1 if none.
    Q_INVOKABLE int find(const QString& query, int from) const;
    Q_INVOKABLE void clear();
    // "" or an error.
    Q_INVOKABLE QString exportCsv(const QString& pathOrUrl) const;
    Q_INVOKABLE QString importCsv(const QString& pathOrUrl);
    Q_INVOKABLE QString suggestedFileName(const QString& prefix) const;   // "<prefix> <title>.csv"

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    static QStringList parseCsvLine(const QString& line, bool* open, QString* carry);
    static QString csvCell(const QString& text);

signals:
    void tableChanged();
    void dirtyChanged();

private:
    QString m_id;
    QString m_title;
    QList<RawColumn> m_columns;
    QList<QStringList> m_rows;
    QList<QStringList> m_original;   // As filled, to mark the changed cells.
    bool m_dirty = false;

    void setDirty(bool on);
};

#endif // RAW_TABLE_MODEL_H
