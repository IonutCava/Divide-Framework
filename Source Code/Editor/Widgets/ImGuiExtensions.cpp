#include "stdafx.h"

#include "Headers/ImGuiExtensions.h"
#include <imgui_internal.h>

namespace ImGui {
    bool ToggleButton(const char* str_id, bool* v)
    {
        bool ret = false;
        const ImVec4* colors = ImGui::GetStyle().Colors;
        const ImVec2 p = GetCursorScreenPos();
        ImDrawList* draw_list = GetWindowDrawList();

        const float height = GetFrameHeight();
        const float width = height * 1.55f;
        const float radius = height * 0.50f;

        if (InvisibleButton(str_id, ImVec2(width, height))) {
            ret = true;
            *v = !*v;
        }

        if (ImGui::IsItemHovered())
            draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_ButtonActive] : ImVec4(0.78f, 0.78f, 0.78f, 1.0f)), height * 0.5f);
        else
            draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(*v ? colors[ImGuiCol_Button] : ImVec4(0.85f, 0.85f, 0.85f, 1.0f)), height * 0.50f);

        draw_list->AddCircleFilled(ImVec2(p.x + radius + (*v ? 1 : 0) * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

        return ret;
    }
} //namespace ImGui
