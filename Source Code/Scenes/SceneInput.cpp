#include "stdafx.h"

#include "Headers/Scene.h"
#include "Headers/SceneInput.h"

#include "Core/Headers/PlatformContext.h"

namespace Divide {

namespace {
    constexpr bool g_recordInput = true;
}

void PressReleaseActionCbks::from(const PressReleaseActions& actions, const InputActionList& actionList) {

    _entries.reserve(actions.entries().size());
    for (const PressReleaseActions::Entry& entry : actions.entries()) {
        Entry& newEntry = _entries.emplace_back();
        newEntry._actions[to_base(PressReleaseActions::Action::PRESS)].reserve(entry.pressIDs().size());
        newEntry._actions[to_base(PressReleaseActions::Action::RELEASE)].reserve(entry.releaseIDs().size());
        newEntry._modifiers = entry.modifiers();

        for (const U16 id : entry.pressIDs()) {
            newEntry._actions[to_base(PressReleaseActions::Action::PRESS)].push_back(actionList.getInputAction(id)._action);
        }
        for (const U16 id : entry.releaseIDs()) {
            newEntry._actions[to_base(PressReleaseActions::Action::RELEASE)].push_back(actionList.getInputAction(id)._action);
        }
    }
}

SceneInput::SceneInput(Scene& parentScene) 
    : _parentScene(parentScene)
{
}


void SceneInput::onSetActive() {
    
}

void SceneInput::onRemoveActive() {

}

void SceneInput::onPlayerAdd(const U8 index) {
    insert(_keyLog, index, KeyLog());
    insert(_mouseBtnLog, index, MouseBtnLog());
}

void SceneInput::onPlayerRemove(const U8 index) {
    _keyLog.find(index)->second.clear();
    _mouseBtnLog.find(index)->second.clear();
}

U8 SceneInput::getPlayerIndexForDevice(const U8 deviceIndex) const {
    return deviceIndex;
}

bool SceneInput::handleCallbacks(const PressReleaseActionCbks& cbks,
                                 const InputParams& params,
                                 const bool onPress)
{
    for (const PressReleaseActionCbks::Entry& entry : cbks._entries) {
        //Check modifiers
        for (const Input::KeyCode modifier : entry._modifiers) {
            if (GetKeyState(params._deviceIndex, modifier) != Input::InputState::PRESSED) {
                goto next;
            }
        }

        {
            const auto& actions = entry._actions[to_base(onPress ? PressReleaseActions::Action::PRESS 
                                                                 : PressReleaseActions::Action::RELEASE)];
            if (!actions.empty()) {
                for (const auto& action : actions) {
                    action(params);
                }

                return true;
            }
        }

    next:
        //Failed modifier test
        NOP();
    }

    return false;
}

bool SceneInput::onKeyDown(const Input::KeyEvent& arg) {
    if (g_recordInput) {
        _keyLog[arg._deviceIndex].emplace_back(
            KeyLogState{
                arg._key,
                Input::InputState::PRESSED
            }
        );
    }

    PressReleaseActionCbks cbks;
    if (getKeyMapping(arg._key, cbks)) {
        return handleCallbacks(cbks,
                               InputParams(arg._deviceIndex, to_I32(arg._key), arg._modMask),
                               true);
    }

    return false;
}

bool SceneInput::onKeyUp(const Input::KeyEvent& arg) {
    if (g_recordInput) {
        _keyLog[arg._deviceIndex].emplace_back(
            KeyLogState{
                arg._key,
                Input::InputState::RELEASED
            }
        );
    }

    PressReleaseActionCbks cbks;
    if (getKeyMapping(arg._key, cbks)) {
        return handleCallbacks(cbks,
                               InputParams(arg._deviceIndex, to_I32(arg._key), arg._modMask),
                               false);
    }

    return false;
}

bool SceneInput::joystickButtonPressed(const Input::JoystickEvent& arg) {
    const Input::Joystick joy = static_cast<Input::Joystick>(arg._deviceIndex);
    PressReleaseActionCbks cbks;


    if (getJoystickMapping(joy, arg._element._type, arg._element._elementIndex, cbks)) {
        return handleCallbacks(cbks, InputParams(arg._deviceIndex, to_I32(arg._element._elementIndex)), true);
    }

    return false;
}

bool SceneInput::joystickButtonReleased(const Input::JoystickEvent& arg) {
    const Input::Joystick joy = static_cast<Input::Joystick>(arg._deviceIndex);

    PressReleaseActionCbks cbks;
    if (getJoystickMapping(joy, arg._element._type, arg._element._elementIndex, cbks)) {
        return handleCallbacks(cbks, InputParams(arg._deviceIndex, to_I32(arg._element._elementIndex)), false);
    }

    return false;
}

bool SceneInput::joystickAxisMoved(const Input::JoystickEvent& arg) {
    const Input::Joystick joy = static_cast<Input::Joystick>(arg._deviceIndex);

    PressReleaseActionCbks cbks;
    if (getJoystickMapping(joy, arg._element._type, arg._element._elementIndex, cbks)) {
        const InputParams params(arg._deviceIndex,
                                 arg._element._data._data, // move value
                                 arg._element._elementIndex, // axis index
                                 arg._element._data._gamePad ? 1 : 0, // is gamepad
                                 arg._element._data._deadZone); // dead zone
        return handleCallbacks(cbks, params, true);
    }
    
    return false;
}

bool SceneInput::joystickPovMoved(const Input::JoystickEvent& arg) {
    const Input::Joystick joy = static_cast<Input::Joystick>(arg._deviceIndex);

    PressReleaseActionCbks cbks;
    if (getJoystickMapping(joy, arg._element._type, arg._element._elementIndex, cbks)) {
        const InputParams params(arg._deviceIndex,
                                 arg._element._data._data);
        return handleCallbacks(cbks, params, true);
    }

    return false;
}

bool SceneInput::joystickBallMoved(const Input::JoystickEvent& arg) {
    const Input::Joystick joy = static_cast<Input::Joystick>(arg._deviceIndex);

    PressReleaseActionCbks cbks;
    if (getJoystickMapping(joy, arg._element._type, arg._element._elementIndex, cbks)) {
        const InputParams params(arg._deviceIndex,
                                 arg._element._data._smallData[0],
                                 arg._element._data._smallData[1],
                                 arg._element._data._gamePad ? 1 : 0);
        return handleCallbacks(cbks, params, true);
    }

    return false;
}

bool SceneInput::joystickAddRemove(const Input::JoystickEvent& arg) {
    return false;
}

bool SceneInput::joystickRemap(const Input::JoystickEvent &arg) {
    return false;
}

bool SceneInput::mouseMoved(const Input::MouseMoveEvent& arg) {
    const PlayerIndex idx = getPlayerIndexForDevice(arg._deviceIndex);
    SceneStatePerPlayer& state = _parentScene.state()->playerState(idx);

    if (arg.wheelEvent()) {
        const I32 wheel = arg.WheelV();
        state.zoom(wheel > 0 ? MoveDirection::POSITIVE
                                : wheel < 0 ? MoveDirection::NEGATIVE
                                            : MoveDirection::NONE);
    } else if (state.cameraLockedToMouse()) {
        const I32 xRel = arg.relativePos().x;
        const I32 yRel = arg.relativePos().y;
        state.angleLR(xRel > 0 ? MoveDirection::POSITIVE
                                    : xRel < 0 ? MoveDirection::NEGATIVE
                                                : MoveDirection::NONE);

        state.angleUD(yRel > 0 ? MoveDirection::POSITIVE
                                                : yRel < 0 ? MoveDirection::NEGATIVE
                                                                : MoveDirection::NONE);
    }

    return Attorney::SceneInput::mouseMoved(_parentScene, arg);
}

bool SceneInput::mouseButtonPressed(const Input::MouseButtonEvent& arg) {

    if (g_recordInput) {
        _mouseBtnLog[arg._deviceIndex].emplace_back(
            MouseLogState{
                arg.button(),
                Input::InputState::PRESSED,
                vec2<I32>(arg.absPosition().x, arg.absPosition().y)
            }
        );
    }

    PressReleaseActionCbks cbks = {};
    if (getMouseMapping(arg.button(), cbks)) {
        InputParams params(arg._deviceIndex, to_I32(arg.button()));
        params._var[2] = arg.absPosition().x;
        params._var[3] = arg.absPosition().y;
        return handleCallbacks(cbks, params, true);
    }

    return false;
}

bool SceneInput::mouseButtonReleased(const Input::MouseButtonEvent& arg) {
    if (g_recordInput) {
        _mouseBtnLog[arg._deviceIndex].emplace_back(
            MouseLogState{
                arg.button(),
                Input::InputState::RELEASED,
                vec2<I32>(arg.absPosition().x, arg.absPosition().y)
            }
        );
    }

    PressReleaseActionCbks cbks = {};
    if (getMouseMapping(arg.button(), cbks)) {
        InputParams params(arg._deviceIndex, to_I32(arg.button()));
        params._var[2] = arg.absPosition().x;
        params._var[3] = arg.absPosition().y;
        return handleCallbacks(cbks, params, false);
    }

    return false;
}

bool SceneInput::onUTF8([[maybe_unused]] const Input::UTF8Event& arg) {
    return false;
}

bool SceneInput::addKeyMapping(const Input::KeyCode key, const PressReleaseActions::Entry& keyCbks) {
    return _keyMap[key].add(keyCbks);
}

bool SceneInput::removeKeyMapping(const Input::KeyCode key) {
    const KeyMap::iterator it = _keyMap.find(key);
    if (it != std::end(_keyMap)) {
        _keyMap.erase(it);
        return false;
    }

    return false;
}

bool SceneInput::getKeyMapping(const Input::KeyCode key, PressReleaseActionCbks& keyCbksOut) {
    const KeyMapCache::const_iterator itCache = _keyMapCache.find(key);
    if (itCache != std::cend(_keyMapCache)) {
        keyCbksOut = itCache->second;
        return true;
    }

    const KeyMap::const_iterator it = _keyMap.find(key);
    if (it != std::cend(_keyMap)) {
        const PressReleaseActions& actions = it->second;
        keyCbksOut.from(actions, _actionList);
        insert(_keyMapCache, key, keyCbksOut);

        return true;
    }

    return false;
}

bool SceneInput::addMouseMapping(const Input::MouseButton button, const PressReleaseActions::Entry& btnCbks) {
    return _mouseMap[button].add(btnCbks);
}

bool SceneInput::removeMouseMapping(const Input::MouseButton button) {
    const MouseMap::iterator it = _mouseMap.find(button);
    if (it != std::end(_mouseMap)) {
        _mouseMap.erase(it);
        return false;
    }

    return false;
}

bool SceneInput::getMouseMapping(const Input::MouseButton button, PressReleaseActionCbks& btnCbksOut) {
    const MouseMapCache::const_iterator itCache = _mouseMapCache.find(button);
    if (itCache != std::cend(_mouseMapCache)) {
        btnCbksOut = itCache->second;
        return true;
    }


    const MouseMap::const_iterator it = _mouseMap.find(button);
    if (it != std::cend(_mouseMap)) {
        const PressReleaseActions& actions = it->second;
        btnCbksOut.from(actions, _actionList);
        insert(_mouseMapCache, button, btnCbksOut);
        return true;
    }

    return false;
}

bool SceneInput::addJoystickMapping(const Input::Joystick device, const Input::JoystickElementType elementType, const U32 id, const PressReleaseActions::Entry& btnCbks) {
    const JoystickMapKey key{ elementType, id };
    return _joystickMap[to_base(device)][key].add(btnCbks);
}

bool SceneInput::removeJoystickMapping(const Input::Joystick device, const Input::JoystickElementType elementType, const U32 id) {
    JoystickMapEntry& entry = _joystickMap[to_base(device)];

    const JoystickMapEntry::iterator it = entry.find({ elementType, id });
    if (it != std::end(entry)) {
        entry.erase(it);
        return false;
    }

    return false;
}

bool SceneInput::getJoystickMapping(const Input::Joystick device, const Input::JoystickElementType elementType, U32 id, PressReleaseActionCbks& btnCbksOut) {
    JoystickMapCacheEntry& entry = _joystickMapCache[to_base(device)];

    const JoystickMapCacheEntry::const_iterator itCache = entry.find({ elementType, id });
    if (itCache != std::cend(entry)) {
        btnCbksOut = itCache->second;
        return true;
    }

    JoystickMapEntry& entry2 = _joystickMap[to_base(device)];
    const JoystickMapEntry::const_iterator it = entry2.find({ elementType, id });
    if (it != std::cend(entry2)) {
        const PressReleaseActions& actions = it->second;
        btnCbksOut.from(actions, _actionList);
        const JoystickMapKey key{ elementType, id };
        entry[key] = btnCbksOut;
        return true;
    }

    return false;
}

InputActionList& SceneInput::actionList() noexcept {
    return _actionList;
}

void SceneInput::flushCache() {
    _keyMapCache.clear();
    _mouseMapCache.clear();
    _joystickMapCache.clear();
}

}