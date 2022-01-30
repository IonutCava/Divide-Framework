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
#ifndef _GL_GENERIC_BUFFER_H_
#define _GL_GENERIC_BUFFER_H_

#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

namespace Divide {

struct GenericBufferParams {
    BufferParams _bufferParams;
    GLenum _usage = GL_NONE;
    GLuint _ringSizeFactor = 1;
    const char* _name = "";
};

class glBufferImpl;
class glGenericBuffer {
  public:
      glGenericBuffer(GFXDevice& context, const GenericBufferParams& params);
      ~glGenericBuffer();

      [[nodiscard]] GLuint elementCount() const noexcept { return _elementCount; }

      [[nodiscard]] GLuint bufferHandle() const noexcept;

      void clearData(GLuint elementOffset,
                     GLuint ringWriteOffset) const;

      void writeData(GLuint elementCount,
                     GLuint elementOffset,
                     GLuint ringWriteOffset,
                     bufferPtr data) const;

      void readData(GLuint elementCount,
                    GLuint elementOffset,
                    GLuint ringReadOffset,
                    bufferPtr dataOut) const;

      POINTER_R(glBufferImpl, bufferImpl, nullptr);

      [[nodiscard]] bool needsSynchronization() const noexcept;

  protected:
      GLuint _elementCount;
      GLuint _ringSizeFactor;
};

}; //namespace Divide

#endif //_GENERIC_BUFFER_H_
