#include "hex_viewer_model.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"

#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <exception>

using namespace Qt::StringLiterals;

namespace {

constexpr char kHexDigits[] = "0123456789ABCDEF";

QString humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + u" MB"_s;
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + u" KB"_s;
    return QString::number(bytes) + u" B"_s;
}

QString sizeWithBytes(qint64 bytes) {
    return u"%1 (%2 bytes)"_s.arg(humanSize(bytes), QLocale().toString(bytes));
}

} // namespace

HexViewerModel::HexViewerModel(QObject* parent) : QAbstractListModel(parent) {}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

int HexViewerModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>((m_data.size() + kBytesPerRow - 1) / kBytesPerRow);
}

QVariant HexViewerModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const qint64 start = static_cast<qint64>(index.row()) * kBytesPerRow;
    const qint64 count = std::min<qint64>(kBytesPerRow, m_data.size() - start);
    const auto* bytes = reinterpret_cast<const uchar*>(m_data.constData()) + start;

    switch (role) {
    case AddressRole:
        return QString::number(m_baseOffset + start, 16).toUpper().rightJustified(8, u'0');

    case HexDataRole: {
        QString hex;
        hex.reserve(kBytesPerRow * 3 + 1);
        for (qint64 i = 0; i < count; ++i) {
            if (i > 0)
                hex += (i == kBytesPerRow / 2) ? u"  "_s : u" "_s; // Extra gap between the two 8 byte halves.
            hex += QLatin1Char(kHexDigits[bytes[i] >> 4]);
            hex += QLatin1Char(kHexDigits[bytes[i] & 0x0F]);
        }
        return hex;
    }

    case AsciiDataRole: {
        QString ascii;
        ascii.reserve(kBytesPerRow);
        for (qint64 i = 0; i < count; ++i)
            ascii += (bytes[i] >= 0x20 && bytes[i] <= 0x7E) ? QLatin1Char(static_cast<char>(bytes[i])) : QLatin1Char('.');
        return ascii;
    }

    case IsVramRole:
        return m_mainSize >= 0 && m_mainSize < m_data.size() && start >= m_mainSize;

    default:
        return {};
    }
}

QHash<int, QByteArray> HexViewerModel::roleNames() const {
    return {
        {AddressRole, "address"},
        {HexDataRole, "hexData"},
        {AsciiDataRole, "asciiData"},
        {IsVramRole, "isVram"},
    };
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void HexViewerModel::loadData(const QString& filePath, qint64 offset, qint64 size) {
    const QString path = filePath.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(filePath).toLocalFile()
                                                                              : filePath;

    startLoad(QtConcurrent::run([path, offset, size]() {
        LoadResult r;
        r.baseOffset = offset;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            r.error = u"Failed to open '%1' for hex inspection: %2"_s.arg(path, file.errorString());
            return r;
        }
        if (offset < 0 || offset > file.size() || !file.seek(offset)) {
            r.error = u"Offset 0x%1 is outside of '%2'."_s.arg(QString::number(offset, 16).toUpper(), path);
            return r;
        }

        const qint64 available = file.size() - offset;
        r.data = file.read(size > 0 ? std::min(size, available) : available);
        return r;
    }), QFileInfo(path).fileName());
}

void HexViewerModel::loadFromBlobset(const QString& hexHash, const QString& displayName) {
    const QString name = displayName.isEmpty() ? u"0x"_s + hexHash.toUpper() : displayName;

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        resetData(name);
        const QString message = u"This asset (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper());
        setStatus(false, message);
        emit errorOccurred(message);
        return;
    }

    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        resetData(name);
        setStatus(false, u"The application orchestrator is unavailable."_s);
        return;
    }

    // Read the settings here (UI thread); the worker only gets plain values.
    const QString gameLocation = orchestrator->gameLocation();
    const int version = orchestrator->blobsetVersion();
    const int platform = orchestrator->platform();

    startLoad(QtConcurrent::run([entry, gameLocation, version, platform]() {
        LoadResult r;
        try {
            r.data = BlobsetAssetReader::read(entry, gameLocation, version, platform, &r.mainSize);
        } catch (const std::exception& ex) {
            r.error = QString::fromUtf8(ex.what());
        }
        return r;
    }), name);
}

void HexViewerModel::startLoad(const QFuture<LoadResult>& future, const QString& sourceName) {
    const quint64 generation = ++m_generation;

    resetData(sourceName);
    setBusy(true);
    setStatus(true, u"Reading %1..."_s.arg(sourceName));

    auto* watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this, [this, watcher, generation]() {
        const LoadResult result = watcher->result();
        watcher->deleteLater();

        if (generation != m_generation)
            return; // A newer load was started in the meantime.

        setBusy(false);
        applyResult(result);
    });
    watcher->setFuture(future);
}

void HexViewerModel::applyResult(const LoadResult& result) {
    if (!result.error.isEmpty()) {
        setStatus(false, result.error);
        emit errorOccurred(result.error);
        return;
    }

    beginResetModel();
    m_data = result.data;
    m_baseOffset = result.baseOffset;
    m_mainSize = result.mainSize;
    endResetModel();
    emit contentChanged();

    QString message = u"%1 | %2"_s.arg(m_sourceName, sizeWithBytes(m_data.size()));
    if (m_mainSize >= 0 && m_mainSize < m_data.size())
        message += u" | Main: %1 | VRAM: %2"_s.arg(humanSize(m_mainSize), humanSize(m_data.size() - m_mainSize));
    setStatus(true, message);
}

void HexViewerModel::resetData(const QString& sourceName) {
    beginResetModel();
    m_data.clear();
    m_baseOffset = 0;
    m_mainSize = -1;
    m_sourceName = sourceName;
    endResetModel();
    emit contentChanged();
}

void HexViewerModel::clear() {
    ++m_generation;
    setBusy(false);
    resetData(QString());
    setStatus(true, QString());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int HexViewerModel::rowForOffset(qint64 offset) const {
    const qint64 relative = offset - m_baseOffset;
    if (relative < 0 || relative >= m_data.size())
        return -1;
    return static_cast<int>(relative / kBytesPerRow);
}

void HexViewerModel::reportMessage(const QString& message, bool ok) {
    setStatus(ok, message);
}

void HexViewerModel::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void HexViewerModel::setStatus(bool ok, const QString& message) {
    if (m_statusOk == ok && m_statusText == message)
        return;
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}
