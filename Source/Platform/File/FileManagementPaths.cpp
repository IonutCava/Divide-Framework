
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Headers/FileManagement.h"

namespace Divide {

ResourcePath Paths::g_logPath;

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
ResourcePath Paths::g_projectsLocation;

ResourcePath Paths::g_saveLocation;
ResourcePath Paths::g_nodesSaveLocation;
ResourcePath Paths::g_imagesLocation;
ResourcePath Paths::g_materialsLocation;
ResourcePath Paths::g_navMeshesLocation;
ResourcePath Paths::g_GUILocation;
ResourcePath Paths::g_fontsPath;
ResourcePath Paths::g_iconsPath;
ResourcePath Paths::g_soundsLocation;
ResourcePath Paths::g_localisationPath;
ResourcePath Paths::g_cacheLocation;
ResourcePath Paths::g_buildTypeLocation;
ResourcePath Paths::g_terrainCacheLocation;
ResourcePath Paths::g_geometryCacheLocation;
ResourcePath Paths::g_collisionMeshCacheLocation;

ResourcePath Paths::Editor::g_saveLocation;

ResourcePath Paths::Scripts::g_scriptsLocation;
ResourcePath Paths::Scripts::g_scriptsAtomsLocation;

ResourcePath Paths::Textures::g_metadataLocation;
Str<8> Paths::Textures::g_ddsExtension;

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
Str<8> Paths::Shaders::GLSL::g_meshAtomExt;
Str<8> Paths::Shaders::GLSL::g_taskAtomExt;
Str<8> Paths::Shaders::GLSL::g_comnAtomExt;

ResourcePath Paths::Shaders::GLSL::g_GLSLShaderLoc;
// Atom folder names in parent shader folder
ResourcePath Paths::Shaders::GLSL::g_fragAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_vertAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_geomAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_tescAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_teseAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_compAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_meshAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_taskAtomLoc;
ResourcePath Paths::Shaders::GLSL::g_comnAtomLoc;

void Paths::initPaths() 
{
    g_logPath          = ResourcePath( "Logs" );
    g_xmlDataLocation  = ResourcePath( "XML" );
    g_screenshotPath   = ResourcePath( "Screenshots" );
    g_projectsLocation = ResourcePath( "Projects" );
    g_saveLocation     = ResourcePath( "SaveData" );
    g_assetsLocation   = ResourcePath( "Assets" );
    g_localisationPath = ResourcePath( "Localisation" );
    g_cacheLocation    = ResourcePath( "Cache" );

    g_terrainCacheLocation       = g_cacheLocation / "Terrain";
    g_geometryCacheLocation      = g_cacheLocation / "Geometry";
    g_collisionMeshCacheLocation = g_cacheLocation / "CollisionMeshes";

    g_modelsLocation    = g_assetsLocation / "Models";
    g_shadersLocation   = g_assetsLocation / "Shaders";
    g_imagesLocation    = g_assetsLocation / "MiscImages";
    g_materialsLocation = g_assetsLocation / "Materials";
    g_GUILocation       = g_assetsLocation / "GUI";
    g_iconsPath         = g_assetsLocation / "Icons";
    g_soundsLocation    = g_assetsLocation / "Sounds";
    g_texturesLocation  = g_assetsLocation / "Textures";
    g_heightmapLocation = g_assetsLocation / "Terrain";

    g_fontsPath = g_GUILocation / "Fonts";

    g_proceduralTexturesLocation = g_texturesLocation / "ProcTextures";

    g_climatesLowResLocation  = g_heightmapLocation / "Climates_05k";
    g_climatesMedResLocation  = g_heightmapLocation / "Climates_1k";
    g_climatesHighResLocation = g_heightmapLocation / "Climates_4k";

    // Project dependent
    g_scenesLocation = ResourcePath( "Scenes" );

    // Scene dependent
    g_navMeshesLocation = ResourcePath("NavMeshes");
    g_nodesSaveLocation = ResourcePath("nodes");

    if constexpr(Config::Build::IS_DEBUG_BUILD)
    {
        g_buildTypeLocation = ResourcePath("Debug");
    }
    else if constexpr(Config::Build::IS_PROFILE_BUILD)
    {
        g_buildTypeLocation = ResourcePath("Profile");
    }
    else
    {
        g_buildTypeLocation = ResourcePath("Release");
    }

    Scripts::g_scriptsLocation      = g_assetsLocation / "Scripts";
    Scripts::g_scriptsAtomsLocation = Scripts::g_scriptsLocation / "Atoms";

    Editor::g_saveLocation        = ResourcePath("Editor");

    Textures::g_metadataLocation = g_cacheLocation / "TextureData";
    Textures::g_ddsExtension = "dds";

    Shaders::g_cacheLocation     = g_cacheLocation / "Shaders";

    Shaders::g_cacheLocationGL   = ResourcePath("OpenGL");
    Shaders::g_cacheLocationVK   = ResourcePath("Vulkan");
    Shaders::g_cacheLocationText = ResourcePath("Text");
    Shaders::g_cacheLocationSpv  = ResourcePath("SPV");
    Shaders::g_cacheLocationRefl = ResourcePath("Refl");

    Shaders::g_ReflectionExt = "refl";
    Shaders::g_SPIRVExt = "spv";
    Shaders::g_SPIRVShaderLoc = ResourcePath("SPIRV");

    // these must match the last 4 characters of the atom file
    Shaders::GLSL::g_fragAtomExt = "frag";
    Shaders::GLSL::g_vertAtomExt = "vert";
    Shaders::GLSL::g_geomAtomExt = "geom";
    Shaders::GLSL::g_tescAtomExt = "tesc";
    Shaders::GLSL::g_teseAtomExt = "tese";
    Shaders::GLSL::g_compAtomExt = "comp";
    Shaders::GLSL::g_meshAtomExt = "mesh";
    Shaders::GLSL::g_taskAtomExt = "task";
    Shaders::GLSL::g_comnAtomExt = "cmn";

    Shaders::GLSL::g_GLSLShaderLoc = g_shadersLocation / "GLSL";
    // Atom folder names in parent shader folder
    Shaders::GLSL::g_fragAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "FragmentAtoms";
    Shaders::GLSL::g_vertAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "VertexAtoms";
    Shaders::GLSL::g_geomAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "GeometryAtoms";
    Shaders::GLSL::g_tescAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "TessellationCAtoms";
    Shaders::GLSL::g_teseAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "TessellationEAtoms";
    Shaders::GLSL::g_compAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "ComputeAtoms";
    Shaders::GLSL::g_meshAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "MeshAtoms";
    Shaders::GLSL::g_taskAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "TaskAtoms";
    Shaders::GLSL::g_comnAtomLoc = Shaders::GLSL::g_GLSLShaderLoc / "Common";
}


} //namespace Divide
