#include "dds_image_provider.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "core_defs.h"
#include "dds_processor.h"
#include "gcm_texture.h"
#include "mini_txpk_file.h"
#include "package_stream.h"
#include "x360_texture.h"

#include <QBuffer>
#include <QDataStream>
#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QUrlQuery>
#include <QtEndian>

#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>

namespace {

const QString kMemoryPrefix = QStringLiteral("mem-");

// Keeps the last decoded texture so view changes (flip / alpha) are instant.
struct DecodedCache {
    QMutex mutex;
    QString hash;
    quint32 session = 0;
    QImage image;
};

DecodedCache& decodedCache() {
    static DecodedCache cache;
    return cache;
}

// Images handed over by storeImage() (requestImage() can run on a loader thread, hence the mutex).
struct MemoryStore {
    QMutex mutex;
    QHash<QString, QImage> images;
    quint64 next = 0;
};

MemoryStore& memoryStore() {
    static MemoryStore store;
    return store;
}

QImage memoryImage(const QString& key) {
    MemoryStore& store = memoryStore();
    QMutexLocker locker(&store.mutex);
    return store.images.value(key);
}

// One texture of the loaded blobset: its mini header and the texture data that goes with it.
struct LoadedTexture {
    int platform = 0;
    bool isConsole = false;
    bool hasMini = false;
    MiniTxpk mini;
    QByteArray pixels;  // PC: a complete .dds file. Consoles: the bare texture data the mini header describes.
};

// Every DDS entry is a mini TXPK: the main section is the mini header, the vram section the texture
// (PC: a complete .dds file, consoles: the bare pixels described by the header). When an entry has no
// vram section, the texture follows the header inside the main section. Throws std::runtime_error.
LoadedTexture readTexture(const BlobsetEntry& entry, const AppOrchestrator* orchestrator) {
    LoadedTexture texture;
    texture.platform = orchestrator->platform();
    texture.isConsole = texture.platform != static_cast<int>(CoreDefs::Platforms::Windows);

    // Right container (V1/V2: data-N.blob...), right byte order, LZMA / ZSTD - all handled here.
    qint64 mainSize = 0;
    QByteArray asset = BlobsetAssetReader::read(entry, orchestrator->assetLocation(),
                                                orchestrator->blobsetVersion(), texture.platform, &mainSize);
    mainSize = std::clamp<qint64>(mainSize, 0, asset.size());

    // The header is parsed straight out of the asset (no copy of the main section).
    QByteArray mainPart = QByteArray::fromRawData(asset.constData(), mainSize);
    const bool hasVram = mainSize < asset.size();
    const bool pcDds = !texture.isConsole && hasVram
                       && QByteArrayView(asset).sliced(mainSize).startsWith("DDS ");

    qint64 pixelsAt = mainSize;
    {
        QBuffer buffer(&mainPart);
        buffer.open(QIODevice::ReadOnly);
        PackageStream stream(&buffer, texture.isConsole ? PackageStream::Big : PackageStream::Little);
        if (pcDds) {
            try { // Only wanted for its path here: the .dds carries everything else.
                texture.mini.deserialize(stream, false, orchestrator->gameId());
                texture.hasMini = true;
            } catch (const std::exception&) {
            }
        } else {
            texture.mini.deserialize(stream, texture.isConsole, orchestrator->gameId());
            texture.hasMini = true;
            if (!hasVram)
                pixelsAt = stream.position();   // No vram section: the texture follows the header.
        }
    }
    mainPart.clear();   // Drops the raw view before the asset buffer is reused below.

    // The pixels are the tail of the asset: moved and trimmed in place instead of copied.
    asset.remove(0, pixelsAt);
    texture.pixels = std::move(asset);
    return texture;
}

// Every DDS entry is a mini TXPK: the main section is the mini header, the vram section the texture
// (PC: a complete .dds file, consoles: the bare pixels described by the header). When an entry has no
// vram section, the texture follows the header inside the main section. Throws std::runtime_error.
//
// Selecting a texture asks for its image and its info right after each other, so the last texture is
// kept: the second request doesn't read and decompress the asset again.
LoadedTexture loadTexture(const QString& hash) {
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hash, entry))
        throw std::runtime_error(QStringLiteral("The texture (hash %1) isn't in the loaded blobset.").arg(hash).toStdString());

    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator)
        throw std::runtime_error("The application orchestrator is unavailable.");

    // Everything that identifies the stored bytes: a rebuilt blobset moves or resizes the entry.
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                            .arg(orchestrator->assetLocation(), hash)
                            .arg(BlobsetEntryRegistry::generation())
                            .arg(entry.blobSetNumber)
                            .arg(entry.mainFinalOffset)
                            .arg(entry.mainCompressedSize)
                            .arg(entry.vramFinalOffset)
                            .arg(entry.vramCompressedSize);

    static QMutex cacheMutex;
    static QString cachedKey;
    static LoadedTexture cached;
    {
        QMutexLocker locker(&cacheMutex);
        if (key == cachedKey)
            return cached;   // Implicitly shared: no copy of the pixels.
    }

    LoadedTexture texture = readTexture(entry, orchestrator);
    QMutexLocker locker(&cacheMutex);
    cachedKey = key;
    cached = texture;
    return texture;
}

} // namespace

