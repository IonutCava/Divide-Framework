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
#ifndef GL_TEXTURE_H
#define GL_TEXTURE_H

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

class glTexture final : public Texture {
   public:
    explicit glTexture(GFXDevice& context,
                       size_t descriptorHash,
                       const Str256& name,
                       const ResourcePath& resourceName,
                       const ResourcePath& resourceLocation,
                       const TextureDescriptor& texDescriptor,
                       ResourceCache& parentCache);

    ~glTexture();

    bool unload() override;

    void bindLayer(U8 slot, U8 level, U8 layer, bool layered, Image::Flag rwFlag) override;

    void clearData(const UColour4& clearColour, U8 level) const override;
    void clearSubData(const UColour4& clearColour, U8 level, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const override;

    static void copy(const TextureData& source, U8 sourceSamples, const TextureData& destination, U8 destinationSamples, const CopyTexParams& params);

    TextureReadbackData readData(U16 mipLevel, GFXDataFormat desiredFormat) const override;

   protected:
    void postLoad() override;
    void reserveStorage();
    void loadDataInternal(const ImageTools::ImageData& imageData) override;
    void prepareTextureData(U16 width, U16 height, U16 depth) override;
    void submitTextureData() override;

    void clearDataInternal(const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const;


   private:
    GLenum _type{GL_NONE};
    glLockManager _lockManager;
    bool _hasStorage{ false };
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(glTexture);

};  // namespace Divide

#endif //GL_TEXTURE_H