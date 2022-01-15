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

SamplerAddress Material::s_defaultTextureAddress = 0u;
bool Material::s_shadersDirty = false;

void Material::ApplyDefaultStateBlocks(Material& target) {
    /// Normal state for final rendering
    RenderStateBlock stateDescriptor = {};
    stateDescriptor.setCullMode(target.properties().doubleSided() ? CullMode::NONE : CullMode::BACK);
    stateDescriptor.setZFunc(ComparisonFunction::EQUAL);

    /// the z-pre-pass descriptor does not process colours
    RenderStateBlock zPrePassDescriptor(stateDescriptor);
    zPrePassDescriptor.setZFunc(ComparisonFunction::LEQUAL);

    RenderStateBlock depthPassDescriptor(zPrePassDescriptor);
    depthPassDescriptor.setColourWrites(false, false, false, false);

    /// A descriptor used for rendering to depth map
    RenderStateBlock shadowDescriptor(stateDescriptor);
    shadowDescriptor.setColourWrites(true, true, false, false);
    shadowDescriptor.setZFunc(ComparisonFunction::LESS);
    //shadowDescriptor.setZBias(1.1f, 4.f);
    shadowDescriptor.setCullMode(CullMode::BACK);

    target.setRenderStateBlock(depthPassDescriptor.getHash(), RenderStage::COUNT,  RenderPassType::PRE_PASS);
    target.setRenderStateBlock(zPrePassDescriptor.getHash(),  RenderStage::DISPLAY,RenderPassType::PRE_PASS);
    target.setRenderStateBlock(stateDescriptor.getHash(),     RenderStage::COUNT,  RenderPassType::MAIN_PASS);
    target.setRenderStateBlock(stateDescriptor.getHash(),     RenderStage::COUNT,  RenderPassType::OIT_PASS);
    target.setRenderStateBlock(shadowDescriptor.getHash(),    RenderStage::SHADOW, RenderPassType::COUNT);
}

void Material::OnStartup(const SamplerAddress defaultTexAddress) {
    s_defaultTextureAddress = defaultTexAddress;
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
    properties().useBindlessTextures(context.context().config().rendering.useBindlessTextures);
    properties().debugBindlessTextures(context.context().config().rendering.debugBindlessTextures);
    properties().receivesShadows(_context.context().config().rendering.shadowMapping.enabled);

    for (TextureInfo& tex : _textures) {
        tex._address = s_defaultTextureAddress;
    }

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

    ApplyDefaultStateBlocks(*this);
}

Material_ptr Material::clone(const Str256& nameSuffix) {
    DIVIDE_ASSERT(!nameSuffix.empty(), "Material error: clone called without a valid name suffix!");

    Material_ptr cloneMat = CreateResource<Material>(_parentCache, ResourceDescriptor(resourceName() + nameSuffix.c_str()));
    cloneMat->_baseMaterial = this;
    cloneMat->_properties = this->_properties;
    cloneMat->_extraShaderDefines = this->_extraShaderDefines;
    cloneMat->_customShaderCBK = this->_customShaderCBK;
    cloneMat->_shaderInfo = this->_shaderInfo;
    cloneMat->_defaultRenderStates = this->_defaultRenderStates;
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

    _instances.emplace_back(cloneMat.get());
    return cloneMat;
}

bool Material::update([[maybe_unused]] const U64 deltaTimeUS) {
    if (properties()._transparencyUpdated) {
        updateTransparency();
        properties()._transparencyUpdated = false;
    }
    if (properties()._cullUpdated) {
        for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
            if (s == to_U8(RenderStage::SHADOW)) {
                continue;
            }
            auto& perPassStates = _defaultRenderStates[s];
            for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
                for (size_t& hash : perPassStates[p]) {
                    if (hash != g_invalidStateHash) {
                        RenderStateBlock tempBlock = RenderStateBlock::get(hash);
                        tempBlock.setCullMode(properties().doubleSided() ? CullMode::NONE : CullMode::BACK);
                        hash = tempBlock.getHash();
                    }
                }
            }
        }
        properties()._cullUpdated = false;
    }
    if (properties()._needsNewShader || s_shadersDirty) {
        recomputeShaders();
        properties()._needsNewShader = false;
        return true;
    }

    return false;
}

bool Material::setSampler(const TextureUsage textureUsageSlot, const size_t samplerHash)
{

    TextureInfo& tex = _textures[to_U32(textureUsageSlot)];
    if (tex._address != s_defaultTextureAddress &&  tex._sampler != samplerHash) {
        assert(tex._ptr != nullptr && tex._ptr->getState() == ResourceState::RES_LOADED);
        tex._address = tex._ptr->getGPUAddress(samplerHash);
    }
    tex._sampler = samplerHash;

    return true;
}

