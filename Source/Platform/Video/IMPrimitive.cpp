

#include "Headers/IMPrimitive.h"

#include "Core/Math/BoundingVolumes/Headers/OBB.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Headers/CommandBufferPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"

namespace Divide
{

namespace
{
    std::array<NS_GLIM::GLIM_ENUM, to_base(PrimitiveTopology::COUNT)> glimPrimitiveType;

    inline size_t GetSizeFactor(const NS_GLIM::GLIM_ENUM dataType) noexcept
    {
        switch (dataType)
        {
            case NS_GLIM::GLIM_ENUM::GLIM_1I:
            case NS_GLIM::GLIM_ENUM::GLIM_1F:
            case NS_GLIM::GLIM_ENUM::GLIM_4UB: return 1u;

            case NS_GLIM::GLIM_ENUM::GLIM_2F:
            case NS_GLIM::GLIM_ENUM::GLIM_2I: return 2u;

            case NS_GLIM::GLIM_ENUM::GLIM_3F:
            case NS_GLIM::GLIM_ENUM::GLIM_3I: return 3u;

            case NS_GLIM::GLIM_ENUM::GLIM_4F:
            case NS_GLIM::GLIM_ENUM::GLIM_4I: return 4u;

            default: break;
        }

        DIVIDE_UNEXPECTED_CALL();
        return 0u;
    }

