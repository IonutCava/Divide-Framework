/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef DVD_GL_RESOURCES_INL_
#define DVD_GL_RESOURCES_INL_

namespace Divide {
namespace GLUtil {

template<typename T>
void getGLValue(const gl::GLenum param, T& value, const gl::GLint index)
{
    gl::GLint valueTemp = 0;
    if (index < 0)
    {
        gl::glGetIntegerv(param, &valueTemp);
    }
    else
    {
        gl::glGetIntegeri_v(param, static_cast<gl::GLuint>(index), &valueTemp);
    }

    value = static_cast<T>(valueTemp);
}

template<typename T>
void getGLValue(const gl::GLenum param, T* value)
{
    gl::glGetIntegerv(param, value);
}

template<>
inline void getGLValue(const gl::GLenum param, U32& value, const gl::GLint index)
{
    value = static_cast<U32>(getGLValueIndexed<gl::GLint>(param, index));
}

template<>
inline void getGLValue(const gl::GLenum param, F32& value, const gl::GLint index)
{
    if (index < 0)
    {
        gl::glGetFloatv(param, &value);
    }
    else
    {
        gl::glGetFloati_v(param, static_cast<gl::GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const gl::GLenum param, gl::GLboolean& value, const gl::GLint index)
{
    if (index < 0)
    {
        gl::glGetBooleanv(param, &value);
    }
    else
    {
        gl::glGetBooleani_v(param, static_cast<gl::GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const gl::GLenum param, D64& value, const gl::GLint index)
{
    if (index < 0)
    {
        gl::glGetDoublev(param, &value);
    }
    else
    {
        gl::glGetDoublei_v(param, static_cast<gl::GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const gl::GLenum param, gl::GLint64& value, const gl::GLint index)
{
    if (index < 0)
    {
        gl::glGetInteger64v(param, &value);
    }
    else
    {
        gl::glGetInteger64i_v(param, static_cast<gl::GLuint>(index), &value);
    }
}

template<typename T>
T getGLValue( gl::GLenum param)
{
    T ret = {};
    getGLValue(param, ret, -1);
    return ret;
}

template<typename T>
T getGLValueIndexed( gl::GLenum param, gl::GLint index)
{
    T ret = {};
    getGLValue(param, ret, index);
    return ret;
}
}; // namespace GLUtil
}; // namespace Divide

#endif  //DVD_GL_RESOURCES_INL_
