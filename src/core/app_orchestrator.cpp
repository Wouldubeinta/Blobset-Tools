#include "app_orchestrator.h"
#include <exception>

#include "app_paths.h"
#include "audio_decoder.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "blobset_file.h"
#include "core_defs.h"
#include "create_worker.h"
#include "dds_image_provider.h"
#include "dds_processor.h"
#include "extract_worker.h"
#include "file_mapping_worker.h"
#include "hash_utility.h"
#include "name_resolver_worker.h"
#include "restore_worker.h"
#include "m3mp_file.h"
#include "mesh_file.h"
#include "mod_library.h"
#include "modify_worker.h"
#include "package_stream.h"
#include "txpk_file.h"
#include "utilities.h"
#include "xml_models.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

const QString kMainFormView = u"qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml"_s;

// Everything the workspace loader produces off the UI thread.
/// Steam's steamapps\appmanifest_<app id>.acf: the installed build and whether an update is waiting.
struct SteamManifest {
    bool found = false;
    QString buildId;         // Installed build.
    QString targetBuildId;   // Build Steam wants to install (0 / empty / buildId when up to date).
    uint stateFlags = 0;     // 2 = update required, 4 = fully installed.
    qint64 lastUpdated = 0;  // Unix time of the last install / update.
};

SteamManifest readSteamManifest(uint appId) {
    SteamManifest m;
    if (appId == 0)
        return m;
    const QString library = Utilities::getSteamGamesLocation(appId);
    if (library.isEmpty())
        return m;
    QFile file(QDir(library).filePath(u"steamapps/appmanifest_%1.acf"_s.arg(appId)));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return m;
    const QString text = QString::fromUtf8(file.readAll());
    const auto value = [&text](const QString& key) {
        const QRegularExpression re(u"\"%1\"\\s+\"([^\"]*)\""_s.arg(key), QRegularExpression::CaseInsensitiveOption);
        return re.match(text).captured(1).trimmed();
    };
    m.buildId = value(u"buildid"_s);
    m.targetBuildId = value(u"TargetBuildID"_s);
    m.stateFlags = value(u"StateFlags"_s).toUInt();
    m.lastUpdated = value(u"LastUpdated"_s).toLongLong();
    m.found = !m.buildId.isEmpty();
    return m;
}

struct WorkspaceLoad {
    QStringList filePaths;
    QHash<QString, quint64> pathHashes;   // Normalized path ('/' separators) -> path hash.
    QList<BlobsetEntry> entries;
    QString error;                        // Mapping couldn't be read: nothing is shown.
    QString blobsetError;                 // Blobset couldn't be read: the tree is shown, previews aren't.
};

WorkspaceLoad readWorkspace(const QString& mappingPath, const QString& blobsetFile, int version, bool bigEndian,
                            int gameId) {
    WorkspaceLoad r;

    try {
        const std::vector<FileMappingWorker::MappingEntry> mapping = FileMappingWorker::readMappingFile(mappingPath);
        r.filePaths.reserve(static_cast<qsizetype>(mapping.size()));
        r.pathHashes.reserve(static_cast<qsizetype>(mapping.size()));

        for (const FileMappingWorker::MappingEntry& entry : mapping) {
            r.filePaths.append(entry.path);
            r.pathHashes.insert(QString(entry.path).trimmed().replace(u'\\', u'/'), entry.hash);
        }
    } catch (const std::exception& ex) {
        r.error = QString::fromUtf8(ex.what());
        return r;
    }

    if (blobsetFile.isEmpty())
        return r; // No game location yet: folder tree only.

    QFile file(blobsetFile);
    if (!file.open(QIODevice::ReadOnly)) {
        r.blobsetError = u"Could not open the blobset: %1"_s.arg(file.errorString());
        return r;
    }

    QDataStream in(&file);
    BlobsetFile blobset;
    if (!blobset.read(in, static_cast<CoreDefs::BlobsetVersion>(version), bigEndian, gameId)) {
        r.blobsetError = u"Invalid BLOB magic signature in %1"_s.arg(blobsetFile);
        return r;
    }

    const bool legacyHash = version < static_cast<int>(CoreDefs::BlobsetVersion::V4);
    for (qsizetype i = 0; i < blobset.entries.size(); ++i) {
        BlobsetEntry& entry = blobset.entries[i];
        entry.fileIndex = static_cast<quint32>(i);
        if (legacyHash)
            entry.folderFileHash64 = entry.folderFileHash32; // Same zero extended key the mapping uses.
    }
    r.entries = std::move(blobset.entries);
    return r;
}

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

// ---------------------------------------------------------------------------
// Console Log asset info (requestAssetInfo)
// ---------------------------------------------------------------------------

// Same colors as BtDarkStyle.qml.
constexpr auto kInfoHeaderColor = "#f5c518";  // accentColor
constexpr auto kInfoKeyColor    = "#88888c";  // textDim
constexpr auto kInfoValueColor  = "#eceff1";  // textMain
constexpr auto kInfoErrorColor  = "#ef5350";

struct AssetInfoRequest {
    bool found = false;
    BlobsetEntry entry;
    QString hexHash;
    QString workspacePath;     // As shown in the folder tree, '\' separated.
    QString gameLocation;
    int version = 0;
    int platform = 0;
    int gameId = 0;
};

class InfoHtml {
public:
    void section(const QString& title) {
        if (!m_html.isEmpty())
            m_html += u"<br>"_s;
        m_html += u"<span style=\"color:%1; font-weight:600;\">*** %2 ***</span><br>"_s
                      .arg(QLatin1StringView(kInfoHeaderColor), title.toHtmlEscaped());
    }
    void line(const QString& text, const char* color = kInfoValueColor) {
        m_html += u"<span style=\"color:%1;\">%2</span><br>"_s.arg(QLatin1StringView(color), text.toHtmlEscaped());
    }
    void field(const QString& key, const QString& value) {
        m_html += u"<span style=\"color:%1;\">%2:</span> <span style=\"color:%3;\">%4</span><br>"_s
                      .arg(QLatin1StringView(kInfoKeyColor), key.toHtmlEscaped(),
                           QLatin1StringView(kInfoValueColor), value.toHtmlEscaped());
    }
    void error(const QString& text) { line(text, kInfoErrorColor); }
    QString html() const { return m_html; }

private:
    QString m_html;
};

QString assetTypeLabel(const QString& path) {
    const QString name = QFileInfo(QString(path).replace(u'\\', u'/')).fileName();
    const qsizetype dot = name.lastIndexOf(u'.');
    const QString ext = dot >= 0 ? name.mid(dot + 1).toUpper() : QString();
    return ext.isEmpty() ? u"Asset"_s : ext;
}

// Physical file that holds the entry: V3/V4 have one file per asset, V1/V2 numbered blob containers.
QString entryFileName(const AssetInfoRequest& r) {
    if (r.version == static_cast<int>(CoreDefs::BlobsetVersion::V4)
        || r.version == static_cast<int>(CoreDefs::BlobsetVersion::V3)) {
        return QDir::toNativeSeparators(QDir(QFileInfo(r.gameLocation).absolutePath())
                                            .filePath(r.entry.folderHashName + u'/' + r.entry.fileHashName));
    }
    QString path = r.gameLocation;
    const qsizetype pos = path.lastIndexOf(u"0.blob"_s);
    if (pos >= 0)
        path.replace(pos, 6, QString::number(r.entry.blobSetNumber) + u".blob"_s);
    return QDir::toNativeSeparators(path);
}

void addBlobsetInfo(InfoHtml& out, const AssetInfoRequest& r) {
    const BlobsetEntry& e = r.entry;
    const bool legacy = r.version == static_cast<int>(CoreDefs::BlobsetVersion::V1)
                        || r.version == static_cast<int>(CoreDefs::BlobsetVersion::V2);   // Container offsets.

    out.section(u"Blobset Info"_s);
    out.field(u"FileIndex"_s, QString::number(e.fileIndex));
    out.field(u"PathHash"_s, u"0x"_s + r.hexHash.toUpper());
    out.field(u"FileName"_s, entryFileName(r));
    if (legacy) {
        out.field(u"MainFinalOffset"_s, u"0x"_s + QString::number(e.mainFinalOffset, 16).toUpper());
        out.field(u"VramFinalOffset"_s, u"0x"_s + QString::number(e.vramFinalOffset, 16).toUpper());
    }
    out.field(u"MainCompressedSize"_s, QString::number(e.mainCompressedSize));
    out.field(u"MainUnCompressedSize"_s, QString::number(e.mainUnCompressedSize));
    out.field(u"VramCompressedSize"_s, QString::number(e.vramCompressedSize));
    out.field(u"VramUnCompressedSize"_s, QString::number(e.vramUnCompressedSize));
}