    inline PrimitiveTopology GetTopology(const NS_GLIM::GLIM_BUFFER_TYPE type)
    {
        switch (type)
        {
            case NS_GLIM::GLIM_BUFFER_TYPE::POINTS:    return PrimitiveTopology::POINTS;
            case NS_GLIM::GLIM_BUFFER_TYPE::LINES:     return PrimitiveTopology::LINES;
            case NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME: return PrimitiveTopology::LINES;
            case NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES: return PrimitiveTopology::TRIANGLES;

            default: break;
        }

        return PrimitiveTopology::COUNT;
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
    glimPrimitiveType[to_base(PrimitiveTopology::MESHLET)] = NS_GLIM::GLIM_ENUM::GLIM_POLYGON;
}

IMPrimitive::IMPrimitive(GFXDevice& context, const Str<64>& name)
    : _name(name)
    , _context(context)
{
    _imInterface = std::make_unique<NS_GLIM::GLIM_BATCH>();

    reset();
}

void IMPrimitive::reset()
{
    clearBatch();
    _drawFlags.fill(false);
    _indexRange.fill({});
    _pipelines.fill(nullptr);
    _indices.resize(0u);

    // Create general purpose render state blocks
    _basePipelineDescriptor._primitiveTopology = PrimitiveTopology::COUNT;
    _basePipelineDescriptor._vertexFormat = {};

    AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[0u];
    desc._vertexBindingIndex = 0u;
    desc._componentsPerElement = 3u;
    desc._dataType = GFXDataFormat::FLOAT_32;
    desc._normalized = false;
    desc._strideInBytes = 0;
}


void IMPrimitive::beginBatch(const bool reserveBuffers, const U32 vertexCount, const U32 attributeCount)
{
    _imInterface->BeginBatch(reserveBuffers, vertexCount, attributeCount);
}

void IMPrimitive::clearBatch()
{
    _imInterface->Clear(true, 64 * 3, 1);
}

bool IMPrimitive::hasBatch() const noexcept
{
    return !_imInterface->isCleared();
}

void IMPrimitive::begin(const PrimitiveTopology type)
{
    _imInterface->Begin(glimPrimitiveType[to_U32(type)]);
}

void IMPrimitive::vertex(const F32 x, const  F32 y, const F32 z)
{
    _imInterface->Vertex(x, y, z);
}

void IMPrimitive::attribute1f(const U32 attribLocation, const F32 value)
{
    _imInterface->Attribute1f(attribLocation, value);

    AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[attribLocation];
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 1u;
    desc._dataType = GFXDataFormat::FLOAT_32;
}

void IMPrimitive::attribute2f(const U32 attribLocation, const float2 value)
{
    _imInterface->Attribute2f(attribLocation, value.x, value.y);
    AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[attribLocation];
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 2u;
    desc._dataType = GFXDataFormat::FLOAT_32;
}

void IMPrimitive::attribute3f(const U32 attribLocation, const float3 value)
{
    _imInterface->Attribute3f(attribLocation, value.x, value.y, value.z);
    Divide::AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[attribLocation];
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 3u;
    desc._dataType = GFXDataFormat::FLOAT_32;
}

void IMPrimitive::attribute4ub(const U32 attribLocation, const U8 x, const U8 y, const U8 z, const U8 w)
{
    _imInterface->Attribute4ub(attribLocation, x, y, z, w);
    AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[attribLocation];
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 4u;
    desc._dataType = GFXDataFormat::UNSIGNED_BYTE;
}

void IMPrimitive::attribute4f(const U32 attribLocation, const F32 x, const F32 y, const F32 z, const F32 w)
{
    _imInterface->Attribute4f(attribLocation, x, y, z, w);
    AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[attribLocation];
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 4u;
    desc._dataType = GFXDataFormat::FLOAT_32;
}

void IMPrimitive::attribute1i(const U32 attribLocation, const I32 value)
{
    _imInterface->Attribute1i(attribLocation, value);
    AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[attribLocation];
    desc._normalized = false;
    desc._strideInBytes = 0u;
    desc._componentsPerElement = 1u;
    desc._dataType = Divide::GFXDataFormat::SIGNED_INT;
}

void IMPrimitive::end()
{
    _imInterface->End();
}

void IMPrimitive::endBatch() noexcept
{
    auto& batchData = _imInterface->EndBatch();

    _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES)] = !batchData.m_IndexBuffer_Triangles.empty();
    _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME)] = !batchData.m_IndexBuffer_Wireframe.empty();
    _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::LINES)] = !batchData.m_IndexBuffer_Lines.empty();
    _drawFlags[to_base(NS_GLIM::GLIM_BUFFER_TYPE::POINTS)] = !batchData.m_IndexBuffer_Points.empty();
    _memCmd._bufferLocks.resize(0);

    if ((batchData.m_Attributes.size() + 1u) > _buffers.size())
    {
        _buffers.resize(batchData.m_Attributes.size() + 1u);
        _bufferHandles.resize(batchData.m_Attributes.size() + 1u, GPUVertexBuffer::INVALID_HANDLE);
    }

    efficient_clear(_basePipelineDescriptor._vertexFormat._vertexBindings);

    // Set positions
    {
        auto& posBuffer = _buffers[0u];
        if ( !posBuffer )
        {
            posBuffer = std::make_unique<GPUVertexBuffer>(_context, _name + "_IMPrimitive_Positions");
            posBuffer->_vertexBuffer = _context.newGPUBuffer(1u, _name + "_IMPrimitive_Positions_Buffer");
            posBuffer->_indexBuffer = _context.newGPUBuffer(1u, _name + "_IMPrimitive_Indices_Buffer");
        }

        GPUBuffer::SetBufferParams params = {};
        params._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        params._bufferParams._usageType = BufferUsageType::VERTEX_BUFFER;
        params._bufferParams._elementSize = sizeof(NS_GLIM::Glim4ByteData);
        params._bufferParams._elementCount = to_U32(batchData.m_PositionData.size());
        params._initialData = { batchData.m_PositionData.data(), batchData.m_PositionData.size() * sizeof(NS_GLIM::Glim4ByteData) };
        _memCmd._bufferLocks.emplace_back(posBuffer->_vertexBuffer->setBuffer(params));
        
        posBuffer->_vertexBufferBinding._bindIdx = 0u;
        posBuffer->_vertexBufferBinding._elementStride = sizeof(NS_GLIM::Glim4ByteData) * 3;

        auto& vertBinding = _basePipelineDescriptor._vertexFormat._vertexBindings.emplace_back();
        vertBinding._bufferBindIndex = posBuffer->_vertexBufferBinding._bindIdx;
        vertBinding._strideInBytes = posBuffer->_vertexBufferBinding._elementStride;
        _bufferHandles[0u] = posBuffer->_handle;
    }

    efficient_clear(batchData.m_PositionData);

    U8 bufferIdx = 1u;
    // now upload each attribute array one after another
    for (auto& [index, data] : batchData.m_Attributes)
    {
        assert(index != 0u);
        auto& attribBuffer = _buffers[bufferIdx];
        if (!attribBuffer)
        {
            attribBuffer = std::make_unique<GPUVertexBuffer>(_context, Util::StringFormat("{}_IMPrimitive_Atrrib_Buffer_{}", _name, bufferIdx).c_str());
            attribBuffer->_vertexBuffer = _context.newGPUBuffer(1u, _name + Util::StringFormat("{}_IMPrimitive_Atrrib_VB_Buffer_{}", _name, bufferIdx).c_str());
        }

        GPUBuffer::SetBufferParams params = {};
        params._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
        params._bufferParams._usageType = BufferUsageType::VERTEX_BUFFER;
        params._bufferParams._elementSize = sizeof(NS_GLIM::Glim4ByteData);
        params._bufferParams._elementCount = to_U32(data.m_ArrayData.size());
        params._initialData = { data.m_ArrayData.data(), data.m_ArrayData.size() * sizeof(NS_GLIM::Glim4ByteData) };
        _memCmd._bufferLocks.emplace_back(attribBuffer->_vertexBuffer->setBuffer(params));

        attribBuffer->_vertexBufferBinding._bindIdx = to_U16(index);
        attribBuffer->_vertexBufferBinding._elementStride = sizeof(NS_GLIM::Glim4ByteData) * GetSizeFactor(data.m_DataType);

        AttributeDescriptor& desc = _basePipelineDescriptor._vertexFormat._attributes[index];
        desc._vertexBindingIndex = attribBuffer->_vertexBufferBinding._bindIdx;

        auto& vertBinding = _basePipelineDescriptor._vertexFormat._vertexBindings.emplace_back();
        vertBinding._bufferBindIndex = desc._vertexBindingIndex;
        vertBinding._strideInBytes = attribBuffer->_vertexBufferBinding._elementStride;
        _bufferHandles[bufferIdx] = attribBuffer->_handle;
        ++bufferIdx;
    }

    for (auto& [index, data] : batchData.m_Attributes)
    {
        efficient_clear(data.m_ArrayData);
    }

    // Gather all the required sizes and offsets to allocate needed memory upfront.
    size_t requiredSize = 0u;
    const auto updateRange = [&requiredSize](const vector<U32>& indices, BufferRange<U32>& rangeInOut)
    {
        const size_t count = indices.size();

        rangeInOut._startOffset = to_U32(requiredSize);
        rangeInOut._length = to_U32(count);
        requiredSize += count;
    };

    for (U8 i = 0u; i < to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT); ++i)
    {
        if (!_drawFlags[i])
        {
            continue;
        }

        const NS_GLIM::GLIM_BUFFER_TYPE glimType = static_cast<NS_GLIM::GLIM_BUFFER_TYPE>(i);

        BufferRange<U32>& range = _indexRange[i];

        switch (glimType)
        { 
            case NS_GLIM::GLIM_BUFFER_TYPE::LINES:     updateRange(batchData.m_IndexBuffer_Lines,     range); break;
            case NS_GLIM::GLIM_BUFFER_TYPE::POINTS:    updateRange(batchData.m_IndexBuffer_Points,    range); break;
            case NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES: updateRange(batchData.m_IndexBuffer_Triangles, range); break;
            case NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME: updateRange(batchData.m_IndexBuffer_Wireframe, range); break;
            default:
            case NS_GLIM::GLIM_BUFFER_TYPE::COUNT:     DIVIDE_UNEXPECTED_CALL();                              break;
        }
    }

    // Reserve the memory we need and actually construct the index buffer
    _indices.resize(0);
    _indices.reserve(requiredSize);

    for (U8 i = 0u; i < to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT); ++i)
    {
        if (!_drawFlags[i])
        {
            continue;
        }

        const NS_GLIM::GLIM_BUFFER_TYPE glimType = static_cast<NS_GLIM::GLIM_BUFFER_TYPE>(i);

        _basePipelineDescriptor._primitiveTopology = GetTopology(glimType);
        _pipelines[i] = _context.newPipeline(_basePipelineDescriptor);

        switch (glimType)
        {
            case NS_GLIM::GLIM_BUFFER_TYPE::LINES:     insert(_indices, batchData.m_IndexBuffer_Lines);     break;
            case NS_GLIM::GLIM_BUFFER_TYPE::POINTS:    insert(_indices, batchData.m_IndexBuffer_Points);    break;
            case NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES: insert(_indices, batchData.m_IndexBuffer_Triangles); break;
            case NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME: insert(_indices, batchData.m_IndexBuffer_Wireframe); break;
            default:
            case NS_GLIM::GLIM_BUFFER_TYPE::COUNT:     DIVIDE_UNEXPECTED_CALL();                            break;
        }
    }

    efficient_clear(batchData.m_IndexBuffer_Wireframe);
    efficient_clear(batchData.m_IndexBuffer_Triangles);
    efficient_clear(batchData.m_IndexBuffer_Lines);
    efficient_clear(batchData.m_IndexBuffer_Points);

    GPUBuffer::SetBufferParams ibParams = {};
    ibParams._bufferParams._updateFrequency = BufferUpdateFrequency::OCASSIONAL;
    ibParams._bufferParams._usageType = BufferUsageType::INDEX_BUFFER;
    ibParams._bufferParams._elementSize = sizeof(U32);
    ibParams._bufferParams._elementCount = to_U32(_indices.size());
    ibParams._initialData = { _indices.data(), _indices.size() * sizeof(U32) };
    _memCmd._bufferLocks.emplace_back(_buffers[0u]->_indexBuffer->setBuffer(ibParams));
}

