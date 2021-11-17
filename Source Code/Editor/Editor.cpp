#include "stdafx.h"

#include "Headers/Editor.h"

#include "Editor/Widgets/Headers/EditorOptionsWindow.h"
#include "Editor/Widgets/Headers/MenuBar.h"
#include "Editor/Widgets/Headers/StatusBar.h"

#include "Editor/Widgets/DockedWindows/Headers/ContentExplorerWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/OutputWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/PostFXWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/PropertyWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/SceneViewWindow.h"
#include "Editor/Widgets/DockedWindows/Headers/SolutionExplorerWindow.h"

#include "Editor/Widgets/Headers/ImGuiExtensions.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Managers/Headers/SceneManager.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include "Rendering/Camera/Headers/FreeFlyCamera.h"

#include "Geometry/Shapes/Headers/Mesh.h"

#include "ECS/Components/Headers/TransformComponent.h"

#include "Platform/Video/Headers/CommandBufferPool.h"

#include <imgui_internal.h>
#include <imgui_club/imgui_memory_editor/imgui_memory_editor.h>
#include <ImGuiMisc/imguivariouscontrols/imguivariouscontrols.h>

namespace Divide {

namespace {
    constexpr F32 DEFAULT_MONITOR_DPI = 96.0f;
    const char* g_editorFontFile = "Arial.ttf";
    const char* g_editorSaveFile = "Editor.xml";
    const char* g_editorSaveFileBak = "Editor.xml.bak";

    WindowManager* g_windowManager = nullptr;

    struct ImGuiViewportData
    {
        DisplayWindow*  _window = nullptr;
        bool            _windowOwned = false;
    };


    struct TextureCallbackData {
        vec4<I32> _colourData = { 1, 1, 1, 1 };
        vec2<F32> _depthRange = { 0.002f, 1.f };
        bool _isDepthTexture = false;
        bool _flip = true;
    };

    hashMap<I64, TextureCallbackData> g_modalTextureData;
}

namespace ImGuiCustom {
    struct ImGuiAllocatorUserData {
        PlatformContext* _context = nullptr;
    };

    FORCE_INLINE void* MallocWrapper(const size_t size, void* user_data) noexcept {
        [[maybe_unused]] PlatformContext* context = static_cast<PlatformContext*>(user_data);

        return xmalloc(size);
    }

    FORCE_INLINE void FreeWrapper(void* ptr, void* user_data) noexcept {
        [[maybe_unused]] PlatformContext* context = static_cast<PlatformContext*>(user_data);

        xfree(ptr);
    }

    ImGuiMemAllocFunc g_ImAllocatorAllocFunc = MallocWrapper;
    ImGuiMemFreeFunc  g_ImAllocatorFreeFunc = FreeWrapper;
    ImGuiAllocatorUserData g_ImAllocatorUserData{};
}; //namespace ImGuiCustom

void InitBasicImGUIState(ImGuiIO& io) noexcept {
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.KeyMap[ImGuiKey_Tab] = to_I32(Input::KeyCode::KC_TAB);
    io.KeyMap[ImGuiKey_LeftArrow] = to_I32(Input::KeyCode::KC_LEFT);
    io.KeyMap[ImGuiKey_RightArrow] = to_I32(Input::KeyCode::KC_RIGHT);
    io.KeyMap[ImGuiKey_UpArrow] = to_I32(Input::KeyCode::KC_UP);
    io.KeyMap[ImGuiKey_DownArrow] = to_I32(Input::KeyCode::KC_DOWN);
    io.KeyMap[ImGuiKey_PageUp] = to_I32(Input::KeyCode::KC_PGUP);
    io.KeyMap[ImGuiKey_PageDown] = to_I32(Input::KeyCode::KC_PGDOWN);
    io.KeyMap[ImGuiKey_Home] = to_I32(Input::KeyCode::KC_HOME);
    io.KeyMap[ImGuiKey_End] = to_I32(Input::KeyCode::KC_END);
    io.KeyMap[ImGuiKey_Delete] = to_I32(Input::KeyCode::KC_DELETE);
    io.KeyMap[ImGuiKey_Backspace] = to_I32(Input::KeyCode::KC_BACK);
    io.KeyMap[ImGuiKey_Enter] = to_I32(Input::KeyCode::KC_RETURN);
    io.KeyMap[ImGuiKey_KeyPadEnter] = to_I32(Input::KeyCode::KC_NUMPADENTER);
    io.KeyMap[ImGuiKey_Escape] = to_I32(Input::KeyCode::KC_ESCAPE);
    io.KeyMap[ImGuiKey_Space] = to_I32(Input::KeyCode::KC_SPACE);
    io.KeyMap[ImGuiKey_A] = to_I32(Input::KeyCode::KC_A);
    io.KeyMap[ImGuiKey_C] = to_I32(Input::KeyCode::KC_C);
    io.KeyMap[ImGuiKey_V] = to_I32(Input::KeyCode::KC_V);
    io.KeyMap[ImGuiKey_X] = to_I32(Input::KeyCode::KC_X);
    io.KeyMap[ImGuiKey_Y] = to_I32(Input::KeyCode::KC_Y);
    io.KeyMap[ImGuiKey_Z] = to_I32(Input::KeyCode::KC_Z);

    io.SetClipboardTextFn = SetClipboardText;
    io.GetClipboardTextFn = GetClipboardText;
    io.ClipboardUserData = nullptr;
}

std::array<Input::MouseButton, 5> Editor::g_oisButtons = {
        Input::MouseButton::MB_Left,
        Input::MouseButton::MB_Right,
        Input::MouseButton::MB_Middle,
        Input::MouseButton::MB_Button3,
        Input::MouseButton::MB_Button4,
};

std::array<const char*, 3> Editor::g_supportedExportPlatforms = {
    "Windows",
    "Linux",
    "macOS"
};

Editor::Editor(PlatformContext& context, const ImGuiStyleEnum theme)
    : PlatformContextComponent(context),
      FrameListener("Editor", context.kernel().frameListenerMgr(), 9999),
      _editorUpdateTimer(Time::ADD_TIMER("Editor Update Timer")),
      _editorRenderTimer(Time::ADD_TIMER("Editor Render Timer")),
      _currentTheme(theme),
      _recentSceneList(10)
{
    ImGui::SetAllocatorFunctions(ImGuiCustom::g_ImAllocatorAllocFunc,
                                 ImGuiCustom::g_ImAllocatorFreeFunc,
                                 &ImGuiCustom::g_ImAllocatorUserData);

    _menuBar = eastl::make_unique<MenuBar>(context, true);
    _statusBar = eastl::make_unique<StatusBar>(context);
    _optionsWindow = eastl::make_unique<EditorOptionsWindow>(context);

    _undoManager = eastl::make_unique<UndoManager>(25);
    g_windowManager = &context.app().windowManager();
    _memoryEditorData = std::make_pair(nullptr, 0);
}

Editor::~Editor()
{
    close();
    for (DockedWindow* window : _dockedWindows) {
        MemoryManager::SAFE_DELETE(window);
    }

    g_windowManager = nullptr;
}

void Editor::idle() noexcept {
    NOP();
}

void Editor::createFontTexture(const F32 DPIScaleFactor) {
    constexpr F32 fontSize = 13.f;

    if (!_fontTexture) {
        TextureDescriptor texDescriptor(TextureType::TEXTURE_2D,
                                        GFXImageFormat::RGBA,
                                        GFXDataFormat::UNSIGNED_BYTE);

        ResourceDescriptor resDescriptor("IMGUI_font_texture");
        resDescriptor.threaded(false);
        resDescriptor.flag(true);
        resDescriptor.propertyDescriptor(texDescriptor);
        ResourceCache* parentCache = _context.kernel().resourceCache();
        _fontTexture = CreateResource<Texture>(parentCache, resDescriptor);
    }
    assert(_fontTexture);

    ImGuiIO& io = _imguiContexts[to_base(ImGuiContextType::Editor)]->IO;
    U8* pPixels = nullptr;
    I32 iWidth = 0;
    I32 iHeight = 0;
    ResourcePath fontPath(Paths::g_assetsLocation + Paths::g_GUILocation + Paths::g_fontsPath + g_editorFontFile);

    ImFontConfig font_cfg;
    font_cfg.OversampleH = font_cfg.OversampleV = 1;
    font_cfg.PixelSnapH = true;
    font_cfg.SizePixels = fontSize * DPIScaleFactor;
    font_cfg.EllipsisChar = (ImWchar)0x0085;
    font_cfg.GlyphOffset.y = 1.0f * IM_FLOOR(font_cfg.SizePixels / fontSize);  // Add +1 offset per 13 units
    ImFormatString(font_cfg.Name, IM_ARRAYSIZE(font_cfg.Name), "%s, %dpx", g_editorFontFile, (int)font_cfg.SizePixels);

    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize * DPIScaleFactor, &font_cfg);
    io.Fonts->GetTexDataAsRGBA32(&pPixels, &iWidth, &iHeight);
    _fontTexture->loadData({ (Byte*)pPixels, iWidth * iHeight * 4 }, vec2<U16>(iWidth, iHeight));
    // Store our identifier as reloding data may change the handle!
    io.Fonts->TexID = (void*)(intptr_t)_fontTexture->data()._textureHandle;
}

