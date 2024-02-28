/*
Copyright (c) 2018 DIVIDE-Studio
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
#ifndef _SCENE_INPUT_ACTIONS_H_
#define _SCENE_INPUT_ACTIONS_H_

#include "Platform/Input/Headers/Input.h"

namespace Divide {

struct InputParams {
    InputParams(const U8 deviceIndex) noexcept
        : InputParams(deviceIndex, -1)
    {
    }

    InputParams(const U8 deviceIndex, const I32 var1) noexcept
        : InputParams(deviceIndex, var1, -1)
    {
    }

    InputParams(const U8 deviceIndex, const I32 var1, const I32 var2) noexcept
        : InputParams(deviceIndex, var1, var2, -1)
    {
    }

    InputParams(const U8 deviceIndex, const I32 var1, const I32 var2, const I32 var3) noexcept
        : InputParams(deviceIndex, var1, var2, var3, -1)
    {
    }

    InputParams(const U8 deviceIndex, const I32 var1, const I32 var2, const I32 var3, const I32 var4) noexcept
        : _var{ var1, var2, var3, var4 },
          _deviceIndex(deviceIndex)
    {
    }

    I32 _var[4];
    U8  _deviceIndex;
};

class PressReleaseActions {
public:
    enum class Action : U8 {
        PRESS = 0,
        RELEASE,
        COUNT
    };

    enum class Modifier : U8 {
        LEFT_CTRL,
        RIGHT_CTRL,
        LEFT_ALT,
        RIGHT_ALT,
        LEFT_SHIFT,
        RIGHT_SHIFT,
        COUNT
    };

    static constexpr Input::KeyCode s_modifierMappings[to_base(Modifier::COUNT)] = {
        Input::KeyCode::KC_LCONTROL,
        Input::KeyCode::KC_RCONTROL,
        Input::KeyCode::KC_LMENU,
        Input::KeyCode::KC_RMENU,
        Input::KeyCode::KC_LSHIFT,
        Input::KeyCode::KC_RSHIFT
    };

    static constexpr const char* s_modifierNames[to_base(Modifier::COUNT)] = {
        "LCtrl", "RCtrl", "LAlt", "RAlt", "LShift", "RShift"
    };

    struct Entry {
        PROPERTY_RW(eastl::set<Input::KeyCode>, modifiers);
        PROPERTY_RW(eastl::set<U16>, pressIDs);
        PROPERTY_RW(eastl::set<U16>, releaseIDs);

        void clear() noexcept {
            modifiers().clear();
            pressIDs().clear();
            releaseIDs().clear();
        }
    };
public:
    void clear() noexcept;
    bool add(const Entry& entry);
    
    PROPERTY_R(vector<Entry>, entries);
};

struct InputAction {
    explicit InputAction(DELEGATE<void, InputParams> action);

    DELEGATE<void, InputParams> _action;
    // This will be useful for menus and the like (defined in XML)
    Str<64> _displayName;

    void displayName(const Str<64>& name);
};

class InputActionList {
   public:
    InputActionList();

    [[nodiscard]] bool registerInputAction(U16 id, const InputAction& action);
    [[nodiscard]] bool registerInputAction(U16 id, const DELEGATE<void, InputParams>& action);
    [[nodiscard]] InputAction& getInputAction(U16 id);
    [[nodiscard]] const InputAction& getInputAction(U16 id) const;

   protected:
    hashMap<U16 /*actionID*/, InputAction> _inputActions;
    InputAction _noOPAction;
};

} //namespace Divide

#endif //_SCENE_INPUT_ACTIONS_H_
