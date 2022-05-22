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
    };
}


#pragma once

#endif



