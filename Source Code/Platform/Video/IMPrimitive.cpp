#include "stdafx.h"

#include "Headers/IMPrimitive.h"

#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

namespace Divide {

namespace {
    std::array<NS_GLIM::GLIM_ENUM, to_base(PrimitiveTopology::COUNT)> glimPrimitiveType;
    inline size_t GetSizeFactor(const NS_GLIM::GLIM_ENUM dataType) noexcept {
        switch (dataType) {
            case NS_GLIM::GLIM_ENUM::GLIM_1I:
            case NS_GLIM::GLIM_ENUM::GLIM_1F:
            case NS_GLIM::GLIM_ENUM::GLIM_4UB: return 1u;

            case NS_GLIM::GLIM_ENUM::GLIM_2F:
            case NS_GLIM::GLIM_ENUM::GLIM_2I: return 2u;

            case NS_GLIM::GLIM_ENUM::GLIM_3F:
            case NS_GLIM::GLIM_ENUM::GLIM_3I: return 3u;

            case NS_GLIM::GLIM_ENUM::GLIM_4F:
            case NS_GLIM::GLIM_ENUM::GLIM_4I: return 4u;
        }

        DIVIDE_UNEXPECTED_CALL();
        return 0u;
    }
};

void IMPrimitive::InitStaticData()
{
    glimPrimitiveType[to_base(PrimitiveTopology::POINTS)] = NS_GLIM::GLIM_ENUM::GLIM_POINTS;
    glimPrimitiveType[to_base(PrimitiveTopology::LINES)] = NS_GLIM::GLIM_ENUM::GLIM_LINES;
    glimPrimitiveType[to_base(PrimitiveTopology::LINE_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_LINE_STRIP;
    glimPrimitiveType[to_base(PrimitiveTopology::TRIANGLES)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLES;
    glimPrimitiveType[to_base(PrimitiveTopology::TRIANGLE_STRIP)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLE_STRIP;
    glimPrimitiveType[to_base(PrimitiveTopology::TRIANGLE_FAN)] = NS_GLIM::GLIM_ENUM::GLIM_TRIANGLE_FAN;
}

IMPrimitive::IMPrimitive(GFXDevice& context)
    : _context(context)
{
    _imInterface = eastl::make_unique<NS_GLIM::GLIM_BATCH>();
    for (auto& buffer : _dataBuffers) {
        buffer = context.newGVD(1);
        buffer->renderIndirect(false);
    }
    _drawFlags.fill(false);

    AttributeDescriptor& desc = _vertexFormat[0u];
    desc._bindingIndex = 0u;
    desc._componentsPerElement = 3u;
    desc._dataType = GFXDataFormat::FLOAT_32;
    desc._normalized = false;
    desc._strideInBytes = 0;
}

void IMPrimitive::reset() {
    clearBatch();
    for (U8 i = 1u; i < to_base(AttribLocation::COUNT); ++i) {
        _vertexFormat[i] = {};
    }
    _drawFlags.fill(false);
}


void IMPrimitive::beginBatch(const bool reserveBuffers, const U32 vertexCount, const U32 attributeCount) {
    _imInterface->BeginBatch(reserveBuffers, vertexCount, attributeCount);
}

void IMPrimitive::clearBatch() {
    _imInterface->Clear(true, 64 * 3, 1);
}

bool IMPrimitive::hasBatch() const noexcept {
    return !_imInterface->isCleared();
}

void IMPrimitive::begin(const PrimitiveTopology type) {
    _imInterface->Begin(glimPrimitiveType[to_U32(type)]);
}

void IMPrimitive::vertex(const F32 x, const  F32 y, const F32 z) {
    _imInterface->Vertex(x, y, z);
    _dirty = true;
}

void IMPrimitive::attribute1f(const U32 attribLocation, const F32 value) {
    _imInterface->Attribute1f(attribLocation, value);

    AttributeDescriptor& desc = _vertexFormat[attribLocation];
    desc._bindingIndex = to_U16(attribLocation);
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 1u;
    desc._dataType = GFXDataFormat::FLOAT_32;

    _dirty = true;
}

void IMPrimitive::attribute2f(const U32 attribLocation, const vec2<F32> value) {
    _imInterface->Attribute2f(attribLocation, value.x, value.y);
    AttributeDescriptor& desc = _vertexFormat[attribLocation];
    desc._bindingIndex = to_U16(attribLocation);
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 2u;
    desc._dataType = GFXDataFormat::FLOAT_32;

    _dirty = true;
}

void IMPrimitive::attribute3f(const U32 attribLocation, const vec3<F32> value) {
    _imInterface->Attribute3f(attribLocation, value.x, value.y, value.z);
    Divide::AttributeDescriptor& desc = _vertexFormat[attribLocation];
    desc._bindingIndex = to_U16(attribLocation);
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 3u;
    desc._dataType = GFXDataFormat::FLOAT_32;

    _dirty = true;
}

void IMPrimitive::attribute4ub(const U32 attribLocation, const U8 x, const U8 y, const U8 z, const U8 w) {
    _imInterface->Attribute4ub(attribLocation, x, y, z, w);
    AttributeDescriptor& desc = _vertexFormat[attribLocation];
    desc._bindingIndex = to_U16(attribLocation);
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 4u;
    desc._dataType = GFXDataFormat::UNSIGNED_BYTE;

    _dirty = true;
}

void IMPrimitive::attribute4f(const U32 attribLocation, const F32 x, const F32 y, const F32 z, const F32 w) {
    _imInterface->Attribute4f(attribLocation, x, y, z, w);
    AttributeDescriptor& desc = _vertexFormat[attribLocation];
    desc._bindingIndex = to_U16(attribLocation);
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 4u;
    desc._dataType = GFXDataFormat::FLOAT_32;

    _dirty = true;
}

void IMPrimitive::attribute1i(const U32 attribLocation, const I32 value) {
    _imInterface->Attribute1i(attribLocation, value);
    AttributeDescriptor& desc = _vertexFormat[attribLocation];
    desc._bindingIndex = to_U16(attribLocation);
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 1u;
    desc._dataType = Divide::GFXDataFormat::SIGNED_INT;

    _dirty = true;
}

void IMPrimitive::end() {
    _imInterface->End();
}

void IMPrimitive::endBatch() noexcept {
    _imInterface->EndBatch();
}

void IMPrimitive::fromLines(const LineDescriptor& lines) {
    fromLines(lines._lines.data(), lines._lines.size());
}

void IMPrimitive::fromLines(const LineDescriptor* lines, size_t count) {
    for (size_t i = 0u; i < count; ++i) {
        fromLines(lines[i]._lines.data(), lines[i]._lines.size());
    }
}
void IMPrimitive::fromFrustum(const FrustumDescriptor& frustum) {
    fromFrustums(&frustum, 1u);
}

void IMPrimitive::fromFrustums(const FrustumDescriptor* frustums, size_t count) {
    Line temp = {};
    std::array<Line, to_base(FrustumPlane::COUNT) * 2> lines = {};
    std::array<vec3<F32>, to_base(FrustumPoints::COUNT)> corners = {};

    // Create the object containing all of the lines
    beginBatch(true, to_U32(lines.size() * count) * 2u, 2);

        for (size_t i = 0u; i < count; ++i) {
            U8 lineCount = 0;

            frustums[i].frustum.getCornersWorldSpace(corners);
            const FColour3& endColour = frustums[i].colour;
            const FColour3 startColour = endColour * 0.25f;

            const UColour4 startColourU = Util::ToUIntColour(startColour);
            const UColour4 endColourU = Util::ToUIntColour(endColour);

            // Draw Near Plane
            temp.positionStart(corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)]);
            temp.colourStart(startColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)]);
            temp.colourStart(startColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_TOP)]);
            temp.colourStart(startColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::NEAR_LEFT_TOP)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]);
            temp.colourStart(startColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            // Draw Far Plane
            temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]);
            temp.positionEnd(corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]);
            temp.colourStart(endColourU);
            temp.colourEnd(endColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]);
            temp.positionEnd(corners[to_base(FrustumPoints::FAR_RIGHT_TOP)]);
            temp.colourStart(endColourU);
            temp.colourEnd(endColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_TOP)]);
            temp.positionEnd(corners[to_base(FrustumPoints::FAR_LEFT_TOP)]);
            temp.colourStart(endColourU);
            temp.colourEnd(endColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_TOP)]);
            temp.positionEnd(corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]);
            temp.colourStart(endColourU);
            temp.colourEnd(endColourU);
            lines[lineCount++] = temp;

            // Connect Planes
            temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)]);
            temp.colourStart(endColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::FAR_RIGHT_TOP)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)]);
            temp.colourStart(endColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_TOP)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_TOP)]);
            temp.colourStart(endColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;

            temp.positionStart(corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)]);
            temp.positionEnd(corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)]);
            temp.colourStart(endColourU);
            temp.colourEnd(startColourU);
            lines[lineCount++] = temp;
            fromLinesInternal(lines.data(), lineCount);
    }
    endBatch();
}

