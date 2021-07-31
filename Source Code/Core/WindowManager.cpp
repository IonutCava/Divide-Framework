#include "stdafx.h"

#include "Headers/WindowManager.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

namespace Divide {
 

namespace {
    
    SDL_SystemCursor CursorToSDL(const CursorStyle style) noexcept {
        switch (style) {
            case CursorStyle::ARROW: return SDL_SYSTEM_CURSOR_ARROW;
            case CursorStyle::HAND: return SDL_SYSTEM_CURSOR_HAND;
            case CursorStyle::NONE: return SDL_SYSTEM_CURSOR_NO;
            case CursorStyle::RESIZE_ALL: return SDL_SYSTEM_CURSOR_SIZEALL;
            case CursorStyle::RESIZE_EW: return SDL_SYSTEM_CURSOR_SIZEWE;
            case CursorStyle::RESIZE_NS: return SDL_SYSTEM_CURSOR_SIZENS;
            case CursorStyle::RESIZE_NESW: return SDL_SYSTEM_CURSOR_SIZENESW;
            case CursorStyle::RESIZE_NWSE: return SDL_SYSTEM_CURSOR_SIZENWSE;
            case CursorStyle::TEXT_INPUT: return SDL_SYSTEM_CURSOR_IBEAM;
            case CursorStyle::COUNT: break;
        }

        return SDL_SYSTEM_CURSOR_NO;
    }

    bool Validate(const I32 errCode) {
        if (errCode != 0) {
            Console::errorfn(Locale::Get(_ID("SDL_ERROR")), SDL_GetError());
            return false;
        }

        return true;
    }

