#include "stdafx.h"

#include "Headers/ImageTools.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#define STB_IMAGE_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable:4505) //unreferenced local function has been removed
#pragma warning(disable:4189) //local variable is initialized but not referenced
#pragma warning(disable:4244) //conversion from X to Y possible loss of data
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define IL_STATIC_LIB
#undef _UNICODE
#include <IL/il.h>
#include <IL/ilu.h>
//#include <IL/ilut.h>
#pragma warning(pop)

#include <nvtt/include/nvtt/nvtt.h>

namespace Divide::ImageTools {
    constexpr bool g_KeepDevILDDSCompatibility = true;

namespace nvttHelpers {
    struct ErrorHandler : public nvtt::ErrorHandler {
        void error(nvtt::Error e) override {
            switch (e) {
                case nvtt::Error_Unknown: Console::errorfn("NVTT : unknown error"); break;
                case nvtt::Error_InvalidInput: Console::errorfn("NVTT : invalid input"); break;
                case nvtt::Error_UnsupportedFeature: Console::errorfn("NVTT : unsupported feature"); break;
                case nvtt::Error_CudaError: Console::errorfn("NVTT : cuda error"); break;
                case nvtt::Error_FileOpen: Console::errorfn("NVTT : file open error"); break;
                case nvtt::Error_FileWrite: Console::errorfn("NVTT : file write error"); break;
                default: break;
            }
        }
    };
    struct OutputHandler : public nvtt::OutputHandler {
        struct MipMapData {
            vector<U8> _pixelData;
            I32 _width = 0;
            I32 _height = 0;
            I32 _depth = 0;
        };
        vector<MipMapData> _mipmaps;
        I32 _currentMipLevel = 0;
        U8 _numComponents = 0u;
        nvtt::Format _format = nvtt::Format::Format_BC1;
        bool _discardAlpha = false;

        OutputHandler(nvtt::Format format, bool discardAlpha, U8 numComponents)
            : _format(format), _discardAlpha(discardAlpha), _numComponents(numComponents)
        {
        }

        virtual ~OutputHandler()
        {
        }

        // create the osg image from the given format
        bool assignImage(ImageLayer& image) {
            // convert nvtt format to OpenGL pixel format
            GFXImageFormat pixelFormat = GFXImageFormat::COUNT;
            switch (_format)
            {
            case nvtt::Format_RGBA:
                pixelFormat = _discardAlpha ? GFXImageFormat::RGB : GFXImageFormat::RGBA;
                break;
            case nvtt::Format_BC1:
                pixelFormat = GFXImageFormat::BC1;
                break;
            case nvtt::Format_BC1a:
                pixelFormat = GFXImageFormat::BC1a;
                break;
            case nvtt::Format_BC2:
                pixelFormat = GFXImageFormat::BC2;
                break;
            case nvtt::Format_BC3:
                pixelFormat = GFXImageFormat::BC3;
                break;
            case nvtt::Format_BC4:
                pixelFormat = GFXImageFormat::BC4u;
                break;
            case nvtt::Format_BC5:
                pixelFormat = GFXImageFormat::BC5u;
                break; 
            case nvtt::Format_BC6:
                pixelFormat = GFXImageFormat::BC6u;
                break;
            case nvtt::Format_BC7:
                pixelFormat = GFXImageFormat::BC7;
                break;
            default:
                Console::errorfn("NVTT: Invalid or not supported format");
                return false;
            }

            for (MipMapData& data : _mipmaps) {
                if (!image.allocateMip(data._pixelData.data(), data._pixelData.size(), to_U16(data._width), to_U16(data._height), to_U16(data._depth), _numComponents)) {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }

            return true;
        }

        /// Indicate the start of a new compressed image that's part of the final texture.
        virtual void beginImage(int size, int width, int height, int depth, int face, int miplevel)
        {
            MipMapData& data = _mipmaps.push_back();
            data._width = width;
            data._height = height;
            data._depth = depth;
            data._pixelData.resize(size);
            _currentMipLevel = miplevel;
        }

        virtual void endImage()
        {
        }

        /// Output data. Compressed data is output as soon as it's generated to minimize memory allocations.
        virtual bool writeData(const void* data, int size)
        {
            // Copy mipmap data
            memcpy(&_mipmaps[_currentMipLevel]._pixelData, data, size);
            return true;
        }
    };