void IMPrimitive::fromOBB(const OBBDescriptor& box) {
    fromOBBs(&box, 1u);
}

void IMPrimitive::fromOBBs(const OBBDescriptor* boxes, const size_t count) {
    if (count == 0u) {
        return;
    }
    std::array<Line, 12> lines = {};

    // Create the object containing all of the lines
    beginBatch(true, 12 * to_U32(count) * 2 * 14, 2);
        for (size_t i = 0u; i < count; ++i) {
            const OBBDescriptor& descriptor = boxes[i];
            OBB::OOBBEdgeList edges = descriptor.box.edgeList();
            for (U8 j = 0u; j < 12u; ++j)
            {
                lines[j].positionStart(edges[j]._start);
                lines[j].positionEnd(edges[j]._end);
                lines[j].colourStart(descriptor.colour);
                lines[j].colourEnd(descriptor.colour);
            }

            fromLinesInternal(lines.data(), lines.size());
        }
    endBatch();
}

void IMPrimitive::fromBox(const BoxDescriptor& box) {
    fromBoxes(&box, 1u);
}

void IMPrimitive::fromBoxes(const BoxDescriptor* boxes, const size_t count) {
    if (count == 0u) {
        return;
    }

    // Create the object
    beginBatch(true, to_U32(count * 16u), 1);
        for (size_t i = 0u; i < count; ++i) {
            const BoxDescriptor& box = boxes[i];
            const UColour4& colour = box.colour;
            const vec3<F32>& min = box.min;
            const vec3<F32>& max = box.max;

            // Set it's colour
            attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(colour));
            // Draw the bottom loop
            begin(PrimitiveTopology::LINE_STRIP);
                vertex(min.x, min.y, min.z);
                vertex(max.x, min.y, min.z);
                vertex(max.x, min.y, max.z);
                vertex(min.x, min.y, max.z);
                vertex(min.x, min.y, min.z);
            end();
            // Draw the top loop
            begin(PrimitiveTopology::LINE_STRIP);
                vertex(min.x, max.y, min.z);
                vertex(max.x, max.y, min.z);
                vertex(max.x, max.y, max.z);
                vertex(min.x, max.y, max.z);
                vertex(min.x, max.y, min.z);
            end();
            // Connect the top to the bottom
            begin(PrimitiveTopology::LINES);
                vertex(min.x, min.y, min.z);
                vertex(min.x, max.y, min.z);
                vertex(max.x, min.y, min.z);
                vertex(max.x, max.y, min.z);
                vertex(max.x, min.y, max.z);
                vertex(max.x, max.y, max.z);
                vertex(min.x, min.y, max.z);
                vertex(min.x, max.y, max.z);
            end();
        }
    // Finish our object
    endBatch();
}