bool Editor::init(const vec2<U16>& renderResolution) {
    if (isInit()) {
        // double init
        return false;
    }
    
    if (!CreateDirectories((Paths::g_saveLocation + Paths::Editor::g_saveLocation).c_str())) {
        DebugBreak();
    }

    _mainWindow = &_context.app().windowManager().getWindow(0u);

    _editorCamera = Camera::createCamera<FreeFlyCamera>("Editor Camera");
    FreeFlyCamera* baseCamera = Camera::utilityCamera<FreeFlyCamera>(Camera::UtilityCamera::DEFAULT);
    _editorCamera->fromCamera(*baseCamera);

    IMGUI_CHECKVERSION();
    assert(_imguiContexts[to_base(ImGuiContextType::Editor)] == nullptr);
    
    ImGuiCustom::g_ImAllocatorUserData._context = &context();

    _imguiContexts[to_base(ImGuiContextType::Editor)] = ImGui::CreateContext();

    createFontTexture(1.f);

    ResourceCache* parentCache = _context.kernel().resourceCache();

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "IMGUI.glsl";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "IMGUI.glsl";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor shaderResDescriptor("IMGUI");
    shaderResDescriptor.propertyDescriptor(shaderDescriptor);
    _imguiProgram = CreateResource<ShaderProgram>(parentCache, shaderResDescriptor);

    ImGuiIO& io = _imguiContexts[to_base(ImGuiContextType::Editor)]->IO;
    ImGui::ResetStyle(_currentTheme);

    io.ConfigViewportsNoDecoration = true;
    io.ConfigViewportsNoTaskBarIcon = true;
    io.ConfigDockingTransparentPayload = true;
    io.ConfigViewportsNoAutoMerge = true;

    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking

    io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;        // We can honor io.WantSetMousePos requests (optional, rarely used)
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;  // We can create multi-viewports on the Platform side (optional)

    io.BackendPlatformName = "Divide Framework";

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    InitBasicImGUIState(io);

    io.DisplaySize.x = to_F32(_mainWindow->getDimensions().width);
    io.DisplaySize.y = to_F32(_mainWindow->getDimensions().height);

    const vec2<U16> display_size = _mainWindow->getDrawableSize();
    io.DisplayFramebufferScale.x = io.DisplaySize.x > 0 ? (F32)display_size.width / io.DisplaySize.x  : 0.f;
    io.DisplayFramebufferScale.y = io.DisplaySize.y > 0 ? (F32)display_size.height / io.DisplaySize.y : 0.f;

    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandle = _mainWindow;

    ImGuiPlatformIO& platform_io = _imguiContexts[to_base(ImGuiContextType::Editor)]->PlatformIO;
    platform_io.Platform_CreateWindow = [](ImGuiViewport* viewport)
    {
        if (g_windowManager != nullptr) {
            const DisplayWindow& window = g_windowManager->getWindow(0u);
            WindowDescriptor winDescriptor = {};
            winDescriptor.title = "No Title Yet";
            winDescriptor.targetDisplay = to_U32(window.currentDisplayIndex());
            winDescriptor.clearColour.set(0.0f, 0.0f, 0.0f, 1.0f);
            winDescriptor.flags = to_U16(WindowDescriptor::Flags::HIDDEN) | 
                                    to_U16(WindowDescriptor::Flags::CLEAR_COLOUR) |
                                    to_U16(WindowDescriptor::Flags::CLEAR_DEPTH);
            // We don't enable SDL_WINDOW_RESIZABLE because it enforce windows decorations
            winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_NoDecoration ? 0 : to_U32(WindowDescriptor::Flags::DECORATED);
            winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_NoDecoration ? 0 : to_U32(WindowDescriptor::Flags::RESIZEABLE);
            winDescriptor.flags |= viewport->Flags & ImGuiViewportFlags_TopMost ? to_U32(WindowDescriptor::Flags::ALWAYS_ON_TOP) : 0;
            winDescriptor.flags |= to_U32(WindowDescriptor::Flags::SHARE_CONTEXT);

            winDescriptor.dimensions.set(viewport->Size.x, viewport->Size.y);
            winDescriptor.position.set(viewport->Pos.x, viewport->Pos.y);
            winDescriptor.externalClose = true;
            winDescriptor.targetAPI = window.context().gfx().renderAPI();

            ErrorCode err = ErrorCode::NO_ERR;
            DisplayWindow* newWindow = g_windowManager->createWindow(winDescriptor, err);
            if (err == ErrorCode::NO_ERR) {
                assert(newWindow != nullptr);

                newWindow->hidden(false);
                newWindow->bringToFront();

                newWindow->addEventListener(WindowEvent::CLOSE_REQUESTED, [viewport]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept {
                    viewport->PlatformRequestClose = true;
                    return true; 
                });

                newWindow->addEventListener(WindowEvent::MOVED, [viewport]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept {
                    viewport->PlatformRequestMove = true;
                    return true;
                });

                newWindow->addEventListener(WindowEvent::RESIZED, [viewport]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept {
                    viewport->PlatformRequestResize = true;
                    return true;
                });

                viewport->PlatformHandle = (void*)newWindow;
                viewport->PlatformUserData = IM_NEW(ImGuiViewportData){newWindow, true};
            } else {
                DIVIDE_UNEXPECTED_CALL_MSG("Editor::Platform_CreateWindow failed!");
                g_windowManager->destroyWindow(newWindow);
            }
        }
    };

    platform_io.Platform_DestroyWindow = [](ImGuiViewport* viewport)
    {
        if (g_windowManager != nullptr) {
            if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData)
            {
                if (data->_window && data->_windowOwned) {
                    g_windowManager->destroyWindow(data->_window);
                }
                data->_window = nullptr;
                IM_DELETE(data);
            }
            viewport->PlatformUserData = viewport->PlatformHandle = nullptr;
        }
    };

    platform_io.Platform_ShowWindow = [](ImGuiViewport* viewport) {
        if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData)
        {
            data->_window->hidden(false);
        }
    };

    platform_io.Platform_SetWindowPos = [](ImGuiViewport* viewport, const ImVec2 pos) {
        if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData)
        {
            data->_window->setPosition((I32)pos.x, (I32)pos.y);
        }
    };

    platform_io.Platform_GetWindowPos = [](ImGuiViewport* viewport) -> ImVec2 {
        if (const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData)
        {
            const vec2<I32>& pos = data->_window->getPosition();
            return ImVec2((F32)pos.x, (F32)pos.y);
        }
        DIVIDE_UNEXPECTED_CALL_MSG("Editor::Platform_GetWindowPos failed!");
        return {};
    };

    platform_io.Platform_GetWindowSize = [](ImGuiViewport* viewport) -> ImVec2 {
        if (const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            const vec2<U16>& dim = data->_window->getDimensions();
            return ImVec2((F32)dim.width, (F32)dim.height);
        }
        DIVIDE_UNEXPECTED_CALL_MSG("Editor::Platform_GetWindowSize failed!");
        return {};
    };

    platform_io.Platform_GetWindowFocus = [](ImGuiViewport* viewport) -> bool {
        if (const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            return data->_window->hasFocus();
        }
        DIVIDE_UNEXPECTED_CALL_MSG("Editor::Platform_GetWindowFocus failed!");
        return false;
    };

    platform_io.Platform_SetWindowAlpha = [](ImGuiViewport* viewport, const float alpha) {
        if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            data->_window->opacity(to_U8(alpha * 255));
        }
    };

    platform_io.Platform_SetWindowSize = [](ImGuiViewport* viewport, ImVec2 size) {
        if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            WAIT_FOR_CONDITION(data->_window->setDimensions(to_U16(size.x), to_U16(size.y)));
        }
    };

    platform_io.Platform_SetWindowFocus = [](ImGuiViewport* viewport) {
        if (const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            data->_window->bringToFront();
        }
    };

    platform_io.Platform_SetWindowTitle = [](ImGuiViewport* viewport, const char* title) {
        if (const ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            data->_window->title(title);
        }
    };

    platform_io.Platform_RenderWindow = [](ImGuiViewport* viewport, void* platformContext) {
        if (PlatformContext* context = (PlatformContext*)platformContext) {
            context->gfx().beginFrame(*(DisplayWindow*)viewport->PlatformHandle, false);
        }
    };

    platform_io.Renderer_RenderWindow = [](ImGuiViewport* viewport, void* platformContext) {
        if (PlatformContext* context = (PlatformContext*)platformContext) {
            Editor* editor = &context->editor();

            ImGui::SetCurrentContext(editor->_imguiContexts[to_base(ImGuiContextType::Editor)]);
            GFX::ScopedCommandBuffer sBuffer = GFX::AllocateScopedCommandBuffer();
            GFX::CommandBuffer& buffer = sBuffer();
            const ImDrawData* pDrawData = viewport->DrawData;
            const I32 fb_width = to_I32(pDrawData->DisplaySize.x * ImGui::GetIO().DisplayFramebufferScale.x);
            const I32 fb_height = to_I32(pDrawData->DisplaySize.y * ImGui::GetIO().DisplayFramebufferScale.y);
            editor->renderDrawList(viewport->DrawData, Rect<I32>(0, 0, fb_width, fb_height), ((DisplayWindow*)viewport->PlatformHandle)->getGUID(), buffer);
            context->gfx().flushCommandBuffer(buffer);
        }
    };

    platform_io.Platform_SwapBuffers = [](ImGuiViewport* viewport, void* platformContext) {
        if (g_windowManager != nullptr) {
            PlatformContext* context = (PlatformContext*)platformContext;
            context->gfx().endFrame(*(DisplayWindow*)viewport->PlatformHandle, false);
        }
    };

    platform_io.Platform_OnChangedViewport = [](ImGuiViewport* viewport) {
        static F32 previousDPIScale = 1.f;
        if (ImGuiViewportData* data = (ImGuiViewportData*)viewport->PlatformUserData) {
            if (viewport->DpiScale != previousDPIScale) {
                previousDPIScale = viewport->DpiScale;
                ImGui::GetStyle().ScaleAllSizes(previousDPIScale);
                data->_window->context().editor().createFontTexture(previousDPIScale);
            }
        }
    };

    const vector<WindowManager::MonitorData>& monitors = g_windowManager->monitorData();
    const I32 monitorCount = to_I32(monitors.size());

    platform_io.Monitors.resize(monitorCount);

    for (I32 i = 0; i < monitorCount; ++i) {
        const WindowManager::MonitorData& monitor = monitors[i];
        ImGuiPlatformMonitor& imguiMonitor = platform_io.Monitors[i];

        // Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
        imguiMonitor.MainPos = ImVec2(to_F32(monitor.viewport.x), to_F32(monitor.viewport.y));
        imguiMonitor.WorkPos = ImVec2(to_F32(monitor.drawableArea.x), to_F32(monitor.drawableArea.y));

        imguiMonitor.MainSize = ImVec2(to_F32(monitor.viewport.z), to_F32(monitor.viewport.w));
        imguiMonitor.WorkSize = ImVec2(to_F32(monitor.drawableArea.z), to_F32(monitor.drawableArea.w));
        imguiMonitor.DpiScale = monitor.dpi / DEFAULT_MONITOR_DPI;
    }
    ImGuiViewportData* data = IM_NEW(ImGuiViewportData)();
    data->_window = _mainWindow;
    data->_windowOwned = false;
    main_viewport->PlatformUserData = data;
    

    ImGuiContext*& gizmoContext = _imguiContexts[to_base(ImGuiContextType::Gizmo)];
    gizmoContext = ImGui::CreateContext(io.Fonts);
    InitBasicImGUIState(gizmoContext->IO);
    gizmoContext->Viewports[0]->PlatformHandle = _mainWindow;
    _gizmo = eastl::make_unique<Gizmo>(*this, gizmoContext);

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    DockedWindow::Descriptor descriptor = {};
    descriptor.position = ImVec2(0, 0);
    descriptor.size = ImVec2(300, 550);
    descriptor.minSize = ImVec2(200, 200);
    descriptor.name = "Solution Explorer";
    _dockedWindows[to_base(WindowType::SolutionExplorer)] = MemoryManager_NEW SolutionExplorerWindow(*this, _context, descriptor);

    descriptor.position = ImVec2(0, 0);
    descriptor.minSize = ImVec2(200, 200);
    descriptor.name = "PostFX Settings";
    _dockedWindows[to_base(WindowType::PostFX)] = MemoryManager_NEW PostFXWindow(*this, _context, descriptor);

    descriptor.position = ImVec2(to_F32(renderResolution.width) - 300, 0);
    descriptor.name = "Property Explorer";
    _dockedWindows[to_base(WindowType::Properties)] = MemoryManager_NEW PropertyWindow(*this, _context, descriptor);

    descriptor.position = ImVec2(0, 550.0f);
    descriptor.size = ImVec2(to_F32(renderResolution.width * 0.5f), to_F32(renderResolution.height) - 550 - 3);
    descriptor.name = "Content Explorer";
    descriptor.flags |= ImGuiWindowFlags_NoTitleBar;
    _dockedWindows[to_base(WindowType::ContentExplorer)] = MemoryManager_NEW ContentExplorerWindow(*this, descriptor);

    descriptor.position = ImVec2(to_F32(renderResolution.width * 0.5f), 550);
    descriptor.size = ImVec2(to_F32(renderResolution.width * 0.5f), to_F32(renderResolution.height) - 550 - 3);
    descriptor.name = "Application Output";
    _dockedWindows[to_base(WindowType::Output)] = MemoryManager_NEW OutputWindow(*this, descriptor);

    descriptor.position = ImVec2(150, 150);
    descriptor.size = ImVec2(640, 480);
    descriptor.name = "Scene View";
    descriptor.minSize = ImVec2(100, 100);
    descriptor.flags = 0;
    _dockedWindows[to_base(WindowType::SceneView)] = MemoryManager_NEW SceneViewWindow(*this, descriptor);

    return loadFromXML();
}

