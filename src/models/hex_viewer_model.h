#ifndef HEX_VIEWER_MODEL_H
#define HEX_VIEWER_MODEL_H

#include <QAbstractListModel>
#include <QByteArray>
#include <QFuture>
#include <QQmlEngine>
#include <QString>

/// One row = 16 bytes of the loaded data (address, hex dump, ASCII).
///
/// Data comes either from a blobset asset (decompressed main + vram sections, via BlobsetAssetReader)
/// or from a byte range of a file on disk. Loading runs on a worker thread so big assets don't freeze the UI.
class HexViewerModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY contentChanged)
    Q_PROPERTY(qint64 dataSize READ dataSize NOTIFY contentChanged)
    Q_PROPERTY(qint64 mainSize READ mainSize NOTIFY contentChanged)     // -1 when there are no sections.
    Q_PROPERTY(qint64 baseOffset READ baseOffset NOTIFY contentChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)

public:
    enum HexRoles {
        AddressRole = Qt::UserRole + 1,
        HexDataRole,
        AsciiDataRole,
        IsVramRole      // The row starts inside the vram section.
    };

    static constexpr int kBytesPerRow = 16;

    explicit HexViewerModel(QObject* parent = nullptr);
    ~HexViewerModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool busy() const { return m_busy; }
    QString sourceName() const { return m_sourceName; }
    qint64 dataSize() const { return m_data.size(); }
    qint64 mainSize() const { return m_mainSize; }
    qint64 baseOffset() const { return m_baseOffset; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // Reads `size` bytes at `offset` of a file on disk (size <= 0: up to the end of the file).
    Q_INVOKABLE void loadData(const QString& filePath, qint64 offset = 0, qint64 size = 0);

    // Reads the asset registered under that path hash in the loaded blobset (main section + vram section).
    Q_INVOKABLE void loadFromBlobset(const QString& hexHash, const QString& displayName = QString());

    // Row that holds the (displayed) address `offset`, -1 when it is outside of the data.
    Q_INVOKABLE int rowForOffset(qint64 offset) const;

    // Shows a message in the status bar.
    Q_INVOKABLE void reportMessage(const QString& message, bool ok = false);

    Q_INVOKABLE void clear();

signals:
    void errorOccurred(const QString& message);
    void contentChanged();
    void busyChanged();
    void statusChanged();

private:
    struct LoadResult {
        QByteArray data;
        qint64 baseOffset = 0;
        qint64 mainSize = -1;
        QString error;
    };

    void startLoad(const QFuture<LoadResult>& future, const QString& sourceName);
    void applyResult(const LoadResult& result);
    void resetData(const QString& sourceName);
    void setBusy(bool busy);
    void setStatus(bool ok, const QString& message);

    QByteArray m_data;
    qint64 m_baseOffset = 0;
    qint64 m_mainSize = -1;
    QString m_sourceName;

    QString m_statusText;
    bool m_statusOk = true;
    bool m_busy = false;
    quint64 m_generation = 0;   // Incremented per load; results of an older load are dropped.
};

#endif // HEX_VIEWER_MODEL_H
