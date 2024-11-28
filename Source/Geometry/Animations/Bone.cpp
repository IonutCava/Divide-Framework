#include "Headers/Bone.h"

namespace Divide
{

Bone::Bone(const std::string_view name, Bone* parent)
    : _name(name)
    , _nameHash(_ID(name))
    , _parent(parent)
{
    if ( _parent != nullptr)
    {
        _parent->_children.emplace_back(this);
    }
}

size_t Bone::hierarchyDepth() const
{
    size_t size = _children.size();

    for (const Bone_uptr& child : _children)
    {
        size += child->hierarchyDepth();
    }

    return size;
}

Bone* Bone::find(const U64 nameKey)
{
    if (_nameHash == nameKey)
    {
        return this;
    }

    for (const auto& child : _children)
    {
        Bone* childNode = child->find(nameKey);
        if (childNode != nullptr)
        {
            return childNode;
        }
    }

    return nullptr;
}

} //namespace Divide