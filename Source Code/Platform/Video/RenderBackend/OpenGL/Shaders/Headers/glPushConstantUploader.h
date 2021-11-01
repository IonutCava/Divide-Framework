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
#ifndef _GL_PUSH_CONSTANT_UPLOADER_H_
#define _GL_PUSH_CONSTANT_UPLOADER_H_

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"

namespace Divide {
    struct glPushConstantUploader
    {
        explicit glPushConstantUploader(const GLuint programHandle) : _programHandle(programHandle) {}
        virtual ~glPushConstantUploader() = default;

        /// Gather the list of active uniforms in the parent program so that we can reference their location,size, etc later
        virtual void cacheUniforms() = 0;
        /// Submit all of the changed push constants to VRAM (e.g. write constant buffer data from client to server)
        virtual void commit() = 0;
        /// Make sure the current uploader is set-up for memory transactions (e.g., the buffer is bound and active)
        virtual void prepare() = 0;
        /// Update a single push constant. This MAY have no effect until "commit" is called.
        virtual void uploadPushConstant(const GFX::PushConstant& constant, bool force = false) noexcept = 0;

    protected:
        GLuint _programHandle = 0u;
    };
} // namespace Divide

#endif //_GL_PUSH_CONSTANT_UPLOADER_H_
