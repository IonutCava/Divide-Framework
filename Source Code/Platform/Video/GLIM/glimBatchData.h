/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#ifndef GLIM_GLIMBATCHDATA_H
#define GLIM_GLIMBATCHDATA_H

#include "Declarations.h"
#include "Core/TemplateLibraries/Headers/Vector.h"
#include "Core/TemplateLibraries/Headers/HashMap.h"
#include "Core/TemplateLibraries/Headers/String.h"

namespace NS_GLIM
{
    // holds one element of attribute data
    union Glim4ByteData
    {
        Glim4ByteData ()  noexcept : Int (0) {}
        Glim4ByteData(int Int_) noexcept : Int(Int_) {}
        Glim4ByteData(float Float_) noexcept : Float(Float_) {}

        int Int;
        float Float;
        unsigned char Bytes[4];
    };

    // one attribute array
    struct GlimArrayData
    {
        GlimArrayData ();

        void Reset (void);
        void PushElement (void);

        // what kind of data this array holds (1 float, 2 floats, 4 unsigned byte, etc.)
        GLIM_ENUM m_DataType;
        // the current value that shall be used for all new elements
        Glim4ByteData m_CurrentValue[4];
        // the actual array of accumulated elements
        Divide::vector<Glim4ByteData> m_ArrayData;

        // the offset into the GL buffer, needed for binding it
        unsigned int m_uiBufferOffset;
        unsigned int m_uiBufferStride;
        // previous attribute location (second) used with the saved shader program(first)
        using AttributeLocationMap = Divide::hashMap<Divide::I64, int>;
        AttributeLocationMap m_programAttribLocation;
    };

    // used for tracking erroneous use of the interface
    enum class GLIM_BATCH_STATE : unsigned int
    {
        STATE_EMPTY,
        STATE_BEGINNING_BATCH,
        STATE_FINISHED_BATCH,
        STATE_BEGIN_PRIMITIVE,
        STATE_END_PRIMITIVE,

    };

    // the data of one entire batch
    struct glimBatchData
    {
        glimBatchData ();
        ~glimBatchData ();

        // Deletes all allocated data and resets default states
        void Reset(bool reserveBuffers = false, unsigned int vertexCount = 64 * 3, unsigned int attributeCount = 1);

        // Uploads the data onto the GPU
        void Upload ();
        // Binds the vertex arrays for rendering.
        void Bind (Divide::I64 uiCurrentProgram);
        // Unbinds all data after rendering.
        void Unbind (void) noexcept;

#ifdef AE_RENDERAPI_OPENGL
        // Uploads the data onto the GPU
        void UploadOGL ();
        // Binds the vertex arrays for rendering.
        void BindOGL (Divide::I64 uiCurrentProgram);
        // Unbinds all data after rendering.
        void UnbindOGL (void) noexcept;
#endif

#ifdef AE_RENDERAPI_D3D11
        // Uploads the data onto the GPU
        void UploadD3D11 (void);
        // Binds the vertex arrays for rendering.
        void BindD3D11 (Divide::I64 uiCurrentProgram);
        // Unbinds all data after rendering.
        void UnbindD3D11 (void);
#endif

        // Returns the size in bytes of ONE vertex (all attributes together)
        unsigned int getVertexDataSize (void) const;

        // Returns the index of the just added vertex.
        unsigned int AddVertex (float x, float y, float z);

        // Used for error detection.
        GLIM_BATCH_STATE m_State;

        // All attributes accessible by name.
        Divide::hashMap<unsigned int, GlimArrayData> m_Attributes;

        // Position data is stored separately, not as an attribute.
        Divide::vector<Glim4ByteData> m_PositionData;

        // Index Buffer for points.
        Divide::vector<unsigned int> m_IndexBuffer_Points;
        // Index Buffer for Lines.
        Divide::vector<unsigned int> m_IndexBuffer_Lines;
        // Index Buffer for Triangles.
        Divide::vector<unsigned int> m_IndexBuffer_Triangles;
        // Index Buffer for wireframe rendering of polygons.
        Divide::vector<unsigned int> m_IndexBuffer_Wireframe;

        // Number of Points to render. Used after m_IndexBuffer_Points has been cleared. 
        unsigned int m_uiPointElements;
        // Number of Lines to render. Used after m_IndexBuffer_Lines has been cleared. 
        unsigned int m_uiLineElements;
        // Number of Triangles to render. Used after m_IndexBuffer_Triangles has been cleared. 
        unsigned int m_uiTriangleElements;
        // Number of Lines to render. Used after m_IndexBuffer_Wireframe has been cleared. 
        unsigned int m_uiWireframeElements;

        // Whether the data has already been uploaded to the GPU.
        bool m_bUploadedToGPU;
        // Whether VBOs where ever created.
        bool m_bCreatedVBOs;
        Divide::vector<Glim4ByteData> m_bufferData;
#ifdef AE_RENDERAPI_OPENGL
        unsigned int m_VertexArrayObjectID;
        // GL attrib location of the vertex data in the shader program
        unsigned int m_VertAttribLocation;
        // Current bound shader program's handle
        Divide::I64 m_ShaderProgramHandle;
        // GL ID of the vertex array.
        unsigned int m_uiVertexBufferID;
        // GL ID of the index buffer for points.
        unsigned int m_uiElementBufferID_Points;
        // GL ID of the index buffer for lines.
        unsigned int m_uiElementBufferID_Lines;
        // GL ID of the index buffer for triangles.
        unsigned int m_uiElementBufferID_Triangles;
        // GL ID of the index buffer for wireframe rendering.
        unsigned int m_uiElementBufferID_Wireframe;
#endif

#ifdef AE_RENDERAPI_D3D11
        void GenerateSignature (void);

        unsigned int m_uiVertexSize;
        ID3D11Buffer* m_pVertexBuffer;
        ID3D11Buffer* m_pIndexBuffer_Points;
        ID3D11Buffer* m_pIndexBuffer_Lines;
        ID3D11Buffer* m_pIndexBuffer_Triangles;
        ID3D11Buffer* m_pIndexBuffer_Wireframe;

        vector<D3D11_INPUT_ELEMENT_DESC> m_Signature;
#endif

        // AABB 
        float m_fMinX;
        float m_fMaxX;
        float m_fMinY;
        float m_fMaxY;
        float m_fMinZ;
        float m_fMaxZ;

    };
}


#pragma once

#endif



