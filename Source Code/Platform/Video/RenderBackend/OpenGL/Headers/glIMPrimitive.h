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
#ifndef _GL_IM_EMULATION_H_
#define _GL_IM_EMULATION_H_

#include "Platform/Video/Headers/RenderAPIWrapper.h"
#include "Platform/Video/Headers/IMPrimitive.h"

namespace NS_GLIM {
    class GLIM_BATCH;
}; //namespace NS_GLIM

namespace Divide {

/// An Implementation of the NS_GLIM library.
class glIMPrimitive final : public IMPrimitive {
   public:
    glIMPrimitive(GFXDevice& context);
    ~glIMPrimitive();

   public:
    /// Begins defining one piece of geometry that can later be rendered with
    /// one set of states.
    void beginBatch(bool reserveBuffers, U32 vertexCount, U32 attributeCount) override;
    /// Ends defining the batch. After this call "RenderBatch" can be called to
    /// actually render it.
    void endBatch() noexcept override;
    /// Resets the batch so that the primitive has nothing left to draw
    void clearBatch() override;
    /// Return true if this primitive contains drawable geometry data
    bool hasBatch() const noexcept override;
    /// Begins gathering information about the given type of primitives.
    void begin(PrimitiveTopology type) override;
    /// Ends gathering information about the primitives.
    void end() override;
    /// Specify the position of a vertex belonging to this primitive
    void vertex(F32 x, F32 y, F32 z) override;
    /// Specify each attribute at least once(even with dummy values) before
    /// calling begin!
    /// Specify an attribute that will be applied to all vertex calls after this
    void attribute1i(U32 attribLocation, I32 value) override;
    void attribute1f(U32 attribLocation, F32 value) override;
    void attribute2f(U32 attribLocation, vec2<F32> value) override;
    void attribute3f(U32 attribLocation, vec3<F32> value) override;
    /// Specify an attribute that will be applied to all vertex calls after this
    void attribute4ub(U32 attribLocation, U8 x, U8 y, U8 z, U8 w) override;
    /// Specify an attribute that will be applied to all vertex calls after this
    void attribute4f(U32 attribLocation, F32 x, F32 y, F32 z, F32 w) override;
    /// Submit the created batch to the GPU for rendering
    void draw(const GenericDrawCommand& cmd) override;
    void pipeline(const Pipeline& pipeline) noexcept override;

   protected:
    /// Rendering API specific implementation
    NS_GLIM::GLIM_BATCH* _imInterface;
};

};  // namespace Divide
#endif