void addTxpkInfo(InfoHtml& out, const AssetInfoRequest& r) {
    const qint64 totalSize = static_cast<qint64>(r.entry.mainUnCompressedSize) + r.entry.vramUnCompressedSize;
    const bool bigEndian = r.platform != static_cast<int>(CoreDefs::Platforms::Windows);

    // The texture list lives in the main section, so the (big) vram section is never read.
    QByteArray mainPart = BlobsetAssetReader::read(r.entry, r.gameLocation, r.version, r.platform, nullptr, false);
    QBuffer buffer(&mainPart);
    buffer.open(QIODevice::ReadOnly);
    PackageStream in(&buffer, bigEndian ? PackageStream::Big : PackageStream::Little);

    TXPK txpk;
    if (mainPart.size() >= 8) {
        const uchar* raw = reinterpret_cast<const uchar*>(mainPart.constData());
        const quint32 count = bigEndian ? qFromBigEndian<quint32>(raw + 4) : qFromLittleEndian<quint32>(raw + 4);
        if (static_cast<qint64>(count) * 16 + 8 > mainPart.size())
            throw std::runtime_error("Invalid TXPK header (bad file count).");
        txpk.deserialize(in, bigEndian);
    }

    out.section(u"TXPK Info"_s);
    out.field(u"DDS File Count"_s, QString::number(txpk.getEntries().size()));
    out.field(u"TXPK File Size"_s, DdsImageProvider::humanSize(totalSize));

    out.section(u"TXPK DDS File List"_s);
    if (txpk.getEntries().isEmpty()) {
        out.line(u"(empty TXPK - no textures)"_s, kInfoKeyColor);
        return;
    }
    int i = 0;
    for (const TXPK::Entry& e : txpk.getEntries()) {
        QString path = e.DDSFilePath;
        path.replace(u'/', u'\\');
        if (r.gameId > 15)
            path.replace(u".badds"_s, u".dds"_s); // Newer BigAnt games map .badds to .dds
        if (!QFileInfo(QString(path).replace(u'\\', u'/')).fileName().contains(u'.'))
            path += u".dds"_s;
        out.line(u"%1 - %2"_s.arg(i++).arg(path));
    }
}

void addM3mpInfo(InfoHtml& out, const AssetInfoRequest& r) {
    const qint64 totalSize = static_cast<qint64>(r.entry.mainUnCompressedSize) + r.entry.vramUnCompressedSize;
    const bool bigEndian = r.platform != static_cast<int>(CoreDefs::Platforms::Windows);

    // Header, path table and chunk table are all in the main section.
    QByteArray mainPart = BlobsetAssetReader::read(r.entry, r.gameLocation, r.version, r.platform, nullptr, false);
    if (mainPart.size() < 16)
        throw std::runtime_error("The M3MP is too small to hold a header.");

    const uchar* raw = reinterpret_cast<const uchar*>(mainPart.constData());
    const quint32 count = bigEndian ? qFromBigEndian<quint32>(raw + 4) : qFromLittleEndian<quint32>(raw + 4);
    if (static_cast<qint64>(count) * 16 + 16 > mainPart.size())
        throw std::runtime_error("Invalid M3MP header (bad file count).");

    QBuffer buffer(&mainPart);
    buffer.open(QIODevice::ReadOnly);
    PackageStream in(&buffer, bigEndian ? PackageStream::Big : PackageStream::Little);

    M3MP m3mp;
    m3mp.deserialize(in, bigEndian);
    const auto& files = m3mp.getUnCompressedEntries();

    out.section(u"M3MP Info"_s);
    out.field(u"M3MP File Count"_s, QString::number(files.size()));
    out.field(u"M3MP File Size"_s, DdsImageProvider::humanSize(totalSize));

    out.section(u"M3MP File List"_s);
    if (files.isEmpty()) {
        out.line(u"(empty M3MP - no files)"_s, kInfoKeyColor);
        return;
    }
    int i = 0;
    for (const M3MP::UnCompressedEntry& e : files) {
        QString path = e.filePath;
        path.replace(u'/', u'\\');
        out.line(u"%1 - %2"_s.arg(i++).arg(path));
    }
}

// "8.39 KB" - two decimals like the WEM readout of the original tool.
QString preciseSize(double bytes) {
    if (bytes >= 1024.0 * 1024.0)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + u" MB"_s;
    if (bytes >= 1024.0)
        return QString::number(bytes / 1024.0, 'f', 2) + u" KB"_s;
    return QString::number(qRound64(bytes)) + u" B"_s;
}

// "0:01.037"
QString formatDuration(double seconds) {
    const qint64 ms = qRound64(seconds * 1000.0);
    return u"%1:%2.%3"_s.arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString waveFormatName(quint16 tag) {
    switch (tag) {
    case 0x0001: return u"PCM"_s;
    case 0x0002: return u"Microsoft ADPCM"_s;
    case 0x0003: return u"IEEE Float"_s;
    case 0x0011: return u"IMA ADPCM"_s;
    case 0x0055: return u"MP3"_s;
    case 0x0165: return u"XMA"_s;
    case 0x0166: return u"XMA2"_s;
    case 0xFFFE: return u"Extensible"_s;
    case 0xFFFF: return u"Wwise Vorbis"_s;
    default:     return u"0x%1"_s.arg(static_cast<uint>(tag), 4, 16, QLatin1Char('0')).toUpper().replace(u"0X"_s, u"0x"_s);
    }
}

// WEM and WAV are both RIFF (RIFX on consoles) audio: the same readout, labelled per type.
void addRiffAudioInfo(InfoHtml& out, const AssetInfoRequest& r, const QString& title, const QString& label,
                      const QString& virtualExt) {
    const QByteArray data = BlobsetAssetReader::read(r.entry, r.gameLocation, r.version, r.platform);
    if (data.size() < 12 || (!data.startsWith("RIFF") && !data.startsWith("RIFX")) || data.mid(8, 4) != "WAVE")
        throw std::runtime_error("Not a RIFF / WAVE stream.");

    // The "fmt " chunk: format tag, channels, sample rate, average bytes per second, block align, bits.
    const bool bigEndian = data.startsWith("RIFX");
    const uchar* raw = reinterpret_cast<const uchar*>(data.constData());
    const auto u16 = [&](qsizetype at) { return bigEndian ? qFromBigEndian<quint16>(raw + at) : qFromLittleEndian<quint16>(raw + at); };
    const auto u32 = [&](qsizetype at) { return bigEndian ? qFromBigEndian<quint32>(raw + at) : qFromLittleEndian<quint32>(raw + at); };

    quint16 formatTag = 0, channels = 0, blockAlign = 0, bits = 0;
    quint32 sampleRate = 0, avgBytes = 0, dataSize = 0;
    bool haveFmt = false;
    for (qsizetype pos = 12; pos + 8 <= data.size();) {
        const QByteArray id = data.mid(pos, 4);
        const quint32 size = u32(pos + 4);
        if (id == "fmt " && size >= 16 && pos + 8 + 16 <= data.size()) {
            formatTag = u16(pos + 8);
            channels = u16(pos + 10);
            sampleRate = u32(pos + 12);
            avgBytes = u32(pos + 16);
            blockAlign = u16(pos + 20);
            bits = u16(pos + 22);
            if (formatTag == 0xFFFE && size >= 40 && pos + 34 <= data.size())
                formatTag = u16(pos + 32);   // WAVE_FORMAT_EXTENSIBLE: the sub format GUID starts with the tag.
            haveFmt = true;
        } else if (id == "data") {
            dataSize = size;
        }
        pos += 8 + size + (size & 1);
    }
    if (!haveFmt)
        throw std::runtime_error("The file has no fmt chunk.");

    // vgmstream knows the real sample count (Vorbis / ADPCM WEMs don't have a fixed bytes-per-second ratio).
    Metadata meta;
    QString name = QFileInfo(QString(r.workspacePath).replace(u'\\', u'/')).fileName();
    if (!name.endsWith(virtualExt, Qt::CaseInsensitive))
        name += virtualExt;
    double seconds = -1.0;
    if (AudioProcessor::probeAudioBuffer(data, name, meta) && meta.sampleRate > 0 && meta.totalSamples > 0)
        seconds = static_cast<double>(meta.totalSamples) / meta.sampleRate;
    else if (avgBytes > 0 && dataSize > 0)
        seconds = static_cast<double>(dataSize) / avgBytes;

    out.section(title + u" Info"_s);
    if (label != u"WEM"_s)
        out.field(label + u" Format"_s, waveFormatName(formatTag));
    if (!meta.codecName.isEmpty())
        out.field(label + u" Codec"_s, meta.codecName);
    out.field(label + u" Channel Count"_s, QString::number(channels));
    out.field(label + u" Sample Rate"_s, u"%1 Hz"_s.arg(sampleRate));
    out.field(label + u" Bit Rate"_s, u"%1 Bit"_s.arg(bits > 0 ? bits : 16)); // Compressed audio stores 0: decodes to 16 bit.
    out.field(label + u" Average Bytes Per Second"_s, preciseSize(avgBytes));
    if (label != u"WEM"_s)
        out.field(label + u" Block Align"_s, QString::number(blockAlign));
    out.field(label + u" Duration"_s, seconds >= 0 ? formatDuration(seconds) : u"Unknown"_s);
    out.field(label + u" File Size"_s, preciseSize(static_cast<double>(data.size())));
    if (meta.hasLoopPoints)
        out.field(label + u" Loop"_s, u"%1 - %2 samples"_s.arg(meta.loopStartSample).arg(meta.loopEndSample));
}

void addMeshInfo(InfoHtml& out, const AssetInfoRequest& r) {
    qint64 mainSize = -1;
    const QByteArray data = BlobsetAssetReader::read(r.entry, r.gameLocation, r.version, r.platform, &mainSize, true);

    MeshFile mesh;
    QString error;
    out.section(u"MESH Info"_s);
    if (!mesh.parse(data, &error, mainSize)) {
        out.field(u"Mesh Size"_s, DdsImageProvider::humanSize(data.size()));
        out.error(error);
        return;
    }

    const QVector3D size = mesh.boundsMax - mesh.boundsMin;
    out.field(u"Vertex Format"_s, mesh.formatName);
    if (mesh.legacyLayout)
        out.field(u"Vertex Buffers"_s, QString::number(mesh.vertexBufferCount));
    out.field(u"Vertex Streams"_s, QString::number(mesh.streams.size()));
    out.field(u"Vertices"_s, QString::number(mesh.vertexCount));
    out.field(u"Triangles"_s, QString::number(mesh.triangleCount()));
    out.field(u"Submeshes"_s, QString::number(mesh.submeshes.size()));
    out.field(u"Size"_s, u"%1 x %2 x %3"_s.arg(size.x(), 0, 'f', 2).arg(size.y(), 0, 'f', 2).arg(size.z(), 0, 'f', 2));
    out.field(u"Mesh Size"_s, DdsImageProvider::humanSize(data.size()));
}

void addDdsInfo(InfoHtml& out, const AssetInfoRequest& r) {
    QString error;
    const QVariantMap info = DdsImageProvider::readTextureInfo(r.hexHash, &error);
    if (info.isEmpty())
        throw std::runtime_error(error.toStdString());

    const QString format = info.value(u"format"_s).toString();
    const bool compressed = format.startsWith(u"DXT"_s) || format.startsWith(u"ATI"_s) || format.startsWith(u"BC"_s);

    out.section(u"DDS Info"_s);
    out.field(u"Format"_s, format.isEmpty() ? u"Unknown"_s : format);
    if (format != u"DX10"_s)
        out.field(u"Compression"_s, compressed ? u"Compressed"_s : u"UnCompressed"_s);
    out.field(u"Width"_s, QString::number(info.value(u"width"_s).toUInt()));
    out.field(u"Height"_s, QString::number(info.value(u"height"_s).toUInt()));
    out.field(u"MipMaps"_s, QString::number(info.value(u"mipMaps"_s).toUInt()));
    out.field(u"Size"_s, info.value(u"size"_s).toString());
}

QString buildAssetInfoHtml(const AssetInfoRequest& r) {
    const QString type = assetTypeLabel(r.workspacePath);
    const QString ext = type.toLower();
    const QString title = ext == u"wem"_s ? u"WWise Audiokinetic WEM"_s
                        : ext == u"wav"_s ? u"WAV Audio"_s
                                          : type;

    InfoHtml out;
    out.section(title + u" Location"_s);
    out.line(r.workspacePath);

    if (!r.found) {
        out.section(u"Blobset Info"_s);
        out.error(r.gameLocation.isEmpty() ? u"Unavailable: the game location isn't set."_s
                                           : u"This file isn't in the loaded blobset."_s);
        return out.html();
    }

    addBlobsetInfo(out, r);

    try {
        if (ext == u"txpk"_s) {
            addTxpkInfo(out, r);
        } else if (ext == u"dds"_s) {
            addDdsInfo(out, r);
        } else if (ext == u"m3mp"_s) {
            addM3mpInfo(out, r);
        } else if (ext == u"mesh"_s) {
            addMeshInfo(out, r);
        } else if (ext == u"wem"_s) {
            addRiffAudioInfo(out, r, title, u"WEM"_s, u".wem"_s);
        } else if (ext == u"wav"_s) {
            addRiffAudioInfo(out, r, title, u"WAV"_s, u".wav"_s);
        } else {
            out.section(title + u" Info"_s);
            out.field(u"File Size"_s, DdsImageProvider::humanSize(static_cast<qint64>(r.entry.mainUnCompressedSize)
                                                                  + r.entry.vramUnCompressedSize));
        }
    } catch (const std::exception& ex) {
        out.section(title + u" Info"_s);
        out.error(u"Could not read the %1: %2"_s.arg(type, QString::fromUtf8(ex.what())));
    }
    return out.html();
}

} // namespace

