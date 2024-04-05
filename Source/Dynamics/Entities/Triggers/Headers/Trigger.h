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
#ifndef DVD_TRIGGER_H_
#define DVD_TRIGGER_H_

#include "Graphs/Headers/SceneNode.h"
#include "Platform/Threading/Headers/Task.h"

namespace Divide {

class Unit;
/// When a unit touches the circle described by
class Trigger final : public SceneNode {
   public:
    explicit Trigger(ResourceCache* parentCache, size_t descriptorHash, const std::string_view name);

    /// Trigger's the Task regardless of position
    [[nodiscard]] bool trigger() const;
    /// Enable or disable the trigger
    void setEnabled(const bool state) noexcept { _enabled = state; }
    /// Set the callback
    void setCallback(Task& triggeredTask) noexcept;

    /// SceneNode concrete implementations
    bool unload() override;

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "Trigger"; }

   private:
    /// The Task to be launched when triggered
    Task* _triggeredTask = nullptr;
    TaskPool* _taskPool = nullptr;
    bool _enabled = true;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(Trigger);

}  // namespace Divide

#endif //DVD_TRIGGER_H_
