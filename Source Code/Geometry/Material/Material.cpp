#include "stdafx.h"

#include "Headers/Material.h"
#include "Headers/ShaderComputeQueue.h"

#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Utility/Headers/Localization.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Editor/Headers/Editor.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"

namespace Divide {

namespace {
    constexpr size_t g_materialXMLVersion = 1;

    constexpr size_t g_invalidStateHash = std::numeric_limits<size_t>::max();
};


namespace TypeUtil {
    const char* MaterialDebugFlagToString(const MaterialDebugFlag materialFlag) noexcept {
        return Names::materialDebugFlag[to_base(materialFlag)];
    }

    MaterialDebugFlag StringToMaterialDebugFlag(const string& name) {
        for (U8 i = 0; i < to_U8(MaterialDebugFlag::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::materialDebugFlag[i]) == 0) {
                return static_cast<MaterialDebugFlag>(i);
            }
        }

        return MaterialDebugFlag::COUNT;
    } 
    
    const char* TextureUsageToString(const TextureUsage texUsage) noexcept {
        return Names::textureUsage[to_base(texUsage)];
    }

    TextureUsage StringToTextureUsage(const string& name) {
        for (U8 i = 0; i < to_U8(TextureUsage::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::textureUsage[i]) == 0) {
                return static_cast<TextureUsage>(i);
            }
        }

        return TextureUsage::COUNT;
    }

    const char* BumpMethodToString(const BumpMethod bumpMethod) noexcept {
        return Names::bumpMethod[to_base(bumpMethod)];
    }

    BumpMethod StringToBumpMethod(const string& name) {
        for (U8 i = 0; i < to_U8(BumpMethod::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::bumpMethod[i]) == 0) {
                return static_cast<BumpMethod>(i);
            }
        }

        return BumpMethod::COUNT;
    }

    const char* ShadingModeToString(const ShadingMode shadingMode) noexcept {
        return Names::shadingMode[to_base(shadingMode)];
    }

    ShadingMode StringToShadingMode(const string& name) {
        for (U8 i = 0; i < to_U8(ShadingMode::COUNT); ++i) {
            if (strcmp(name.c_str(), Names::shadingMode[i]) == 0) {
                return static_cast<ShadingMode>(i);
            }
        }

        return ShadingMode::COUNT;
    }

    const char* TextureOperationToString(const TextureOperation textureOp) noexcept {
        return Names::textureOperation[to_base(textureOp)];
    }

    TextureOperation StringToTextureOperation(const string& operation) {
        for (U8 i = 0; i < to_U8(TextureOperation::COUNT); ++i) {
            if (strcmp(operation.c_str(), Names::textureOperation[i]) == 0) {
                return static_cast<TextureOperation>(i);
            }
        }

        return TextureOperation::COUNT;
    }
}; //namespace TypeUtil

bool Material::s_shadersDirty = false;

void Material::OnStartup() {
    NOP();
}

void Material::OnShutdown() {
    s_shadersDirty = false;
}

void Material::RecomputeShaders() {
    s_shadersDirty = true;
}

void Material::Update([[maybe_unused]] const U64 deltaTimeUS) {
    s_shadersDirty = false;
}

Material::Material(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name)
    : CachedResource(ResourceType::DEFAULT, descriptorHash, name),
      _context(context),
      _parentCache(parentCache)
{
    properties().receivesShadows(_context.context().config().rendering.shadowMapping.enabled);

    const ShaderProgramInfo defaultShaderInfo = {};
    // Could just direct copy the arrays, but this looks cool
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        auto& perPassInfo = _shaderInfo[s];
        auto& perPassStates = _defaultRenderStates[s];

        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            perPassInfo[p].fill(defaultShaderInfo);
            perPassStates[p].fill(g_invalidStateHash);
        }
    }

    _computeRenderStateCBK = [](Material* material, const RenderStagePass renderStagePass) {
        RenderStateBlock stateDescriptor = {};
        stateDescriptor.setCullMode(material->properties().doubleSided() ? CullMode::NONE : CullMode::BACK);

        const bool isColourPass = !IsDepthPass(renderStagePass);
        const bool isZPrePass = IsZPrePass(renderStagePass);
        const bool isShadowPass = IsShadowPass(renderStagePass);
        const bool isDepthPass = !isColourPass && !isZPrePass && !isShadowPass;

        stateDescriptor.setZFunc(isColourPass ? ComparisonFunction::EQUAL : ComparisonFunction::LEQUAL);
        if (isShadowPass) {
            stateDescriptor.setColourWrites(true, true, false, false);
            //stateDescriptor.setZBias(1.1f, 4.f);
            stateDescriptor.setCullMode(CullMode::BACK);
        } else if (isDepthPass) {
            stateDescriptor.setColourWrites(false, false, false, false);
        }

        return stateDescriptor.getHash();
    };

    _computeShaderCBK = [](Material* material, const RenderStagePass renderStagePass) {

        const bool isDepthPass = IsDepthPass(renderStagePass);
        const bool isZPrePass = isDepthPass && renderStagePass._stage == RenderStage::DISPLAY;
        const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;

        const Str64 vertSource = isDepthPass ? material->baseShaderData()._depthShaderVertSource : material->baseShaderData()._colourShaderVertSource;
        const Str64 fragSource = isDepthPass ? material->baseShaderData()._depthShaderFragSource : material->baseShaderData()._colourShaderFragSource;

        Str32 vertVariant = isDepthPass ? isShadowPass ? material->baseShaderData()._shadowShaderVertVariant
                                                       : material->baseShaderData()._depthShaderVertVariant
                                        : material->baseShaderData()._colourShaderVertVariant;
        Str32 fragVariant = isDepthPass ? material->baseShaderData()._depthShaderFragVariant : material->baseShaderData()._colourShaderFragVariant;
        ShaderProgramDescriptor shaderDescriptor{};
        shaderDescriptor._name = vertSource + "_" + fragSource;

        if (isShadowPass) {
            vertVariant += "Shadow";
            fragVariant += "Shadow.VSM";
            if (to_U8(renderStagePass._variant) == to_U8(LightType::DIRECTIONAL)) {
                fragVariant += ".ORTHO";
            }
        } else if (isDepthPass) {
            vertVariant += "PrePass";
            fragVariant += "PrePass";
        }

        ShaderModuleDescriptor vertModule = {};
        vertModule._variant = vertVariant;
        vertModule._sourceFile = (vertSource + ".glsl").c_str();
        vertModule._moduleType = ShaderType::VERTEX;
        shaderDescriptor._modules.push_back(vertModule);

        if (!isDepthPass || isZPrePass || isShadowPass || material->hasTransparency()) {
            ShaderModuleDescriptor fragModule = {};
            fragModule._variant = fragVariant;
            fragModule._sourceFile = (fragSource + ".glsl").c_str();
            fragModule._moduleType = ShaderType::FRAGMENT;

            shaderDescriptor._modules.push_back(fragModule);
        }

        return shaderDescriptor;
    };

    _descriptorSetMainPass._usage = DescriptorSetUsage::PER_DRAW_SET;
    _descriptorSetPrePass._usage = DescriptorSetUsage::PER_DRAW_SET;
}

