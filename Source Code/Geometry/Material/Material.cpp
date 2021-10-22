#include "stdafx.h"

#include "Headers/Material.h"
#include "Headers/ShaderComputeQueue.h"

#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Utility/Headers/Localization.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"

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

void Material::ApplyDefaultStateBlocks(Material& target) {
    /// Normal state for final rendering
    RenderStateBlock stateDescriptor = {};
    stateDescriptor.setCullMode(target.doubleSided() ? CullMode::NONE : CullMode::BACK);
    stateDescriptor.setZFunc(ComparisonFunction::EQUAL);

    RenderStateBlock oitDescriptor(stateDescriptor);
    oitDescriptor.setZFunc(ComparisonFunction::LEQUAL);
    oitDescriptor.depthTestEnabled(true);

    /// the z-pre-pass descriptor does not process colours
    RenderStateBlock zPrePassDescriptor(stateDescriptor);
    zPrePassDescriptor.setColourWrites(true, true, true, true);
    zPrePassDescriptor.setZFunc(ComparisonFunction::LEQUAL);

    /// A descriptor used for rendering to depth map
    RenderStateBlock shadowDescriptor(stateDescriptor);
    shadowDescriptor.setColourWrites(true, true, false, false);
    shadowDescriptor.setZFunc(ComparisonFunction::LESS);

    target.setRenderStateBlock(zPrePassDescriptor.getHash(), RenderStage::COUNT,  RenderPassType::PRE_PASS);
    target.setRenderStateBlock(stateDescriptor.getHash(),    RenderStage::COUNT,  RenderPassType::MAIN_PASS);
    target.setRenderStateBlock(oitDescriptor.getHash(),      RenderStage::COUNT,  RenderPassType::OIT_PASS);
    target.setRenderStateBlock(shadowDescriptor.getHash(),   RenderStage::SHADOW, RenderPassType::COUNT);
}

Material::Material(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name)
    : CachedResource(ResourceType::DEFAULT, descriptorHash, name),
      _useBindlessTextures(context.context().config().rendering.useBindlessTextures),
      _context(context),
      _parentCache(parentCache)
{
    receivesShadows(_context.context().config().rendering.shadowMapping.enabled);

    _textureAddresses.fill(0u);
    _textureOperations.fill(TextureOperation::NONE);

    const ShaderProgramInfo defaultShaderInfo = {};
    // Could just direct copy the arrays, but this looks cool
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        ShaderVariantsPerPass& perPassInfo = _shaderInfo[s];
        StateVariantsPerPass& perPassStates = _defaultRenderStates[s];

        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            perPassInfo[p].fill(defaultShaderInfo);
            perPassStates[p].fill(g_invalidStateHash);
        }
    }

    ApplyDefaultStateBlocks(*this);
}

Material_ptr Material::clone(const Str256& nameSuffix) {
    DIVIDE_ASSERT(!nameSuffix.empty(),
                  "Material error: clone called without a valid name suffix!");

    const Material& base = *this;
    Material_ptr cloneMat = CreateResource<Material>(_parentCache, ResourceDescriptor(resourceName() + nameSuffix.c_str()));

    {
        cloneMat->_baseMaterial = this;
        _instances.emplace_back(cloneMat.get());
    }

    cloneMat->_baseColour = base._baseColour;
    cloneMat->_emissive = base._emissive;
    cloneMat->_ambient = base._ambient;
    cloneMat->_specular = base._specular;
    cloneMat->_metallic = base._metallic;
    cloneMat->_roughness = base._roughness;
    cloneMat->_parallaxFactor = base._parallaxFactor;
    cloneMat->_shadingMode = base._shadingMode;
    cloneMat->_translucencySource = base._translucencySource;
    cloneMat->_textureOperations = base._textureOperations;
    cloneMat->_bumpMethod = base._bumpMethod;
    cloneMat->_doubleSided = base._doubleSided;
    cloneMat->_transparencyEnabled = base._transparencyEnabled;
    cloneMat->_receivesShadows = base._receivesShadows;
    cloneMat->_isStatic = base._isStatic;
    cloneMat->_ignoreTexDiffuseAlpha = base._ignoreTexDiffuseAlpha;
    cloneMat->_usePlanarReflections = base._usePlanarReflections;
    cloneMat->_usePlanarRefractions = base._usePlanarRefractions;
    cloneMat->_hardwareSkinning = base._hardwareSkinning;
    cloneMat->_isRefractive = base._isRefractive;
    cloneMat->_textureUseForDepth = _textureUseForDepth;
    cloneMat->_extraShaderDefines = base._extraShaderDefines;
    cloneMat->_customShaderCBK = base._customShaderCBK;

    cloneMat->baseShaderData(base.baseShaderData());
    cloneMat->ignoreXMLData(base.ignoreXMLData());

    // Could just direct copy the arrays, but this looks cool
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            for (U8 v = 0u; v < g_maxVariantsPerPass; ++v) {
                cloneMat->_shaderInfo[s][p][v] = _shaderInfo[s][p][v];
                cloneMat->_defaultRenderStates[s][p][v] = _defaultRenderStates[s][p][v];
            }
        }
    }

    for (U8 i = 0; i < to_U8(base._textures.size()); ++i) {
        const Texture_ptr& tex = base._textures[i];
        if (tex) {
            cloneMat->setTexture(static_cast<TextureUsage>(i), tex, base._samplers[i], base._textureOperations[i]);
        }
    }
    cloneMat->_needsNewShader = true;

    return cloneMat;
}

bool Material::update(const U64 deltaTimeUS) {
    ACKNOWLEDGE_UNUSED(deltaTimeUS);

    if (_needsNewShader) {
        recomputeShaders();
        _needsNewShader = false;
        return true;
    }

    return false;
}

bool Material::setSampler(const TextureUsage textureUsageSlot, const size_t samplerHash, const bool applyToInstances)
{
    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->setSampler(textureUsageSlot, samplerHash, true);
        }
    }

    const U32 slot = to_U32(textureUsageSlot);
    _samplers[slot] = samplerHash;
    if (_textureAddresses[slot] != 0u) {
        assert(_textures[slot] != nullptr);
        assert(_textures[slot]->getState() == ResourceState::RES_LOADED);
        _textureAddresses[slot] = _textures[slot]->getGPUAddress(samplerHash);
    }
    return true;
}

