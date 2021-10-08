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

class Bone {
   protected:
       string _name;
       U64        _nameKey;
   public:
    I32 _boneID;
    mat4<F32> _offsetMatrix;
    mat4<F32> _localTransform;
    mat4<F32> _globalTransform;
    mat4<F32> _originalLocalTransform;

    Bone* _parent;
    vector<Bone*> _children;

    // index in the current animation's channel array.
    Bone(const string& name) noexcept
        : _name(name),
          _nameKey(_ID(name.c_str())),
          _boneID(-1),
          _parent(nullptr)
    {
    }

    Bone() : Bone("")
    {
    }

    ~Bone()
    {
        MemoryManager::DELETE_CONTAINER(_children);
    }

    [[nodiscard]] size_t hierarchyDepth() const {
        size_t size = _children.size();
        for (const Bone* child : _children) {
            size += child->hierarchyDepth();
        }

        return size;
    }

    [[nodiscard]] Bone* find(const string& name) {
        return find(_ID(name.c_str()));
    }

    [[nodiscard]] Bone* find(const U64 nameKey) {
        if (_nameKey == nameKey) {
            return this;
        }

        for (Bone* child : _children) {
            Bone* childNode = child->find(nameKey);
            if (childNode != nullptr) {
                return childNode;
            }
        }

        return nullptr;
    }

    void createBoneList(vector<Bone*>& boneList) {
        boneList.push_back(this);
        for (Bone* child : _children) {
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