Material_ptr Material::clone(const Str256& nameSuffix) {
    DIVIDE_ASSERT(!nameSuffix.empty(), "Material error: clone called without a valid name suffix!");

    Material_ptr cloneMat = CreateResource<Material>(_parentCache, ResourceDescriptor(resourceName() + nameSuffix.c_str()));
    cloneMat->_baseMaterial = this;
    cloneMat->_properties = this->_properties;
    cloneMat->_extraShaderDefines = this->_extraShaderDefines;
    cloneMat->_computeShaderCBK = this->_computeShaderCBK;
    cloneMat->_computeRenderStateCBK = this->_computeRenderStateCBK;
    cloneMat->_shaderInfo = this->_shaderInfo;
    cloneMat->_defaultRenderStates = this->_defaultRenderStates;
    cloneMat->_topology = this->_topology;
    cloneMat->_shaderAttributes = this->_shaderAttributes;
    cloneMat->_shaderAttributesHash = this->_shaderAttributesHash;

    cloneMat->ignoreXMLData(this->ignoreXMLData());
    cloneMat->updatePriorirty(this->updatePriorirty());
    for (U8 i = 0u; i < to_U8(this->_textures.size()); ++i) {
        const TextureInfo& texInfo = this->_textures[i];
        if (texInfo._ptr != nullptr) {
            cloneMat->setTexture(
                static_cast<TextureUsage>(i),
                texInfo._ptr,
                texInfo._sampler,
                texInfo._operation,
                texInfo._useForPrePass);
        }
    }

    ScopedLock<SharedMutex> w_lock(_instanceLock);
    _instances.emplace_back(cloneMat.get());

    return cloneMat;
}

U32 Material::update([[maybe_unused]] const U64 deltaTimeUS) {
    U32 ret = to_U32(UpdateResult::OK);

    if (properties()._transparencyUpdated) {
        SetBit(ret, UpdateResult::TransparencyUpdate);
        updateTransparency();
        properties()._transparencyUpdated = false;
    }
    if (properties()._cullUpdated) {
        SetBit(ret, UpdateResult::NewCull);
        properties()._cullUpdated = false;
    }
    if (properties()._needsNewShader || s_shadersDirty) {
        recomputeShaders();
        properties()._needsNewShader = false;
        SetBit(ret, UpdateResult::NewShader);
    }

    return ret;
}

void Material::clearRenderStates() {
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        auto& perPassStates = _defaultRenderStates[s];
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            perPassStates[p].fill(g_invalidStateHash);
        }
    }
}

void Material::updateCullState() {
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        auto& perPassStates = _defaultRenderStates[s];
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            for (size_t& hash : perPassStates[p]) {
                if (hash != g_invalidStateHash) {
                    RenderStateBlock tempBlock = RenderStateBlock::Get(hash);
                    tempBlock.setCullMode(properties().doubleSided() ? CullMode::NONE : CullMode::BACK);
                    hash = tempBlock.getHash();
                }
            }
        }
    }
}

void Material::setPipelineLayout(const PrimitiveTopology topology, const AttributeMap& shaderAttributes) {
    if (topology != _topology) {
        _topology = topology;
        properties()._needsNewShader = true;
    }

    const size_t newHash = GetHash(shaderAttributes);
    if (_shaderAttributesHash == 0u || _shaderAttributesHash != newHash) {
        _shaderAttributes = shaderAttributes;
        _shaderAttributesHash = newHash;
        properties()._needsNewShader = true;
    }
}

bool Material::setSampler(const TextureUsage textureUsageSlot, const size_t samplerHash)
{
    _textures[to_U32(textureUsageSlot)]._sampler = samplerHash;

    return true;
}

