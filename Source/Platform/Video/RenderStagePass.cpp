

#include "Headers/RenderStagePass.h"
#include "Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h"

namespace Divide
{
    U16 IndexForStage( const RenderStagePass renderStagePass )
    {
        switch ( renderStagePass._stage )
        {
            case RenderStage::DISPLAY:
            case RenderStage::NODE_PREVIEW:
            {
                assert( renderStagePass._variant == RenderStagePass::VariantType::VARIANT_0 );
                assert( renderStagePass._pass == RenderStagePass::PassIndex::PASS_0 );
                assert( renderStagePass._index == 0u );
                return renderStagePass._index + to_U16(renderStagePass._pass);
            }
            case RenderStage::REFLECTION:
            {
                switch (renderStagePass._variant)
                {
                    case static_cast<RenderStagePass::VariantType>(ReflectorType::CUBE):
                    {
                        return (renderStagePass._index * 6u) + to_U16(renderStagePass._pass);
                    }
                    case static_cast<RenderStagePass::VariantType>(ReflectorType::PLANAR):
                    {
                        assert(renderStagePass._pass == RenderStagePass::PassIndex::PASS_0);
                        return GetNodeReflectionIndexOffset() +
                               (Config::MAX_REFLECTIVE_CUBE_NODES_IN_VIEW * 6u) + 
                               renderStagePass._index;
                    }

                    default:
                    case static_cast<RenderStagePass::VariantType>(ReflectorType::COUNT):
                        break;
                }

                DIVIDE_UNEXPECTED_CALL();
                return 0u;
            }
            case RenderStage::REFRACTION:
            {
                switch (renderStagePass._variant)
                {
                    case static_cast<RenderStagePass::VariantType>(RefractorType::CUBE):
                    {
                        return (renderStagePass._index * 6u) + to_U16(renderStagePass._pass);
                    }
                    case static_cast<RenderStagePass::VariantType>(RefractorType::PLANAR):
                    {
                        assert(renderStagePass._pass == RenderStagePass::PassIndex::PASS_0);
                        return GetNodeRefractionIndexOffset() +
                               (Config::MAX_REFRACTIVE_CUBE_NODES_IN_VIEW * 6u) + 
                               renderStagePass._index;
                    }
                    default:
                    case static_cast<RenderStagePass::VariantType>(RefractorType::COUNT):
                        break;
                }

                DIVIDE_UNEXPECTED_CALL();
                return 0u;
            }
            case RenderStage::SHADOW:
            {

                // The really complicated one:
                // 1) Find the proper offset for the current light type (stored in _variant)
                constexpr U16 offsetDir = 0u;
                constexpr U16 offsetPoint = offsetDir + (Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT) + 1u/*world AO*/;
                constexpr U16 offsetSpot = offsetPoint + Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS * 6;
                const U16 lightIndex = renderStagePass._index; // Just a value that gets increment by one for each light we process
                const U8 lightPass = to_base( renderStagePass._pass );   // Either cube face index or CSM split number

                switch ( to_base( renderStagePass._variant ) )
                {
                    case to_base( ShadowType::CSM ):
                    {
                        if ( renderStagePass._index == ShadowMap::WORLD_AO_LAYER_INDEX )
                        {
                            return ShadowMap::WORLD_AO_LAYER_INDEX;
                        }

                        return Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT * lightIndex + offsetDir + lightPass;
                    }
                    case to_base( ShadowType::CUBEMAP ):
                    {
                        return 6 * lightIndex + offsetPoint + lightPass;
                    }
                    case to_base( ShadowType::SINGLE ):
                    {
                        assert( lightPass == 0u );
                        return lightIndex + offsetSpot;
                    }
                    default:
                        DIVIDE_UNEXPECTED_CALL();
                        break;
                }
            } break;
            case RenderStage::COUNT:
            default: DIVIDE_UNEXPECTED_CALL(); break;
        }

        DIVIDE_UNEXPECTED_CALL();
        return 0u;
    }

} //namespace Divide
