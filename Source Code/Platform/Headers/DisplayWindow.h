/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _DISPLAY_WINDOW_H_
#define _DISPLAY_WINDOW_H_

#include "Core/Headers/PlatformContextComponent.h"

#include "Platform/Headers/SDLEventListener.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

using SDL_Window = struct SDL_Window;

namespace Divide {

enum class WindowType : U8 {
    WINDOW = 0,
    FULLSCREEN = 1,
    FULLSCREEN_WINDOWED = 2,
    COUNT
};

enum class CursorStyle : U8 {
    NONE = 0,
    ARROW,
    TEXT_INPUT,
    HAND,
    RESIZE_ALL,
    RESIZE_NS,
    RESIZE_EW,
    RESIZE_NESW,
    RESIZE_NWSE,
    COUNT
};

enum class WindowEvent : U8 {
    HIDDEN = 0,
    SHOWN = 1,
    MINIMIZED = 2,
    MAXIMIZED = 3,
    RESTORED = 4,
    LOST_FOCUS = 5,
    GAINED_FOCUS = 6,
    MOUSE_HOVER_ENTER = 7,
    MOUSE_HOVER_LEAVE = 8,
    RESIZED = 9,
    SIZE_CHANGED = 10,
    MOVED = 11,
    APP_LOOP = 12,
    CLOSE_REQUESTED = 13,
    COUNT
};

enum class WindowFlags : U16 {
    VSYNC = toBit(1),
    HAS_FOCUS = toBit(2),
    IS_HOVERED = toBit(3),
    MINIMIZED = toBit(4),
    MAXIMIZED = toBit(5),
    HIDDEN = toBit(6),
    DECORATED = toBit(7),
    OWNS_RENDER_CONTEXT = toBit(8), //BAD
    COUNT = 8
};

class WindowManager;
class PlatformContext;

struct WindowDescriptor;

enum class ErrorCode : I8;

// Platform specific window
class DisplayWindow final : public GUIDWrapper,
                            public PlatformContextComponent,
                            public SDLEventListener {
public:

    struct UserData {
        SDL_GLContext _glContext{ nullptr };
    };

    struct WindowEventArgs {
        I64 _windowGUID = -1;
        bool _flag = false;
        Input::KeyCode _key = Input::KeyCode::KC_UNASSIGNED;
        const char* _text = nullptr;
        I32 _mod = 0;
        I32 x = -1, y = -1;
        I32 id = -1;
    };

    using EventListener = DELEGATE<bool, const WindowEventArgs&>;

    virtual ~DisplayWindow();

protected:
    SET_SAFE_DELETE_FRIEND
    SET_DELETE_CONTAINER_FRIEND

    friend class WindowManager;
    DisplayWindow(WindowManager& parent, PlatformContext& context);

public:
    ErrorCode init(U32 windowFlags,
                   WindowType initialType,
                   const WindowDescriptor& descriptor);

    ErrorCode destroyWindow();

    [[nodiscard]] inline SDL_Window* getRawWindow() const noexcept;

    [[nodiscard]] I32 currentDisplayIndex() const noexcept;

    [[nodiscard]] inline bool isHovered() const noexcept;
    [[nodiscard]] inline bool hasFocus() const noexcept;

    [[nodiscard]] inline bool minimized() const noexcept;
                         void minimized(bool state) noexcept;

    [[nodiscard]] inline bool maximized() const noexcept;
                         void maximized(bool state) noexcept;

    [[nodiscard]] inline bool hidden() const noexcept;
                         void hidden(bool state) noexcept;

    [[nodiscard]] inline bool decorated() const noexcept;
                         void decorated(bool state) noexcept;

    [[nodiscard]] inline bool fullscreen() const noexcept;

    [[nodiscard]] inline WindowType type() const noexcept;
    inline void changeType(WindowType newType);
    inline void changeToPreviousType();

                         void opacity(U8 opacity) noexcept;
    [[nodiscard]] inline U8   opacity() const noexcept;
    [[nodiscard]] inline U8   prevOpacity() const noexcept;

    /// width and height get adjusted to the closest supported value
    [[nodiscard]] bool setDimensions(U16 width, U16 height);
    [[nodiscard]] bool setDimensions(vec2<U16> dimensions);

    /// Centering is also easier via SDL
    void centerWindowPosition();

    void bringToFront() const noexcept;

    [[nodiscard]] vec2<U16> getDimensions() const noexcept;
    [[nodiscard]] vec2<U16> getPreviousDimensions() const noexcept;

    [[nodiscard]] Rect<I32> getBorderSizes() const noexcept;
    [[nodiscard]] vec2<U16> getDrawableSize() const noexcept;
    [[nodiscard]] vec2<I32> getPosition(bool global = false, bool offset = false) const;

           void setPosition(I32 x, I32 y, bool global = false, bool offset = false);
    inline void setPosition(vec2<I32> position, bool global = false);

    [[nodiscard]] inline const char* title() const noexcept;
    template<typename... Args>
    void title(const char* format, Args&& ...args) noexcept;

    [[nodiscard]] WindowHandle handle() const noexcept;

    inline void addEventListener(WindowEvent windowEvent, const EventListener& listener);
    inline void clearEventListeners(WindowEvent windowEvent);

    void notifyListeners(WindowEvent event, const WindowEventArgs& args);

    inline void destroyCbk(const DELEGATE<void>& destroyCbk);

    [[nodiscard]] inline Rect<I32> windowViewport() const noexcept;

    [[nodiscard]] inline const Rect<I32>& renderingViewport() const noexcept;
    void renderingViewport(const Rect<I32>& viewport) noexcept;

    [[nodiscard]] inline UserData* userData() const noexcept;

    [[nodiscard]] bool grabState() const noexcept;
    void grabState(bool state) const noexcept;

    [[nodiscard]] bool onSDLEvent(SDL_Event event) override;


    [[nodiscard]] GFX::CommandBuffer* getCurrentCommandBuffer();

    /// The display on which this window was initially created on
    PROPERTY_R(U32, initialDisplay, 0u);
    PROPERTY_R(U32, flags, 0u);

private:
    void restore() noexcept;
    /// Changing from one window type to another
    /// should also change display dimensions and position
    void handleChangeWindowType(WindowType newWindowType);
    [[nodiscard]] void updateDrawableSize() noexcept;

private:
    using EventListeners = vector<DELEGATE<bool, WindowEventArgs>>;
    std::array<EventListeners, to_base(WindowEvent::COUNT)> _eventListeners;
    DELEGATE<void> _destroyCbk;

    FColour4  _clearColour;
    Rect<I32> _renderingViewport;

    vec2<U16> _prevDimensions;
    vec2<U16> _drawableSize;
    Uint32 _windowID = 0u;

    WindowManager& _parent;
    SDL_Window* _sdlWindow = nullptr;
    UserData* _userData = nullptr;

    /// The current rendering window type
    WindowType _type = WindowType::COUNT;
    WindowType _previousType = WindowType::COUNT;
    U8 _opacity = 255u;
    U8 _prevOpacity = 255u;
    
    std::array<GFX::CommandBuffer, Config::MAX_FRAMES_IN_FLIGHT> _commandBuffers;
    /// Did we generate the window move event?
    bool _internalMoveEvent = false;
    bool _internalResizeEvent = false;

    static I64 s_cursorWindowGUID;

}; //DisplayWindow

}; //namespace Divide

#include "DisplayWindow.inl"

#endif //_DISPLAY_WINDOW_H_

