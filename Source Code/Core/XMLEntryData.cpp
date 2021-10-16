#include "stdafx.h"

#include "Headers/XMLEntryData.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

XMLEntryData::XMLEntryData() : IXMLSerializable()
{
    scriptLocation = "XML";
    config = "config.xml";
    startupScene = Config::DEFAULT_SCENE_NAME;
    scenesLocation = "Scenes";
    assetsLocation = "assets";
    serverAddress = "192.168.0.2";
}

bool XMLEntryData::fromXML(const char* xmlFile) {
    Console::printfn(Locale::Get(_ID("XML_LOAD_SCRIPTS")));

    if (LoadSave.read(xmlFile, "")) {
        GET_PARAM(scriptLocation);
        GET_PARAM(config);
        GET_PARAM(startupScene);
        GET_PARAM(scenesLocation);
        GET_PARAM(assetsLocation);
        GET_PARAM(serverAddress);
        return true;
    }

    return false;
}

bool XMLEntryData::toXML(const char* xmlFile) const {
    if (LoadSave.prepareSaveFile(xmlFile)) {
        PUT_PARAM(scriptLocation);
        PUT_PARAM(config);
        PUT_PARAM(startupScene);
        PUT_PARAM(scenesLocation);
        PUT_PARAM(assetsLocation);
        PUT_PARAM(serverAddress);
        LoadSave.write();
        return true;
    }

    return false;
}

}; //namespace Divide
