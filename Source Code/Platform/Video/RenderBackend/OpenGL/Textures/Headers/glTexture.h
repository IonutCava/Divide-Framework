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
#ifndef _GL_TEXTURE_H_
#define _GL_TEXTURE_H_

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {
class glLockManager;
class glTexture final : public Texture,
                        public glObject {
   public:
    explicit glTexture(GFXDevice& context,
                       size_t descriptorHash,
                       const Str256& name,
                       const ResourcePath& resourceName,
                       const ResourcePath& resourceLocation,
                       bool isFlipped,
                       bool asyncLoad,
                       const TextureDescriptor& texDescriptor);
    ~glTexture();

    [[nodiscard]] SamplerAddress getGPUAddress(size_t samplerHash) override;

    bool unload() override;

    void bindLayer(U8 slot, U8 level, U8 layer, bool layered, Image::Flag rwFlag) override;

    void loadData(const ImageTools::ImageData& imageLayers) override;
    void loadData(const std::pair<Byte*, size_t>& ptr, const vec2<U16>& dimensions) override;

    void clearData(const UColour4& clearColour, U8 level) const override;
    void clearSubData(const UColour4& clearColour, U8 level, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const override;

    static void copy(const TextureData& source, const TextureData& destination, const CopyTexParams& params);

    std::pair<std::shared_ptr<Byte[]>, size_t> readData(U16 mipLevel, GFXDataFormat desiredFormat) const override;

    static void CleanMemory() noexcept;

   protected:
    void threadedLoad() override;
    void reserveStorage(bool fromFile) const;
    void updateMipsInternal() const;

    void loadDataCompressed(const ImageTools::ImageData& imageData);

    void loadDataUncompressed(const ImageTools::ImageData& imageData) const;
    void clearDataInternal(const UColour4& clearColour, U8 level, bool clearRect, const vec4<I32>& rectToClear, const vec2<I32>& depthRange) const;

    void prepareTextureData(U16 width, U16 height);
    void submitTextureData();

    void validateDescriptor();

   private:
    GLenum _type;
    TextureData _loadingData;
    glLockManager* _lockManager;


    using TextureAddressMap = hashMap<size_t, SamplerAddress>;
    // One GPU address per handle + sampler combo
    SharedMutex _gpuAddressesLock;
    static Mutex s_GLgpuAddressesLock;
    TextureAddressMap _gpuAddresses;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(glTexture);

};  // namespace Divide

#endif