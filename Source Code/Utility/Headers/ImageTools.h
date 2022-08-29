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

#include "ImageToolsFwd.h"

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

    explicit ImageMip(const T* data, size_t len, const U16 width, const U16 height, const U16 depth, const U8 numComponents)
    {
        const size_t totalSizeTest = to_size(width) * height * depth * numComponents;
        const size_t actualSize = std::max(len, totalSizeTest);

        _data.resize(actualSize, T{ 0u });

        if (data != nullptr && len > 0u) {
            std::memcpy(_data.data(), data, len * sizeof(T));
        }

        _size = actualSize;
        _dimensions.set(width, height, depth);
    }

    [[nodiscard]] bufferPtr data() const noexcept override { return (bufferPtr)_data.data(); }

protected:
    vector<T> _data;
};

struct ImageLayer {
    template<typename T>
    [[nodiscard]] T* allocateMip(const T* data, size_t len, U16 width, U16 height, U16 depth, const U8 numComponents) {
        assert(_mips.size() < U8_MAX - 1);

        _mips.emplace_back(eastl::make_unique<ImageMip<T>>(data, len, width, height, depth, numComponents));
        return static_cast<T*>(_mips.back()->data());
    }

    template<typename T>
    [[nodiscard]] T* allocateMip(const size_t len, const U16 width, const U16 height, const U16 depth, const U8 numComponents) {
        return allocateMip<T>(nullptr, len, width, height, depth, numComponents);
    }

    [[nodiscard]] bufferPtr data(const U8 mip) const {
        if (_mips.size() <= mip) {
            return nullptr;
        }

        return _mips[mip]->data();
    }

    [[nodiscard]] LayerData* getMip(const U8 mip) const {
        if (mip < _mips.size()) {
            return _mips[mip].get();
        }

        return nullptr;
    }

    [[nodiscard]] U8 mipCount() const noexcept {
        return to_U8(_mips.size());
    }

private:
    vector<LayerData_uptr> _mips;
};

struct ImageData final : NonCopyable {
    /// image origin information
    void requestedFormat(const GFXDataFormat format) noexcept { _requestedDataFormat = format; }

    /// set and get the image's actual data 
    [[nodiscard]] bufferPtr data(const U32 layer, const U8 mipLevel) const {
        if (layer < _layers.size() &&  mipLevel < mipCount()) {
            // triple data-ception
            return _layers[layer].data(mipLevel);
        }

        return nullptr;
    }

    [[nodiscard]] const vector<ImageLayer>& imageLayers() const noexcept {
        return _layers;
    }

    /// image width, height and depth
    [[nodiscard]] const vec3<U16>& dimensions(const U32 layer, const U8 mipLevel = 0u) const {
        assert(mipLevel < mipCount());
        assert(layer < _layers.size());

        return _layers[layer].getMip(mipLevel)->_dimensions;
    }

    /// get the number of pre-loaded mip maps (same number for each layer)
    [[nodiscard]] U8 mipCount() const { return _layers.empty() ? 0u : _layers.front().mipCount(); }
    /// get the total number of image layers
    [[nodiscard]] U32 layerCount() const noexcept { return to_U32(_layers.size()); }
    /// image depth information
    [[nodiscard]] U8 bpp() const noexcept { return _bpp; }
    /// the filename from which the image is created
    [[nodiscard]] const ResourcePath& name() const noexcept { return _name; }
    /// the image format as given by STB
    [[nodiscard]] GFXImageFormat format() const noexcept { return _format; }

    [[nodiscard]] GFXDataFormat dataType() const noexcept { return _dataType; }
    /// get the texel colour at the specified offset from the origin
    [[nodiscard]] UColour4 getColour(I32 x, I32 y, U32 layer = 0u, U8 mipLevel = 0u) const;
    void getColour(I32 x, I32 y, U8& r, U8& g, U8& b, U8& a, U32 layer = 0u, U8 mipLevel = 0u) const;

    void getColourComponent(const I32 x, const I32 y, const U8 comp, U8& c, const U32 layer, const U8 mipLevel = 0) const;
    FORCE_INLINE void getRed(const I32 x, const I32 y, U8& r, const U32 layer, const U8 mipLevel = 0) const { getColourComponent(x, y, 0, r, layer, mipLevel); }
    FORCE_INLINE void getGreen(const I32 x, const I32 y, U8& g, const U32 layer, const U8 mipLevel = 0) const { getColourComponent(x, y, 1, g, layer, mipLevel); }
    FORCE_INLINE void getBlue(const I32 x, const I32 y, U8& b, const U32 layer, const U8 mipLevel = 0) const { getColourComponent(x, y, 2, b, layer, mipLevel); }
    FORCE_INLINE void getAlpha(const I32 x, const I32 y, U8& a, const U32 layer, const U8 mipLevel = 0) const { getColourComponent(x, y, 3, a, layer, mipLevel); }

    [[nodiscard]] bool loadFromMemory(const Byte* data, size_t size, U16 width, U16 height, U16 depth, U8 numComponents);
    /// creates this image instance from the specified data
    [[nodiscard]] bool loadFromFile(bool srgb, U16 refWidth, U16 refHeight, const ResourcePath& path, const ResourcePath& name);
    [[nodiscard]] bool loadFromFile(bool srgb, U16 refWidth, U16 refHeight, const ResourcePath& path, const ResourcePath& name, ImportOptions options);

    /// If true, then the source image's alpha channel is used for data and not opacity (so skip mip-filtering for example)
    PROPERTY_RW(bool, ignoreAlphaChannelTransparency, false);
    /// If true, then the source image was probably RGB and we loaded it as RGBA
    PROPERTY_RW(bool, hasDummyAlphaChannel, false);

  protected:
    friend class ImageDataInterface;
    [[nodiscard]] bool loadDDS_NVTT(bool srgb, U16 refWidth, U16 refHeight, const ResourcePath& path, const ResourcePath& name);
    [[nodiscard]] bool loadDDS_IL(bool srgb, U16 refWidth, U16 refHeight, const ResourcePath& path, const ResourcePath& name);

   private:
    //Each entry is a separate mip map.
    vector<ImageLayer> _layers{};
    vector<U8> _decompressedData{};
    /// the image path
    ResourcePath _path{};
    /// the actual image filename
    ResourcePath _name{};
    /// the image format
    GFXImageFormat _format = GFXImageFormat::COUNT;
    /// the image data type
    GFXDataFormat _dataType = GFXDataFormat::COUNT;
    /// the image requested data type. COUNT == AUTO
    GFXDataFormat _requestedDataFormat = GFXDataFormat::COUNT;
    /// image's bits per pixel
    U8 _bpp = 0;
    /// 16bit data
    bool _16Bit = false;
    /// HDR data
    bool _isHDR = false;
};

}  // namespace ImageTools
}  // namespace Divide

#endif
