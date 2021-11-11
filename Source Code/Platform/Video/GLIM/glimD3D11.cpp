#include "stdafx.h"

#include "glimBatch.h"

#ifdef AE_RENDERAPI_D3D11

#include <D3D11.h>

namespace NS_GLIM
{
    using namespace std;

    #define BUFFER_OFFSET(i) ((char *)NULL + (i))

    const vector<D3D11_INPUT_ELEMENT_DESC>& GLIM_BATCH::GetSignature (void) const
    {
        return (m_Data.m_Signature);
    }


    bool GLIM_BATCH::BeginRenderD3D11 (void)
    {
        if (m_Data.m_State == STATE_EMPTY)
            return (false);

        GLIM_CHECK (m_Data.m_State == STATE_FINISHED_BATCH, "GLIM_BATCH::RenderBatch: This function can only be called after a batch has been created.");

        m_Data.Upload ();

        // if this happens the array is simply empty
        if (!m_Data.m_bUploadedToGPU)
            return (false);

        // allow the application to apply gl states now
        if (s_StateChangeCallback)
            s_StateChangeCallback ();

        if (s_SetInputLayoutCallback)
            s_SetInputLayoutCallback (this, GetSignature ());

        // bind all vertex arrays
        m_Data.Bind (0);

        return (true);
    }

