

#include "Headers/AudioAPIWrapper.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"

namespace Divide
{
    AudioAPIWrapper::AudioAPIWrapper( const Str<64>& name, PlatformContext& context )
        : PlatformContextComponent(context)
        , FrameListener( name, context.kernel().frameListenerMgr(), 1u )
    {
    }
} //namespace Divide
