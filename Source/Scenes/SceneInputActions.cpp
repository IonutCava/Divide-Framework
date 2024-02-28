

#include "Headers/SceneInputActions.h"

namespace Divide {

namespace {
    DELEGATE<void, InputParams> no_op = [](InputParams) noexcept { NOP(); };
}

InputAction::InputAction(DELEGATE<void, InputParams> action)
    : _action(MOV(action))
{
}

void InputAction::displayName(const Str<64>& name) {
    _displayName = name;
}

InputActionList::InputActionList()
    :_noOPAction(no_op)
{
    _noOPAction.displayName("no-op");
}

bool InputActionList::registerInputAction(const U16 id, const InputAction& action) {
    if (_inputActions.find(id) != std::cend(_inputActions)) {
        return false;
    }

    _inputActions.emplace(id, action);
    return true;
}

bool InputActionList::registerInputAction(const U16 id, const DELEGATE<void, InputParams>& action) {
    return registerInputAction(id, InputAction{action});
}

InputAction& InputActionList::getInputAction(const U16 id) {
    const hashMap<U16, InputAction>::iterator it = _inputActions.find(id);

    if (it != std::cend(_inputActions)) {
        return it->second;
    }

    return _noOPAction;
}

const InputAction& InputActionList::getInputAction(const U16 id) const {
  const hashMap<U16, InputAction>::const_iterator it = _inputActions.find(id);

    if (it != std::cend(_inputActions)) {
        return it->second;
    }

    return _noOPAction;
}


void PressReleaseActions::clear() noexcept {
    for (Entry& entry : _entries) {
        entry.clear();
    }
}

bool PressReleaseActions::add(const Entry& entry) {
    _entries.push_back(entry);
    std::sort(std::begin(_entries), std::end(_entries), [](const Entry& lhs, const Entry& rhs) noexcept {
        return lhs.modifiers().size() > rhs.modifiers().size();
    });

    return true;
}

} //namespace Divide