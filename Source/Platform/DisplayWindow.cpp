

#include "Headers/DisplayWindow.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Utility/Headers/Localization.h"

#include <SDL3/SDL_vulkan.h>

namespace Divide {

DisplayWindow::DisplayWindow(WindowManager& parent, PlatformContext& context)
    : PlatformContextComponent(context)
    , SDLEventListener("DisplayWindow")
    , _windowID(U32_MAX)
    , _clearColour(DefaultColours::BLACK)
    , _parent(parent)
{
    _prevDimensions.set(1u, 1u);
    _drawableSize.set(0u, 0u);
}

DisplayWindow::~DisplayWindow() 
{
    destroyWindow();

    for ( U8 i = 0u; i < Config::MAX_FRAMES_IN_FLIGHT; ++i )
    {
        DIVIDE_ASSERT(_commandBufferQueues[i]._commandBuffers.empty());
    }
}

ErrorCode DisplayWindow::destroyWindow()
{
    if (_type != WindowType::COUNT && _sdlWindow != nullptr) 
    {
        if (_destroyCbk)
        {
            _destroyCbk();
        }

        WindowManager::DestroyAPISettings(this);
        SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
    }

    return ErrorCode::NO_ERR;
}

ErrorCode DisplayWindow::init(const WindowDescriptor& descriptor)
{
    _parentWindow = descriptor.parentWindow;

    if (_parentWindow != nullptr )
    {
        userData(_parentWindow->userData());
        userData()._ownsContext = false;
    }

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty( props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,               descriptor.title.c_str());
    SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,               descriptor.dimensions.width);
    SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,              descriptor.dimensions.height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN,             descriptor.targetAPI == RenderAPI::Vulkan);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN,             descriptor.targetAPI == RenderAPI::OpenGL);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,          descriptor.flags & to_base(WindowDescriptor::Flags::RESIZEABLE));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, descriptor.flags & to_base(WindowDescriptor::Flags::ALLOW_HIGH_DPI));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN,             descriptor.flags & to_base(WindowDescriptor::Flags::HIDDEN));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN,      descriptor.flags & to_base(WindowDescriptor::Flags::ALWAYS_ON_TOP));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN,            descriptor.flags & to_base(WindowDescriptor::Flags::NO_TASKBAR_ICON));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN,         !(descriptor.flags & to_base(WindowDescriptor::Flags::DECORATED)));

    const bool vsync = descriptor.flags & to_base(WindowDescriptor::Flags::VSYNC);

    if (vsync)
    {
        _flags |= to_base( WindowFlags::VSYNC );
    }
    else
    {
        _flags &= ~to_base( WindowFlags::VSYNC );
    }

    WindowType winType = WindowType::WINDOW;
    if (descriptor.flags & to_base(WindowDescriptor::Flags::FULLSCREEN))
    {
        winType = WindowType::FULLSCREEN;
    }
    else if (descriptor.flags & to_base(WindowDescriptor::Flags::FULLSCREEN_DESKTOP))
    {
        winType = WindowType::FULLSCREEN_WINDOWED;
    }

    if (winType == WindowType::FULLSCREEN || winType == WindowType::FULLSCREEN_WINDOWED)
    {
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
    }

    int2 position(descriptor.position);

    if (position.x == -1)
    {
        position.x = SDL_WINDOWPOS_CENTERED_DISPLAY(descriptor.targetDisplay);
    }
    if (position.y == -1)
    {
        position.y = SDL_WINDOWPOS_CENTERED_DISPLAY(descriptor.targetDisplay);
    }

    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, position.x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, position.y);

    _sdlWindow = SDL_CreateWindowWithProperties(props);

    // Check if we have a valid window
    if (_sdlWindow == nullptr)
    {
        Console::errorfn(LOCALE_STR("ERROR_GFX_DEVICE"), Util::StringFormat(LOCALE_STR("ERROR_SDL_WINDOW"), SDL_GetError()).c_str());
        Console::printfn(LOCALE_STR("WARN_APPLICATION_CLOSE"));

        return ErrorCode::SDL_WINDOW_INIT_ERROR;
    }

    if (winType == WindowType::FULLSCREEN || winType == WindowType::FULLSCREEN_WINDOWED)
    {
        // returns a pointer to the exclusive fullscreen mode to use or NULL for borderless fullscreen desktop mode.
        const SDL_DisplayMode* fullScreenMode = SDL_GetWindowFullscreenMode( _sdlWindow );
        if (fullScreenMode != nullptr && winType == WindowType::FULLSCREEN_WINDOWED)
        {
            // Borderless fullscreen mode not available, so we will use the exclusive fullscreen mode instead
            winType = WindowType::FULLSCREEN;
        }
        else if (fullScreenMode == nullptr && winType == WindowType::FULLSCREEN)
        {
            // Exclusive fullscreen mode not available, so we will use the borderless fullscreen desktop mode instead
            winType = WindowType::FULLSCREEN_WINDOWED;
        }
    }

    _previousType = _type = winType;
    _windowID = SDL_GetWindowID(_sdlWindow);
    _drawableSize = descriptor.dimensions;
    _initialDisplay = descriptor.targetDisplay;

    return ErrorCode::NO_ERR;
}

