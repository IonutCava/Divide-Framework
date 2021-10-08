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
#ifndef _PIXEL_BUFFER_OBJECT_H
#define _PIXEL_BUFFER_OBJECT_H

#include "Platform/Headers/PlatformDefines.h"
#include "Platform/Video/Headers/TextureData.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Headers/GraphicsResource.h"

namespace Divide {

class NOINITVTABLE PixelBuffer : public GUIDWrapper, public GraphicsResource {
   public:
       PixelBuffer(GFXDevice& context, const PBType type, const char* name = nullptr)
         : GraphicsResource(context, Type::PIXEL_BUFFER, getGUID(), name == nullptr ? 0u : _ID(name)),
           _pbtype(type),
           _textureID(0),
           _width(0),
           _height(0),
           _depth(0),
           _pixelBufferHandle(0),
           _textureType(TextureType::COUNT),
           _name(name != nullptr ? name : "")
       {
       }

    virtual bool create(
        U16 width, U16 height, U16 depth = 0,
        GFXImageFormat formatEnum = GFXImageFormat::RGBA,
        GFXDataFormat dataTypeEnum = GFXDataFormat::FLOAT_32,
        bool normalized = true) = 0;

    virtual void updatePixels(const F32* pixels, U32 pixelCount) = 0;

    [[nodiscard]] U32 getTextureHandle() const noexcept { return _textureID; }
    [[nodiscard]] U16 getWidth() const noexcept { return _width; }
    [[nodiscard]] U16 getHeight() const noexcept { return _height; }
    [[nodiscard]] U16 getDepth() const noexcept { return _depth; }
    [[nodiscard]] PBType getType() const noexcept { return _pbtype; }

    [[nodiscard]] TextureData getData() const noexcept { return TextureData{ _textureID,  _textureType }; }

   protected:
    PBType _pbtype = PBType::COUNT;
    U32 _textureID;
    U16 _width, _height, _depth;
    U32 _pixelBufferHandle;
    TextureType _textureType;

    string _name;
};

};  // namespace Divide
#endif
