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
#ifndef UTIL_STATE_TRACKER_H_
#define UTIL_STATE_TRACKER_H_

namespace Divide {

template <typename T>
class StateTracker {
   protected:
    struct optionalValue {
        T _value;
        bool _initialized;

        optionalValue()
            : _value(static_cast<T>(0)),
              _initialized(false)
        {
        }
    };

   public:
    enum class State : U8 {
        SKELETON_RENDERED = 0,
        BOUNDING_BOX_RENDERED = 1,
        COUNT
    };

    StateTracker()
    {
    }

    StateTracker(const StateTracker& other)
        : _trackedValues(other._trackedValues)
    {
    }

    ~StateTracker()
    {
    }



    StateTracker& operator=(const StateTracker& other) {
        _trackedValues = other._trackedValues;
        return *this;
    }

    [[nodiscard]] bool isTrackedValueInitialized(State state) const {
        return _trackedValues[to_U32(state)].initialized;
    }

    T getTrackedValue(State state, bool& isInitialized) const {
        const optionalValue& value = _trackedValues[to_U32(state)];
        isInitialized = value.initialized;
        return value.value;
    }

    T getTrackedValue(State state) const {
        return _trackedValues[to_U32(state)].value;
    }

    void setTrackedValue(State state, T value) {
        _trackedValues[to_U32(state)].value = value;
        _trackedValues[to_U32(state)].initialized = true;
    }

    /// Init will not change an already initialized value
    void initTrackedValue(State state, const T value) {
        getTrackedValue(state);
        if (!_trackedValues[to_U32(state)].initialized) {
            setTrackedValue(state, value);
        }
    }

   protected:
    std::array<optionalValue, to_base(State::COUNT)> _trackedValues;
};

}  // namespace Divide

#endif// UTIL_STATE_TRACKER_H_
