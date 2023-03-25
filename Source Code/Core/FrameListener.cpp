#include "stdafx.h"

#include "Headers/FrameListener.h"
#include "Managers/Headers/FrameListenerManager.h"

namespace Divide {
    /// Either give it a name
    FrameListener::FrameListener(const Str64& name, FrameListenerManager& parent, const U32 callOrder)
        : GUIDWrapper(),
         _mgr(parent),
         _name(name),
         _callOrder(callOrder)
    {
        _mgr.registerFrameListener(this, callOrder);
    }

    FrameListener::~FrameListener()
    {
        _mgr.removeFrameListener(this);
    }
} //namespace Divide