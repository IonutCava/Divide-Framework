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
    // Separate activate is used for stuff that do continuous value changes, e.g. colour selectors, but you only want to register the old val once
    template<typename T, bool SeparateActivate, typename Pred>
    void RegisterUndo(Editor& editor, GFX::PushConstantType type, const T& oldVal, const T& newVal, const char* name, Pred&& dataSetter) {
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

    const char* GetFormat(ImGuiDataType dataType, const char* input, bool hex);
    bool colourInput4(Editor& parent, EditorComponentField& field, const char* name = "");
    bool colourInput3(Editor& parent, EditorComponentField& field, const char* name = "");

    template<typename Pred>
    bool colourInput4(Editor& parent, const char* name, FColour4& col, const bool readOnly, Pred&& dataSetter) {
        const bool ret = ImGui::ColorEdit4(name, col._v, ImGuiColorEditFlags__OptionsDefault);
        if (!readOnly) {
            RegisterUndo<FColour4, true>(parent, GFX::PushConstantType::FCOLOUR4, col, col, name, dataSetter);
        }
            
        return dataSetter(col);
    }

    template<typename Pred>
    bool colourInput3(Editor& parent, const char* name, FColour3& col, const bool readOnly, Pred&& dataSetter) {
        const bool ret = ImGui::ColorEdit3(name, col._v, ImGuiColorEditFlags__OptionsDefault);
        if (!readOnly) {
            RegisterUndo<FColour3, true>(parent, GFX::PushConstantType::FCOLOUR3, col, col, name, dataSetter);
        }

        return dataSetter(col);
    }


    template<typename T, size_t num_comp>
    bool inputOrSlider(Editor& parent, const bool isSlider, const char* label, const char* name, const float stepIn, ImGuiDataType data_type, EditorComponentField& field, ImGuiInputTextFlags flags, const char* format, float power = 1.0f) {
        if (isSlider) {
            return inputOrSlider<T, num_comp, true>(parent, label, name, stepIn, data_type, field, flags, format, power);
        }
        return inputOrSlider<T, num_comp, false>(parent, label, name, stepIn, data_type, field, flags, format, power);
    }

    template<typename T, size_t num_comp, bool IsSlider>
    bool inputOrSlider(Editor& parent, const char* label, const char* name, const float stepIn, const ImGuiDataType data_type, EditorComponentField& field, const ImGuiInputTextFlags flags, const char* format, const float power = 1.0f) {
        if (field._readOnly) {
            PushReadOnly();
        }

        T val = field.get<T>();
        const T cStep = static_cast<T>(stepIn * 100);

        const void* step = IS_ZERO(stepIn) ? nullptr : (void*)&stepIn;
        [[maybe_unused]] const void* step_fast = step == nullptr ? nullptr : (void*)&cStep;

        bool ret = false;
        if_constexpr(num_comp == 1) {
            const T min = static_cast<T>(field._range.min);
            T max = static_cast<T>(field._range.max);
            if_constexpr(IsSlider) {
                assert(min <= max);
                ret = ImGui::SliderScalar(label, data_type, (void*)&val, (void*)&min, (void*)&max, GetFormat(data_type, format, field._hexadecimal), power);
            } else {
                ret = ImGui::InputScalar(label, data_type, (void*)&val, step, step_fast, GetFormat(data_type, format, field._hexadecimal), flags);
                if (ret && ImGui::IsItemDeactivated() && max > min) {
                    CLAMP(val, min, max);
                }
            }
        } else {
            T min = T{ field._range.min };
            T max = T{ field._range.max };

            if_constexpr(IsSlider) {
                assert(min <= max);
                ret = ImGui::SliderScalarN(label, data_type, (void*)&val, num_comp, (void*)&min, (void*)&max, GetFormat(data_type, format, field._hexadecimal), power);
            } else {
                ret = ImGui::InputScalarN(label, data_type, (void*)&val, num_comp, step, step_fast, GetFormat(data_type, format, field._hexadecimal), flags);
                if (ret && ImGui::IsItemDeactivated() && max > min) {
                    for (I32 i = 0; i < to_I32(num_comp); ++i) {
                        val[i] = CLAMPED(val[i], min[i], max[i]);
                    }
                }
            }
        }
        if (IsSlider || ret) {
            auto* tempData = field._data;
            auto tempSetter = field._dataSetter;
            RegisterUndo<T, IsSlider>(parent, field._basicType, field.get<T>(), val, name, [tempData, tempSetter](const T& oldVal) {
                if (tempSetter != nullptr) {
                    tempSetter(&oldVal);
                } else {
                    *static_cast<T*>(tempData) = oldVal;
                }
            });
        }
        if (!field._readOnly && ret && !COMPARE(val, field.get<T>())) {
            field.set(val);
        }

        if (field._readOnly) {
            PopReadOnly();
        }

        return ret;
    }

    template<typename T, size_t num_rows>
    bool inputMatrix(Editor & parent, const char* label, const char* name, const float stepIn, const ImGuiDataType data_type, EditorComponentField& field, const ImGuiInputTextFlags flags, const char* format) {
        const T cStep = T(stepIn * 100);

        const void* step = IS_ZERO(stepIn) ? nullptr : (void*)&stepIn;
        const void* step_fast = step == nullptr ? nullptr : (void*)&cStep;

        T mat = field.get<T>();
        bool ret = ImGui::InputScalarN(label, data_type, (void*)mat._vec[0]._v, num_rows, step, step_fast, GetFormat(data_type, format, field._hexadecimal), flags) ||
                   ImGui::InputScalarN(label, data_type, (void*)mat._vec[1]._v, num_rows, step, step_fast, GetFormat(data_type, format, field._hexadecimal), flags);
        if_constexpr(num_rows > 2) {
            ret = ImGui::InputScalarN(label, data_type, (void*)mat._vec[2]._v, num_rows, step, step_fast, GetFormat(data_type, format, field._hexadecimal), flags) || ret;
            if_constexpr(num_rows > 3) {
                ret = ImGui::InputScalarN(label, data_type, (void*)mat._vec[3]._v, num_rows, step, step_fast, GetFormat(data_type, format, field._hexadecimal), flags) || ret;
            }
        }

        if (ret && !field._readOnly && mat != field.get<T>()) {
            auto* tempData = field._data;
            auto tempSetter = field._dataSetter;
            RegisterUndo<T, false>(parent, field._basicType, field.get<T>(), mat, name, [tempData, tempSetter](const T& oldVal) {
                if (tempSetter != nullptr) {
                    tempSetter(&oldVal);
                } else {
                    *static_cast<T*>(tempData) = oldVal;
                }
            });
            field.set<>(mat);
        }
        return ret;
    }
} //namespace Util
} //namespace Divide
#endif // _EDITOR_UTILS_H_
