/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#include "stdafx.h"

#include "glimBatch.h"

#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

namespace NS_GLIM
{
    // One global GLIM_BATCH is always defined for immediate use.
    GLIM_BATCH glim;

    GLIM_BATCH::GLIM_BATCH ()
    {
        Clear (true, 64 * 3, 1);
    }

    void GLIM_BATCH::Clear (const bool reserveBuffers, const unsigned int vertexCount, const unsigned int attributeCount)
    {
        m_PrimitiveType = GLIM_ENUM::GLIM_NOPRIMITIVE;
        m_Data.Reset (reserveBuffers, vertexCount, attributeCount);
    }

    void GLIM_BATCH::getBatchAABB (float& out_fMinX, float& out_fMaxX, float& out_fMinY, float& out_fMaxY, float& out_fMinZ, float& out_fMaxZ) noexcept
    {
        out_fMinX = m_Data.m_fMinX;
        out_fMaxX = m_Data.m_fMaxX;
        out_fMinY = m_Data.m_fMinY;
        out_fMaxY = m_Data.m_fMaxY;
        out_fMinZ = m_Data.m_fMinZ;
        out_fMaxZ = m_Data.m_fMaxZ;
    }

    void GLIM_BATCH::EndRender (void) noexcept
    {
        // disable everything again
        m_Data.Unbind ();
    }

    bool GLIM_BATCH::BeginRender (void)
    {
#ifdef AE_RENDERAPI_OPENGL
        return BeginRenderOGL ();
#else
        return (BeginRenderD3D11 ());
#endif
    }

    void GLIM_BATCH::RenderBatch (bool bWireframe)
    {
#ifdef AE_RENDERAPI_OPENGL
        RenderBatchOGL (bWireframe);
#else
        RenderBatchD3D11 (bWireframe);
#endif
    }

    void GLIM_BATCH::RenderBatchInstanced (int iInstances, bool bWireframe)
    {
#ifdef AE_RENDERAPI_OPENGL
        RenderBatchInstancedOGL (iInstances, bWireframe);
#else
        RenderBatchInstancedD3D11 (iInstances, bWireframe);
#endif
    }


#ifdef AE_RENDERAPI_OPENGL
    bool GLIM_BATCH::BeginRenderOGL (void)
    {
        if (m_Data.m_State == GLIM_BATCH_STATE::STATE_EMPTY)
            return false;

        GLIM_CHECK (m_Data.m_State == GLIM_BATCH_STATE::STATE_FINISHED_BATCH, "GLIM_BATCH::RenderBatch: This function can only be called after a batch has been created.");
        
        // allow the application to apply gl states now
        if (s_StateChangeCallback)
            s_StateChangeCallback ();

        // get the currently active shader program
        GLIM_CHECK (m_Data.m_ShaderProgramHandle > 0, "GLIM_BATCH::RenderBatch: Currently no shader is bound or the shader has an error.");

        m_Data.Upload ();

        // if this happens the array is simply empty
        if (!m_Data.m_bUploadedToGPU)
            return false;

        // bind all vertex arrays
        m_Data.Bind ( m_Data.m_ShaderProgramHandle);

        return true;
    }

