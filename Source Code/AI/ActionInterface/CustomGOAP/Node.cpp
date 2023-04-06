#include "stdafx.h"

#include "Node.h"

#include "Core/Headers/StringHelper.h"

namespace Divide::goap
{

    I32 Node::last_id_ = 0;

Node::Node() noexcept : g_(0), h_(0), parent_id_(-1), action_(nullptr)
{
    id_ = ++last_id_;
}

Node::Node(const WorldState& state, I32 g, I32 h, I32 parent_id, const Action* action)
    : ws_(state)
    , g_(g)
    , h_(h)
    , parent_id_(parent_id)
    , action_(action)
{
    id_ = ++last_id_;
}

bool operator<(const Node& lhs, const Node& rhs) noexcept {
    return lhs.f() < rhs.f();
}

//bool Node::operator<(const Node& other) {
//    return f() < other.f();
//}

string Node::toString() const
{
    return Util::StringFormat( "Node { id: %d parent: %d F: %d G: %d H: %d, %s\n", id_, parent_id_, f(), g_, h_, ws_.toString().c_str());
}

} //namespace Divide::goap