    [[nodiscard]] bool isBC1n(const nvtt::Format format, const bool isNormalMap) noexcept {
        return isNormalMap && format == nvtt::Format_BC1;
    }

    [[nodiscard]] nvtt::Format getNVTTFormat(const ImageOutputFormat outputFormat, const bool isNormalMap, const bool hasAlpha, const bool isGreyscale) noexcept {
        assert(outputFormat != ImageOutputFormat::COUNT);

        if (!g_KeepDevILDDSCompatibility && outputFormat != ImageOutputFormat::AUTO) {
            switch (outputFormat) {
                case ImageOutputFormat::BC1:      return nvtt::Format::Format_BC1;
                case ImageOutputFormat::BC1a:     return nvtt::Format::Format_BC1a;
                case ImageOutputFormat::BC2:      return nvtt::Format::Format_BC2;
                case ImageOutputFormat::BC3:      return isNormalMap ? nvtt::Format::Format_BC3n : nvtt::Format::Format_BC3;
                case ImageOutputFormat::BC4:      return nvtt::Format::Format_BC4;
                case ImageOutputFormat::BC5:      return nvtt::Format::Format_BC5;
                case ImageOutputFormat::BC6:      return nvtt::Format::Format_BC6;
                case ImageOutputFormat::BC7:      return nvtt::Format::Format_BC7;
                //case ImageOutputFormat::BC3_RGBM: return nvtt::Format::Format_BC3_RGBM; //Not supported
            };
        }

        if_constexpr(g_KeepDevILDDSCompatibility) {
            return isNormalMap ? nvtt::Format::Format_BC3n : hasAlpha ? nvtt::Format::Format_BC3 : nvtt::Format::Format_BC1;
        }
        return isNormalMap ? nvtt::Format::Format_BC5 : isGreyscale ? nvtt::Format::Format_BC4 : nvtt::Format::Format_BC7;
    }

    [[nodiscard]] nvtt::MipmapFilter getNVTTMipFilter(const MipMapFilter filter) noexcept {
        switch (filter) {
            case MipMapFilter::BOX: return nvtt::MipmapFilter_Box;
            case MipMapFilter::TRIANGLE: return nvtt::MipmapFilter_Triangle;
            case MipMapFilter::KAISER: return nvtt::MipmapFilter_Kaiser;
        }

        return nvtt::MipmapFilter_Box;
    }
}; // namespace nvttHelpers

namespace {
    Mutex s_imageLoadingMutex;
    bool s_useUpperLeftOrigin = false;

    //ref: https://github.com/nvpro-pipeline/pipeline/blob/master/dp/sg/io/IL/Loader/ILTexLoader.cpp
    FORCE_INLINE I32 determineFace(const I32 i, const bool isDDS, const bool isCube) noexcept {
        if (isDDS && isCube) {
            if (i == 4) {
                return 5;
            } else if (i == 5) {
                return 4;
            }
        }

        return i;
    }