void Editor::close() {
    if (saveToXML()) {
        _context.config().save();
    }

    _fontTexture.reset();
    _imguiProgram.reset();
    _gizmo.reset();

    for (ImGuiContext* context : _imguiContexts) {
        if (context == nullptr) {
            continue;
        }

        ImGui::SetCurrentContext(context);
        ImGui::DestroyPlatformWindows();
        ImGui::DestroyContext(context);
    }
    _imguiContexts.fill(nullptr);
    Camera::destroyCamera<FreeFlyCamera>(_editorCamera);
}

void Editor::onPreviewFocus(const bool state) const {
    ImGuiIO& io = ImGui::GetIO();
    if (state) {
        io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavNoCaptureKeyboard;
    }

    _context.kernel().sceneManager()->onChangeFocus(state);
    Attorney::GizmoEditor::onSceneFocus(_gizmo.get(),  state);
}

void Editor::toggle(const bool state) {
    if (running() == state) {
        return;
    }

    const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();

    running(state);

    if (!state) {
        scenePreviewHovered(false);
        scenePreviewFocused(false);
        onPreviewFocus(false);

        _context.config().save();
        activeScene.state()->renderState().disableOption(SceneRenderState::RenderOptions::SCENE_GIZMO);
        activeScene.state()->renderState().disableOption(SceneRenderState::RenderOptions::SELECTION_GIZMO);
        activeScene.state()->renderState().disableOption(SceneRenderState::RenderOptions::ALL_GIZMOS);
        _context.kernel().sceneManager()->resetSelection(0);
        _stepQueue = 2;
    } else {
        _stepQueue = 0;
        activeScene.state()->renderState().enableOption(SceneRenderState::RenderOptions::SCENE_GIZMO);
        activeScene.state()->renderState().enableOption(SceneRenderState::RenderOptions::SELECTION_GIZMO);
        static_cast<ContentExplorerWindow*>(_dockedWindows[to_base(WindowType::ContentExplorer)])->init();
        /*const Selections& selections = activeScene.getCurrentSelection();
        if (selections._selectionCount == 0) {
            SceneGraphNode* root = activeScene.sceneGraph().getRoot();
            _context.kernel().sceneManager()->setSelected(0, { &root });
        }*/
    }

    _gizmo->enable(state && simulationPauseRequested());
}

