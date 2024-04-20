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
#ifndef DVD_AESOP_ACTION_INTERFACE_H_
#define DVD_AESOP_ACTION_INTERFACE_H_



//ToDo: Implement our own version to avoid using STD
#include "AI/ActionInterface/CustomGOAP/Planner.h"
#include "AI/ActionInterface/CustomGOAP/Action.h"
#include "AI/ActionInterface/CustomGOAP/WorldState.h"

namespace Divide {
namespace AI {

using GOAPFact = I32;
using GOAPValue = bool;
using GOAPAction = goap::Action;
using GOAPWorldState = goap::WorldState;
using GOAPActionSet = vector<const GOAPAction*>;
using GOAPPlan = vector<const GOAPAction*>;

inline const char* GOAPValueName(const GOAPValue val) noexcept {
    return val ? "true" : "false";
}

class GOAPGoal : public goap::WorldState {
   public:
    GOAPGoal(const Divide::string& name, U32 ID);
    virtual ~GOAPGoal() = default;
    GOAPGoal(const GOAPGoal&) = default;

    [[nodiscard]] F32 relevancy() const noexcept { return _relevancy; }
    void relevancy(const F32 relevancy) noexcept { _relevancy = relevancy; }

    [[nodiscard]] const Divide::string& name() const noexcept { return name_; }
    [[nodiscard]] virtual bool plan(const GOAPWorldState& worldState, const GOAPActionSet& actionSet);

    [[nodiscard]] const GOAPPlan& getCurrentPlan() const;

    [[nodiscard]] Divide::string getOpenList() const {
        string ret;
        _planner.printOpenList(ret);
        return ret;
    }

    [[nodiscard]] Divide::string getClosedList() const {
        string ret;
        _planner.printClosedList(ret);
        return ret;
    }

    PROPERTY_R(U32, id);

   protected:
    F32 _relevancy;
    goap::Planner _planner;
    GOAPPlan _currentPlan;
};

using GOAPGoalList = vector<GOAPGoal>;

} // namespace AI
} // namespace Divide

#endif //DVD_AESOP_ACTION_INTERFACE_H_
