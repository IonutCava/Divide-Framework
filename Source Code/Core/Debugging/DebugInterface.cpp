#include "stdafx.h"

#include "Headers/DebugInterface.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ApplicationTimer.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide
{
    void DebugInterface::idle( const PlatformContext& context )
    {
        if constexpr( !Config::Profile::ENABLE_FUNCTION_PROFILING )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        if ( !enabled() )
        {
            return;
        }

        const LoopTimingData& timingData = Attorney::KernelDebugInterface::timingData( context.kernel() );

        if ( GFXDevice::FrameCount() % (Config::TARGET_FRAME_RATE / (Config::Build::IS_DEBUG_BUILD ? 4 : 2)) == 0 )
        {
            _output = Util::StringFormat( "Scene Update Loops: %d", timingData.updateLoops() );

            if constexpr( Config::Profile::ENABLE_FUNCTION_PROFILING )
            {
                const PerformanceMetrics perfMetrics = context.gfx().getPerformanceMetrics();

                _output.append( "\n" );
                _output.append( context.app().timer().benchmarkReport());
                _output.append( "\n" );
                _output.append( Util::StringFormat( "GPU: [ %5.5f ms] [DrawCalls: %d] [Vertices: %zu] [Primitives: %zu]",
                                                    perfMetrics._gpuTimeInMS,
                                                    context.gfx().frameDrawCallsPrev(),
                                                    perfMetrics._verticesSubmitted,
                                                    perfMetrics._primitivesGenerated ) );

                _output.append( "\n" );
                _output.append( Time::ProfileTimer::printAll() );
            }
        }
    }

} //namespace Divide