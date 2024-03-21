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

#pragma once
#ifndef DVD_PATCH_H_
#define DVD_PATCH_H_

namespace Divide {

struct FileData {
    Str<32> ItemName = "";
    Str<32> ModelName = "";
    vec3<F32> Orientation;
    vec3<F32> Position;
    vec3<F32> Scale;
};

struct PatchData {
    vector<string> name;
    vector<string> modelName;
    string sceneName = "";
    U32 size = 0u;
};

namespace Patch {
    bool compareData(const PatchData& data);
    void addGeometry(const FileData& data);
    const vector<FileData>& modelData() noexcept;
    void clearModelData() noexcept;
};

};  // namespace Divide

#endif //DVD_PATCH_H_
