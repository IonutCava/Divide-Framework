#include "Headers/ImageTools.h"

#include "Core/Headers/StringHelper.h"
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

#if !defined(IS_MACOS_BUILD)
#include <nvtt/nvtt.h>
#endif //!IS_MACOS_BUILD

namespace Divide::ImageTools
{
    constexpr bool g_KeepDevILDDSCompatibility = true;

#if !defined(IS_MACOS_BUILD)

namespace nvttHelpers
{
    struct ErrorHandler : public nvtt::ErrorHandler
    {
        void error(nvtt::Error e) override
        {
            constexpr std::array<const char*, static_cast<size_t>(nvtt::Error::Error_Count)> nvttErrors
            {{
                "UNKNOWN",
                "INVALID_INPUT",
                "UNSUPPORTED_FEATURE",
                "CUDA_ERROR",
                "FILE_OPEN",
                "FILE_WRITE",
                "UNSUPPORTED_OUTPUT_FORMAT",
            }};

            Console::errorfn(LOCALE_STR("ERROR_IMAGE_TOOLS_NVT_ERROR"), nvttErrors[to_size(e)]);
        }
    };

    struct OutputHandler : public nvtt::OutputHandler
    {
        struct MipMapData
        {
            vector<U8> _pixelData;
            I32 _width{0};
            I32 _height{0};
            I32 _depth{0};
        };

        vector<MipMapData> _mipmaps;
        I32 _currentMipLevel{0};
        U8 _numComponents{0u};
        nvtt::Format _format{nvtt::Format::Format_BC1};
        bool _discardAlpha{false};

        OutputHandler(nvtt::Format format, bool discardAlpha, const U8 numComponents)
            : _numComponents(numComponents)
            , _format(format)
            , _discardAlpha( discardAlpha )
        {
        }

        /// Indicate the start of a new compressed image that's part of the final texture.
        virtual void beginImage(int size, int width, int height, int depth, [[maybe_unused]] int face, int miplevel) override
        {
            MipMapData& data = _mipmaps.push_back();
            data._width = width;
            data._height = height;
            data._depth = depth;
            data._pixelData.resize(size);
            _currentMipLevel = miplevel;
        }

        virtual void endImage() override
        {
        }

        /// Output data. Compressed data is output as soon as it's generated to minimize memory allocations.
        virtual bool writeData(const void* data, int size) override
        {
            // Copy mipmap data
            memcpy(_mipmaps[_currentMipLevel]._pixelData.data(), data, size);
            return true;
        }
    };

    [[nodiscard]] static bool isBC1n(const nvtt::Format format, const bool isNormalMap) noexcept
    {
        return isNormalMap && format == nvtt::Format_BC1;
    }

    [[nodiscard]] static nvtt::Format getNVTTFormat(const ImageOutputFormat outputFormat, const bool isNormalMap, const bool hasAlpha, const bool isGreyscale) noexcept
    {
        assert(outputFormat != ImageOutputFormat::COUNT);

        if constexpr(g_KeepDevILDDSCompatibility)
        {
            return isNormalMap ? nvtt::Format::Format_BC3n : hasAlpha ? nvtt::Format::Format_BC3 : nvtt::Format::Format_BC1;
        }
        else
        {
            if (outputFormat != ImageOutputFormat::AUTO)
            {
                switch (outputFormat)
                {
                    case ImageOutputFormat::BC1:      return nvtt::Format::Format_BC1;
                    case ImageOutputFormat::BC1a:     return nvtt::Format::Format_BC1a;
                    case ImageOutputFormat::BC2:      return nvtt::Format::Format_BC2;
                    case ImageOutputFormat::BC3:      return isNormalMap ? nvtt::Format::Format_BC3n : nvtt::Format::Format_BC3;
                    case ImageOutputFormat::BC4:      return nvtt::Format::Format_BC4;
                    case ImageOutputFormat::BC5:      return nvtt::Format::Format_BC5;
                    case ImageOutputFormat::BC6:      return nvtt::Format::Format_BC6;
                    case ImageOutputFormat::BC7:      return nvtt::Format::Format_BC7;

                    case ImageOutputFormat::BC3_RGBM: 
                        /*return nvtt::Format::Format_BC3_RGBM;*/ //Not supported
                        Console::errorfn(LOCALE_STR("ERROR_IMAGE_TOOLS_NVT_ERROR"), "BC3_RGBM");
                    default: break;
                };
            }

            return isNormalMap ? nvtt::Format::Format_BC5 : isGreyscale ? nvtt::Format::Format_BC4 : nvtt::Format::Format_BC7;
        }
    }

    [[nodiscard]] static nvtt::MipmapFilter getNVTTMipFilter(const MipMapFilter filter) noexcept
    {
        switch (filter)
        {
            case MipMapFilter::BOX     : return nvtt::MipmapFilter_Box;
            case MipMapFilter::TRIANGLE: return nvtt::MipmapFilter_Triangle;
            case MipMapFilter::KAISER  : return nvtt::MipmapFilter_Kaiser;
            default: break;
        }

        return nvtt::MipmapFilter_Box;
    }
}; // namespace nvttHelpers

#endif //!IS_MACOS_BUILD

namespace
{
    Mutex s_imageCompressionMutex;
    SharedMutex s_imageLoadingMutex;
    bool s_useUpperLeftOrigin = false;

