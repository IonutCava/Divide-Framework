/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _UTILITY_IMAGETOOLS_H
#define _UTILITY_IMAGETOOLS_H
#include "Platform/Video/Headers/RenderAPIEnums.h"

namespace Divide {
namespace ImageTools {

struct LayerData {
    virtual ~LayerData() = default;
    [[nodiscard]] virtual bufferPtr data() const = 0;

    /// the image data as it was read from the file / memory.
    size_t _size = 0u;
    /// with and height
    vec3<U16> _dimensions = { 0, 0, 1 };
};

template<typename T>
struct ImageMip final : LayerData {

    explicit ImageMip(T* data, size_t len, const U16 width, const U16 height, const U16 depth)
        : _data(len, nullptr)
    {
        if (data != nullptr) {
            std::memcpy(_data.data(), data, len * sizeof(T));
        }

        _size = len;
        _dimensions.set(width, height, depth);
    }

    [[nodiscard]] bufferPtr data() const override { return (bufferPtr)_data.data(); }

protected:
    vectorEASTL<T> _data;
};

struct ImageLayer {
    template<typename T>
    [[nodiscard]] T* allocateMip(T* data, size_t len, U16 width, U16 height, U16 depth) {
        assert(_mips.size() < U8_MAX - 1);

        _mips.emplace_back(eastl::make_unique<ImageMip<T>>(data, len, width, height, depth));
        return static_cast<T*>(_mips.back()->data());
    }

    template<typename T>
    [[nodiscard]] T* allocateMip(const size_t len, const U16 width, const U16 height, const U16 depth) {
        return allocateMip<T>(nullptr, len, width, height, depth);
    }

     [[nodiscard]] bufferPtr data(const U8 mip) const {
        if (_mips.size() <= mip) {
            return nullptr;
        }

        return _mips[mip]->data();
    }

     [[nodiscard]] LayerData* getMip(const U8 mip) const noexcept {
        if (mip < _mips.size()) {
            return _mips[mip].get();
        }

        return nullptr;
    }

     [[nodiscard]] U8 mipCount() const noexcept {
        return to_U8(_mips.size());
    }

private:
    vectorEASTL<eastl::unique_ptr<LayerData>> _mips;
};

struct ImageData final : NonCopyable {
    /// image origin information
    void flip(const bool state) noexcept { _flip = state; }
    [[nodiscard]] bool flip() const noexcept { return _flip; }

    void set16Bit(const bool state) noexcept { _16Bit = state; }
    [[nodiscard]] bool is16Bit() const noexcept { return _16Bit; }

    [[nodiscard]] bool isHDR() const noexcept { return _isHDR; }

    /// set and get the image's actual data 
    [[nodiscard]] bufferPtr data(const U32 layer, const U8 mipLevel) const {
        if (layer < _layers.size() &&  mipLevel < mipCount()) {
            // triple data-ception
            return _layers[layer].data(mipLevel);
        }

        return nullptr;
    }

    [[nodiscard]] const vectorEASTL<ImageLayer>& imageLayers() const noexcept {
        return _layers;
    }
    /// image width, height and depth
    [[nodiscard]] const vec3<U16>& dimensions(const U32 layer, const U8 mipLevel = 0u) const {
        assert(mipLevel < mipCount());
        assert(layer < _layers.size());

        return _layers[layer].getMip(mipLevel)->_dimensions;
    }

    /// set and get the image's compression state
    [[nodiscard]] bool compressed() const noexcept { return _compressed; }
    /// get the number of pre-loaded mip maps (same number for each layer)
    [[nodiscard]] U8 mipCount() const { return _layers.empty() ? 0u : _layers.front().mipCount(); }
    /// get the total number of image layers
    [[nodiscard]] U32 layerCount() const noexcept { return to_U32(_layers.size()); }
    /// image transparency information
    [[nodiscard]] bool alpha() const noexcept { return _alpha; }
    /// image depth information
    [[nodiscard]] U8 bpp() const noexcept { return _bpp; }
    /// the filename from which the image is created
    [[nodiscard]] const stringImpl& name() const noexcept { return _name; }
    /// the image format as given by STB
    [[nodiscard]] GFXImageFormat format() const noexcept { return _format; }

    [[nodiscard]] GFXDataFormat dataType() const noexcept { return _dataType; }
    /// get the texel colour at the specified offset from the origin
    [[nodiscard]] UColour4 getColour(I32 x, I32 y, U32 layer = 0u, U8 mipLevel = 0u) const;
    void getColour(I32 x, I32 y, U8& r, U8& g, U8& b, U8& a, U32 layer = 0u, U8 mipLevel = 0u) const;

    void getRed(I32 x, I32 y, U8& r, U32 layer = 0u, U8 mipLevel = 0u) const;
    void getGreen(I32 x, I32 y, U8& g, U32 layer = 0u, U8 mipLevel = 0u) const;
    void getBlue(I32 x, I32 y, U8& b, U32 layer = 0u, U8 mipLevel = 0u) const;
    void getAlpha(I32 x, I32 y, U8& a, U32 layer = 0u, U8 mipLevel = 0u) const;

    [[nodiscard]] TextureType compressedTextureType() const noexcept { return _compressedTextureType; }

    [[nodiscard]] bool addLayer(Byte* data, size_t size, U16 width, U16 height, U16 depth);
    /// creates this image instance from the specified data
    [[nodiscard]] bool addLayer(bool srgb, U16 refWidth, U16 refHeight, const stringImpl& fileName);

  protected:
    friend class ImageDataInterface;
    [[nodiscard]] bool loadDDS_IL(bool srgb, U16 refWidth, U16 refHeight, const stringImpl& filename);

   private:
    //Each entry is a separate mip map.
    vectorEASTL<ImageLayer> _layers{};
    vectorEASTL<U8> _decompressedData{};
    /// is the image stored as a regular image or in a compressed format? (eg. DXT1 / DXT3 / DXT5)
    bool _compressed = false;
    /// should we flip the image's origin on load?
    bool _flip = false;
    /// 16bit data
    bool _16Bit = false;
    /// HDR data
    bool _isHDR = false;
    /// does the image have transparency?
    bool _alpha = false;
    /// the image format
    GFXImageFormat _format = GFXImageFormat::COUNT;
    /// the image date type
    GFXDataFormat _dataType = GFXDataFormat::COUNT;
    /// used by compressed images to load 2D/3D/cubemap textures etc
    TextureType _compressedTextureType = TextureType::COUNT;
    /// the actual image filename
    stringImpl _name{};
    /// image's bits per pixel
    U8 _bpp = 0;
};

class ImageDataInterface {
public:
    //refWidth/Height = if not 0, we will attempt to resize the texture to the specified dimensions
    static bool CreateImageData(const ResourcePath& filename, U16 refWidth, U16 refHeight, bool srgb, ImageData& imgOut);
protected:
    friend struct ImageData;
    /// used to lock image loader in a sequential operating mode in a multithreaded environment
    static Mutex _loadingMutex;
};

/// save a single file to TGA
I8 SaveToTGA(const stringImpl& filename, const vec2<U16>& dimensions, U8 pixelDepth, U8* imageData) noexcept;
/// save a single file to tga using a sequential naming pattern
I8 SaveSeries(const stringImpl& filename, const vec2<U16>& dimensions, U8 pixelDepth, U8* imageData);

}  // namespace ImageTools
}  // namespace Divide

#endif