WindowHandle DisplayWindow::handle() const noexcept
{
    // Varies from OS to OS
    WindowHandle ret{};
    GetWindowHandle(_sdlWindow, ret);
    return ret;
}

void DisplayWindow::notifyListeners(const WindowEvent event, const WindowEventArgs& args)
{
    for (const auto& listener : _eventListeners[to_base(event)])
    {
        if (!listener._cbk(args))
        {
            Console::errorfn(LOCALE_STR("ERROR_SDL_LISTENER_NOTIFY"), listener._name, Names::windowEvent[to_base(event)]);
        }
    }
}

bool DisplayWindow::onSDLEvent(const SDL_Event event)
{
    bool ret = false;

    if (_windowID != event.window.windowID)
    {
        return ret;
    }

    WindowEventArgs args = {};
    args._windowGUID = getGUID();

    updateDrawableSize();

    if (fullscreen())
    {
        args.x = to_I32(WindowManager::GetFullscreenResolution().width);
        args.y = to_I32(WindowManager::GetFullscreenResolution().height);
    }
    else
    {
        args.x = event.window.data1;
        args.y = event.window.data2;
    }

    ret = true;
    switch (event.type)
    {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            args.x = event.quit.type;
            args.y = event.quit.timestamp;
            notifyListeners(WindowEvent::CLOSE_REQUESTED, args);
        } break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        {
            _flags |= to_base( WindowFlags::IS_HOVERED );
            notifyListeners(WindowEvent::MOUSE_HOVER_ENTER, args);
        } break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            _flags &= to_base( WindowFlags::IS_HOVERED );
            notifyListeners(WindowEvent::MOUSE_HOVER_LEAVE, args);
        } break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        {
            _flags |= to_base( WindowFlags::HAS_FOCUS );
            notifyListeners(WindowEvent::GAINED_FOCUS, args);
        } break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            _flags &= to_base( WindowFlags::HAS_FOCUS );
            notifyListeners(WindowEvent::LOST_FOCUS, args);
        } break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        {
            if (!_internalResizeEvent)
            {
                const U16 width = to_U16(event.window.data1);
                const U16 height = to_U16(event.window.data2);
                if (!setDimensions(width, height))
                {
                    NOP();
                }
            }

            args._flag = fullscreen();
            notifyListeners(WindowEvent::SIZE_CHANGED, args);
            _internalResizeEvent = false;
        } break;
        case SDL_EVENT_WINDOW_MOVED:
        {
            notifyListeners(WindowEvent::MOVED, args);
            if (!_internalMoveEvent)
            {
                setPosition(event.window.data1, 
                            event.window.data2);
                _internalMoveEvent = false;
            }
        } break;
        case SDL_EVENT_WINDOW_SHOWN:
        {
            _flags &= to_base( WindowFlags::HIDDEN );
            notifyListeners(WindowEvent::SHOWN, args);
        } break;
        case SDL_EVENT_WINDOW_HIDDEN:
        {
            _flags |= to_base( WindowFlags::HIDDEN);
            notifyListeners(WindowEvent::HIDDEN, args);
        } break;
        case SDL_EVENT_WINDOW_MINIMIZED:
        {
            notifyListeners(WindowEvent::MINIMIZED, args);
            minimized(true);
        } break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
        {
            notifyListeners(WindowEvent::MAXIMIZED, args);
            minimized(false);
        } break;
        case SDL_EVENT_WINDOW_RESTORED:
        {
            notifyListeners(WindowEvent::RESTORED, args);
            minimized(false);
        } break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        {
            args._flag = event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
            notifyListeners(WindowEvent::FULLSCREEN_TOGGLED, args);
            mouseGrabState(args._flag);
        } break;
        default:
        {
            ret = false;
        } break;
    };

    return ret;
}

