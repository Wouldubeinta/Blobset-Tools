#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <QNetworkReply>   // Complete type for QPointer<QNetworkReply>.
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QSaveFile;

/// Auto updater: asks GitHub for the latest release of Wouldubeinta/Blobset-Tools, and when it's newer than this
/// build, downloads
///     https://github.com/Wouldubeinta/Blobset-Tools/releases/download/<tag>/BlobsetTools-Setup-<version>.exe
/// into the temp folder, starts it and closes Blobset Tools (the installer keeps the picked games and the data in
/// Documents\Blobset Tools).
///
/// main.qml checks at startup (at most once a day, Settings.ini [Update] AutoCheck / LastCheck / SkipVersion);
/// Options > Check for Updates checks right away. Portable copies (portable.txt) get the release page instead of
/// the installer.
class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY stateChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)   // 0..1, -1 when the size is unknown.
    Q_PROPERTY(bool portable READ portable CONSTANT)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString releaseUrl() const { return m_releaseUrl; }
    bool checking() const { return m_checkReply != nullptr; }
    bool downloading() const { return m_downloadReply != nullptr; }
    double progress() const { return m_progress; }
    bool portable() const;
    bool autoCheck() const;
    void setAutoCheck(bool enabled);

    /// Startup check: only when AutoCheck is on and the last check was more than a day ago; quiet unless an update
    /// (that wasn't skipped) is found.
    Q_INVOKABLE void checkOnStartup();
    /// Options > Check for Updates: always checks and always answers (updateAvailable / upToDate / checkFailed).
    Q_INVOKABLE void checkNow();
    /// Downloads the installer of latestVersion, starts it and quits Blobset Tools.
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void cancelDownload();
    /// Don't offer latestVersion at startup again (Check for Updates still shows it).
    Q_INVOKABLE void skipThisVersion();
    Q_INVOKABLE void openReleasePage() const;

    /// a > b: 1, a < b: -1, equal: 0 ("2.1.10.56" vs "v2.1.10.55"; missing parts count as 0).
    static int compareVersions(const QString& a, const QString& b);

signals:
    void stateChanged();
    void progressChanged();
    void autoCheckChanged();
    void updateAvailable(const QString& version, const QString& notes);
    void upToDate(const QString& version);
    void checkFailed(const QString& message);
    void downloadFailed(const QString& message);
    void installerStarted(const QString& version);

private:
    void check(bool userAsked);
    void handleCheckReply(bool userAsked);
    void finishDownload();
    QString installerUrl() const;

    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_checkReply;
    QPointer<QNetworkReply> m_downloadReply;
    QSaveFile* m_downloadFile = nullptr;
    QString m_downloadPath;

    QString m_latestVersion;   // "2.1.10.56" (tag without a leading 'v').
    QString m_latestTag;       // The tag as GitHub has it (the download URL's folder).
    QString m_assetUrl;        // The installer asset's URL from the release, when it has one by the usual name.
    qint64 m_assetSize = 0;
    QString m_releaseNotes;
    QString m_releaseUrl;
    double m_progress = 0.0;
};

#endif // UPDATE_CHECKER_H