    void GLIM_BATCH::RenderBatchInstancedOGL(int iInstances, bool bWireframe)
    {
        if (!BeginRender ())
            return;
    
        bWireframe |= s_bForceWireframe;

        Divide::GenericDrawCommand cmd;
        cmd._cmd.primCount = iInstances;

        if (!bWireframe)
        {
            // render all triangles
            if (m_Data.m_uiTriangleElements > 0)
            {
                Divide::GL_API::GetStateTracker()._activeTopology = Divide::PrimitiveTopology::TRIANGLES;
                cmd._cmd.indexCount = m_Data.m_uiTriangleElements;
                if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Data.m_uiElementBufferID_Triangles) == Divide::GLStateTracker::BindResult::FAILED) {
                    Divide::DIVIDE_UNEXPECTED_CALL();
                }
                Divide::GLUtil::SubmitRenderCommand(cmd, false, m_Data.m_uiElementBufferID_Triangles > 0 ? GL_UNSIGNED_INT : GL_NONE);
            }
        }
        else
        {
            // render all triangles
            if (m_Data.m_uiWireframeElements > 0)
            {
                Divide::GL_API::GetStateTracker()._activeTopology = Divide::PrimitiveTopology::LINES;
                cmd._cmd.indexCount = m_Data.m_uiWireframeElements;
                if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Data.m_uiElementBufferID_Wireframe) == Divide::GLStateTracker::BindResult::FAILED) {
                    Divide::DIVIDE_UNEXPECTED_CALL();
                }
                Divide::GLUtil::SubmitRenderCommand(cmd, false, m_Data.m_uiElementBufferID_Wireframe > 0 ? GL_UNSIGNED_INT : GL_NONE);
            }
        }
        
        // render all lines
        if (m_Data.m_uiLineElements > 0)
        {
            Divide::GL_API::GetStateTracker()._activeTopology = Divide::PrimitiveTopology::LINES;
            cmd._cmd.indexCount = m_Data.m_uiLineElements;
            if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Data.m_uiElementBufferID_Lines) == Divide::GLStateTracker::BindResult::FAILED) {
                Divide::DIVIDE_UNEXPECTED_CALL();
            }
            Divide::GLUtil::SubmitRenderCommand(cmd, false, m_Data.m_uiElementBufferID_Lines > 0 ? GL_UNSIGNED_INT : GL_NONE);
        }

        // render all points
        if (m_Data.m_uiPointElements > 0)
        {
            Divide::GL_API::GetStateTracker()._activeTopology = Divide::PrimitiveTopology::API_POINTS;
            cmd._cmd.indexCount = m_Data.m_uiPointElements;
            if (Divide::GL_API::GetStateTracker().setActiveBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Data.m_uiElementBufferID_Points) == Divide::GLStateTracker::BindResult::FAILED) {
                Divide::DIVIDE_UNEXPECTED_CALL();
            }
            Divide::GLUtil::SubmitRenderCommand(cmd, false, m_Data.m_uiElementBufferID_Points > 0 ? GL_UNSIGNED_INT : GL_NONE);
        }

        EndRender ();
    }

    void GLIM_BATCH::RenderBatchOGL (const bool bWireframe)
    {
        RenderBatchInstancedOGL(1, bWireframe);
    }
