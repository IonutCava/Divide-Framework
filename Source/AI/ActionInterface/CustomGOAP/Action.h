/**
 * @class Action
 * @brief Operates on the world state.
 *
 * @date  July 2014
 * @copyright (c) 2014 Prylis Inc.. All rights reserved.
 */

#pragma once

namespace Divide {
namespace goap {
    struct WorldState;

    class Action {
    public: 
        using operations = hashMap<I32, bool>;
        using operationsIterator = hashMap<I32, bool>::const_iterator;

        virtual ~Action() = default;
        Action(const Action&) = default;
        Action& operator=(const Action&) = default;

    private:
        string name_; // The human-readable action name
        I32 cost_;        // The numeric cost of this action

        // Preconditions are things that must be satisfied before this
        // action can be taken. Only preconditions that "matter" are here.
        operations preconditions_;

        // Effects are things that happen when this action takes place.
        operations effects_;

    public:
        Action() noexcept;
        Action(const string& name, I32 cost);

        /**
         Is this action eligible to operate on the given worldstate?
         @param ws the worldstate in question
         @return true if this worldstate meets the preconditions
         */
        bool eligibleFor(const goap::WorldState& ws) const;

        /**
         Act on the given worldstate. Will not check for "eligiblity" and will happily
         act on whatever worldstate you provide it.
         @param ws the worldstate to act on
         @return a copy worldstate, with effects applied
         */
        WorldState actOn(const WorldState& ws) const;

        /**
         Set the given precondition variable and value.
         @param key the name of the precondition
         @param value the value the precondition must hold
         */
        inline void setPrecondition(const I32 key, const bool value) { preconditions_[key] = value; }

        /**
         Set the given effect of this action, in terms of variable and new value.
         @param key the name of the effect
         @param value the value that will result
         */
        inline void setEffect(const I32 key, const bool value) { effects_[key] = value; }

        inline const operations& effects() const noexcept { return effects_; }

        int cost() const noexcept { return cost_; }

        const string& name() const noexcept { return name_; }

        virtual bool checkImplDependentCondition() const { return true; }
    };
} //namespace goap
} //namespace Divide
