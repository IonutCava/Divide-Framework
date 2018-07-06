/*
Copyright (c) 2017 DIVIDE-Studio
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

#ifndef _SCRIPTING_SCRIPT_INL_
#define _SCRIPTING_SCRIPT_INL_

#include <chaiscript/chaiscript.hpp>

namespace Divide {

template<typename T>
inline void Script::addGlobal(const T& var, const char* name, bool asConst, bool overwrite) {
    if (overwrite) {
        if (asConst) {
            _script->set_global_const(chaiscript::const_var(var), name);
        }
        else {
            _script->set_global(chaiscript::var(var), name); // global non-const, overwrites existing object
        }
    }
    else {
        if (asConst) {
            _script->add(chaiscript::const_var(var), name); // copied in and made const
        }
        else {
            _script->add(chaiscript::var(var), name); // copied in
        }
    }
}

template <typename T>
inline void Script::registerType(const char* typeName) {
    _script->add(chaiscript::user_type<T>(), typeName);
}

template <typename Func >
inline void Script::registerFunction(const Func& function, const char* functionName) {
    _script->add(chaiscript::fun(function), functionName);
}

template<typename T>
inline T Script::eval() {
    assert(!_scriptSource.empty());
    return _script->eval<T>(_scriptSource);
}

template<>
inline void Script::eval() {
    assert(!_scriptSource.empty());
    _script->eval(_scriptSource);
}

}; //namespace Divide

#endif //_SCRIPTING_SCRIPT_INL_