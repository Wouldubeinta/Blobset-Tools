#include "compression_io.h"
#include <zstd.h>
#include <lzma.h>
#include <QByteArray>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <QtEndian>
#include "package_stream.h"

namespace CompressionIO {

    namespace {
    // One compression / decompression context per thread, reused for every chunk: creating a context per 256 KB
    // chunk (and freeing it again) cost more than compressing small chunks.
    struct ZstdContexts {
        ZSTD_CCtx* cctx = nullptr;
        ZSTD_DCtx* dctx = nullptr;
        ~ZstdContexts() {
            ZSTD_freeCCtx(cctx);
            ZSTD_freeDCtx(dctx);
        }
    };
    thread_local ZstdContexts tlsZstd;

    ZSTD_CCtx* compressionContext() {
        if (!tlsZstd.cctx)
            tlsZstd.cctx = ZSTD_createCCtx();
        return tlsZstd.cctx;
    }

    ZSTD_DCtx* decompressionContext() {
        if (!tlsZstd.dctx)
            tlsZstd.dctx = ZSTD_createDCtx();
        else
            ZSTD_DCtx_reset(tlsZstd.dctx, ZSTD_reset_session_only);
        return tlsZstd.dctx;
    }
    } // namespace

    QByteArray compressZstd(const QByteArray& input, int compressionLevel) {
        ZSTD_CCtx* cctx = compressionContext();
        if (!cctx) return QByteArray();

        // ZSTD_compressCCtx() works like ZSTD_compress(): one frame at `compressionLevel` (with its content size).
        const size_t bound = ZSTD_compressBound(static_cast<size_t>(input.size()));
        QByteArray compressed(static_cast<qsizetype>(bound), Qt::Uninitialized);
        const size_t cSize = ZSTD_compressCCtx(cctx, compressed.data(), bound, input.constData(),
                                               static_cast<size_t>(input.size()), compressionLevel);
        if (ZSTD_isError(cSize)) return QByteArray();

        compressed.resize(static_cast<qsizetype>(cSize));
        return compressed;
    }

    QByteArray decompressZstd(const QByteArray& input, bool isCompressed) {
        if (!isCompressed || input.isEmpty()) {
            return input;
        }

        // Verify header integrity (0xFD2FB528)
        const unsigned long long contentSize = ZSTD_getFrameContentSize(input.constData(), static_cast<size_t>(input.size()));
        if (contentSize == ZSTD_CONTENTSIZE_ERROR) {
            return QByteArray();
        }

        ZSTD_DCtx* dctx = decompressionContext();
        if (!dctx) return QByteArray();

        // The frame says how big it is (what compressZstd() writes): one exact allocation, one call.
        if (contentSize != ZSTD_CONTENTSIZE_UNKNOWN && contentSize <= static_cast<unsigned long long>(1) << 31) {
            QByteArray decompressed(static_cast<qsizetype>(contentSize), Qt::Uninitialized);
            const size_t written = ZSTD_decompressDCtx(dctx, decompressed.data(), static_cast<size_t>(contentSize),
                                                       input.constData(), static_cast<size_t>(input.size()));
            if (!ZSTD_isError(written)) {
                decompressed.resize(static_cast<qsizetype>(written));
                return decompressed;
            }
            // E.g. more than one frame: decoded below, the same way as data without a size.
            ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
        }

        // No size in the frame (the game's own chunks): stream it, growing by 256 KB (the game's chunk size).
        constexpr size_t kGrowBy = 262144;
        QByteArray decompressed(static_cast<qsizetype>(kGrowBy), Qt::Uninitialized);
        ZSTD_inBuffer inBuffer = { input.constData(), static_cast<size_t>(input.size()), 0 };
        size_t totalBytesWritten = 0;
        size_t status = 1;

        while (inBuffer.pos < inBuffer.size && status > 0) {
            if (decompressed.size() == static_cast<qsizetype>(totalBytesWritten))
                decompressed.resize(static_cast<qsizetype>(totalBytesWritten + kGrowBy));

            ZSTD_outBuffer outBuffer = {
                decompressed.data() + totalBytesWritten,
                static_cast<size_t>(decompressed.size()) - totalBytesWritten,
                0
            };

            status = ZSTD_decompressStream(dctx, &outBuffer, &inBuffer);
            if (ZSTD_isError(status)) return QByteArray();
            totalBytesWritten += outBuffer.pos;
        }

        decompressed.resize(static_cast<qsizetype>(totalBytesWritten));
        return decompressed;
    }

