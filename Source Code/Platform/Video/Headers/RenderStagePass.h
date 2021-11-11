/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _RENDER_STAGE_PASS_H_
#define _RENDER_STAGE_PASS_H_

#include "RenderAPIEnums.h"

namespace Divide {

static constexpr U8  g_AllVariantsID = U8_MAX;
static constexpr U16 g_AllPassID = U16_MAX;
static constexpr U16 g_AllIndicesID = g_AllPassID;

struct RenderStagePass {
    RenderStagePass() = default;
    explicit RenderStagePass(const RenderStage stage, const RenderPassType passType, const U8 variant = 0u, const U16 index = 0u, const U16 pass = 0u) noexcept 
        : _stage(stage),
          _passType(passType),
          _variant(variant),
          _index(index),
          _pass(pass)
    {}

    RenderStage _stage = RenderStage::COUNT;
    RenderPassType _passType = RenderPassType::COUNT;
    U8 _variant = 0;
    U16 _index = 0u; //usually some kind of type info (reflector/refractor index, light type, etc)
    U16 _pass = 0u; //usually some kind of actual pass index (eg. cube face we are rendering into)

    [[nodiscard]] bool isDepthPass() const noexcept {
        return _stage == RenderStage::SHADOW || _passType == RenderPassType::PRE_PASS;
    }

    /// This ignores the variant and pass index flags!
    [[nodiscard]] U8 baseIndex() const noexcept {
        return BaseIndex(_stage, _passType);
    }

    static constexpr U8 BaseIndex(const RenderStage stage, const RenderPassType passType) noexcept {
        return static_cast<U8>(to_base(stage) + to_base(passType) * to_base(RenderStage::COUNT));
    }

    [[nodiscard]] static RenderStagePass FromBaseIndex(const U8 baseIndex) noexcept {
        return RenderStagePass
        {
            static_cast<RenderStage>(baseIndex % to_base(RenderStage::COUNT)),
            static_cast<RenderPassType>(baseIndex / to_base(RenderStage::COUNT))
        };
    }

    [[nodiscard]] static U16 IndexForStage(const RenderStagePass& renderStagePass) {
        switch (renderStagePass._stage) {
            case RenderStage::DISPLAY:
            {
                assert(renderStagePass._variant == renderStagePass._index == renderStagePass._pass == 0u);
                return 0u;
            }
            case RenderStage::REFLECTION: 
            {
                // All reflectors could be cubemaps.
                // For a simple planar reflection, pass should be 0
                assert(renderStagePass._variant != to_base(ReflectorType::PLANAR) || renderStagePass._pass == 0);
                return (renderStagePass._index * 6) + renderStagePass._pass;
            }
            case RenderStage::REFRACTION:
            {
                // Refraction targets are only planar for now
                assert(renderStagePass._variant == to_base(RefractorType::PLANAR));
                return renderStagePass._index;
            }
            case RenderStage::SHADOW:
            {
                // The really complicated one:
                // 1) Find the proper offset for the current light type (stored in _variant)
                constexpr U16 offsetDir = 0u;
                constexpr U16 offsetPoint = offsetDir + Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;
                constexpr U16 offsetSpot = offsetPoint + Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS * 6;
                const U16 lightIndex = renderStagePass._index; // Just a value that gets increment by one for each light we process
                const U16 lightPass = renderStagePass._pass;   // Either cube face index or CSM split number

                switch(renderStagePass._variant) {
                    case to_base(LightType::DIRECTIONAL): 
                        return Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT * lightIndex + offsetDir + lightPass;
                    case to_base(LightType::POINT): 
                        return 6 * lightIndex + offsetPoint + lightPass;
                    case to_base(LightType::SPOT): 
                        assert(lightPass == 0u);
                        return lightIndex + offsetSpot;
                    default: 
                        DIVIDE_UNEXPECTED_CALL();
                }
            }
            case RenderStage::COUNT:
                [[fallthrough]];
            default: 
                DIVIDE_UNEXPECTED_CALL();
        }

        DIVIDE_UNEXPECTED_CALL();
        return 0u;
    }

    [[nodiscard]] static U8 TotalPassCountForStage(const RenderStage renderStage) {
        switch (renderStage) {
            case RenderStage::DISPLAY:
                return 1u;
            case RenderStage::REFLECTION:
                return (Config::MAX_REFLECTIVE_NODES_IN_VIEW + Config::MAX_REFLECTIVE_PROBES_PER_PASS) * 6u;
            case RenderStage::REFRACTION:
                return Config::MAX_REFRACTIVE_NODES_IN_VIEW;
            case RenderStage::SHADOW:
                return Config::Lighting::MAX_SHADOW_PASSES;
            default:
                DIVIDE_UNEXPECTED_CALL();
        }

        DIVIDE_UNEXPECTED_CALL();
        return to_U8(1u);
    }

    [[nodiscard]] static U16 TotalPassCountForAllStages() {
        U16 ret = 0u;
        for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
            ret += TotalPassCountForStage(static_cast<RenderStage>(i));
        }
        return ret;
    }

    [[nodiscard]] static U8 PassCountForStagePass(const RenderStagePass& renderStagePass) {
        switch (renderStagePass._stage) {
            case RenderStage::DISPLAY:
                return 1u;
            case RenderStage::REFLECTION:
                return 6u; //Worst case, all nodes need cubemaps
            case RenderStage::REFRACTION:
                return 1u;
            case RenderStage::SHADOW:
                switch (renderStagePass._variant) {
                    case to_base(LightType::DIRECTIONAL): return Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;
                    case to_base(LightType::POINT): return 6u;
                    case to_base(LightType::SPOT): return 1u;
                    default: DIVIDE_UNEXPECTED_CALL();
                }break;
                
            default:
                DIVIDE_UNEXPECTED_CALL();
        }

        DIVIDE_UNEXPECTED_CALL();
        return to_U8(1u);
    }

    inline bool operator==(const RenderStagePass& other) const noexcept {
        return _variant == other._variant &&
               _pass == other._pass &&
               _index == other._index &&
               _stage == other._stage &&
               _passType == other._passType;
    }

    inline bool operator!=(const RenderStagePass& other) const noexcept {
        return _variant != other._variant ||
               _pass != other._pass ||
               _index != other._index ||
               _stage != other._stage ||
               _passType != other._passType;
    }

};

}; //namespace Divide

#endif //_RENDER_STAGE_PASS_H_