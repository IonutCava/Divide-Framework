#include "stdafx.h"

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
} //namespace ImGui
