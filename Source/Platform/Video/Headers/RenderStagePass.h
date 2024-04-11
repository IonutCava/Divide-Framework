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
#ifndef DVD_RENDER_STAGE_PASS_H_
#define DVD_RENDER_STAGE_PASS_H_

namespace Divide
{

    static constexpr U16 g_AllIndicesID = U16_MAX;

    struct RenderStagePass
    {
        //These enums are here so that we have some sort of upper bound for allocations based on render stages
        //If we know the maximum number of passes and variants, it is easier to allocate memory beforehand
        //These enums can easily be modified (but that will affect memory consumption)

        enum class VariantType : U8
        {
            VARIANT_0 = 0u,
            VARIANT_1,
            VARIANT_2,
            COUNT
        };

        enum class PassIndex : U8
        {
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

        bool operator==(const RenderStagePass&) const = default;
    };

    [[nodiscard]] U16 IndexForStage(RenderStagePass renderStagePass);

    [[nodiscard]] static constexpr bool IsShadowPass(const RenderStagePass stagePass) noexcept
    {
        return stagePass._stage == RenderStage::SHADOW;
    }

    [[nodiscard]] static constexpr bool IsDepthPass(const RenderStagePass stagePass) noexcept
    {
        return IsShadowPass(stagePass) || stagePass._passType == RenderPassType::PRE_PASS;
    }
   
    [[nodiscard]] static constexpr bool IsZPrePass(const RenderStagePass stagePass) noexcept
    {
        return stagePass._stage == RenderStage::DISPLAY && stagePass._passType == RenderPassType::PRE_PASS;
    }

    static constexpr U8 BaseIndex(const RenderStage stage, const RenderPassType passType) noexcept
    {
        return static_cast<U8>(to_base(stage) + to_base(passType) * to_base(RenderStage::COUNT));
    }

    /// This ignores the variant and pass index flags!
    [[nodiscard]] static constexpr U8 BaseIndex(const RenderStagePass stagePass) noexcept
    {
        return BaseIndex(stagePass._stage, stagePass._passType);
    }

    [[nodiscard]] static constexpr U8 TotalPassCountForStage(const RenderStage renderStage)
    {
        switch (renderStage)
        {
            case RenderStage::DISPLAY:
            case RenderStage::NODE_PREVIEW:
                return 1u;
            case RenderStage::REFRACTION:
                return Config::MAX_REFRACTIVE_NODES_IN_VIEW;
            case RenderStage::REFLECTION:
                return (Config::MAX_REFLECTIVE_NODES_IN_VIEW + Config::MAX_REFLECTIVE_PROBES_PER_PASS + 1u/*SkyLight*/) * 6u;
            case RenderStage::SHADOW:
                return Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT +
                       Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS * 6 +
                       Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS +
                       1u /*WORLD AO*/;
            default: break;
        }

        DIVIDE_UNEXPECTED_CALL();
        return U8_ONE;
    }
}; //namespace Divide

#endif //DVD_RENDER_STAGE_PASS_H_
