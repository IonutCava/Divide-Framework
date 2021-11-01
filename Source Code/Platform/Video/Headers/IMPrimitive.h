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
#ifndef _IM_EMULATION_H_
#define _IM_EMULATION_H_

#include "DescriptorSets.h"
#include "Core/Math/Headers/Line.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/Pipeline.h"

namespace Divide {
class OBB;
class Texture;

namespace GFX {
    class CommandBuffer;
};
enum class PrimitiveType : U8;

FWD_DECLARE_MANAGED_CLASS(IMPrimitive);

/// IMPrimitive replaces immediate mode calls to VB based rendering
class NOINITVTABLE IMPrimitive : public VertexDataInterface {
   public:
    const Pipeline* pipeline() const noexcept {
        return _pipeline;
    }

    virtual void pipeline(const Pipeline& pipeline) noexcept;

    void texture(const Texture& texture, size_t samplerHash);

    virtual void beginBatch(bool reserveBuffers, 
                            U32 vertexCount,
                            U32 attributeCount) = 0;

    virtual void begin(PrimitiveType type) = 0;
    virtual void vertex(F32 x, F32 y, F32 z) = 0;
    void vertex(const vec3<F32>& vert) {
        vertex(vert.x, vert.y, vert.z);
    }
    virtual void attribute1i(U32 attribLocation, I32 value) = 0;
    virtual void attribute1f(U32 attribLocation, F32 value) = 0;
    virtual void attribute4ub(U32 attribLocation, U8 x, U8 y, U8 z,  U8 w) = 0;
    virtual void attribute4f(U32 attribLocation, F32 x, F32 y, F32 z, F32 w) = 0;
    void attribute4ub(const U32 attribLocation, const vec4<U8>& value) {
        attribute4ub(attribLocation, value.x, value.y, value.z, value.w);
    }
    void attribute4f(const U32 attribLocation, const vec4<F32>& value) {
        attribute4f(attribLocation, value.x, value.y, value.z, value.w);
    }

    virtual void end() = 0;
    virtual void endBatch() = 0;
    virtual void clearBatch() = 0;
    virtual bool hasBatch() const = 0;
    void reset();

    void forceWireframe(const bool state) noexcept { _forceWireframe = state; }
    bool forceWireframe() const noexcept { return _forceWireframe; }

    const mat4<F32>& worldMatrix() const noexcept { return _worldMatrix; }

    void worldMatrix(const mat4<F32>& worldMatrix) noexcept {
        if (_worldMatrix != worldMatrix) {
            _worldMatrix.set(worldMatrix);
            _cmdBufferDirty = true;
        }
    }

    void resetWorldMatrix() noexcept {
        worldMatrix(MAT4_IDENTITY);
    }

    void viewport(const Rect<I32>& newViewport) noexcept {
        if (_viewport != newViewport) {
            _viewport.set(newViewport);
            _cmdBufferDirty = true;
        }
    }

    void resetViewport() noexcept {
        viewport(Rect<I32>(-1));
    }

    void name([[maybe_unused]] const Str64& name) noexcept {
#       ifdef _DEBUG
        _name = name;
#       endif
    }

    GFX::CommandBuffer& toCommandBuffer() const;

    void fromOBB(const OBB& obb,
                 const UColour4& colour = DefaultColours::WHITE);

    void fromBox(const vec3<F32>& min,
                 const vec3<F32>& max,
                 const UColour4& colour = DefaultColours::WHITE);

    void fromSphere(const vec3<F32>& center,
                    F32 radius,
                    const UColour4& colour = DefaultColours::WHITE);

    void fromCone(const vec3<F32>& root,
                  const vec3<F32>& direction,
                  F32 length, F32 radius,
                  const UColour4& colour = DefaultColours::WHITE);

    void fromLines(const Line* lines, size_t count);

    PROPERTY_RW(bool, skipPostFX, false);

   protected:
    mutable bool _cmdBufferDirty = true;
    GFX::CommandBuffer* _cmdBuffer = nullptr;

    IMPrimitive(GFXDevice& context);
#ifdef _DEBUG
    Str64 _name;
#endif
   public:
    virtual ~IMPrimitive();

   protected:

    const Pipeline* _pipeline = nullptr;
    // render in wireframe mode
    bool _forceWireframe = false;
    TextureEntry _textureEntry;
    Rect<I32> _viewport = {-1, -1, -1, -1};
   private:
    /// The transform matrix for this element
    mat4<F32> _worldMatrix;
};

};  // namespace Divide

#endif