void IMPrimitive::fromSphere(const SphereDescriptor& sphere) {
    fromSpheres(&sphere, 1u);
}

void IMPrimitive::fromSpheres(const SphereDescriptor* spheres, const size_t count) {
    if (count == 0u) {
        return;
    }

    beginBatch(true, 32u * ((32u + 1) * 2), 1);
    for (size_t c = 0u; c < count; ++c) {
        const SphereDescriptor& sphere = spheres[c];
        const F32 drho = M_PI_f / sphere.stacks;
        const F32 dtheta = 2.f * M_PI_f / sphere.slices;
        const F32 ds = 1.f / sphere.slices;
        const F32 dt = 1.f / sphere.stacks;

        F32 t = 1.f;
        // Create the object
            attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(sphere.colour));
            begin(PrimitiveTopology::LINE_STRIP);
                vec3<F32> startVert{};
                for (U32 i = 0u; i < sphere.stacks; i++) {
                    const F32 rho = i * drho;
                    const F32 srho = std::sin(rho);
                    const F32 crho = std::cos(rho);
                    const F32 srhodrho = std::sin(rho + drho);
                    const F32 crhodrho = std::cos(rho + drho);

                    F32 s = 0.0f;
                    for (U32 j = 0; j <= sphere.slices; j++) {
                        const F32 theta = j == sphere.slices ? 0.0f : j * dtheta;
                        const F32 stheta = -std::sin(theta);
                        const F32 ctheta = std::cos(theta);

                        F32 x = stheta * srho;
                        F32 y = ctheta * srho;
                        F32 z = crho;
                        const vec3<F32> vert1{
                            x * sphere.radius + sphere.center.x,
                            y * sphere.radius + sphere.center.y,
                            z * sphere.radius + sphere.center.z
                        };
                        vertex(vert1);
                        x = stheta * srhodrho;
                        y = ctheta * srhodrho;
                        z = crhodrho;
                        s += ds;
                        vertex(x * sphere.radius + sphere.center.x,
                               y * sphere.radius + sphere.center.y,
                               z * sphere.radius + sphere.center.z);

                        if (i == 0 && j == 0) {
                            startVert = vert1;
                        }
                    }
                    t -= dt;
                }
                vertex(startVert.x, startVert.y, startVert.z);
            end();
    }
    endBatch();
}

