#include "raw_table_model.h"

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QString localPath(const QString& pathOrUrl) {
    if (pathOrUrl.startsWith(u"file:"_s))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

// A column name to match by: lower case, no spaces / underscores / dashes.
QString nameKey(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar c : name)
        if (c.isLetterOrNumber() || c == u'\'' || c == u'#')
            out += c.toLower();
    return out;
}

} // namespace

RawTableModel::RawTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void RawTableModel::setTable(const QString& id, const QString& title, const QList<RawColumn>& columns,
                             const QList<QStringList>& rows) {
    beginResetModel();
    m_id = id;
    m_title = title;
    m_columns = columns;
    m_rows = rows;
    for (QStringList& r : m_rows)
        while (r.size() < m_columns.size())
            r << QString();
    m_original = m_rows;
    endResetModel();
    emit tableChanged();
    setDirty(false);
}

void RawTableModel::clear() {
    setTable(QString(), QString(), {}, {});
}

void RawTableModel::setDirty(bool on) {
    if (m_dirty == on)
        return;
    m_dirty = on;
    emit dirtyChanged();
}

QString RawTableModel::cell(int r, int c) const {
    if (r < 0 || r >= m_rows.size() || c < 0)
        return {};
    const QStringList& row = m_rows.at(r);
    return c < row.size() ? row.at(c) : QString();
}

int RawTableModel::columnOfKey(const QString& key) const {
    for (int c = 0; c < m_columns.size(); ++c)
        if (m_columns.at(c).key == key)
            return c;
    return -1;
}

QString RawTableModel::headerName(int column) const {
    return column >= 0 && column < m_columns.size() ? m_columns.at(column).name : QString();
}

bool RawTableModel::columnReadOnly(int column) const {
    return column < 0 || column >= m_columns.size() || m_columns.at(column).readOnly;
}

int RawTableModel::columnWidth(int column) const {
    if (column < 0 || column >= m_columns.size())
        return 80;
    QFont font = QGuiApplication::font();
    font.setPixelSize(12);
    font.setBold(true);
    const QFontMetrics bold(font);
    font.setBold(false);
    const QFontMetrics plain(font);
    int width = bold.horizontalAdvance(m_columns.at(column).name);
    const int sample = std::min<int>(static_cast<int>(m_rows.size()), 400);
    for (int r = 0; r < sample; ++r)
        width = std::max(width, plain.horizontalAdvance(cell(r, column)));
    return std::clamp(width + 22, 50, 320);
}

bool RawTableModel::setText(int row, int column, const QString& text) {
    return setData(index(row, column), text, Qt::EditRole);
}

