#include "stdafx.h"

#include "Headers/SolutionExplorerWindow.h"
#include "Headers/Utils.h"

#include "Editor/Headers/Editor.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Managers/Headers/RenderPassManager.h"
#include "Managers/Headers/SceneManager.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "Platform/Video/Headers/GFXDevice.h"

#include "Graphs/Headers/SceneGraph.h"

#include "Dynamics/Entities/Particles/Headers/ParticleSource.h"
#include "Dynamics/Entities/Particles/Headers/ParticleEmitter.h"
#include "Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleBoxGenerator.h"
#include "Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleColourGenerator.h"
#include "Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleTimeGenerator.h"
#include "Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleVelocityGenerator.h"
#include "Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleBasicColourUpdater.h"
#include "Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleBasicTimeUpdater.h"
#include "Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleEulerUpdater.h"
#include "Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleFloorUpdater.h"

#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/SpotLightComponent.h"
#include "ECS/Components/Headers/PointLightComponent.h"
#include "ECS/Components/Headers/DirectionalLightComponent.h"
#include "ECS/Components/Headers/EnvironmentProbeComponent.h"
#include "ECS/Components/Headers/ScriptComponent.h"
#include "ECS/Components/Headers/UnitComponent.h"

#include <EASTL/deque.h>
#include <IconFontCppHeaders/IconsForkAwesome.h>
#include <imgui_internal.h>

namespace Divide {
    namespace {
        bool s_onlyVisibleNodes = false;
        constexpr U8 g_maxEntryCount = 32;
        eastl::deque<F32> g_framerateBuffer;
        vector<F32> g_framerateBufferCont;
        SceneNodeType g_currentNodeType = SceneNodeType::TYPE_TRANSFORM;
        SceneGraphNodeDescriptor g_nodeDescriptor;
        std::shared_ptr<ParticleData> g_particleEmitterData = nullptr;
        std::shared_ptr<ParticleSource> g_particleSource = nullptr;
        F32 g_particleBounceFactor = 0.65f;
        vec3<F32> g_particleAcceleration = {0.f, -20.f, 0.f};
        FColour4 g_particleStartColour = DefaultColours::BLACK;
        FColour4 g_particleEndColour = DefaultColours::WHITE;
    }

    SolutionExplorerWindow::SolutionExplorerWindow(Editor& parent, PlatformContext& context, const Descriptor& descriptor)
        : DockedWindow(parent, descriptor),
          PlatformContextComponent(context)
    {
        g_framerateBufferCont.reserve(g_maxEntryCount);
    }

    void SolutionExplorerWindow::printCameraNode(SceneManager* sceneManager, Camera* const camera) const {
        if (camera == nullptr) {
            return;
        }

        constexpr ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (_filter.PassFilter(camera->resourceName().c_str())) {
            if (ImGui::TreeNodeEx((void*)(intptr_t)camera->getGUID(), node_flags, "%s %s", ICON_FK_CAMERA, camera->resourceName().c_str())) {
                if (ImGui::IsItemClicked()) {
                    if (sceneManager->resetSelection(0, false)) {
                        if (Attorney::EditorSolutionExplorerWindow::getSelectedCamera(_parent) == camera) {
                            Attorney::EditorSolutionExplorerWindow::setSelectedCamera(_parent, nullptr);
                        } else {
                            Attorney::EditorSolutionExplorerWindow::setSelectedCamera(_parent, camera);
                        }
                    }
                }

                ImGui::TreePop();
            }
        }
    }