DdsImageProvider::DdsImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

// ------------------------------------------------------------------
// Ids / view options
// ------------------------------------------------------------------
QString DdsImageProvider::makeId(const QString& hash, quint32 session, const ViewOptions& options) {
    return QStringLiteral("%1?s=%2&flip=%3&alpha=%4")
        .arg(hash)
        .arg(session)
        .arg(options.flipped ? 1 : 0)
        .arg(options.alphaEnabled ? 1 : 0);
}

QImage DdsImageProvider::applyViewOptions(const QImage& image, const ViewOptions& options) {
    if (image.isNull())
        return image;

    QImage result = image;

    if (options.flipped)
        result = result.flipped(Qt::Vertical);

    if (!options.alphaEnabled && result.hasAlphaChannel()) {
        // The decoder produces straight (non premultiplied) RGBA8888, so dropping the alpha
        // channel keeps the colors untouched. RGBX8888 reads every pixel as fully opaque.
        if (result.format() != QImage::Format_RGBA8888)
            result = result.convertToFormat(QImage::Format_RGBA8888);
        result = result.convertToFormat(QImage::Format_RGBX8888);
    }

    return result;
}

QString DdsImageProvider::storeImage(const QImage& image) {
    MemoryStore& store = memoryStore();
    QMutexLocker locker(&store.mutex);
    const QString key = kMemoryPrefix + QString::number(++store.next);
    store.images.insert(key, image);
    return key;
}

void DdsImageProvider::releaseImage(const QString& key) {
    if (key.isEmpty())
        return;
    MemoryStore& store = memoryStore();
    QMutexLocker locker(&store.mutex);
    store.images.remove(key);
}

QImage DdsImageProvider::decodeCached(const QString& hash, quint32 session) {
    if (session != 0) {
        DecodedCache& cache = decodedCache();
        QMutexLocker locker(&cache.mutex);
        if (cache.session == session && cache.hash == hash && !cache.image.isNull())
            return cache.image;
    }

    const QImage decoded = decodeEntry(hash);

    if (session != 0 && !decoded.isNull()) {
        DecodedCache& cache = decodedCache();
        QMutexLocker locker(&cache.mutex);
        cache.hash = hash;
        cache.session = session;
        cache.image = decoded;
    }
    return decoded;
}

QImage DdsImageProvider::render(const QString& id) {
    const qsizetype queryStart = id.indexOf(QLatin1Char('?'));
    const QString hash = queryStart < 0 ? id : id.left(queryStart);

    ViewOptions options;
    quint32 session = 0;
    if (queryStart >= 0) {
        const QUrlQuery query(id.mid(queryStart + 1));
        session = query.queryItemValue(QStringLiteral("s")).toUInt();
        options.flipped = query.queryItemValue(QStringLiteral("flip")) == QLatin1String("1");
        options.alphaEnabled = query.queryItemValue(QStringLiteral("alpha")) != QLatin1String("0");
    }

    const QImage source = hash.startsWith(kMemoryPrefix) ? memoryImage(hash) : decodeCached(hash, session);
    return applyViewOptions(source, options);
}

QImage DdsImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    QImage img = render(id);

    if (size)
        *size = img.size();

    if (requestedSize.isValid() && !img.isNull())
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return img;
}

// ------------------------------------------------------------------
// Reads one texture of the loaded blobset as a complete .dds file.
// ------------------------------------------------------------------
QByteArray DdsImageProvider::readDds(const QString& hash, QString* error) {
    const auto failWith = [error](const QString& message) {
        if (error)
            *error = message;
        return QByteArray();
    };

    try {
        const LoadedTexture texture = loadTexture(hash);

        if (!texture.isConsole)
            return texture.pixels;

        const MiniTxpk& mini = texture.mini;
        if (mini.isXbox360) {
            // The D3D texture header says how the data is tiled, byte swapped and where the mips are.
            QString reason;
            const std::optional<QByteArray> converted = X360::toDds(mini.x360, texture.pixels, &reason);
            if (!converted)
                return failWith(QStringLiteral("Unsupported Xbox 360 texture: %1").arg(reason));
            return *converted;
        }

        // PS3: the RSX descriptor says how the pixels are stored (swizzled or linear, byte order, mips).
        const std::optional<QByteArray> converted = Gcm::toDds(mini.gcm, texture.pixels);
        if (!converted)
            return failWith(QStringLiteral("Unsupported PS3 texture format: %1").arg(Gcm::formatName(mini.gcm.format)));
        return *converted;
    } catch (const std::exception& e) {
        return failWith(QString::fromUtf8(e.what()));
    }
}