//ref: http://www.freemancw.com/2012/06/opengl-cone-function/
void IMPrimitive::fromCone(const ConeDescriptor& cone) {
    fromCones(&cone, 1u);
}

void IMPrimitive::fromCones(const ConeDescriptor* cones, const size_t count) {
    if (count == 0u) {
        return;
    }

    beginBatch(true, to_U32(count * (32u + 1)), 1u);

    for (size_t i = 0u; i < count; ++i) {
        const ConeDescriptor& cone = cones[i];

        const U8 slices = std::min(cone.slices, to_U8(32u));
        const F32 angInc = 360.0f / slices * M_PIDIV180_f;
        const vec3<F32> invDirection = -cone.direction;
        const vec3<F32> c = cone.root + -invDirection * cone.length;
        const vec3<F32> e0 = Perpendicular(invDirection);
        const vec3<F32> e1 = Cross(e0, invDirection);

        // calculate points around directrix
        std::array<vec3<F32>, 32u> pts = {};
        for (size_t j = 0u; j < slices; ++j) {
            const F32 rad = angInc * j;
            pts[j] = c + (e0 * std::cos(rad) + e1 * std::sin(rad)) * cone.radius;
        }

        // draw cone top
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(cone.colour));
        // Top
        begin(PrimitiveTopology::TRIANGLE_FAN);
            vertex(cone.root);
            for (U8 j = 0u; j < slices; ++j) {
                vertex(pts[j]);
            }
        end();

        // Bottom
        begin(PrimitiveTopology::TRIANGLE_FAN);
            vertex(c);
            for (I8 j = slices - 1; j >= 0; --j) {
                vertex(pts[j]);
            }
        end();
    }
    endBatch();
}

void IMPrimitive::fromLines(const Line* lines, const size_t count) {
    if (count == 0u) {
        return;
    }

    // Check if we have a valid list. The list can be programmatically
    // generated, so this check is required
    // Create the object containing all of the lines
    beginBatch(true, to_U32(count) * 2 * 14, 2);
        fromLinesInternal(lines, count);
    // Finish our object
    endBatch();
}

void IMPrimitive::fromLinesInternal(const Line* lines, size_t count) {
    if (count == 0u) {
        return;
    }

    attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(lines[0].colourStart()));
    attribute2f(to_base(AttribLocation::GENERIC), vec2<F32>(1.f, 1.f));
    // Set the mode to line rendering
    begin(PrimitiveTopology::LINES);
    // Add every line in the list to the batch
    for (size_t i = 0u; i < count; ++i) {
        const Line& line = lines[i];
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line.colourStart()));
        attribute2f(to_base(AttribLocation::GENERIC), vec2<F32>(line.widthStart(), line.widthEnd()));
        vertex(line.positionStart());

        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line.colourEnd()));
        attribute2f(to_base(AttribLocation::GENERIC), vec2<F32>(line.widthStart(), line.widthEnd()));
        vertex(line.positionEnd());

    }
    end();
}

