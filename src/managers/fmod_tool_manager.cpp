#include "fmod_tool_manager.h"

#include "app_orchestrator.h"
#include "audio_decoder.h"
#include "audio_player.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// vgmstream picks the parser from the extension; the manager always hands it the bare FSB.
const QString kVirtualName = u"fmod_preview.fsb"_s;

struct ExtractJob {
    int subsong = 0;    // 1-based vgmstream subsong.
    QString path;       // Output .wav file.
};

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

QString hexValue(qint64 value) {
    return u"0x"_s + QString::number(value, 16).toUpper();
}

QString formatDuration(double seconds) {
    const qint64 totalMs = qRound64(seconds * 1000.0);
    return u"%1:%2.%3"_s.arg(totalMs / 60000)
        .arg((totalMs / 1000) % 60, 2, 10, QChar(u'0'))
        .arg(totalMs % 1000, 3, 10, QChar(u'0'));
}

QString channelText(quint32 channels) {
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

// Sample names come from the bank: make them safe to use as a file name.
QString safeFileName(const QString& name) {
    QString result;
    result.reserve(name.size());
    for (const QChar c : name) {
        const bool invalid = c.unicode() < 0x20 || QStringView(u"<>:\"/\\|?*").contains(c);
        result.append(invalid ? QChar(u'_') : c);
    }

    result = result.trimmed();
    while (result.endsWith(u'.') || result.endsWith(u' '))
        result.chop(1);
    return result.isEmpty() ? u"sample"_s : result;
}

} // namespace

// ==============================================================================================================
// FmodEntryModel
// ==============================================================================================================

int FmodEntryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant FmodEntryModel::data(const QModelIndex& index, int role) const {
    const int entry = index.isValid() ? entryAt(index.row()) : -1;
    if (entry < 0)
        return {};

    const FsbFile::Sample& sample = m_samples[entry];
    switch (role) {
    case NameRole:     return sample.name;
    case SizeTextRole: return humanSize(sample.dataSize);
    case EntryRole:    return entry;
    default:           return {};
    }
}

QHash<int, QByteArray> FmodEntryModel::roleNames() const {
    return {
        {NameRole, "name"},
        {SizeTextRole, "sizeText"},
        {EntryRole, "entry"},
    };
}

void FmodEntryModel::reset(const QList<FsbFile::Sample>& samples, const QList<int>& rows) {
    beginResetModel();
    m_samples = samples;
    m_rows = rows;
    endResetModel();
}

int FmodEntryModel::entryAt(int row) const {
    return (row >= 0 && row < m_rows.size()) ? m_rows[row] : -1;
}

// ==============================================================================================================
// FmodToolManager
// ==============================================================================================================

FmodToolManager::FmodToolManager(QObject* parent)
    : QObject(parent), m_model(new FmodEntryModel(this)) {}

FmodToolManager::~FmodToolManager() {
    m_cancel = true;
    m_extractFuture.waitForFinished(); // The worker still reports back to this object.
}

QString FmodToolManager::currentName() const {
    return m_currentEntry >= 0 ? m_file.samples[m_currentEntry].name : QString();
}

QString FmodToolManager::appVersion() const {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QString() : u"v"_s + version;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void FmodToolManager::loadFmod(const QString& path) {
    if (m_busy)
        return;

    const QString local = toLocalPath(path);
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        failLoad(u"Failed to read the FMOD file: "_s + local);
        return;
    }

    loadData(file.readAll(), QFileInfo(local).fileName());
}

void FmodToolManager::loadFmodFromBlobset(const QString& hexHash) {
    if (m_busy)
        return;

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        failLoad(u"This FMOD file (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return;
    }

    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        failLoad(u"The application orchestrator is unavailable."_s);
        return;
    }

    QByteArray data;
    try {
        data = BlobsetAssetReader::read(entry, orchestrator->gameLocation(), orchestrator->blobsetVersion(),
                                        orchestrator->platform());
    } catch (const std::exception& ex) {
        failLoad(QString::fromUtf8(ex.what()));
        return;
    }

    loadData(data, u"0x"_s + hexHash.toUpper());
}

void FmodToolManager::loadData(const QByteArray& data, const QString& sourceName) {
    clearFile();

    try {
        FsbFile file;
        QString error;
        if (!file.parse(data, &error)) {
            failLoad(error);
            return;
        }

        m_fsb = file.hasFsb() ? data.mid(file.fsbOffset, file.fsbSize) : QByteArray();
        m_fileSize = data.size();
        m_file = std::move(file);
        m_sourceName = sourceName;
    } catch (const std::exception& ex) {
        failLoad(u"Failed to read %1: %2"_s.arg(sourceName, QString::fromUtf8(ex.what())));
        return;
    }

    rebuildFileInfo();
    emit fileChanged();
    rebuildRows();

    if (!m_file.samples.isEmpty())
        setStatus(true, u"Loaded %1 sample(s) from %2."_s.arg(m_file.samples.size()).arg(sourceName));
    else if (!m_file.note.isEmpty())
        setStatus(true, u"%1: %2"_s.arg(sourceName, m_file.note));
    else
        setStatus(true, u"%1 has no samples."_s.arg(sourceName));
}

