/**
 * @class Planner
 * @brief Implements an A* algorithm for searching the action space
 *
 * @date July 2015
 * @copyright (c) 2015 Prylis Inc. All rights reserved.
 */

#pragma once

#include "Action.h"
#include "Node.h"
#include "WorldState.h"

namespace Divide {
namespace goap {
    class Planner {
    private:
        /// A master lookup table of ID-to-Node; useful during the action replay
        hashMap<I32, Node> known_nodes_;

        vector<Node> open_;   // The A* open list
        vector<Node> closed_; // The A* closed list

        /**
         Is the given worldstate a member of the closed list? (And by that we mean,
         does any node on the closed list contain an equal worldstate.)
         @param ws the worldstate in question
         @return true if it's been closed, false if not
         */
        bool memberOfClosed(const WorldState& ws) const;

        /**
         Is the given worldstate a member of the open list? (And by that we mean,
         does any node on the open list contain an equal worldstate.)
         @param ws the worldstate in question
         @return a pointer to the note if found, end(open_) if not
         */
        vector<goap::Node>::iterator memberOfOpen(const WorldState& ws);

        /**
         Pops the first Node from the 'open' list, moves it to the 'closed' list, and
         returns a reference to this newly-closed Node. Its behavior is undefined if
         you call on an empty list.
         @return a reference to the newly closed Node
         */
        Node& popAndClose();

        /**
         Moves the given Node (an rvalue reference) into the 'open' list.
         @param node an rvalue reference to a Node that will be moved to the open list
         */
        void addToOpenList(Node&& node);

        /**
         Given two worldstates, calculates an estimated distance (the A* 'heuristic')
         between the two.
         @param now the present worldstate
         @param goal the desired worldstate
         @return an estimated distance between them
         */
        int calculateHeuristic(const WorldState& now, const WorldState& goal) const;

    public:
        Planner() noexcept = default;

        /**
         Useful when you're debugging a GOAP plan: simply dumps the open list to stdout.
        */
        void printOpenList(Divide::string& output) const;

        /**
         Useful when you're debugging a GOAP plan: simply dumps the closed list to stdout.
        */
        void printClosedList(Divide::string& output) const;

        /**
         Actually attempt to formulate a plan to get from start to goal, given a pool of
         available actions.
         @param start the starting worldstate
         @param goal the goal worldstate
         @param actions the available action pool
         @return a vector of Actions in REVERSE ORDER - use a reverse_iterator on this to get stepwise-order
         @exception std::runtime_error if no plan could be made with the available actions and states
         */
        vector<const Action*> plan(const WorldState& start, const WorldState& goal, const vector<const Action*>& actions);
    };

} //namespace goap
} //namespace Divide