void IMPrimitive::fromLines(const IM::LineDescriptor& lines)
{
    fromLines(lines._lines.data(), lines._lines.size());
}

void IMPrimitive::fromLines(const IM::LineDescriptor* lines, size_t count)
{
    for (size_t i = 0u; i < count; ++i)
    {
        fromLines(lines[i]._lines.data(), lines[i]._lines.size());
    }
}
void IMPrimitive::fromFrustum(const IM::FrustumDescriptor& frustum)
{
    fromFrustums(&frustum, 1u);
}

void IMPrimitive::fromFrustums(const IM::FrustumDescriptor* frustums, size_t count)
{
    Line temp = {};
    std::array<Line, to_base(FrustumPlane::COUNT) * 2> lines = {};
    std::array<float3, to_base(FrustumPoints::COUNT)> corners = {};

    // Create the object containing all of the lines
    beginBatch(true, to_U32(lines.size() * count) * 2u, 2);
        for (size_t i = 0u; i < count; ++i)
        {
            U8 lineCount = 0;

            frustums[i].frustum.getCornersWorldSpace(corners);
            const UColour4& endColour = frustums[i].colour;
            const UColour4 startColour = Util::ToByteColour( Util::ToFloatColour( frustums[i].colour ) * 0.25f);

            // Draw Near Plane
            temp._positionStart = corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)];
            temp._colourStart = startColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)];
            temp._colourStart = startColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_LEFT_TOP)];
            temp._colourStart = startColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::NEAR_LEFT_TOP)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)];
            temp._colourStart = startColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            // Draw Far Plane
            temp._positionStart = corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)];
            temp._positionEnd = corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)];
            temp._colourStart = endColour;
            temp._colourEnd = endColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)];
            temp._positionEnd = corners[to_base(FrustumPoints::FAR_RIGHT_TOP)];
            temp._colourStart = endColour;
            temp._colourEnd = endColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::FAR_RIGHT_TOP)];
            temp._positionEnd = corners[to_base(FrustumPoints::FAR_LEFT_TOP)];
            temp._colourStart = endColour;
            temp._colourEnd = endColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::FAR_LEFT_TOP)];
            temp._positionEnd = corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)];
            temp._colourStart = endColour;
            temp._colourEnd = endColour;
            lines[lineCount++] = temp;

            // Connect Planes
            temp._positionStart = corners[to_base(FrustumPoints::FAR_RIGHT_BOTTOM)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_RIGHT_BOTTOM)];
            temp._colourStart = endColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::FAR_RIGHT_TOP)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_RIGHT_TOP)];
            temp._colourStart = endColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::FAR_LEFT_TOP)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_LEFT_TOP)];
            temp._colourStart = endColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;

            temp._positionStart = corners[to_base(FrustumPoints::FAR_LEFT_BOTTOM)];
            temp._positionEnd = corners[to_base(FrustumPoints::NEAR_LEFT_BOTTOM)];
            temp._colourStart = endColour;
            temp._colourEnd = startColour;
            lines[lineCount++] = temp;
            fromLinesInternal(lines.data(), lineCount);
        }
    endBatch();
}

