#include "stdafx.h"

#include "Headers/Utils.h"

#undef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
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
            const auto setter = [&field](const FColour4& col) {
                field.set(col);
            };

            if (colourInput4(parent, field._name.c_str(), val, field._readOnly, setter) && val != field.get<FColour4>()) {
                field.set(val);
                return true;
            }

            return false;
        }

        bool colourInput3(Editor& parent, EditorComponentField& field) {
            FColour3 val = field.get<FColour3>();
            const auto setter = [&field](const FColour3& col) {
                field.set(col);
            };

            if (colourInput3(parent, field._name.c_str(), val, field._readOnly, setter) && val != field.get<FColour3>()) {
                field.set(val);
                return true;
            }

            return false;
        }
    }
} //namespace Divide
