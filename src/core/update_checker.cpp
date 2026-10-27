#include "update_checker.h"

#include "app_paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace {

const QString kRepository = u"Wouldubeinta/Blobset-Tools"_s;
const QString kLatestReleaseApi = u"https://api.github.com/repos/%1/releases/latest"_s.arg(kRepository);
const QString kReleasesPage = u"https://github.com/%1/releases/latest"_s.arg(kRepository);
// <tag> / <version>: e.g. 2.1.10.56 / 2.1.10.56 (a "v2.1.10.56" tag keeps its v in the folder only).
const QString kInstallerUrl = u"https://github.com/%1/releases/download/%2/BlobsetTools-Setup-%3.exe"_s;
constexpr qint64 kCheckIntervalSecs = 24 * 60 * 60;

QString settingsFile() {
    return QDir(AppPaths::dataDir()).filePath(u"Settings.ini"_s);
}

QString withoutV(QString version) {
    version = version.trimmed();
    if (version.startsWith(u'v') || version.startsWith(u'V'))
        version.remove(0, 1);
    return version;
}

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest r(url);
    // GitHub's API refuses requests without a User-Agent.
    r.setHeader(QNetworkRequest::UserAgentHeader, u"Blobset-Tools/%1"_s.arg(QCoreApplication::applicationVersion()));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    return r;
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

UpdateChecker::~UpdateChecker() {
    if (m_downloadReply)
        m_downloadReply->abort();
    delete m_downloadFile;
}

QString UpdateChecker::currentVersion() const {
    return QCoreApplication::applicationVersion();
}

bool UpdateChecker::portable() const {
    return AppPaths::isPortable();
}

bool UpdateChecker::autoCheck() const {
    return QSettings(settingsFile(), QSettings::IniFormat).value(u"Update/AutoCheck"_s, true).toBool();
}

void UpdateChecker::setAutoCheck(bool enabled) {
    if (enabled == autoCheck())
        return;
    QSettings(settingsFile(), QSettings::IniFormat).setValue(u"Update/AutoCheck"_s, enabled);
    emit autoCheckChanged();
}

int UpdateChecker::compareVersions(const QString& a, const QString& b) {
    const QStringList left = withoutV(a).split(u'.');
    const QStringList right = withoutV(b).split(u'.');
    for (qsizetype i = 0; i < std::max(left.size(), right.size()); ++i) {
        const qint64 l = i < left.size() ? left[i].toLongLong() : 0;
        const qint64 r = i < right.size() ? right[i].toLongLong() : 0;
        if (l != r)
            return l > r ? 1 : -1;
    }
    return 0;
}

void UpdateChecker::checkOnStartup() {
    if (!autoCheck())
        return;
    const QSettings settings(settingsFile(), QSettings::IniFormat);
    const qint64 last = settings.value(u"Update/LastCheck"_s, 0).toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - last < kCheckIntervalSecs)
        return;
    check(false);
}

void UpdateChecker::checkNow() {
    check(true);
}

void UpdateChecker::check(bool userAsked) {
    if (m_checkReply || m_downloadReply)
        return;
    m_checkReply = m_network->get(request(QUrl(kLatestReleaseApi)));
    emit stateChanged();
    connect(m_checkReply, &QNetworkReply::finished, this, [this, userAsked]() { handleCheckReply(userAsked); });
}

void UpdateChecker::handleCheckReply(bool userAsked) {
    QNetworkReply* reply = m_checkReply;
    m_checkReply = nullptr;
    reply->deleteLater();
    emit stateChanged();

    const auto fail = [this, userAsked](const QString& message) {
        if (userAsked)
            emit checkFailed(message);
    };
    if (reply->error() != QNetworkReply::NoError) {
        fail(u"Couldn't reach GitHub: %1"_s.arg(reply->errorString()));
        return;
    }
    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release.value(u"tag_name"_s).toString().trimmed();
    if (tag.isEmpty()) {
        fail(u"GitHub didn't say which release is the latest."_s);
        return;
    }
    QSettings(settingsFile(), QSettings::IniFormat).setValue(u"Update/LastCheck"_s, QDateTime::currentSecsSinceEpoch());

    m_latestTag = tag;
    m_latestVersion = withoutV(tag);
    m_releaseNotes = release.value(u"body"_s).toString().trimmed();
    m_releaseUrl = release.value(u"html_url"_s).toString(kReleasesPage);

    // The installer asset by its usual name (its real URL and size), else the URL it should have.
    m_assetUrl.clear();
    m_assetSize = 0;
    const QString expectedName = u"BlobsetTools-Setup-%1.exe"_s.arg(m_latestVersion);
    for (const QJsonValue& value : release.value(u"assets"_s).toArray()) {
        const QJsonObject asset = value.toObject();
        if (asset.value(u"name"_s).toString().compare(expectedName, Qt::CaseInsensitive) == 0) {
            m_assetUrl = asset.value(u"browser_download_url"_s).toString();
            m_assetSize = asset.value(u"size"_s).toInteger();
        }
    }
    emit stateChanged();

    if (compareVersions(m_latestVersion, currentVersion()) <= 0) {
        if (userAsked)
            emit upToDate(currentVersion());
        return;
    }
    const QString skipped = QSettings(settingsFile(), QSettings::IniFormat).value(u"Update/SkipVersion"_s).toString();
    if (!userAsked && compareVersions(skipped, m_latestVersion) == 0)
        return;
    emit updateAvailable(m_latestVersion, m_releaseNotes);
}

