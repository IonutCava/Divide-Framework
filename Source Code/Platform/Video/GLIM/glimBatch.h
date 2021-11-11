/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#ifndef GLIM_GLIMBATCH_H
#define GLIM_GLIMBATCH_H

#include "glimInterface.h"
#include "glimBatchData.h"

namespace NS_GLIM
{
    //! An Implementation of the GLIM_Interface.
    class GLIM_BATCH final : public GLIM_Interface
    {
    public:
        GLIM_BATCH();
        virtual ~GLIM_BATCH() = default;

        // Begins defining one piece of geometry that can later be rendered with one set of states.
        void BeginBatch (bool reserveBuffers = true, unsigned int vertexCount = 64 * 3, unsigned int attributeCount = 1) override;
        //! Ends defining the batch. After this call "RenderBatch" can be called to actually render it.
        void EndBatch (void) noexcept override;

        //! Renders the batch that has been defined previously.
        void RenderBatch (bool bWireframe = false) override;
        //! Renders n instances of the batch that has been defined previously.
        void RenderBatchInstanced (int iInstances, bool bWireframe = false) override;

        //! Begins gathering information about the given type of primitives. 
        void Begin (GLIM_ENUM eType) noexcept override;
        //! Ends gathering information about the primitives.
        void End (void) override;
#ifdef AE_RENDERAPI_OPENGL
        //! Specifies the shader program's attribute location for vertex data
        virtual void SetVertexAttribLocation(unsigned int vertexLocation = 0) noexcept;
        virtual void SetShaderProgramHandle(Divide::I64 shaderProgramHandle) noexcept;
#endif
        //! Specifies a new vertex of a primitive.
        void Vertex (float x, float y, float z = 0.0f) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute1f (unsigned int attribLocation, float a1) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute2f (unsigned int attribLocation, float a1, float a2) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute3f (unsigned int attribLocation, float a1, float a2, float a3) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute4f (unsigned int attribLocation, float a1, float a2, float a3, float a4) override;

        //! Specifies a new value for the attribute with the given location.
        GLIM_ATTRIBUTE Attribute1i (unsigned int attribLocation, int a1) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute2i (unsigned int attribLocation, int a1, int a2) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute3i (unsigned int attribLocation, int a1, int a2, int a3) override;
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute4i (unsigned int attribLocation, int a1, int a2, int a3, int a4) override;

        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute4ub (unsigned int attribLocation, unsigned char a1, unsigned char a2, unsigned char a3, unsigned char a4 = 255) override;


        //! Specifies a new value for the attribute with the given name.
        void Attribute1f (GLIM_ATTRIBUTE Attr, float a1) noexcept override;
        //! Specifies a new value for the attribute with the given name.
        void Attribute2f (GLIM_ATTRIBUTE Attr, float a1, float a2) noexcept override;
        //! Specifies a new value for the attribute with the given name.
        void Attribute3f (GLIM_ATTRIBUTE Attr, float a1, float a2, float a3) noexcept override;
        //! Specifies a new value for the attribute with the given name.
        void Attribute4f (GLIM_ATTRIBUTE Attr, float a1, float a2, float a3, float a4) noexcept override;

        //! Specifies a new value for the attribute with the given name.
        void Attribute1i (GLIM_ATTRIBUTE Attr, int a1) noexcept override;
        //! Specifies a new value for the attribute with the given name.
        void Attribute2i (GLIM_ATTRIBUTE Attr, int a1, int a2) noexcept override;
        //! Specifies a new value for the attribute with the given name.
        void Attribute3i (GLIM_ATTRIBUTE Attr, int a1, int a2, int a3) noexcept override;
        //! Specifies a new value for the attribute with the given name.
        void Attribute4i (GLIM_ATTRIBUTE Attr, int a1, int a2, int a3, int a4) noexcept override;

        //! Specifies a new value for the attribute with the given name.
        void Attribute4ub (GLIM_ATTRIBUTE Attr, unsigned char a1, unsigned char a2, unsigned char a3, unsigned char a4 = 255) noexcept override;


        //! Returns the axis-aligned bounding box of the batches geometry, if there is any geometry. Else the results are undefined.
        void getBatchAABB (float& out_fMinX, float& out_fMaxX, float& out_fMinY, float& out_fMaxY, float& out_fMinZ, float& out_fMaxZ) noexcept override;

        //! Deletes all data associated with this object.
        void Clear (bool reserveBuffers, unsigned int vertexCount, unsigned int attributeCount);

        //! Returns true if the GLIM_BATCH contains no batch data.
        bool isCleared (void) const noexcept {return m_Data.m_State == GLIM_BATCH_STATE::STATE_EMPTY;}

#ifdef AE_RENDERAPI_D3D11
        const vector<D3D11_INPUT_ELEMENT_DESC>& GetSignature (void) const;
#endif

    private:
        GLIM_BATCH (const GLIM_BATCH& cc);
        const GLIM_BATCH& operator= (const GLIM_BATCH& cc);

        bool BeginRender (void);
        void EndRender (void) noexcept;

#ifdef AE_RENDERAPI_OPENGL
        bool BeginRenderOGL (void);
        void RenderBatchOGL (bool bWireframe);
        void RenderBatchInstancedOGL (int iInstances, bool bWireframe);
#endif

#ifdef AE_RENDERAPI_D3D11
        bool BeginRenderD3D11 (void);
        void RenderBatchD3D11 (bool bWireframe);
        void RenderBatchInstancedD3D11 (int iInstances, bool bWireframe);
#else
        void placeHolder();
#endif

        // The currently generated primitive type as specified via 'Begin'
        GLIM_ENUM m_PrimitiveType;

        // Counts how many vertices have been added since 'Begin'
        unsigned int m_uiPrimitiveVertex;
        // The index of the first vertex passed after 'Begin'
        unsigned int m_uiPrimitiveFirstIndex;
        // The Index of the Vertex passed previously.
        unsigned int m_uiPrimitiveLastIndex;

        glimBatchData m_Data;
    };
}

#pragma once


#endif