void Editor::update(const U64 deltaTimeUS) {
    OPTICK_EVENT();
    static bool allGizmosEnabled = false;

    Time::ScopedTimer timer(_editorUpdateTimer);

    for (ImGuiContext* context : _imguiContexts) {
        ImGui::SetCurrentContext(context);

        ImGuiIO& io = context->IO;
        io.DeltaTime = Time::MicrosecondsToSeconds<F32>(deltaTimeUS);

        ToggleCursor(!io.MouseDrawCursor);
        if (io.MouseDrawCursor || ImGui::GetMouseCursor() == ImGuiMouseCursor_None) {
            WindowManager::SetCursorStyle(CursorStyle::NONE);
        } else if (!COMPARE(io.MousePos.x, -1.f) && !COMPARE(io.MousePos.y, -1.f)) {
            switch (ImGui::GetCurrentContext()->MouseCursor)
            {
                default:
                case ImGuiMouseCursor_Arrow:
                    WindowManager::SetCursorStyle(CursorStyle::ARROW);
                    break;
                case ImGuiMouseCursor_TextInput:         // When hovering over InputText, etc.
                    WindowManager::SetCursorStyle(CursorStyle::TEXT_INPUT);
                    break;
                case ImGuiMouseCursor_ResizeAll:         // Unused
                    WindowManager::SetCursorStyle(CursorStyle::RESIZE_ALL);
                    break;
                case ImGuiMouseCursor_ResizeNS:          // Unused
                    WindowManager::SetCursorStyle(CursorStyle::RESIZE_NS);
                    break;
                case ImGuiMouseCursor_ResizeEW:          // When hovering over a column
                    WindowManager::SetCursorStyle(CursorStyle::RESIZE_EW);
                    break;
                case ImGuiMouseCursor_ResizeNESW:        // Unused
                    WindowManager::SetCursorStyle(CursorStyle::RESIZE_NESW);
                    break;
                case ImGuiMouseCursor_ResizeNWSE:        // When hovering over the bottom-right corner of a window
                    WindowManager::SetCursorStyle(CursorStyle::RESIZE_NWSE);
                    break;
                case ImGuiMouseCursor_Hand:
                    WindowManager::SetCursorStyle(CursorStyle::HAND);
                    break;
            }
        }
    }
    
    Attorney::GizmoEditor::update(_gizmo.get(), deltaTimeUS);
    if (running()) {
        _statusBar->update(deltaTimeUS);
        _optionsWindow->update(deltaTimeUS);

        static_cast<ContentExplorerWindow*>(_dockedWindows[to_base(WindowType::ContentExplorer)])->update(deltaTimeUS);


        if (_isScenePaused != simulationPauseRequested()) {
            _isScenePaused = simulationPauseRequested();

            _gizmo->enable(_isScenePaused);
            SceneManager* sMgr = _context.kernel().sceneManager();
            sMgr->resetSelection(0);
            const Scene& activeScene = sMgr->getActiveScene();
            
            const PlayerIndex idx = sMgr->playerPass();
            SceneStatePerPlayer& playerState = activeScene.state()->playerState(idx);
            if (_isScenePaused) {
                playerState.overrideCamera(editorCamera());
                activeScene.state()->renderState().enableOption(SceneRenderState::RenderOptions::SCENE_GIZMO);
                activeScene.state()->renderState().enableOption(SceneRenderState::RenderOptions::SELECTION_GIZMO);
                if (allGizmosEnabled) {
                    activeScene.state()->renderState().enableOption(SceneRenderState::RenderOptions::ALL_GIZMOS);
                }
            } else {
                playerState.overrideCamera(nullptr);
                allGizmosEnabled = activeScene.state()->renderState().isEnabledOption(SceneRenderState::RenderOptions::ALL_GIZMOS);
                activeScene.state()->renderState().disableOption(SceneRenderState::RenderOptions::SCENE_GIZMO);
                activeScene.state()->renderState().disableOption(SceneRenderState::RenderOptions::SELECTION_GIZMO);
                activeScene.state()->renderState().disableOption(SceneRenderState::RenderOptions::ALL_GIZMOS);
            }
        }
    }
}

bool Editor::render([[maybe_unused]] const U64 deltaTime) {
    const F32 statusBarHeight = _statusBar->height();
    
    static ImGuiDockNodeFlags opt_flags = ImGuiDockNodeFlags_None | ImGuiDockNodeFlags_PassthruCentralNode;
    // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    // because it would be confusing to have two docking targets within each others.
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size + ImVec2(0.0f, -statusBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    windowFlags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    const bool optionsVisible = _showOptionsWindow;

    if (opt_flags & ImGuiDockNodeFlags_PassthruCentralNode) {
        ImGui::SetNextWindowBgAlpha(0.0f);
    }
    if (windowFlags & ImGuiDockNodeFlags_PassthruCentralNode) {
        windowFlags |= ImGuiWindowFlags_NoBackground;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Editor", nullptr, windowFlags);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar(2);

    ImGui::DockSpace(ImGui::GetID("EditorDockspace"), ImVec2(0.0f, 0.0f), opt_flags);

    if (scenePreviewFocused() || optionsVisible) {
        PushReadOnly();
    }

    _menuBar->draw();
    _statusBar->draw();

    for (DockedWindow* window : _dockedWindows) {
        window->draw();
    }

    if (_showMemoryEditor && !optionsVisible) {
        if (_memoryEditorData.first != nullptr && _memoryEditorData.second > 0) {
            static MemoryEditor memEditor;
            memEditor.DrawWindow("Memory Editor", _memoryEditorData.first, _memoryEditorData.second);
        }
    }

    if (_showSampleWindow && !optionsVisible) {
        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
        ImGui::ShowDemoWindow(&_showSampleWindow);
    }

    if (scenePreviewFocused() || optionsVisible) {
        PopReadOnly();
    }

    _optionsWindow->draw(_showOptionsWindow);

    ImGui::End();

    return true;
}

void Editor::drawScreenOverlay(const Camera* camera, const Rect<I32>& targetViewport, GFX::CommandBuffer& bufferInOut) const {
    Attorney::GizmoEditor::render(_gizmo.get(), camera, targetViewport, bufferInOut);
}

bool Editor::framePostRenderStarted(const FrameEvent& evt) {
    for (DockedWindow* window : _dockedWindows) {
        window->backgroundUpdate();
    }

    if (!running()) {
        return true;
    }

    Time::ScopedTimer timer(_editorRenderTimer);
    ImGui::SetCurrentContext(_imguiContexts[to_base(ImGuiContextType::Editor)]);

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        io.MouseHoveredViewport = 0;
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        for (I32 n = 0; n < platform_io.Viewports.Size; n++) {
            const ImGuiViewport* viewport = platform_io.Viewports[n];
            const DisplayWindow* window = static_cast<DisplayWindow*>(viewport->PlatformHandle);
            if (window != nullptr && window->isHovered() && !(viewport->Flags & ImGuiViewportFlags_NoInputs)) {
                ImGui::GetIO().MouseHoveredViewport = viewport->ID;
            }
        }
    }

    ImGui::NewFrame();

    if (render(evt._timeSinceLastFrameUS)) {
        ImGui::Render();

        GFX::ScopedCommandBuffer sBuffer = GFX::AllocateScopedCommandBuffer();
        GFX::CommandBuffer& buffer = sBuffer();
        ImDrawData* pDrawData = ImGui::GetDrawData();
        const I32 fb_width = to_I32(pDrawData->DisplaySize.x * ImGui::GetIO().DisplayFramebufferScale.x);
        const I32 fb_height = to_I32(pDrawData->DisplaySize.y * ImGui::GetIO().DisplayFramebufferScale.y);
        renderDrawList(pDrawData, Rect<I32>(0, 0, fb_width, fb_height), _mainWindow->getGUID(), buffer);
        _context.gfx().flushCommandBuffer(buffer, false);


        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault(&context(), &context());
        }

        return true;
    }

    return false;
}


bool Editor::frameEnded([[maybe_unused]] const FrameEvent& evt) noexcept {

    if (running() && _stepQueue > 0) {
        --_stepQueue;
    }

    return true;
}

