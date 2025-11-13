

#include "Headers/WindowManager.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/DisplayManager.h"
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
            case CursorStyle::ARROW:       return SDL_SYSTEM_CURSOR_DEFAULT;
            case CursorStyle::HAND:        return SDL_SYSTEM_CURSOR_POINTER;
            case CursorStyle::NONE:        return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
            case CursorStyle::WAIT:        return SDL_SYSTEM_CURSOR_PROGRESS;
            case CursorStyle::RESIZE_ALL:  return SDL_SYSTEM_CURSOR_MOVE;
            case CursorStyle::RESIZE_EW:   return SDL_SYSTEM_CURSOR_EW_RESIZE;
            case CursorStyle::RESIZE_NS:   return SDL_SYSTEM_CURSOR_NS_RESIZE;
            case CursorStyle::RESIZE_NESW: return SDL_SYSTEM_CURSOR_NESW_RESIZE;
            case CursorStyle::RESIZE_NWSE: return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
            case CursorStyle::TEXT_INPUT:  return SDL_SYSTEM_CURSOR_TEXT;
            default:
            case CursorStyle::COUNT:       break;
        }

        return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    }

    bool Validate(const bool result)
    {
        if (!result)
        {
            Console::errorfn(LOCALE_STR("SDL_ERROR"), SDL_GetError());
            return false;
        }

        return true;
    }

    bool ValidateAssert(const bool result)
    {
        if (!Validate(result))
        {
            DIVIDE_UNEXPECTED_CALL();
            return false;
        }

        return true;
    }

} // namespace 

