#include "stdafx.h"

#include "Headers/PushConstants.h"

namespace Divide {
PushConstants::PushConstants(const GFX::PushConstant& constant)
    : _data{ constant }
{
}

PushConstants::PushConstants(const PushConstantsStruct& pushConstants)
    : _fastData(pushConstants)
{
    _fastData._set = true;
}

PushConstants::PushConstants(GFX::PushConstant&& constant)
    : _data{ MOV(constant) }
{
}

void PushConstants::set(const GFX::PushConstant& constant)
{
    for (GFX::PushConstant& iter : _data)
    {
        if (iter.bindingHash() == constant.bindingHash())
        {
            iter = constant;
            return;
        }
    }

    _data.emplace_back(constant);
}

void PushConstants::set(const PushConstantsStruct& fastData)
{
    _fastData = fastData;
    _fastData._set = true;
}

void PushConstants::clear() noexcept
{
    _data.clear();
    _fastData._set = false;
}

[[nodiscard]] bool PushConstants::empty() const noexcept
{
    return _data.empty() && !_fastData._set;
}

void PushConstants::countHint(const size_t count)
{
    _data.reserve(count);
}

const vector_fast<GFX::PushConstant>& PushConstants::data() const noexcept
{
    return _data;
}

const PushConstantsStruct& PushConstants::fastData() const noexcept
{
    return _fastData;
}

bool Merge(PushConstants& lhs, const PushConstants& rhs, bool& partial)
{
    if (lhs._fastData != rhs._fastData)
    {
        return false;
    }

    for (const GFX::PushConstant& ourConstant : lhs._data)
    {
        for (const GFX::PushConstant& otherConstant : rhs._data)
        {
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

    if (!lhs._fastData._set && rhs._fastData._set)
    {
        lhs._fastData = rhs._fastData;
    }

    return true;
}

}; //namespace Divide
