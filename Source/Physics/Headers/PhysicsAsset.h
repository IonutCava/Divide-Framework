/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_PHYSICS_ASSET_H_
#define DVD_PHYSICS_ASSET_H_

#include "Core/Math/Headers/TransformInterface.h"

namespace Divide {
class PXDevice;
enum class PhysicsGroup : U8;
class RigidBodyComponent;

class PhysicsAsset : public ITransform {
public:
    explicit PhysicsAsset(RigidBodyComponent& parent) noexcept;

    virtual void physicsCollisionGroup(PhysicsGroup group);

    [[nodiscard]] RigidBodyComponent& getParent() const noexcept { return _parentComponent; }

protected:
    RigidBodyComponent & _parentComponent;
    PXDevice& _context;
};

FWD_DECLARE_MANAGED_CLASS(PhysicsAsset);

};

#endif //DVD_PHYSICS_ASSET_H_
