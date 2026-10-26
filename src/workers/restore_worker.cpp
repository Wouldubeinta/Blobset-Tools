#include "restore_worker.h"

#include "blobset_file.h"
#include "core_defs.h"
#include "utilities.h"

#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <exception>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

BlobsetFile readHeader(const QString& path, CoreDefs::BlobsetVersion version, bool bigEndian) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
    QDataStream in(&file);
    BlobsetFile blobset;
    if (!blobset.read(in, version, bigEndian, 0))   // V3 / V4 headers have no SHA1.
        fail(u"%1 isn't a valid blobset header."_s.arg(QDir::toNativeSeparators(path)));
    return blobset;
}

/// Replaces `target` with `source` (moved when possible, the backup is used up).
void moveBack(const QString& source, const QString& target) {
    if (QFile::exists(target) && !QFile::remove(target))
        fail(u"Could not remove the modified file %1 (is the game running?)."_s.arg(QDir::toNativeSeparators(target)));
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (QFile::rename(source, target))
        return;
    if (!QFile::copy(source, target))
        fail(u"Could not restore %1."_s.arg(QDir::toNativeSeparators(target)));
    QFile::remove(source);
}

} // namespace

RestoreWorker::RestoreWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& backupDir,
                             int gameId, QObject* parent)
    : QObject(parent),
      m_blobsetFile(blobsetFile),
      m_version(version),
      m_isBigEndian(isBigEndian),
      m_backupDir(backupDir),
      m_gameId(gameId) {}