AppOrchestrator* AppOrchestrator::s_instance = nullptr;

AppOrchestrator::AppOrchestrator(QObject* parent)
    : QObject(parent),
    m_gameId(0),
    m_steamGameId(0),
    m_blobsetVersion(0),
    m_platformId(0),
    m_isBigEndian(false)
{
    s_instance = this;
    m_treeModel = new TreeModel(this);

    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_modLoaderMode = settings.value(u"Settings/ModLoader"_s, false).toBool();
    m_loadGameMode = settings.value(u"Settings/LoadGame"_s, false).toBool();
}

AppOrchestrator::~AppOrchestrator() {
    if (s_instance == this)
        s_instance = nullptr;
}

AppOrchestrator* AppOrchestrator::instance() {
    return s_instance;
}

// ---------------------------------------------------------------------------
// Thread-safe getters
// ---------------------------------------------------------------------------

QString AppOrchestrator::gameLocation() const {
    QMutexLocker locker(&m_mutex);
    return m_gameLocation;
}

int AppOrchestrator::blobsetVersion() const {
    QMutexLocker locker(&m_mutex);
    return m_blobsetVersion;
}

int AppOrchestrator::platform() const {
    QMutexLocker locker(&m_mutex);
    return m_platformId;
}

QString AppOrchestrator::currentGameImage() {
    if (loadedGames.isEmpty()) {
        // Normally read by the game selection screen; read it here when MainForm asks first.
        XmlGameSelectionInfo xmlParser;
        if (xmlParser.deserialize(AppPaths::gameSelectionInfoFile()))
            loadedGames = xmlParser.games;
    }

    const int index = gameId();   // The game list order is the game id (see GameSelection.qml).
    if (index < 0 || index >= loadedGames.size() || loadedGames[index].image.isEmpty())
        return {};

    // Same folder GameSelection.qml shows it from.
    const QString path = QDir(AppPaths::installDir()).filePath(u"assets/games/"_s + loadedGames[index].image);
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString();
}

uint AppOrchestrator::steamGameId() const {
    QMutexLocker locker(&m_mutex);
    return m_steamGameId;
}

int AppOrchestrator::gameId() const {
    QMutexLocker locker(&m_mutex);
    return m_gameId;
}

QString AppOrchestrator::currentGameName() const {
    QMutexLocker locker(&m_mutex);
    return m_gameName.isEmpty() ? m_modGameFolderName : m_gameName;
}

QString AppOrchestrator::getHashFromPath(const QString& filePath) const {
    QMutexLocker locker(&m_mutex);
    const QString cleanPath = filePath.trimmed().replace(u'\\', u'/');

    const quint64 rawHash = m_pathHashLookup.value(cleanPath, 0);
    return BlobsetEntryRegistry::makeKey(rawHash);
}

QVariantList AppOrchestrator::searchAssets(const QString& query, int maxResults) const {
    const QString q = query.trimmed().toLower().replace(u'\\', u'/');
    if (q.isEmpty())
        return {};

    // "0x1A2B..." or a long run of hex digits also matches the path hashes.
    QString hexQuery = q.startsWith(u"0x"_s) ? q.mid(2) : q;
    static const QRegularExpression hexOnly(u"^[0-9a-f]{4,16}$"_s);
    if (!hexOnly.match(hexQuery).hasMatch())
        hexQuery.clear();

    struct Hit {
        int rank;
        QString folder;
        QString file;
        QString path;
    };
    std::vector<Hit> hits;
    QSet<QString> folders;

    // 0: exact name, 1: name starts with it, 2: name contains it, 3: path contains it, 4: hash contains it.
    const auto rankName = [&](const QString& name, const QString& path) -> int {
        const QString lower = name.toLower();
        const qsizetype dot = lower.indexOf(u'.');
        if (lower == q || (dot > 0 && lower.left(dot) == q))
            return 0;
        if (lower.startsWith(q))
            return 1;
        if (lower.contains(q))
            return 2;
        if (path.contains(q, Qt::CaseInsensitive))
            return 3;
        return -1;
    };

    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_pathHashLookup.cbegin(); it != m_pathHashLookup.cend(); ++it) {
            const QString& path = it.key();
            const qsizetype slash = path.lastIndexOf(u'/');
            const QString folder = slash >= 0 ? path.left(slash) : QString();
            const QString file = path.mid(slash + 1);

            int rank = rankName(file, path);
            if (rank < 0 && !hexQuery.isEmpty() && QString::number(it.value(), 16).contains(hexQuery))
                rank = 4;
            if (rank >= 0)
                hits.push_back({rank, folder, file, path});

            for (qsizetype p = folder.size(); p > 0; p = folder.lastIndexOf(u'/', p - 1)) {
                const QString f = folder.left(p);
                if (folders.contains(f))
                    break;   // Its parents were added with it.
                folders.insert(f);
            }
        }
    }

    for (const QString& folder : std::as_const(folders)) {
        const int rank = rankName(folder.mid(folder.lastIndexOf(u'/') + 1), QString());
        if (rank >= 0)
            hits.push_back({rank, folder, QString(), folder});
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        if (a.file.isEmpty() != b.file.isEmpty())
            return a.file.isEmpty();   // A folder before the files of the same rank.
        return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });

    QVariantList results;
    const int count = std::min<int>(int(hits.size()), std::max(1, maxResults));
    results.reserve(count);
    for (int i = 0; i < count; ++i)
        results.append(QVariantMap{{u"folder"_s, hits[size_t(i)].folder},
                                   {u"file"_s, hits[size_t(i)].file},
                                   {u"path"_s, hits[size_t(i)].path}});
    return results;
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// The exe's folder (games/, GameSelectionInfo.xml and Settings.ini live next to it), whatever the working folder is.
// The writable data folder (Documents\Blobset Tools): Settings.ini and the games folder. See app_paths.h.
QString AppOrchestrator::appPath() {
    return AppPaths::dataDir();
}

