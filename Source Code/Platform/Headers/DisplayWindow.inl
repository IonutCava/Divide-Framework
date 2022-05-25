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
#ifndef _DISPLAY_WINDOW_INL_
#define _DISPLAY_WINDOW_INL_

#include "Core/Headers/StringHelper.h"

namespace Divide {

    inline SDL_Window* DisplayWindow::getRawWindow() const noexcept {
        return _sdlWindow;
    }

    inline bool DisplayWindow::fullscreen() const noexcept {
        return type() == WindowType::FULLSCREEN ||
               type() == WindowType::FULLSCREEN_WINDOWED;
    }

    inline void DisplayWindow::setPosition(const vec2<I32>& position, const bool global) {
        setPosition(position.x, position.y, global);
    }

    inline bool DisplayWindow::isHovered() const noexcept {
        return BitCompare(_flags, WindowFlags::IS_HOVERED);
    }

    inline bool DisplayWindow::hasFocus() const noexcept {
        return BitCompare(_flags, WindowFlags::HAS_FOCUS);
    }

    inline U8 DisplayWindow::opacity() const noexcept {
        return _opacity;
    }

    inline U8 DisplayWindow::prevOpacity() const noexcept {
        return _prevOpacity;
    }

    inline void DisplayWindow::clearColour(const FColour4& colour) noexcept {
        _clearColour.set(colour);
    }

    void DisplayWindow::clearFlags(const bool clearColour, const bool clearDepth)noexcept {
        ToggleBit(_flags, WindowFlags::CLEAR_COLOUR, clearColour);
        ToggleBit(_flags, WindowFlags::CLEAR_DEPTH, clearDepth);
    }

    inline const FColour4& DisplayWindow::clearColour() const noexcept {
        bool shouldClearColour, shouldClearDepth;
        return clearColour(shouldClearColour, shouldClearDepth);
    }

    inline const FColour4& DisplayWindow::clearColour(bool &clearColour, bool &clearDepth) const noexcept {
        clearColour = BitCompare(_flags, WindowFlags::CLEAR_COLOUR);
        clearDepth = BitCompare(_flags, WindowFlags::CLEAR_DEPTH);
        return _clearColour;
    }

    inline bool DisplayWindow::minimized() const noexcept {
        return BitCompare(_flags, WindowFlags::MINIMIZED);
    }

    inline bool DisplayWindow::maximized() const noexcept {
        return BitCompare(_flags, WindowFlags::MAXIMIZED);
    }

    inline bool DisplayWindow::decorated() const noexcept {
        return BitCompare(_flags, WindowFlags::DECORATED);
    }

        inline bool DisplayWindow::hidden() const noexcept {
        return BitCompare(_flags, WindowFlags::HIDDEN);
    }

    inline WindowType DisplayWindow::type() const noexcept {
        return _type;
    }

    inline void DisplayWindow::changeType(const WindowType newType) {
        if (newType != _type) {
            _queuedType = newType;
            handleChangeWindowType(_queuedType);
        }
    }

    inline void DisplayWindow::changeToPreviousType() {
        changeType(_previousType);
    }

    inline const char* DisplayWindow::title() const noexcept {
        return SDL_GetWindowTitle(_sdlWindow);
    }

    inline void DisplayWindow::addEventListener(const WindowEvent windowEvent, const EventListener& listener) {
        _eventListeners[to_base(windowEvent)].push_back(listener);
    }

    inline void DisplayWindow::clearEventListeners(const WindowEvent windowEvent) {
        _eventListeners[to_base(windowEvent)].clear();
    }

    inline void DisplayWindow::destroyCbk(const DELEGATE<void>& destroyCbk) {
        _destroyCbk = destroyCbk;
    }

    inline Rect<I32> DisplayWindow::windowViewport() const noexcept {
        const vec2<U16>& dim = getDimensions();
        return Rect<I32>(0, 0, to_I32(dim.width), to_I32(dim.height));
    }

    inline const Rect<I32>& DisplayWindow::renderingViewport() const noexcept {
        return _renderingViewport;
    }

    inline DisplayWindow::UserData* DisplayWindow::userData() const noexcept {
        return _userData;
    }

    template<typename... Args>
    void DisplayWindow::title(const char* format, Args&& ...args) noexcept {
        if_constexpr(sizeof...(Args) > 0) {
            SDL_SetWindowTitle(_sdlWindow, Util::StringFormat(format, static_cast<Args&&>(args)...).c_str());
        } else {
            SDL_SetWindowTitle(_sdlWindow, format);
        }
    }
}; //namespace Divide

#endif //_DISPLAY_WINDOW_INL_