    QByteArray compressLzma(const QByteArray& inputData) {
        if (inputData.isEmpty()) {
            return QByteArray();
        }

        lzma_options_lzma options;
        if (lzma_lzma_preset(&options, LZMA_PRESET_DEFAULT)) {
            return QByteArray();
        }

        options.dict_size = 1 << 21; // 2MB
        options.pb = 2;
        options.lc = 3;
        options.lp = 0;
        options.mode = LZMA_MODE_NORMAL;
        options.nice_len = 5;

        lzma_filter filters[2];
        filters[0].id = LZMA_FILTER_LZMA1;
        filters[0].options = &options;
        filters[1].id = LZMA_VLI_UNKNOWN;
        filters[1].options = nullptr;

        const size_t maxOutputSize = lzma_stream_buffer_bound(static_cast<size_t>(inputData.size())) + 5;
        QByteArray compressed(static_cast<qsizetype>(maxOutputSize), Qt::Uninitialized);

        uint8_t* headerPtr = reinterpret_cast<uint8_t*>(compressed.data());
        lzma_ret ret = lzma_properties_encode(&filters[0], headerPtr);
        if (ret != LZMA_OK) {
            return QByteArray();
        }

        lzma_stream strm = LZMA_STREAM_INIT;
        ret = lzma_raw_encoder(&strm, filters);
        if (ret != LZMA_OK) {
            return QByteArray();
        }

        strm.next_in = reinterpret_cast<const uint8_t*>(inputData.constData());
        strm.avail_in = inputData.size();
        strm.next_out = reinterpret_cast<uint8_t*>(compressed.data()) + 5;
        strm.avail_out = compressed.size() - 5;

        ret = lzma_code(&strm, LZMA_FINISH);
        size_t actualSize = (compressed.size() - 5) - strm.avail_out + 5;
        lzma_end(&strm);

        if (ret == LZMA_STREAM_END) {
            compressed.resize(static_cast<qsizetype>(actualSize));
            return compressed;
        }

        return QByteArray();
    }

    QByteArray decompressLzma(const QByteArray& compressedData, quint32 uncompressedSize) {
        if (compressedData.size() <= 5) {
            return QByteArray();
        }

        QByteArray decompressed(static_cast<qsizetype>(uncompressedSize), Qt::Uninitialized);

        lzma_filter filters[2];
        filters[0].id = LZMA_FILTER_LZMA1;
        filters[0].options = nullptr;
        filters[1].id = LZMA_VLI_UNKNOWN;
        filters[1].options = nullptr;

        const uint8_t* propHeader = reinterpret_cast<const uint8_t*>(compressedData.constData());

        // Extract 5-byte properties
        lzma_ret ret = lzma_properties_decode(&filters[0], nullptr, propHeader, 5);
        if (ret != LZMA_OK) {
            return QByteArray();
        }

        lzma_stream strm = LZMA_STREAM_INIT;
        ret = lzma_raw_decoder(&strm, filters);
        if (ret != LZMA_OK) {
            std::free(filters[0].options);
            return QByteArray();
        }

        strm.next_in = reinterpret_cast<const uint8_t*>(compressedData.constData()) + 5;
        strm.avail_in = compressedData.size() - 5;
        strm.next_out = reinterpret_cast<uint8_t*>(decompressed.data());
        strm.avail_out = decompressed.size();

        ret = lzma_code(&strm, LZMA_FINISH);

        lzma_end(&strm);
        std::free(filters[0].options); // Bypasses resource leaks safely

        if (ret == LZMA_STREAM_END || (ret == LZMA_OK && strm.avail_out == 0)) {
            return decompressed;
        }

        return QByteArray();
    }
}

// ===========================================================================
// V2 LZMA sections (Rugby League Live 4)
// ===========================================================================
namespace CompressionIO {

    namespace {
        // The LZMA properties V3 chunks leave out: lc 3, lp 0, pb 2 (0x5D), 2 MB dictionary.
        const QByteArray kV3LzmaProps = QByteArray::fromHex("5d00002000");

        // V2 / V3 chunk runs. V3 XORs the sizes and leaves the LZMA properties out.
        QByteArray readLzmaChunks(PackageStream& in, qint64 uncompressedSize, qint64 maxBytes, quint32 sizeKey,
                                  bool hasProps) {
            QByteArray out;
            if (uncompressedSize <= 0)
                return out;

            const qint64 limit = maxBytes >= 0 ? std::min(maxBytes, uncompressedSize) : uncompressedSize;
            out.reserve(std::min<qint64>(uncompressedSize, limit + kLzmaV2ChunkSize));
            while (out.size() < limit) {
                const qint64 dataSize = static_cast<qint64>(static_cast<qint32>(in.readUInt32() ^ sizeKey)) - 4;
                if (dataSize <= 0 || dataSize > in.length() - in.position())
                    throw std::runtime_error("Invalid LZMA chunk size in blobset entry");

                const QByteArray chunk = in.readBytes(dataSize);
                const qint64 expected = std::min<qint64>(kLzmaV2ChunkSize, uncompressedSize - out.size());

                if (chunk.size() == expected) {
                    out += chunk; // Stored.
                    continue;
                }

                const QByteArray unpacked = decompressLzma(hasProps ? chunk : kV3LzmaProps + chunk,
                                                           static_cast<quint32>(expected));
                if (unpacked.size() != expected)
                    throw std::runtime_error("An LZMA chunk of the blobset entry could not be decompressed");
                out += unpacked;
            }
            return out;
        }
    } // namespace

