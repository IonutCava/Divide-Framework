#include "stdafx.h"

#include "Headers/IMPrimitive.h"

#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

IMPrimitive::IMPrimitive(GFXDevice& context)
    : VertexDataInterface(context)
{
    assert(handle()._id != 0);
    _cmdBuffer = GFX::AllocateCommandBuffer();
}

IMPrimitive::~IMPrimitive() 
{
    DeallocateCommandBuffer(_cmdBuffer);
}

void IMPrimitive::reset() {
    resetWorldMatrix();
    resetViewport();
    _textureEntry = {};
    _cmdBufferDirty = true;
    clearBatch();
}

void IMPrimitive::fromOBB(const OBB& obb, const UColour4& colour) {
    OBB::OOBBEdgeList edges = obb.edgeList();
    std::array<Line, 12> lines = {};
    for (U8 i = 0u; i < 12u; ++i)
    {
        lines[i].positionStart(edges[i]._start);
        lines[i].positionEnd(edges[i]._end);
        lines[i].colourStart(colour);
        lines[i].colourEnd(colour);
    }

    fromLines(lines.data(), lines.size());
}

void IMPrimitive::fromBox(const vec3<F32>& min, const vec3<F32>& max, const UColour4& colour) {
    // Create the object
    beginBatch(true, 16, 1);
        // Set it's colour
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(colour));
        // Draw the bottom loop
        begin(PrimitiveType::LINE_LOOP);
            vertex(min.x, min.y, min.z);
            vertex(max.x, min.y, min.z);
            vertex(max.x, min.y, max.z);
            vertex(min.x, min.y, max.z);
        end();
        // Draw the top loop
        begin(PrimitiveType::LINE_LOOP);
            vertex(min.x, max.y, min.z);
            vertex(max.x, max.y, min.z);
            vertex(max.x, max.y, max.z);
            vertex(min.x, max.y, max.z);
        end();
        // Connect the top to the bottom
        begin(PrimitiveType::LINES);
            vertex(min.x, min.y, min.z);
            vertex(min.x, max.y, min.z);
            vertex(max.x, min.y, min.z);
            vertex(max.x, max.y, min.z);
            vertex(max.x, min.y, max.z);
            vertex(max.x, max.y, max.z);
            vertex(min.x, min.y, max.z);
            vertex(min.x, max.y, max.z);
        end();
    // Finish our object
    endBatch();
}

void IMPrimitive::fromSphere(const vec3<F32>& center,
                             const F32 radius,
                             const UColour4& colour) {
    constexpr U32 slices = 8, stacks = 8;
    constexpr F32 drho = M_PI_f / stacks;
    constexpr F32 dtheta = 2.0f * M_PI_f / slices;
    constexpr F32 ds = 1.0f / slices;
    constexpr F32 dt = 1.0f / stacks;

    F32 t = 1.0f;
    // Create the object
    beginBatch(true, stacks * ((slices + 1) * 2), 1);
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(colour));
        begin(PrimitiveType::LINE_LOOP);
            for (U32 i = 0; i < stacks; i++) {
                const F32 rho = i * drho;
                const F32 srho = std::sin(rho);
                const F32 crho = std::cos(rho);
                const F32 srhodrho = std::sin(rho + drho);
                const F32 crhodrho = std::cos(rho + drho);

                F32 s = 0.0f;
                for (U32 j = 0; j <= slices; j++) {
                    const F32 theta = j == slices ? 0.0f : j * dtheta;
                    const F32 stheta = -std::sin(theta);
                    const F32 ctheta = std::cos(theta);

                    F32 x = stheta * srho;
                    F32 y = ctheta * srho;
                    F32 z = crho;
                    vertex(x * radius + center.x,
                        y * radius + center.y,
                        z * radius + center.z);
                    x = stheta * srhodrho;
                    y = ctheta * srhodrho;
                    z = crhodrho;
                    s += ds;
                    vertex(x * radius + center.x,
                        y * radius + center.y,
                        z * radius + center.z);
                }
                t -= dt;
            }
        end();
    endBatch();
}

