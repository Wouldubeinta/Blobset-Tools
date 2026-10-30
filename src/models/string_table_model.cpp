#include "string_table_model.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QtEndian>

#include <exception>

using namespace Qt::StringLiterals;

namespace {

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

quint32 readU32(const QByteArray& data, qsizetype at, bool bigEndian) {
    const uchar* p = reinterpret_cast<const uchar*>(data.constData()) + at;
    return bigEndian ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
}

void appendU32(QByteArray& out, quint32 value, bool bigEndian) {
    uchar bytes[4];
    if (bigEndian)
        qToBigEndian(value, bytes);
    else
        qToLittleEndian(value, bytes);
    out.append(reinterpret_cast<const char*>(bytes), 4);
}

/// Walks the table; returns the entry count when `data` is a complete table in this byte order, else -1.
qint64 validate(const QByteArray& data, bool bigEndian) {
    if (data.size() < 4)
        return -1;
    const quint32 count = readU32(data, 0, bigEndian);
    if (qint64(count) * 8 + 4 > data.size())
        return -1;
    qsizetype at = 4;
    for (quint32 i = 0; i < count * 2; ++i) {
        if (at + 4 > data.size())
            return -1;
        const quint32 length = readU32(data, at, bigEndian);
        at += 4;
        if (qint64(length) > data.size() - at)
            return -1;
        at += length;
    }
    return at == data.size() ? qint64(count) : -1;
}

QString csvField(const QString& value) {
    QString out = value;
    out.replace(u'"', u"\"\""_s);
    return u'"' + out + u'"';
}

/// RFC 4180 rows (quoted fields may hold commas, quotes and line breaks).
QList<QStringList> parseCsv(const QString& text) {
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool fieldStarted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == u'"') {
                if (i + 1 < text.size() && text[i + 1] == u'"') {
                    field += u'"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == u'"' && !fieldStarted) {
            quoted = true;
            fieldStarted = true;
        } else if (c == u',') {
            row.append(field);
            field.clear();
            fieldStarted = false;
        } else if (c == u'\n' || c == u'\r') {
            if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            row.append(field);
            field.clear();
            fieldStarted = false;
            if (!(row.size() == 1 && row.first().isEmpty()))
                rows.append(row);
            row.clear();
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !row.isEmpty()) {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

} // namespace

StringTableModel::StringTableModel(QObject* parent) : QAbstractListModel(parent) {}

int StringTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant StringTableModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Entry& entry = m_entries[size_t(index.row())];
    switch (role) {
    case KeyRole:
        return entry.key;
    case Qt::DisplayRole:
    case TextRole:
        return entry.text;
    case PreviewRole: {
        QString preview = entry.text.left(400);
        preview.replace(u"\r\n"_s, u" ⏎ "_s).replace(u'\n', u" ⏎ "_s).replace(u'\t', u' ');
        return preview;
    }
    case ModifiedRole:
        return entry.changed();
    case AddedRole:
        return entry.added;
    default:
        return {};
    }
}

QHash<int, QByteArray> StringTableModel::roleNames() const {
    return {{KeyRole, "key"}, {TextRole, "text"}, {PreviewRole, "preview"}, {ModifiedRole, "modified"},
            {AddedRole, "added"}};
}

int StringTableModel::modifiedCount() const {
    int changed = 0;
    for (const Entry& entry : m_entries)
        changed += entry.changed() ? 1 : 0;
    return changed;
}

void StringTableModel::setSource(const QString& name, const QString& workspacePath, const QString& filePath) {
    m_sourceName = name;
    m_workspacePath = workspacePath;
    m_filePath = filePath;
    emit sourceChanged();
}

void StringTableModel::markSaved() {
    for (Entry& entry : m_entries) {
        entry.originalKey = entry.key;
        entry.originalText = entry.text;
        entry.added = false;
    }
    m_structureChanged = false;
    if (!m_entries.empty())
        emit dataChanged(index(0), index(count() - 1), {ModifiedRole, AddedRole});
    emit modifiedChanged();
}

void StringTableModel::rebuildKeyIndex() {
    m_keyRows.clear();
    m_keyRows.reserve(qsizetype(m_entries.size()));
    for (int i = 0; i < count(); ++i)
        m_keyRows.insert(m_entries[size_t(i)].key, i);
}

// ---------------------------------------------------------------------------------------------------------------
// Loading / saving
// ---------------------------------------------------------------------------------------------------------------

bool StringTableModel::parse(const QByteArray& data, const QString& sourceName, const QString& workspacePath,
                             const QString& filePath) {
    bool bigEndian = false;
    qint64 total = validate(data, false);
    if (total < 0) {
        total = validate(data, true);
        bigEndian = true;
    }
    if (total < 0) {
        emit errorOccurred(u"%1 isn't a string table (count + length prefixed key / text pairs)."_s.arg(sourceName));
        return false;
    }

    std::vector<Entry> entries;
    entries.reserve(size_t(total));
    qsizetype at = 4;
    const auto next = [&]() {
        const quint32 length = readU32(data, at, bigEndian);
        const QString value = QString::fromUtf8(data.constData() + at + 4, length);
        at += 4 + length;
        return value;
    };
    for (qint64 i = 0; i < total; ++i) {
        Entry entry;
        entry.key = entry.originalKey = next();
        entry.text = entry.originalText = next();
        entries.push_back(std::move(entry));
    }

    beginResetModel();
    m_entries = std::move(entries);
    m_bigEndian = bigEndian;
    m_structureChanged = false;
    rebuildKeyIndex();
    endResetModel();

    setSource(sourceName, workspacePath, filePath);
    emit countChanged();
    emit modifiedChanged();
    emit actionCompleted(u"Loaded %1 strings from %2."_s.arg(total).arg(sourceName));
    return true;
}

bool StringTableModel::loadFromBlobset(const QString& hexHash, const QString& workspacePath) {
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        emit errorOccurred(u"This string table (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return false;
    }
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        emit errorOccurred(u"The application orchestrator is unavailable."_s);
        return false;
    }

