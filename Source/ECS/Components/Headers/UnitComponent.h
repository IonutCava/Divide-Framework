/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _UNIT_COMPONENT_H_
#define _UNIT_COMPONENT_H_

#include "SGNComponent.h"

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(Unit);
BEGIN_COMPONENT(Unit, ComponentType::UNIT)
public:
    UnitComponent(SceneGraphNode* parentSGN, PlatformContext& context);

    // This call will take ownership of the specified pointer!
    bool setUnit(const Unit_ptr& unit);

    template <typename T = Unit> requires std::is_base_of_v<Unit, T>
    [[nodiscard]] std::shared_ptr<T> getUnit() const noexcept
    {
        return std::static_pointer_cast<T>(_unit);
    }

private:
    Unit_ptr _unit;
END_COMPONENT(Unit);

} //namespace Divide

#endif //_UNIT_COMPONENT_H_