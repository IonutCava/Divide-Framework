#include "stdafx.h"

#include "Headers/PushConstants.h"

namespace Divide {
PushConstants::PushConstants(const GFX::PushConstant& constant)
    : _data{ constant }
{
}

PushConstants::PushConstants(GFX::PushConstant&& constant)
    : _data{ MOV(constant) }
{
}

void PushConstants::set(const GFX::PushConstant& constant) {
    for (GFX::PushConstant& iter : _data) {
        if (iter.bindingHash() == constant.bindingHash()) {
            iter = constant;
            return;
        }
    }

    _data.emplace_back(constant);
}

void PushConstants::clear() noexcept {
    _data.clear();
}

[[nodiscard]] bool PushConstants::empty() const noexcept {
    return _data.empty();
}

void PushConstants::countHint(const size_t count) {
    _data.reserve(count);
}

[[nodiscard]] const vector_fast<GFX::PushConstant>& PushConstants::data() const noexcept {
    return _data;
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