    QByteArray data;
    try {
        data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                        orchestrator->platform());
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromUtf8(ex.what()));
        return false;
    }

    QString path = workspacePath;
    path.replace(u'\\', u'/');
    const QString name = path.isEmpty() ? u"0x"_s + hexHash.toUpper() : path;
    return parse(data, name, path, QString());
}

bool StringTableModel::loadFile(const QString& pathIn) {
    const QString path = toLocalPath(pathIn);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    return parse(file.readAll(), QFileInfo(path).fileName(), QString(), path);
}

QByteArray StringTableModel::serialize() const {
    QByteArray out;
    qsizetype size = 4;
    for (const Entry& entry : m_entries)
        size += 8 + entry.key.size() + entry.text.size() * 2;
    out.reserve(size);

    appendU32(out, quint32(m_entries.size()), m_bigEndian);
    for (const Entry& entry : m_entries) {
        const QByteArray key = entry.key.toUtf8();
        const QByteArray text = entry.text.toUtf8();
        appendU32(out, quint32(key.size()), m_bigEndian);
        out += key;
        appendU32(out, quint32(text.size()), m_bigEndian);
        out += text;
    }
    return out;
}

bool StringTableModel::writeFile(const QString& path, const QByteArray& data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        emit errorOccurred(u"Could not create the folder of %1."_s.arg(QDir::toNativeSeparators(path)));
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        emit errorOccurred(u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return false;
    }
    return true;
}

QString StringTableModel::saveToMods() {
    if (m_workspacePath.isEmpty()) {
        emit errorOccurred(u"This table wasn't opened from the blobset, so its place in the mods folder isn't "
                           u"known: use Save As."_s);
        return {};
    }
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const QString mods = orchestrator ? orchestrator->modsFolderPath() : QString();
    if (mods.isEmpty()) {
        emit errorOccurred(u"No game is loaded, so there's no mods folder."_s);
        return {};
    }

    const QString path = QDir::cleanPath(QDir(mods).filePath(m_workspacePath));
    if (!writeFile(path, serialize()))
        return {};

    m_filePath = path;
    markSaved();
    emit sourceChanged();
    emit actionCompleted(u"Saved %1 strings to %2. Run Modify to put them in the game."_s
                             .arg(count()).arg(QDir::toNativeSeparators(path)));
    return path;
}