// base = base texture
// second = second texture used for multitexturing
// bump = bump map
bool Material::setTexture(const TextureUsage textureUsageSlot, const Texture_ptr& texture, const size_t samplerHash, const TextureOperation op, const bool applyToInstances) 
{
    setSampler(textureUsageSlot, samplerHash, applyToInstances);

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->setTexture(textureUsageSlot, texture, samplerHash, op, true);
        }
    }

    const U32 slot = to_U32(textureUsageSlot);
    setTextureOperation(textureUsageSlot, op, false);

    {
        ScopedLock<SharedMutex> w_lock(_textureLock);
        if (_textures[slot]) {
            // Skip adding same texture
            if (texture != nullptr && _textures[slot]->getGUID() == texture->getGUID()) {
                return true;
            }
        }

        _textures[slot] = texture;
        _textureAddresses[slot] = texture ? texture->getGPUAddress(samplerHash) : 0u;

        if (textureUsageSlot == TextureUsage::METALNESS) {
            _usePackedOMR = (texture != nullptr && texture->numChannels() > 2u);
        }
        
        if (textureUsageSlot == TextureUsage::NORMALMAP ||
            textureUsageSlot == TextureUsage::HEIGHTMAP)
        {
            const bool isHeight = textureUsageSlot == TextureUsage::HEIGHTMAP;

            // If we have the opacity texture is the albedo map, we don't need it. We can just use albedo alpha
            const Texture_ptr& otherTex = _textures[isHeight ? to_base(TextureUsage::NORMALMAP)
                                                             : to_base(TextureUsage::HEIGHTMAP)];

            if (otherTex != nullptr && texture != nullptr && otherTex->data()._textureHandle == texture->data()._textureHandle) {
                _textures[to_base(TextureUsage::HEIGHTMAP)] = nullptr;
                _textureAddresses[to_base(TextureUsage::HEIGHTMAP)] = 0u;
            }
        }

        if (textureUsageSlot == TextureUsage::UNIT0 ||
            textureUsageSlot == TextureUsage::OPACITY)
        {
            bool isOpacity = true;
            if (textureUsageSlot == TextureUsage::UNIT0) {
                _textureKeyCache = texture == nullptr ? std::numeric_limits<I32>::lowest() : (_useBindlessTextures ? 0u : texture->data()._textureHandle);
                isOpacity = false;
            }

            // If the opacity texture is the same as the albedo map, we don't need it. We can just use albedo alpha
            const Texture_ptr& otherTex = _textures[isOpacity ? to_base(TextureUsage::UNIT0)
                                                              : to_base(TextureUsage::OPACITY)];

            if (otherTex != nullptr && texture != nullptr && otherTex->data()._textureHandle == texture->data()._textureHandle) {
                _textures[to_base(TextureUsage::OPACITY)] = nullptr;
                _textureAddresses[to_base(TextureUsage::OPACITY)] = 0u;
            }

            updateTransparency();
        }
    }

    _needsNewShader = true;

    return true;
}

void Material::setTextureOperation(const TextureUsage textureUsageSlot, 
                                   const TextureOperation op, 
                                   const bool applyToAllInstances) {

    TextureOperation& crtOp = _textureOperations[to_base(textureUsageSlot)];

    if (crtOp != op) {
        crtOp = op;
        _needsNewShader = true;
    }

    if (applyToAllInstances) {
        for (Material* instance : _instances) {
            instance->setTextureOperation(textureUsageSlot, op, true);
        }
    }
}

void Material::setShaderProgramInternal(const ShaderProgram_ptr& shader,
                                        ShaderProgramInfo& shaderInfo,
                                        const RenderStage stage,
                                        const RenderPassType pass,
                                        U8 variant) const
{
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
                        TypeUtil::RenderStageToString(stage),
                        TypeUtil::RenderPassTypeToString(pass),
                        variant);
            }
        }
    }

    shaderInfo._shaderRef = shader;
    shaderInfo._shaderCompStage = shader == nullptr || shader->getState() == ResourceState::RES_LOADED
                                                     ? (shaderInfo._customShader ? ShaderBuildStage::COMPUTED : ShaderBuildStage::READY)
                                                     : ShaderBuildStage::COMPUTED;

    if (shader != nullptr && shaderInfo._shaderCompStage == ShaderBuildStage::READY) {
        shaderInfo._shaderKeyCache = shaderInfo._shaderRef->getGUID();
    }
}

void Material::setShaderProgramInternal(const ResourceDescriptor& shaderDescriptor,
                                        const RenderStagePass& stagePass,
                                        const bool computeOnAdd) {
    OPTICK_EVENT();

    ShaderProgramInfo& info = shaderInfo(stagePass);
    // if we already have a different shader assigned ...
    if (info._shaderRef != nullptr && info._shaderRef->resourceName().compare(shaderDescriptor.resourceName()) != 0)
    {
        // We cannot replace a shader that is still loading in the background
        WAIT_FOR_CONDITION(info._shaderRef->getState() == ResourceState::RES_LOADED);
        Console::printfn(Locale::Get(_ID("REPLACE_SHADER")),
            info._shaderRef->resourceName().c_str(),
            shaderDescriptor.resourceName().c_str(),
            TypeUtil::RenderStageToString(stagePass._stage),
            TypeUtil::RenderPassTypeToString(stagePass._passType),
            stagePass._variant);
    }

    ShaderComputeQueue::ShaderQueueElement shaderElement{ info._shaderRef, shaderDescriptor };
    if (computeOnAdd) {
        _context.shaderComputeQueue().process(shaderElement);
        info._shaderCompStage = ShaderBuildStage::COMPUTED;
        assert(info._shaderRef != nullptr);
    } else {
        _context.shaderComputeQueue().addToQueueBack(shaderElement);
        info._shaderCompStage = ShaderBuildStage::QUEUED;
    }
}

