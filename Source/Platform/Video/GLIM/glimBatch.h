/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#ifndef GLIM_GLIMBATCH_H
#define GLIM_GLIMBATCH_H

#include "glimBatchData.h"

namespace NS_GLIM
{
    //! An Implementation of the GLIM_Interface.
    class GLIM_BATCH
    {
    public:
        GLIM_BATCH();
        ~GLIM_BATCH() = default;

        // Begins defining one piece of geometry that can later be rendered with one set of states.
        void BeginBatch (bool reserveBuffers = true, unsigned int vertexCount = 64 * 3, unsigned int attributeCount = 1);
        //! Ends defining the batch. After this call "RenderBatch" can be called to actually render it.
        glimBatchData& EndBatch (void) noexcept;

        //! Renders n instances of the batch that has been defined previously.
        bool PrepareRender();

        //! Begins gathering information about the given type of primitives. 
        void Begin (GLIM_ENUM eType) noexcept;
        //! Ends gathering information about the primitives.
        void End (void);

        //! Specifies a new vertex of a primitive.
        void Vertex (float x, float y, float z = 0.0f);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute1f (unsigned int attribLocation, float a1);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute2f (unsigned int attribLocation, float a1, float a2);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute3f (unsigned int attribLocation, float a1, float a2, float a3);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute4f (unsigned int attribLocation, float a1, float a2, float a3, float a4);

        //! Specifies a new value for the attribute with the given location.
        GLIM_ATTRIBUTE Attribute1i (unsigned int attribLocation, int a1);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute2i (unsigned int attribLocation, int a1, int a2);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute3i (unsigned int attribLocation, int a1, int a2, int a3);
        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute4i (unsigned int attribLocation, int a1, int a2, int a3, int a4);

        //! Specifies a new value for the attribute with the given name.
        GLIM_ATTRIBUTE Attribute4ub (unsigned int attribLocation, unsigned char a1, unsigned char a2, unsigned char a3, unsigned char a4 = 255);


        //! Specifies a new value for the attribute with the given name.
        void Attribute1f (GLIM_ATTRIBUTE Attr, float a1) noexcept;
        //! Specifies a new value for the attribute with the given name.
        void Attribute2f (GLIM_ATTRIBUTE Attr, float a1, float a2) noexcept;
        //! Specifies a new value for the attribute with the given name.
        void Attribute3f (GLIM_ATTRIBUTE Attr, float a1, float a2, float a3) noexcept;
        //! Specifies a new value for the attribute with the given name.
        void Attribute4f (GLIM_ATTRIBUTE Attr, float a1, float a2, float a3, float a4) noexcept;

        //! Specifies a new value for the attribute with the given name.
        void Attribute1i (GLIM_ATTRIBUTE Attr, int a1) noexcept;
        //! Specifies a new value for the attribute with the given name.
        void Attribute2i (GLIM_ATTRIBUTE Attr, int a1, int a2) noexcept;
        //! Specifies a new value for the attribute with the given name.
        void Attribute3i (GLIM_ATTRIBUTE Attr, int a1, int a2, int a3) noexcept;
        //! Specifies a new value for the attribute with the given name.
        void Attribute4i (GLIM_ATTRIBUTE Attr, int a1, int a2, int a3, int a4) noexcept;

        //! Specifies a new value for the attribute with the given name.
        void Attribute4ub (GLIM_ATTRIBUTE Attr, unsigned char a1, unsigned char a2, unsigned char a3, unsigned char a4 = 255) noexcept;

        //! Deletes all data associated with this object.
        void Clear (bool reserveBuffers, unsigned int vertexCount, unsigned int attributeCount);

        //! Returns true if the GLIM_BATCH contains no batch data.
        bool isCleared (void) const noexcept {return m_Data.m_State == GLIM_BATCH_STATE::STATE_EMPTY;}

    private:
        GLIM_BATCH (const GLIM_BATCH& cc);
        const GLIM_BATCH& operator= (const GLIM_BATCH& cc);

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


