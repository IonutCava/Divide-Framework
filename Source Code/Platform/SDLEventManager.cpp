#include "stdafx.h"

#include "Headers/SDLEventManager.h"
#include "Headers/SDLEventListener.h"

namespace Divide {

    SharedMutex SDLEventManager::s_eventListenerLock;
    vectorEASTLFast<SDLEventListener*> SDLEventManager::s_eventListeners;

    void SDLEventManager::registerListener(SDLEventListener& listener) {
        ScopedLock<SharedMutex> lock(s_eventListenerLock);

        assert(!eastl::any_of(eastl::cbegin(s_eventListeners),
                              eastl::cend(s_eventListeners),
                              [&listener](SDLEventListener* l) {
                                    return l != nullptr && l->listenerID() == listener.listenerID();
                              }));

        s_eventListeners.push_back(&listener);
    }

    void SDLEventManager::unregisterListener(SDLEventListener& listener) {
        ScopedLock<SharedMutex> lock(s_eventListenerLock);

        const U64 targetID = listener.listenerID();
        const bool success = dvd_erase_if(s_eventListeners,
                                          [targetID](SDLEventListener* l)
                                          {
                                              return l && l->listenerID() == targetID;
                                          });
        DIVIDE_ASSERT(success);
    }

    void SDLEventManager::pollEvents() {
        OPTICK_EVENT();

        SDL_Event evt;
        while (SDL_PollEvent(&evt)) {
            OPTICK_EVENT("OnLoop");
            OPTICK_TAG("Event", evt.type);

            SharedLock<SharedMutex> lock(s_eventListenerLock);

            for (SDLEventListener* listener : s_eventListeners) {
                assert(listener != nullptr);

                if (listener->onSDLEvent(evt)) {
                    break;
                }
            }
        }
    }
}; //namespace Divide
