/***********************************************************************
    created:    Tue Apr 30 2013
    authors:    Paul D Turner <paul@cegui.org.uk>
                Lukas E Meindl
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2013 Paul D Turner & The CEGUI Development Team
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
#ifndef _CEGUIRenderer_inl_
#define _CEGUIRenderer_inl_

namespace CEGUI
{

inline const String& CEGUIRenderer::getIdentifierString() const
{
    return s_rendererID;
}

inline Divide::GFXDevice& CEGUIRenderer::context()
{
    return _context;
}

inline bool CEGUIRenderer::flipClippingHeight() const noexcept
{
    return _flipClippingHeight;
}

inline Divide::GFX::CommandBuffer* CEGUIRenderer::cmdBuffer() const
{
    return _bufferInOut;
}

inline Divide::GFX::MemoryBarrierCommand* CEGUIRenderer::memCmd() const
{
    return _memCmdInOut;
}

inline RenderTarget& CEGUIRenderer::getDefaultRenderTarget()
{
    return *_defaultTarget;
}

inline const glm::mat4& CEGUIRenderer::getViewProjectionMatrix() const noexcept
{
    return _viewProjectionMatrix;
}

inline void CEGUIRenderer::setViewProjectionMatrix( const glm::mat4& viewProjectionMatrix )
{
    _viewProjectionMatrix = viewProjectionMatrix;
}

inline const Sizef& CEGUIRenderer::getDisplaySize() const
{
    return _displaySize;
}

inline const Vector2f& CEGUIRenderer::getDisplayDPI() const
{
    return _displayDPI;
}

inline void CEGUIRenderer::setActiveRenderTarget( RenderTarget* renderTarget )
{
    _activeRenderTarget = renderTarget;
}

}

#endif