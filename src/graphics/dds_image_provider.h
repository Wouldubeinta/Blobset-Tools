#ifndef DDS_IMAGE_PROVIDER_H
#define DDS_IMAGE_PROVIDER_H

#include "blobset_file.h"

#include <QQuickImageProvider>
#include <QImage>
#include <QString>
#include <QVariantMap>

// Serves "image://dds_provider/<id>". The id is a path hash (or an in-memory key), optionally followed by view options:
//
//     <hash>?s=<session>&flip=<0|1>&alpha=<0|1>
//
//   flip    1 = flip the image vertically.
//   alpha   0 = ignore the alpha channel (the image is shown fully opaque).
//   s       Non-zero = the decoded texture is kept in memory per (hash, session), so flipping or
//           toggling alpha does not decompress and decode the texture again. A new session always
//           decodes from disk, so a stale texture can never be shown after the files changed.
//
// Ids starting with "mem-" are images handed over with storeImage() (e.g. a texture inside a TXPK):
// they are served from memory, the view options are applied the same way.
class DdsImageProvider : public QQuickImageProvider {
public:
    struct ViewOptions {
        bool flipped = false;
        bool alphaEnabled = true;
    };

    DdsImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // Shared by the provider, DdsViewerBridge and TxpkToolManager so the id format lives in exactly one place.
    static QString makeId(const QString& hash, quint32 session, const ViewOptions& options);
    static QImage render(const QString& id);
    static QImage applyViewOptions(const QImage& image, const ViewOptions& options);

    // A complete PC .dds file of the texture registered under `hash` (console data is converted).
    // Empty on failure, with the reason in `error`.
    static QByteArray readDds(const QString& hash, QString* error = nullptr);

    // { path, width, height, mipMaps, format, size } of the texture registered under `hash` - the same keys
    // TxpkToolManager::entryInfo() fills, so both viewers show the same metadata. Console textures report the
    // RSX format ("DXT5", "A8R8G8B8 (linear)", ...), PC ones what their DDS header says.
    // Empty map on failure, with the reason in `error`.
    static QVariantMap readTextureInfo(const QString& hash, QString* error = nullptr);

    // "path  |  W x H  |  format  |  N mips  |  size": the info strip of the DDS and TXPK viewers.
    static QString formatInfoLine(const QVariantMap& info);

    // Reads size, mip count and format out of the PC .dds header at `offset`. False when there is no valid header.
    static bool parsePcDdsHeader(const QByteArray& data, qint64 offset, quint32* width, quint32* height,
                                 quint32* mipMaps, QString* format);

    // "1.25 MB", "12.0 KB", "512 B".
    static QString humanSize(qint64 bytes);

    // In-memory images. storeImage() returns the key to use as the id; release it when it's no longer shown.
    static QString storeImage(const QImage& image);
    static void releaseImage(const QString& key);

private:
    static QImage decodeEntry(const QString& hash);
    static QImage decodeCached(const QString& hash, quint32 session);
};

#endif // DDS_IMAGE_PROVIDER_H