const SDL_DisplayMode* WindowManager::s_mainDisplayMode = nullptr;
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
    if (s_mainDisplayMode == nullptr)
    {
        return vec2<U16>{1u};
    }

    return vec2<U16>( s_mainDisplayMode->w, s_mainDisplayMode->h);
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

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        return ErrorCode::WINDOW_INIT_ERROR;
    }

    _context = &context;
    U8 numDrivers = SDL_GetNumVideoDrivers();
    for ( U8 i = 0u; i < numDrivers; ++i)
    {
        const char* crtDriver = SDL_GetVideoDriver(i);
        Console::printfn(LOCALE_STR("SDL_VIDEO_DRIVER"), crtDriver != nullptr ? crtDriver : fmt::format("UNKNOWN DRIVER [ {} ]", i));
    }

    const char* videoDriver = SDL_GetCurrentVideoDriver();

    Console::printfn(LOCALE_STR("SDL_CURRENT_VIDEO_DRIVER"), videoDriver != nullptr ? videoDriver : "UNKNOWN");

    I32 displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
    if ( displays && displayCount > 0)
    {
        for (I32 i = 0; i < displayCount; ++i)
        {
            SDL_DisplayID instance_id = displays[i];

            MonitorData data = {};

            SDL_Rect r;
            SDL_GetDisplayBounds(instance_id, &r);
            data.viewport.xy = { to_I16(r.x), to_I16(r.y) };
            data.viewport.zw = { to_I16(r.w), to_I16(r.h) };

            SDL_GetDisplayUsableBounds(instance_id, &r);
            data.drawableArea.xy = { to_I16(r.x), to_I16(r.y) };
            data.drawableArea.zw = { to_I16(r.w), to_I16(r.h) };
            data.dpi = PlatformDefaultDPI();

            _monitors.push_back(data);
        }
    }
    else
    {
        SDL_free(displays);
        return ErrorCode::SDL_WINDOW_INIT_ERROR;
    }

    const I32 displayIndex = std::max(std::min(targetDisplayIndex, displayCount - 1), 0);
    s_mainDisplayMode = SDL_GetCurrentDisplayMode(displays[displayIndex]);
    SDL_free(displays);



    for ( U8 i = 0; i < to_U8( CursorStyle::COUNT ); ++i )
    {
        s_cursors[i] = SDL_CreateSystemCursor( CursorToSDL( static_cast<CursorStyle>(i) ) );
    }

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
        descriptor.flags &= ~to_base(WindowDescriptor::Flags::RESIZABLE);
    }

    if constexpr (Config::ENABLE_GPU_VALIDATION)
    {
        if (context.config().debug.renderer.enableRenderAPIDebugging)
        {
            _apiSettings._createDebugContext = true;
        }
    }
    if (context.config().debug.renderer.enableRenderAPIBestPractices)
    {
        _apiSettings._isolateGraphicsContext = true;
        _apiSettings._requestRobustContext = true;
        _apiSettings._enableCompatibilityLayer = false;
    }

    ErrorCode err = ErrorCode::NO_ERR;
    err = findAndApplyAPISettings(context, descriptor);
    if (err != ErrorCode::NO_ERR)
    {
        return err;
    }

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

        string refreshRates;
        DisplayManager::OutputDisplayProperties prevMode;
        const auto printMode = [&refreshRates](DisplayManager::OutputDisplayProperties& crtMode, const DisplayManager::OutputDisplayProperties& nextMode)
        {
            if (!refreshRates.empty() )
            {
                Console::printfn(LOCALE_STR("CURRENT_DISPLAY_MODE"),
                    crtMode._resolution.width,
                    crtMode._resolution.height,
                    crtMode._bitsPerPixel,
                    crtMode._formatName.c_str(),
                    refreshRates.c_str());

                refreshRates = "";
            }
            crtMode = nextMode;
        };


        I32 numDisplays = 0, numDisplayModes = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&numDisplays);
        if (displays)
        {
            for (I32 displayIndex = 0; displayIndex < numDisplays; ++displayIndex)
            {
                SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displays[displayIndex], &numDisplayModes);

                if ( modes )
                {
                    Console::printfn(LOCALE_STR("AVAILABLE_VIDEO_MODES"), displayIndex, numDisplayModes);

                    for (I32 modeIndex = 0; modeIndex < numDisplayModes; ++modeIndex)
                    {
                        SDL_DisplayMode* mode = modes[modeIndex];

                        DisplayManager::OutputDisplayProperties tempDisplayMode
                        {
                            ._formatName = SDL_GetPixelFormatName(mode->format),
                            ._resolution = {mode->w, mode->h},
                            ._maxRefreshRate = mode->refresh_rate,
                            ._bitsPerPixel = to_U8(SDL_BITSPERPIXEL(mode->format))
                        };

                        SDL_copyp(&tempDisplayMode._internalMode, mode);

                        Attorney::DisplayManagerWindowManager::RegisterDisplayMode(tempDisplayMode);

                        if (prevMode._resolution   != tempDisplayMode._resolution ||
                            prevMode._bitsPerPixel != tempDisplayMode._bitsPerPixel ||
                            prevMode._formatName   != tempDisplayMode._formatName)
                        {
                            printMode(prevMode, tempDisplayMode);
                        }

                        if (refreshRates.empty())
                        {
                            refreshRates = Util::StringFormat("{}", tempDisplayMode._maxRefreshRate);
                        }
                        else
                        {
                            refreshRates.append(Util::StringFormat(", {}", tempDisplayMode._maxRefreshRate));
                        }
                    }
                    SDL_free(modes);
                }
            }

            printMode(prevMode, {});
            SDL_free(displays);
        }
        else
        {
            return ErrorCode::WINDOW_INIT_ERROR;
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
        SDL_DestroyCursor(it);
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

    if (descriptor.targetAPI == RenderAPI::OpenGL && descriptor.parentWindow != nullptr )
    {
        Validate( SDL_GL_MakeCurrent( descriptor.parentWindow->getRawWindow(), descriptor.parentWindow->userData()._glContext ) );
    }

    auto window = std::make_unique<DisplayWindow>(*this, *_context);
    err = applyAPISettingsPreCreate(*_context, descriptor.targetAPI);
    if (err != ErrorCode::NO_ERR)
    {
        Console::errorfn(LOCALE_STR("ERROR_SDL_WINDOW"), SDL_GetError());
        Console::errorfn(LOCALE_STR("ERROR_SDL_OPENGL_CONTEXT"), GetLastErrorText());
        Console::warnfn(LOCALE_STR("WARN_SWITCH_API"));
        Console::warnfn(LOCALE_STR("WARN_APPLICATION_CLOSE"));

        return nullptr;
    }

    err = window->init(descriptor);
    if (err != ErrorCode::NO_ERR)
    {
        return nullptr;
    }

    err = applyAPISettingsPostCreate(*_context, descriptor.targetAPI, window.get());
    if (err != ErrorCode::NO_ERR)
    {
        return nullptr;
    }

    if (_mainWindow == nullptr)
    {
        DIVIDE_ASSERT( descriptor.parentWindow == nullptr );
        _mainWindow = window.get();
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

    if ( window->userData()._glContext != nullptr && window->userData()._ownsContext)
    {
        SDL_GL_DestroyContext( window->userData()._glContext );
    }

    window->userData()._glContext = nullptr;
}

ErrorCode WindowManager::applyAPISettingsPreCreate(const PlatformContext& context, const RenderAPI api)
{
    if ( api != RenderAPI::OpenGL)
    {
        return ErrorCode::NO_ERR;
    }

    Uint32 OpenGLFlags = 0u;
    if (_apiSettings._enableCompatibilityLayer)
    {
        OpenGLFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
    }

    if (_apiSettings._isolateGraphicsContext)
    {
        OpenGLFlags |= SDL_GL_CONTEXT_RESET_ISOLATION_FLAG;
    }
    if (_apiSettings._requestRobustContext)
    {
        OpenGLFlags |= SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG;
    }
    if (_apiSettings._createDebugContext)
    {
        OpenGLFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
    }
    else
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
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, _apiSettings._enableCompatibilityLayer ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY : SDL_GL_CONTEXT_PROFILE_CORE));

    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6));
    ValidateAssert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1));
    if (context.config().rendering.MSAASamples > 0u)
    {
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1));
        ValidateAssert(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, std::min(context.config().rendering.MSAASamples, to_U8(4u)))); // Cap to 4xMSAA as we can't query HW support yet
    }

    return ErrorCode::NO_ERR;
}