    bool ValidateAssert(const I32 errCode) {
        if (!Validate(errCode)) {
            assert(errCode == 0);
            return false;
        }

        return true;
    }

} // namespace 

hashMap<CursorStyle, SDL_Cursor*> WindowManager::s_cursors;

WindowManager::WindowManager()  noexcept
{
    SDL_Init(SDL_INIT_VIDEO);
}

WindowManager::~WindowManager()
{
    close();
}

vec2<U16> WindowManager::GetFullscreenResolution() noexcept {
    const SysInfo& systemInfo = sysInfo();
    return vec2<U16>(systemInfo._systemResolutionWidth,
                     systemInfo._systemResolutionHeight);
}

ErrorCode WindowManager::init(PlatformContext& context,
                              const RenderAPI renderingAPI,
                              const vec2<I16>& initialPosition,
                              const vec2<U16>& initialSize,
                              const WindowMode windowMode,
                              const I32 targetDisplayIndex)
{
    if (!_monitors.empty()) {
        // Double init
        return ErrorCode::WINDOW_INIT_ERROR;
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        return ErrorCode::WINDOW_INIT_ERROR;
    }
    
    _context = &context;

    _monitors.resize(0);
    const I32 displayCount = SDL_GetNumVideoDisplays();
    for (I32 i = 0; i < displayCount; ++i) {
        MonitorData data = {};

        SDL_Rect r;
        SDL_GetDisplayBounds(i, &r);
        data.viewport.xy = { to_I16(r.x), to_I16(r.y) };
        data.viewport.zw = { to_I16(r.w), to_I16(r.h) };

        SDL_GetDisplayUsableBounds(i, &r);
        data.drawableArea.xy = { to_I16(r.x), to_I16(r.y) };
        data.drawableArea.zw = { to_I16(r.w), to_I16(r.h) };

        SDL_GetDisplayDPI(i, &data.dpi, nullptr, nullptr);

        _monitors.push_back(data);
    }

    const I32 displayIndex = std::max(std::min(targetDisplayIndex, displayCount - 1), 0);

    SysInfo& systemInfo = sysInfo();
    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(displayIndex, &displayMode);
    systemInfo._systemResolutionWidth = displayMode.w;
    systemInfo._systemResolutionHeight = displayMode.h;

    _apiFlags = CreateAPIFlags(renderingAPI);


    WindowDescriptor descriptor = {};
    descriptor.position = initialPosition;
    descriptor.dimensions = initialSize;
    descriptor.targetDisplay = to_U32(displayIndex);
    descriptor.title = _context->config().runtime.title;
    descriptor.externalClose = false;
    descriptor.targetAPI = renderingAPI;

    if (_context->config().runtime.enableVSync) {
        SetBit(descriptor.flags, WindowDescriptor::Flags::VSYNC);
    }

    SetBit(descriptor.flags, WindowDescriptor::Flags::HIDDEN);

    if (windowMode == WindowMode::FULLSCREEN) {
        SetBit(descriptor.flags, WindowDescriptor::Flags::FULLSCREEN);
    } else if (windowMode == WindowMode::BORDERLESS_WINDOWED) {
        SetBit(descriptor.flags, WindowDescriptor::Flags::FULLSCREEN_DESKTOP);
    }

    if (windowMode != WindowMode::WINDOWED || !_context->config().runtime.windowResizable) {
        ClearBit(descriptor.flags, WindowDescriptor::Flags::RESIZEABLE);
    }

    // We don't need to clear colour and depth for the main window as we always render a single textured quad. Rendering is done offscreen.
    ClearBit(descriptor.flags, WindowDescriptor::Flags::CLEAR_COLOUR);
    ClearBit(descriptor.flags, WindowDescriptor::Flags::CLEAR_DEPTH);

    ErrorCode err = ErrorCode::NO_ERR;
    DisplayWindow* window = createWindow(descriptor, err);

    if (err == ErrorCode::NO_ERR) {
        _mainWindowGUID = window->getGUID();

        window->addEventListener(WindowEvent::MINIMIZED, [this](const DisplayWindow::WindowEventArgs& args) noexcept {
            ACKNOWLEDGE_UNUSED(args);
            _context->app().mainLoopPaused(true);
            return true;
        });
        window->addEventListener(WindowEvent::MAXIMIZED, [this](const DisplayWindow::WindowEventArgs& args) noexcept {
            ACKNOWLEDGE_UNUSED(args);
            _context->app().mainLoopPaused(false);
            return true;
        });
        window->addEventListener(WindowEvent::RESTORED, [this](const DisplayWindow::WindowEventArgs& args) noexcept {
            ACKNOWLEDGE_UNUSED(args);
            _context->app().mainLoopPaused(false);
            return true;
        });

        GPUState& gState = _context->gfx().gpuState();
        // Query available display modes (resolution, bit depth per channel and refresh rates)
        I32 numberOfDisplayModes[GPUState::maxDisplayCount()] = {};
        const I32 numDisplays = std::min(SDL_GetNumVideoDisplays(), to_I32(GPUState::maxDisplayCount()));

        for (I32 display = 0; display < numDisplays; ++display) {
            numberOfDisplayModes[display] = SDL_GetNumDisplayModes(display);
        }

        GPUState::GPUVideoMode tempDisplayMode = {};
        for (I32 display = 0; display < numDisplays; ++display) {
            gState.setDisplayModeCount(to_U8(display), numberOfDisplayModes[display]);

            // Register the display modes with the GFXDevice object
            for (I32 mode = 0; mode < numberOfDisplayModes[display]; ++mode) {
                SDL_GetDisplayMode(display, mode, &displayMode);
                // Register the display modes with the GFXDevice object
                tempDisplayMode._resolution.set(displayMode.w, displayMode.h);
                tempDisplayMode._bitDepth = SDL_BITSPERPIXEL(displayMode.format);
                tempDisplayMode._refreshRate = to_U8(displayMode.refresh_rate);
                tempDisplayMode._formatName = SDL_GetPixelFormatName(displayMode.format);
                Util::ReplaceStringInPlace(tempDisplayMode._formatName, "SDL_PIXELFORMAT_", "");
                gState.registerDisplayMode(to_U8(display), tempDisplayMode);
            }
        }
    }

    for (U8 i = 0; i < to_U8(CursorStyle::COUNT); ++i) {
        const auto style = static_cast<CursorStyle>(i);
        s_cursors[style] = SDL_CreateSystemCursor(CursorToSDL(style));
    }

    return err;
}

void WindowManager::postInit() {
    for (DisplayWindow* window : _windows) {
        window->refreshDrawableSize();
    }
}

void WindowManager::close() {
    for (DisplayWindow* window : _windows) {
        window->destroyWindow();
    }
    MemoryManager::DELETE_CONTAINER(_windows);

    for (const auto& it : s_cursors) {
        SDL_FreeCursor(it.second);
    }
    s_cursors.clear();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

DisplayWindow* WindowManager::createWindow(const WindowDescriptor& descriptor, ErrorCode& err, U32& windowIndex ) {
    windowIndex = U32_MAX;
    auto* window = MemoryManager_NEW DisplayWindow(*this, *_context);
    assert(window != nullptr);

    if (err != ErrorCode::NO_ERR) {
        MemoryManager::SAFE_DELETE(window);
        return nullptr;
    }

    if (_mainWindow == nullptr) {
        _mainWindow = window;
    }

    U32 windowFlags = _apiFlags;
    if (BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::RESIZEABLE))) {
        windowFlags |= SDL_WINDOW_RESIZABLE;
    }
    if (BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::ALLOW_HIGH_DPI))) {
        windowFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
    if (BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::HIDDEN))) {
        windowFlags |= SDL_WINDOW_HIDDEN;
    }
    if (!BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::DECORATED))) {
        windowFlags |= SDL_WINDOW_BORDERLESS;
    }
    if (BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::ALWAYS_ON_TOP))) {
        windowFlags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }

    WindowType winType = WindowType::WINDOW;
    if (BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::FULLSCREEN))) {
        winType = WindowType::FULLSCREEN;
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    } else if (BitCompare(descriptor.flags, to_base(WindowDescriptor::Flags::FULLSCREEN_DESKTOP))) {
        winType = WindowType::FULLSCREEN_WINDOWED;
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    if (err == ErrorCode::NO_ERR) {
        err = configureAPISettings(descriptor.targetAPI, descriptor.flags);
    }

    if (err != ErrorCode::NO_ERR) {
        MemoryManager::SAFE_DELETE(window);
        return nullptr;
    }

    err = window->init(windowFlags,
                       winType,
                       descriptor);

    if (err == ErrorCode::NO_ERR) {
        window->clearColour(descriptor.clearColour);
        window->clearFlags(BitCompare(descriptor.flags, WindowDescriptor::Flags::CLEAR_COLOUR),
                           BitCompare(descriptor.flags, WindowDescriptor::Flags::CLEAR_DEPTH));

        err = applyAPISettings(window);
    }

    if (err != ErrorCode::NO_ERR) {
        MemoryManager::SAFE_DELETE(window);
        return nullptr;
    }
 
    windowIndex = to_U32(_windows.size());
    _windows.emplace_back(window);
    window->addEventListener(WindowEvent::SIZE_CHANGED, [this](const DisplayWindow::WindowEventArgs& args) {
        SizeChangeParams params;
        params.width = to_U16(args.x);
        params.height = to_U16(args.y);
        params.isWindowResize = true;
        params.isFullScreen = args._flag;
        params.winGUID = args._windowGUID;

        // Only if rendering window
        return _context->app().onSizeChange(params);
    });

    if (!descriptor.externalClose) {
        window->addEventListener(WindowEvent::CLOSE_REQUESTED, [this](const DisplayWindow::WindowEventArgs& args) {
            Console::d_printfn(Locale::Get(_ID("WINDOW_CLOSE_EVENT")), args._windowGUID);

            if (_mainWindowGUID == args._windowGUID) {
                _context->app().RequestShutdown();
            } else {
                for (DisplayWindow*& win : _windows) {
                    if (win->getGUID() == args._windowGUID) {
                        if (!destroyWindow(win)) {
                            Console::errorfn(Locale::Get(_ID("WINDOW_CLOSE_EVENT_ERROR")), args._windowGUID);
                            win->hidden(true);
                        }
                        break;
                    }
                }
                return false;
            }
            return true;
        });
    }

    return window;
}

