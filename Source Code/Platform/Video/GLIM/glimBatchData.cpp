/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#include "stdafx.h"

#include "glimBatchData.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

#define BUFFER_OFFSET(i) ((char*)NULL + (i))

namespace NS_GLIM {

GlimArrayData::GlimArrayData() { Reset(); }

void GlimArrayData::Reset(void) {
    m_DataType = GLIM_ENUM::GLIM_NODATA;
    m_ArrayData.clear();

    m_CurrentValue[0].Int = 0;
    m_CurrentValue[1].Int = 0;
    m_CurrentValue[2].Int = 0;
    m_CurrentValue[3].Int = 0;
}

void GlimArrayData::PushElement(void) {
    switch (m_DataType) {
        // 4 byte data
        case GLIM_ENUM::GLIM_1I:
        case GLIM_ENUM::GLIM_1F:
        case GLIM_ENUM::GLIM_4UB:
            m_ArrayData.push_back(m_CurrentValue[0]);
            return;

        // 8 byte data
        case GLIM_ENUM::GLIM_2I:
        case GLIM_ENUM::GLIM_2F:
            m_ArrayData.push_back(m_CurrentValue[0]);
            m_ArrayData.push_back(m_CurrentValue[1]);
            return;

        // 12 byte data
        case GLIM_ENUM::GLIM_3I:
        case GLIM_ENUM::GLIM_3F:
            m_ArrayData.push_back(m_CurrentValue[0]);
            m_ArrayData.push_back(m_CurrentValue[1]);
            m_ArrayData.push_back(m_CurrentValue[2]);
            return;

        // 16 byte data
        case GLIM_ENUM::GLIM_4I:
        case GLIM_ENUM::GLIM_4F:
            m_ArrayData.push_back(m_CurrentValue[0]);
            m_ArrayData.push_back(m_CurrentValue[1]);
            m_ArrayData.push_back(m_CurrentValue[2]);
            m_ArrayData.push_back(m_CurrentValue[3]);
            return;

        default:
            // throws an exception
            GLIM_CHECK(false,
                       "GlimArrayData::PushElement: Data-Type is invalid.");
            return;
    }
}

glimBatchData::glimBatchData()
{
    Reset();
}

glimBatchData::~glimBatchData()
{
    Reset();
}

void glimBatchData::Reset(bool reserveBuffers, unsigned int vertexCount, unsigned int attributeCount) {
    m_State = GLIM_BATCH_STATE::STATE_EMPTY;

    m_Attributes.clear();
    m_Attributes.reserve(attributeCount);
    m_PositionData.resize(0);
    
    m_IndexBuffer_Points.clear();
    m_IndexBuffer_Lines.clear();
    m_IndexBuffer_Triangles.clear();
    m_IndexBuffer_Wireframe.clear();

    if (reserveBuffers) {
        vertexCount *= 3;
        m_PositionData.reserve(vertexCount);
        m_IndexBuffer_Points.reserve(std::max(vertexCount / 4, 1u));
        m_IndexBuffer_Lines.reserve(std::max(vertexCount / 2, 1u));
        m_IndexBuffer_Triangles.reserve(vertexCount * 2u);
        m_IndexBuffer_Wireframe.reserve(vertexCount * 2u);
    }
}

unsigned int glimBatchData::AddVertex(float x, float y, float z) {
    m_PositionData.emplace_back(x);
    m_PositionData.emplace_back(y);
    m_PositionData.emplace_back(z);

    for (auto&[_, data] : m_Attributes) {
        data.PushElement();
    }

    return (unsigned int)(m_PositionData.size() / 3) - 1;
}

}
