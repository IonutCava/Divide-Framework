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
#ifndef _GL_VERTEX_ARRAY_H_
#define _GL_VERTEX_ARRAY_H_

#include "glVAOCache.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

/// Always bind a shader, even a dummy one when rendering geometry. No more
/// fixed matrix API means no more VBs or VAs
/// One VAO contains: one VB for data, one IB for indices and uploads to the
/// shader vertex attribs for:
///- Vertex Data  bound to location Divide::VERTEX_POSITION
///- Colours      bound to location Divide::VERTEX_COLOR
///- Normals      bound to location Divide::VERTEX_NORMAL
///- TexCoords    bound to location Divide::VERTEX_TEXCOORD
///- Tangents     bound to location Divide::VERTEX_TANGENT
///- Bone weights bound to location Divide::VERTEX_BONE_WEIGHT
///- Bone indices bound to location Divide::VERTEX_BONE_INDICE
///- Line width   bound to location Divide::VERTEX_WIDTH

namespace Divide {

class glVertexArray final : public VertexBuffer,
                            public glVertexDataContainer {
   public:
    explicit glVertexArray(GFXDevice& context);
    ~glVertexArray();

    bool create(bool staticDraw = true) override;

    /// Never call Refresh() just queue it and the data will update before drawing
    bool queueRefresh() override {
        _refreshQueued = true;
        return true;
    }

   protected:
    void draw(const GenericDrawCommand& command) override;

   protected:
    friend class GL_API;
    void reset() override;
    /// Prepare data for upload
    bool refresh() override;
    /// Create vao objects
    void upload();
    /// Internally create the VB
    bool createInternal() override;
    /// Enable full VAO based VB (all pointers are tracked by VAO's)
    void uploadVBAttributes(GLuint VAO);
    
    /// Calculates the appropriate attribute offsets and returns the total size of a vertex for this buffer
    size_t populateAttributeSize();

    bool getMinimalData(const vector<Vertex>& dataIn, Byte* dataOut, size_t dataOutBufferLength);

    static void cleanup();

   protected:
    GLenum _formatInternal = GL_NONE;
    GLuint _IBid = 0;
    // VB GL ID and Offset.
    // This could easily be a std::pair, but having names for variables makes everything clearer
    GLUtil::AllocationHandle _VBHandle;
    GLenum _usage = GL_STATIC_DRAW;
    ///< A refresh call might be called before "Create()". This should help with that
    bool _refreshQueued = false;
    bool _uploadQueued = false;
    size_t _prevSize = 0;
    size_t _prevSizeIndices = 0;
    size_t _effectiveEntrySize = 0;
    AttribFlags _useAttribute{};
    using AttribValues = std::array<GLuint, to_base(AttribLocation::COUNT)>;
    AttribValues _attributeOffset{};

    // Both for forward pass and pre-pass
    std::array<GLuint, to_base(RenderStage::COUNT) * to_base(RenderPassType::COUNT)> _vaoCaches{};
    
    static GLUtil::glVAOCache _VAOMap;
};

};  // namespace Divide

#endif
