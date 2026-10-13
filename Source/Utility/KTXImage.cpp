#include "Headers/KTXImage.h"
#include "EngineConfig.h"

#include <ktx.h>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <limits>
#include <set>
#include <type_traits>
#include <tuple>
#include <mutex>
#include <fstream>
#include <random>

namespace Divide::ImageTools
{
    GFXImageFormat SelectKTXGPUFormat(const KTXImage& image, const KTXGPUCompressionOptions& options) noexcept
    {
        if (image._compressed || image._componentType != KTXComponentType::UNORM_8 ||
            image._dimensions != 2u || image._depth != 1u || !options._sampledOnly ||
            options._preserveCPUData || (image._components != 1u && image._components != 2u && image._components != 4u) ||
            (options._semantic == KTXMipSemantic::NORMAL && image._components != 4u))
        {
            return GFXImageFormat::COUNT;
        }
        const bool data = options._semantic == KTXMipSemantic::DATA ||
                          image._alphaInterpretation == KTXAlphaInterpretation::DATA || image._components != 4u;
        if (data && !options._allowLossyData) { return GFXImageFormat::COUNT; }
        if (!image._srgb && image._components == 1u && options._bc4) { return GFXImageFormat::BC4u; }
        if (!image._srgb && image._components == 2u && options._bc5) { return GFXImageFormat::BC5u; }
        // RGBA normal maps retain Z and genuine alpha: no implicit reconstruction.
        if (image._components == 4u && options._bc7) { return GFXImageFormat::BC7; }
        return GFXImageFormat::COUNT;
    }

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

        constexpr std::string_view GPU_CACHE_PREFIX = "divide-ktx-gpu-v1-";
        constexpr std::array<char, 8> GPU_CACHE_MAGIC{'D','V','K','T','G','P','U','1'};
        constexpr U64 GPU_HASH_SEED = 14695981039346656037ull;

        U64 GPUHash(const std::span<const Byte> bytes, U64 hash = GPU_HASH_SEED)
        {
            for (const Byte byte : bytes) { hash = (hash ^ std::to_integer<U8>(byte)) * 1099511628211ull; }
            return hash;
        }

        std::vector<Byte> GPUIdentity(const KTXImage& input, const GFXImageFormat target, const KTXGPUCacheOptions& options)
        {
            std::vector<Byte> identity;
            size_t bytes = 160u + input._subresources.size() * 56u;
            for (const auto& resource : input._subresources) { bytes += std::get<std::vector<U8>>(resource._pixels).size(); }
            identity.reserve(bytes);
            const auto append = [&](const U64 value)
            {
                for (U32 i = 0u; i < 8u; ++i) { identity.push_back(Byte((value >> (i * 8u)) & 255u)); }
            };
            // Schema, libktx version, channel policy, tile size, quality and
            // semantics are part of identity, not merely the filename digest.
            for (const U64 value : std::array<U64, 20>{1u, 40402u, 1u, 512u, to_base(target),
                target == GFXImageFormat::BC7 ? U64(KTX_PACK_UASTC_LEVEL_SLOWER | KTX_PACK_UASTC_FAVOR_BC7_ERROR)
                                             : U64(KTX_PACK_UASTC_LEVEL_DEFAULT),
                to_base(options._semantic), options._allowLossyData, input._width, input._height, input._depth,
                input._layers, input._faces, input._mips, input._components, input._dimensions,
                input._isArray, input._srgb, to_base(input._alphaInterpretation), to_base(input._textureType)})
            {
                append(value);
            }
            for (const auto& resource : input._subresources)
            {
                for (const U64 value : {U64(resource._mip), U64(resource._layer), U64(resource._face),
                                       U64(resource._depthSlice), U64(resource._width), U64(resource._height)})
                {
                    append(value);
                }
                const auto& pixels = std::get<std::vector<U8>>(resource._pixels);
                append(pixels.size());
                const auto* data = reinterpret_cast<const Byte*>(pixels.data());
                identity.insert(identity.end(), data, data + pixels.size());
            }
            return identity;
        }

        KTXImage GPUImageHeader(const KTXImage& input, const GFXImageFormat target)
        {
            KTXImage result;
            result._width = input._width; result._height = input._height;
            result._layers = input._layers; result._faces = input._faces; result._mips = input._mips;
            result._components = input._components; result._isArray = input._isArray;
            result._srgb = input._srgb; result._native = true; result._compressed = true;
            result._format = target; result._dataType = GFXDataFormat::UNSIGNED_BYTE;
            result._packing = input._srgb ? GFXImagePacking::NORMALIZED_SRGB : GFXImagePacking::NORMALIZED;
            result._textureType = input._faces == 6u
                ? input._isArray ? TextureType::TEXTURE_CUBE_ARRAY : TextureType::TEXTURE_CUBE_MAP
                : input._isArray ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
            result._alphaInterpretation = input._alphaInterpretation; result._alpha = input._alpha;
            return result;
        }

