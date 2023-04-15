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
#ifndef _EDITOR_UTILS_INL_
#define _EDITOR_UTILS_INL_

#include "Core/Headers/StringHelper.h"
#include "imgui_internal.h"

namespace Divide {

namespace Util {
template<typename T, bool SeparateActivate, typename Pred>
inline void RegisterUndo(Editor& editor, PushConstantType type, const T& oldVal, const T& newVal, const char* name, Pred&& dataSetter) {
    static hashMap<U64, UndoEntry<T>> _undoEntries;
    UndoEntry<T>& undo = _undoEntries[_ID(name)];
    if (!SeparateActivate || ImGui::IsItemActivated()) {
        undo._oldVal = oldVal;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        undo._type = type;
        undo._name = name;
        undo._newVal = newVal;
        undo._dataSetter = dataSetter;
        editor.registerUndoEntry(undo);
    }
}

template<typename T, bool isSlider>
inline DrawReturnValue DrawVecComponent(ImGuiDataType data_type,
                                        const char* label,
                                        T& value,
                                        const T resetValue,
                                        const T minValue,
                                        const T maxValue,
                                        const T step,
                                        const T stepFast,
                                        const bool readOnly,
                                        const bool hexadecimal,
                                        const ImVec4 buttonColour,
                                        const ImVec4 buttonColourHovered,
                                        const ImVec4 buttonColourActive,
                                        const char* format)
{
    bool ret = false, wasDeactivated = false;

    const T cStep = step * 100;
    const void* step_ptr = IS_ZERO(step) ? nullptr : (void*)&step;
    const void* step_fast_ptr = step_ptr == nullptr ? nullptr : (void*)&cStep;

    const F32 lineHeight = GetLineHeight();
    const ImVec2 buttonSize = { lineHeight + 3.f, lineHeight };

    if (readOnly) {
        PushReadOnly();
    }

    PushButtonStyle(true, buttonColour, buttonColourHovered, buttonColourActive);
    if (ImGui::Button(label, buttonSize)) {
        value = resetValue;
        ret = true;
    }
    PopButtonStyle();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Reset to %s", Util::StringFormat(format, resetValue).c_str());
    }
        
    ImGui::SameLine();

    if constexpr(isSlider) {
        const ImGuiSliderFlags flags = readOnly ? ImGuiSliderFlags_ReadOnly : 0u;
        ret = ImGui::DragScalar(Util::StringFormat("##_value_%s_", label).c_str(), data_type, &value, 0.1f, &minValue, &maxValue, format, flags) || ret;
    } else {
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                            ImGuiInputTextFlags_CharsNoBlank |
                                            (hexadecimal ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal) |
                                            (readOnly ? ImGuiInputTextFlags_ReadOnly : 0u);
        ret = ImGui::InputScalar(Util::StringFormat("##_value_%s_", label).c_str(), data_type, &value, step_ptr, step_fast_ptr, format, flags) || ret;
    }
    if (ret && ImGui::IsItemDeactivated()) {
        wasDeactivated = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(format, value);
    }
    if (readOnly) {
        PopReadOnly();
    }

