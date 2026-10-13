#include "Headers/ImageTools.h"
#include "Headers/KTXImageCache.h"

#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Core/Headers/PlatformContext.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG

#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION

DISABLE_NON_MSVC_WARNING_PUSH("array-bounds-pointer-arithmetic")
#include "stb_image_resize2.h"
DISABLE_NON_MSVC_WARNING_POP()

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#undef _UNICODE
#include <IL/il.h>
#include <IL/ilu.h>
//#include <IL/ilut.h>

#include <glm/detail/type_half.hpp>
#include <fstream>
#include <limits>
#include <mutex>
#include <type_traits>

namespace Divide::ImageTools
{
namespace
{
    SharedMutex s_imageLoadingMutex;
    bool s_useUpperLeftOrigin = false;

    //ref: https://github.com/nvpro-pipeline/pipeline/blob/master/dp/sg/io/IL/Loader/ILTexLoader.cpp
    FORCE_INLINE I32 determineFace(const I32 i, const bool isDDS, const bool isCube) noexcept
    {
        if (isDDS && isCube)
        {
            if (i == 4)
            {
                return 5;
            }
            else if (i == 5)
            {
                return 4;
            }
        }

        return i;
    }

    [[nodiscard]] inline bool checkError(string& messageInOut) noexcept
    {
        bool ret = false;

        ILenum error = ilGetError();

        messageInOut.resize(0);
        while (error != IL_NO_ERROR)
        {
            ret = true;

            messageInOut.append("\n").append( iluErrorString(error) );

            ILenum nextError = ilGetError();
            while (error == nextError)
            {
                nextError = ilGetError();
            }
            error = nextError;
        }

        return ret;
    }
}

void OnStartup(const bool upperLeftOrigin)
{
    s_useUpperLeftOrigin = upperLeftOrigin;

    ilInit();
    iluInit();
    //ilutInit();
    ilEnable(IL_FILE_OVERWRITE);
    ilSetInteger(IL_KEEP_DXTC_DATA, IL_TRUE);
    //ilutRenderer(ILUT_WIN32);
    iluImageParameter(ILU_FILTER, ILU_SCALE_MITCHELL);
}

void OnShutdown()
{
    ilShutDown();
}

bool UseUpperLeftOrigin() noexcept
{
    return s_useUpperLeftOrigin;
}

bool ImageData::loadFromMemory( std::span<const Byte> data, const vec3<U16>& dimensions, const U16 layerCount, const U8 numComponents)
{
    _hasDummyData = data.empty();

    for ( U16 i = 0u; i < layerCount; ++i )
    {
        ImageLayer& layer = _layers.emplace_back();
        if ( layer.allocateMip( data.data(), data.size(), dimensions.x, dimensions.y, dimensions.z, numComponents ) == nullptr )
        {
            return false;
        }
    }

    return true;
}

bool ImageData::loadFromFile(PlatformContext& context, const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name)
{
    ImportOptions options{};
    options._useKTXCache = false;
    return loadFromFile(context, srgb, refWidth, refHeight, path, name, options);
}

namespace
{
    static stbi__uint16* stbi__hdr_to_16( float* data, int x, int y, int comp )
    {
        if ( !data ) 
        {
            return nullptr;
        }

        stbi__uint16* output = (stbi__uint16*)stbi__malloc_mad4( x, y, comp, sizeof(stbi__uint16), 0 );
        if ( output == NULL )
        {
            STBI_FREE( data );
            return nullptr;
        }

        // compute number of non-alpha components
        int n = 0;
        if ( comp & 1 )
        {
            n = comp;
        }
        else
        {
            n = comp - 1;
        }

        for (int i = 0; i < x * y; ++i )
        {
            int k = 0;
            for (; k < n; ++k )
            {
                float z = (float)pow( data[i * comp + k] * stbi__h2l_scale_i, stbi__h2l_gamma_i ) * 65536 + 0.5f;
                if ( z < 0 ) z = 0;
                if ( z > 65536 ) z = 65536;
                output[i * comp + k] = (stbi__uint16)to_I32( z );
            }
            if ( k < comp )
            {
                float z = data[i * comp + k] * 65536 + 0.5f;
                if ( z < 0 ) z = 0;
                if ( z > 65536 ) z = 65536;
                output[i * comp + k] = (stbi__uint16)to_I32( z );
            }
        }

        STBI_FREE( data );


        return output;
    }

    static glm::detail::hdata* stbi__hdr_to_half( float* data, int x, int y, int comp )
    {
        if ( !data ) 
        {
            return nullptr;
        }

        glm::detail::hdata* output = (glm::detail::hdata*)stbi__malloc_mad4( x, y, comp, sizeof(glm::detail::hdata), 0 );
        if ( output == NULL )
        {
            STBI_FREE( data );
            return nullptr;
        }

        // compute number of non-alpha components
        int n = 0;
        if ( comp & 1 ) 
        {
            n = comp;
        }
        else
        {
            n = comp - 1;
        }

        for (int i = 0; i < x * y; ++i )
        {
            int k = 0;
            for (; k < n; ++k )
            {
                output[i * comp + k] = glm::detail::toFloat16( data[i * comp + k]);
            }
            if ( k < comp )
            {
                output[i * comp + k] = glm::detail::toFloat16( data[i * comp + k] );
            }
        }

        STBI_FREE( data );
        return output;
    }

    static glm::detail::hdata* stbi__ldr_to_half( stbi_uc* data, int x, int y, int comp )
    {
        if ( !data )
        {
            return nullptr;
        }

        glm::detail::hdata* output = (glm::detail::hdata*)stbi__malloc_mad4( x, y, comp, sizeof( glm::detail::hdata ), 0 );
        if ( output == NULL )
        {
            STBI_FREE( data );
            return nullptr;
        }

        // compute number of non-alpha components
        int n = 0;
        if ( comp & 1 )
        {
            n = comp;
        }
        else
        {
            n = comp - 1;
        }

        for (int i = 0; i < x * y; ++i )
        {
            for (int k = 0; k < n; ++k )
            {
                output[i * comp + k] = glm::detail::toFloat16( (float)(pow( data[i * comp + k] / 255.0f, stbi__l2h_gamma ) * stbi__l2h_scale));
            }
        }
        if ( n < comp )
        {
            for (int i = 0; i < x * y; ++i )
            {
                output[i * comp + n] = glm::detail::toFloat16( data[i * comp + n] / 255.0f );
            }
        }

        STBI_FREE( data );
        return output;
    }

    static glm::detail::hdata* stbi__16_to_half( stbi__uint16* data, int x, int y, int comp )
    {
        if ( !data )
        {
            return nullptr;
        }

        glm::detail::hdata* output = (glm::detail::hdata*)stbi__malloc_mad4( x, y, comp, sizeof( glm::detail::hdata ), 0 );

        if ( output == NULL )
        {
            STBI_FREE( data );
            return nullptr;
        }

        // compute number of non-alpha components
        int n = 0;
        if ( comp & 1 )
        {
            n = comp;
        }
        else
        {
            n = comp - 1;
        }

        for (int i = 0; i < x * y; ++i )
        {
            for (int k = 0; k < n; ++k )
            {
                output[i * comp + k] = glm::detail::toFloat16((float)(pow( data[i * comp + k] / 65536.0f, stbi__l2h_gamma ) * stbi__l2h_scale));
            }
        }
        if ( n < comp )
        {
            for (int i = 0; i < x * y; ++i )
            {
                output[i * comp + n] = glm::detail::toFloat16( data[i * comp + n] / 65536.0f );
            }
        }

        STBI_FREE( data );
        return output;
    }

    static float* stbi__16_to_hdr( stbi__uint16* data, int x, int y, int comp )
    {
        if ( !data )
        {
            return nullptr;
        }

        float* output = (float*)stbi__malloc_mad4( x, y, comp, sizeof( float ), 0 );
        if ( output == NULL )
        {
            STBI_FREE( data );
            return nullptr;
        }

        // compute number of non-alpha components
        int n = 0;
        if ( comp & 1 )
        {
            n = comp;
        }
        else
        {
            n = comp - 1;
        }

        for (int i = 0; i < x * y; ++i )
        {
            for (int k = 0; k < n; ++k )
            {
                output[i * comp + k] = (float)(pow( data[i * comp + k] / 65536.0f, stbi__l2h_gamma ) * stbi__l2h_scale);
            }
        }
        if ( n < comp )
        {
            for (int i = 0; i < x * y; ++i )
            {
                output[i * comp + n] = data[i * comp + n] / 65536.0f;
            }
        }

        STBI_FREE( data );
        return output;
    }

