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
#ifndef _POST_EFFECTS_H
#define _POST_EFFECTS_H

#include "PreRenderBatch.h"
#include "Core/Headers/PlatformContextComponent.h"

namespace Divide {

class Quad3D;
class Camera;
class GFXDevice;
class ShaderProgram;
class Texture;

class PostFX final : public PlatformContextComponent {
private:
    enum class FXDisplayFunction : U8 {
        Vignette = 0,
        Noise,
        Underwater,
        Normal,
        PassThrough,
        COUNT
    };

    enum class FXRoutines : U8 {
        Vignette = 0,
        Noise,
        Screen,
        COUNT
    };

    enum class TexOperatorBindPoint : U8 {
        TEX_BIND_POINT_SCREEN = to_base(TextureUsage::UNIT0),
        TEX_BIND_POINT_BORDER = to_base(TextureUsage::UNIT1),
        TEX_BIND_POINT_NOISE = to_base(TextureUsage::SCENE_NORMALS),
        TEX_BIND_POINT_UNDERWATER = to_base(TextureUsage::OPACITY),
        TEX_BIND_POINT_LINDEPTH = to_base(TextureUsage::OCCLUSION),
        TEX_BIND_POINT_DEPTH = to_base(TextureUsage::DEPTH),
        TEX_BIND_POINT_SSR = to_base(TextureUsage::NORMALMAP),
        TEX_BIND_POINT_SCENE_DATA = to_base(TextureUsage::ROUGHNESS),
        TEX_BIND_POINT_SCENE_VELOCITY = to_base(TextureUsage::METALNESS),
        COUNT
    };

public:
    explicit PostFX(PlatformContext& context, ResourceCache* cache);
    ~PostFX();

    void prePass(PlayerIndex idx, const CameraSnapshot& cameraSnapshot,GFX::CommandBuffer& bufferInOut);
    void apply(PlayerIndex idx, const CameraSnapshot& cameraSnapshot, GFX::CommandBuffer& bufferInOut);

    void idle(const Configuration& config);
    void update(U64 deltaTimeUSFixed, U64 deltaTimeUSApp);

    void updateResolution(U16 newWidth, U16 newHeight);

    void pushFilter(const FilterType filter, const bool overrideScene = false) {
        if (!getFilterState(filter)) {
            if (overrideScene) {
                SetBit(_overrideFilterStack, 1u << to_U32(filter));
            } else {
                SetBit(_filterStack, 1u << to_U32(filter));
            }
            _filtersDirty = true;
            getFilterBatch().onFilterToggle(filter, true);
        }
    }

    void popFilter(const FilterType filter, const bool overrideScene = false) {
        if (getFilterState(filter)) {
            if (overrideScene) {
                ClearBit(_overrideFilterStack, 1u << to_U32(filter));
            } else {
                ClearBit(_filterStack, 1u << to_U32(filter));
            }
            _filtersDirty = true;
            getFilterBatch().onFilterToggle(filter, false);
        }
    }

    [[nodiscard]] bool getFilterState(const FilterType filter) const noexcept {
        return BitCompare(_filterStack, 1u << to_U32(filter)) ||
               BitCompare(_overrideFilterStack, 1u << to_U32(filter));
    }

    [[nodiscard]] PreRenderBatch& getFilterBatch() noexcept {
        return _preRenderBatch;
    }

    // fade the screen to the specified colour lerping over the specified time interval
    // set durationMS to instantly set fade colour
    // optionally, set a callback when fade out completes
    // waitDurationMS = how much time to wait before calling the completion callback after fade out completes
    void setFadeOut(const UColour3& targetColour, D64 durationMS, D64 waitDurationMS, DELEGATE<void> onComplete = DELEGATE<void>());
    // clear any fading effect currently active over the specified time interval
    // set durationMS to instantly clear the fade effect
    // optionally, set a callback when fade in completes
    void setFadeIn(D64 durationMS, DELEGATE<void> onComplete = DELEGATE<void>());
    // fade out to specified colour and back again within the given time slice
    // if duration is 0.0, nothing happens
    // waitDurationMS is the amount of time to wait before fading back in
    void setFadeOutIn(const UColour3& targetColour, D64 durationFadeOutMS, D64 waitDurationMS);
    void setFadeOutIn(const UColour3& targetColour, D64 durationFadeOutMS, D64 durationFadeInMS, D64 waitDurationMS);

    [[nodiscard]] static const char* FilterName(FilterType filter) noexcept;

    // Use this to change various PostFX settings that depend on the time of day (e.g. exterior cube reflections, LUT correction, etc)
    PROPERTY_RW(bool, isDayTime, true);
private:
    PreRenderBatch _preRenderBatch;
    /// Screen Border
    Texture_ptr _screenBorder = nullptr;
    /// Noise
    Texture_ptr _noise = nullptr;

    F32 _randomNoiseCoefficient = 0.0f, _randomFlashCoefficient = 0.0f;
    D64 _noiseTimer = 0.0, _tickInterval = 1.0;

    ShaderProgram_ptr _postProcessingShader = nullptr;
    Texture_ptr _underwaterTexture = nullptr;
    vec2<U16> _resolutionCache;

    RTDrawDescriptor _postFXTarget;

    //fade settings
    D64 _currentFadeTimeMS = 0.0;
    D64 _targetFadeTimeMS = 0.0;
    D64 _fadeWaitDurationMS = 0.0;
    bool _fadeOut = false;
    bool _fadeActive = false;
    DELEGATE<void> _fadeOutComplete;
    DELEGATE<void> _fadeInComplete;

    U32 _filterStack = 0u;
    U32 _overrideFilterStack = 0u;

    bool _filtersDirty = true;

    Pipeline* _drawPipeline = nullptr;
    GFX::SendPushConstantsCommand _drawConstantsCmd;
    GFX::SetCameraCommand _setCameraCmd;
};

}  // namespace Divide

#endif
