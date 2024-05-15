

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
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

PlatformContext::PlatformContext(Application& app)
  : _app(app)
  , _paramHandler(std::make_unique<ParamHandler>())
  , _config(std::make_unique<Configuration>())
  , _debug(std::make_unique<DebugInterface>())
  , _server(std::make_unique<Server>())
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
        _taskPool[i] = std::make_unique<TaskPool>(taskPoolNames[i]);
    }
}

PlatformContext::~PlatformContext()
{
    assert(_gfx == nullptr);
}

void PlatformContext::init(Kernel& kernel)
{
    assert(_gfx == nullptr);

    _kernel = &kernel;

    _inputHandler = std::make_unique<Input::InputHandler>( kernel, _app );
    _gfx = std::make_unique<GFXDevice>(*this);
    _sfx = std::make_unique<SFXDevice>(*this);
    _pfx = std::make_unique<PXDevice>(*this);
    _gui =  std::make_unique<GUI>( kernel );
    _client = std::make_unique<LocalClient>( kernel );
    _editor = (Config::Build::ENABLE_EDITOR ? std::make_unique<Editor>(*this) : nullptr);
}

void PlatformContext::terminate()
{
    _editor.reset();
    _client.reset();
    _gui.reset();
    _pfx.reset();
    _sfx.reset();
    _gfx.reset();
    _inputHandler.reset();
}

void PlatformContext::idle(const bool fast, const U64 deltaTimeUSGame, const U64 deltaTimeUSApp )
{
    PROFILE_SCOPE_AUTO( Profiler::Category::IO );

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

    for (U8 i = 0u; i < to_U8( TaskPoolType::COUNT ); ++i)
    {
        _taskPool[i]->flushCallbackQueue();
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
    if ( poolType == TaskPoolType::ASSET_LOADER ||
        poolType == TaskPoolType::RENDERER )
    {
        _gfx->onThreadCreated(threadID, isMainRenderThread);

        if ( poolType == TaskPoolType::ASSET_LOADER )
        {
            ShaderProgram::OnThreadCreated(*_gfx, threadID, isMainRenderThread);
        }
    }
}

}; //namespace Divide