    template<typename To, typename From>
    static To* stbi_convert( From* data, int x, int y, int comp )
    {
        if ( !data )
        {
            return nullptr;
        }

        To* output = (To*)stbi__malloc_mad4( x, y, comp, sizeof(To), 0 );
        if ( output == NULL )
        {
            STBI_FREE( data );
            return nullptr;
        }
        
        // compute number of non-alpha components
        int n = 0;
        if ( comp & 1 )
        {
            n = comp;
        }
        else
        {
            n = comp - 1;
        }

        for (int i = 0; i < x * y; ++i )
        {
            int k = 0;
            for (; k < n; ++k )
            {
                output[i * comp + k] = static_cast<To>( data[i * comp + k] );
            }
            if ( k < comp )
            {
                output[i * comp + k] = static_cast<To>( data[i * comp + k] );
            }
        }

        STBI_FREE( data );
        return output;
    }
}

bool ImageData::loadFromFile([[maybe_unused]] PlatformContext& context, const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name, ImportOptions& options)
{
    _path = path;
    _name = name;

    ignoreAlphaChannelTransparency(!options._alphaChannelTransparency);

    if (_isKTXImage)
    {
        Console::errorfn("KTX2 assets cannot be combined with comma-separated image inputs: {}", name);
        return false;
    }

    if (!_layers.empty())
    {
        // Decode subsequent legacy inputs separately. A DDS block payload must
        // never be appended to STB texels (or vice versa) with last-format-wins.
        ImageData incoming;
        incoming.requestedFormat(_requestedDataFormat);
        incoming.allowKTXSourceImport(false);
        if (!incoming.loadFromFile(context, srgb, refWidth, refHeight, path, name, options))
        {
            return false;
        }
        bool compatible = !incoming._isKTXImage && !incoming._layers.empty() &&
                          _format == incoming._format && _dataType == incoming._dataType &&
                          _sourceDataType == incoming._sourceDataType && _bpp == incoming._bpp;
        for (const auto& layer : incoming._layers)
        {
            compatible = compatible && layer.mipCount() == mipCount();
            if (!compatible) { break; }
            for (U8 mip = 0u; mip < mipCount(); ++mip)
            {
                const auto* source = layer.getMip(mip);
                const auto* reference = _layers.front().getMip(mip);
                compatible = source->_dimensions == reference->_dimensions && source->_size == reference->_size;
                if (!compatible) { break; }
            }
            if (!compatible) { break; }
        }
        if (!compatible)
        {
            _sourceListLayoutMismatch = true;
            Console::errorfn("Incompatible texture source list entry {}: format, type, dimensions and mip layout must match", path / name);
            return false;
        }
        for (auto& layer : incoming._layers)
        {
            _layers.push_back(std::move(layer));
        }
        _hasDummyAlphaChannel = _hasDummyAlphaChannel && incoming._hasDummyAlphaChannel;
        _ignoreAlphaChannelTransparency = !options._alphaChannelTransparency || _hasDummyAlphaChannel;
        _decompressedData = std::move(incoming._decompressedData);
        return true;
    }

    if (hasExtension(_name, "ktx2"))
    {
        return loadKTX(_path, _name);
    }

    const bool isPNG = hasExtension(_name, "png");
    const bool isJPEG = hasExtension(_name, "jpg") || hasExtension(_name, "jpeg");
    const bool isBMP = hasExtension(_name, "bmp");
    if (options._useKTXPipeline && allowKTXSourceImport() && (isPNG || isJPEG || isBMP))
    {
        KTXSourceSettings settings;
        settings._format = isPNG ? KTXSourceFormat::PNG : isJPEG ? KTXSourceFormat::JPEG : KTXSourceFormat::BMP;
        settings._srgb = srgb;
        settings._alphaInterpretation = options._alphaChannelTransparency
            ? KTXAlphaInterpretation::OPACITY : KTXAlphaInterpretation::DATA;
        settings._semantic = options._isNormalMap ? KTXMipSemantic::NORMAL
            : srgb || options._alphaChannelTransparency ? KTXMipSemantic::COLOR : KTXMipSemantic::DATA;
        settings._generateMipmaps = !options._skipMipMaps;
        settings._preserveAlphaCoverage = settings._generateMipmaps && options._alphaChannelTransparency && !options._isNormalMap;
        settings._diskCache = Texture::UseTextureKTXCache() && options._useKTXCache;
        KTXImage image;
        std::string error;
        if (!LoadCachedKTXSource(_path / _name, Paths::Textures::g_metadataLocation / "KTX2",
                                 settings, image, &error))
        {
            Console::errorfn("Unable to import image through KTX2 {}: {}", _path / _name, error);
            return false;
        }
        if (!loadFromKTXImage(image))
        {
            return false;
        }
        return true;
    }

    // DDS is an asset import format only. Never probe legacy DDS cache files.
    if (hasExtension(_name, Paths::Textures::g_ddsExtension))
    {
        if (loadDDS_IL(srgb, refWidth, refHeight, _path, _name,
                      options._useKTXPipeline && allowKTXSourceImport(), options)) [[likely]]
        {
            return true;
        }

        return false;
    }

    const string fullPath = (_path / _name).string();

    FILE* f = stbi__fopen(fullPath.c_str(), "rb");
    if ( !f )
    {
        return false;
    }
    SCOPE_EXIT
    {
        fclose(f);
    };

    I32 width = 0, height = 0, comp = 0;

    stbi_uc* dataLDR = nullptr;
    stbi__uint16* data16Bit = nullptr;
    F32* dataHDR = nullptr;
    U32* dataUINT = nullptr;
    glm::detail::hdata* dataHalf = nullptr;

    _sourceDataType = SourceDataType::BYTE;

    if ( stbi_is_hdr_from_file( f ) == 1 )
    {
        _sourceDataType = SourceDataType::FLOAT;
    }
    else if ( stbi_is_16_bit_from_file( f ) == 1 )
    {
        _sourceDataType = SourceDataType::SHORT;
    }

    stbi_set_flip_vertically_on_load_thread(UseUpperLeftOrigin() ? 0 : 1); //< If TRUE: flip the image vertically, so the first pixel in the output array is the bottom left

    _hasDummyAlphaChannel = false;
    {
        I32 x, y, n;
        if (stbi_info_from_file(f, &x, &y, &n) && n == 3)
        {
            _hasDummyAlphaChannel = true;
            _ignoreAlphaChannelTransparency = true;
            Console::warnfn(LOCALE_STR("WARN_IMAGETOOLS_RGB_FORMAT"), fullPath);
        }
    }

    const auto req_comp = _hasDummyAlphaChannel ? STBI_rgb_alpha : STBI_default;
    switch ( _sourceDataType )
    {
        case SourceDataType::FLOAT:
        {
            dataHDR = stbi_loadf_from_file( f, &width, &height, &comp, req_comp );
            _dataType = GFXDataFormat::FLOAT_32;
        } break;
        case SourceDataType::SHORT:
        {
            data16Bit = stbi_load_from_file_16( f, &width, &height, &comp, req_comp );
            _dataType = GFXDataFormat::UNSIGNED_SHORT;
        } break;
        case SourceDataType::BYTE:
        {
            dataLDR = stbi_load_from_file( f, &width, &height, &comp, req_comp );
            _dataType = GFXDataFormat::UNSIGNED_BYTE;
        } break;
        default: break;
    }

    if (dataHDR == nullptr && data16Bit == nullptr && dataLDR == nullptr)
    {
        Console::errorfn(LOCALE_STR("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE"), fullPath, stbi_failure_reason() );
        return false;
    }

    if (_hasDummyAlphaChannel)
    {
        comp = req_comp;
    }

    ImageLayer& layer = _layers.emplace_back();

    if (_requestedDataFormat != GFXDataFormat::COUNT && _requestedDataFormat != _dataType)
    {
        stbi_ldr_to_hdr_scale( 1.f );
        stbi_ldr_to_hdr_gamma( 1.f );

        switch (_requestedDataFormat)
        {
            case GFXDataFormat::UNSIGNED_BYTE:
            {
                switch ( _sourceDataType )
                {
                    case SourceDataType::BYTE: break;
                    case SourceDataType::SHORT:
                    {
                        dataLDR = stbi__convert_16_to_8( data16Bit, width, height, comp );
                        data16Bit = nullptr;
                    } break;
                    case SourceDataType::FLOAT:
                    {
                        dataLDR = stbi__hdr_to_ldr( dataHDR, width, height, comp );
                        dataHDR = nullptr;
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                };
                _sourceDataType = SourceDataType::BYTE;
            } break;
            case GFXDataFormat::UNSIGNED_SHORT:
            {
                switch ( _sourceDataType )
                {
                    case SourceDataType::BYTE:
                    {
                        data16Bit = stbi__convert_8_to_16( dataLDR, width, height, comp );
                        dataLDR = nullptr;
                    } break;
                    case SourceDataType::SHORT: break;
                    case SourceDataType::FLOAT:
                    {
                        data16Bit = stbi__hdr_to_16( dataHDR, width, height, comp );
                        dataHDR = nullptr;
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                };
                _sourceDataType = SourceDataType::SHORT;
            } break;
            case GFXDataFormat::UNSIGNED_INT: 
            {
                switch ( _sourceDataType )
                {
                    case SourceDataType::BYTE:
                    {
                        dataUINT = stbi_convert<U32, stbi_uc>(dataLDR, width, height, comp);
                        dataLDR = nullptr;
                    } break;
                    case SourceDataType::SHORT:
                    {
                        dataUINT = stbi_convert<U32, stbi__uint16>( data16Bit, width, height, comp );
                        data16Bit = nullptr;
                    } break;
                    case SourceDataType::FLOAT:
                    {
                        data16Bit = stbi__hdr_to_16( dataHDR, width, height, comp );
                        dataHDR = nullptr;
                        dataUINT = stbi_convert<U32, stbi__uint16>( data16Bit, width, height, comp );
                        data16Bit = nullptr;
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                };
                _sourceDataType = SourceDataType::UINT;
            } break;
            case GFXDataFormat::SIGNED_BYTE:
            case GFXDataFormat::SIGNED_SHORT:
            case GFXDataFormat::SIGNED_INT:
            {
                DIVIDE_UNEXPECTED_CALL_MSG("Signed data types not supported when loading from file!");
            } break;
            case GFXDataFormat::FLOAT_16:
            {
                switch ( _sourceDataType )
                {
                    case SourceDataType::BYTE:
                    {
                        dataHalf = stbi__ldr_to_half( dataLDR, width, height, comp );
                        dataLDR = nullptr;
                    }break;
                    case SourceDataType::SHORT:
                    {
                        dataHalf = stbi__16_to_half( data16Bit, width, height, comp );
                        data16Bit = nullptr;
                    }break;
                    case SourceDataType::FLOAT:
                    {
                        dataHalf = stbi__hdr_to_half( dataHDR, width, height, comp );
                        dataHDR = nullptr;
                    }break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                };
                _sourceDataType = SourceDataType::HALF;
            } break;
            case GFXDataFormat::FLOAT_32:
            {
                switch ( _sourceDataType )
                {
                    case SourceDataType::BYTE:
                    {
                        dataHDR = stbi__ldr_to_hdr( dataLDR, width, height, comp );
                        dataLDR = nullptr;
                    }break;
                    case SourceDataType::SHORT:
                    {
                        dataHDR = stbi__16_to_hdr( data16Bit, width, height, comp );
                        data16Bit = nullptr;
                    }break;
                    case SourceDataType::FLOAT: break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
                _sourceDataType = SourceDataType::FLOAT;
            } break;
            default: DIVIDE_UNEXPECTED_CALL_MSG("Invalid requested texture format!"); break;
        }

        _dataType = _requestedDataFormat;
    }

    DIVIDE_ASSERT(comp != 3, "RGB textures (e.g. 24bit) not supported due to Vulkan limitations");

    switch (comp)
    {
        case 1 : _format = GFXImageFormat::RED;  break;
        case 2 : _format = GFXImageFormat::RG;   break;
        case 4 : _format = GFXImageFormat::RGBA; break;
        default:
            DIVIDE_UNEXPECTED_CALL();
            break;
    }

    if (refWidth != 0 && refHeight != 0 && (refWidth != width || refHeight != height))
    {
        stbir_pixel_layout pixelLayout = STBIR_4CHANNEL;
        switch (comp)
        {
            default:
            case STBI_default    : pixelLayout = STBIR_BGR;      break;
            case STBI_grey       : pixelLayout = STBIR_1CHANNEL; break;
            case STBI_grey_alpha : pixelLayout = STBIR_2CHANNEL; break;
            case STBI_rgb        : pixelLayout = STBIR_RGB;      break;
            case STBI_rgb_alpha  : pixelLayout = STBIR_RGBA;     break;
        };

        if ( data16Bit != nullptr)
        {
            U16* resizedData16 = reinterpret_cast<U16*>(stbir_resize( data16Bit, width, height, 0, nullptr,  refWidth, refHeight, 0, pixelLayout,
                                                                      STBIR_TYPE_UINT16, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT ));
            if (resizedData16 != nullptr)
            {
                width = refWidth;
                height = refHeight;
                stbi_image_free(data16Bit);
                data16Bit = resizedData16;
            }
        }
        else if ( dataHDR != nullptr )
        {
            F32* resizedDataHDR = stbir_resize_float_linear( dataHDR, width, height, 0, nullptr, refWidth, refHeight, 0, pixelLayout );
            if (resizedDataHDR != nullptr)
            {
                width = refWidth;
                height = refHeight;
                stbi_image_free(dataHDR);
                dataHDR = resizedDataHDR;
            }
        }
        else
        {
            U8* resizedDataLDR = srgb ? stbir_resize_uint8_srgb(   dataLDR, width, height, 0, nullptr, refWidth, refHeight, 0, pixelLayout )
                                      : stbir_resize_uint8_linear( dataLDR, width, height, 0, nullptr, refWidth, refHeight, 0, pixelLayout );
            if (resizedDataLDR != nullptr)
            {
                width = refWidth;
                height = refHeight;
                stbi_image_free(dataLDR);
                dataLDR = resizedDataLDR;
            }
        }
    }

    _bpp = to_U8( ((dataUINT != nullptr || dataHDR != nullptr) ? 32u : (data16Bit != nullptr || dataHalf != nullptr) ? 16u : 8u) * comp );

    bool ret = false;
    const size_t dataSize = to_size(width) * height * comp;
    if (dataHDR != nullptr)
    {
        ret = layer.allocateMip(dataHDR, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free(dataHDR);
    }
    else if ( dataUINT != nullptr)
    {
        ret = layer.allocateMip( dataUINT, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free( dataUINT );
    }
    else if (data16Bit != nullptr)
    {
        ret = layer.allocateMip(data16Bit, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free(data16Bit);
    }
    else if ( dataHalf != nullptr)
    {
        ret = layer.allocateMip( dataHalf, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free( dataHalf );
    }
    else if (dataLDR != nullptr)
    {
        ret = layer.allocateMip(dataLDR, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free(dataLDR);
    }
    else
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    return ret;
}

bool ImageData::loadKTX(const ResourcePath& path, const std::string_view name)
{
    // Cooked KTX2 assets are self-contained; no source image or authoring tool is required.
    if (!_layers.empty())
    {
        Console::errorfn("KTX2 assets cannot be combined with comma-separated image inputs: {}", name);
        return false;
    }
    std::ifstream stream;
    if (readFile(path, name, FileType::BINARY, stream) != FileError::NONE)
    {
        return false;
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(KTX_IMAGE_BYTE_LIMIT))
    {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    std::vector<Byte> bytes(static_cast<size_t>(length));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
        return false;
    }
    return loadFromKTXMemory(bytes);
}

bool ImageData::loadFromKTXMemory(const std::span<const Byte> bytes)
{
    if (!_layers.empty() || bytes.size() > KTX_IMAGE_BYTE_LIMIT)
    {
        return false;
    }
    KTXImage image;
    std::string error;
    if (!LoadKTXImage(bytes, image, &error))
    {
        Console::errorfn("Unable to load KTX2 image {}: {}", _name, error);
        return false;
    }
    return convertKTXImage(image, bytes.size(), false);
}

bool ImageData::encodeKTX(std::vector<Byte>& output, std::string* error) const
{
    const auto fail = [&](const char* message)
    {
        if (error != nullptr) { *error = message; }
        return false;
    };
    if (!_isKTXImage || IsCompressed(_format) || _layers.size() != 1u ||
        _ktxTextureType != TextureType::TEXTURE_2D || mipCount() == 0u ||
        (_dataType != GFXDataFormat::UNSIGNED_BYTE && _dataType != GFXDataFormat::UNSIGNED_SHORT &&
         _dataType != GFXDataFormat::FLOAT_16 && _dataType != GFXDataFormat::FLOAT_32))
    {
        return fail("KTX2 export requires a native typed single-2D image");
    }
    KTXImage image;
    image._width = dimensions(0u).x;
    image._height = dimensions(0u).y;
    image._mips = mipCount();
    image._components = to_U8(NumChannels(_format));
    image._srgb = _ktxSRGB;
    image._alphaInterpretation = _ktxAlphaInterpretation;
    image._componentType = _dataType == GFXDataFormat::FLOAT_32 ? KTXComponentType::FLOAT_32
                         : _dataType == GFXDataFormat::FLOAT_16 ? KTXComponentType::FLOAT_16
                         : _dataType == GFXDataFormat::UNSIGNED_SHORT ? KTXComponentType::UNORM_16
                         : KTXComponentType::UNORM_8;
    const size_t componentBytes = _dataType == GFXDataFormat::FLOAT_32 ? sizeof(F32)
                                : _dataType == GFXDataFormat::UNSIGNED_BYTE ? sizeof(U8) : sizeof(U16);
    size_t bytes = 0u;
    for (U8 mip = 0u; mip < image._mips; ++mip)
    {
        const LayerData* source = _layers.front().getMip(mip);
        const size_t samples = to_size(source->_dimensions.x) * source->_dimensions.y * image._components;
        if (source->_dimensions.z != 1u || source->_size != samples * componentBytes ||
            samples > (KTX_IMAGE_BYTE_LIMIT - bytes) / componentBytes)
        {
            return fail("Invalid native KTX2 export mip storage");
        }
        bytes += samples * componentBytes;
    }
    // EncodeKTXImage budgets five payloads. Also retain this ImageData and the
    // caller's previous output while reconstructing exact typed slices.
    const U64 bookkeeping = U64(image._mips) * (sizeof(KTXSubresource) * 3u + 64u) + 4096u;
    if (U64(bytes) * 6u + output.capacity() + bookkeeping >= KTX_IMAGE_BYTE_LIMIT)
    {
        return fail("KTX2 export exceeds the aggregate memory budget");
    }
    for (U8 mip = 0u; mip < image._mips; ++mip)
    {
        const LayerData* source = _layers.front().getMip(mip);
        KTXSubresource resource;
        resource._mip = mip;
        resource._width = source->_dimensions.x;
        resource._height = source->_dimensions.y;
        const auto copyPixels = [&]<typename T>()
        {
            const auto* pixels = static_cast<const T*>(source->data());
            resource._pixels = std::vector<T>(pixels, pixels + source->_size / sizeof(T));
        };
        if (_dataType == GFXDataFormat::FLOAT_32) { copyPixels.operator()<F32>(); }
        else if (_dataType == GFXDataFormat::UNSIGNED_BYTE) { copyPixels.operator()<U8>(); }
        else { copyPixels.operator()<U16>(); }
        image._subresources.push_back(std::move(resource));
    }
    return EncodeKTXImage(image, output, error);
}

GFXImageFormat ImageData::ktxGPUFormat(const KTXGPUCompressionOptions& options) const noexcept
{
    if (!_isKTXImage || _dataType != GFXDataFormat::UNSIGNED_BYTE || _layers.empty())
    {
        return GFXImageFormat::COUNT;
    }
    KTXImage image;
    image._compressed = IsCompressed(_format);
    image._components = _format == GFXImageFormat::RED ? 1u : _format == GFXImageFormat::RG ? 2u : 4u;
    image._dimensions = _ktxTextureType == TextureType::TEXTURE_3D ? 3u
                      : (_ktxTextureType == TextureType::TEXTURE_1D || _ktxTextureType == TextureType::TEXTURE_1D_ARRAY) ? 1u : 2u;
    image._depth = dimensions(0u).z;
    image._srgb = _ktxSRGB;
    image._alphaInterpretation = _ktxAlphaInterpretation;
    return SelectKTXGPUFormat(image, options);
}

bool ImageData::compressedKTXCopy(const GFXImageFormat target, ImageData& output, const KTXGPUCacheOptions& cache) const
{
    if (!output._layers.empty() || !_isKTXImage || IsCompressed(_format) ||
        _dataType != GFXDataFormat::UNSIGNED_BYTE || _layers.empty() ||
        (_ktxTextureType != TextureType::TEXTURE_2D && _ktxTextureType != TextureType::TEXTURE_2D_ARRAY &&
         _ktxTextureType != TextureType::TEXTURE_CUBE_MAP && _ktxTextureType != TextureType::TEXTURE_CUBE_ARRAY))
    {
        return false;
    }
    // Include upload preparation in serialization: otherwise many asset jobs
    // could retain full native-input copies while waiting for the encoder.
    static std::mutex preparationMutex;
    const std::lock_guard preparationLock(preparationMutex);
    KTXImage image;
    image._width = dimensions(0u).x; image._height = dimensions(0u).y;
    image._layers = _ktxLayerCount;
    image._faces = IsCubeTexture(_ktxTextureType) ? 6u : 1u;
    image._isArray = IsArrayTexture(_ktxTextureType);
    image._mips = mipCount(); image._srgb = _ktxSRGB;
    image._components = _format == GFXImageFormat::RED ? 1u : _format == GFXImageFormat::RG ? 2u : 4u;
    image._alphaInterpretation = _ktxAlphaInterpretation; image._alpha = _ktxAlpha;
    image._textureType = _ktxTextureType;
    size_t bytes = 0u;
    // Account for caller-owned native pixels, input copy, blocks and scratch
    // before allocating. Encoder work itself is tiled and serialized.
    U64 paddedTexels = 0u;
    for (const auto& layer : _layers)
    {
        for (U8 mip = 0u; mip < mipCount(); ++mip)
        {
            const auto* data = layer.getMip(mip);
            if (data->_dimensions.z != 1u || data->_size > KTX_IMAGE_BYTE_LIMIT - bytes) { return false; }
            bytes += data->_size;
            paddedTexels += U64((U32(data->_dimensions.x) + 3u) & ~3u) * ((U32(data->_dimensions.y) + 3u) & ~3u);
        }
    }
    const U64 blocks = paddedTexels / 16u * (target == GFXImageFormat::BC4u ? 8u : 16u);
    const U64 bookkeeping = U64(_layers.size()) * mipCount() * (sizeof(KTXSubresource) * 3u + 128u);
    if (bytes * 3u + blocks * 2u + 48u * 1024u * 1024u + bookkeeping > KTX_IMAGE_BYTE_LIMIT) { return false; }
    for (U8 mip = 0u; mip < mipCount(); ++mip)
    {
        for (U32 layer = 0u; layer < _layers.size(); ++layer)
        {
            const auto* data = _layers[layer].getMip(mip);
            KTXSubresource resource;
            resource._mip = mip; resource._layer = layer / image._faces; resource._face = layer % image._faces;
            resource._width = data->_dimensions.x; resource._height = data->_dimensions.y;
            const auto* pixels = static_cast<const U8*>(data->data());
            resource._pixels = std::vector<U8>(pixels, pixels + data->_size);
            image._subresources.push_back(std::move(resource));
        }
    }
    KTXImage compressed;
    if (!CompressKTXImageForGPU(image, target, compressed, nullptr, cache)) { return false; }
    vector<ImageLayer> layers(_layers.size());
    for (const auto& resource : compressed._subresources)
    {
        const auto& pixels = std::get<std::vector<U8>>(resource._pixels);
        auto& layer = layers[resource._layer * image._faces + resource._face];
        // Block payloads are bytes, not width*height*components texels.
        DIVIDE_UNUSED(layer.allocateMip<U8>(pixels.data(), pixels.size(), resource._width, resource._height, 1u, 0u));
    }
    output._layers = std::move(layers);
    output._format = target; output._dataType = _dataType; output._sourceDataType = _sourceDataType;
    output._bpp = _bpp; output._isKTXImage = true; output._ktxSRGB = _ktxSRGB;
    output._ktxAlpha = _ktxAlpha; output._ktxAlphaInterpretation = _ktxAlphaInterpretation;
    output._ktxTextureType = _ktxTextureType; output._ktxLayerCount = _ktxLayerCount;
    output._hasDummyAlphaChannel = _hasDummyAlphaChannel;
    output._ignoreAlphaChannelTransparency = _ignoreAlphaChannelTransparency;
    return true;
}

bool ImageData::loadFromKTXImage(const KTXImage& image)
{
    if (!_layers.empty())
    {
        return false;
    }
    size_t payloadBytes = 0u;
    for (const auto& resource : image._subresources)
    {
        const size_t size = std::visit([](const auto& pixels)
        {
            return pixels.size() * sizeof(typename std::decay_t<decltype(pixels)>::value_type);
        }, resource._pixels);
        if (size > KTX_IMAGE_BYTE_LIMIT - payloadBytes)
        {
            return false;
        }
        payloadBytes += size;
    }
    // Caller-owned pixels, the classification copy and destination coexist.
    if (payloadBytes > KTX_IMAGE_BYTE_LIMIT / 3u)
    {
        return false;
    }
    KTXImage copy = image;
    return convertKTXImage(copy, payloadBytes, true);
}

bool ImageData::convertKTXImage(KTXImage& image, const size_t retainedBytes, const bool classifyAlpha)
{
    if (!_layers.empty() || image._width == 0u || image._height == 0u || image._depth == 0u ||
        image._layers == 0u || image._dimensions < 1u || image._dimensions > 3u ||
        to_base(image._componentType) > to_base(KTXComponentType::FLOAT_32) ||
        (image._faces != 1u && image._faces != 6u) ||
        (image._components != 1u && image._components != 2u && image._components != 4u) ||
        (image._dimensions == 1u && image._height != 1u) ||
        (image._dimensions != 3u && image._depth != 1u) ||
        (image._dimensions == 3u && (image._isArray || image._layers != 1u || image._faces != 1u)) ||
        (image._faces == 6u && (image._dimensions != 2u || image._width != image._height)) ||
        (!image._isArray && image._layers != 1u) ||
        image._mips == 0u || image._mips > 16u ||
        (image._srgb && (image._componentType != KTXComponentType::UNORM_8 || image._components != 4u)))
    {
        return false;
    }
    U16 largestDimension = std::max({image._width, image._height, image._depth});
    U8 maxMips = 1u;
    while (largestDimension > 1u)
    {
        largestDimension >>= 1u;
        ++maxMips;
    }
    if (image._mips > maxMips)
    {
        return false;
    }
    const TextureType textureType = image._faces == 6u
        ? image._isArray ? TextureType::TEXTURE_CUBE_ARRAY : TextureType::TEXTURE_CUBE_MAP
        : image._dimensions == 3u ? TextureType::TEXTURE_3D
        : image._dimensions == 1u ? image._isArray ? TextureType::TEXTURE_1D_ARRAY : TextureType::TEXTURE_1D
        : image._isArray ? TextureType::TEXTURE_2D_ARRAY : TextureType::TEXTURE_2D;
    const size_t flattenedLayers = size_t{image._layers} * image._faces;
    size_t payloadBytes = 0u;
    for (const auto& resource : image._subresources)
    {
        const size_t size = std::visit([](const auto& pixels)
        {
            return pixels.size() * sizeof(typename std::decay_t<decltype(pixels)>::value_type);
        }, resource._pixels);
        if (size > KTX_IMAGE_BYTE_LIMIT - payloadBytes)
        {
            return false;
        }
        payloadBytes += size;
    }
    // The native payload and the ImageData copy coexist with the caller's input.
    if (image._compressed || flattenedLayers > U16_MAX ||
        retainedBytes > KTX_IMAGE_BYTE_LIMIT ||
        payloadBytes > (KTX_IMAGE_BYTE_LIMIT - retainedBytes) / 2u)
    {
        Console::errorfn("KTX2 image exceeds the native allocation budget: {}", _name);
        return false;
    }
    if (classifyAlpha || image._alphaInterpretation == KTXAlphaInterpretation::UNKNOWN)
    {
        const auto interpretation = image._alphaInterpretation != KTXAlphaInterpretation::UNKNOWN ? image._alphaInterpretation
            : image._components != 4u ? KTXAlphaInterpretation::SYNTHETIC
            : ignoreAlphaChannelTransparency() ? KTXAlphaInterpretation::DATA : KTXAlphaInterpretation::OPACITY;
        if (!ClassifyKTXImageAlpha(image, interpretation))
        {
            Console::errorfn("KTX2 image requires explicit alpha import metadata: {}", _name);
            return false;
        }
        if (interpretation == KTXAlphaInterpretation::OPACITY && image._alpha._allZero)
        {
            Console::errorfn("KTX2 image has all-zero opacity alpha; classifying as transparent: {}", _name);
        }
    }
    vector<ImageLayer> layers(flattenedLayers);
    size_t resourceIndex = 0u;
    // The reader guarantees mip/layer/face/slice order. Join volume slices
    // directly into the destination so no additional full-volume copy is needed.
    for (U8 mip = 0u; mip < image._mips; ++mip)
    {
        const U16 depth = std::max<U16>(1u, image._depth >> mip);
        for (size_t l = 0u; l < flattenedLayers; ++l)
        {
            if (resourceIndex + depth > image._subresources.size())
            {
                return false;
            }
            const auto& first = image._subresources[resourceIndex];
            if (first._width != std::max<U16>(1u, image._width >> mip) ||
                first._height != std::max<U16>(1u, image._height >> mip) ||
                (image._componentType == KTXComponentType::UNORM_8 && !std::holds_alternative<std::vector<U8>>(first._pixels)) ||
                ((image._componentType == KTXComponentType::UNORM_16 || image._componentType == KTXComponentType::FLOAT_16) &&
                 !std::holds_alternative<std::vector<U16>>(first._pixels)) ||
                (image._componentType == KTXComponentType::FLOAT_32 && !std::holds_alternative<std::vector<F32>>(first._pixels)))
            {
                return false;
            }
            const size_t expectedSliceElements = size_t{first._width} * first._height * image._components;
            for (U16 z = 0u; z < depth; ++z)
            {
                const auto& resource = image._subresources[resourceIndex + z];
                if (resource._mip != mip || resource._layer != l / image._faces ||
                    resource._face != l % image._faces || resource._depthSlice != z ||
                    resource._width != first._width || resource._height != first._height ||
                    resource._pixels.index() != first._pixels.index() ||
                    !std::visit([expectedSliceElements](const auto& pixels) { return pixels.size() == expectedSliceElements; }, resource._pixels))
                {
                    return false;
                }
            }
            bool valid = true;
            std::visit([&](const auto& pixels)
            {
                using T = typename std::decay_t<decltype(pixels)>::value_type;
                const size_t sliceElements = size_t{first._width} * first._height * image._components;
                T* target = layers[l].allocateMip<T>(sliceElements * depth, first._width, first._height, depth, image._components);
                for (U16 z = 0u; z < depth; ++z)
                {
                    const auto& resource = image._subresources[resourceIndex + z];
                    const auto* slice = std::get_if<std::vector<T>>(&resource._pixels);
                    if (resource._mip != mip || resource._layer != l / image._faces ||
                        resource._face != l % image._faces || resource._depthSlice != z ||
                        resource._width != first._width || resource._height != first._height ||
                        slice == nullptr || slice->size() != sliceElements)
                    {
                        valid = false;
                        return;
                    }
                    std::memcpy(target + z * sliceElements, slice->data(), sliceElements * sizeof(T));
                }
                layers[l].getMip(mip)->_size = sliceElements * depth * sizeof(T);
            }, first._pixels);
            if (!valid)
            {
                return false;
            }
            resourceIndex += depth;
        }
    }
    if (resourceIndex != image._subresources.size())
    {
        return false;
    }
    _format = image._components == 1u ? GFXImageFormat::RED
            : image._components == 2u ? GFXImageFormat::RG : GFXImageFormat::RGBA;
    _dataType = image._componentType == KTXComponentType::UNORM_8 ? GFXDataFormat::UNSIGNED_BYTE
              : image._componentType == KTXComponentType::UNORM_16 ? GFXDataFormat::UNSIGNED_SHORT
              : image._componentType == KTXComponentType::FLOAT_16 ? GFXDataFormat::FLOAT_16 : GFXDataFormat::FLOAT_32;
    _sourceDataType = image._componentType == KTXComponentType::UNORM_8 ? SourceDataType::BYTE
                    : image._componentType == KTXComponentType::UNORM_16 ? SourceDataType::SHORT
                    : image._componentType == KTXComponentType::FLOAT_16 ? SourceDataType::HALF : SourceDataType::FLOAT;
    _bpp = image._components * (image._componentType == KTXComponentType::UNORM_8 ? 8u
                            : image._componentType == KTXComponentType::FLOAT_32 ? 32u : 16u);
    _layers = std::move(layers);
    _isKTXImage = true;
    _ktxSRGB = image._srgb;
    _ktxAlpha = image._alpha;
    _ktxAlphaInterpretation = image._alphaInterpretation;
    _ktxTextureType = textureType;
    _ktxLayerCount = image._layers;
    _hasDummyAlphaChannel = image._alphaInterpretation == KTXAlphaInterpretation::SYNTHETIC;
    return true;
}

bool ImageData::loadDDS_IL(const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name, const bool typedImport, const ImportOptions& options)
{

    LockGuard<SharedMutex> lock(s_imageLoadingMutex);
    string devilErrors;

    ILuint imageID = 0u;
    ilGenImages(1, &imageID);
    ilBindImage(imageID);
    SCOPE_EXIT
    {
        ilDeleteImage(imageID);
        if ( checkError( devilErrors ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
        }
    };
    
    if (checkError( devilErrors ))
    {
        Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
    }


    const string fullPath = (path / name).string();
    if (ilLoadImage(fullPath.c_str() ) == IL_FALSE)
    {
        if ( checkError( devilErrors ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
        }
        Console::errorfn(LOCALE_STR("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE"), fullPath, LOCALE_STR("ERROR_UNKNOWN") );
        return false;
    }
    
    // DevIL is a temporary DDS source codec. Its legacy topology heuristics
    // cannot safely identify arrays/cubes/volumes, so only an unambiguous 2D
    // source enters typed KTX here. Lists and other topologies retain the direct
    // import path below; neither path reads or writes DDS engine caches.
    ILinfo sourceInfo;
    iluGetImageInfo(&sourceInfo);
    std::array<U8, 128u> header{};
    std::ifstream sourceStream(fullPath.c_str(), std::ios::binary);
    const bool hasHeader = bool(sourceStream.read(reinterpret_cast<char*>(header.data()), header.size()));
    const auto headerWord = [&](const size_t offset)
    {
        return U32(header[offset]) | (U32(header[offset + 1u]) << 8u) |
               (U32(header[offset + 2u]) << 16u) | (U32(header[offset + 3u]) << 24u);
    };
    // DX10 topology is not reliably represented by DevIL; do not flatten it.
    const bool legacy2D = hasHeader && headerWord(0u) == 0x20534444u &&
                          headerWord(4u) == 124u && headerWord(76u) == 32u &&
                          headerWord(84u) != 0x30315844u &&
                          (headerWord(112u) & 0x20FE00u) == 0u && headerWord(24u) <= 1u;
    const ILint sourceChannels = ilGetInteger(IL_IMAGE_CHANNELS);
    const bool supportedType = sourceInfo.Type == IL_UNSIGNED_BYTE ||
                               sourceInfo.Type == IL_UNSIGNED_SHORT || sourceInfo.Type == IL_FLOAT;
    const bool supportedFormat = sourceInfo.Format == IL_LUMINANCE ||
                                 sourceInfo.Format == IL_LUMINANCE_ALPHA ||
                                 sourceInfo.Format == IL_RGB || sourceInfo.Format == IL_BGR ||
                                 sourceInfo.Format == IL_RGBA || sourceInfo.Format == IL_BGRA;
    const bool grayOpacity = sourceInfo.Format == IL_LUMINANCE_ALPHA && !ignoreAlphaChannelTransparency();
    // KTX's native sRGB contract is RGBA8. Never silently reinterpret a
    // higher-precision or packed-data sRGB request as linear.
    const bool supportedSRGB = !srgb || (sourceInfo.Type == IL_UNSIGNED_BYTE &&
                                       (sourceChannels >= 3 || grayOpacity));
    if (typedImport && legacy2D && _layers.empty() && supportedType && supportedFormat &&
        supportedSRGB && sourceChannels >= 1 && sourceChannels <= 4 &&
        ilGetInteger(IL_NUM_IMAGES) == 0 && ilGetInteger(IL_NUM_FACES) == 0 &&
        ilGetInteger(IL_IMAGE_CUBEFLAGS) == 0 && sourceInfo.Depth == 1 &&
        sourceInfo.Width > 0 && sourceInfo.Width <= U16_MAX &&
        sourceInfo.Height > 0 && sourceInfo.Height <= U16_MAX &&
        (refWidth == 0 || refHeight == 0 ||
         (sourceInfo.Width == refWidth && sourceInfo.Height == refHeight)))
    {
        KTXImage image;
        image._width = to_U16(sourceInfo.Width);
        image._height = to_U16(sourceInfo.Height);
        image._mips = to_U8(sourceInfo.NumMips + 1u);
        image._components = sourceChannels == 3 || grayOpacity ? 4u : to_U8(sourceChannels);
        image._componentType = sourceInfo.Type == IL_FLOAT ? KTXComponentType::FLOAT_32
                             : sourceInfo.Type == IL_UNSIGNED_SHORT ? KTXComponentType::UNORM_16
                             : KTXComponentType::UNORM_8;
        image._srgb = srgb && image._components == 4u && sourceInfo.Type == IL_UNSIGNED_BYTE;
        image._alphaInterpretation = sourceChannels == 3 ? KTXAlphaInterpretation::SYNTHETIC
            : ignoreAlphaChannelTransparency() ? KTXAlphaInterpretation::DATA
            : image._components != 4u ? KTXAlphaInterpretation::SYNTHETIC
            : KTXAlphaInterpretation::OPACITY;
        const ILenum format = sourceChannels == 4 ? IL_RGBA : sourceChannels == 3 ? IL_RGB
                            : sourceChannels == 2 ? IL_LUMINANCE_ALPHA : IL_LUMINANCE;
        if (sourceInfo.NumMips >= 16u || checkError(devilErrors))
        {
            return false;
        }
        constexpr size_t DDS_TYPED_BYTE_LIMIT = 32u * 1024u * 1024u;
        size_t bytes = 0u;
        for (U8 mip = 0u; mip < image._mips; ++mip)
        {
            ilBindImage(imageID);
            if (!ilActiveMipmap(mip))
            {
                return false;
            }
            const ILint width = ilGetInteger(IL_IMAGE_WIDTH);
            const ILint height = ilGetInteger(IL_IMAGE_HEIGHT);
            if (width != std::max(1, to_I32(image._width) >> mip) ||
                height != std::max(1, to_I32(image._height) >> mip) ||
                ilGetInteger(IL_IMAGE_DEPTH) != 1)
            {
                return false;
            }
            const size_t samples = to_size(width) * height * image._components;
            const size_t sampleBytes = sourceInfo.Type == IL_FLOAT ? sizeof(F32)
                                     : sourceInfo.Type == IL_UNSIGNED_SHORT ? sizeof(U16) : sizeof(U8);
            const size_t mipBytes = samples * sampleBytes;
            // Decoder storage, typed slices and loadFromKTXImage copies coexist.
            if (mipBytes > DDS_TYPED_BYTE_LIMIT - bytes)
            {
                return false;
            }
            bytes += mipBytes;
            KTXSubresource resource;
            resource._mip = mip;
            resource._width = to_U16(width);
            resource._height = to_U16(height);
            const auto copyPixels = [&]<typename T>()
            {
                std::vector<T> pixels(samples);
                // Same component type, only channel order / synthetic alpha
                // changes. No 8-bit staging for UNORM16 or floating-point DDS.
                const size_t sourceSamples = to_size(width) * height * sourceChannels;
                if (ilCopyPixels(0, 0, 0, width, height, 1, format, sourceInfo.Type, pixels.data()) !=
                    sourceSamples * sizeof(T))
                {
                    return false;
                }
                // Expand backwards in the final allocation: genuine gray alpha
                // is preserved, while RGB gets an explicit exact opaque alpha.
                if (grayOpacity || sourceChannels == 3)
                {
                    const T opaque = []()
                    {
                        if constexpr (std::is_floating_point_v<T>) { return T{1}; }
                        else { return std::numeric_limits<T>::max(); }
                    }();
                    for (size_t texel = to_size(width) * height; texel-- > 0u;)
                    {
                        const size_t source = texel * sourceChannels;
                        const T r = pixels[source];
                        const T g = grayOpacity ? r : pixels[source + 1u];
                        const T b = grayOpacity ? r : pixels[source + 2u];
                        const T a = grayOpacity ? pixels[source + 1u] : opaque;
                        pixels[texel * 4u] = r;
                        pixels[texel * 4u + 1u] = g;
                        pixels[texel * 4u + 2u] = b;
                        pixels[texel * 4u + 3u] = a;
                    }
                }
                const bool upperLeft = ilGetInteger(IL_IMAGE_ORIGIN) == IL_ORIGIN_UPPER_LEFT;
                if (upperLeft)
                {
                    const size_t rowSamples = to_size(width) * image._components;
                    for (size_t row = 0u; row < to_size(height) / 2u; ++row)
                    {
                        std::swap_ranges(pixels.begin() + row * rowSamples,
                                         pixels.begin() + (row + 1u) * rowSamples,
                                         pixels.begin() + (to_size(height) - 1u - row) * rowSamples);
                    }
                }
                resource._pixels = std::move(pixels);
                return true;
            };
            const bool copied = sourceInfo.Type == IL_FLOAT ? copyPixels.operator()<F32>()
                              : sourceInfo.Type == IL_UNSIGNED_SHORT ? copyPixels.operator()<U16>()
                              : copyPixels.operator()<U8>();
            if (!copied || checkError(devilErrors))
            {
                return false;
            }
            image._subresources.push_back(std::move(resource));
        }
        if (!options._skipMipMaps && image._mips == 1u)
        {
            // Native KTX uploads use explicit mip levels. Generate the chain
            // here rather than losing the legacy AUTO-mipmap behavior.
            U64 projectedBytes = 0u;
            U16 width = image._width;
            U16 height = image._height;
            U8 mipCount = 0u;
            do
            {
                projectedBytes += U64(width) * height * image._components *
                    (sourceInfo.Type == IL_FLOAT ? sizeof(F32)
                     : sourceInfo.Type == IL_UNSIGNED_SHORT ? sizeof(U16) : sizeof(U8));
                ++mipCount;
                if (width == 1u && height == 1u) { break; }
                width = std::max<U16>(1u, width >> 1u);
                height = std::max<U16>(1u, height >> 1u);
            } while (true);
            const U64 bookkeeping = U64(mipCount) * (sizeof(KTXSubresource) * 3u + 64u) + 4096u;
            // Include retained decoder/source pixels beyond the mip helper's
            // own budget, before it allocates any enlarged native chain.
            if (projectedBytes > DDS_TYPED_BYTE_LIMIT ||
                U64(bytes) * 2u + projectedBytes * 4u + bookkeeping >= KTX_IMAGE_BYTE_LIMIT)
            {
                return false;
            }
            KTXMipOptions mipOptions;
            mipOptions._semantic = options._isNormalMap && image._components == 4u ? KTXMipSemantic::NORMAL
                : image._srgb ? KTXMipSemantic::COLOR
                : image._components != 4u || !options._alphaChannelTransparency ? KTXMipSemantic::DATA
                : KTXMipSemantic::COLOR;
            mipOptions._preserveAlphaCoverage = image._alphaInterpretation == KTXAlphaInterpretation::OPACITY &&
                                                options._alphaChannelTransparency && !options._isNormalMap;
            if (!GenerateKTXMipmaps(image, mipOptions))
            {
                return false;
            }
        }
        return loadFromKTXImage(image);
    }

    const ILint dxtFormat = ilGetInteger(IL_DXTC_DATA_FORMAT);
    const bool compressed = dxtFormat == IL_DXT1 ||
                            dxtFormat == IL_DXT1A||
                            dxtFormat == IL_DXT2 ||
                            dxtFormat == IL_DXT3 ||
                            dxtFormat == IL_DXT4 ||
                            dxtFormat == IL_DXT5;


    const auto flipActiveMip = [&]() {
        if (!s_useUpperLeftOrigin)
        {
            if (compressed)
            {
                ilFlipSurfaceDxtcData();
            } 
            else
            {
                iluFlipImage();
            }

            if ( checkError( devilErrors ) )
            {
                Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
            }
        }
    };

    ILinfo imageInfo;
    iluGetImageInfo(&imageInfo);
    if ( checkError( devilErrors ) )
    {
        Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
    }

    // Avoid, double, longs, unsigned ints, etc
    if (imageInfo.Type != IL_BYTE && imageInfo.Type != IL_UNSIGNED_BYTE &&
        imageInfo.Type != IL_FLOAT &&
        imageInfo.Type != IL_UNSIGNED_SHORT && imageInfo.Type != IL_SHORT) {
        ilConvertImage(imageInfo.Format, IL_FLOAT);
        imageInfo.Type = IL_FLOAT;
        if ( checkError( devilErrors ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
        }
    }

    ILint channelCount = ilGetInteger(IL_IMAGE_CHANNELS);
    // We don't support paletted images (or RGB8 in Vulkan)
    if (imageInfo.Format == IL_COLOUR_INDEX || (channelCount == 3 && imageInfo.Type == IL_UNSIGNED_BYTE) )
    {
        Console::warnfn( LOCALE_STR( "WARN_IMAGETOOLS_RGB_FORMAT" ), fullPath );

        ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
        imageInfo.Format = IL_RGBA;
        imageInfo.Type = IL_UNSIGNED_BYTE;
        channelCount = 4u;
        if ( checkError( devilErrors ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
        }
    }

    size_t storageSizeFactor = 1u;
    switch (imageInfo.Type) {
        case IL_BYTE:
            _dataType = GFXDataFormat::SIGNED_BYTE;
            _sourceDataType = SourceDataType::BYTE;
            break;
        case IL_UNSIGNED_BYTE:
            _dataType = GFXDataFormat::UNSIGNED_BYTE;
            _sourceDataType = SourceDataType::BYTE;
            break;
        case IL_SHORT:
            _dataType = GFXDataFormat::SIGNED_SHORT;
            _sourceDataType = SourceDataType::SHORT;
            storageSizeFactor = 2;
            break;
        case IL_UNSIGNED_SHORT:
            _dataType = GFXDataFormat::UNSIGNED_SHORT;
            _sourceDataType = SourceDataType::SHORT;
            storageSizeFactor = 2;
            break;
        case IL_FLOAT:
            _dataType = GFXDataFormat::FLOAT_32;
            _sourceDataType = SourceDataType::FLOAT;
            storageSizeFactor = 4;
            break;

        default: return false;
    };

    // Resize if needed
    if (refWidth != 0 && refHeight != 0 && (imageInfo.Width != refWidth || imageInfo.Height != refHeight))
    {
        if (iluScale(refWidth, refHeight, imageInfo.Depth))
        {
            imageInfo.Width = refWidth;
            imageInfo.Height = refHeight;
        }
        if ( checkError( devilErrors ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
        }
    }

    if (compressed)
    {
        switch (dxtFormat)
        {
            case IL_DXT1:
            {
                _format = GFXImageFormat::DXT1_RGBA; //dummy alpha channel
                _hasDummyAlphaChannel = true;
                _ignoreAlphaChannelTransparency = true;
            }  break;
            case IL_DXT1A:
            {
                _format = GFXImageFormat::DXT1_RGBA;
            } break;
            case IL_DXT3:
            {
                _format = GFXImageFormat::DXT3_RGBA;
            } break;
            case IL_DXT5:
            {
                _format = GFXImageFormat::DXT5_RGBA;
            } break;
            default:
            {
                DIVIDE_UNEXPECTED_CALL();
                return false;
            }
        }
    }
    else
    {
        switch (imageInfo.Format) 
        {
            default:
            case IL_COLOUR_INDEX: 
                DIVIDE_UNEXPECTED_CALL();
                return false;
            case IL_ALPHA:
            case IL_LUMINANCE:
                _format = GFXImageFormat::RED;
                break;
            case IL_LUMINANCE_ALPHA:
                _format = GFXImageFormat::RG;
                break;
            case IL_RGB:
            {
                _format = GFXImageFormat::RGB;
                _hasDummyAlphaChannel = true;
                _ignoreAlphaChannelTransparency = true;
            } break;
            case IL_BGR:
            {
                _format = GFXImageFormat::BGR;
                _hasDummyAlphaChannel = true;
                _ignoreAlphaChannelTransparency = true;
            } break;
            case IL_RGBA:
                _format = GFXImageFormat::RGBA;
                break;
            case IL_BGRA:
                _format = GFXImageFormat::BGRA;
                break;
        }
    }

    const ILint numImages = ilGetInteger(IL_NUM_IMAGES) + 1;
    // ^^^^^^^^^^^^^ Querying for IL_NUM_IMAGES returns the number of images following the current one. Add 1 for the right image count!
    const bool isCube = ilGetInteger(IL_IMAGE_CUBEFLAGS) != 0 || numImages % 6 == 0;
    
    ILint numFaces = ilGetInteger(IL_NUM_FACES) + 1;

    _bpp = imageInfo.Bpp * 8;

    _layers.reserve(_layers.size() + to_size(numFaces) * numImages);
    for (ILint image = 0; image < numImages; ++image) {
        // cube faces within DevIL philosophy are organized like this:
        //
        //   image -> 1st face -> face index 0
        //   face1 -> 2nd face -> face index 1
        //   ...
        //   face5 -> 6th face -> face index 5
        numFaces = ilGetInteger(IL_NUM_FACES) + 1;

        for (I32 f = 0; f < numFaces; ++f) {
            // need to juggle with the faces to get them aligned with
            // how OpenGL expects cube faces ...
            const I32 face = determineFace(f, true, isCube);
            ImageLayer& layer = _layers.emplace_back();

            for (ILuint m = 0u; m <= imageInfo.NumMips; ++m)
            {
                // DevIL frequently loses track of the current state
                ilBindImage(imageID);
                ilActiveImage(image);
                ilActiveFace(face);
                ilActiveMipmap(m);
                flipActiveMip();
                if ( checkError( devilErrors ) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
                }
                const ILint width  = ilGetInteger(IL_IMAGE_WIDTH);
                const ILint height = ilGetInteger(IL_IMAGE_HEIGHT);
                const ILint depth  = ilGetInteger(IL_IMAGE_DEPTH);
                const ILuint size   = compressed ? ilGetDXTCData( nullptr, 0, dxtFormat ) 
                                                 : static_cast<ILuint>(ilGetInteger( IL_IMAGE_SIZE_OF_DATA ));

                if ( checkError( devilErrors ) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
                }

                Byte* data = layer.allocateMip<Byte>(size,
                                                     to_U16(width),
                                                     to_U16(height),
                                                     to_U16(depth),
                                                     compressed ? 0  // 0 here means that our calculated size will be 0, thus our specified size will always be used
                                                                : to_U8(channelCount * storageSizeFactor)); //For short and float type data we need to increase the available storage a bit (channel count assumes a Byte per component here)
                if (compressed)
                {
                    ilGetDXTCData(data, size, dxtFormat);
                }
                else
                {
                    memcpy(data, ilGetData(), size);
                }
                if ( checkError( devilErrors ) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
                }


                if ( compressed && image == 0 && face == 0 && m == 0 )
                {
                    _decompressedData.resize( to_size( imageInfo.Width ) * imageInfo.Height * imageInfo.Depth * 4 );
                    ilCopyPixels( 0, 0, 0, imageInfo.Width, imageInfo.Height, imageInfo.Depth, IL_RGBA, IL_UNSIGNED_BYTE, _decompressedData.data() );
                    if ( checkError( devilErrors ) )
                    {
                        Console::errorfn( LOCALE_STR( "ERROR_IMAGE_TOOLS_DEVIL_ERROR" ), devilErrors );
                    }
                }
            }
        }
    }

    return true;
}

UColour4 ImageData::getColour(const I32 x, const I32 y, [[maybe_unused]] U32 layer, const U8 mipLevel) const
{
    UColour4 returnColour;
    getColour(x, y, returnColour.r, returnColour.g, returnColour.b, returnColour.a, mipLevel);
    return returnColour;
}

namespace
{
    [[nodiscard]] FORCE_INLINE U8 F32ToU8Colour(const F32 val) noexcept { return to_U8((val / F32_MAX) * 255); }
    [[nodiscard]] FORCE_INLINE U8 U32ToU8Colour(const U32 val) noexcept { return to_U8(CLAMPED(val, 0u, 255u)); }
    [[nodiscard]] FORCE_INLINE U8 U16ToU8Colour(const U16 val) noexcept { return to_U8((val / ( U16_MAX * 1.f )) * 255); }
    [[nodiscard]] FORCE_INLINE U8 HalfToU8Colour(const glm::detail::hdata val) noexcept { return F32ToU8Colour(glm::detail::toFloat32(val)); }
    [[nodiscard]] FORCE_INLINE U8 U8ToU8Colour(const U8 val) noexcept { return val; }
};

void ImageData::getColourComponent(const I32 x, const I32 y, const U8 comp, U8& c, const U32 layer, const U8 mipLevel) const
{
    assert(comp >= 0 && comp < 4);
    assert(!IsCompressed(_format) || mipLevel == 0);
    assert(_layers.size() > layer);

    if (!HasAlphaChannel(_format) && comp == 3)
    {
        c = U8_MAX;
        return;
    }
    if (_isKTXImage && comp >= NumChannels(_format))
    {
        c = 0u;
        return;
    }

    const LayerData* mip = _layers[layer].getMip(mipLevel);

    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = IsCompressed(_format) ? 4 : _isKTXImage ? NumChannels(_format) : _bpp / 8;
    const I32 idx = ((y * mip->_dimensions.width + x) * pixelStride) + comp;
    if (IsCompressed(_format))
    {
        // Decompressed data is always UByte-RGBA
        c = _decompressedData[idx];
    }
    else
    {
        switch(_sourceDataType )
        {
            case SourceDataType::BYTE : 
            {
                c = U8ToU8Colour( static_cast<const U8*>(mip->data())[idx] );
            } break;
            case SourceDataType::SHORT:
            {
                c = U16ToU8Colour( static_cast<const U16*>(mip->data())[idx] );
            } break;
            case SourceDataType::HALF:
            {
                const auto value = static_cast<const glm::detail::hdata*>(mip->data())[idx];
                const F32 normalized = glm::detail::toFloat32(value);
                c = _isKTXImage ? to_U8(std::isfinite(normalized) ? std::clamp(normalized, 0.f, 1.f) * 255.f : 0.f)
                               : HalfToU8Colour(value);
            } break;
            case SourceDataType::FLOAT:
            {
                const F32 value = static_cast<const F32*>(mip->data())[idx];
                c = _isKTXImage ? to_U8(std::isfinite(value) ? std::clamp(value, 0.f, 1.f) * 255.f : 0.f)
                               : F32ToU8Colour(value);
            } break;
            case SourceDataType::UINT:
            {
                c = U32ToU8Colour( static_cast<const U32*>(mip->data())[idx] );
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }
}

void ImageData::getColour(const I32 x, const I32 y, U8& r, U8& g, U8& b, U8& a, const U32 layer, const U8 mipLevel) const
{
    if (_isKTXImage)
    {
        getColourComponent(x, y, 0u, r, layer, mipLevel);
        getColourComponent(x, y, 1u, g, layer, mipLevel);
        getColourComponent(x, y, 2u, b, layer, mipLevel);
        getColourComponent(x, y, 3u, a, layer, mipLevel);
        return;
    }
    assert(!IsCompressed(_format) || mipLevel == 0);
    assert(_layers.size() > layer);

    const LayerData* mip = _layers[layer].getMip(mipLevel);
    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = IsCompressed(_format) ? 4 : _bpp / 8;
    const I32 idx = ((y * mip->_dimensions.width + x) * pixelStride);

    if (IsCompressed(_format))
    {
        // Decompressed data is always UByte-RGBA
        const U8* src = _decompressedData.data();
        r = src[idx + 0];
        g = src[idx + 1];
        b = src[idx + 2]; 
        a = HasAlphaChannel(_format) ? src[idx + 3] : 255;
    }
    else
    {
        switch ( _sourceDataType )
        {
            case SourceDataType::BYTE:
            {
                const U8* src = static_cast<U8*>(mip->data());
                r = U8ToU8Colour( src[idx + 0] );
                g = U8ToU8Colour( src[idx + 1] );
                b = U8ToU8Colour( src[idx + 2] );
                a = HasAlphaChannel( _format ) ? U8ToU8Colour( src[idx + 3] ) : 255;
            } break;
            case SourceDataType::SHORT:
            {
                const U16* src = static_cast<U16*>(mip->data());
                r = U16ToU8Colour( src[idx + 0] );
                g = U16ToU8Colour( src[idx + 1] );
                b = U16ToU8Colour( src[idx + 2] );
                a = HasAlphaChannel( _format ) ? U16ToU8Colour( src[idx + 3] ) : 255;
            } break;
            case SourceDataType::HALF:
            {
                const U8* src = static_cast<U8*>(mip->data());
                r = HalfToU8Colour( src[idx + 0] );
                g = HalfToU8Colour( src[idx + 1] );
                b = HalfToU8Colour( src[idx + 2] );
                a = HasAlphaChannel( _format ) ? HalfToU8Colour( src[idx + 3] ) : 255;
            } break;
            case SourceDataType::FLOAT:
            {
                const F32* src = static_cast<F32*>(mip->data());
                r = F32ToU8Colour( src[idx + 0] );
                g = F32ToU8Colour( src[idx + 1] );
                b = F32ToU8Colour( src[idx + 2] );
                a = HasAlphaChannel( _format ) ? F32ToU8Colour( src[idx + 3] ) : 255;
            } break;
            case SourceDataType::UINT:
            {
                const U8* src = static_cast<U8*>(mip->data());
                r = U32ToU8Colour( src[idx + 0] );
                g = U32ToU8Colour( src[idx + 1] );
                b = U32ToU8Colour( src[idx + 2] );
                a = HasAlphaChannel( _format ) ? U32ToU8Colour( src[idx + 3] ) : 255;
            } break;
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }
}

namespace 
{
    template<size_t src_comp_num, bool source_is_BGR>
    void flipAndConvertToRGB8( const Byte* sourceBuffer, Byte* destBuffer, const U16 width, const U16 height, const U8 bytesPerPixel )
    {
        // The only reason this templated function exist is to collapse conversion to a single loop while also avoiding expensive if checks on the number of components in the middle of it.
        // The template allows us to use the compile-time constexpr check for number of source components.

        for ( I32 j = height - 1; j >= 0; --j )
        {
            const Byte* src = sourceBuffer + (bytesPerPixel * width * j);
                  Byte* dst = destBuffer   + (3 * width * (height - 1 - j));

            for (U16 i = 0u; i < width; ++i)
            {
                *dst++ = *(src + (source_is_BGR ? 2 : 0));

                if constexpr ( src_comp_num > 1 )
                {
                    *dst++ = *(src + 1);

                    if constexpr (src_comp_num > 2 )
                    {
                        *dst++ = *(src + (source_is_BGR ? 0 : 2));
                    }
                }

                src += bytesPerPixel;
            }
        }
    }
}

bool SaveImage(const ResourcePath& filename, const U16 width, const U16 height, const U8 numberOfComponents, const U8 bytesPerPixel, const bool sourceIsBGR, const Byte* imageData, const SaveImageFormat format) {
    // Flip data upside-down and convert to RGB8
    if ( width == 0u || height == 0 || numberOfComponents == 0 )
    {
        return false;
    }

    vector<Byte> pix( width * height * 3 );
    Byte* dest = pix.data();

    switch (numberOfComponents )
    {
        //Granted, this is not pretty, but we have:
        //    A) a limited number of components to handle (R/RG/RGB/RGBA)
        //    B) way faster&cleaner to use the template than an if-check or switch and copy-pasting the loop a few times.

        case 1: sourceIsBGR ? flipAndConvertToRGB8<1, true>( imageData, dest, width, height, bytesPerPixel ) : flipAndConvertToRGB8<1, false>( imageData, dest, width, height, bytesPerPixel ); break;
        case 2: sourceIsBGR ? flipAndConvertToRGB8<2, true>( imageData, dest, width, height, bytesPerPixel ) : flipAndConvertToRGB8<2, false>( imageData, dest, width, height, bytesPerPixel ); break;
        case 3: sourceIsBGR ? flipAndConvertToRGB8<3, true>( imageData, dest, width, height, bytesPerPixel ) : flipAndConvertToRGB8<3, false>( imageData, dest, width, height, bytesPerPixel ); break;
        case 4: sourceIsBGR ? flipAndConvertToRGB8<4, true>( imageData, dest, width, height, bytesPerPixel ) : flipAndConvertToRGB8<4, false>( imageData, dest, width, height, bytesPerPixel ); break;
        default: DIVIDE_UNEXPECTED_CALL(); return false;
    }

    switch (format)
    {
        case SaveImageFormat::PNG: return stbi_write_png(filename.string().c_str(), width, height, 3, pix.data(), width * 3 * sizeof(Byte)) == 1;
        case SaveImageFormat::BMP: return stbi_write_bmp(filename.string().c_str(), width, height, 3, pix.data())                           == 1;
        case SaveImageFormat::TGA: return stbi_write_tga(filename.string().c_str(), width, height, 3, pix.data())                           == 1;
        case SaveImageFormat::JPG: return stbi_write_jpg(filename.string().c_str(), width, height, 3, pix.data(), 85)                       == 1;
        default: DIVIDE_UNEXPECTED_CALL(); break;
    }

    return false;
}

bool SaveImageHDR(const ResourcePath& filename, const U16 width, const U16 height, const U8 numberOfComponents, [[maybe_unused]] const U8 bytesPerPixel, [[maybe_unused]] const bool sourceIsBGR, const F32* imageData)
{
    return stbi_write_hdr(filename.string().c_str(), width, height, numberOfComponents, imageData) == 1;
}
}  // namespace Divide::ImageTools
