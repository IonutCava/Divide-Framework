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
#ifndef DVD_EDITOR_IMGUI_EXTENSIONS_H_
#define DVD_EDITOR_IMGUI_EXTENSIONS_H_

struct ImVec2;

namespace ImGui {
#ifndef ImTextureID
    typedef void* ImTextureID;          // Default: store a pointer or an integer fitting in a pointer (most renderer backends are ok with that)
#endif

    bool ToggleButton(const char* str_id, bool* v);

    //From: https://github.com/Flix01/imgui
    // zoomCenter is panning in [(0,0),(1,1)]
    // returns true if some user interaction have been processed
    bool ImageZoomAndPan( ImTextureID user_texture_id, const ImVec2& size,float aspectRatio,float& zoom, ImVec2& zoomCenter,int panMouseButtonDrag,int resetZoomAndPanMouseButton,const ImVec2& zoomMaxAndZoomStep);
} //namespace ImGui

#endif //DVD_EDITOR_IMGUI_EXTENSIONS_H_