bool Material::setTexture(const TextureUsage textureUsageSlot, const Texture_ptr& texture, const size_t samplerHash, const TextureOperation op, const TexturePrePassUsage prePassUsage)
{
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
        texInfo._address = texture ? texture->getGPUAddress(samplerHash) : s_defaultTextureAddress;

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
    shaderResDescriptor.threaded(false);

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
}

void Material::setShaderProgramInternal(const ShaderProgramDescriptor& shaderDescriptor,
                                        const RenderStagePass stagePass)
{
    OPTICK_EVENT();

    ShaderProgramDescriptor shaderDescriptorRef = shaderDescriptor;
    computeAndAppendShaderDefines(shaderDescriptorRef, stagePass);

    ResourceDescriptor shaderResDescriptor{ shaderDescriptorRef._name };
    shaderResDescriptor.propertyDescriptor(shaderDescriptorRef);
    shaderResDescriptor.threaded(false);

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

I64 Material::computeAndGetProgramGUID(const RenderStagePass renderStagePass) {
    constexpr U8 maxRetries = 250;

    bool justFinishedLoading = false;
    for (U8 i = 0; i < maxRetries; ++i) {
        if (!canDraw(renderStagePass, justFinishedLoading)) {
            if (!_context.shaderComputeQueue().stepQueue()) {
                NOP();
            }
        } else {
            return getProgramGUID(renderStagePass);
        }
    }

    return ShaderProgram::DefaultShader()->getGUID();
}

I64 Material::getProgramGUID(const RenderStagePass renderStagePass) const {

    const ShaderProgramInfo& info = shaderInfo(renderStagePass);

    if (info._shaderRef != nullptr) {
        WAIT_FOR_CONDITION(info._shaderRef->getState() == ResourceState::RES_LOADED);
        return info._shaderRef->getGUID();
    }
    DIVIDE_UNEXPECTED_CALL();

    return ShaderProgram::DefaultShader()->getGUID();
}

bool Material::canDraw(const RenderStagePass renderStagePass, bool& shaderJustFinishedLoading) {
    OPTICK_EVENT();

    shaderJustFinishedLoading = false;
    ShaderProgramInfo& info = shaderInfo(renderStagePass);
    if (info._shaderCompStage == ShaderBuildStage::REQUESTED) {
        computeShader(renderStagePass);
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

    ModuleDefines globalDefines = {};

    const bool msaaScreenTarget = _context.renderTargetPool().screenTargetID()._usage == RenderTargetUsage::SCREEN_MS;
    if (msaaScreenTarget) {
        globalDefines.emplace_back("MSAA_SCREEN_TARGET", true);
    }

    if (renderStagePass._stage == RenderStage::SHADOW) {
        globalDefines.emplace_back("SHADOW_PASS", true);
    } else if (isDepthPass) {
        globalDefines.emplace_back("PRE_PASS", true);
    }
    if (renderStagePass._stage == RenderStage::REFLECTION && to_U8(renderStagePass._variant) != to_base(ReflectorType::CUBE)) {
        globalDefines.emplace_back("REFLECTION_PASS", true);
    }
    if (renderStagePass._stage == RenderStage::DISPLAY) {
        globalDefines.emplace_back("MAIN_DISPLAY_PASS", true);
    }
    if (renderStagePass._passType == RenderPassType::OIT_PASS) {
        moduleDefines[to_base(ShaderType::FRAGMENT)].emplace_back("OIT_PASS", true);
    }
    switch (properties().shadingMode()) {
        case ShadingMode::FLAT: {
            globalDefines.emplace_back("SHADING_MODE_FLAT", true);
        } break;
        case ShadingMode::TOON: {
            globalDefines.emplace_back("SHADING_MODE_TOON", true);
        } break;
        case ShadingMode::BLINN_PHONG: {
            globalDefines.emplace_back("SHADING_MODE_BLINN_PHONG", true);
        } break;
        case ShadingMode::PBR_MR: {
            globalDefines.emplace_back("SHADING_MODE_PBR_MR", true);
        } break; 
        case ShadingMode::PBR_SG: {
            globalDefines.emplace_back("SHADING_MODE_PBR_SG", true);
        } break;
        default: DIVIDE_UNEXPECTED_CALL(); break;
    }
    // Display pre-pass caches normal maps in a GBuffer, so it's the only exception
    if ((!isDepthPass || renderStagePass._stage == RenderStage::DISPLAY) &&
        _textures[to_base(TextureUsage::NORMALMAP)]._ptr != nullptr &&
        properties().bumpMethod() != BumpMethod::NONE) 
    {
        // Bump mapping?
        globalDefines.emplace_back("COMPUTE_TBN", true);
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

    globalDefines.emplace_back(properties().isStatic() ? "NODE_STATIC" : "NODE_DYNAMIC", true);

    if (properties().isInstanced()) {
        globalDefines.emplace_back("OVERRIDE_DATA_IDX", true);
    }

    if (properties().hardwareSkinning()) {
        moduleDefines[to_base(ShaderType::VERTEX)].emplace_back("USE_GPU_SKINNING", true);
    }

    for (ShaderModuleDescriptor& module : shaderDescriptor._modules) {
        module._defines.insert(eastl::end(module._defines), eastl::begin(globalDefines), eastl::end(globalDefines));
        module._defines.insert(eastl::end(module._defines), eastl::begin(moduleDefines[to_base(module._moduleType)]), eastl::end(moduleDefines[to_base(module._moduleType)]));
        module._defines.insert(eastl::end(module._defines), eastl::begin(_extraShaderDefines[to_base(module._moduleType)]), eastl::end(_extraShaderDefines[to_base(module._moduleType)]));
        module._defines.emplace_back("DEFINE_PLACEHOLDER", false);
        shaderDescriptor._name.append(Util::StringFormat("_%zu", ShaderProgram::DefinesHash(module._defines)));
    }
}

/// If the current material doesn't have a shader associated with it, then add the default ones.
void Material::computeShader(const RenderStagePass renderStagePass) {
    OPTICK_EVENT();

    if (_customShaderCBK) {
        const ShaderProgramDescriptor descriptor = _customShaderCBK(renderStagePass);
        setShaderProgramInternal(descriptor, renderStagePass);
        return;
    }

    const bool isDepthPass = IsDepthPass(renderStagePass);
    const bool isZPrePass = isDepthPass && renderStagePass._stage == RenderStage::DISPLAY;
    const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;

    const Str64 vertSource = isDepthPass ? baseShaderData()._depthShaderVertSource : baseShaderData()._colourShaderVertSource;
    const Str64 fragSource = isDepthPass ? baseShaderData()._depthShaderFragSource : baseShaderData()._colourShaderFragSource;

    Str32 vertVariant = isDepthPass 
                            ? isShadowPass 
                                ? baseShaderData()._shadowShaderVertVariant
                                : baseShaderData()._depthShaderVertVariant
                            : baseShaderData()._colourShaderVertVariant;
    Str32 fragVariant = isDepthPass ? baseShaderData()._depthShaderFragVariant : baseShaderData()._colourShaderFragVariant;
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
    vertModule._batchSameFile = false;
    vertModule._moduleType = ShaderType::VERTEX;
    shaderDescriptor._modules.push_back(vertModule);

    if (!isDepthPass || isZPrePass || isShadowPass || hasTransparency()) {
        ShaderModuleDescriptor fragModule = {};
        fragModule._variant = fragVariant;
        fragModule._sourceFile = (fragSource + ".glsl").c_str();
        fragModule._moduleType = ShaderType::FRAGMENT;

        shaderDescriptor._modules.push_back(fragModule);
    }

    setShaderProgramInternal(shaderDescriptor, renderStagePass);
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
        erase_if(_baseMaterial->_instances,
                 [guid = getGUID()](Material* instance) noexcept {
                     return instance->getGUID() == guid;
                 });
    }

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

size_t Material::getRenderStateBlock(const RenderStagePass renderStagePass) const {
    const auto& variantMap = _defaultRenderStates[to_base(renderStagePass._stage)][to_base(renderStagePass._passType)];

    const size_t ret = variantMap[to_base(renderStagePass._variant)];
    // If we haven't defined a state for this variant, use the default one
    if (ret == g_invalidStateHash) {
        return variantMap[0u];
    }

    return ret;
}

void Material::getSortKeys(const RenderStagePass renderStagePass, I64& shaderKey, I32& textureKey) const {
    shaderKey = shaderInfo(renderStagePass)._shaderKeyCache;
    if (properties().useBindlessTextures()) {
        textureKey = 0u;
    } else {
        SharedLock<SharedMutex> r_lock(_textureLock);
        if (_textures[to_base(TextureUsage::UNIT0)]._ptr != nullptr) {
            textureKey = _textures[to_base(TextureUsage::UNIT0)]._ptr->data()._textureHandle;
        } else {
            textureKey = std::numeric_limits<I32>::lowest();
        }
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
                                          0.f);
    dataOut._data.y = Util::PACK_UNORM4x8(specColour.r, specColour.g, specColour.b, (properties().doubleSided() ? 1.f : 0.f));
    dataOut._data.z = Util::PACK_UNORM4x8(0u, to_U8(properties().shadingMode()), properties().usePackedOMR() ? 1u : 0u, to_U8(properties().bumpMethod()));
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

void Material::getTextures(const RenderingComponent& parentComp, NodeMaterialTextures& texturesOut) {
    for (U8 i = 0u; i < MATERIAL_TEXTURE_COUNT; ++i) {
        texturesOut[i] = TextureToUVec2(_textures[to_base(g_materialTextures[i])]._address);
    }
}

bool Material::getTextureData(const RenderStagePass renderStagePass, TextureDataContainer& textureData) {
    OPTICK_EVENT();
    // We only need to actually bind NON-RESIDENT textures. 
    if (properties().useBindlessTextures() && !properties().debugBindlessTextures()) {
        return true;
    }

    const bool isPrePass = (renderStagePass._passType == RenderPassType::PRE_PASS);
    const auto addTexture = [&](const U8 slot) {
        const Texture_ptr& crtTexture = _textures[slot]._ptr;
        if (crtTexture != nullptr) {
            textureData.add(TextureEntry{ crtTexture->data(), _textures[slot]._sampler, slot });
            return true;
        }
        return false;
    };

    SharedLock<SharedMutex> r_lock(_textureLock);
    bool ret = false;
    if (!isPrePass) {
        for (const U8 slot : g_materialTextureSlots) {
            if (addTexture(slot)) {
                ret = true;
            }
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

            if (add && addTexture(i)) {
                ret = true;
            }
        }
    }

    return ret;
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
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            for (U8 v = 0u; v < to_U8(RenderStagePass::VariantType::COUNT); ++v) {
                // we could just use _defaultRenderStates[s][p][v] for a direct lookup, but this handles the odd double-sided / no cull case
                const size_t stateHash = getRenderStateBlock(
                    RenderStagePass{
                        static_cast<RenderStage>(s),
                        static_cast<RenderPassType>(p),
                        0u,
                        static_cast<RenderStagePass::VariantType>(v)
                    }
                );
                if (stateHash != g_invalidStateHash && previousHashValues.find(stateHash) == std::cend(previousHashValues)) {
                    blockIndex++;
                    RenderStateBlock::saveToXML(
                        RenderStateBlock::get(stateHash),
                        Util::StringFormat("%s.RenderStates.%u",
                                           entryName.c_str(),
                                           blockIndex),
                        pt);
                    previousHashValues[stateHash] = blockIndex;
                }
                pt.put(Util::StringFormat("%s.%s.%s.%d.id",
                            entryName.c_str(),
                            TypeUtil::RenderStageToString(static_cast<RenderStage>(s)),
                            TypeUtil::RenderPassTypeToString(static_cast<RenderPassType>(p)),
                            v), 
                    previousHashValues[stateHash]);
            }
        }
    }
}

void Material::loadRenderStatesFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
    hashMap<U32, size_t> previousHashValues;

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            for (U8 v = 0u; v < to_U8(RenderStagePass::VariantType::COUNT); ++v) {
                const U32 stateIndex = pt.get<U32>(
                    Util::StringFormat("%s.%s.%s.%d.id", 
                            entryName.c_str(),
                            TypeUtil::RenderStageToString(static_cast<RenderStage>(s)),
                            TypeUtil::RenderPassTypeToString(static_cast<RenderPassType>(p)),
                            v
                        ),
                    0
                );
                if (stateIndex != 0) {
                    const auto& it = previousHashValues.find(stateIndex);
                    if (it != cend(previousHashValues)) {
                        _defaultRenderStates[s][p][v] = it->second;
                    } else {
                        const size_t stateHash = RenderStateBlock::loadFromXML(Util::StringFormat("%s.RenderStates.%u", entryName.c_str(), stateIndex), pt);
                        _defaultRenderStates[s][p][v] = stateHash;
                        previousHashValues[stateIndex] = stateHash;
                    }
                }
            }
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
                XMLParser::saveToXML(SamplerDescriptor::get(samplerHash), Util::StringFormat("%s.SamplerDescriptors.%u", entryName.c_str(), samplerCount), pt);
                previousHashValues[samplerHash] = samplerCount;
            }
            pt.put(textureNode + ".Sampler.id", previousHashValues[samplerHash]);
            pt.put(textureNode + ".UseForPrePass", to_U32(_textures[to_base(usage)]._useForPrePass));
        }
    }
}

void Material::loadTextureDataFromXML(const string& entryName, const boost::property_tree::ptree& pt) {
    hashMap<U32, size_t> previousHashValues;

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

                {
                    ScopedLock<SharedMutex> w_lock(_textureLock);
                    const Texture_ptr& crtTex = _textures[to_base(usage)]._ptr;
                    if (crtTex == nullptr) {
                        op = TextureOperation::NONE;
                    } else if (crtTex->assetLocation() + crtTex->assetName() == texPath + texName) {
                        continue;
                    }
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