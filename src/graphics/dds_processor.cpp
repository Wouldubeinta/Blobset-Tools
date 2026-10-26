#include "dds_processor.h"

// ===================================================================
// PURE CLEAN MINGW GCC COMPATIBILITY LAYER
// ===================================================================
// 1. Force DirectXMath to compile in clean C++ standalone mode.
// This bypasses MSVC asm blocks and removes the inline CPUID clash completely!
#ifndef _XM_NO_INTRINSICS_
#define _XM_NO_INTRINSICS_
#endif

// 2. Enforce structural cross-platform runtime definitions
#ifndef HAVE_DIRECTXMATH
#define HAVE_DIRECTXMATH
#endif

// 3. Bring in the local project dependencies sequentially
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include "DirectXTex.h"
// ===================================================================

#include <QImage>
#include <QByteArray>
#include <QDebug>

QImage DdsProcessor::decodeDdsToQImage(const QByteArray &ddsFileData) {
    using namespace DirectX;

    TexMetadata metadata;
    ScratchImage rawImage;

    // Load raw DDS bytes array directly from current memory allocations
    HRESULT hr = LoadFromDDSMemory(
        reinterpret_cast<const uint8_t*>(ddsFileData.constData()),
        static_cast<size_t>(ddsFileData.size()),
        DDS_FLAGS_NONE,
        &metadata,
        rawImage
        );

    if (FAILED(hr)) {
        qWarning() << "DirectXTex failed to parse the DDS layout/headers.";
        return QImage();
    }

    ScratchImage decompressedImage;
    const Image* finalImagePtr = rawImage.GetImage(0, 0, 0);
    if (!finalImagePtr) return QImage();

    // Only the top mip of the first image is shown, so only that one is decompressed / converted
    // (not every mip level, array slice and cube face).
    if (IsCompressed(metadata.format)) {
        hr = Decompress(*finalImagePtr, DXGI_FORMAT_R8G8B8A8_UNORM, decompressedImage);

        if (FAILED(hr)) {
            qWarning() << "Failed to decompress block-compressed DXT/BC texture formats.";
            return QImage();
        }

        finalImagePtr = decompressedImage.GetImage(0, 0, 0);
        metadata = decompressedImage.GetMetadata();
    }
    else if (metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM && metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        hr = Convert(*finalImagePtr, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, 0, decompressedImage);

        if (FAILED(hr)) {
            qWarning() << "Failed to convert raw color format spaces to standard RGBA structures.";
            return QImage();
        }

        finalImagePtr = decompressedImage.GetImage(0, 0, 0);
        metadata = decompressedImage.GetMetadata();
    }

    if (!finalImagePtr) return QImage();

    // Map pixels directly into standard Qt linear output canvases properties
    QImage resultImg(
        finalImagePtr->pixels,
        static_cast<int>(metadata.width),
        static_cast<int>(metadata.height),
        static_cast<int>(finalImagePtr->rowPitch),
        QImage::Format_RGBA8888
        );

    return resultImg.copy(); // Deep copy detaches memory frames securely
}
