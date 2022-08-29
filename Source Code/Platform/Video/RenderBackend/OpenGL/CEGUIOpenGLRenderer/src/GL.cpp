#include "stdafx.h"

/***********************************************************************
    created:    21/7/2015
    author:     Yaron Cohen-Tal
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2015 Paul D Turner & The CEGUI Development Team
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 ***************************************************************************/
#include "GL.h"
#include "CEGUI/String.h"
#include "CEGUI/Exceptions.h"

#include <glbinding/gl/gl.h>

namespace CEGUI
{

OpenGLInfo OpenGLInfo::s_instance;

//----------------------------------------------------------------------------//
OpenGLInfo::OpenGLInfo() :
    d_type(TYPE_NONE),
    d_verMajor(-1),
    d_verMinor(-1),
    d_verMajorForce(-1),
    d_verMinorForce(-1),
    d_isS3tcSupported(false),
    d_isNpotTextureSupported(false),
    d_isReadBufferSupported(false),
    d_isPolygonModeSupported(false),
    d_isSizedInternalFormatSupported(false)
{
}

//----------------------------------------------------------------------------//
void OpenGLInfo::init()
{
    initTypeAndVer();
    initSupportedFeatures();
}

void OpenGLInfo::verForce(GLint verMajor_, GLint verMinor_)
{
    d_verMajorForce = verMajor_;
    d_verMinorForce = verMinor_;
}

//----------------------------------------------------------------------------//
void OpenGLInfo::initTypeAndVer()
{
    d_type = TYPE_DESKTOP;
    glGetError();
    d_verMajor = 4;
    d_verMinor = 6;
}

//----------------------------------------------------------------------------//
void OpenGLInfo::initSupportedFeatures()
{
    d_isS3tcSupported = true;
    d_isNpotTextureSupported = true;
    d_isSizedInternalFormatSupported = true;
    d_isPolygonModeSupported = true;
}

} // namespace CEGUI
