#include "bnk_tool_manager.h"

#include "app_orchestrator.h"
#include "audio_decoder.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "core_defs.h"
#include "utilities.h"
#include "wem_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

// QML hands folders / files over as "file:///..." URLs, C++ callers as plain paths.
QString toLocalPath(const QString& path) {
    if (path.startsWith(u"file:"_s, Qt::CaseInsensitive))
        return QUrl(path).toLocalFile();
    return path;
}

QString humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + u" MB"_s;
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + u" KB"_s;
    return QString::number(bytes) + u" B"_s;
}

QString sizeWithBytes(qint64 bytes) {
    return u"%1  (%2 bytes)"_s.arg(humanSize(bytes), QLocale().toString(bytes));
}

QString hexId(quint32 value) {
    return u"0x"_s + QString::number(value, 16).toUpper().rightJustified(8, u'0');
}

QString idText(quint32 value) {
    return u"%1  (%2)"_s.arg(value).arg(hexId(value));
}

QString wemFileName(quint32 id) {
    return QString::number(id) + u".wem"_s;
}

QString formatDuration(double seconds) {
    const qint64 totalMs = qRound64(seconds * 1000.0);
    return u"%1:%2.%3"_s.arg(totalMs / 60000)
        .arg((totalMs / 1000) % 60, 2, 10, QChar(u'0'))
        .arg(totalMs % 1000, 3, 10, QChar(u'0'));
}

QString channelText(int channels) {
    switch (channels) {
    case 1:  return u"1 (Mono)"_s;
    case 2:  return u"2 (Stereo)"_s;
    case 6:  return u"6 (5.1)"_s;
    case 8:  return u"8 (7.1)"_s;
    default: return QString::number(channels);
    }
}

QVariantMap infoRow(const QString& label, const QString& value) {
    return {{u"label"_s, label}, {u"value"_s, value}};
}

// Returns an error text, empty on success.
QString writeWem(const QByteArray& data, const BnkFile::Media& media, const QString& outDir) {
    if (!QDir().mkpath(outDir))
        return u"Could not create the output folder: "_s + outDir;

    const QString path = QDir(outDir).filePath(wemFileName(media.id));
    const QByteArray bytes = data.mid(media.offset, media.size);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
        return u"%1: %2"_s.arg(path, out.errorString());
    return {};
}

} // namespace

// ==============================================================================================================
// BnkEntryModel
// ==============================================================================================================

int BnkEntryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant BnkEntryModel::data(const QModelIndex& index, int role) const {
    const int entry = index.isValid() ? entryAt(index.row()) : -1;
    if (entry < 0)
        return {};

    const BnkFile::Media& media = m_media[entry];
    switch (role) {
    case NameRole:     return wemFileName(media.id);
    case WemIdRole:    return media.id;
    case SizeTextRole: return humanSize(media.size);
    case EntryRole:    return entry;
    default:           return {};
    }
}

QHash<int, QByteArray> BnkEntryModel::roleNames() const {
    return {
        {NameRole, "name"},
        {WemIdRole, "wemId"},
        {SizeTextRole, "sizeText"},
        {EntryRole, "entry"},
    };
}

void BnkEntryModel::reset(const QList<BnkFile::Media>& media, const QList<int>& rows) {
    beginResetModel();
    m_media = media;
    m_rows = rows;
    endResetModel();
}

int BnkEntryModel::entryAt(int row) const {
    return (row >= 0 && row < m_rows.size()) ? m_rows[row] : -1;
}

// ==============================================================================================================
// BnkToolManager
// ==============================================================================================================

BnkToolManager::BnkToolManager(QObject* parent)
    : QObject(parent), m_model(new BnkEntryModel(this)) {}

BnkToolManager::~BnkToolManager() {
    m_cancel = true;
    m_extractFuture.waitForFinished(); // The worker still reports back to this object.
}

QString BnkToolManager::currentName() const {
    return m_currentEntry >= 0 ? wemFileName(m_bank.media[m_currentEntry].id) : QString();
}