Rect<I32> Editor::scenePreviewRect(const bool globalCoords) const noexcept {
    const SceneViewWindow* sceneView = static_cast<SceneViewWindow*>(_dockedWindows[to_base(WindowType::SceneView)]);
    if (globalCoords) {
        return sceneView->sceneRect();
    }

    Rect<I32> rect = sceneView->sceneRect();
    const vec2<I32>& offset = sceneView->getWindowOffset();
    rect.x -= offset.x;
    rect.y -= offset.y;
    return rect;
}

// Needs to be rendered immediately. *IM*GUI. IMGUI::NewFrame invalidates this data
void Editor::renderDrawList(ImDrawData* pDrawData, const Rect<I32>& targetViewport, I64 windowGUID, GFX::CommandBuffer& bufferInOut) const
{
    static bool s_initDrawCmds = false;
    static GFX::BindPipelineCommand s_pipelineCmd = {};
    static GFX::SendPushConstantsCommand s_pushConstantsCommand = {};
    static GFX::SetBlendCommand s_blendCmd = {};
    static GFX::BeginDebugScopeCommand s_beginDebugScope{ "Render IMGUI" };

    if (windowGUID == -1) {
        windowGUID = _mainWindow->getGUID();
    }

    const ImGuiIO& io = ImGui::GetIO();

    if (targetViewport.z <= 0 || targetViewport.w <= 0) {
        return;
    }

    pDrawData->ScaleClipRects(io.DisplayFramebufferScale);

    if (pDrawData->CmdListsCount == 0) {
        return;
    }

    if (!s_initDrawCmds) {
        RenderStateBlock state = {};
        state.setCullMode(CullMode::NONE);
        state.depthTestEnabled(false);
        state.setScissorTest(true);

        PipelineDescriptor pipelineDesc = {};
        pipelineDesc._stateHash = state.getHash();
        pipelineDesc._shaderProgramHandle = _imguiProgram->getGUID();
        s_pipelineCmd._pipeline = _context.gfx().newPipeline(pipelineDesc);

        PushConstants pushConstants = {};
        pushConstants.set(_ID("toggleChannel"), GFX::PushConstantType::IVEC4, vec4<I32>(1, 1, 1, 1));
        pushConstants.set(_ID("depthTexture"), GFX::PushConstantType::INT, 0);
        pushConstants.set(_ID("depthRange"), GFX::PushConstantType::VEC2, vec2<F32>(0.0f, 1.0f));

        s_pushConstantsCommand._constants = pushConstants;

        s_blendCmd._blendProperties = BlendingProperties{
            BlendProperty::SRC_ALPHA,
            BlendProperty::INV_SRC_ALPHA,
            BlendOperation::ADD
        };
        s_initDrawCmds = true;
    }

    EnqueueCommand(bufferInOut, s_beginDebugScope);

    s_blendCmd._blendProperties._enabled = true;
    EnqueueCommand(bufferInOut, s_blendCmd);
    EnqueueCommand(bufferInOut, s_pipelineCmd);
    EnqueueCommand(bufferInOut, s_pushConstantsCommand);
    EnqueueCommand(bufferInOut, GFX::SetViewportCommand{ targetViewport });

    const F32 L = pDrawData->DisplayPos.x;
    const F32 R = pDrawData->DisplayPos.x + pDrawData->DisplaySize.x;
    const F32 T = pDrawData->DisplayPos.y;
    const F32 B = pDrawData->DisplayPos.y + pDrawData->DisplaySize.y;
    const F32 ortho_projection[4][4] =
    {
        { 2.0f / (R - L),    0.0f,               0.0f,   0.0f },
        { 0.0f,              2.0f / (T - B),     0.0f,   0.0f },
        { 0.0f,              0.0f,              -1.0f,   0.0f },
        { (R + L) / (L - R), (T + B) / (B - T),  0.0f,   1.0f },
    };

    GFX::SetCameraCommand cameraCmd = {};
    cameraCmd._cameraSnapshot = Camera::utilityCamera(Camera::UtilityCamera::_2D_FLIP_Y)->snapshot();
    memcpy(cameraCmd._cameraSnapshot._projectionMatrix.m, ortho_projection, sizeof(F32) * 16);
    EnqueueCommand(bufferInOut, cameraCmd);

    GFX::DrawIMGUICommand drawIMGUI = {};
    drawIMGUI._data = pDrawData;
    drawIMGUI._windowGUID = windowGUID;
    EnqueueCommand(bufferInOut, drawIMGUI);

    s_blendCmd._blendProperties._enabled = false;
    EnqueueCommand(bufferInOut, s_blendCmd);

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(bufferInOut);
}

void Editor::selectionChangeCallback(const PlayerIndex idx, const vector<SceneGraphNode*>& nodes) const {
    if (idx != 0) {
        return;
    }

    Attorney::GizmoEditor::updateSelection(_gizmo.get(), nodes);
}

void Editor::copyPlayerCamToEditorCam() noexcept {
    _editorCamera->fromCamera(*Attorney::SceneManagerEditor::playerCamera(_context.kernel().sceneManager(), 0, true));
}

bool Editor::Undo() const {
    if (_undoManager->Undo()) {
        showStatusMessage(Util::StringFormat("Undo: %s", _undoManager->lasActionName().c_str()), Time::SecondsToMilliseconds<F32>(2.0f), false);
        return true;
    }

    return false;
}

bool Editor::Redo() const {
    if (_undoManager->Redo()) {
        showStatusMessage(Util::StringFormat("Redo: %s", _undoManager->lasActionName().c_str()), Time::SecondsToMilliseconds<F32>(2.0f), false);
        return true;
    }

    return false;
}

/// Key pressed: return true if input was consumed
bool Editor::onKeyDown(const Input::KeyEvent& key) {
    if (!isInit() || !running() || !inEditMode() && scenePreviewFocused()) {
        return false;
    }

    if (scenePreviewFocused()) {
        return _gizmo->onKey(true, key);
    }

    ImGuiIO& io = _imguiContexts[to_base(ImGuiContextType::Editor)]->IO;

    io.KeysDown[to_I32(key._key)] = true;
    if (key._text != nullptr) {
        io.AddInputCharactersUTF8(key._text);
    }

    if (key._key == Input::KeyCode::KC_LCONTROL || key._key == Input::KeyCode::KC_RCONTROL) {
        io.KeyCtrl = true;
    }
    if (key._key == Input::KeyCode::KC_LSHIFT || key._key == Input::KeyCode::KC_RSHIFT) {
        io.KeyShift = true;
    }
    if (key._key == Input::KeyCode::KC_LMENU || key._key == Input::KeyCode::KC_RMENU) {
        io.KeyAlt = true;
    }
    if (key._key == Input::KeyCode::KC_LWIN || key._key == Input::KeyCode::KC_RWIN) {
        io.KeySuper = true;
    }

    return wantsKeyboard();
}

// Key released: return true if input was consumed
bool Editor::onKeyUp(const Input::KeyEvent& key) {
    if (!isInit() || !running() || !inEditMode() && scenePreviewFocused()) {
        return false;
    }

    ImGuiIO& io = _imguiContexts[to_base(ImGuiContextType::Editor)]->IO;
    if (io.KeyCtrl) {
        if (key._key == Input::KeyCode::KC_Z) {
            if (Undo()) {
                return true;
            }
        } else if (key._key == Input::KeyCode::KC_Y) {
            if (Redo()) {
                return true;
            }
        }
    }

    if (scenePreviewFocused()) {
        return _gizmo->onKey(false, key);
    }

    io.KeysDown[to_I32(key._key)] = false;

    if (key._key == Input::KeyCode::KC_LCONTROL || key._key == Input::KeyCode::KC_RCONTROL) {
        io.KeyCtrl = false;
    }

    if (key._key == Input::KeyCode::KC_LSHIFT || key._key == Input::KeyCode::KC_RSHIFT) {
        io.KeyShift = false;
    }

    if (key._key == Input::KeyCode::KC_LMENU || key._key == Input::KeyCode::KC_RMENU) {
        io.KeyAlt = false;
    }

    if (key._key == Input::KeyCode::KC_LWIN || key._key == Input::KeyCode::KC_RWIN) {
        io.KeySuper = false;
    }

    return wantsKeyboard();
}

ImGuiViewport* Editor::FindViewportByPlatformHandle(ImGuiContext* context, const DisplayWindow* window) {
    if (window != nullptr) {
        for (I32 i = 0; i != context->Viewports.Size; i++) {
            const DisplayWindow* it = static_cast<DisplayWindow*>(context->Viewports[i]->PlatformHandle);

            if (it != nullptr && it->getGUID() == window->getGUID()) {
                return context->Viewports[i];
            }
        }
    }

    return nullptr;
}

