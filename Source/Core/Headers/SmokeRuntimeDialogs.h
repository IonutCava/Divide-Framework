#pragma once
// Copyright (c) 2026 DIVIDE-Studio

#if defined(_WIN32) && defined(_MSC_VER)
#include <cstdlib>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif

namespace Divide::RenderSmokeTest
{
    [[nodiscard]] inline bool ConfigureNonInteractiveCRT() noexcept
    {
#if defined(_WIN32) && defined(_MSC_VER)
        if ( _set_error_mode( _OUT_TO_STDERR ) == -1 )
        {
            return false;
        }
#if defined(_DEBUG)
        if ( _CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR ) == _CRTDBG_HFILE_ERROR ||
             _CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG ) == -1 )
        {
            return false;
        }
#endif
        _set_abort_behavior( 0, _CALL_REPORTFAULT );
#endif
        return true;
    }
}
