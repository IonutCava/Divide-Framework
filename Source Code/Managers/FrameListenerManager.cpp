#include "stdafx.h"

#include "Headers/FrameListenerManager.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

/// Register a new Frame Listener to be processed every frame
void FrameListenerManager::registerFrameListener(FrameListener* listener, const U32 callOrder) {
    OPTICK_EVENT();

    assert(Runtime::isMainThread());
    assert(listener != nullptr);

    listener->setCallOrder(callOrder);
    if (listener->getListenerName().empty()) {
        listener->name(Util::StringFormat("generic_f_listener_%d", listener->getGUID()).c_str());
    }
    insert_sorted(_listeners, listener, eastl::less<>());
    listener->enabled(true);

}

/// Remove an existent Frame Listener from our collection
void FrameListenerManager::removeFrameListener(FrameListener* const listener) {
    OPTICK_EVENT();

    assert(Runtime::isMainThread());

    assert(listener != nullptr);
    listener->enabled(false);
    if (!dvd_erase_if(_listeners,
                      [targetGUID = listener->getGUID()](FrameListener const* fl) noexcept {
                            return fl->getGUID() == targetGUID;
                       }))
    {
        Console::errorfn(Locale::Get(_ID("ERROR_FRAME_LISTENER_REMOVE")), listener->getListenerName().c_str());
    }
}

/// For each listener, notify of current event and check results
/// If any Listener returns false, the whole manager returns false for this specific step
/// If the manager returns false at any step, the application exists
bool FrameListenerManager::frameEvent(const FrameEvent& evt) {
    switch (evt._type) {
        case FrameEventType::FRAME_EVENT_STARTED     : return frameStarted(evt);
        case FrameEventType::FRAME_PRERENDER         : return framePreRender(evt);
        case FrameEventType::FRAME_SCENERENDER_START : return frameSceneRenderStarted(evt);
        case FrameEventType::FRAME_SCENERENDER_END   : return frameSceneRenderEnded(evt);
        case FrameEventType::FRAME_POSTRENDER        : return framePostRender(evt);
        case FrameEventType::FRAME_EVENT_PROCESS     : return frameRenderingQueued(evt);
        case FrameEventType::FRAME_EVENT_ENDED       : return frameEnded(evt);
        case FrameEventType::FRAME_EVENT_ANY         : return true;
    };

    return false;
}

bool FrameListenerManager::frameStarted(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && !listener->frameStarted(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::framePreRender(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && !listener->framePreRender(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameSceneRenderStarted(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && !listener->frameSceneRenderStarted(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameSceneRenderEnded(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && !listener->frameSceneRenderEnded(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameRenderingQueued(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && !listener->frameRenderingQueued(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::framePostRender(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (listener->enabled() && !listener->framePostRender(evt)) {
            return false;
        }
    }
    return true;
}

bool FrameListenerManager::frameEnded(const FrameEvent& evt) {
    OPTICK_EVENT();

    for (FrameListener* listener : _listeners) {
        if (!listener->frameEnded(evt)) {
            return false;
        }
    }

    return true;
}

bool FrameListenerManager::createAndProcessEvent(const FrameEventType type, FrameEvent& evt) {
    OPTICK_EVENT();

    evt._type = type;
    return frameEvent(evt);
}

};