SDL_DisplayID DisplayWindow::currentDisplayIndex() const noexcept
{
    return SDL_GetDisplayForWindow(_sdlWindow);
}

Rect<I32> DisplayWindow::getBorderSizes() const noexcept
{
    I32 top = 0, left = 0, bottom = 0, right = 0;
    if (SDL_GetWindowBordersSize(_sdlWindow, &top, &left, &bottom, &right))
    {
        return { top, left, bottom, right };
    }

    return {};
}

vec2<U16> DisplayWindow::getDrawableSize() const noexcept
{
    return _drawableSize;
}

void DisplayWindow::updateDrawableSize() noexcept
{
    if ( _type == WindowType::FULLSCREEN || _type == WindowType::FULLSCREEN_WINDOWED )
    {
        _drawableSize = WindowManager::GetFullscreenResolution();
    }
    else
    {
        I32 w = 1, h = 1;
        SDL_GetWindowSizeInPixels(_sdlWindow, &w, &h);
        if (w > 0 && h > 0)
        {
            _drawableSize.set(w, h);
        }
    }
    
}
void DisplayWindow::opacity(const U8 opacity) noexcept
{
    if (SDL_SetWindowOpacity(_sdlWindow, to_F32(opacity) / 255))
    {
        _prevOpacity = _opacity;
        _opacity = opacity;
    }
}

/// Window positioning is handled by SDL
void DisplayWindow::setPosition(I32 x, I32 y, const bool global, const bool offset) {
    _internalMoveEvent = true;

    const I32 displayIndex = currentDisplayIndex();
    if (x == -1) {
        x = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
    } else if (!global && offset) {
        x += _parent.monitorData()[displayIndex].viewport.x;
    }
    if (y == -1) {
        y = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
    } else if (!global && offset) {
        y += _parent.monitorData()[displayIndex].viewport.y;
    }

    SDL_SetWindowPosition(_sdlWindow, x, y);
}

int2 DisplayWindow::getPosition(const bool global, const bool offset) const {
    int2 ret;
    SDL_GetWindowPosition(_sdlWindow, &ret.x, &ret.y);

    if (!global && offset) {
        const int2 pOffset = _parent.monitorData()[currentDisplayIndex()].viewport.xy;
        ret -= pOffset;
    }

    return ret;
}

void DisplayWindow::bringToFront() const noexcept {
    SDL_RaiseWindow(_sdlWindow);
}

/// Centering is also easier via SDL
void DisplayWindow::centerWindowPosition() {
    setPosition(-1, -1);
}

void DisplayWindow::decorated(const bool state) noexcept {
    // documentation states that this is a no-op on redundant state, so no need to bother checking
    SDL_SetWindowBordered(_sdlWindow, state);

    state ? _flags |= to_base( WindowFlags::DECORATED ) : _flags &= to_base( WindowFlags::DECORATED );
}

void DisplayWindow::hidden(const bool state) noexcept
{
    if (state)
    {
        SDL_HideWindow(_sdlWindow);
        _flags |= to_base(WindowFlags::HIDDEN);
    }
    else
    {
        SDL_ShowWindow(_sdlWindow);
        _flags &= to_base(WindowFlags::HIDDEN);
    }
}

void DisplayWindow::restore() noexcept
{
    SDL_RestoreWindow(_sdlWindow);

    _flags &= ~to_base(WindowFlags::MAXIMIZED);
    _flags &= ~to_base(WindowFlags::MINIMIZED);
}

void DisplayWindow::minimized(const bool state) noexcept {
    if (((SDL_GetWindowFlags(_sdlWindow) & to_U32(SDL_WINDOW_MINIMIZED)) != 0u) != state)
    {
        if (state)
        {
            SDL_MinimizeWindow(_sdlWindow);
        }
        else
        {
            restore();
        }
    }

    state ? _flags |= to_base( WindowFlags::MINIMIZED ) : _flags &= to_base( WindowFlags::MINIMIZED );
}

