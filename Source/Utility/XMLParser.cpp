

#include "Headers/XMLParser.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/StringHelper.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/ProjectManager.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Scenes/Headers/SceneInput.h"
#include "Utility/Headers/Localization.h"
#include <boost/property_tree/xml_parser.hpp>

namespace Divide::XML {

using boost::property_tree::ptree;

bool loadFromXML(IXMLSerializable& object, const ResourcePath& filePath, const char* fileName )
{
    return object.fromXML(filePath, fileName);
}

bool saveToXML(const IXMLSerializable& object, const ResourcePath& filePath, const char* fileName )
{
    return object.toXML(filePath, fileName);
}

namespace
{
    ptree g_emptyPtree;
}

namespace detail
{
    bool LoadSave::read(const ResourcePath& filePath, const char* fileName, const string& rootNode)
    {
        _filePath = filePath;
        _fileName = fileName;
        _rootNodePath = rootNode;

        const ResourcePath loadPath = filePath / fileName;

        const ResourcePath testPath(loadPath);

        if (!fileExists(testPath) || fileIsEmpty(testPath))
        {
            const FileNameAndPath data = splitPathToNameAndLocation(testPath);
            const FileError backupReturnCode = copyFile( data._path, (data._fileName + ".bak"), data._path, data._fileName, true);
            if (backupReturnCode != FileError::NONE &&
                backupReturnCode != FileError::FILE_NOT_FOUND &&
                backupReturnCode != FileError::FILE_EMPTY)
            {
                if constexpr(!Config::Build::IS_SHIPPING_BUILD)
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
        }

        try
        {
            read_xml(loadPath.string(),
                     XmlTree,
                     boost::property_tree::xml_parser::trim_whitespace);

            return !XmlTree.empty();
        }
        catch ( const boost::property_tree::xml_parser_error& error )
        {
            Console::errorfn( error.what() );
        }
        catch(...)
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        return false;
    }

    bool LoadSave::write( const ResourcePath& filePath, const char* fileName ) const
    {
        const ResourcePath savePath = filePath / fileName;

        if (fileExists(savePath))
        {
            const auto[file, path] = splitPathToNameAndLocation(savePath);

            const FileError backupReturnCode = copyFile(path, file, path, (file + ".bak"), true);
            if (backupReturnCode != FileError::NONE &&
                backupReturnCode != FileError::FILE_NOT_FOUND &&
                backupReturnCode != FileError::FILE_EMPTY)
            {
                    DIVIDE_UNEXPECTED_CALL();
            }
        }
        else if (!createFile(savePath, true))
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        try
        {
            write_xml(savePath.string(),
                      XmlTree,
                      std::locale(),
                      boost::property_tree::xml_writer_make_settings<boost::property_tree::iptree::key_type>('\t', 1));

            return true;
        }
        catch(const boost::property_tree::xml_parser_error& error)
        {
            Console::errorfn(error.what());
        }
        catch(...)
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        return false;
    }
}

static void PopulatePressRelease(const ptree & attributes, PressReleaseActions::Entry& entryOut)
{
    static vector<std::string> modifiersOut, actionsUpOut, actionsDownOut;

    entryOut.clear();
    modifiersOut.resize(0);
    actionsUpOut.resize(0);
    actionsDownOut.resize(0);

    U16 id = 0;

    const std::string modifiers = attributes.get<std::string>("modifier", "");
    if (!modifiers.empty()) {
        Util::Split<vector<std::string>, std::string>(modifiers.c_str(), ',', modifiersOut);
        for (const auto& it : modifiersOut) {
            for (U8 i = 0; i < to_base(PressReleaseActions::Modifier::COUNT); ++i) {
                if (it == PressReleaseActions::s_modifierNames[i]) {
                    entryOut.modifiers().insert(PressReleaseActions::s_modifierMappings[i]);
                    break;
                }
            }
        }
    }

    const std::string actionsUp = attributes.get<std::string>("actionUp", "");
    if (!actionsUp.empty()) {
        Util::Split<vector<std::string>, std::string>(actionsUp.c_str(), ',', actionsUpOut);
        for (const auto& it : actionsUpOut) {
            if (!it.empty()) {
                std::stringstream ss(Util::Trim(it));
                ss >> id;
                if (!ss.fail()) {
                    entryOut.releaseIDs().insert(id);
                }
            }
        }
    }

    const std::string actionsDown = attributes.get<std::string>("actionDown", "");
    if (!actionsDown.empty()) {
        Util::Split<vector<std::string>, std::string>(actionsDown.c_str(), ',', actionsDownOut);
        for (const auto& it : actionsDownOut) {
            if (!it.empty()) {
                std::stringstream ss(Util::Trim(it));
                ss >> id;
                if (!ss.fail()) {
                    entryOut.pressIDs().insert(id);
                }
            }
        }
    }
}

void loadDefaultKeyBindings(const ResourcePath& file, const Scene* scene) {
    ptree pt;
    Console::printfn(LOCALE_STR("XML_LOAD_DEFAULT_KEY_BINDINGS"), file);
    read_xml(file.string(), pt);

    for(const auto & [tag, data] : pt.get_child("actions", g_emptyPtree))
    {
        const ptree & attributes = data.get_child("<xmlattr>", g_emptyPtree);
        scene->input()->actionList().getInputAction(attributes.get<U16>("id", 0)).displayName(attributes.get<string>("name", "").c_str());
    }

    PressReleaseActions::Entry entry = {};
    for (const auto & [tag, data] : pt.get_child("keys", g_emptyPtree))
    {
        if (tag == "<xmlcomment>") {
            continue;
        }

        const ptree & attributes = data.get_child("<xmlattr>", g_emptyPtree);
        PopulatePressRelease(attributes, entry);

        const Input::KeyCode key = Input::KeyCodeByName(Util::Trim(data.data()).c_str());
        scene->input()->addKeyMapping(key, entry);
    }

    for (const auto & [tag, data] : pt.get_child("mouseButtons", g_emptyPtree))
    {
        if (tag == "<xmlcomment>") {
            continue;
        }

        const ptree & attributes = data.get_child("<xmlattr>", g_emptyPtree);
        PopulatePressRelease(attributes, entry);

        const Input::MouseButton btn = Input::mouseButtonByName(Util::Trim(data.data()));

        scene->input()->addMouseMapping(btn, entry);
    }

    const string label("joystickButtons.joystick");
    for (U32 i = 0 ; i < to_base(Input::Joystick::COUNT); ++i) {
        const Input::Joystick joystick = static_cast<Input::Joystick>(i);
        
        for (const auto & [tag, value] : pt.get_child(label + std::to_string(i + 1), g_emptyPtree))
        {
            if (tag == "<xmlcomment>") {
                continue;
            }

            const ptree & attributes = value.get_child("<xmlattr>", g_emptyPtree);
            PopulatePressRelease(attributes, entry);

            const Input::JoystickElement element = Input::joystickElementByName(Util::Trim(value.data()));

            scene->input()->addJoystickMapping(joystick, element._type, element._elementIndex, entry);
        }
    }
}

void loadMusicPlaylist(const ResourcePath& scenePath, const Str<64>& fileName, const Scene* const scene, [[maybe_unused]] const Configuration& config)
{
    const ResourcePath file = scenePath / fileName;

    if (!fileExists(file))
    {
        return;
    }

    Console::printfn(LOCALE_STR("XML_LOAD_MUSIC"), file);

    ptree pt;
    read_xml(file.string(), pt);

    for (const auto & [tag, data] : pt.get_child("backgroundThemes", g_emptyPtree))
    {
        const ptree & attributes = data.get_child("<xmlattr>", g_emptyPtree);
        scene->addMusic(MusicType::TYPE_BACKGROUND,
                        attributes.get<string>("name", "").c_str(),
                        Paths::g_assetsLocation / attributes.get<string>("src", "") );
    }
}

void writeXML(const ResourcePath& path, const ptree& tree)
{
    static boost::property_tree::xml_writer_settings<std::string> settings(' ', 4);

    write_xml(path.string(), tree, std::locale(), settings);
}

void readXML(const ResourcePath& path, ptree& tree)
{
    try
    {
        read_xml(path.string(), tree);
    }
    catch (const boost::property_tree::xml_parser_error& e)
    {
        Console::errorfn(LOCALE_STR("ERROR_XML_INVALID_FILE"), path, e.what());
    }
}
}  // namespace Divide::XML
