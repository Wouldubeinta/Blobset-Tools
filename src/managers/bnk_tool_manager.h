#ifndef BNK_TOOL_MANAGER_H
#define BNK_TOOL_MANAGER_H

#include "bnk_file.h"

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

/// The audio file list of the sound bank viewer (only the entries that match the filter).
class BnkEntryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by BnkToolManager")

public:
    enum Roles { NameRole = Qt::UserRole + 1, WemIdRole, SizeTextRole, EntryRole };

    explicit BnkEntryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QList<BnkFile::Media>& media, const QList<int>& rows);
    int entryAt(int row) const; // Index into the bank's media list, -1 for a bad row.

private:
    QList<BnkFile::Media> m_media;
    QList<int> m_rows;
};

class BnkToolManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // --- Loaded bank --------------------------------------------------------------------------------------
    Q_PROPERTY(BnkEntryModel* model READ model CONSTANT)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY bankChanged)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY bankChanged)
    Q_PROPERTY(QVariantList bankInfo READ bankInfo NOTIFY bankChanged)          // [{ label, value }]
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY filterApplied)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

    // --- Selection ----------------------------------------------------------------------------------------
    Q_PROPERTY(int currentEntry READ currentEntry NOTIFY currentChanged)        // Index into the bank's media.
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QVariantList entryInfo READ entryInfo NOTIFY currentChanged)     // [{ label, value }]

    // --- Status bar ---------------------------------------------------------------------------------------
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit BnkToolManager(QObject* parent = nullptr);
    ~BnkToolManager() override;

    BnkEntryModel* model() const { return m_model; }
    QString sourceName() const { return m_sourceName; }
    int entryCount() const { return static_cast<int>(m_bank.media.size()); }
    QVariantList bankInfo() const { return m_bankInfo; }
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

    // Opens a .bnk from disk.
    Q_INVOKABLE void loadBnk(const QString& path);

    // Opens the .bnk registered under that path hash in the loaded (V4) blobset.
    Q_INVOKABLE void loadBnkFromBlobset(const QString& hexHash);

    // Selects row `row` of the (filtered) list; -1 clears.
    Q_INVOKABLE void selectRow(int row);

    // The raw bytes of embedded WEM `entry` (for playback).
    Q_INVOKABLE QByteArray wemBytes(int entry) const;

    Q_INVOKABLE void extractSelected(const QString& outDirPath);
    Q_INVOKABLE void extractAll(const QString& outDirPath);

    // Shows a message (e.g. from the audio player) in the status bar.
    Q_INVOKABLE void reportMessage(const QString& message);

signals:
    void bankChanged();
    void filterApplied();       // The list was rebuilt (load or filter change).
    void filterTextChanged();
    void currentChanged();
    void busyChanged();
    void statusChanged();
    void progressChanged();

private:
    void loadFile(const QString& path, const QString& sourceName);
    void loadBytes(QByteArray data, const QString& sourceName);   // Parses an in-memory bank.
    void failLoad(const QString& message);
    void clearBank();
    void rebuildBankInfo();
    void rebuildRows();
    QVariantList buildEntryInfo(int entry);

    void setBusy(bool busy);
    void setStatus(bool ok, const QString& message);
    void setProgress(int percent);

    BnkEntryModel* m_model = nullptr;
    QByteArray m_data;
    BnkFile m_bank;
    QString m_sourceName;
    QVariantList m_bankInfo;

    QString m_filterText;
    int m_visibleCount = 0;

    int m_currentEntry = -1;
    QVariantList m_entryInfo;
    QHash<int, QVariantList> m_infoCache; // Entry -> probed info, so re-selecting is instant.

    QString m_statusText;
    bool m_statusOk = true;
    int m_progress = 0;

    bool m_busy = false;
    std::atomic<bool> m_cancel{false};
    QFuture<void> m_extractFuture;
};

#endif // BNK_TOOL_MANAGER_H
