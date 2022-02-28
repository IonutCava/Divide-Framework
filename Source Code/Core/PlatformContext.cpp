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
  ,  _inputHandler(MemoryManager_NEW Input::InputHandler(_kernel, _app))
  ,  _paramHandler(MemoryManager_NEW ParamHandler())
  , _editor(Config::Build::ENABLE_EDITOR ? MemoryManager_NEW Editor(*this) : nullptr)
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
    MemoryManager::SAFE_DELETE(_editor);
    MemoryManager::DELETE(_inputHandler);
    MemoryManager::DELETE(_entryData);
    MemoryManager::DELETE(_config);
    MemoryManager::DELETE(_client);
    MemoryManager::DELETE(_server);
    MemoryManager::DELETE(_debug);
    MemoryManager::DELETE(_gui);
    MemoryManager::DELETE(_pfx);
    MemoryManager::DELETE(_paramHandler);
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
    if (BitCompare(componentMask, SystemComponentType::PXDevice)) {
        _pfx->beginFrame();
    }
    if_constexpr(Config::Build::ENABLE_EDITOR) {
        if (BitCompare(componentMask, SystemComponentType::Editor)) {
            _editor->beginFrame();
        }
    }
}

void PlatformContext::idle(const bool fast, const U32 componentMask) {
    OPTICK_EVENT();

    for (TaskPool* pool : _taskPool) {
        pool->flushCallbackQueue();
    }

    if (BitCompare(componentMask, SystemComponentType::Application)) {
        _app.idle();
    }
    if (BitCompare(componentMask, SystemComponentType::GFXDevice)) {
        _gfx->idle(fast);
    }
    if (BitCompare(componentMask, SystemComponentType::SFXDevice)) {
        _sfx->idle();
    }
    if (BitCompare(componentMask, SystemComponentType::PXDevice)) {
        _pfx->idle();
    }
    if (BitCompare(componentMask, SystemComponentType::GUI)) {
        _gui->idle();
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
    if (BitCompare(componentMask, SystemComponentType::PXDevice)) {
        _pfx->endFrame();
    }
    if_constexpr(Config::Build::ENABLE_EDITOR) {
        if (BitCompare(componentMask, SystemComponentType::Editor)) {
            _editor->endFrame();
        }
    }
}

DisplayWindow& PlatformContext::mainWindow() noexcept {
    return *app().windowManager().mainWindow();
}

const DisplayWindow& PlatformContext::mainWindow() const noexcept {
    return *app().windowManager().mainWindow();
}

Kernel& PlatformContext::kernel() noexcept {
    return _kernel;
}

const Kernel& PlatformContext::kernel() const noexcept {
    return _kernel;
}

void PlatformContext::onThreadCreated(const TaskPoolType poolType, const std::thread::id& threadID) const {
    if (poolType != TaskPoolType::LOW_PRIORITY) {
        _gfx->onThreadCreated(threadID);
    }
}

}; //namespace Divide