void Material::recomputeShaders() {
    OPTICK_EVENT();

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            RenderStagePass stagePass{ static_cast<RenderStage>(s), static_cast<RenderPassType>(p) };
            ShaderPerVariant& variantMap = _shaderInfo[s][p];

            for (U8 v = 0; v < g_maxVariantsPerPass; ++v) {
                ShaderProgramInfo& shaderInfo = variantMap[v];
                if (shaderInfo._shaderCompStage == ShaderBuildStage::COUNT) {
                    continue;
                }

                stagePass._variant = v;
                if (!shaderInfo._customShader) {
                    shaderInfo._shaderCompStage = ShaderBuildStage::REQUESTED;
                    computeShader(stagePass);
                } else {
                    if (shaderInfo._shaderCompStage == ShaderBuildStage::COMPUTED) {
                        shaderInfo._shaderCompStage = ShaderBuildStage::READY;
                    } else if (shaderInfo._shaderCompStage == ShaderBuildStage::READY && _customShaderCBK) {
                        _customShaderCBK(*this, stagePass);
                    }

                    if (shaderInfo._shaderRef != nullptr && shaderInfo._shaderCompStage == ShaderBuildStage::READY) {
                        shaderInfo._shaderKeyCache = shaderInfo._shaderRef->getGUID();
                    }
                }
            }
        }
    }
}