//ref: http://www.freemancw.com/2012/06/opengl-cone-function/
void IMPrimitive::fromCone(const vec3<F32>& root,
                           const vec3<F32>& direction,
                           F32 length, F32 radius,
                           const UColour4& colour) {
    constexpr U8 slices = 16u;
    const vec3<F32> invDirection = -direction;
    const vec3<F32> c = root + -invDirection * length;
    const vec3<F32> e0 = Perpendicular(invDirection);
    const vec3<F32> e1 = Cross(e0, invDirection);
    constexpr F32 angInc = 360.0f / slices * M_PIDIV180_f;

    // calculate points around directrix
    std::array<vec3<F32>, slices> pts = {};
    for (size_t i = 0; i < pts.size(); ++i) {
        const F32 rad = angInc * i;
        pts[i] = c + (e0 * std::cos(rad) + e1 * std::sin(rad)) * radius;
    }

    // draw cone top
    beginBatch(true, slices + 1, 1);
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(colour));
        // Top
        begin(PrimitiveType::TRIANGLE_FAN);
            vertex(root);
            for (U8 i = 0; i < slices; ++i) {
                vertex(pts[i]);
            }
        end();

        // Bottom
        begin(PrimitiveType::TRIANGLE_FAN);
            vertex(c);
            for (I8 i = slices - 1; i >= 0; --i) {
                vertex(pts[i]);
            }
        end();
    endBatch();
}

void IMPrimitive::fromLines(const Line* lines, const size_t count) {
    
    // Check if we have a valid list. The list can be programmatically
    // generated, so this check is required
    if (count > 0) {
        // Create the object containing all of the lines
        beginBatch(true, to_U32(count) * 2 * 14, 1);
            attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(lines[0].colourStart()));
            // Set the mode to line rendering
            begin(PrimitiveType::LINES);
                // Add every line in the list to the batch
                for (size_t i = 0u; i < count; ++i) {
                    const Line& line = lines[i];
                    attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line.colourStart()));
                    vertex(line.positionStart());

                    attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line.colourEnd()));

                    vertex(line.positionEnd());

                }
            end();
        // Finish our object
        endBatch();
    }
}

void IMPrimitive::setPushConstants(const PushConstants& constants) {
    _additionalConstats = constants;
    _cmdBufferDirty = true;
}

void IMPrimitive::pipeline(const Pipeline& pipeline) noexcept {
    if (_pipeline == nullptr || *_pipeline != pipeline) {
        _pipeline = &pipeline;
        _cmdBufferDirty = true;
    }
}

void IMPrimitive::texture(const Texture& texture, const size_t samplerHash) {
    TextureEntry tempEntry{};
    tempEntry._data = texture.data();
    tempEntry._sampler = samplerHash;
    tempEntry._binding = to_U8(TextureUsage::UNIT0);

    if (_textureEntry != tempEntry) {
        _textureEntry = tempEntry;
        _cmdBufferDirty = true;
    }
}

GFX::CommandBuffer& IMPrimitive::toCommandBuffer() const {
    if (_cmdBufferDirty) {
        _cmdBuffer->clear();

        DIVIDE_ASSERT(_pipeline->shaderProgramHandle() != 0, "IMPrimitive error: Draw call received without a valid shader defined!");

        GFX::EnqueueCommand(*_cmdBuffer, GFX::BindPipelineCommand{ _pipeline });

        PushConstants& constants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(*_cmdBuffer)->_constants;
        // Inform the shader if we have (or don't have) a texture
        constants.set(_ID("useTexture"), GFX::PushConstantType::BOOL, IsValid(_textureEntry));
        // Upload the primitive's world matrix to the shader
        constants.set(_ID("dvd_WorldMatrix"), GFX::PushConstantType::MAT4, worldMatrix());

        if (!_additionalConstats.empty()) {
            bool partial = false;
            Merge(constants, _additionalConstats, partial);
        }

        if (IsValid(_textureEntry)) {
            GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(*_cmdBuffer)->_set._textureData.add(_textureEntry);
        }

        if (_viewport != Rect<I32>(-1)) {
            GFX::EnqueueCommand(*_cmdBuffer, GFX::SetViewportCommand{ _viewport });
        }

        GenericDrawCommand cmd{};
        cmd._primitiveType = PrimitiveType::TRIANGLE_STRIP;
        cmd._sourceBuffer = handle();
        GFX::EnqueueCommand(*_cmdBuffer, GFX::DrawCommand{ cmd });

        _cmdBufferDirty = false;
    }

    return *_cmdBuffer;
}


};