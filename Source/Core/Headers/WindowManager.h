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
#ifndef DVD_CORE_WINDOW_MANAGER_H_
#define DVD_CORE_WINDOW_MANAGER_H_

#include "Platform/Headers/DisplayWindow.h"
#include "Utility/Headers/Colours.h"

namespace Divide {

enum class WindowMode : U8
{
    WINDOWED = 0,
    BORDERLESS_WINDOWED,
    FULLSCREEN
};

enum class RenderAPI : U8;
class PlatformContext;
struct WindowDescriptor;
struct SizeChangeParams;

class WindowManager final : NonCopyable {
public:
    struct MonitorData {
        Rect<I16> viewport{};
        Rect<I16> drawableArea{};
        F32 dpi{0.f};
    };

public:
    WindowManager() noexcept;
    ~WindowManager();

    void hideAll() noexcept;

    DisplayWindow* createWindow(const WindowDescriptor& descriptor, ErrorCode& err);
    bool destroyWindow(DisplayWindow*& window);

    void drawToWindow( DisplayWindow& window );
    void flushWindow();

    void toggleFullScreen() const;
    void increaseResolution();
    void decreaseResolution();
    void stepResolution( bool increment );

    static bool SetGlobalCursorPosition(I32 x, I32 y) noexcept;
    static float2 GetCursorPosition() noexcept;
    static float2 GetGlobalCursorPosition() noexcept;
    static U32 GetMouseState(float2& pos, bool global) noexcept;
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

    [[nodiscard]] inline const vector<MonitorData>& monitorData() const noexcept;

    static vec2<U16> GetFullscreenResolution() noexcept;

    static void CaptureMouse(bool state) noexcept;

    static void SetCursorStyle(CursorStyle style);

    static void ToggleRelativeMouseMode(const DisplayWindow* window, bool state) noexcept;
    static bool IsRelativeMouseMode(const DisplayWindow* window) noexcept;

    static bool SetCursorPosition(const DisplayWindow* window, I32 x, I32 y) noexcept;
    static void SnapCursorToCenter(const DisplayWindow* window);

    [[nodiscard]] DisplayWindow* activeWindow() const noexcept;
    /// Returns the total number of active windows after the push
    size_t pushActiveWindow( DisplayWindow* window );
    /// Returns the remaining number of windows after the pop
    size_t popActiveWindow();

    POINTER_R(DisplayWindow, mainWindow, nullptr);
    PROPERTY_R(SDL_DisplayMode, fullscreenMode);

protected:
    friend class Application;

    // Can be called at startup directly
    ErrorCode init(PlatformContext& context,
                   RenderAPI renderingAPI,
                   vec2<I16> initialPosition,
                   vec2<U16> initialSize,
                   WindowMode windowMode,
                   I32 targetDisplayIndex);

    void close();

    bool onWindowSizeChange(const DisplayWindow::WindowEventArgs& args);
    bool onResolutionChange(const SizeChangeParams& params);

protected:
    struct APISettings
    {
        bool _isolateGraphicsContext{ true }; //< If true, we'll try and isolate the graphics context from others on the GPU for extra robustness at a possible slight performance cost (See GL_ARB_robustness_isolation)
        bool _createDebugContext{ !Config::Build::IS_RELEASE_BUILD && Config::ENABLE_GPU_VALIDATION };
        bool _enableCompatibilityLayer{ false };
        bool _requestRobustContext{ true };
    };
    friend class DisplayWindow;
    [[nodiscard]] ErrorCode findAndApplyAPISettings(const PlatformContext& context, const WindowDescriptor& descriptor);
    [[nodiscard]] ErrorCode applyAPISettingsPreCreate(const PlatformContext& context, RenderAPI api);
    [[nodiscard]] ErrorCode applyAPISettingsPostCreate( const PlatformContext& context, RenderAPI api, DisplayWindow* targetWindow );

    static void DestroyAPISettings(DisplayWindow* window) noexcept;

protected:
    I64 _mainWindowGUID{ -1 };
    std::pair<vec2<U16>, bool> _resolutionChangeQueued;
    PlatformContext* _context{ nullptr };
    vector<MonitorData> _monitors;
    vector<std::unique_ptr<DisplayWindow>> _windows;
    static const SDL_DisplayMode* s_mainDisplayMode;
    static std::array<SDL_Cursor*, to_base(CursorStyle::COUNT)> s_cursors;
    eastl::stack<DisplayWindow*> _activeWindows;
    APISettings _apiSettings{};
    
};

struct WindowDescriptor
{
    enum class Flags : U16
    {
        FULLSCREEN = toBit( 1 ),
        FULLSCREEN_DESKTOP = toBit( 2 ),
        DECORATED = toBit( 3 ),
        RESIZEABLE = toBit( 4 ),
        HIDDEN = toBit( 5 ),
        ALLOW_HIGH_DPI = toBit( 6 ),
        ALWAYS_ON_TOP = toBit( 7 ),
        VSYNC = toBit( 8 ),
        NO_TASKBAR_ICON = toBit( 9 )
    };

    string title = "";
    DisplayWindow* parentWindow = nullptr;
    vec2<I16> position = {};
    vec2<U16> dimensions = {};
    U32 targetDisplay = 0u;
    U16 flags = to_U16( to_base( Flags::DECORATED ) |
                        to_base( Flags::RESIZEABLE ) |
                        to_base( Flags::ALLOW_HIGH_DPI ) );
    RenderAPI targetAPI;
    bool externalClose{ false };
    bool startMaximized{ false };
};


struct SizeChangeParams
{
    /// Window GUID
    I64 winGUID{ -1 };
    /// The new width and height
    U16 width{ 0u };
    U16 height{ 0u };
    /// Is the window that fired the event fullscreen?
    bool isFullScreen{ false };
    bool isMainWindow{ false };
};

} //namespace Divide
#endif //DVD_CORE_WINDOW_MANAGER_H_

#include "WindowManager.inl"
