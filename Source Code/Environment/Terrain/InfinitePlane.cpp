#include "stdafx.h"

#include "Headers/InfinitePlane.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Managers/Headers/SceneManager.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Predefined/Headers/Quad3D.h"
#include "ECS/Components/Headers/TransformComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide {

InfinitePlane::InfinitePlane(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, vec2<U16> dimensions)
    : SceneNode(parentCache, descriptorHash, name, ResourcePath{ name }, {}, SceneNodeType::TYPE_INFINITEPLANE, to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS)),
      _context(context),
      _dimensions(MOV(dimensions)),
      _plane(nullptr),
      _planeRenderStateHash(0),
      _planeRenderStateHashPrePass(0)
{
    _renderState.addToDrawExclusionMask(RenderStage::SHADOW);
    _renderState.addToDrawExclusionMask(RenderStage::REFLECTION);
    _renderState.addToDrawExclusionMask(RenderStage::REFRACTION);
}

bool InfinitePlane::load() {
    if (_plane != nullptr) {
        return false;
    }

    setState(ResourceState::RES_LOADING);

    ResourceDescriptor planeMaterialDescriptor("infinitePlaneMaterial");
    Material_ptr planeMaterial = CreateResource<Material>(_parentCache, planeMaterialDescriptor);
    planeMaterial->shadingMode(ShadingMode::BLINN_PHONG);
    planeMaterial->baseColour(FColour4(DefaultColours::WHITE.rgb * 0.5f, 1.0f));
    planeMaterial->roughness(1.0f);

    SamplerDescriptor albedoSampler = {};
    albedoSampler.wrapUVW(TextureWrap::REPEAT);
    albedoSampler.minFilter(TextureFilter::LINEAR);
    albedoSampler.magFilter(TextureFilter::LINEAR);
    albedoSampler.anisotropyLevel(8);

    TextureDescriptor miscTexDescriptor(TextureType::TEXTURE_2D_ARRAY);
    miscTexDescriptor.srgb(true);

    ResourceDescriptor textureWaterCaustics("Plane Water Caustics");
    textureWaterCaustics.assetLocation(Paths::g_assetsLocation + Paths::g_imagesLocation);
    textureWaterCaustics.assetName(ResourcePath{ "terrain_water_caustics.jpg" });
    textureWaterCaustics.propertyDescriptor(miscTexDescriptor);

    planeMaterial->setTexture(TextureUsage::UNIT0, CreateResource<Texture>(_parentCache, textureWaterCaustics), albedoSampler.getHash(), TextureOperation::REPLACE);

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "terrainPlane.glsl";
    vertModule._variant = "Colour";
    vertModule._defines.emplace_back("UNDERWATER_TILE_SCALE 100", true);
    vertModule._defines.emplace_back("NODE_STATIC", true);

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "terrainPlane.glsl";
    fragModule._variant = "Colour";
    fragModule._defines.emplace_back("NODE_STATIC", true);

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    ResourceDescriptor terrainShader("terrainPlane_Colour");
    terrainShader.propertyDescriptor(shaderDescriptor);
    ShaderProgram_ptr terrainColourShader = CreateResource<ShaderProgram>(_parentCache, terrainShader);

    planeMaterial->setShaderProgram(terrainColourShader, RenderStage::COUNT, RenderPassType::COUNT);

    shaderDescriptor._modules.back()._defines.emplace_back("MAIN_DISPLAY_PASS", true);
    ResourceDescriptor terrainShaderMain("terrainPlane_Colour_Main");
    terrainShaderMain.propertyDescriptor(shaderDescriptor);
    ShaderProgram_ptr terrainColourShaderMain = CreateResource<ShaderProgram>(_parentCache, terrainShaderMain);

    planeMaterial->setShaderProgram(terrainColourShaderMain, RenderStage::DISPLAY, RenderPassType::MAIN_PASS);

    vertModule._variant = "PrePass";
    shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);

    ResourceDescriptor terrainShaderPrePass("terrainPlane_PrePass");
    terrainShaderPrePass.propertyDescriptor(shaderDescriptor);
    ShaderProgram_ptr terrainPrePassShader = CreateResource<ShaderProgram>(_parentCache, terrainShaderPrePass);
    planeMaterial->setShaderProgram(terrainPrePassShader, RenderStage::COUNT, RenderPassType::PRE_PASS);

    setMaterialTpl(planeMaterial);

    ResourceDescriptor infinitePlane("infinitePlane");
    infinitePlane.flag(true);  // No default material
    infinitePlane.threaded(false);
    infinitePlane.data().set(Util::PACK_HALF1x16(_dimensions.x * 1.5f) * 2u, 0u, Util::PACK_HALF1x16(_dimensions.y * 1.5f) * 2u);

    _plane = CreateResource<Quad3D>(_parentCache, infinitePlane);
    _boundingBox.set(vec3<F32>(-_dimensions.x * 1.5f, -0.5f, -_dimensions.y * 1.5f),
                     vec3<F32>( _dimensions.x * 1.5f,  0.5f,  _dimensions.y * 1.5f));

    return SceneNode::load();
}

void InfinitePlane::postLoad(SceneGraphNode* sgn) {
    assert(_plane != nullptr);

    RenderingComponent* renderable = sgn->get<RenderingComponent>();
    if (renderable) {
        renderable->toggleRenderOption(RenderingComponent::RenderOptions::CAST_SHADOWS, false);
    }

    SceneNode::postLoad(sgn);
}

void InfinitePlane::sceneUpdate(const U64 deltaTimeUS, SceneGraphNode* sgn, SceneState& sceneState) {
    OPTICK_EVENT();

    TransformComponent* tComp = sgn->get<TransformComponent>();

    const vec3<F32>& newEye = sceneState.parentScene().playerCamera()->getEye();

    tComp->setPosition(newEye.x, tComp->getPosition().y, newEye.z);
}

void InfinitePlane::buildDrawCommands(SceneGraphNode* sgn,
                                      const RenderStagePass& renderStagePass,
                                      const Camera& crtCamera,
                                      RenderPackage& pkgInOut) {

    //infinite plane
    GenericDrawCommand planeCmd = {};
    planeCmd._primitiveType = PrimitiveType::TRIANGLE_STRIP;
    planeCmd._cmd.firstIndex = 0u;
    planeCmd._cmd.indexCount = to_U32(_plane->getGeometryVB()->getIndexCount());
    planeCmd._sourceBuffer = _plane->getGeometryVB()->handle();
    planeCmd._bufferIndex = renderStagePass.baseIndex();
    {
        pkgInOut.add(GFX::DrawCommand{ planeCmd });
    }

    SceneNode::buildDrawCommands(sgn, renderStagePass, crtCamera, pkgInOut);
}

} //namespace Divide