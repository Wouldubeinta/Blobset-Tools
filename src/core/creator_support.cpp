#include "creator_support.h"

#include "blobset_entry_registry.h"
#include "blobset_file.h"
#include "core_defs.h"
#include "xml_models.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <utility>

using namespace Qt::StringLiterals;

namespace CreatorSupport {

SourceInfo fromBlobset(const BlobsetEntry& entry, int version, int platform, const QString& extension) {
    SourceInfo info;
    info.index = entry.fileIndex;
    info.folderHash = entry.folderHashName;
    info.fileHash = entry.fileHashName;
    info.isCompressed = entry.mainCompressedSize != entry.mainUnCompressedSize;
    info.version = version;
    info.platform = platform;
    info.outputName = QString::number(entry.fileIndex) + u'.' + extension;
    return info;
}

SourceInfo fromFile(const QString& path, int version, int platform) {
    SourceInfo info;
    info.version = version;
    info.platform = platform;
    const QFileInfo file(path);
    info.outputName = file.fileName();

    // A rebuilt file (or one prepared for Modify) carries its ModifyFileInfo next to it.
    const QString sidecar = file.absolutePath() + u'/' + file.completeBaseName() + u".xml"_s;
    if (QFile::exists(sidecar)) {
        XmlModifyFileInfo xml;
        if (xml.deserialize(sidecar) && (!xml.folderHash.isEmpty() || !xml.fileNameHash.isEmpty() || xml.index > 0)) {
            info.index = xml.index;
            info.folderHash = xml.folderHash;
            info.fileHash = xml.fileNameHash;
            info.isCompressed = xml.isCompressed;
            return info;
        }
    }

    // Extract names unknown assets after their header index.
    bool numeric = false;
    const quint32 index = file.completeBaseName().toUInt(&numeric);
    if (numeric) {
        info.index = index;
        BlobsetEntry entry;
        if (BlobsetEntryRegistry::findByIndex(index, entry)) {
            info.folderHash = entry.folderHashName;
            info.fileHash = entry.fileHashName;
            info.isCompressed = entry.mainCompressedSize != entry.mainUnCompressedSize;
        }
    }
    return info;
}

QString platformName(int platform) {
    switch (static_cast<CoreDefs::Platforms>(platform)) {
    case CoreDefs::Platforms::PS3: return u"PS3"_s;
    case CoreDefs::Platforms::Xbox360: return u"Xbox360"_s;
    default: return u"PC"_s;
    }
}

int platformFromName(const QString& name, int fallback) {
    if (name.compare(u"PC"_s, Qt::CaseInsensitive) == 0 || name.compare(u"Windows"_s, Qt::CaseInsensitive) == 0)
        return static_cast<int>(CoreDefs::Platforms::Windows);
    if (name.compare(u"PS3"_s, Qt::CaseInsensitive) == 0)
        return static_cast<int>(CoreDefs::Platforms::PS3);
    if (name.compare(u"Xbox360"_s, Qt::CaseInsensitive) == 0 || name.compare(u"X360"_s, Qt::CaseInsensitive) == 0)
        return static_cast<int>(CoreDefs::Platforms::Xbox360);
    return fallback;
}

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

quint32 detectAlignment(const QList<QPair<qint64, qint64>>& blocks, quint32 fallback, quint32 maxAlignment) {
    if (blocks.size() < 2)
        return fallback;

    bool packed = true;
    for (qsizetype i = 0; i + 1 < blocks.size(); ++i) {
        if (blocks[i].first + blocks[i].second != blocks[i + 1].first) {
            packed = false;
            break;
        }
    }
    if (packed)
        return 1;

    quint32 alignment = maxAlignment;
    while (alignment > 1) {
        bool all = true;
        for (const auto& block : blocks) {
            if (block.first % alignment != 0) {
                all = false;
                break;
            }
        }
        if (all)
            return alignment;
        alignment /= 2;
    }
    return 1;
}

QString resolveListFile(const QString& sourceDir, const QString& file, const QString& storedPath) {
    const QDir dir(sourceDir);
    QStringList candidates;
    if (!file.isEmpty())
        candidates << file;

    QString path = storedPath;
    path.replace(u'\\', u'/');
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    candidates << path << path + u".dds"_s;
    if (path.endsWith(u".badds"_s, Qt::CaseInsensitive))
        candidates << path.left(path.size() - 6) + u".dds"_s;

    const QString root = QDir::cleanPath(dir.absolutePath()) + u'/';
    for (const QString& candidate : std::as_const(candidates)) {
        if (candidate.isEmpty())
            continue;
        const QString full = QDir::cleanPath(dir.absoluteFilePath(candidate));
        if (full.startsWith(root, Qt::CaseInsensitive) && QFileInfo(full).isFile())
            return full;
    }
    return {};
}

QVariantMap inspectList(const QString& xmlPath, const QString& sourceDir, const QString& expectedType) {
    QVariantMap result;
    const QString xmlFile = toLocalPath(xmlPath);
    result[u"ok"_s] = false;

    if (xmlFile.isEmpty() || !QFileInfo(xmlFile).isFile()) {
        result[u"error"_s] = u"Choose the %1 list (%2) written by the %1 Viewer's Extract All."_s
                                 .arg(expectedType, listNameFor(expectedType));
        return result;
    }

    XmlExtractFileInfo list;
    if (!list.deserialize(xmlFile)) {
        result[u"error"_s] = u"%1 isn't a valid creator list (XML error)."_s.arg(QFileInfo(xmlFile).fileName());
        return result;
    }
    if (!list.type.isEmpty() && list.type.compare(expectedType, Qt::CaseInsensitive) != 0) {
        result[u"error"_s] = u"%1 is a %2 list, not a %3 list."_s.arg(QFileInfo(xmlFile).fileName(), list.type, expectedType);
        return result;
    }
    if (list.entries.isEmpty() && expectedType != u"BSB"_s) {   // A BSB may have no textures.
        result[u"error"_s] = u"%1 lists no files."_s.arg(QFileInfo(xmlFile).fileName());
        return result;
    }

    const QString folder = sourceDir.trimmed().isEmpty() ? QFileInfo(xmlFile).absolutePath() : toLocalPath(sourceDir);
    QVariantList entries;
    int missing = 0;
    qint64 totalSize = 0;
    for (const ExtractFileEntry& entry : std::as_const(list.entries)) {
        const QString found = resolveListFile(folder, entry.file, entry.filePath);
        QVariantMap row;
        row[u"path"_s] = entry.filePath;
        row[u"file"_s] = entry.file.isEmpty() ? entry.filePath : entry.file;
        row[u"found"_s] = !found.isEmpty();
        const qint64 size = found.isEmpty() ? 0 : QFileInfo(found).size();
        row[u"size"_s] = size;
        totalSize += size;
        if (found.isEmpty())
            ++missing;
        entries.append(row);
    }

    result[u"ok"_s] = true;
    result[u"type"_s] = list.type.isEmpty() ? expectedType : list.type;
    result[u"count"_s] = static_cast<int>(list.entries.size());
    result[u"platform"_s] = list.platform.isEmpty() ? u"PC"_s : list.platform;
    result[u"version"_s] = list.version;
    result[u"index"_s] = static_cast<qlonglong>(list.index);
    result[u"folderHash"_s] = list.folderHash;
    result[u"fileNameHash"_s] = list.fileNameHash;
    result[u"hasTarget"_s] = !list.folderHash.isEmpty() || !list.fileNameHash.isEmpty() || list.index > 0;
    result[u"outputName"_s] = list.outputName.isEmpty()
                                  ? QFileInfo(xmlFile).dir().dirName() + u'.' + expectedType.toLower()
                                  : list.outputName;
    result[u"sourceDir"_s] = folder;
    if (expectedType == u"BSB"_s) {
        // The Flash movie the BSB wraps.
        const QString movie = list.movie.isEmpty() ? QString() : QDir(folder).filePath(list.movie);
        const bool movieFound = !movie.isEmpty() && QFileInfo(movie).isFile();
        result[u"movie"_s] = list.movie;
        result[u"movieFound"_s] = movieFound;
        result[u"movieSize"_s] = movieFound ? QFileInfo(movie).size() : 0;
        if (!movieFound)
            ++missing;
        result[u"missing"_s] = missing;
    }
    // Next to the source folder (never inside it, so a second run doesn't pick the output up).
    result[u"suggestedOutput"_s] = QDir::cleanPath(QDir(folder).absoluteFilePath(u"../"_s + result[u"outputName"_s].toString()));
    result[u"missing"_s] = missing;
    result[u"totalSize"_s] = totalSize;
    result[u"entries"_s] = entries;
    return result;
}

} // namespace CreatorSupport
