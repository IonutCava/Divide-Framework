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
#ifndef RESOURCE_LOADER_H_
#define RESOURCE_LOADER_H_

#include "ResourceDescriptor.h"
#include "Resource.h"
#include "Core/Headers/PlatformContextComponent.h"

namespace Divide {

class ResourceCache;
// Used to delete resources
struct DeleteResource {
    explicit DeleteResource(ResourceCache* context) noexcept
        : _context(context)
    {
    }

    void operator()(CachedResource* res) const;

    ResourceCache* _context = nullptr;
};

class PlatformContext;
class NOINITVTABLE ResourceLoader : public PlatformContextComponent {
   public:
    ResourceLoader(ResourceCache* cache, PlatformContext& context, const ResourceDescriptor& descriptor, const size_t loadingDescriptorHash)
        : PlatformContextComponent(context),
          _cache(cache),
          _descriptor(descriptor),
          _loadingDescriptorHash(loadingDescriptorHash)
    {
    }

    virtual CachedResource_ptr operator()() = 0;

   protected:
    ResourceCache* _cache;
    ResourceDescriptor _descriptor;
    size_t _loadingDescriptorHash;
};

template <typename ResourceType>
class ImplResourceLoader final : public ResourceLoader {
   public:
    ImplResourceLoader(ResourceCache* cache, PlatformContext& context, const ResourceDescriptor& descriptor, const size_t loadingDescriptorHash)
        : ResourceLoader(cache, context, descriptor, loadingDescriptorHash)
    {
    }

    CachedResource_ptr operator()() override;

   protected:

    static bool Load(std::shared_ptr<ResourceType> res) {
        if (res) {
            res->setState(ResourceState::RES_LOADING);
            return res->load();
        }

        return false;
    }

};

}  // namespace Divide

#endif
