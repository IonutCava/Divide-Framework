

#include "Headers/SDLEventListener.h"
#include "Headers/SDLEventManager.h"

namespace Divide {

    std::atomic<U64> SDLEventListener::s_listenerIDCounter;

    SDLEventListener::SDLEventListener(const std::string_view name) noexcept
        : _listenerID(s_listenerIDCounter.fetch_add(1))
        , _name(name)
    {
        SDLEventManager::registerListener(*this);
    }

    SDLEventListener::~SDLEventListener()
    {
        SDLEventManager::unregisterListener(*this);
    }
} // namespace Divide