I64 Material::computeAndGetProgramGUID(const RenderStagePass& renderStagePass) {
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

I64 Material::getProgramGUID(const RenderStagePass& renderStagePass) const {

    const ShaderProgramInfo& info = shaderInfo(renderStagePass);

    if (info._shaderRef != nullptr) {
        WAIT_FOR_CONDITION(info._shaderRef->getState() == ResourceState::RES_LOADED);
        return info._shaderRef->getGUID();
    }
    DIVIDE_UNEXPECTED_CALL();

    return ShaderProgram::DefaultShader()->getGUID();
}

bool Material::canDraw(const RenderStagePass& renderStagePass, bool& shaderJustFinishedLoading) {
    OPTICK_EVENT();

    shaderJustFinishedLoading = false;
    ShaderProgramInfo& info = shaderInfo(renderStagePass);
    // If we have a shader queued (with a valid ref) ...
    if (info._shaderCompStage == ShaderBuildStage::QUEUED) {
        // ... we are now passed the "compute" stage. We just need to wait for it to load
        if (info._shaderRef != nullptr) {
            info._shaderCompStage = ShaderBuildStage::COMPUTED;
        } else {
            // Shader is still in the queue
            return false;
        }
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
        // If it's a custom shader, not much we can do as custom shaders are either already computed or ready
        if (info._customShader) {
            // Just wait for it to load
            DIVIDE_ASSERT(info._shaderCompStage == ShaderBuildStage::COMPUTED);
            return false;
        }

        // This is usually the first step in generating a shader: No shader available but we need to render in this stagePass
        if (info._shaderCompStage == ShaderBuildStage::COUNT) {
            // So request a new shader
            info._shaderCompStage = ShaderBuildStage::REQUESTED;
            computeShader(renderStagePass);
        }

        return false;
    }

    // Shader should be in the ready state
    return true;
}

/// If the current material doesn't have a shader associated with it, then add the default ones.
void Material::computeShader(const RenderStagePass& renderStagePass) {
    OPTICK_EVENT();

    const bool isDepthPass = renderStagePass.isDepthPass();
    const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;

    // At this point, only computation requests are processed
    constexpr U32 slot0 = to_base(TextureUsage::UNIT0);
    constexpr U32 slot1 = to_base(TextureUsage::UNIT1);

    DIVIDE_ASSERT(_shadingMode != ShadingMode::COUNT, "Material computeShader error: Invalid shading mode specified!");


    ModuleDefines vertDefines = {};
    ModuleDefines fragDefines = {};
    ModuleDefines globalDefines = {};

    vertDefines.insert(std::cend(vertDefines),
                       std::cbegin(_extraShaderDefines[to_base(ShaderType::VERTEX)]),
                       std::cend(_extraShaderDefines[to_base(ShaderType::VERTEX)]));

    fragDefines.insert(std::cend(fragDefines),
                       std::cbegin(_extraShaderDefines[to_base(ShaderType::FRAGMENT)]),
                       std::cend(_extraShaderDefines[to_base(ShaderType::FRAGMENT)]));

    for(const TextureUsage usage : g_materialTextures) {
        const U8 slot = to_base(usage);
        if (_textures[slot] != nullptr) {
            if (_textures[slot]->data()._textureType == TextureType::TEXTURE_2D_ARRAY) {
                  globalDefines.emplace_back(Util::StringFormat("SAMPLER_%s_IS_ARRAY", Names::textureUsage[slot]), true);
            }
        }
    }

    if (_textures[slot1] && !_textures[slot0]) {
        std::swap(_textures[slot0], _textures[slot1]);
        std::swap(_textureAddresses[slot0], _textureAddresses[slot1]);
        updateTransparency();
    }

    const Str64 vertSource = isDepthPass ? baseShaderData()._depthShaderVertSource : baseShaderData()._colourShaderVertSource;
    const Str64 fragSource = isDepthPass ? baseShaderData()._depthShaderFragSource : baseShaderData()._colourShaderFragSource;

    Str32 vertVariant = isDepthPass 
                            ? isShadowPass 
                                ? baseShaderData()._shadowShaderVertVariant
                                : baseShaderData()._depthShaderVertVariant
                            : baseShaderData()._colourShaderVertVariant;
    Str32 fragVariant = isDepthPass ? baseShaderData()._depthShaderFragVariant : baseShaderData()._colourShaderFragVariant;
    Str256 shaderName = vertSource + "_" + fragSource;

    if (isShadowPass) {
        shaderName += ".SHDW";
        vertVariant += "Shadow";
        fragVariant += "Shadow.VSM";
        globalDefines.emplace_back("SHADOW_PASS", true);
        if (renderStagePass._variant == to_U8(LightType::DIRECTIONAL)) {
            fragVariant += ".ORTHO";
            fragDefines.emplace_back("ORTHO_PROJECTION", true);
        }
    } else if (isDepthPass) {
        shaderName += ".PP";
        vertVariant += "PrePass";
        fragVariant += "PrePass";
        globalDefines.emplace_back("PRE_PASS", true);
    }

    if (renderStagePass._passType == RenderPassType::OIT_PASS) {
        shaderName += ".OIT";
        fragDefines.emplace_back("OIT_PASS", true);
    } else if (renderStagePass._stage == RenderStage::DISPLAY) {
        shaderName += ".MDP";
        fragDefines.emplace_back("MAIN_DISPLAY_PASS", true);
    }

    if (!_textures[slot0]) {
        fragDefines.emplace_back("SKIP_TEX0", true);
        shaderName += ".NTex0";
    }
    if (!_textures[slot1]) {
        fragDefines.emplace_back("SKIP_TEX1", true);
        shaderName += ".NTex1";
    }
    // Display pre-pass caches normal maps in a GBuffer, so it's the only exception
    if ((!isDepthPass || renderStagePass._stage == RenderStage::DISPLAY) &&
        _textures[to_base(TextureUsage::NORMALMAP)] != nullptr &&
        _bumpMethod != BumpMethod::NONE) 
    {
        // Bump mapping?
        globalDefines.emplace_back("COMPUTE_TBN", true);
        if (_bumpMethod != BumpMethod::NORMAL) {
            globalDefines.emplace_back("COMPUTE_POM", true);
        }
    }

    if (!isDepthPass) {
        if (_textures[to_base(TextureUsage::SPECULAR)] != nullptr) {
            shaderName += ".S";
            fragDefines.emplace_back("USE_SPECULAR_MAP", true);
        }
        if (_textures[to_base(TextureUsage::METALNESS)] != nullptr) {
            shaderName += ".M";
            fragDefines.emplace_back("USE_METALLIC_MAP", true);
        }
        if (_textures[to_base(TextureUsage::ROUGHNESS)] != nullptr) {
            shaderName += ".R";
            fragDefines.emplace_back("USE_ROUGHNESS_MAP", true);
        }
        if (_textures[to_base(TextureUsage::OCCLUSION)] != nullptr) {
            shaderName += ".O";
            fragDefines.emplace_back("USE_OCCLUSION_MAP", true);
        }
        if (_textures[to_base(TextureUsage::EMISSIVE)] != nullptr) {
            shaderName += ".E";
            fragDefines.emplace_back("USE_EMISSIVE_MAP", true);
        }
    }

    updateTransparency();
    if (hasTransparency()) {
        shaderName += ".T";
        fragDefines.emplace_back("HAS_TRANSPARENCY", true);

        if (renderStagePass._passType != RenderPassType::OIT_PASS) {
            shaderName += ".AD";
            fragDefines.emplace_back("USE_ALPHA_DISCARD", true);
        }

        switch (_translucencySource) {
            case TranslucencySource::OPACITY_MAP_A:
            case TranslucencySource::OPACITY_MAP_R: {
                fragDefines.emplace_back("USE_OPACITY_MAP", true);
                if (_translucencySource == TranslucencySource::OPACITY_MAP_R) {
                    shaderName += ".OMapR";
                    fragDefines.emplace_back("USE_OPACITY_MAP_RED_CHANNEL", true);
                } else {
                    shaderName += ".OMapA";
                }
            } break;
            case TranslucencySource::ALBEDO_COLOUR: {
                shaderName += ".ACAlpha";
                fragDefines.emplace_back("USE_ALBEDO_COLOUR_ALPHA", true);
            } break; 
            
            case TranslucencySource::ALBEDO_TEX: {
                shaderName += ".ATAlpha";
                fragDefines.emplace_back("USE_ALBEDO_TEX_ALPHA", true);
            } break;
            default: break;
        };
    }

    if (doubleSided()) {
        shaderName += ".2Sided";
        fragDefines.emplace_back("USE_DOUBLE_SIDED", true);
    }

    if (!receivesShadows()) {
        shaderName += ".NSHDW";
        fragDefines.emplace_back("DISABLE_SHADOW_MAPPING", true);
    } else {
        ProcessShadowMappingDefines(_parentCache->context().config(), fragDefines);
    }

    if (!_isStatic) {
        shaderName += ".D";
        globalDefines.emplace_back("NODE_DYNAMIC", true);
    } else {
        shaderName += ".S";
        globalDefines.emplace_back("NODE_STATIC", true);
    }

    if (usePlanarReflections()) {
        shaderName += ".PRefl";
        globalDefines.emplace_back("USE_PLANAR_REFLECTION", true);
    }

    if (usePlanarRefractions()) {
        shaderName += ".PRefr";
        globalDefines.emplace_back("USE_PLANAR_REFRACTION", true);
    }

    if (!isDepthPass) {
        switch (_shadingMode) {
            default:
            case ShadingMode::FLAT: {
                fragDefines.emplace_back("USE_SHADING_FLAT", true);
                shaderName += ".Flat";
            } break;
            case ShadingMode::PHONG:
            case ShadingMode::BLINN_PHONG: {
                fragDefines.emplace_back("USE_SHADING_BLINN_PHONG", true);
                shaderName += ".Phong";
            } break;
            case ShadingMode::TOON: {
                fragDefines.emplace_back("USE_SHADING_TOON", true);
                shaderName += ".Toon";
            } break;
            case ShadingMode::OREN_NAYAR: {
                fragDefines.emplace_back("USE_SHADING_OREN_NAYAR", true);
                shaderName += ".OrenN";
            } break;
            case ShadingMode::COOK_TORRANCE: {
                fragDefines.emplace_back("USE_SHADING_COOK_TORRANCE", true);
                shaderName += ".CookT";
            } break;
        }
    }

    if (_hardwareSkinning) {
        globalDefines.emplace_back("USE_GPU_SKINNING", true);
        shaderName += ".Sknd";
    }

    if (_usePackedOMR) {
        globalDefines.emplace_back("SAMPLER_OMR_COMPACT", true);
        shaderName += ".OMRCompact";
    }

    vertDefines.emplace_back("HAS_CLIPPING_OUT", true);

    globalDefines.emplace_back("DEFINE_PLACEHOLDER", false);

    vertDefines.insert(eastl::cend(vertDefines), eastl::cbegin(globalDefines), eastl::cend(globalDefines));
    fragDefines.insert(eastl::cend(fragDefines), eastl::cbegin(globalDefines), eastl::cend(globalDefines));

    shaderName.append(
        Util::StringFormat("_%zu_%zu",
                           ShaderProgram::DefinesHash(vertDefines),
                           ShaderProgram::DefinesHash(fragDefines))
    );

    ShaderModuleDescriptor vertModule = {};
    vertModule._variant = vertVariant;
    vertModule._sourceFile = (vertSource + ".glsl").c_str();
    vertModule._batchSameFile = false;
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._defines = vertDefines;

    ShaderProgramDescriptor shaderDescriptor = {};
    if (!isDepthPass ||       // Normal colour pass
        hasTransparency() ||  // Has transparency and may need alpha_discard in the PrePass or ShadowPass
        isShadowPass ||       // Is a shadow pass
        !_isStatic ||         // Is a depth pass but with a dynamic node, so output velocity vector
        _hardwareSkinning)    // Node is animated, so we need velocity output
    {
        ShaderModuleDescriptor fragModule = {};
        fragModule._variant = fragVariant;
        fragModule._sourceFile = (fragSource + ".glsl").c_str();
        fragModule._moduleType = ShaderType::FRAGMENT;
        fragModule._defines = fragDefines;

        shaderDescriptor._modules.push_back(fragModule);
    }

    shaderDescriptor._modules.push_back(vertModule);

    ResourceDescriptor shaderResDescriptor(shaderName);
    shaderResDescriptor.propertyDescriptor(shaderDescriptor);
    shaderResDescriptor.flag(true);

    setShaderProgramInternal(shaderResDescriptor, renderStagePass, false);
}

bool Material::getTextureData(const RenderStagePass& renderStagePass, TextureDataContainer& textureData) {
    OPTICK_EVENT();
    if (_useBindlessTextures) {
        return true;
    }

    const auto registerTexture = [this](const U8 slot, const bool condition, TextureDataContainer& textureData) {
        if (condition) {
            const Texture_ptr& crtTexture = _textures[slot];
            if (crtTexture != nullptr) {
                // We only need to actually bind NON-RESIDENT textures. 
                textureData.add(TextureEntry{ crtTexture->data(), _samplers[slot], slot });
                return true;
            }
        }

        return false;
    };

    bool ret = false;
    const bool depthStage = renderStagePass.isDepthPass();
    const bool transparencyState = hasTransparency() && _translucencySource != TranslucencySource::ALBEDO_COLOUR;

    SharedLock<SharedMutex> r_lock(_textureLock);
    for (const U8 slot : g_TransparentSlots) {
        ret = registerTexture(slot, (transparencyState || !depthStage || _textureUseForDepth[slot]), textureData) || ret;
    }

    for (const U8 slot : g_ExtraSlots) {
        ret = registerTexture(slot, (!depthStage || _textureUseForDepth[slot]), textureData) || ret;
    }

    return ret;
}

bool Material::unload() {
    _textures.fill(nullptr);

    static ShaderProgramInfo defaultShaderInfo = {};

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        ShaderVariantsPerPass& passMapShaders = _shaderInfo[s];
        StateVariantsPerPass& passMapStates = _defaultRenderStates[s];
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            passMapShaders[p].fill(defaultShaderInfo);
            passMapStates[p].fill(g_invalidStateHash);
        }
    }
    
    if (_baseMaterial != nullptr) {
        erase_if(_baseMaterial->_instances,
                 [guid = getGUID()](Material* instance) {
                     return instance->getGUID() == guid;
                 });
    }

    for (Material* instance : _instances) {
        instance->_baseMaterial = nullptr;
    }

    return true;
}

