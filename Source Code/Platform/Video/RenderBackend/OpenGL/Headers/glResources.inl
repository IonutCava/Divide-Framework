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
#ifndef _GL_RESOURCES_INL_
#define _GL_RESOURCES_INL_

namespace Divide {
namespace GLUtil {

template<typename T>
void getGLValue(const GLenum param, T& value, const GLint index) {
    if (index < 0) {
        glGetIntegerv(param, static_cast<GLint*>(&value));
    } else {
        glGetIntegeri_v(param, static_cast<GLuint>(index), static_cast<GLint*>(&value));
    }
}

template<typename T>
void getGLValue(const GLenum param, T* value) {
    glGetIntegerv(param, value);
}

template<>
inline void getGLValue(const GLenum param, U32& value, const GLint index) {
    value = static_cast<U32>(getGLValueIndexed<GLint>(param, index));
}

template<>
inline void getGLValue(const GLenum param, F32& value, const GLint index) {
    if (index < 0) {
        glGetFloatv(param, &value);
    } else {
        glGetFloati_v(param, static_cast<GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const GLenum param, GLboolean& value, const GLint index) {
    if (index < 0) {
        glGetBooleanv(param, &value);
    } else {
        glGetBooleani_v(param, static_cast<GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const GLenum param, D64& value, const GLint index) {
    if (index < 0) {
        glGetDoublev(param, &value);
    } else {
        glGetDoublei_v(param, static_cast<GLuint>(index), &value);
    }
}

template<>
inline void getGLValue(const GLenum param, GLint64& value, const GLint index) {
    if (index < 0) {
        glGetInteger64v(param, &value);
    } else {
        glGetInteger64i_v(param, static_cast<GLuint>(index), &value);
    }
}

template<typename T>
T getGLValue(GLenum param) {
    T ret = {};
    getGLValue(param, ret, -1);
    return ret;
}

template<typename T>
T getGLValueIndexed(GLenum param, GLint index) {
    T ret = {};
    getGLValue(param, ret, index);
    return ret;
}
}; // namespace GLUtil
}; // namespace Divide
#endif  //_GL_RESOURCES_INL_