QString UpdateChecker::installerUrl() const {
    return !m_assetUrl.isEmpty() ? m_assetUrl : kInstallerUrl.arg(kRepository, m_latestTag, m_latestVersion);
}

void UpdateChecker::downloadAndInstall() {
    if (m_downloadReply || m_latestVersion.isEmpty())
        return;
    if (portable()) {
        openReleasePage();   // A portable copy isn't updated by the installer.
        return;
    }

    const QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_downloadPath = QDir(folder).filePath(u"BlobsetTools-Setup-%1.exe"_s.arg(m_latestVersion));
    delete m_downloadFile;
    m_downloadFile = new QSaveFile(m_downloadPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        emit downloadFailed(u"Couldn't write %1."_s.arg(QDir::toNativeSeparators(m_downloadPath)));
        return;
    }

    m_progress = m_assetSize > 0 ? 0.0 : -1.0;
    emit progressChanged();
    m_downloadReply = m_network->get(request(QUrl(installerUrl())));
    m_downloadReply->setReadBufferSize(0);
    emit stateChanged();

    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadReply && m_downloadFile)
            m_downloadFile->write(m_downloadReply->readAll());
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total <= 0)
            total = m_assetSize;
        m_progress = total > 0 ? double(received) / double(total) : -1.0;
        emit progressChanged();
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateChecker::finishDownload);
}

void UpdateChecker::cancelDownload() {
    if (m_downloadReply)
        m_downloadReply->abort();   // finishDownload() cleans up.
}

void UpdateChecker::finishDownload() {
    QNetworkReply* reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->deleteLater();

    const auto fail = [this](const QString& message) {
        if (m_downloadFile)
            m_downloadFile->cancelWriting();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        m_progress = 0.0;
        emit progressChanged();
        emit stateChanged();
        emit downloadFailed(message);
    };

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        fail(u"The download was cancelled."_s);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(u"The download failed: %1\n%2"_s.arg(reply->errorString(), installerUrl()));
        return;
    }
    m_downloadFile->write(reply->readAll());

    // A Windows program starts with "MZ"; anything else is an error page, not the installer.
    const qint64 size = m_downloadFile->size();
    if (size < 1024 * 1024 || (m_assetSize > 0 && size != m_assetSize)) {
        fail(u"The downloaded installer is incomplete (%1 bytes). Try again, or download it from the release page."_s.arg(size));
        return;
    }
    if (!m_downloadFile->commit()) {
        fail(u"Couldn't save %1."_s.arg(QDir::toNativeSeparators(m_downloadPath)));
        return;
    }
    delete m_downloadFile;
    m_downloadFile = nullptr;
    {
        QFile check(m_downloadPath);
        if (!check.open(QIODevice::ReadOnly) || check.read(2) != "MZ") {
            check.close();
            QFile::remove(m_downloadPath);
            fail(u"The download isn't the installer. Try again, or download it from the release page."_s);
            return;
        }
    }

    m_progress = 1.0;
    emit progressChanged();
    emit stateChanged();

    // /SP- skips "This will install..."; setup closes nothing itself (Blobset Tools quits below) and keeps the games
    // picked last time.
    if (!QProcess::startDetached(m_downloadPath, {u"/SP-"_s})) {
        emit downloadFailed(u"Couldn't start %1. Run it yourself to update."_s.arg(QDir::toNativeSeparators(m_downloadPath)));
        return;
    }
    emit installerStarted(m_latestVersion);
    QTimer::singleShot(300, qApp, &QCoreApplication::quit);
}

void UpdateChecker::skipThisVersion() {
    if (!m_latestVersion.isEmpty())
        QSettings(settingsFile(), QSettings::IniFormat).setValue(u"Update/SkipVersion"_s, m_latestVersion);
}

void UpdateChecker::openReleasePage() const {
    QDesktopServices::openUrl(QUrl(m_releaseUrl.isEmpty() ? kReleasesPage : m_releaseUrl));
}
