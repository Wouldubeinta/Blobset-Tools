#include "update_checker.h"

#include "app_paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
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
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

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

QNetworkRequest request(const QUrl& url, const QString& userAgent) {
    QNetworkRequest r(url);
    // GitHub's API refuses requests without a User-Agent.
    r.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    return r;
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

UpdateChecker::~UpdateChecker() {
    // The worker threads post their results back to this object: let them finish first.
    m_cancel = true;
    m_checkFuture.waitForFinished();
    m_downloadFuture.waitForFinished();
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
    if (m_checking || m_downloading)
        return;
    m_checking = true;
    emit stateChanged();

    const QString userAgent = u"Blobset-Tools/%1"_s.arg(currentVersion());
    m_checkFuture = QtConcurrent::run([this, userAsked, userAgent]() {
        // Worker thread: its own network manager and event loop.
        QString error;
        QByteArray body;
        {
            QNetworkAccessManager network;
            QEventLoop loop;
            QNetworkReply* reply = network.get(request(QUrl(kLatestReleaseApi), userAgent));
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            if (reply->error() != QNetworkReply::NoError)
                error = u"Couldn't reach GitHub: %1"_s.arg(reply->errorString());
            else
                body = reply->readAll();
            delete reply;
        }
        QMetaObject::invokeMethod(this, [this, userAsked, error, body]() { handleCheckResult(userAsked, error, body); },
                                  Qt::QueuedConnection);
    });
}

void UpdateChecker::handleCheckResult(bool userAsked, const QString& error, const QByteArray& body) {
    m_checking = false;
    emit stateChanged();

    const auto fail = [this, userAsked](const QString& message) {
        if (userAsked)
            emit checkFailed(message);
    };
    if (!error.isEmpty()) {
        fail(error);
        return;
    }
    const QJsonObject release = QJsonDocument::fromJson(body).object();
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
    if (m_downloading || m_checking || m_latestVersion.isEmpty())
        return;
    if (portable()) {
        openReleasePage();   // A portable copy isn't updated by the installer.
        return;
    }

    const QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_downloadPath = QDir(folder).filePath(u"BlobsetTools-Setup-%1.exe"_s.arg(m_latestVersion));
    m_cancel = false;
    m_downloading = true;
    m_progress = m_assetSize > 0 ? 0.0 : -1.0;
    emit progressChanged();
    emit stateChanged();

    const QUrl url(installerUrl());
    const QString path = m_downloadPath;
    const qint64 expectedSize = m_assetSize;
    const QString userAgent = u"Blobset-Tools/%1"_s.arg(currentVersion());

    m_downloadFuture = QtConcurrent::run([this, url, path, expectedSize, userAgent]() {
        // Worker thread: download into the temp folder; progress is posted to the GUI thread.
        QString error;
        {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                error = u"Couldn't write %1."_s.arg(QDir::toNativeSeparators(path));
            } else {
                QNetworkAccessManager network;
                QEventLoop loop;
                QNetworkReply* reply = network.get(request(url, userAgent));
                reply->setReadBufferSize(0);
                QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, &file]() { file.write(reply->readAll()); });

                qint64 lastPost = 0;
                QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                                 [this, expectedSize, &lastPost](qint64 received, qint64 total) {
                    if (total <= 0)
                        total = expectedSize;
                    // A few updates a second are plenty for the progress bar.
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    if (now - lastPost < 100 && received != total)
                        return;
                    lastPost = now;
                    const double progress = total > 0 ? double(received) / double(total) : -1.0;
                    QMetaObject::invokeMethod(this, [this, progress]() {
                        m_progress = progress;
                        emit progressChanged();
                    }, Qt::QueuedConnection);
                });

                // Cancel (the dialog's button) is a flag: look at it a few times a second.
                QTimer cancelPoll;
                QObject::connect(&cancelPoll, &QTimer::timeout, reply, [this, reply]() {
                    if (m_cancel)
                        reply->abort();
                });
                cancelPoll.start(100);

                QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();
                cancelPoll.stop();

                if (reply->error() == QNetworkReply::OperationCanceledError) {
                    error = u"The download was cancelled."_s;
                } else if (reply->error() != QNetworkReply::NoError) {
                    error = u"The download failed: %1\n%2"_s.arg(reply->errorString(), url.toString());
                } else {
                    file.write(reply->readAll());
                    const qint64 size = file.size();
                    if (size < 1024 * 1024 || (expectedSize > 0 && size != expectedSize))
                        error = u"The downloaded installer is incomplete (%1 bytes). Try again, or download it from "
                                u"the release page."_s.arg(size);
                    else if (!file.commit())
                        error = u"Couldn't save %1."_s.arg(QDir::toNativeSeparators(path));
                }
                delete reply;
            }
            // Not committed (error): QSaveFile drops the partial file.
        }

        // A Windows program starts with "MZ"; anything else is an error page, not the installer.
        if (error.isEmpty()) {
            QFile check(path);
            const bool isProgram = check.open(QIODevice::ReadOnly) && check.read(2) == "MZ";
            check.close();
            if (!isProgram) {
                QFile::remove(path);
                error = u"The download isn't the installer. Try again, or download it from the release page."_s;
            }
        }
        QMetaObject::invokeMethod(this, [this, error]() { finishDownload(error); }, Qt::QueuedConnection);
    });
}

void UpdateChecker::cancelDownload() {
    m_cancel = true;   // The download thread aborts and reports back through finishDownload().
}

void UpdateChecker::finishDownload(const QString& error) {
    m_downloading = false;
    if (!error.isEmpty()) {
        m_progress = 0.0;
        emit progressChanged();
        emit stateChanged();
        emit downloadFailed(error);
        return;
    }

    m_progress = 1.0;
    emit progressChanged();
    emit stateChanged();

    // /SP- skips "This will install..."; setup keeps the games picked last time. Blobset Tools quits below.
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
