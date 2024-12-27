

#include "Headers/GUIConsoleCommandParser.h"

#include "Headers/GUI.h"

#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/ProjectManager.h"
#include "Rendering/Camera/Headers/Camera.h"

#include "Graphs/Headers/SceneGraph.h"
#include "AI/Headers/AIManager.h"

#include "Platform/Audio/Headers/SFXDevice.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

GUIConsoleCommandParser::GUIConsoleCommandParser(PlatformContext& context)
    : PlatformContextComponent(context)
{
    _commands[_ID("say")] = [this](const string& args) { handleSayCommand(args); };
    _commands[_ID("quit")] = [this](const string& args) { handleQuitCommand(args); };
    _commands[_ID("help")] = [this](const string& args) { handleHelpCommand(args); };
    _commands[_ID("playsound")] = [this](const string& args) { handlePlaySoundCommand(args); };
    _commands[_ID("createnavmesh")] = [this](const string& args) { handleNavMeshCommand(args); };
    _commands[_ID("setfov")] = [this](const string& args) { handleFOVCommand(args); };
    _commands[_ID("invalidcommand")] = [this](const string& args) { handleInvalidCommand(args); };
    _commands[_ID("recompileshader")] = [this](const string& args) { handleShaderRecompileCommand(args); };

    _commandHelp[_ID("say")] = LOCALE_STR("CONSOLE_SAY_COMMAND_HELP");
    _commandHelp[_ID("quit")] = LOCALE_STR("CONSOLE_QUIT_COMMAND_HELP");
    _commandHelp[_ID("help")] = LOCALE_STR("CONSOLE_HELP_COMMAND_HELP");
    _commandHelp[_ID("playsound")] = LOCALE_STR("CONSOLE_PLAYSOUND_COMMAND_HELP");
    _commandHelp[_ID("createnavmesh")] = LOCALE_STR("CONSOLE_NAVMESH_COMMAND_HELP");
    _commandHelp[_ID("recompileshader")] = LOCALE_STR("CONSOLE_SHADER_RECOMPILE_COMMAND_HELP");
    _commandHelp[_ID("setfov")] = LOCALE_STR("CONSOLE_CHANGE_FOV_COMMAND_HELP");
    _commandHelp[_ID("addObject")] = LOCALE_STR("CONSOLE_ADD_OBJECT_COMMAND_HELP");
    _commandHelp[_ID("invalidhelp")] = LOCALE_STR("CONSOLE_INVALID_HELP_ARGUMENT");
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
            Console::printfn({}, commandString .c_str());  
        }
    }
    return true;
}

void GUIConsoleCommandParser::handleSayCommand(const string& args) {
    Console::printfn(LOCALE_STR("CONSOLE_SAY_NAME_TAG"), args.c_str());
}

void GUIConsoleCommandParser::handleQuitCommand(const string& args) {
    if (!args.empty()) {
        // quit command can take an extra argument. A reason, for example
        Console::printfn(LOCALE_STR("CONSOLE_QUIT_COMMAND_ARGUMENT"),
                         args.c_str());
    }
    _context.app().RequestShutdown(false);
}

void GUIConsoleCommandParser::handleHelpCommand(const string& args) {
    if (args.empty()) {
        Console::printfn(LOCALE_STR("HELP_CONSOLE_COMMAND"));
        for (const CommandMap::value_type& it : _commands) {
            if (it.first != _ID("invalidhelp") &&
                it.first != _ID("invalidcommand")) {
                Console::printfn("{}", _commandHelp[it.first]);
            }
        }
    } else {
        if (_commandHelp.find(_ID(args.c_str())) != std::end(_commandHelp)) {
            Console::printfn("{}", _commandHelp[_ID(args.c_str())]);
        } else {
            Console::printfn("{}", _commandHelp[_ID("invalidhelp")]);
        }
    }
}