void RestoreWorker::restore_backups() {
    try {
        emit updateConsole(u"Restoring the original game files from %1..."_s.arg(QDir::toNativeSeparators(m_backupDir)));
        const bool legacy = m_version == static_cast<int>(CoreDefs::BlobsetVersion::V1)
                            || m_version == static_cast<int>(CoreDefs::BlobsetVersion::V2);
        const int restored = legacy ? restoreV1() : restoreV3V4();
        emit progressUpdated(100);
        emit taskSucceeded();   // Nothing to restore is fine too: the game's files are the originals.
        emit taskFinished(restored == 0 ? u"Nothing to restore: no backups were found for this game."_s
                                        : u"The original game files have been restored."_s);
    } catch (const std::exception& ex) {
        emit taskFinished(u"Restoring the backups failed.\n\n"_s + QString::fromUtf8(ex.what()));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// V1 / V2
// ---------------------------------------------------------------------------------------------------------------

int RestoreWorker::restoreV1() {
    int restored = 0;
    const QString name = QFileInfo(m_blobsetFile).fileName();
    const QDir backup(m_backupDir);

    // Create: the whole original blobset.
    const QString fullBackup = backup.filePath(name);
    if (QFile::exists(fullBackup)) {
        emit updateStatus(u"Restoring %1..."_s.arg(name));
        if (QFile::exists(m_blobsetFile) && !QFile::remove(m_blobsetFile))
            fail(u"Could not replace %1 (is the game running?)."_s.arg(QDir::toNativeSeparators(m_blobsetFile)));
        if (!QFile::copy(fullBackup, m_blobsetFile))
            fail(u"Could not copy the original blobset back to %1."_s.arg(QDir::toNativeSeparators(m_blobsetFile)));
        emit updateConsole(u"Restored the original %1 (Create backup)."_s.arg(name));
        ++restored;
    }
    emit progressUpdated(60);

    // Modify: the original header (+ the data a grown header covered); the mod container is removed.
    const QString headerBackup = backup.filePath(name + u".header"_s);
    if (QFile::exists(headerBackup)) {
        emit updateStatus(u"Restoring the blobset header..."_s);
        if (!Utilities::resetBlobset(headerBackup, m_blobsetFile))
            fail(u"Could not write the original header back to %1 (is the game running?)."_s
                     .arg(QDir::toNativeSeparators(m_blobsetFile)));
        emit updateConsole(u"Restored the original blobset header and removed the mod container."_s);
        ++restored;
    }
    return restored;
}

// ---------------------------------------------------------------------------------------------------------------
// V3 / V4
// ---------------------------------------------------------------------------------------------------------------

int RestoreWorker::restoreV3V4() {
    int restored = 0;
    const auto layout = static_cast<CoreDefs::BlobsetVersion>(m_version);
    const bool hash64 = layout == CoreDefs::BlobsetVersion::V4;
    const QDir dataDir = QFileInfo(m_blobsetFile).absoluteDir();
    const QString headerBackup = QDir(m_backupDir).filePath(QFileInfo(m_blobsetFile).fileName());

    if (QFile::exists(headerBackup)) {
        // Assets Modify added (hashes the original header doesn't have) are deleted.
        const BlobsetFile original = readHeader(headerBackup, layout, m_isBigEndian);
        QSet<quint64> originalHashes;
        for (const BlobsetEntry& entry : original.entries)
            originalHashes.insert(hash64 ? entry.folderFileHash64 : quint64(entry.folderFileHash32));

        int removed = 0;
        if (QFile::exists(m_blobsetFile)) {
            const BlobsetFile current = readHeader(m_blobsetFile, layout, m_isBigEndian);
            for (const BlobsetEntry& entry : current.entries) {
                const quint64 hash = hash64 ? entry.folderFileHash64 : quint64(entry.folderFileHash32);
                if (originalHashes.contains(hash))
                    continue;
                if (QFile::remove(dataDir.filePath(entry.folderHashName + u'/' + entry.fileHashName)))
                    ++removed;
            }
        }
        if (removed > 0)
            emit updateConsole(u"Removed %1 asset(s) that Modify had added."_s.arg(removed));
        emit progressUpdated(20);

        // The original assets: backup/<2 hex folder>/<file> -> data/<folder>/<file>.
        static const QRegularExpression hexFolder(u"^[0-9a-fA-F]{2}$"_s);
        QStringList files;
        for (const QFileInfo& folder : QDir(m_backupDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!hexFolder.match(folder.fileName()).hasMatch())
                continue;
            for (const QFileInfo& file : QDir(folder.absoluteFilePath()).entryInfoList(QDir::Files))
                files.append(folder.fileName() + u'/' + file.fileName());
        }
        for (qsizetype i = 0; i < files.size(); ++i) {
            emit updateStatus(u"Restoring %1"_s.arg(files[i]));
            moveBack(QDir(m_backupDir).filePath(files[i]), dataDir.filePath(files[i]));
            emit progressUpdated(int(20 + 70 * (i + 1) / files.size()));
        }
        // Empty backup asset folders are left over once their files moved back.
        for (const QFileInfo& folder : QDir(m_backupDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (hexFolder.match(folder.fileName()).hasMatch())
                QDir(m_backupDir).rmdir(folder.fileName());
        if (!files.isEmpty())
            emit updateConsole(u"Restored %1 original asset(s)."_s.arg(files.size()));

        // The original header (the backup stays: it's the original).
        if (QFile::exists(m_blobsetFile) && !QFile::remove(m_blobsetFile))
            fail(u"Could not replace %1 (is the game running?)."_s.arg(QDir::toNativeSeparators(m_blobsetFile)));
        if (!QFile::copy(headerBackup, m_blobsetFile))
            fail(u"Could not copy the original header back to %1."_s.arg(QDir::toNativeSeparators(m_blobsetFile)));
        emit updateConsole(u"Restored the original blobset header."_s);
        ++restored;
    }

    // Mod Loader mode: the mods live in <game>\BlobsetMods, the game's files were never changed.
    if (layout == CoreDefs::BlobsetVersion::V4) {
        QDir gameRoot = dataDir;
        gameRoot.cdUp();
        const QString modsFolder = gameRoot.filePath(u"BlobsetMods"_s);
        if (QFileInfo(modsFolder).isDir()) {
            if (!QDir(modsFolder).removeRecursively())
                fail(u"Could not remove %1 (is the game running?)."_s.arg(QDir::toNativeSeparators(modsFolder)));
            emit updateConsole(u"Removed the Mod Loader mods (%1)."_s.arg(QDir::toNativeSeparators(modsFolder)));
            ++restored;
        }
    }
    return restored;
}
