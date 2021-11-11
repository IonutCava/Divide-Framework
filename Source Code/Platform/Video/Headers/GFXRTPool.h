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
#ifndef _HARDWARE_VIDEO_GFX_RT_POOL_H_
#define _HARDWARE_VIDEO_GFX_RT_POOL_H_

#include "Platform/Headers/PlatformDefines.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

namespace Divide {

class GFXRTPool {
protected:
    using TargetsPerUsage = vector<std::shared_ptr<RenderTarget>>;
    using RenderTargets = std::array<TargetsPerUsage, to_base(RenderTargetUsage::COUNT)>;

protected:
    friend class GFXDevice;
    explicit GFXRTPool(GFXDevice& parent);
    ~GFXRTPool() = default;

    void resizeTargets(RenderTargetUsage target, U16 width, U16 height);
    void updateSampleCount(RenderTargetUsage target, U8 sampleCount);

    void clear();
    void set(RenderTargetID target, const std::shared_ptr<RenderTarget>& newTarget);
    RenderTargetHandle add(RenderTargetUsage targetUsage, const std::shared_ptr<RenderTarget>& newTarget, U8 index);

    bool remove(RenderTargetHandle& handle);

    void set(const RenderTargetHandle& handle, const std::shared_ptr<RenderTarget>& newTarget) {
        set(handle._targetID, newTarget);
    }

public:
    RenderTargetHandle allocateRT(RenderTargetUsage targetUsage, const RenderTargetDescriptor& descriptor);
    RenderTargetHandle allocateRT(RenderTargetUsage targetUsage, const RenderTargetDescriptor& descriptor, U8 index);

    RenderTarget& renderTarget(const RenderTargetHandle& handle) {
        return renderTarget(handle._targetID);
    }

    [[nodiscard]] const RenderTarget& renderTarget(const RenderTargetHandle& handle) const {
        return renderTarget(handle._targetID);
    }

    RenderTarget& renderTarget(const RenderTargetID target) {
        return *_renderTargets[to_U32(target._usage)][target._index];
    }

    [[nodiscard]] const RenderTarget& renderTarget(const RenderTargetID target) const {
        return *_renderTargets[to_U32(target._usage)][target._index];
    }

    // Bit of a hack, but cleans up a lot of code
    [[nodiscard]] const RenderTarget& screenTarget() const;
    [[nodiscard]] RenderTargetID screenTargetID() const noexcept;

    vector<std::shared_ptr<RenderTarget>>& renderTargets(const RenderTargetUsage target) noexcept {
        return _renderTargets[to_U32(target)];
    }

    [[nodiscard]] const vector<std::shared_ptr<RenderTarget>>& renderTargets(const RenderTargetUsage target) const noexcept {
        return _renderTargets[to_U32(target)];
    }

    RenderTargetHandle allocateRT(const RenderTargetDescriptor& descriptor) {
        return allocateRT(RenderTargetUsage::OTHER, descriptor);
    }

    bool deallocateRT(RenderTargetHandle& handle) {
        return remove(handle);
    }

protected:
    SET_SAFE_DELETE_FRIEND

    GFXDevice& _parent;
    RenderTargets _renderTargets;
};
}; //namespace Divide

#endif //_HARDWARE_VIDEO_GFX_RT_POOL_H_