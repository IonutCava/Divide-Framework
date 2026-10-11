#include "Headers/KTXImage.h"

#include <ktx.h>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <type_traits>

namespace Divide::ImageTools
{
    namespace
    {
        struct TextureDeleter
        {
            void operator()(ktxTexture2* texture) const noexcept { ktxTexture2_Destroy(texture); }
        };
        using TextureOwner = std::unique_ptr<ktxTexture2, TextureDeleter>;

        struct Format
        {
            U8 _components;
            U8 _bytes;
            KTXComponentType _type;
            bool _srgb;
        };

        bool GetFormat(const U32 format, Format& result)
        {
            // Vulkan format identifiers are part of the KTX2 file format,
            // not a dependency on the engine's active rendering backend.
            switch (format)
            {
                case 9: result = {1, 1, KTXComponentType::UNORM_8, false}; break;
                case 16: result = {2, 1, KTXComponentType::UNORM_8, false}; break;
                case 37: result = {4, 1, KTXComponentType::UNORM_8, false}; break;
                case 43: result = {4, 1, KTXComponentType::UNORM_8, true}; break;
                case 70: result = {1, 2, KTXComponentType::UNORM_16, false}; break;
                case 77: result = {2, 2, KTXComponentType::UNORM_16, false}; break;
                case 91: result = {4, 2, KTXComponentType::UNORM_16, false}; break;
                case 76: result = {1, 2, KTXComponentType::FLOAT_16, false}; break;
                case 83: result = {2, 2, KTXComponentType::FLOAT_16, false}; break;
                case 97: result = {4, 2, KTXComponentType::FLOAT_16, false}; break;
                case 100: result = {1, 4, KTXComponentType::FLOAT_32, false}; break;
                case 103: result = {2, 4, KTXComponentType::FLOAT_32, false}; break;
                case 109: result = {4, 4, KTXComponentType::FLOAT_32, false}; break;
                default: return false;
            }
            return true;
        }

        bool HasOpaqueAlpha(const KTXImage& image)
        {
            if (image._components != 4u) { return true; }
            if (image._subresources.empty()) { return false; }
            for (const KTXSubresource& resource : image._subresources)
            {
                const bool opaque = std::visit([&](const auto& pixels)
                {
                    using T = typename std::decay_t<decltype(pixels)>::value_type;
                    if (pixels.empty() || pixels.size() != size_t(resource._width) * resource._height * 4u)
                    {
                        return false;
                    }
                    for (size_t alpha = 3u; alpha < pixels.size(); alpha += 4u)
                    {
                        if constexpr (std::is_same_v<T, U8>)
                        {
                            if (image._componentType != KTXComponentType::UNORM_8 || pixels[alpha] != 255u) { return false; }
                        }
                        else if constexpr (std::is_same_v<T, U16>)
                        {
                            const U16 expected = image._componentType == KTXComponentType::FLOAT_16 ? 0x3C00u : 65535u;
                            if ((image._componentType != KTXComponentType::FLOAT_16 &&
                                 image._componentType != KTXComponentType::UNORM_16) || pixels[alpha] != expected) { return false; }
                        }
                        else
                        {
                            if (image._componentType != KTXComponentType::FLOAT_32 || pixels[alpha] != 1.f) { return false; }
                        }
                    }
                    return true;
                }, resource._pixels);
                if (!opaque) { return false; }
            }
            return true;
        }

        U64 ReadLE(std::span<const Byte> bytes, const size_t offset, const size_t count)
        {
            U64 value = 0;
            for (size_t i = 0; i < count; ++i)
            {
                value |= U64(std::to_integer<U8>(bytes[offset + i])) << (8u * i);
            }
            return value;
        }

        bool Multiply(U64& value, const U64 factor)
        {
            if (factor != 0u && value > KTX_IMAGE_BYTE_LIMIT / factor)
            {
                return false;
            }
            value *= factor;
            return true;
        }

        bool Fail(std::string* error, const char* message)
        {
            if (error != nullptr)
            {
                *error = message;
            }
            return false;
        }

