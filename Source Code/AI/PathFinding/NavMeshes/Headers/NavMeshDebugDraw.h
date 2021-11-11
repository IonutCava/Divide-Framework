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
#ifndef _NAV_MESH_DEBUG_DRAW_H_
#define _NAV_MESH_DEBUG_DRAW_H_

#include <ReCast/DebugUtils/Include/DebugDraw.h>
#include "Platform/Video/Headers/CommandBuffer.h"

namespace Divide {

class GFXDevice;
class IMPrimitive;
class RenderStateBlock;

namespace AI {
namespace Navigation {

/// Convert a Recast colour integer to RGBA components.
inline void rcCol(U32 col, U8& r, U8& g, U8& b, U8& a) noexcept {
    r = col % 256;
    col /= 256;
    g = col % 256;
    col /= 256;
    b = col % 256;
    col /= 256;
    a = col % 256;
}

class NavMeshDebugDraw final : public duDebugDraw {
   public:
    NavMeshDebugDraw(GFXDevice& context) noexcept;
    virtual ~NavMeshDebugDraw();

    void paused(bool state) noexcept;
    void overrideColour(U32 col) noexcept;
    void beginBatch();
    void endBatch();

    void begin(duDebugDrawPrimitives prim, F32 size = 1.0f) override;
    void vertex(F32 x, F32 y, F32 z, U32 colour) override;
    void end() override;

    void setDirty(const bool state) noexcept { _dirty = state; }
    [[nodiscard]] bool isDirty() const noexcept { return _dirty; }
    [[nodiscard]] bool paused() const noexcept { return _paused; }
    void cancelOverride() noexcept { _overrideColour = false; }

    void texture([[maybe_unused]] bool state) noexcept override {}

    void depthMask(bool state) noexcept override;

    void vertex(const F32* pos, const U32 colour) override
    {
        vertex(pos[0], pos[1], pos[2], colour);
    }

    void vertex(const F32* pos, const U32 colour, const F32* uv) override
    {
        vertex(pos[0], pos[1], pos[2], colour);
    }

    void vertex(const F32 x, const F32 y, const F32 z, const U32 colour, const F32 u, const F32 v) override
    {
        vertex(x, y, z, colour);
    }

    [[nodiscard]] GFX::CommandBuffer& toCommandBuffer() const;

   private:
    GFXDevice& _context;
    PrimitiveType _primType = PrimitiveType::COUNT;
    IMPrimitive*_primitive = nullptr;
    U32 _colour = 0;
    bool _overrideColour = false;
    bool _dirty = true;
    bool _paused = false;
    bool _depthMask = true;
};
}  // namespace Navigation
}  // namespace AI
}  // namespace Divide

#endif
