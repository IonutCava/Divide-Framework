#include "stdafx.h"

/***********************************************************************
    created:    Wed, 8th Feb 2012
    author:     Lukas E Meindl
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2012 Paul D Turner & The CEGUI Development Team
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
#include "StateChangeWrapper.h"  
#include "Platform/Video/Headers/RenderStateBlock.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

namespace {
    const GLuint s_invalidBuffer = std::numeric_limits<GLuint>::max();
};

namespace Divide {
    BlendProperty getProperty(GLenum property) {
        for (U32 i = 0; i < to_U32(BlendProperty::COUNT); ++i) {
            if (GLUtil::glBlendTable[i] == property) {
                return static_cast<BlendProperty>(i);
            }
        }
        return BlendProperty::COUNT;
    }
};

namespace CEGUI
{

    OpenGL3StateChangeWrapper::BlendFuncParams::BlendFuncParams()
    {
        reset();
    }

    void OpenGL3StateChangeWrapper::BlendFuncParams::reset()    
    {
        d_dFactor = GL_NONE;
        d_sFactor = GL_NONE;
    }

    bool OpenGL3StateChangeWrapper::BlendFuncParams::equal(GLenum sFactor, GLenum dFactor)
    {
        const bool equal = d_sFactor == sFactor && d_dFactor == dFactor;
        if(!equal)
        {
            d_sFactor = sFactor;
            d_dFactor = dFactor;
        }
        return equal;
    }

    OpenGL3StateChangeWrapper::BlendFuncSeperateParams::BlendFuncSeperateParams()   
    {
        reset();
    }

    void OpenGL3StateChangeWrapper::BlendFuncSeperateParams::reset()    
    {
        d_sfactorRGB = GL_NONE;
        d_dfactorRGB = GL_NONE;
        d_sfactorAlpha = GL_NONE;
        d_dfactorAlpha = GL_NONE;
    }

    bool OpenGL3StateChangeWrapper::BlendFuncSeperateParams::equal(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
    {
        const bool equal = d_sfactorRGB == sfactorRGB && d_dfactorRGB == dfactorRGB && d_sfactorAlpha == sfactorAlpha && d_dfactorAlpha == dfactorAlpha;
        if(!equal)
        {
            d_sfactorRGB = sfactorRGB;
            d_dfactorRGB = dfactorRGB;
            d_sfactorAlpha = sfactorAlpha;
            d_dfactorAlpha = dfactorAlpha;
        }
        return equal;
    }

    OpenGL3StateChangeWrapper::PortParams::PortParams() 
    {
        reset();
    }
    void OpenGL3StateChangeWrapper::PortParams::reset() 
    {
        d_x = -1;
        d_y = -1;
        d_width = -1;
        d_height = -1;
    }

    bool OpenGL3StateChangeWrapper::PortParams::equal(GLint x, GLint y, GLsizei width, GLsizei height)
    {
        const bool equal = d_x == x && d_y == y && d_width == width && d_height == height;
        if(!equal)
        {
            d_x = x;
            d_y = y;
            d_width = width;
            d_height = height;
        }
        return equal;
    }

    OpenGL3StateChangeWrapper::BindBufferParams::BindBufferParams()
    {
        reset();
    }
    void OpenGL3StateChangeWrapper::BindBufferParams::reset()           
    {
        d_target = GL_NONE;
        d_buffer = s_invalidBuffer;
    }

    bool OpenGL3StateChangeWrapper::BindBufferParams::equal(GLenum target, GLuint buffer)
    {
        const bool equal = d_target == target && d_buffer == buffer;
        if(!equal)
        {
            d_target = target;
            d_buffer = buffer;
        }
        return equal;
    }

OpenGL3StateChangeWrapper::OpenGL3StateChangeWrapper()
{
    reset();
}

OpenGL3StateChangeWrapper::OpenGL3StateChangeWrapper(OpenGL3Renderer& /*owner*/)
{
    reset();
}

OpenGL3StateChangeWrapper::~OpenGL3StateChangeWrapper()
{
}

void OpenGL3StateChangeWrapper::reset()
{
    d_vertexArrayObject = 0u;
    d_blendFuncParams.reset();
    d_blendFuncSeperateParams.reset();
    d_viewPortParams.reset();
    d_scissorParams.reset();
    d_bindBufferParams.reset();

    if (d_defaultStateHashScissor == 0)
    {
        Divide::RenderStateBlock defaultState;
        defaultState.setCullMode(Divide::CullMode::NONE);
        defaultState.setFillMode(Divide::FillMode::SOLID);
        defaultState.depthTestEnabled(false);

        defaultState.setScissorTest(true);
        d_defaultStateHashScissor = defaultState.getHash();

        defaultState.setScissorTest(false);
        d_defaultStateHashNoScissor = defaultState.getHash();
    }
}

void OpenGL3StateChangeWrapper::bindVertexArray(GLuint vertexArray)
{
    if(vertexArray != d_vertexArrayObject)
    {
        if (Divide::GL_API::GetStateTracker().setActiveVAO(vertexArray) == Divide::GLStateTracker::BindResult::FAILED) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
        d_vertexArrayObject = vertexArray;
    }
}

void OpenGL3StateChangeWrapper::blendFunc(GLenum sfactor, GLenum dfactor)
{
    const bool callIsRedundant = d_blendFuncParams.equal(sfactor, dfactor);
    if(!callIsRedundant)
    {
        Divide::BlendingSettings blend{};
        blend.enabled(true);
        blend.blendSrc(Divide::getProperty(sfactor));
        blend.blendDest(Divide::getProperty(dfactor));
        blend.blendOp(Divide::BlendOperation::ADD);

        Divide::GL_API::GetStateTracker().setBlending(blend);
    }
}

void OpenGL3StateChangeWrapper::blendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    const bool callIsRedundant = d_blendFuncSeperateParams.equal(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
    if (!callIsRedundant) {
        Divide::BlendingSettings blend{};
        blend.enabled(true);
        blend.blendSrc(Divide::getProperty(sfactorRGB));
        blend.blendDest(Divide::getProperty(dfactorRGB));
        blend.blendOp(Divide::BlendOperation::ADD);
        blend.blendSrcAlpha(Divide::getProperty(sfactorAlpha));
        blend.blendDestAlpha(Divide::getProperty(dfactorAlpha));
        blend.blendOpAlpha(Divide::BlendOperation::ADD);

        Divide::GL_API::GetStateTracker().setBlending(blend);
    }
}

void OpenGL3StateChangeWrapper::viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    const bool callIsRedundant = d_viewPortParams.equal(x, y, width, height);
    if (!callIsRedundant) {
        Divide::GL_API::GetStateTracker().setViewport(x, y, width, height);
    }
}

void OpenGL3StateChangeWrapper::scissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    const bool callIsRedundant = d_scissorParams.equal(x, y, width, height);
    if (!callIsRedundant) {
        Divide::GL_API::GetStateTracker().setScissor(x, y, width, height);
    }
}
void OpenGL3StateChangeWrapper::bindBuffer(GLenum target, GLuint buffer)
{
    const bool callIsRedundant = d_bindBufferParams.equal(target, buffer);
    if (!callIsRedundant) {
        if (Divide::GL_API::GetStateTracker().setActiveBuffer(target, buffer) == Divide::GLStateTracker::BindResult::FAILED) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
    }
}

void OpenGL3StateChangeWrapper::bindDefaultState(bool scissor)
{
    if (Divide::GL_API::GetStateTracker().setStateBlock(scissor ? d_defaultStateHashScissor : d_defaultStateHashNoScissor) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
}

}
