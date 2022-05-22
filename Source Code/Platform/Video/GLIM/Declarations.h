/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#ifndef GLIM_DECLARATIONS_H
#define GLIM_DECLARATIONS_H

#include "Core/TemplateLibraries/Headers/String.h"
#include <assert.h>

namespace NS_GLIM
{

    //! The enum holding all important GLIM configuration values.
    enum class GLIM_ENUM : int
    {
        GLIM_NOPRIMITIVE,        // for internal use

        GLIM_TRIANGLES,            //!< Can be passed to GLIM::Begin
        GLIM_TRIANGLE_STRIP,    //!< Can be passed to GLIM::Begin    (not yet implemented)
        GLIM_TRIANGLE_FAN,        //!< Can be passed to GLIM::Begin
        GLIM_QUADS,                //!< Can be passed to GLIM::Begin
        GLIM_QUAD_STRIP,        //!< Can be passed to GLIM::Begin    (not yet implemented)
        GLIM_POINTS,            //!< Can be passed to GLIM::Begin
        GLIM_LINES,                //!< Can be passed to GLIM::Begin
        GLIM_LINE_STRIP,        //!< Can be passed to GLIM::Begin
        GLIM_LINE_LOOP,            //!< Can be passed to GLIM::Begin
        GLIM_POLYGON,            //!< Can be passed to GLIM::Begin

        GLIM_NODATA,            // for internal use
        GLIM_1F,                // for internal use
        GLIM_2F,                // for internal use
        GLIM_3F,                // for internal use
        GLIM_4F,                // for internal use
        GLIM_1I,                // for internal use
        GLIM_2I,                // for internal use
        GLIM_3I,                // for internal use
        GLIM_4I,                // for internal use
        GLIM_4UB,                // for internal use
    };

    enum class GLIM_BUFFER_TYPE : unsigned int {
        POINTS,
        LINES,
        TRIANGLES,
        WIREFRAME,
        COUNT
    };

    // One implementation of GLIM_Interface.
    class GLIM_BATCH;

    // One global GLIM_BATCH is always defined for immediate use.
    extern GLIM_BATCH glim;

    struct GlimArrayData;

    typedef GlimArrayData& GLIM_ATTRIBUTE;


    //! Declaration for a callback-function that will be called directly before each drawcall / shader-query.
    typedef void (*GLIM_CALLBACK)(void);

    //! Assert Macro used internally.
    inline void GLIM_CHECK (bool bCondition, const char* szErrorMsg) noexcept {
        Divide::DIVIDE_ASSERT(bCondition, szErrorMsg);
    }

}

#pragma once

#endif


