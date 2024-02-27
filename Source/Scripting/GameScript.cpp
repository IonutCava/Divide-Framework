

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
            { chaiscript::fun(&GameScriptInstance::framePreRender), "framePreRender" },
            { chaiscript::fun(&GameScriptInstance::frameRenderingQueued), "frameRenderingQueued" },
            { chaiscript::fun(&GameScriptInstance::framePostRender), "framePostRender" },
            { chaiscript::fun(&GameScriptInstance::frameEnded), "frameEnded" }
        }
    );

    _script->add(m);
}

bool GameScript::frameStarted([[maybe_unused]] const FrameEvent& evt) {
    return true;
}

bool GameScript::framePreRender([[maybe_unused]] const FrameEvent& evt) {
    return true;
}

bool GameScript::frameRenderingQueued([[maybe_unused]] const FrameEvent& evt) {
    return true;
}

bool GameScript::framePostRender([[maybe_unused]] const FrameEvent& evt) {
    return true;
}


bool GameScript::frameEnded([[maybe_unused]] const FrameEvent& evt) {
    return true;
}

}; //namespace Divide