QString AppOrchestrator::settingsPath() const {
    return QDir(appPath()).filePath(u"Settings.ini"_s);
}

QString AppOrchestrator::platformFolder() const {
    switch (m_platformId) {
    case 1:  return u"ps3"_s;
    case 2:  return u"xbox360"_s;
    default: return u"pc"_s;
    }
}

QVariantMap AppOrchestrator::gameInfoPreview(const QString& modGameFolderName, int platformId) const {
    static const QString platformFolders[] = {u"pc"_s, u"ps3"_s, u"xbox360"_s};
    static const QString platformLabels[] = {u"Windows"_s, u"PlayStation 3"_s, u"Xbox 360"_s};
    const int platformIndex = (platformId >= 0 && platformId < 3) ? platformId : 0;
    const QString folder = platformFolders[platformIndex];

    QVariantMap info;
    info[u"platform"_s] = platformLabels[platformIndex];
    if (modGameFolderName.isEmpty())
        return info;

    const QDir platformDir(QDir(appPath()).filePath(u"games/%1/%2"_s.arg(modGameFolderName, folder)));
    const QString iniPath = platformDir.filePath(u"GameInfo.ini"_s);
    info[u"found"_s] = QFileInfo::exists(iniPath);
    if (!info[u"found"_s].toBool())
        return info;

    QSettings ini(iniPath, QSettings::IniFormat);
    ini.beginGroup(u"GameInfo"_s);
    const int version = ini.value(u"BlobsetVersion"_s, 3).toInt();
    const QString location = QDir::fromNativeSeparators(ini.value(u"GameLocation"_s).toString().trimmed());
    info[u"gameName"_s] = ini.value(u"GameName"_s).toString();
    info[u"gameId"_s] = ini.value(u"GameId"_s, 0).toInt();
    info[u"gameVersion"_s] = ini.value(u"Version"_s).toString();
    info[u"steamGameId"_s] = ini.value(u"SteamGameId"_s, 0).toUInt();
    ini.endGroup();

    // CoreDefs::BlobsetVersion is 0-based (V1 = 0 ... V4 = 3).
    info[u"blobsetVersion"_s] = u"V%1"_s.arg(version + 1);
    info[u"compression"_s] = version >= 3 ? u"ZSTD"_s : u"LZMA"_s;
    info[u"gameLocation"_s] = QDir::toNativeSeparators(location);

    const QFileInfo blobset(location);
    info[u"locationExists"_s] = !location.isEmpty() && blobset.isFile();
    if (blobset.isFile())
        info[u"blobsetSize"_s] = QLocale::system().formattedDataSize(blobset.size(), 2, QLocale::DataSizeTraditionalFormat);

    const QFileInfo mapping(platformDir.filePath(u"data/data-0.blobset.%1.mapping"_s.arg(folder)));
    info[u"mappingExists"_s] = mapping.isFile();
    if (mapping.isFile())
        info[u"mappingDate"_s] = QLocale::system().toString(mapping.lastModified(), QLocale::ShortFormat);

    // Mod files sit in category subfolders (characters, dds_txpk, ...), so count recursively.
    int modCount = 0;
    for (QDirIterator it(platformDir.filePath(u"mods"_s), QDir::Files, QDirIterator::Subdirectories); it.hasNext(); it.next())
        ++modCount;
    info[u"modCount"_s] = modCount;
    return info;
}

QString AppOrchestrator::archiveName() const {
    return u"data-0.blobset."_s + platformFolder();
}

QString AppOrchestrator::gameInfoIniPath() const {
    return QDir(appPath()).filePath(u"games/%1/%2/GameInfo.ini"_s.arg(m_modGameFolderName, platformFolder()));
}

QString AppOrchestrator::modsFolderPath() const {
    if (m_modGameFolderName.isEmpty())
        return {};
    return QDir(appPath()).filePath(u"games/%1/%2/mods"_s.arg(m_modGameFolderName, platformFolder()));
}

QString AppOrchestrator::dataFolderPath() const {
    return QDir::toNativeSeparators(AppPaths::dataDir());
}

QString AppOrchestrator::platformDirPath() const {
    if (m_modGameFolderName.isEmpty())
        return {};
    return QDir(appPath()).filePath(u"games/%1/%2"_s.arg(m_modGameFolderName, platformFolder()));
}

QString AppOrchestrator::gameRootPath() const {
    const QString location = gameLocation();
    if (location.isEmpty())
        return {};
    // <root>\data-0.blobset.pc, or <root>\data\data-0.blobset.pc for the newer games.
    QDir root = QFileInfo(location).absoluteDir();
    if (m_relativeBlobsetPath.contains(u'/'))
        root.cdUp();
    return root.absolutePath();
}

QString AppOrchestrator::externalModsPath() const {
    // Only PC games are deployed by Vortex.
    const QString root = gameRootPath();
    if (root.isEmpty() || m_platformId != 0)
        return {};
    return QDir(root).filePath(QString::fromLatin1(ModLibrary::kExternalFolder));
}

ModLibrary AppOrchestrator::modLibrary() const {
    return ModLibrary(platformDirPath(), externalModsPath(), ModifyWorker::supportedExtensions(blobsetVersion()));
}

QSet<QString> AppOrchestrator::mappedPaths() const {
    QSet<QString> paths;
    const QString mapping = findMappingFile();
    if (mapping.isEmpty())
        return paths;
    try {
        const std::vector<FileMappingWorker::MappingEntry> entries = FileMappingWorker::readMappingFile(mapping);
        paths.reserve(static_cast<qsizetype>(entries.size()));
        for (const FileMappingWorker::MappingEntry& entry : entries)
            paths.insert(QString(entry.path).replace(u'\\', u'/').toLower());
    } catch (const std::exception&) {
        // No usable mapping: mods are still installed, their root found by their folder names.
    }
    return paths;
}

bool AppOrchestrator::selectGameHeadless(int gameId, int platformId, const QString& modGameFolderName,
                                         const QString& gameFolderName) {
    {
        QMutexLocker locker(&m_mutex);
        m_gameId = gameId;
        m_platformId = platformId;
    }
    m_modGameFolderName = modGameFolderName;
    m_gameFolderName = gameFolderName;
    m_isBigEndian = platformId > 0;
    m_relativeBlobsetPath = gameId < 11 ? archiveName() : u"data/"_s + archiveName();

    if (!QFileInfo::exists(gameInfoIniPath())) {
        emit uiConsoleUpdate(u"Error: %1 has no GameInfo.ini for %2 (%3)."_s
                                 .arg(modGameFolderName, platformFolder(), QDir::toNativeSeparators(gameInfoIniPath())));
        return false;
    }
    readGameIni(gameInfoIniPath());
    resolveGameLocation();
    return true;
}

/// The mapping lives in the data folder; one shipped with Blobset Tools (install folder, or a few levels above a
/// build folder) is used until Update File Mapping Data writes the game's own.
QString AppOrchestrator::findMappingFile() const {
    const QString relative = u"games/%1/%2/data/%3.mapping"_s.arg(m_modGameFolderName, platformFolder(), archiveName());
    const QString inData = QDir(appPath()).filePath(relative);
    if (QFile::exists(inData))
        return inData;

    const QDir install(AppPaths::installDir());
    for (const QString& prefix : {u"defaults/"_s, QString(), u"../"_s, u"../../../../"_s}) {
        const QString candidate = QDir::cleanPath(install.filePath(prefix + relative));
        if (QFile::exists(candidate))
            return candidate;
    }
    return {};
}

// ---------------------------------------------------------------------------
// INI / game location
// ---------------------------------------------------------------------------

void AppOrchestrator::readGameIni(const QString& iniPath) {
    const QFileInfo iniFileInfo(iniPath);
    if (!iniFileInfo.isFile()) {
        emit uiConsoleUpdate(u"Warning: INI file not found at %1"_s.arg(iniPath));
        return;
    }

    QSettings iniSettings(iniPath, QSettings::IniFormat);
    iniSettings.beginGroup(u"GameInfo"_s);

    const QString gameVersion = iniSettings.value(u"Version"_s, u"1.00"_s).toString();
    QString gameName;
    int version = 0;
    {
        QMutexLocker locker(&m_mutex);
        m_gameId = iniSettings.value(u"GameId"_s, 0).toInt();
        m_gameName = iniSettings.value(u"GameName"_s).toString();
        m_blobsetVersion = iniSettings.value(u"BlobsetVersion"_s, 3).toInt();
        m_gameLocation = QDir::fromNativeSeparators(iniSettings.value(u"GameLocation"_s).toString().trimmed());
        m_steamGameId = iniSettings.value(u"SteamGameId"_s, 0).toUInt();
        gameName = m_gameName;
        version = m_blobsetVersion;
    }
    iniSettings.endGroup();

    emit gameInfoChanged();
    emit uiConsoleUpdate(u"Loaded INI for %1 (Version: %2, Blobset v%3)"_s.arg(gameName, gameVersion).arg(version + 1));
}