#endif

    void GLIM_BATCH::BeginBatch (bool reserveBuffers, unsigned int vertexCount, unsigned int attributeCount)
    {
        GLIM_CHECK (m_Data.m_State == GLIM_BATCH_STATE::STATE_EMPTY || m_Data.m_State == GLIM_BATCH_STATE::STATE_FINISHED_BATCH, "GLIM_BATCH::BeginBatch: This function cannot be called again before EndBatch has been called.");

        // clear all previous data
        Clear (reserveBuffers, vertexCount, attributeCount);

        // start an entirely new batch
        m_Data.m_State = GLIM_BATCH_STATE::STATE_BEGINNING_BATCH;
    }

    void GLIM_BATCH::EndBatch (void) noexcept
    {
        // if the state is STATE_BEGINNING_BATCH, than no Begin/End call has been made => created an empty batch, which is ok
        GLIM_CHECK (m_Data.m_State == GLIM_BATCH_STATE::STATE_END_PRIMITIVE || m_Data.m_State == GLIM_BATCH_STATE::STATE_BEGINNING_BATCH, "GLIM_BATCH::EndBatch: This function must be called after a call to \"End\".");

        // mark this batch as finished
        m_Data.m_State = GLIM_BATCH_STATE::STATE_FINISHED_BATCH;

#ifdef AE_RENDERAPI_D3D11
        m_Data.GenerateSignature ();
#endif
    }

    void GLIM_BATCH::Begin (GLIM_ENUM eType) noexcept
    {
        // if the state is STATE_BEGINNING_BATCH, than no Begin/End call has been made yet
        // if it is STATE_END_PRIMITIVE then a previous Begin/End call has been made
        GLIM_CHECK (m_Data.m_State == GLIM_BATCH_STATE::STATE_END_PRIMITIVE || m_Data.m_State == GLIM_BATCH_STATE::STATE_BEGINNING_BATCH, "GLIM_BATCH::Begin: This function must be called after a call to \"BeginBatch\" or after a \"Begin\"/\"End\"-pair.");

        m_Data.m_State = GLIM_BATCH_STATE::STATE_BEGIN_PRIMITIVE;
        m_PrimitiveType = eType;
        m_uiPrimitiveVertex = 0;
        m_uiPrimitiveFirstIndex = (unsigned int) m_Data.m_PositionData.size () / 3; // three floats per vertex

        switch (m_PrimitiveType)
        {
        case GLIM_ENUM::GLIM_TRIANGLES:
        case GLIM_ENUM::GLIM_POINTS:
        case GLIM_ENUM::GLIM_LINES:
        case GLIM_ENUM::GLIM_LINE_STRIP:
        case GLIM_ENUM::GLIM_LINE_LOOP:
        case GLIM_ENUM::GLIM_POLYGON:
        case GLIM_ENUM::GLIM_TRIANGLE_FAN:
        case GLIM_ENUM::GLIM_QUADS:
            // Life is good.
            break;

        case GLIM_ENUM::GLIM_QUAD_STRIP:
        case GLIM_ENUM::GLIM_TRIANGLE_STRIP:
        
            //! \todo Stuff...

            GLIM_CHECK (false, "GLIM_BATCH::Begin: The given primitive-type is currently not supported.");
            break;

        default:
            GLIM_CHECK (false, "GLIM_BATCH::Begin: The given primitive-type is unknown.");
            return;
        }
    }

    void GLIM_BATCH::End (void)
    {
        GLIM_CHECK (m_Data.m_State == GLIM_BATCH_STATE::STATE_BEGIN_PRIMITIVE, "GLIM_BATCH::End: This function can only be called after a call to \"Begin\".");

        m_Data.m_State = GLIM_BATCH_STATE::STATE_END_PRIMITIVE;

        switch (m_PrimitiveType)
        {
        case GLIM_ENUM::GLIM_TRIANGLES:
            {
                GLIM_CHECK (m_uiPrimitiveVertex % 3 == 0, "GLIM_BATCH::End: You did not finish constructing the last triangle.");
            }
            break;

        case GLIM_ENUM::GLIM_TRIANGLE_STRIP:
            {
                //! \todo Stuff...
            }
            break;

        case GLIM_ENUM::GLIM_TRIANGLE_FAN:
            {
                GLIM_CHECK (m_uiPrimitiveVertex >= 4, "GLIM_BATCH::End: You did not finish constructing the triangle fan. At least 4 vertices are required.");

                // add the very first vertex index
                m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveFirstIndex);
                // add the previous vertex index
                m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveLastIndex);

                // add the second vertex index
                m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveFirstIndex + 1);

                m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveFirstIndex);
                m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);

                m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveFirstIndex + 1);
            }
            break;

        case GLIM_ENUM::GLIM_QUADS:
            {
                GLIM_CHECK (m_uiPrimitiveVertex % 4 == 0, "GLIM_BATCH::End: You did not finish constructing the last Quad.");
            }
            break;

        case GLIM_ENUM::GLIM_QUAD_STRIP:
            {
                //! \todo Stuff...
            }
            break;

        case GLIM_ENUM::GLIM_POINTS:
            {
                // nothing to do
            }
            break;

        case GLIM_ENUM::GLIM_LINES:
            {
                GLIM_CHECK (m_uiPrimitiveVertex % 2 == 0, "GLIM_BATCH::End: You did not finish constructing the last Line.");
            }
            break;

        case GLIM_ENUM::GLIM_LINE_STRIP:
            {
                GLIM_CHECK (m_uiPrimitiveVertex > 1, "GLIM_BATCH::End: You did not finish constructing the Line-strip.");
            }
            break;

        case GLIM_ENUM::GLIM_LINE_LOOP:
            {
                GLIM_CHECK (m_uiPrimitiveVertex > 1, "GLIM_BATCH::End: You did not finish constructing the Line-Loop.");

                m_Data.m_IndexBuffer_Lines.push_back (m_uiPrimitiveLastIndex);
                m_Data.m_IndexBuffer_Lines.push_back (m_uiPrimitiveFirstIndex);
            }
            break;

        case GLIM_ENUM::GLIM_POLYGON:
            {
                GLIM_CHECK (m_uiPrimitiveVertex == 0 || m_uiPrimitiveVertex >= 3, "GLIM_BATCH::End: You did not finish constructing the last Polygon.");

                m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveFirstIndex);
            }
            break;

        default:
            GLIM_CHECK (false, "GLIM_BATCH::End: The given primitive-type is unknown.");
            return;
        }
    }

    void GLIM_BATCH::SetVertexAttribLocation(unsigned int vertexLocation) noexcept
    {
        m_Data.m_VertAttribLocation = vertexLocation;
    }
    
    void GLIM_BATCH::SetShaderProgramHandle(Divide::I64 shaderProgramHandle) noexcept
    {
        m_Data.m_ShaderProgramHandle = shaderProgramHandle;
    }

    void GLIM_BATCH::Vertex (float x, float y, float z)
    {
        GLIM_CHECK (m_Data.m_State == GLIM_BATCH_STATE::STATE_BEGIN_PRIMITIVE, "GLIM_BATCH::Vertex: This function can only be called after a call to \"Begin\".");

        switch (m_PrimitiveType)
        {
        case GLIM_ENUM::GLIM_TRIANGLES:
            {
                ++m_uiPrimitiveVertex;
                const unsigned int uiIndex = m_Data.AddVertex (x, y, z);
                m_Data.m_IndexBuffer_Triangles.push_back (uiIndex);

                if (m_uiPrimitiveVertex > 1)
                {
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (uiIndex);
                }

                if (m_uiPrimitiveVertex == 3)
                {
                    m_uiPrimitiveVertex = 0;

                    m_Data.m_IndexBuffer_Wireframe.push_back (uiIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveFirstIndex);
                }

                // if this is the first vertex of any quad, store that index
                if (m_uiPrimitiveVertex == 1)
                    m_uiPrimitiveFirstIndex = uiIndex;

                m_uiPrimitiveLastIndex = uiIndex;
            }
            break;

        case GLIM_ENUM::GLIM_TRIANGLE_STRIP:
            {
                //! \todo Stuff...
            }
            break;

        case GLIM_ENUM::GLIM_TRIANGLE_FAN:
            {
                ++m_uiPrimitiveVertex;

                // first 3 vertices are simply added; at the fourth we use two cached vertices to construct a new triangle
                if (m_uiPrimitiveVertex >= 4)
                {
                    // add the very first vertex index
                    m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveFirstIndex);
                    // add the previous vertex index
                    m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveLastIndex);

                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveFirstIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                }

                const unsigned int uiIndex = m_Data.AddVertex (x, y, z);
                m_Data.m_IndexBuffer_Triangles.push_back (uiIndex);

                if (m_uiPrimitiveVertex > 1)
                {
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (uiIndex);
                }

                m_uiPrimitiveLastIndex = uiIndex;
                
            }
            break;

        case GLIM_ENUM::GLIM_QUADS:
            {
                ++m_uiPrimitiveVertex;

                // first 3 vertices are simply added, at the fourth we use two cached vertices to construct a new triangle
                if (m_uiPrimitiveVertex == 4)
                {
                    // add the very first vertex index
                    m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveFirstIndex);
                    // add the previous vertex index
                    m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveLastIndex);
                }

                const unsigned int uiIndex = m_Data.AddVertex (x, y, z);
                m_Data.m_IndexBuffer_Triangles.push_back (uiIndex);

                if (m_uiPrimitiveVertex > 1)
                {
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (uiIndex);
                }

                if (m_uiPrimitiveVertex == 4)
                {
                    // reset the vertex counter
                    m_uiPrimitiveVertex = 0;

                    m_Data.m_IndexBuffer_Wireframe.push_back (uiIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveFirstIndex);
                }

                // if this is the first vertex of any quad, store that index
                if (m_uiPrimitiveVertex == 1)
                    m_uiPrimitiveFirstIndex = uiIndex;

                m_uiPrimitiveLastIndex = uiIndex;
            }
            break;

        case GLIM_ENUM::GLIM_QUAD_STRIP:
            {
                //! \todo Stuff...
            }
            break;

        case GLIM_ENUM::GLIM_POINTS:
            {
                const unsigned int uiIndex = m_Data.AddVertex (x, y, z);
                m_Data.m_IndexBuffer_Points.push_back (uiIndex);
            }
            break;

        case GLIM_ENUM::GLIM_LINES:
            {
                ++m_uiPrimitiveVertex;
                m_Data.m_IndexBuffer_Lines.push_back (m_Data.AddVertex(x, y, z));
            }
            break;

        case GLIM_ENUM::GLIM_LINE_STRIP:
        case GLIM_ENUM::GLIM_LINE_LOOP:
            {
                ++m_uiPrimitiveVertex;

                // very first vertex, just store it, but don't create a line yet
                if (m_uiPrimitiveVertex == 1)
                {
                    m_uiPrimitiveLastIndex = m_Data.AddVertex (x, y, z);
                    break;
                }

                // push the previous vertex into the index-buffer
                m_Data.m_IndexBuffer_Lines.push_back (m_uiPrimitiveLastIndex);

                // store the current vertex in the vertex-array
                m_uiPrimitiveLastIndex = m_Data.AddVertex (x, y, z);
                // push the current vertex into the index buffer
                m_Data.m_IndexBuffer_Lines.push_back (m_uiPrimitiveLastIndex);
            }
            break;

        case GLIM_ENUM::GLIM_POLYGON:
            {
                ++m_uiPrimitiveVertex;

                // first 3 vertices are simply added, at the fourth we use two cached vertices to construct a new triangle
                if (m_uiPrimitiveVertex >= 4)
                {
                    // add the very first vertex index
                    m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveFirstIndex);
                    // add the previous vertex index
                    m_Data.m_IndexBuffer_Triangles.push_back (m_uiPrimitiveLastIndex);
                }

                const unsigned int uiIndex = m_Data.AddVertex (x, y, z);
                m_Data.m_IndexBuffer_Triangles.push_back (uiIndex);

                if (m_uiPrimitiveVertex > 1)
                {
                    m_Data.m_IndexBuffer_Wireframe.push_back (m_uiPrimitiveLastIndex);
                    m_Data.m_IndexBuffer_Wireframe.push_back (uiIndex);
                }

                m_uiPrimitiveLastIndex = uiIndex;
            }
            break;

        default:
            GLIM_CHECK (false, "GLIM_BATCH::Vertex: The given primitive-type is unknown.");
            return;
        }
    }
}
