

#include "Headers/FrameListener.h"
#include "Managers/Headers/FrameListenerManager.h"

namespace Divide {
    /// Either give it a name
    FrameListener::FrameListener(const Str<64>& name, FrameListenerManager& parent, const U32 callOrder)
        : GUIDWrapper()
        , _name(name)
        , _mgr(parent)
        , _callOrder(callOrder)
    {
        _mgr.registerFrameListener(this, callOrder);
    }

    FrameListener::~FrameListener()
    {
        _mgr.removeFrameListener(this);
    }
} //namespace Divide