void FmodToolManager::failLoad(const QString& message) {
    clearFile();
    emit fileChanged();
    rebuildRows();
    setStatus(false, message);
}

void FmodToolManager::clearFile() {
    m_fsb.clear();
    m_fileSize = 0;
    m_file = FsbFile();
    m_sourceName.clear();
    m_fileInfo.clear();
    m_infoCache.clear();
    setProgress(0);
}

void FmodToolManager::rebuildFileInfo() {
    const FsbFile& f = m_file;
    const bool isBank = f.container == FsbFile::Container::StudioBank;
    const bool isLegacy = f.container == FsbFile::Container::Fsb3 || f.container == FsbFile::Container::Fsb4;

    m_fileInfo.clear();
    m_fileInfo << infoRow(u"Source"_s, m_sourceName)
               << infoRow(u"Format"_s, f.formatName);

    if (!f.note.isEmpty())
        m_fileInfo << infoRow(u"Note"_s, f.note);

    if (isBank) {
        m_fileInfo << infoRow(u"Bank format version"_s, QString::number(f.bankVersion));
        if (!f.chunks.isEmpty())
            m_fileInfo << infoRow(u"Chunks"_s, f.chunks.join(u", "_s));
        if (f.hasFsb())
            m_fileInfo << infoRow(u"FSB offset in bank"_s, hexValue(f.fsbOffset));
        if (f.truncated)
            m_fileInfo << infoRow(u"Warning"_s, u"The bank ends in the middle of a chunk."_s);
    }

    m_fileInfo << infoRow(u"File size"_s, sizeWithBytes(m_fileSize));

    if (f.hasFsb()) {
        if (isBank)
            m_fileInfo << infoRow(u"FSB size"_s, sizeWithBytes(f.fsbSize));
        if (!f.codecName.isEmpty())
            m_fileInfo << infoRow(u"Codec"_s, f.codecName);

        m_fileInfo << infoRow(u"Samples"_s, QString::number(f.samples.size()))
                   << infoRow(u"Sample data"_s, sizeWithBytes(f.sampleDataSize));

        if (isLegacy)
            m_fileInfo << infoRow(u"Header flags"_s, hexValue(f.headerFlags));
    }
}

// ---------------------------------------------------------------------------
// List / selection
// ---------------------------------------------------------------------------

void FmodToolManager::setFilterText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_filterText == trimmed)
        return;

    m_filterText = trimmed;
    emit filterTextChanged();
    rebuildRows();
}

void FmodToolManager::rebuildRows() {
    QList<int> rows;
    rows.reserve(m_file.samples.size());

    for (int i = 0; i < m_file.samples.size(); ++i) {
        if (m_filterText.isEmpty() || m_file.samples[i].name.contains(m_filterText, Qt::CaseInsensitive))
            rows.append(i);
    }

    m_visibleCount = static_cast<int>(rows.size());
    m_model->reset(m_file.samples, rows);

    m_currentEntry = -1;
    m_entryInfo.clear();
    emit currentChanged();
    emit filterApplied();
}

void FmodToolManager::selectRow(int row) {
    const int entry = m_model->entryAt(row);
    if (entry == m_currentEntry)
        return;

    m_currentEntry = entry;
    m_entryInfo = entry >= 0 ? buildEntryInfo(entry) : QVariantList();
    emit currentChanged();

    if (entry >= 0)
        setStatus(true, u"Selected: %1 (%2)"_s.arg(currentName(), humanSize(m_file.samples[entry].dataSize)));
}

QVariantList FmodToolManager::buildEntryInfo(int entry) {
    const auto cached = m_infoCache.constFind(entry);
    if (cached != m_infoCache.constEnd())
        return cached.value();

    const FsbFile::Sample& s = m_file.samples[entry];

    QVariantList info;
    info << infoRow(u"Name"_s, s.name)
         << infoRow(u"Subsong"_s, u"%1 of %2"_s.arg(entry + 1).arg(m_file.samples.size()));
    if (s.dataOffset >= 0)
        info << infoRow(u"Offset in FSB"_s, hexValue(s.dataOffset));
    info << infoRow(u"Data size"_s, sizeWithBytes(s.dataSize));

    // vgmstream knows the codec and the exact length; the header values are the fallback.
    Metadata meta;
    const bool probed = !m_fsb.isEmpty()
                        && AudioProcessor::probeAudioBuffer(m_fsb, kVirtualName, meta, entry + 1);

    const quint32 channels = probed ? static_cast<quint32>(meta.channels) : s.channels;
    const quint32 rate = probed ? static_cast<quint32>(meta.sampleRate) : s.sampleRate;
    const quint32 samples = probed ? static_cast<quint32>(meta.totalSamples) : s.sampleCount;
    const bool hasLoop = probed ? meta.hasLoopPoints : s.hasLoop;
    const quint32 loopStart = probed ? static_cast<quint32>(meta.loopStartSample) : s.loopStart;
    const quint32 loopEnd = probed ? static_cast<quint32>(meta.loopEndSample) : s.loopEnd;

    if (probed)
        info << infoRow(u"Codec"_s, meta.codecName);
    else
        info << infoRow(u"Codec"_s, (m_file.codecName.isEmpty() ? u"Unknown"_s : m_file.codecName)
                                        + u" - vgmstream couldn't read this sample"_s);

    if (channels > 0)
        info << infoRow(u"Channels"_s, channelText(channels));
    if (rate > 0)
        info << infoRow(u"Sample rate"_s, u"%1 Hz"_s.arg(rate));

    if (rate > 0 && samples > 0) {
        const double seconds = static_cast<double>(samples) / rate;
        info << infoRow(u"Duration"_s, u"%1  (%2 samples)"_s.arg(formatDuration(seconds), QLocale().toString(samples)));
        if (s.dataSize > 0)
            info << infoRow(u"Average bitrate"_s, u"%1 kbps"_s.arg(qRound(s.dataSize * 8.0 / seconds / 1000.0)));
    }

    info << infoRow(u"Loop"_s, hasLoop ? u"%1 - %2 (samples)"_s.arg(loopStart).arg(loopEnd) : u"None"_s);

    m_infoCache.insert(entry, info);
    return info;
}

