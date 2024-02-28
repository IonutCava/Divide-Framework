

#include "Headers/ImGuiExtensions.h"
#include "Editor/Headers/Utils.h"

#include <imgui_internal.h>

namespace ImGui {
    bool ToggleButton(const char* str_id, bool* v)
    {
        bool ret = false;
        const ImVec4* colours = ImGui::GetStyle().Colors;
        const ImVec2 p = GetCursorScreenPos();
        ImDrawList* draw_list = GetWindowDrawList();

        const float height = GetFrameHeight();
        const float width = height * 1.55f;
        const float radius = height * 0.50f;

        if (InvisibleButton(str_id, ImVec2(width, height))) {
            ret = true;
            *v = !*v;
        }

        if (ImGui::IsItemHovered()) {
            draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? Divide::Util::ColoursHovered[1] : Divide::Util::ColoursHovered[0]), height * 0.5f);
            draw_list->AddCircleFilled(ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f), p.y + radius), radius - 1.5f, ImGui::GetColorU32(colours[ImGuiCol_ButtonHovered]));
        } else {
            draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? Divide::Util::Colours[1] : Divide::Util::Colours[0]), height * 0.50f);
            draw_list->AddCircleFilled(ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f), p.y + radius), radius - 1.5f, ImGui::GetColorU32(colours[ImGuiCol_ButtonActive]));
        }

        return ret;
    }

    // ref:https://github.com/Flix01/imgui

    // This software is provided 'as-is', without any express or implied
    // warranty.  In no event will the authors be held liable for any damages
    // arising from the use of this software.
    // Permission is granted to anyone to use this software for any purpose,
    // including commercial applications, and to alter it and redistribute it
    // freely, subject to the following restrictions:
    // 1. The origin of this software must not be misrepresented; you must not
    //    claim that you wrote the original software. If you use this software
    //    in a product, an acknowledgment in the product documentation would be
    //    appreciated but is not required.
    // 2. Altered source versions must be plainly marked as such, and must not be
    //    misrepresented as being the original software.
    // 3. This notice may not be removed or altered from any source distribution.

    /*
    inline ImVec2 mouseToPdfRelativeCoords(const ImVec2 &mp) const {
    return ImVec2((mp.x+cursorPosAtStart.x-startPos.x)*(uv1.x-uv0.x)/zoomedImageSize.x+uv0.x,
    (mp.y+cursorPosAtStart.y-startPos.y)*(uv1.y-uv0.y)/zoomedImageSize.y+uv0.y);
    }
    inline ImVec2 pdfRelativeToMouseCoords(const ImVec2 &mp) const {
    return ImVec2((mp.x-uv0.x)*(zoomedImageSize.x)/(uv1.x-uv0.x)+startPos.x-cursorPosAtStart.x,(mp.y-uv0.y)*(zoomedImageSize.y)/(uv1.y-uv0.y)+startPos.y-cursorPosAtStart.y);
    }
    */
    bool ImageZoomAndPan(ImTextureID user_texture_id, const ImVec2& size, float aspectRatio, float& zoom, ImVec2& zoomCenter, int panMouseButtonDrag, int resetZoomAndPanMouseButton, const ImVec2& zoomMaxAndZoomStep)
    {
        bool rv = false;
        ImGuiWindow* window = GetCurrentWindow();
        if (!window || window->SkipItems) return rv;
        ImVec2 curPos = ImGui::GetCursorPos();
        const ImVec2 wndSz(size.x > 0 ? size.x : ImGui::GetWindowSize().x - curPos.x, size.y > 0 ? size.y : ImGui::GetWindowSize().y - curPos.y);

        IM_ASSERT(wndSz.x != 0 && wndSz.y != 0 && zoom != 0);

        // Here we use the whole size (although it can be partially empty)
        ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + wndSz.x, window->DC.CursorPos.y + wndSz.y));
        ItemSize(bb);
        if (!ItemAdd(bb, 0)) return rv;

        ImVec2 imageSz = wndSz;
        ImVec2 remainingWndSize(0, 0);
        if (aspectRatio != 0) {
            const float wndAspectRatio = wndSz.x / wndSz.y;
            if (aspectRatio >= wndAspectRatio) { imageSz.y = imageSz.x / aspectRatio; remainingWndSize.y = wndSz.y - imageSz.y; }
            else { imageSz.x = imageSz.y * aspectRatio; remainingWndSize.x = wndSz.x - imageSz.x; }
        }

        if (ImGui::IsItemHovered()) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.MouseWheel != 0) {
                if (io.KeyCtrl) {
                    const float zoomStep = zoomMaxAndZoomStep.y;
                    const float zoomMin = 1.f;
                    const float zoomMax = zoomMaxAndZoomStep.x;
                    if (io.MouseWheel < 0) { zoom /= zoomStep; if (zoom < zoomMin) zoom = zoomMin; }
                    else { zoom *= zoomStep; if (zoom > zoomMax) zoom = zoomMax; }
                    rv = true;
                    /*if (io.FontAllowUserScaling) {
                    // invert effect:
                    // Zoom / Scale window
                    ImGuiContext& g = *GImGui;
                    ImGuiWindow* window = g.HoveredWindow;
                    float new_font_scale = ImClamp(window->FontWindowScale - g.IO.MouseWheel * 0.10f, 0.50f, 2.50f);
                    float scale = new_font_scale / window->FontWindowScale;
                    window->FontWindowScale = new_font_scale;

                    const ImVec2 offset = window->Size * (1.0f - scale) * (g.IO.MousePos - window->Pos) / window->Size;
                    window->Pos += offset;
                    window->PosFloat += offset;
                    window->Size *= scale;
                    window->SizeFull *= scale;
                    }*/
                }
                else {
                    const bool scrollDown = io.MouseWheel <= 0;
                    const float zoomFactor = .5f / zoom;
                    if ((!scrollDown && zoomCenter.y > zoomFactor) || (scrollDown && zoomCenter.y < 1.f - zoomFactor)) {
                        const float slideFactor = zoomMaxAndZoomStep.y * 0.1f * zoomFactor;
                        if (scrollDown) {
                            zoomCenter.y += slideFactor;///(imageSz.y*zoom);
                            if (zoomCenter.y > 1.f - zoomFactor) zoomCenter.y = 1.f - zoomFactor;
                        }
                        else {
                            zoomCenter.y -= slideFactor;///(imageSz.y*zoom);
                            if (zoomCenter.y < zoomFactor) zoomCenter.y = zoomFactor;
                        }
                        rv = true;
                    }
                }
            }
            if (io.MouseClicked[resetZoomAndPanMouseButton]) { zoom = 1.f; zoomCenter.x = zoomCenter.y = .5f; rv = true; }
            if (ImGui::IsMouseDragging(panMouseButtonDrag, 1.f)) {
                zoomCenter.x -= io.MouseDelta.x / (imageSz.x * zoom);
                zoomCenter.y -= io.MouseDelta.y / (imageSz.y * zoom);
                rv = true;
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
        }

        const float zoomFactor = .5f / zoom;
        if (rv) {
            if (zoomCenter.x < zoomFactor) zoomCenter.x = zoomFactor;
            else if (zoomCenter.x > 1.f - zoomFactor) zoomCenter.x = 1.f - zoomFactor;
            if (zoomCenter.y < zoomFactor) zoomCenter.y = zoomFactor;
            else if (zoomCenter.y > 1.f - zoomFactor) zoomCenter.y = 1.f - zoomFactor;
        }

        ImVec2 uvExtension(2.f * zoomFactor, 2.f * zoomFactor);
        if (remainingWndSize.x > 0) {
            const float remainingSizeInUVSpace = 2.f * zoomFactor * (remainingWndSize.x / imageSz.x);
            const float deltaUV = uvExtension.x;
            const float remainingUV = 1.f - deltaUV;
            if (deltaUV < 1) {
                float adder = (remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace);
                uvExtension.x += adder;
                remainingWndSize.x -= adder * zoom * imageSz.x;
                imageSz.x += adder * zoom * imageSz.x;

                if (zoomCenter.x < uvExtension.x * .5f) zoomCenter.x = uvExtension.x * .5f;
                else if (zoomCenter.x > 1.f - uvExtension.x * .5f) zoomCenter.x = 1.f - uvExtension.x * .5f;
            }
        }
        if (remainingWndSize.y > 0) {
            const float remainingSizeInUVSpace = 2.f * zoomFactor * (remainingWndSize.y / imageSz.y);
            const float deltaUV = uvExtension.y;
            const float remainingUV = 1.f - deltaUV;
            if (deltaUV < 1) {
                float adder = (remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace);
                uvExtension.y += adder;
                remainingWndSize.y -= adder * zoom * imageSz.y;
                imageSz.y += adder * zoom * imageSz.y;

                if (zoomCenter.y < uvExtension.y * .5f) zoomCenter.y = uvExtension.y * .5f;
                else if (zoomCenter.y > 1.f - uvExtension.y * .5f) zoomCenter.y = 1.f - uvExtension.y * .5f;
            }
        }

        ImVec2 uv0((zoomCenter.x - uvExtension.x * .5f), (zoomCenter.y - uvExtension.y * .5f));
        ImVec2 uv1((zoomCenter.x + uvExtension.x * .5f), (zoomCenter.y + uvExtension.y * .5f));

        /* // Here we use just the window size, but then ImGui::IsItemHovered() should be moved below this block. How to do it?
        ImVec2 startPos=window->DC.CursorPos;
        startPos.x+= remainingWndSize.x*.5f;
        startPos.y+= remainingWndSize.y*.5f;
        ImVec2 endPos(startPos.x+imageSz.x,startPos.y+imageSz.y);
        ImRect bb(startPos, endPos);
        ItemSize(bb);
        if (!ItemAdd(bb, NULL)) return rv;*/

        ImVec2 startPos = bb.Min, endPos = bb.Max;
        startPos.x += remainingWndSize.x * .5f;
        startPos.y += remainingWndSize.y * .5f;
        endPos.x = startPos.x + imageSz.x;
        endPos.y = startPos.y + imageSz.y;

        window->DrawList->AddImage(user_texture_id, startPos, endPos, uv0, uv1);

        return rv;
    }
} //namespace ImGui
