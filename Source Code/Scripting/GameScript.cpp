#include "stdafx.h"

#include "Headers/GameScript.h"
#include "Headers/ScriptBindings.h"

#include "Managers/Headers/FrameListenerManager.h"

namespace Divide {

GameScript::GameScript(const string& sourceCode, FrameListenerManager& parent, const U32 callOrder)
    : Script(sourceCode),
      FrameListener("Script", parent, callOrder)
{
    _script->add(create_chaiscript_bindings());
    addGameInstance();
}

GameScript::GameScript(const string& scriptPath, const FileType fileType, FrameListenerManager& parent, const U32 callOrder)
    : Script(scriptPath, fileType),
      FrameListener("Script", parent, callOrder)
{
    _script->add(create_chaiscript_bindings());
    addGameInstance();
}

void GameScript::addGameInstance() const {
    const chaiscript::ModulePtr m = chaiscript::ModulePtr(new chaiscript::Module());
    chaiscript::utility::add_class<GameScriptInstance>(*m,
        "GameScriptInstance",
        { 
            chaiscript::constructor<GameScriptInstance()>(),
            chaiscript::constructor<GameScriptInstance(const GameScriptInstance &)>()
        },
        {
            { chaiscript::fun(&GameScriptInstance::frameStarted), "frameStarted"  },
            { chaiscript::fun(&GameScriptInstance::framePreRenderStarted), "framePreRenderStarted" },
            { chaiscript::fun(&GameScriptInstance::framePreRenderEnded), "framePreRenderEnded" },
            { chaiscript::fun(&GameScriptInstance::frameRenderingQueued), "frameRenderingQueued" },
            { chaiscript::fun(&GameScriptInstance::framePostRenderStarted), "framePostRenderStarted" },
            { chaiscript::fun(&GameScriptInstance::framePostRenderEnded), "framePostRenderEnded" },
            { chaiscript::fun(&GameScriptInstance::frameEnded), "frameEnded" }
        }
    );

    _script->add(m);
}

bool GameScript::frameStarted(const FrameEvent& evt) {
    return true;
}

bool GameScript::framePreRenderStarted(const FrameEvent& evt) {
    return true;
}

bool GameScript::framePreRenderEnded(const FrameEvent& evt) {
    return true;
}

bool GameScript::frameRenderingQueued(const FrameEvent& evt) {
    return true;
}

bool GameScript::framePostRenderStarted(const FrameEvent& evt) {
    return true;
}

bool GameScript::framePostRenderEnded(const FrameEvent& evt) {
    return true;
}

bool GameScript::frameEnded(const FrameEvent& evt) {
    return true;
}

}; //namespace Divide