void IMPrimitive::fromOBB(const IM::OBBDescriptor& box)
{
    fromOBBs(&box, 1u);
}

void IMPrimitive::fromOBBs(const IM::OBBDescriptor* boxes, const size_t count)
{
    if (count == 0u)
    {
        return;
    }

    std::array<Line, 12> lines = {};

    // Create the object containing all of the lines
    beginBatch(true, 12 * to_U32(count) * 2 * 14, 2);
        for (size_t i = 0u; i < count; ++i)
        {
            const IM::OBBDescriptor& descriptor = boxes[i];
            OBB::OOBBEdgeList edges = descriptor.box.edgeList();
            for (U8 j = 0u; j < 12u; ++j)
            {
                lines[j]._positionStart = edges[j]._start;
                lines[j]._positionEnd = edges[j]._end;
                lines[j]._colourStart = descriptor.colour;
                lines[j]._colourEnd = descriptor.colour;
            }

            fromLinesInternal(lines.data(), lines.size());
        }
    endBatch();
}

void IMPrimitive::fromBox(const IM::BoxDescriptor& box)
{
    fromBoxes(&box, 1u);
}

void IMPrimitive::fromBoxes(const IM::BoxDescriptor* boxes, const size_t count)
{
    if (count == 0u)
    {
        return;
    }

    // Create the object
    beginBatch(true, to_U32(count * 16u), 1);
        attribute2f( to_base( AttribLocation::GENERIC ), float2( 1.f, 1.f ) );
        for (size_t i = 0u; i < count; ++i)
        {
            const IM::BoxDescriptor& box = boxes[i];
            const UColour4& colour = box.colour;
            const float3& min = box.min;
            const float3& max = box.max;

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

void IMPrimitive::fromSphere(const IM::SphereDescriptor& sphere)
{
    fromSpheres(&sphere, 1u);
}

void IMPrimitive::fromSpheres(const IM::SphereDescriptor* spheres, const size_t count)
{
    if (count == 0u)
    {
        return;
    }

    beginBatch(true, 32u * ((32u + 1) * 2), 1);
    attribute2f( to_base( AttribLocation::GENERIC ), float2( 1.f, 1.f ) );

    for (size_t c = 0u; c < count; ++c)
    {
        const IM::SphereDescriptor& sphere = spheres[c];
        const F32 drho = M_PI_f / sphere.stacks;
        const F32 dtheta = 2.f * M_PI_f / sphere.slices;

        // Create the object
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(sphere.colour));
        begin(PrimitiveTopology::LINE_STRIP);
            float3 startVert{};
            for (U32 i = 0u; i < sphere.stacks; i++)
            {
                const F32 rho = i * drho;
                const F32 srho = std::sin(rho);
                const F32 crho = std::cos(rho);
                const F32 srhodrho = std::sin(rho + drho);
                const F32 crhodrho = std::cos(rho + drho);
                for (U32 j = 0; j <= sphere.slices; j++)
                {
                    const F32 theta = j == sphere.slices ? 0.0f : j * dtheta;
                    const F32 stheta = -std::sin(theta);
                    const F32 ctheta = std::cos(theta);

                    F32 x = stheta * srho;
                    F32 y = ctheta * srho;
                    F32 z = crho;
                    const float3 vert1
                    {
                        x * sphere.radius + sphere.center.x,
                        y * sphere.radius + sphere.center.y,
                        z * sphere.radius + sphere.center.z
                    };
                    vertex(vert1);

                    x = stheta * srhodrho;
                    y = ctheta * srhodrho;
                    z = crhodrho;
                    vertex(x * sphere.radius + sphere.center.x,
                            y * sphere.radius + sphere.center.y,
                            z * sphere.radius + sphere.center.z);

                    if (i == 0 && j == 0)
                    {
                        startVert = vert1;
                    }
                }
            }
            vertex(startVert.x, startVert.y, startVert.z);
        end();
    }
    endBatch();
}

//ref: http://www.freemancw.com/2012/06/opengl-cone-function/
void IMPrimitive::fromCone(const IM::ConeDescriptor& cone)
{
    fromCones(&cone, 1u);
}

void IMPrimitive::fromCones(const IM::ConeDescriptor* cones, const size_t count)
{
    if (count == 0u)
    {
        return;
    }

    beginBatch(true, to_U32(count * (32u + 1)), 1u);
    attribute2f( to_base( AttribLocation::GENERIC ), float2( 1.f, 1.f ) );

    for (size_t i = 0u; i < count; ++i)
    {
        const IM::ConeDescriptor& cone = cones[i];

        const U8 slices = std::min(cone.slices, to_U8(32u));
        const F32 angInc = 360.0f / slices * M_PI_DIV_180_f;
        const float3 invDirection = -cone.direction;
        const float3 c = cone.root + -invDirection * cone.length;
        const float3 e0 = Perpendicular(invDirection);
        const float3 e1 = Cross(e0, invDirection);

        // calculate points around directrix
        std::array<float3, 32u> pts = {};
        for (size_t j = 0u; j < slices; ++j)
        {
            const F32 rad = angInc * j;
            pts[j] = c + (e0 * std::cos(rad) + e1 * std::sin(rad)) * cone.radius;
        }

        // draw cone top
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(cone.colour));
        // Top
        begin(PrimitiveTopology::TRIANGLE_FAN);
            vertex(cone.root);
            for (U8 j = 0u; j < slices; ++j)
            {
                vertex(pts[j]);
            }
        end();

        // Bottom
        begin(PrimitiveTopology::TRIANGLE_FAN);
            vertex(c);
            for (I8 j = slices - 1; j >= 0; --j)
            {
                vertex(pts[j]);
            }
        end();
    }
    endBatch();
}

