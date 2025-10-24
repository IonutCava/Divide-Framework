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
#ifndef DVD_SCENE_INPUT_ACTIONS_H_
#define DVD_SCENE_INPUT_ACTIONS_H_

#include "Platform/Input/Headers/Input.h"

namespace Divide {

struct InputParams
{
    static constexpr U8 MAX_PARAMS = 6u;

    explicit InputParams(const Input::InputDeviceType deviceType, const U32 deviceIndex, const U8 elementIndex) noexcept
        : _deviceIndex( deviceIndex )
        , _elementIndex(elementIndex )
        , _deviceType( deviceType )
    {
    }

    vec2<F32> _coords{0.f};
    U32 _deviceIndex{0u};
    union
    {
        U32 _modMask{0u};
        U32 _povMask;
        I16 _signedData[2];
    };

    U8   _elementIndex{0u};
    Input::InputDeviceType _deviceType{Input::InputDeviceType::COUNT};
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

#endif //DVD_SCENE_INPUT_ACTIONS_H_
