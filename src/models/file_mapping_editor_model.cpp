#include "file_mapping_editor_model.h"

#include <QUrl>
#include <QVariantMap>

#include <utility>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

/// Canonical hash text: exactly 16 lowercase hex digits (matches BlobsetEntryRegistry::makeKey).
QString formatHash(quint64 hash) {
    return QString::number(hash, 16).rightJustified(16, u'0');
}

/// Accepts "1A2B...", "0x1A2B..." with up to 16 hex digits. Hashes are 64 bit, so they are edited as text:
/// QML numbers are doubles and would silently round anything above 2^53.
bool parseHash(QString text, quint64& hash) {
    text = text.trimmed();
    if (text.startsWith(u"0x"_s, Qt::CaseInsensitive))
        text.remove(0, 2);

    if (text.isEmpty() || text.size() > 16)
        return false;

    const bool allHex = std::all_of(text.cbegin(), text.cend(), [](QChar c) {
        const char16_t u = c.unicode();
        return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F');
    });
    if (!allHex)
        return false;

    bool ok = false;
    const quint64 value = text.toULongLong(&ok, 16);
    if (ok)
        hash = value;
    return ok;
}

} // namespace

FileMappingEditorModel::FileMappingEditorModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int FileMappingEditorModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

int FileMappingEditorModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 2; // File Path, Hash - the two fields of MappingEntry.
}

QVariant FileMappingEditorModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};

    const FileMappingWorker::MappingEntry& entry = m_entries[index.row()];

    switch (role) {
    case PathRole:
        return entry.path;
    case HashRole:
        return formatHash(entry.hash);
    case Qt::DisplayRole:
    case Qt::EditRole:
        return index.column() == 0 ? entry.path : formatHash(entry.hash);
    default:
        return {};
    }
}

bool FileMappingEditorModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return false;

    if (role == Qt::EditRole || role == Qt::DisplayRole)
        role = index.column() == 0 ? PathRole : HashRole;

    FileMappingWorker::MappingEntry& entry = m_entries[index.row()];

    if (role == PathRole) {
        const QString path = FileMappingWorker::normalizePath(value.toString());
        if (path == entry.path)
            return true;

        const QString problem = FileMappingWorker::validatePath(path);
        if (!problem.isEmpty()) {
            emit errorOccurred(problem);
            return false;
        }
        entry.path = path;
    } else if (role == HashRole) {
        quint64 hash = 0;
        if (!parseHash(value.toString(), hash)) {
            emit errorOccurred(u"The hash must be 1 to 16 hexadecimal digits."_s);
            return false;
        }
        if (hash == entry.hash)
            return true;
        entry.hash = hash;
    } else {
        return false;
    }

    // Both cells read the same row, so refresh the whole row.
    emit dataChanged(this->index(index.row(), 0), this->index(index.row(), 1),
                     {PathRole, HashRole, Qt::DisplayRole, Qt::EditRole});
    setModified(true);
    return true;
}

Qt::ItemFlags FileMappingEditorModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant FileMappingEditorModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;   // Row numbers.

    switch (section) {
    case 0: return u"File Path"_s;
    case 1: return u"Hash"_s;
    default: return {};
    }
}

QHash<int, QByteArray> FileMappingEditorModel::roleNames() const {
    return {
        {PathRole, "pathVal"},
        {HashRole, "hashVal"},
        {Qt::DisplayRole, "display"},
        {Qt::EditRole, "edit"},     // TableView's edit delegate commits through this.
    };
}

void FileMappingEditorModel::loadMapping(const QString& filePath) {
    QString path = filePath;
    if (path.startsWith(u"file:"_s, Qt::CaseInsensitive))
        path = QUrl(path).toLocalFile();

    // Read first: a failed load leaves the table that is currently open untouched.
    std::vector<FileMappingWorker::MappingEntry> entries;
    try {
        entries = FileMappingWorker::readMappingFile(path);
    } catch (const std::exception& ex) {
        emit errorOccurred(u"Failed to open File Mapping table: %1"_s.arg(QString::fromUtf8(ex.what())));
        return;
    }

    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();

    setFilePath(path);
    setModified(false);
    emit countChanged();
    emit actionCompleted(u"Successfully loaded %1 mapping entries."_s.arg(m_entries.size()));
}

void FileMappingEditorModel::saveMapping() {
    if (m_filePath.isEmpty()) {
        emit errorOccurred(u"No File Mapping table is loaded."_s);
        return;
    }

    try {
        FileMappingWorker::writeMappingFile(m_filePath, m_entries);
    } catch (const std::exception& ex) {
        emit errorOccurred(u"Failed to write to File Mapping table: %1"_s.arg(QString::fromUtf8(ex.what())));
        return;
    }

    setModified(false);
    emit saved();
    emit actionCompleted(u"File Mapping table changes have been successfully saved."_s);
}

namespace {
// "0x1A2B" -> "1a2b" when the query could be (part of) a hash, else empty.
QString hashNeedle(QString query) {
    query = query.trimmed().toLower();
    if (query.startsWith(u"0x"_s))
        query.remove(0, 2);
    if (query.isEmpty() || query.size() > 16)
        return {};
    for (QChar c : std::as_const(query)) {
        const char16_t u = c.unicode();
        if (!((u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f')))
            return {};
    }
    return query;
}
} // namespace

bool FileMappingEditorModel::matches(qsizetype row, const QString& path, const QString& hash) const {
    const FileMappingWorker::MappingEntry& entry = m_entries[size_t(row)];
    if (!path.isEmpty() && entry.path.contains(path, Qt::CaseInsensitive))
        return true;
    return !hash.isEmpty() && formatHash(entry.hash).contains(hash);
}

int FileMappingEditorModel::search(const QString& query, int startIndex, bool backward) {
    // Paths are stored with '\', so "ui/bsb" finds "ui\bsb" as well.
    const QString needle = FileMappingWorker::normalizePath(query);
    const QString hash = hashNeedle(query);
    const int total = count();
    if (needle.isEmpty() || total == 0)
        return -1;

    if (startIndex < 0 || startIndex >= total)
        startIndex = backward ? total - 1 : 0;

    for (int step = 0; step < total; ++step) {
        const int row = backward ? (startIndex - step + total) % total
                                 : (startIndex + step) % total;
        if (matches(row, needle, hash))
            return row;
    }
    return -1;
}

QVariantMap FileMappingEditorModel::searchInfo(const QString& query, int row) const {
    const QString needle = FileMappingWorker::normalizePath(query);
    const QString hash = hashNeedle(query);
    int total = 0;
    int position = 0;
    if (!needle.isEmpty()) {
        for (qsizetype i = 0; i < qsizetype(m_entries.size()); ++i) {
            if (!matches(i, needle, hash))
                continue;
            ++total;
            if (i == row)
                position = total;
        }
    }
    return {{u"count"_s, total}, {u"position"_s, position}};
}

void FileMappingEditorModel::reload() {
    if (m_filePath.isEmpty()) {
        emit errorOccurred(u"No File Mapping table is loaded."_s);
        return;
    }
    loadMapping(m_filePath);
}

void FileMappingEditorModel::setModified(bool modified) {
    if (m_modified == modified)
        return;
    m_modified = modified;
    emit modifiedChanged();
}

void FileMappingEditorModel::setFilePath(const QString& path) {
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit filePathChanged();
}