bool WindowManager::destroyWindow(DisplayWindow*& window) {
    if (window == nullptr) {
        return true;
    }

    SDL_HideWindow(window->getRawWindow());
    I64 targetGUID = window->getGUID();
    if (window->destroyWindow() == ErrorCode::NO_ERR) {
        _windows.erase(
            std::remove_if(std::begin(_windows), std::end(_windows),
                           [&targetGUID](DisplayWindow* win) noexcept
                               -> bool { return win->getGUID() == targetGUID;}),
            std::end(_windows));
        MemoryManager::SAFE_DELETE(window);
        return true;
    }
    return false;
}

void WindowManager::update(const U64 deltaTimeUS) {
    OPTICK_EVENT();

    for (DisplayWindow* win : _windows) {
        win->update(deltaTimeUS);
    }
}

bool WindowManager::onSDLEvent(SDL_Event event) noexcept {
    // Nothing yet? Wow ...
    return false;
}

bool WindowManager::anyWindowFocus() const noexcept {
    return getFocusedWindow() != nullptr;
}

U32 WindowManager::CreateAPIFlags(const RenderAPI api) noexcept {
    if (api == RenderAPI::OpenGL || api == RenderAPI::OpenGLES) {
        return SDL_WINDOW_OPENGL;
    } 

    return 0u;
}