ErrorCode WindowManager::findAndApplyAPISettings(const PlatformContext& context, const WindowDescriptor& descriptor)
{
    const RenderAPI api = descriptor.targetAPI;

    if (api == RenderAPI::Vulkan || api == RenderAPI::None)
    {
        NOP();
    }
    else if (api == RenderAPI::NRI_Vulkan || api == RenderAPI::NRI_D3D11 || api == RenderAPI::NRI_D3D12 || api == RenderAPI::NRI_None)
    {
        NOP();
    }
    else if (api == RenderAPI::OpenGL)
    {
        SDL_Window* testWindow = SDL_CreateWindow("OpenGL Settings Window", 320, 240, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!testWindow)
        {
            return ErrorCode::SDL_WINDOW_INIT_ERROR;
        }
        SCOPE_EXIT
        {
            SDL_DestroyWindow(testWindow);
        };

        const auto applyCurrentSettings = [&]()
        {
            ErrorCode err = applyAPISettingsPreCreate(context, descriptor.targetAPI);
            if ( err != ErrorCode::NO_ERR )
            {
                return err;
            }

            SDL_GLContext context = SDL_GL_CreateContext(testWindow);
            if (context == nullptr)
            {
                return ErrorCode::GL_OLD_HARDWARE;
            }
            SDL_GL_DestroyContext(context);
            return ErrorCode::NO_ERR;
        };

        ErrorCode err = applyCurrentSettings();
        if (err == ErrorCode::GL_OLD_HARDWARE)
        {
            Console::errorfn(LOCALE_STR("INVALID_OPENGL_CONTEXT_SETTINGS"), _apiSettings._isolateGraphicsContext, _apiSettings._createDebugContext, _apiSettings._enableCompatibilityLayer, _apiSettings._requestRobustContext);
            _apiSettings._isolateGraphicsContext = false;
            err = applyCurrentSettings();

            if (err == ErrorCode::GL_OLD_HARDWARE)
            {
                Console::errorfn(LOCALE_STR("INVALID_OPENGL_CONTEXT_SETTINGS"), _apiSettings._isolateGraphicsContext, _apiSettings._createDebugContext, _apiSettings._enableCompatibilityLayer, _apiSettings._requestRobustContext);
                _apiSettings._requestRobustContext = false;
                _apiSettings._createDebugContext = false;
                _apiSettings._enableCompatibilityLayer = true;
                err = applyCurrentSettings();
                if (err == ErrorCode::GL_OLD_HARDWARE)
                {
                    Console::errorfn(LOCALE_STR("INVALID_OPENGL_CONTEXT_SETTINGS"), _apiSettings._isolateGraphicsContext, _apiSettings._createDebugContext, _apiSettings._enableCompatibilityLayer, _apiSettings._requestRobustContext);
                }
            }
        }

        if (err == ErrorCode::NO_ERR)
        {
            Console::printfn(LOCALE_STR("VALID_OPENGL_CONTEXT_SETTINGS"), _apiSettings._isolateGraphicsContext, _apiSettings._createDebugContext, _apiSettings._enableCompatibilityLayer, _apiSettings._requestRobustContext);
        }

    }
    else
    {
        return ErrorCode::GFX_NOT_SUPPORTED;
    }

    return ErrorCode::NO_ERR;
}