    void GLIM_BATCH::RenderBatchD3D11 (bool bWireframe)
    {
        if (!BeginRender ())
            return;

        bWireframe |= GLIM_Interface::s_bForceWireframe;

        if (!bWireframe)
        {
            // render all triangles
            if (m_Data.m_uiTriangleElements > 0)
            {
                s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Triangles, DXGI_FORMAT_R32_UINT, 0);
                s_pContext->DrawIndexed (m_Data.m_uiTriangleElements, 0, 0);
            }
        }
        else
        {
            // render all triangles
            if (m_Data.m_uiWireframeElements > 0)
            {
                s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
                s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Wireframe, DXGI_FORMAT_R32_UINT, 0);
                s_pContext->DrawIndexed (m_Data.m_uiWireframeElements, 0, 0);
            }
        }
        
        // render all lines
        if (m_Data.m_uiLineElements > 0)
        {
            s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Lines, DXGI_FORMAT_R32_UINT, 0);
            s_pContext->DrawIndexed (m_Data.m_uiLineElements, 0, 0);
        }

        // render all points
        if (m_Data.m_uiPointElements > 0)
        {
            s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Points, DXGI_FORMAT_R32_UINT, 0);
            s_pContext->DrawIndexed (m_Data.m_uiPointElements, 0, 0);
        }

        EndRender ();
    }

    void GLIM_BATCH::RenderBatchInstancedD3D11 (int iInstances, bool bWireframe)
    {
        if (!BeginRender ())
            return;

        bWireframe |= GLIM_Interface::s_bForceWireframe;

        if (!bWireframe)
        {
            // render all triangles
            if (m_Data.m_uiTriangleElements > 0)
            {
                s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Triangles, DXGI_FORMAT_R32_UINT, 0);
                s_pContext->DrawIndexedInstanced (m_Data.m_uiTriangleElements, iInstances, 0, 0, 0);
            }
        }
        else
        {
            // render all triangles
            if (m_Data.m_uiWireframeElements > 0)
            {
                s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
                s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Wireframe, DXGI_FORMAT_R32_UINT, 0);
                s_pContext->DrawIndexedInstanced (m_Data.m_uiWireframeElements, iInstances, 0, 0, 0);
            }
        }
        
        // render all lines
        if (m_Data.m_uiLineElements > 0)
        {
            s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Lines, DXGI_FORMAT_R32_UINT, 0);
            s_pContext->DrawIndexedInstanced (m_Data.m_uiLineElements, iInstances, 0, 0, 0);
        }

        // render all points
        if (m_Data.m_uiPointElements > 0)
        {
            s_pContext->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            s_pContext->IASetIndexBuffer (m_Data.m_pIndexBuffer_Points, DXGI_FORMAT_R32_UINT, 0);
            s_pContext->DrawIndexedInstanced (m_Data.m_uiPointElements, iInstances, 0, 0, 0);
        }

        EndRender ();
    }

    void glimBatchData::UnbindD3D11 (void)
    {
    }

    void glimBatchData::BindD3D11 (unsigned int uiCurrentProgram)
    {
        if (!m_bUploadedToGPU)
            return;

        const unsigned int uiStreams = 1 + m_Attributes.size (); 

        ID3D11Buffer* pVertexBuffer[16];
        unsigned int uiStride[16];
        unsigned int uiOffset[16];

        pVertexBuffer[0] = m_pVertexBuffer;
        uiStride[0] = sizeof (float) * 3;
        uiOffset[0] = 0;

        hashMap<U64, GlimArrayData>::const_iterator it, itend;
        itend = m_Attributes.end ();
        int iSlot = 1;
        for (it = m_Attributes.begin (); it != itend; ++it, ++iSlot)
        {
            pVertexBuffer[iSlot] = m_pVertexBuffer;
            uiOffset[iSlot] = it->second.m_uiBufferOffset;
            uiStride[iSlot] = it->second.m_uiBufferStride;
        }

        GLIM_Interface::s_pContext->IASetVertexBuffers (0, uiStreams, pVertexBuffer, uiStride, uiOffset);
    }

    void glimBatchData::UploadD3D11 (void)
    {
        if (m_bUploadedToGPU)
            return;

        if (m_PositionData.empty ())
            return;

        m_bUploadedToGPU = true;


        const unsigned int uiVertexDataSize = (unsigned int) getVertexDataSize ();
        const unsigned int uiVertices = (unsigned int) (m_PositionData.size ()) / 3;

        m_uiVertexSize = uiVertexDataSize;

        vector<unsigned char> TempData (uiVertices * uiVertexDataSize);

        // first upload the position data
        unsigned int uiOffset = uiVertices * sizeof (float) * 3;

        // offset is zero, but can use uiOffset as size
        memcpy (&TempData[0], &m_PositionData[0], uiOffset);
        // the buffer in RAM can be cleared now
        m_PositionData.clear ();


        // now upload each attribute array one after another

        hashMap<U64, GlimArrayData>::iterator it, itend;
        itend = m_Attributes.end ();

        for (it = m_Attributes.begin (); it != itend; ++it)
        {
            const unsigned int uiAttributeSize = (unsigned int) (it->second.m_ArrayData.size ()) * 4; // already includes the number of vertices

            // upload the data
            memcpy (&TempData[uiOffset], &it->second.m_ArrayData[0].Float, uiAttributeSize);

            // free the temporary buffer in RAM, the data is now in a better place (the GPU)
            it->second.m_ArrayData.clear ();

            it->second.m_uiBufferStride = uiAttributeSize / uiVertices;

            // store the buffer offset for later use
            it->second.m_uiBufferOffset = uiOffset;

            // increase the buffer offset
            uiOffset += uiAttributeSize;
        }

        // upload vertex buffer
        {
            D3D11_BUFFER_DESC bd;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = TempData.size ();
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = 0;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;

            D3D11_SUBRESOURCE_DATA InitData;
            InitData.SysMemPitch = 0;
            InitData.SysMemSlicePitch = 0;
            InitData.pSysMem = (void*) &TempData[0];

            if (FAILED (GLIM_Interface::s_pDevice->CreateBuffer (&bd, &InitData, &m_pVertexBuffer))) {
                assert(false && "GLIM: Failed to upload the VertexBuffer.");
            }
        }

        m_uiPointElements = 0;
        m_uiLineElements = 0;
        m_uiTriangleElements = 0;
        m_uiWireframeElements = 0;

        // upload the index buffer for the points
        if (!m_IndexBuffer_Points.empty ())
        {
            m_uiPointElements = (unsigned int) m_IndexBuffer_Points.size ();

            D3D11_BUFFER_DESC bd;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = 4 * m_IndexBuffer_Points.size ();
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            bd.CPUAccessFlags = 0;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;

            D3D11_SUBRESOURCE_DATA InitData;
            InitData.SysMemPitch = 0;
            InitData.SysMemSlicePitch = 0;
            InitData.pSysMem = (void*) &m_IndexBuffer_Points[0];

            if (FAILED (GLIM_Interface::s_pDevice->CreateBuffer (&bd, &InitData, &m_pIndexBuffer_Points))) {
                assert(false && "GLIM: Failed to upload the Line-IndexBuffer.");
            }

            m_IndexBuffer_Points.clear ();
        }
            
        // upload the index buffer for the lines
        if (!m_IndexBuffer_Lines.empty ())
        {
            m_uiLineElements = (unsigned int) m_IndexBuffer_Lines.size ();

            D3D11_BUFFER_DESC bd;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = 4 * m_uiLineElements;
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            bd.CPUAccessFlags = 0;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;

            D3D11_SUBRESOURCE_DATA InitData;
            InitData.SysMemPitch = 0;
            InitData.SysMemSlicePitch = 0;
            InitData.pSysMem = (void*) &m_IndexBuffer_Lines[0];

            if (FAILED (GLIM_Interface::s_pDevice->CreateBuffer (&bd, &InitData, &m_pIndexBuffer_Lines))) {
                assert(false && "GLIM: Failed to upload the Line-IndexBuffer.");
            }
            m_IndexBuffer_Lines.clear ();
        }


        // upload the index buffer for the triangles
        if (!m_IndexBuffer_Triangles.empty ())
        {
            m_uiTriangleElements = (unsigned int) m_IndexBuffer_Triangles.size ();
            
            D3D11_BUFFER_DESC bd;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = 4 * m_uiTriangleElements;
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            bd.CPUAccessFlags = 0;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;

            D3D11_SUBRESOURCE_DATA InitData;
            InitData.SysMemPitch = 0;
            InitData.SysMemSlicePitch = 0;
            InitData.pSysMem = (void*) &m_IndexBuffer_Triangles[0];

            if (FAILED (GLIM_Interface::s_pDevice->CreateBuffer (&bd, &InitData, &m_pIndexBuffer_Triangles))) {
                assert(false && "GLIM: Failed to upload the Triangle-IndexBuffer.");
            }

            m_IndexBuffer_Triangles.clear ();
        }

        // upload the index buffer for the triangles
        if (!m_IndexBuffer_Wireframe.empty ())
        {
            m_uiWireframeElements = (unsigned int) m_IndexBuffer_Wireframe.size ();
            
            D3D11_BUFFER_DESC bd;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = 4 * m_uiWireframeElements;
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            bd.CPUAccessFlags = 0;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;

            D3D11_SUBRESOURCE_DATA InitData;
            InitData.SysMemPitch = 0;
            InitData.SysMemSlicePitch = 0;
            InitData.pSysMem = (void*) &m_IndexBuffer_Wireframe[0];

            char szTemp[256];
            sprintf (szTemp, "GLIM: Failed to upload the Wireframe-IndexBuffer: Elements = %d, Bytes = %d", m_uiWireframeElements, bd.ByteWidth);

            if (FAILED (GLIM_Interface::s_pDevice->CreateBuffer (&bd, &InitData, &m_pIndexBuffer_Wireframe))) {
                assert(false && szTemp);//"GLIM: Failed to upload the Wireframe-IndexBuffer");
            }
            m_IndexBuffer_Wireframe.clear ();
        }
    }

}
#else
    void placeHolder() noexcept {}
#endif