        bool ReadGPUEntry(const std::filesystem::path& path, const std::span<const Byte> identity,
                          const KTXImage& input, const GFXImageFormat target, const U64 payloadSize, KTXImage& output)
        {
            std::error_code ec;
            if (std::filesystem::symlink_status(path, ec).type() != std::filesystem::file_type::regular || ec ||
                std::filesystem::file_size(path, ec) != GPU_CACHE_MAGIC.size() + 24u + identity.size() + payloadSize || ec)
            {
                return false;
            }
            std::ifstream stream(path, std::ios::binary);
            std::array<char, 8> magic{};
            stream.read(magic.data(), magic.size());
            const auto number = [&]()
            {
                std::array<Byte, 8> bytes{};
                stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                return ReadLE(bytes, 0u, 8u);
            };
            const U64 identitySize = number(), storedSize = number(), storedHash = number();
            if (!stream || magic != GPU_CACHE_MAGIC || identitySize != identity.size() || storedSize != payloadSize) { return false; }
            std::array<Byte, 8192> compare{};
            for (size_t offset = 0u; offset < identity.size();)
            {
                const size_t count = std::min(compare.size(), identity.size() - offset);
                stream.read(reinterpret_cast<char*>(compare.data()), static_cast<std::streamsize>(count));
                if (!stream || !std::equal(compare.begin(), compare.begin() + count, identity.begin() + offset)) { return false; }
                offset += count;
            }
            KTXImage result = GPUImageHeader(input, target);
            U64 hash = GPU_HASH_SEED;
            const size_t blockBytes = target == GFXImageFormat::BC4u ? 8u : 16u;
            for (const auto& resource : input._subresources)
            {
                KTXSubresource blocks;
                blocks._mip = resource._mip; blocks._layer = resource._layer; blocks._face = resource._face;
                blocks._width = resource._width; blocks._height = resource._height;
                const size_t size = size_t((resource._width + 3u) / 4u) * ((resource._height + 3u) / 4u) * blockBytes;
                blocks._pixels = std::vector<U8>(size);
                auto& bytes = std::get<std::vector<U8>>(blocks._pixels);
                stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
                if (!stream) { return false; }
                hash = GPUHash({reinterpret_cast<const Byte*>(bytes.data()), bytes.size()}, hash);
                result._subresources.push_back(std::move(blocks));
            }
            if (hash != storedHash || stream.peek() != std::char_traits<char>::eof()) { return false; }
            output = std::move(result);
            return true;
        }

