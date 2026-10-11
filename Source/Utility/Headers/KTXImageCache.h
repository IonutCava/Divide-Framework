#pragma once

#include "KTXImporter.h"
#include "Platform/File/Headers/ResourcePath.h"

#include <stop_token>

namespace Divide::ImageTools
{
    struct KTXSourceSettings
    {
        KTXSourceFormat _format = KTXSourceFormat::PNG;
        bool _srgb = false;
        KTXAlphaInterpretation _alphaInterpretation = KTXAlphaInterpretation::UNKNOWN;
        KTXMipSemantic _semantic = KTXMipSemantic::COLOR;
        bool _generateMipmaps = true;
        // Applied only to genuine RGBA opacity; synthetic/data alpha is skipped.
        bool _preserveAlphaCoverage = false;
        bool _diskCache = true;
        U64 _cacheQuotaBytes = 1024ull * 1024ull * 1024ull;
        std::stop_token _stopToken;
    };

    // Synchronous, lossless CPU authoring only: no GPU or target-specific cache.
    // Warm hits still read/hash source bytes but do not decode source or encode.
    // Source <=32 MiB, decoded mip chain <=32 MiB, payload <=64 MiB keep
    // retained input plus decoder/authoring temporaries within a bounded budget.
    // At most two active calls; concurrent identical requests share conversion.
    // Disk failures are nonfatal; cancellation/failure leaves output unchanged.
    // FNV filenames are not security hashes: exact settings/source verify identity.
    // Each disk entry stores an extra exact copy of the source bytes to detect
    // key collisions, alongside canonical settings and the native KTX2 payload.
    // Caller supplies a separate TextureData/KTX2 directory and maps legacy
    // useDDSCache to _diskCache. Only single-file PNG/JPEG/24-bit BMP 2D sources
    // accepted by KTXImporter are supported.
    // Precision and channel preservation policies are fixed and versioned in
    // the key; packed grayscale should request DATA alpha interpretation.
    [[nodiscard]] bool LoadCachedKTXSource(const ResourcePath& source,
                                           const ResourcePath& cacheDir,
                                           const KTXSourceSettings& settings,
                                           KTXImage& output,
                                           std::string* error = nullptr);

    // Removes only completed files owned by this helper. Active publication is
    // serialized with invalidation; in-flight calls may repopulate the cache.
    [[nodiscard]] bool InvalidateKTXSourceCache(const ResourcePath& cacheDir,
                                              std::string* error = nullptr);
}
