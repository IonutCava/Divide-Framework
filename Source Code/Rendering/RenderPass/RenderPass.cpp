#include "stdafx.h"

#include "config.h"

#include "Headers/RenderPass.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Graphs/Headers/SceneGraph.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"

#include "Platform/Video/Headers/GFXDevice.h"

#include "Geometry/Material/Headers/Material.h"
#include "Scenes/Headers/Scene.h"

#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Headers/NodeBufferedData.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

namespace Divide {

namespace {
    // We need a proper, time-based system, to check reflection budget
    namespace ReflectionUtil {
        U16 g_reflectionBudget = 0;

        [[nodiscard]] bool isInBudget() noexcept { return g_reflectionBudget < Config::MAX_REFLECTIVE_NODES_IN_VIEW; }
                      void resetBudget() noexcept { g_reflectionBudget = 0; }
                      void updateBudget() noexcept { ++g_reflectionBudget; }
        [[nodiscard]] U16  currentEntry() noexcept { return g_reflectionBudget; }
    }

    namespace RefractionUtil {
        U16 g_refractionBudget = 0;

        [[nodiscard]] bool isInBudget() noexcept { return g_refractionBudget < Config::MAX_REFRACTIVE_NODES_IN_VIEW;  }
                      void resetBudget() noexcept { g_refractionBudget = 0; }
                      void updateBudget() noexcept { ++g_refractionBudget;  }
        [[nodiscard]] U16  currentEntry() noexcept { return g_refractionBudget; }
    }
}

RenderPass::RenderPass(RenderPassManager& parent, GFXDevice& context, Str64 name, const U8 sortKey, const RenderStage passStageFlag, const vector<U8>& dependencies, const bool performanceCounters)
    : _performanceCounters(performanceCounters),
      _context(context),
      _parent(parent),
      _config(context.context().config()),
      _stageFlag(passStageFlag),
      _sortKey(sortKey),
      _dependencies(dependencies),
      _name(MOV(name))
{
    for (U8 i = 0u; i < to_base(_stageFlag); ++i) {
        const U8 passCountToSkip = TotalPassCountForStage(static_cast<RenderStage>(i));
        _transformIndexOffset += passCountToSkip * Config::MAX_VISIBLE_NODES;
    }
}

void RenderPass::initBufferData() {
    if (_performanceCounters) {
        // Atomic counter for occlusion culling
        ShaderBufferDescriptor bufferDescriptor = {};
        bufferDescriptor._usage = ShaderBuffer::Usage::ATOMIC_COUNTER;
        bufferDescriptor._bufferParams._elementCount = 1;
        bufferDescriptor._bufferParams._elementSize = sizeof(U32);
        bufferDescriptor._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        bufferDescriptor._bufferParams._updateUsage = BufferUpdateUsage::CPU_W_GPU_R;
        bufferDescriptor._ringBufferLength = 5;
        bufferDescriptor._separateReadWrite = true;
        bufferDescriptor._name = Util::StringFormat("CULL_COUNTER_%s", TypeUtil::RenderStageToString(_stageFlag));
        _cullCounter = _context.newSB(bufferDescriptor);
    }
}

RenderPass::BufferData RenderPass::getBufferData(const RenderStagePass stagePass) const noexcept {
    assert(_stageFlag == stagePass._stage);

    BufferData ret{};
    ret._cullCounterBuffer = _cullCounter;
    ret._lastCommandCount = &_lastCmdCount;
    ret._lastNodeCount = &_lastNodeCount;
    return ret;
}

void RenderPass::render([[maybe_unused]] const Task& parentTask, const SceneRenderState& renderState, GFX::CommandBuffer& bufferInOut) const {
    OPTICK_EVENT();

    switch(_stageFlag) {
        case RenderStage::DISPLAY: {
            OPTICK_EVENT("RenderPass - Main");

            static GFX::ClearRenderTargetCommand clearMainTarget = {};
            static RenderPassParams params = {};

            static bool initDrawCommands = false;
            if (!initDrawCommands) {
                RTClearDescriptor clearDescriptor = {};
                clearDescriptor.clearColours(true);
                clearDescriptor.clearDepth(true);
                //ToDo: Causing issues if disabled with WOIT (e.g. grass) if disabled. Investigate! -Ionut
                clearDescriptor.clearColour(to_U8(GFXDevice::ScreenTargets::ALBEDO), true);

                //Not everything gets drawn during the depth PrePass (E.g. sky)
                clearDescriptor.clearColour(to_U8(GFXDevice::ScreenTargets::VELOCITY), true); 
                clearDescriptor.clearColour(to_U8(GFXDevice::ScreenTargets::NORMALS), true);
                clearMainTarget._descriptor = clearDescriptor;

                RTDrawDescriptor prePassPolicy = {};
                prePassPolicy.drawMask().disableAll();
                prePassPolicy.drawMask().setEnabled(RTAttachmentType::Depth, 0, true);
                prePassPolicy.drawMask().setEnabled(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::VELOCITY), true);
                prePassPolicy.drawMask().setEnabled(RTAttachmentType::Colour, to_base(GFXDevice::ScreenTargets::NORMALS), true);
                //prePassPolicy.alphaToCoverage(true);

                RTDrawDescriptor mainPassPolicy = {};
                mainPassPolicy.drawMask().setEnabled(RTAttachmentType::Depth, 0, false);
                mainPassPolicy.drawMask().setEnabled(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::VELOCITY), false);
                mainPassPolicy.drawMask().setEnabled(RTAttachmentType::Colour, to_U8(GFXDevice::ScreenTargets::NORMALS), false);

                const RTDrawDescriptor oitCompositionPassPolicy = mainPassPolicy;

                params._passName = "MainRenderPass";
                params._stagePass = RenderStagePass{ _stageFlag, RenderPassType::COUNT };
                params._targetDescriptorPrePass = prePassPolicy;
                params._targetDescriptorMainPass = mainPassPolicy;
                params._targetDescriptorComposition = oitCompositionPassPolicy;
                params._targetHIZ = RenderTargetID(RenderTargetUsage::HI_Z);

                initDrawCommands = true;
            }

            params._targetOIT = params._target._usage == RenderTargetUsage::SCREEN_MS ? RenderTargetID(RenderTargetUsage::OIT_MS) : RenderTargetID(RenderTargetUsage::OIT);
            params._target = _context.renderTargetPool().screenTargetID();
            clearMainTarget._target = params._target;

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Main Display Pass" });
            GFX::EnqueueCommand(bufferInOut, clearMainTarget);

            _parent.doCustomPass(Attorney::SceneManagerCameraAccessor::playerCamera(_parent.parent().sceneManager()), params, bufferInOut);

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
        } break;

