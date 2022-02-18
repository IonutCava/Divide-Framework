/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#include "stdafx.h"

#include "glimBatchData.h"
#include "glimInterface.h"

#include "Core/Headers/StringHelper.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"
#include "Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h"

#define BUFFER_OFFSET(i) ((char*)NULL + (i))

namespace NS_GLIM {
GlimArrayData::GlimArrayData() { Reset(); }

void GlimArrayData::Reset(void) {
    m_uiBufferOffset = 0;
    m_uiBufferStride = 0;
    m_DataType = GLIM_ENUM::GLIM_NODATA;
    m_ArrayData.clear();

    m_CurrentValue[0].Int = 0;
    m_CurrentValue[1].Int = 0;
    m_CurrentValue[2].Int = 0;
    m_CurrentValue[3].Int = 0;
    m_programAttribLocation.clear();
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

void glimBatchData::Upload() {
#ifdef AE_RENDERAPI_OPENGL
    UploadOGL();
#else
    UploadD3D11();
#endif
}

void glimBatchData::Bind(Divide::I64 uiCurrentProgram) {
#ifdef AE_RENDERAPI_OPENGL
    BindOGL(uiCurrentProgram);
#else
    BindD3D11(uiCurrentProgram);
#endif
}

void glimBatchData::Unbind(void) noexcept {
#ifdef AE_RENDERAPI_OPENGL
    UnbindOGL();
#else
    UnbindD3D11();
#endif
}

glimBatchData::glimBatchData() {
    m_bUploadedToGPU = false;
    m_bCreatedVBOs = false;

#ifdef AE_RENDERAPI_D3D11
    m_pVertexBuffer = nullptr;
    m_pIndexBuffer_Points = nullptr;
    m_pIndexBuffer_Lines = nullptr;
    m_pIndexBuffer_Triangles = nullptr;
    m_pIndexBuffer_Wireframe = nullptr;
#else
    m_VertAttribLocation = 0;
    m_ShaderProgramHandle = 0;
#endif

    Reset();
}

glimBatchData::~glimBatchData() {
    Reset();

#ifdef AE_RENDERAPI_OPENGL
    if (m_bCreatedVBOs) {
        m_bCreatedVBOs = false;
        Divide::GL_API::DeleteVAOs(1, &m_VertexArrayObjectID);
        Divide::GLUtil::freeBuffer(m_uiVertexBufferID);
        Divide::GLUtil::freeBuffer(m_uiElementBufferID_Points);
        Divide::GLUtil::freeBuffer(m_uiElementBufferID_Lines);
        Divide::GLUtil::freeBuffer(m_uiElementBufferID_Triangles);
        Divide::GLUtil::freeBuffer(m_uiElementBufferID_Wireframe);
    }

#endif
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

    m_bUploadedToGPU = false;

    m_fMinX = 99999999.0f;
    m_fMaxX = -99999999.0f;
    m_fMinY = 99999999.0f;
    m_fMaxY = -99999999.0f;
    m_fMinZ = 99999999.0f;
    m_fMaxZ = -99999999.0f;

    if (reserveBuffers) {
        m_PositionData.reserve(vertexCount);
        m_IndexBuffer_Points.reserve(std::max(vertexCount / 4, 1u));
        m_IndexBuffer_Lines.reserve(std::max(vertexCount / 2, 1u));
        m_IndexBuffer_Triangles.reserve(vertexCount * 2u);
        m_IndexBuffer_Wireframe.reserve(vertexCount * 2u);
    }
#ifdef AE_RENDERAPI_D3D11
    if (m_pVertexBuffer) {
        GLIM_Interface::s_ReleaseResourceCallback(m_pVertexBuffer);
        // m_pVertexBuffer->Release ();
        m_pVertexBuffer = nullptr;
    }
    if (m_pIndexBuffer_Points) {
        GLIM_Interface::s_ReleaseResourceCallback(m_pIndexBuffer_Points);
        // m_pIndexBuffer_Points->Release ();
        m_pIndexBuffer_Points = nullptr;
    }
    if (m_pIndexBuffer_Lines) {
        GLIM_Interface::s_ReleaseResourceCallback(m_pIndexBuffer_Lines);
        // m_pIndexBuffer_Lines->Release ();
        m_pIndexBuffer_Lines = nullptr;
    }
    if (m_pIndexBuffer_Triangles) {
        GLIM_Interface::s_ReleaseResourceCallback(m_pIndexBuffer_Triangles);
        // m_pIndexBuffer_Triangles->Release ();
        m_pIndexBuffer_Triangles = nullptr;
    }
    if (m_pIndexBuffer_Wireframe) {
        GLIM_Interface::s_ReleaseResourceCallback(m_pIndexBuffer_Wireframe);
        // m_pIndexBuffer_Wireframe->Release ();
        m_pIndexBuffer_Wireframe = nullptr;
    }

    m_Signature.clear();
#endif
}

unsigned int glimBatchData::AddVertex(float x, float y, float z) {
    if (x < m_fMinX) m_fMinX = x;
    if (x > m_fMaxX) m_fMaxX = x;

    if (y < m_fMinY) m_fMinY = y;
    if (y > m_fMaxY) m_fMaxY = y;

    if (z < m_fMinZ) m_fMinZ = z;
    if (z > m_fMaxZ) m_fMaxZ = z;

    m_PositionData.emplace_back(x);
    m_PositionData.emplace_back(y);
    m_PositionData.emplace_back(z);

    Divide::hashMap<unsigned int, GlimArrayData>::iterator it = begin(m_Attributes);
    const Divide::hashMap<unsigned int, GlimArrayData>::const_iterator itend = cend(m_Attributes);

    if (!m_Attributes.empty()) {
        it->second.m_ArrayData.reserve(m_Attributes.size());
    }

    for (; it != itend; ++it) {
        it->second.PushElement();
    }

    return (unsigned int)(m_PositionData.size() / 3) - 1;
}

#ifdef AE_RENDERAPI_D3D11
void glimBatchData::GenerateSignature(void) {
    m_Signature.clear();
    m_Signature.reserve(1 + m_Attributes.size());

    D3D11_INPUT_ELEMENT_DESC sig;
    sig.InputSlot = 0;
    sig.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    sig.InstanceDataStepRate = 0;
    sig.SemanticIndex = 0;
    sig.SemanticName = GLIM_Interface::s_VertexPosSemanticName;
    sig.AlignedByteOffset = 0;
    sig.Format = DXGI_FORMAT_R32G32B32_FLOAT;

    m_Signature.push_back(sig);

    hashMap<U64, GlimArrayData>::const_iterator it, itend;
    itend = m_Attributes.end();

    unsigned int uiOffset = sizeof(float) * 3;

    int iSlot = 1;
    for (it = m_Attributes.begin(); it != itend; ++it, ++iSlot) {
        D3D11_INPUT_ELEMENT_DESC sig;
        sig.InputSlot = iSlot;
        sig.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        sig.InstanceDataStepRate = 0;
        sig.SemanticIndex = 0;
        sig.SemanticName = it->first.c_str();
        sig.AlignedByteOffset = 0;  // uiOffset;

        switch (it->second.m_DataType) {
            // 4 byte data
            case GLIM_1I:
                uiOffset += sizeof(int) * 1;
                sig.Format = DXGI_FORMAT_R32_SINT;
                break;
            case GLIM_1F:
                uiOffset += sizeof(float) * 1;
                sig.Format = DXGI_FORMAT_R32_FLOAT;
                break;
            case GLIM_4UB:
                uiOffset += sizeof(int) * 1;
                sig.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                break;

            // 8 byte data
            case GLIM_2I:
                uiOffset += sizeof(int) * 2;
                sig.Format = DXGI_FORMAT_R32G32_SINT;
                break;
            case GLIM_2F:
                uiOffset += sizeof(float) * 2;
                sig.Format = DXGI_FORMAT_R32G32_FLOAT;
                break;

            // 12 byte data
            case GLIM_3I:
                uiOffset += sizeof(int) * 3;
                sig.Format = DXGI_FORMAT_R32G32B32_SINT;
                break;
            case GLIM_3F:
                uiOffset += sizeof(float) * 3;
                sig.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;

            // 16 byte data
            case GLIM_4I:
                uiOffset += sizeof(int) * 4;
                sig.Format = DXGI_FORMAT_R32G32B32A32_SINT;
                break;
            case GLIM_4F:
                uiOffset += sizeof(float) * 4;
                sig.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;

            default:
                // throws an exception
                GLIM_CHECK(
                    false,
                    "glimBatchData::GenerateSignature: Data-Type is invalid.");
                break;
        }

        m_Signature.push_back(sig);
    }
}
#endif

unsigned int glimBatchData::getVertexDataSize(void) const {
    unsigned int uiVertexDataSize = sizeof(float) * 3;

    const Divide::hashMap<unsigned int, GlimArrayData>::const_iterator itend = m_Attributes.end();

    for (Divide::hashMap<unsigned int, GlimArrayData>::const_iterator it = m_Attributes.begin(); it != itend; ++it) {
        switch (it->second.m_DataType) {
            // 4 byte data
            case GLIM_ENUM::GLIM_1I:
            case GLIM_ENUM::GLIM_1F:
            case GLIM_ENUM::GLIM_4UB:
                uiVertexDataSize += sizeof(int) * 1;
                break;

            // 8 byte data
            case GLIM_ENUM::GLIM_2I:
            case GLIM_ENUM::GLIM_2F:
                uiVertexDataSize += sizeof(int) * 2;
                break;

            // 12 byte data
            case GLIM_ENUM::GLIM_3I:
            case GLIM_ENUM::GLIM_3F:
                uiVertexDataSize += sizeof(int) * 3;
                break;

            // 16 byte data
            case GLIM_ENUM::GLIM_4I:
            case GLIM_ENUM::GLIM_4F:
                uiVertexDataSize += sizeof(int) * 4;
                break;

            default:
                // throws an exception
                GLIM_CHECK(false,
                           "glimBatchData::Upload: Data-Type is invalid.");
                break;
        }
    }

    return uiVertexDataSize;
}

#ifdef AE_RENDERAPI_OPENGL

void glimBatchData::UnbindOGL(void) noexcept {
    if (!m_bUploadedToGPU) 
        return;

    if (Divide::GL_API::GetStateTracker().setActiveVAO(0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
}

void glimBatchData::BindOGL(Divide::I64 uiCurrentProgram) {
    if (!m_bUploadedToGPU)
        return;

    if (Divide::GL_API::GetStateTracker().setActiveVAO(m_VertexArrayObjectID) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ARRAY_BUFFER, m_uiVertexBufferID) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }
    if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) == Divide::GLStateTracker::BindResult::FAILED) {
        Divide::DIVIDE_UNEXPECTED_CALL();
    }

    const Divide::hashMap<unsigned int, GlimArrayData>::iterator itend = m_Attributes.end();

    for (Divide::hashMap<unsigned int, GlimArrayData>::iterator it = m_Attributes.begin(); it != itend; ++it) {
        int iAttributeArray = 0;
        GlimArrayData::AttributeLocationMap& attribs = it->second.m_programAttribLocation;
        const GlimArrayData::AttributeLocationMap::const_iterator it2 = attribs.find(uiCurrentProgram);

        if (it2 != std::cend(attribs)) {
            iAttributeArray = it2->second;
        } else {
            iAttributeArray = it->first;
            insert(attribs, uiCurrentProgram, iAttributeArray);
        }

        if (iAttributeArray < 0) {
            continue;
        }

        assert(m_VertAttribLocation != (GLuint)iAttributeArray);
        assert(BUFFER_OFFSET(it->second.m_uiBufferOffset) != BUFFER_OFFSET(0));

        glEnableVertexAttribArray(iAttributeArray);

        switch (it->second.m_DataType) {
            case GLIM_ENUM::GLIM_1F:
                glVertexAttribPointer(
                    iAttributeArray, 1, GL_FLOAT, GL_FALSE, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_2F:
                glVertexAttribPointer(
                    iAttributeArray, 2, GL_FLOAT, GL_FALSE, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_3F:
                glVertexAttribPointer(
                    iAttributeArray, 3, GL_FLOAT, GL_FALSE, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_4F:
                glVertexAttribPointer(
                    iAttributeArray, 4, GL_FLOAT, GL_FALSE, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_1I:
                glVertexAttribIPointer(
                    iAttributeArray, 1, GL_INT, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_2I:
                glVertexAttribIPointer(
                    iAttributeArray, 2, GL_INT, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_3I:
                glVertexAttribIPointer(
                    iAttributeArray, 3, GL_INT, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_4I:
                glVertexAttribIPointer(
                    iAttributeArray, 4, GL_INT, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
            case GLIM_ENUM::GLIM_4UB:
                glVertexAttribIPointer(
                    iAttributeArray, 4, GL_UNSIGNED_BYTE, 0,
                    BUFFER_OFFSET(it->second.m_uiBufferOffset));
                break;
        }
    }

    // set the pointer for position last
    glEnableVertexAttribArray(m_VertAttribLocation);
    glVertexAttribPointer(m_VertAttribLocation, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
}

void glimBatchData::UploadOGL() {
    if (m_bUploadedToGPU) return;

    if (m_PositionData.empty()) return;

    m_bUploadedToGPU = true;

    const unsigned int uiVertexDataSize = (unsigned int)getVertexDataSize();
    const unsigned int uiVertices = (unsigned int)m_PositionData.size() / 3;

    if (!m_bCreatedVBOs) {
        m_bCreatedVBOs = true;
        glCreateVertexArrays(1, &m_VertexArrayObjectID);
        glCreateBuffers(1, &m_uiVertexBufferID);
        glCreateBuffers(1, &m_uiElementBufferID_Points);
        glCreateBuffers(1, &m_uiElementBufferID_Lines);
        glCreateBuffers(1, &m_uiElementBufferID_Triangles);
        glCreateBuffers(1, &m_uiElementBufferID_Wireframe);

        if (Divide::Config::ENABLE_GPU_VALIDATION) {
            glObjectLabel(GL_VERTEX_ARRAY,
                          m_VertexArrayObjectID,
                          -1,
                          Divide::Util::StringFormat("DVD_GLIM_VAO_%d", m_VertexArrayObjectID).c_str());
            glObjectLabel(GL_BUFFER,
                          m_uiVertexBufferID,
                          -1,
                          Divide::Util::StringFormat("DVD_GLIM_VB_%d", m_uiVertexBufferID).c_str());
            glObjectLabel(GL_BUFFER,
                          m_uiElementBufferID_Points,
                          -1,
                          Divide::Util::StringFormat("DVD_GLIM_VB_POINTS_%d", m_uiElementBufferID_Points).c_str());
            glObjectLabel(GL_BUFFER,
                          m_uiElementBufferID_Lines,
                          -1,
                          Divide::Util::StringFormat("DVD_GLIM_VB_LINES_%d", m_uiElementBufferID_Lines).c_str());
            glObjectLabel(GL_BUFFER,
                          m_uiElementBufferID_Triangles,
                          -1,
                          Divide::Util::StringFormat("DVD_GLIM_VB_TRIANGLES_%d", m_uiElementBufferID_Triangles).c_str());
            glObjectLabel(GL_BUFFER,
                          m_uiElementBufferID_Wireframe,
                          -1,
                          Divide::Util::StringFormat("DVD_GLIM_VB_WIREFRAME_%d", m_uiElementBufferID_Wireframe).c_str());
        }
    } else {
        // Buffer orphaning
        glInvalidateBufferData(m_uiVertexBufferID);
    }

    // space reservation pre-pass;
    size_t bufferSize = m_PositionData.size();
    Divide::hashMap<unsigned int, GlimArrayData>::iterator it = begin(m_Attributes);
    const Divide::hashMap<unsigned int, GlimArrayData>::iterator itend = end(m_Attributes);
    for (; it != itend; ++it) {
        bufferSize += it->second.m_ArrayData.size();
    }

    m_bufferData.resize(0);
    m_bufferData.reserve(bufferSize);
    m_bufferData.insert(std::end(m_bufferData),
                        std::cbegin(m_PositionData),
                        std::cend(m_PositionData));
    m_PositionData.resize(0);

    unsigned int uiOffset = uiVertices * sizeof(Glim4ByteData) * 3;

    // now upload each attribute array one after another
    for (it = std::begin(m_Attributes); it != itend; ++it) {
        GlimArrayData& data = it->second;
        m_bufferData.insert(std::end(m_bufferData),
                            std::cbegin(data.m_ArrayData),
                            std::cend(data.m_ArrayData));

        // already includes the number of vertices
        const unsigned int uiAttributeSize = (unsigned int)data.m_ArrayData.size() *
                                             sizeof(Glim4ByteData);  
        // free the temporary buffer in RAM
        data.m_ArrayData.resize(0);
        data.m_uiBufferStride = uiAttributeSize / uiVertices;
        // store the buffer offset for later use
        data.m_uiBufferOffset = uiOffset;
        // increase the buffer offset
        uiOffset += uiAttributeSize;
    }

    glNamedBufferData(m_uiVertexBufferID,
                      (size_t)uiVertices * uiVertexDataSize,
                      m_bufferData.data(),
                      GL_STREAM_DRAW);
    // the buffer in RAM can be cleared now
    m_bufferData.clear();

    m_uiWireframeElements = (unsigned int)m_IndexBuffer_Wireframe.size();
    m_uiPointElements = (unsigned int)m_IndexBuffer_Points.size();
    m_uiLineElements = (unsigned int)m_IndexBuffer_Lines.size();
    m_uiTriangleElements = (unsigned int)m_IndexBuffer_Triangles.size();

    // upload the index buffer for the points
    if (m_uiPointElements > 0) {
        glInvalidateBufferData(m_uiElementBufferID_Points);
        glNamedBufferData(m_uiElementBufferID_Points,
                          m_uiPointElements * sizeof(unsigned int),
                          m_IndexBuffer_Points.data(),
                          GL_STREAM_DRAW);
        m_IndexBuffer_Points.clear();
    }

    // upload the index buffer for the lines
    if (m_uiLineElements > 0) {
        glInvalidateBufferData(m_uiElementBufferID_Lines);
        glNamedBufferData(m_uiElementBufferID_Lines,
                          m_uiLineElements * sizeof(unsigned int),
                          m_IndexBuffer_Lines.data(),
                          GL_STREAM_DRAW);
        m_IndexBuffer_Lines.clear();
    }

    // upload the index buffer for the triangles
    if (m_uiTriangleElements > 0) {
        glInvalidateBufferData(m_uiElementBufferID_Triangles);
        glNamedBufferData(m_uiElementBufferID_Triangles,
                          m_uiTriangleElements * sizeof(unsigned int),
                          m_IndexBuffer_Triangles.data(),
                          GL_STREAM_DRAW);
        m_IndexBuffer_Triangles.clear();
    }

    // upload the index buffer for the wireframe
    if (m_uiWireframeElements > 0) {
        glInvalidateBufferData(m_uiElementBufferID_Wireframe);
        glNamedBufferData(m_uiElementBufferID_Wireframe,
                          m_uiWireframeElements * sizeof(unsigned int),
                          m_IndexBuffer_Wireframe.data(),
                          GL_STREAM_DRAW);
        m_IndexBuffer_Wireframe.clear();
    }
}
#endif
}
