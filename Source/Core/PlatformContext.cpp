

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

PlatformContext::PlatformContext(Application& app)
  : _app(app)
  , _paramHandler(MemoryManager_NEW ParamHandler())
  , _config(MemoryManager_NEW Configuration())
  , _debug(MemoryManager_NEW DebugInterface())
  , _server(MemoryManager_NEW Server())
{
    const char* taskPoolNames[] =
    {
        "WORKER_THREAD",
        "BACKUP_THREAD",
        "RENDERER",
        "ASSET_LOADER"
    };

    static_assert(std::size(taskPoolNames) == to_base(TaskPoolType::COUNT));

    for ( U8 i = 0u; i < to_U8( TaskPoolType::COUNT ); ++i )
    {
        _taskPool[i] = MemoryManager_NEW TaskPool(taskPoolNames[i]);
    }
}

PlatformContext::~PlatformContext()
{
    assert(_gfx == nullptr);
    for ( U8 i = 0u; i < to_U32( TaskPoolType::COUNT ); ++i )
    {
        MemoryManager::DELETE( _taskPool[i] );
    }
    MemoryManager::DELETE( _server );
    MemoryManager::DELETE( _paramHandler );
    MemoryManager::DELETE( _debug );
    MemoryManager::DELETE( _config );
}

void PlatformContext::init(Kernel& kernel)
{
    assert(_gfx == nullptr);

    _kernel = &kernel;

    _inputHandler = MemoryManager_NEW Input::InputHandler( kernel, _app );
    _gfx = MemoryManager_NEW GFXDevice(*this);
    _sfx = MemoryManager_NEW SFXDevice(*this);
    _pfx = MemoryManager_NEW PXDevice(*this);
    _gui =  MemoryManager_NEW GUI( kernel );
    _client = MemoryManager_NEW LocalClient( kernel );
    _editor = (Config::Build::ENABLE_EDITOR ? MemoryManager_NEW Editor(*this) : nullptr);

}

void PlatformContext::terminate()
{
    MemoryManager::SAFE_DELETE(_editor);
    MemoryManager::DELETE(_client);
    MemoryManager::DELETE(_gui);
    MemoryManager::DELETE(_pfx);
    MemoryManager::DELETE(_sfx);
    MemoryManager::DELETE(_gfx);
    MemoryManager::DELETE(_inputHandler);
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

DisplayWindow& PlatformContext::activeWindow() noexcept
{
    return *app().windowManager().activeWindow();
}

const DisplayWindow& PlatformContext::activeWindow() const noexcept
{
    return *app().windowManager().activeWindow();
}

Kernel& PlatformContext::kernel() noexcept
{
    assert( _kernel != nullptr );
    return *_kernel;
}

const Kernel& PlatformContext::kernel() const noexcept
{
    assert(_kernel != nullptr);
    return *_kernel;
}

void PlatformContext::onThreadCreated(const TaskPoolType poolType, const std::thread::id& threadID, bool isMainRenderThread ) const
{
    if (poolType == TaskPoolType::ASSET_LOADER ||
        poolType == TaskPoolType::RENDERER)
    {
        _gfx->onThreadCreated(threadID, isMainRenderThread);
    }
}

}; //namespace Divide
