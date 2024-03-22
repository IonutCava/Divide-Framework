

#include "Core/Headers/XMLEntryData.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Headers/FileManagement.h"

namespace Divide {

ResourcePath Paths::g_logPath = ResourcePath("logs/");

ResourcePath Paths::g_rootPath;
ResourcePath Paths::g_screenshotPath;
ResourcePath Paths::g_assetsLocation;
ResourcePath Paths::g_modelsLocation;
ResourcePath Paths::g_shadersLocation;
ResourcePath Paths::g_texturesLocation;
ResourcePath Paths::g_proceduralTexturesLocation;
ResourcePath Paths::g_heightmapLocation;
ResourcePath Paths::g_climatesLowResLocation;
ResourcePath Paths::g_climatesMedResLocation;
ResourcePath Paths::g_climatesHighResLocation;
ResourcePath Paths::g_xmlDataLocation;
ResourcePath Paths::g_scenesLocation;

ResourcePath Paths::g_saveLocation;
ResourcePath Paths::g_imagesLocation;
ResourcePath Paths::g_materialsLocation;
ResourcePath Paths::g_navMeshesLocation;
ResourcePath Paths::g_GUILocation;
ResourcePath Paths::g_fontsPath;
ResourcePath Paths::g_soundsLocation;
ResourcePath Paths::g_localisationPath;
ResourcePath Paths::g_cacheLocation;
ResourcePath Paths::g_buildTypeLocation;
ResourcePath Paths::g_terrainCacheLocation;
ResourcePath Paths::g_geometryCacheLocation;
ResourcePath Paths::g_collisionMeshCacheLocation;

ResourcePath Paths::Editor::g_saveLocation;
ResourcePath Paths::Editor::g_tabLayoutFile;
ResourcePath Paths::Editor::g_panelLayoutFile;

ResourcePath Paths::Scripts::g_scriptsLocation;
ResourcePath Paths::Scripts::g_scriptsAtomsLocation;

ResourcePath Paths::Textures::g_metadataLocation;

ResourcePath Paths::Shaders::g_cacheLocation;
ResourcePath Paths::Shaders::g_cacheLocationGL;
ResourcePath Paths::Shaders::g_cacheLocationVK;
ResourcePath Paths::Shaders::g_cacheLocationText;
ResourcePath Paths::Shaders::g_cacheLocationSpv;
ResourcePath Paths::Shaders::g_cacheLocationRefl;

Str<8> Paths::Shaders::g_ReflectionExt;
Str<8> Paths::Shaders::g_SPIRVExt;
ResourcePath Paths::Shaders::g_SPIRVShaderLoc;

// these must match the last 4 characters of the atom file
Str<8> Paths::Shaders::GLSL::g_fragAtomExt;
Str<8> Paths::Shaders::GLSL::g_vertAtomExt;
Str<8> Paths::Shaders::GLSL::g_geomAtomExt;
Str<8> Paths::Shaders::GLSL::g_tescAtomExt;
Str<8> Paths::Shaders::GLSL::g_teseAtomExt;
Str<8> Paths::Shaders::GLSL::g_compAtomExt;
Str<8> Paths::Shaders::GLSL::g_comnAtomExt;

ResourcePath Paths::Shaders::GLSL::g_GLSLShaderLoc;
// Atom folder names in parent shader folder
ResourcePath Paths::Shaders::GLSL::g_fragAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_vertAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_geomAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_tescAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_teseAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_compAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_comnAtomLoc;

void Paths::initPaths(const SysInfo& info) 
{
    g_rootPath = ResourcePath(info._workingDirectory);
    g_logPath = ResourcePath("Logs/");
    g_xmlDataLocation = ResourcePath("XML/");
    g_screenshotPath = ResourcePath("Screenshots/");
    g_assetsLocation = ResourcePath("Assets/");
    g_modelsLocation = ResourcePath("Models/");
    g_shadersLocation = ResourcePath("Shaders/");
    g_texturesLocation = ResourcePath("Textures/");
    g_proceduralTexturesLocation = ResourcePath("ProcTextures/");
    g_heightmapLocation = ResourcePath("Terrain/");
    g_climatesLowResLocation = ResourcePath("Climates_05k/");
    g_climatesMedResLocation = ResourcePath("Climates_1k/");
    g_climatesHighResLocation = ResourcePath("Climates_4k/");
    g_scenesLocation = ResourcePath("Scenes/");

    g_saveLocation = ResourcePath("SaveData/");
    g_imagesLocation = ResourcePath("MiscImages/");
    g_materialsLocation = ResourcePath("Materials/");
    g_navMeshesLocation = ResourcePath("NavMeshes/");
    g_GUILocation = ResourcePath("GUI/");
    g_fontsPath = ResourcePath("Fonts/");
    g_soundsLocation = ResourcePath("Sounds/");
    g_localisationPath = ResourcePath("Localisation/");
    g_cacheLocation = ResourcePath("Cache/");
    if constexpr(Config::Build::IS_DEBUG_BUILD) {
        g_buildTypeLocation = ResourcePath("Debug/");
    } else if constexpr(Config::Build::IS_PROFILE_BUILD) {
        g_buildTypeLocation = ResourcePath("Profile/");
    } else {
        g_buildTypeLocation = ResourcePath("Release/");
    }
    g_terrainCacheLocation = ResourcePath("Terrain/");
    g_geometryCacheLocation = ResourcePath("Geometry/");
    g_collisionMeshCacheLocation = ResourcePath("CollisionMeshes/");

    Editor::g_saveLocation = ResourcePath("Editor/");
    Editor::g_tabLayoutFile = ResourcePath("Tabs.layout");
    Editor::g_panelLayoutFile = ResourcePath("Panels.layout");

    Scripts::g_scriptsLocation = g_assetsLocation + "Scripts/";
    Scripts::g_scriptsAtomsLocation = Scripts::g_scriptsLocation + "Atoms/";

    Textures::g_metadataLocation = ResourcePath("TextureData/");

    Shaders::g_cacheLocation = ResourcePath("Shaders/");
    Shaders::g_cacheLocationGL = ResourcePath("OpenGL/");
    Shaders::g_cacheLocationVK = ResourcePath("Vulkan/");
    Shaders::g_cacheLocationText = ResourcePath("Text/");
    Shaders::g_cacheLocationSpv = ResourcePath("SPV/");
    Shaders::g_cacheLocationRefl = ResourcePath("Refl/");

    Shaders::g_ReflectionExt = "refl";
    Shaders::g_SPIRVExt = "spv";
    Shaders::g_SPIRVShaderLoc = ResourcePath("SPIRV/");

    // these must match the last 4 characters of the atom file
    Shaders::GLSL::g_fragAtomExt = "frag";
    Shaders::GLSL::g_vertAtomExt = "vert";
    Shaders::GLSL::g_geomAtomExt = "geom";
    Shaders::GLSL::g_tescAtomExt = "tesc";
    Shaders::GLSL::g_teseAtomExt = "tese";
    Shaders::GLSL::g_compAtomExt = "comp";
    Shaders::GLSL::g_comnAtomExt = "cmn";

    Shaders::GLSL::g_GLSLShaderLoc = ResourcePath("GLSL/");
    // Atom folder names in parent shader folder
    Shaders::GLSL::g_fragAtomLoc = ResourcePath("FragmentAtoms/");
    Shaders::GLSL::g_vertAtomLoc = ResourcePath("VertexAtoms/");
    Shaders::GLSL::g_geomAtomLoc = ResourcePath("GeometryAtoms/");
    Shaders::GLSL::g_tescAtomLoc = ResourcePath("TessellationCAtoms/");
    Shaders::GLSL::g_teseAtomLoc = ResourcePath("TessellationEAtoms/");
    Shaders::GLSL::g_compAtomLoc = ResourcePath("ComputeAtoms/");
    Shaders::GLSL::g_comnAtomLoc = ResourcePath("Common/");
}

void Paths::updatePaths(const PlatformContext& context) {
    const Configuration& config = context.config();
    const XMLEntryData& entryData = context.entryData();
        
    g_assetsLocation = ResourcePath(entryData.assetsLocation + "/");
    g_shadersLocation = ResourcePath(config.defaultAssetLocation.shaders + "/");
    g_texturesLocation = ResourcePath(config.defaultAssetLocation.textures + "/");
    g_scenesLocation = ResourcePath(entryData.scenesLocation + "/");
    Scripts::g_scriptsLocation = g_assetsLocation + "Scripts/";
    Scripts::g_scriptsAtomsLocation = Scripts::g_scriptsLocation + "Atoms/";
}

}; //namespace Divide