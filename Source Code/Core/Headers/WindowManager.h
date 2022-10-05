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
#ifndef _CORE_WINDOW_MANAGER_H_
#define _CORE_WINDOW_MANAGER_H_

#include "Platform/Headers/DisplayWindow.h"
#include "Utility/Headers/Colours.h"

namespace Divide {

enum class RenderAPI : U8;

enum class WindowMode : U8 {
    WINDOWED = 0,
    BORDERLESS_WINDOWED,
    FULLSCREEN
};

struct WindowDescriptor {
    enum class Flags : U16 {
        FULLSCREEN = toBit(1),
        FULLSCREEN_DESKTOP = toBit(2),
        DECORATED = toBit(3),
        RESIZEABLE = toBit(4),
        HIDDEN = toBit(5),
        ALLOW_HIGH_DPI = toBit(6),
        ALWAYS_ON_TOP = toBit(7),
        VSYNC = toBit(8),
        SHARE_CONTEXT = toBit(9)
    };

    string title = "";
    vec2<I16> position = {};
    vec2<U16> dimensions = {};
    U32 targetDisplay = 0u;
    U16 flags = to_U16(to_base(Flags::DECORATED) |
                       to_base(Flags::RESIZEABLE) |
                       to_base(Flags::ALLOW_HIGH_DPI));
    RenderAPI targetAPI;
    bool externalClose = false;
    bool startMaximized = false;
};

class PlatformContext;
class WindowManager final : public SDLEventListener, NonCopyable {
public:
    struct MonitorData {
        Rect<I16> viewport;
        Rect<I16> drawableArea;
        F32 dpi = 0.f;
    };

public:
    WindowManager() noexcept;
    ~WindowManager();

    // Can be called at startup directly
    ErrorCode init(PlatformContext& context,
                   RenderAPI renderingAPI,
                   vec2<I16> initialPosition,
                   vec2<U16> initialSize,
                   WindowMode windowMode,
                   I32 targetDisplayIndex);
    // Needs to be called after the graphics system has fully initialized
    void postInit();

    void close();
    void hideAll() noexcept;

    void update(U64 deltaTimeUS);

    [[nodiscard]] bool anyWindowFocus() const noexcept;

    inline DisplayWindow* createWindow(const WindowDescriptor& descriptor, ErrorCode& err);

    DisplayWindow* createWindow(const WindowDescriptor& descriptor, ErrorCode& err, U32& windowIndex);
    bool destroyWindow(DisplayWindow*& window);

    bool setCursorPosition(I32 x, I32 y) noexcept;
    void snapCursorToCenter();

    static bool SetGlobalCursorPosition(I32 x, I32 y) noexcept;
    static vec2<I32> GetCursorPosition() noexcept;
    static vec2<I32> GetGlobalCursorPosition() noexcept;
    static U32 GetMouseState(vec2<I32>& pos, bool global) noexcept;
    static void SetCaptureMouse(bool state) noexcept;

    //Returns null if no window is currently focused
    inline DisplayWindow* getFocusedWindow() noexcept;
    [[nodiscard]] inline const DisplayWindow* getFocusedWindow() const noexcept;

    //Returns null if no window is currently hovered
    inline DisplayWindow* getHoveredWindow() noexcept;
    [[nodiscard]] inline const DisplayWindow* getHoveredWindow() const noexcept;

    inline DisplayWindow& getWindow(I64 guid);
    [[nodiscard]] inline const DisplayWindow& getWindow(I64 guid) const;

    inline DisplayWindow& getWindow(U32 index);
    [[nodiscard]] inline const DisplayWindow& getWindow(U32 index) const;

    inline DisplayWindow* getWindowByID(U32 ID) noexcept;
    [[nodiscard]] inline const DisplayWindow* getWindowByID(U32 ID) const noexcept;

    [[nodiscard]] inline U32 getWindowCount() const noexcept;

    [[nodiscard]] inline const vector<MonitorData>& monitorData() const noexcept;

    static vec2<U16> GetFullscreenResolution() noexcept;

    static void CaptureMouse(bool state) noexcept;

    static void SetCursorStyle(CursorStyle style);

    static void ToggleRelativeMouseMode(bool state) noexcept;
    static bool IsRelativeMouseMode() noexcept;

    POINTER_R(DisplayWindow, mainWindow, nullptr);

protected:
    bool onSDLEvent(SDL_Event event) noexcept override;

protected:
    friend class DisplayWindow;
    [[nodiscard]] static U32 CreateAPIFlags(RenderAPI api) noexcept;
    [[nodiscard]] ErrorCode configureAPISettings(RenderAPI api, U16 descriptorFlags) const;
    [[nodiscard]] ErrorCode applyAPISettings(RenderAPI api, DisplayWindow* window);
    static void DestroyAPISettings(DisplayWindow* window) noexcept;

protected:
    U32 _apiFlags{ 0 };
    I64 _mainWindowGUID{ -1 };

    PlatformContext* _context{ nullptr };
    vector<MonitorData> _monitors;
    vector<DisplayWindow*> _windows;
    DisplayWindow::UserData _globalUserData{};
    static std::array<SDL_Cursor*, to_base(CursorStyle::COUNT)> s_cursors;
};
} //namespace Divide
#endif //_CORE_WINDOW_MANAGER_H_

#include "WindowManager.inl"