void FmodToolManager::playSelected(QObject* player) {
    auto* audio = qobject_cast<AudioPlayer*>(player);
    if (!audio || m_currentEntry < 0 || m_fsb.isEmpty())
        return;

    audio->playAudioData(m_fsb, kVirtualName, m_currentEntry + 1);
}

// ---------------------------------------------------------------------------
// Extraction (decoded to .wav)
// ---------------------------------------------------------------------------

void FmodToolManager::extractSelected(const QString& outDirPath) {
    if (m_currentEntry < 0) {
        setStatus(false, u"Select a sample first."_s);
        return;
    }
    startExtraction({m_currentEntry}, outDirPath);
}

void FmodToolManager::extractAll(const QString& outDirPath) {
    QList<int> entries;
    entries.reserve(m_file.samples.size());
    for (int i = 0; i < m_file.samples.size(); ++i)
        entries.append(i);

    if (entries.isEmpty()) {
        setStatus(false, u"There are no samples to extract."_s);
        return;
    }
    startExtraction(entries, outDirPath);
}

void FmodToolManager::startExtraction(const QList<int>& entries, const QString& outDirPath) {
    if (m_busy || entries.isEmpty() || m_fsb.isEmpty())
        return;

    const QString outDir = toLocalPath(outDirPath);
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        setStatus(false, u"Could not create the output folder: "_s + outDir);
        return;
    }

    // File names are worked out up front, so duplicate sample names don't overwrite each other.
    QList<ExtractJob> jobs;
    jobs.reserve(entries.size());
    QSet<QString> used;

    for (const int entry : entries) {
        const QString base = safeFileName(m_file.samples[entry].name);
        QString name = base + u".wav"_s;
        for (int n = 2; used.contains(name.toLower()); ++n)
            name = u"%1 (%2).wav"_s.arg(base).arg(n);

        used.insert(name.toLower());
        jobs.append({entry + 1, QDir(outDir).filePath(name)});
    }

    const QByteArray fsb = m_fsb; // Shared copy: a reload can't pull the bytes out from under the worker.

    m_cancel = false;
    setProgress(0);
    setBusy(true);
    setStatus(true, u"Decoding %1 sample(s) to WAV..."_s.arg(jobs.size()));

    m_extractFuture = QtConcurrent::run([this, fsb, jobs, outDir]() {
        const int total = static_cast<int>(jobs.size());
        int failed = 0;
        QString firstError;
        int lastPercent = -1;

        for (int i = 0; i < total && !m_cancel; ++i) {
            const ExtractJob& job = jobs[i];
            QString error;

            DecodedTrack track;
            if (!AudioProcessor::decodeSubsongToWav(fsb, kVirtualName, job.subsong, track)) {
                error = u"%1: vgmstream couldn't decode this sample."_s.arg(QFileInfo(job.path).fileName());
            } else {
                QFile out(job.path);
                if (!out.open(QIODevice::WriteOnly) || out.write(track.wavData) != track.wavData.size())
                    error = u"%1: %2"_s.arg(job.path, out.errorString());
            }

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
        QString summary;
        if (ok && total == 1)
            summary = u"Extracted: "_s + jobs.first().path;
        else if (ok)
            summary = u"Extracted %1 sample(s) as WAV to %2."_s.arg(total).arg(outDir);
        else
            summary = u"Extracted %1 of %2 sample(s). %3 failed - first error: %4"_s
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

void FmodToolManager::reportMessage(const QString& message) {
    setStatus(false, message);
}

void FmodToolManager::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void FmodToolManager::setStatus(bool ok, const QString& message) {
    if (m_statusOk == ok && m_statusText == message)
        return;
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
}

void FmodToolManager::setProgress(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (m_progress == percent)
        return;
    m_progress = percent;
    emit progressChanged();
}