void WindowManager::DestroyAPISettings(DisplayWindow* window) noexcept {
    if (!window || !BitCompare(SDL_GetWindowFlags(window->getRawWindow()), to_U32(SDL_WINDOW_OPENGL))) {
        return;
    }

    if (BitCompare(window->_flags, WindowFlags::OWNS_RENDER_CONTEXT)) {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(window->_userData));
        window->_userData = nullptr;
    }
}

ErrorCode WindowManager::configureAPISettings(const RenderAPI api, const U16 descriptorFlags) const
{
    Uint32 OpenGLFlags = SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG | SDL_GL_CONTEXT_RESET_ISOLATION_FLAG;

    bool useDebugContext = false;
    if_constexpr (Config::ENABLE_GPU_VALIDATION) {
        // OpenGL error handling is available in any build configuration if the proper defines are in place.
        OpenGLFlags |= SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG;
        if (_context->config().debug.enableRenderAPIDebugging) {
            useDebugContext = true;
            OpenGLFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
        }
    }
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, OpenGLFlags));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_RELEASE_BEHAVIOR, SDL_GL_CONTEXT_RELEASE_BEHAVIOR_NONE));

    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1));
    // 32Bit RGBA (R8G8B8A8), 24bit Depth, 8bit Stencil
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24));

    if (!useDebugContext) {
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_NO_ERROR, 1));
    }

    // OpenGL ES is not yet supported, but when added, it will need to mirror
    // OpenGL functionality 1-to-1
    if (api == RenderAPI::OpenGLES) {
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1));
    } else {
        Validate(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4));
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) != 0) {
            ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5));
        }
    }

    if (BitCompare(descriptorFlags, to_base(WindowDescriptor::Flags::SHARE_CONTEXT))) {
        Validate(SDL_GL_MakeCurrent(mainWindow()->getRawWindow(), static_cast<SDL_GLContext>(mainWindow()->userData())));
    } else {
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, _context->config().rendering.MSAASamples));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1));
    }

    return ErrorCode::NO_ERR;
}

