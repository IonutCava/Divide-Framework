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

#ifndef DVD_CORE_WINDOW_MANAGER_INL_
#define DVD_CORE_WINDOW_MANAGER_INL_

namespace Divide {

inline DisplayWindow& WindowManager::getWindow(const I64 guid)
{
    for (DisplayWindow* win : _windows)
    {
        if (win->getGUID() == guid)
        {
            return *win;
        }
    }

    return *_windows.front();
}

inline const DisplayWindow& WindowManager::getWindow(const I64 guid) const
{
    for (const DisplayWindow* win : _windows)
    {
        if (win->getGUID() == guid)
        {
            return *win;
        }
    }

    return *_windows.front();
}

inline DisplayWindow* WindowManager::activeWindow() const noexcept
{
    return _activeWindows.empty() ? nullptr : _activeWindows.top();
}

inline size_t WindowManager::pushActiveWindow( DisplayWindow* window )
{
    _activeWindows.push(window);
    return _activeWindows.size();
}

inline size_t WindowManager::popActiveWindow()
{
    _activeWindows.pop();
    return _activeWindows.size();
}

inline DisplayWindow* WindowManager::getFocusedWindow() noexcept
{
    for (DisplayWindow* win : _windows)
    {
        if (win->hasFocus())
        {
            return win;
        }
    }

    return nullptr;
}

inline const DisplayWindow* WindowManager::getFocusedWindow() const noexcept
{
    for (const DisplayWindow* win : _windows)
    {
        if (win->hasFocus())
        {
            return win;
        }
    }

    return nullptr;
}

inline DisplayWindow* WindowManager::getHoveredWindow() noexcept
{
    for (DisplayWindow* win : _windows)
    {
        if (win->isHovered())
        {
            return win;
        }
    }

    return nullptr;
}

inline const DisplayWindow* WindowManager::getHoveredWindow() const noexcept
{
    for (const DisplayWindow* win : _windows)
    {
        if (win->isHovered())
        {
            return win;
        }
    }

    return nullptr;
}

inline DisplayWindow& WindowManager::getWindow(const U32 index)
{
    assert(index < to_U32(_windows.size()));
    return *_windows[index];
}

inline const DisplayWindow& WindowManager::getWindow(const U32 index) const
{
    assert(index < to_U32(_windows.size()));
    return *_windows[index];
}

DisplayWindow* WindowManager::getWindowByID(const U32 ID) noexcept
{
    for (DisplayWindow* win : _windows)
    {
        if (win->_windowID == ID)
        {
            return win;
        }
    }

    return nullptr;
}

const DisplayWindow* WindowManager::getWindowByID(const U32 ID) const noexcept
{
    for (const DisplayWindow* win : _windows)
    {
        if (win->_windowID == ID)
        {
            return win;
        }
    }

    return nullptr;
}

inline const vector<WindowManager::MonitorData>& WindowManager::monitorData() const noexcept
{
    return _monitors;
}

} //namespace Divide

#endif //DVD_CORE_WINDOW_MANAGER_INL_
