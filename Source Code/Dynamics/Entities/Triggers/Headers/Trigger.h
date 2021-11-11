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
#ifndef _TRIGGER_H_
#define _TRIGGER_H_

#include "Graphs/Headers/SceneNode.h"
#include "Platform/Threading/Headers/Task.h"

namespace Divide {

class Unit;
/// When a unit touches the circle described by
class Trigger final : public SceneNode {
   public:
    explicit Trigger(ResourceCache* parentCache, size_t descriptorHash, const Str256& name);

    void sceneUpdate(U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) override;

    /// Checks if the unit has activated this trigger and launches the Task
    /// If we receive a nullptr unit as a param, we use the camera position
    [[nodiscard]] bool check(Unit* unit, const vec3<F32>& camEyePos = VECTOR3_ZERO) const;
    /// Trigger's the Task regardless of position
    [[nodiscard]] bool trigger() const;
    /// Draw a sphere at the trigger's position
    /// The impostor has the radius of the trigger's radius
    void setDrawImpostor(const bool state) noexcept { _drawImpostor = state; }
    /// Enable or disable the trigger
    void setEnabled(const bool state) noexcept { _enabled = state; }
    /// Set the callback, the position and the radius of the trigger
    void setParams(Task& triggeredTask, const vec3<F32>& triggerPosition, F32 radius) noexcept;
    /// Just update the callback
    void setParams(Task& triggeredTask) noexcept {
        setParams(triggeredTask, _triggerPosition, _radius);
    }

    /// SceneNode concrete implementations
    bool unload() override;

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Trigger"; }

   private:
    /// The Task to be launched when triggered
    Task* _triggeredTask = nullptr;
    TaskPool* _taskPool = nullptr;
    /// The trigger circle's center position
    vec3<F32> _triggerPosition;
    /// The trigger's radius
    F32 _radius = 1.f;
    /// Draw the impostor?
    bool _drawImpostor = false;
    bool _enabled = true;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Trigger);

}  // namespace Divide

#endif