// ------------------------------------------------------------------
// Texture metadata (same keys as TxpkToolManager::entryInfo)
// ------------------------------------------------------------------
QVariantMap DdsImageProvider::readTextureInfo(const QString& hash, QString* error) {
    QVariantMap info;
    try {
        const LoadedTexture texture = loadTexture(hash);
        const MiniTxpk& mini = texture.mini;

        quint32 width = mini.ddsWidth;
        quint32 height = mini.ddsHeight;
        quint32 mipMaps = 1;
        QString format;

        if (texture.isConsole && mini.isXbox360) {
            mipMaps = mini.x360.levelCount();
            format = X360::formatName(mini.x360.format());
        } else if (texture.isConsole) {
            mipMaps = std::max<quint32>(1, mini.gcm.mipmap);
            format = Gcm::formatName(mini.gcm.format);
        } else {
            parsePcDdsHeader(texture.pixels, 0, &width, &height, &mipMaps, &format);
        }

        QString path = texture.hasMini ? mini.ddsFilePath : QString();
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));

        info[QStringLiteral("path")] = path;
        info[QStringLiteral("width")] = width;
        info[QStringLiteral("height")] = height;
        info[QStringLiteral("mipMaps")] = mipMaps;
        info[QStringLiteral("format")] = format;
        info[QStringLiteral("size")] = humanSize(texture.pixels.size());
    } catch (const std::exception& e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
    return info;
}

QString DdsImageProvider::formatInfoLine(const QVariantMap& info) {
    if (info.isEmpty())
        return {};

    QStringList parts;
    const QString path = info.value(QStringLiteral("path")).toString();
    if (!path.isEmpty())
        parts << path;
    parts << QStringLiteral("%1 x %2").arg(info.value(QStringLiteral("width")).toUInt())
                                      .arg(info.value(QStringLiteral("height")).toUInt());

    const QString format = info.value(QStringLiteral("format")).toString();
    if (!format.isEmpty())
        parts << format;

    const uint mips = info.value(QStringLiteral("mipMaps")).toUInt();
    parts << (mips == 1 ? QStringLiteral("1 mip") : QStringLiteral("%1 mips").arg(mips));
    parts << info.value(QStringLiteral("size")).toString();
    return parts.join(QStringLiteral("  |  "));
}

bool DdsImageProvider::parsePcDdsHeader(const QByteArray& data, qint64 offset, quint32* width, quint32* height,
                                        quint32* mipMaps, QString* format) {
    if (offset < 0 || offset + 128 > data.size() || data.mid(offset, 4) != "DDS ")
        return false;

    const uchar* p = reinterpret_cast<const uchar*>(data.constData()) + offset;
    if (height)
        *height = qFromLittleEndian<quint32>(p + 12);
    if (width)
        *width = qFromLittleEndian<quint32>(p + 16);
    if (mipMaps)
        *mipMaps = std::max<quint32>(1, qFromLittleEndian<quint32>(p + 28));

    if (format) {
        const quint32 pfFlags = qFromLittleEndian<quint32>(p + 80);
        if (pfFlags & 0x4) { // DDPF_FOURCC
            *format = QString::fromLatin1(reinterpret_cast<const char*>(p + 84), 4);
        } else {
            const quint32 bits = qFromLittleEndian<quint32>(p + 88);
            *format = ((pfFlags & 0x1) ? QStringLiteral("RGBA%1") : QStringLiteral("RGB%1")).arg(bits);
        }
    }
    return true;
}

QString DdsImageProvider::humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 2) + QStringLiteral(" MB");
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(bytes) + QStringLiteral(" B");
}

QImage DdsImageProvider::decodeEntry(const QString& hash) {
    QString error;
    const QByteArray dds = readDds(hash, &error);
    if (dds.isEmpty()) {
        qWarning() << "DDS Provider:" << error;
        return {};
    }

    const QImage image = DdsProcessor::decodeDdsToQImage(dds);
    if (image.isNull())
        qWarning() << "DDS Provider: could not decode texture" << hash;
    return image;
}

