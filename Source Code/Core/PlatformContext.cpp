#include "stdafx.h"

#include "Headers/PlatformContext.h"
#include "Headers/Configuration.h"
#include "Headers/XMLEntryData.h"

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
  :  _app(app)
  ,  _kernel(kernel)
  ,  _taskPool{}
  ,  _gfx(MemoryManager_NEW GFXDevice(_kernel))         // Video
  ,  _gui(MemoryManager_NEW GUI(_kernel))               // Audio
  ,  _sfx(MemoryManager_NEW SFXDevice(_kernel))         // Physics
  ,  _pfx(MemoryManager_NEW PXDevice(_kernel))          // Graphical User Interface
  ,  _entryData(MemoryManager_NEW XMLEntryData())       // Initial XML data
  ,  _config(MemoryManager_NEW Configuration())         // XML based configuration
  ,  _client(MemoryManager_NEW LocalClient(_kernel))    // Network client
  ,  _server(MemoryManager_NEW Server())                // Network server
  ,  _debug(MemoryManager_NEW DebugInterface(_kernel))  // Debug Interface
  ,  _editor(Config::Build::ENABLE_EDITOR ? MemoryManager_NEW Editor(*this) : nullptr)
  ,  _inputHandler(MemoryManager_NEW Input::InputHandler(_kernel, _app))
  ,  _paramHandler(MemoryManager_NEW ParamHandler())
{
    for (U8 i = 0; i < to_U8(TaskPoolType::COUNT); ++i) {
        _taskPool[i] = MemoryManager_NEW TaskPool();
    }
}


PlatformContext::~PlatformContext()
{
    assert(_gfx == nullptr);
}

void PlatformContext::terminate() {
    for (U32 i = 0; i < to_U32(TaskPoolType::COUNT); ++i) {
        MemoryManager::DELETE(_taskPool[i]);
    }
    MemoryManager::DELETE(_editor);
    MemoryManager::DELETE(_inputHandler);
    MemoryManager::DELETE(_entryData);
    MemoryManager::DELETE(_config);
    MemoryManager::DELETE(_client);
    MemoryManager::DELETE(_server);
    MemoryManager::DELETE(_debug);
    MemoryManager::DELETE(_gui);
    MemoryManager::DELETE(_pfx);
    MemoryManager::DELETE(_paramHandler);

    Console::printfn(Locale::Get(_ID("STOP_HARDWARE")));

    MemoryManager::DELETE(_sfx);
    MemoryManager::DELETE(_gfx);
}

void PlatformContext::beginFrame(const U32 componentMask) {
    OPTICK_EVENT();

    if (BitCompare(componentMask, SystemComponentType::GFXDevice)) {
        _gfx->beginFrame(*app().windowManager().mainWindow(), true);
    }

    if (BitCompare(componentMask, SystemComponentType::SFXDevice)) {
        _sfx->beginFrame();
    }
}

void PlatformContext::idle(const bool fast, const U32 componentMask) {
    OPTICK_EVENT();

    for (auto pool : _taskPool) {
        pool->flushCallbackQueue();
    }

    if (BitCompare(componentMask, SystemComponentType::Application)) {
       
    }

    if (BitCompare(componentMask, SystemComponentType::GFXDevice)) {
        _gfx->idle(fast);
    }
    if (BitCompare(componentMask, SystemComponentType::SFXDevice)) {
        //_sfx->idle();
    }
    if (BitCompare(componentMask, SystemComponentType::PXDevice)) {
        _pfx->idle();
    }
    if (BitCompare(componentMask, SystemComponentType::GUI)) {
        //_gui->idle();
    }
    if (BitCompare(componentMask, SystemComponentType::DebugInterface)) {
        _debug->idle();
    }

    if_constexpr(Config::Build::ENABLE_EDITOR) {
        if (BitCompare(componentMask, SystemComponentType::Editor)) {
            _editor->idle();
        }
    }
}

void PlatformContext::endFrame(const U32 componentMask) {
    OPTICK_EVENT();

    if (BitCompare(componentMask, SystemComponentType::GFXDevice)) {
        _gfx->endFrame(*app().windowManager().mainWindow(), true);
    }

    if (BitCompare(componentMask, SystemComponentType::SFXDevice)) {
        _sfx->endFrame();
    }
}

DisplayWindow& PlatformContext::mainWindow() {
    return *app().windowManager().mainWindow();
}

const DisplayWindow& PlatformContext::mainWindow() const {
    return *app().windowManager().mainWindow();
}

Kernel& PlatformContext::kernel() {
    return _kernel;
}

const Kernel& PlatformContext::kernel() const {
    return _kernel;
}

void PlatformContext::onThreadCreated(const std::thread::id& threadID) const {
    _gfx->onThreadCreated(threadID);
}

}; //namespace Divide