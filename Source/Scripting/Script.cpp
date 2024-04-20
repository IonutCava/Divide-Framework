

#include "Headers/Script.h"
#include "Headers/ScriptBindings.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/File/Headers/FileUpdateMonitor.h"
#include "Platform/File/Headers/FileWatcherManager.h"

namespace Divide {

namespace {
    UpdateListener s_fileWatcherListener([](const std::string_view atomName, FileUpdateEvent evt) {
        Script::onScriptModify(atomName, evt);
    });
}

I64 Script::s_scriptFileWatcher = -1;

Script::ScriptMap Script::s_scripts;
bool Script::s_scriptsReady = false;

Script::Script(const string& scriptPathOrCode, const FileType fileType)
    : GUIDWrapper()
    , _script(nullptr)
    , _scriptFileType(fileType)
{
    if (!scriptPathOrCode.empty())
    {
        if (scriptPathOrCode.ends_with(".chai"))
        {
            _scriptFile = splitPathToNameAndLocation( ResourcePath{ scriptPathOrCode });
        }
        else
        {
            _scriptSource = scriptPathOrCode;
        }
    }

    compile();
    bootstrap();
    extractAtoms();

    if (s_scriptsReady)
    {
        insert(s_scripts, hashAlg::make_pair(getGUID(), this));
    }
}

Script::~Script()
{
    if (s_scriptsReady) {
      const ScriptMap::iterator it = s_scripts.find(getGUID());
        if (it != std::cend(s_scripts)) {
            s_scripts.erase(it);
        }
    }
}

void Script::idle() {
}

bool Script::OnStartup() {
    s_scripts.reserve(100);
    s_scriptsReady = true;

    if constexpr (!Config::Build::IS_SHIPPING_BUILD)
    {
        try
        {
            FileWatcher& scriptFileWatcher = FileWatcherManager::allocateWatcher();
            s_scriptFileWatcher = scriptFileWatcher.getGUID();

            s_fileWatcherListener.addIgnoredEndCharacter('~');
            s_fileWatcherListener.addIgnoredExtension("tmp");
            scriptFileWatcher().addWatch(Paths::Scripts::g_scriptsLocation.string(), &s_fileWatcherListener);
            scriptFileWatcher().addWatch(Paths::Scripts::g_scriptsAtomsLocation.string(), &s_fileWatcherListener);
        }
        catch(const std::exception& e)
        {
            Console::errorfn( LOCALE_STR( "SCRIPT_OTHER_EXCEPTION" ), e.what() );
            return false;
        }

    }

    return true;
}

bool Script::OnShutdown() {
    s_scriptsReady = false;

    if constexpr (!Config::Build::IS_SHIPPING_BUILD) {
        FileWatcherManager::deallocateWatcher(s_scriptFileWatcher);
        s_scriptFileWatcher = -1;
    }

    s_scripts.clear();

    return true;
}

void Script::compile()
{
    if (!_scriptFile._fileName.empty())
    {
        if (readFile(_scriptFile._path, _scriptFile._fileName, _scriptSource, _scriptFileType) != FileError::NONE)
        {
            NOP();
        }
    }
}

void Script::bootstrap()
{
    std::vector<std::string> scriptPath{Paths::Scripts::g_scriptsLocation.string() + Paths::g_pathSeparator,
                                        Paths::Scripts::g_scriptsAtomsLocation.string() + Paths::g_pathSeparator };

    _script =  std::make_unique<chaiscript::ChaiScript>(scriptPath,
                                                          scriptPath,
                                                          std::vector<chaiscript::Options> 
                                                          {
                                                             chaiscript::Options::Load_Modules,
                                                             chaiscript::Options::External_Scripts
                                                          });

    _script->add(create_chaiscript_stl_extra());

    _script->add(chaiscript::fun(&Script::handleOutput), "handle_output");
}

void Script::preprocessIncludes(const string& source, const I32 level /*= 0 */) {
    if (level > 32) {
        Console::errorfn(LOCALE_STR("ERROR_SCRIPT_INCLUD_LIMIT"));
    }

    string line, include_string;

    istringstream input(source);

    while ( Util::GetLine(input, line) )
    {
        if (auto m = ctre::match<Paths::g_usePattern>(line))
        {
            ResourcePath include_file = ResourcePath{ Util::Trim(m.get<1>().str()).c_str() };
            _usedAtoms.push_back(include_file);

            // Open the atom file and add the code to the atom cache for future reference
            if (readFile(Paths::Scripts::g_scriptsLocation, include_file.string(), include_string, FileType::TEXT) != FileError::NONE)
            {
                NOP();
            }
            if (include_string.empty())
            {
                if (readFile(Paths::Scripts::g_scriptsAtomsLocation, include_file.string(), include_string, FileType::TEXT) != FileError::NONE)
                {
                    NOP();
                }
            }

            if (!include_string.empty())
            {
                preprocessIncludes(include_string, level + 1);
            }
        }
    }
}

void Script::extractAtoms()
{
    _usedAtoms.clear();

    if (!_scriptFile._fileName.empty())
    {
        _usedAtoms.emplace_back(_scriptFile._fileName );
    }

    if (!_scriptSource.empty())
    {
        preprocessIncludes(_scriptSource, 0);
    }
}

void Script::handleOutput(const std::string &msg)
{
    Console::printfn(LOCALE_STR("SCRIPT_CONSOLE_OUTPUT"), msg.c_str());
}

void Script::onScriptModify(const std::string_view script, FileUpdateEvent& /*evt*/)
{
    vector<Script*> scriptsToReload;

    for (ScriptMap::value_type it : s_scripts) 
    {
        for (const ResourcePath& atom : it.second->_usedAtoms)
        {
            if (Util::CompareIgnoreCase(atom.string(), script))
            {
                scriptsToReload.push_back(it.second);
                break;
            }
        }
    }

    for (Script* it : scriptsToReload)
    {
        it->compile();
        it->extractAtoms();
    }
}

void Script::caughtException(const char* message, const bool isEvalException) const
{
    Console::printfn(Locale::Get(isEvalException ? _ID("SCRIPT_EVAL_EXCEPTION")
                                                 : _ID("SCRIPT_OTHER_EXCEPTION")),
                     message);
}

} //namespace Divide