QString BnkToolManager::appVersion() const {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QString() : u"v"_s + version;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void BnkToolManager::loadBnk(const QString& path) {
    if (m_busy)
        return;

    const QString local = toLocalPath(path);
    loadFile(local, QFileInfo(local).fileName());
}

void BnkToolManager::loadBnkFromBlobset(const QString& hexHash) {
    if (m_busy)
        return;

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        failLoad(u"This sound bank (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return;
    }

    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        failLoad(u"The application orchestrator is unavailable."_s);
        return;
    }

    // An edited copy in the mods folder (Save Changes) is what Modify uses: show and edit that one.
    const QString workspacePath = orchestrator->pathFromHash(hexHash);
    const QString modsCopy = workspacePath.isEmpty() ? QString() : orchestrator->modFilePath(workspacePath);
    QByteArray data;
    bool fromMods = false;
    if (!modsCopy.isEmpty() && QFileInfo(modsCopy).isFile()) {
        QFile file(modsCopy);
        if (file.open(QIODevice::ReadOnly)) {
            data = file.readAll();
            fromMods = true;
        }
    }

    // Wwise banks ship in V3 (Cricket 19) and V4 blobsets. BlobsetAssetReader finds the asset's file for either
    // (32 / 64 bit hash names) and undoes the section compression, so a stored or compressed bank both load.
    if (!fromMods) {
        try {
            data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                            orchestrator->platform());
        } catch (const std::exception& ex) {
            failLoad(u"Failed to read the sound bank: "_s + QString::fromUtf8(ex.what()));
            return;
        }
    }

    loadBytes(std::move(data), u"0x"_s + hexHash.toUpper());
    if (m_bank.media.isEmpty() && m_data.isEmpty())
        return;   // Failed to load.
    m_workspacePath = workspacePath;
    emit changesChanged();
    if (fromMods)
        setStatus(true, u"Showing your edited copy from the mods folder: %1"_s.arg(QDir::toNativeSeparators(modsCopy)));
}

// ---------------------------------------------------------------------------
// Edit: replace WEMs, Save Changes
// ---------------------------------------------------------------------------

QString BnkToolManager::modsTarget() const {
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    return orchestrator && !m_workspacePath.isEmpty() ? QDir::toNativeSeparators(orchestrator->modFilePath(m_workspacePath))
                                                      : QString();
}

bool BnkToolManager::replaceEntry(int entry, const QString& file) {
    if (m_busy || entry < 0 || entry >= m_bank.media.size())
        return false;
    const BnkFile::Media& media = m_bank.media[entry];
    const QString name = wemFileName(media.id);
    const QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix != u"wav"_s && suffix != u"wem"_s) {
        setStatus(false, u"%1 can be replaced with a .wav or a .wem (not %2)."_s.arg(name, QFileInfo(file).fileName()));
        return false;
    }
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Could not read %1."_s.arg(QDir::toNativeSeparators(file)));
        return false;
    }
    const QByteArray original = m_data.mid(media.offset, media.size);
    QString error;
    const QByteArray wem = suffix == u"wav"_s ? WemFile::fromWav(input.readAll(), original, &error)
                                              : WemFile::withOriginalHash(input.readAll(), original, &error);
    if (wem.isEmpty()) {
        setStatus(false, u"%1: %2"_s.arg(QFileInfo(file).fileName(), error));
        return false;
    }

    m_replacements.insert(entry, wem);
    m_unsavedChanges = true;
    m_infoCache.remove(entry);
    emit changesChanged();
    if (entry == m_currentEntry) {
        m_entryInfo = buildEntryInfo(entry);
        emit currentChanged();
    }
    setStatus(true, u"Replaced %1 with %2 (%3) - Save Changes to write the bank into the mods folder."_s
                        .arg(name, QFileInfo(file).fileName(), WemFile::describe(wem)));
    return true;
}

bool BnkToolManager::replaceRow(int row, const QString& pathOrUrl) {
    return replaceEntry(m_model->entryAt(row), toLocalPath(pathOrUrl));
}

