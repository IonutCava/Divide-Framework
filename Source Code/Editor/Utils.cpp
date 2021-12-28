#include "stdafx.h"

#include "Headers/Utils.h"

#include <imgui_internal.h>

namespace Divide {
namespace {
    static I32 g_lastComponentWidhtPushCount = 0;
    static bool g_isBoldButtonPushed = false;
    static bool g_isNarrowLabelWidthPushed = false;
    static const char* g_pushedTooltip = "";
}
} //namespace Divide

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
        F32 GetLineHeight() noexcept {
            return GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
        }
        
        void AddUnderLine() {
            ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            min.y = max.y;
            ImGui::GetWindowDrawList()->AddLine(min, max, ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
        }

        void BeginPropertyTable(const I32 numComponents, const char* label) {
            ImFont* boldFont = ImGui::GetIO().Fonts->Fonts[1];

            ImGui::PushID(label);
            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, g_isNarrowLabelWidthPushed ? LabelColumnWidthNarrow : LabelColumnWidth);
            ImGui::PushFont(boldFont);
            ImGui::Text(label);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (Util::IsPushedTooltip()) {
                    ImGui::SetTooltip(Util::PushedToolTip());
                } else {
                    ImGui::SetTooltip(label);
                }
            }

            ImGui::PopFont();
            ImGui::NextColumn();
            if (numComponents == 1) {
                ImGui::PushItemWidth(ImGui::CalcItemWidth());
            } else {
                ImGui::PushMultiItemsWidths(numComponents, ImGui::CalcItemWidth());
            }
            g_lastComponentWidhtPushCount = numComponents;
        }

        void EndPropertyTable() {
            for (I32 i = 0; i < g_lastComponentWidhtPushCount; ++i) {
                ImGui::PopItemWidth();
            }
            g_lastComponentWidhtPushCount = 0;
            ImGui::Columns(1);
            ImGui::PopID();
        }

        void PushBoldFont() {
            if (!g_isBoldButtonPushed) {
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                g_isBoldButtonPushed = true;
            }
        }

        void PopBoldFont() {
            if (g_isBoldButtonPushed) {
                ImGui::PopFont();
                g_isBoldButtonPushed = false;
            }
        }

        void PushNarrowLabelWidth() {
            if (!g_isNarrowLabelWidthPushed) {
                g_isNarrowLabelWidthPushed = true;
            }
        }

        void PopNarrowLabelWidth() {
            if (g_isNarrowLabelWidthPushed) {
                g_isNarrowLabelWidthPushed = false;
            }
        }

        void PushTooltip(const char* tooltip) {
            g_pushedTooltip = tooltip;
        }

        void PopTooltip() {
            g_pushedTooltip = "";
        }

        [[nodiscard]] bool IsPushedTooltip() {
            return strlen(g_pushedTooltip) > 0;
        }

        const char* PushedToolTip() {
            return g_pushedTooltip;
        }

        void PushButtonStyle(const bool bold,
                             const ImVec4 buttonColour,
                             const ImVec4 buttonColourHovered,
                             const ImVec4 buttonColourActive)
        {
            if (bold) {
                PushBoldFont();
            }
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColour);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonColourHovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonColourActive);
        }

        void PopButtonStyle() {
            PopBoldFont();
            ImGui::PopStyleColor(3);
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

        bool colourInput4(Editor& parent, EditorComponentField& field) {
            FColour4 val = field.get<FColour4>();
            const auto setter = [val/*by value*/, &field](const FColour4& col) {
                if (col != val) {
                    field.set(col);
                    return true;
                }
                return false;
            };

            return colourInput4(parent, field._name.c_str(), val, field._readOnly, setter);
        }

        bool colourInput3(Editor& parent, EditorComponentField& field) {
            FColour3 val = field.get<FColour3>();
            const auto setter = [val/*by value*/, &field](const FColour3& col) {
                if (col != val) {
                    field.set(col);
                    return true;
                }
                return false;
            };

            return colourInput3(parent, field._name.c_str(), val, field._readOnly, setter);
        }
    } //namespace Util
} //namespace Divide