bool Material::setTexture(const TextureUsage textureUsageSlot, const Texture_ptr& texture, const size_t samplerHash, const TextureOperation op, const TexturePrePassUsage prePassUsage)
{
    // Invalidate our descriptor sets
    _descriptorSetMainPass._bindings.resize(0);
    _descriptorSetPrePass._bindings.resize(0);

    const U32 slot = to_U32(textureUsageSlot);

    TextureInfo& texInfo = _textures[slot];

    if (samplerHash != _textures[slot]._sampler) {
        setSampler(textureUsageSlot, samplerHash);
    }

    setTextureOperation(textureUsageSlot, texture ? op : TextureOperation::NONE);

    {
        ScopedLock<SharedMutex> w_lock(_textureLock);
        if (texInfo._ptr != nullptr) {
            // Skip adding same texture
            if (texture != nullptr && texInfo._ptr->getGUID() == texture->getGUID()) {
                return true;
            }
        }

        texInfo._useForPrePass = texture ? prePassUsage : TexturePrePassUsage::AUTO;
        texInfo._ptr = texture;

        if (textureUsageSlot == TextureUsage::METALNESS) {
            properties()._usePackedOMR = (texture != nullptr && texture->numChannels() > 2u);
        }
        
        if (textureUsageSlot == TextureUsage::UNIT0 ||
            textureUsageSlot == TextureUsage::OPACITY)
        {
            updateTransparency();
        }
    }

    properties()._needsNewShader = true;

    return true;
}

void Material::setTextureOperation(const TextureUsage textureUsageSlot, const TextureOperation op) {

    TextureOperation& crtOp = _textures[to_base(textureUsageSlot)]._operation;

    if (crtOp != op) {
        crtOp = op;
        properties()._needsNewShader = true;
    }
}

void Material::setShaderProgramInternal(const ShaderProgramDescriptor& shaderDescriptor,
                                        ShaderProgramInfo& shaderInfo,
                                        const RenderStagePass stagePass) const
{
    OPTICK_EVENT();

    ShaderProgramDescriptor shaderDescriptorRef = shaderDescriptor;
    computeAndAppendShaderDefines(shaderDescriptorRef, stagePass);

    ResourceDescriptor shaderResDescriptor(shaderDescriptorRef._name);
    shaderResDescriptor.propertyDescriptor(shaderDescriptorRef);

    ShaderProgram_ptr shader = CreateResource<ShaderProgram>(_context.parent().resourceCache(), shaderResDescriptor);
    if (shader != nullptr) {
        const ShaderProgram* oldShader = shaderInfo._shaderRef.get();
        if (oldShader != nullptr) {
            const char* newShaderName = shader == nullptr ? nullptr : shader->resourceName().c_str();

            if (newShaderName == nullptr || strlen(newShaderName) == 0 || oldShader->resourceName().compare(newShaderName) != 0) {
                // We cannot replace a shader that is still loading in the background
                WAIT_FOR_CONDITION(oldShader->getState() == ResourceState::RES_LOADED);
                    Console::printfn(Locale::Get(_ID("REPLACE_SHADER")),
                        oldShader->resourceName().c_str(),
                        newShaderName != nullptr ? newShaderName : "NULL",
                        TypeUtil::RenderStageToString(stagePass._stage),
                        TypeUtil::RenderPassTypeToString(stagePass._passType),
                        to_base(stagePass._variant));
            }
        }
    }

    shaderInfo._shaderRef = shader;
    shaderInfo._shaderCompStage = ShaderBuildStage::COMPUTED;
    shaderInfo._shaderRef->waitForReady();
}

void Material::setShaderProgramInternal(const ShaderProgramDescriptor& shaderDescriptor,
                                        const RenderStagePass stagePass)
{
    OPTICK_EVENT();

    ShaderProgramDescriptor shaderDescriptorRef = shaderDescriptor;
    computeAndAppendShaderDefines(shaderDescriptorRef, stagePass);

    ShaderProgramInfo& info = shaderInfo(stagePass);
    // if we already have a different shader assigned ...
    if (info._shaderRef != nullptr && info._shaderRef->resourceName().compare(shaderDescriptorRef._name) != 0)
    {
        // We cannot replace a shader that is still loading in the background
        WAIT_FOR_CONDITION(info._shaderRef->getState() == ResourceState::RES_LOADED);
        Console::printfn(Locale::Get(_ID("REPLACE_SHADER")),
            info._shaderRef->resourceName().c_str(),
            shaderDescriptorRef._name.c_str(),
            TypeUtil::RenderStageToString(stagePass._stage),
            TypeUtil::RenderPassTypeToString(stagePass._passType),
            stagePass._variant);
    }

    ShaderComputeQueue::ShaderQueueElement shaderElement{ info._shaderRef, shaderDescriptorRef };
    if (updatePriorirty() == UpdatePriority::High) {
        _context.shaderComputeQueue().process(shaderElement);
        info._shaderCompStage = ShaderBuildStage::COMPUTED;
        assert(info._shaderRef != nullptr);
        info._shaderRef->waitForReady();
    } else {
        if (updatePriorirty() == UpdatePriority::Medium) {
            _context.shaderComputeQueue().addToQueueFront(shaderElement);
        } else {
            _context.shaderComputeQueue().addToQueueBack(shaderElement);
        }
        info._shaderCompStage = ShaderBuildStage::QUEUED;
    }
}

void Material::recomputeShaders() {
    OPTICK_EVENT();

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            RenderStagePass stagePass{ static_cast<RenderStage>(s), static_cast<RenderPassType>(p) };
            auto& variantMap = _shaderInfo[s][p];

            for (U8 v = 0u; v < to_U8(RenderStagePass::VariantType::COUNT); ++v) {
                ShaderProgramInfo& shaderInfo = variantMap[v];
                if (shaderInfo._shaderCompStage == ShaderBuildStage::COUNT) {
                    continue;
                }

                stagePass._variant = static_cast<RenderStagePass::VariantType>(v);
                shaderInfo._shaderCompStage = ShaderBuildStage::REQUESTED;
            }
        }
    }
}

