#include "UnitTests/unitTestCommon.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Utility/Headers/KTXImage.h"
#include "Utility/Headers/ImageTools.h"
#include "Utility/Headers/KTXImporter.h"
#include "Utility/Headers/KTXImageCache.h"

#include <ktx.h>
#include <glm/gtc/packing.hpp>
#include <turbojpeg.h>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace Divide
{
    TEST_CASE( "KTX2 independent alpha classification", "[ktx2]" )
    {
        using namespace ImageTools;
        std::vector<Byte> alpha{Byte{0}, Byte{1}, Byte{0}, Byte{1}, Byte{0},
                                Byte{2}, Byte{250}, Byte{2}, Byte{250}, Byte{2},
                                Byte{251}, Byte{255}, Byte{251}, Byte{255}, Byte{251}};
        const auto result = ClassifyKTXAlpha(alpha);
        CHECK(result._transparent);
        CHECK(result._translucent);
        CHECK(result._transparentCount == 5u);
        CHECK(result._translucentCount == 5u);
        CHECK(result._opaqueCount == 5u);
        CHECK_FALSE(result._allZero);
        CHECK(ClassifyKTXAlpha(alpha, true)._skipped);
        CHECK(ClassifyKTXAlpha(alpha, false, true)._skipped);
        CHECK_FALSE(ClassifyKTXAlpha(alpha, true)._transparent);
        std::vector<Byte> four(4u, Byte{1});
        CHECK_FALSE(ClassifyKTXAlpha(four)._transparent);
        four.assign(4u, Byte{250});
        CHECK_FALSE(ClassifyKTXAlpha(four)._translucent);
        four.assign(4u, Byte{251});
        CHECK_FALSE(ClassifyKTXAlpha(four)._transparent);
        CHECK_FALSE(ClassifyKTXAlpha(four)._translucent);
        four.assign(4u, Byte{0});
        CHECK(ClassifyKTXAlpha(four)._transparent);
        CHECK_FALSE(ClassifyKTXAlpha(four)._translucent);
        bool logged = false;
        const auto zero = ClassifyKTXAlpha(four, false, false,
            [&](std::string_view) { logged = true; });
        CHECK(logged);
        CHECK(zero._allZero);
        CHECK(zero._transparent);
        CHECK_FALSE(zero._translucent);
        CHECK_FALSE(ClassifyKTXAlpha({})._allZero);
    }

    TEST_CASE( "KTX2 screenshot round trip is lossless and bottom-left", "[ktx2]" )
    {
        using namespace ImageTools;
        const std::vector<Byte> rgb{Byte{1}, Byte{2}, Byte{3}, Byte{4}, Byte{5}, Byte{6},
                                   Byte{7}, Byte{8}, Byte{9}, Byte{10}, Byte{11}, Byte{12}};
        std::vector<Byte> encoded;
        REQUIRE(EncodeKTXImage(rgb, 2, 2, 3, 3, false, true, encoded));
        KTXImage image;
        std::string error;
        REQUIRE(LoadKTXImage(encoded, image, &error));
        CHECK(error.empty());
        CHECK(image._srgb);
        CHECK(image._native);
        CHECK_FALSE(image._compressed);
        CHECK(image._format == GFXImageFormat::RGBA);
        CHECK(image._dataType == GFXDataFormat::UNSIGNED_BYTE);
        CHECK(image._packing == GFXImagePacking::NORMALIZED_SRGB);
        CHECK(image._textureType == TextureType::TEXTURE_2D);
        CHECK(image._components == 4u);
        CHECK(image._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        CHECK(image._alpha._skipped);
        REQUIRE(image._subresources.size() == 1u);
        const auto& pixels = std::get<std::vector<U8>>(image._subresources.front()._pixels);
        CHECK(pixels == std::vector<U8>{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255});
        REQUIRE(EncodeKTXImage(rgb, 2, 2, 3, 3, true, false, encoded));
        REQUIRE(LoadKTXImage(encoded, image));
        CHECK(std::get<std::vector<U8>>(image._subresources.front()._pixels)[0] == 3u);
        CHECK_FALSE(EncodeKTXImage(rgb, 2, 2, 4, 4, false, false, encoded));
        CHECK_FALSE(EncodeKTXImage(rgb, 0, 2, 3, 3, false, false, encoded));
        CHECK_FALSE(EncodeKTXImage(rgb, 2, 2, 3, 4, false, false, encoded));

        const std::vector<Byte> rgba{Byte{20}, Byte{30}, Byte{40}, Byte{0}};
        REQUIRE(EncodeKTXImage(rgba, 1, 1, 4, 4, false, false, encoded));
        REQUIRE(LoadKTXImage(encoded, image));
        CHECK(image._alphaInterpretation == KTXAlphaInterpretation::OPACITY);
        CHECK(image._alpha._allZero);
        CHECK(image._alpha._transparent);
    }

    TEST_CASE( "KTX2 typed opacity classification aggregates only base subresources", "[ktx2]" )
    {
        using namespace ImageTools;
        KTXImage image;
        image._components = 4u;
        image._componentType = KTXComponentType::UNORM_16;
        const U16 values[] = {258u, 514u, 64507u};
        for (U32 category = 0u; category < 3u; ++category)
        {
            for (U32 i = 0u; i < 5u; ++i)
            {
                KTXSubresource resource;
                resource._width = 1u; resource._height = 1u;
                resource._layer = i; resource._face = category; resource._depthSlice = i;
                resource._pixels = std::vector<U16>{0u, 0u, 0u, values[category]};
                image._subresources.push_back(std::move(resource));
            }
        }
        KTXSubresource mip;
        mip._mip = 1u; mip._width = 1u; mip._height = 1u;
        mip._pixels = std::vector<U16>{0u, 0u, 0u, 0u};
        image._subresources.push_back(mip);
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK(image._alpha._transparentCount == 5u);
        CHECK(image._alpha._translucentCount == 5u);
        CHECK(image._alpha._opaqueCount == 5u);
        CHECK(image._alpha._transparent);
        CHECK(image._alpha._translucent);
        CHECK_FALSE(image._alpha._allZero);

        image._subresources.resize(1u);
        image._subresources[0]._pixels = std::vector<U16>{0u, 0u, 0u, 1u};
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK_FALSE(image._alpha._allZero);
        CHECK_FALSE(image._alpha._transparent);
        image._componentType = KTXComponentType::FLOAT_16;
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK_FALSE(image._alpha._allZero); // Smallest half subnormal is not exact zero.
        image._subresources[0]._pixels = std::vector<U16>{0u, 0u, 0u, 0u};
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK(image._alpha._allZero);
        CHECK(image._alpha._transparent);
        image._subresources[0]._pixels = std::vector<U16>{0u, 0u, 0u, 0x3800u}; // 0.5
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK(image._alpha._translucentCount == 1u);
        for (const U16 invalid : {U16{0x7C00u}, U16{0x7E00u}, U16{0xBC00u}, U16{0x4000u}})
        {
            image._subresources[0]._pixels = std::vector<U16>{0u, 0u, 0u, invalid};
            CHECK_FALSE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
            CHECK(image._alpha._translucentCount == 1u);
        }

        image._componentType = KTXComponentType::FLOAT_32;
        image._alphaInterpretation = KTXAlphaInterpretation::UNKNOWN;
        image._subresources[0]._pixels = std::vector<F32>{0.f, 0.f, 0.f, std::numeric_limits<F32>::denorm_min()};
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK_FALSE(image._alpha._allZero);
        CHECK_FALSE(image._alpha._transparent);
        image._subresources[0]._pixels = std::vector<F32>{0.f, 0.f, 0.f, 251.f / 255.f};
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        CHECK(image._alpha._opaqueCount == 1u);
        CHECK_FALSE(image._alpha._transparent);
        CHECK_FALSE(image._alpha._translucent);
        for (const F32 invalid : {std::numeric_limits<F32>::infinity(),
                                  std::numeric_limits<F32>::quiet_NaN(), -0.1f, 1.1f})
        {
            image._subresources[0]._pixels = std::vector<F32>{0.f, 0.f, 0.f, invalid};
            image._alphaInterpretation = KTXAlphaInterpretation::UNKNOWN;
            CHECK_FALSE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
            CHECK(image._alphaInterpretation == KTXAlphaInterpretation::UNKNOWN);
        }
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::DATA));
        CHECK(image._alpha._skipped);
        CHECK_FALSE(image._alpha._transparent);
        CHECK_FALSE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::UNKNOWN));
        image._components = 2u;
        CHECK_FALSE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::OPACITY));
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::SYNTHETIC));
        CHECK(image._alpha._skipped);
        image._components = 4u;
        CHECK_FALSE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::SYNTHETIC));
        image._subresources[0]._pixels = std::vector<F32>{0.f, 0.f, 0.f, 1.f};
        REQUIRE(ClassifyKTXImageAlpha(image, KTXAlphaInterpretation::SYNTHETIC));
    }

    TEST_CASE( "KTX2 typed native arrays cubes volumes and mips", "[ktx2]" )
    {
        using namespace ImageTools;
        const auto fixture = [](U32 format, U32 components, U32 bytes, U32 layers, U32 faces, U32 depth)
        {
            ktxTextureCreateInfo info{};
            info.vkFormat = format;
            info.baseWidth = 4; info.baseHeight = 4; info.baseDepth = depth;
            info.numDimensions = depth > 1u ? 3u : 2u;
            info.numLevels = 3; info.numLayers = layers; info.numFaces = faces;
            info.isArray = layers > 1u ? KTX_TRUE : KTX_FALSE;
            ktxTexture2* raw = nullptr;
            REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw) == KTX_SUCCESS);
            const std::unique_ptr<ktxTexture2, decltype(&ktxTexture2_Destroy)> texture(raw, &ktxTexture2_Destroy);
            const char* orientation = depth > 1u ? "rui" : "ru";
            REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "KTXorientation",
                static_cast<unsigned int>(std::strlen(orientation) + 1u), orientation) == KTX_SUCCESS);
            for (U32 mip = 0; mip < 3u; ++mip)
            {
                const size_t size = size_t(std::max(1u, 4u >> mip)) * std::max(1u, 4u >> mip) * components * bytes;
                for (U32 layer = 0; layer < layers; ++layer)
                {
                    for (U32 face = 0; face < (depth > 1u ? std::max(1u, depth >> mip) : faces); ++face)
                    {
                        const std::vector<ktx_uint8_t> pixels(size, static_cast<ktx_uint8_t>(1u + mip + layer + face));
                        REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(raw), mip, layer, face,
                            pixels.data(), pixels.size()) == KTX_SUCCESS);
                    }
                }
            }
            ktx_uint8_t* encoded = nullptr;
            ktx_size_t size = 0;
            REQUIRE(ktxTexture2_WriteToMemory(raw, &encoded, &size) == KTX_SUCCESS);
            const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> owner(encoded, &std::free);
            return std::vector<Byte>(reinterpret_cast<const Byte*>(encoded), reinterpret_cast<const Byte*>(encoded) + size);
        };
        struct NativeFormat { U32 _format; U32 _components; U32 _bytes; KTXComponentType _type; };
        const NativeFormat formats[] = {
            {9, 1, 1, KTXComponentType::UNORM_8}, {16, 2, 1, KTXComponentType::UNORM_8},
            {37, 4, 1, KTXComponentType::UNORM_8}, {70, 1, 2, KTXComponentType::UNORM_16},
            {77, 2, 2, KTXComponentType::UNORM_16}, {91, 4, 2, KTXComponentType::UNORM_16},
            {76, 1, 2, KTXComponentType::FLOAT_16}, {83, 2, 2, KTXComponentType::FLOAT_16},
            {97, 4, 2, KTXComponentType::FLOAT_16}, {100, 1, 4, KTXComponentType::FLOAT_32},
            {103, 2, 4, KTXComponentType::FLOAT_32}, {109, 4, 4, KTXComponentType::FLOAT_32}};
        for (const auto& format : formats)
        {
            INFO(format._format);
            KTXImage image;
            REQUIRE(LoadKTXImage(fixture(format._format, format._components, format._bytes, 2, 1, 1), image));
            CHECK(image._isArray);
            CHECK(image._layers == 2u);
            CHECK(image._componentType == format._type);
            CHECK(image._format == (format._components == 1u ? GFXImageFormat::RED :
                                    format._components == 2u ? GFXImageFormat::RG : GFXImageFormat::RGBA));
            CHECK(image._dataType == (format._type == KTXComponentType::UNORM_8 ? GFXDataFormat::UNSIGNED_BYTE :
                                      format._type == KTXComponentType::UNORM_16 ? GFXDataFormat::UNSIGNED_SHORT :
                                      format._type == KTXComponentType::FLOAT_16 ? GFXDataFormat::FLOAT_16 : GFXDataFormat::FLOAT_32));
            CHECK(image._packing == GFXImagePacking::NORMALIZED);
            CHECK(image._textureType == TextureType::TEXTURE_2D_ARRAY);
            CHECK(image._alphaInterpretation == KTXAlphaInterpretation::UNKNOWN);
            REQUIRE(image._subresources.size() == 6u);
            const auto& resource = image._subresources[1];
            CHECK(resource._mip == 0u);
            CHECK(resource._layer == 1u);
            std::visit([&](const auto& pixels)
            {
                CHECK(pixels.size() * sizeof(pixels.front()) == 16u * format._components * format._bytes);
                const auto* bytes = reinterpret_cast<const U8*>(pixels.data());
                CHECK(bytes[0] == 2u);
            }, resource._pixels);
        }
        KTXImage cube;
        REQUIRE(LoadKTXImage(fixture(37, 4, 1, 2, 6, 1), cube));
        CHECK(cube._faces == 6u);
        CHECK(cube._textureType == TextureType::TEXTURE_CUBE_ARRAY);
        CHECK(cube._subresources.size() == 36u);
        CHECK(cube._subresources[11]._face == 5u);
        CHECK(cube._subresources[11]._layer == 1u);
        KTXImage volume;
        REQUIRE(LoadKTXImage(fixture(70, 1, 2, 1, 1, 4), volume));
        CHECK(volume._dimensions == 3u);
        CHECK(volume._textureType == TextureType::TEXTURE_3D);
        CHECK(volume._subresources.size() == 7u);
        CHECK(volume._subresources[3]._depthSlice == 3u);
        CHECK(volume._subresources[4]._mip == 1u);
    }

    TEST_CASE( "KTX source PNG preserves precision orientation and genuine gray alpha", "[ktx2]" )
    {
        using namespace ImageTools;
        constexpr U8 rgb8[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0x16, 0xE3, 0x21,
            0x70, 0x00, 0x00, 0x00, 0x10, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x60, 0x64, 0x62, 0x66,
            0x60, 0x61, 0x65, 0x03, 0x00, 0x00, 0x46, 0x00, 0x16, 0x77, 0xF5, 0xC8, 0x7F, 0x00, 0x00, 0x00,
            0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        constexpr U8 gray16[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x81, 0xD9, 0xFC,
            0x15, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x60, 0x64, 0x5A, 0x7D,
            0x16, 0x00, 0x02, 0x32, 0x01, 0x7C, 0x83, 0x13, 0x40, 0x2F, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
            0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        constexpr U8 grayAlpha[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0x5E, 0x2B, 0xB7,
            0x01, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x50, 0x60, 0x68, 0xF8,
            0x0F, 0x00, 0x02, 0x84, 0x01, 0xA0, 0xC1, 0xA5, 0x4D, 0x40, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
            0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        KTXImage image;
        REQUIRE(DecodeKTXSource(std::as_bytes(std::span(rgb8)), KTXSourceFormat::PNG, true,
                               KTXAlphaInterpretation::UNKNOWN, image));
        CHECK(image._srgb);
        CHECK(image._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        CHECK(std::get<std::vector<U8>>(image._subresources[0]._pixels) == std::vector<U8>{4, 5, 6, 255, 1, 2, 3, 255});
        REQUIRE(DecodeKTXSource(std::as_bytes(std::span(gray16)), KTXSourceFormat::PNG, false,
                               KTXAlphaInterpretation::DATA, image));
        CHECK(image._componentType == KTXComponentType::UNORM_16);
        CHECK(image._components == 1u);
        CHECK(std::get<std::vector<U16>>(image._subresources[0]._pixels) == std::vector<U16>{0x0102u, 0xABCDu});
        std::vector<Byte> encoded;
        REQUIRE(EncodeKTXImage(image, encoded));
        KTXImage roundtrip;
        REQUIRE(LoadKTXImage(encoded, roundtrip));
        CHECK(roundtrip._subresources[0]._pixels == image._subresources[0]._pixels);
        REQUIRE(DecodeKTXSource(std::as_bytes(std::span(grayAlpha)), KTXSourceFormat::PNG, false,
                               KTXAlphaInterpretation::OPACITY, image));
        CHECK(image._components == 4u);
        CHECK(image._alphaInterpretation == KTXAlphaInterpretation::OPACITY);
        CHECK(std::get<std::vector<U8>>(image._subresources[0]._pixels) == std::vector<U8>{32, 32, 32, 0, 128, 128, 128, 255});
        REQUIRE(DecodeKTXSource(std::as_bytes(std::span(grayAlpha)), KTXSourceFormat::PNG, false,
                               KTXAlphaInterpretation::DATA, image));
        CHECK(image._components == 2u);
        CHECK(std::get<std::vector<U8>>(image._subresources[0]._pixels) == std::vector<U8>{32, 0, 128, 255});
        const auto original = image._subresources[0]._pixels;
        CHECK_FALSE(DecodeKTXSource(std::as_bytes(std::span(grayAlpha)), KTXSourceFormat::PNG, false,
                                   KTXAlphaInterpretation::UNKNOWN, image));
        CHECK(image._subresources[0]._pixels == original);
        CHECK_FALSE(DecodeKTXSource(std::as_bytes(std::span(gray16)), KTXSourceFormat::PNG, true,
                                   KTXAlphaInterpretation::DATA, image));
        CHECK(image._subresources[0]._pixels == original);
        CHECK_FALSE(DecodeKTXSource(std::as_bytes(std::span(rgb8)), KTXSourceFormat::PNG, false,
                                   KTXAlphaInterpretation::UNKNOWN, image, nullptr, 1u));
        CHECK(image._subresources[0]._pixels == original);
        const std::string filename = "ktx-source-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png";
        struct SourceCleanup
        {
            std::string _filename;
            std::string _cacheDirectory;
            ~SourceCleanup()
            {
                std::error_code ignored;
                std::filesystem::remove(_filename, ignored);
                std::filesystem::remove_all(_cacheDirectory, ignored);
            }
        } cleanup{filename, filename + ".cache"};
        {
            std::ofstream file(filename, std::ios::binary);
            REQUIRE(file.is_open());
            file.write(reinterpret_cast<const char*>(rgb8), sizeof(rgb8));
            REQUIRE(file.good());
        }
        KTXSourceSettings settings;
        settings._diskCache = false;
        settings._srgb = true;
        KTXImage cached;
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, ResourcePath{}, settings, cached));
        CHECK(cached._mips == 2u);
        CHECK(cached._srgb);
        CHECK(cached._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        CHECK(std::get<std::vector<U8>>(cached._subresources[0]._pixels) == std::vector<U8>{4, 5, 6, 255, 1, 2, 3, 255});
        settings._preserveAlphaCoverage = true;
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, ResourcePath{}, settings, cached));
        CHECK(cached._mips == 2u);
        CHECK(cached._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        CHECK(std::get<std::vector<U8>>(cached._subresources[1]._pixels)[3] == 255u);
        settings._generateMipmaps = false;
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, ResourcePath{}, settings, cached));
        CHECK(cached._mips == 1u);
        const auto preserved = cached._subresources[0]._pixels;
        std::stop_source stop;
        stop.request_stop();
        settings._stopToken = stop.get_token();
        CHECK_FALSE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, ResourcePath{}, settings, cached));
        CHECK(cached._mips == 1u);
        CHECK(cached._subresources[0]._pixels == preserved);
        settings._stopToken = {};
        settings._diskCache = true;
        settings._generateMipmaps = true;
        const ResourcePath cacheDirectory{cleanup._cacheDirectory.c_str()};
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, cacheDirectory, settings, cached));
        const auto cold = cached._subresources;
        const auto entries = [&]()
        {
            std::vector<std::filesystem::path> result;
            for (const auto& item : std::filesystem::directory_iterator(cleanup._cacheDirectory))
            {
                if (item.path().extension() == ".entry") { result.push_back(item.path()); }
            }
            return result;
        };
        REQUIRE(entries().size() == 1u);
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, cacheDirectory, settings, cached));
        REQUIRE(cached._subresources.size() == cold.size());
        CHECK(cached._subresources[0]._pixels == cold[0]._pixels);
        CHECK(cached._subresources[1]._pixels == cold[1]._pixels);
        const auto entry = entries()[0];
        {
            std::ofstream corrupt(entry, std::ios::binary | std::ios::trunc);
            corrupt << "invalid";
        }
        settings._diskCache = false;
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, cacheDirectory, settings, cached));
        CHECK(std::filesystem::file_size(entry) == 7u);
        settings._diskCache = true;
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, cacheDirectory, settings, cached));
        CHECK(std::filesystem::file_size(entry) > 7u);
        settings._generateMipmaps = false;
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, cacheDirectory, settings, cached));
        CHECK(cached._mips == 1u);
        CHECK(entries().size() == 2u);
        const auto unrelated = std::filesystem::path(cleanup._cacheDirectory) / "unrelated.entry";
        { std::ofstream keep(unrelated); keep << "preserve"; }
        REQUIRE(InvalidateKTXSourceCache(cacheDirectory));
        CHECK(std::filesystem::exists(unrelated));
        CHECK(entries().size() == 1u);
        // A regular source file cannot be a cache directory; authoring remains
        // successful because disk failures are nonfatal.
        REQUIRE(LoadCachedKTXSource(ResourcePath{filename.c_str()}, ResourcePath{filename.c_str()}, settings, cached));
        CHECK(cached._mips == 1u);
    }

    TEST_CASE( "KTX source JPEG decodes RGB without adding lossy authoring conversion", "[ktx2]" )
    {
        using namespace ImageTools;
        const std::unique_ptr<void, decltype(&tj3Destroy)> encoder(tj3Init(TJINIT_COMPRESS), &tj3Destroy);
        REQUIRE(encoder != nullptr);
        REQUIRE(tj3Set(encoder.get(), TJPARAM_QUALITY, 100) == 0);
        REQUIRE(tj3Set(encoder.get(), TJPARAM_SUBSAMP, TJSAMP_444) == 0);
        const U8 pixels[] = {240, 20, 20, 240, 20, 20, 20, 20, 240, 20, 20, 240};
        unsigned char* jpeg = nullptr;
        size_t jpegSize = 0u;
        REQUIRE(tj3Compress8(encoder.get(), pixels, 2, 0, 2, TJPF_RGB, &jpeg, &jpegSize) == 0);
        const std::unique_ptr<unsigned char, decltype(&tj3Free)> owner(jpeg, &tj3Free);
        KTXImage image;
        REQUIRE(DecodeKTXSource({reinterpret_cast<const Byte*>(jpeg), jpegSize}, KTXSourceFormat::JPEG,
                               false, KTXAlphaInterpretation::UNKNOWN, image));
        CHECK(image._components == 4u);
        CHECK(image._componentType == KTXComponentType::UNORM_8);
        CHECK(image._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        const auto& decoded = std::get<std::vector<U8>>(image._subresources[0]._pixels);
        CHECK(decoded[2] > decoded[0]);
        CHECK(decoded[8] > decoded[10]);
        CHECK(decoded[3] == 255u);
        std::vector<Byte> encoded;
        REQUIRE(EncodeKTXImage(image, encoded));
        KTXImage roundtrip;
        REQUIRE(LoadKTXImage(encoded, roundtrip));
        CHECK(roundtrip._subresources[0]._pixels == image._subresources[0]._pixels);
    }

    TEST_CASE( "KTX2 typed authoring preserves all samples and topology", "[ktx2]" )
    {
        using namespace ImageTools;
        for (const auto type : {KTXComponentType::UNORM_8, KTXComponentType::UNORM_16,
                                KTXComponentType::FLOAT_16, KTXComponentType::FLOAT_32})
        {
            for (const U8 components : {U8{1}, U8{2}, U8{4}})
            {
                for (const U8 dimensions : {U8{1}, U8{2}, U8{3}})
                {
                    for (const bool cube : {false, true})
                    {
                        if (cube && dimensions != 2u) { continue; }
                        KTXImage input;
                        input._width = 4u; input._height = dimensions == 1u ? 1u : 4u;
                        input._depth = dimensions == 3u ? 3u : 1u;
                        input._dimensions = dimensions; input._faces = cube ? 6u : 1u;
                        input._layers = dimensions == 3u ? 1u : 2u;
                        input._isArray = dimensions != 3u;
                        input._components = components; input._componentType = type; input._mips = 3u;
                        input._alphaInterpretation = KTXAlphaInterpretation::DATA;
                        for (U32 mip = 0u; mip < input._mips; ++mip)
                        {
                            for (U32 layer = 0u; layer < input._layers; ++layer)
                            {
                                for (U32 face = 0u; face < input._faces; ++face)
                                {
                                    for (U32 slice = 0u; slice < std::max(1u, U32(input._depth) >> mip); ++slice)
                                    {
                                        KTXSubresource resource;
                                        resource._mip = mip; resource._layer = layer; resource._face = face; resource._depthSlice = slice;
                                        resource._width = static_cast<U16>(std::max(1u, 4u >> mip));
                                        resource._height = static_cast<U16>(std::max(1u, U32(input._height) >> mip));
                                        const size_t samples = size_t(resource._width) * resource._height * components;
                                        const U32 seed = mip * 13u + layer * 7u + face * 3u + slice;
                                        if (type == KTXComponentType::UNORM_8)
                                        {
                                            std::vector<U8> pixels(samples);
                                            for (size_t i = 0u; i < samples; ++i) { pixels[i] = static_cast<U8>(seed + i); }
                                            resource._pixels = std::move(pixels);
                                        }
                                        else if (type == KTXComponentType::FLOAT_32)
                                        {
                                            std::vector<F32> pixels(samples);
                                            for (size_t i = 0u; i < samples; ++i) { pixels[i] = F32(seed) + F32(i) * 0.03125f + 8192.f; }
                                            resource._pixels = std::move(pixels);
                                        }
                                        else
                                        {
                                            std::vector<U16> pixels(samples);
                                            for (size_t i = 0u; i < samples; ++i)
                                            {
                                                pixels[i] = type == KTXComponentType::FLOAT_16 ?
                                                    glm::packHalf1x16(F32(seed) + F32(i) * 0.125f) : static_cast<U16>(seed + i * 257u);
                                            }
                                            resource._pixels = std::move(pixels);
                                        }
                                        input._subresources.push_back(std::move(resource));
                                    }
                                }
                            }
                        }
                        std::vector<Byte> encoded;
                        REQUIRE(EncodeKTXImage(input, encoded));
                        KTXImage decoded;
                        REQUIRE(LoadKTXImage(encoded, decoded));
                        CHECK(decoded._dimensions == dimensions);
                        CHECK(decoded._layers == input._layers);
                        CHECK(decoded._faces == input._faces);
                        CHECK(decoded._mips == 3u);
                        CHECK(decoded._componentType == type);
                        CHECK(decoded._alphaInterpretation == KTXAlphaInterpretation::DATA);
                        REQUIRE(decoded._subresources.size() == input._subresources.size());
                        for (size_t i = 0u; i < input._subresources.size(); ++i)
                        {
                            CHECK(decoded._subresources[i]._pixels == input._subresources[i]._pixels);
                            CHECK(decoded._subresources[i]._depthSlice == input._subresources[i]._depthSlice);
                        }
                    }
                }
            }
        }
    }

    TEST_CASE( "ImageData KTX memory helper preserves typed topology and fails transactionally", "[ktx2]" )
    {
        using namespace ImageTools;
        for (const bool volume : {false, true})
        {
            KTXImage source;
            source._width = 2u; source._height = 2u; source._depth = volume ? 2u : 1u;
            source._dimensions = volume ? 3u : 2u; source._faces = volume ? 1u : 6u;
            source._layers = volume ? 1u : 2u; source._isArray = !volume;
            source._components = 2u; source._componentType = KTXComponentType::FLOAT_16;
            source._alphaInterpretation = KTXAlphaInterpretation::DATA; source._mips = 2u;
            for (U32 mip = 0u; mip < 2u; ++mip)
            {
                for (U32 layer = 0u; layer < source._layers; ++layer)
                {
                    for (U32 face = 0u; face < source._faces; ++face)
                    {
                        for (U32 slice = 0u; slice < std::max(1u, U32(source._depth) >> mip); ++slice)
                        {
                            const U16 size = static_cast<U16>(2u >> mip);
                            source._subresources.push_back({mip, layer, face, slice, size, size,
                                std::vector<U16>(size_t(size) * size * 2u, glm::packHalf1x16(F32(1u + layer + face + slice + mip)))});
                        }
                    }
                }
            }
            std::vector<Byte> encoded;
            REQUIRE(EncodeKTXImage(source, encoded));
            ImageData image;
            REQUIRE(image.loadFromKTXMemory(encoded));
            CHECK(image.isKTXImage());
            CHECK(image.ktxLayerCount() == source._layers);
            CHECK(image.layerCount() == (volume ? 1u : 12u));
            CHECK(image.mipCount() == 2u);
            CHECK(image.dataType() == GFXDataFormat::FLOAT_16);
            CHECK(image.format() == GFXImageFormat::RG);
            CHECK(image.ktxTextureType() == (volume ? TextureType::TEXTURE_3D : TextureType::TEXTURE_CUBE_ARRAY));
            CHECK(image.dimensions(0u, 0u).z == source._depth);
            CHECK(image.dimensions(0u, 1u).z == 1u);
            CHECK(image.imageLayers()[0].getMip(0u)->_size == size_t(2u * 2u * source._depth * 2u * sizeof(U16)));
            CHECK(image.imageLayers()[0].getMip(1u)->_size == 2u * sizeof(U16));
            const auto* bytes = static_cast<const U8*>(image.data(0u, 0u));
            REQUIRE(bytes != nullptr);
            U16 first = 0u;
            std::memcpy(&first, bytes, sizeof(first));
            CHECK(first == glm::packHalf1x16(1.f));
            if (volume)
            {
                std::memcpy(&first, bytes + 2u * 2u * 2u * sizeof(U16), sizeof(first));
                CHECK(first == glm::packHalf1x16(2.f));
            }
            CHECK_FALSE(image.loadFromKTXMemory({}));
            CHECK(image.isKTXImage());
            CHECK(image.mipCount() == 2u);
            CHECK(image.data(0u, 0u) == bytes);
            const auto originalPixels = source._subresources[0]._pixels;
            source._textureType = TextureType::TEXTURE_1D;
            ImageData direct;
            REQUIRE(direct.loadFromKTXImage(source));
            CHECK(direct.ktxTextureType() == (volume ? TextureType::TEXTURE_3D : TextureType::TEXTURE_CUBE_ARRAY));
            CHECK(source._subresources[0]._pixels == originalPixels);
            CHECK(direct.dataType() == GFXDataFormat::FLOAT_16);
            CHECK_FALSE(direct.loadFromKTXImage(source));
            CHECK(direct.dataType() == GFXDataFormat::FLOAT_16);
            CHECK(direct.mipCount() == 2u);
            KTXImage invalid = source;
            invalid._subresources[0]._width = 1u;
            ImageData rejected;
            CHECK_FALSE(rejected.loadFromKTXImage(invalid));
            CHECK_FALSE(rejected.isKTXImage());
            CHECK(rejected.layerCount() == 0u);
            CHECK(rejected.dataType() == GFXDataFormat::COUNT);
        }
    }

    TEST_CASE( "KTX2 authoring validates slices and recomputes base alpha", "[ktx2]" )
    {
        using namespace ImageTools;
        KTXImage input;
        input._width = 3u; input._height = 2u; input._components = 4u;
        input._subresources.push_back({0u, 0u, 0u, 0u, 3u, 2u, std::vector<U8>(24u, 0u)});
        input._alpha._transparent = false;
        std::vector<Byte> encoded;
        REQUIRE(EncodeKTXImage(input, encoded));
        KTXImage decoded;
        REQUIRE(LoadKTXImage(encoded, decoded));
        CHECK(decoded._alpha._allZero);
        CHECK(decoded._alpha._transparent);
        const auto original = encoded;
        const auto reject = [&](const KTXImage& invalid)
        {
            CHECK_FALSE(EncodeKTXImage(invalid, encoded));
            CHECK(encoded == original);
        };
        auto invalid = input;
        invalid._alphaInterpretation = KTXAlphaInterpretation::SYNTHETIC;
        reject(invalid);
        invalid = input; invalid._subresources.push_back(invalid._subresources.front()); reject(invalid);
        invalid = input; invalid._subresources.front()._depthSlice = 1u; reject(invalid);
        invalid = input; invalid._subresources.front()._width = 2u; reject(invalid);
        invalid = input; invalid._subresources.front()._pixels = std::vector<U16>(24u); reject(invalid);
        invalid = input; invalid._subresources.front()._pixels = std::vector<U8>(23u); reject(invalid);
        invalid = input; invalid._mips = 3u; reject(invalid);
        invalid = input; invalid._width = invalid._height = 65535u; reject(invalid);
        input._alphaInterpretation = KTXAlphaInterpretation::DATA;
        REQUIRE(EncodeKTXImage(input, encoded));
        REQUIRE(LoadKTXImage(encoded, decoded));
        CHECK(decoded._alpha._skipped);
        CHECK_FALSE(decoded._alpha._transparent);
    }

    TEST_CASE( "KTX2 semantic mips filter sRGB in linear space and retain base classification", "[ktx2]" )
    {
        using namespace ImageTools;
        KTXImage image;
        image._width = 2u; image._height = 1u; image._components = 4u; image._srgb = true;
        image._subresources.push_back({0u, 0u, 0u, 0u, 2u, 1u,
            std::vector<U8>{0, 0, 0, 0, 255, 255, 255, 255}});
        const auto base = image._subresources[0]._pixels;
        REQUIRE(GenerateKTXMipmaps(image));
        CHECK(image._mips == 2u);
        CHECK(image._subresources[0]._pixels == base);
        const auto& mip = std::get<std::vector<U8>>(image._subresources[1]._pixels);
        CHECK(mip == std::vector<U8>{188, 188, 188, 128});
        CHECK(image._alpha._transparentCount == 1u);
        CHECK(image._alpha._opaqueCount == 1u);
        CHECK(image._alpha._translucentCount == 0u);
        std::vector<Byte> encoded;
        REQUIRE(EncodeKTXImage(image, encoded));
        KTXImage decoded;
        REQUIRE(LoadKTXImage(encoded, decoded));
        CHECK_FALSE(decoded._alpha._translucent);
        CHECK(decoded._subresources[1]._pixels == image._subresources[1]._pixels);
        REQUIRE(GenerateKTXMipmaps(image));
        CHECK(image._subresources[1]._pixels == decoded._subresources[1]._pixels);
    }

    TEST_CASE( "KTX2 data mips include odd edges in 1D and volumes without HDR clipping", "[ktx2]" )
    {
        using namespace ImageTools;
        const KTXMipOptions options{KTXMipSemantic::DATA};
        KTXImage image;
        image._width = 5u; image._height = 1u; image._dimensions = 1u;
        image._components = 1u; image._componentType = KTXComponentType::FLOAT_32;
        image._subresources.push_back({0u, 0u, 0u, 0u, 5u, 1u, std::vector<F32>{0.f, 0.f, 0.f, 0.f, 10.f}});
        REQUIRE(GenerateKTXMipmaps(image, options));
        REQUIRE(image._subresources.size() == 3u);
        CHECK(std::get<std::vector<F32>>(image._subresources[1]._pixels) == std::vector<F32>{0.f, 4.f});
        CHECK(std::get<std::vector<F32>>(image._subresources[2]._pixels)[0] == 2.f);
        CHECK(image._alphaInterpretation == KTXAlphaInterpretation::DATA);
        KTXImage volume;
        volume._width = 3u; volume._height = 3u; volume._depth = 3u; volume._dimensions = 3u;
        volume._components = 1u; volume._componentType = KTXComponentType::FLOAT_32;
        for (U32 z = 0u; z < 3u; ++z)
        {
            std::vector<F32> pixels(9u, 0.f);
            if (z == 2u) { pixels[8] = 270.f; }
            volume._subresources.push_back({0u, 0u, 0u, z, 3u, 3u, pixels});
        }
        REQUIRE(GenerateKTXMipmaps(volume, options));
        REQUIRE(volume._subresources.size() == 4u);
        CHECK(std::abs(std::get<std::vector<F32>>(volume._subresources[3]._pixels)[0] - 10.f) < 0.0001f);
        KTXImage hdr;
        hdr._width = 2u; hdr._components = 1u; hdr._componentType = KTXComponentType::FLOAT_32;
        hdr._subresources.push_back({0u, 0u, 0u, 0u, 2u, 1u,
            std::vector<F32>{std::numeric_limits<F32>::max(), std::numeric_limits<F32>::max()}});
        REQUIRE(GenerateKTXMipmaps(hdr, options));
        CHECK(std::get<std::vector<F32>>(hdr._subresources[1]._pixels)[0] == std::numeric_limits<F32>::max());
        hdr._mips = 1u; hdr._subresources.resize(1u); hdr._componentType = KTXComponentType::FLOAT_16;
        hdr._subresources[0]._pixels = std::vector<U16>{glm::packHalf1x16(65504.f), glm::packHalf1x16(65504.f)};
        REQUIRE(GenerateKTXMipmaps(hdr, options));
        CHECK(std::get<std::vector<U16>>(hdr._subresources[1]._pixels)[0] == glm::packHalf1x16(65504.f));
        KTXImage packed;
        packed._width = 2u; packed._components = 4u; packed._componentType = KTXComponentType::FLOAT_32;
        packed._isArray = true; packed._layers = 2u;
        packed._subresources.push_back({0u, 0u, 0u, 0u, 2u, 1u,
            std::vector<F32>{2.f, 4.f, 6.f, -4.f, 4.f, 8.f, 12.f, 8.f}});
        packed._subresources.push_back({0u, 1u, 0u, 0u, 2u, 1u,
            std::vector<F32>{20.f, 40.f, 60.f, -40.f, 40.f, 80.f, 120.f, 80.f}});
        REQUIRE(GenerateKTXMipmaps(packed, options));
        CHECK(std::get<std::vector<F32>>(packed._subresources[2]._pixels) == std::vector<F32>{3.f, 6.f, 9.f, 2.f});
        CHECK(std::get<std::vector<F32>>(packed._subresources[3]._pixels) == std::vector<F32>{30.f, 60.f, 90.f, 20.f});
        KTXImage terminal;
        terminal._width = 1u; terminal._components = 1u;
        terminal._subresources.push_back({0u, 0u, 0u, 0u, 1u, 1u, std::vector<U8>{77u}});
        REQUIRE(GenerateKTXMipmaps(terminal, options));
        CHECK(terminal._mips == 1u);
        REQUIRE(terminal._subresources.size() == 1u);
        CHECK(std::get<std::vector<U8>>(terminal._subresources[0]._pixels)[0] == 77u);
        KTXImage synthetic;
        synthetic._width = 2u; synthetic._components = 4u;
        synthetic._alphaInterpretation = KTXAlphaInterpretation::SYNTHETIC;
        synthetic._subresources.push_back({0u, 0u, 0u, 0u, 2u, 1u, std::vector<U8>{0, 0, 0, 255, 255, 255, 255, 255}});
        REQUIRE(GenerateKTXMipmaps(synthetic, {KTXMipSemantic::COLOR, false, true}));
        CHECK(synthetic._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        CHECK(std::get<std::vector<U8>>(synthetic._subresources[1]._pixels)[3] == 255u);
    }

    TEST_CASE( "KTX2 normal mips renormalize first mip preserve alpha and optionally coverage", "[ktx2]" )
    {
        using namespace ImageTools;
        KTXImage image;
        image._width = 4u; image._height = 1u; image._components = 4u;
        image._componentType = KTXComponentType::FLOAT_32;
        image._subresources.push_back({0u, 0u, 0u, 0u, 4u, 1u,
            std::vector<F32>{1.f, .5f, .5f, 1.f, .5f, 1.f, .5f, 1.f,
                             1.f, .5f, .5f, 0.f, .5f, 1.f, .5f, 0.f}});
        const auto base = image._subresources[0]._pixels;
        REQUIRE(GenerateKTXMipmaps(image, {KTXMipSemantic::NORMAL}));
        CHECK(image._subresources[0]._pixels == base);
        const auto& pixels = std::get<std::vector<F32>>(image._subresources[1]._pixels);
        CHECK(std::abs(pixels[0] - 0.8535534f) < 0.00001f);
        CHECK(std::abs(pixels[1] - 0.8535534f) < 0.00001f);
        CHECK(pixels[2] == .5f);
        CHECK(pixels[3] == 1.f);
        CHECK(pixels[7] == 0.f);
        CHECK(std::get<std::vector<F32>>(image._subresources[2]._pixels)[3] == .5f);
        REQUIRE(GenerateKTXMipmaps(image, {KTXMipSemantic::NORMAL, true, true}));
        CHECK(std::get<std::vector<F32>>(image._subresources[1]._pixels)[3] >= Config::ALPHA_DISCARD_THRESHOLD);
        CHECK(std::get<std::vector<F32>>(image._subresources[1]._pixels)[7] < Config::ALPHA_DISCARD_THRESHOLD);
        CHECK(std::get<std::vector<F32>>(image._subresources[2]._pixels)[3] >= Config::ALPHA_DISCARD_THRESHOLD);
        CHECK(image._alpha._opaqueCount == 2u);
        CHECK(image._alpha._transparentCount == 2u);
    }

    TEST_CASE( "KTX2 semantic failures are transactional and supplied partial mips stay exact", "[ktx2]" )
    {
        using namespace ImageTools;
        KTXImage image;
        image._width = 4u; image._components = 4u; image._mips = 2u;
        image._subresources.push_back({0u, 0u, 0u, 0u, 4u, 1u, std::vector<U8>(16u, 255u)});
        image._subresources.push_back({1u, 0u, 0u, 0u, 2u, 1u, std::vector<U8>(8u, 17u)});
        const auto supplied = image._subresources[1]._pixels;
        REQUIRE(GenerateKTXMipmaps(image));
        CHECK(image._mips == 2u);
        CHECK(image._subresources[1]._pixels == supplied);
        const auto reject = [&](KTXMipOptions options)
        {
            const auto original = image;
            CHECK_FALSE(GenerateKTXMipmaps(image, options));
            CHECK(image._mips == original._mips);
            CHECK(image._alphaInterpretation == original._alphaInterpretation);
            REQUIRE(image._subresources.size() == original._subresources.size());
            for (size_t i = 0u; i < original._subresources.size(); ++i)
            {
                CHECK(image._subresources[i]._pixels == original._subresources[i]._pixels);
            }
        };
        reject({KTXMipSemantic::DATA, true, true});
        image._srgb = true; reject({KTXMipSemantic::NORMAL, true}); reject({KTXMipSemantic::DATA, true});
        image._srgb = false;
        REQUIRE(GenerateKTXMipmaps(image, {KTXMipSemantic::COLOR, true}));
        CHECK(image._mips == 3u);
        CHECK(std::get<std::vector<U8>>(image._subresources[1]._pixels) == std::vector<U8>(8u, 255u));
        image._componentType = KTXComponentType::FLOAT_32;
        image._subresources[0]._pixels = std::vector<F32>(16u, std::numeric_limits<F32>::infinity());
        reject({KTXMipSemantic::COLOR, true});
    }

    TEST_CASE( "KTX2 rejects malformed headers and metadata before allocation", "[ktx2]" )
    {
        using namespace ImageTools;
        const std::vector<Byte> pixels(16u, Byte{255});
        std::vector<Byte> valid;
        REQUIRE(EncodeKTXImage(pixels, 2, 2, 4, 4, false, false, valid));
        KTXImage limited;
        limited._width = 123u;
        CHECK_FALSE(LoadKTXImage(valid, limited, nullptr, 15u));
        CHECK(limited._width == 123u);
        REQUIRE(LoadKTXImage(valid, limited, nullptr, 128u * 1024u));
        const auto writeLE = [](std::vector<Byte>& bytes, size_t offset, U64 value, size_t count)
        {
            for (size_t i = 0; i < count; ++i) { bytes[offset + i] = Byte(static_cast<U8>(value >> (i * 8u))); }
        };
        const auto reject = [&](std::vector<Byte> bytes)
        {
            KTXImage image;
            image._width = 123u;
            std::string error;
            CHECK_FALSE(LoadKTXImage(bytes, image, &error));
            CHECK_FALSE(error.empty());
            CHECK(image._width == 123u);
        };
        reject({});
        reject(std::vector<Byte>(valid.begin(), valid.begin() + 79));
        for (const size_t offset : {20u, 24u, 32u})
        {
            auto bytes = valid;
            writeLE(bytes, offset, 65536u, 4);
            reject(bytes);
        }
        auto bytes = valid;
        writeLE(bytes, 44, 2u, 4); // Zstd must not reach decompression.
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 12, 0u, 4); // Basis / undefined VkFormat.
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 80, U64_MAX, 8);
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 88, 1u, 8);
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 96, KTX_IMAGE_BYTE_LIMIT + 1u, 8);
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 48, 80u, 4); // DFD overlaps level index.
        reject(bytes);
        bytes = valid;
        const auto readLE = [](const std::vector<Byte>& input, const size_t offset)
        {
            U32 value = 0u;
            for (size_t i = 0u; i < 4u; ++i) { value |= U32(std::to_integer<U8>(input[offset + i])) << (8u * i); }
            return value;
        };
        const U32 dfdOffset = readLE(valid, 48u), dfdSize = readLE(valid, 52u);
        const std::vector<Byte> relocatedDFD(valid.begin() + dfdOffset, valid.begin() + dfdOffset + dfdSize);
        while (bytes.size() % 4u != 0u) { bytes.push_back(Byte{0}); }
        const size_t relocatedOffset = bytes.size();
        bytes.insert(bytes.end(), relocatedDFD.begin(), relocatedDFD.end());
        writeLE(bytes, 48u, relocatedOffset, 4u);
        std::fill(bytes.begin() + dfdOffset, bytes.begin() + dfdOffset + dfdSize, Byte{0});
        // A valid offset DFD must not hide an unchecked zero-size DFD at the
        // position consumed by libktx's sequential reader.
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 40, 16u, 4);
        reject(bytes);
        bytes = valid;
        writeLE(bytes, 20, 65535u, 4);
        writeLE(bytes, 24, 65535u, 4);
        reject(bytes);
        const auto corruptText = [&](std::string_view text, size_t index, char replacement)
        {
            auto corrupted = valid;
            auto found = std::search(corrupted.begin(), corrupted.end(),
                reinterpret_cast<const Byte*>(text.data()), reinterpret_cast<const Byte*>(text.data()) + text.size());
            REQUIRE(found != corrupted.end());
            *(found + index) = Byte(static_cast<U8>(replacement));
            reject(corrupted);
        };
        corruptText("ru", 1, 'd');
        corruptText("v=1;threshold=4", 2, '9');
        corruptText("interpretation=opacity", 15, 'X');

        ktxTexture2* raw = nullptr;
        REQUIRE(ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(valid.data()),
            valid.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw) == KTX_SUCCESS);
        const std::unique_ptr<ktxTexture2, decltype(&ktxTexture2_Destroy)> texture(raw, &ktxTexture2_Destroy);
        REQUIRE(ktxHashList_DeleteKVPair(&raw->kvDataHead, "DivideAlpha") == KTX_SUCCESS);
        const auto serialize = [&](ktxTexture2* source = nullptr)
        {
            if (source == nullptr) { source = raw; }
            ktx_uint8_t* encoded = nullptr;
            ktx_size_t size = 0;
            REQUIRE(ktxTexture2_WriteToMemory(source, &encoded, &size) == KTX_SUCCESS);
            const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> owner(encoded, &std::free);
            return std::vector<Byte>(reinterpret_cast<const Byte*>(encoded), reinterpret_cast<const Byte*>(encoded) + size);
        };
        KTXImage unknownAlpha;
        REQUIRE(LoadKTXImage(serialize(), unknownAlpha));
        CHECK(unknownAlpha._alphaInterpretation == KTXAlphaInterpretation::UNKNOWN);
        CHECK_FALSE(unknownAlpha._alpha._transparent);
        CHECK_FALSE(unknownAlpha._alpha._translucent);
        REQUIRE(ktxHashList_DeleteKVPair(&raw->kvDataHead, "KTXorientation") == KTX_SUCCESS);
        reject(serialize());
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "KTXorientation", 3u, "ru") == KTX_SUCCESS);
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "KTXswizzle", 5u, "bgra") == KTX_SUCCESS);
        reject(serialize());
        REQUIRE(ktxHashList_DeleteKVPair(&raw->kvDataHead, "KTXswizzle") == KTX_SUCCESS);
        const std::string prefix = "v=1;threshold=4;transparent=0-1;translucent=2-250;opaque=251-255;";
        const std::string synthetic = prefix + "interpretation=synthetic;skip=1;transparent=0;translucent=0;allzero=0";
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "DivideAlpha",
            static_cast<unsigned int>(synthetic.size() + 1u), synthetic.c_str()) == KTX_SUCCESS);
        KTXImage validSynthetic;
        REQUIRE(LoadKTXImage(serialize(), validSynthetic));
        const U8 binaryMetadata[] = {0xDEu, 0u, 0xFFu, 1u};
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "KTXwriterScParams",
            sizeof(binaryMetadata), binaryMetadata) == KTX_SUCCESS);
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "vendor.binary",
            sizeof(binaryMetadata), binaryMetadata) == KTX_SUCCESS);
        REQUIRE(LoadKTXImage(serialize(), validSynthetic));
        CHECK(validSynthetic._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC);
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "KTXorientation", 2u, "ru") == KTX_SUCCESS);
        reject(serialize());
        REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, "KTXorientation", 3u, "ru") == KTX_SUCCESS);
        for (U32 i = 0; i < 256u; ++i)
        {
            const std::string key = "vendor.entry." + std::to_string(i);
            REQUIRE(ktxHashList_AddKVPair(&raw->kvDataHead, key.c_str(),
                sizeof(binaryMetadata), binaryMetadata) == KTX_SUCCESS);
        }
        reject(serialize());
        for (U32 i = 0; i < 256u; ++i)
        {
            const std::string key = "vendor.entry." + std::to_string(i);
            REQUIRE(ktxHashList_DeleteKVPair(&raw->kvDataHead, key.c_str()) == KTX_SUCCESS);
        }
        auto nonOpaque = pixels;
        nonOpaque[3] = Byte{254};
        REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(raw), 0, 0, 0,
            reinterpret_cast<const ktx_uint8_t*>(nonOpaque.data()), nonOpaque.size()) == KTX_SUCCESS);
        reject(serialize());

        std::vector<Byte> singleChannel;
        REQUIRE(EncodeKTXImage(std::vector<Byte>{Byte{255}}, 1, 1, 1, 1, false, false, singleChannel));
        ktxTexture2* red = nullptr;
        REQUIRE(ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(singleChannel.data()),
            singleChannel.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &red) == KTX_SUCCESS);
        const std::unique_ptr<ktxTexture2, decltype(&ktxTexture2_Destroy)> redTexture(red, &ktxTexture2_Destroy);
        const std::string opacity = prefix + "interpretation=opacity;skip=0;transparent=1;translucent=0;allzero=0";
        REQUIRE(ktxHashList_AddKVPair(&red->kvDataHead, "DivideAlpha",
            static_cast<unsigned int>(opacity.size() + 1u), opacity.c_str()) == KTX_SUCCESS);
        reject(serialize(red));
    }

    TEST_CASE( "Texture update source layouts", "[texture_updates]" )
    {
        TextureDescriptor descriptor;
        TextureUpdateLayout layout;

        SECTION( "compact RGBA rectangle uses region size, not texture size" )
        {
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 12u );
            CHECK( layout._rowStride == 12u );
            CHECK( layout._sourceOffset == 0u );
            CHECK( layout._stagingSize == 24u );
            CHECK( layout._requiredSize == 24u );
        }
        SECTION( "full atlas buffer uses texel row length and source skips" )
        {
            const PixelAlignment alignment{ ._rowLength = 64u, ._skipPixels = 5u, ._skipRows = 7u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, alignment, layout ) );
            CHECK( layout._rowStride == 256u );
            CHECK( layout._sourceOffset == 1812u );
            CHECK( layout._requiredSize == 2080u );
            CHECK( layout._stagingSize == 24u );
        }
        SECTION( "RGB row alignment rounds up instead of multiplying" )
        {
            descriptor._baseFormat = GFXImageFormat::RGB;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 9u );
            CHECK( layout._rowStride == 12u );
            CHECK( layout._requiredSize == 21u );
            CHECK( layout._stagingSize == 18u );
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 1u }, layout ) );
            CHECK( layout._requiredSize == 18u );
        }
        SECTION( "single-channel font atlas" )
        {
            descriptor._baseFormat = GFXImageFormat::RED;
            const PixelAlignment alignment{ ._alignment = 1u, ._rowLength = 64u, ._skipPixels = 5u, ._skipRows = 7u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, alignment, layout ) );
            CHECK( layout._sourceOffset == 453u );
            CHECK( layout._requiredSize == 520u );
            CHECK( layout._stagingSize == 6u );
        }
        SECTION( "multiple layers or depth slices retain source pitch" )
        {
            const PixelAlignment alignment{ ._rowLength = 8u, ._skipPixels = 1u, ._skipRows = 1u };
            REQUIRE( Texture::GetUpdateLayout( descriptor, {2u, 3u, 2u}, alignment, layout ) );
            CHECK( layout._sliceStride == 96u );
            CHECK( layout._sourceOffset == 36u );
            CHECK( layout._requiredSize == 204u );
            CHECK( layout._stagingSize == 48u );
        }
        SECTION( "packed pixels respect requested row alignment" )
        {
            descriptor._packing = GFXImagePacking::RGBA_4444;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 1u }, layout ) );
            CHECK( layout._rowBytes == 6u );
            CHECK( layout._rowStride == 6u );
            CHECK( layout._requiredSize == 12u );
        }
        SECTION( "R11G11B10 uploads describe the shared half-float source" )
        {
            descriptor._baseFormat = GFXImageFormat::RGB;
            descriptor._dataType = GFXDataFormat::FLOAT_16;
            descriptor._packing = GFXImagePacking::RGB_111110F;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, {}, layout ) );
            CHECK( layout._rowBytes == 18u );
            CHECK( layout._rowStride == 20u );
            CHECK( layout._requiredSize == 38u );
            CHECK( layout._stagingSize == 36u );
        }
        SECTION( "1D uploads ignore row skips like OpenGL" )
        {
            descriptor._texType = TextureType::TEXTURE_1D;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {3u, 1u, 1u}, { ._rowLength = 8u, ._skipPixels = 2u, ._skipRows = 7u }, layout ) );
            CHECK( layout._sourceOffset == 8u );
            CHECK( layout._requiredSize == 20u );
        }
        SECTION( "BC blocks include incomplete edge blocks" )
        {
            descriptor._baseFormat = GFXImageFormat::BC1;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {7u, 5u, 2u}, {}, layout ) );
            CHECK( layout._rowBytes == 16u );
            CHECK( layout._rowCount == 2u );
            CHECK( layout._requiredSize == 64u );
            descriptor._baseFormat = GFXImageFormat::BC3;
            REQUIRE( Texture::GetUpdateLayout( descriptor, {7u, 5u, 2u}, {}, layout ) );
            CHECK( layout._requiredSize == 128u );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {4u, 4u, 1u}, { ._rowLength = 8u }, layout ) );
        }
        SECTION( "invalid and overflowing pixel-store inputs are rejected" )
        {
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._alignment = 3u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = 2u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = 4u, ._skipPixels = 2u }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._rowLength = SIZE_MAX }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {3u, 2u, 1u}, { ._skipRows = SIZE_MAX }, layout ) );
            CHECK_FALSE( Texture::GetUpdateLayout( descriptor, {0u, 2u, 1u}, {}, layout ) );
        }
    }

    TEST_CASE( "Texture update mip and layer bounds", "[texture_updates]" )
    {
        SECTION( "nonzero mip coordinates are already mip-local" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 2u, {13u, 6u, 0u}, {3u, 2u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 2u, {14u, 6u, 0u}, {3u, 2u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, ALL_MIPS, {}, {1u, 1u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 7u, {}, {1u, 1u, 1u} ) );
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 6u, {}, {1u, 1u, 1u} ) );
        }
        SECTION( "array layers do not shrink with mips" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D_ARRAY, {64u, 32u, 1u}, 4u, 7u, 2u, {0u, 0u, 2u}, {4u, 4u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D_ARRAY, {64u, 32u, 1u}, 4u, 7u, 2u, {0u, 0u, 3u}, {4u, 4u, 2u} ) );
        }
        SECTION( "cube z selects individual faces" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_CUBE_ARRAY, {32u, 32u, 1u}, 2u, 6u, 1u, {0u, 0u, 11u}, {4u, 4u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_CUBE_ARRAY, {32u, 32u, 1u}, 2u, 6u, 1u, {0u, 0u, 12u}, {4u, 4u, 1u} ) );
        }
        SECTION( "3D depth shrinks with mips" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_3D, {32u, 32u, 8u}, 1u, 6u, 1u, {0u, 0u, 2u}, {4u, 4u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_3D, {32u, 32u, 8u}, 1u, 6u, 1u, {0u, 0u, 3u}, {4u, 4u, 2u} ) );
        }
        SECTION( "1D arrays use z for layers" )
        {
            CHECK( Texture::IsValidUpdateRegion( TextureType::TEXTURE_1D_ARRAY, {32u, 1u, 1u}, 4u, 6u, 1u, {0u, 0u, 2u}, {4u, 1u, 2u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_1D_ARRAY, {32u, 1u, 1u}, 4u, 6u, 1u, {0u, 1u, 2u}, {4u, 1u, 2u} ) );
        }
        SECTION( "zero extents and wrapped coordinates are invalid" )
        {
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 0u, {}, {1u, 0u, 1u} ) );
            CHECK_FALSE( Texture::IsValidUpdateRegion( TextureType::TEXTURE_2D, {64u, 32u, 1u}, 1u, 7u, 0u, {65535u, 0u, 0u}, {2u, 1u, 1u} ) );
        }
    }

    struct VKAPITestAccessor
    {
        static VKTransferQueue& queue() noexcept { return VK_API::s_transferQueue; }
    };

    TEST_CASE("VKTransferQueue producer non-blocking and Flush drains", "[vk_transfer_queue]") {
        // Drain any leftover state
        VKTransferQueue::TransferRequest tmp{};

        while (VKAPITestAccessor::queue()._requests.try_dequeue(tmp))
        {
        }

        VKAPITestAccessor::queue()._dirty.store(false, std::memory_order_release);

        SECTION("single enqueue is non-blocking and visible")
        {
            VKTransferQueue::TransferRequest req{};
            req.srcBuffer = VK_NULL_HANDLE; // valid request form for this test

            VK_API::RegisterTransferRequest(req);

            // Immediately visible on the lock-free queue
            REQUIRE(VKAPITestAccessor::queue()._requests.try_dequeue(tmp));
        }

        SECTION("Flush drains all queued requests and clears dirty")
        {
            // Enqueue multiple requests
            constexpr size_t N = 128;
            VKTransferQueue::TransferRequest req{};
            req.srcBuffer = VK_NULL_HANDLE;

            for (size_t i = 0; i < N; ++i)
            {
                VK_API::RegisterTransferRequest(req);
            }

            // Sanity: producers published work
            REQUIRE(VKAPITestAccessor::queue()._dirty.load(std::memory_order_acquire));

            // Call the consumer (simulating the render thread). VK_NULL_HANDLE is acceptable for this unit test.
            VK_API::FlushBufferTransferRequests(VK_NULL_HANDLE);

            // After Flush, queue should be drained and dirty cleared.
            REQUIRE(!VKAPITestAccessor::queue()._dirty.load(std::memory_order_acquire));
            REQUIRE(!VKAPITestAccessor::queue()._requests.try_dequeue(tmp));
        }
    }
} //namespace Divide