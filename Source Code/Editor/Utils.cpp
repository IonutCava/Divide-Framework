#include "stdafx.h"

#include "Headers/Utils.h"

#include <imgui_internal.h>

namespace ImGui {
    bool InputDoubleN(const char* label, double* v, const int components, const char* display_format, const ImGuiInputTextFlags extra_flags)     {
        const ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const ImGuiContext& g = *GImGui;
        bool value_changed = false;
        BeginGroup();
        PushID(label);
        PushMultiItemsWidths(components, CalcItemWidth());
        for (int i = 0; i < components; i++)         {
            PushID(i);
            value_changed |= InputDouble("##v", &v[i], 0.0, 0.0, display_format, extra_flags);
            SameLine(0, g.Style.ItemInnerSpacing.x);
            PopID();
            PopItemWidth();
        }
        PopID();

        TextUnformatted(label, FindRenderedTextEnd(label));
        EndGroup();

        return value_changed;
    }

    bool InputDouble2(const char* label, double v[2], const char* display_format, const ImGuiInputTextFlags extra_flags)     {
        return InputDoubleN(label, v, 2, display_format, extra_flags);
    }

    bool InputDouble3(const char* label, double v[3], const char* display_format, const ImGuiInputTextFlags extra_flags)     {
        return InputDoubleN(label, v, 3, display_format, extra_flags);
    }

    bool InputDouble4(const char* label, double v[4], const char* display_format, const ImGuiInputTextFlags extra_flags)     {
        return InputDoubleN(label, v, 4, display_format, extra_flags);
    }
}

namespace Divide {
    namespace Util {
        void BeginPropertyTable(const I32 numComponents, const char* label) {
            ImFont* boldFont = ImGui::GetIO().Fonts->Fonts[1];

            ImGui::PushID(label);
            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, LabelColumnWidth);
            ImGui::PushFont(boldFont);
            ImGui::Text(label);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(label);
            }
            ImGui::PopFont();
            ImGui::NextColumn();
            ImGui::PushMultiItemsWidths(numComponents, ImGui::CalcItemWidth());
        }

        void EndPropertyTable() {
            ImGui::Columns(1);
            ImGui::PopID();
        }

        const char* GetFormat(ImGuiDataType dataType, const char* input, const bool hex) {
            if (input == nullptr || strlen(input) == 0) {
                const auto unsignedType = [dataType]() {
                    return dataType == ImGuiDataType_U8 || dataType == ImGuiDataType_U16 || dataType == ImGuiDataType_U32 || dataType == ImGuiDataType_U64;
                };

                return dataType == ImGuiDataType_Float ? "%.3f"
                                : dataType == ImGuiDataType_Double ? "%.6f"
                                           : hex ? "%08X" : (unsignedType() ? "%u" : "%d");
            }

            return input;
        }

        bool colourInput4(Editor& parent, EditorComponentField& field, const char* name) {
            FColour4 val = field.get<FColour4>();
            const auto setter = [val/*by value*/, &field](const FColour4& col) {
                if (col != val) {
                    field.set(col);
                    return true;
                }
                return false;
            };

            return colourInput4(parent, name, val, field._readOnly, setter);
        }

        bool colourInput3(Editor& parent, EditorComponentField& field, const char* name) {
            FColour3 val = field.get<FColour3>();
            const auto setter = [val/*by value*/, &field](const FColour3& col) {
                if (col != val) {
                    field.set(col);
                    return true;
                }
                return false;
            };

            return colourInput3(parent, name, val, field._readOnly, setter);
        }
    } //namespace Util
} //namespace Divide
