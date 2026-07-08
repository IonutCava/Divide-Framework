

#include "Headers/FrameListenerManager.h"

#include "Utility/Headers/Localization.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

/// Register a new Frame Listener to be processed every frame
void FrameListenerManager::registerFrameListener(FrameListener* listener, const U32 callOrder) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    assert(Runtime::isMainThread());
    assert(listener != nullptr);

    LockGuard<SharedMutex> w_lock(_listenerLock);
    listener->setCallOrder(callOrder);
    if (listener->name().empty())
    {
        listener->name(Util::StringFormat("generic_f_listener_{}", listener->getGUID()).c_str());
    }
    insert_sorted(_listeners, listener, eastl::less<>());
    listener->enabled(true);

}

/// Remove an existent Frame Listener from our collection
void FrameListenerManager::removeFrameListener(FrameListener* const listener) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    assert(Runtime::isMainThread());

    LockGuard<SharedMutex> w_lock(_listenerLock);
    assert(listener != nullptr);
    listener->enabled(false);
    if (!dvd_erase_if(_listeners,
                      [targetGUID = listener->getGUID()](FrameListener const* fl) noexcept {
                            return fl->getGUID() == targetGUID;
                       }))
    {
        Console::errorfn(LOCALE_STR("ERROR_FRAME_LISTENER_REMOVE"), listener->name().c_str());
    }
}

/// For each listener, notify of current event and check results
/// If any Listener returns false, the whole manager returns false for this specific step
/// If the manager returns false at any step, the application exists
bool FrameListenerManager::frameEvent(const FrameEvent& evt) {
    return frameEvent(evt, FrameExecutionDomain::LOGIC);
}

bool FrameListenerManager::frameEvent(const FrameEvent& evt, const FrameExecutionDomain domain) {
    switch (evt._type)
    {
        case FrameEventType::FRAME_EVENT_STARTED     : return frameStarted(evt, domain);
        case FrameEventType::FRAME_PRERENDER         : return framePreRender(evt, domain);
        case FrameEventType::FRAME_SCENERENDER_START : return frameSceneRenderStarted(evt, domain);
        case FrameEventType::FRAME_SCENERENDER_END   : return frameSceneRenderEnded(evt, domain);
        case FrameEventType::FRAME_POSTRENDER        : return framePostRender(evt, domain);
        case FrameEventType::FRAME_EVENT_PROCESS     : return frameRenderingQueued(evt, domain);
        case FrameEventType::FRAME_EVENT_ENDED       : return frameEnded(evt, domain);
        case FrameEventType::FRAME_EVENT_ANY         : return true;
        default: break;
    };

    return false;
}

bool FrameListenerManager::shouldDispatch(const FrameListener& listener, const FrameExecutionDomain domain) const noexcept {
    return listener.executionDomain() == FrameExecutionDomain::BOTH || listener.executionDomain() == domain;
}

bool FrameListenerManager::frameStarted(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && shouldDispatch(*listener, domain) && !listener->frameStarted(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::framePreRender(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && shouldDispatch(*listener, domain) && !listener->framePreRender(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameSceneRenderStarted(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && shouldDispatch(*listener, domain) && !listener->frameSceneRenderStarted(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameSceneRenderEnded(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && shouldDispatch(*listener, domain) && !listener->frameSceneRenderEnded(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameRenderingQueued(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && shouldDispatch(*listener, domain) && !listener->frameRenderingQueued(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::framePostRender(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && shouldDispatch(*listener, domain) && !listener->framePostRender(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameEnded(const FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const SharedLock<SharedMutex> r_lock(_listenerLock);
    for (FrameListener* listener : _listeners) {
        if (shouldDispatch(*listener, domain) && !listener->frameEnded(evt)) {
            return false;
        }
    }

    return true;
}

bool FrameListenerManager::createAndProcessEvent(const FrameEventType type, FrameEvent& evt) {
    return createAndProcessEvent(type, evt, FrameExecutionDomain::LOGIC);
}

bool FrameListenerManager::createAndProcessEvent(const FrameEventType type, FrameEvent& evt, const FrameExecutionDomain domain) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    evt._type = type;
    return frameEvent(evt, domain);
}

};