void AppOrchestrator::storeGameLocation(const QString& blobsetFile) {
    {
        QMutexLocker locker(&m_mutex);
        m_gameLocation = blobsetFile;
    }

    QSettings ini(gameInfoIniPath(), QSettings::IniFormat);
    ini.setValue(u"GameInfo/GameLocation"_s, blobsetFile);
    ini.sync();
}

bool AppOrchestrator::resolveGameLocation() {
    // 1. GameInfo.ini - the blobset itself, or the game folder that holds it.
    QString location = gameLocation();
    if (!location.isEmpty() && QFileInfo(location).isDir())
        location = QDir(location).filePath(m_relativeBlobsetPath);

    if (!location.isEmpty() && QFileInfo(location).isFile()) {
        QMutexLocker locker(&m_mutex);
        m_gameLocation = location;
        return true;
    }

    // 2. Steam library (games without a Steam id have to be located by hand).
    if (m_steamGameId > 0) {
        const QString steamLocation = Utilities::getSteamGamesLocation(m_steamGameId);
        if (!steamLocation.isEmpty()) {
            const QString discovered = QDir(steamLocation).filePath(
                u"steamapps/common/%1/%2"_s.arg(m_gameFolderName, m_relativeBlobsetPath));
            if (QFileInfo(discovered).isFile()) {
                storeGameLocation(discovered);
                emit uiConsoleUpdate(u"Found the game in the Steam library: %1"_s.arg(discovered));
                return true;
            }
        }
    }
    return false;
}

void AppOrchestrator::checkSteamUpdate() {
    if (m_headless || m_platformId != 0 || m_workerRunning)
        return;
    const uint appId = steamGameId();
    const SteamManifest steam = readSteamManifest(appId);
    if (!steam.found)
        return;   // Not a Steam game, or Steam / the manifest can't be found.

    const QString name = currentGameName();

    // 1. Steam has an update waiting ("Only update at game launch"): restore the original files before it installs.
    const bool targetDiffers = !steam.targetBuildId.isEmpty() && steam.targetBuildId != u"0"_s
                               && steam.targetBuildId != steam.buildId;
    if ((steam.stateFlags & 2u) || targetDiffers) {
        const QString key = u"pending:%1:%2"_s.arg(appId).arg(steam.targetBuildId);
        if (!m_steamNoticesShown.contains(key)) {
            m_steamNoticesShown.insert(key);
            emit uiConsoleUpdate(u"Steam has an update waiting for %1 (build %2 -> %3)."_s
                                     .arg(name, steam.buildId, targetDiffers ? steam.targetBuildId : u"?"_s));
            emit steamUpdateDetected(true, name, targetDiffers ? u"Installed build %1, update build %2."_s
                                                                     .arg(steam.buildId, steam.targetBuildId)
                                                               : u"Installed build %1."_s.arg(steam.buildId));
        }
        return;
    }

    // 2. Updated since the mapping was made.
    QSettings ini(gameInfoIniPath(), QSettings::IniFormat);
    const QString mappedBuild = ini.value(u"GameInfo/SteamBuildId"_s).toString().trimmed();
    bool updated = false;
    if (mappedBuild.isEmpty()) {
        // Nothing recorded yet (a mapping made before this check existed, or shipped with Blobset Tools):
        // compare the mapping's date with Steam's last update instead, then remember the build.
        const QFileInfo mapping(findMappingFile());
        updated = mapping.isFile() && steam.lastUpdated > mapping.lastModified().toSecsSinceEpoch() + 60;
        if (!updated) {
            ini.setValue(u"GameInfo/SteamBuildId"_s, steam.buildId);
            return;
        }
    } else {
        updated = mappedBuild != steam.buildId;
    }
    if (!updated)
        return;

    const QString key = u"updated:%1:%2"_s.arg(appId).arg(steam.buildId);
    if (m_steamNoticesShown.contains(key))
        return;
    m_steamNoticesShown.insert(key);
    const QString details = mappedBuild.isEmpty() ? u"Steam updated it on %1, after its file mapping was made."_s
                                                        .arg(QLocale::system().toString(QDateTime::fromSecsSinceEpoch(steam.lastUpdated),
                                                                                        QLocale::ShortFormat))
                                                  : u"File mapping made for build %1, installed build %2."_s
                                                        .arg(mappedBuild, steam.buildId);
    emit uiConsoleUpdate(u"%1 was updated by Steam. %2 Run Options > Update File Mapping Data."_s.arg(name, details));
    emit steamUpdateDetected(false, name, details);
}

void AppOrchestrator::recordSteamBuild() {
    if (m_platformId != 0)
        return;
    const SteamManifest steam = readSteamManifest(steamGameId());
    if (!steam.found)
        return;
    QSettings ini(gameInfoIniPath(), QSettings::IniFormat);
    ini.setValue(u"GameInfo/SteamBuildId"_s, steam.buildId);
    ini.sync();
}

void AppOrchestrator::setFileMappingMissing(bool missing) {
    if (m_fileMappingMissing == missing)
        return;
    m_fileMappingMissing = missing;
    emit fileMappingMissingChanged();
}

void AppOrchestrator::setGameLocationRequired(bool required) {
    if (m_gameLocationRequired == required)
        return;
    m_gameLocationRequired = required;
    emit gameLocationRequiredChanged();
}

