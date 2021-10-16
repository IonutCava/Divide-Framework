#include "stdafx.h"

#include "Headers/GUIConsoleCommandParser.h"

#include "Headers/GUI.h"

#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/ParamHandler.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Math/Headers/MathHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

GUIConsoleCommandParser::GUIConsoleCommandParser(PlatformContext& context, ResourceCache* cache)
    : PlatformContextComponent(context),
      _resCache(cache),
      _sound(nullptr)
{
    _commands[_ID("say")] = [this](const string& args) { handleSayCommand(args); };
    _commands[_ID("quit")] = [this](const string& args) { handleQuitCommand(args); };
    _commands[_ID("help")] = [this](const string& args) { handleHelpCommand(args); };
    _commands[_ID("editparam")] = [this](const string& args) { handleEditParamCommand(args); };
    _commands[_ID("playsound")] = [this](const string& args) { handlePlaySoundCommand(args); };
    _commands[_ID("createnavmesh")] = [this](const string& args) { handleNavMeshCommand(args); };
    _commands[_ID("setfov")] = [this](const string& args) { handleFOVCommand(args); };
    _commands[_ID("invalidcommand")] = [this](const string& args) { handleInvalidCommand(args); };
    _commands[_ID("recompileshader")] = [this](const string& args) { handleShaderRecompileCommand(args); };

    _commandHelp[_ID("say")] = Locale::Get(_ID("CONSOLE_SAY_COMMAND_HELP"));
    _commandHelp[_ID("quit")] = Locale::Get(_ID("CONSOLE_QUIT_COMMAND_HELP"));
    _commandHelp[_ID("help")] = Locale::Get(_ID("CONSOLE_HELP_COMMAND_HELP"));
    _commandHelp[_ID("editparam")] = Locale::Get(_ID("CONSOLE_EDITPARAM_COMMAND_HELP"));
    _commandHelp[_ID("playsound")] = Locale::Get(_ID("CONSOLE_PLAYSOUND_COMMAND_HELP"));
    _commandHelp[_ID("createnavmesh")] = Locale::Get(_ID("CONSOLE_NAVMESH_COMMAND_HELP"));
    _commandHelp[_ID("recompileshader")] = Locale::Get(_ID("CONSOLE_SHADER_RECOMPILE_COMMAND_HELP"));
    _commandHelp[_ID("setfov")] = Locale::Get(_ID("CONSOLE_CHANGE_FOV_COMMAND_HELP"));
    _commandHelp[_ID("addObject")] = Locale::Get(_ID("CONSOLE_ADD_OBJECT_COMMAND_HELP"));
    _commandHelp[_ID("invalidhelp")] = Locale::Get(_ID("CONSOLE_INVALID_HELP_ARGUMENT"));
}

bool GUIConsoleCommandParser::processCommand(const string& commandString) {
    // Be sure we have a string longer than 0
    if (commandString.length() >= 1) {
        // Check if the first letter is a 'command' operator
        if (commandString.at(0) == '/') {
            const string::size_type commandEnd = commandString.find(' ', 1);
            string command = commandString.substr(1, commandEnd - 1);
            string commandArgs = commandString.substr(commandEnd + 1, commandString.length() - (commandEnd + 1));

            if (commandString != commandArgs) {
                commandArgs.clear();
            }
            // convert command to lower case
            for (auto& it : command) {
                it = static_cast<char>(tolower(it));
            }
            if (_commands.find(_ID(command.c_str())) != std::end(_commands)) {
                // we have a valid command
                _commands[_ID(command.c_str())](commandArgs);
            } else {
                // invalid command
                _commands[_ID("invalidcommand")](command);
            }
        } else {
            // no commands, just output what was typed
            Console::printfn("%s", commandString .c_str());  
        }
    }
    return true;
}

void GUIConsoleCommandParser::handleSayCommand(const string& args) {
    Console::printfn(Locale::Get(_ID("CONSOLE_SAY_NAME_TAG")), args.c_str());
}

void GUIConsoleCommandParser::handleQuitCommand(const string& args) {
    if (!args.empty()) {
        // quit command can take an extra argument. A reason, for example
        Console::printfn(Locale::Get(_ID("CONSOLE_QUIT_COMMAND_ARGUMENT")),
                         args.c_str());
    }
    _context.app().RequestShutdown();
}

void GUIConsoleCommandParser::handleHelpCommand(const string& args) {
    if (args.empty()) {
        Console::printfn(Locale::Get(_ID("HELP_CONSOLE_COMMAND")));
        for (const CommandMap::value_type& it : _commands) {
            if (it.first != _ID("invalidhelp") &&
                it.first != _ID("invalidcommand")) {
                Console::printfn("%s", _commandHelp[it.first]);
            }
        }
    } else {
        if (_commandHelp.find(_ID(args.c_str())) != std::end(_commandHelp)) {
            Console::printfn("%s", _commandHelp[_ID(args.c_str())]);
        } else {
            Console::printfn("%s", _commandHelp[_ID("invalidhelp")]);
        }
    }
}

