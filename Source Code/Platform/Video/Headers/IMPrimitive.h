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
#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Rendering/Camera/Headers/Frustum.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/Pipeline.h"
#include "Platform/Video/Headers/PushConstants.h"
#include "Platform/Video/GLIM/glim.h"

namespace Divide {

namespace GFX {
    class CommandBuffer;
};

FWD_DECLARE_MANAGED_CLASS(IMPrimitive);
FWD_DECLARE_MANAGED_CLASS(GenericVertexData);

/// IMPrimitive replaces immediate mode calls to VB based rendering
class IMPrimitive final {
   public:
       struct BaseDescriptor {
           UColour4  colour{ DefaultColours::WHITE };
           mat4<F32> worldMatrix;
           bool noDepth{ false };
           bool noCull{ false };
           bool wireframe{ false };
       };

       struct OBBDescriptor final : public BaseDescriptor {
           OBB box;
       };

       struct FrustumDescriptor final : public BaseDescriptor {
           Frustum frustum;
       };

       struct BoxDescriptor final : public BaseDescriptor {
           vec3<F32> min{ VECTOR3_UNIT * -0.5f };
           vec3<F32> max{ VECTOR3_UNIT * 0.5f };
       }; 
       
       struct LineDescriptor final : public BaseDescriptor {
           vector<Line> _lines;
       };

       struct SphereDescriptor final : public BaseDescriptor {
           vec3<F32> center{ VECTOR3_ZERO };
           F32 radius{ 1.f };
           U8 slices{ 8u };
           U8 stacks{ 8u };
       };

       struct ConeDescriptor final : public BaseDescriptor {
           vec3<F32> root{ VECTOR3_ZERO };
           vec3<F32> direction{ WORLD_Y_AXIS };
           F32 length{ 1.f };
           F32 radius{ 2.f };
           U8 slices{ 16u }; //max 32u
       };

   public:
    static void InitStaticData();

   public:
     IMPrimitive(GFXDevice& context);
     ~IMPrimitive() = default;

    void setPushConstants(const PushConstants& constants);

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
    inline void attribute4ub(const U32 attribLocation, const vec4<U8>& value) { attribute4ub(attribLocation, value.x, value.y, value.z, value.w); }
    inline void attribute4f(const U32 attribLocation, const vec4<F32>& value) { attribute4f(attribLocation, value.x, value.y, value.z, value.w); }

    void fromLines(const LineDescriptor& lines);
    void fromLines(const LineDescriptor* lines, size_t count);
    
    void fromFrustum(const FrustumDescriptor& frustum);
    void fromFrustums(const FrustumDescriptor* frustums, size_t count);
    
    void fromOBB(const OBBDescriptor& box);
    void fromOBBs(const OBBDescriptor* boxes, size_t count);

    void fromBox(const BoxDescriptor& box);
    void fromBoxes(const BoxDescriptor* boxes, size_t count);
    void fromSphere(const SphereDescriptor& sphere);
    void fromSpheres(const SphereDescriptor* spheres, size_t count);
    void fromCone(const ConeDescriptor& cone);
    void fromCones(const ConeDescriptor* cones, size_t count);
    void fromLines(const Line* lines, size_t count);

    template<size_t N>
    inline void fromLines(const std::array<LineDescriptor, N>& lines) { fromLines(lines.data(), lines.size()); } 
    template<size_t N>
    inline void fromOBBs(const std::array<OBBDescriptor, N>& obbs) { fromOBBs(obbs.data(), obbs.size()); } 
    template<size_t N>
    inline void fromBoxes(const std::array<BoxDescriptor, N>& boxes) { fromBoxes(boxes.data(), boxes.size()); }
    template<size_t N>
    inline void fromSpheres(const std::array<SphereDescriptor, N>& spheres) { fromSpheres(spheres.data(), spheres.size()); }
    template<size_t N>
    inline void fromCones(const std::array<ConeDescriptor, N>& cones) { fromCones(cones.data(), cones.size()); }

    void getCommandBuffer(PipelineDescriptor& pipelineDescriptorInOut,
                          GFX::CommandBuffer& commandBufferInOut);
    void getCommandBuffer(const mat4<F32>& worldMatrix,
                          PipelineDescriptor& pipelineDescriptorInOut,
                          GFX::CommandBuffer& commandBufferInOut);
    void getCommandBuffer(const mat4<F32>& worldMatrix,
                          const TextureData& texture,
                          size_t samplerHash,
                          PipelineDescriptor& pipelineDescriptorInOut,
                          GFX::CommandBuffer& commandBufferInOut);

    PROPERTY_RW(Str64, name);
    PROPERTY_RW(bool, forceWireframe, false);

   protected:
    template <typename Data, size_t N>
    friend struct DebugPrimitiveHandler;

    [[nodiscard]] inline GFXDevice& context() noexcept { return _context; }

   private:
    void fromLinesInternal(const Line* lines, size_t count);

   private:
    GFXDevice& _context;
    PushConstants _additionalConstats;
    eastl::unique_ptr<NS_GLIM::GLIM_BATCH> _imInterface;
    AttributeMap _vertexFormat{};
    std::array<bool, to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT)> _drawFlags;
    std::array<GenericVertexData_ptr, to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT)> _dataBuffers;

    bool _dirty = false;
};

};  // namespace Divide

#endif