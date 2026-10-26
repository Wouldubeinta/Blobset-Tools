#include "dds_viewer_bridge.h"
#include "dds_image_provider.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <atomic>

using namespace Qt::StringLiterals;

namespace {

// Process wide, so two bridges can never share a decoded texture of the image provider.
std::atomic<quint32> g_nextSession{0};

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

} // namespace

DdsViewerBridge::DdsViewerBridge(QObject* parent) : QObject(parent) {}

void DdsViewerBridge::loadHash(const QString& hexHash, const QString& displayName) {
    const QString hash = hexHash.trimmed().toLower();
    if (hash.isEmpty()) {
        clear();
        return;
    }

    m_hash = hash;
    m_name = displayName.trimmed().isEmpty() ? u"0x"_s + hash.toUpper() : displayName.trimmed();
    m_session = ++g_nextSession;   // Fresh decode from disk for every load.

    // A new texture: the previous extraction result no longer applies, the status bar shows the info line again.
    if (!m_statusText.isEmpty() || !m_statusOk) {
        m_statusText.clear();
        m_statusOk = true;
        emit statusChanged();
    }

    rebuildSource();
}

QString DdsViewerBridge::infoLine() const {
    if (m_hash.isEmpty())
        return {};

    if (m_infoSession != m_session) {
        m_infoSession = m_session;

        QString error;
        QVariantMap info = DdsImageProvider::readTextureInfo(m_hash, &error);
        if (info.isEmpty()) {
            m_infoLine = m_name + u"  |  "_s + error;
        } else {
            info[u"path"_s] = QString(m_name).replace(u'\\', u'/'); // The workspace path, as in the folder tree.
            m_infoLine = DdsImageProvider::formatInfoLine(info);
        }
    }
    return m_infoLine;
}

void DdsViewerBridge::clear() {
    if (m_hash.isEmpty() && m_imageSource.isEmpty())
        return;

    m_hash.clear();
    m_name.clear();
    m_imageSource.clear();
    m_session = 0;
    m_infoSession = 0;
    m_infoLine.clear();
    emit imageChanged();
}

void DdsViewerBridge::flipImage() {
    m_flipped = !m_flipped;
    emit viewChanged();
    if (hasImage())
        rebuildSource();
}

void DdsViewerBridge::toggleAlpha(bool enable) {
    if (m_alphaEnabled == enable)
        return;

    m_alphaEnabled = enable;
    emit viewChanged();
    if (hasImage())
        rebuildSource();
}

bool DdsViewerBridge::extractTexture(const QString& outDirPath, const QString& format) {
    if (!hasImage()) {
        report(false, u"No texture is loaded."_s);
        return false;
    }

    const QString outDir = toLocalPath(outDirPath);
    if (outDir.isEmpty() || !QDir().mkpath(outDir)) {
        report(false, u"Could not create the output folder: "_s + outDir);
        return false;
    }

    const bool asPng = format.compare(u"png"_s, Qt::CaseInsensitive) == 0;

    QString baseName = QFileInfo(QString(m_name).replace(u'\\', u'/')).completeBaseName();
    if (baseName.isEmpty())
        baseName = u"0x"_s + m_hash.toUpper();
    const QString path = QDir(outDir).filePath(baseName + (asPng ? u".png"_s : u".dds"_s));

    if (asPng) {
        const QImage image = DdsImageProvider::render(
            DdsImageProvider::makeId(m_hash, m_session, {m_flipped, m_alphaEnabled}));
        if (image.isNull()) {
            report(false, u"Could not decode the texture."_s);
            return false;
        }
        if (!image.save(path, "PNG")) {
            report(false, u"Could not write "_s + QDir::toNativeSeparators(path));
            return false;
        }
    } else {
        QString error;
        const QByteArray dds = DdsImageProvider::readDds(m_hash, &error);
        if (dds.isEmpty()) {
            report(false, error);
            return false;
        }

        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(dds) != dds.size() || !out.commit()) {
            report(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
            return false;
        }
    }

    report(true, u"Extracted: "_s + QDir::toNativeSeparators(path));
    return true;
}

void DdsViewerBridge::rebuildSource() {
    m_imageSource = u"image://dds_provider/"_s
                    + DdsImageProvider::makeId(m_hash, m_session, {m_flipped, m_alphaEnabled});
    emit imageChanged();
}

void DdsViewerBridge::report(bool ok, const QString& message) {
    m_statusOk = ok;
    m_statusText = message;
    emit statusChanged();
    if (!ok)
        emit errorOccurred(message);
}
