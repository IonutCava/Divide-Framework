#include "stdafx.h"

#include "Headers/CubeShadowMapGenerator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/SceneManager.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Rendering/Lighting/Headers/Light.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
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
    Console::printfn(Locale::Get(_ID("LIGHT_CREATE_SHADOW_FB")), "Single Shadow Map");
    g_shadowSettings = context.context().config().rendering.shadowMapping;
}

void CubeShadowMapGenerator::render([[maybe_unused]] const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut, GFX::MemoryBarrierCommand& memCmdInOut) {
    PROFILE_SCOPE();

    const vec3<F32> lightPos = light.getSGN()->get<TransformComponent>()->getWorldPosition();

    auto& shadowCameras = ShadowMap::shadowCameras(ShadowType::CUBEMAP);

    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(shadowCameras), std::min(cameras.size(),shadowCameras.size()), std::begin(cameras));

    GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Cube Shadow Pass Light: [ %d ]", lightIndex).c_str(), lightIndex));
    GFX::EnqueueCommand<GFX::SetClippingStateCommand>(bufferInOut)->_negativeOneToOneDepth = false;

    RenderPassParams params = {};
    params._target = ShadowMap::getShadowMap(_type)._targetID;
    params._sourceNode = light.getSGN();
    params._stagePass = { RenderStage::SHADOW, RenderPassType::MAIN_PASS, lightIndex, static_cast<RenderStagePass::VariantType>(light.getLightType()) };

    _context.generateCubeMap(params,
                             light.getShadowArrayOffset(),
                             light.getSGN()->get<TransformComponent>()->getWorldPosition(),
                             vec2<F32>(0.001f, light.range() * 1.1f),
                             bufferInOut,
                             memCmdInOut,
                             cameras);

    for (U8 i = 0u; i < 6u; ++i) {
        light.setShadowLightPos(  i, lightPos);
        light.setShadowFloatValue(i, shadowCameras[i]->snapshot()._zPlanes.max);
        light.setShadowVPMatrix(  i, mat4<F32>::Multiply(shadowCameras[i]->viewProjectionMatrix(), MAT4_BIAS));
    }

    GFX::EnqueueCommand<GFX::SetClippingStateCommand>(bufferInOut)->_negativeOneToOneDepth = true;
    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void CubeShadowMapGenerator::updateMSAASampleCount(const U8 sampleCount) noexcept {
    DIVIDE_UNEXPECTED_CALL();
}
};