ShaderProgramHandle Material::computeAndGetProgramHandle(const RenderStagePass renderStagePass) {
    constexpr U8 maxRetries = 250;

    bool justFinishedLoading = false;
    for (U8 i = 0; i < maxRetries; ++i) {
        if (!canDraw(renderStagePass, justFinishedLoading)) {
            if (!_context.shaderComputeQueue().stepQueue()) {
                NOP();
            }
        } else {
            return getProgramHandle(renderStagePass);
        }
    }

    return _context.defaultIMShader()->handle();
}

ShaderProgramHandle Material::getProgramHandle(const RenderStagePass renderStagePass) const {

    const ShaderProgramInfo& info = shaderInfo(renderStagePass);

    if (info._shaderRef != nullptr) {
        WAIT_FOR_CONDITION(info._shaderRef->getState() == ResourceState::RES_LOADED);
        return info._shaderRef->handle();
    }
    DIVIDE_UNEXPECTED_CALL();

    return _context.defaultIMShader()->handle();
}

bool Material::canDraw(const RenderStagePass renderStagePass, bool& shaderJustFinishedLoading) {
    OPTICK_EVENT();

    shaderJustFinishedLoading = false;
    ShaderProgramInfo& info = shaderInfo(renderStagePass);
    if (info._shaderCompStage == ShaderBuildStage::REQUESTED) {
        _computeShaderCBK(this, renderStagePass);
        const ShaderProgramDescriptor descriptor = _computeShaderCBK(this, renderStagePass);
        setShaderProgramInternal(descriptor, renderStagePass);
    }

    // If we have a shader queued (with a valid ref) ...
    if (info._shaderCompStage == ShaderBuildStage::QUEUED) {
        // ... we are now passed the "compute" stage. We just need to wait for it to load
        if (info._shaderRef == nullptr) {
            // Shader is still in the queue
            return false;
        }
        info._shaderCompStage = ShaderBuildStage::COMPUTED;
    }

    // If the shader is computed ...
    if (info._shaderCompStage == ShaderBuildStage::COMPUTED) {
        assert(info._shaderRef != nullptr);
        // ... wait for the shader to finish loading
        if (info._shaderRef->getState() != ResourceState::RES_LOADED) {
            return false;
        }
        // Once it has finished loading, it is ready for drawing
        shaderJustFinishedLoading = true;
        info._shaderCompStage = ShaderBuildStage::READY;
        info._shaderKeyCache = info._shaderRef->getGUID();
    }

    // If the shader isn't ready it may have not passed through the computational stage yet (e.g. the first time this method is called)
    if (info._shaderCompStage != ShaderBuildStage::READY) {
        // This is usually the first step in generating a shader: No shader available but we need to render in this stagePass
        if (info._shaderCompStage == ShaderBuildStage::COUNT) {
            // So request a new shader
            info._shaderCompStage = ShaderBuildStage::REQUESTED;
        }

        return false;
    }

    // Shader should be in the ready state
    return true;
}

