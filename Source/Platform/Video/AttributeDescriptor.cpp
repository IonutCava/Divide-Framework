

#include "Headers/AttributeDescriptor.h"

namespace Divide
{
    [[nodiscard]] size_t GetHash( const AttributeDescriptor& descriptor )
    {
        if ( descriptor._dataType == GFXDataFormat::COUNT )
        {
            return 0u;
        }

        size_t hash = 1337;
        Util::Hash_combine( hash, descriptor._strideInBytes, descriptor._vertexBindingIndex,
                            descriptor._componentsPerElement,
                            descriptor._dataType, descriptor._normalized );

        return hash;
    }

    [[nodiscard]] size_t GetHash( const VertexBinding& vertexBinding )
    {
        size_t hash = 1337;
        Util::Hash_combine( hash, vertexBinding._strideInBytes, vertexBinding._bufferBindIndex, vertexBinding._perVertexInputRate);
        return hash;
    }

    [[nodiscard]] size_t GetHash( const AttributeMap& attributes )
    {
        size_t vertexFormatHash = 1337;
        for ( const AttributeDescriptor& attrDescriptor : attributes._attributes )
        {
            if ( attrDescriptor._dataType != GFXDataFormat::COUNT )
            {
                Util::Hash_combine( vertexFormatHash, GetHash( attrDescriptor ) );
            }
        }
        for ( const VertexBinding& vertBinding : attributes._vertexBindings )
        {
            Util::Hash_combine( vertexFormatHash, GetHash( vertBinding ) );
        }

        return vertexFormatHash;
    }

}; //namespace Divide