void DisplayWindow::maximized(const bool state) noexcept
{
    if (((SDL_GetWindowFlags(_sdlWindow) & to_U32(SDL_WINDOW_MAXIMIZED)) != 0u) != state)
    {
        if (state)
        {
            SDL_MaximizeWindow(_sdlWindow);
        }
        else
        {
            restore();
        }
    }

    state ? _flags |= to_base( WindowFlags::MAXIMIZED ) : _flags &= to_base( WindowFlags::MAXIMIZED );
}

bool DisplayWindow::mouseGrabState() const noexcept
{
    return SDL_GetWindowMouseGrab(_sdlWindow);
}

void DisplayWindow::mouseGrabState(const bool state) const noexcept
{
    SDL_SetWindowMouseGrab(_sdlWindow, state);
}

void DisplayWindow::handleChangeWindowType(const WindowType newWindowType)
{
    if (_type == newWindowType)
    {
        return;
    }

    _previousType = _type;
    _type = newWindowType;

    switch (newWindowType)
    {
        case WindowType::WINDOW:
            DIVIDE_EXPECTED_CALL(SDL_SetWindowFullscreen(_sdlWindow, false));
            break;
        case WindowType::FULLSCREEN_WINDOWED:
            DIVIDE_EXPECTED_CALL(SDL_SetWindowFullscreen(_sdlWindow, true) && SDL_SetWindowFullscreenMode(_sdlWindow, nullptr));
            break;
        case WindowType::FULLSCREEN:
            DIVIDE_EXPECTED_CALL(SDL_SetWindowFullscreen(_sdlWindow, true) && SDL_SetWindowFullscreenMode(_sdlWindow, &_parent.fullscreenMode()));
            break;
        default: break;
    };

    SDLEventManager::pollEvents();
}

vec2<U16> DisplayWindow::getPreviousDimensions() const noexcept
{
    if (fullscreen()) {
        return WindowManager::GetFullscreenResolution();
    }
    return _prevDimensions;
}

bool DisplayWindow::setDimensions(U16 width, U16 height)
{
    const vec2<U16> dim = getDimensions();
    if (dim == vec2<U16>(width, height))
    {
        return true;
    }

    _internalResizeEvent = true;

    bool error = false;
    switch(_type)
    {
        case WindowType::FULLSCREEN:
        {
            // Find a decent resolution close to our dragged dimensions
            SDL_DisplayMode closestMode = {};
            const SDL_DisplayMode* crtMode = SDL_GetCurrentDisplayMode(currentDisplayIndex());
            if ( !SDL_GetClosestFullscreenDisplayMode(currentDisplayIndex(), to_I32(width), to_I32(height),crtMode->refresh_rate, true, &closestMode))
            {
                error = true;
            }
            else if ( !SDL_SetWindowFullscreenMode(_sdlWindow, &closestMode))
            {
                error = true;
            }
        } break;
        case WindowType::FULLSCREEN_WINDOWED: //fall-through
            changeType(WindowType::WINDOW);
            SDL_SyncWindow(_sdlWindow);
        case WindowType::WINDOW: [[fallthrough]];
        default:
        {
            maximized(false);
            if ( !SDL_SetWindowSize(_sdlWindow, width, height) )
            {
                error = true;
            }
        } break;
    }

    if ( error )
    {
        Console::errorfn(LOCALE_STR("SDL_ERROR"), SDL_GetError());
        return false;
    }

    SDLEventManager::pollEvents();
    _prevDimensions.set(dim);
    return true;
}

bool DisplayWindow::setDimensions(const vec2<U16> dimensions) {
    return setDimensions(dimensions.x, dimensions.y);
}

vec2<U16> DisplayWindow::getDimensions() const noexcept {
    I32 width = -1, height = -1;
    SDL_GetWindowSize(_sdlWindow, &width, &height);

    return vec2<U16>(width, height);
}

void DisplayWindow::renderingViewport(const Rect<I32>& viewport) noexcept {
    _renderingViewport.set(viewport);
}

GFX::CommandBufferQueue& DisplayWindow::getCurrentCommandBufferQueue()
{
    return _commandBufferQueues[GFXDevice::FrameCount() % Config::MAX_FRAMES_IN_FLIGHT];
}

} //namespace Divide
