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
#ifndef _GL_BUFFERED_PUSH_CONSTANT_UPLOADER_H_
#define _GL_BUFFERED_PUSH_CONSTANT_UPLOADER_H_

#include "glPushConstantUploader.h"

namespace Divide {
    struct glBufferedPushConstantUploaderDescriptor
    {
        eastl::string _uniformBufferName = "";
        eastl::string _parentShaderName = "";
        GLuint _programHandle = 0u;
        GLuint _blockIndex = 0u;
    };

    struct glBufferedPushConstantUploader final : glPushConstantUploader
    {
        struct BlockMember
        {
            Str256 _name;
            U64    _nameHash = 0u;
            size_t _offset = 0;
            size_t _size = 0;
            GLint  _index = -1;
            GLint  _arraySize = -1;
            GLenum _type = GL_NONE;
            bool   _wasWrittenTo = false;
        };

        explicit glBufferedPushConstantUploader(const glBufferedPushConstantUploaderDescriptor& descriptor);
        ~glBufferedPushConstantUploader();

        void uploadPushConstant(const GFX::PushConstant& constant, bool force = false) override;
        void cacheUniforms() override;
        void commit() override;
        void prepare() override;

    private:
        vector<BlockMember> _blockMembers;
        eastl::string _uniformBufferName;
        eastl::string _parentShaderName;
        Byte* _uniformBlockBuffer = nullptr;
        GLuint _uniformBlockIndex = GLUtil::k_invalidObjectID;
        GLuint _uniformBlockBufferHandle = GLUtil::k_invalidObjectID;
        GLint _uniformBlockSize = 0;
        bool _uniformBlockDirty = false;
    };
} // namespace Divide

#endif //_GL_BUFFERED_PUSH_CONSTANT_UPLOADER_H_
