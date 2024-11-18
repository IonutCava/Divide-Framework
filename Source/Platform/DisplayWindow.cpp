

#include "Headers/DisplayWindow.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Headers/SDLEventManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBufferPool.h"
#include "Utility/Headers/Localization.h"

#include <SDL2/SDL_vulkan.h>

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

ErrorCode DisplayWindow::init(const U32 windowFlags,
                              const WindowType initialType,
                              const WindowDescriptor& descriptor)
{
    _parentWindow = descriptor.parentWindow;

    const bool vsync = descriptor.flags & to_base(WindowDescriptor::Flags::VSYNC);

    if (vsync)
    {
        _flags |= to_base( WindowFlags::VSYNC );
    }
    else
    {
        _flags &= ~to_base( WindowFlags::VSYNC );
    }

    _previousType = _type = initialType;

    int2 position(descriptor.position);

    _initialDisplay = descriptor.targetDisplay;

    if (position.x == -1)
    {
        position.x = SDL_WINDOWPOS_CENTERED_DISPLAY(descriptor.targetDisplay);
    }
    if (position.y == -1)
    {
        position.y = SDL_WINDOWPOS_CENTERED_DISPLAY(descriptor.targetDisplay);
    }

    _sdlWindow = SDL_CreateWindow( descriptor.title.c_str(),
                                  position.x,
                                  position.y,
                                  descriptor.dimensions.width,
                                  descriptor.dimensions.height,
                                  windowFlags);

    // Check if we have a valid window
    if (_sdlWindow == nullptr)
    {
        Console::errorfn(LOCALE_STR("ERROR_GFX_DEVICE"), Util::StringFormat(LOCALE_STR("ERROR_SDL_WINDOW"), SDL_GetError()).c_str());
        Console::printfn(LOCALE_STR("WARN_APPLICATION_CLOSE"));

        return ErrorCode::SDL_WINDOW_INIT_ERROR;
    }

    _windowID = SDL_GetWindowID(_sdlWindow);
    _drawableSize = descriptor.dimensions;
    
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

    if (event.type != SDL_WINDOWEVENT ||
        _windowID != event.window.windowID)
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
    switch (event.window.event)
    {
        case SDL_WINDOWEVENT_CLOSE:
        {
            args.x = event.quit.type;
            args.y = event.quit.timestamp;
            notifyListeners(WindowEvent::CLOSE_REQUESTED, args);
        } break;
        case SDL_WINDOWEVENT_ENTER:
        {
            _flags |= to_base( WindowFlags::IS_HOVERED );
            notifyListeners(WindowEvent::MOUSE_HOVER_ENTER, args);
        } break;
        case SDL_WINDOWEVENT_LEAVE:
        {
            _flags &= to_base( WindowFlags::IS_HOVERED );
            notifyListeners(WindowEvent::MOUSE_HOVER_LEAVE, args);
        } break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
        {
            _flags |= to_base( WindowFlags::HAS_FOCUS );
            notifyListeners(WindowEvent::GAINED_FOCUS, args);
        } break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
        {
            _flags &= to_base( WindowFlags::HAS_FOCUS );
            notifyListeners(WindowEvent::LOST_FOCUS, args);
        } break;
        case SDL_WINDOWEVENT_RESIZED:
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
            notifyListeners(WindowEvent::RESIZED, args);
            _internalResizeEvent = false;
        } break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        {
            args._flag = fullscreen();
            notifyListeners(WindowEvent::SIZE_CHANGED, args);
        } break;
        case SDL_WINDOWEVENT_MOVED:
        {
            notifyListeners(WindowEvent::MOVED, args);
            if (!_internalMoveEvent)
            {
                setPosition(event.window.data1, 
                            event.window.data2);
                _internalMoveEvent = false;
            }
        } break;
        case SDL_WINDOWEVENT_SHOWN:
        {
            _flags &= to_base( WindowFlags::HIDDEN );
            notifyListeners(WindowEvent::SHOWN, args);
        } break;
        case SDL_WINDOWEVENT_HIDDEN:
        {
            _flags |= to_base( WindowFlags::HIDDEN);
            notifyListeners(WindowEvent::HIDDEN, args);
        } break;
        case SDL_WINDOWEVENT_MINIMIZED:
        {
            notifyListeners(WindowEvent::MINIMIZED, args);
            minimized(true);
        } break;
        case SDL_WINDOWEVENT_MAXIMIZED:
        {
            notifyListeners(WindowEvent::MAXIMIZED, args);
            minimized(false);
        } break;
        case SDL_WINDOWEVENT_RESTORED:
        {
            notifyListeners(WindowEvent::RESTORED, args);
            minimized(false);
        } break;

        default:
        {
            ret = false;
        } break;
    };

    return ret;
}

I32 DisplayWindow::currentDisplayIndex() const noexcept {
    const I32 displayIndex = SDL_GetWindowDisplayIndex(_sdlWindow);
    assert(displayIndex != -1);
    return displayIndex;
}