void Material::computeAndAppendShaderDefines(ShaderProgramDescriptor& shaderDescriptor, const RenderStagePass renderStagePass) const {
    OPTICK_EVENT();

    const bool isDepthPass = IsDepthPass(renderStagePass);

    DIVIDE_ASSERT(properties().shadingMode() != ShadingMode::COUNT, "Material computeShader error: Invalid shading mode specified!");
    std::array<ModuleDefines, to_base(ShaderType::COUNT)> moduleDefines = {};

    if (_context.context().config().rendering.MSAASamples > 0u) {
        shaderDescriptor._globalDefines.emplace_back("MSAA_SCREEN_TARGET", true);
    }
    if (renderStagePass._stage == RenderStage::SHADOW) {
        shaderDescriptor._globalDefines.emplace_back("SHADOW_PASS", true);
    } else if (isDepthPass) {
        shaderDescriptor._globalDefines.emplace_back("PRE_PASS", true);
    }
    if (renderStagePass._stage == RenderStage::REFLECTION && to_U8(renderStagePass._variant) != to_base(ReflectorType::CUBE)) {
        shaderDescriptor._globalDefines.emplace_back("REFLECTION_PASS", true);
    }
    if (renderStagePass._stage == RenderStage::DISPLAY) {
        shaderDescriptor._globalDefines.emplace_back("MAIN_DISPLAY_PASS", true);
    }
    if (renderStagePass._passType == RenderPassType::OIT_PASS) {
        moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("OIT_PASS", true);
    } else if (renderStagePass._passType == RenderPassType::TRANSPARENCY_PASS) {
        moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("TRANSPARENCY_PASS", true);
    }
    switch (properties().shadingMode()) {
        case ShadingMode::FLAT: {
            shaderDescriptor._globalDefines.emplace_back("SHADING_MODE_FLAT", true);
        } break;
        case ShadingMode::TOON: {
            shaderDescriptor._globalDefines.emplace_back("SHADING_MODE_TOON", true);
        } break;
        case ShadingMode::BLINN_PHONG: {
            shaderDescriptor._globalDefines.emplace_back("SHADING_MODE_BLINN_PHONG", true);
        } break;
        case ShadingMode::PBR_MR: {
            shaderDescriptor._globalDefines.emplace_back("SHADING_MODE_PBR_MR", true);
        } break; 
        case ShadingMode::PBR_SG: {
            shaderDescriptor._globalDefines.emplace_back("SHADING_MODE_PBR_SG", true);
        } break;
        default: DIVIDE_UNEXPECTED_CALL(); break;
    }
    // Display pre-pass caches normal maps in a GBuffer, so it's the only exception
    if ((!isDepthPass || renderStagePass._stage == RenderStage::DISPLAY) &&
        _textures[to_base(TextureUsage::NORMALMAP)]._ptr != nullptr &&
        properties().bumpMethod() != BumpMethod::NONE) 
    {
        // Bump mapping?
        shaderDescriptor._globalDefines.emplace_back("COMPUTE_TBN", true);
    }

    switch (_topology) {
        case PrimitiveTopology::POINTS:                   shaderDescriptor._globalDefines.emplace_back("GEOMETRY_POINTS", true);    break;
        case PrimitiveTopology::LINES:                                                                                              break;
        case PrimitiveTopology::LINE_STRIP:                                                                                         break;
        case PrimitiveTopology::LINES_ADJANCENCY:                                                                                   break;
        case PrimitiveTopology::LINE_STRIP_ADJACENCY:     shaderDescriptor._globalDefines.emplace_back("GEOMETRY_LINES", true);     break;
        case PrimitiveTopology::TRIANGLES:                                                                                          break;
        case PrimitiveTopology::TRIANGLE_STRIP:                                                                                     break;
        case PrimitiveTopology::TRIANGLE_FAN:                                                                                       break;
        case PrimitiveTopology::TRIANGLES_ADJACENCY:                                                                                break;
        case PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY: shaderDescriptor._globalDefines.emplace_back("GEOMETRY_TRIANGLES", true); break;
        case PrimitiveTopology::PATCH:                    shaderDescriptor._globalDefines.emplace_back("GEOMETRY_PATCH", true);     break;
        default: DIVIDE_UNEXPECTED_CALL();
    }

    for (U8 i = 0u; i < to_U8(AttribLocation::COUNT); ++i) {
        const AttributeDescriptor& descriptor = _shaderAttributes[i];
        if (descriptor._dataType != GFXDataFormat::COUNT) {
            shaderDescriptor._globalDefines.emplace_back(Util::StringFormat("HAS_%s_ATTRIBUTE", Names::attribLocation[i]).c_str(), true);
        }
    }

    if (hasTransparency()) {
        moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("HAS_TRANSPARENCY", true);

        if (properties().overrides().useAlphaDiscard() && 
            renderStagePass._passType != RenderPassType::OIT_PASS)
        {
            moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("USE_ALPHA_DISCARD", true);
        }
    }

    const Configuration& config = _parentCache->context().config();
    if (!config.rendering.shadowMapping.enabled) {
        moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("DISABLE_SHADOW_MAPPING", true);
    } else {
        if (!config.rendering.shadowMapping.csm.enabled) {
            moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("DISABLE_SHADOW_MAPPING_CSM", true);
        }
        if (!config.rendering.shadowMapping.spot.enabled) {
            moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("DISABLE_SHADOW_MAPPING_SPOT", true);
        }
        if (!config.rendering.shadowMapping.point.enabled) {
            moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("DISABLE_SHADOW_MAPPING_POINT", true);
        }
    }

    shaderDescriptor._globalDefines.emplace_back(properties().isStatic() ? "NODE_STATIC" : "NODE_DYNAMIC", true);

    if (properties().isInstanced()) {
        shaderDescriptor._globalDefines.emplace_back("OVERRIDE_DATA_IDX", true);
    }

    if (properties().hardwareSkinning()) {
        moduleDefines[to_base(ShaderType::VERTEX)].emplace_back("USE_GPU_SKINNING", true);
    }

    for (ShaderModuleDescriptor& module : shaderDescriptor._modules) {
        module._defines.insert(eastl::end(module._defines), eastl::begin(moduleDefines[to_base(module._moduleType)]), eastl::end(moduleDefines[to_base(module._moduleType)]));
        module._defines.insert(eastl::end(module._defines), eastl::begin(_extraShaderDefines[to_base(module._moduleType)]), eastl::end(_extraShaderDefines[to_base(module._moduleType)]));
        module._defines.emplace_back("DEFINE_PLACEHOLDER", false);
    }
}

bool Material::unload() {
    for (TextureInfo& tex : _textures) {
        tex._ptr.reset();
    }

    static ShaderProgramInfo defaultShaderInfo = {};

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        auto& passMapShaders = _shaderInfo[s];
        auto& passMapStates = _defaultRenderStates[s];
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            passMapShaders[p].fill(defaultShaderInfo);
            passMapStates[p].fill(g_invalidStateHash);
        }
    }
    
    if (_baseMaterial != nullptr) {
        ScopedLock<SharedMutex> w_lock(_instanceLock);
        erase_if(_baseMaterial->_instances,
                 [guid = getGUID()](Material* instance) noexcept {
                     return instance->getGUID() == guid;
                 });
    }

    SharedLock<SharedMutex> r_lock(_instanceLock);
    for (Material* instance : _instances) {
        instance->_baseMaterial = nullptr;
    }

    return true;
}

void Material::setRenderStateBlock(const size_t renderStateBlockHash, const RenderStage stage, const RenderPassType pass, const RenderStagePass::VariantType variant) {
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            const RenderStage crtStage = static_cast<RenderStage>(s);
            const RenderPassType crtPass = static_cast<RenderPassType>(p);
            if ((stage == RenderStage::COUNT || stage == crtStage) && (pass == RenderPassType::COUNT || pass == crtPass)) {
                if (variant == RenderStagePass::VariantType::COUNT) {
                    _defaultRenderStates[s][p].fill(renderStateBlockHash);
                } else {
                    _defaultRenderStates[s][p][to_base(variant)] = renderStateBlockHash;
                }
            }
        }
    }
}


