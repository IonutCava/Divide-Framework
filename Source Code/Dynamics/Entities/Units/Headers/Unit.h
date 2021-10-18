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
#ifndef _UNIT_H_
#define _UNIT_H_

#include "Rendering/Headers/FrameListener.h"

namespace Divide {

class UnitComponent;
namespace Attorney {
    class UnitComponent;
}

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);

/// Currently supported unit types
enum class UnitType : U8 {
    /// "Living beings"
    UNIT_TYPE_CHARACTER,
    /// e.g. Cars, planes, ships etc
    UNIT_TYPE_VEHICLE,
    /// add more types above this
    COUNT
};
namespace Names {
    static const char* unitType[] = {
          "CHARACTER", "VEHICLE", "UNKOWN"
    };
}

namespace TypeUtil {
    const char* UnitTypeToString(UnitType unitType) noexcept;
    UnitType StringToUnitType(const string& name);
}

/// Unit interface
class Unit : public GUIDWrapper {
   public:
    using AttributeMap = hashMap<U32, I32>;
    friend class Attorney::UnitComponent;

   public:

    explicit Unit(UnitType type);
    virtual ~Unit() = default;

    /// moveTo makes the unit follow a path from it's current position to the
    /// targets position
    [[nodiscard]] virtual bool moveTo(const vec3<F32>& targetPosition);
    [[nodiscard]] virtual bool moveToX(F32 targetPosition);
    [[nodiscard]] virtual bool moveToY(F32 targetPosition);
    [[nodiscard]] virtual bool moveToZ(F32 targetPosition);
    /// teleportTo instantly places the unit at the desired position
    [[nodiscard]] virtual bool teleportTo(const vec3<F32>& targetPosition);

    /// Accesors
    /// Get the unit's position in the world
    [[nodiscard]] const vec3<F32>& getCurrentPosition() const {
        return _currentPosition;
    }
    /// Get the unit's target coordinates in the world
    [[nodiscard]] const vec3<F32>& getTargetPosition() const {
        return _currentTargetPosition;
    }
    /// Set the unit's movement speed in metres per second (minimum is 0 = the
    /// unit does not move / is rooted)
    virtual void setMovementSpeed(const F32 movementSpeed) {
        _moveSpeed = movementSpeed;
        CLAMP<F32>(_moveSpeed, 0.0f, 100.0f);
    }
    /// Set the unit's acceleration in metres per second squared
    virtual void setAcceleration(const F32 acceleration) {
        _acceleration = acceleration;
        CLAMP<F32>(_moveSpeed, 0.0f, 100.0f);
    }
    /// Get the unit's current movement speed
    [[nodiscard]] virtual F32 getMovementSpeed() const { return _moveSpeed; }
    /// Get the unit's acceleration rate
    [[nodiscard]] virtual F32 getAcceleration() const { return _acceleration; }
    /// Set destination tolerance
    void setMovementTolerance(const F32 movementTolerance) {
        _moveTolerance = std::max(0.1f, movementTolerance);
    }
    /// Get the units current movement tolerance
    [[nodiscard]] F32 getMovementTolerance() const { return _moveTolerance; }
    /// Get bound node
    [[nodiscard]] SceneGraphNode* getBoundNode() const { return _node; }

    virtual void setAttribute(U32 attributeID, I32 initialValue);
    [[nodiscard]] virtual I32 getAttribute(U32 attributeID) const;

    PROPERTY_RW(UnitType, type, UnitType::COUNT);

   protected:
    virtual void setParentNode(SceneGraphNode* node);

   protected:
    /// Movement speed (meters per second)
    F32 _moveSpeed;
    /// Acceleration (meters per second squared
    F32 _acceleration;
    /// acceptable distance from target
    F32 _moveTolerance;
    /// previous time, in milliseconds when last move was applied
    D64 _prevTime;
    /// Unit position in world
    vec3<F32> _currentPosition;
    /// Current destination point cached
    vec3<F32> _currentTargetPosition;
    SceneGraphNode* _node;
    AttributeMap _attributes;
    mutable SharedMutex _unitUpdateMutex;
};
namespace Attorney {
    class UnitComponent {
    private:
        static void setParentNode(Unit* unit, SceneGraphNode* node) {
            unit->setParentNode(node);
        }
        friend class Divide::UnitComponent;
    };
}
}  // namespace Divide

#endif