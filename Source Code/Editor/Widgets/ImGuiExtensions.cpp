#include "stdafx.h"

#include "Headers/ImGuiExtensions.h"

namespace ImGui {
    bool ToggleButton(const char* str_id, bool* v)
    {
        bool ret = false;
        const ImVec2 p = GetCursorScreenPos();
        ImDrawList* draw_list = GetWindowDrawList();

        const float height = GetFrameHeight();
        const float width = height * 1.55f;
        const float radius = height * 0.50f;

        if (InvisibleButton(str_id, ImVec2(width, height))) {
            ret = true;
            *v = !*v;
        }
        ImU32 col_bg = IM_COL32(0, 0, 0, 0);
        if (IsItemHovered())
            col_bg = *v ? IM_COL32(145 + 20, 211, 68 + 20, 255) : IM_COL32(218 - 20, 218 - 20, 218 - 20, 255);
        else
            col_bg = *v ? IM_COL32(145, 211, 68, 255) : IM_COL32(218, 218, 218, 255);

        draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
        draw_list->AddCircleFilled(ImVec2(*v ? p.x + width - radius : p.x + radius, p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

        return ret;
    }
} //namespace ImGui
