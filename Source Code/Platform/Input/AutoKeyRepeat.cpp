#include "stdafx.h"

#include "Headers/AutoKeyRepeat.h"

namespace Divide {
namespace Input {

AutoRepeatKey::AutoRepeatKey(const D64 repeatDelay, const D64 initialDelay) noexcept
    : _key(nullptr, 0), 
      _elapsed(0.0),
      _delay(initialDelay),
      _repeatDelay(repeatDelay),
      _initialDelay(initialDelay)
{
}

void AutoRepeatKey::begin(const KeyEvent &evt) noexcept {
    _key = evt;
    _elapsed = 0.0;
    _delay = _initialDelay;
}

void AutoRepeatKey::end(const KeyEvent &evt) noexcept {
    if (_key._text != evt._text) {
        return;
    }

    _key._key = KeyCode::KC_UNASSIGNED;
}

// Inject key repeats if the _repeatDelay expired between calls
void AutoRepeatKey::update(const U64 deltaTimeUS) {
    if (_key._key == KeyCode::KC_UNASSIGNED) {
        return;
    }

    _elapsed += Time::MicrosecondsToSeconds(deltaTimeUS);
    if (_elapsed < _delay) return;

    _elapsed -= _delay;
    _delay = _repeatDelay;

    do {
        repeatKey(to_I32(_key._key), _key._text[0]);

        _elapsed -= _repeatDelay;
    } while (_elapsed >= _repeatDelay);

    _elapsed = 0.0;
}

};  // namespace Input
};  // namespace Divide