void IMPrimitive::setPushConstants(const PushConstants& constants) {
    _additionalConstats = constants;
}

void IMPrimitive::getCommandBuffer(PipelineDescriptor& pipelineDescriptorInOut,
                                   GFX::CommandBuffer& commandBufferInOut) 
{
    getCommandBuffer(MAT4_IDENTITY, pipelineDescriptorInOut, commandBufferInOut);
}

void IMPrimitive::getCommandBuffer(const mat4<F32>& worldMatrix,
                                   PipelineDescriptor& pipelineDescriptorInOut,
                                   GFX::CommandBuffer& commandBufferInOut)
{
    getCommandBuffer(worldMatrix, {}, 0u, pipelineDescriptorInOut, commandBufferInOut);
}

void IMPrimitive::getCommandBuffer(const mat4<F32>& worldMatrix, 
                                   const TextureData& texture,
                                   const size_t samplerHash,
                                   PipelineDescriptor& pipelineDescriptorInOut,
                                   GFX::CommandBuffer& commandBufferInOut)
{
    if (!_imInterface->PrepareRender()) {
        return;
    }

    const bool hasTexture = IsValid(texture);
    auto& batchData = _imInterface->GetData();

    GenericVertexData::SetBufferParams params{};
    params._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
    params._bufferParams._updateUsage = BufferUpdateUsage::CPU_R_GPU_W;
    params._bufferParams._elementSize = sizeof(NS_GLIM::Glim4ByteData);
    params._useChunkAllocation = true;

    GenericVertexData::IndexBuffer idxBuff{};
    idxBuff.smallIndices = false;
    idxBuff.dynamic = true;

    const auto setParams = [&params, &idxBuff, &batchData](GenericVertexData* buffer, const NS_GLIM::GLIM_BUFFER_TYPE type) {
        U8 bufferIdx = 0u;
        // Set positions
        {
            params._bindConfig = { bufferIdx++, 0u };
            params._bufferParams._elementCount = to_U32(batchData.m_PositionData.size());
            params._initialData = { batchData.m_PositionData.data(), batchData.m_PositionData.size() * sizeof(NS_GLIM::Glim4ByteData) };
            params._elementStride = sizeof(NS_GLIM::Glim4ByteData) * 3;
            buffer->setBuffer(params);
        }

        // now upload each attribute array one after another
        for (auto& [index, data] : batchData.m_Attributes) {
            assert(index != 0u);
            params._bindConfig = { bufferIdx++, index };
            params._bufferParams._elementCount = to_U32(data.m_ArrayData.size());
            params._initialData = { data.m_ArrayData.data(), data.m_ArrayData.size() * sizeof(NS_GLIM::Glim4ByteData) };
            params._elementStride = sizeof(NS_GLIM::Glim4ByteData) * GetSizeFactor(data.m_DataType);
            buffer->setBuffer(params);
        }

        switch (type) {
            case NS_GLIM::GLIM_BUFFER_TYPE::LINES: {
                idxBuff.count = batchData.m_IndexBuffer_Lines.size();
                idxBuff.data = batchData.m_IndexBuffer_Lines.data();
            } break;
            case NS_GLIM::GLIM_BUFFER_TYPE::POINTS: {
                idxBuff.count = batchData.m_IndexBuffer_Points.size();
                idxBuff.data = batchData.m_IndexBuffer_Points.data();
            } break;
            case NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES: {
                idxBuff.count = batchData.m_IndexBuffer_Triangles.size();
                idxBuff.data = batchData.m_IndexBuffer_Triangles.data();
            } break;
            case NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME: {
                idxBuff.count = batchData.m_IndexBuffer_Wireframe.size();
                idxBuff.data = batchData.m_IndexBuffer_Wireframe.data();
            } break;
        }

        buffer->setIndexBuffer(idxBuff);
    };

    const auto drawType = [&](PipelineDescriptor& pipelineDescriptor, const NS_GLIM::GLIM_BUFFER_TYPE type) {
        GenericVertexData* buffer = _dataBuffers[to_base(type)].get();
        if (_dirty) {
            setParams(buffer, type);
        }

        GFX::EnqueueCommand<GFX::BindPipelineCommand>(commandBufferInOut)->_pipeline = _context.newPipeline(pipelineDescriptor);
        PushConstants& constants = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(commandBufferInOut)->_constants;
        constants.set(_ID("dvd_WorldMatrix"), GFX::PushConstantType::MAT4, worldMatrix);
        constants.set(_ID("useTexture"), GFX::PushConstantType::BOOL, hasTexture);
        if (!_additionalConstats.empty()) {
            bool partial = false;
            Merge(constants, _additionalConstats, partial);
        }

        GenericDrawCommand cmd{};
        cmd._drawCount = 1u;
        cmd._cmd.primCount = 1u;
        cmd._cmd.indexCount = to_U32(buffer->idxBuffer().count);
        cmd._sourceBuffer = buffer->handle();
        GFX::EnqueueCommand(commandBufferInOut, GFX::DrawCommand{ cmd });
    };

    GFX::EnqueueCommand(commandBufferInOut, GFX::BeginDebugScopeCommand{ Util::StringFormat("Render IMPrimitive [ %s ]", _name.empty() ? "generic" : _name.c_str()).c_str() });

    DIVIDE_ASSERT(pipelineDescriptorInOut._shaderProgramHandle != ShaderProgram::INVALID_HANDLE, "IMPrimitive error: Draw call received without a valid shader defined!");

    if (hasTexture) {
        auto& set = GFX::EnqueueCommand<GFX::BindDescriptorSetsCommand>(commandBufferInOut)->_set;
        set._usage = DescriptorSetUsage::PER_DRAW_SET;

        auto& binding = set._bindings.emplace_back();
        binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
        binding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::FRAGMENT;
        binding._resourceSlot = to_U8(TextureUsage::UNIT0);

        auto& imageSampler = binding._data.As<DescriptorCombinedImageSampler>();
        imageSampler._image = texture;
        imageSampler._samplerHash = samplerHash;
    }

    for (auto& desc : _vertexFormat) {
        if (desc._dataType != GFXDataFormat::COUNT) {
            pipelineDescriptorInOut._vertexFormat[desc._bindingIndex] = desc;
        }
    }

    if (_dirty) {
        _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES)] = !batchData.m_IndexBuffer_Triangles.empty();
        _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME)] = !batchData.m_IndexBuffer_Wireframe.empty();
        _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::LINES)] = !batchData.m_IndexBuffer_Lines.empty();
        _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::POINTS)] = !batchData.m_IndexBuffer_Points.empty();
    }

    if (!_forceWireframe && _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES)])
    {
        pipelineDescriptorInOut._primitiveTopology = PrimitiveTopology::TRIANGLES;
        drawType(pipelineDescriptorInOut, NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES);
    }
    else if (_drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME)])
    {
        pipelineDescriptorInOut._primitiveTopology = PrimitiveTopology::LINES;
        drawType(pipelineDescriptorInOut, NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME);
    }

    // render all lines
    if (_drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::LINES)]) {
        pipelineDescriptorInOut._primitiveTopology = PrimitiveTopology::LINES;
        drawType(pipelineDescriptorInOut, NS_GLIM::GLIM_BUFFER_TYPE::LINES);
    }

    // render all points
    if (_drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::POINTS)]) {
        pipelineDescriptorInOut._primitiveTopology = PrimitiveTopology::POINTS;
        drawType(pipelineDescriptorInOut, NS_GLIM::GLIM_BUFFER_TYPE::POINTS);
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(commandBufferInOut);

    if (_dirty) {
        // free the temporary buffer in RAM
        for (auto& [index, data] : batchData.m_Attributes) {
            data.m_ArrayData.resize(0);
        }

        batchData.m_PositionData.resize(0);
        batchData.m_IndexBuffer_Wireframe.resize(0);
        batchData.m_IndexBuffer_Triangles.resize(0);
        batchData.m_IndexBuffer_Lines.resize(0);
        batchData.m_IndexBuffer_Points.resize(0);
        _dirty = false;
    }
}

};