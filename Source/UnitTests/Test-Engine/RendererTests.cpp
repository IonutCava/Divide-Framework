#include "UnitTests/unitTestCommon.h"

#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Utility/Headers/KTXImage.h"

#include <ktx.h>
#include <cstdlib>
#include <cstring>

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

    TEST_CASE( "KTX2 rejects malformed headers and metadata before allocation", "[ktx2]" )
    {
        using namespace ImageTools;
        const std::vector<Byte> pixels(16u, Byte{255});
        std::vector<Byte> valid;
        REQUIRE(EncodeKTXImage(pixels, 2, 2, 4, 4, false, false, valid));
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