    void SolutionExplorerWindow::drawContextMenu(SceneGraphNode* sgn) {
        if (ImGui::BeginPopupContextItem("Context menu")) {
            const SceneNode& node = sgn->getNode();
            const bool isSubMesh = node.type() == SceneNodeType::TYPE_SUBMESH;
            const bool isRoot = sgn->parent() == nullptr;

            ImGui::Text(Util::StringFormat("%s [%s]", getIconForNode(sgn), sgn->name().c_str()).c_str());
            ImGui::Separator();
            if (isSubMesh) {
                PushReadOnly();
            }
            if (ImGui::Selectable(ICON_FK_USERS" Change Parent")) {
                _childNode = sgn;
                _reparentSelectRequested = true;
            }
            if (isSubMesh) {
                PopReadOnly();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Can't re-parent sub-meshes!");
                }
            }
            if (ImGui::Selectable(ICON_FK_CHILD"  Add Child")) {
                g_particleEmitterData.reset();
                g_particleSource.reset();

                g_nodeDescriptor = {};
                g_nodeDescriptor._name = Util::StringFormat("New_Child_Node_%d", sgn->getGUID());
                g_nodeDescriptor._componentMask = to_U32(ComponentType::TRANSFORM);
                g_currentNodeType = SceneNodeType::TYPE_TRANSFORM;
                _parentNode = sgn;
            }
            ImGui::Separator();

            if (ImGui::Selectable(ICON_FK_LOCATION_ARROW"  Go To")) {
                goToNode(sgn);
            }
            ImGui::Separator();

            if (ImGui::Selectable(ICON_FK_FLOPPY_O"  Save Changes")) {
                saveNode(sgn);
            }
            if (ImGui::Selectable(ICON_FK_FILE"  Load from file")) {
                loadNode(sgn);
            }
            ImGui::NewLine();
            ImGui::Separator();
            if (isRoot) {
                PushReadOnly();
            }
            if (ImGui::Selectable(ICON_FK_TRASH"  Remove")) {
                _nodeToRemove = sgn->getGUID();
            }
            if (isRoot) {
                PopReadOnly();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Can't remove the root node!");
                }
            }
            ImGui::EndPopup();
        }
    }

    bool SolutionExplorerWindow::nodeHasChildrenInView(const SceneGraphNode* sgn) const {
        const SceneGraphNode::ChildContainer& children = sgn->getChildren();
        SharedLock<SharedMutex> r_lock(children._lock);
        const U32 childCount = children._count;
        for (U32 i = 0u; i < childCount; ++i) {
            if (Attorney::EditorSolutionExplorerWindow::isNodeInView(_parent, *children._data[i]) ||
                nodeHasChildrenInView(children._data[i]))
            {
                return true;
            }
        }

        return  false;
    }

    void SolutionExplorerWindow::printSceneGraphNode(SceneManager* sceneManager,
                                                     SceneGraphNode* sgn,
                                                     const I32 nodeIDX,
                                                     const bool open,
                                                     const bool secondaryView,
                                                     const bool modifierPressed)
    {
        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                                        //Conflicts with "Teleport to node on double click"
                                        // | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        const bool wasSelected = secondaryView ? _tempParent != nullptr && _tempParent->getGUID() == sgn->getGUID() : sgn->hasFlag(SceneGraphNode::Flags::SELECTED);

        if (sgn->hasFlag(SceneGraphNode::Flags::SELECTED))
        {
            Attorney::EditorGeneralWidget::setPreviewNode(_parent, sgn);
        }

        if (open) {
            node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (wasSelected) {
            node_flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (sgn->getChildren()._count.load() == 0u) {
            node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
        }

        const auto printNode = [&](const char* icon) {
            if (s_onlyVisibleNodes && !Attorney::EditorSolutionExplorerWindow::isNodeInView(_parent, *sgn)) {
                if (!sgn->hasFlag(SceneGraphNode::Flags::IS_CONTAINER) || !nodeHasChildrenInView(sgn)) {
                    return false;
                }
            }
            const bool isHovered = sgn->hasFlag(SceneGraphNode::Flags::HOVERED);

            const bool isRoot = sgn->parent() == nullptr;
            const bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)sgn->getGUID(),
                                                    node_flags,
                                                    "%s [%d] %s %s %s",
                                                    icon == nullptr ? ICON_FK_QUESTION : icon,
                                                    nodeIDX,
                                                    sgn->name().c_str(), 
                                                    (modifierPressed && !isRoot) ? ICON_FK_CHECK_SQUARE_O : "",
                                                    (wasSelected ? ICON_FK_CHEVRON_CIRCLE_LEFT : isHovered ? ICON_FK_CHEVRON_LEFT : ""));
            
            if (!secondaryView && wasSelected) {
                drawContextMenu(sgn);
            }

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                if (secondaryView) {
                    _tempParent = sgn;
                } else {
                    const bool parentSelected = !isRoot && sgn->parent()->hasFlag(SceneGraphNode::Flags::SELECTED);
                    const bool childrenSelected = sgn->getChildren()._count.load() > 0u && sgn->getChildren().getChild(0u)->hasFlag(SceneGraphNode::Flags::SELECTED);

                    if (modifierPressed || sceneManager->resetSelection(0, false)) {
                        if (!wasSelected || parentSelected || childrenSelected) {
                            sceneManager->setSelected(0, { sgn }, wasSelected);
                        }
                        Attorney::EditorSolutionExplorerWindow::setSelectedCamera(_parent, nullptr);
                    }
                }
            }
            if (!secondaryView && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_None)) {
                goToNode(sgn);
            }
            return nodeOpen;
        };

        if (_filter.Filters.empty()) {
            if (printNode(getIconForNode(sgn))) {
                const SceneGraphNode::ChildContainer& children = sgn->getChildren();
                SharedLock<SharedMutex> r_lock(children._lock);
                const U32 childCount = children._count;
                for (U32 i = 0u; i < childCount; ++i) {
                    printSceneGraphNode(sceneManager, children._data[i], i, false, secondaryView, modifierPressed);
                }
                ImGui::TreePop();
            }
        } else {
            bool nodeOpen = false;
            if (_filter.PassFilter(sgn->name().c_str())) {
                nodeOpen = printNode(getIconForNode(sgn));
            }
            const SceneGraphNode::ChildContainer& children = sgn->getChildren();
            SharedLock<SharedMutex> r_lock(children._lock);
            const U32 childCount = children._count;
            for (U32 i = 0u; i < childCount; ++i) {
                printSceneGraphNode(sceneManager, children._data[i], i, false, secondaryView, modifierPressed);
            }
            if (nodeOpen) {
                ImGui::TreePop();
            }
        }
    }

    void SolutionExplorerWindow::drawInternal()
    {
        PROFILE_SCOPE();

        SceneManager* sceneManager = context().kernel().sceneManager();
        Scene& activeScene = sceneManager->getActiveScene();
        SceneGraphNode* root = activeScene.sceneGraph()->getRoot();
        Attorney::EditorGeneralWidget::setPreviewNode(_parent, nullptr);

        const bool lockExplorer = Attorney::EditorSolutionExplorerWindow::lockSolutionExplorer(_parent);
        const ImGuiContext& imguiContext = Attorney::EditorGeneralWidget::getImGuiContext(_context.editor(), Editor::ImGuiContextType::Editor);
        const bool modifierPressed = imguiContext.IO.KeyShift;
        if (lockExplorer)
        {
            PushReadOnly();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::Text(ICON_FK_SEARCH" Find node: ");
        ImGui::SameLine();
        ImGui::PushID("GraphSearchFilter");
        _filter.Draw("", 160);
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Checkbox(ICON_FK_EYE, &s_onlyVisibleNodes);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Only visible nodes");
        }
        ImGui::BeginChild("SceneGraph", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetWindowHeight() * .5f), true, 0);
        if (ImGui::TreeNodeEx(activeScene.resourceName().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s%s", ICON_FK_HOME, activeScene.resourceName().c_str()))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize() * 3); // Increase spacing to differentiate leaves from expanded contents.
            printCameraNode(sceneManager, _parent.editorCamera());
            printCameraNode(sceneManager, _parent.nodePreviewCamera());
            for (PlayerIndex i = 0; i < static_cast<PlayerIndex>(Config::MAX_LOCAL_PLAYER_COUNT); ++i)
            {
                printCameraNode(sceneManager, Attorney::SceneManagerCameraAccessor::playerCamera(sceneManager, i, true));
            }
            {
                PROFILE_SCOPE("Print SceneGraph");
                printSceneGraphNode(sceneManager, root, 0, true, false, modifierPressed);
            }
            ImGui::PopStyleVar();
            ImGui::TreePop();
        }

        ImGui::EndChild();
        if (lockExplorer)
        {
            PopReadOnly();
        }
        ImGui::Separator();

        // Calculate and show framerate
        static F32 max_ms_per_frame = 0;

        static F32 ms_per_frame[g_maxEntryCount] = { 0 };
        static I32 ms_per_frame_idx = 0;
        static F32 ms_per_frame_accum = 0.0f;
        ms_per_frame_accum -= ms_per_frame[ms_per_frame_idx];
        ms_per_frame[ms_per_frame_idx] = ImGui::GetIO().DeltaTime * 1000.0f;
        ms_per_frame_accum += ms_per_frame[ms_per_frame_idx];
        ms_per_frame_idx = (ms_per_frame_idx + 1) % g_maxEntryCount;
        const F32 ms_per_frame_avg = ms_per_frame_accum / g_maxEntryCount;
        if (ms_per_frame_avg + Config::TARGET_FRAME_RATE / 1000.0f > max_ms_per_frame) {
            max_ms_per_frame = ms_per_frame_avg + Config::TARGET_FRAME_RATE / 1000.0f;
        }

        // We need this bit to get a nice "flowing" feel
        g_framerateBuffer.push_back(ms_per_frame_avg);
        if (g_framerateBuffer.size() > g_maxEntryCount)
        {
            g_framerateBuffer.pop_front();
        }
        efficient_clear( g_framerateBufferCont );
        g_framerateBufferCont.insert(cbegin(g_framerateBufferCont),
                                     cbegin(g_framerateBuffer),
                                     cend(g_framerateBuffer));
        ImGui::PushItemWidth(-1);
        {
            ImGui::PlotHistogram("",
                                 g_framerateBufferCont.data(),
                                 to_I32(g_framerateBufferCont.size()),
                                 0,
                                 Util::StringFormat("%.3f ms/frame (%.1f FPS)", ms_per_frame_avg, ms_per_frame_avg > 0.01f ? 1000.0f / ms_per_frame_avg : 0.0f).c_str(),
                                 0.0f,
                                 max_ms_per_frame,
                                 ImVec2(0, 50));
        }
        ImGui::PopItemWidth();

        static bool performanceStatsWereEnabled = false;
        static U32 s_maxLocksInFlight = 0u;
        if (ImGui::CollapsingHeader(ICON_FK_TACHOMETER" Performance Stats"))
        {
            I32 fpsLimit = to_I32(context().config().runtime.frameRateLimit);
            bool limit = fpsLimit > 0;
            if (ImGui::Checkbox("Limit FPS", &limit))
            {
                context().config().runtime.frameRateLimit = limit ? 120 : 0;
                context().config().changed(true);
            }
            if (!limit)
            {
                PushReadOnly();
            }
            if (ImGui::SliderInt("FPS limit", &fpsLimit, 10, 320))
            {
                context().config().runtime.frameRateLimit = to_I16(fpsLimit);
                context().config().changed(true);
            }
            if (!limit)
            {
                PopReadOnly();
            }
            PROFILE_SCOPE("Get/Print Performance Stats");
            performanceStatsWereEnabled = context().gfx().queryPerformanceStats();
            context().gfx().queryPerformanceStats(true);
            const auto& rpm = _context.kernel().renderPassManager();

            static std::array<I32, to_base(RenderStage::COUNT)> maxDrawCallCount{};
            static std::array<I32, to_base(RenderStage::COUNT)> crtDrawCallCount{};
            for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i)
            {
                crtDrawCallCount[i] = rpm->drawCallCount(static_cast<RenderStage>(i));
            }

            const PerformanceMetrics perfMetrics = context().gfx().getPerformanceMetrics();
            const vec4<U32>& cullCount = context().gfx().lastCullCount();
            static U32 cachedSyncCount[3]{};
            static U32 cachedCamWrites[2]{};
            if (ms_per_frame_idx % 2 == 0)
            {
                std::memcpy(cachedSyncCount, perfMetrics._syncObjectsInFlight, 3 * sizeof(U32));
                std::memcpy(cachedCamWrites, perfMetrics._scratchBufferQueueUsage, 2 * sizeof(U32));
                s_maxLocksInFlight = std::max(cachedSyncCount[0], s_maxLocksInFlight);
            }

            ImGui::NewLine();
            ImGui::Columns(to_base(RenderStage::COUNT) + 1u, "draw_call_columns");
            ImGui::Separator();

            ImGui::Text("Data");        ImGui::NextColumn();
            ImGui::Text("Display");     ImGui::NextColumn();
            ImGui::Text("Shadows");     ImGui::NextColumn();
            ImGui::Text("Reflections"); ImGui::NextColumn();
            ImGui::Text("Refractions"); ImGui::NextColumn();
            ImGui::Separator();

            static bool maxCalls = false;
            if (maxCalls)
            {
                ImGui::Text(ICON_FK_PENCIL" (Max)");
            }
            else
            {
                ImGui::Text(ICON_FK_PENCIL);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draw Calls. Click to toggle between Max Calls and Current Calls"); 
            }
            if (ImGui::IsItemClicked())
            {
                maxCalls = !maxCalls;
            }
            ImGui::NextColumn();

            for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i)
            {
                ImGui::Text("%d", maxCalls ? maxDrawCallCount[i] : crtDrawCallCount[i]);
                if (ImGui::IsItemHovered())
                { 
                    if (maxCalls)
                    {
                        ImGui::SetTooltip("Current calls: %d", crtDrawCallCount[i]);
                    }
                    else
                    {
                        ImGui::SetTooltip("Max calls: %d", maxDrawCallCount[i]); 
                    }
                }
                ImGui::NextColumn();
            }
  
            ImGui::Text(ICON_FK_EYE);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Visible Nodes");
            }
            ImGui::NextColumn();

            for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i)
            {
                ImGui::Text("%d", rpm->getLastTotalBinSize(static_cast<RenderStage>(i)));
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::NewLine();
            bool enableHiZ = context().gfx().enableOcclusionCulling();
            ImGui::PushID("ToggleHiZCheckBox");
            if (ImGui::Checkbox("", &enableHiZ))
            {
                context().gfx().enableOcclusionCulling(enableHiZ);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Enable / Disable GPU Hi-Z occlusion culling");
            }
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("HiZ Cull Counts: %d | %d | %d | %d", cullCount.x, cullCount.y, cullCount.z, cullCount.w);
            
            ImGui::NewLine();
            ImGui::Text("GPU Frame Time: %.2f ms", perfMetrics._gpuTimeInMS);
            ImGui::NewLine();
            ImGui::Text("Submitted Vertices: %s", Util::commaprint(perfMetrics._verticesSubmitted));
            ImGui::NewLine();
            ImGui::Text("Primitves Generated: %s", Util::commaprint(perfMetrics._primitivesGenerated));
            ImGui::NewLine();
            ImGui::Text("Tessellation Patches: %s", Util::commaprint(perfMetrics._tessellationPatches));
            ImGui::NewLine();
            ImGui::Text("Tessellation Invocations: %s", Util::commaprint(perfMetrics._tessellationInvocations));
            ImGui::NewLine();
            ImGui::Text("Generated Render Targets: %d", perfMetrics._generatedRenderTargetCount);
            ImGui::NewLine();
            ImGui::Text("Queued GPU Frames: %d", perfMetrics._queuedGPUFrames);
            ImGui::NewLine();  
            ImGui::Text("Shader uniforms VRAM usage: %.2f Kb", (perfMetrics._uniformBufferVRAMUsage / 1024.f));
            ImGui::NewLine();
            ImGui::Text("Shader buffers VRAM usage: %.2f Mb", (perfMetrics._bufferVRAMUsage / 1024.f / 1024.f));
            ImGui::NewLine();
            ImGui::Text("Sync objects in flight : %d / %d / %d   Max: %d", cachedSyncCount[0], cachedSyncCount[1], cachedSyncCount[2], s_maxLocksInFlight);

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("[ Current Frame - 2 ] / [ Current Frame - 1] / [ Current Frame ]");
            }

            ImGui::Text("Cam Buffer Writes: %d | Render Buffer Writes: %d", cachedCamWrites[0], cachedCamWrites[1]);

            ImGui::NewLine();
            ImGui::Separator();
            ImGui::NewLine();
            for (U8 i = 0u; i < to_base(RenderStage::COUNT); ++i)
            {
                maxDrawCallCount[i] = std::max(rpm->drawCallCount(static_cast<RenderStage>(i)), maxDrawCallCount[i]);
            }
        }
        else
        {
            if (!performanceStatsWereEnabled && context().gfx().queryPerformanceStats())
            {
                context().gfx().queryPerformanceStats(false);
            }
            s_maxLocksInFlight = 0u;
        }
        
        static string dayNightText = Util::StringFormat("%s/%s Day/Night Settings", ICON_FK_SUN_O, ICON_FK_MOON_O).c_str();

        if (ImGui::CollapsingHeader(dayNightText.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            bool dayNightEnabled = activeScene.dayNightCycleEnabled();
            if (ImGui::Checkbox("Enable day/night cycle", &dayNightEnabled))
            {
                activeScene.dayNightCycleEnabled(dayNightEnabled);
            }

            ImGui::Text(ICON_FK_CLOCK_O" Time of Day:");
            SimpleTime time = activeScene.getTimeOfDay();
            SimpleLocation location = activeScene.getGeographicLocation();

            constexpr U8 min = 0u;
            constexpr U8 maxHour = 24u;
            constexpr U8 maxMinute = 59u;

            const bool hourChanged = ImGui::SliderScalar("Hour", ImGuiDataType_U8, &time._hour, &min, &maxHour, "%02d");
            const bool minutesChanged = ImGui::SliderScalar("Minute", ImGuiDataType_U8, &time._minutes, &min, &maxMinute, "%02d");
            if (hourChanged || minutesChanged)
            {
                activeScene.setTimeOfDay(time);
            }
            F32 timeFactor = activeScene.getDayNightCycleTimeFactor();
            if (ImGui::InputFloat("Time factor", &timeFactor))
            {
                activeScene.setDayNightCycleTimeFactor(CLAMPED(timeFactor, -500.f, 500.f));
            }

            ImGui::Text(ICON_FK_GLOBE_W" Global positioning:");
            const bool latitudeChanged = ImGui::SliderFloat("Latitude", &location._latitude, -90.f, 90.f, "%.6f");
            const bool longitudeChanged = ImGui::SliderFloat("Longitude", &location._longitude, -180.f, 180.f, "%.6f");
            if (latitudeChanged || longitudeChanged)
            {
                activeScene.setGeographicLocation(location);
            }

            if (!dayNightEnabled)
            {
                PushReadOnly();
            }

            const SunInfo sun = activeScene.getCurrentSunDetails();
            const vec3<F32> sunPosition = activeScene.getSunPosition();
            const vec3<F32> sunDirection = activeScene.getSunDirection();

            ImGui::Text(ICON_FK_INFO_CIRCLE);
            ImGui::SameLine();
            ImGui::Text("Sunset: %02d:%02d", sun.sunsetTime._hour, sun.sunsetTime._minutes);
            ImGui::SameLine(); ImGui::Text(" | "); ImGui::SameLine();
            ImGui::Text("Sunrise: %02d:%02d", sun.sunriseTime._hour, sun.sunriseTime._minutes);
            ImGui::SameLine();  ImGui::Text(" | "); ImGui::SameLine();
            ImGui::Text("Noon: %02d:%02d", sun.noonTime._hour, sun.noonTime._minutes);
            ImGui::Text("Sun Pos|Dir: (%1.2f, %1.2f, %1.2f) | (%1.2f, %1.2f, %1.2f)", sunPosition.x, sunPosition.y, sunPosition.z, sunDirection.x, sunDirection.y, sunDirection.z);
            ImGui::Text("Sun altitude(max) | azimuth : (%3.2f | %3.2f) degrees", Angle::RadiansToDegrees(sun.altitude), sun.altitudeMax, Angle::RadiansToDegrees(sun.azimuth));

            if (!dayNightEnabled)
            {
                PopReadOnly();
            }
        }
        
        drawRemoveNodeDialog();
        drawAddNodeDialog();
        drawChangeParentWindow();
        drawReparentNodeDialog();
    }

    void SolutionExplorerWindow::drawRemoveNodeDialog()
    {
        if (_nodeToRemove == -1)
        {
            return;
        }

        Util::OpenCenteredPopup("Confirm Remove");

        if (ImGui::BeginPopupModal("Confirm Remove", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Are you sure you want remove the selected node [ %zu ]?", _nodeToRemove);
            ImGui::Separator();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                _nodeToRemove = -1;
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Yes", ImVec2(120, 0)))
            {

                ImGui::CloseCurrentPopup();
                Attorney::EditorSolutionExplorerWindow::queueRemoveNode(_parent, _nodeToRemove);
                _nodeToRemove = -1;
            }
            ImGui::EndPopup();
        }
    }

    void SolutionExplorerWindow::drawReparentNodeDialog()
    {
        if (!_reparentConfirmRequested)
        {
            return;
        }

        Util::OpenCenteredPopup("Confirm Re-parent");

        if (ImGui::BeginPopupModal("Confirm Re-parent", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Are you sure you want change the selected node's [ %s ] parent?", _childNode->name().c_str());
            ImGui::Text("Old Parent [ %s ] | New Parent [ %s ]", _childNode->parent()->name().c_str(), _tempParent->name().c_str());
            ImGui::Separator();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                _reparentConfirmRequested = false;
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Yes", ImVec2(120, 0)))
            {
                _childNode->setParent(_tempParent, true);
                _childNode = nullptr;
                _tempParent = nullptr;
                Attorney::EditorGeneralWidget::registerUnsavedSceneChanges(_context.editor());
                ImGui::CloseCurrentPopup();
                _reparentConfirmRequested = false;
            }
            ImGui::EndPopup();
        }
    }

    void SolutionExplorerWindow::drawAddNodeDialog()
    {
        if (_parentNode == nullptr)
        {
            return;
        }

        Util::OpenCenteredPopup("Create New Node");

        if (ImGui::BeginPopupModal("Create New Node", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Creating a child node for SGN [ %d ][ %s ]?", _parentNode->getGUID(), _parentNode->name().c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::BeginChild("Node Properties", ImVec2(0, 400), true, 0);

            static char buf[64];
            if (ImGui::InputText("Name", &buf[0], 61)) {
                g_nodeDescriptor._name = buf;
            }

            const char* currentType = Names::sceneNodeType[to_base(g_currentNodeType)];
            if (ImGui::BeginCombo("Node Type", currentType, ImGuiComboFlags_PopupAlignLeft))
            {
                for (U8 t = 0; t < to_U8(SceneNodeType::COUNT); ++t)
                {
                    const SceneNodeType type = static_cast<SceneNodeType>(t);
                    const bool isSelected = g_currentNodeType == type;
                    const bool valid = type != SceneNodeType::TYPE_SKY &&
                                       type != SceneNodeType::TYPE_VEGETATION &&
                                       !Is3DObject(type);

                    if (ImGui::Selectable(Names::sceneNodeType[t], isSelected, valid ? 0 : ImGuiSelectableFlags_Disabled))
                    {
                        g_currentNodeType = type;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginChild("Components", ImVec2(0, 260), true, 0);

            U32& componentMask = g_nodeDescriptor._componentMask;
            if (g_currentNodeType == SceneNodeType::TYPE_WATER || g_currentNodeType == SceneNodeType::TYPE_PARTICLE_EMITTER)
            {
                componentMask |= to_U32(ComponentType::NAVIGATION) | 
                                 to_U32(ComponentType::RIGID_BODY) |
                                 to_U32(ComponentType::RENDERING);
            }
            else if (g_currentNodeType == SceneNodeType::TYPE_INFINITEPLANE)
            {
                componentMask |= to_U32(ComponentType::RENDERING);
            }

            if (g_currentNodeType == SceneNodeType::TYPE_PARTICLE_EMITTER)
            {
                componentMask |= to_U32(ComponentType::SELECTION);
            }

            for (auto i = 1u; i < to_base(ComponentType::COUNT) + 1; ++i)
            {
                const U32 componentBit = 1 << i;
                bool required = componentBit == to_U32(ComponentType::TRANSFORM) || 
                                componentBit == to_U32(ComponentType::BOUNDS);

                if (g_currentNodeType == SceneNodeType::TYPE_WATER ||
                    g_currentNodeType == SceneNodeType::TYPE_PARTICLE_EMITTER)
                {
                    required = required ||
                               componentBit == to_U32(ComponentType::NAVIGATION) ||
                               componentBit == to_U32(ComponentType::RIGID_BODY) ||
                               componentBit == to_U32(ComponentType::RENDERING);
                    if (g_currentNodeType == SceneNodeType::TYPE_PARTICLE_EMITTER)
                    {
                        required = required || componentBit == to_U32(ComponentType::SELECTION);
                    }
                }
                else if (g_currentNodeType == SceneNodeType::TYPE_INFINITEPLANE)
                {
                    required = required || componentBit == to_U32(ComponentType::RENDERING);
                }

                const bool invalid = componentBit == to_U32(ComponentType::INVERSE_KINEMATICS) ||
                                     componentBit == to_U32(ComponentType::ANIMATION) ||
                                     componentBit == to_U32(ComponentType::RAGDOLL);
                if (required || invalid)
                {
                    PushReadOnly();
                }

                bool componentEnabled = TestBit(componentMask, componentBit);
                const char* compLabel = TypeUtil::ComponentTypeToString(static_cast<ComponentType>(componentBit));
                if (ImGui::Checkbox(compLabel, &componentEnabled))
                {
                    SetBit(componentMask, componentBit);
                }
                if (ImGui::IsItemHovered())
                {
                    if (required)
                    {
                        ImGui::SetTooltip("Required component for current node type!");
                    }
                    else if (invalid)
                    {
                        ImGui::SetTooltip("Component type not (yet) supported!");
                    }
                }
                if (required || invalid)
                {
                    PopReadOnly();
                }
            }

            ImGui::EndChild();
            ImGui::Separator();
            bool nodeDynamic = g_nodeDescriptor._usageContext == NodeUsageContext::NODE_DYNAMIC;
            if (ImGui::Checkbox("Dynamic", &nodeDynamic))
            {
                g_nodeDescriptor._usageContext = nodeDynamic ? NodeUsageContext::NODE_DYNAMIC : NodeUsageContext::NODE_STATIC;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Static or dynamic node? Affects navigation, collision detection and other systems.");
            }

            ImGui::Checkbox("Serializable", &g_nodeDescriptor._serialize);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("State is saved and loaded to and from external files?");
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();

            drawNodeParametersChildWindow();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                _parentNode = nullptr;
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Yes", ImVec2(120, 0)))
            {
                g_nodeDescriptor._node = createNode();
                _parentNode->addChildNode(g_nodeDescriptor);
                Attorney::EditorGeneralWidget::registerUnsavedSceneChanges(_context.editor());
                g_nodeDescriptor._node.reset();
                ImGui::CloseCurrentPopup();
              
                _parentNode = nullptr;
            }
            ImGui::EndPopup();
        }
    }

    void SolutionExplorerWindow::drawNodeParametersChildWindow()
    {
        if (g_currentNodeType == SceneNodeType::TYPE_PARTICLE_EMITTER)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::BeginChild("Type Specific Properties", ImVec2(0, 200), true, 0);
            if (g_particleEmitterData == nullptr)
            {
                constexpr U32 options =
                    to_U32(ParticleDataProperties::PROPERTIES_POS) |
                    to_U32(ParticleDataProperties::PROPERTIES_VEL) |
                    to_U32(ParticleDataProperties::PROPERTIES_ACC) |
                    to_U32(ParticleDataProperties::PROPERTIES_COLOR) |
                    to_U32(ParticleDataProperties::PROPERTIES_COLOR_TRANS);
                g_particleEmitterData = std::make_shared<ParticleData>(context().gfx(), 1000, options);
                g_particleSource = std::make_shared<ParticleSource>(context().gfx(), 250.f);

                g_particleEmitterData->_textureFileName = "particle.DDS";

                std::shared_ptr<ParticleBoxGenerator> boxGenerator = std::make_shared<ParticleBoxGenerator>();
                boxGenerator->halfExtent({ 0.3f, 0.0f, 0.3f });
                g_particleSource->addGenerator(boxGenerator);

                std::shared_ptr<ParticleColourGenerator> colGenerator = std::make_shared<ParticleColourGenerator>();
                colGenerator->_minStartCol.set(g_particleStartColour);
                colGenerator->_maxStartCol.set(g_particleStartColour);
                colGenerator->_minEndCol.set(g_particleEndColour);
                colGenerator->_maxEndCol.set(g_particleEndColour);
                g_particleSource->addGenerator(colGenerator);

                std::shared_ptr<ParticleVelocityGenerator> velGenerator = std::make_shared<ParticleVelocityGenerator>();
                velGenerator->_minStartVel = { -1.0f, 0.22f, -1.0f };
                velGenerator->_maxStartVel = { 1.0f, 3.45f, 1.0f };
                g_particleSource->addGenerator(velGenerator);

                const std::shared_ptr<ParticleTimeGenerator> timeGenerator = std::make_shared<ParticleTimeGenerator>();
                timeGenerator->_minTime = 8.5f;
                timeGenerator->_maxTime = 20.5f;
                g_particleSource->addGenerator(timeGenerator);
            }

            PushReadOnly();
            ImGui::InputText("Texture File Name", g_particleEmitterData->_textureFileName.data(), 128);
            PopReadOnly();

            U32 componentMask = g_particleEmitterData->optionsMask();
            U32 particleCount = g_particleEmitterData->totalCount();
            if (ImGui::InputScalar("Particle Count", ImGuiDataType_U32, &particleCount))
            {
                if (particleCount == 0)
                {
                    particleCount = 1;
                }
                g_particleEmitterData->generateParticles(particleCount, componentMask);
            }
            
            F32 emitRate = g_particleSource->emitRate();
            if (ImGui::InputFloat("Emit rate", &emitRate))
            {
                if (emitRate <= EPSILON_F32 )
                {
                    emitRate = 1.0f;
                }
                g_particleSource->updateEmitRate(emitRate);
            }

            for (U8 i = 1; i < to_U8(ParticleDataProperties::COUNT) + 1; ++i)
            {
                const U32 componentBit = 1 << i;
                bool componentEnabled = TestBit(componentMask, componentBit);
                const char* compLabel = Names::particleDataProperties[i - 1];
                if (ImGui::Checkbox(compLabel, &componentEnabled))
                {
                    SetBit(componentMask, componentBit);
                    g_particleEmitterData->generateParticles(particleCount, componentMask);
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }

    void SolutionExplorerWindow::goToNode(const SceneGraphNode* sgn) const
    {
        Attorney::EditorSolutionExplorerWindow::teleportToNode(_parent, _parent.editorCamera(), sgn);
    }

    void SolutionExplorerWindow::saveNode(const SceneGraphNode* sgn) const
    {
        Attorney::EditorSolutionExplorerWindow::saveNode(_parent, sgn);
    }

    void SolutionExplorerWindow::loadNode(SceneGraphNode* sgn) const
    {
        Attorney::EditorSolutionExplorerWindow::loadNode(_parent, sgn);
    }

    SceneNode_ptr SolutionExplorerWindow::createNode()
    {
        const ResourceDescriptor descriptor(Util::StringFormat("%s_node", g_nodeDescriptor._name.c_str()));
        SceneNode_ptr ptr =  Attorney::EditorSolutionExplorerWindow::createNode(_parent, g_currentNodeType, descriptor);
        if (ptr)
        {
            if (g_currentNodeType == SceneNodeType::TYPE_PARTICLE_EMITTER)
            {
                ParticleEmitter* emitter = static_cast<ParticleEmitter*>(ptr.get());
                if (emitter->initData(g_particleEmitterData))
                {
                    emitter->addSource(g_particleSource);

                    std::shared_ptr<ParticleEulerUpdater> eulerUpdater = std::make_shared<ParticleEulerUpdater>(context());
                    eulerUpdater->_globalAcceleration.set(g_particleAcceleration);
                    emitter->addUpdater(eulerUpdater);
                    const std::shared_ptr<ParticleFloorUpdater> floorUpdater = std::make_shared<ParticleFloorUpdater>(context());
                    floorUpdater->_bounceFactor = g_particleBounceFactor;
                    emitter->addUpdater(floorUpdater);
                    emitter->addUpdater(std::make_shared<ParticleBasicTimeUpdater>(context()));
                    emitter->addUpdater(std::make_shared<ParticleBasicColourUpdater>(context()));
                }
                else
                {
                    ptr.reset();
                }
                g_particleEmitterData.reset();
                g_particleSource.reset();
            }
        }
        return ptr;
    }

    void SolutionExplorerWindow::drawChangeParentWindow()
    {
        if (_reparentSelectRequested)
        {
            Util::OpenCenteredPopup("Select New Parent");

            if (ImGui::BeginPopupModal("Select New Parent", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                SceneManager* sceneManager = context().kernel().sceneManager();
                const Scene& activeScene = sceneManager->getActiveScene();
                SceneGraphNode* root = activeScene.sceneGraph()->getRoot();

                ImGui::Text("Selecting a new parent for SGN [ %d ][ %s ]?", _childNode->getGUID(), _childNode->name().c_str());

                if (ImGui::BeginChild("SceneGraph", ImVec2(0, 400), true, 0))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize() * 3); // Increase spacing to differentiate leaves from expanded contents.
                    printSceneGraphNode(sceneManager, root, 0, true, true, false);
                    ImGui::PopStyleVar();

                    ImGui::EndChild();
                }

                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    _childNode = nullptr;
                    _tempParent = nullptr;

                    ImGui::CloseCurrentPopup();
                    _reparentSelectRequested = false;
                }

                ImGui::SameLine();
                if (ImGui::Button("Done", ImVec2(120, 0)))
                {
                    _reparentConfirmRequested = true;

                    ImGui::CloseCurrentPopup();
                    _reparentSelectRequested = false;
                }
                ImGui::EndPopup();
            }
        }
    }
}