void IMPrimitive::fromLines(const Line* lines, const size_t count)
{
    if (count == 0u)
    {
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

void IMPrimitive::fromLinesInternal(const Line* lines, size_t count)
{
    if (count == 0u)
    {
        return;
    }

    attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(lines[0]._colourStart));
    attribute2f(to_base(AttribLocation::GENERIC), float2(1.f, 1.f));
    // Set the mode to line rendering
    begin(PrimitiveTopology::LINES);
    // Add every line in the list to the batch
    for (size_t i = 0u; i < count; ++i)
    {
        const Line& line = lines[i];
        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line._colourStart));
        attribute2f(to_base(AttribLocation::GENERIC), float2(line._widthStart, line._widthEnd));
        vertex(line._positionStart);

        attribute4f(to_base(AttribLocation::COLOR), Util::ToFloatColour(line._colourEnd));
        attribute2f(to_base(AttribLocation::GENERIC), float2(line._widthStart, line._widthEnd));
        vertex(line._positionEnd);

    }
    end();
}

void IMPrimitive::setPushConstants( const PushConstantsStruct& fastData )
{
    _fastData = fastData;
}

void IMPrimitive::setUniformData( const UniformData& constants )
{
    _additionalUniforms = constants;
}

void IMPrimitive::setUniformDataAndConstants(const UniformData& constants, const PushConstantsStruct& fastData )
{
    _additionalUniforms = constants;
    _fastData = fastData;
}