    return { ret, wasDeactivated };
}

template<typename T, size_t N, bool isSlider>
inline DrawReturnValue DrawVec(ImGuiDataType data_type,
                                const char* label,
                                const char* const compLabels[],
                                T* values,
                                const bool readOnly,
                                const bool hexadecimal,
                                const T resetValue,
                                const T minValue,
                                const T maxValue,
                                const T step,
                                const T stepFast,
                                const char* format)
{
    bool ret = false, wasDeactivated = false;
    BeginPropertyTable(N, label);
    for (size_t i = 0; i < N; ++i) {
        const DrawReturnValue temp = DrawVecComponent<T, isSlider>(data_type, compLabels[i], values[i], resetValue, minValue, maxValue, step, stepFast, readOnly, hexadecimal, Colours[i % 3], ColoursHovered[i % 3], Colours[i % 3], format);
        ret = temp.wasChanged || ret;
        wasDeactivated = temp.wasDeactivated || wasDeactivated;
        ImGui::SameLine();
    }
    ImGui::Dummy(ImVec2(0,0));
    EndPropertyTable();

    return { ret, wasDeactivated };
}

template<typename T, size_t N, bool isSlider>
FORCE_INLINE DrawReturnValue DrawVec(ImGuiDataType data_type,
                                        const char* label,
                                        T* values,
                                        bool readOnly,
                                        T resetValue,
                                        const char* format) 
{
    return DrawVec<T, N, isSlider>(data_type, label, FieldLabels, values, readOnly, false, resetValue, T{ 0 }, T{ 0 }, T{ 0 }, T{ 0 }, format);
}

template<typename Pred>
inline bool colourInput4(Editor& parent, const char* name, FColour4& col, const bool readOnly, Pred&& dataSetter) {
    BeginPropertyTable(1, name);
    if (readOnly) {
        PushReadOnly();
    }
    ImGui::PushID(name);
    const bool ret = ImGui::ColorEdit4("", col._v, ImGuiColorEditFlags_DefaultOptions_);
    ImGui::PopID();
    if (readOnly) {
        PopReadOnly();
    }
    EndPropertyTable();

    if (!readOnly && ret) {
        RegisterUndo<FColour4, true>(parent, PushConstantType::FCOLOUR4, col, col, name, dataSetter);
    }
        
    return readOnly ? false : (ret ? dataSetter(col) : false);
}

template<typename Pred>
inline bool colourInput3(Editor& parent, const char* name, FColour3& col, const bool readOnly, Pred&& dataSetter) {
    BeginPropertyTable(1, name);
    if (readOnly) {
        PushReadOnly();
    }
    ImGui::PushID(name);
    const bool ret = ImGui::ColorEdit3("", col._v, ImGuiColorEditFlags_DefaultOptions_);
    ImGui::PopID();
    if (readOnly) {
        PopReadOnly();
    }
    EndPropertyTable();
    if (!readOnly && ret) {
        RegisterUndo<FColour3, true>(parent, PushConstantType::FCOLOUR3, col, col, name, dataSetter);
    }

    return readOnly ? false : (ret ? dataSetter(col) : false);
}


template<typename FieldDataType, typename ComponentType, size_t num_comp>
inline bool inputOrSlider(Editor& parent, const bool isSlider, const char* label, const F32 stepIn, ImGuiDataType data_type, EditorComponentField& field, ImGuiInputTextFlags flags, const char* format) {
    if (isSlider) {
        return inputOrSlider<FieldDataType, ComponentType, num_comp, true>(parent, label, stepIn, data_type, field, flags, format);
    }
    return inputOrSlider<FieldDataType, ComponentType, num_comp, false>(parent, label, stepIn, data_type, field, flags, format);
}

template<typename FieldDataType, typename ComponentType, size_t num_comp, bool IsSlider>
inline bool inputOrSlider(Editor& parent, const char* label, const F32 stepIn, const ImGuiDataType data_type, EditorComponentField& field, const ImGuiInputTextFlags flags, const char* format) {
    FieldDataType val = field.get<FieldDataType>();
    const ComponentType cStep = static_cast<ComponentType>(stepIn * 100);

    const ComponentType min = static_cast<ComponentType>(field._range.min);
    const ComponentType max = static_cast<ComponentType>(field._range.max);

    assert(min <= max);

    const DrawReturnValue ret = 
        Util::DrawVec<ComponentType, num_comp, IsSlider>(data_type,
                                                            label,
                                                            field._labels == nullptr ? FieldLabels : field._labels,
                                                            (ComponentType*) & val,
                                                            field._readOnly,
                                                            field._hexadecimal,
                                                            static_cast<ComponentType>(field._resetValue),
                                                            min,
                                                            max,
                                                            static_cast<ComponentType>(stepIn),
                                                            cStep,
                                                            GetFormat(data_type, format, field._hexadecimal));
    if (ret.wasDeactivated && max > min) {
        if constexpr(num_comp > 1) {
            for (I32 i = 0; i < to_I32(num_comp); ++i) {
                val[i] = CLAMPED(val[i], min, max);
            }
        } else {
            val = CLAMPED(val, min, max);
        }
    }

    if (IsSlider || ret.wasChanged) {
        auto* tempData = field._data;
        auto tempSetter = field._dataSetter;
        RegisterUndo<FieldDataType, IsSlider>(parent, field._basicType, field.get<FieldDataType>(), val, label, [tempData, tempSetter](const FieldDataType& oldVal) {
            if (tempSetter != nullptr) {
                tempSetter(&oldVal);
            } else {
                *static_cast<FieldDataType*>(tempData) = oldVal;
            }
        });
    }
    if (!field._readOnly && ret.wasChanged && !COMPARE(val, field.get<FieldDataType>())) {
        field.set(val);
    }

    return ret.wasChanged;
}

template<typename T, size_t num_rows>
inline bool inputMatrix(Editor & parent, const char* label, const F32 stepIn, const ImGuiDataType data_type, EditorComponentField& field, const ImGuiInputTextFlags flags, const char* format) {
    ImGui::Separator();
    ImGui::Text("[ %s ]", label);
    if (Util::IsPushedTooltip() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(Util::PushedToolTip());
    }
    const T cStep = static_cast<T>(stepIn * 100);
    const void* step = IS_ZERO(stepIn) ? nullptr : (void*)&stepIn;
    const void* step_fast = step == nullptr ? nullptr : (void*)&cStep;

    T mat = field.get<T>();
    if (field._readOnly) {
        PushReadOnly();
    }
    bool showTooltip = false, copyToClipboard = false;;
    const char* parsedFormat = GetFormat(data_type, format, field._hexadecimal);
    bool ret = false;
    ImGui::PushItemWidth(250);
    ret = ImGui::InputScalarN(Util::StringFormat("##%s_0", label).c_str(), data_type, (void*)mat._vec[0]._v, num_rows, step, step_fast, parsedFormat, flags);
    showTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || showTooltip;
    copyToClipboard = (showTooltip && ImGui::IsMouseClicked(0)) || copyToClipboard;
    ret = ImGui::InputScalarN(Util::StringFormat("##%s_1", label).c_str(), data_type, (void*)mat._vec[1]._v, num_rows, step, step_fast, parsedFormat, flags) || ret;
    showTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || showTooltip;
    copyToClipboard = (showTooltip && ImGui::IsMouseClicked(0)) || copyToClipboard;
    if constexpr(num_rows > 2) {
        ret = ImGui::InputScalarN(Util::StringFormat("##%s_2", label).c_str(), data_type, (void*)mat._vec[2]._v, num_rows, step, step_fast, parsedFormat, flags) || ret;
        showTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || showTooltip;
        copyToClipboard = (showTooltip && ImGui::IsMouseClicked(0)) || copyToClipboard;
        if constexpr(num_rows > 3) {
            ret = ImGui::InputScalarN(Util::StringFormat("##%s_3", label).c_str(), data_type, (void*)mat._vec[3]._v, num_rows, step, step_fast, parsedFormat, flags) || ret;
            showTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || showTooltip;
            copyToClipboard = (showTooltip && ImGui::IsMouseClicked(0)) || copyToClipboard;
        }
    }
    ImGui::PopItemWidth();
    if (field._readOnly) {
        PopReadOnly();
    }

    if (showTooltip && field._readOnly) {
        string tooltip = "\n\nClick to copy values to clipboard.";
        string matrixText = "";
        for (size_t i = 0; i < num_rows; ++i) {
            auto& row = mat.m[i];
            for (size_t j = 0; j < num_rows; ++j) {
                if (row[j] >= 0.f) {
                    matrixText.append(" ");
                }
                matrixText.append(Util::StringFormat(parsedFormat, row[j]));
                if (j < num_rows - 1) {
                    matrixText.append("  ");
                }
            }
            if (i < num_rows - 1) {
                matrixText.append("\n\n");
            }
        }
            ImGui::SetTooltip((matrixText + tooltip).c_str());
            if (copyToClipboard) {
                SetClipboardText(nullptr, matrixText.c_str());
                parent.showStatusMessage("Copied values to clipboard!", Time::SecondsToMilliseconds<F32>(3.f), false);
            }
    }

    if (ret && !field._readOnly && mat != field.get<T>()) {
        auto* tempData = field._data;
        auto tempSetter = field._dataSetter;
        RegisterUndo<T, false>(parent, field._basicType, field.get<T>(), mat, label, [tempData, tempSetter](const T& oldVal) {
            if (tempSetter != nullptr) {
                tempSetter(&oldVal);
            } else {
                *static_cast<T*>(tempData) = oldVal;
            }
        });
        field.set<>(mat);
    }
    ImGui::Separator();

    return ret;
}
} //namespace Util
} //namespace Divide

#endif //_EDITOR_UTILS_INL_