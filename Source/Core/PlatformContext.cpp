

#include "Headers/PlatformContext.h"
#include "Headers/Configuration.h"

#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Utility/Headers/Localization.h"

#include "Core/Debugging/Headers/DebugInterface.h"
#include "Core/Networking/Headers/LocalClient.h"
#include "Core/Networking/Headers/Server.h"
#include "Editor/Headers/Editor.h"
#include "GUI/Headers/GUI.h"
#include "Physics/Headers/PXDevice.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/Input/Headers/InputHandler.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

PlatformContext::PlatformContext(Application& app, Kernel& kernel)
  : _app(app)
  , _kernel(kernel)
  , _taskPool{}
  , _paramHandler(MemoryManager_NEW ParamHandler())
  , _config(MemoryManager_NEW Configuration())         // XML based configuration
  , _debug(MemoryManager_NEW DebugInterface())         // Debug Interface
  , _inputHandler(MemoryManager_NEW Input::InputHandler(_kernel, _app))
  , _gfx(MemoryManager_NEW GFXDevice(*this))           // Video
  , _gui(MemoryManager_NEW GUI(_kernel))               // Audio
  , _sfx(MemoryManager_NEW SFXDevice(*this))           // Physics
  , _pfx(MemoryManager_NEW PXDevice(*this))            // Graphical User Interface
  , _client(MemoryManager_NEW LocalClient(_kernel))    // Network client
  , _server(MemoryManager_NEW Server())                // Network server
  , _editor(Config::Build::ENABLE_EDITOR ? MemoryManager_NEW Editor(*this) : nullptr)
{
    for (U8 i = 0u; i < to_U8(TaskPoolType::COUNT); ++i)
    {
        _taskPool[i] = MemoryManager_NEW TaskPool();
    }
}


PlatformContext::~PlatformContext()
{
    assert(_gfx == nullptr);
}

void PlatformContext::terminate()
{
    for ( U8 i = 0u; i < to_U32(TaskPoolType::COUNT); ++i)
    {
        MemoryManager::DELETE(_taskPool[i]);
    }

    MemoryManager::SAFE_DELETE(_editor);
    MemoryManager::DELETE(_server);
    MemoryManager::DELETE(_client);
    MemoryManager::DELETE(_pfx);
    MemoryManager::DELETE(_sfx);
    MemoryManager::DELETE(_gui);
    MemoryManager::DELETE(_gfx);
    MemoryManager::DELETE(_inputHandler);
    MemoryManager::DELETE(_debug);
    MemoryManager::DELETE(_config);
    MemoryManager::DELETE(_paramHandler);
}

void PlatformContext::idle(const bool fast, const U64 deltaTimeUSGame, const U64 deltaTimeUSApp )
{
    PROFILE_SCOPE_AUTO( Profiler::Category::IO );

    for (TaskPool* pool : _taskPool)
    {
        pool->flushCallbackQueue();
    }

    if (componentMask() & to_base(SystemComponentType::GFXDevice))
    {
        _gfx->idle(fast, deltaTimeUSGame, deltaTimeUSApp );
    }
    if (componentMask() & to_base(SystemComponentType::SFXDevice))
    {
        _sfx->idle();
    }
    if (componentMask() & to_base(SystemComponentType::PXDevice))
    {
        _pfx->idle();
    }
    if (componentMask() & to_base(SystemComponentType::DebugInterface))
    {
        _debug->idle(*this);
    }
    if constexpr(Config::Build::ENABLE_EDITOR)
    {
        if (componentMask() & to_base(SystemComponentType::Editor))
        {
            _editor->idle();
        }
    }
}

DisplayWindow& PlatformContext::mainWindow() noexcept
{
    return *app().windowManager().mainWindow();
}

const DisplayWindow& PlatformContext::mainWindow() const noexcept
{
    return *app().windowManager().mainWindow();
}

Kernel& PlatformContext::kernel() noexcept
{
    return _kernel;
}

const Kernel& PlatformContext::kernel() const noexcept
{
    return _kernel;
}

void PlatformContext::onThreadCreated(const TaskPoolType poolType, const std::thread::id& threadID, bool isMainRenderThread ) const
{
    if (poolType != TaskPoolType::LOW_PRIORITY)
    {
        _gfx->onThreadCreated(threadID, isMainRenderThread);
    }
}

}; //namespace Divide