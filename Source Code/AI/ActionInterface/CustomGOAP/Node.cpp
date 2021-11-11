#include "stdafx.h"

#include "Node.h"

int goap::Node::last_id_ = 0;

goap::Node::Node() noexcept : g_(0), h_(0), parent_id_(-1), action_(nullptr)
{
    id_ = ++last_id_;
}

goap::Node::Node(const WorldState& state, int g, int h, int parent_id, const Action* action) :
    ws_(state), g_(g), h_(h), parent_id_(parent_id), action_(action)
{
    id_ = ++last_id_;
}

bool goap::operator<(const goap::Node& lhs, const goap::Node& rhs) noexcept {
    return lhs.f() < rhs.f();
}

//bool goap::Node::operator<(const Node& other) {
//    return f() < other.f();
//}
