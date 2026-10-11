#pragma once

#include "Platform/Video/Headers/RenderAPIEnums.h"

#include <functional>
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
    // Failure leaves output untouched.
    [[nodiscard]] bool LoadKTXImage(std::span<const Byte> input, KTXImage& output,
                                    std::string* error = nullptr);
    [[nodiscard]] bool EncodeKTXImage(std::span<const Byte> input, U16 width, U16 height,
                                      U8 components, U8 bytesPerPixel, bool sourceIsBGR,
                                      bool srgb, std::vector<Byte>& output);
    [[nodiscard]] bool SaveKTXImage(const ResourcePath& filename, std::span<const Byte> input,
                                    U16 width, U16 height, U8 components, U8 bytesPerPixel,
                                    bool sourceIsBGR, bool srgb);
}
