

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
            Console::errorfn(LOCALE_STR("SDL_ERROR"), SDL_GetError());
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

SDL_DisplayMode WindowManager::s_mainDisplayMode;
std::array<SDL_Cursor*, to_base(CursorStyle::COUNT)> WindowManager::s_cursors = create_array<to_base(CursorStyle::COUNT), SDL_Cursor*>(nullptr);

WindowManager::WindowManager() noexcept
{
}

WindowManager::~WindowManager()
{
    DIVIDE_ASSERT( _windows.empty(), "WindowManager::~WindowManager(): close() was not called before destruction!" );
}

vec2<U16> WindowManager::GetFullscreenResolution() noexcept {
    return vec2<U16>( s_mainDisplayMode.w, s_mainDisplayMode.h);
}

ErrorCode WindowManager::init(PlatformContext& context,
                              const RenderAPI renderingAPI,
                              const vec2<I16> initialPosition,
                              const vec2<U16> initialSize,
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

    efficient_clear( _monitors );
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

    SDL_GetCurrentDisplayMode(displayIndex, &s_mainDisplayMode );

    WindowDescriptor descriptor = {};
    descriptor.position = initialPosition;
    descriptor.dimensions = initialSize;
    descriptor.targetDisplay = to_U32(displayIndex);
    descriptor.title = _context->config().runtime.title;
    descriptor.externalClose = false;
    descriptor.targetAPI = renderingAPI;

    if (_context->config().runtime.enableVSync)
    {
        descriptor.flags |= to_base(WindowDescriptor::Flags::VSYNC);
    }

    descriptor.flags |= to_base(WindowDescriptor::Flags::HIDDEN);

    if (windowMode == WindowMode::FULLSCREEN)
    {
        descriptor.flags |= to_base(WindowDescriptor::Flags::FULLSCREEN);
    }
    else if(windowMode == WindowMode::BORDERLESS_WINDOWED)
    {
        descriptor.flags |= to_base(WindowDescriptor::Flags::FULLSCREEN_DESKTOP);
    }

    if (windowMode != WindowMode::WINDOWED || !_context->config().runtime.windowResizable)
    {
        descriptor.flags &= ~to_base(WindowDescriptor::Flags::RESIZEABLE);
    }

    ErrorCode err = ErrorCode::NO_ERR;
    DisplayWindow* window = createWindow(descriptor, err);

    if (err == ErrorCode::NO_ERR) {
        _mainWindowGUID = window->getGUID();

        Application& app = _context->app();

        window->addEventListener(WindowEvent::MINIMIZED, [&app]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept {
            app.mainLoopPaused(true);
            return true;
        });
        window->addEventListener(WindowEvent::MAXIMIZED, [&app]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept {
            app.mainLoopPaused(false);
            return true;
        });
        window->addEventListener(WindowEvent::RESTORED, [&app]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept {
            app.mainLoopPaused(false);
            return true;
        });

        // Query available display modes (resolution, bit depth per channel and refresh rates)
        I32 numberOfDisplayModes[DisplayManager::g_maxDisplayOutputs] = {};

        const U8 numDisplays = to_U8(std::min(SDL_GetNumVideoDisplays(), to_I32( DisplayManager::g_maxDisplayOutputs )));
        Attorney::DisplayManagerWindowManager::SetActiveDisplayCount(numDisplays);

        for (I32 display = 0; display < numDisplays; ++display)
        {
            numberOfDisplayModes[display] = SDL_GetNumDisplayModes(display);
        }

        DisplayManager::OutputDisplayProperties tempDisplayMode = {};
        for (U8 display = 0u; display < numDisplays; ++display) {
            // Register the display modes with the GFXDevice object
            for (I32 mode = 0; mode < numberOfDisplayModes[display]; ++mode) {
                SDL_GetDisplayMode(display, mode, &s_mainDisplayMode );
                tempDisplayMode._maxRefreshRate = to_U8( s_mainDisplayMode.refresh_rate);
                tempDisplayMode._resolution.set( s_mainDisplayMode.w, s_mainDisplayMode.h);
                tempDisplayMode._bitsPerPixel = SDL_BITSPERPIXEL( s_mainDisplayMode.format);
                tempDisplayMode._formatName = SDL_GetPixelFormatName( s_mainDisplayMode.format);
                Util::ReplaceStringInPlace(tempDisplayMode._formatName, "SDL_PIXELFORMAT_", "");
                Attorney::DisplayManagerWindowManager::RegisterDisplayMode(to_U8(display), tempDisplayMode);
            }
        }
    }

    for (U8 i = 0; i < to_U8(CursorStyle::COUNT); ++i) {
        s_cursors[i] = SDL_CreateSystemCursor(CursorToSDL(static_cast<CursorStyle>(i)));
    }

    return err;
}

