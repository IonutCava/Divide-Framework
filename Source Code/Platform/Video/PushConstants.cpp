#include "stdafx.h"

#include "Headers/PushConstants.h"

namespace Divide {

void PushConstants::set(const GFX::PushConstant& constant) {
    for (GFX::PushConstant& iter : _data) {
        if (iter._bindingHash == constant._bindingHash) {
            iter = constant;
            return;
        }
    }

    _data.emplace_back(constant);
}

bool Merge(PushConstants& lhs, const PushConstants& rhs, bool& partial) {
    vector<GFX::PushConstant>& ourConstants = lhs._data;
    const vector<GFX::PushConstant>& otherConstants = rhs._data;

    // Check stage
    for (const GFX::PushConstant& ourConstant : ourConstants) {
        for (const GFX::PushConstant& otherConstant : otherConstants) {
            // If we have the same binding, but different data, merging isn't possible
            if (ourConstant._bindingHash == otherConstant._bindingHash &&
                ourConstant._buffer != otherConstant._buffer)
            {
                return false;
            }
        }
    }

    // Merge stage
    partial = true;
    insert_unique(ourConstants, otherConstants);

    return true;
}

}; //namespace Divide