        case RenderStage::SHADOW: {
            OPTICK_EVENT("RenderPass - Shadow");
            if (_config.rendering.shadowMapping.enabled) {
                constexpr bool useNegOneToOneDepth = false;
                SceneManager* mgr = _parent.parent().sceneManager();
                const Camera* camera = Attorney::SceneManagerCameraAccessor::playerCamera(mgr);

                LightPool& lightPool = Attorney::SceneManagerRenderPass::lightPool(mgr);

                GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Shadow Render Stage" });

                if_constexpr(useNegOneToOneDepth) {
                    //ToDo: remove this and change lookup code
                    GFX::SetClippingStateCommand clipStateCmd;
                    clipStateCmd._negativeOneToOneDepth = true;
                    GFX::EnqueueCommand(bufferInOut, clipStateCmd);
                }
               lightPool.generateShadowMaps(*camera, bufferInOut);

               if_constexpr(useNegOneToOneDepth) {
                   GFX::SetClippingStateCommand clipStateCmd;
                   clipStateCmd._negativeOneToOneDepth = false;
                   GFX::EnqueueCommand(bufferInOut, clipStateCmd);
               }
               GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
            }
        } break;

        case RenderStage::REFLECTION: {
            SceneManager* mgr = _parent.parent().sceneManager();
            Camera* camera = Attorney::SceneManagerCameraAccessor::playerCamera(mgr);

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Reflection Pass" });
            {
                OPTICK_EVENT("RenderPass - Probes");
                SceneEnvironmentProbePool::Prepare(bufferInOut);

                SceneEnvironmentProbePool* envProbPool = Attorney::SceneRenderPass::getEnvProbes(mgr->getActiveScene());
                envProbPool->lockProbeList();
                const EnvironmentProbeList& probes = envProbPool->sortAndGetLocked(camera->getEye());
                U32 idx = 0u;
                for (const auto& probe : probes) {
                    if (probe->refresh(bufferInOut) && ++idx == Config::MAX_REFLECTIVE_PROBES_PER_PASS) {
                        break;
                    }
                }
                envProbPool->unlockProbeList();
            }
            {
                OPTICK_EVENT("RenderPass - Reflection");
                static VisibleNodeList s_Nodes;
                //Update classic reflectors (e.g. mirrors, water, etc)
                //Get list of reflective nodes from the scene manager
                mgr->getSortedReflectiveNodes(camera, RenderStage::REFLECTION, true, s_Nodes);

                // While in budget, update reflections
                ReflectionUtil::resetBudget();
                for (size_t i = 0; i < s_Nodes.size(); ++i) {
                    const VisibleNode& node = s_Nodes.node(i);
                    RenderingComponent* const rComp = node._node->get<RenderingComponent>();
                    if (Attorney::RenderingCompRenderPass::updateReflection(*rComp,
                                                                            ReflectionUtil::currentEntry(),
                                                                            ReflectionUtil::isInBudget(),
                                                                            camera,
                                                                            renderState,
                                                                            bufferInOut))
                    {
                        ReflectionUtil::updateBudget();
                    }
                }
            }
            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        } break;

        case RenderStage::REFRACTION: {
            static VisibleNodeList s_Nodes;

            GFX::EnqueueCommand(bufferInOut, GFX::BeginDebugScopeCommand{ "Refraction Pass" });

            OPTICK_EVENT("RenderPass - Refraction");
            // Get list of refractive nodes from the scene manager
            const SceneManager* mgr = _parent.parent().sceneManager();
            Camera* camera = Attorney::SceneManagerCameraAccessor::playerCamera(mgr);
            {
                mgr->getSortedRefractiveNodes(camera, RenderStage::REFRACTION, true, s_Nodes);
                // While in budget, update refractions
                RefractionUtil::resetBudget();
                for (size_t i = 0; i < s_Nodes.size(); ++i) {
                     const VisibleNode& node = s_Nodes.node(i);
                     RenderingComponent* const rComp = node._node->get<RenderingComponent>();
                     if (Attorney::RenderingCompRenderPass::updateRefraction(*rComp,
                                                                            RefractionUtil::currentEntry(),
                                                                            RefractionUtil::isInBudget(),
                                                                            camera,
                                                                            renderState,
                                                                            bufferInOut))
                    {
                        RefractionUtil::updateBudget();
                    }
                }
            }

            GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);

        } break;

        case RenderStage::COUNT:
            DIVIDE_UNEXPECTED_CALL();
            break;
    };
}

};