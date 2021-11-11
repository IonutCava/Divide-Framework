#include "stdafx.h"

#include "Action.h"
#include "WorldState.h"

goap::Action::Action()  noexcept : cost_(0) {
}

goap::Action::Action(const Divide::string& name, int cost) : Action() {
    // Because delegating constructors cannot initialize & delegate at the same time...
    name_ = name;
    cost_ = cost;
}

bool goap::Action::eligibleFor(const WorldState& ws) const {
    if (!checkImplDependentCondition()) {
        return false;
    }

    for (const auto& precond : preconditions_) {
        try {
            if (ws.vars_.at(precond.first) != precond.second) {
                return false;
            }
        }
        catch (const std::out_of_range&) {
            return false;
        }
    }
    return true;
}

goap::WorldState goap::Action::actOn(const WorldState& ws) const {
    goap::WorldState tmp(ws);
    for (const auto& effect : effects_) {
        tmp.setVariable(effect.first, effect.second);
    }
    return tmp;
}