ErrorCode WindowManager::applyAPISettingsPostCreate( const PlatformContext& context, const RenderAPI api, DisplayWindow* targetWindow )
{
    DestroyAPISettings(targetWindow);

    if (api == RenderAPI::OpenGL)
    {
       // Create a context and make it current
        DIVIDE_ASSERT( targetWindow->userData()._glContext == nullptr );

        if ( targetWindow->parentWindow() == nullptr )
        {
            targetWindow->userData()._glContext = SDL_GL_CreateContext( targetWindow->getRawWindow() );
            if ( targetWindow->userData()._glContext == nullptr)
            {
                return ErrorCode::GL_OLD_HARDWARE;
            }

            targetWindow->userData()._ownsContext = true;
        }
        else
        {
            targetWindow->userData()._glContext = targetWindow->parentWindow()->userData()._glContext;
            targetWindow->userData()._ownsContext = false;
        }

        if ( targetWindow->flags() & to_base(WindowFlags::VSYNC))
        {
            // Vsync is toggled on or off via the external config file
            bool vsyncSet = false;
            // Late swap may fail
            if ( context.config().runtime.adaptiveSync )
            {
                vsyncSet = SDL_GL_SetSwapInterval(SDL_WINDOW_SURFACE_VSYNC_ADAPTIVE);
                if (!vsyncSet)
                {
                    Console::warnfn(LOCALE_STR("WARN_ADAPTIVE_SYNC_NOT_SUPPORTED"));
                }
            }

            if (!vsyncSet)
            {
                vsyncSet = SDL_GL_SetSwapInterval(SDL_WINDOW_SURFACE_VSYNC_ENABLED);
            }

            DIVIDE_ASSERT(vsyncSet, "VSync change failed!");
        }
        else
        {
            SDL_GL_SetSwapInterval(SDL_WINDOW_SURFACE_VSYNC_DISABLED);
        }
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

    const SDL_DisplayID crtDisplayIndex = mainWindow()->currentDisplayIndex();

    const auto& displayModes = DisplayManager::GetDisplayModes();

    const auto renderingResolution = _context->gfx().renderingResolution();

    bool found = false;
    vec2<U16> foundRes;
    if ( increment )
    {
        for ( auto it = displayModes.rbegin(); it != displayModes.rend(); ++it )
        {
            if ( it->_internalMode.displayID != crtDisplayIndex)
            {
                continue;
            }

            const vec2<U16> res = it->_resolution;
            if ( compare( res, renderingResolution ) )
            {
                found = true;
                foundRes.set( res );
                _fullscreenMode = it->_internalMode;
                break;
            }
        }
    }
    else
    {
        for ( const auto& mode : displayModes )
        {
            if (mode._internalMode.displayID != crtDisplayIndex)
            {
                continue;
            }

            const vec2<U16> res = mode._resolution;
            if ( compare( renderingResolution, res ) )
            {
                found = true;
                foundRes.set( res );
                _fullscreenMode = mode._internalMode;
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
    SDL_CaptureMouse(state);
}

bool WindowManager::SetCursorPosition(const DisplayWindow* window, I32 x, I32 y) noexcept
{
    if (window == nullptr)
    {
        return false;
    }

    if (x == -1)
    {
        x = SDL_WINDOWPOS_CENTERED_DISPLAY(window->currentDisplayIndex());
    }

    if (y == -1)
    {
        y = SDL_WINDOWPOS_CENTERED_DISPLAY(window->currentDisplayIndex());
    }

    SDL_WarpMouseInWindow(window->getRawWindow(), x, y);
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

void WindowManager::ToggleRelativeMouseMode(const DisplayWindow* window, const bool state) noexcept
{
    if (window == nullptr)
    {
        return;
    }

    SDL_SetWindowRelativeMouseMode(window->getRawWindow(), state);
}

bool WindowManager::IsRelativeMouseMode(const DisplayWindow* window) noexcept
{
    if (window == nullptr)
    {
        return false;
    }

    return SDL_GetWindowRelativeMouseMode(window->getRawWindow());
}

float2 WindowManager::GetGlobalCursorPosition() noexcept
{
    float2 ret(-1.f);
    SDL_GetGlobalMouseState(&ret.x, &ret.y);
    return ret;
}

float2 WindowManager::GetCursorPosition() noexcept
{
    float2 ret(-1.f);
    SDL_GetMouseState(&ret.x, &ret.y);
    return ret;
}

U32 WindowManager::GetMouseState(float2& pos, const bool global) noexcept
{
    if (global)
    {
        return to_U32(SDL_GetGlobalMouseState(&pos.x, &pos.y));
    }
    
    return to_U32(SDL_GetMouseState(&pos.x, &pos.y));
}

void WindowManager::SetCaptureMouse(const bool state) noexcept
{
    SDL_CaptureMouse(state);
}

void WindowManager::SnapCursorToCenter(const DisplayWindow* window)
{
    if (window == nullptr )
    {
        return;
    }

    const vec2<U16>& center = window->getDimensions();
    SetCursorPosition(window, to_I32(center.x * 0.5f), to_I32(center.y * 0.5f));
}

void WindowManager::hideAll() noexcept
{
    for ( const auto& win : _windows)
    {
        win->hidden(true);
    }
}

} //namespace Divide
