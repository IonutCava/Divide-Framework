/**
 * @class Node
 * @brief A node is any point on the path between staring point and ending point (inclusive)
 *
 * @date July 2014
 * @copyright (c) 2014 Prylis Inc. All rights reserved.
 */

#pragma once

#include "Action.h"
#include "WorldState.h"

namespace Divide {

namespace goap {
    struct Node {
        static I32 last_id_; // a static that lets us assign incrementing, unique IDs to nodes

        WorldState ws_;      // The state of the world at this node.
        I32 id_;             // the unique ID of this node
        I32 parent_id_;      // the ID of this node's immediate predecessor
        I32 g_;              // The A* cost from 'start' to 'here'
        I32 h_;              // The estimated remaining cost to 'goal' form 'here'
        const Action* action_;     // The action that got us here (for replay purposes)

        Node() noexcept;
        Node(const WorldState& state, I32 g, I32 h, I32 parent_id, const Action* action);

        // F -- which is simply G+H -- is autocalculated
        inline I32 f() const noexcept { return g_ + h_; }

//        /**
//         Less-than operator, needed for keeping Nodes sorted.
//         @param other the other node to compare against
//         @return true if this node is less than the other (using F)
//         */
//        bool operator<(const Node& other);

        [[nodiscard]] string toString( ) const;

    };

    bool operator<(const Node& lhs, const Node& rhs) noexcept;
} //namespace goap
} //namespace Divide