void Material::refractive(const bool state, const bool applyToInstances) {
    if (_isRefractive != state) {
        _isRefractive = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->refractive(state, true);
        }
    }
}

void Material::doubleSided(const bool state, const bool applyToInstances) {
    if (_doubleSided != state) {
        _doubleSided = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->doubleSided(state, true);
        }
    }
}

void Material::receivesShadows(const bool state, const bool applyToInstances) {
    if (_receivesShadows != state) {
        _receivesShadows = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->receivesShadows(state, true);
        }
    }
}

void Material::isStatic(const bool state, const bool applyToInstances) {
    if (_isStatic != state) {
        _isStatic = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->isStatic(state, true);
        }
    }
}

void Material::ignoreTexDiffuseAlpha(const bool state, const bool applyToInstances) {
    if (_ignoreTexDiffuseAlpha != state) {
        _ignoreTexDiffuseAlpha = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->ignoreTexDiffuseAlpha(state, true);
        }
    }
}

void Material::usePlanarReflections(const bool state, const bool applyToInstances) {
    if (_usePlanarReflections != state) {
        _usePlanarReflections = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->usePlanarReflections(state, true);
        }
    }
}

void Material::usePlanarRefractions(const bool state, const bool applyToInstances) {
    if (_usePlanarRefractions != state) {
        _usePlanarRefractions = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->usePlanarRefractions(state, true);
        }
    }
}

void Material::hardwareSkinning(const bool state, const bool applyToInstances) {
    if (_hardwareSkinning != state) {
        _hardwareSkinning = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->hardwareSkinning(state, true);
        }
    }
}

void Material::textureUseForDepth(const TextureUsage slot, const bool state, const bool applyToInstances) {
    _textureUseForDepth[to_base(slot)] = state;

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->textureUseForDepth(slot, state, true);
        }
    }
}

