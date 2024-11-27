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
#ifndef DVD_BONE_H_
#define DVD_BONE_H_

namespace Divide
{

FWD_DECLARE_MANAGED_CLASS(Bone);

class Bone
{
public:
    static constexpr U8 INVALID_BONE_IDX = U8_MAX;

    PROPERTY_RW(string, name);
    PROPERTY_RW(U8, boneID, INVALID_BONE_IDX);


    PROPERTY_R_IW(U64, nameHash, 0u );
    POINTER_R_IW(Bone, parent, nullptr);

    mat4<F32> _offsetMatrix;
    mat4<F32> _localTransform;
    
    explicit Bone(std::string_view name, Bone* parent);

    [[nodiscard]] size_t hierarchyDepth() const;

    [[nodiscard]] Bone* find(const U64 nameKey);

    inline const vector<Bone_uptr>& children() const { return _children; }

private:
    vector<Bone_uptr> _children;
};


}  // namespace Divide

#endif //DVD_BONE_H_