        bool OwnedGPUEntry(const std::filesystem::path& path)
        {
            const std::string name = path.filename().string();
            if (!name.starts_with(GPU_CACHE_PREFIX) || !name.ends_with(".entry")) { return false; }
            const auto key = std::string_view(name).substr(GPU_CACHE_PREFIX.size(), name.size() - GPU_CACHE_PREFIX.size() - 6u);
            return !key.empty() && key.size() <= 20u &&
                   std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        bool EvictGPUEntries(const std::filesystem::path& directory, const U64 quota, const U64 incoming)
        {
            struct Entry { std::filesystem::path _path; std::filesystem::file_time_type _time; U64 _bytes; };
            std::vector<Entry> entries;
            U64 total = incoming;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
            {
                if (!OwnedGPUEntry(it->path()) || it->symlink_status(ec).type() != std::filesystem::file_type::regular || ec)
                {
                    ec.clear(); continue;
                }
                if (entries.size() >= 4096u) { return false; }
                const auto size = it->file_size(ec);
                if (ec) { ec.clear(); continue; }
                const auto time = it->last_write_time(ec);
                if (ec) { ec.clear(); continue; }
                total = size > U64_MAX - total ? U64_MAX : total + size;
                entries.push_back({it->path(), time, size});
            }
            if (ec) { return false; }
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a._time < b._time; });
            for (const auto& entry : entries)
            {
                if (total <= quota) { break; }
                if (std::filesystem::remove(entry._path, ec)) { total -= entry._bytes; }
                ec.clear();
            }
            return total <= quota;
        }

        void PublishGPUEntry(const std::filesystem::path& path, const std::span<const Byte> identity,
                             const KTXImage& image, const U64 payloadSize, const U64 requestedQuota)
        {
            const U64 quota = std::min<U64>(requestedQuota, 1024ull * 1024ull * 1024ull);
            const U64 size = GPU_CACHE_MAGIC.size() + 24u + identity.size() + payloadSize;
            if (size > quota || size > KTX_IMAGE_BYTE_LIMIT) { return; }
            std::error_code ec;
            const auto directory = path.parent_path();
            std::filesystem::create_directories(directory, ec);
            if (ec) { return; }
            const U64 nonce = (U64(std::random_device{}()) << 32u) ^ std::random_device{}();
            auto partial = path;
            partial += "." + std::to_string(nonce) + ".part";
            // Exclusive reservation, then same-directory rename: readers see
            // either a complete validated entry or a miss, never partial bytes.
            if (!std::filesystem::create_directory(partial, ec)) { return; }
            struct Cleanup
            {
                std::filesystem::path _directory;
                ~Cleanup()
                {
                    std::error_code ignored;
                    std::filesystem::remove(_directory / "entry", ignored);
                    std::filesystem::remove(_directory, ignored);
                }
            } cleanup{partial};
            const auto pending = partial / "entry";
            std::ofstream stream(pending, std::ios::binary | std::ios::trunc);
            if (!stream) { return; }
            const auto number = [&](const U64 value)
            {
                std::array<unsigned char, 8> bytes{};
                for (U32 i = 0u; i < 8u; ++i) { bytes[i] = static_cast<unsigned char>(value >> (i * 8u)); }
                stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            };
            U64 hash = GPU_HASH_SEED;
            for (const auto& resource : image._subresources)
            {
                const auto& bytes = std::get<std::vector<U8>>(resource._pixels);
                hash = GPUHash({reinterpret_cast<const Byte*>(bytes.data()), bytes.size()}, hash);
            }
            stream.write(GPU_CACHE_MAGIC.data(), GPU_CACHE_MAGIC.size());
            number(identity.size()); number(payloadSize); number(hash);
            stream.write(reinterpret_cast<const char*>(identity.data()), static_cast<std::streamsize>(identity.size()));
            for (const auto& resource : image._subresources)
            {
                const auto& bytes = std::get<std::vector<U8>>(resource._pixels);
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            stream.flush();
            if (!stream) { return; }
            stream.close();
            if (!stream) { return; }
            // Serialize quota accounting and publication across engine processes.
            // A busy/stale lock only disables this optional disk publication.
            const auto publicationLock = directory / "divide-ktx-gpu-publication.lock";
            if (!std::filesystem::create_directory(publicationLock, ec)) { return; }
            struct UnlockPublication
            {
                std::filesystem::path _path;
                ~UnlockPublication()
                {
                    std::error_code ignored;
                    std::filesystem::remove(_path, ignored);
                }
            } unlockPublication{publicationLock};
            if (!EvictGPUEntries(directory, quota, size)) { return; }
            std::filesystem::rename(pending, path, ec);
#if defined(_WIN32)
            if (ec)
            {
                std::filesystem::remove(path, ec);
                if (!ec) { std::filesystem::rename(pending, path, ec); }
            }
#endif
        }

        U8 FullMipCount(const KTXImage& image)
        {
            U32 size = std::max(U32(image._width), std::max(U32(image._height), U32(image._depth)));
            U8 count = 1u;
            while (size > 1u) { size >>= 1u; ++count; }
            return count;
        }

        bool ValidateTyped(const KTXImage& image, U32& vkFormat, U64& bytes, std::string* error)
        {
            if (image._width == 0u || image._height == 0u || image._depth == 0u ||
                image._layers == 0u || image._dimensions < 1u || image._dimensions > 3u ||
                (image._dimensions == 1u && image._height != 1u) ||
                (image._dimensions != 3u && image._depth != 1u) ||
                (image._dimensions == 3u && (image._isArray || image._layers != 1u || image._faces != 1u)) ||
                (!image._isArray && image._layers != 1u) ||
                (image._faces != 1u && image._faces != 6u) ||
                (image._faces == 6u && (image._dimensions != 2u || image._width != image._height)) ||
                image._mips == 0u || image._mips > FullMipCount(image) || image._compressed)
            {
                return Fail(error, "Invalid typed KTX topology");
            }
            const U32 index = image._components == 1u ? 0u : image._components == 2u ? 1u :
                              image._components == 4u ? 2u : 3u;
            if (index == 3u || (image._srgb && (image._componentType != KTXComponentType::UNORM_8 || index != 2u)))
            {
                return Fail(error, "Unsupported typed KTX format");
            }
            constexpr U32 formats[][3] = {{9, 16, 37}, {70, 77, 91}, {76, 83, 97}, {100, 103, 109}};
            const U32 type = static_cast<U32>(image._componentType);
            if (type >= 4u) { return Fail(error, "Invalid typed KTX component type"); }
            vkFormat = image._srgb ? 43u : formats[type][index];
            const U32 componentBytes = type == 0u ? 1u : type == 3u ? 4u : 2u;
            bytes = 0u;
            size_t expected = 0u;
            for (U32 mip = 0u; mip < image._mips; ++mip)
            {
                U64 level = std::max(1u, U32(image._width) >> mip);
                const U32 slices = std::max(1u, U32(image._depth) >> mip);
                if (!Multiply(level, std::max(1u, U32(image._height) >> mip)) ||
                    !Multiply(level, slices) || !Multiply(level, image._layers) ||
                    !Multiply(level, image._faces) || !Multiply(level, image._components) ||
                    !Multiply(level, componentBytes) || level > KTX_IMAGE_BYTE_LIMIT - bytes)
                {
                    return Fail(error, "Oversized typed KTX payload");
                }
                bytes += level;
                expected += size_t(image._layers) * image._faces * slices;
            }
            // Covers input, working copy, libktx storage, encoded storage and output,
            // as well as subresource bookkeeping and metadata.
            if (bytes * 5u + U64(expected) * (sizeof(KTXSubresource) * 3u + 64u) + 4096u >= KTX_IMAGE_BYTE_LIMIT ||
                image._subresources.size() != expected)
            {
                return Fail(error, "Missing slices or excessive KTX working memory");
            }
            std::set<std::tuple<U32, U32, U32, U32>> unique;
            for (const KTXSubresource& resource : image._subresources)
            {
                if (resource._mip >= image._mips || resource._layer >= image._layers || resource._face >= image._faces ||
                    resource._depthSlice >= std::max(1u, U32(image._depth) >> resource._mip) ||
                    resource._width != std::max(1u, U32(image._width) >> resource._mip) ||
                    resource._height != std::max(1u, U32(image._height) >> resource._mip) ||
                    !unique.emplace(resource._mip, resource._layer, resource._face, resource._depthSlice).second)
                {
                    return Fail(error, "Invalid or duplicate typed KTX slice");
                }
                const size_t samples = size_t(resource._width) * resource._height * image._components;
                const bool valid = std::visit([&](const auto& pixels)
                {
                    using T = typename std::decay_t<decltype(pixels)>::value_type;
                    return pixels.size() == samples &&
                        ((std::is_same_v<T, U8> && type == 0u) ||
                         (std::is_same_v<T, U16> && (type == 1u || type == 2u)) ||
                         (std::is_same_v<T, F32> && type == 3u));
                }, resource._pixels);
                if (!valid) { return Fail(error, "Typed KTX slice payload mismatch"); }
            }
            return true;
        }

        double ReadSample(const KTXSubresource& resource, const size_t sample, const KTXComponentType type)
        {
            if (type == KTXComponentType::UNORM_8) { return double(std::get<std::vector<U8>>(resource._pixels)[sample]) / 255.0; }
            if (type == KTXComponentType::UNORM_16) { return double(std::get<std::vector<U16>>(resource._pixels)[sample]) / 65535.0; }
            if (type == KTXComponentType::FLOAT_16) { return glm::unpackHalf1x16(std::get<std::vector<U16>>(resource._pixels)[sample]); }
            return std::get<std::vector<F32>>(resource._pixels)[sample];
        }

        void WriteSample(KTXSubresource& resource, const size_t sample, const KTXComponentType type, const double value)
        {
            if (type == KTXComponentType::UNORM_8)
            {
                std::get<std::vector<U8>>(resource._pixels)[sample] = static_cast<U8>(std::round(std::clamp(value, 0.0, 1.0) * 255.0));
            }
            else if (type == KTXComponentType::UNORM_16)
            {
                std::get<std::vector<U16>>(resource._pixels)[sample] = static_cast<U16>(std::round(std::clamp(value, 0.0, 1.0) * 65535.0));
            }
            else if (type == KTXComponentType::FLOAT_16)
            {
                std::get<std::vector<U16>>(resource._pixels)[sample] = glm::packHalf1x16(static_cast<F32>(value));
            }
            else { std::get<std::vector<F32>>(resource._pixels)[sample] = static_cast<F32>(value); }
        }

        KTXAlphaInterpretation ResolvedAlpha(const KTXImage& image)
        {
            return image._alphaInterpretation == KTXAlphaInterpretation::UNKNOWN ?
                (image._components == 4u ? KTXAlphaInterpretation::OPACITY : KTXAlphaInterpretation::SYNTHETIC) :
                image._alphaInterpretation;
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
                      U64& decodedBytes, std::string* error, const size_t byteLimit)
        {
            constexpr U8 identifier[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
            if (input.size() < 80u || input.size() > byteLimit ||
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
            // libktx 4.4.2 reads the DFD immediately after the index, without
            // seeking to dfdOffset. Validate exactly the bytes it will consume.
            if (dfdOffset != indexEnd || dfdSize != 28u + 16u * format._components ||
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
                    resources > byteLimit - resourceBytes)
                {
                    return Fail(error, "KTX2 subresource count exceeds budget");
                }
                resourceBytes += resources;
                if (length != expected || ReadLE(input, index + 16, 8) != expected ||
                    offset % alignment != 0u || !addRange(offset, length) ||
                    paddedLength > byteLimit - input.size() - decodedBytes)
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
            if (metadataAllowance > byteLimit - input.size() ||
                decodedBytes > (byteLimit - input.size() - metadataAllowance) / 2u ||
                resourceBytes > byteLimit - input.size() - metadataAllowance - 2u * decodedBytes)
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
                            alpha = glm::unpackHalf1x16(pixels[component]);
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

    bool LoadKTXImage(const std::span<const Byte> input, KTXImage& output, std::string* error,
                      const size_t decodedByteLimit)
    {
        KTXImage image;
        Format format{};
        U64 decodedBytes = 0u;
        if (!Validate(input, image, format, decodedBytes, error, KTX_IMAGE_BYTE_LIMIT)) { return false; }
        if (decodedBytes > std::min(decodedByteLimit, KTX_IMAGE_BYTE_LIMIT))
        {
            return Fail(error, "KTX2 decoded payload exceeds requested budget");
        }
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

    bool CompressKTXImageForGPU(const KTXImage& input, const GFXImageFormat target,
                               KTXImage& output, std::string* error, const KTXGPUCacheOptions& cache, bool* cacheHit)
    {
        if (cacheHit != nullptr) { *cacheHit = false; }
        static std::mutex encoderMutex;
        const std::lock_guard lock(encoderMutex);
        // One process-local target entry, keyed by exact lossless pixels and
        // topology (not a collision-prone filename hash). Encoder settings are
        // fixed below and libktx is pinned; native disk masters stay lossless.
        static KTXImage cachedInput, cachedOutput;
        static KTXMipSemantic cachedSemantic = KTXMipSemantic::COLOR;
        static bool cachedLossyData = false;
        constexpr U64 cacheBudget = 16u * 1024u * 1024u;
        U32 format = 0u;
        U64 bytes = 0u;
        if (!ValidateTyped(input, format, bytes, error)) { return false; }
        if (input._componentType != KTXComponentType::UNORM_8 || input._dimensions != 2u ||
            !((target == GFXImageFormat::BC7 && input._components == 4u) ||
              (target == GFXImageFormat::BC4u && input._components == 1u && !input._srgb) ||
              (target == GFXImageFormat::BC5u && input._components == 2u && !input._srgb)))
        {
            return Fail(error, "Incompatible KTX GPU compression target");
        }
        // UASTC is encoded in independent, block-aligned 512x512 tiles, so
        // encoder scratch is bounded even for large mip chains/cube arrays.
        constexpr U32 tileExtent = 512u;
        constexpr U64 encoderBudget = 32u * 1024u * 1024u;
        const size_t blockBytes = target == GFXImageFormat::BC4u ? 8u : 16u;
        U64 texels = 0u;
        for (const auto& resource : input._subresources)
        {
            texels += U64((U32(resource._width) + 3u) & ~3u) * ((U32(resource._height) + 3u) & ~3u);
        }
        const U64 blockBytesTotal = texels / 16u * blockBytes;
        const U64 bookkeeping = U64(input._subresources.size()) * (sizeof(KTXSubresource) * 3u + 128u);
        if (bytes * 3u + blockBytesTotal * 2u + cacheBudget + encoderBudget + bookkeeping > KTX_IMAGE_BYTE_LIMIT)
        {
            return Fail(error, "KTX GPU encoder working budget exceeded");
        }
        bool cacheMatch = cache._memoryCache && cachedSemantic == cache._semantic && cachedLossyData == cache._allowLossyData &&
            cachedOutput._format == target && cachedOutput._compressed &&
            cachedInput._width == input._width && cachedInput._height == input._height &&
            cachedInput._layers == input._layers && cachedInput._faces == input._faces &&
            cachedInput._mips == input._mips && cachedInput._components == input._components &&
            cachedInput._isArray == input._isArray && cachedInput._srgb == input._srgb &&
            cachedInput._alphaInterpretation == input._alphaInterpretation &&
            cachedInput._subresources.size() == input._subresources.size();
        if (cacheMatch)
        {
            for (size_t i = 0u; i < input._subresources.size(); ++i)
            {
                const auto& a = input._subresources[i];
                const auto& b = cachedInput._subresources[i];
                if (a._mip != b._mip || a._layer != b._layer || a._face != b._face ||
                    a._width != b._width || a._height != b._height || a._pixels != b._pixels)
                {
                    cacheMatch = false;
                    break;
                }
            }
        }
        if (cacheMatch)
        {
            KTXImage result = cachedOutput;
            result._alphaInterpretation = input._alphaInterpretation;
            result._alpha = input._alpha;
            output = std::move(result);
            if (cacheHit != nullptr) { *cacheHit = true; }
            return true;
        }
        const auto remember = [&](const KTXImage& image)
        {
            if (cache._memoryCache &&
                bytes + blockBytesTotal + U64(input._subresources.size()) * sizeof(KTXSubresource) * 2u <= cacheBudget)
            {
                cachedInput = KTXImage{};
                cachedOutput = KTXImage{};
                cachedInput = input; cachedOutput = image;
                cachedSemantic = cache._semantic; cachedLossyData = cache._allowLossyData;
            }
        };
        std::vector<Byte> identity;
        std::filesystem::path cachePath;
        if (!cache._directory.empty())
        {
            identity = GPUIdentity(input, target, cache);
            cachePath = cache._directory / (std::string(GPU_CACHE_PREFIX) + std::to_string(GPUHash(identity)) + ".entry");
            KTXImage hit;
            if (ReadGPUEntry(cachePath, identity, input, target, blockBytesTotal, hit))
            {
                remember(hit);
                output = std::move(hit);
                if (cacheHit != nullptr) { *cacheHit = true; }
                return true;
            }
        }
        ktxBasisParams params{};
        params.structSize = sizeof(params);
        params.uastc = KTX_TRUE;
        params.threadCount = 1u;
        params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
        // libktx 4.4.2's UASTC BC5 transcoder reads R and A, whereas its
        // default two-component encoder places G in G. Explicitly preserve G
        // in alpha; this is channel packing, not normal-map renormalization.
        if (input._components == 2u) { std::memcpy(params.inputSwizzle, "rrrg", 4u); }
        // Optimize BC7 reconstruction rather than an intermediate that is
        // never sampled. Keep rate-distortion optimization disabled.
        params.uastcFlags = target == GFXImageFormat::BC7
            ? KTX_PACK_UASTC_LEVEL_SLOWER | KTX_PACK_UASTC_FAVOR_BC7_ERROR
            : KTX_PACK_UASTC_LEVEL_DEFAULT;
        const auto transcode = target == GFXImageFormat::BC7 ? KTX_TTF_BC7_RGBA
                             : target == GFXImageFormat::BC4u ? KTX_TTF_BC4_R : KTX_TTF_BC5_RG;
        KTXImage result = GPUImageHeader(input, target);
        for (const auto& resource : input._subresources)
        {
            KTXSubresource compressed;
            compressed._mip = resource._mip; compressed._layer = resource._layer; compressed._face = resource._face;
            compressed._width = resource._width; compressed._height = resource._height;
            const size_t blockWidth = (resource._width + 3u) / 4u;
            compressed._pixels = std::vector<U8>(blockWidth * ((resource._height + 3u) / 4u) * blockBytes);
            auto& blocks = std::get<std::vector<U8>>(compressed._pixels);
            const auto& pixels = std::get<std::vector<U8>>(resource._pixels);
            for (U32 y = 0u; y < resource._height; y += tileExtent)
            {
                for (U32 x = 0u; x < resource._width; x += tileExtent)
                {
                    const U32 width = std::min(tileExtent, U32(resource._width) - x);
                    const U32 height = std::min(tileExtent, U32(resource._height) - y);
                    std::vector<U8> tile(size_t(width) * height * input._components);
                    for (U32 row = 0u; row < height; ++row)
                    {
                        std::memcpy(tile.data() + size_t(row) * width * input._components,
                            pixels.data() + (size_t(y + row) * resource._width + x) * input._components,
                            size_t(width) * input._components);
                    }
                    ktxTextureCreateInfo info{};
                    info.vkFormat = format;
                    info.baseWidth = width; info.baseHeight = height; info.baseDepth = 1u;
                    info.numDimensions = 2u; info.numLevels = info.numLayers = info.numFaces = 1u;
                    ktxTexture2* raw = nullptr;
                    const auto status = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
                    const TextureOwner texture(raw);
                    if (status != KTX_SUCCESS || !texture) { return Fail(error, "libktx GPU tile allocation failed"); }
                    if (ktxTexture_SetImageFromMemory(ktxTexture(raw), 0u, 0u, 0u, tile.data(), tile.size()) != KTX_SUCCESS)
                    {
                        return Fail(error, "libktx GPU tile upload failed");
                    }
                    if (ktxTexture2_CompressBasisEx(raw, &params) != KTX_SUCCESS)
                    {
                        return Fail(error, "libktx UASTC tile encoding failed");
                    }
                    if (ktxTexture2_TranscodeBasis(raw, transcode, 0u) != KTX_SUCCESS)
                    {
                        return Fail(error, "libktx native BC tile transcoding failed");
                    }
                    ktx_size_t offset = 0u;
                    const size_t rowBytes = size_t((width + 3u) / 4u) * blockBytes;
                    const U32 rows = (height + 3u) / 4u;
                    if (ktxTexture_GetImageOffset(ktxTexture(raw), 0u, 0u, 0u, &offset) != KTX_SUCCESS ||
                        offset > raw->dataSize || rowBytes * rows > raw->dataSize - offset)
                    {
                        return Fail(error, "Invalid libktx native BC tile");
                    }
                    for (U32 row = 0u; row < rows; ++row)
                    {
                        std::memcpy(blocks.data() + ((size_t(y / 4u) + row) * blockWidth + x / 4u) * blockBytes,
                            raw->pData + offset + row * rowBytes, rowBytes);
                    }
                }
            }
            result._subresources.push_back(std::move(compressed));
        }
        if (!cachePath.empty())
        {
            try { PublishGPUEntry(cachePath, identity, result, blockBytesTotal, cache._quotaBytes); }
            catch (const std::exception&) { /* Disk failure never invalidates encoded GPU data. */ }
        }
        remember(result);
        output = std::move(result);
        return true;
    }

    bool EncodeKTXImage(const KTXImage& input, std::vector<Byte>& output, std::string* error)
    {
        U32 format = 0u;
        U64 bytes = 0u;
        if (!ValidateTyped(input, format, bytes, error) ||
            bytes * 5u + output.size() + U64(input._subresources.size()) *
                (sizeof(KTXSubresource) * 3u + 64u) + 4096u >= KTX_IMAGE_BYTE_LIMIT)
        {
            return Fail(error, "Invalid typed KTX input or excessive output memory");
        }
        KTXImage classified = input;
        if (!ClassifyKTXImageAlpha(classified, ResolvedAlpha(input)))
        {
            return Fail(error, "Invalid typed KTX alpha interpretation");
        }
        ktxTextureCreateInfo info{};
        info.vkFormat = format;
        info.baseWidth = input._width; info.baseHeight = input._height; info.baseDepth = input._depth;
        info.numDimensions = input._dimensions; info.numLevels = input._mips;
        info.numLayers = input._layers; info.numFaces = input._faces;
        info.isArray = input._isArray ? KTX_TRUE : KTX_FALSE;
        ktxTexture2* raw = nullptr;
        const KTX_error_code status = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw);
        const TextureOwner texture(raw);
        if (status != KTX_SUCCESS || !texture) { return Fail(error, "libktx allocation failed"); }
        const auto addMetadata = [&](const char* key, const std::string& value)
        {
            return ktxHashList_AddKVPair(&raw->kvDataHead, key, static_cast<unsigned int>(value.size() + 1u),
                                         value.c_str()) == KTX_SUCCESS;
        };
        std::string metadata = "v=1;threshold=4;transparent=0-1;translucent=2-250;opaque=251-255;";
        const auto interpretation = classified._alphaInterpretation;
        if (interpretation == KTXAlphaInterpretation::OPACITY)
        {
            metadata += "interpretation=opacity;skip=0;transparent=" + std::to_string(classified._alpha._transparent) +
                ";translucent=" + std::to_string(classified._alpha._translucent) +
                ";allzero=" + std::to_string(classified._alpha._allZero);
        }
        else
        {
            metadata += interpretation == KTXAlphaInterpretation::DATA ? "interpretation=data;" : "interpretation=synthetic;";
            metadata += "skip=1;transparent=0;translucent=0;allzero=0";
        }
        if (!addMetadata("KTXorientation", input._dimensions == 1u ? "r" : input._dimensions == 3u ? "rui" : "ru") ||
            !addMetadata("DivideAlpha", metadata))
        {
            return Fail(error, "libktx metadata failed");
        }
        for (const KTXSubresource& resource : input._subresources)
        {
            const bool written = std::visit([&](const auto& pixels)
            {
                return ktxTexture_SetImageFromMemory(ktxTexture(raw), resource._mip, resource._layer,
                    input._dimensions == 3u ? resource._depthSlice : resource._face,
                    reinterpret_cast<const ktx_uint8_t*>(pixels.data()), pixels.size() * sizeof(pixels.front())) == KTX_SUCCESS;
            }, resource._pixels);
            if (!written) { return Fail(error, "libktx slice upload failed"); }
        }
        ktx_uint8_t* encoded = nullptr;
        ktx_size_t encodedSize = 0u;
        const KTX_error_code written = ktxTexture2_WriteToMemory(raw, &encoded, &encodedSize);
        const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> encodedOwner(encoded, &std::free);
        if (written != KTX_SUCCESS || encoded == nullptr ||
            bytes * 3u + output.size() + U64(encodedSize) * 2u +
                U64(input._subresources.size()) * sizeof(KTXSubresource) * 2u >= KTX_IMAGE_BYTE_LIMIT)
        {
            return Fail(error, "libktx encoding failed or exceeded memory bound");
        }
        std::vector<Byte> result(reinterpret_cast<const Byte*>(encoded), reinterpret_cast<const Byte*>(encoded) + encodedSize);
        output = std::move(result);
        if (error != nullptr) { error->clear(); }
        return true;
    }

    bool GenerateKTXMipmaps(KTXImage& image, const KTXMipOptions& options, std::string* error)
    {
        U32 format = 0u;
        U64 bytes = 0u;
        if (!ValidateTyped(image, format, bytes, error)) { return false; }
        const auto interpretation = options._semantic == KTXMipSemantic::DATA ?
            KTXAlphaInterpretation::DATA : ResolvedAlpha(image);
        const bool preserveCoverage = options._preserveAlphaCoverage &&
            interpretation == KTXAlphaInterpretation::OPACITY;
        if ((options._semantic != KTXMipSemantic::COLOR && options._semantic != KTXMipSemantic::NORMAL &&
             options._semantic != KTXMipSemantic::DATA) ||
            (options._semantic == KTXMipSemantic::NORMAL && (image._srgb || image._components != 4u)) ||
            (options._semantic == KTXMipSemantic::DATA && image._srgb) ||
            (options._preserveAlphaCoverage && (options._semantic == KTXMipSemantic::DATA ||
             interpretation == KTXAlphaInterpretation::DATA)) ||
            (preserveCoverage && image._components != 4u))
        {
            return Fail(error, "Unsupported KTX mip semantics");
        }
        for (const auto& resource : image._subresources)
        {
            const size_t samples = size_t(resource._width) * resource._height * image._components;
            for (size_t sample = 0u; sample < samples; ++sample)
            {
                const double value = ReadSample(resource, sample, image._componentType);
                if (!std::isfinite(value) || (options._semantic == KTXMipSemantic::NORMAL &&
                    sample % image._components < 3u && (value < 0.0 || value > 1.0)) ||
                    (preserveCoverage && sample % 4u == 3u && (value < 0.0 || value > 1.0)))
                {
                    return Fail(error, "Nonfinite or invalid semantic KTX sample");
                }
            }
        }
        const U8 fullMips = FullMipCount(image);
        U64 projected = 0u;
        size_t resourceCount = 0u;
        for (U32 mip = 0u; mip < fullMips; ++mip)
        {
            const U64 slices = U64(image._layers) * image._faces * std::max(1u, U32(image._depth) >> mip);
            projected += slices * std::max(1u, U32(image._width) >> mip) *
                std::max(1u, U32(image._height) >> mip) * image._components *
                (image._componentType == KTXComponentType::UNORM_8 ? 1u : image._componentType == KTXComponentType::FLOAT_32 ? 4u : 2u);
            resourceCount += static_cast<size_t>(slices);
        }
        if (bytes + projected * 4u + U64(resourceCount) * (sizeof(KTXSubresource) * 3u + 64u) + 4096u >= KTX_IMAGE_BYTE_LIMIT)
        {
            return Fail(error, "KTX mip output exceeds working memory bound");
        }
        KTXImage result = image;
        if (!ClassifyKTXImageAlpha(result, interpretation)) { return Fail(error, "Invalid KTX base alpha"); }
        if (image._mips > 1u && !options._regenerate)
        {
            image = std::move(result);
            if (error != nullptr) { error->clear(); }
            return true;
        }
        std::erase_if(result._subresources, [](const KTXSubresource& resource) { return resource._mip != 0u; });
        std::sort(result._subresources.begin(), result._subresources.end(), [](const auto& a, const auto& b)
        {
            return std::tie(a._layer, a._face, a._depthSlice) < std::tie(b._layer, b._face, b._depthSlice);
        });
        result._subresources.reserve(resourceCount);
        size_t previousStart = 0u;
        const auto linear = [](const double value)
        {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        const auto srgb = [](const double value)
        {
            return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        };
        for (U32 mip = 1u; mip < fullMips; ++mip)
        {
            const U32 sw = std::max(1u, U32(image._width) >> (mip - 1u));
            const U32 sh = std::max(1u, U32(image._height) >> (mip - 1u));
            const U32 sd = std::max(1u, U32(image._depth) >> (mip - 1u));
            const U32 dw = std::max(1u, U32(image._width) >> mip);
            const U32 dh = std::max(1u, U32(image._height) >> mip);
            const U32 dd = std::max(1u, U32(image._depth) >> mip);
            const size_t levelStart = result._subresources.size();
            for (U32 layer = 0u; layer < image._layers; ++layer)
            {
                for (U32 face = 0u; face < image._faces; ++face)
                {
                    const size_t groupStart = result._subresources.size();
                    for (U32 z = 0u; z < dd; ++z)
                    {
                        KTXSubresource resource;
                        resource._mip = mip; resource._layer = layer; resource._face = face;
                        resource._depthSlice = z; resource._width = static_cast<U16>(dw); resource._height = static_cast<U16>(dh);
                        const size_t samples = size_t(dw) * dh * image._components;
                        if (image._componentType == KTXComponentType::UNORM_8) { resource._pixels = std::vector<U8>(samples); }
                        else if (image._componentType == KTXComponentType::FLOAT_32) { resource._pixels = std::vector<F32>(samples); }
                        else { resource._pixels = std::vector<U16>(samples); }
                        for (U32 y = 0u; y < dh; ++y)
                        {
                            for (U32 x = 0u; x < dw; ++x)
                            {
                                const double lo[] = {double(x) * sw / dw, double(y) * sh / dh, double(z) * sd / dd};
                                const double hi[] = {double(x + 1u) * sw / dw, double(y + 1u) * sh / dh, double(z + 1u) * sd / dd};
                                const double volume = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]);
                                std::array<double, 4> average{};
                                for (U32 iz = static_cast<U32>(lo[2]); iz < static_cast<U32>(std::ceil(hi[2])); ++iz)
                                {
                                    const auto& source = result._subresources[previousStart + (size_t(layer) * image._faces + face) * sd + iz];
                                    for (U32 iy = static_cast<U32>(lo[1]); iy < static_cast<U32>(std::ceil(hi[1])); ++iy)
                                    {
                                        for (U32 ix = static_cast<U32>(lo[0]); ix < static_cast<U32>(std::ceil(hi[0])); ++ix)
                                        {
                                            const double weight = (std::min(hi[0], double(ix + 1u)) - std::max(lo[0], double(ix))) *
                                                (std::min(hi[1], double(iy + 1u)) - std::max(lo[1], double(iy))) *
                                                (std::min(hi[2], double(iz + 1u)) - std::max(lo[2], double(iz))) / volume;
                                            for (U32 c = 0u; c < image._components; ++c)
                                            {
                                                double value = ReadSample(source, (size_t(iy) * sw + ix) * image._components + c, image._componentType);
                                                if (options._semantic == KTXMipSemantic::NORMAL && c < 3u) { value = value * 2.0 - 1.0; }
                                                else if (options._semantic == KTXMipSemantic::COLOR && image._srgb && c < 3u) { value = linear(value); }
                                                average[c] += value * weight;
                                            }
                                        }
                                    }
                                }
                                if (options._semantic == KTXMipSemantic::NORMAL)
                                {
                                    const double length = std::hypot(average[0], average[1], average[2]);
                                    for (U32 c = 0u; c < 3u; ++c)
                                    {
                                        average[c] = ((length > 0.0 ? average[c] / length : c == 2u ? 1.0 : 0.0) + 1.0) * 0.5;
                                    }
                                }
                                for (U32 c = 0u; c < image._components; ++c)
                                {
                                    double value = average[c];
                                    if (options._semantic == KTXMipSemantic::COLOR && image._srgb && c < 3u) { value = srgb(value); }
                                    // Convex HDR filtering must not overflow at representable maxima.
                                    if (image._componentType == KTXComponentType::FLOAT_16) { value = std::clamp(value, -65504.0, 65504.0); }
                                    if (image._componentType == KTXComponentType::FLOAT_32)
                                    {
                                        value = std::clamp(value, -double(std::numeric_limits<F32>::max()), double(std::numeric_limits<F32>::max()));
                                    }
                                    WriteSample(resource, (size_t(y) * dw + x) * image._components + c, image._componentType, value);
                                }
                            }
                        }
                        result._subresources.push_back(std::move(resource));
                    }
                    if (preserveCoverage)
                    {
                        size_t covered = 0u, baseCount = 0u;
                        for (U32 z = 0u; z < image._depth; ++z)
                        {
                            const auto& base = result._subresources[(size_t(layer) * image._faces + face) * image._depth + z];
                            for (size_t a = 3u; a < size_t(base._width) * base._height * 4u; a += 4u)
                            {
                                ++baseCount;
                                covered += ReadSample(base, a, image._componentType) >= double(Config::ALPHA_DISCARD_THRESHOLD);
                            }
                        }
                        std::vector<double> alpha;
                        for (size_t i = groupStart; i < result._subresources.size(); ++i)
                        {
                            const auto& resource = result._subresources[i];
                            for (size_t a = 3u; a < size_t(dw) * dh * 4u; a += 4u)
                            {
                                alpha.push_back(ReadSample(resource, a, image._componentType));
                            }
                        }
                        std::sort(alpha.begin(), alpha.end());
                        const size_t target = static_cast<size_t>(std::round(double(covered) / double(baseCount) * double(alpha.size())));
                        const size_t current = static_cast<size_t>(std::count_if(alpha.begin(), alpha.end(), [](const double value)
                        {
                            return value >= double(Config::ALPHA_DISCARD_THRESHOLD);
                        }));
                        double scale = current == target ? 1.0 : 0.0;
                        if (current != target && target > 0u && alpha[alpha.size() - target] > 0.0)
                        {
                            double threshold = Config::ALPHA_DISCARD_THRESHOLD;
                            if (image._componentType == KTXComponentType::UNORM_8) { threshold = std::ceil(threshold * 255.0) / 255.0; }
                            else if (image._componentType == KTXComponentType::UNORM_16) { threshold = std::ceil(threshold * 65535.0) / 65535.0; }
                            else if (image._componentType == KTXComponentType::FLOAT_16)
                            {
                                U16 bits = glm::packHalf1x16(static_cast<F32>(threshold));
                                if (glm::unpackHalf1x16(bits) < threshold) { ++bits; }
                                threshold = glm::unpackHalf1x16(bits);
                            }
                            scale = std::nextafter(threshold / alpha[alpha.size() - target], std::numeric_limits<double>::infinity());
                        }
                        for (size_t i = groupStart; i < result._subresources.size(); ++i)
                        {
                            auto& resource = result._subresources[i];
                            for (size_t a = 3u; a < size_t(dw) * dh * 4u; a += 4u)
                            {
                                WriteSample(resource, a, image._componentType, std::clamp(ReadSample(resource, a, image._componentType) * scale, 0.0, 1.0));
                            }
                        }
                    }
                }
            }
            previousStart = levelStart;
        }
        result._mips = fullMips;
        image = std::move(result);
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
