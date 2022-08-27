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

static constexpr U16 g_AllIndicesID = U16_MAX;

struct RenderStagePass {
    //These enums are here so that we have some sort of upper bound for allocations based on render stages
    //If we know the maximum number of passes and variants, it is easier to allocate memory beforehand
    //These enums can easily be modified (but that will affect memory consumption)

    enum class VariantType : U8 {
        VARIANT_0 = 0u,
        VARIANT_1,
        VARIANT_2,
        COUNT
    };

    enum class PassIndex : U8 {
        PASS_0 = 0u,
        PASS_1,
        PASS_2,
        PASS_3,
        PASS_4,
        PASS_5,
        COUNT
    };

    RenderStage _stage = RenderStage::COUNT;
    RenderPassType _passType = RenderPassType::COUNT;
    U16 _index = 0u; // reflector index, light index ,etc
    VariantType _variant = VariantType::VARIANT_0;// reflector type, light type, etc
    PassIndex _pass = PassIndex::PASS_0;          // usually some kind of actual pass index (eg. cube face we are rendering into, current CSM split, etc)
};

    [[nodiscard]] static constexpr bool IsShadowPass(const RenderStagePass stagePass) noexcept {
        return stagePass._stage == RenderStage::SHADOW;
    }

    [[nodiscard]] static constexpr bool IsDepthPrePass(const RenderStagePass stagePass) noexcept {
        return IsShadowPass(stagePass) || stagePass._passType == RenderPassType::PRE_PASS;
    }  
    
    [[nodiscard]] static constexpr bool IsDepthPass(const RenderStagePass stagePass) noexcept {
        return IsShadowPass(stagePass) || stagePass._passType == RenderPassType::PRE_PASS;
    }
   
    [[nodiscard]] static constexpr bool IsZPrePass(const RenderStagePass stagePass) noexcept {
        return stagePass._stage == RenderStage::DISPLAY && stagePass._passType == RenderPassType::PRE_PASS;
    }

    static constexpr U8 BaseIndex(const RenderStage stage, const RenderPassType passType) noexcept {
        return static_cast<U8>(to_base(stage) + to_base(passType) * to_base(RenderStage::COUNT));
    }

    /// This ignores the variant and pass index flags!
    [[nodiscard]] static constexpr U8 BaseIndex(const RenderStagePass stagePass) noexcept {
        return BaseIndex(stagePass._stage, stagePass._passType);
    }

    [[nodiscard]] static constexpr RenderStagePass FromBaseIndex(const U8 baseIndex) noexcept {
        return RenderStagePass
        {
            static_cast<RenderStage>(baseIndex % to_base(RenderStage::COUNT)),
            static_cast<RenderPassType>(baseIndex / to_base(RenderStage::COUNT))
        };
    }

    [[nodiscard]] inline U16 IndexForStage(const RenderStagePass renderStagePass) {
        switch (renderStagePass._stage) {
            case RenderStage::DISPLAY:
            {
                assert(renderStagePass._variant == RenderStagePass::VariantType::VARIANT_0);
                assert(renderStagePass._pass == RenderStagePass::PassIndex::PASS_0);
                assert(renderStagePass._index == 0u);
                return 0u;
            }
            case RenderStage::REFLECTION: 
            {
                // All reflectors could be cubemaps.
                // For a simple planar reflection, pass should be 0
                assert(to_base(renderStagePass._variant) != to_base(ReflectorType::PLANAR) || renderStagePass._pass == RenderStagePass::PassIndex::PASS_0);
                return (renderStagePass._index * 6) + to_base(renderStagePass._pass);
            }
            case RenderStage::REFRACTION:
            {
                // Refraction targets are only planar for now
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
                const U8 lightPass = to_base(renderStagePass._pass);   // Either cube face index or CSM split number

                switch(to_base(renderStagePass._variant)) {
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
            } break;
            case RenderStage::COUNT:
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }

        DIVIDE_UNEXPECTED_CALL();
        return 0u;
    }

    [[nodiscard]] static constexpr U8 TotalPassCountForStage(const RenderStage renderStage) {
        switch (renderStage) {
            case RenderStage::DISPLAY:
                return 1u;
            case RenderStage::REFLECTION:
                return (Config::MAX_REFLECTIVE_NODES_IN_VIEW + Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u/*SkyLight*/) * 6u;
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

    [[nodiscard]] static constexpr U16 TotalPassCountForAllStages() {
        U16 ret = 0u;
        for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i) {
            ret += TotalPassCountForStage(static_cast<RenderStage>(i));
        }
        return ret;
    }

    [[nodiscard]] inline U8 PassCountForStagePass(RenderStagePass renderStagePass) {
        switch (renderStagePass._stage) {
            case RenderStage::DISPLAY:
                return 1u;
            case RenderStage::REFLECTION:
                return 6u; //Worst case, all nodes need cubemaps
            case RenderStage::REFRACTION:
                return 1u;
            case RenderStage::SHADOW:
                switch (renderStagePass._variant) {
                    case static_cast<RenderStagePass::VariantType>(to_base(LightType::DIRECTIONAL)): return Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT;
                    case static_cast<RenderStagePass::VariantType>(to_base(LightType::POINT)): return 6u;
                    case static_cast<RenderStagePass::VariantType>(to_base(LightType::SPOT)): return 1u;
                    default: DIVIDE_UNEXPECTED_CALL();
                }break;
                
            default:
                DIVIDE_UNEXPECTED_CALL();
        }
         
        DIVIDE_UNEXPECTED_CALL();
        return to_U8(1u);
    }

    inline bool operator==(const RenderStagePass lhs, const RenderStagePass rhs) noexcept {
        return lhs._variant  == rhs._variant &&
               lhs._pass     == rhs._pass &&
               lhs._index    == rhs._index &&
               lhs._stage    == rhs._stage &&
               lhs._passType == rhs._passType;
    }

    inline bool operator!=(const RenderStagePass lhs, const RenderStagePass rhs) noexcept {
        return lhs._variant  != rhs._variant ||
               lhs._pass     != rhs._pass ||
               lhs._index    != rhs._index ||
               lhs._stage    != rhs._stage ||
               lhs._passType != rhs._passType;
    }
}; //namespace Divide

#endif //_RENDER_STAGE_PASS_H_