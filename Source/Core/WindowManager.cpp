

#include "Headers/WindowManager.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

namespace
{
    [[nodiscard]] SDL_SystemCursor CursorToSDL(const CursorStyle style) noexcept
    {
        switch (style)
        {
            case CursorStyle::ARROW:       return SDL_SYSTEM_CURSOR_ARROW;
            case CursorStyle::HAND:        return SDL_SYSTEM_CURSOR_HAND;
            case CursorStyle::NONE:        return SDL_SYSTEM_CURSOR_NO;
            case CursorStyle::RESIZE_ALL:  return SDL_SYSTEM_CURSOR_SIZEALL;
            case CursorStyle::RESIZE_EW:   return SDL_SYSTEM_CURSOR_SIZEWE;
            case CursorStyle::RESIZE_NS:   return SDL_SYSTEM_CURSOR_SIZENS;
            case CursorStyle::RESIZE_NESW: return SDL_SYSTEM_CURSOR_SIZENESW;
            case CursorStyle::RESIZE_NWSE: return SDL_SYSTEM_CURSOR_SIZENWSE;
            case CursorStyle::TEXT_INPUT:  return SDL_SYSTEM_CURSOR_IBEAM;
            default:
            case CursorStyle::COUNT:       break;
        }

        return SDL_SYSTEM_CURSOR_NO;
    }

    bool Validate(const I32 errCode)
    {
        if (errCode != 0)
        {
            Console::errorfn(LOCALE_STR("SDL_ERROR"), SDL_GetError());
            return false;
        }

        return true;
    }

