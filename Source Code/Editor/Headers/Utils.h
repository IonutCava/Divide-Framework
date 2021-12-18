/*
Copyright (c) 2021 DIVIDE-Studio
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
#ifndef _EDITOR_UTILS_H_
#define _EDITOR_UTILS_H_

#include "Headers/UndoManager.h"
#include "Platform/Video/Headers/PushConstant.h"
#include "Headers/Editor.h"

namespace ImGui {
    bool InputDoubleN(const char* label, double* v, int components, const char* display_format, ImGuiInputTextFlags extra_flags);
    bool InputDouble2(const char* label, double v[2], const char* display_format, ImGuiInputTextFlags extra_flags);
    bool InputDouble3(const char* label, double v[3], const char* display_format, ImGuiInputTextFlags extra_flags);
    bool InputDouble4(const char* label, double v[4], const char* display_format, ImGuiInputTextFlags extra_flags);
} // namespace ImGui

namespace Divide {

namespace Util {
    constexpr F32 LabelColumnWidth = 200.f;
    constexpr F32 LabelColumnWidthNarrow = 110.f;

    static const ImVec4 Colours[] = {
         {0.8f, 0.1f, 0.15f, 1.f},
         {0.2f, 0.7f, 0.2f, 1.f},
         {0.1f, 0.25f, 0.8f, 1.f}
    };
    static const ImVec4 ColoursHovered[] = {
         {0.9f, 0.2f, 0.2f, 1.f},
         {0.3f, 0.8f, 0.3f, 1.f},
         {0.2f, 0.35f, 0.8f, 1.f}
    };

    constexpr char* FieldLabels[] = {
        "X", "Y", "Z", "W", "U", "V", "T"
    };
    // Separate activate is used for stuff that do continuous value changes, e.g. colour selectors, but you only want to register the old val once
    template<typename T, bool SeparateActivate, typename Pred>
    void RegisterUndo(Editor& editor, GFX::PushConstantType type, const T& oldVal, const T& newVal, const char* name, Pred&& dataSetter);

    struct DrawReturnValue {
        bool wasChanged = false;
        bool wasDeactivated = false;
    };

    template<typename T, size_t N, bool isSlider>
    DrawReturnValue DrawVec(ImGuiDataType data_type,
                            const char* label,
                            T* values,
                            bool readOnly,
                            T resetValue = 0,
                            const char* format = "%.2f");

    template<typename T, size_t N, bool isSlider>
    DrawReturnValue DrawVec(ImGuiDataType data_type, 
                            const char* label, 
                            const char* const compLabels[],
                            T* values,
                            bool readOnly,
                            bool hexadecimal = false,
                            T resetValue = 0,
                            T minValue = 0,
                            T maxValue = 0,
                            T step = 0,
                            T stepFast = 0,
                            const char* format = "%.2f");

    template<typename T, bool isSlider>
    DrawReturnValue DrawVecComponent(ImGuiDataType data_type,
                                     const char* label,
                                     T& value,
                                     T resetValue,
                                     T minValue,
                                     T maxValue,
                                     T step,
                                     T stepFast,
                                     bool readOnly,
                                     bool hexadecimal,
                                     ImVec4 buttonColour,
                                     ImVec4 buttonColourHovered,
                                     ImVec4 buttonColourActive,
                                     const char* format = "%.2f");

    const char* GetFormat(ImGuiDataType dataType, const char* input, bool hex);
    bool colourInput4(Editor& parent, EditorComponentField& field, const char* name = "");
    bool colourInput3(Editor& parent, EditorComponentField& field, const char* name = "");

    template<typename Pred>
    bool colourInput4(Editor& parent, const char* name, FColour4& col, const bool readOnly, Pred&& dataSetter);
    template<typename Pred>
    bool colourInput3(Editor& parent, const char* name, FColour3& col, const bool readOnly, Pred&& dataSetter);

    template<typename FieldDataType, typename ComponentType, size_t num_comp>
    bool inputOrSlider(Editor& parent, const bool isSlider, const char* label, const F32 stepIn, ImGuiDataType data_type, EditorComponentField& field, ImGuiInputTextFlags flags, const char* format);

    template<typename FieldDataType, typename ComponentType, size_t num_comp, bool IsSlider>
    bool inputOrSlider(Editor& parent, const char* label, const F32 stepIn, const ImGuiDataType data_type, EditorComponentField& field, const ImGuiInputTextFlags flags, const char* format);

    template<typename T, size_t num_rows>
    bool inputMatrix(Editor& parent, const char* label, const F32 stepIn, const ImGuiDataType data_type, EditorComponentField& field, const ImGuiInputTextFlags flags, const char* format);

    [[nodiscard]] F32 GetLineHeight() noexcept;
    void AddUnderLine();

    void BeginPropertyTable(I32 numComponents, const char* label);
    void EndPropertyTable();

    void PushButtonStyle(bool bold,
                        ImVec4 buttonColour,
                        ImVec4 buttonColourHovered,
                        ImVec4 buttonColourActive);
    void PopButtonStyle();
    void PushBoldFont();
    void PopBoldFont();
    void PushNarrowLabelWidth();
    void PopNarrowLabelWidth();
    void PushTooltip(const char* tooltip);
    void PopTooltip();
    [[nodiscard]] bool IsPushedTooltip();
    const char* PushedToolTip();
} //namespace Util
} //namespace Divide

#include "Utils.inl"

#endif // _EDITOR_UTILS_H_