    inline void checkError() noexcept {
        ILenum error = ilGetError();
        while (error != IL_NO_ERROR) {
            DebugBreak();
            error = ilGetError();
        }
    }
};

void OnStartup(const bool upperLeftOrigin) {
    s_useUpperLeftOrigin = upperLeftOrigin;

    ilInit();
    iluInit();
    //ilutInit();
    ilEnable(IL_FILE_OVERWRITE);
    ilSetInteger(IL_KEEP_DXTC_DATA, IL_TRUE);
    //ilutRenderer(ILUT_WIN32);
    iluImageParameter(ILU_FILTER, ILU_SCALE_MITCHELL);
}

void OnShutdown() {
    ilShutDown();
}

bool UseUpperLeftOrigin() noexcept {
    return s_useUpperLeftOrigin;
}

bool ImageData::loadFromMemory(const Byte* data, const size_t size, const U16 width, const U16 height, const U16 depth, const U8 numComponents) {
    ImageLayer& layer = _layers.emplace_back();
    return layer.allocateMip(data, size, width, height, depth, numComponents);
}

bool ImageData::loadFromFile(const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const ResourcePath& name) {
    ImportOptions options{};
    options._useDDSCache = false;
    return loadFromFile(srgb, refWidth, refHeight, path, name, options);
}

bool ImageData::loadFromFile(const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const ResourcePath& name, const ImportOptions options) {
    _path = path;
    _name = name;
    _name.convertToLower();
    ignoreAlphaChannelTransparency(!options._alphaChannelTransparency);

    // We can handle DDS files directly
    if (hasExtension(_name, "dds")) {
        return loadDDS_NVTT(srgb, refWidth, refHeight, _path, _name);
    }

    const ResourcePath fullPath = _path + _name;
    FILE* f = stbi__fopen(fullPath.c_str(), "rb");
    SCOPE_EXIT{
        if (f) {
            fclose(f);
        }
    };

    if (!f) {
        return false;
    }

    // If TRUE: flip the image vertically, so the first pixel in the output array is the bottom left
    stbi_set_flip_vertically_on_load_thread(UseUpperLeftOrigin() ? TRUE : FALSE);

    I32 width = 0, height = 0, comp = 0;
    U8* dataLDR = nullptr;
    U16* data16Bit = nullptr;
    F32* dataHDR = nullptr;
    _isHDR = (stbi_is_hdr_from_file(f) == TRUE);
    _16Bit = _isHDR ? false : (stbi_is_16_bit_from_file(f) == TRUE);


    _hasDummyAlphaChannel = false;
    {
        I32 x, y, n;
        if (stbi_info_from_file(f, &x, &y, &n) && n == 3) {
            _hasDummyAlphaChannel = true;
            Console::warnfn(Locale::Get(_ID("WARN_IMAGETOOLS_RGB_FORMAT")), fullPath.c_str());
        }
    }

    if (!_isHDR && !_16Bit) {
        if (Texture::UseTextureDDSCache() && !_isHDR && !_16Bit) {
            const ResourcePath cachePath = Texture::GetCachePath(_path);
            const ResourcePath cacheName = _name + ".dds";

            STUBBED("Get rid of DevIL completely! It is really really bad for DDS handling (quality/performance) compared to the alternatives -Ionut");

            // Try and save regular images to DDS for better compression next time
            if (options._useDDSCache) {
                if (!createDirectory(cachePath)) {
                    DebugBreak();
                }

                const ResourcePath cacheFilePath = cachePath + cacheName;
                if (!fileExists(cacheFilePath)) {
                    ScopedLock<Mutex> lock(s_imageLoadingMutex);

                    nvtt::Context context;
                    context.enableCudaAcceleration(true);

                    nvtt::Surface image;
                    bool hasAlpha = false;
                    if (image.load(fullPath.c_str(), &hasAlpha)) {
                        constexpr bool isGreyScale = false;
                        const nvtt::Format outputFormat = nvttHelpers::getNVTTFormat(options._outputFormat, options._isNormalMap, hasAlpha, isGreyScale);

                        // Setup compression options.
                        nvtt::CompressionOptions compressionOptions;
                        compressionOptions.setFormat(outputFormat);
                        compressionOptions.setQuality(options._fastCompression ? nvtt::Quality::Quality_Fastest : nvtt::Quality::Quality_Normal);
                        if (outputFormat == nvtt::Format_BC6) {
                            compressionOptions.setPixelType(nvtt::PixelType_UnsignedFloat);
                        } else if (outputFormat == nvtt::Format_BC2) {
                            // Dither alpha when using BC2.
                            compressionOptions.setQuantization(/*color dithering*/false, /*alpha dithering*/true, /*binary alpha*/false);
                        } else if (outputFormat == nvtt::Format_BC1a) {
                            // Binary alpha when using BC1a.
                            compressionOptions.setQuantization(/*color dithering*/false, /*alpha dithering*/true, /*binary alpha*/true, 127);
                        }

                        if (nvttHelpers::isBC1n(outputFormat, options._isNormalMap)) {
                            compressionOptions.setColorWeights(1, 1, 0);
                        }

                        nvtt::OutputOptions outputOptions;
                        outputOptions.setFileName(cacheFilePath.c_str());
                        nvttHelpers::ErrorHandler errorHandler;
                        outputOptions.setErrorHandler(&errorHandler);
                        if (outputFormat == nvtt::Format_BC6 || outputFormat == nvtt::Format_BC7) {
                            outputOptions.setContainer(nvtt::Container_DDS10);
                        } else {
                            outputOptions.setContainer(nvtt::Container_DDS);
                        }
                        if (options._outputSRGB) {
                            outputOptions.setSrgbFlag(true);
                        }

                        image.setNormalMap(options._isNormalMap);

                        if (!context.outputHeader(image, image.countMipmaps(), compressionOptions, outputOptions)) {
                            DebugBreak();
                        }

                        if (UseUpperLeftOrigin()) {
                            image.flipY();
                        }
                        F32 coverage = 0.f;
                        if (options._isNormalMap) {
                            image.normalizeNormalMap();
                        } else {
                            if (hasAlpha && options._alphaChannelTransparency) {
                                coverage = image.alphaTestCoverage(Config::ALPHA_DISCARD_THRESHOLD);
                                image.setAlphaMode(nvtt::AlphaMode::AlphaMode_Transparency);
                            } else {
                                image.setAlphaMode(nvtt::AlphaMode::AlphaMode_None);
                            }
                        }
                        if (!context.compress(image, 0, 0, compressionOptions, outputOptions)) {
                            DebugBreak();
                        }

                        // Build and output mipmaps.
                        if (!options._skipMipMaps) {
                            I32 m = 1;
                            while (image.buildNextMipmap(nvttHelpers::getNVTTMipFilter(options._mipFilter))) {
                                if (options._isNormalMap) {
                                    image.normalizeNormalMap();
                                } else {
                                    if (hasAlpha && options._alphaChannelTransparency) {
                                        image.scaleAlphaToCoverage(coverage, Config::ALPHA_DISCARD_THRESHOLD);
                                    }
                                }

                                context.compress(image, 0, m, compressionOptions, outputOptions);
                                m++;
                            }
                        }

                    }
                }

                return loadDDS_NVTT(srgb, refWidth, refHeight, cachePath, cacheName);
            }
        }
    }

    const I32 req_comp = _hasDummyAlphaChannel ? 4 : 0;
    if (_isHDR) {
        dataHDR = stbi_loadf_from_file(f, &width, &height, &comp, req_comp);
        _dataType = GFXDataFormat::FLOAT_32;
    } else if (_16Bit) {
        data16Bit = stbi_load_from_file_16(f, &width, &height, &comp, req_comp);
        _dataType = GFXDataFormat::UNSIGNED_SHORT;
    } else {
        dataLDR = stbi_load_from_file(f, &width, &height, &comp, req_comp);
        _dataType = GFXDataFormat::UNSIGNED_BYTE;
    }

    if (dataHDR == nullptr && data16Bit == nullptr && dataLDR == nullptr) {
        Console::errorfn(Locale::Get(_ID("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE")), fullPath.c_str());
        return false;
    }

    if (_hasDummyAlphaChannel) {
        comp = req_comp;
    }

    ImageLayer& layer = _layers.emplace_back();

    if (_requestedDataFormat != GFXDataFormat::COUNT && _requestedDataFormat != _dataType) {
        switch (_requestedDataFormat) {
            case GFXDataFormat::UNSIGNED_BYTE: {
                if (_isHDR) {
                    _isHDR = false;
                    dataLDR = stbi__hdr_to_ldr(dataHDR, width, height, comp);
                    dataHDR = nullptr;
                } else if (_16Bit) {
                    _16Bit = false;
                    dataLDR = stbi__convert_16_to_8(data16Bit, width, height, comp);
                    data16Bit = nullptr;
                }
            } break;
            case GFXDataFormat::UNSIGNED_SHORT: {
                if (_isHDR || !_16Bit) {
                    if (_isHDR) {
                        _isHDR = false;
                        dataLDR = stbi__hdr_to_ldr(dataHDR, width, height, comp);
                        dataHDR = nullptr;
                    }
                    data16Bit = stbi__convert_8_to_16(dataLDR, width, height, comp);
                    dataLDR = nullptr;
                }
                _16Bit = true;
            } break;
            case GFXDataFormat::FLOAT_32: {
                if (!_isHDR) {
                    if (_16Bit) {
                        _16Bit = false;
                        dataLDR = stbi__convert_16_to_8(data16Bit, width, height, comp);
                        data16Bit = nullptr;
                    }
                    dataHDR = stbi__ldr_to_hdr(dataLDR, width, height, comp);
                    _isHDR = true;
                }
            } break;
        };
    }
    DIVIDE_ASSERT(comp != 3, "RGB textures (e.g. 24bit) not supported due to Vulkan limitations");

    switch (comp) {
        case 1 : _format = GFXImageFormat::RED;  break;
        case 2 : _format = GFXImageFormat::RG;   break;
        case 4 : _format = GFXImageFormat::RGBA; break;
        default:
            DIVIDE_UNEXPECTED_CALL();
            break;
    }

    _bpp = to_U8((_isHDR ? 32u : _16Bit ? 16u : 8u) * comp);

    if (refWidth != 0 && refHeight != 0 && (refWidth != width || refHeight != height)) {
        if (_16Bit) {
            U16* resizedData16 = (U16*)STBI_MALLOC(to_size(refWidth)* refHeight * (_bpp / 8));
            const I32 ret = stbir_resize_uint16_generic(data16Bit, width, height, 0,
                                                        resizedData16, refWidth, refHeight, 0,
                                                        comp, -1, 0,
                                                        STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT, STBIR_COLORSPACE_LINEAR,
                                                        nullptr);
            if (ret == 1) {
                width = refWidth;
                height = refHeight;
                stbi_image_free(data16Bit);
                data16Bit = resizedData16;
            }
        } else if (_isHDR) {
            F32* resizedDataHDR = (F32*)STBI_MALLOC(to_size(refWidth) * refHeight * (_bpp / 8));
            const I32 ret = stbir_resize_float(dataHDR, width, height, 0, resizedDataHDR, refWidth, refHeight, 0, comp);
            if (ret == 1) {
                width = refWidth;
                height = refHeight;
                stbi_image_free(dataHDR);
                dataHDR = resizedDataHDR;
            }
        } else {
            U8* resizedDataLDR = (U8*)STBI_MALLOC(to_size(refWidth) * refHeight * (_bpp / 8));
            const I32 ret = srgb ? stbir_resize_uint8_srgb(dataLDR, width, height, 0, resizedDataLDR, refWidth, refHeight, 0, comp, -1, 0)
                                 : stbir_resize_uint8(dataLDR, width, height, 0, resizedDataLDR, refWidth, refHeight, 0, comp);
            if (ret == 1) {
                width = refWidth;
                height = refHeight;
                stbi_image_free(dataLDR);
                dataLDR = resizedDataLDR;
            }
        }
    }

    bool ret = false;
    const size_t dataSize = to_size(width) * height * comp;
    if (_isHDR && dataHDR != nullptr) {
        ret = layer.allocateMip(dataHDR, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free(dataHDR);
    } else if (_16Bit && data16Bit != nullptr) {
        ret = layer.allocateMip(data16Bit, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free(data16Bit);
    } else if (dataLDR != nullptr) {
        ret = layer.allocateMip(dataLDR, dataSize, to_U16(width), to_U16(height), 1u, to_U8(comp));
        stbi_image_free(dataLDR);
    }

    
    return ret;
}

bool ImageData::loadDDS_NVTT([[maybe_unused]] const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const ResourcePath& name) {
    //ToDo: Use a better DDS loader
    return loadDDS_IL(srgb, refWidth, refHeight, path, name);
}

bool ImageData::loadDDS_IL([[maybe_unused]] const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const ResourcePath& name) {
    const ResourcePath fullPath = path + name;

    ScopedLock<Mutex> lock(s_imageLoadingMutex);

    ILuint imageID = 0u;
    ilGenImages(1, &imageID);
    ilBindImage(imageID);
    checkError();

    SCOPE_EXIT{
        ilDeleteImage(imageID);
        checkError();
    };

    if (ilLoadImage(fullPath.c_str()) == IL_FALSE) {
        checkError();
        Console::errorfn(Locale::Get(_ID("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE")), _name.c_str());
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
        if (!s_useUpperLeftOrigin) {
            if (compressed) {
                ilFlipSurfaceDxtcData();
            } else {
                iluFlipImage();
            }
            checkError();
        }
    };

    ILinfo imageInfo;
    iluGetImageInfo(&imageInfo);
    checkError();

    // Avoid, double, longs, unsigned ints, etc
    if (imageInfo.Type != IL_BYTE && imageInfo.Type != IL_UNSIGNED_BYTE &&
        imageInfo.Type != IL_FLOAT &&
        imageInfo.Type != IL_UNSIGNED_SHORT && imageInfo.Type != IL_SHORT) {
        ilConvertImage(imageInfo.Format, IL_FLOAT);
        imageInfo.Type = IL_FLOAT;
        checkError();
    }

    // We don't support paletted images
    if (imageInfo.Format == IL_COLOUR_INDEX) {
        ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
        imageInfo.Format = IL_RGBA;
        imageInfo.Type = IL_UNSIGNED_BYTE;
        checkError();
    }

    size_t storageSizeFactor = 1u;
    switch (imageInfo.Type) {
        case IL_BYTE:
            _dataType = GFXDataFormat::SIGNED_BYTE;
            _isHDR = _16Bit = false;
            break;
        case IL_UNSIGNED_BYTE:
            _isHDR = _16Bit = false;
            _dataType = GFXDataFormat::UNSIGNED_BYTE;
            break;
        case IL_SHORT:
            _dataType = GFXDataFormat::SIGNED_SHORT;
            _isHDR = false;
            _16Bit = true;
            storageSizeFactor = 2;
            break;
        case IL_UNSIGNED_SHORT:
            _isHDR = false;
            _16Bit = true;
            _dataType = GFXDataFormat::UNSIGNED_SHORT;
            storageSizeFactor = 2;
            break;
        case IL_FLOAT:
            _isHDR = true;
            _16Bit = false;
            _dataType = GFXDataFormat::FLOAT_32;
            storageSizeFactor = 4;
            break;
        default: {
            return false;
        } break;
    };

    // Resize if needed
    if (refWidth != 0 && refHeight != 0 && (imageInfo.Width != refWidth || imageInfo.Height != refHeight)) {
        if (iluScale(refWidth, refHeight, imageInfo.Depth)) {
            imageInfo.Width = refWidth;
            imageInfo.Height = refHeight;
        }
        checkError();
    }

    const ILint channelCount = ilGetInteger(IL_IMAGE_CHANNELS);
    checkError();

    if (compressed) {
        switch (dxtFormat) {
            case IL_DXT1: {
                _format = channelCount == 3 ? GFXImageFormat::DXT1_RGB : GFXImageFormat::DXT1_RGBA;
            }  break;
            case IL_DXT3: {
                _format = GFXImageFormat::DXT3_RGBA;
            } break;
            case IL_DXT5: {
                _format = GFXImageFormat::DXT5_RGBA;
            } break;
            default: {
                DIVIDE_UNEXPECTED_CALL();
                return false;
            }
        }
    } else {
        switch (imageInfo.Format) {
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
                _format = GFXImageFormat::RGB;
                break;
            case IL_BGR:
                _format = GFXImageFormat::BGR;
                break;
            case IL_RGBA:
                _format = GFXImageFormat::RGBA;
                break;
            case IL_BGRA:
                _format = GFXImageFormat::BGRA;
                break;
        };
    }

    const ILint numImages = ilGetInteger(IL_NUM_IMAGES) + 1;
    // ^^^^^^^^^^^^^ Querying for IL_NUM_IMAGES returns the number of images
    //               following the current one. Add 1 for the right image count!
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

            for (ILuint m = 0u; m <= imageInfo.NumMips; ++m) {
                // DevIL frequently loses track of the current state
                ilBindImage(imageID);
                ilActiveImage(image);
                ilActiveFace(face);
                ilActiveMipmap(m);
                flipActiveMip();
                checkError();

                if (compressed && image == 0 && face == 0 && m == 0) {
                    _decompressedData.resize(to_size(imageInfo.Width) * imageInfo.Height * imageInfo.Depth * 4);
                    ilCopyPixels(0, 0, 0, imageInfo.Width, imageInfo.Height, imageInfo.Depth, IL_RGBA, IL_UNSIGNED_BYTE, _decompressedData.data());
                }
                const ILint width  = ilGetInteger(IL_IMAGE_WIDTH);
                const ILint height = ilGetInteger(IL_IMAGE_HEIGHT);
                const ILint depth  = ilGetInteger(IL_IMAGE_DEPTH);
                checkError();

                ILuint size = width * height * depth * imageInfo.Bpp;
                if (compressed) {
                    size = ilGetDXTCData(nullptr, 0, dxtFormat);
                    checkError();
                }

                Byte* data = layer.allocateMip<Byte>(to_size(size),
                                                     to_U16(width),
                                                     to_U16(height),
                                                     to_U16(depth),
                                                     compressed ? 0  // 0 here means that our calculated size will be 0, thus our specified size will always be used
                                                                : to_U8(channelCount * storageSizeFactor)); //For short and float type data we need to increase the available storage a bit (channel count assumes a Byte per component here)
                if (compressed) {
                    ilGetDXTCData(data, size, dxtFormat);
                    checkError();
                } else {
                    memcpy(data, ilGetData(), size);
                    checkError();
                }
            }
        }
    }

    return true;
}

UColour4 ImageData::getColour(const I32 x, const I32 y, [[maybe_unused]] U32 layer, const U8 mipLevel) const {
    UColour4 returnColour;
    getColour(x, y, returnColour.r, returnColour.g, returnColour.b, returnColour.a, mipLevel);
    return returnColour;
}

namespace {
    constexpr F32 U32_MAX_F = (U32_MAX - 1u) * 1.f;
    constexpr F32 U16_MAX_F = U16_MAX * 1.f;
    FORCE_INLINE [[nodiscard]] U8 F32ToU8Colour(const F32 val) noexcept { return to_U8((val / U32_MAX_F) * 255); }
    FORCE_INLINE [[nodiscard]] U8 U16ToU8Colour(const U16 val) noexcept { return to_U8((val / U16_MAX_F) * 255); }
    FORCE_INLINE [[nodiscard]] U8 U8ToU8Colour(const U8 val) noexcept { return val; }
};

void ImageData::getColourComponent(const I32 x, const I32 y, const U8 comp, U8& c, const U32 layer, const U8 mipLevel) const {
    assert(comp >= 0 && comp < 4);
    assert(!IsCompressed(_format) || mipLevel == 0);
    assert(_layers.size() > layer);

    if (!HasAlphaChannel(_format) && comp == 3) {
        c = U8_MAX;
        return;
    }

    const LayerData* mip = _layers[layer].getMip(mipLevel);

    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = IsCompressed(_format) ? 4 : _bpp / 8;
    const I32 idx = ((y * mip->_dimensions.width + x) * pixelStride) + comp;
    if (IsCompressed(_format)) {
        // Decompressed data is always UByte-RGBA
        c = _decompressedData[idx];
    } else {
        if (_isHDR) {
            c = F32ToU8Colour(static_cast<const F32*>(mip->data())[idx]);
        } else if (_16Bit) {
            c = U16ToU8Colour(static_cast<const U16*>(mip->data())[idx]);
        } else {
            c = U8ToU8Colour(static_cast<const U8*>(mip->data())[idx]);
        }
    }
}

void ImageData::getColour(const I32 x, const I32 y, U8& r, U8& g, U8& b, U8& a, const U32 layer, const U8 mipLevel) const {
    assert(!IsCompressed(_format) || mipLevel == 0);
    assert(_layers.size() > layer);

    const LayerData* mip = _layers[layer].getMip(mipLevel);
    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = IsCompressed(_format) ? 4 : _bpp / 8;
    const I32 idx = ((y * mip->_dimensions.width + x) * pixelStride);

    if (IsCompressed(_format)) {
        // Decompressed data is always UByte-RGBA
        const U8* src = _decompressedData.data();
        r = src[idx + 0];
        g = src[idx + 1];
        b = src[idx + 2]; 
        a = HasAlphaChannel(_format) ? src[idx + 3] : 255;
    } else {
        if (_isHDR) {
            const F32* src = static_cast<F32*>(mip->data());
            r = F32ToU8Colour(src[idx + 0]);
            g = F32ToU8Colour(src[idx + 1]);
            b = F32ToU8Colour(src[idx + 2]);
            a = HasAlphaChannel(_format) ? F32ToU8Colour(src[idx + 3]) : 255;
        } else if (_16Bit) {
            const U16* src = static_cast<U16*>(mip->data());
            r = U16ToU8Colour(src[idx + 0]); 
            g = U16ToU8Colour(src[idx + 1]);
            b = U16ToU8Colour(src[idx + 2]);
            a = HasAlphaChannel(_format) ? U16ToU8Colour(src[idx + 3]) : 255;
        } else {
            const U8* src = static_cast<U8*>(mip->data());
            r = U8ToU8Colour(src[idx + 0]);
            g = U8ToU8Colour(src[idx + 1]);
            b = U8ToU8Colour(src[idx + 2]);
            a = HasAlphaChannel(_format) ? U8ToU8Colour(src[idx + 3]) : 255;
        }
    }
}

bool SaveImage(const ResourcePath& filename, const vec2<U16>& dimensions, const U8 numberOfComponents, U8* imageData, const SaveImageFormat format) {
    switch (format) {
        case SaveImageFormat::PNG: return stbi_write_png(filename.c_str(), dimensions.width, dimensions.height, numberOfComponents, imageData, dimensions.width * numberOfComponents) == TRUE;
        case SaveImageFormat::BMP: return stbi_write_bmp(filename.c_str(), dimensions.width, dimensions.height, numberOfComponents, imageData) == TRUE;
        case SaveImageFormat::TGA: return stbi_write_tga(filename.c_str(), dimensions.width, dimensions.height, numberOfComponents, imageData) == TRUE;
        case SaveImageFormat::JPG: return stbi_write_jpg(filename.c_str(), dimensions.width, dimensions.height, numberOfComponents, imageData, 85) == TRUE;
    }

    return false;
}

bool SaveImageHDR(const ResourcePath& filename, const vec2<U16>& dimensions, const U8 numberOfComponents, F32* imageData) {
    return stbi_write_hdr(filename.c_str(), dimensions.width, dimensions.height, numberOfComponents, imageData) == TRUE;
}
}  // namespace Divide::ImageTools

