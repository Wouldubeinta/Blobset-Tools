#ifndef FMOD_TOOL_MANAGER_H
#define FMOD_TOOL_MANAGER_H

#include "fsb_file.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <atomic>

/// The sample list of the FMOD viewer (only the samples that match the filter).
class FmodEntryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FmodToolManager")

public:
    enum Roles { NameRole = Qt::UserRole + 1, SizeTextRole, EntryRole };

    explicit FmodEntryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QList<FsbFile::Sample>& samples, const QList<int>& rows);
    int entryAt(int row) const; // Index into the sample list, -1 for a bad row.

private:
    QList<FsbFile::Sample> m_samples;
    QList<int> m_rows;
};

/// Backs FMODViewer.qml: FMOD Studio banks (.bank) and FSB3/4/5 sound banks (.fsb).
class FmodToolManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // --- Loaded file --------------------------------------------------------------------------------------
    Q_PROPERTY(FmodEntryModel* model READ model CONSTANT)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY fileChanged)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY fileChanged)
    Q_PROPERTY(QVariantList fileInfo READ fileInfo NOTIFY fileChanged)          // [{ label, value }]
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY filterApplied)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

    // --- Selection ----------------------------------------------------------------------------------------
    Q_PROPERTY(int currentEntry READ currentEntry NOTIFY currentChanged)        // Index into the sample list.
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QVariantList entryInfo READ entryInfo NOTIFY currentChanged)     // [{ label, value }]

    // --- Status bar ---------------------------------------------------------------------------------------
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit FmodToolManager(QObject* parent = nullptr);
    ~FmodToolManager() override;

    FmodEntryModel* model() const { return m_model; }
    QString sourceName() const { return m_sourceName; }
    int entryCount() const { return static_cast<int>(m_file.samples.size()); }
    QVariantList fileInfo() const { return m_fileInfo; }
    int visibleCount() const { return m_visibleCount; }

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);

    int currentEntry() const { return m_currentEntry; }
    QString currentName() const;
    QVariantList entryInfo() const { return m_entryInfo; }

    bool busy() const { return m_busy; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }
    int progress() const { return m_progress; }
    QString appVersion() const;

    // Opens a .bank / .fsb from disk.
    Q_INVOKABLE void loadFmod(const QString& path);

    // Opens the .bank / .fsb registered under that path hash in the loaded blobset (V1, V2 or V4).
    Q_INVOKABLE void loadFmodFromBlobset(const QString& hexHash);

    // Selects row `row` of the (filtered) list; -1 clears.
    Q_INVOKABLE void selectRow(int row);

    // Plays the selected sample on `player` (the "audioPlayer" context object). The FSB is shared, not copied.
    Q_INVOKABLE void playSelected(QObject* player);

    // Decode samples to .wav files (on a worker thread).
    Q_INVOKABLE void extractSelected(const QString& outDirPath);
    Q_INVOKABLE void extractAll(const QString& outDirPath);

    // Shows a message (e.g. from the audio player) in the status bar.
    Q_INVOKABLE void reportMessage(const QString& message);

signals:
    void fileChanged();
    void filterApplied();       // The list was rebuilt (load or filter change).
    void filterTextChanged();
    void currentChanged();
    void busyChanged();
    void statusChanged();
    void progressChanged();

private:
    void loadData(const QByteArray& data, const QString& sourceName);
    void failLoad(const QString& message);
    void clearFile();
    void rebuildFileInfo();
    void rebuildRows();
    QVariantList buildEntryInfo(int entry);
    void startExtraction(const QList<int>& entries, const QString& outDirPath);

    void setBusy(bool busy);
    void setStatus(bool ok, const QString& message);
    void setProgress(int percent);

    FmodEntryModel* m_model = nullptr;
    QByteArray m_fsb;           // Just the FSB (for a .bank: the FSB5 inside it) - what vgmstream is given.
    qint64 m_fileSize = 0;
    FsbFile m_file;
    QString m_sourceName;
    QVariantList m_fileInfo;

    QString m_filterText;
    int m_visibleCount = 0;

    int m_currentEntry = -1;
    QVariantList m_entryInfo;
    QHash<int, QVariantList> m_infoCache;

    QString m_statusText;
    bool m_statusOk = true;
    int m_progress = 0;

    bool m_busy = false;
    std::atomic<bool> m_cancel{false};
    QFuture<void> m_extractFuture;
};

#endif // FMOD_TOOL_MANAGER_H