    QByteArray readLzmaSectionV2(PackageStream& in, qint64 uncompressedSize, qint64 maxBytes) {
        return readLzmaChunks(in, uncompressedSize, maxBytes, 0, true);
    }

    QByteArray readLzmaSectionV3(PackageStream& in, qint64 uncompressedSize, qint64 maxBytes) {
        return readLzmaChunks(in, uncompressedSize, maxBytes, kLzmaV3SizeKey, false);
    }

    QByteArray buildLzmaSectionV3(const QByteArray& data) {
        QByteArray block;
        for (qint64 offset = 0; offset < data.size(); offset += kLzmaV2ChunkSize) {
            const QByteArray chunk = data.mid(offset, kLzmaV2ChunkSize);
            const QByteArray packed = compressLzma(chunk);   // 5 byte properties + data.
            if (packed.size() <= 5)
                throw std::runtime_error("LZMA compression failed.");

            // V3 leaves the properties out; only kept when it's smaller (a full size chunk reads back as stored).
            const QByteArray stream = packed.mid(5);
            const QByteArray& payload = stream.size() < chunk.size() ? stream : chunk;

            uchar size[4];
            qToLittleEndian(static_cast<quint32>(payload.size() + 4) ^ kLzmaV3SizeKey, size);
            block.append(reinterpret_cast<const char*>(size), 4);
            block += payload;
        }
        return block;
    }

    QByteArray buildLzmaSectionV2(const QByteArray& data, bool bigEndian) {
        QByteArray block;
        for (qint64 offset = 0; offset < data.size(); offset += kLzmaV2ChunkSize) {
            const QByteArray chunk = data.mid(offset, kLzmaV2ChunkSize);
            const QByteArray packed = compressLzma(chunk);
            if (packed.isEmpty())
                throw std::runtime_error("LZMA compression failed.");

            // Only kept when it's smaller: a chunk of the full size reads back as stored.
            const QByteArray& payload = packed.size() < chunk.size() ? packed : chunk;

            uchar size[4];
            const quint32 stored = static_cast<quint32>(payload.size() + 4);
            if (bigEndian)
                qToBigEndian(stored, size);
            else
                qToLittleEndian(stored, size);
            block.append(reinterpret_cast<const char*>(size), 4);
            block += payload;
        }
        return block;
    }


    namespace {
    // 5 byte LZMA properties: lc / lp / pb byte (< 9 * 5 * 5), then a sane dictionary size.
    bool hasLzmaProperties(const char* data, qsizetype size) {
        if (size <= 5)
            return false;
        const uchar props = static_cast<uchar>(data[0]);
        const quint32 dictionary = qFromLittleEndian<quint32>(data + 1);
        return props < 225 && dictionary >= (1u << 12) && dictionary <= (1u << 30);
    }
    } // namespace

    QByteArray decodeV1Chunk(const QByteArray& chunk, quint32 uncompressedSize) {
        if (static_cast<quint32>(chunk.size()) != uncompressedSize)
            return decompressLzma(chunk, uncompressedSize);
        if (hasLzmaProperties(chunk.constData(), chunk.size())) {
            const QByteArray decoded = decompressLzma(chunk, uncompressedSize);
            if (static_cast<quint32>(decoded.size()) == uncompressedSize)
                return decoded;
        }
        return chunk;   // Stored.
    }

    bool isStoredV1Section(PackageStream& in, qint64 offset, quint32 compressedSize, quint32 uncompressedSize) {
        if (compressedSize != uncompressedSize)
            return false;
        if (compressedSize < 4 + 4 + 4 + 6)
            return true;

        const qint64 saved = in.position();
        bool block = false;
        try {
            in.seek(offset);
            const qint64 count = static_cast<qint32>(in.readUInt32());
            if (count >= 1 && count <= 65536 && 4 + 8 * count + 6 <= compressedSize) {
                qint64 total = 4 + 4 * count;
                qint64 firstChunk = -1;
                bool sane = true;
                for (qint64 i = 0; i < count; ++i) {
                    const qint64 stored = static_cast<qint32>(in.readUInt32());
                    if (stored < 4 + 6) {
                        sane = false;
                        break;
                    }
                    if (i == 0)
                        firstChunk = stored - 4;
                    total += stored;   // The stored sizes include the chunk's 4 byte uncompressed size.
                }
                if (sane && total == compressedSize) {
                    in.readUInt32();   // First chunk's uncompressed size.
                    const QByteArray head = in.readBytes(std::min<qint64>(firstChunk, 5));
                    block = hasLzmaProperties(head.constData(), head.size() == 5 ? 6 : 0);
                }
            }
        } catch (const std::exception&) {
            block = false;
        }
        in.seek(saved);
        return !block;
    }
}
