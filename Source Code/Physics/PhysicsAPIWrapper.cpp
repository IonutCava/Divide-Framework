#include "stdafx.h"

#include "Headers/PhysicsAPIWrapper.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

namespace Divide
{
    PhysicsAPIWrapper::PhysicsAPIWrapper( const Str64& name, PlatformContext& context )
        : FrameListener( name, context.kernel().frameListenerMgr(), 1u )
        , PlatformContextComponent( context )
    {
    }
} //namespace Divide