void Material::updateTransparency() {
    const TranslucencySource oldSource = properties()._translucencySource;
    properties()._translucencySource = TranslucencySource::COUNT;
    if (properties().overrides().transparencyEnabled()) {
        // In order of importance (less to more)!
        // diffuse channel alpha
        if (properties().baseColour().a < 0.95f && _textures[to_base(TextureUsage::UNIT0)]._operation != TextureOperation::REPLACE) {
            properties()._translucencySource = TranslucencySource::ALBEDO_COLOUR;
        }

        // base texture is translucent
        const Texture_ptr& albedo = _textures[to_base(TextureUsage::UNIT0)]._ptr;
        if (albedo && albedo->hasTransparency() && !properties().overrides().ignoreTexDiffuseAlpha()) {
            properties()._translucencySource = TranslucencySource::ALBEDO_TEX;
        }

        // opacity map
        const Texture_ptr& opacity = _textures[to_base(TextureUsage::OPACITY)]._ptr;
        if (opacity) {
            const U8 channelCount = NumChannels(opacity->descriptor().baseFormat());
            properties()._translucencySource = (channelCount == 4 && opacity->hasTransparency())
                                                                   ? TranslucencySource::OPACITY_MAP_A
                                                                   : TranslucencySource::OPACITY_MAP_R;
        }
    }

    properties()._needsNewShader = oldSource != properties().translucencySource();
}

size_t Material::getOrCreateRenderStateBlock(const RenderStagePass renderStagePass) {
    size_t& ret = _defaultRenderStates[to_base(renderStagePass._stage)]
                                      [to_base(renderStagePass._passType)]
                                      [to_base(renderStagePass._variant)];
    // If we haven't defined a state for this variant, use the default one
    if (ret == g_invalidStateHash) {
        ret = _computeRenderStateCBK(this, renderStagePass);
        assert(ret != g_invalidStateHash);
    }

    return ret;
}

void Material::getSortKeys(const RenderStagePass renderStagePass, I64& shaderKey, I32& textureKey) const {
    shaderKey = shaderInfo(renderStagePass)._shaderKeyCache;
    SharedLock<SharedMutex> r_lock(_textureLock);
    if (_textures[to_base(TextureUsage::UNIT0)]._ptr != nullptr) {
        textureKey = _textures[to_base(TextureUsage::UNIT0)]._ptr->data()._textureHandle;
    } else {
        textureKey = std::numeric_limits<I32>::lowest();
    }
}

FColour4 Material::getBaseColour(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::UNIT0)]._ptr != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::UNIT0)]._ptr.get();
    }
    return properties().baseColour();
}

FColour3 Material::getEmissive(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::EMISSIVE)]._ptr != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::EMISSIVE)]._ptr.get();
    }

    return properties().emissive();
}

FColour3 Material::getAmbient(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = false;

    return properties().ambient();
}

FColour3 Material::getSpecular(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::SPECULAR)]._ptr != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::SPECULAR)]._ptr.get();
    }
    return properties().specular();
}

F32 Material::getMetallic(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::METALNESS)]._ptr != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::METALNESS)]._ptr.get();
    }
    return properties().metallic();
}

F32 Material::getRoughness(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::ROUGHNESS)]._ptr != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::ROUGHNESS)]._ptr.get();
    }
    return properties().roughness();
}

F32 Material::getOcclusion(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::OCCLUSION)]._ptr != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::OCCLUSION)]._ptr.get();
    }
    return properties().occlusion();
}

void Material::getData(const RenderingComponent& parentComp, const U32 bestProbeID, NodeMaterialData& dataOut) {
    const FColour3& specColour = properties().specular(); //< For PHONG_SPECULAR
    const F32 shininess = CLAMPED(properties().shininess(), 0.f, MAX_SHININESS);

    const bool useOpacityAlphaChannel = properties().translucencySource() == TranslucencySource::OPACITY_MAP_A;
    const bool useAlbedoTexAlphachannel = properties().translucencySource() == TranslucencySource::ALBEDO_TEX;

    //ToDo: Maybe store all of these material properties in an internal, cached, NodeMaterialData structure? -Ionut
    dataOut._albedo.set(properties().baseColour());
    dataOut._colourData.set(properties().ambient(), shininess);
    dataOut._emissiveAndParallax.set(properties().emissive(), properties().parallaxFactor());
    dataOut._data.x = Util::PACK_UNORM4x8(CLAMPED_01(properties().occlusion()),
                                          CLAMPED_01(properties().metallic()),
                                          CLAMPED_01(properties().roughness()),
                                          (properties().doubleSided() ? 1.f : 0.f));
    dataOut._data.y = Util::PACK_UNORM4x8(specColour.r,
                                          specColour.g,
                                          specColour.b,
                                          properties().usePackedOMR() ? 1.f : 0.f);
    dataOut._data.z = Util::PACK_UNORM4x8(to_U8(properties().bumpMethod()),
                                          to_U8(properties().shadingMode()),
                                          0u,
                                          0u);
    dataOut._data.w = bestProbeID;

    dataOut._textureOperations.x = Util::PACK_UNORM4x8(to_U8(_textures[to_base(TextureUsage::UNIT0)]._operation),
                                                       to_U8(_textures[to_base(TextureUsage::UNIT1)]._operation),
                                                       to_U8(_textures[to_base(TextureUsage::SPECULAR)]._operation),
                                                       to_U8(_textures[to_base(TextureUsage::EMISSIVE)]._operation));
    dataOut._textureOperations.y = Util::PACK_UNORM4x8(to_U8(_textures[to_base(TextureUsage::OCCLUSION)]._operation),
                                                       to_U8(_textures[to_base(TextureUsage::METALNESS)]._operation),
                                                       to_U8(_textures[to_base(TextureUsage::ROUGHNESS)]._operation),
                                                       to_U8(_textures[to_base(TextureUsage::OPACITY)]._operation));
    dataOut._textureOperations.z = Util::PACK_UNORM4x8(useAlbedoTexAlphachannel ? 1.f : 0.f, 
                                                       useOpacityAlphaChannel ? 1.f : 0.f,
                                                       properties().specGloss().x,
                                                       properties().specGloss().y);
    dataOut._textureOperations.w = Util::PACK_UNORM4x8(properties().receivesShadows() ? 1.f : 0.f, 0.f, 0.f, 0.f);
}

