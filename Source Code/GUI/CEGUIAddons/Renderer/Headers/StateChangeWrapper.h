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

#pragma once
#ifndef _CEGUIStateChangeWrapper_h_
#define _CEGUIStateChangeWrapper_h_

#include "RendererBase.h"

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4251)
#endif

// Start of CEGUI namespace section
namespace CEGUI
{

    /*!
    \brief
    StateChangeWrapper - wraps OpenGL calls and checks for redundant calls beforehand
    */
    class StateChangeWrapper : public AllocatedObject<StateChangeWrapper>
    {
    protected:
        struct BlendFuncParams
        {
            BlendFuncParams();
            void reset();
            bool equal(GLenum sFactor, GLenum dFactor);
            GLenum d_sFactor, d_dFactor;
        };
        struct BlendFuncSeperateParams
        {
            BlendFuncSeperateParams();
            void reset();
            bool equal(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
            GLenum d_sfactorRGB, d_dfactorRGB, d_sfactorAlpha, d_dfactorAlpha;
        };
        struct PortParams
        {
            PortParams();
            void reset();
            bool equal(GLint x, GLint y, GLsizei width, GLsizei height);
            GLint d_x, d_y;
            GLsizei d_width, d_height;
        };
        struct BindBufferParams
        {
            BindBufferParams();
            void reset();
            bool equal(GLenum target, GLuint buffer);
            GLenum d_target;
            GLuint d_buffer;
        };

public:

    StateChangeWrapper();
    StateChangeWrapper( DivideRenderer& owner);
    virtual ~StateChangeWrapper();

    void reset();

    void bindVertexArray(GLuint vertexArray);
    void blendFunc(GLenum sfactor, GLenum dfactor);
    void viewport(GLint x, GLint y, GLsizei width, GLsizei height);
    void scissor(GLint x, GLint y, GLsizei width, GLsizei height);
    void bindBuffer(GLenum target, GLuint buffer);
    void bindDefaultState(bool scissor);
protected:
    GLuint                      d_vertexArrayObject;
    BlendFuncParams             d_blendFuncParams;
    BlendFuncSeperateParams     d_blendFuncSeperateParams;
    PortParams                  d_viewPortParams;
    PortParams                  d_scissorParams;
    BindBufferParams            d_bindBufferParams;


    size_t d_defaultStateHashScissor = 0;
    size_t d_defaultStateHashNoScissor = 0;
};

} // End of  CEGUI namespace section

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif
