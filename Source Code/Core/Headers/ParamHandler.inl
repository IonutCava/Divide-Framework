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

#ifndef _CORE_PARAM_HANDLER_INL_
#define _CORE_PARAM_HANDLER_INL_

#include "Utility/Headers/Localization.h"

namespace Divide {

inline void ParamHandler::setDebugOutput(const bool logState) noexcept {
    _logState = logState;
}

template <typename T>
bool ParamHandler::isParam(const HashType nameID) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    return _params.find(nameID) != std::cend(_params);
}

template <typename T>
T ParamHandler::getParam(HashType nameID, T defaultValue) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    const ParamMap::const_iterator it = _params.find(nameID);
    if (it != std::cend(_params)) {
        return std::any_cast<T>(it->second);
    }

    Console::errorfn(Locale::Get(_ID("ERROR_PARAM_GET")), nameID);
    return defaultValue;
}

template <typename T>
void ParamHandler::setParam(HashType nameID, T&& value) {
    LockGuard<SharedMutex> w_lock(_mutex);
    _params.insert_or_assign(nameID, MOV(value));
}

template <typename T>
void ParamHandler::delParam(HashType nameID) {
    if (isParam<T>(nameID)) {
        LockGuard<SharedMutex> w_lock(_mutex);
        _params.erase(nameID);
        if (_logState) {
            Console::printfn(Locale::Get(_ID("PARAM_REMOVE")), nameID);
        }
    } else {
        Console::errorfn(Locale::Get(_ID("ERROR_PARAM_REMOVE")), nameID);
    }
}

template <>
inline string ParamHandler::getParam(HashType nameID, string defaultValue) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    const ParamStringMap::const_iterator it = _paramsStr.find(nameID);
    if (it != std::cend(_paramsStr)) {
        return it->second;
    }

    Console::errorfn(Locale::Get(_ID("ERROR_PARAM_GET")), nameID);
    return defaultValue;
}

template <>
inline void ParamHandler::setParam(const HashType nameID, string&& value) {
    LockGuard<SharedMutex> w_lock(_mutex);
    const ParamStringMap::iterator it = _paramsStr.find(nameID);
    if (it == std::end(_paramsStr)) {
        const bool result = insert(_paramsStr, nameID, MOV(value)).second;
        DIVIDE_ASSERT(result, "ParamHandler error: can't add specified value to map!");
    } else {
        it->second = MOV(value);
    }
}

template <>
inline bool ParamHandler::isParam<string>(const HashType nameID) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    return _paramsStr.find(nameID) != std::cend(_paramsStr);
}


template <>
inline void ParamHandler::delParam<string>(HashType nameID) {
    if (isParam<string>(nameID)) {
        if (_logState) {
            Console::printfn(Locale::Get(_ID("PARAM_REMOVE")), nameID);
        }
        LockGuard<SharedMutex> w_lock(_mutex);
        _paramsStr.erase(nameID);
    } else {
        Console::errorfn(Locale::Get(_ID("ERROR_PARAM_REMOVE")), nameID);
    }
}

template <>
inline bool ParamHandler::getParam(HashType nameID, const bool defaultValue) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    const ParamBoolMap::const_iterator it = _paramBool.find(nameID);
    if (it != std::cend(_paramBool)) {
        return it->second;
    }

    Console::errorfn(Locale::Get(_ID("ERROR_PARAM_GET")), nameID);
    return defaultValue;
}

template <>
inline void ParamHandler::setParam(const HashType nameID, bool&& value) {
    LockGuard<SharedMutex> w_lock(_mutex);
    _paramBool.insert_or_assign(nameID, MOV(value));
}

template <>
inline bool ParamHandler::isParam<bool>(const HashType nameID) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    return _paramBool.find(nameID) != std::cend(_paramBool);
}

template <>
inline void ParamHandler::delParam<bool>(HashType nameID) {
    if (isParam<bool>(nameID)) {
        LockGuard<SharedMutex> w_lock(_mutex);
        _paramBool.erase(nameID);
        if (_logState) {
            Console::printfn(Locale::Get(_ID("PARAM_REMOVE")), nameID);
        }
    } else {
        Console::errorfn(Locale::Get(_ID("ERROR_PARAM_REMOVE")), nameID);
    }
}

template <>
inline F32 ParamHandler::getParam(HashType nameID, const F32 defaultValue) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    const ParamFloatMap::const_iterator it = _paramsFloat.find(nameID);
    if (it != std::cend(_paramsFloat)) {
        return it->second;
    }

    Console::errorfn(Locale::Get(_ID("ERROR_PARAM_GET")), nameID);
    return defaultValue;
}

template <>
inline void ParamHandler::setParam(const HashType nameID, F32&& value) {
    LockGuard<SharedMutex> w_lock(_mutex);
    _paramsFloat.insert_or_assign(nameID, MOV(value));
}

template <>
inline bool ParamHandler::isParam<F32>(const HashType nameID) const {
    SharedLock<SharedMutex> r_lock(_mutex);
    return _paramsFloat.find(nameID) != std::cend(_paramsFloat);
}

template <>
inline void ParamHandler::delParam<F32>(HashType nameID) {
    if (isParam<F32>(nameID)) {
        LockGuard<SharedMutex> w_lock(_mutex);
        _paramsFloat.erase(nameID);
        if (_logState) {
            Console::printfn(Locale::Get(_ID("PARAM_REMOVE")), nameID);
        }
    } else {
        Console::errorfn(Locale::Get(_ID("ERROR_PARAM_REMOVE")), nameID);
    }
}

}  // namespace Divide

#endif  //_CORE_PARAM_HANDLER_INL_