void Material::setShaderProgram(const ShaderProgram_ptr& shader, const RenderStage stage, const RenderPassType pass, const U8 variant) {
    assert(variant == g_AllVariantsID || variant < g_maxVariantsPerPass);

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            const RenderStage crtStage = static_cast<RenderStage>(s);
            const RenderPassType crtPass = static_cast<RenderPassType>(p);
            if ((stage == RenderStage::COUNT || stage == crtStage) && (pass == RenderPassType::COUNT || pass == crtPass)) {
                if (variant == g_AllVariantsID) {
                    for (U8 i = 0u; i < g_maxVariantsPerPass; ++i) {
                        ShaderProgramInfo& shaderInfo = _shaderInfo[s][p][i];
                        shaderInfo._customShader = true;
                        shaderInfo._shaderCompStage = ShaderBuildStage::COUNT;
                        setShaderProgramInternal(shader, shaderInfo, crtStage, crtPass, variant);
                    }
                } else {
                    ShaderProgramInfo& shaderInfo = _shaderInfo[s][p][variant];
                    shaderInfo._customShader = true;
                    shaderInfo._shaderCompStage = ShaderBuildStage::COUNT;
                    setShaderProgramInternal(shader, shaderInfo, crtStage, crtPass, variant);
                }
            }
        }
    }
}

void Material::setRenderStateBlock(const size_t renderStateBlockHash, const RenderStage stage, const RenderPassType pass, const U8 variant) {
    assert(variant == g_AllVariantsID || variant < g_maxVariantsPerPass);

    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            const RenderStage crtStage = static_cast<RenderStage>(s);
            const RenderPassType crtPass = static_cast<RenderPassType>(p);
            if ((stage == RenderStage::COUNT || stage == crtStage) && (pass == RenderPassType::COUNT || pass == crtPass)) {
                if (variant == g_AllVariantsID) {
                    _defaultRenderStates[s][p].fill(renderStateBlockHash);
                } else {
                    _defaultRenderStates[s][p][variant] = renderStateBlockHash;
                }
            }
        }
    }
}

void Material::toggleTransparency(const bool state, const bool applyToInstances) {
    if (_transparencyEnabled != state) {
        _transparencyEnabled = state;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->toggleTransparency(state, true);
        }
    }
}

void Material::baseColour(const FColour4& colour, const bool applyToInstances) {
    _baseColour = colour;
    updateTransparency();

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->baseColour(colour, true);
        }
    }
}

void Material::emissiveColour(const FColour3& colour, const bool applyToInstances) {
    _emissive = colour;

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->emissiveColour(colour, true);
        }
    }
}

void Material::ambientColour(const FColour3& colour, const bool applyToInstances) {
    _ambient = colour;

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->ambientColour(colour, true);
        }
    }
}

void Material::metallic(const F32 value, const bool applyToInstances) {
    _metallic = CLAMPED_01(value);

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->metallic(value, true);
        }
    }
}

void Material::roughness(const F32 value, const bool applyToInstances) {
    _roughness = CLAMPED_01(value);

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->roughness(value, true);
        }
    }
}

void Material::occlusion(const F32 value, const bool applyToInstances) {
    _occlusion = CLAMPED_01(value);

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->occlusion(value, true);
        }
    }
}

void Material::specularColour(const FColour3& value, const bool applyToInstances) {
    _specular.rgb = value;

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->specularColour(value, true);
        }
    }
}

void Material::shininess(const F32 value, const bool applyToInstances) {
    _specular.a = value;

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->shininess(value, true);
        }
    }
}

void Material::parallaxFactor(const F32 factor, const bool applyToInstances) {
    _parallaxFactor = CLAMPED_01(factor);

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->parallaxFactor(factor, true);
        }
    }
}

void Material::bumpMethod(const BumpMethod newBumpMethod, const bool applyToInstances) {
    if (_bumpMethod != newBumpMethod) {
        _bumpMethod = newBumpMethod;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->bumpMethod(newBumpMethod, true);
        }
    }
}

void Material::shadingMode(const ShadingMode mode, const bool applyToInstances) {
    if (_shadingMode != mode) {
        _shadingMode = mode;
        _needsNewShader = true;
    }

    if (applyToInstances) {
        for (Material* instance : _instances) {
            instance->shadingMode(mode, true);
        }
    }
}

void Material::updateTransparency() {
    if (!_transparencyEnabled) {
        return;
    }

    const TranslucencySource oldSource = _translucencySource;
    _translucencySource = TranslucencySource::COUNT;

    // In order of importance (less to more)!
    // diffuse channel alpha
    if (baseColour().a < 0.95f && _textureOperations[to_base(TextureUsage::UNIT0)] != TextureOperation::REPLACE) {
        _translucencySource = TranslucencySource::ALBEDO_COLOUR;
    }

    // base texture is translucent
    const Texture_ptr& albedo = _textures[to_base(TextureUsage::UNIT0)];
    if (albedo && albedo->hasTransparency() && !_ignoreTexDiffuseAlpha) {
        _translucencySource = TranslucencySource::ALBEDO_TEX;
    }

    // opacity map
    const Texture_ptr& opacity = _textures[to_base(TextureUsage::OPACITY)];
    if (opacity && opacity->hasTransparency()) {
        const U8 channelCount = NumChannels(opacity->descriptor().baseFormat());
        DIVIDE_ASSERT(channelCount == 1 || channelCount == 4, "Material::updateTranslucency: Opacity textures must be either single-channel or RGBA!");

        _translucencySource = channelCount == 4 ? TranslucencySource::OPACITY_MAP_A : TranslucencySource::OPACITY_MAP_R;
    }

    _needsNewShader = oldSource != _translucencySource;
}

size_t Material::getRenderStateBlock(const RenderStagePass& renderStagePass) const {
    const StatesPerVariant& variantMap = _defaultRenderStates[to_base(renderStagePass._stage)][to_base(renderStagePass._passType)];

    assert(renderStagePass._variant < g_maxVariantsPerPass);

    size_t ret = variantMap[renderStagePass._variant];
    // If we haven't defined a state for this variant, use the default one
    if (ret == g_invalidStateHash) {
        ret = variantMap[0u];
    }

    if (ret != g_invalidStateHash) {
        // We don't need to update cull params for shadow mapping unless this is a directional light
        // since CSM splits cause all sorts of errors
        if (_doubleSided) {
            RenderStateBlock tempBlock = RenderStateBlock::get(ret);
            tempBlock.setCullMode(CullMode::NONE);
            ret = tempBlock.getHash();
        }
    }

    return ret;
}