void WindowManager::close() {
    for (DisplayWindow* window : _windows) {
        window->destroyWindow();
    }
    MemoryManager::DELETE_CONTAINER(_windows);

    for (SDL_Cursor* it : s_cursors) {
        SDL_FreeCursor(it);
    }
    s_cursors.fill(nullptr);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

DisplayWindow* WindowManager::createWindow(const WindowDescriptor& descriptor, ErrorCode& err )
{

    if ( descriptor.targetAPI == RenderAPI::COUNT )
    {
        err = ErrorCode::GFX_NON_SPECIFIED;
        return nullptr;
    }

    DisplayWindow* window = MemoryManager_NEW DisplayWindow(*this, *_context);
    DIVIDE_ASSERT(window != nullptr);

    if (err != ErrorCode::NO_ERR) {
        MemoryManager::SAFE_DELETE(window);
        return nullptr;
    }

    if (_mainWindow == nullptr) {
        _mainWindow = window;
    }

    U32 windowFlags = descriptor.targetAPI == RenderAPI::Vulkan ? SDL_WINDOW_VULKAN : descriptor.targetAPI == RenderAPI::OpenGL ? SDL_WINDOW_OPENGL : 0u;

    if (descriptor.flags & to_base(WindowDescriptor::Flags::RESIZEABLE))
    {
        windowFlags |= SDL_WINDOW_RESIZABLE;
    }
    if (descriptor.flags & to_base(WindowDescriptor::Flags::ALLOW_HIGH_DPI))
    {
        windowFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
    if (descriptor.flags & to_base(WindowDescriptor::Flags::HIDDEN))
    {
        windowFlags |= SDL_WINDOW_HIDDEN;
    }
    if (!(descriptor.flags & to_base(WindowDescriptor::Flags::DECORATED)))
    {
        windowFlags |= SDL_WINDOW_BORDERLESS;
    }
    if (descriptor.flags & to_base(WindowDescriptor::Flags::ALWAYS_ON_TOP))
    {
        windowFlags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }

    WindowType winType = WindowType::WINDOW;
    if (descriptor.flags & to_base(WindowDescriptor::Flags::FULLSCREEN))
    {
        winType = WindowType::FULLSCREEN;
        windowFlags |= SDL_WINDOW_FULLSCREEN;
    }
    else if (descriptor.flags & to_base(WindowDescriptor::Flags::FULLSCREEN_DESKTOP))
    {
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
        err = applyAPISettings(descriptor.targetAPI, window);
    }

    if (err != ErrorCode::NO_ERR) {
        MemoryManager::SAFE_DELETE(window);
        return nullptr;
    }
 
    _windows.emplace_back(window);
    window->addEventListener(WindowEvent::SIZE_CHANGED, [&](const DisplayWindow::WindowEventArgs& args) {
        SizeChangeParams params{};
        params.width = to_U16(args.x);
        params.height = to_U16(args.y);
        params.isFullScreen = args._flag;
        params.winGUID = args._windowGUID;
        params.isMainWindow = args._windowGUID == _mainWindow->getGUID();
        // Only if rendering window
        return _context->app().onWindowSizeChange(params);
    });

    if (!descriptor.externalClose) {
        window->addEventListener(WindowEvent::CLOSE_REQUESTED, [this](const DisplayWindow::WindowEventArgs& args) {
            Console::d_printfn(LOCALE_STR("WINDOW_CLOSE_EVENT"), args._windowGUID);

            if (_mainWindowGUID == args._windowGUID) {
                _context->app().RequestShutdown(false);
            } else {
                for (DisplayWindow*& win : _windows) {
                    if (win->getGUID() == args._windowGUID) {
                        if (!destroyWindow(win)) {
                            Console::errorfn(LOCALE_STR("WINDOW_CLOSE_EVENT_ERROR"), args._windowGUID);
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
    if (window->destroyWindow() == ErrorCode::NO_ERR) {
        erase_if(_windows, [targetGUID = window->getGUID()](DisplayWindow* win) noexcept { return win->getGUID() == targetGUID;});
        MemoryManager::SAFE_DELETE(window);
        return true;
    }
    return false;
}

void WindowManager::DestroyAPISettings(DisplayWindow* window) noexcept
{
    if (!window || !(SDL_GetWindowFlags(window->getRawWindow()) & to_U32(SDL_WINDOW_OPENGL)))
    {
        return;
    }

    if (window->_flags & to_base(WindowFlags::OWNS_RENDER_CONTEXT))
    {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(window->_userData));
        window->_userData = nullptr;
    }
}

ErrorCode WindowManager::configureAPISettings(const RenderAPI api, const U16 descriptorFlags) const
{
    if (api == RenderAPI::Vulkan || api == RenderAPI::None)
    {
        NOP();
    }
    else if (api == RenderAPI::OpenGL)
    {
        Uint32 OpenGLFlags = SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG | SDL_GL_CONTEXT_RESET_ISOLATION_FLAG;

        bool useDebugContext = false;
        if constexpr(Config::ENABLE_GPU_VALIDATION)
        {
            // OpenGL error handling is available in any build configuration if the proper defines are in place.
            OpenGLFlags |= SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG;
            if (_context->config().debug.renderer.enableRenderAPIDebugging || _context->config().debug.renderer.enableRenderAPIBestPractices)
            {
                useDebugContext = true;
                OpenGLFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
            }
        }
        if (!useDebugContext)
        {
            ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_NO_ERROR, 1));
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


        Validate(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4));
        if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) != 0)
        {
            ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5));
        }
        if (descriptorFlags & to_base(WindowDescriptor::Flags::SHARE_CONTEXT))
        {
            Validate(SDL_GL_MakeCurrent(mainWindow()->getRawWindow(), mainWindow()->userData()->_glContext));
        }
        else
        {
            ValidateAssert(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1));
            ValidateAssert(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4));
            ValidateAssert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1));
        }
    }
    else
    {
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    return ErrorCode::NO_ERR;
}