DescriptorSet& Material::getDescriptorSet(const RenderStagePass& renderStagePass) {
    OPTICK_EVENT();

    const bool isPrePass = (renderStagePass._passType == RenderPassType::PRE_PASS);
    DescriptorSet& set = isPrePass ? _descriptorSetPrePass : _descriptorSetMainPass;
    if (set._bindings.empty()) {
        SharedLock<SharedMutex> r_lock(_textureLock);
        // Check again
        if (set._bindings.empty()) {
            const auto addTexture = [&](const U8 slot) {
                const Texture_ptr& crtTexture = _textures[slot]._ptr;
                if (crtTexture != nullptr) {
                    auto& texBinding = set._bindings.emplace_back();
                    texBinding._resourceSlot = slot;
                    STUBBED("Add a per-texture switch for shader-stage visibility - Ionut");
                    texBinding._shaderStageVisibility = DescriptorSetBinding::ShaderStageVisibility::ALL;
                    texBinding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                    texBinding._data.As<DescriptorCombinedImageSampler>() = { crtTexture->data(), _textures[slot]._sampler };
                }
            };

            if (!isPrePass) {
                for (const U8 slot : g_materialTextureSlots) {
                    addTexture(slot);
                }
            } else {
                for (U8 i = 0u; i < to_U8(TextureUsage::COUNT); ++i) {
                    bool add = false;
                    switch (_textures[i]._useForPrePass) {
                        case TexturePrePassUsage::ALWAYS: {
                            // Always add
                            add = true;
                        } break;
                        case TexturePrePassUsage::NEVER: {
                            //Skip
                            add = false;
                        } break;
                        case TexturePrePassUsage::AUTO: {
                            // Some best-fit heuristics that will surely break at one point
                            switch (static_cast<TextureUsage>(i)) {
                                case TextureUsage::UNIT0: {
                                    add = hasTransparency() && properties().translucencySource() == TranslucencySource::ALBEDO_TEX;
                                } break;
                                case TextureUsage::NORMALMAP: {
                                    add = renderStagePass._stage == RenderStage::DISPLAY;
                                } break;
                                case TextureUsage::SPECULAR: {
                                    add = renderStagePass._stage == RenderStage::DISPLAY &&
                                        (properties().shadingMode() != ShadingMode::PBR_MR && properties().shadingMode() != ShadingMode::PBR_SG);
                                } break;
                                case TextureUsage::METALNESS: {
                                    add = renderStagePass._stage == RenderStage::DISPLAY && properties().usePackedOMR();
                                } break;
                                case TextureUsage::ROUGHNESS: {
                                    add = renderStagePass._stage == RenderStage::DISPLAY && !properties().usePackedOMR();
                                } break;
                                case TextureUsage::HEIGHTMAP: {
                                    add = true;
                                } break;
                                case TextureUsage::OPACITY: {
                                    add = hasTransparency() &&
                                        (properties().translucencySource() == TranslucencySource::OPACITY_MAP_A ||
                                         properties().translucencySource() == TranslucencySource::OPACITY_MAP_R);
                                } break;
                            };
                        } break;
                    }

                    if (add) {
                        addTexture(i);
                    }
                }
            }
        }
    }

    return set;
}

void Material::rebuild() {
    recomputeShaders();

    // Alternatively we could just copy the maps directly
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            auto& shaders = _shaderInfo[s][p];
            for (const ShaderProgramInfo& info : shaders) {
                if (info._shaderRef != nullptr && info._shaderRef->getState() == ResourceState::RES_LOADED) {
                    info._shaderRef->recompile();
                }
            }
        }
    }
}

void Material::saveToXML(const string& entryName, boost::property_tree::ptree& pt) const {
    pt.put(entryName + ".version", g_materialXMLVersion);

    properties().saveToXML(entryName, pt);
    saveRenderStatesToXML(entryName, pt);
    saveTextureDataToXML(entryName, pt);
}

void Material::loadFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
    if (ignoreXMLData()) {
        return;
    }

    const size_t detectedVersion = pt.get<size_t>(entryName + ".version", 0);
    if (detectedVersion != g_materialXMLVersion) {
        Console::printfn(Locale::Get(_ID("MATERIAL_WRONG_VERSION")), assetName().c_str(), detectedVersion, g_materialXMLVersion);
        return;
    }

    properties().loadFromXML(entryName, pt);
    loadRenderStatesFromXML(entryName, pt);
    loadTextureDataFromXML(entryName, pt);
}

void Material::saveRenderStatesToXML(const string& entryName, boost::property_tree::ptree& pt) const {
    hashMap<size_t, U32> previousHashValues;

    U32 blockIndex = 0u;

    const string stateNode = Util::StringFormat("%s.RenderStates", entryName.c_str());
    const string blockNode = Util::StringFormat("%s.RenderStateIndex.PerStagePass", entryName.c_str());

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            for (U8 v = 0u; v < to_U8(RenderStagePass::VariantType::COUNT); ++v) {
                const size_t stateHash = _defaultRenderStates[s][p][v];
                if (stateHash == g_invalidStateHash) {
                    continue;
                }
                if (previousHashValues.find(stateHash) == std::cend(previousHashValues)) {
                    RenderStateBlock::SaveToXML(
                        RenderStateBlock::Get(stateHash),
                        Util::StringFormat("%s.%u", stateNode.c_str(), blockIndex),
                        pt);
                    previousHashValues[stateHash] = blockIndex++;
                }

                boost::property_tree::ptree stateTree;
                stateTree.put("StagePass.<xmlattr>.index", previousHashValues[stateHash]);
                stateTree.put("StagePass.<xmlattr>.stage", s);
                stateTree.put("StagePass.<xmlattr>.pass", p);
                stateTree.put("StagePass.<xmlattr>.variant", v);
                
                pt.add_child(blockNode, stateTree.get_child("StagePass"));
            }
        }
    }
}

