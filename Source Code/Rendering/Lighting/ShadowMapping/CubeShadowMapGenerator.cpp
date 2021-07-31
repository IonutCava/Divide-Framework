#include "stdafx.h"

#include "Headers/CubeShadowMapGenerator.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Headers/Configuration.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/SceneManager.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Platform/Video/Headers/GFXDevice.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "Managers/Headers/RenderPassManager.h"

namespace Divide {
namespace {
    const RenderTargetID g_depthMapID(RenderTargetUsage::SHADOW, to_base(ShadowType::CUBEMAP));
    Configuration::Rendering::ShadowMapping g_shadowSettings;
};

CubeShadowMapGenerator::CubeShadowMapGenerator(GFXDevice& context)
    : ShadowMapGenerator(context, ShadowType::CUBEMAP)
{
    Console::printfn(Locale::Get(_ID("LIGHT_CREATE_SHADOW_FB")), "Single Shadow Map");
    g_shadowSettings = context.context().config().rendering.shadowMapping;
}

void CubeShadowMapGenerator::render(const Camera& playerCamera, Light& light, U16 lightIndex, GFX::CommandBuffer& bufferInOut) {
    OPTICK_EVENT();

    ACKNOWLEDGE_UNUSED(playerCamera);

    const vec3<F32> lightPos = light.getSGN()->get<TransformComponent>()->getPosition();

    auto& shadowCameras = ShadowMap::shadowCameras(ShadowType::CUBEMAP);

    std::array<Camera*, 6> cameras = {};
    std::copy_n(std::begin(shadowCameras), std::min(cameras.size(),shadowCameras.size()), std::begin(cameras));

    EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand(Util::StringFormat("Cube Shadow Pass Light: [ %d ]", lightIndex).c_str()));

    RenderPassParams params = {};
    params._target = RenderTargetID(RenderTargetUsage::SHADOW, to_base(ShadowType::CUBEMAP));
    params._sourceNode = light.getSGN();
    params._stagePass = RenderStagePass(RenderStage::SHADOW, RenderPassType::MAIN_PASS, to_U8(light.getLightType()), lightIndex);

    _context.generateCubeMap(params,
                             light.getShadowOffset(),
                             light.getSGN()->get<TransformComponent>()->getPosition(),
                             vec2<F32>(0.001f, light.range() * 1.1f),
                             bufferInOut,
                             cameras);

    for (U8 i = 0; i < 6; ++i) {
        light.setShadowLightPos(  i, lightPos);
        light.setShadowFloatValue(i, shadowCameras[i]->getZPlanes().y);
        light.setShadowVPMatrix(  i, mat4<F32>::Multiply(shadowCameras[i]->viewProjectionMatrix(), MAT4_BIAS));
    }

    EnqueueCommand(bufferInOut, GFX::EndDebugScopeCommand{});
}

void CubeShadowMapGenerator::updateMSAASampleCount(const U8 sampleCount) {
    DIVIDE_UNEXPECTED_CALL();
}
};