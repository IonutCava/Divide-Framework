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

namespace Divide::ImageTools {

namespace {
    Mutex s_imageLoadingMutex;
    bool s_useUpperLeftOrigin = false;

    //ref: https://github.com/nvpro-pipeline/pipeline/blob/master/dp/sg/io/IL/Loader/ILTexLoader.cpp
    static I32 determineFace(const I32 i, const bool isDDS, const bool isCube) {
        I32 image = i;
        if (isDDS) {
            if (isCube) {
                if (4 == image) {
                    image = 5;
                } else if (5 == image) {
                    image = 4;
                }
            }
        }

        return(image);
    }

    const auto checkError = []() noexcept {
        ILenum error = ilGetError();
        while (error != IL_NO_ERROR) {
            DebugBreak();
            error = ilGetError();
        }
    };
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
    ilEnable(IL_TYPE_SET);
    ilTypeFunc(IL_UNSIGNED_BYTE);
    ilEnable(IL_FORMAT_SET);
    ilFormatFunc(IL_RGB);
}

void OnShutdown() {
    // Restore IL state
    ilDisable(IL_FORMAT_SET);
    ilDisable(IL_TYPE_SET);
    ilShutDown();
}

bool UseUpperLeftOrigin() noexcept {
    return s_useUpperLeftOrigin;
}

bool ImageData::loadFromMemory(Byte* data, const size_t size, const U16 width, const U16 height, const U16 depth, const U8 numComponents) {
    ImageLayer& layer = _layers.emplace_back();
    return layer.allocateMip(data, size, width, height, depth, numComponents);
}

bool ImageData::loadFromFile(const bool srgb, const U16 refWidth, const U16 refHeight, const ResourcePath& path, const ResourcePath& name, const bool useDDSCache) {
    _path = path;
    _name = name;

    // We can handle DDS files directly
    if (hasExtension(_name, "DDS")) {
        return loadDDS_IL(srgb, refWidth, refHeight, _path, _name);
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
    // By default, STB images are loaded with the origin in the top(upper) left. So don't flip if s_useUpperLeftOrigin is TRUE as that is our loading default
    stbi_set_flip_vertically_on_load_thread(UseUpperLeftOrigin() ? FALSE : TRUE);

    I32 width = 0, height = 0, comp = 0;
    U8* dataLDR = nullptr;
    U16* data16Bit = nullptr;
    F32* dataHDR = nullptr;
    _isHDR = (stbi_is_hdr_from_file(f) == TRUE);
    _16Bit = _isHDR ? false : (stbi_is_16_bit_from_file(f) == TRUE);

    if (!_isHDR && !_16Bit) {
        // Sadly, we need to double load these files because DevIL is really bad at detecting the proper number of channels
        // an image has. 32bit pngs sometimes show up as 24bit RGB.
        dataLDR = stbi_load_from_file(f, &width, &height, &comp, 0);
        _dataType = GFXDataFormat::UNSIGNED_BYTE;
        if (dataLDR == nullptr) {
            Console::errorfn(Locale::Get(_ID("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE")), fullPath.c_str());
            return false;
        }


        if (Texture::UseTextureDDSCache() && !_isHDR && !_16Bit && comp < 4 /*disabled RGBA support as it doesn't seem to work properly with DevIL*/) {
            const ResourcePath cachePath = Texture::GetCachePath(_path);
            const ResourcePath cacheName = _name + ".DDS";

            STUBBED("Get rid of DevIL completely! It is really really bad for DDS handling compared to the alternatives -Ionut");

            // Try and save regular images to DDS for better compression next time
            if (useDDSCache) {
                const ResourcePath cacheFilePath = cachePath + cacheName;

                if (!fileExists(cacheFilePath)) {
                    if (createDirectory(cachePath)) {
                        ScopedLock<Mutex> lock(s_imageLoadingMutex);
                        ILuint imageID = 0u;
                        ilGenImages(1, &imageID);
                        ilBindImage(imageID);
                        checkError();
                        if (ilLoadImage(fullPath.c_str()) == IL_TRUE) {
                            ilSetInteger(IL_DXTC_FORMAT, comp == 4 ? IL_DXT5 : IL_DXT1);
                            iluBuildMipmaps();
                            ilSave(IL_DDS, cacheFilePath.c_str());
                            checkError();
                        }
                        ilDeleteImages(1, &imageID);
                        checkError();
                    }
                }
                assert(fileExists(cacheFilePath));

                return loadDDS_IL(srgb, refWidth, refHeight, cachePath, cacheName);
            }
        }
    }
    if (_isHDR) {
        dataHDR = stbi_loadf_from_file(f, &width, &height, &comp, 0);
        _dataType = GFXDataFormat::FLOAT_32;
    } else if (_16Bit) {
        data16Bit = stbi_load_from_file_16(f, &width, &height, &comp, 0);
        _dataType = GFXDataFormat::UNSIGNED_SHORT;
    }
    if (dataHDR == nullptr && data16Bit == nullptr && dataLDR == nullptr) {
        Console::errorfn(Locale::Get(_ID("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE")), fullPath.c_str());
        return false;
    }
    _compressed = false;

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

    switch (comp) {
        case 1 : _format = GFXImageFormat::RED;  break;
        case 2 : _format = GFXImageFormat::RG;   break;
        case 3 : _format = GFXImageFormat::RGB;  break;
        case 4 : _format = GFXImageFormat::RGBA; break;
        default:
            DIVIDE_UNEXPECTED_CALL();
            break;
    }

    _bpp = to_U8((_isHDR ? 32u : _16Bit ? 16u : 8u) * comp);

    if (refWidth != 0 && refHeight != 0 && (refWidth != width || refHeight != height)) {
        if (_16Bit) {
            U16* resizedData16 = (U16*)STBI_MALLOC(to_size(refWidth)* refHeight * comp * 2);
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
            F32* resizedDataHDR = (F32*)STBI_MALLOC(to_size(refWidth) * refHeight * comp * 4);
            const I32 ret = stbir_resize_float(dataHDR, width, height, 0, resizedDataHDR, refWidth, refHeight, 0, comp);
            if (ret == 1) {
                width = refWidth;
                height = refHeight;
                stbi_image_free(dataHDR);
                dataHDR = resizedDataHDR;
            }
        } else {
            U8* resizedDataLDR = (U8*)STBI_MALLOC(to_size(refWidth) * refHeight * comp * 1);
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

    const auto flipActiveMip = [&]() {
        if (!s_useUpperLeftOrigin) {
            if (_compressed) {
                ilFlipSurfaceDxtcData();
            } else {
                iluFlipImage();
            }
            checkError();
        }
    };

    if (ilLoadImage(fullPath.c_str()) == IL_FALSE) {
        checkError();
        Console::errorfn(Locale::Get(_ID("ERROR_IMAGETOOLS_INVALID_IMAGE_FILE")), _name.c_str());
        return false;
    }
    
    const ILint dxtFormat = ilGetInteger(IL_DXTC_DATA_FORMAT);
    _compressed = dxtFormat == IL_DXT1 ||
                  dxtFormat == IL_DXT1A||
                  dxtFormat == IL_DXT2 ||
                  dxtFormat == IL_DXT3 ||
                  dxtFormat == IL_DXT4 ||
                  dxtFormat == IL_DXT5;
    
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
            [[fallthrough]];
        case IL_UNSIGNED_BYTE:
            _isHDR = _16Bit = false;
            _dataType = GFXDataFormat::UNSIGNED_BYTE;
            break;
        case IL_SHORT:
            _dataType = GFXDataFormat::SIGNED_SHORT;
            [[fallthrough]];
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

    if (_compressed) {
        switch (dxtFormat) {
            case IL_DXT1: {
                _format = channelCount == 3 ? GFXImageFormat::COMPRESSED_RGB_DXT1 : GFXImageFormat::COMPRESSED_RGBA_DXT1;
            }  break;
            case IL_DXT3: {
                _format = GFXImageFormat::COMPRESSED_RGBA_DXT3;
            } break;
            case IL_DXT5: {
                _format = GFXImageFormat::COMPRESSED_RGBA_DXT5;
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
    const ILint numMipMaps = ilGetInteger(IL_NUM_MIPMAPS);
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

                if (_compressed && image == 0 && face == 0 && m == 0) {
                    _decompressedData.resize(to_size(imageInfo.Width) * imageInfo.Height * imageInfo.Depth * 4);
                    ilCopyPixels(0, 0, 0, imageInfo.Width, imageInfo.Height, imageInfo.Depth, IL_RGBA, IL_UNSIGNED_BYTE, _decompressedData.data());
                }
                const ILint width  = ilGetInteger(IL_IMAGE_WIDTH);
                const ILint height = ilGetInteger(IL_IMAGE_HEIGHT);
                const ILint depth  = ilGetInteger(IL_IMAGE_DEPTH);
                checkError();

                ILuint size = width * height * depth * imageInfo.Bpp;
                if (_compressed) {
                    size = ilGetDXTCData(nullptr, 0, dxtFormat);
                    checkError();
                }

                Byte* data = layer.allocateMip<Byte>(to_size(size),
                                                     to_U16(width),
                                                     to_U16(height),
                                                     to_U16(depth),
                                                     _compressed ? 0  // 0 here means that our calculated size will be 0, thus our specified size will always be used
                                                                 : to_U8(channelCount * storageSizeFactor)); //For short and float type data we need to increase the available storage a bit (channel count assumes a Byte per component here)
                if (_compressed) {
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

bool ImageData::hasAlphaChannel() const noexcept {
    if (_compressed) {
        return _format != GFXImageFormat::COMPRESSED_RGB_DXT1;
    }

    return _format == GFXImageFormat::BGRA || _format == GFXImageFormat::RGBA;
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
    assert(!_compressed || mipLevel == 0);
    assert(_layers.size() > layer);

    if (!hasAlphaChannel() && comp == 3) {
        c = U8_MAX;
        return;
    }

    assert(!_compressed || mipLevel == 0);
    assert(_layers.size() > layer);

    LayerData* mip = _layers[layer].getMip(mipLevel);

    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = _compressed ? 4 : _bpp / 8;
    const I32 idx = ((y * mip->_dimensions.width + x) * pixelStride) + comp;
    if (_compressed) {
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
    assert(!_compressed || mipLevel == 0);
    assert(_layers.size() > layer);

    LayerData* mip = _layers[layer].getMip(mipLevel);
    // Decompressed data is always UByte-RGBA
    const U32 pixelStride = _compressed ? 4 : _bpp / 8;
    const I32 idx = ((y * mip->_dimensions.width + x) * pixelStride);

    if (_compressed) {
        // Decompressed data is always UByte-RGBA
        const U8* src = _decompressedData.data();
        r = src[idx + 0];
        g = src[idx + 1];
        b = src[idx + 2]; 
        a = hasAlphaChannel() ? src[idx + 3] : 255;
    } else {
        if (_isHDR) {
            const F32* src = static_cast<F32*>(mip->data());
            r = F32ToU8Colour(src[idx + 0]);
            g = F32ToU8Colour(src[idx + 1]);
            b = F32ToU8Colour(src[idx + 2]);
            a = hasAlphaChannel() ? F32ToU8Colour(src[idx + 3]) : 255;
        } else if (_16Bit) {
            const U16* src = static_cast<U16*>(mip->data());
            r = U16ToU8Colour(src[idx + 0]); 
            g = U16ToU8Colour(src[idx + 1]);
            b = U16ToU8Colour(src[idx + 2]);
            a = hasAlphaChannel() ? U16ToU8Colour(src[idx + 3]) : 255;
        } else {
            const U8* src = static_cast<U8*>(mip->data());
            r = U8ToU8Colour(src[idx + 0]);
            g = U8ToU8Colour(src[idx + 1]);
            b = U8ToU8Colour(src[idx + 2]);
            a = hasAlphaChannel() ? U8ToU8Colour(src[idx + 3]) : 255;
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