void GUIConsoleCommandParser::handleEditParamCommand(const string& args) {
    if (context().paramHandler().isParam<string>(_ID(args.c_str()))) {
        Console::printfn(Locale::Get(_ID("CONSOLE_EDITPARAM_FOUND")), args.c_str(),
                         "N/A", "N/A", "N/A");
    } else {
        Console::printfn(Locale::Get(_ID("CONSOLE_EDITPARAM_NOT_FOUND")), args.c_str());
    }
}

void GUIConsoleCommandParser::handlePlaySoundCommand(const string& args) {
    const ResourcePath filename(Paths::g_assetsLocation + args);

    const std::ifstream soundfile(filename.str());
    if (soundfile) {
        // Check extensions (not really, musicwav.abc would still be valid, but
        // still ...)
        if (!hasExtension(filename, "wav") &&
            !hasExtension(filename, "mp3") &&
            !hasExtension(filename, "ogg")) {
            Console::errorfn(Locale::Get(_ID("CONSOLE_PLAY_SOUND_INVALID_FORMAT")));
            return;
        }

        auto[name, path] = splitPathToNameAndLocation(filename);

        // The file is valid, so create a descriptor for it
        ResourceDescriptor sound("consoleFilePlayback");
        sound.assetName(name);
        sound.assetLocation(path);
        _sound = CreateResource<AudioDescriptor>(_resCache, sound);
        if (filename.str().find("music") != string::npos) {
            // play music
            _context.sfx().playMusic(_sound);
        } else {
            // play sound but stop music first if it's playing
            _context.sfx().stopMusic();
            _context.sfx().playSound(_sound);
        }
    } else {
        Console::errorfn(Locale::Get(_ID("CONSOLE_PLAY_SOUND_INVALID_FILE")),
                         filename.c_str());
    }
}

void GUIConsoleCommandParser::handleNavMeshCommand(const string& args) {
    SceneManager* sMgr = _context.kernel().sceneManager();
    auto& sceneGraph = sMgr->getActiveScene().sceneGraph();
    if (!args.empty()) {
        SceneGraphNode* sgn = sceneGraph->findNode(args.c_str());
        if (!sgn) {
            Console::errorfn(Locale::Get(_ID("CONSOLE_NAVMESH_NO_NODE")), args.c_str());
            return;
        }
    }
    auto& aiManager = sceneGraph->parentScene().aiManager();
    // Check if we already have a NavMesh created
    AI::Navigation::NavigationMesh* temp = aiManager->getNavMesh(AI::AIEntity::PresetAgentRadius::AGENT_RADIUS_SMALL);
    // Create a new NavMesh if we don't currently have one
    if (!temp) {
        temp = MemoryManager_NEW AI::Navigation::NavigationMesh(_context, *sMgr->recast());
    }
    // Set it's file name
    temp->setFileName(_context.gui().activeScene()->resourceName());
    // Try to load it from file
    bool loaded = temp->load(sceneGraph->getRoot());
    if (!loaded) {
        // If we failed to load it from file, we need to build it first
        loaded = temp->build(
            sceneGraph->getRoot(),
            AI::Navigation::NavigationMesh::CreationCallback(), false);
        // Then save it to file
        temp->save(sceneGraph->getRoot());
    }
    // If we loaded/built the NavMesh correctly, add it to the AIManager
    if (loaded) {
        aiManager->addNavMesh(AI::AIEntity::PresetAgentRadius::AGENT_RADIUS_SMALL, temp);
    }
}

void GUIConsoleCommandParser::handleShaderRecompileCommand(const string& args) {
    ShaderProgram::RecompileShaderProgram(args.c_str());
}

void GUIConsoleCommandParser::handleFOVCommand(const string& args) {
    if (!Util::IsNumber(args)) {
        Console::errorfn(Locale::Get(_ID("CONSOLE_INVALID_NUMBER")));
        return;
    }

    const I32 FoV = CLAMPED<I32>(atoi(args.c_str()), 40, 140);

    Attorney::SceneManagerCameraAccessor::playerCamera(_context.kernel().sceneManager())->setHorizontalFoV(Angle::DEGREES<F32>(FoV));
}

void GUIConsoleCommandParser::handleInvalidCommand(const string& args) {
    Console::errorfn(Locale::Get(_ID("CONSOLE_INVALID_COMMAND")), args.c_str());
}
};