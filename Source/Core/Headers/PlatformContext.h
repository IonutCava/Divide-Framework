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
#ifndef DVD_PLATFORM_CONTEXT_H_
#define DVD_PLATFORM_CONTEXT_H_

namespace Divide {

class GUI;
class Kernel;
class Server;
class Editor;
class GFXDevice;
class SFXDevice;
class PXDevice;
class Application;
class LocalClient;
class ParamHandler;
class DisplayWindow;

struct Configuration;
struct DebugInterface;

namespace Attorney {
    class PlatformContextKernel;
};

namespace Input {
    class InputHandler;
};

enum class TaskPoolType : U8
{
    HIGH_PRIORITY = 0,
    LOW_PRIORITY,
    COUNT
};

class PlatformContext final {
    friend class Attorney::PlatformContextKernel;

 public:
    enum class SystemComponentType : U32 {
        NONE = 0,
        Application = 1 << 1,
        GFXDevice = 1 << 2,
        SFXDevice = 1 << 3,
        PXDevice = 1 << 4,
        GUI = 1 << 5,
        XMLData = 1 << 6,
        Configuration = 1 << 7,
        LocalClient = 1 << 8,
        DebugInterface = 1 << 9,
        Editor = 1 << 10,
        InputHandler = 1 << 11,
        COUNT = 11,
        ALL = Application | GFXDevice | SFXDevice | PXDevice | GUI | XMLData |
              Configuration | LocalClient | DebugInterface | Editor | InputHandler
    };

 public:
    explicit PlatformContext(Application& app, Kernel& kernel);
    ~PlatformContext();

    void idle(bool fast = true, U64 deltaTimeUSGame = 0u, U64 deltaTimeUSApp = 0u );

    void terminate();

    [[nodiscard]] Application& app()  noexcept { return _app; }
    [[nodiscard]] const Application& app() const noexcept { return _app; }

    [[nodiscard]] GFXDevice& gfx() noexcept { return *_gfx; }
    [[nodiscard]] const GFXDevice& gfx() const noexcept { return *_gfx; }

    [[nodiscard]] GUI& gui() noexcept { return *_gui; }
    [[nodiscard]] const GUI& gui() const noexcept { return *_gui; }

    [[nodiscard]] SFXDevice& sfx() noexcept { return *_sfx; }
    [[nodiscard]] const SFXDevice& sfx() const noexcept { return *_sfx; }

    [[nodiscard]] PXDevice& pfx() noexcept { return *_pfx; }
    [[nodiscard]] const PXDevice& pfx() const noexcept { return *_pfx; }

    [[nodiscard]] Configuration& config() noexcept { return *_config; }
    [[nodiscard]] const Configuration& config() const noexcept { return *_config; }

    [[nodiscard]] LocalClient& client() noexcept { return *_client; }
    [[nodiscard]] const LocalClient& client() const noexcept { return *_client; }

    [[nodiscard]] Server& server() noexcept { return *_server; }
    [[nodiscard]] const Server& server() const noexcept { return *_server; }

    [[nodiscard]] DebugInterface& debug() noexcept { return *_debug; }
    [[nodiscard]] const DebugInterface& debug() const noexcept { return *_debug; }

    [[nodiscard]] Editor& editor() noexcept { return *_editor; }
    [[nodiscard]] const Editor& editor() const noexcept { return *_editor; }

    [[nodiscard]] TaskPool& taskPool(const TaskPoolType type) noexcept {return *_taskPool[to_base(type)]; }
    [[nodiscard]] const TaskPool& taskPool(const TaskPoolType type) const noexcept { return *_taskPool[to_base(type)]; }

    [[nodiscard]] Input::InputHandler& input() noexcept { return *_inputHandler; }
    [[nodiscard]] const Input::InputHandler& input() const noexcept { return *_inputHandler; }

    [[nodiscard]] ParamHandler& paramHandler() noexcept { return *_paramHandler; }
    [[nodiscard]] const ParamHandler& paramHandler() const noexcept { return *_paramHandler; }

    [[nodiscard]] Kernel& kernel() noexcept;
    [[nodiscard]] const Kernel& kernel() const noexcept;

    [[nodiscard]] DisplayWindow& mainWindow() noexcept;
    [[nodiscard]] const DisplayWindow& mainWindow() const noexcept;

    PROPERTY_RW(U32, componentMask, 0u);

  protected:
    void onThreadCreated(const TaskPoolType type, const std::thread::id& threadID, bool isMainRenderThread) const;

  private:
    /// Main application instance
    Application& _app;
    /// Main app's kernel
    Kernel& _kernel;

    /// Task pools
    std::array<TaskPool*, to_base(TaskPoolType::COUNT)> _taskPool;
    /// Param handler
    ParamHandler* _paramHandler{ nullptr };
    /// User configured settings
    Configuration* _config{ nullptr };
    /// Debugging interface: read only / editable variables
    DebugInterface* _debug{ nullptr };
    /// Input handler
    Input::InputHandler* _inputHandler{ nullptr };
    /// Access to the GPU
    GFXDevice* _gfx{ nullptr };
    /// The graphical user interface
    GUI* _gui{ nullptr };
    /// Access to the audio device
    SFXDevice* _sfx{ nullptr };
    /// Access to the physics system
    PXDevice* _pfx{ nullptr };
    /// Networking client
    LocalClient* _client{ nullptr };
    /// Networking server
    Server* _server{ nullptr };
    /// Game editor
    Editor* _editor{ nullptr };
};

namespace Attorney
{
    class PlatformContextKernel
    {
        static void onThreadCreated(const PlatformContext& context, const TaskPoolType poolType, const std::thread::id& threadID, const bool isMainRenderThread )
        {
            context.onThreadCreated(poolType, threadID, isMainRenderThread);
        }

        friend class Divide::Kernel;
        friend void Divide::PlatformContextIdleCall();
    };
};  // namespace Attorney

}; //namespace Divide

#endif //DVD_PLATFORM_CONTEXT_H_