void Material::getSortKeys(const RenderStagePass& renderStagePass, I64& shaderKey, I32& textureKey) const {
    shaderKey = shaderInfo(renderStagePass)._shaderKeyCache;
    textureKey = _textureKeyCache;
}

FColour4 Material::getBaseColour(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::UNIT0)] != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::UNIT0)].get();
    }
    return baseColour();
}

FColour3 Material::getEmissive(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::EMISSIVE)] != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::EMISSIVE)].get();
    }

    return emissive();
}

FColour3 Material::getAmbient(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = false;

    return ambient();
}

FColour4 Material::getSpecular(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::SPECULAR)] != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::SPECULAR)].get();
    }
    return specular();
}

F32 Material::getMetallic(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::METALNESS)] != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::METALNESS)].get();
    }
    return metallic();
}

F32 Material::getRoughness(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::ROUGHNESS)] != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::ROUGHNESS)].get();
    }
    return roughness();
}

F32 Material::getOcclusion(bool& hasTextureOverride, Texture*& textureOut) const noexcept {
    textureOut = nullptr;
    hasTextureOverride = _textures[to_base(TextureUsage::OCCLUSION)] != nullptr;
    if (hasTextureOverride) {
        textureOut = _textures[to_base(TextureUsage::OCCLUSION)].get();
    }
    return occlusion();
}

void Material::getData(const RenderingComponent& parentComp, const U32 bestProbeID, NodeMaterialData& dataOut, NodeMaterialTextures& texturesOut) {
    for (U8 i = 0u; i < MATERIAL_TEXTURE_COUNT; ++i) {
        texturesOut[i] = _textureAddresses[to_base(g_materialTextures[i])];
    }

    SceneGraphNode* parentSGN = parentComp.getSGN();

    F32 selectionFlag = 0.0f;
    if (parentSGN->hasFlag(SceneGraphNode::Flags::HOVERED)) {
        selectionFlag = 0.5f;
    }
    // We don't propagate selection flags to children outside of the editor, so check for that
    if (parentSGN->hasFlag(SceneGraphNode::Flags::SELECTED) ||
        parentSGN->parent() && parentSGN->parent()->hasFlag(SceneGraphNode::Flags::SELECTED)) {
        if_constexpr(Config::Build::ENABLE_EDITOR) {
            const Editor& editor = _context.parent().platformContext().editor();
            selectionFlag = (!editor.running() || editor.showEmissiveSelections()) ? 1.0f : selectionFlag;
        } else {
            selectionFlag = 1.0f;
        }
    }

    const FColour4& specColour = specular(); //< For PHONG_SPECULAR

    //ToDo: Maybe store all of these material properties in an internal, cached, NodeMaterialData structure? -Ionut
    dataOut._albedo.set(baseColour());
    dataOut._colourData.set(ambient(), specColour.a);
    dataOut._emissiveAndParallax.set(emissive(), parallaxFactor());
    dataOut._data.x = Util::PACK_UNORM4x8(occlusion(), metallic(), roughness(), selectionFlag);
    dataOut._data.y = Util::PACK_UNORM4x8(specColour.r, specColour.g, specColour.b, 1.f);
    dataOut._data.z = Util::PACK_UNORM4x8(to_U8(_textureOperations[to_base(TextureUsage::UNIT0)]),
                                          to_U8(_textureOperations[to_base(TextureUsage::UNIT1)]),
                                          to_U8(_textureOperations[to_base(TextureUsage::SPECULAR)]),
                                          to_U8(bumpMethod()));
    dataOut._data.w = bestProbeID;
}

void Material::rebuild() {
    recomputeShaders();

    // Alternatively we could just copy the maps directly
    bool skipped = false;
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            ShaderPerVariant& shaders = _shaderInfo[s][p];
            for (ShaderProgramInfo& info : shaders) {
                if (info._shaderRef != nullptr && info._shaderRef->getState() == ResourceState::RES_LOADED) {
                    info._shaderRef->recompile(true, skipped);
                }
            }
        }
    }
}