ErrorCode WindowManager::applyAPISettings(const RenderAPI api, DisplayWindow* window)
{
    // Create a context and make it current
    if (window->_flags & to_base(WindowFlags::OWNS_RENDER_CONTEXT))
    {
        if (api == RenderAPI::OpenGL) {
            _globalUserData._glContext = SDL_GL_CreateContext(window->getRawWindow());
        }
        window->_userData = &_globalUserData;
    }
    else
    {
        window->_userData = mainWindow()->userData();
    }

    if (window->_userData == nullptr) {
        Console::errorfn(LOCALE_STR("ERROR_GFX_DEVICE"), SDL_GetError());
        Console::warnfn(LOCALE_STR("WARN_SWITCH_API"));
        Console::warnfn(LOCALE_STR("WARN_APPLICATION_CLOSE"));
        return ErrorCode::GL_OLD_HARDWARE;
    }

    if (api == RenderAPI::OpenGL) {
        Validate(SDL_GL_MakeCurrent(window->getRawWindow(), window->userData()->_glContext));

        if (window->_flags & to_base(WindowFlags::VSYNC))
        {
            // Vsync is toggled on or off via the external config file
            bool vsyncSet = false;
            // Late swap may fail
            if (_context->config().runtime.adaptiveSync) {
                vsyncSet = SDL_GL_SetSwapInterval(-1) != -1;
                if (!vsyncSet) {
                    Console::warnfn(LOCALE_STR("WARN_ADAPTIVE_SYNC_NOT_SUPPORTED"));
                }
            }

            if (!vsyncSet) {
                vsyncSet = SDL_GL_SetSwapInterval(1) != -1;
            }
            DIVIDE_ASSERT(vsyncSet, "VSync change failed!");
        } else {
            SDL_GL_SetSwapInterval(0);
        }

        // Switch back to the main render context (may be the same window ...)
        Validate(SDL_GL_MakeCurrent(mainWindow()->getRawWindow(), mainWindow()->userData()->_glContext));
    }
    return ErrorCode::NO_ERR;
}

void WindowManager::CaptureMouse(const bool state) noexcept {
    SDL_CaptureMouse(state ? SDL_TRUE : SDL_FALSE);
}

bool WindowManager::setCursorPosition(I32 x, I32 y) noexcept {
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
    static CursorStyle s_CurrentStyle = CursorStyle::NONE;
    if (style != s_CurrentStyle )
    {
        s_CurrentStyle = style;
        SDL_SetCursor( s_cursors[to_base( style )] );
    }
}

void WindowManager::ToggleRelativeMouseMode(const bool state) noexcept {
    [[maybe_unused]] const I32 result = SDL_SetRelativeMouseMode(state ? SDL_TRUE : SDL_FALSE);
    assert(result != -1);
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