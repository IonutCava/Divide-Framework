/***********************************************************************
    created:    Wed, 8th Feb 2012
    author:     Lukas E Meindl (based on code by Paul D Turner)
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
#ifndef _CEGUIDVDGeometryBuffer_inl_
#define _CEGUIDVDGeometryBuffer_inl_

#include "DVDTexture.h"

namespace CEGUI
{

inline void DVDGeometryBuffer::appendVertex( const Vertex& vertex )
{
    appendGeometry( &vertex, 1 );
}

inline void DVDGeometryBuffer::setTranslation( const Vector3f& v )
{
    _translation = v;
    _matrixValid = false;
}

inline void DVDGeometryBuffer::setRotation( const Quaternion& r )
{
    _rotation = r;
    _matrixValid = false;
}

inline void DVDGeometryBuffer::setPivot( const Vector3f& p )
{
    _pivot = Vector3f( p.d_x, p.d_y, p.d_z );
    _matrixValid = false;
}

inline void DVDGeometryBuffer::setClippingRegion( const Rectf& region )
{
    _clipRect.top( ceguimax( 0.0f, region.top() ) );
    _clipRect.left( ceguimax( 0.0f, region.left() ) );
    _clipRect.bottom( ceguimax( 0.0f, region.bottom() ) );
    _clipRect.right( ceguimax( 0.0f, region.right() ) );
}

inline void DVDGeometryBuffer::setActiveTexture( Texture* texture )
{
    _activeTexture = static_cast<DVDTexture*>(texture);
}

inline Texture* DVDGeometryBuffer::getActiveTexture() const
{
    return _activeTexture;
}

inline uint DVDGeometryBuffer::getVertexCount() const
{
    return (uint)_vertices.size();
}

inline uint DVDGeometryBuffer::getBatchCount() const
{
    return (uint)_batches.size();
}

inline void DVDGeometryBuffer::setRenderEffect( RenderEffect* effect )
{
    _effect = effect;
}

inline RenderEffect* DVDGeometryBuffer::getRenderEffect()
{
    return _effect;
}

inline void DVDGeometryBuffer::setClippingActive( const bool active )
{
    _clippingActive = active;
}

inline bool DVDGeometryBuffer::isClippingActive() const
{
    return _clippingActive;
}

}

#endif
