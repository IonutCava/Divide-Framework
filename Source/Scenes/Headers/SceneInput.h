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
#ifndef DVD_SCENE_INPUT_H_
#define DVD_SCENE_INPUT_H_

#include "SceneInputActions.h"
#include "Platform/Input/Headers/InputAggregatorInterface.h"

namespace Divide
{
    struct JoystickMapKey
    {
        U32 _id{ 0u };
        Input::JoystickElementType _element{ Input::JoystickElementType::COUNT };

        bool operator==(const JoystickMapKey&) const = default;
    };


    struct JoystickMapKeyHash
    {
        std::size_t operator () ( const JoystickMapKey& p ) const
        {
            size_t hash = 17;
            Util::Hash_combine( hash, p._element, p._id );
            return hash;
        }
    };

    // This is the callback equivalent of PressReleaseAction with IDs resolved
    struct PressReleaseActionCbks
    {
        struct Entry
        {
            eastl::set<Input::KeyCode> _modifiers;
            std::array<vector<DELEGATE<void, InputParams>>, to_base( PressReleaseActions::Action::COUNT )> _actions;
        };

        vector<Entry> _entries;

        void from( const PressReleaseActions& actions, const InputActionList& actionList );
    };

    class SceneInput final : public Input::InputAggregatorInterface
    {
        public:
        using KeyMapCache = hashMap<Input::KeyCode, PressReleaseActionCbks>;
        using MouseMapCache = hashMap<Input::MouseButton, PressReleaseActionCbks>;
        using JoystickMapCacheEntry = ska::bytell_hash_map<JoystickMapKey, PressReleaseActionCbks, JoystickMapKeyHash>;
        using JoystickMapCache = ska::bytell_hash_map<BaseType<Input::Joystick>, JoystickMapCacheEntry>;

        using KeyMap = hashMap<Input::KeyCode, PressReleaseActions>;
        using MouseMap = hashMap<Input::MouseButton, PressReleaseActions>;
        using JoystickMapEntry = ska::bytell_hash_map<JoystickMapKey, PressReleaseActions, JoystickMapKeyHash>;
        using JoystickMap = ska::bytell_hash_map<BaseType<Input::Joystick>, JoystickMapEntry>;

        struct KeyLogState
        {
            Input::KeyCode _key{ Input::KeyCode::KC_SLEEP };
            Input::InputState _state{ Input::InputState::COUNT };
        };

        struct MouseLogState
        {
            int2 _position{ VECTOR2_ZERO };
            Input::MouseButton _button{ Input::MouseButton::COUNT };
            Input::InputState _state{ Input::InputState::COUNT };
        };

        using KeyLog = vector<KeyLogState>;
        using MouseBtnLog = vector<MouseLogState>;

        explicit SceneInput( Scene& parentScene );

        //Keyboard: return true if input was consumed
        bool onKeyDownInternal( Input::KeyEvent& argInOut) override;
        bool onKeyUpInternal( Input::KeyEvent& argInOut) override;
        /// Joystick or Gamepad: return true if input was consumed
        bool joystickButtonPressedInternal( Input::JoystickEvent& argInOut) override;
        bool joystickButtonReleasedInternal( Input::JoystickEvent& argInOut) override;
        bool joystickAxisMovedInternal( Input::JoystickEvent& argInOut) override;
        bool joystickPovMovedInternal( Input::JoystickEvent& argInOut) override;
        bool joystickBallMovedInternal( Input::JoystickEvent& argInOut) override;
        bool joystickAddRemoveInternal( Input::JoystickEvent& argInOut) override;
        bool joystickRemapInternal( Input::JoystickEvent& argInOut) override;
        /// Mouse: return true if input was consumed
        bool mouseMovedInternal( Input::MouseMoveEvent& argInOut) override;
        bool mouseButtonPressedInternal( Input::MouseButtonEvent& argInOut) override;
        bool mouseButtonReleasedInternal( Input::MouseButtonEvent& argInOut) override;
        bool onTextInputInternal(Input::TextInputEvent& argInOut) override;
        bool onTextEditInternal(Input::TextEditEvent& argInOut) override;
        /// Returns false if the key is already assigned and couldn't be merged
        /// Call removeKeyMapping for the specified key first
        bool addKeyMapping( Input::KeyCode key, const PressReleaseActions::Entry& keyCbks );
        /// Returns false if the key wasn't previously assigned
        bool removeKeyMapping( Input::KeyCode key );
        /// Returns true if the key has a valid mapping and sets the callback output
        /// to the mapping's function
        bool getKeyMapping( Input::KeyCode key, PressReleaseActionCbks& keyCbksOut );

        /// Returns false if the button is already assigned.
        /// Call removeButtonMapping for the specified key first
        bool addMouseMapping( Input::MouseButton button, const PressReleaseActions::Entry& btnCbks );
        /// Returns false if the button wasn't previously assigned
        bool removeMouseMapping( Input::MouseButton button );
        /// Returns true if the button has a valid mapping and sets the callback
        /// output to the mapping's function
        bool getMouseMapping( Input::MouseButton button, PressReleaseActionCbks& btnCbksOut );

        /// Returns false if the button is already assigned.
        /// Call removeJoystickMapping for the specified key first
        bool addJoystickMapping( Input::Joystick device, Input::JoystickElementType elementType, U32 id, const PressReleaseActions::Entry& btnCbks );
        /// Returns false if the button wasn't previously assigned
        bool removeJoystickMapping( Input::Joystick device, Input::JoystickElementType elementType, U32 id );
        /// Returns true if the button has a valid mapping and sets the callback
        /// output to the mapping's function
        bool getJoystickMapping( Input::Joystick device, Input::JoystickElementType elementType, U32 id, PressReleaseActionCbks& btnCbksOut );

        InputActionList& actionList() noexcept;

        U8 getPlayerIndexForDevice( Input::InputDeviceType deviceType, U8 deviceIndex ) const;

        void flushCache();

        void onPlayerAdd( U8 index );
        void onPlayerRemove( U8 index );

        void onSetActive();
        void onRemoveActive();

        protected:
        bool handleCallbacks( const PressReleaseActionCbks& cbks,
                              const InputParams& params,
                              bool onPress );

        private:
        Scene& _parentScene;

        KeyMap _keyMap;
        MouseMap _mouseMap;
        JoystickMap _joystickMap;

        KeyMapCache _keyMapCache;
        MouseMapCache _mouseMapCache;
        JoystickMapCache _joystickMapCache;

        InputActionList _actionList;

        hashMap<U8, KeyLog> _keyLog;
        hashMap<U8, MouseBtnLog> _mouseBtnLog;

    };  // SceneInput

    FWD_DECLARE_MANAGED_CLASS( SceneInput );

}  // namespace Divide
#endif  //DVD_SCENE_INPUT_H_
