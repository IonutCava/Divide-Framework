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
#ifndef DVD_AUTO_KEY_REPEAT_H_
#define DVD_AUTO_KEY_REPEAT_H_

// Adapted from:
// http://www.ogre3d.org/tikiwiki/tiki-index.php?page=Auto+Repeat+Key+Input
#include "Platform/Input/Headers/InputAggregatorInterface.h"

namespace Divide {
namespace Input {
/// A class that repeatedly calls "repeatKey" between "begin" and "end" calls at
/// specified intervals
class AutoRepeatKey {
   private:
    KeyEvent _key;

    D64 _elapsed;
    D64 _delay;
    /// Time intervals between key injections
    D64 _repeatDelay;
    /// The time after begin() and before repeatKey() is called. If end() is
    /// called in that interval, the key will not repeat
    D64 _initialDelay;

   protected:
    /// Override this to define custom events for key repeats
    virtual void repeatKey(const Input::KeyEvent& evt) = 0;

   public:
    virtual ~AutoRepeatKey() = default;
    /// Default constructor
    AutoRepeatKey(D64 repeatDelay = 0.035, D64 initialDelay = 0.300) noexcept;
    /// Called when a key is pressed
    void begin(const KeyEvent&evt) noexcept;
    /// Called when a key is released
    void end(const KeyEvent&evt) noexcept;
    /// Update the internal time interval between frames (microseconds)
    void update(U64 deltaTimeUS);
    /// Adjust delay between key injections
    void setRepeatDelay(const D64 repeatDelay) noexcept { _repeatDelay = repeatDelay; }
    /// Adjust the initial delay before we start injecting key repeats
    void setInitialDelay(const D64 initialDelay) noexcept { _initialDelay = initialDelay; }
};
};  // namespace Input
};  // namespace Divide

#endif //DVD_AUTO_KEY_REPEAT_H_
