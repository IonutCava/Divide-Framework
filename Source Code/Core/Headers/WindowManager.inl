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

#ifndef _CORE_WINDOW_MANAGER_INL_
#define _CORE_WINDOW_MANAGER_INL_

namespace Divide {

inline DisplayWindow* WindowManager::createWindow(const WindowDescriptor& descriptor) {
    ErrorCode err = ErrorCode::NO_ERR;
    return createWindow(descriptor, err);
}
inline DisplayWindow* WindowManager::createWindow(const WindowDescriptor& descriptor, ErrorCode& err) {
    U32 idx = 0;
    return createWindow(descriptor, err, idx);
}

inline U32 WindowManager::getWindowCount() const noexcept {
    return to_U32(_windows.size());
}

inline DisplayWindow& WindowManager::getWindow(const I64 guid) {
    for (DisplayWindow* win : _windows) {
        if (win->getGUID() == guid) {
            return *win;
        }
    }

    return *_windows.front();
}

inline const DisplayWindow& WindowManager::getWindow(const I64 guid) const {
    for (const DisplayWindow* win : _windows) {
        if (win->getGUID() == guid) {
            return *win;
        }
    }

    return *_windows.front();
}

inline DisplayWindow* WindowManager::getFocusedWindow() noexcept {
    for (DisplayWindow* win : _windows) {
        if (win->hasFocus()) {
            return win;
        }
    }

    return nullptr;
}

inline const DisplayWindow* WindowManager::getFocusedWindow() const noexcept {
    for (const DisplayWindow* win : _windows) {
        if (win->hasFocus()) {
            return win;
        }
    }

    return nullptr;
}

inline DisplayWindow* WindowManager::getHoveredWindow() noexcept {
    for (DisplayWindow* win : _windows) {
        if (win->isHovered()) {
            return win;
        }
    }

    return nullptr;
}

inline const DisplayWindow* WindowManager::getHoveredWindow() const noexcept {
    for (const DisplayWindow* win : _windows) {
        if (win->isHovered()) {
            return win;
        }
    }

    return nullptr;
}

inline DisplayWindow& WindowManager::getWindow(const U32 index) {
    assert(index < to_U32(_windows.size()));
    return *_windows[index];
}

inline const DisplayWindow& WindowManager::getWindow(const U32 index) const {
    assert(index < to_U32(_windows.size()));
    return *_windows[index];
}

DisplayWindow* WindowManager::getWindowByID(const U32 ID) noexcept {
    for (DisplayWindow* win : _windows) {
        if (win->_windowID == ID) {
            return win;
        }
    }

    return nullptr;
}

const DisplayWindow* WindowManager::getWindowByID(const U32 ID) const noexcept {
    for (const DisplayWindow* win : _windows) {
        if (win->_windowID == ID) {
            return win;
        }
    }

    return nullptr;
}

inline const vector<WindowManager::MonitorData>& WindowManager::monitorData() const noexcept {
    return _monitors;
}

} //namespace Divide

#endif //_CORE_WINDOW_MANAGER_INL_