void Material::loadRenderStatesFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
    hashMap<U32, size_t> previousHashValues;

    static boost::property_tree::ptree g_emptyPtree;
    const string stateNode = Util::StringFormat("%s.RenderStates", entryName.c_str());
    const string blockNode = Util::StringFormat("%s.RenderStateIndex", entryName.c_str());
    for (const auto& [tag, data] : pt.get_child(blockNode, g_emptyPtree))
    {
        assert(tag == "PerStagePass");

        const U32 b = data.get<U32>("<xmlattr>.index",   U32_MAX);                                    assert(b != U32_MAX);
        const U8  s = data.get<U8> ("<xmlattr>.stage",   to_U8(RenderStage::COUNT));                  assert(s != to_U8(RenderStage::COUNT));
        const U8  p = data.get<U8> ("<xmlattr>.pass",    to_U8(RenderPassType::COUNT));               assert(p != to_U8(RenderPassType::COUNT));
        const U8  v = data.get<U8> ("<xmlattr>.variant", to_U8(RenderStagePass::VariantType::COUNT)); assert(v != to_U8(RenderStagePass::VariantType::COUNT));

        const auto& it = previousHashValues.find(b);
        if (it != cend(previousHashValues)) {
            _defaultRenderStates[s][p][v] = it->second;
        } else {
            const RenderStateBlock block = RenderStateBlock::LoadFromXML(Util::StringFormat("%s.%u", stateNode.c_str(), b), pt);
            const size_t loadedHash = block.getHash();
            _defaultRenderStates[s][p][v] = loadedHash;
            previousHashValues[b] = loadedHash;
        }
    }
}

void Material::saveTextureDataToXML(const string& entryName, boost::property_tree::ptree& pt) const {
    hashMap<size_t, U32> previousHashValues;

    U32 samplerCount = 0u;
    for (const TextureUsage usage : g_materialTextures) {
        Texture_wptr tex = getTexture(usage);
        if (!tex.expired()) {
            const Texture_ptr texture = tex.lock();


            const string textureNode = entryName + ".texture." + TypeUtil::TextureUsageToString(usage);

            pt.put(textureNode + ".name", texture->assetName().str());
            pt.put(textureNode + ".path", texture->assetLocation().str());
            pt.put(textureNode + ".usage", TypeUtil::TextureOperationToString(_textures[to_base(usage)]._operation));

            const size_t samplerHash = _textures[to_base(usage)]._sampler;

            if (previousHashValues.find(samplerHash) == std::cend(previousHashValues)) {
                samplerCount++;
                XMLParser::saveToXML(SamplerDescriptor::Get(samplerHash), Util::StringFormat("%s.SamplerDescriptors.%u", entryName.c_str(), samplerCount), pt);
                previousHashValues[samplerHash] = samplerCount;
            }
            pt.put(textureNode + ".Sampler.id", previousHashValues[samplerHash]);
            pt.put(textureNode + ".UseForPrePass", to_U32(_textures[to_base(usage)]._useForPrePass));
        }
    }
}

void Material::loadTextureDataFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
    hashMap<U32, size_t> previousHashValues;

    ScopedLock<SharedMutex> w_lock(_textureLock);
    for (const TextureUsage usage : g_materialTextures) {
        if (pt.get_child_optional(entryName + ".texture." + TypeUtil::TextureUsageToString(usage) + ".name")) {
            const string textureNode = entryName + ".texture." + TypeUtil::TextureUsageToString(usage);

            const ResourcePath texName = ResourcePath(pt.get<string>(textureNode + ".name", ""));
            const ResourcePath texPath = ResourcePath(pt.get<string>(textureNode + ".path", ""));
            // May be a procedural texture
            if (texPath.empty()) {
                continue;
            }

            if (!texName.empty()) {
                _textures[to_base(usage)]._useForPrePass = static_cast<TexturePrePassUsage>(pt.get<U32>(textureNode + ".UseForPrePass", to_U32(_textures[to_base(usage)]._useForPrePass)));
                const U32 index = pt.get<U32>(textureNode + ".Sampler.id", 0);
                const auto& it = previousHashValues.find(index);

                size_t hash = 0u;
                if (it != cend(previousHashValues)) {
                    hash = it->second;
                } else {
                     hash = XMLParser::loadFromXML(Util::StringFormat("%s.SamplerDescriptors.%u", entryName.c_str(), index), pt);
                     previousHashValues[index] = hash;
                }

                if (_textures[to_base(usage)]._sampler != hash) {
                    setSampler(usage, hash);
                }

                TextureOperation& op = _textures[to_base(usage)]._operation;
                op = TypeUtil::StringToTextureOperation(pt.get<string>(textureNode + ".usage", TypeUtil::TextureOperationToString(op)));

                const Texture_ptr& crtTex = _textures[to_base(usage)]._ptr;
                if (crtTex == nullptr) {
                    op = TextureOperation::NONE;
                } else if (crtTex->assetLocation() + crtTex->assetName() == texPath + texName) {
                    continue;
                }

                TextureDescriptor texDesc(TextureType::TEXTURE_2D_ARRAY);
                ResourceDescriptor texture(texName.str());
                texture.assetName(texName);
                texture.assetLocation(texPath);
                texture.propertyDescriptor(texDesc);
                texture.waitForReady(true);

                Texture_ptr tex = CreateResource<Texture>(_context.parent().resourceCache(), texture);
                setTexture(usage, tex, hash, op);
            }
        }
    }
}

};