    bool ValidateAssert(const I32 errCode)
    {
        if (!Validate(errCode))
        {
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

vec2<U16> WindowManager::GetFullscreenResolution() noexcept
{
    return vec2<U16>( s_mainDisplayMode.w, s_mainDisplayMode.h);
}

ErrorCode WindowManager::init(PlatformContext& context,
                              const RenderAPI renderingAPI,
                              const vec2<I16> initialPosition,
                              const vec2<U16> initialSize,
                              const WindowMode windowMode,
                              const I32 targetDisplayIndex)
{
    if (!_monitors.empty())
    {
        // Double init
        return ErrorCode::WINDOW_INIT_ERROR;
    }

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
    {
        return ErrorCode::WINDOW_INIT_ERROR;
    }

    _context = &context;

    efficient_clear( _monitors );
    const I32 displayCount = SDL_GetNumVideoDisplays();
    for (I32 i = 0; i < displayCount; ++i)
    {
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

    for ( U8 i = 0; i < to_U8( CursorStyle::COUNT ); ++i )
    {
        s_cursors[i] = SDL_CreateSystemCursor( CursorToSDL( static_cast<CursorStyle>(i) ) );
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

    if (err == ErrorCode::NO_ERR)
    {
        _mainWindowGUID = window->getGUID();

        Application& app = _context->app();

        window->addEventListener(WindowEvent::MINIMIZED,
        {
            ._cbk = [&app]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept
                    {
                        app.mainLoopPaused(true);
                        return true;
                    },
            ._name = "Application::MINIMIZED"
        });

        window->addEventListener(WindowEvent::MAXIMIZED,
        {
            ._cbk = [&app]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept
            {
                app.mainLoopPaused(false);
                return true;
            },
            ._name = "Application::MAXIMIZED"
        });
        window->addEventListener(WindowEvent::RESTORED,
        {
            ._cbk = [&app]([[maybe_unused]] const DisplayWindow::WindowEventArgs& args) noexcept
            {
                app.mainLoopPaused(false);
                return true;
            },
            ._name = "Application::RESTORED"
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
        for (U8 display = 0u; display < numDisplays; ++display)
        {
            // Register the display modes with the GFXDevice object
            for (I32 mode = 0; mode < numberOfDisplayModes[display]; ++mode)
            {
                SDL_GetDisplayMode(display, mode, &s_mainDisplayMode );
                tempDisplayMode._maxRefreshRate = to_U8( s_mainDisplayMode.refresh_rate);
                tempDisplayMode._resolution.set( s_mainDisplayMode.w, s_mainDisplayMode.h);
                tempDisplayMode._bitsPerPixel = SDL_BITSPERPIXEL( s_mainDisplayMode.format);
                tempDisplayMode._formatName = SDL_GetPixelFormatName( s_mainDisplayMode.format);
                Util::ReplaceStringInPlace(tempDisplayMode._formatName, "SDL_PIXELFORMAT_", "");
                Attorney::DisplayManagerWindowManager::RegisterDisplayMode(to_U8(display), tempDisplayMode);
            }
        }

        GFX::InitPools(64u);
    }

    return err;
}

void WindowManager::close()
{
    for ( auto& window : _windows)
    {
        window->destroyWindow();
    }

    _windows.clear();

    for (SDL_Cursor* it : s_cursors)
    {
        SDL_FreeCursor(it);
    }

    s_cursors.fill(nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    GFX::DestroyPools();
}

DisplayWindow* WindowManager::createWindow(const WindowDescriptor& descriptor, ErrorCode& err )
{
    if ( descriptor.targetAPI == RenderAPI::COUNT )
    {
        err = ErrorCode::GFX_NON_SPECIFIED;
        return nullptr;
    }

    std::unique_ptr<DisplayWindow> window = std::make_unique<DisplayWindow>(*this, *_context);
    DIVIDE_ASSERT(window != nullptr);

    if (err != ErrorCode::NO_ERR)
    {
        return nullptr;
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
    if (descriptor.flags & to_base(WindowDescriptor::Flags::NO_TASKBAR_ICON))
    {
        windowFlags |= SDL_WINDOW_SKIP_TASKBAR;
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

    if (err == ErrorCode::NO_ERR)
    {
        err = ConfigureAPISettings( *_context, descriptor );
    }

    if (err != ErrorCode::NO_ERR)
    {
        return nullptr;
    }

    DisplayWindow* crtWindow = activeWindow();

    bool contextChanged = false;
    if ( descriptor.parentWindow != nullptr )
    {
        Validate( SDL_GL_MakeCurrent( descriptor.parentWindow->getRawWindow(), descriptor.parentWindow->userData()._glContext ) );
        contextChanged = true;
    }

    err = window->init(windowFlags, winType, descriptor);

    if ( crtWindow != nullptr && contextChanged )
    {
        Validate( SDL_GL_MakeCurrent( crtWindow->getRawWindow(), nullptr ) );
    }

    if (err != ErrorCode::NO_ERR)
    {
        return nullptr;
    }

    const bool isMainWindow = _mainWindow == nullptr;
    if ( isMainWindow )
    {
        DIVIDE_ASSERT( descriptor.parentWindow == nullptr );
        _mainWindow = window.get();
    }

    err = ApplyAPISettings( *_context, descriptor.targetAPI, window.get(), crtWindow != nullptr ? crtWindow : mainWindow() );

    if ( err != ErrorCode::NO_ERR )
    {
        if ( isMainWindow )
        {
            _mainWindow = nullptr;
        }

        return nullptr;
    }
 
    window->addEventListener(WindowEvent::SIZE_CHANGED,
    {
        ._cbk = [&](const DisplayWindow::WindowEventArgs& args)
        {
            return onWindowSizeChange(args);
        },
        ._name = "WindowManager::SIZE_CHANGED"
    });

    if (!descriptor.externalClose)
    {
        window->addEventListener(WindowEvent::CLOSE_REQUESTED,
        {
            ._cbk = [this](const DisplayWindow::WindowEventArgs& args)
            {
                Console::d_printfn(LOCALE_STR("WINDOW_CLOSE_EVENT"), args._windowGUID);

                if (_mainWindowGUID == args._windowGUID)
                {
                    _context->app().RequestShutdown(false);
                }
                else
                {
                    for ( auto& win : _windows)
                    {
                        if (win->getGUID() == args._windowGUID)
                        {
                            auto tempWindow = win.get();
                            if (!destroyWindow(tempWindow))
                            {
                                Console::errorfn(LOCALE_STR("WINDOW_CLOSE_EVENT_ERROR"), args._windowGUID);
                                win->hidden(true);
                            }
                            break;
                        }
                    }
                    return false;
                }
                return true;
            },
            ._name = "WindowManager::CLOSE_REQUESTED"
        });
    }
    
    return _windows.emplace_back(MOV(window)).get();
}

bool WindowManager::destroyWindow(DisplayWindow*& window)
{
    if (window == nullptr)
    {
        return true;
    }

    SDL_HideWindow(window->getRawWindow());

    if (window->destroyWindow() == ErrorCode::NO_ERR)
    {
        if (window->getGUID() == _mainWindowGUID)
        {
            _mainWindowGUID = -1;
            _mainWindow = nullptr;
        }

        erase_if(_windows, [targetGUID = window->getGUID()](auto& win) noexcept { return win->getGUID() == targetGUID;});
        window = nullptr;
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

    if ( window->userData()._glContext  != nullptr)
    {
        if ( window->userData()._ownsContext)
        {
            SDL_GL_DeleteContext( window->userData()._glContext );
        }

        window->userData()._glContext = nullptr;
    }
}

ErrorCode WindowManager::ConfigureAPISettings( const PlatformContext& context, const WindowDescriptor& descriptor )
{
    const RenderAPI api = descriptor.targetAPI;

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
            if (context.config().debug.renderer.enableRenderAPIDebugging || context.config().debug.renderer.enableRenderAPIBestPractices)
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

        if ( context.config().rendering.MSAASamples > 0u)
        {
            ValidateAssert( SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 ) );
            ValidateAssert( SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, std::min( context.config().rendering.MSAASamples, to_U8(4u)) ) ); // Cap to 4xMSAA as we can't query HW support yet
        }
    }
    else
    {
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    return ErrorCode::NO_ERR;
}

ErrorCode WindowManager::ApplyAPISettings( const PlatformContext& context, const RenderAPI api, DisplayWindow* targetWindow, DisplayWindow* activeWindow )
{
    // Create a context and make it current
    if ( targetWindow->parentWindow() != nullptr)
    {
        targetWindow->userData( targetWindow->parentWindow()->userData() );
        targetWindow->userData()._ownsContext = false;
    }

    if (api == RenderAPI::OpenGL)
    {
        if ( targetWindow->userData()._glContext == nullptr )
        {
            targetWindow->userData()._glContext = SDL_GL_CreateContext( targetWindow->getRawWindow() );
            targetWindow->userData()._ownsContext = true;
            ValidateAssert( SDL_GL_SetAttribute( SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1 ) );
        }

        if ( targetWindow->userData()._glContext == nullptr)
        {
            Console::errorfn(LOCALE_STR("ERROR_SDL_WINDOW"), SDL_GetError());
            Console::warnfn(LOCALE_STR("WARN_SWITCH_API"));
            Console::warnfn(LOCALE_STR("WARN_APPLICATION_CLOSE"));
            return ErrorCode::GL_OLD_HARDWARE;
        }

        if ( targetWindow->flags() & to_base(WindowFlags::VSYNC))
        {
            // Vsync is toggled on or off via the external config file
            bool vsyncSet = false;
            // Late swap may fail
            if (context.config().runtime.adaptiveSync)
            {
                vsyncSet = SDL_GL_SetSwapInterval(-1) != -1;
                if (!vsyncSet)
                {
                    Console::warnfn(LOCALE_STR("WARN_ADAPTIVE_SYNC_NOT_SUPPORTED"));
                }
            }

            if (!vsyncSet)
            {
                vsyncSet = SDL_GL_SetSwapInterval(1) != -1;
            }

            DIVIDE_ASSERT(vsyncSet, "VSync change failed!");
        }
        else
        {
            SDL_GL_SetSwapInterval(0);
        }

        // Creating a context will also set it as current in SDL. So ... unset it here.
        Validate(SDL_GL_MakeCurrent( activeWindow->getRawWindow(), nullptr));
    }

    return ErrorCode::NO_ERR;
}

void WindowManager::drawToWindow( DisplayWindow& window )
{

    if ( window.parentWindow() == nullptr && _resolutionChangeQueued.second )
    {
        onResolutionChange(SizeChangeParams
        {
            .winGUID = mainWindow()->getGUID(),
            .width = _resolutionChangeQueued.first.width,
            .height = _resolutionChangeQueued.first.height,
            .isFullScreen = window.fullscreen(),
            .isMainWindow = window.getGUID() == mainWindow()->getGUID(),
        });

        _resolutionChangeQueued.second = false;
    }

    pushActiveWindow(&window);

    Attorney::GFXDeviceWindowManager::drawToWindow( _context->gfx(), window);
}

void WindowManager::flushWindow()
{
    DisplayWindow* window = activeWindow();
    DIVIDE_ASSERT( window != nullptr );

    Attorney::GFXDeviceWindowManager::flushWindow( _context->gfx(), *window );

    const size_t remainingWindows = popActiveWindow();

    if ( remainingWindows > 0 )
    {
        // Switch back to the previous window we were drawing to
        window = activeWindow();
        DIVIDE_ASSERT( window != nullptr );
        popActiveWindow();

        drawToWindow( *window );
    }
}

void WindowManager::toggleFullScreen() const
{
    switch ( mainWindow()->type() )
    {
        case WindowType::WINDOW:
            mainWindow()->changeType( WindowType::FULLSCREEN_WINDOWED );
            break;
        case WindowType::FULLSCREEN_WINDOWED:
            mainWindow()->changeType( WindowType::FULLSCREEN );
            break;
        case WindowType::FULLSCREEN:
            mainWindow()->changeType( WindowType::WINDOW );
            break;
        case WindowType::COUNT:
        default: DIVIDE_UNEXPECTED_CALL(); break;
    }
}

void WindowManager::increaseResolution()
{
    stepResolution( true );
}

void WindowManager::decreaseResolution()
{
    stepResolution( false );
}

void WindowManager::stepResolution( const bool increment )
{
    const auto compare = []( const vec2<U16> a, const vec2<U16> b ) noexcept -> bool
    {
        return a.x > b.x || a.y > b.y;
    };

    const auto& displayModes = DisplayManager::GetDisplayModes( mainWindow()->currentDisplayIndex() );

    const auto renderingResolution = _context->gfx().renderingResolution();

    bool found = false;
    vec2<U16> foundRes;
    if ( increment )
    {
        for ( auto it = displayModes.rbegin(); it != displayModes.rend(); ++it )
        {
            const vec2<U16> res = it->_resolution;
            if ( compare( res, renderingResolution ) )
            {
                found = true;
                foundRes.set( res );
                break;
            }
        }
    }
    else
    {
        for ( const auto& mode : displayModes )
        {
            const vec2<U16> res = mode._resolution;
            if ( compare( renderingResolution, res ) )
            {
                found = true;
                foundRes.set( res );
                break;
            }
        }
    }

    if ( found )
    {
        _resolutionChangeQueued.first.set( foundRes );
        _resolutionChangeQueued.second = true;
    }
}

bool WindowManager::onResolutionChange(const SizeChangeParams& params)
{
    return _context->app().onResolutionChange(params);
}

bool WindowManager::onWindowSizeChange(const DisplayWindow::WindowEventArgs& args)
{
    const bool isMainWindow = args._windowGUID == _mainWindow->getGUID();

    return _context->app().onWindowSizeChange(SizeChangeParams
    {
        .winGUID = args._windowGUID,
        .width = to_U16(args.x),
        .height = to_U16(args.y),
        .isFullScreen = args._flag,
        .isMainWindow = isMainWindow
    });
}

void WindowManager::CaptureMouse(const bool state) noexcept
{
    SDL_CaptureMouse(state ? SDL_TRUE : SDL_FALSE);
}

bool WindowManager::setCursorPosition(I32 x, I32 y) noexcept
{
    const DisplayWindow* focusedWindow = getFocusedWindow();
    if (focusedWindow == nullptr)
    {
        focusedWindow = mainWindow();
    }

    if (x == -1)
    {
        x = SDL_WINDOWPOS_CENTERED_DISPLAY(focusedWindow->currentDisplayIndex());
    }

    if (y == -1)
    {
        y = SDL_WINDOWPOS_CENTERED_DISPLAY(focusedWindow->currentDisplayIndex());
    }

    SDL_WarpMouseInWindow(focusedWindow->getRawWindow(), x, y);
    return true;
}

bool WindowManager::SetGlobalCursorPosition(I32 x, I32 y) noexcept
{
    if (x == -1)
    {
        x = SDL_WINDOWPOS_CENTERED;
    }

    if (y == -1)
    {
        y = SDL_WINDOWPOS_CENTERED;
    }

    return SDL_WarpMouseGlobal(x, y) == 0;
}

void WindowManager::SetCursorStyle(const CursorStyle style)
{
    static CursorStyle s_CurrentStyle = CursorStyle::NONE;
    if (style != s_CurrentStyle )
    {
        s_CurrentStyle = style;
        SDL_SetCursor( s_cursors[to_base( style )] );
    }
}

void WindowManager::ToggleRelativeMouseMode(const bool state) noexcept
{
    [[maybe_unused]] const I32 result = SDL_SetRelativeMouseMode(state ? SDL_TRUE : SDL_FALSE);
    assert(result != -1);
}

bool WindowManager::IsRelativeMouseMode() noexcept
{
    return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

int2 WindowManager::GetGlobalCursorPosition() noexcept
{
    int2 ret(-1);
    SDL_GetGlobalMouseState(&ret.x, &ret.y);
    return ret;
}

int2 WindowManager::GetCursorPosition() noexcept
{
    int2 ret(-1);
    SDL_GetMouseState(&ret.x, &ret.y);
    return ret;
}

U32 WindowManager::GetMouseState(int2& pos, const bool global) noexcept
{
    if (global)
    {
        return to_U32(SDL_GetGlobalMouseState(&pos.x, &pos.y));
    }
    
    return to_U32(SDL_GetMouseState(&pos.x, &pos.y));
}

void WindowManager::SetCaptureMouse(const bool state) noexcept
{
    SDL_CaptureMouse(state ? SDL_TRUE : SDL_FALSE);
}

void WindowManager::snapCursorToCenter()
{
    const DisplayWindow* focusedWindow = getFocusedWindow();
    if (focusedWindow == nullptr)
    {
        focusedWindow = mainWindow();
    }

    const vec2<U16>& center = focusedWindow->getDimensions();
    setCursorPosition(to_I32(center.x * 0.5f), to_I32(center.y * 0.5f));
}

void WindowManager::hideAll() noexcept
{
    for ( const auto& win : _windows)
    {
        win->hidden(true);
    }
}

} //namespace Divide
