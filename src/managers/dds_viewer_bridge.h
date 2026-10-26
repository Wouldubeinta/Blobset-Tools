#ifndef DDS_VIEWER_BRIDGE_H
#define DDS_VIEWER_BRIDGE_H

#include <QObject>
#include <QQmlEngine>
#include <QString>

// View state of one DDS preview (DDSViewer.qml and the Image Preview of MainForm.qml each own one).
//
// The bridge never decodes anything itself: it only composes an "image://dds_provider/..." source
// for the texture that is selected, so QML goes through the same provider as the rest of the app.
class DdsViewerBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString imageSource READ imageSource NOTIFY imageChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY imageChanged)
    // "path  |  W x H  |  format  |  N mips  |  size", the same info line the TXPK viewer shows.
    // Read lazily, so the Image Preview of MainForm.qml (which doesn't show it) never pays for it.
    Q_PROPERTY(QString infoLine READ infoLine NOTIFY imageChanged)
    Q_PROPERTY(bool flipped READ isFlipped NOTIFY viewChanged)
    Q_PROPERTY(bool alphaEnabled READ isAlphaEnabled WRITE setAlphaEnabled NOTIFY viewChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool statusOk READ statusOk NOTIFY statusChanged)

public:
    explicit DdsViewerBridge(QObject* parent = nullptr);
    ~DdsViewerBridge() override = default;

    QString imageSource() const { return m_imageSource; }
    bool hasImage() const { return !m_hash.isEmpty(); }
    QString fileName() const { return m_name; }
    QString infoLine() const;
    bool isFlipped() const { return m_flipped; }
    bool isAlphaEnabled() const { return m_alphaEnabled; }
    QString statusText() const { return m_statusText; }
    bool statusOk() const { return m_statusOk; }

    // Flip and alpha are kept between textures, so they stay as set while browsing a folder.
    // displayName (the workspace path) names the extracted file; without it the hash is used.
    Q_INVOKABLE void loadHash(const QString& hexHash, const QString& displayName = QString());
    Q_INVOKABLE void clear();

    Q_INVOKABLE void flipImage();                 // Vertical flip, on/off.
    Q_INVOKABLE void toggleAlpha(bool enable);
    void setAlphaEnabled(bool enable) { toggleAlpha(enable); }

    // Writes the texture into outDirPath (path or file:// URL).
    //   "dds": the texture as a standard PC .dds (console data converted, view options not applied).
    //   "png": what is currently shown (flip / alpha applied).
    Q_INVOKABLE bool extractTexture(const QString& outDirPath, const QString& format);

signals:
    void imageChanged();
    void viewChanged();
    void statusChanged();
    void errorOccurred(const QString& message);

private:
    void rebuildSource();
    void report(bool ok, const QString& message);

    QString m_hash;
    QString m_name;
    QString m_imageSource;
    quint32 m_session = 0;
    bool m_flipped = false;
    bool m_alphaEnabled = true;
    QString m_statusText;
    bool m_statusOk = true;

    // infoLine() cache, keyed by the load session.
    mutable quint32 m_infoSession = 0;
    mutable QString m_infoLine;
};

#endif // DDS_VIEWER_BRIDGE_H