int BnkToolManager::replaceByName(const QStringList& pathsOrUrls) {
    int replaced = 0;
    QStringList unmatched;
    for (const QString& item : pathsOrUrls) {
        const QString file = toLocalPath(item);
        const QString base = QFileInfo(file).completeBaseName();
        int entry = -1;
        for (int i = 0; i < m_bank.media.size() && entry < 0; ++i)
            if (QString::number(m_bank.media[i].id) == base)
                entry = i;
        if (entry < 0)
            unmatched << QFileInfo(file).fileName();
        else if (replaceEntry(entry, file))
            ++replaced;
    }
    if (!unmatched.isEmpty())
        setStatus(false, u"No WEM with the id of %1 in this bank - name the file <id>.wav / <id>.wem, or drop it onto "
                         u"the WEM it should replace."_s.arg(unmatched.join(u", "_s)));
    else if (replaced > 1)
        setStatus(true, u"Replaced %1 WEMs - Save Changes to write the bank into the mods folder."_s.arg(replaced));
    return replaced;
}

void BnkToolManager::revertRow(int row) {
    const int entry = m_model->entryAt(row);
    if (m_busy || !m_replacements.remove(entry))
        return;
    m_unsavedChanges = true;
    m_infoCache.remove(entry);
    emit changesChanged();
    if (entry == m_currentEntry) {
        m_entryInfo = buildEntryInfo(entry);
        emit currentChanged();
    }
    setStatus(true, u"%1 is back to the bank's own audio."_s.arg(wemFileName(m_bank.media[entry].id)));
}

void BnkToolManager::discardChanges() {
    if (m_busy || m_replacements.isEmpty())
        return;
    m_replacements.clear();
    m_unsavedChanges = false;
    m_infoCache.clear();
    emit changesChanged();
    if (m_currentEntry >= 0) {
        m_entryInfo = buildEntryInfo(m_currentEntry);
        emit currentChanged();
    }
}

void BnkToolManager::saveChanges() {
    if (m_busy)
        return;
    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator || m_workspacePath.isEmpty()) {
        setStatus(false, u"Save Changes needs a sound bank opened from the game's files (double click it in the workspace)."_s);
        return;
    }
    if (m_replacements.isEmpty()) {
        setStatus(false, u"Nothing to save: replace a WEM first (drag a .wav / .wem onto it, or right click > Replace)."_s);
        return;
    }

    int patched = 0;
    QString error;
    const QByteArray rebuilt = BnkFile::rebuild(m_data, m_bank, m_replacements, &patched, &error);
    if (rebuilt.isEmpty()) {
        setStatus(false, u"Save Changes: "_s + error);
        return;
    }
    const QVariantMap written = orchestrator->writeModFile(m_workspacePath, rebuilt);
    if (!written.value(u"ok"_s).toBool()) {
        setStatus(false, u"Save Changes: "_s + written.value(u"message"_s).toString());
        return;
    }
    m_unsavedChanges = false;
    emit changesChanged();
    const QString target = modsTarget();
    const QString message = u"Saved the bank to %1 (%2 WEM(s) replaced, %3 sound source(s) updated). Click Modify to put "
                            u"it into the game."_s.arg(target).arg(m_replacements.size()).arg(patched);
    emit orchestrator->uiConsoleUpdate(message);
    setStatus(true, message);
    emit changesSaved(target);
}

void BnkToolManager::loadFile(const QString& path, const QString& sourceName) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        failLoad(u"Failed to read the sound bank: "_s + path);
        return;
    }

    QByteArray data = file.readAll();
    file.close();
    loadBytes(std::move(data), sourceName);
}

void BnkToolManager::loadBytes(QByteArray data, const QString& sourceName) {
    clearBank();

    BnkFile bank;
    QString error;
    if (!bank.parse(data, &error)) {
        failLoad(error);
        return;
    }

    m_data = std::move(data);
    m_bank = std::move(bank);
    m_sourceName = sourceName;

    rebuildBankInfo();
    emit bankChanged();
    rebuildRows();

    if (m_bank.media.isEmpty())
        setStatus(true, u"%1 has no embedded audio (its WEM files are streamed separately)."_s.arg(sourceName));
    else
        setStatus(true, u"Loaded %1 embedded WEM file(s) from %2."_s.arg(m_bank.media.size()).arg(sourceName));
}

void BnkToolManager::failLoad(const QString& message) {
    clearBank();
    emit bankChanged();
    rebuildRows();
    setStatus(false, message);
}