ErrorCode WindowManager::applyAPISettings(DisplayWindow* window) const
{
    // Create a context and make it current
    if (BitCompare(window->_flags, WindowFlags::OWNS_RENDER_CONTEXT)) {
        window->_userData = SDL_GL_CreateContext(window->getRawWindow());
    } else {
        window->_userData = mainWindow()->userData();
    }

    if (window->_userData == nullptr) {
        Console::errorfn(Locale::Get(_ID("ERROR_GFX_DEVICE")), SDL_GetError());
        Console::printfn(Locale::Get(_ID("WARN_SWITCH_API")));
        Console::printfn(Locale::Get(_ID("WARN_APPLICATION_CLOSE")));
        return ErrorCode::OGL_OLD_HARDWARE;
    }

    Validate(SDL_GL_MakeCurrent(window->getRawWindow(), static_cast<SDL_GLContext>(window->userData())));

    if (BitCompare(window->_flags, WindowFlags::VSYNC)) {
        // Vsync is toggled on or off via the external config file
        bool vsyncSet = false;
        // Late swap may fail
        if (_context->config().runtime.adaptiveSync) {
            vsyncSet = SDL_GL_SetSwapInterval(-1) != -1;
        }

        if (!vsyncSet) {
            vsyncSet = SDL_GL_SetSwapInterval(1) != -1;
        }
        DIVIDE_ASSERT(vsyncSet, "VSync change failed!");
    } else {
        SDL_GL_SetSwapInterval(0);
    }

    // Switch back to the main render context
    Validate(SDL_GL_MakeCurrent(mainWindow()->getRawWindow(), static_cast<SDL_GLContext>(mainWindow()->userData())));

    return ErrorCode::NO_ERR;
}

void WindowManager::CaptureMouse(const bool state) noexcept {
    SDL_CaptureMouse(state ? SDL_TRUE : SDL_FALSE);
}

bool WindowManager::setCursorPosition(I32 x, I32 y) {
    DisplayWindow* focusedWindow = getFocusedWindow();
    if (focusedWindow == nullptr) {
        focusedWindow = mainWindow();
    }

    if (x == -1) {
        x = SDL_WINDOWPOS_CENTERED_DISPLAY(focusedWindow->currentDisplayIndex());
    }
    if (y == -1) {
        y = SDL_WINDOWPOS_CENTERED_DISPLAY(focusedWindow->currentDisplayIndex());
    }

    SDL_WarpMouseInWindow(focusedWindow->getRawWindow(), x, y);
    return true;
}

bool WindowManager::SetGlobalCursorPosition(I32 x, I32 y) noexcept {
    if (x == -1) {
        x = SDL_WINDOWPOS_CENTERED;
    }
    if (y == -1) {
        y = SDL_WINDOWPOS_CENTERED;
    }

    return SDL_WarpMouseGlobal(x, y) == 0;
}

void WindowManager::SetCursorStyle(const CursorStyle style) {
    SDL_SetCursor(s_cursors[style]);
}

void WindowManager::ToggleRelativeMouseMode(const bool state) noexcept {
    const I32 result = SDL_SetRelativeMouseMode(state ? SDL_TRUE : SDL_FALSE);
    assert(result != -1);
    ACKNOWLEDGE_UNUSED(result);
}

bool WindowManager::IsRelativeMouseMode() noexcept {
    return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

vec2<I32> WindowManager::GetGlobalCursorPosition() noexcept {
    vec2<I32> ret(-1);
    SDL_GetGlobalMouseState(&ret.x, &ret.y);
    return ret;
}

vec2<I32> WindowManager::GetCursorPosition() noexcept {
    vec2<I32> ret(-1);
    SDL_GetMouseState(&ret.x, &ret.y);
    return ret;
}

U32 WindowManager::GetMouseState(vec2<I32>& pos, const bool global) noexcept {
    if (global) {
        return to_U32(SDL_GetGlobalMouseState(&pos.x, &pos.y));
    }
    
    return to_U32(SDL_GetMouseState(&pos.x, &pos.y));
}

void WindowManager::SetCaptureMouse(const bool state) noexcept {
    SDL_CaptureMouse(state ? SDL_TRUE : SDL_FALSE);
}

void WindowManager::snapCursorToCenter() {
    const DisplayWindow* focusedWindow = getFocusedWindow();
    if (focusedWindow == nullptr) {
        focusedWindow = mainWindow();
    }

    const vec2<U16>& center = focusedWindow->getDimensions();
    setCursorPosition(to_I32(center.x * 0.5f), to_I32(center.y * 0.5f));
}

void WindowManager::hideAll() noexcept {
    for (DisplayWindow* win : _windows) {
        win->hidden(true);
    }
}

} //namespace Divide