int RawTableModel::find(const QString& query, int from) const {
    const QString q = query.trimmed();
    const int count = static_cast<int>(m_rows.size());
    if (q.isEmpty() || count == 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        const int r = (std::max(0, from) + i) % count;
        for (const QString& c : m_rows.at(r))
            if (c.contains(q, Qt::CaseInsensitive))
                return r;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

bool RawTableModel::parseBool(const QString& text, bool* ok) {
    const QString t = text.trimmed().toLower();
    *ok = true;
    if (t == u"true"_s || t == u"1"_s || t == u"yes"_s || t == u"on"_s || t == u"255"_s)
        return true;
    if (t == u"false"_s || t == u"0"_s || t == u"no"_s || t == u"off"_s || t.isEmpty())
        return false;
    *ok = false;
    return false;
}

qint64 RawTableModel::parseInt(const QString& text, bool* ok) {
    const QString t = text.trimmed();
    if (t.startsWith(u"0x"_s, Qt::CaseInsensitive))
        return static_cast<qint64>(t.mid(2).toULongLong(ok, 16));
    qint64 v = t.toLongLong(ok);
    if (*ok)
        return v;
    if (t.compare(u"true"_s, Qt::CaseInsensitive) == 0 || t.compare(u"false"_s, Qt::CaseInsensitive) == 0) {
        *ok = true;
        return t.size() == 4 ? 1 : 0;
    }
    const double d = t.toDouble(ok);
    if (*ok && d == static_cast<double>(static_cast<qint64>(d)))
        return static_cast<qint64>(d);
    *ok = false;
    return 0;
}

int RawTableModel::recordOfRow(int r, int count) const {
    const int c = columnOfKey(u"#index"_s);
    int record = r;
    if (c >= 0) {
        bool ok = false;
        record = static_cast<int>(parseInt(cell(r, c), &ok));
        if (!ok)
            return -1;
    }
    return record >= 0 && record < count ? record : -1;
}

QString RawTableModel::apply(const std::function<int(int row)>& record, const Getter& get, const Setter& set,
                             int* changed) {
    QStringList errors;
    int count = 0;
    for (int r = 0; r < m_rows.size(); ++r) {
        const int rec = record(r);
        if (rec < 0)
            continue;
        for (int c = 0; c < m_columns.size(); ++c) {
            const RawColumn& col = m_columns.at(c);
            if (col.readOnly || col.key == u"#index"_s)
                continue;
            const QString text = cell(r, c).trimmed();
            if (text == get(rec, col.key).trimmed())
                continue;
            QString error;
            if (set(rec, col.key, text, &error)) {
                ++count;
            } else if (!error.isEmpty() && errors.size() < 12) {
                errors << u"Row %1, %2: %3"_s.arg(r + 1).arg(col.name, error);
            }
        }
    }
    if (changed)
        *changed = count;
    if (errors.isEmpty()) {
        m_original = m_rows;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0, 0), index(rows() - 1, columns() - 1), {ChangedRole});
        setDirty(false);
    }
    return errors.join(u'\n');
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

QString RawTableModel::csvCell(const QString& text) {
    QString t = text;
    t.remove(QChar(0));
    if (t.contains(u',') || t.contains(u'"') || t.contains(u'\n') || t.contains(u'\r'))
        return u'"' + t.replace(u"\""_s, u"\"\""_s) + u'"';
    return t;
}

// One line of a CSV file. A quoted cell can go over lines: `open` says the last cell is still open (its text so
// far in `carry`), and the next line continues it.
QStringList RawTableModel::parseCsvLine(const QString& line, bool* open, QString* carry) {
    QStringList out;
    QString cur = *open ? *carry + u'\n' : QString();
    bool quoted = *open;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (quoted) {
            if (ch == u'"') {
                if (i + 1 < line.size() && line.at(i + 1) == u'"') {
                    cur += u'"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cur += ch;
            }
        } else if (ch == u'"' && cur.trimmed().isEmpty()) {
            cur.clear();
            quoted = true;
        } else if (ch == u',') {
            out << cur;
            cur.clear();
        } else {
            cur += ch;
        }
    }
    *open = quoted;
    if (quoted) {
        *carry = cur;
        return out;
    }
    out << cur;
    return out;
}

QString RawTableModel::exportCsv(const QString& pathOrUrl) const {
    if (m_columns.isEmpty())
        return u"Nothing to export."_s;
    QStringList lines;
    QStringList header;
    for (const RawColumn& c : m_columns)
        header << csvCell(c.name);
    lines << header.join(u',');
    for (const QStringList& r : m_rows) {
        QStringList cells;
        for (int c = 0; c < m_columns.size(); ++c)
            cells << csvCell(c < r.size() ? r.at(c) : QString());
        lines << cells.join(u',');
    }
    QSaveFile file(localPath(pathOrUrl));
    if (!file.open(QIODevice::WriteOnly))
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    file.write(lines.join(u"\r\n"_s).toUtf8() + "\r\n");
    if (!file.commit())
        return u"Could not write %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    return {};
}

QString RawTableModel::importCsv(const QString& pathOrUrl) {
    if (m_columns.isEmpty())
        return u"Open a table first."_s;
    QFile file(localPath(pathOrUrl));
    if (!file.open(QIODevice::ReadOnly))
        return u"Could not read %1."_s.arg(QDir::toNativeSeparators(file.fileName()));
    QByteArray bytes = file.readAll();
    // UTF-8 (with or without a BOM), else the C# editors' ANSI (Windows-1252, Latin-1 near enough).
    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    text = utf8.decode(bytes);
    if (utf8.hasError())
        text = QString::fromLatin1(bytes);

    const QStringList lines = text.split(u'\n');
    QList<QStringList> records;
    bool open = false;
    QString carry;
    for (QString line : lines) {
        if (line.endsWith(u'\r'))
            line.chop(1);
        if (!open && line.trimmed().isEmpty())
            continue;
        const bool wasOpen = open;
        QStringList cells = parseCsvLine(line, &open, &carry);
        if (wasOpen && !records.isEmpty()) {
            // Continues the last record's open cell.
            QStringList& last = records.last();
            if (!cells.isEmpty()) {
                last.last() = cells.takeFirst();
                last << cells;
            }
            if (open)
                last << QString();
        } else {
            if (open)
                cells << QString();
            records << cells;
        }
    }
    if (records.isEmpty())
        return u"The file is empty."_s;

    // The file's columns -> the grid's (by name).
    const QStringList header = records.takeFirst();
    QHash<QString, int> byName;
    for (int c = 0; c < m_columns.size(); ++c)
        byName.insert(nameKey(m_columns.at(c).name), c);
    QList<int> map;
    int matched = 0;
    for (const QString& h : header) {
        const int c = byName.value(nameKey(h), -1);
        map << c;
        if (c >= 0)
            ++matched;
    }
    if (matched == 0)
        return u"None of the file's columns are this table's (%1)."_s.arg(m_title);

    // A row's cells the file doesn't have: what the grid had for that record (by its index, else its position).
    const int indexColumn = columnOfKey(u"#index"_s);
    QHash<QString, int> rowByIndex;
    if (indexColumn >= 0)
        for (int r = 0; r < m_rows.size(); ++r)
            rowByIndex.insert(m_rows.at(r).value(indexColumn).trimmed(), r);
    const int fileIndex = indexColumn >= 0 ? static_cast<int>(map.indexOf(indexColumn)) : -1;

    QList<QStringList> rows;
    rows.reserve(records.size());
    for (int i = 0; i < records.size(); ++i) {
        const QStringList& rec = records.at(i);
        int base = i;
        if (fileIndex >= 0 && fileIndex < rec.size())
            base = rowByIndex.value(rec.at(fileIndex).trimmed(), -1);
        QStringList row = base >= 0 && base < m_rows.size() ? m_rows.at(base) : QStringList();
        while (row.size() < m_columns.size())
            row << QString();
        for (int k = 0; k < rec.size() && k < map.size(); ++k)
            if (map.at(k) >= 0)
                row[map.at(k)] = rec.at(k).trimmed();
        rows << row;
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit tableChanged();
    setDirty(true);
    if (matched < m_columns.size())
        return u"Imported %1 rows; %2 of the table's %3 columns were in the file (the rest kept)."_s
            .arg(rows.size()).arg(matched).arg(m_columns.size()).prepend(u"!"_s);
    return {};
}

QString RawTableModel::suggestedFileName(const QString& prefix) const {
    QString name = (prefix.isEmpty() ? QString() : prefix + u' ') + m_title + u".csv"_s;
    static const QString bad = u"\\/:*?\"<>|"_s;
    for (QChar& c : name)
        if (bad.contains(c))
            c = u'_';
    return name;
}

// ---------------------------------------------------------------------------
// QAbstractTableModel
// ---------------------------------------------------------------------------

int RawTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int RawTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_columns.size());
}

QVariant RawTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: return cell(index.row(), index.column());
    case ReadOnlyRole: return columnReadOnly(index.column());
    case ChangedRole: {
        const int r = index.row();
        const int c = index.column();
        const QString orig = r < m_original.size() && c < m_original.at(r).size() ? m_original.at(r).at(c) : QString();
        return orig != cell(r, c);
    }
    default: return {};
    }
}

bool RawTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole || columnReadOnly(index.column()))
        return false;
    QStringList& row = m_rows[index.row()];
    while (row.size() <= index.column())
        row << QString();
    const QString text = value.toString();
    if (row.at(index.column()) == text)
        return false;
    row[index.column()] = text;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, ChangedRole});
    setDirty(true);
    return true;
}

QVariant RawTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal)
        return headerName(section);
    return section + 1;
}

Qt::ItemFlags RawTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!columnReadOnly(index.column()))
        f |= Qt::ItemIsEditable;
    return f;
}

QHash<int, QByteArray> RawTableModel::roleNames() const {
    return {{Qt::DisplayRole, "display"}, {Qt::EditRole, "edit"}, {ReadOnlyRole, "readOnly"}, {ChangedRole, "changed"}};
}