void BnkToolManager::clearBank() {
    const bool hadChanges = !m_replacements.isEmpty() || m_unsavedChanges || !m_workspacePath.isEmpty();
    m_replacements.clear();
    m_unsavedChanges = false;
    m_workspacePath.clear();
    if (hadChanges)
        emit changesChanged();
    m_data.clear();
    m_bank = BnkFile();
    m_sourceName.clear();
    m_bankInfo.clear();
    m_infoCache.clear();
    setProgress(0);
}

void BnkToolManager::rebuildBankInfo() {
    const BnkFile& b = m_bank;
    m_bankInfo.clear();

    m_bankInfo << infoRow(u"Source"_s, m_sourceName);
    if (!b.bankName.isEmpty())
        m_bankInfo << infoRow(u"Bank name"_s, b.bankName);

    m_bankInfo << infoRow(u"Bank ID"_s, idText(b.bankId))
               << infoRow(u"Bank version"_s, QString::number(b.version))
               << infoRow(u"Byte order"_s, b.isBigEndian ? u"Big endian"_s : u"Little endian"_s)
               << infoRow(u"File size"_s, sizeWithBytes(m_data.size()))
               << infoRow(u"Chunks"_s, b.chunks.join(u", "_s));

    m_bankInfo << infoRow(u"Embedded audio"_s,
                          b.media.isEmpty() ? u"None (streamed bank)"_s
                                            : u"%1 WEM file(s), %2"_s.arg(b.media.size()).arg(humanSize(b.dataSize)));

    if (b.hircObjects > 0)
        m_bankInfo << infoRow(u"Hierarchy objects"_s,
                              u"%1  (%2 sounds, %3 events)"_s.arg(b.hircObjects).arg(b.hircSounds).arg(b.hircEvents));

    if (b.truncated)
        m_bankInfo << infoRow(u"Warning"_s, u"The bank ends in the middle of a chunk."_s);
}

// ---------------------------------------------------------------------------
// List / selection
// ---------------------------------------------------------------------------

void BnkToolManager::setFilterText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_filterText == trimmed)
        return;

    m_filterText = trimmed;
    emit filterTextChanged();
    rebuildRows();
}

void BnkToolManager::rebuildRows() {
    QList<int> rows;
    rows.reserve(m_bank.media.size());

    for (int i = 0; i < m_bank.media.size(); ++i) {
        const quint32 id = m_bank.media[i].id;
        if (m_filterText.isEmpty()
            || wemFileName(id).contains(m_filterText, Qt::CaseInsensitive)
            || hexId(id).contains(m_filterText, Qt::CaseInsensitive)) {
            rows.append(i);
        }
    }

    m_visibleCount = static_cast<int>(rows.size());
    m_model->reset(m_bank.media, rows);

    m_currentEntry = -1;
    m_entryInfo.clear();
    emit currentChanged();
    emit filterApplied();
}

void BnkToolManager::selectRow(int row) {
    const int entry = m_model->entryAt(row);
    if (entry == m_currentEntry)
        return;

    m_currentEntry = entry;
    m_entryInfo = entry >= 0 ? buildEntryInfo(entry) : QVariantList();
    emit currentChanged();

    if (entry >= 0)
        setStatus(true, u"Selected: %1 (%2)"_s.arg(currentName(), humanSize(m_bank.media[entry].size)));
}