Rect<I32> DisplayWindow::getBorderSizes() const noexcept {
    I32 top = 0, left = 0, bottom = 0, right = 0;
    if (SDL_GetWindowBordersSize(_sdlWindow, &top, &left, &bottom, &right) != -1) {
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
        int w = 1, h = 1;
        switch ( _context.gfx().renderAPI() )
        {
            case RenderAPI::None:   NOP();                                            break;
            case RenderAPI::Vulkan: SDL_Vulkan_GetDrawableSize( _sdlWindow, &w, &h ); break;
            case RenderAPI::OpenGL: SDL_GL_GetDrawableSize( _sdlWindow, &w, &h );     break;
            default:                DIVIDE_UNEXPECTED_CALL();                         break;
        }
        if (w > 0 && h > 0)
        {
            _drawableSize.set(w, h);
        }
    }
    
}
void DisplayWindow::opacity(const U8 opacity) noexcept {
    if (SDL_SetWindowOpacity(_sdlWindow, to_F32(opacity) / 255) != -1) {
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
    SDL_SetWindowBordered(_sdlWindow, state ? SDL_TRUE : SDL_FALSE);

    state ? _flags |= to_base( WindowFlags::DECORATED ) : _flags &= to_base( WindowFlags::DECORATED );
}

void DisplayWindow::hidden(const bool state) noexcept {
    if (((SDL_GetWindowFlags(_sdlWindow) & to_U32(SDL_WINDOW_SHOWN)) != 0u) == state)
    {
        if (state)
        {
            SDL_HideWindow(_sdlWindow);
        }
        else
        {
            SDL_ShowWindow(_sdlWindow);
        }
    }

    state ? _flags |= to_base( WindowFlags::HIDDEN ) : _flags &= to_base( WindowFlags::HIDDEN );
}

void DisplayWindow::restore() noexcept {
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

void DisplayWindow::maximized(const bool state) noexcept {
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

bool DisplayWindow::grabState() const noexcept {
    return SDL_GetWindowGrab(_sdlWindow) == SDL_TRUE;
}

void DisplayWindow::grabState(const bool state) const noexcept {
    SDL_SetWindowGrab(_sdlWindow, state ? SDL_TRUE : SDL_FALSE);
}

void DisplayWindow::handleChangeWindowType(const WindowType newWindowType) {
    if (_type == newWindowType) {
        return;
    }

    _previousType = _type;
    _type = newWindowType;
    I32 switchState = -1;

    grabState(false);
    switch (newWindowType) {
        case WindowType::WINDOW: {
            switchState = SDL_SetWindowFullscreen(_sdlWindow, 0);
            assert(switchState >= 0);
            decorated(true);
        } break;
        case WindowType::FULLSCREEN_WINDOWED: {
            switchState = SDL_SetWindowFullscreen(_sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
            assert(switchState >= 0);
            decorated(false);
            centerWindowPosition();
        } break;
        case WindowType::FULLSCREEN: {
            switchState = SDL_SetWindowFullscreen(_sdlWindow, SDL_WINDOW_FULLSCREEN);
            assert(switchState >= 0);
            decorated(false);
            grabState(true);
            centerWindowPosition();
        } break;
        default: break;
    };

    SDLEventManager::pollEvents();
}

vec2<U16> DisplayWindow::getPreviousDimensions() const noexcept {
    if (fullscreen()) {
        return WindowManager::GetFullscreenResolution();
    }
    return _prevDimensions;
}

bool DisplayWindow::setDimensions(U16 width, U16 height) {
    const vec2<U16> dim = getDimensions();
    if (dim == vec2<U16>(width, height)) {
        return true;
    }

    _internalResizeEvent = true;

    I32 newW = to_I32(width);
    I32 newH = to_I32(height);
    switch(_type) {
        case WindowType::FULLSCREEN: {
            // Find a decent resolution close to our dragged dimensions
            SDL_DisplayMode mode = {}, closestMode = {};
            SDL_GetCurrentDisplayMode(currentDisplayIndex(), &mode);
            mode.w = width;
            mode.h = height;
            SDL_GetClosestDisplayMode(currentDisplayIndex(), &mode, &closestMode);
            width = to_U16(closestMode.w);
            height = to_U16(closestMode.h);
            SDL_SetWindowDisplayMode(_sdlWindow, &closestMode);
        } break;
        case WindowType::FULLSCREEN_WINDOWED: //fall-through
            changeType(WindowType::WINDOW);
        case WindowType::WINDOW: [[fallthrough]];
        default:
        {
            maximized(false);
            SDL_SetWindowSize(_sdlWindow, newW, newH);
            SDL_GetWindowSize(_sdlWindow, &newW, &newH);
        } break;
    }

    SDLEventManager::pollEvents();

    if (newW == width && newH == height) {
        _prevDimensions.set(dim);
        return true;
    }

    return false;
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