    static eastl::set<U64> s_fileLoadingHashes;

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

bool ImageData::loadFromMemory(const Byte* data, const size_t size, const U16 width, const U16 height, const U16 depth, const U8 numComponents)
{
    ImageLayer& layer = _layers.emplace_back();
    return layer.allocateMip(data, size, width, height, depth, numComponents);
}

bool ImageData::loadFromFile(PlatformContext& context, const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name)
{
    ImportOptions options{};
    options._useDDSCache = false;
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

bool ImageData::loadFromFile(PlatformContext& context, const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name, ImportOptions& options, const bool isRetry)
{
    _path = path;
    _name = name;

    ignoreAlphaChannelTransparency(!options._alphaChannelTransparency);

    // We can handle DDS files directly in a very FATAL way
    if (hasExtension(_name, Paths::Textures::g_ddsExtension))
    {
        _loadingData._loadedDDSData = loadDDS_NVTT(srgb, refWidth, refHeight, _path, _name);
        if ( _loadingData._loadedDDSData ) [[likely]]
        {
            ++_loadingData._fileIndex;

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

    bool useCache = Texture::UseTextureDDSCache() && options._useDDSCache;

    if ( _loadingData._fileIndex == 0u && _sourceDataType != SourceDataType::BYTE)
    {
        options._useDDSCache = false;
        useCache = false;
    }

#if defined(IS_MACOS_BUILD)
    const bool createDDS = false;
#else
    const bool createDDS = useCache || _loadingData._createdDDSData;
#endif

    // We either want to convert or we already have DDS faces/layers
    if ( _loadingData._fileIndex > 0u)
    {
        useCache = _loadingData._loadedDDSData;
        options._useDDSCache = useCache;
        options._waitForDDSConversion = useCache;
    }

    if ( createDDS )
    {
#if defined(IS_MACOS_BUILD)
        _loadingData._createdDDSData = false;
#else //IS_MACOS_BUILD
        _loadingData._createdDDSData = true;

        const ResourcePath cachePath = Paths::Textures::g_metadataLocation / _path;

        // Try and save regular images to DDS for better compression next time
        DIVIDE_EXPECTED_CALL( createDirectory(cachePath) == FileError::NONE );

        const string cacheFileName = Util::StringFormat("{}.{}", _name, Paths::Textures::g_ddsExtension);

        const ResourcePath cacheFilePath = cachePath / cacheFileName;

        Task* ddsConversionTask = nullptr;
        if ( !fileExists(ResourcePath{ cacheFilePath }) )
        {
            ddsConversionTask = CreateTask( [fullPath, cacheFilePath, cachePath, cacheFileName, options]( const Task& )
            {
                const size_t cacheFilePathHash = _ID(cacheFilePath.string());
                do
                {
                    {
                        UniqueLock<Mutex> lock(s_imageCompressionMutex);

                        if (s_fileLoadingHashes.insert(cacheFilePathHash).second)
                        {
                            break;
                        }
                    }
                    std::this_thread::yield();
                } while (true);

                bool success = false;

                const string cacheFileNameTemp = cacheFileName + ".tmp";

                SCOPE_EXIT
                {
                    DIVIDE_EXPECTED_CALL(success && moveFile(cachePath, cacheFileNameTemp, cachePath, cacheFileName) == FileError::NONE);

                    UniqueLock<Mutex> lock(s_imageCompressionMutex);
                    DIVIDE_EXPECTED_CALL(s_fileLoadingHashes.erase(cacheFilePathHash) > 0u);
                };

                {
                    // Check if the file was created between the check and the task actually starting
                    if (fileExists(ResourcePath{ cacheFilePath }))
                    {
                        return;
                    }
                }

                nvtt::Context context;
                context.enableCudaAcceleration(true);

                nvtt::Surface image;
                bool hasAlpha = false;
                if (image.load(fullPath.c_str(), &hasAlpha))
                {
                    image.setNormalMap(options._isNormalMap);

                    constexpr bool isGreyScale = false;
                    const nvtt::Format outputFormat = nvttHelpers::getNVTTFormat(options._outputFormat, options._isNormalMap, hasAlpha, isGreyScale);

                    // Setup compression options.
                    nvtt::CompressionOptions compressionOptions;
                    compressionOptions.setFormat(outputFormat);
                    compressionOptions.setQuality(options._fastCompression ? nvtt::Quality::Quality_Fastest : nvtt::Quality::Quality_Normal);
                    if (nvttHelpers::isBC1n(outputFormat, options._isNormalMap))
                    {
                        compressionOptions.setColorWeights(1, 1, 0);
                    }

                    const ResourcePath cacheFilePathTemp = cachePath / cacheFileNameTemp;

                    nvttHelpers::ErrorHandler errorHandler;
                    nvtt::OutputOptions outputOptions;
                    outputOptions.setFileName(cacheFilePathTemp.string().c_str());
                    outputOptions.setErrorHandler(&errorHandler);
                    outputOptions.setContainer(nvtt::Container_DDS);
                    outputOptions.setSrgbFlag(options._outputSRGB);

                    switch (outputFormat)
                    {
                        case nvtt::Format_BC6:
                            compressionOptions.setPixelType(nvtt::PixelType_UnsignedFloat);
                            outputOptions.setContainer(nvtt::Container_DDS10);
                            break;
                        case nvtt::Format_BC7:
                            outputOptions.setContainer(nvtt::Container_DDS10);
                            break;
                        case nvtt::Format_BC2:
                            compressionOptions.setQuantization(/*color dithering*/false, /*alpha dithering*/true, /*binary alpha*/false); // Dither alpha when using BC2.
                            break;
                        case nvtt::Format_BC1a:
                            compressionOptions.setQuantization(/*color dithering*/false, /*alpha dithering*/true, /*binary alpha*/true, 127); // Binary alpha when using BC1a.
                            break;
                        default:
                            break;
                    }

                    DIVIDE_EXPECTED_CALL( context.outputHeader(image, image.countMipmaps(), compressionOptions, outputOptions) );
                    
                    F32 coverage = 0.f;
                    if (options._isNormalMap)
                    {
                        image.normalizeNormalMap();
                    }
                    else if (hasAlpha && options._alphaChannelTransparency)
                    {
                        coverage = image.alphaTestCoverage(Config::ALPHA_DISCARD_THRESHOLD);
                        image.setAlphaMode(nvtt::AlphaMode::AlphaMode_Transparency);
                    }

                    DIVIDE_EXPECTED_CALL( context.compress(image, 0, 0, compressionOptions, outputOptions) );

                    // Build and output mipmaps.
                    if (!options._skipMipMaps)
                    {
                        I32 m = 1;
                        while (image.buildNextMipmap(nvttHelpers::getNVTTMipFilter(options._mipFilter)))
                        {
                            if (options._isNormalMap)
                            {
                                image.normalizeNormalMap();
                            }
                            else if (hasAlpha && options._alphaChannelTransparency)
                            {
                                image.scaleAlphaToCoverage(coverage, Config::ALPHA_DISCARD_THRESHOLD);
                            }

                            DIVIDE_EXPECTED_CALL( context.compress(image, 0, m, compressionOptions, outputOptions) );

                            m++;
                        }
                    }
                    success = true;
                }
            });

            Start
            (
                *ddsConversionTask, 
                context.taskPool( TaskPoolType::HIGH_PRIORITY ),
                options._waitForDDSConversion ? TaskPriority::REALTIME
                                              : TaskPriority::DONT_CARE 
            );
        }

        // We want to load a DDS cached file if available
        if ( useCache )
        {
            // We are either creating it now ...
            if ( ddsConversionTask != nullptr )
            {
                // ... in which case, see if we need to wait for it
                if (options._waitForDDSConversion || _loadingData._loadedDDSData) 
                {
                    Wait(*ddsConversionTask, context.taskPool( TaskPoolType::HIGH_PRIORITY ));
                    ddsConversionTask = nullptr;
                }
            }
            // Or we assume it's already created, so just check that it exists
            else if ( fileExists( cacheFilePath ) )
            {
                // Try and load it normally...
                if (loadDDS_NVTT( srgb, refWidth, refHeight, cachePath, cacheFileName ))
                {
                    _loadingData._loadedDDSData = true;
                    ++_loadingData._fileIndex;
                    return true;
                }
                // But if we fail, maybe the file is corrupt since it passed the existence check?
                else
                {
                    // Warn about it and retry only once
                    Console::errorfn(LOCALE_STR("ERROR_IMAGE_TOOLS_DDS_LOAD_ERROR"), cachePath / cacheFileName );
                    if (!isRetry)
                    {
                        // Delete whatever we tried to load and fail
                        if (deleteFile( cachePath, cacheFileName ) != FileError::NONE)
                        {
                            Console::errorfn(LOCALE_STR("ERROR_IMAGE_TOOLS_DDS_DELETE_ERROR"), cachePath / cacheFileName );
                        }

                        // And try again
                        return loadFromFile( context, srgb, refWidth, refHeight, path, name, options, true );
                    }
                }
            }
        }
#       endif //!IS_MACOS_BUILD
    }

    // At this point, we either don't care about DDS versions, or we utterly failed to create a DDS cached file, so just load the original image
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

    ++_loadingData._fileIndex;
    return ret;
}

bool ImageData::loadDDS_NVTT([[maybe_unused]] const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name)
{
    //ToDo: Use a better DDS loader
    return loadDDS_IL(srgb, refWidth, refHeight, path, name);
}

bool ImageData::loadDDS_IL([[maybe_unused]] const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const std::string_view name)
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

    const LayerData* mip = _layers[layer].getMip(mipLevel);

    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = IsCompressed(_format) ? 4 : _bpp / 8;
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
                c = HalfToU8Colour( static_cast<const glm::detail::hdata*>(mip->data())[idx] );
            } break;
            case SourceDataType::FLOAT:
            {
                c = F32ToU8Colour( static_cast<const F32*>(mip->data())[idx] );
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

