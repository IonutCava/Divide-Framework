#pragma once

#include "Platform/Video/Headers/RenderAPIEnums.h"

#include <functional>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Divide::ImageTools
{
    constexpr size_t KTX_IMAGE_BYTE_LIMIT = 256u * 1024u * 1024u;

    enum class KTXComponentType : U8 { UNORM_8, UNORM_16, FLOAT_16, FLOAT_32 };
    enum class KTXAlphaInterpretation : U8 { UNKNOWN, OPACITY, DATA, SYNTHETIC };

    struct AlphaClassification
    {
        size_t _transparentCount = 0u;
        size_t _translucentCount = 0u;
        size_t _opaqueCount = 0u;
        bool _transparent = false;
        bool _translucent = false;
        bool _allZero = false;
        bool _skipped = false;
    };

    // Transparency/translucency counts are independent and require >4 samples.
    // All-zero opacity is always transparent; the diagnostic is optional.
    [[nodiscard]] AlphaClassification ClassifyKTXAlpha(
        std::span<const Byte> alpha, bool synthetic = false, bool data = false,
        const std::function<void(std::string_view)>& errorfn = {});

    struct KTXSubresource
    {
        U32 _mip = 0u;
        U32 _layer = 0u;
        U32 _face = 0u;
        U32 _depthSlice = 0u;
        U16 _width = 0u;
        U16 _height = 0u;
        // FLOAT_16 stores the exact IEEE half bit patterns, not UNORM values.
        std::variant<std::vector<U8>, std::vector<U16>, std::vector<F32>> _pixels;
    };

    struct KTXImage
    {
        U16 _width = 0u;
        U16 _height = 1u;
        U16 _depth = 1u;
        U16 _layers = 1u;
        U8 _dimensions = 2u;
        U8 _faces = 1u;
        U8 _mips = 1u;
        U8 _components = 0u;
        bool _isArray = false;
        bool _srgb = false;
        bool _native = false;
        bool _compressed = false;
        GFXImageFormat _format = GFXImageFormat::RGBA;
        GFXDataFormat _dataType = GFXDataFormat::UNSIGNED_BYTE;
        GFXImagePacking _packing = GFXImagePacking::NORMALIZED;
        TextureType _textureType = TextureType::TEXTURE_2D;
        KTXComponentType _componentType = KTXComponentType::UNORM_8;
        KTXAlphaInterpretation _alphaInterpretation = KTXAlphaInterpretation::UNKNOWN;
        AlphaClassification _alpha;
        // Tight 2D slices, ordered mip -> layer -> face -> depth slice.
        std::vector<KTXSubresource> _subresources;
    };

    // Explicitly classify base-level alpha across every layer/face/depth slice.
    // Opacity requires RGBA and finite [0,1] values; failure leaves image unchanged.
    // Legacy buckets use floor(alpha * 255); all-zero tests the original value.
    [[nodiscard]] bool ClassifyKTXImageAlpha(KTXImage& image, KTXAlphaInterpretation interpretation);

    // Native, uncompressed KTX2 only. Explicit bottom-left orientation required;
    // absent or different orientation/swizzle/alpha conventions are rejected.
    // decodedByteLimit additionally caps decoded storage before allocation.
    // Failure leaves output untouched.
    [[nodiscard]] bool LoadKTXImage(std::span<const Byte> input, KTXImage& output,
                                    std::string* error = nullptr,
                                    size_t decodedByteLimit = KTX_IMAGE_BYTE_LIMIT);
    enum class KTXMipSemantic : U8 { COLOR, NORMAL, DATA };
    struct KTXGPUCompressionOptions
    {
        KTXMipSemantic _semantic = KTXMipSemantic::COLOR;
        bool _allowLossyData = false;
        bool _preserveCPUData = false;
        bool _sampledOnly = true;
        bool _bc4 = false;
        bool _bc5 = false;
        bool _bc7 = false;
    };
    struct KTXGPUCacheOptions
    {
        // Empty disables disk caching. Internal entries contain exact lossless
        // identity and validated block payloads, not externally imported KTX2.
        std::filesystem::path _directory;
        // Hard-capped at 1 GiB; only this helper's versioned entries are evicted.
        U64 _quotaBytes = 1024ull * 1024ull * 1024ull;
        KTXMipSemantic _semantic = KTXMipSemantic::COLOR;
        bool _allowLossyData = false;
        bool _memoryCache = true;
    };
    // COUNT means retain the exact native format. No HDR encoder is used.
    [[nodiscard]] GFXImageFormat SelectKTXGPUFormat(const KTXImage& image,
                                                   const KTXGPUCompressionOptions& options) noexcept;
    // Public libktx UASTC -> native BC blocks. Single encoder, one worker,
    // bounded tile working budget, one <=16 MiB process-local target cache,
    // optional atomic/quota-bounded persistent cache; failure leaves output
    // unchanged. cacheHit reports reuse without invoking the encoder.
    // Native authoring/cache data must never be replaced with this lossy result.
    [[nodiscard]] bool CompressKTXImageForGPU(const KTXImage& input, GFXImageFormat target,
                                             KTXImage& output, std::string* error = nullptr,
                                             const KTXGPUCacheOptions& cache = {}, bool* cacheHit = nullptr);
    struct KTXMipOptions
    {
        KTXMipSemantic _semantic = KTXMipSemantic::COLOR;
        // Supplied mip chains are retained unless regeneration is explicit.
        bool _regenerate = false;
        bool _preserveAlphaCoverage = false;
    };
    // Area-weighted filtering of each layer/face independently (no cube seams).
    // Classification uses original base samples; failure leaves image unchanged.
    [[nodiscard]] bool GenerateKTXMipmaps(KTXImage& image, const KTXMipOptions& options = {},
                                         std::string* error = nullptr);
    // Complete typed slices only; samples are copied without conversion.
    // Classification is recomputed, not taken from caller-provided flags.
    [[nodiscard]] bool EncodeKTXImage(const KTXImage& input, std::vector<Byte>& output,
                                      std::string* error = nullptr);
    [[nodiscard]] bool EncodeKTXImage(std::span<const Byte> input, U16 width, U16 height,
                                      U8 components, U8 bytesPerPixel, bool sourceIsBGR,
                                      bool srgb, std::vector<Byte>& output);
    [[nodiscard]] bool SaveKTXImage(const ResourcePath& filename, std::span<const Byte> input,
                                    U16 width, U16 height, U8 components, U8 bytesPerPixel,
                                    bool sourceIsBGR, bool srgb);
}