/// Mouse moved: return true if input was consumed
bool Editor::mouseMoved(const Input::MouseMoveEvent& arg) {
    if (!isInit() || !running() || WindowManager::IsRelativeMouseMode()) {
        return false;
    }

    if (!arg.wheelEvent()) {
        ImGuiViewport* viewport = nullptr;
                
        DisplayWindow* focusedWindow = g_windowManager->getFocusedWindow();
        if (focusedWindow == nullptr) {
            focusedWindow = g_windowManager->mainWindow();
        }

        ImGuiContext* editorContext = _imguiContexts[to_base(ImGuiContextType::Editor)];
        ImGuiContext* gizmoContext = _imguiContexts[to_base(ImGuiContextType::Gizmo)];
        assert(editorContext->IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable);
        viewport = FindViewportByPlatformHandle(editorContext, focusedWindow);

        vec2<I32> mPosGlobal(-1);
        Rect<I32> viewportSize(-1);
        WindowManager::GetMouseState(mPosGlobal, true);
        if (viewport == nullptr) {
            mPosGlobal -= focusedWindow->getPosition();
            viewportSize = {mPosGlobal.x, mPosGlobal.y, focusedWindow->getDrawableSize().x, focusedWindow->getDrawableSize().y };
        } else {
            viewportSize = {viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y };
        }

        bool anyDown = false;
        for (U8 i = 0; i < to_U8(ImGuiContextType::COUNT); ++i) {
            ImGuiIO& io = _imguiContexts[i]->IO;

            if (io.WantSetMousePos) {
                WindowManager::SetGlobalCursorPosition(to_I32(io.MousePos.x), to_I32(io.MousePos.y));
            } else {
                io.MousePos = ImVec2(to_F32(mPosGlobal.x), to_F32(mPosGlobal.y));
            }

            for (const bool down : io.MouseDown) {
                if (down) {
                    anyDown = true;
                    break;
                }
            }
        }
        WindowManager::SetCaptureMouse(anyDown);
        const SceneViewWindow* sceneView = static_cast<SceneViewWindow*>(_dockedWindows[to_base(WindowType::SceneView)]);
        const ImVec2 editorMousePos = _imguiContexts[to_base(ImGuiContextType::Editor)]->IO.MousePos;
        scenePreviewHovered(sceneView->isHovered() && sceneView->sceneRect().contains(editorMousePos.x, editorMousePos.y));

        vec2<I32> gizmoMousePos(editorMousePos.x, editorMousePos.y);
        const Rect<I32>& sceneRect = scenePreviewRect(true);
        gizmoMousePos = COORD_REMAP(gizmoMousePos, sceneRect, Rect<I32>(0, 0, viewportSize.z, viewportSize.w));
        gizmoContext->IO.MousePos = ImVec2(to_F32(gizmoMousePos.x), to_F32(gizmoMousePos.y));
        if (_gizmo->hovered()) {
            return true;
        }
    } else {
        for (ImGuiContext* ctx : _imguiContexts) {
            if (arg.WheelH() > 0) {
                ctx->IO.MouseWheelH += 1;
            }
            if (arg.WheelH() < 0) {
                ctx->IO.MouseWheelH -= 1;
            }
            if (arg.WheelV() > 0) {
                ctx->IO.MouseWheel += 1;
            }
            if (arg.WheelV() < 0) {
                ctx->IO.MouseWheel -= 1;
            }
        }
    }

    return wantsMouse();
}

/// Mouse button pressed: return true if input was consumed
bool Editor::mouseButtonPressed(const Input::MouseButtonEvent& arg) {
    if (!isInit() || !running() || WindowManager::IsRelativeMouseMode()) {
        return false;
    }

    for (ImGuiContext* ctx : _imguiContexts) {
        for (U8 i = 0; i < 5; ++i) {
            if (arg.button() == g_oisButtons[i]) {
                ctx->IO.MouseDown[i] = true;
                break;
            }
        }
    }

    if (scenePreviewFocused()) {
        _gizmo->onMouseButton(true);
    }
    
    return wantsMouse();
}

/// Mouse button released: return true if input was consumed
bool Editor::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    if (!isInit() || !running() || WindowManager::IsRelativeMouseMode()) {
        return false;
    }

    if (scenePreviewFocused() != scenePreviewHovered()) {
        scenePreviewFocused(scenePreviewHovered());
        onPreviewFocus(scenePreviewHovered());

        ImGuiContext* editorContext = _imguiContexts[to_base(ImGuiContextType::Editor)];
        ImGui::SetCurrentContext(editorContext);
        if (!_autoFocusEditor) {
            return !scenePreviewHovered();
        }
    }

    for (ImGuiContext* ctx : _imguiContexts) {
        for (U8 i = 0; i < 5; ++i) {
            if (arg.button() == g_oisButtons[i]) {
                ctx->IO.MouseDown[i] = false;
                break;
            }
        }
    }

    _gizmo->onMouseButton(false);

    return wantsMouse();
}

