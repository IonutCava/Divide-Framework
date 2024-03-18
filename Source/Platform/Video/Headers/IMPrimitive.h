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

#include "IMPrimitiveDescriptors.h"
#include "DescriptorSetsFwd.h"
#include "Pipeline.h"
#include "PushConstants.h"
#include "Platform/Video/GLIM/glim.h"
#include "Platform/Video/Headers/CommandsImpl.h"

namespace NS_GLIM {
    FWD_DECLARE_MANAGED_CLASS(GLIM_BATCH);
}

namespace Divide {

namespace GFX {
    class CommandBuffer;
};

class GFXDevice;
FWD_DECLARE_MANAGED_CLASS(IMPrimitive);
FWD_DECLARE_MANAGED_CLASS(GenericVertexData);

/// IMPrimitive replaces immediate mode calls to VB based rendering
class IMPrimitive final {
   public:
      
   public:
    static void InitStaticData();

   public:
     IMPrimitive(GFXDevice& context, const Str<64>& name);
     ~IMPrimitive() = default;

    void setPushConstants(const PushConstants& constants);
    void setPipelineDescriptor(const PipelineDescriptor& descriptor);
    void setTexture(const ImageView& texture, SamplerDescriptor sampler);

    void begin(PrimitiveTopology type);
    void end();

    void beginBatch(bool reserveBuffers, U32 vertexCount, U32 attributeCount);
    void endBatch() noexcept;

    void clearBatch();
    bool hasBatch() const noexcept;
    void reset();

    void vertex(F32 x, F32 y, F32 z);
    void attribute1i(U32 attribLocation, I32 value);
    void attribute1f(U32 attribLocation, F32 value);
    void attribute2f(U32 attribLocation, vec2<F32> value);
    void attribute3f(U32 attribLocation, vec3<F32> value);
    void attribute4ub(U32 attribLocation, U8 x, U8 y, U8 z,  U8 w);
    void attribute4f(U32 attribLocation, F32 x, F32 y, F32 z, F32 w);
    inline void vertex(const vec3<F32>& vert) { vertex(vert.x, vert.y, vert.z); }
    inline void attribute4ub(const U32 attribLocation, const vec4<U8> value)  { attribute4ub(attribLocation, value.x, value.y, value.z, value.w); }
    inline void attribute4f(const U32 attribLocation, const vec4<F32>& value) { attribute4f(attribLocation, value.x, value.y, value.z, value.w); }

    void fromLines(const IM::LineDescriptor& lines);
    void fromLines(const IM::LineDescriptor* lines, size_t count);
    
    void fromFrustum(const IM::FrustumDescriptor& frustum);
    void fromFrustums(const IM::FrustumDescriptor* frustums, size_t count);
    
    void fromOBB(const IM::OBBDescriptor& box);
    void fromOBBs(const IM::OBBDescriptor* boxes, size_t count);

    void fromBox(const IM::BoxDescriptor& box);
    void fromBoxes(const IM::BoxDescriptor* boxes, size_t count);
    void fromSphere(const IM::SphereDescriptor& sphere);
    void fromSpheres(const IM::SphereDescriptor* spheres, size_t count);
    void fromCone(const IM::ConeDescriptor& cone);
    void fromCones(const IM::ConeDescriptor* cones, size_t count);
    void fromLines(const Line* lines, size_t count);

    template<size_t N>
    inline void fromLines(const std::array<IM::LineDescriptor, N>& lines) { fromLines(lines.data(), lines.size()); }
    template<size_t N>
    inline void fromOBBs(const std::array<IM::OBBDescriptor, N>& obbs) { fromOBBs(obbs.data(), obbs.size()); }
    template<size_t N>
    inline void fromBoxes(const std::array<IM::BoxDescriptor, N>& boxes) { fromBoxes(boxes.data(), boxes.size()); }
    template<size_t N>
    inline void fromSpheres(const std::array<IM::SphereDescriptor, N>& spheres) { fromSpheres(spheres.data(), spheres.size()); }
    template<size_t N>
    inline void fromCones(const std::array<IM::ConeDescriptor, N>& cones) { fromCones(cones.data(), cones.size()); }

    void getCommandBuffer(GFX::CommandBuffer& commandBufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);
    void getCommandBuffer(const mat4<F32>& worldMatrix, GFX::CommandBuffer& commandBufferInOut, GFX::MemoryBarrierCommand& memCmdInOut);

    PROPERTY_R(Str<64>, name);
    PROPERTY_RW(bool, forceWireframe, false);

   protected:
    template <typename Data, size_t N>
    friend struct DebugPrimitiveHandler;
    friend void DestroyIMP(IMPrimitive*&);

    [[nodiscard]] inline GFXDevice& context() noexcept { return _context; }

   private:
    void fromLinesInternal(const Line* lines, size_t count);

   private:
    GFXDevice& _context;
    PushConstants _additionalConstats;
    NS_GLIM::GLIM_BATCH_uptr _imInterface;
    PipelineDescriptor _basePipelineDescriptor{};
    ImageView _texture{};
    SamplerDescriptor _sampler{};
    std::array<bool, to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT)> _drawFlags;
    std::array<size_t, to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT)> _indexCount;
    std::array<Pipeline*, to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT)> _pipelines;
    std::array<U8, to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT)> _indexBufferId;
    GenericVertexData_ptr _dataBuffer = nullptr;

    GFX::MemoryBarrierCommand _memCmd{};
};

};  // namespace Divide

#endif