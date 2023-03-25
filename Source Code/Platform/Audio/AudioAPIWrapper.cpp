#include "stdafx.h"

#include "Headers/AudioAPIWrapper.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

namespace Divide
{
    AudioAPIWrapper::AudioAPIWrapper( const Str64& name, PlatformContext& context )
        : FrameListener( name, context.kernel().frameListenerMgr(), 1u )
        , PlatformContextComponent(context)
    {
    }
} //namespace Divide
