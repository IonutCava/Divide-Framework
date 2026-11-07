// Copyright (c) 2026 DIVIDE-Studio
#include "Headers/FrameListenerManager.h"

#include "Utility/Headers/Localization.h"
#include "Platform/Headers/PlatformRuntime.h"

namespace Divide {

/// Register a new Frame Listener to be processed every frame
void FrameListenerManager::registerFrameListener(FrameListener* listener, const U32 callOrder) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    assert(Runtime::isMainThread());
    assert(listener != nullptr);

    erase_if(_listeners, [listener](const FrameListener* registered) noexcept
    {
        return registered == listener;
    });

    listener->setCallOrder(callOrder);
    if (listener->name().empty())
    {
        listener->name(Util::StringFormat("generic_f_listener_{}", listener->getGUID()).c_str());
    }
    insert_sorted(_listeners, listener, [](const FrameListener* lhs, const FrameListener* rhs) noexcept
    {
        return std::pair{lhs->_callOrder, lhs->getGUID()} < std::pair{rhs->_callOrder, rhs->getGUID()};
    });
    listener->enabled(true);

}

/// Remove an existent Frame Listener from our collection
void FrameListenerManager::removeFrameListener(FrameListener* const listener) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    assert(Runtime::isMainThread());

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
    assert(Runtime::isMainThread());

    switch (evt._type)
    {
        case FrameEventType::FRAME_EVENT_STARTED     : return dispatchFrameEvent(evt, &FrameListener::frameStarted);
        case FrameEventType::FRAME_PRERENDER         : return dispatchFrameEvent(evt, &FrameListener::framePreRender);
        case FrameEventType::FRAME_SCENERENDER_START : return dispatchFrameEvent(evt, &FrameListener::frameSceneRenderStarted);
        case FrameEventType::FRAME_SCENERENDER_END   : return dispatchFrameEvent(evt, &FrameListener::frameSceneRenderEnded);
        case FrameEventType::FRAME_POSTRENDER        : return dispatchFrameEvent(evt, &FrameListener::framePostRender);
        case FrameEventType::FRAME_EVENT_PROCESS     : return dispatchFrameEvent(evt, &FrameListener::frameRenderingQueued);
        case FrameEventType::FRAME_EVENT_ENDED       : return dispatchFrameEvent(evt, &FrameListener::frameEnded);
        case FrameEventType::FRAME_EVENT_ANY         : return true;
        default: break;
    };

    return false;
}

bool FrameListenerManager::dispatchFrameEvent(const FrameEvent& evt, const FrameCallback callback) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    const size_t depth = _dispatchDepth++;
    SCOPE_EXIT
    {
        --_dispatchDepth;
    };

    if (depth == _dispatchedListeners.size())
    {
        _dispatchedListeners.emplace_back();
    }
    _dispatchedListeners[depth].clear();

    size_t index = 0u;
    while (index < _listeners.size())
    {
        FrameListener* const listener = _listeners[index];
        const auto key = std::pair{listener->_callOrder, listener->getGUID()};
        const auto& dispatched = _dispatchedListeners[depth];
        if (listener->enabled() && eastl::find(dispatched.cbegin(), dispatched.cend(), key.second) == dispatched.cend())
        {
            _dispatchedListeners[depth].push_back(key.second);
            if (!(listener->*callback)(evt))
            {
                return false;
            }
        }

        const auto next = eastl::upper_bound(_listeners.cbegin(), _listeners.cend(), key,
            [](const std::pair<U32, I64>& current, const FrameListener* candidate) noexcept
            {
                return current < std::pair{candidate->_callOrder, candidate->getGUID()};
            });
        index = to_size(next - _listeners.cbegin());
    }

    return true;
}

bool FrameListenerManager::createAndProcessEvent(const FrameEventType type, FrameEvent& evt) {
    PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

    evt._type = type;
    return frameEvent(evt);
}

};