bool AppOrchestrator::setGameLocation(const QString& path) {
    QFileInfo info(QDir::fromNativeSeparators(toLocalPath(path)));
    if (info.isDir())
        info = QFileInfo(QDir(info.absoluteFilePath()).filePath(m_relativeBlobsetPath));

    if (!info.isFile()) {
        emit uiConsoleUpdate(u"Error: %1 doesn't exist."_s.arg(QDir::toNativeSeparators(info.absoluteFilePath())));
        return false;
    }

    if (info.fileName().compare(archiveName(), Qt::CaseInsensitive) != 0) {
        emit uiConsoleUpdate(u"Error: '%1' isn't the game's blobset, please select %2."_s.arg(info.fileName(), archiveName()));
        return false;
    }

    storeGameLocation(info.absoluteFilePath());
    setGameLocationRequired(false);
    emit uiConsoleUpdate(u"Game location set: %1"_s.arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    if (m_headless)
        return true;   // Command line: no workspace to load.

    const QString mappingPath = findMappingFile();
    if (mappingPath.isEmpty()) {
        if (std::exchange(m_updateMappingAfterLocation, false))
            updateFileMapping();          // The user already asked for it; it was waiting for the location.
        else
            setFileMappingMissing(true);  // MainForm offers to run it.
        return true;
    }

    m_treeModel->clear();
    startWorkspaceLoad(mappingPath);
    return true;
}

bool AppOrchestrator::ensureGameLocation() {
    if (QFileInfo(gameLocation()).isFile())
        return true;

    readGameIni(gameInfoIniPath());
    if (resolveGameLocation())
        return true;

    emit uiConsoleUpdate(u"Error: Unable to locate the active game container. Please set the game location."_s);
    setGameLocationRequired(true);
    emit gameLocationRequested();
    return false;
}

// ---------------------------------------------------------------------------
// Game selection / workspace loading
// ---------------------------------------------------------------------------

void AppOrchestrator::selectGameAndLoad(int gameId, int platformId, const QString& modGameFolderName,
                                        const QString& gameFolderName) {
    if (gameId < 0)
        return;

    {
        QMutexLocker locker(&m_mutex);
        m_gameId = gameId;
        m_platformId = platformId;
    }
    emit gameInfoChanged();   // MainForm's game picture follows the selection.
    m_modGameFolderName = modGameFolderName;
    m_gameFolderName = gameFolderName;
    m_isBigEndian = platformId > 0;
    m_relativeBlobsetPath = gameId < 11 ? archiveName() : u"data/"_s + archiveName();

    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.beginGroup(u"Settings"_s);
    settings.setValue(u"DefaultGame"_s, gameId);
    settings.setValue(u"Platform"_s, platformId);
    settings.setValue(u"ModGameFolderName"_s, modGameFolderName);
    settings.setValue(u"GameFolderName"_s, gameFolderName);
    settings.endGroup();

    m_updateMappingAfterLocation = false;
    const QString mappingPath = findMappingFile();

    // Reset before MainForm is created, so it never acts on the previous game's state.
    setGameLocationRequired(false);
    setFileMappingMissing(mappingPath.isEmpty());

    if (mappingPath.isEmpty()) {
        // No mapping yet: MainForm still opens (empty) and offers to run Update File Mapping Data.
        readGameIni(gameInfoIniPath());
        resolveGameLocation();
        BlobsetEntryRegistry::clear();
        m_treeModel->clear();
        emit viewChanged(kMainFormView);
        emit uiConsoleUpdate(u"Game Profile Set. Game: %1, Platform: %2"_s.arg(modGameFolderName, platformFolder().toUpper()));
        emit uiConsoleUpdate(u"There's no file mapping for %1 yet: run Options > Update File Mapping Data."_s
                                 .arg(currentGameName()));
        return;
    }

    emit uiConsoleUpdate(u"Game Profile Set. Game: %1, Platform: %2"_s.arg(modGameFolderName, platformFolder().toUpper()));
    emit viewChanged(kMainFormView);   // Loaded synchronously, so MainForm hears everything below.

    startWorkspaceLoad(mappingPath);
}

void AppOrchestrator::startWorkspaceLoad(const QString& mappingPath) {
    readGameIni(gameInfoIniPath());

    const bool located = resolveGameLocation();
    setGameLocationRequired(!located);
    if (!located) {
        emit uiConsoleUpdate(u"The game location for %1 isn't set. Select its %2 to load the assets."_s
                                 .arg(currentGameName(), archiveName()));
        emit gameLocationRequested();
    }

    const QString blobsetFile = located ? gameLocation() : QString();
    const int version = blobsetVersion();
    const int iniGameId = gameId();
    const bool bigEndian = m_isBigEndian;
    const quint64 generation = ++m_loadGeneration;

    QPointer<AppOrchestrator> safeThis(this);
    QThreadPool::globalInstance()->start([safeThis, mappingPath, blobsetFile, version, bigEndian, iniGameId, generation]() {
        WorkspaceLoad result = readWorkspace(mappingPath, blobsetFile, version, bigEndian, iniGameId);

        if (safeThis.isNull())
            return;

        QMetaObject::invokeMethod(safeThis.data(), [safeThis, result = std::move(result), generation]() mutable {
            if (safeThis.isNull() || generation != safeThis->m_loadGeneration)
                return; // The game or its location changed while this was loading.

            if (!result.error.isEmpty()) {
                emit safeThis->uiConsoleUpdate(u"Assets Tree Parsing Failed: %1"_s.arg(result.error));
                safeThis->setFileMappingMissing(true);   // Unreadable mapping: offer to make it again.
                return;
            }
            safeThis->setFileMappingMissing(result.filePaths.isEmpty());
            if (!result.filePaths.isEmpty())
                safeThis->checkSteamUpdate();

            {
                QMutexLocker locker(&safeThis->m_mutex);
                safeThis->m_pathHashLookup = std::move(result.pathHashes);
            }

            // Registered before the tree is built: MainForm previews the restored selection as soon as rows appear.
            BlobsetEntryRegistry::replaceAll(result.entries);

            safeThis->m_treeModel->buildTree(result.filePaths);

            if (!result.blobsetError.isEmpty())
                emit safeThis->uiConsoleUpdate(u"Warning: %1"_s.arg(result.blobsetError));

            if (result.entries.isEmpty())
                emit safeThis->uiConsoleUpdate(u"Folder tree loaded. Previews are unavailable until the game location is set."_s);
            else
                emit safeThis->uiConsoleUpdate(u"Asset registry maps synchronized. (%1 metadata frames bound successfully.)"_s
                                                   .arg(result.entries.size()));
        }, Qt::QueuedConnection);
    });
}

void AppOrchestrator::selectFolderIndex(const QModelIndex& index) {
    m_currentFolderFiles.clear();
    qsizetype longestCharCount = 0;

    const TreeNode* node = index.isValid() ? static_cast<TreeNode*>(index.internalPointer()) : m_treeModel->rootNode();
    if (node && node->isFolder) {
        for (const auto& child : node->children) {
            if (child->isFolder)
                continue;
            m_currentFolderFiles.append(child->name);
            longestCharCount = std::max(longestCharCount, child->name.length());
        }
    }

    m_maxFileItemWidth = std::max(180, static_cast<int>(longestCharCount * 6.8) + 36);
    emit currentFolderFilesChanged();
}

void AppOrchestrator::requestAssetInfo(const QString& hexHash, const QString& workspacePath) {
    AssetInfoRequest request;
    request.hexHash = BlobsetEntryRegistry::normalizeKey(hexHash);
    request.workspacePath = QString(workspacePath).replace(u'/', u'\\');
    request.found = !request.hexHash.isEmpty() && BlobsetEntryRegistry::findEntry(request.hexHash, request.entry);
    request.gameLocation = gameLocation();
    request.version = blobsetVersion();
    request.platform = platform();
    request.gameId = gameId();

    const quint64 ticket = ++m_assetInfoTicket;
    QPointer<AppOrchestrator> safeThis(this);

    QThreadPool::globalInstance()->start([safeThis, request = std::move(request), ticket]() {
        // Arrowing through a folder queues a request per file: only the newest one does the work.
        if (safeThis.isNull() || ticket != safeThis->m_assetInfoTicket.load())
            return;

        const QString html = buildAssetInfoHtml(request);

        QMetaObject::invokeMethod(safeThis.data(), [safeThis, html, ticket]() {
            if (!safeThis.isNull() && ticket == safeThis->m_assetInfoTicket.load())
                emit safeThis->assetInfoReady(html);
        }, Qt::QueuedConnection);
    });
}

void AppOrchestrator::openWorkspaceFileByHash(const QString& hexHash, const QString& fileExtension,
                                              const QString& displayName)
{
    enum class Viewer { Dds, Txpk, M3mp, Mesh, Bnk, Fmod, Str, Bsb, Hex };

    static const QHash<QString, Viewer> viewers = {
                                                    { u"dds"_s,  Viewer::Dds  },
                                                    { u"txpk"_s, Viewer::Txpk },
                                                    { u"m3mp"_s, Viewer::M3mp },
                                                    { u"mesh"_s, Viewer::Mesh },
                                                    { u"bnk"_s,  Viewer::Bnk  },
                                                    { u"bank"_s, Viewer::Fmod },
                                                    { u"fsb"_s,  Viewer::Fmod },
                                                    { u"str"_s,  Viewer::Str  },
                                                    { u"bsb"_s,  Viewer::Bsb  },
                                                    { u"dat"_s,  Viewer::Hex  },
                                                    };

    const QString ext = fileExtension.toLower().trimmed();
    const QString hash = hexHash.toLower().trimmed();
    const QString base = u"qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/"_s;

    const auto found = viewers.constFind(ext);
    const Viewer viewer = found != viewers.constEnd() ? found.value() : Viewer::Hex;
    const QString name = displayName.isEmpty() ? u"0x"_s + hash.toUpper() : displayName;

    // The view is loaded synchronously by main.qml, so its Connections exist before the open signal is raised.
    switch (viewer) {
    case Viewer::Dds:
        emit viewChanged(base + u"DDSViewer.qml"_s);
        emit openDdsViewerByHash(hash, name);
        break;
    case Viewer::Txpk:
        emit viewChanged(base + u"TXPKViewer.qml"_s);
        emit openTxpkViewerByHash(hash);
        break;
    case Viewer::M3mp:
        emit viewChanged(base + u"M3MPViewer.qml"_s);
        emit openM3mpViewerByHash(hash);
        break;
    case Viewer::Mesh:
        emit viewChanged(base + u"MeshViewer.qml"_s);
        emit openMeshViewerByHash(hash, name);
        break;
    case Viewer::Bnk:
        emit viewChanged(base + u"BNKViewer.qml"_s);
        emit openBnkViewerByHash(hash);
        break;
    case Viewer::Fmod:
        emit viewChanged(base + u"FMODViewer.qml"_s);
        emit openFmodViewerByHash(hash);
        break;
    case Viewer::Bsb:
        emit viewChanged(base + u"BSBViewer.qml"_s);
        emit openBsbViewerByHash(hash, name);
        break;
    case Viewer::Str:
        emit viewChanged(base + u"StringEditor.qml"_s);
        emit openStringEditorByHash(hash, name);
        break;
    case Viewer::Hex: {
        if (found == viewers.constEnd())
            emit uiConsoleUpdate(u"No viewer for '.%1' files, opening %2 in the Hex Inspector."_s.arg(ext, name));
        emit viewChanged(base + u"HexViewer.qml"_s);
        emit openHexViewerByHash(hash, name);
        break;
    }
    }
}

void AppOrchestrator::loadGameProfile() {
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.beginGroup(u"Settings"_s);
    {
        QMutexLocker locker(&m_mutex);
        m_gameId = settings.value(u"DefaultGame"_s, 0).toInt();
        m_platformId = settings.value(u"Platform"_s, 0).toInt();
    }
    m_modGameFolderName = settings.value(u"ModGameFolderName"_s).toString();
    m_gameFolderName = settings.value(u"GameFolderName"_s).toString();
    settings.endGroup();
    m_isBigEndian = m_platformId > 0;

    gameSelectionInfo();

    emit profileLoaded(m_gameId, m_platformId, m_modGameFolderName, m_gameFolderName);
    emit uiConsoleUpdate(u"Restored previous game profile settings."_s);
}

void AppOrchestrator::gameSelectionInfo() {
    XmlGameSelectionInfo xmlParser;
    if (!xmlParser.deserialize(AppPaths::gameSelectionInfoFile())) {
        emit uiConsoleUpdate(u"Failed to deserialize GameSelectionInfo.xml"_s);
        return;
    }

    loadedGames = xmlParser.games;

    QVariantList qmlList;
    qmlList.reserve(loadedGames.size());
    for (const GameEntry& entry : std::as_const(loadedGames)) {
        qmlList.append(QVariantMap{
            {u"name"_s, entry.name},
            {u"folderName"_s, entry.folderName},
            {u"modFolder"_s, entry.modFolder},
            {u"pc"_s, entry.pc},
            {u"ps3"_s, entry.ps3},
            {u"xbox360"_s, entry.xbox360},
            {u"image"_s, entry.image},
            });
    }

    emit gameSelectionInfoLoaded(qmlList);
}

// ---------------------------------------------------------------------------
// Workers
// ---------------------------------------------------------------------------

void AppOrchestrator::setModLoaderMode(bool enabled) {
    if (m_modLoaderMode == enabled)
        return;
    m_modLoaderMode = enabled;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(u"Settings/ModLoader"_s, enabled);
    emit modLoaderModeChanged();
    emit uiConsoleUpdate(enabled ? u"Mod Loader on: Modify (V4) builds the mods in <game>\\BlobsetMods and leaves the "
                                   u"game's files alone. Needs dinput8.dll (Blobset Mod Loader) next to the game."_s
                                 : u"Mod Loader off: Modify (V4) changes the game's files directly (with backups)."_s);
}

void AppOrchestrator::setLoadGameMode(bool enabled) {
    if (m_loadGameMode == enabled)
        return;
    m_loadGameMode = enabled;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(u"Settings/LoadGame"_s, enabled);
    emit loadGameModeChanged();
    emit uiConsoleUpdate(enabled ? u"Load Game on: the game starts through Steam after Modify / Create succeeded."_s
                                 : u"Load Game off."_s);
}

void AppOrchestrator::launchGame() {
    const uint steamId = steamGameId();
    if (steamId == 0) {
        emit uiConsoleUpdate(u"Load Game: this game has no Steam App ID (SteamGameId in GameInfo.ini)."_s);
        return;
    }
    emit uiConsoleUpdate(u"Starting %1 through Steam (App ID %2)..."_s.arg(currentGameName()).arg(steamId));
    if (!QDesktopServices::openUrl(QUrl(u"steam://rungameid/%1"_s.arg(steamId))))
        emit uiConsoleUpdate(u"Could not start the game: Steam doesn't seem to be installed."_s);
}

void AppOrchestrator::validateSteamFiles() {
    const uint steamId = steamGameId();
    if (steamId == 0) {
        emit uiConsoleUpdate(u"Validate Steam Files: this game has no Steam App ID (SteamGameId in GameInfo.ini)."_s);
        return;
    }
    emit uiConsoleUpdate(u"Asking Steam to verify the files of %1 (App ID %2). Steam replaces any changed game files; "
                         u"Mod Loader mods (BlobsetMods) aren't affected."_s.arg(currentGameName()).arg(steamId));
    if (!QDesktopServices::openUrl(QUrl(u"steam://validate/%1"_s.arg(steamId))))
        emit uiConsoleUpdate(u"Could not reach Steam: it doesn't seem to be installed."_s);
}

void AppOrchestrator::setWorkerRunning(bool running) {
    if (m_workerRunning == running)
        return;
    m_workerRunning = running;
    emit workerRunningChanged();
}

bool AppOrchestrator::startWorkerAllowed() {
    if (!m_workerRunning)
        return true;
    emit uiConsoleUpdate(u"Please wait: another task is still running."_s);
    return false;
}

template <typename Worker>
void AppOrchestrator::runWorker(Worker* worker, void (Worker::*start)(), bool reloadWorkspace) {
    setWorkerRunning(true);
    m_reloadAfterTask = reloadWorkspace;
    // The worker and its thread delete themselves once the task finished (taskFinished -> quit -> deleteLater).
    auto* workerThread = new QThread();
    worker->moveToThread(workerThread);

    // Last line of defence: an exception escaping the task (bad_alloc on a huge file, a parser bug) would
    // otherwise end the thread silently and leave the UI locked in "worker running".
    connect(workerThread, &QThread::started, worker, [worker, start]() {
        try {
            (worker->*start)();
        } catch (const std::exception& ex) {
            emit worker->taskFinished(u"The task stopped unexpectedly.\n\n"_s + QString::fromUtf8(ex.what()));
        } catch (...) {
            emit worker->taskFinished(u"The task stopped unexpectedly (unknown error)."_s);
        }
    });
    connect(worker, &Worker::progressUpdated, this, &AppOrchestrator::handleProgress);
    connect(worker, &Worker::updateConsole, this, &AppOrchestrator::handleConsoleUpdate);
    connect(worker, &Worker::updateStatus, this, &AppOrchestrator::handleStatusUpdate);
    connect(worker, &Worker::taskFinished, this, &AppOrchestrator::handleFinished);
    m_taskSucceeded = false;
    if constexpr (requires { &Worker::taskSucceeded; })
        connect(worker, &Worker::taskSucceeded, this, [this]() { m_taskSucceeded = true; });

    connect(worker, &Worker::taskFinished, workerThread, &QThread::quit);
    connect(worker, &Worker::taskFinished, worker, &QObject::deleteLater);
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);

    workerThread->start();
}