bool Editor::joystickButtonPressed([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::joystickButtonReleased([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::joystickAxisMoved([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::joystickPovMoved([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::joystickBallMoved([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::joystickAddRemove([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::joystickRemap([[maybe_unused]] const Input::JoystickEvent &arg) noexcept {

    if (!isInit() || !running()) {
        return false;
    }

    return wantsJoystick();
}

bool Editor::wantsMouse() const {
    if (!isInit() || !running()) {
        return false;
    }

    if (scenePreviewFocused()) {
        if (!simulationPauseRequested()) {
            return false;
        }

        return _gizmo->needsMouse();
    }

    for (const ImGuiContext* ctx : _imguiContexts) {
        if (ctx->IO.WantCaptureMouse) {
            return true;
        }
    }

    return false;
}

bool Editor::wantsKeyboard() const noexcept {
    if (!isInit() || !running() || scenePreviewFocused()) {
        return false;
    }
    if (scenePreviewFocused() && !simulationPauseRequested()) {
        return false;
    }
    for (const ImGuiContext* ctx : _imguiContexts) {
        if (ctx->IO.WantCaptureKeyboard) {
            return true;
        }
    }

    return false;
}

bool Editor::wantsJoystick() const noexcept {
    if (!isInit() || !running()) {
        return false;
    }

    return !scenePreviewFocused();
}

bool Editor::usingGizmo() const {
    if (!isInit() || !running()) {
        return false;
    }

    return _gizmo->needsMouse();
}

bool Editor::onUTF8(const Input::UTF8Event& arg) {
    if (!isInit() || !running() || scenePreviewFocused()) {
        return false;
    }

    bool wantsCapture = false;
    for (U8 i = 0; i < to_U8(ImGuiContextType::COUNT); ++i) {
        ImGuiIO& io = _imguiContexts[i]->IO;
        io.AddInputCharactersUTF8(arg._text);
        wantsCapture = io.WantCaptureKeyboard || wantsCapture;
    }

    return wantsCapture;
}

void Editor::onSizeChange(const SizeChangeParams& params) {
    if (!params.isWindowResize) {
        _targetViewport.set(0, 0, params.width, params.height);
    } else if (_mainWindow != nullptr && params.winGUID == _mainWindow->getGUID()) {
        if (!isInit()) {
            return;
        }

        const vec2<U16> displaySize = _mainWindow->getDrawableSize();

        for (U8 i = 0u; i < to_U8(ImGuiContextType::COUNT); ++i) {
            ImGuiIO& io = _imguiContexts[i]->IO;
            io.DisplaySize.x = to_F32(params.width);
            io.DisplaySize.y = to_F32(params.height);
            io.DisplayFramebufferScale = ImVec2(params.width > 0u ? to_F32(displaySize.width) / params.width : 0.f,
                                                params.height > 0u ? to_F32(displaySize.height) / params.height : 0.f);
        }
    }
}

bool Editor::saveSceneChanges(const DELEGATE<void, std::string_view>& msgCallback, const DELEGATE<void, bool>& finishCallback, const char* sceneNameOverride) const {
    if (_context.kernel().sceneManager()->saveActiveScene(false, true, msgCallback, finishCallback, sceneNameOverride)) {
        if (saveToXML()) {
            _context.config().save();
            return true;
        }
    }

    return false;
}

bool Editor::switchScene(const char* scenePath) {
    static CircularBuffer<Str256> tempBuffer(10);

    if (Util::IsEmptyOrNull(scenePath)) {
        return false;
    }

    const auto [sceneName, _] = splitPathToNameAndLocation(scenePath);
    if (Util::CompareIgnoreCase(sceneName, Config::DEFAULT_SCENE_NAME)) {
        showStatusMessage("Error: can't load default scene! Selected scene is only used as a template!", Time::SecondsToMilliseconds<F32>(3.f), true);
        return false;
    }

    if (!_context.kernel().sceneManager()->switchScene(sceneName.c_str(), true, true, false)) {
        Console::errorfn(Locale::Get(_ID("ERROR_SCENE_LOAD")), sceneName.c_str());
        showStatusMessage(Util::StringFormat(Locale::Get(_ID("ERROR_SCENE_LOAD")), sceneName.c_str()), Time::SecondsToMilliseconds<F32>(3.f), true);
        return false;
    }

    tempBuffer.reset();
    const Str256 nameToAdd(sceneName.c_str());
    for (size_t i = 0u; i < _recentSceneList.size(); ++i) {
        const Str256& crtEntry = _recentSceneList.get(i);
        if (crtEntry != nameToAdd) {
            tempBuffer.put(crtEntry);
        }
    }
    tempBuffer.put(nameToAdd);
    _recentSceneList.reset();
    size_t i = tempBuffer.size();
    while(i--) {
        _recentSceneList.put(tempBuffer.get(i));
    }
    
    return true;
}

void Editor::onChangeScene(Scene* newScene) {
    _lastOpenSceneGUID = newScene == nullptr ? -1 : newScene->getGUID();
}

U32 Editor::saveItemCount() const noexcept {
    U32 ret = 10u; // All of the scene stuff (settings, music, etc)

    const auto& graph = _context.kernel().sceneManager()->getActiveScene().sceneGraph();
    ret += to_U32(graph->getTotalNodeCount());

    return ret;
}

bool Editor::isDefaultScene() const noexcept {
    const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();
    return activeScene.getGUID() == Scene::DEFAULT_SCENE_GUID;
}

bool Editor::modalTextureView(const char* modalName, const Texture* tex, const vec2<F32>& dimensions, const bool preserveAspect, const bool useModal) const {

    if (tex == nullptr) {
        return false;
    }

    static std::array<bool, 4> state = { true, true, true, true };
    static GFXDevice& gfxDevice = _context.gfx();

    const ImDrawCallback toggleColours { []([[maybe_unused]] const ImDrawList* parent_list, const ImDrawCmd* cmd) -> void {
        const TextureCallbackData data = *static_cast<TextureCallbackData*>(cmd->UserCallbackData);

        GFX::ScopedCommandBuffer sBuffer = GFX::AllocateScopedCommandBuffer();
        GFX::CommandBuffer& buffer = sBuffer();

        PushConstants pushConstants = {};
        pushConstants.set(_ID("toggleChannel"), GFX::PushConstantType::IVEC4, data._colourData);
        pushConstants.set(_ID("depthTexture"), GFX::PushConstantType::INT, data._isDepthTexture ? 1 : 0);
        pushConstants.set(_ID("depthRange"), GFX::PushConstantType::VEC2, data._depthRange);
        pushConstants.set(_ID("flip"), GFX::PushConstantType::INT, data._flip ? 1 : 0);

        GFX::SendPushConstantsCommand pushConstantsCommand = {};
        pushConstantsCommand._constants = pushConstants;
        EnqueueCommand(buffer, pushConstantsCommand);
        gfxDevice.flushCommandBuffer(buffer);
    } };

    bool closed = false;
    bool opened = false;
    if (useModal) {
        ImGui::OpenPopup(modalName);
        opened = ImGui::BeginPopupModal(modalName, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    } else {
        opened = ImGui::Begin(modalName, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    }

    if (opened) {
        assert(tex != nullptr);
        assert(modalName != nullptr);

        static TextureCallbackData defaultData = {};
        defaultData._isDepthTexture = false;
        defaultData._flip = false;

        TextureCallbackData& data = g_modalTextureData[tex->getGUID()];
        data._isDepthTexture = tex->descriptor().baseFormat() == GFXImageFormat::DEPTH_COMPONENT;

        const U8 numChannels = NumChannels(tex->descriptor().baseFormat());

        assert(numChannels > 0);

        if (data._isDepthTexture) {
            ImGui::Text("Depth: ");  ImGui::SameLine(); ImGui::ToggleButton("Depth", &state[0]);
            ImGui::SameLine();
            ImGui::Text("Range: "); ImGui::SameLine();
            ImGui::DragFloatRange2("", &data._depthRange[0], &data._depthRange[1], 0.005f, 0.f, 1.f);
        } else {
            ImGui::Text("R: ");  ImGui::SameLine(); ImGui::ToggleButton("R", &state[0]);

            if (numChannels > 1) {
                ImGui::SameLine();
                ImGui::Text("G: ");  ImGui::SameLine(); ImGui::ToggleButton("G", &state[1]);

                if (numChannels > 2) {
                    ImGui::SameLine();
                    ImGui::Text("B: ");  ImGui::SameLine(); ImGui::ToggleButton("B", &state[2]);
                }

                if (numChannels > 3) {
                    ImGui::SameLine();
                    ImGui::Text("A: ");  ImGui::SameLine(); ImGui::ToggleButton("A", &state[3]);
                }
            }
        }
        ImGui::SameLine();
        ImGui::Text("Flip: ");  ImGui::SameLine(); ImGui::ToggleButton("Flip", &data._flip);
        const bool nonDefaultColours = data._isDepthTexture || !state[0] || !state[1] || !state[2] || !state[3] || data._flip;
        data._colourData.set(state[0] ? 1 : 0, state[1] ? 1 : 0, state[2] ? 1 : 0, state[3] ? 1 : 0);

        if (nonDefaultColours) {
            ImGui::GetWindowDrawList()->AddCallback(toggleColours, &data);
        }

        F32 aspect = 1.0f;
        if (preserveAspect) {
            const U16 w = tex->width();
            const U16 h = tex->height();
            aspect = w / to_F32(h);
        }

        static F32 zoom = 1.0f;
        static ImVec2 zoomCenter(0.5f, 0.5f);
        ImGui::ImageZoomAndPan((ImTextureID)(intptr_t)tex->data()._textureHandle, ImVec2(dimensions.width, dimensions.height / aspect), aspect, zoom, zoomCenter, 2, 3);

        if (nonDefaultColours) {
            // Reset draw data
            ImGui::GetWindowDrawList()->AddCallback(toggleColours, &defaultData);
        }

        ImGui::Text("Mouse: Wheel = scroll | CTRL + Wheel = zoom | Hold Wheel Button = pan");

        if (ImGui::Button("Close")) {
            zoom = 1.0f;
            zoomCenter = ImVec2(0.5f, 0.5f);
            if (useModal) {
                ImGui::CloseCurrentPopup();
            }
            g_modalTextureData.erase(tex->getGUID());
            closed = true;
        }

        if (useModal) {
            ImGui::EndPopup();
        } else {
            ImGui::End();
        }
    } else if (!useModal) {
        ImGui::End();
    }

    return closed;
}

bool Editor::modalModelSpawn(const char* modalName, const Mesh_ptr& mesh) const {
    if (mesh == nullptr) {
        return false;
    }

    static vec3<F32> scale(1.0f);
    static char inputBuf[256] = {};

    bool closed = false;

    ImGui::OpenPopup(modalName);
    if (ImGui::BeginPopupModal(modalName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        assert(mesh != nullptr);
        if (Util::IsEmptyOrNull(inputBuf)) {
            strcpy_s(&inputBuf[0], std::min(to_size(254), mesh->resourceName().length()) + 1, mesh->resourceName().c_str());
        }
        ImGui::Text("Spawn [ %s ]?", mesh->resourceName().c_str());
        ImGui::Separator();


        if (ImGui::InputFloat3("Scale", scale._v)) {
        }

        if (ImGui::InputText("Name", inputBuf, IM_ARRAYSIZE(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        }

        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            closed = true;
            scale.set(1.0f);
            inputBuf[0] = '\0';
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            closed = true;
            if (!spawnGeometry(mesh, scale, inputBuf)) {
                
            }
            scale.set(1.0f);
            inputBuf[0] = '\0';
        }
        ImGui::EndPopup();
    }

    return closed;
}

void Editor::showStatusMessage(const string& message, const F32 durationMS, bool error) const {
    _statusBar->showMessage(message, durationMS, error);
}

bool Editor::spawnGeometry(const Mesh_ptr& mesh, const vec3<F32>& scale, const string& name) const {
    constexpr U32 normalMask = to_base(ComponentType::TRANSFORM) |
                               to_base(ComponentType::BOUNDS) |
                               to_base(ComponentType::NETWORKING) |
                               to_base(ComponentType::RENDERING);

    SceneGraphNodeDescriptor nodeDescriptor = {};
    nodeDescriptor._name = name;
    nodeDescriptor._componentMask = normalMask;
    nodeDescriptor._node = mesh;

    const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();
    const SceneGraphNode* node = activeScene.sceneGraph()->getRoot()->addChildNode(nodeDescriptor);
    if (node != nullptr) {
        const Camera* playerCam = Attorney::SceneManagerCameraAccessor::playerCamera(_context.kernel().sceneManager());

        TransformComponent* tComp = node->get<TransformComponent>();
        tComp->setPosition(playerCam->getEye());
        tComp->rotate(RotationFromVToU(tComp->getFwdVector(), playerCam->getForwardDir()));
        tComp->setScale(scale);

        return true;
    }

    return false;
}

ECSManager& Editor::getECSManager() const {
    const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();
    return activeScene.sceneGraph()->GetECSManager();
}

LightPool& Editor::getActiveLightPool() const {
    const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();
    return *activeScene.lightPool();
}

SceneEnvironmentProbePool* Editor::getActiveEnvProbePool() const noexcept {
    return Attorney::SceneManagerEditor::getEnvProbes(_context.kernel().sceneManager());
}

void Editor::teleportToNode(const SceneGraphNode* sgn) const {
    Attorney::SceneManagerCameraAccessor::moveCameraToNode(_context.kernel().sceneManager(), sgn);
}

void Editor::saveNode(const SceneGraphNode* sgn) const {
    if (Attorney::SceneManagerEditor::saveNode(_context.kernel().sceneManager(), sgn)) {
        bool savedParent = false, savedScene = false;
        // Save the parent as well (if it isn't the root node) as this node may be one that's been newly added
        if (sgn->parent() != nullptr && sgn->parent()->parent() != nullptr) {
            savedParent = Attorney::SceneManagerEditor::saveNode(_context.kernel().sceneManager(), sgn->parent());
        }
        if (unsavedSceneChanges()) {
            savedScene = saveSceneChanges({},{});
        }

        showStatusMessage(Util::StringFormat("Saved node [ %s ] to file! (Saved parent: %s) (Saved scene: %s)", 
                                             sgn->name().c_str(), 
                                             savedParent ? "Yes" : "No", 
                                             savedScene ? "Yes" : "No"), 
                          Time::SecondsToMilliseconds<F32>(3), false);
    }
}

void Editor::loadNode(SceneGraphNode* sgn) const {
    if (Attorney::SceneManagerEditor::loadNode(_context.kernel().sceneManager(), sgn)) {
        showStatusMessage(Util::StringFormat("Reloaded node [ %s ] from file!", sgn->name().c_str()), Time::SecondsToMilliseconds<F32>(3), false);
    }
}

void Editor::queueRemoveNode(const I64 nodeGUID) {
    const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();
    activeScene.sceneGraph()->removeNode(nodeGUID);
    unsavedSceneChanges(true);
}

bool Editor::addComponent(SceneGraphNode* selection, const ComponentType newComponentType) const {
    if (selection != nullptr && newComponentType != ComponentType::COUNT) {
        selection->AddComponents(to_U32(newComponentType), true);
        return BitCompare(selection->componentMask(), to_U32(newComponentType));
    }

    return false;
}

bool Editor::addComponent(const Selections& selections, const ComponentType newComponentType) const {
    bool ret = false;
    if (selections._selectionCount > 0) {
        const Scene& activeScene = context().kernel().sceneManager()->getActiveScene();

        for (U8 i = 0; i < selections._selectionCount; ++i) {
            SceneGraphNode* sgn = activeScene.sceneGraph()->findNode(selections._selections[i]);
            ret = addComponent(sgn, newComponentType) || ret;
        }
    }

    return ret;
}

bool Editor::removeComponent(SceneGraphNode* selection, const ComponentType newComponentType) const {
    if (selection != nullptr && newComponentType != ComponentType::COUNT) {
        selection->RemoveComponents(to_U32(newComponentType));
        return !BitCompare(selection->componentMask(), to_U32(newComponentType));
    }

    return false;
}

bool Editor::removeComponent(const Selections& selections, const ComponentType newComponentType) const {
    bool ret = false;
    if (selections._selectionCount > 0) {
        const Scene& activeScene = context().kernel().sceneManager()->getActiveScene();

        for (U8 i = 0; i < selections._selectionCount; ++i) {
            SceneGraphNode* sgn = activeScene.sceneGraph()->findNode(selections._selections[i]);
            ret = removeComponent(sgn, newComponentType) || ret;
        }
    }

    return ret;
}

SceneNode_ptr Editor::createNode(const SceneNodeType type, const ResourceDescriptor& descriptor) {
    return Attorney::SceneManagerEditor::createNode(context().kernel().sceneManager(), type, descriptor);
}

bool Editor::saveToXML() const {
    boost::property_tree::ptree pt;
    const ResourcePath editorPath = Paths::g_xmlDataLocation + Paths::Editor::g_saveLocation;

    pt.put("editor.showMemEditor", _showMemoryEditor);
    pt.put("editor.showSampleWindow", _showSampleWindow);
    pt.put("editor.autoFocusEditor", _autoFocusEditor);
    pt.put("editor.showEmissiveSelections", _showEmissiveSelections);
    pt.put("editor.themeIndex", to_I32(_currentTheme));
    pt.put("editor.textEditor", _externalTextEditorPath);
    pt.put("editor.lastOpenSceneGUID", _lastOpenSceneGUID);
    _editorCamera->saveToXML(pt, "editor");
    for (size_t i = 0u; i < _recentSceneList.size(); ++i) {
        pt.put("editor.recentScene.entry.<xmlattr>.name", _recentSceneList.get(i).c_str());
    }
    if (createDirectory(editorPath.c_str())) {
        if (copyFile(editorPath.c_str(), g_editorSaveFile, editorPath.c_str(), g_editorSaveFileBak, true) == FileError::NONE) {
            XML::writeXML((editorPath + g_editorSaveFile).str(), pt);
            return true;
        }
    }

    return false;
}

bool Editor::loadFromXML() {
    static boost::property_tree::ptree g_emptyPtree;

    boost::property_tree::ptree pt;
    const ResourcePath editorPath = Paths::g_xmlDataLocation + Paths::Editor::g_saveLocation;
    if (!fileExists((editorPath + g_editorSaveFile).c_str())) {
        if (fileExists((editorPath + g_editorSaveFileBak).c_str())) {
            if (copyFile(editorPath.c_str(), g_editorSaveFileBak, editorPath.c_str(), g_editorSaveFile, true) != FileError::NONE) {
                return false;
            }
        }
    }

    if (fileExists((editorPath + g_editorSaveFile).c_str())) {
        const Scene& activeScene = _context.kernel().sceneManager()->getActiveScene();

        XML::readXML((editorPath + g_editorSaveFile).str(), pt);
        _showMemoryEditor = pt.get("editor.showMemEditor", false);
        _showSampleWindow = pt.get("editor.showSampleWindow", false);
        _autoFocusEditor = pt.get("editor.autoFocusEditor", true);
        _showEmissiveSelections = pt.get("editor.showEmissiveSelections", true);
        _currentTheme = static_cast<ImGuiStyleEnum>(pt.get("themeIndex", to_I32(_currentTheme)));
        ImGui::ResetStyle(_currentTheme);
        _externalTextEditorPath = pt.get<string>("editor.textEditor", "");
        for (const auto& [tag, data] : pt.get_child("editor.recentScene", g_emptyPtree))
        {
            if (tag == "<xmlcomment>") {
                continue;
            }
            const boost::property_tree::ptree& attributes = data.get_child("<xmlattr>", g_emptyPtree);
            const std::string name = attributes.get<std::string>("name", "");
            if (!name.empty()) {
                _recentSceneList.put(name);
            }
        }
        const I64 lastSceneGUID = pt.get("editor.lastOpenSceneGUID",-1);
        if (lastSceneGUID == activeScene.getGUID()) {
            _editorCamera->loadFromXML(pt, "editor");
        }
        return true;
    }

    return false;
}

void PushReadOnly() {
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
}

void PopReadOnly() {
    ImGui::PopItemFlag();
    ImGui::PopStyleVar();
}
} //namespace Divide