QVariantList BnkToolManager::buildEntryInfo(int entry) {
    const auto cached = m_infoCache.constFind(entry);
    if (cached != m_infoCache.constEnd())
        return cached.value();

    const BnkFile::Media& media = m_bank.media[entry];

    QVariantList info;
    info << infoRow(u"File"_s, wemFileName(media.id))
         << infoRow(u"Wwise ID"_s, idText(media.id))
         << infoRow(u"Entry"_s, u"%1 of %2"_s.arg(entry + 1).arg(m_bank.media.size()))
         << infoRow(u"Offset in bank"_s, u"0x"_s + QString::number(media.offset, 16).toUpper())
         << infoRow(u"Size"_s, m_replacements.contains(entry)
                                   ? sizeWithBytes(m_replacements.value(entry).size()) + u"  (replaced, was "_s
                                         + humanSize(media.size) + u")"_s
                                   : sizeWithBytes(media.size));

    Metadata meta;
    if (AudioProcessor::probeAudioBuffer(wemBytes(entry), wemFileName(media.id), meta)) {
        info << infoRow(u"Codec"_s, meta.codecName)
             << infoRow(u"Channels"_s, channelText(meta.channels))
             << infoRow(u"Sample rate"_s, u"%1 Hz"_s.arg(meta.sampleRate));

        if (meta.sampleRate > 0 && meta.totalSamples > 0) {
            const double seconds = static_cast<double>(meta.totalSamples) / meta.sampleRate;
            info << infoRow(u"Duration"_s, u"%1  (%2 samples)"_s.arg(formatDuration(seconds))
                                                               .arg(QLocale().toString(meta.totalSamples)));
            info << infoRow(u"Average bitrate"_s,
                            u"%1 kbps"_s.arg(qRound(media.size * 8.0 / seconds / 1000.0)));
        }

        info << infoRow(u"Loop"_s, meta.hasLoopPoints
                                       ? u"%1 - %2 (samples)"_s.arg(meta.loopStartSample).arg(meta.loopEndSample)
                                       : u"None"_s);
    } else {
        info << infoRow(u"Format"_s, u"Unknown - vgmstream couldn't read this WEM"_s);
    }

    m_infoCache.insert(entry, info);
    return info;
}

QByteArray BnkToolManager::wemBytes(int entry) const {
    if (entry < 0 || entry >= m_bank.media.size())
        return {};
    if (m_replacements.contains(entry))
        return m_replacements.value(entry);   // Play / probe the replacement.

    const BnkFile::Media& media = m_bank.media[entry];
    return m_data.mid(media.offset, media.size);
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

void BnkToolManager::extractSelected(const QString& outDirPath) {
    if (m_busy)
        return;

    if (m_currentEntry < 0) {
        setStatus(false, u"Select an audio file first."_s);
        return;
    }

    const QString outDir = toLocalPath(outDirPath);
    const BnkFile::Media& media = m_bank.media[m_currentEntry];
    const QString error = writeWem(m_data, media, outDir);

    if (error.isEmpty())
        setStatus(true, u"Extracted: "_s + QDir(outDir).filePath(wemFileName(media.id)));
    else
        setStatus(false, error);
}

void BnkToolManager::extractAll(const QString& outDirPath) {
    if (m_busy)
        return;

    const QList<BnkFile::Media> media = m_bank.media;
    if (media.isEmpty()) {
        setStatus(false, u"There is no embedded audio to extract."_s);
        return;
    }

    const QString outDir = toLocalPath(outDirPath);
    const QByteArray data = m_data; // Shared copy: a reload can't pull the bytes out from under the worker.

    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Extracting %1 WEM file(s)..."_s.arg(media.size()));

    m_extractFuture = QtConcurrent::run([this, data, media, outDir]() {
        const int total = static_cast<int>(media.size());
        int failed = 0;
        QString firstError;
        int lastPercent = -1;

        for (int i = 0; i < total && !m_cancel; ++i) {
            const QString error = writeWem(data, media[i], outDir);
            if (!error.isEmpty()) {
                ++failed;
                if (firstError.isEmpty())
                    firstError = error;
            }

            const int percent = static_cast<int>((static_cast<qint64>(i) + 1) * 100 / total);
            if (percent != lastPercent) {
                lastPercent = percent;
                QMetaObject::invokeMethod(this, [this, percent]() { setProgress(percent); }, Qt::QueuedConnection);
            }
        }

        if (m_cancel)
            return;

        const bool ok = failed == 0;
        const QString summary = ok ? u"Extracted %1 WEM file(s) to %2."_s.arg(total).arg(outDir)
                                   : u"Extracted %1 of %2 WEM file(s). %3 failed - first error: %4"_s
                                         .arg(total - failed).arg(total).arg(failed).arg(firstError);

        QMetaObject::invokeMethod(this, [this, ok, summary]() {
            setBusy(false);
            setStatus(ok, summary);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void BnkToolManager::reportMessage(const QString& message) {
    setStatus(false, message);
}

void BnkToolManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void BnkToolManager::setStatus(bool ok, const QString& message) {
    if (m_statusOk == ok && m_statusText == message)
        return;
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}

void BnkToolManager::setProgress(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (m_progress == percent)
        return;
    m_progress = percent;
    emit progressChanged();
}
