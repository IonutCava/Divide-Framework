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
void getGLValue(const gl46core::GLenum param, T& value, const gl46core::GLint index)
{
    gl46core::GLint valueTemp = 0;
    if (index < 0)
    {
        gl46core::glGetIntegerv(param, &valueTemp);
    }
    else
    {
        gl46core::glGetIntegeri_v(param, static_cast<gl46core::GLuint>(index), &valueTemp);
    }

    value = static_cast<T>(valueTemp);
}

template<typename T>
void getGLValue(const gl46core::GLenum param, T* value)
{
    gl46core::glGetIntegerv(param, value);
}

template<>
inline void getGLValue(const gl46core::GLenum param, U32& value, const gl46core::GLint index)
{
    value = static_cast<U32>(getGLValueIndexed<gl46core::GLint>(param, index));
}

template<>
inline void getGLValue(const gl46core::GLenum param, F32& value, const gl46core::GLint index)
{
    if (index < 0)
    {
        gl46core::glGetFloatv(param, &value);
    }
    else
    {
        gl46core::glGetFloati_v(param, static_cast<gl46core::GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const gl46core::GLenum param, gl46core::GLboolean& value, const gl46core::GLint index)
{
    if (index < 0)
    {
        gl46core::glGetBooleanv(param, &value);
    }
    else
    {
        gl46core::glGetBooleani_v(param, static_cast<gl46core::GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const gl46core::GLenum param, D64& value, const gl46core::GLint index)
{
    if (index < 0)
    {
        gl46core::glGetDoublev(param, &value);
    }
    else
    {
        gl46core::glGetDoublei_v(param, static_cast<gl46core::GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const gl46core::GLenum param, gl46core::GLint64& value, const gl46core::GLint index)
{
    if (index < 0)
    {
        gl46core::glGetInteger64v(param, &value);
    }
    else
    {
        gl46core::glGetInteger64i_v(param, static_cast<gl46core::GLuint>(index), &value);
    }
}

template<typename T>
T getGLValue( gl46core::GLenum param)
{
    T ret = {};
    getGLValue(param, ret, -1);
    return ret;
}

template<typename T>
T getGLValueIndexed( gl46core::GLenum param, gl46core::GLint index)
{
    T ret = {};
    getGLValue(param, ret, index);
    return ret;
}

}; // namespace GLUtil
}; // namespace Divide

#endif  //DVD_GL_RESOURCES_INL_