void AppOrchestrator::updateFileMapping() {
    if (!startWorkerAllowed())
        return;
    emit uiConsoleUpdate(u"Preparing dynamic Blobset Tools metadata sync from registry parameters..."_s);
    if (!ensureGameLocation()) {
        m_updateMappingAfterLocation = true;   // setGameLocation() carries on once the location is picked.
        return;
    }
    m_updateMappingAfterLocation = false;

    // Always written into the data folder. A mapping found elsewhere (shipped with Blobset Tools) is copied there
    // first, so a V4 update only has to add what changed.
    const QString outputMapping = QDir(appPath()).filePath(u"games/%1/%2/data/%3.mapping"_s
                                                               .arg(m_modGameFolderName, platformFolder(), archiveName()));
    const QString existing = findMappingFile();
    if (!existing.isEmpty() && QDir::cleanPath(existing) != QDir::cleanPath(outputMapping)) {
        QDir().mkpath(QFileInfo(outputMapping).absolutePath());
        QFile::remove(outputMapping);
        QFile::copy(existing, outputMapping);
    }

    // The mapping is about to be rewritten: empty the Folders and Files panels while it runs (the workspace is
    // read again when it's done), and drop any workspace load still in flight.
    ++m_loadGeneration;
    m_recordSteamBuildAfterTask = true;
    BlobsetEntryRegistry::clear();
    m_treeModel->clear();
    m_currentFolderFiles.clear();
    emit currentFolderFilesChanged();

    runWorker(new FileMappingWorker(gameLocation(), blobsetVersion(), m_isBigEndian, outputMapping, gameId(), platform()),
              &FileMappingWorker::generate_mapping);
}

void AppOrchestrator::restoreBackupFiles() {
    if (!startWorkerAllowed())
        return;
    if (!ensureGameLocation())
        return;
    restoreBackupFilesUnchecked();
}

void AppOrchestrator::restoreBackupFilesUnchecked() {
    const QDir platformDir(QDir(appPath()).filePath(u"games/%1/%2"_s.arg(m_modGameFolderName, platformFolder())));
    runWorker(new RestoreWorker(gameLocation(), blobsetVersion(), m_isBigEndian, platformDir.filePath(u"backup"_s),
                                gameId()),
              &RestoreWorker::restore_backups);
}

void AppOrchestrator::resolveFileNames() {
    if (!startWorkerAllowed())
        return;
    if (!ensureGameLocation())
        return;

    const QString mapping = findMappingFile();
    if (mapping.isEmpty()) {
        emit uiConsoleUpdate(u"There's no file mapping yet: run 'Update File Mapping Data' first."_s);
        return;
    }

    // Users can add their own candidate names (one per line) to games/<game>/<platform>/names.txt.
    const QDir platformDir(QDir(appPath()).filePath(u"games/%1/%2"_s.arg(m_modGameFolderName, platformFolder())));
    runWorker(new NameResolverWorker(gameLocation(), blobsetVersion(), m_isBigEndian, mapping,
                                     platformDir.filePath(u"names.txt"_s), BlobsetHash::gameKeyFor(currentGameName()),
                                     gameId(), platform()),
              &NameResolverWorker::resolve_names);
}

void AppOrchestrator::extractBlobset() {
    if (!startWorkerAllowed())
        return;
    emit uiConsoleUpdate(u"Preparing dynamic Blobset Tools metadata sync from registry parameters..."_s);
    if (!ensureGameLocation())
        return;

    const QString outputFolder = QDir(appPath()).filePath(u"games/%1/%2/%3"_s
                                                              .arg(m_modGameFolderName, platformFolder(), archiveName()));

    runWorker(new ExtractWorker(gameLocation(), blobsetVersion(), m_isBigEndian, outputFolder, m_modGameFolderName,
                                gameId(), platform()),
              &ExtractWorker::extract_blobset, false);   // Extract only reads the game.
}

void AppOrchestrator::modifyBlobset() {
    if (!startWorkerAllowed())
        return;
    emit uiConsoleUpdate(u"Preparing dynamic Blobset Tools metadata sync from registry parameters..."_s);
    if (!ensureGameLocation())
        return;

    ModLibrary library = modLibrary();
    library.load();
    const QStringList sources = library.enabledSources();
    const int files = library.enabledFileCount();

    int active = 0;
    for (const ModLibrary::Mod& mod : library.mods())
        if (mod.enabled && mod.fileCount > 0)
            ++active;
    emit uiConsoleUpdate(u"Mods: %1 file(s) from %2 enabled source(s) (workspace + Mod Manager, in load order)."_s
                             .arg(files).arg(active));

    const auto version = static_cast<CoreDefs::BlobsetVersion>(blobsetVersion());
    const bool loader = version == CoreDefs::BlobsetVersion::V4 && modLoaderMode();
    const bool inPlaceV3V4 = !loader && (version == CoreDefs::BlobsetVersion::V3 || version == CoreDefs::BlobsetVersion::V4);

    if (files == 0 && !loader) {
        // Nothing to apply: the game goes back to its original files.
        emit uiConsoleUpdate(u"No mods are enabled: restoring the original game files."_s);
        restoreBackupFilesUnchecked();
        return;
    }
    if (inPlaceV3V4) {
        // Files a removed / disabled mod changed have to go back first: Restore, then Modify.
        emit uiConsoleUpdate(u"Restoring the original game files before applying the mods..."_s);
        m_nextTask = [this, sources]() { startModify(sources); };
        restoreBackupFilesUnchecked();
        return;
    }
    startModify(sources);
}