        bool ParseAlpha(const std::string_view text, KTXImage& image)
        {
            // Versioned, deliberately closed vocabulary: unknown interpretation
            // and classification revisions must not silently become opaque.
            constexpr std::string_view prefix = "v=1;threshold=4;transparent=0-1;translucent=2-250;opaque=251-255;";
            if (!text.starts_with(prefix))
            {
                return false;
            }
            const std::string_view tail = text.substr(prefix.size());
            if (tail == "interpretation=synthetic;skip=1;transparent=0;translucent=0;allzero=0")
            {
                image._alphaInterpretation = KTXAlphaInterpretation::SYNTHETIC;
                image._alpha._skipped = true;
                return true;
            }
            if (tail == "interpretation=data;skip=1;transparent=0;translucent=0;allzero=0")
            {
                image._alphaInterpretation = KTXAlphaInterpretation::DATA;
                image._alpha._skipped = true;
                return true;
            }
            for (U8 transparent = 0; transparent < 2; ++transparent)
            {
                for (U8 translucent = 0; translucent < 2; ++translucent)
                {
                    for (U8 zero = 0; zero < 2; ++zero)
                    {
                        if (zero != 0u && (transparent == 0u || translucent != 0u))
                        {
                            continue;
                        }
                        const std::string expected = "interpretation=opacity;skip=0;transparent=" +
                            std::to_string(transparent) + ";translucent=" + std::to_string(translucent) +
                            ";allzero=" + std::to_string(zero);
                        if (tail == expected)
                        {
                            image._alphaInterpretation = KTXAlphaInterpretation::OPACITY;
                            image._alpha._transparent = transparent != 0u;
                            image._alpha._translucent = translucent != 0u;
                            image._alpha._allZero = zero != 0u;
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool Validate(std::span<const Byte> input, KTXImage& image, Format& format,
                      U64& decodedBytes, std::string* error)
        {
            constexpr U8 identifier[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
            if (input.size() < 80u || input.size() > KTX_IMAGE_BYTE_LIMIT ||
                std::memcmp(input.data(), identifier, sizeof(identifier)) != 0)
            {
                return Fail(error, "Invalid or oversized KTX2 header");
            }
            const U32 vkFormat = static_cast<U32>(ReadLE(input, 12, 4));
            if (!GetFormat(vkFormat, format) || ReadLE(input, 16, 4) != format._bytes ||
                ReadLE(input, 44, 4) != 0u || ReadLE(input, 64, 8) != 0u || ReadLE(input, 72, 8) != 0u)
            {
                return Fail(error, "Unsupported format, Basis or supercompression");
            }
            const U64 width = ReadLE(input, 20, 4), height = ReadLE(input, 24, 4);
            const U64 depth = ReadLE(input, 28, 4), layers = ReadLE(input, 32, 4);
            const U64 faces = ReadLE(input, 36, 4), mips = ReadLE(input, 40, 4);
            if (width == 0u || width > 65535u || height > 65535u || depth > 65535u ||
                layers > 65535u || (faces != 1u && faces != 6u) || mips == 0u || mips > 16u ||
                (height == 0u && depth != 0u) || (depth != 0u && (layers != 0u || faces != 1u)) ||
                (faces == 6u && (height != width || depth != 0u)))
            {
                return Fail(error, "Invalid KTX2 dimensions");
            }
            U64 largest = std::max(width, std::max(height, depth));
            U64 maxMips = 1u;
            while (largest > 1u) { largest >>= 1u; ++maxMips; }
            const U64 indexEnd = 80u + 24u * mips;
            if (mips > maxMips || indexEnd > input.size())
            {
                return Fail(error, "Invalid KTX2 mip index");
            }
            image._width = static_cast<U16>(width);
            image._height = static_cast<U16>(std::max(U64{1}, height));
            image._depth = static_cast<U16>(std::max(U64{1}, depth));
            image._layers = static_cast<U16>(std::max(U64{1}, layers));
            image._dimensions = depth != 0u ? 3u : height != 0u ? 2u : 1u;
            image._faces = static_cast<U8>(faces);
            image._mips = static_cast<U8>(mips);
            image._components = format._components;
            image._componentType = format._type;
            image._srgb = format._srgb;
            image._isArray = layers != 0u;
            image._native = true;
            image._format = format._components == 1u ? GFXImageFormat::RED :
                            format._components == 2u ? GFXImageFormat::RG : GFXImageFormat::RGBA;
            image._dataType = format._type == KTXComponentType::UNORM_8 ? GFXDataFormat::UNSIGNED_BYTE :
                              format._type == KTXComponentType::UNORM_16 ? GFXDataFormat::UNSIGNED_SHORT :
                              format._type == KTXComponentType::FLOAT_16 ? GFXDataFormat::FLOAT_16 : GFXDataFormat::FLOAT_32;
            image._packing = format._srgb ? GFXImagePacking::NORMALIZED_SRGB : GFXImagePacking::NORMALIZED;
            image._textureType = faces == 6u ? (image._isArray ? TextureType::TEXTURE_CUBE_ARRAY : TextureType::TEXTURE_CUBE_MAP) :
                                  image._dimensions == 3u ? TextureType::TEXTURE_3D :
                                  image._dimensions == 1u ? (image._isArray ? TextureType::TEXTURE_1D_ARRAY : TextureType::TEXTURE_1D) :
                                  image._isArray ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;

            struct Range { U64 _start; U64 _end; };
            std::vector<Range> ranges{{0u, indexEnd}};
            const auto addRange = [&](const U64 offset, const U64 length)
            {
                if (offset > input.size() || length > input.size() - offset)
                {
                    return false;
                }
                for (const Range& range : ranges)
                {
                    if (offset < range._end && offset + length > range._start)
                    {
                        return false;
                    }
                }
                ranges.push_back({offset, offset + length});
                return true;
            };
            const U64 dfdOffset = ReadLE(input, 48, 4), dfdSize = ReadLE(input, 52, 4);
            const U64 kvdOffset = ReadLE(input, 56, 4), kvdSize = ReadLE(input, 60, 4);
            if (dfdOffset % 4u != 0u || dfdSize != 28u + 16u * format._components ||
                !addRange(dfdOffset, dfdSize) ||
                ReadLE(input, static_cast<size_t>(dfdOffset), 4) != dfdSize ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 4), 4) != 0u ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 8), 2) != 2u ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 10), 2) != dfdSize - 4u ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 12), 1) != 1u ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 14), 1) != (format._srgb ? 2u : 1u) ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 15), 1) != 0u ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 16), 4) != 0u ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 20), 1) != format._bytes * format._components ||
                ReadLE(input, static_cast<size_t>(dfdOffset + 21), 7) != 0u)
            {
                return Fail(error, "Unsupported or malformed KTX2 data descriptor");
            }
            // Validate every sample before libktx inspects the descriptor.
            for (U8 component = 0; component < format._components; ++component)
            {
                const size_t sample = static_cast<size_t>(dfdOffset + 28u + 16u * component);
                const U8 channel = component == 3u ? 15u : component;
                const U8 qualifiers = format._type == KTXComponentType::FLOAT_16 ||
                                      format._type == KTXComponentType::FLOAT_32 ? 0xC0u : 0u;
                if (ReadLE(input, sample, 2) != component * format._bytes * 8u ||
                    ReadLE(input, sample + 2, 1) != format._bytes * 8u - 1u ||
                    ReadLE(input, sample + 3, 1) != (channel | qualifiers | (format._srgb && component == 3u ? 0x10u : 0u)) ||
                    ReadLE(input, sample + 4, 4) != 0u ||
                    ReadLE(input, sample + 8, 4) != (qualifiers != 0u ? 0xBF800000u : 0u) ||
                    ReadLE(input, sample + 12, 4) != (qualifiers != 0u ? 0x3F800000u : format._bytes == 1u ? 255u : 65535u))
                {
                    return Fail(error, "Unsupported KTX2 component descriptor");
                }
            }
            if (kvdSize > 65536u || (kvdSize == 0u && kvdOffset != 0u) ||
                (kvdSize != 0u && (kvdOffset % 4u != 0u || !addRange(kvdOffset, kvdSize))))
            {
                return Fail(error, "Invalid KTX2 metadata range");
            }
            std::set<std::string_view> keys;
            U64 cursor = kvdOffset;
            const U64 end = kvdOffset + kvdSize;
            while (cursor < end)
            {
                if (end - cursor < 4u) { return Fail(error, "Truncated KTX2 metadata"); }
                const U64 length = ReadLE(input, static_cast<size_t>(cursor), 4);
                cursor += 4u;
                if (length < 2u || length > end - cursor) { return Fail(error, "Invalid KTX2 metadata length"); }
                const char* entry = reinterpret_cast<const char*>(input.data() + cursor);
                const char* separator = static_cast<const char*>(std::memchr(entry, 0, static_cast<size_t>(length)));
                if (separator == nullptr || separator == entry) { return Fail(error, "Invalid KTX2 metadata key"); }
                const std::string_view key(entry, static_cast<size_t>(separator - entry));
                if (key.size() > 1024u || keys.size() >= 256u)
                {
                    return Fail(error, "KTX2 metadata exceeds entry budget");
                }
                if (!keys.insert(key).second) { return Fail(error, "Duplicate KTX2 metadata"); }
                const U64 keySize = static_cast<U64>(separator - entry) + 1u;
                const std::string_view raw(separator + 1, static_cast<size_t>(length - keySize));
                const bool known = key == "KTXorientation" || key == "KTXswizzle" || key == "DivideAlpha";
                if (known && (raw.empty() || raw.back() != '\0' ||
                    raw.substr(0, raw.size() - 1u).find('\0') != std::string_view::npos))
                {
                    return Fail(error, "Malformed KTX2 metadata value");
                }
                const std::string_view value = known ? raw.substr(0, raw.size() - 1u) : std::string_view{};
                if (key == "KTXorientation")
                {
                    const std::string_view orientation = image._dimensions == 3u ? "rui" : image._dimensions == 2u ? "ru" : "r";
                    if (value != orientation) { return Fail(error, "Unsupported KTX2 orientation"); }
                }
                else if (key == "KTXswizzle")
                {
                    if (value != "rgba") { return Fail(error, "Unsupported KTX2 swizzle"); }
                }
                else if (key == "DivideAlpha")
                {
                    if (!ParseAlpha(value, image)) { return Fail(error, "Unknown KTX2 alpha metadata"); }
                }
                cursor += length;
                const U64 padding = (4u - (length % 4u)) % 4u;
                if (padding > end - cursor) { return Fail(error, "Invalid KTX2 metadata padding"); }
                for (U64 i = 0; i < padding; ++i)
                {
                    if (input[static_cast<size_t>(cursor + i)] != Byte{0})
                    {
                        return Fail(error, "Nonzero KTX2 metadata padding");
                    }
                }
                cursor += padding;
            }
            // KTX2's missing-orientation default is top-left, not our canonical
            // bottom-left storage. Require an explicit declaration in phase one.
            if (!keys.contains("KTXorientation"))
            {
                return Fail(error, "KTX2 requires explicit bottom-left orientation");
            }
            if (image._alphaInterpretation == KTXAlphaInterpretation::OPACITY && image._components != 4u)
            {
                return Fail(error, "KTX2 opacity metadata requires an alpha channel");
            }
            decodedBytes = 0u;
            U64 resourceBytes = 0u;
            std::array<U64, 16> levelOffsets{};
            std::array<U64, 16> paddedLengths{};
            for (U64 mip = 0; mip < mips; ++mip)
            {
                U64 expected = std::max(U64{1}, width >> mip);
                if (!Multiply(expected, std::max(U64{1}, height >> mip)) ||
                    !Multiply(expected, std::max(U64{1}, depth >> mip)) ||
                    !Multiply(expected, image._layers) || !Multiply(expected, faces) ||
                    !Multiply(expected, format._components * format._bytes))
                {
                    return Fail(error, "KTX2 decoded size exceeds budget");
                }
                const size_t index = static_cast<size_t>(80u + 24u * mip);
                const U64 offset = ReadLE(input, index, 8), length = ReadLE(input, index + 8, 8);
                const U64 alignment = std::max(U64{4}, U64(format._components * format._bytes));
                const U64 paddedLength = mip == 0u ? expected : (expected + alignment - 1u) / alignment * alignment;
                U64 resources = std::max(U64{1}, depth >> mip);
                if (!Multiply(resources, image._layers) || !Multiply(resources, faces) ||
                    !Multiply(resources, sizeof(KTXSubresource)) ||
                    resources > KTX_IMAGE_BYTE_LIMIT - resourceBytes)
                {
                    return Fail(error, "KTX2 subresource count exceeds budget");
                }
                resourceBytes += resources;
                if (length != expected || ReadLE(input, index + 16, 8) != expected ||
                    offset % alignment != 0u || !addRange(offset, length) ||
                    paddedLength > KTX_IMAGE_BYTE_LIMIT - input.size() - decodedBytes)
                {
                    return Fail(error, "Invalid KTX2 level range or decoded budget");
                }
                decodedBytes += paddedLength;
                levelOffsets[static_cast<size_t>(mip)] = offset;
                paddedLengths[static_cast<size_t>(mip)] = paddedLength;
            }
            // libktx allocates the span from the smallest mip through level 0.
            // Require canonical adjacency so gaps cannot inflate that allocation.
            for (size_t mip = 1u; mip < mips; ++mip)
            {
                if (levelOffsets[mip - 1u] != levelOffsets[mip] + paddedLengths[mip])
                {
                    return Fail(error, "Noncanonical KTX2 mip layout");
                }
            }
            const U64 metadataAllowance = 2u * kvdSize + 65536u;
            if (metadataAllowance > KTX_IMAGE_BYTE_LIMIT - input.size() ||
                decodedBytes > (KTX_IMAGE_BYTE_LIMIT - input.size() - metadataAllowance) / 2u ||
                resourceBytes > KTX_IMAGE_BYTE_LIMIT - input.size() - metadataAllowance - 2u * decodedBytes)
            {
                return Fail(error, "KTX2 owned payload exceeds memory budget");
            }
            return true;
        }
    }

    AlphaClassification ClassifyKTXAlpha(const std::span<const Byte> alpha, const bool synthetic,
                                         const bool data, const std::function<void(std::string_view)>& errorfn)
    {
        AlphaClassification result;
        result._skipped = synthetic || data;
        if (result._skipped || alpha.empty()) { return result; }
        result._allZero = true;
        for (const Byte byte : alpha)
        {
            const U8 value = std::to_integer<U8>(byte);
            result._allZero = result._allZero && value == 0u;
            if (value <= 1u) { ++result._transparentCount; }
            else if (value <= 250u) { ++result._translucentCount; }
            else { ++result._opaqueCount; }
        }
        result._transparent = result._transparentCount > 4u;
        result._translucent = result._translucentCount > 4u;
        if (result._allZero)
        {
            if (errorfn)
            {
                errorfn("KTX2 opacity alpha is all zero; classifying as transparent");
            }
            result._transparent = true;
            result._translucent = false;
        }
        return result;
    }

    bool ClassifyKTXImageAlpha(KTXImage& image, const KTXAlphaInterpretation interpretation)
    {
        if (interpretation == KTXAlphaInterpretation::UNKNOWN)
        {
            return false;
        }
        AlphaClassification classification;
        if (interpretation == KTXAlphaInterpretation::DATA || interpretation == KTXAlphaInterpretation::SYNTHETIC)
        {
            if (interpretation == KTXAlphaInterpretation::SYNTHETIC && !HasOpaqueAlpha(image))
            {
                return false;
            }
            classification._skipped = true;
            image._alpha = classification;
            image._alphaInterpretation = interpretation;
            return true;
        }
        if (interpretation != KTXAlphaInterpretation::OPACITY || image._components != 4u)
        {
            return false;
        }
        classification._allZero = true;
        size_t samples = 0u;
        for (const KTXSubresource& resource : image._subresources)
        {
            if (resource._mip != 0u) { continue; }
            const bool valid = std::visit([&](const auto& pixels)
            {
                using T = typename std::decay_t<decltype(pixels)>::value_type;
                if (pixels.empty() || pixels.size() != size_t(resource._width) * resource._height * 4u)
                {
                    return false;
                }
                for (size_t component = 3u; component < pixels.size(); component += 4u)
                {
                    double alpha = 0.0;
                    if constexpr (std::is_same_v<T, U8>)
                    {
                        if (image._componentType != KTXComponentType::UNORM_8) { return false; }
                        alpha = double(pixels[component]) / 255.0;
                    }
                    else if constexpr (std::is_same_v<T, U16>)
                    {
                        if (image._componentType == KTXComponentType::UNORM_16)
                        {
                            alpha = double(pixels[component]) / 65535.0;
                        }
                        else if (image._componentType == KTXComponentType::FLOAT_16)
                        {
                            const U16 bits = pixels[component];
                            const U16 exponent = (bits >> 10u) & 31u;
                            const U16 mantissa = bits & 1023u;
                            if (exponent == 31u) { return false; }
                            alpha = exponent == 0u ? std::ldexp(double(mantissa), -24) :
                                std::ldexp(double(1024u + mantissa), int(exponent) - 25);
                            if ((bits & 0x8000u) != 0u) { alpha = -alpha; }
                        }
                        else { return false; }
                    }
                    else
                    {
                        if (image._componentType != KTXComponentType::FLOAT_32) { return false; }
                        alpha = double(pixels[component]);
                    }
                    if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0) { return false; }
                    classification._allZero = classification._allZero && alpha == 0.0;
                    U8 bucket = static_cast<U8>(std::floor(std::clamp(alpha, 0.0, 1.0) * 255.0));
                    if constexpr (std::is_same_v<T, U8>)
                    {
                        bucket = pixels[component];
                    }
                    else if constexpr (std::is_same_v<T, U16>)
                    {
                        if (image._componentType == KTXComponentType::UNORM_16)
                        {
                            bucket = static_cast<U8>(U32(pixels[component]) * 255u / 65535u);
                        }
                    }
                    if (bucket <= 1u) { ++classification._transparentCount; }
                    else if (bucket <= 250u) { ++classification._translucentCount; }
                    else { ++classification._opaqueCount; }
                    ++samples;
                }
                return true;
            }, resource._pixels);
            if (!valid) { return false; }
        }
        if (samples == 0u) { return false; }
        classification._transparent = classification._transparentCount > 4u;
        classification._translucent = classification._translucentCount > 4u;
        if (classification._allZero)
        {
            Console::errorfn("KTX2 opacity alpha is all zero; classifying as transparent");
            classification._transparent = true;
            classification._translucent = false;
        }
        image._alpha = classification;
        image._alphaInterpretation = interpretation;
        return true;
    }

    bool LoadKTXImage(const std::span<const Byte> input, KTXImage& output, std::string* error)
    {
        KTXImage image;
        Format format{};
        U64 decodedBytes = 0u;
        if (!Validate(input, image, format, decodedBytes, error)) { return false; }
        ktxTexture2* raw = nullptr;
        const KTX_error_code status = ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t*>(input.data()), input.size(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
        const TextureOwner texture(raw);
        if (status != KTX_SUCCESS || !texture || ktxTexture2_NeedsTranscoding(raw) ||
            raw->dataSize != decodedBytes || raw->pData == nullptr ||
            ktxTexture2_GetNumComponents(raw) != format._components)
        {
            return Fail(error, "libktx could not load native image");
        }
        size_t resourceCount = 0u;
        for (U32 mip = 0; mip < image._mips; ++mip)
        {
            resourceCount += size_t(image._layers) * image._faces * std::max(U32{1}, U32(image._depth) >> mip);
        }
        image._subresources.reserve(resourceCount);
        for (U32 mip = 0; mip < image._mips; ++mip)
        {
            const U16 width = static_cast<U16>(std::max(U32{1}, U32(image._width) >> mip));
            const U16 height = static_cast<U16>(std::max(U32{1}, U32(image._height) >> mip));
            const U32 depth = std::max(U32{1}, U32(image._depth) >> mip);
            const size_t samples = size_t(width) * height * format._components;
            const size_t bytes = samples * format._bytes;
            for (U32 layer = 0; layer < image._layers; ++layer)
            {
                for (U32 face = 0; face < image._faces; ++face)
                {
                    for (U32 slice = 0; slice < depth; ++slice)
                    {
                        ktx_size_t offset = 0u;
                        if (ktxTexture2_GetImageOffset(raw, mip, layer, image._dimensions == 3u ? slice : face, &offset) != KTX_SUCCESS ||
                            offset > raw->dataSize || bytes > raw->dataSize - offset)
                        {
                            return Fail(error, "Invalid libktx subresource offset");
                        }
                        KTXSubresource resource;
                        resource._mip = mip; resource._layer = layer; resource._face = face;
                        resource._depthSlice = slice; resource._width = width; resource._height = height;
                        if (format._bytes == 1u) { resource._pixels = std::vector<U8>(samples); }
                        else if (format._bytes == 2u) { resource._pixels = std::vector<U16>(samples); }
                        else { resource._pixels = std::vector<F32>(samples); }
                        std::visit([&](auto& pixels) { std::memcpy(pixels.data(), raw->pData + offset, bytes); }, resource._pixels);
                        image._subresources.push_back(std::move(resource));
                    }
                }
            }
        }
        if (image._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC && !HasOpaqueAlpha(image))
        {
            return Fail(error, "KTX2 synthetic alpha must be fully opaque");
        }
        if (image._alphaInterpretation == KTXAlphaInterpretation::OPACITY && image._alpha._allZero)
        {
            Console::errorfn("KTX2 metadata records all-zero opacity alpha; classifying as transparent");
        }
        output = std::move(image);
        if (error != nullptr) { error->clear(); }
        return true;
    }

    bool EncodeKTXImage(const std::span<const Byte> input, const U16 width, const U16 height,
                        const U8 components, const U8 bytesPerPixel, const bool sourceIsBGR,
                        const bool srgb, std::vector<Byte>& output)
    {
        if (width == 0u || height == 0u || components < 1u || components > 4u ||
            bytesPerPixel != components || (sourceIsBGR && components < 3u) ||
            (srgb && components < 3u))
        {
            return false;
        }
        U64 size = width;
        const U8 outputComponents = components == 3u ? 4u : components;
        if (!Multiply(size, height) || !Multiply(size, outputComponents) ||
            input.size() != size_t(width) * height * components ||
            4u * size + input.size() + (components == 4u ? size / 4u : 0u) + 4096u > KTX_IMAGE_BYTE_LIMIT)
        {
            return false;
        }
        std::vector<Byte> pixels(static_cast<size_t>(size));
        std::vector<Byte> alpha;
        if (components == 4u) { alpha.reserve(size_t(width) * height); }
        for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
        {
            for (U8 component = 0; component < outputComponents; ++component)
            {
                U8 source = component;
                if (sourceIsBGR && component < 3u) { source = 2u - component; }
                pixels[pixel * outputComponents + component] = component >= components ?
                    Byte{255} : input[pixel * components + source];
            }
            if (components == 4u) { alpha.push_back(pixels[pixel * outputComponents + 3u]); }
        }
        const AlphaClassification classification = ClassifyKTXAlpha(alpha, components != 4u, false,
            [](const std::string_view message) { Console::errorfn("{}", message); });
        ktxTextureCreateInfo info{};
        info.vkFormat = outputComponents == 1u ? 9u : outputComponents == 2u ? 16u : srgb ? 43u : 37u;
        info.baseWidth = width; info.baseHeight = height; info.baseDepth = 1;
        info.numDimensions = 2; info.numLevels = 1; info.numLayers = 1; info.numFaces = 1;
        ktxTexture2* raw = nullptr;
        const KTX_error_code status = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
        const TextureOwner texture(raw);
        if (status != KTX_SUCCESS || !texture) { return false; }
        const auto addMetadata = [&](const char* key, const std::string_view value)
        {
            return ktxHashList_AddKVPair(&raw->kvDataHead, key, static_cast<unsigned int>(value.size() + 1u),
                                       value.data()) == KTX_SUCCESS;
        };
        std::string metadata = "v=1;threshold=4;transparent=0-1;translucent=2-250;opaque=251-255;";
        if (components != 4u)
        {
            metadata += "interpretation=synthetic;skip=1;transparent=0;translucent=0;allzero=0";
        }
        else
        {
            metadata += "interpretation=opacity;skip=0;transparent=" + std::to_string(classification._transparent) +
                        ";translucent=" + std::to_string(classification._translucent) +
                        ";allzero=" + std::to_string(classification._allZero);
        }
        if (!addMetadata("KTXorientation", "ru") || !addMetadata("DivideAlpha", metadata) ||
            ktxTexture_SetImageFromMemory(ktxTexture(raw), 0, 0, 0,
                reinterpret_cast<const ktx_uint8_t*>(pixels.data()), pixels.size()) != KTX_SUCCESS)
        {
            return false;
        }
        ktx_uint8_t* encoded = nullptr;
        ktx_size_t encodedSize = 0;
        const KTX_error_code written = ktxTexture2_WriteToMemory(raw, &encoded, &encodedSize);
        const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> encodedOwner(encoded, &std::free);
        if (written != KTX_SUCCESS || encoded == nullptr || encodedSize > KTX_IMAGE_BYTE_LIMIT) { return false; }
        output.assign(reinterpret_cast<const Byte*>(encoded), reinterpret_cast<const Byte*>(encoded) + encodedSize);
        return true;
    }

    bool SaveKTXImage(const ResourcePath& filename, const std::span<const Byte> input,
                      const U16 width, const U16 height, const U8 components, const U8 bytesPerPixel,
                      const bool sourceIsBGR, const bool srgb)
    {
        std::vector<Byte> encoded;
        if (!EncodeKTXImage(input, width, height, components, bytesPerPixel, sourceIsBGR, srgb, encoded)) { return false; }
        const auto path = filename.string();
        FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr) { return false; }
        const bool success = std::fwrite(encoded.data(), 1u, encoded.size(), file) == encoded.size();
        const bool closed = std::fclose(file) == 0;
        return success && closed;
    }
}