void IMPrimitive::setPipelineDescriptor(const PipelineDescriptor& descriptor)
{
    DIVIDE_ASSERT(descriptor._vertexFormat._vertexBindings.empty());

    const AttributeMap existingAttributes = _basePipelineDescriptor._vertexFormat;
    _basePipelineDescriptor = descriptor;
    _basePipelineDescriptor._vertexFormat = existingAttributes;
}

void IMPrimitive::setTexture(const ImageView& texture, const SamplerDescriptor sampler)
{
    _texture = texture;
    _sampler = sampler;
}

void IMPrimitive::getCommandBuffer( GFX::CommandBuffer& commandBufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
{
    getCommandBuffer(MAT4_IDENTITY, commandBufferInOut, memCmdInOut);
}

void IMPrimitive::getCommandBuffer(const mat4<F32>& worldMatrix, GFX::CommandBuffer& commandBufferInOut, GFX::MemoryBarrierCommand& memCmdInOut )
{
    if (!_imInterface->PrepareRender())
    {
        return;
    }

    const bool useTexture = TargetType( _texture ) != TextureType::COUNT;

    _basePipelineDescriptor._shaderProgramHandle = useTexture ? _context.imShaders()->imWorldShader()
                                                              : _context.imShaders()->imWorldShaderNoTexture();

    DIVIDE_ASSERT(_basePipelineDescriptor._shaderProgramHandle != INVALID_HANDLE<ShaderProgram>, "IMPrimitive error: Draw call received without a valid shader defined!");

    _additionalUniforms.set(_ID("dvd_WorldMatrix"), PushConstantType::MAT4, worldMatrix);
    _additionalUniforms.set(_ID("useTexture"),      PushConstantType::BOOL, useTexture);

    GenericDrawCommand drawCmd{};
    drawCmd._drawCount = 1u;
    drawCmd._cmd.instanceCount = 1u;
    drawCmd._sourceBuffers = _bufferHandles.data();
    drawCmd._sourceBuffersCount = _bufferHandles.size();

    GFX::EnqueueCommand<GFX::BeginDebugScopeCommand>(commandBufferInOut)->_scopeName = _name.c_str();
    {
        auto cmd = GFX::EnqueueCommand<GFX::BindShaderResourcesCommand>(commandBufferInOut);
        cmd->_usage = DescriptorSetUsage::PER_DRAW;
        DescriptorSetBinding& binding = AddBinding( cmd->_set, 0u, ShaderStageVisibility::FRAGMENT );

        if ( TargetType( _texture ) == TextureType::COUNT )
        {
            Set( binding._data, Texture::DefaultTexture2D(), Texture::DefaultSampler());
        }
        else
        {
            Set( binding._data, _texture, _sampler );
        }

        for (U8 i = 0u; i < to_base(NS_GLIM::GLIM_BUFFER_TYPE::COUNT); ++i)
        {
            if (_drawFlags[i])
            {
                const NS_GLIM::GLIM_BUFFER_TYPE glimType = static_cast<NS_GLIM::GLIM_BUFFER_TYPE>(i);
                if ((glimType == NS_GLIM::GLIM_BUFFER_TYPE::TRIANGLES && _forceWireframe) ||
                    (glimType == NS_GLIM::GLIM_BUFFER_TYPE::WIREFRAME && !_forceWireframe))
                {
                    continue;
                }

                GFX::EnqueueCommand<GFX::BindPipelineCommand>(commandBufferInOut)->_pipeline = _pipelines[i];
                auto pushConstantsCmd = GFX::EnqueueCommand<GFX::SendPushConstantsCommand>(commandBufferInOut);
                pushConstantsCmd->_uniformData = &_additionalUniforms;
                pushConstantsCmd->_fastData = _fastData;

                const BufferRange<U32>& idxRange = _indexRange[i];
                drawCmd._cmd.indexCount = idxRange._length;
                drawCmd._cmd.firstIndex = idxRange._startOffset;
                GFX::EnqueueCommand(commandBufferInOut, GFX::DrawCommand{ drawCmd });
            }
        }
    }

    GFX::EnqueueCommand<GFX::EndDebugScopeCommand>(commandBufferInOut);

    if ( !_memCmd._bufferLocks.empty() )
    {
        memCmdInOut._bufferLocks.insert( memCmdInOut._bufferLocks.cend(), _memCmd._bufferLocks.cbegin(), _memCmd._bufferLocks.cend() );
        _memCmd._bufferLocks.resize(0);
    }
}

} //namespace Divide