void AppOrchestrator::startModify(const QStringList& sources) {
    const QDir platformDir(platformDirPath());

    auto* worker = new ModifyWorker(gameLocation(), blobsetVersion(), m_isBigEndian, platformDir.filePath(u"mods"_s),
                                    platformDir.filePath(u"backup"_s),
                                    platformDir.filePath(u"data/%1.mapping"_s.arg(archiveName())), gameId(), platform());
    worker->setModSources(sources);
    worker->setGameKey(BlobsetHash::gameKeyFor(currentGameName()));   // V3 hashes new assets with it.
    connect(worker, &ModifyWorker::taskSucceeded, this, [this]() { m_launchAfterTask = true; });
    if (blobsetVersion() == static_cast<int>(CoreDefs::BlobsetVersion::V4) && modLoaderMode()) {
        // <game>\data\data-0.blobset.pc -> <game>\BlobsetMods
        QDir gameRoot = QFileInfo(gameLocation()).absoluteDir();
        gameRoot.cdUp();
        worker->setModLoaderDir(gameRoot.filePath(u"BlobsetMods"_s));
    }
    runWorker(worker, &ModifyWorker::modify_blobset);
}

void AppOrchestrator::createBlobset() {
    if (!startWorkerAllowed())
        return;
    emit uiConsoleUpdate(u"Preparing dynamic Blobset Tools metadata sync from registry parameters..."_s);
    if (!ensureGameLocation())
        return;

    const QDir platformDir(QDir(appPath()).filePath(u"games/%1/%2"_s.arg(m_modGameFolderName, platformFolder())));

    // The untouched original blobset, kept in the backup folder. CreateWorker copies it there before the first
    // Create replaces the game's blobset, and reads original entries (".dat" files) from it.
    const QString originalCopy = QDir(platformDir.filePath(u"backup"_s)).filePath(QFileInfo(gameLocation()).fileName());

    auto* worker = new CreateWorker(gameLocation(), blobsetVersion(), m_isBigEndian, platformDir.filePath(u"mods"_s),
                                    platformDir.filePath(u"data/%1.mapping"_s.arg(archiveName())),
                                    originalCopy, gameId(), platform());
    connect(worker, &CreateWorker::taskSucceeded, this, [this]() { m_launchAfterTask = true; });
    runWorker(worker, &CreateWorker::create_blobset);
}

void AppOrchestrator::handleProgress(int percentage) { emit uiProgressUpdate(percentage); }
void AppOrchestrator::handleConsoleUpdate(const QString& message) { emit uiConsoleUpdate(message); }
void AppOrchestrator::handleStatusUpdate(const QString& message) { emit uiStatusUpdate(message); }

void AppOrchestrator::handleFinished(const QString& finalStatus) {
    setWorkerRunning(false);
    emit uiConsoleUpdate(finalStatus);   // The workers say themselves whether it worked.
    emit uiProgressUpdate(0);

    // A new mapping matches the installed Steam build: no "game updated" popup until Steam updates it again.
    if (std::exchange(m_recordSteamBuildAfterTask, false) && m_taskSucceeded)
        recordSteamBuild();

    // A chain (Restore -> Modify): the next step only runs when this one worked.
    if (m_nextTask) {
        std::function<void()> next = std::move(m_nextTask);
        m_nextTask = nullptr;
        if (m_taskSucceeded) {
            next();
            return;
        }
        m_launchAfterTask = false;
    }
    emit taskCompleted(m_taskSucceeded, finalStatus);

    if (m_headless) {
        m_launchAfterTask = false;
        return;
    }

    // Tasks that changed the blobset or the mapping: show the result (the workspace is read again).
    if (m_reloadAfterTask) {
        BlobsetEntryRegistry::clear();
        m_currentFolderFiles.clear();
        emit currentFolderFilesChanged();

        loadGameProfile();
        selectGameAndLoad(m_gameId, m_platformId, m_modGameFolderName, m_gameFolderName);
    }

    // Load Game: start the game once Modify / Create succeeded.
    if (m_launchAfterTask) {
        m_launchAfterTask = false;
        if (loadGameMode() && steamGameId() > 0)
            launchGame();
    }
}

// ---------------------------------------------------------------------------
// Save As (MainForm.qml right click on a file)
// ---------------------------------------------------------------------------

void AppOrchestrator::saveAssetAs(const QString& hexHash, const QString& workspacePath, const QString& outPathIn,
                                  const QString& formatIn, bool flipped, bool alphaEnabled) {
    const QString outPath = outPathIn.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(outPathIn).toLocalFile()
                                                                                    : outPathIn;
    const QString format = formatIn.toLower();
    const QString name = QFileInfo(QString(workspacePath).replace(u'\\', u'/')).fileName();

    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash, entry)) {
        const QString message = u"Save As: %1 (hash 0x%2) isn't in the loaded blobset."_s.arg(name, hexHash.toUpper());
        emit uiConsoleUpdate(message);
        emit assetSaved(false, message);
        return;
    }

    emit uiStatusUpdate(u"Saving %1..."_s.arg(QFileInfo(outPath).fileName()));
    const QString location = gameLocation();
    const int version = blobsetVersion();
    const int platformId = platform();
    const QString hash = hexHash;
    QPointer<AppOrchestrator> self(this);

    QThreadPool::globalInstance()->start([self, entry, location, version, platformId, hash, name, outPath, format,
                                          flipped, alphaEnabled]() {
        QString error;
        QByteArray bytes;
        QByteArray vram;             // "raw": the vram section, saved next to the main section.
        try {
            if (format == u"dds"_s || format == u"png"_s) {
                const QByteArray dds = DdsImageProvider::readDds(hash, &error);
                if (dds.isEmpty()) {
                    if (error.isEmpty())
                        error = u"The texture couldn't be read."_s;
                } else if (format == u"dds"_s) {
                    bytes = dds;
                } else {
                    const QImage image = DdsImageProvider::applyViewOptions(DdsProcessor::decodeDdsToQImage(dds),
                                                                            {flipped, alphaEnabled});
                    if (image.isNull()) {
                        error = u"The texture couldn't be decoded (unsupported DDS format)."_s;
                    } else {
                        QBuffer buffer(&bytes);
                        buffer.open(QIODevice::WriteOnly);
                        if (!image.save(&buffer, "PNG"))
                            error = u"The PNG couldn't be encoded."_s;
                    }
                }
            } else if (format == u"wav"_s) {
                const QByteArray wem = BlobsetAssetReader::read(entry, location, version, platformId);
                QString virtualName = name.isEmpty() ? u"asset.wem"_s : name;
                if (!virtualName.endsWith(u".wem"_s, Qt::CaseInsensitive))
                    virtualName += u".wem"_s;
                const QVector<DecodedTrack> tracks = AudioProcessor::convertAudioBufferToWav(wem, virtualName, 1);
                if (tracks.isEmpty() || tracks.first().wavData.isEmpty())
                    error = u"vgmstream couldn't decode the WEM."_s;
                else
                    bytes = tracks.first().wavData;
            } else {
                // "raw" / "wem": decompressed (the reader undoes the blobset's LZMA / ZSTD chunks).
                qint64 mainSize = 0;
                const QByteArray data = BlobsetAssetReader::read(entry, location, version, platformId, &mainSize);
                if (data.isEmpty()) {
                    error = u"The asset is empty."_s;
                } else if (format == u"raw"_s && mainSize > 0 && mainSize < data.size()) {
                    bytes = data.left(mainSize);    // main section -> the chosen file
                    vram = data.mid(mainSize);      // vram section -> "<chosen file>.vram"
                } else {
                    bytes = data;
                }
            }
        } catch (const std::exception& ex) {
            error = QString::fromUtf8(ex.what());
        }

        const QString vramPath = outPath + u".vram"_s;
        const auto writeFile = [&error](const QString& path, const QByteArray& data) {
            QSaveFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
                error = u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString());
        };
        if (error.isEmpty()) {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            writeFile(outPath, bytes);
            if (error.isEmpty() && !vram.isEmpty())
                writeFile(vramPath, vram);
        }

        const bool ok = error.isEmpty();
        QString message;
        if (!ok)
            message = u"Save As %1 failed: %2"_s.arg(name, error);
        else if (vram.isEmpty())
            message = u"Saved %1 (%2)."_s.arg(QDir::toNativeSeparators(outPath), DdsImageProvider::humanSize(bytes.size()));
        else
            message = u"Saved %1 (main, %2) and %3 (vram, %4)."_s
                          .arg(QDir::toNativeSeparators(outPath), DdsImageProvider::humanSize(bytes.size()),
                               QFileInfo(vramPath).fileName(), DdsImageProvider::humanSize(vram.size()));
        QMetaObject::invokeMethod(qApp, [self, ok, message]() {
            if (!self)
                return;
            emit self->uiConsoleUpdate(message);
            emit self->uiStatusUpdate(message);
            emit self->assetSaved(ok, message);
        }, Qt::QueuedConnection);
    });
}
