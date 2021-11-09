#include "stdafx.h"

#include "Headers/PushConstants.h"

namespace Divide {

void PushConstants::set(const GFX::PushConstant& constant) {
    for (GFX::PushConstant& iter : _data) {
        if (iter.bindingHash() == constant.bindingHash()) {
            iter = constant;
            return;
        }
    }

    _data.emplace_back(constant);
}

bool Merge(PushConstants& lhs, const PushConstants& rhs, bool& partial) {
    for (const GFX::PushConstant& ourConstant : lhs._data) {
        for (const GFX::PushConstant& otherConstant : rhs._data) {
            // If we have the same binding, but different data, merging isn't possible
            if (ourConstant.bindingHash() == otherConstant.bindingHash() &&
                ourConstant != otherConstant)
            {
                return false;
            }
        }
    }

    // Merge stage
    partial = true;
    insert_unique(lhs._data, rhs._data);

    return true;
}

}; //namespace Divide
