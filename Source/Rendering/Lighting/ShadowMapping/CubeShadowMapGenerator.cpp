

#include "Headers/CubeShadowMapGenerator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/ProjectManager.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/Light.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "Managers/Headers/RenderPassManager.h"

namespace Divide {
namespace {
    Configuration::Rendering::ShadowMapping g_shadowSettings;
};

CubeShadowMapGenerator::CubeShadowMapGenerator(GFXDevice& context)
    : ShadowMapGenerator(context, ShadowType::CUBEMAP)
{
    Console::printfn(LOCALE_STR("LIGHT_CREATE_SHADOW_FB"), "Single Shadow Map");
    g_shadowSettings = context.context().config().rendering.shadowMapping;
}

void CubeShadowMapGenerator::render([[maybe_unused]] const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

    const vec3<F32> lightPos = light.sgn()->get<TransformComponent>()->getWorldPosition();

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Cube Shadow Pass Light: [ {} ]", lightIndex).c_str(), lightIndex));

    RenderPassParams params = {};
    params._target = ShadowMap::getShadowMap(_type)._targetID;
    params._sourceNode = light.sgn();
    params._refreshLightData = false;
    params._stagePass = { RenderStage::SHADOW, RenderPassType::MAIN_PASS, lightIndex, static_cast<RenderStagePass::VariantType>(ShadowType::CUBEMAP) };
    params._targetDescriptorMainPass._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
    params._clearDescriptorMainPass[RT_DEPTH_ATTACHMENT_IDX] = DEFAULT_CLEAR_ENTRY;
    params._clearDescriptorMainPass[to_base( RTColourAttachmentSlot::SLOT_0 )] = DEFAULT_CLEAR_ENTRY;

    mat4<F32> viewProjMatrix[6];
    _context.generateCubeMap(params,
                             light.getShadowArrayOffset(),
                             light.sgn()->get<TransformComponent>()->getWorldPosition(),
                             vec2<F32>(0.01f, light.range() * 1.1f),
                             bufferInOut,
                             memCmdInOut,
                             viewProjMatrix);

    for (U8 i = 0u; i < 6u; ++i)
    {
        light.setShadowLightPos(  i, lightPos);
        light.setShadowFloatValue(i, light.range() * 1.1f );
        light.setShadowVPMatrix(  i, mat4<F32>::Multiply(viewProjMatrix[i], MAT4_BIAS_ZERO_ONE_Z ));
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void CubeShadowMapGenerator::updateMSAASampleCount([[maybe_unused]] const U8 sampleCount) noexcept {
    DIVIDE_UNEXPECTED_CALL();
}
};