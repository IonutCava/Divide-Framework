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
#ifndef DVD_SCRIPTING_SCRIPT_H_
#define DVD_SCRIPTING_SCRIPT_H_

namespace chaiscript {
    FWD_DECLARE_MANAGED_CLASS(ChaiScript);
}

namespace Divide {

enum class FileUpdateEvent : U8;

class Script : public GUIDWrapper {
public:
    explicit Script(const string& scriptPathOrCode, FileType fileType = FileType::TEXT);
    virtual ~Script() override;

    static void idle();
    static bool OnStartup();
    static bool OnShutdown();

    template<typename T>
    void addGlobal(const T& var, const char* name, bool asConst, bool overwrite);

    template <typename T>
    void registerType(const char* typeName);

    template <typename Func>
    void registerFunction(const Func& function, const char* functionName);

    template<typename T = void>
    T eval();

    static void onScriptModify(std::string_view script, FileUpdateEvent& evt);

protected:
    void compile();
    void bootstrap();
    void extractAtoms();
    void preprocessIncludes(const std::string& source, I32 level /*= 0 */);
    void caughtException(const char* message, bool isEvalException) const;

protected:
    static void handleOutput(std::string_view msg);

protected:
    //ToDo: Move this somewhere else to avoid having the include in this file -Ionut
    chaiscript::ChaiScript_uptr _script;
    std::string  _scriptSource;
    FileNameAndPath _scriptFile;
    FileType    _scriptFileType;
    vector<ResourcePath> _usedAtoms;

    static I64 s_scriptFileWatcher;

    using ScriptMap = hashMap<I64, Script*>;
    static  ScriptMap s_scripts;
    static bool s_scriptsReady;
};

} //namespace Divide

#endif //DVD_SCRIPTING_SCRIPT_H_

#include "Script.inl"
