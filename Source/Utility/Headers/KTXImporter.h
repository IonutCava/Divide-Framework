#pragma once

#include "KTXImage.h"

namespace Divide::ImageTools
{
    enum class KTXSourceFormat : U8 { PNG, JPEG, BMP };

    // Decode one tightly packed 2D base level in bottom-left orientation.
    // Grayscale R is DATA; gray-alpha stays RG for DATA or expands to RGBA
    // for OPACITY. RGB receives synthetic opaque alpha.
    // Actual PNG alpha (including tRNS) requires OPACITY or DATA.
    // UNKNOWN is resolved only for grayscale R or synthetic RGB/BMP/JPEG alpha.
    // BMP accepts uncompressed 24-bit RGB only; alpha/bitfield BMPs are rejected.
    // sRGB is supported only for 8-bit RGB/RGBA. PNG16/JPEG16 retain numeric
    // samples in UNORM_16 without gamma conversion. JPEG12 is normalized to
    // full UNORM_16 with nearest rounding (12-bit samples remain recoverable).
    // No file I/O or cache writes. Failure leaves output untouched.
    // decodedByteLimit additionally bounds the tight base level before allocation.
    // PNG working memory is source + two decoded planes + at most 64 MiB codec.
    [[nodiscard]] bool DecodeKTXSource(std::span<const Byte> input, KTXSourceFormat format,
                                       bool srgb, KTXAlphaInterpretation alphaInterpretation,
                                       KTXImage& output, std::string* error = nullptr,
                                       size_t decodedByteLimit = KTX_IMAGE_BYTE_LIMIT);
}
