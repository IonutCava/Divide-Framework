#include "stdafx.h"

#include "Headers/AttributeDescriptor.h"

namespace Divide {
    size_t GetHash(const AttributeDescriptor& descriptor)
    {
        if (descriptor._dataType == GFXDataFormat::COUNT) {
            return 0u;
        }

        size_t hash = 1337;
        Util::Hash_combine(hash, descriptor._strideInBytes,descriptor._bindingIndex,
                                 descriptor._componentsPerElement, descriptor._perVertexInputRate,
                                 descriptor._dataType, descriptor._normalized);

        return hash;
    }

    bool operator==(const AttributeDescriptor& lhs, const AttributeDescriptor& rhs) noexcept {
        return lhs._strideInBytes        == rhs._strideInBytes &&
               lhs._bindingIndex         == rhs._bindingIndex &&
               lhs._componentsPerElement == rhs._componentsPerElement &&
               lhs._perVertexInputRate   == rhs._perVertexInputRate &&
               lhs._dataType             == rhs._dataType &&
               lhs._normalized           == rhs._normalized;
    }

    bool operator!=(const AttributeDescriptor& lhs, const AttributeDescriptor& rhs) noexcept {
        return lhs._strideInBytes        != rhs._strideInBytes ||
               lhs._bindingIndex         != rhs._bindingIndex ||
               lhs._componentsPerElement != rhs._componentsPerElement ||
               lhs._perVertexInputRate   != rhs._perVertexInputRate ||
               lhs._dataType             != rhs._dataType ||
               lhs._normalized           != rhs._normalized;
    }
}; //namespace Divide