void Material::saveToXML(const string& entryName, boost::property_tree::ptree& pt) const {
    pt.put(entryName + ".version", g_materialXMLVersion);

    pt.put(entryName + ".shadingMode", TypeUtil::ShadingModeToString(shadingMode()));

    pt.put(entryName + ".colour.<xmlattr>.r", baseColour().r);
    pt.put(entryName + ".colour.<xmlattr>.g", baseColour().g);
    pt.put(entryName + ".colour.<xmlattr>.b", baseColour().b);
    pt.put(entryName + ".colour.<xmlattr>.a", baseColour().a);

    pt.put(entryName + ".emissive.<xmlattr>.r", emissive().r);
    pt.put(entryName + ".emissive.<xmlattr>.g", emissive().g);
    pt.put(entryName + ".emissive.<xmlattr>.b", emissive().b);

    pt.put(entryName + ".ambient.<xmlattr>.r", ambient().r);
    pt.put(entryName + ".ambient.<xmlattr>.g", ambient().g);
    pt.put(entryName + ".ambient.<xmlattr>.b", ambient().b);

    pt.put(entryName + ".specular.<xmlattr>.r", specular().r);
    pt.put(entryName + ".specular.<xmlattr>.g", specular().g);
    pt.put(entryName + ".specular.<xmlattr>.b", specular().b);
    pt.put(entryName + ".specular.<xmlattr>.a", specular().a);

    pt.put(entryName + ".metallic", metallic());

    pt.put(entryName + ".roughness", roughness());

    pt.put(entryName + ".doubleSided", doubleSided());

    pt.put(entryName + ".receivesShadows", receivesShadows());

    pt.put(entryName + ".ignoreTexDiffuseAlpha", ignoreTexDiffuseAlpha());

    pt.put(entryName + ".bumpMethod", TypeUtil::BumpMethodToString(bumpMethod()));

    pt.put(entryName + ".parallaxFactor", parallaxFactor());

    pt.put(entryName + ".transparencyEnabled", transparencyEnabled());

    pt.put(entryName + ".isRefractive", _isRefractive);

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

    shadingMode(TypeUtil::StringToShadingMode(pt.get<string>(entryName + ".shadingMode", TypeUtil::ShadingModeToString(shadingMode()))), false);

    baseColour(FColour4(pt.get<F32>(entryName + ".colour.<xmlattr>.r", baseColour().r),
                        pt.get<F32>(entryName + ".colour.<xmlattr>.g", baseColour().g),
                        pt.get<F32>(entryName + ".colour.<xmlattr>.b", baseColour().b),
                        pt.get<F32>(entryName + ".colour.<xmlattr>.a", baseColour().a)), false);
                          
    emissiveColour(FColour3(pt.get<F32>(entryName + ".emissive.<xmlattr>.r", emissive().r),
                            pt.get<F32>(entryName + ".emissive.<xmlattr>.g", emissive().g),
                            pt.get<F32>(entryName + ".emissive.<xmlattr>.b", emissive().b)), false);  
    
    ambientColour(FColour3(pt.get<F32>(entryName + ".ambient.<xmlattr>.r", ambient().r),
                           pt.get<F32>(entryName + ".ambient.<xmlattr>.g", ambient().g),
                           pt.get<F32>(entryName + ".ambient.<xmlattr>.b", ambient().b)), false);

    specularColour(FColour4(pt.get<F32>(entryName + ".specular.<xmlattr>.r", specular().r),
                            pt.get<F32>(entryName + ".specular.<xmlattr>.g", specular().g),
                            pt.get<F32>(entryName + ".specular.<xmlattr>.b", specular().b),
                            pt.get<F32>(entryName + ".specular.<xmlattr>.a", specular().a)), false);

    metallic(pt.get<F32>(entryName + ".metallic", metallic()), false);

    roughness(pt.get<F32>(entryName + ".roughness", roughness()), false);

    doubleSided(pt.get<bool>(entryName + ".doubleSided", doubleSided()), false);

    receivesShadows(pt.get<bool>(entryName + ".receivesShadows", receivesShadows()), false);

    ignoreTexDiffuseAlpha(pt.get<bool>(entryName + ".ignoreTexDiffuseAlpha", ignoreTexDiffuseAlpha()), false);

    bumpMethod(TypeUtil::StringToBumpMethod(pt.get<string>(entryName + ".bumpMethod", TypeUtil::BumpMethodToString(bumpMethod()))), false);

    parallaxFactor(pt.get<F32>(entryName + ".parallaxFactor", parallaxFactor()), false);

    toggleTransparency(pt.get<bool>(entryName + ".transparencyEnabled", transparencyEnabled()), false);

    refractive(pt.get<bool>(entryName + ".isRefractive", refractive()), false);

    loadRenderStatesFromXML(entryName, pt);
    loadTextureDataFromXML(entryName, pt);
}

void Material::saveRenderStatesToXML(const string& entryName, boost::property_tree::ptree& pt) const {
    hashMap<size_t, U32> previousHashValues;

    U32 blockIndex = 0u;
    for (U8 s = 0u; s < to_U8(RenderStage::COUNT); ++s) {
        for (U8 p = 0u; p < to_U8(RenderPassType::COUNT); ++p) {
            for (U8 v = 0u; v < g_maxVariantsPerPass; ++v) {
                // we could just use _defaultRenderStates[s][p][v] for a direct lookup, but this handles the odd double-sided / no cull case
                const size_t stateHash = getRenderStateBlock(
                    RenderStagePass{
                        static_cast<RenderStage>(s),
                        static_cast<RenderPassType>(p),
                        v
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
            for (U8 v = 0u; v < g_maxVariantsPerPass; ++v) {
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
            pt.put(textureNode + ".flipped", texture->flipped());
            pt.put(textureNode + ".usage", TypeUtil::TextureOperationToString(_textureOperations[to_base(usage)]));

            const size_t samplerHash = _samplers[to_base(usage)];

            if (previousHashValues.find(samplerHash) == std::cend(previousHashValues)) {
                samplerCount++;
                XMLParser::saveToXML(SamplerDescriptor::get(samplerHash), Util::StringFormat("%s.SamplerDescriptors.%u", entryName.c_str(), samplerCount), pt);
                previousHashValues[samplerHash] = samplerCount;
            }
            pt.put(textureNode + ".Sampler.id", previousHashValues[samplerHash]);
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
            const bool flipped = pt.get(textureNode + ".flipped", false);

            if (!texName.empty()) {
                const U32 index = pt.get<U32>(textureNode + ".Sampler.id", 0);
                const auto& it = previousHashValues.find(index);

                size_t hash;
                if (it != cend(previousHashValues)) {
                    hash = it->second;
                } else {
                     hash = XMLParser::loadFromXML(Util::StringFormat("%s.SamplerDescriptors.%u", entryName.c_str(), index), pt);
                     previousHashValues[index] = hash;
                }

                if (_samplers[to_base(usage)] != hash) {
                    setSampler(usage, hash);
                }

                TextureOperation op = TextureOperation::NONE;
                _textureOperations[to_base(usage)] = TypeUtil::StringToTextureOperation(pt.get<string>(textureNode + ".usage", TypeUtil::TextureOperationToString(_textureOperations[to_base(usage)])));

                {
                    ScopedLock<SharedMutex> w_lock(_textureLock);
                    const Texture_ptr& crtTex = _textures[to_base(usage)];
                    if (crtTex != nullptr &&
                        crtTex->flipped() == flipped &&
                        crtTex->assetLocation() + crtTex->assetName() == texPath + texName)
                    {
                      continue;
                    }
                }

                TextureDescriptor texDesc(TextureType::TEXTURE_2D);
                ResourceDescriptor texture(texName.str());
                texture.assetName(texName);
                texture.assetLocation(texPath);
                texture.propertyDescriptor(texDesc);
                texture.waitForReady(true);
                texture.flag(!flipped);

                Texture_ptr tex = CreateResource<Texture>(_context.parent().resourceCache(), texture);
                setTexture(usage, tex, hash, op);
            }
        }
    }
}

};