bool StringTableModel::saveAs(const QString& pathIn) {
    const QString path = toLocalPath(pathIn);
    if (!writeFile(path, serialize()))
        return false;
    m_filePath = path;
    markSaved();
    emit sourceChanged();
    emit actionCompleted(u"Saved %1 strings to %2."_s.arg(count()).arg(QDir::toNativeSeparators(path)));
    return true;
}

bool StringTableModel::exportCsv(const QString& pathIn) const {
    const QString path = toLocalPath(pathIn);
    QString csv = u"Key,Text\r\n"_s;
    for (const Entry& entry : m_entries)
        csv += csvField(entry.key) + u',' + csvField(entry.text) + u"\r\n"_s;

    QSaveFile out(path);
    const QByteArray bytes = QByteArray("\xEF\xBB\xBF") + csv.toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        emit const_cast<StringTableModel*>(this)->errorOccurred(
            u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return false;
    }
    emit const_cast<StringTableModel*>(this)->actionCompleted(
        u"Exported %1 strings to %2."_s.arg(count()).arg(QDir::toNativeSeparators(path)));
    return true;
}

bool StringTableModel::importCsv(const QString& pathIn) {
    const QString path = toLocalPath(pathIn);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);

    const QList<QStringList> rows = parseCsv(QString::fromUtf8(bytes));
    int updated = 0, added = 0, skipped = 0;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QStringList& row = rows[i];
        if (row.size() < 2 || row[0].trimmed().isEmpty()) {
            ++skipped;
            continue;
        }
        if (i == 0 && row[0].compare(u"Key"_s, Qt::CaseInsensitive) == 0)
            continue;   // Header.

        const QString key = row[0];
        const auto existing = m_keyRows.constFind(key);
        if (existing != m_keyRows.constEnd()) {
            if (m_entries[size_t(existing.value())].text != row[1]) {
                m_entries[size_t(existing.value())].text = row[1];
                notifyRow(existing.value());
                ++updated;
            }
        } else {
            const int r = count();
            beginInsertRows({}, r, r);
            Entry entry;
            entry.key = key;
            entry.text = row[1];
            entry.added = true;
            m_entries.push_back(entry);
            m_keyRows.insert(key, r);
            endInsertRows();
            ++added;
        }
    }

    if (added > 0)
        emit countChanged();
    emit modifiedChanged();
    if (updated == 0 && added == 0 && rows.isEmpty()) {
        emit errorOccurred(u"%1 has no Key,Text rows."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    emit actionCompleted(u"Imported %1: %2 text(s) changed, %3 new string(s)%4."_s
                             .arg(QFileInfo(path).fileName()).arg(updated).arg(added)
                             .arg(skipped > 0 ? u", %1 row(s) skipped"_s.arg(skipped) : QString()));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------------------------------------------

void StringTableModel::notifyRow(int row) {
    const QModelIndex i = index(row);
    emit dataChanged(i, i);
    emit modifiedChanged();
}

QString StringTableModel::keyAt(int row) const {
    return row >= 0 && row < count() ? m_entries[size_t(row)].key : QString();
}

QString StringTableModel::textAt(int row) const {
    return row >= 0 && row < count() ? m_entries[size_t(row)].text : QString();
}

QString StringTableModel::originalTextAt(int row) const {
    return row >= 0 && row < count() ? m_entries[size_t(row)].originalText : QString();
}

bool StringTableModel::setText(int row, const QString& text) {
    if (row < 0 || row >= count())
        return false;
    Entry& entry = m_entries[size_t(row)];
    if (entry.text == text)
        return true;
    entry.text = text;
    notifyRow(row);
    return true;
}

bool StringTableModel::setKey(int row, const QString& keyIn) {
    if (row < 0 || row >= count())
        return false;
    const QString key = keyIn.trimmed();
    Entry& entry = m_entries[size_t(row)];
    if (key == entry.key)
        return true;
    if (key.isEmpty()) {
        emit errorOccurred(u"A key can't be empty."_s);
        return false;
    }
    if (m_keyRows.contains(key)) {
        emit errorOccurred(u"The key \"%1\" is already used (row %2)."_s.arg(key).arg(m_keyRows.value(key) + 1));
        return false;
    }
    m_keyRows.remove(entry.key);
    entry.key = key;
    m_keyRows.insert(key, row);
    notifyRow(row);
    return true;
}

QString StringTableModel::uniqueKey(const QString& baseIn) const {
    const QString base = baseIn.trimmed().isEmpty() ? u"NewString"_s : baseIn.trimmed();
    if (!m_keyRows.contains(base))
        return base;
    for (int n = 2;; ++n) {
        const QString candidate = u"%1_%2"_s.arg(base).arg(n);
        if (!m_keyRows.contains(candidate))
            return candidate;
    }
}

int StringTableModel::addEntry(const QString& keyIn, const QString& text) {
    const QString key = keyIn.trimmed();
    if (key.isEmpty() || m_keyRows.contains(key)) {
        emit errorOccurred(key.isEmpty() ? u"A key can't be empty."_s
                                         : u"The key \"%1\" is already used."_s.arg(key));
        return -1;
    }
    const int row = count();
    beginInsertRows({}, row, row);
    Entry entry;
    entry.key = key;
    entry.text = text;
    entry.added = true;
    m_entries.push_back(entry);
    m_keyRows.insert(key, row);
    endInsertRows();
    emit countChanged();
    emit modifiedChanged();
    return row;
}

bool StringTableModel::removeEntry(int row) {
    if (row < 0 || row >= count())
        return false;
    const bool wasAdded = m_entries[size_t(row)].added;
    beginRemoveRows({}, row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    rebuildKeyIndex();
    if (!wasAdded)
        m_structureChanged = true;
    emit countChanged();
    emit modifiedChanged();
    return true;
}

void StringTableModel::revert(int row) {
    if (row < 0 || row >= count())
        return;
    Entry& entry = m_entries[size_t(row)];
    if (entry.added) {
        removeEntry(row);
        return;
    }
    if (entry.key != entry.originalKey && !m_keyRows.contains(entry.originalKey)) {
        m_keyRows.remove(entry.key);
        entry.key = entry.originalKey;
        m_keyRows.insert(entry.key, row);
    }
    entry.text = entry.originalText;
    notifyRow(row);
}

// ---------------------------------------------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------------------------------------------

bool StringTableModel::matches(const Entry& entry, const QString& query, int scope) const {
    if (scope != 2 && entry.key.contains(query, Qt::CaseInsensitive))
        return true;
    return scope != 1 && entry.text.contains(query, Qt::CaseInsensitive);
}

int StringTableModel::search(const QString& queryIn, int startRow, bool backward, int scope) const {
    const QString query = queryIn.trimmed();
    const int total = count();
    if (query.isEmpty() || total == 0)
        return -1;
    if (startRow < 0 || startRow >= total)
        startRow = backward ? total - 1 : 0;
    for (int step = 0; step < total; ++step) {
        const int row = backward ? (startRow - step + total) % total : (startRow + step) % total;
        if (matches(m_entries[size_t(row)], query, scope))
            return row;
    }
    return -1;
}

QVariantMap StringTableModel::searchInfo(const QString& queryIn, int row, int scope) const {
    const QString query = queryIn.trimmed();
    int total = 0, position = 0;
    if (!query.isEmpty()) {
        for (int i = 0; i < count(); ++i) {
            if (!matches(m_entries[size_t(i)], query, scope))
                continue;
            ++total;
            if (i == row)
                position = total;
        }
    }
    return {{u"count"_s, total}, {u"position"_s, position}};
}