void GUIConsoleCommandParser::handlePlaySoundCommand(const string& args) {
    const ResourcePath filename(Paths::g_assetsLocation / args);

    const std::ifstream soundfile(filename.string().c_str() );
    if (soundfile) {
        // Check extensions (not really, musicwav.abc would still be valid, but
        // still ...)
        if (!hasExtension(filename, "wav") &&
            !hasExtension(filename, "mp3") &&
            !hasExtension(filename, "ogg")) {
            Console::errorfn(LOCALE_STR("CONSOLE_PLAY_SOUND_INVALID_FORMAT"));
            return;
        }

        const FileNameAndPath data = splitPathToNameAndLocation(filename);

        // The file is valid, so create a descriptor for it
        ResourceDescriptor<AudioDescriptor> sound("consoleFilePlayback");
        sound.assetName(data._fileName);
        sound.assetLocation(data._path);
        _sound = CreateResource(sound);
        if (filename.string().find("music") != string::npos) {
            // play music
            _context.sfx().playMusic(_sound);
        } else {
            // play sound but stop music first if it's playing
            _context.sfx().stopMusic();
            _context.sfx().playSound(_sound);
        }
    } else {
        Console::errorfn(LOCALE_STR("CONSOLE_PLAY_SOUND_INVALID_FILE"), filename.string());
    }
}

void GUIConsoleCommandParser::handleNavMeshCommand(const string& args) {
    ProjectManager* sMgr = _context.kernel().projectManager().get();
    auto& sceneGraph = sMgr->activeProject()->getActiveScene()->sceneGraph();
    if (!args.empty()) {
        const SceneGraphNode* sgn = sceneGraph->findNode(args.c_str());
        if (!sgn) {
            Console::errorfn(LOCALE_STR("CONSOLE_NAVMESH_NO_NODE"), args.c_str());
            return;
        }
    }
    auto& aiManager = sceneGraph->parentScene().aiManager();
    // Check if we already have a NavMesh created
    AI::Navigation::NavigationMesh* temp = aiManager->getNavMesh(AI::AIEntity::PresetAgentRadius::AGENT_RADIUS_SMALL);
    // Create a new NavMesh if we don't currently have one
    if (temp == nullptr)
    {
        temp = aiManager->addNavMesh( _context, *sMgr->recast(), sceneGraph->parentScene(), AI::AIEntity::PresetAgentRadius::AGENT_RADIUS_SMALL );
    }
    // Set it's file name
    temp->setFileName( sMgr->activeProject()->getActiveScene()->resourceName());
    // Try to load it from file
    bool loaded = temp->load(sceneGraph->getRoot());
    if (!loaded)
    {
        // If we failed to load it from file, we need to build it first
        loaded = temp->build( sceneGraph->getRoot(), AI::Navigation::NavigationMesh::CreationCallback(), false);
        // Then save it to file
        temp->save(sceneGraph->getRoot());
    }
    // If we loaded/built the NavMesh correctly, add it to the AIManager
    if (loaded)
    {
        aiManager->destroyNavMesh(AI::AIEntity::PresetAgentRadius::AGENT_RADIUS_SMALL);
    }
}

void GUIConsoleCommandParser::handleShaderRecompileCommand(const string& args) {
    ShaderProgram::RecompileShaderProgram(args.c_str());
}

void GUIConsoleCommandParser::handleFOVCommand(const string& args) {
    if (!Util::IsNumber(args)) {
        Console::errorfn(LOCALE_STR("CONSOLE_INVALID_NUMBER"));
        return;
    }

    const I32 FoV = CLAMPED<I32>(atoi(args.c_str()), 40, 140);

    Attorney::ProjectManagerCameraAccessor::playerCamera(_context.kernel().projectManager().get())->setHorizontalFoV(Angle::DEGREES_F(FoV));
}

void GUIConsoleCommandParser::handleInvalidCommand(const string& args) {
    Console::errorfn(LOCALE_STR("CONSOLE_INVALID_COMMAND"), args.c_str());
}
} //namespace Divide
