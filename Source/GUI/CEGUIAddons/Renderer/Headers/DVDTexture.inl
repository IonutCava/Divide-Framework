/***********************************************************************
    created:    Sun Jan 11 2009
    author:     Paul D Turner
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2009 Paul D Turner & The CEGUI Development Team
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
#ifndef _CEGUIDVDTexture_inl_
#define _CEGUIDVDTexture_inl_

namespace CEGUI
{

inline const String& DVDTexture::getName() const
{
    return _name;
}

inline const Sizef& DVDTexture::getSize() const
{
    return _size;
}

inline const Sizef& DVDTexture::getOriginalDataSize() const
{
    return _dataSize;
}

inline const Vector2f& DVDTexture::getTexelScaling() const
{
    return _texelScaling;
}

inline void DVDTexture::updateCachedScaleValues()
{
    _texelScaling.d_x = _size.d_width  == 0.f ? 0.f : 1.f / _size.d_width;
    _texelScaling.d_y = _size.d_height == 0.f ? 0.f : 1.f / _size.d_height;
}

inline Divide::Texture_ptr DVDTexture::getDVDTexture() const
{
    return _texture;
}

}

#endif

