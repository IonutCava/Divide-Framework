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

/*Code references:
    http://nolimitsdesigns.com/game-design/open-asset-import-library-animation-loader/
*/

#pragma once
#ifndef BONE_H_
#define BONE_H_

namespace Divide {

class Bone : public eastl::enable_shared_from_this<Bone> {
   protected:
       string _name;
       U64    _nameKey = 0u;
   public:
    I32 _boneID = -1;
    mat4<F32> _offsetMatrix;
    mat4<F32> _localTransform;
    mat4<F32> _globalTransform;
    mat4<F32> _originalLocalTransform;

    eastl::shared_ptr<Bone> _parent = nullptr;
    vector<eastl::shared_ptr<Bone>> _children;

    // index in the current animation's channel array.
    Bone(const string& name)
        : _name(name),
          _nameKey(_ID(name.c_str()))
    {
    }

    Bone() noexcept = default;
    ~Bone() = default;

    [[nodiscard]] size_t hierarchyDepth() const {
        size_t size = _children.size();
        for (const auto& child : _children) {
            size += child->hierarchyDepth();
        }

        return size;
    }

    [[nodiscard]] eastl::shared_ptr<Bone> find(const string& name) {
        return find(_ID(name.c_str()));
    }

    [[nodiscard]] eastl::shared_ptr<Bone> find(const U64 nameKey) {
        if (_nameKey == nameKey) {
            return shared_from_this();
        }

        for (const auto& child : _children) {
            eastl::shared_ptr<Bone> childNode = child->find(nameKey);
            if (childNode != nullptr) {
                return childNode;
            }
        }

        return nullptr;
    }

    void createBoneList(vector<eastl::shared_ptr<Bone>>& boneList) {
        boneList.push_back(shared_from_this());
        for (const auto& child : _children) {
            child->createBoneList(boneList);
        }
    }

    [[nodiscard]] const string& name() const noexcept {
        return _name;
    }

    void name(const string& name) {
        _name = name;
        _nameKey = _ID(name.c_str());
    }
};

};  // namespace Divide

#endif