

#include "Headers/Material.h"
#include "Headers/ShaderComputeQueue.h"

#include "Managers/Headers/ProjectManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Utility/Headers/Localization.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/Kernel.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "ECS/Components/Headers/RenderingComponent.h"
#include "Editor/Headers/Editor.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"

namespace Divide
{

    namespace
    {
        constexpr size_t g_materialXMLVersion = 1u;
    };

    namespace TypeUtil
    {
        const char* MaterialDebugFlagToString( const MaterialDebugFlag materialFlag ) noexcept
        {
            return Names::materialDebugFlag[to_base( materialFlag )];
        }

        MaterialDebugFlag StringToMaterialDebugFlag( const std::string_view name )
        {
            for ( U8 i = 0; i < to_U8( MaterialDebugFlag::COUNT ); ++i )
            {
                if ( name == Names::materialDebugFlag[i] )
                {
                    return static_cast<MaterialDebugFlag>(i);
                }
            }

            return MaterialDebugFlag::COUNT;
        }

        const char* TextureSlotToString( const TextureSlot texUsage ) noexcept
        {
            return Names::textureSlot[to_base( texUsage )];
        }

        TextureSlot StringToTextureSlot( const std::string_view name )
        {
            for ( U8 i = 0; i < to_U8( TextureSlot::COUNT ); ++i )
            {
                if (  name == Names::textureSlot[i] )
                {
                    return static_cast<TextureSlot>(i);
                }
            }

            return TextureSlot::COUNT;
        }

        const char* BumpMethodToString( const BumpMethod bumpMethod ) noexcept
        {
            return Names::bumpMethod[to_base( bumpMethod )];
        }

        BumpMethod StringToBumpMethod( const std::string_view name )
        {
            for ( U8 i = 0; i < to_U8( BumpMethod::COUNT ); ++i )
            {
                if ( name == Names::bumpMethod[i] )
                {
                    return static_cast<BumpMethod>(i);
                }
            }

            return BumpMethod::COUNT;
        }

        const char* ShadingModeToString( const ShadingMode shadingMode ) noexcept
        {
            return Names::shadingMode[to_base( shadingMode )];
        }

        ShadingMode StringToShadingMode( const std::string_view name )
        {
            for ( U8 i = 0; i < to_U8( ShadingMode::COUNT ); ++i )
            {
                if ( name == Names::shadingMode[i] )
                {
                    return static_cast<ShadingMode>(i);
                }
            }

            return ShadingMode::COUNT;
        }

        const char* SkinningModeToString(const SkinningMode skinningMode) noexcept
        {
            return Names::skinningMode[to_base( skinningMode )];
        }

        SkinningMode StringToSkinningMode(const std::string_view name)
        {
            for (U8 i = 0; i < to_U8(SkinningMode::COUNT); ++i)
            {
                if (name == Names::skinningMode[i])
                {
                    return static_cast<SkinningMode>(i);
                }
            }

            return SkinningMode::COUNT;
        }

        const char* TextureOperationToString( const TextureOperation textureOp ) noexcept
        {
            return Names::textureOperation[to_base( textureOp )];
        }

        TextureOperation StringToTextureOperation( const std::string_view operation )
        {
            for ( U8 i = 0; i < to_U8( TextureOperation::COUNT ); ++i )
            {
                if ( operation == Names::textureOperation[i] )
                {
                    return static_cast<TextureOperation>(i);
                }
            }

            return TextureOperation::COUNT;
        }

        const char* ReflectorTypeToString(const ReflectorType reflectorType) noexcept
        {
            return Names::reflectorType[to_base( reflectorType )];
        }

        ReflectorType StringToReflectorType(std::string_view name)
        {
            for (U8 i = 0; i < to_U8(ReflectorType::COUNT); ++i)
            {
                if (name == Names::reflectorType[i])
                {
                    return static_cast<ReflectorType>(i);
                }
            }

            return ReflectorType::COUNT;
        }

        const char* RefractorTypeToString(const RefractorType refractorType) noexcept
        {
            return Names::refractorType[to_base(refractorType)];
        }

        RefractorType StringToRefractorType(std::string_view name)
        {
            for (U8 i = 0; i < to_U8(RefractorType::COUNT); ++i)
            {
                if (name == Names::refractorType[i])
                {
                    return static_cast<RefractorType>(i);
                }
            }

            return RefractorType::COUNT;
        }
    }; //namespace TypeUtil

    bool Material::s_shadersDirty = false;

    void Material::OnStartup()
    {
        NOP();
    }

    void Material::OnShutdown()
    {
        s_shadersDirty = false;
    }

    void Material::RecomputeShaders()
    {
        s_shadersDirty = true;
    }

    void Material::Update( [[maybe_unused]] const U64 deltaTimeUS )
    {
        s_shadersDirty = false;
    }

    Material::Material( const ResourceDescriptor<Material>& descriptor )
        : CachedResource( descriptor, "Material")
    {
        const ShaderProgramInfo defaultShaderInfo = {};
        // Could just direct copy the arrays, but this looks cool
        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            auto& perPassInfo = _shaderInfo[s];
            auto& perPassStates = _defaultRenderStates[s];

            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                perPassInfo[p].fill( defaultShaderInfo );
                perPassStates[p].fill( {{}, false});
            }
        }

        _computeShaderCBK = []( Material* material, const RenderStagePass renderStagePass )
        {

            const bool isDepthPass = IsDepthPass( renderStagePass );
            const bool isZPrePass = isDepthPass && renderStagePass._stage == RenderStage::DISPLAY;
            const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;

            const Str<64> vertSource = isDepthPass ? material->baseShaderData()._depthShaderVertSource : material->baseShaderData()._colourShaderVertSource;
            const Str<64> fragSource = isDepthPass ? material->baseShaderData()._depthShaderFragSource : material->baseShaderData()._colourShaderFragSource;

            Str<32> vertVariant = isDepthPass ? isShadowPass ? material->baseShaderData()._shadowShaderVertVariant
                                                           : material->baseShaderData()._depthShaderVertVariant
                                            : material->baseShaderData()._colourShaderVertVariant;

            Str<32> fragVariant = isDepthPass ? material->baseShaderData()._depthShaderFragVariant
                                            : material->baseShaderData()._colourShaderFragVariant;

            ShaderProgramDescriptor shaderDescriptor{};
            shaderDescriptor._name = Str<256>(vertSource.c_str()) + "_" + fragSource.c_str();

            if ( isShadowPass )
            {
                vertVariant += "Shadow";
                fragVariant += "Shadow.VSM";
            }
            else if ( isDepthPass )
            {
                vertVariant += "PrePass";
                fragVariant += "PrePass";
            }

            ShaderModuleDescriptor vertModule = {};
            vertModule._variant = vertVariant.c_str();
            vertModule._sourceFile = (vertSource + ".glsl").c_str();
            vertModule._moduleType = ShaderType::VERTEX;
            shaderDescriptor._modules.push_back( vertModule );

            if ( !isDepthPass || isZPrePass || isShadowPass || material->hasTransparency() )
            {
                ShaderModuleDescriptor fragModule = {};
                fragModule._variant = fragVariant.c_str();
                fragModule._sourceFile = (fragSource + ".glsl").c_str();
                fragModule._moduleType = ShaderType::FRAGMENT;

                shaderDescriptor._modules.push_back( fragModule );
            }

            return shaderDescriptor;
        };

        _recomputeShadersCBK = []()
        {
            NOP();
        };
    }

    bool Material::load( PlatformContext& context )
    {
        _context = &context.gfx();
        properties().receivesShadows( context.config().rendering.shadowMapping.enabled );

        return CachedResource::load( context );
    }

    bool Material::Clone(Handle<Material> src, Handle<Material>& dst, const std::string_view nameSuffix)
    {
        DIVIDE_ASSERT( !nameSuffix.empty(), "Material error: clone called without a valid name suffix!" );
        DIVIDE_ASSERT(src != INVALID_HANDLE<Material>, "Material error: clone called on an invalid source material!");

        Material* srcMat = Get(src);
        DIVIDE_ASSERT(srcMat != nullptr);

        dst = CreateResource( ResourceDescriptor<Material>(srcMat->resourceName() + nameSuffix ) );
        DIVIDE_ASSERT(dst != INVALID_HANDLE<Material>);

        ResourcePtr<Material> cloneMat = Get(dst);

        cloneMat->_baseMaterial = src;

        cloneMat->_properties = srcMat->_properties;
        cloneMat->_extraShaderDefines = srcMat->_extraShaderDefines;
        cloneMat->_computeShaderCBK = srcMat->_computeShaderCBK;
        cloneMat->_computeRenderStateCBK = srcMat->_computeRenderStateCBK;
        cloneMat->_defaultRenderStates = srcMat->_defaultRenderStates;
        cloneMat->_topology = srcMat->_topology;
        cloneMat->_shaderAttributes = srcMat->_shaderAttributes;
        cloneMat->_shaderAttributesHash = srcMat->_shaderAttributesHash;

        cloneMat->ignoreXMLData(srcMat->ignoreXMLData() );
        cloneMat->updatePriorirty(srcMat->updatePriorirty() );

        for ( U8 i = 0u; i < to_U8(srcMat->_textures.size() ); ++i )
        {
            const TextureInfo& texInfo = srcMat->_textures[i];
            if ( texInfo._ptr != INVALID_HANDLE<Texture> )
            {
                const Handle<Texture> cloneTex = GetResourceRef( texInfo._ptr );
                cloneMat->setTexture(
                    static_cast<TextureSlot>(i),
                    cloneTex,
                    texInfo._sampler,
                    texInfo._operation,
                    texInfo._useInGeometryPasses );
            }
        }

        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                const auto& variantMapSrc = srcMat->_shaderInfo[s][p];
                      auto& variantMapDst = cloneMat->_shaderInfo[s][p];
                for ( U8 v = 0u; v < to_U8( RenderStagePass::VariantType::COUNT ); ++v )
                {
                    variantMapDst[v] = variantMapSrc[v].clone();
                }
            }
        }

        LockGuard<SharedMutex> w_lock(srcMat->_instanceLock );
        srcMat->_instances.emplace_back(dst);

        return true;
    }

    U32 Material::update( [[maybe_unused]] const U64 deltaTimeUS )
    {
        U32 ret = to_U32( MaterialUpdateResult::OK );

        if ( properties()._transparencyUpdated )
        {
            ret |= to_base( MaterialUpdateResult::NEW_TRANSPARENCY );
            updateTransparency();
            properties()._transparencyUpdated = false;
        }
        if ( properties()._cullUpdated )
        {
            ret |= to_base( MaterialUpdateResult::NEW_CULL );
            properties()._cullUpdated = false;
        }
        if ( properties()._needsNewShader || s_shadersDirty )
        {
            recomputeShaders();
            properties()._needsNewShader = false;
            ret |= to_base( MaterialUpdateResult::NEW_SHADER );
        }

        return ret;
    }

    void Material::clearRenderStates()
    {
        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            auto& perPassStates = _defaultRenderStates[s];
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                perPassStates[p].fill({{}, false});
            }
        }
    }

    void Material::updateCullState()
    {
        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            auto& perPassStates = _defaultRenderStates[s];
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                for ( auto& state : perPassStates[p] )
                {
                    if ( state._isSet )
                    {
                        state._block._cullMode = ( properties().doubleSided() ? CullMode::NONE : CullMode::BACK );
                    }
                }
            }
        }
    }

    void Material::setPipelineLayout( const PrimitiveTopology topology, const AttributeMap& shaderAttributes )
    {
        if ( topology != _topology )
        {
            _topology = topology;
            properties()._needsNewShader = true;
        }

        const size_t newHash = GetHash( shaderAttributes );
        if ( _shaderAttributesHash == 0u || _shaderAttributesHash != newHash )
        {
            _shaderAttributes = shaderAttributes;
            _shaderAttributesHash = newHash;
            properties()._needsNewShader = true;
        }
    }

    bool Material::setSampler( const TextureSlot textureUsageSlot, const SamplerDescriptor sampler )
    {
        _textures[to_U32( textureUsageSlot )]._sampler = sampler;

        return true;
    }

    bool Material::setTextureLocked( const TextureSlot textureUsageSlot, const ResourceDescriptor<Texture>& texture, const SamplerDescriptor sampler, const TextureOperation op, const bool useInGeometryPasses )
    {
        return setTextureLocked(textureUsageSlot, CreateResource(texture), sampler, op, useInGeometryPasses);
    }

    bool Material::setTextureLocked( const TextureSlot textureUsageSlot, const Handle<Texture> texture, const SamplerDescriptor sampler, const TextureOperation op, const bool useInGeometryPasses )
    {
        // Invalidate our descriptor sets
        _descriptorSetMainPass._bindingCount = {0u};
        _descriptorSetSecondaryPass._bindingCount = { 0u };
        _descriptorSetPrePass._bindingCount = { 0u };
        _descriptorSetShadow._bindingCount = { 0u };

        const U32 slot = to_U32( textureUsageSlot );

        TextureInfo& texInfo = _textures[slot];

        setSampler( textureUsageSlot, sampler );

        setTextureOperation( textureUsageSlot, texture != INVALID_HANDLE<Texture> ? op : TextureOperation::NONE );

        if ( texInfo._ptr != INVALID_HANDLE<Texture> )
        {
            // Skip adding same texture
            if ( texture != INVALID_HANDLE<Texture> && texInfo._ptr == texture )
            {
                return true;
            }
        }

        texInfo._srgb = texture != INVALID_HANDLE<Texture> ? Get(texture)->descriptor()._packing == GFXImagePacking::NORMALIZED_SRGB : false;
        texInfo._useInGeometryPasses = texture != INVALID_HANDLE<Texture> ? useInGeometryPasses : false;
        texInfo._ptr = texture;

        if ( textureUsageSlot == TextureSlot::METALNESS )
        {
            properties()._usePackedOMR = (texture != INVALID_HANDLE<Texture> && Get(texture)->numChannels() > 2u);
        }

        if ( textureUsageSlot == TextureSlot::UNIT0 ||
             textureUsageSlot == TextureSlot::OPACITY )
        {
            updateTransparency();
        }

        properties()._needsNewShader = true;

        return true;
    }

    bool Material::setTexture( const TextureSlot textureUsageSlot,
                               const ResourceDescriptor<Texture>& texture,
                               const SamplerDescriptor sampler,
                               const TextureOperation op,
                               bool useInGeometryPasses)
    {
        LockGuard<SharedMutex> w_lock( _textureLock );
        return setTextureLocked(textureUsageSlot, texture, sampler, op, useInGeometryPasses);
    }

    bool Material::setTexture( const TextureSlot textureUsageSlot,
                               const Handle<Texture> texture,
                               const SamplerDescriptor sampler,
                               const TextureOperation op,
                               const bool useInGeometryPasses )
    {
        LockGuard<SharedMutex> w_lock( _textureLock );
        return setTextureLocked( textureUsageSlot, texture, sampler, op, useInGeometryPasses );
    }

    void Material::setTextureOperation( const TextureSlot textureUsageSlot, const TextureOperation op )
    {

        TextureOperation& crtOp = _textures[to_base( textureUsageSlot )]._operation;

        if ( crtOp != op )
        {
            crtOp = op;
            properties()._needsNewShader = true;
        }
    }

    void Material::setShaderProgramInternal( ShaderProgramDescriptor shaderDescriptor, const RenderStagePass stagePass )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        computeAndAppendShaderDefines( shaderDescriptor, stagePass );

        ShaderProgramInfo& info = shaderInfo( stagePass );
        // if we already have a different shader assigned ...
        if ( info._shaderRef != INVALID_HANDLE<ShaderProgram> )
        {
            ResourcePtr<ShaderProgram> ptr = Get( info._shaderRef);

            // We cannot replace a shader that is still loading in the background
            WAIT_FOR_CONDITION( ptr->getState() == ResourceState::RES_LOADED );
            if ( ptr->descriptor() != shaderDescriptor )
            {
                Console::printfn( LOCALE_STR( "REPLACE_SHADER" ),
                                  ptr->resourceName().c_str(),
                                  shaderDescriptor._name.c_str(),
                                  TypeUtil::RenderStageToString( stagePass._stage ),
                                  TypeUtil::RenderPassTypeToString( stagePass._passType ),
                                  to_base(stagePass._variant) );
            }
            else
            {
                return;
            }
        }

        ShaderComputeQueue::ShaderQueueElement shaderElement{ &info._shaderRef, shaderDescriptor };
        if ( updatePriorirty() == UpdatePriority::High )
        {
            _context->shaderComputeQueue().process( shaderElement );
            info._shaderCompStage = ShaderBuildStage::COMPUTED;
            DIVIDE_ASSERT( info._shaderRef != INVALID_HANDLE<ShaderProgram> );
            if ( !WaitForReady( Get(info._shaderRef) ) )
            {
                info._shaderCompStage = ShaderBuildStage::FAILED;
            }
        }
        else
        {
            if ( updatePriorirty() == UpdatePriority::Medium )
            {
                _context->shaderComputeQueue().addToQueueFront( shaderElement );
            }
            else
            {
                _context->shaderComputeQueue().addToQueueBack( shaderElement );
            }
            info._shaderCompStage = ShaderBuildStage::QUEUED;
        }
    }

    void Material::recomputeShaders()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                RenderStagePass stagePass{ static_cast<RenderStage>(s), static_cast<RenderPassType>(p) };
                auto& variantMap = _shaderInfo[s][p];

                for ( U8 v = 0u; v < to_U8( RenderStagePass::VariantType::COUNT ); ++v )
                {
                    ShaderProgramInfo& shaderInfo = variantMap[v];
                    if ( shaderInfo._shaderCompStage == ShaderBuildStage::COUNT )
                    {
                        continue;
                    }

                    stagePass._variant = static_cast<RenderStagePass::VariantType>(v);
                    shaderInfo._shaderCompStage = ShaderBuildStage::REQUESTED;
                }
            }
        }

        _recomputeShadersCBK();
    }

    Handle<ShaderProgram> Material::computeAndGetProgramHandle( const RenderStagePass renderStagePass )
    {
        constexpr U8 maxRetries = 250;

        bool justFinishedLoading = false;
        for ( U8 i = 0; i < maxRetries; ++i )
        {
            if ( !canDraw( renderStagePass, justFinishedLoading ) )
            {
                if ( !_context->shaderComputeQueue().stepQueue() )
                {
                    NOP();
                }
            }
            else
            {
                return getProgramHandle( renderStagePass );
            }
        }

        return _context->imShaders()->imWorldShaderNoTexture();
    }

    Handle<ShaderProgram> Material::getProgramHandle( const RenderStagePass renderStagePass ) const
    {

        const ShaderProgramInfo& info = shaderInfo( renderStagePass );

        if ( info._shaderRef != INVALID_HANDLE<ShaderProgram> )
        {
            WAIT_FOR_CONDITION( Get(info._shaderRef)->getState() == ResourceState::RES_LOADED );
            return info._shaderRef;
        }
        DIVIDE_UNEXPECTED_CALL();

        return _context->imShaders()->imWorldShaderNoTexture();
    }

    bool Material::canDraw( const RenderStagePass renderStagePass, bool& shaderJustFinishedLoading )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        shaderJustFinishedLoading = false;
        ShaderProgramInfo& info = shaderInfo( renderStagePass );
        if ( info._shaderCompStage == ShaderBuildStage::FAILED )
        {
            return false;
        }

        if ( info._shaderCompStage == ShaderBuildStage::REQUESTED )
        {
            setShaderProgramInternal( _computeShaderCBK( this, renderStagePass ),
                                      renderStagePass );
        }

        // If we have a shader queued (with a valid ref) ...
        if ( info._shaderCompStage == ShaderBuildStage::QUEUED )
        {
            // ... we are now passed the "compute" stage. We just need to wait for it to load
            if ( info._shaderRef == INVALID_HANDLE<ShaderProgram> )
            {
                // Shader is still in the queue
                return false;
            }
            info._shaderCompStage = ShaderBuildStage::COMPUTED;
        }

        // If the shader is computed ...
        if ( info._shaderCompStage == ShaderBuildStage::COMPUTED )
        {
            assert( info._shaderRef != INVALID_HANDLE<ShaderProgram> );
            // ... wait for the shader to finish loading
            if ( !WaitForReady( Get(info._shaderRef) ) )
            {
                info._shaderCompStage = ShaderBuildStage::FAILED;
            }
            else
            {
                info._shaderCompStage = ShaderBuildStage::READY;
                info._shaderKeyCache = Get(info._shaderRef)->getGUID();
            }
            // Once it has finished loading, it is ready for drawing
            shaderJustFinishedLoading = true;
        }

        // If the shader isn't ready it may have not passed through the computational stage yet (e.g. the first time this method is called)
        if ( info._shaderCompStage != ShaderBuildStage::READY )
        {
            // This is usually the first step in generating a shader: No shader available but we need to render in this stagePass
            if ( info._shaderCompStage == ShaderBuildStage::COUNT )
            {
                // So request a new shader
                info._shaderCompStage = ShaderBuildStage::REQUESTED;
            }

            return false;
        }

        // Shader should be in the ready state
        return true;
    }

    void Material::addShaderDefineInternal(const ShaderType type, const string& define, const bool addPrefix)
    {
        ModuleDefines& defines = _extraShaderDefines[to_base(type)];

        for (const ModuleDefine& it : defines)
        {
            if (it._addPrefix == addPrefix && it._define == define)
            {
                return;
            }
        }

        defines.emplace_back(define, addPrefix);
    }

    void Material::addShaderDefine(const ShaderType type, const string& define, const bool addPrefix)
    {
        if (type != ShaderType::COUNT)
        {
            addShaderDefineInternal(type, define, addPrefix);
        }
        else
        {
            addShaderDefine(define, addPrefix);
        }
    }

    void Material::computeAndAppendShaderDefines( ShaderProgramDescriptor& shaderDescriptor, const RenderStagePass renderStagePass ) const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        const bool isDepthPass = IsDepthPass( renderStagePass );
        const bool isPrePass = renderStagePass._passType == RenderPassType::PRE_PASS;
        const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;
        const bool isWorldAOPass = isShadowPass && renderStagePass._index == ShadowMap::WORLD_AO_LAYER_INDEX;

        DIVIDE_ASSERT( properties().shadingMode() != ShadingMode::COUNT, "Material computeShader error: Invalid shading mode specified!" );
        std::array<ModuleDefines, to_base( ShaderType::COUNT )> moduleDefines = {};

        if ( _context->context().config().rendering.MSAASamples > 0u )
        {
            shaderDescriptor._globalDefines.emplace_back( "MSAA_SCREEN_TARGET", true );
        }

        if ( isShadowPass )
        {
            shaderDescriptor._globalDefines.emplace_back( "SHADOW_PASS", true );
            shaderDescriptor._globalDefines.emplace_back( "SKIP_REFLECT_REFRACT", true );

            if ( isWorldAOPass || to_base( renderStagePass._variant) == to_base( ShadowType::CSM ) )
            {
                shaderDescriptor._globalDefines.emplace_back( "ORTHO_PROJECTION", true );
                if ( isWorldAOPass )
                {
                    shaderDescriptor._globalDefines.emplace_back( "WORLD_AO_PASS", true );
                }
            }
        }
        else if ( isDepthPass )
        {
            shaderDescriptor._globalDefines.emplace_back( "PRE_PASS", true );
            shaderDescriptor._globalDefines.emplace_back( "SKIP_REFLECT_REFRACT", true );
        }
        if ( renderStagePass._stage == RenderStage::REFLECTION && to_base( renderStagePass._variant ) != to_base( ReflectorType::CUBE ) )
        {
            shaderDescriptor._globalDefines.emplace_back( "REFLECTION_PASS", true );
        }
        if ( renderStagePass._stage == RenderStage::DISPLAY )
        {
            shaderDescriptor._globalDefines.emplace_back( "MAIN_DISPLAY_PASS", true );
            if ( !properties().isStatic() )
            {
                shaderDescriptor._globalDefines.emplace_back( "HAS_VELOCITY", true );
            }
        }
        if ( renderStagePass._passType == RenderPassType::OIT_PASS )
        {
            moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "OIT_PASS", true );
        }
        else if ( renderStagePass._passType == RenderPassType::TRANSPARENCY_PASS )
        {
            moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "TRANSPARENCY_PASS", true );
        }
        switch ( properties().shadingMode() )
        {
            case ShadingMode::FLAT:        shaderDescriptor._globalDefines.emplace_back( "SHADING_MODE_FLAT", true );        break;
            case ShadingMode::TOON:        shaderDescriptor._globalDefines.emplace_back( "SHADING_MODE_TOON", true );        break;
            case ShadingMode::BLINN_PHONG: shaderDescriptor._globalDefines.emplace_back( "SHADING_MODE_BLINN_PHONG", true ); break;
            case ShadingMode::PBR_MR:      shaderDescriptor._globalDefines.emplace_back( "SHADING_MODE_PBR_MR", true );      break;
            case ShadingMode::PBR_SG:      shaderDescriptor._globalDefines.emplace_back( "SHADING_MODE_PBR_SG", true );      break;
            default:                       DIVIDE_UNEXPECTED_CALL();                                                         break;
        }
        // Display pre-pass caches normal maps in a GBuffer, so it's the only exception
        if ( (!isDepthPass || renderStagePass._stage == RenderStage::DISPLAY) &&
             _textures[to_base( TextureSlot::NORMALMAP )]._ptr != INVALID_HANDLE<Texture> &&
             properties().bumpMethod() != BumpMethod::NONE )
        {
            // Bump mapping?
            shaderDescriptor._globalDefines.emplace_back( "COMPUTE_TBN", true );
        }

        switch (properties().reflectorType())
        {
            default:
            case ReflectorType::COUNT  : break;
            case ReflectorType::PLANAR : shaderDescriptor._globalDefines.emplace_back("USE_PLANAR_REFLECTOR", true); break;
            case ReflectorType::CUBE   : shaderDescriptor._globalDefines.emplace_back("USE_CUBE_REFLECTOR", true); break;
        }

        switch (properties().refractorType())
        {
            default:
            case RefractorType::COUNT  : break;
            case RefractorType::PLANAR : shaderDescriptor._globalDefines.emplace_back("USE_PLANAR_REFRACTOR", true); break;
            case RefractorType::CUBE   : shaderDescriptor._globalDefines.emplace_back("USE_CUBE_REFRACTOR", true); break;
        }

        switch ( _topology )
        {
            case PrimitiveTopology::POINTS:                   shaderDescriptor._globalDefines.emplace_back( "GEOMETRY_POINTS", true );    break;
            case PrimitiveTopology::LINES:                                                                                                break;
            case PrimitiveTopology::LINE_STRIP:                                                                                           break;
            case PrimitiveTopology::LINES_ADJANCENCY:                                                                                     break;
            case PrimitiveTopology::LINE_STRIP_ADJACENCY:     shaderDescriptor._globalDefines.emplace_back( "GEOMETRY_LINES", true );     break;
            case PrimitiveTopology::TRIANGLES:                                                                                            break;
            case PrimitiveTopology::TRIANGLE_STRIP:                                                                                       break;
            case PrimitiveTopology::TRIANGLE_FAN:                                                                                         break;
            case PrimitiveTopology::TRIANGLES_ADJACENCY:                                                                                  break;
            case PrimitiveTopology::TRIANGLE_STRIP_ADJACENCY: shaderDescriptor._globalDefines.emplace_back( "GEOMETRY_TRIANGLES", true ); break;
            case PrimitiveTopology::PATCH:                    shaderDescriptor._globalDefines.emplace_back( "GEOMETRY_PATCH", true );     break;
            case PrimitiveTopology::MESHLET:                  shaderDescriptor._globalDefines.emplace_back( "GEOMETRY_MESHLET", true );   break;
            default: DIVIDE_UNEXPECTED_CALL();
        }

        for ( U8 i = 0u; i < to_U8( AttribLocation::COUNT ); ++i )
        {
            const AttributeDescriptor& descriptor = _shaderAttributes._attributes[i];
            if ( descriptor._dataType != GFXDataFormat::COUNT )
            {
                shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "HAS_{}_ATTRIBUTE", Names::attribLocation[i] ).c_str(), true );
            }
        }

        if ( hasTransparency() )
        {
            moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "HAS_TRANSPARENCY", true );

            if ( properties().overrides().useAlphaDiscard() &&
                 renderStagePass._passType != RenderPassType::OIT_PASS )
            {
                moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "USE_ALPHA_DISCARD", true );
            }
        }

        const Configuration& config = _context->context().config();
        if ( !config.rendering.shadowMapping.enabled )
        {
            moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "DISABLE_SHADOW_MAPPING", true );
        }
        else
        {
            if ( !config.rendering.shadowMapping.csm.enabled )
            {
                moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "DISABLE_SHADOW_MAPPING_CSM", true );
            }
            if ( !config.rendering.shadowMapping.spot.enabled )
            {
                moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "DISABLE_SHADOW_MAPPING_SPOT", true );
            }
            if ( !config.rendering.shadowMapping.point.enabled )
            {
                moduleDefines[to_base( ShaderType::FRAGMENT )].emplace_back( "DISABLE_SHADOW_MAPPING_POINT", true );
            }
        }

        shaderDescriptor._globalDefines.emplace_back( properties().isStatic() ? "NODE_STATIC" : "NODE_DYNAMIC", true );

        if ( properties().isInstanced() )
        {
            shaderDescriptor._globalDefines.emplace_back( "OVERRIDE_DATA_IDX", true );
        }

        if (properties().skinningMode() != SkinningMode::COUNT)
        {
            moduleDefines[to_base(ShaderType::VERTEX)].emplace_back("USE_GPU_SKINNING", true);
        }

        switch ( properties().skinningMode() )
        {
            case SkinningMode::MATRIX:     moduleDefines[to_base( ShaderType::VERTEX )].emplace_back( "USE_MATRIX_SKINNING", true ); break;
            case SkinningMode::DUAL_QUAT:  moduleDefines[to_base( ShaderType::VERTEX )].emplace_back( "USE_DUAL_QUAT_SKINNING", true ); break;

            default:
            case SkinningMode::COUNT: break;
        }
        if ( !properties().texturesInFragmentStageOnly() )
        {
            shaderDescriptor._globalDefines.emplace_back( "NEED_TEXTURE_DATA_ALL_STAGES", true );
        }

        for ( U8 i = 0u; i < to_U8( TextureSlot::COUNT ); ++i )
        {
            if ( usesTextureInShader( static_cast<TextureSlot>(i), isPrePass, isShadowPass ) )
            {
                shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "USE_{}_TEXTURE", Names::textureSlot[i] ), true );
            }
        }

        for ( ShaderModuleDescriptor& module : shaderDescriptor._modules )
        {
            module._defines.insert( eastl::end( module._defines ), eastl::begin( moduleDefines[to_base( module._moduleType )] ), eastl::end( moduleDefines[to_base( module._moduleType )] ) );
            module._defines.insert( eastl::end( module._defines ), eastl::begin( _extraShaderDefines[to_base( module._moduleType )] ), eastl::end( _extraShaderDefines[to_base( module._moduleType )] ) );
            module._defines.emplace_back( "DEFINE_PLACEHOLDER", false );
        }
    }

    bool Material::usesTextureInShader( const TextureSlot slot, const bool isPrePass, const bool isShadowPass ) const noexcept
    {
        if ( _textures[to_base( slot )]._ptr == INVALID_HANDLE<Texture> )
        {
            return false;
        }

        if ( !isPrePass && !isShadowPass )
        {
            return true;
        }

        bool add = _textures[to_base( slot )]._useInGeometryPasses;
        if ( !add )
        {
            if ( hasTransparency() )
            {
                if ( slot == TextureSlot::UNIT0 )
                {
                    add = properties().translucencySource() == TranslucencySource::ALBEDO_TEX;
                }
                else if ( slot == TextureSlot::OPACITY )
                {
                    add = properties().translucencySource() == TranslucencySource::OPACITY_MAP_A ||
                          properties().translucencySource() == TranslucencySource::OPACITY_MAP_R;
                }
            }

            if ( isPrePass )
            {
                // Some best-fit heuristics that will surely break at one point
                switch ( slot )
                {
                    case TextureSlot::NORMALMAP:
                    case TextureSlot::HEIGHTMAP:
                    {
                        add = true;
                    } break;
                    case TextureSlot::SPECULAR:
                    {
                        add = properties().shadingMode() != ShadingMode::PBR_MR && properties().shadingMode() != ShadingMode::PBR_SG;
                    } break;
                    case TextureSlot::METALNESS:
                    {
                        add = properties().usePackedOMR();
                    } break;
                    case TextureSlot::ROUGHNESS:
                    {
                        add = !properties().usePackedOMR();
                    } break;
                    default: break;
                };
            }
        }

        return add;
    }

    bool Material::unload()
    {
        for ( TextureInfo& tex : _textures )
        {
            DestroyResource(tex._ptr);
        }

        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            auto& passMapShaders = _shaderInfo[s];
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                auto& shaders = passMapShaders[p];
                for ( ShaderProgramInfo& info : shaders )
                {
                    DestroyResource(info._shaderRef);
                    info = {};
                }
            }
        }

        if ( _baseMaterial != INVALID_HANDLE<Material> )
        {
            LockGuard<SharedMutex> w_lock( _instanceLock );
            Material* baseMat = Get(_baseMaterial);
            DIVIDE_ASSERT(baseMat != nullptr);

            erase_if( baseMat->_instances,
                      [guid = getGUID()]( Handle<Material> instance ) noexcept
                      {
                          if (instance != INVALID_HANDLE<Material>)
                          {
                              return true;
                          }

                          Material* mat = Get(instance);
                          return mat == nullptr || mat->getGUID() == guid;
                      } );
        }

        LockGuard<SharedMutex> r_lock( _instanceLock );
        erase_if(_instances,
                 [](Handle<Material> instance) noexcept
                 {
                    return instance == INVALID_HANDLE<Material>;
                 });

        for ( Handle<Material> instance : _instances )
        {
            Material* mat = Get(instance);
            if ( mat != nullptr )
            {
                mat->_baseMaterial = INVALID_HANDLE<Material>;
            }
        }

        return true;
    }

    void Material::setRenderStateBlock( const RenderStateBlock& renderStateBlock, const RenderStage stage, const RenderPassType pass, const RenderStagePass::VariantType variant )
    {
        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                const RenderStage crtStage = static_cast<RenderStage>(s);
                const RenderPassType crtPass = static_cast<RenderPassType>(p);
                if ( (stage == RenderStage::COUNT || stage == crtStage) && (pass == RenderPassType::COUNT || pass == crtPass) )
                {
                    if ( variant == RenderStagePass::VariantType::COUNT )
                    {
                        _defaultRenderStates[s][p].fill( { renderStateBlock, true} );
                    }
                    else
                    {
                        _defaultRenderStates[s][p][to_base( variant )] = { renderStateBlock, true};
                    }
                }
            }
        }
    }

    void Material::updateTransparency()
    {
        const TranslucencySource oldSource = properties()._translucencySource;
        properties()._translucencySource = TranslucencySource::COUNT;
        if ( properties().overrides().transparencyEnabled() )
        {
            // In order of importance (less to more)!
            // diffuse channel alpha
            if ( properties().baseColour().a < 0.95f && _textures[to_base( TextureSlot::UNIT0 )]._operation != TextureOperation::REPLACE )
            {
                properties()._translucencySource = TranslucencySource::ALBEDO_COLOUR;
            }

            // base texture is translucent
            const Handle<Texture> albedo = _textures[to_base( TextureSlot::UNIT0 )]._ptr;
            if ( albedo != INVALID_HANDLE<Texture> && Get(albedo)->hasTransparency() && !properties().overrides().ignoreTexDiffuseAlpha() )
            {
                properties()._translucencySource = TranslucencySource::ALBEDO_TEX;
            }

            // opacity map
            const Handle<Texture> opacity = _textures[to_base( TextureSlot::OPACITY )]._ptr;
            if ( opacity != INVALID_HANDLE<Texture>)
            {
                const U8 channelCount = NumChannels( Get(opacity)->descriptor()._baseFormat );
                properties()._translucencySource = (channelCount == 4 && Get(opacity)->hasTransparency())
                    ? TranslucencySource::OPACITY_MAP_A
                    : TranslucencySource::OPACITY_MAP_R;
            }
        }

        properties()._needsNewShader = oldSource != properties().translucencySource();
    }

    const RenderStateBlock& Material::getOrCreateRenderStateBlock( const RenderStagePass renderStagePass )
    {
        auto&[ret, isSet] = _defaultRenderStates[to_base( renderStagePass._stage )][to_base( renderStagePass._passType )][to_base( renderStagePass._variant )];
        // If we haven't defined a state for this variant, use the default one
        if ( !isSet )
        {
            // Defaults
            ret._cullMode = properties().doubleSided() ? CullMode::NONE : CullMode::BACK;
            ret._colourWrite.b[0] = ret._colourWrite.b[1] = ret._colourWrite.b[2] = ret._colourWrite.b[3] = true;

            if ( IsDepthPass( renderStagePass ) ) // DEPTH, Z-PREPASS, SHADOW
            {
                ret._zFunc = ComparisonFunction::LEQUAL;
                ret._depthWriteEnabled = true;

                if ( IsShadowPass( renderStagePass ) ) //SHADOW
                {
                    ret._colourWrite.b[2] = ret._colourWrite.b[3] = false;
                    ret._cullMode = CullMode::BACK;
                    //ret.setZBias(1.1f, 4.f);
                }
                else if ( IsZPrePass( renderStagePass ) ) //Z-PRE_PASS
                {
                    NOP();
                }
                else //DEPTH
                {
                    ret._colourWrite.b[0] = ret._colourWrite.b[1] = ret._colourWrite.b[2] = ret._colourWrite.b[3] = false;
                }
            }
            else if( renderStagePass._passType == RenderPassType::MAIN_PASS )
            {
                ret._zFunc = ComparisonFunction::EQUAL;
                ret._depthWriteEnabled = false;
            }
            else //OIT/TRANSPARENT
            {
                DIVIDE_ASSERT( renderStagePass._passType == RenderPassType::OIT_PASS ||
                               renderStagePass._passType == RenderPassType::TRANSPARENCY_PASS );
                // We use alpha-discard in the prepass for transparent/translucent objects, so depth values for translucent pixels will be invalid
                ret._zFunc = ComparisonFunction::LEQUAL;
                ret._depthWriteEnabled = false;
                ret._cullMode = CullMode::NONE;
            }
            if ( _computeRenderStateCBK )
            {
                _computeRenderStateCBK( this, renderStagePass, ret );
            }

            isSet = true;
        }

        return ret;
    }

    void Material::getSortKeys( const RenderStagePass renderStagePass, I64& shaderKey, I64& textureKey, bool& transparencyFlag ) const
    {
        shaderKey = shaderInfo( renderStagePass )._shaderKeyCache;
        SharedLock<SharedMutex> r_lock( _textureLock );
        if ( _textures[to_base( TextureSlot::UNIT0 )]._ptr != INVALID_HANDLE<Texture> )
        {
            textureKey = Get(_textures[to_base( TextureSlot::UNIT0 )]._ptr)->getGUID();
        }
        else
        {
            textureKey = I64_LOWEST;
        }
        transparencyFlag = hasTransparency();
    }

    FColour4 Material::getBaseColour( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = _textures[to_base( TextureSlot::UNIT0 )]._ptr != INVALID_HANDLE<Texture>;
        if ( hasTextureOverride )
        {
            textureOut = _textures[to_base( TextureSlot::UNIT0 )]._ptr;
        }

        return properties().baseColour();
    }

    FColour3 Material::getEmissive( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = _textures[to_base( TextureSlot::EMISSIVE )]._ptr != INVALID_HANDLE<Texture>;
        if ( hasTextureOverride )
        {
            textureOut = _textures[to_base( TextureSlot::EMISSIVE )]._ptr;
        }

        return properties().emissive();
    }

    FColour3 Material::getAmbient( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = false;

        return properties().ambient();
    }

    FColour3 Material::getSpecular( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = _textures[to_base( TextureSlot::SPECULAR )]._ptr != INVALID_HANDLE<Texture>;
        if ( hasTextureOverride )
        {
            textureOut = _textures[to_base( TextureSlot::SPECULAR )]._ptr;
        }
        return properties().specular();
    }

    F32 Material::getMetallic( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = _textures[to_base( TextureSlot::METALNESS )]._ptr != INVALID_HANDLE<Texture>;
        if ( hasTextureOverride )
        {
            textureOut = _textures[to_base( TextureSlot::METALNESS )]._ptr;
        }
        return properties().metallic();
    }

    F32 Material::getRoughness( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = _textures[to_base( TextureSlot::ROUGHNESS )]._ptr != INVALID_HANDLE<Texture>;
        if ( hasTextureOverride )
        {
            textureOut = _textures[to_base( TextureSlot::ROUGHNESS )]._ptr;
        }
        return properties().roughness();
    }

    F32 Material::getOcclusion( bool& hasTextureOverride, Handle<Texture>& textureOut ) const noexcept
    {
        textureOut = INVALID_HANDLE<Texture>;
        hasTextureOverride = _textures[to_base( TextureSlot::OCCLUSION )]._ptr != INVALID_HANDLE<Texture>;
        if ( hasTextureOverride )
        {
            textureOut = _textures[to_base( TextureSlot::OCCLUSION )]._ptr;
        }
        return properties().occlusion();
    }

    void Material::getData( const U32 bestProbeID, NodeMaterialData& dataOut )
    {
        const FColour3& specColour = properties().specular(); ///< For PHONG_SPECULAR
        const F32 shininess = CLAMPED( properties().shininess(), 0.f, MAX_SHININESS );

        const bool useOpacityAlphaChannel = properties().translucencySource() == TranslucencySource::OPACITY_MAP_A;
        const bool useAlbedoTexAlphachannel = properties().translucencySource() == TranslucencySource::ALBEDO_TEX;

        //ToDo: Maybe store all of these material properties in an internal, cached, NodeMaterialData structure? -Ionut
        dataOut._albedo.set( properties().baseColour() );
        dataOut._colourData.set( properties().ambient(), shininess );
        dataOut._emissiveAndParallax.set( properties().emissive(), properties().parallaxFactor() );
        dataOut._data.x = Util::PACK_UNORM4x8( CLAMPED_01( properties().occlusion() ),
                                               CLAMPED_01( properties().metallic() ),
                                               CLAMPED_01( properties().roughness() ),
                                               (properties().doubleSided() ? 1.f : 0.f) );
        dataOut._data.y = Util::PACK_UNORM4x8( specColour.r,
                                               specColour.g,
                                               specColour.b,
                                               properties().usePackedOMR() ? 1.f : 0.f );
        dataOut._data.z = Util::PACK_UNORM4x8( to_U8( properties().bumpMethod() ),
                                               to_U8( properties().shadingMode() ),
                                               0u,
                                               0u );
        dataOut._data.w = bestProbeID;

        dataOut._textureOperations.x = Util::PACK_UNORM4x8( to_U8( _textures[to_base( TextureSlot::UNIT0 )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::UNIT1 )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::SPECULAR )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::EMISSIVE )]._operation ) );
        dataOut._textureOperations.y = Util::PACK_UNORM4x8( to_U8( _textures[to_base( TextureSlot::OCCLUSION )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::METALNESS )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::ROUGHNESS )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::OPACITY )]._operation ) );
        dataOut._textureOperations.z = Util::PACK_UNORM4x8( useAlbedoTexAlphachannel ? 1.f : 0.f,
                                                            useOpacityAlphaChannel ? 1.f : 0.f,
                                                            properties().specGloss().x,
                                                            properties().specGloss().y );
        dataOut._textureOperations.w = Util::PACK_UNORM4x8( properties().receivesShadows() ? 1.f : 0.f, 0.f, 0.f, 0.f );
    }

    DescriptorSet& Material::getDescriptorSet( const RenderStagePass& renderStagePass )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        ShaderStageVisibility texVisibility = properties().texturesInFragmentStageOnly() ? ShaderStageVisibility::FRAGMENT : ShaderStageVisibility::ALL_DRAW;
        const bool isPrePass = renderStagePass._passType == RenderPassType::PRE_PASS;
        const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;
        auto& descriptor = isShadowPass
                                ? _descriptorSetShadow
                                : isPrePass
                                      ? _descriptorSetPrePass
                                      : renderStagePass._stage == RenderStage::DISPLAY
                                                                ? _descriptorSetMainPass
                                                                : _descriptorSetSecondaryPass;

        if ( descriptor._bindingCount == 0u )
        {
            ScopedLock<SharedMutex> w_lock( _textureLock );
            // Check again
            if ( descriptor._bindingCount == 0u )
            {
                for ( U8 i = 0u; i < to_U8( TextureSlot::COUNT ); ++i )
                {
                    if ( usesTextureInShader( static_cast<TextureSlot>(i), isPrePass, isShadowPass ) )
                    {
                        DescriptorSetBinding& binding = AddBinding( descriptor, i, texVisibility );
                        Set( binding._data, _textures[i]._ptr, _textures[i]._sampler );
                    }
                }
            }
        }

        return descriptor;
    }

    void Material::rebuild()
    {
        recomputeShaders();

        // Alternatively we could just copy the maps directly
        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                auto& shaders = _shaderInfo[s][p];
                for ( const ShaderProgramInfo& info : shaders )
                {
                    if ( info._shaderRef != INVALID_HANDLE<ShaderProgram> && Get(info._shaderRef)->getState() == ResourceState::RES_LOADED )
                    {
                        Get(info._shaderRef)->recompile();
                    }
                }
            }
        }
    }

    void Material::saveToXML( const std::string& entryName, boost::property_tree::ptree& pt ) const
    {
        pt.put( entryName + ".version", g_materialXMLVersion );

        properties().saveToXML( entryName, pt );
        saveRenderStatesToXML( entryName, pt );
        saveTextureDataToXML( entryName, pt );
    }

    void Material::loadFromXML( const std::string& entryName, const boost::property_tree::ptree& pt )
    {
        if ( ignoreXMLData() )
        {
            return;
        }

        const size_t detectedVersion = pt.get<size_t>( entryName + ".version", 0 );
        if ( detectedVersion != g_materialXMLVersion )
        {
            Console::printfn( LOCALE_STR( "MATERIAL_WRONG_VERSION" ), assetName(), detectedVersion, g_materialXMLVersion );
            return;
        }

        properties().loadFromXML( entryName, pt );
        loadRenderStatesFromXML( entryName, pt );
        loadTextureDataFromXML( entryName, pt );
    }

    void Material::saveRenderStatesToXML( const std::string& entryName, boost::property_tree::ptree& pt ) const
    {
        hashMap<size_t, U32> previousHashValues;

        U32 blockIndex = 0u;

        const std::string stateNode = Util::StringFormat( "{}.RenderStates", entryName).c_str();
        const std::string blockNode = Util::StringFormat( "{}.RenderStateIndex.PerStagePass", entryName).c_str();

        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
                for ( U8 v = 0u; v < to_U8( RenderStagePass::VariantType::COUNT ); ++v )
                {
                    auto& entry = _defaultRenderStates[s][p][v];
                    if ( !entry._isSet )
                    {
                        continue;
                    }

                    auto& block = entry._block;
                    const size_t stateHash = GetHash(block);
                    if ( previousHashValues.find( stateHash ) == std::cend( previousHashValues ) )
                    {
                        SaveToXML(
                            block,
                            Util::StringFormat( "{}.{}", stateNode, blockIndex ).c_str(),
                            pt );
                        previousHashValues[stateHash] = blockIndex++;
                    }

                    boost::property_tree::ptree stateTree;
                    stateTree.put( "StagePass.<xmlattr>.index", previousHashValues[stateHash] );
                    stateTree.put( "StagePass.<xmlattr>.stage", s );
                    stateTree.put( "StagePass.<xmlattr>.pass", p );
                    stateTree.put( "StagePass.<xmlattr>.variant", v );

                    pt.add_child( blockNode, stateTree.get_child( "StagePass" ) );
                }
            }
        }
    }

    void Material::loadRenderStatesFromXML( const std::string& entryName, const boost::property_tree::ptree& pt )
    {
        hashMap<U32, RenderStateBlock> previousBlocks;

        static boost::property_tree::ptree g_emptyPtree;
        const std::string stateNode = Util::StringFormat( "{}.RenderStates", entryName).c_str();
        const std::string blockNode = Util::StringFormat( "{}.RenderStateIndex", entryName).c_str();
        for ( const auto& [tag, data] : pt.get_child( blockNode, g_emptyPtree ) )
        {
            assert( tag == "PerStagePass" );

            const U32 b = data.get<U32>( "<xmlattr>.index", U32_MAX );                                    assert( b != U32_MAX );
            const U8  s = data.get<U8>( "<xmlattr>.stage", to_U8( RenderStage::COUNT ) );                  assert( s != to_U8( RenderStage::COUNT ) );
            const U8  p = data.get<U8>( "<xmlattr>.pass", to_U8( RenderPassType::COUNT ) );               assert( p != to_U8( RenderPassType::COUNT ) );
            const U8  v = data.get<U8>( "<xmlattr>.variant", to_U8( RenderStagePass::VariantType::COUNT ) ); assert( v != to_U8( RenderStagePass::VariantType::COUNT ) );

            const auto& it = previousBlocks.find( b );
            if ( it != cend( previousBlocks ) )
            {
                _defaultRenderStates[s][p][v] = { it->second, true };
            }
            else
            {
                RenderStateBlock block{};
                LoadFromXML( Util::StringFormat( "{}.{}", stateNode, b ).c_str(), pt, block );

                _defaultRenderStates[s][p][v] = { block, true };
                previousBlocks[b] = block;
            }
        }
    }

    void Material::saveTextureDataToXML( const std::string& entryName, boost::property_tree::ptree& pt ) const
    {
        hashMap<size_t, U32> previousHashValues;

        U32 samplerCount = 0u;
        for ( U8 i = 0u; i < to_U8( TextureSlot::COUNT ); ++i )
        {
            const TextureSlot usage = static_cast<TextureSlot>(i);

            Handle<Texture> tex = getTexture( usage );
            if ( tex != INVALID_HANDLE<Texture> )
            {
                const Texture* texture = Get(tex);


                const std::string textureNode = entryName + ".texture." + TypeUtil::TextureSlotToString( usage );

                pt.put( textureNode + ".name", texture->assetName().c_str() );
                pt.put( textureNode + ".path", texture->assetLocation().string() );
                pt.put( textureNode + ".usage", TypeUtil::TextureOperationToString( _textures[to_base( usage )]._operation ) );
                pt.put( textureNode + ".srgb", _textures[to_base( usage )]._srgb );

                const SamplerDescriptor sampler = _textures[to_base( usage )]._sampler;
                const size_t samplerHash = GetHash(sampler);

                if ( previousHashValues.find( samplerHash ) == std::cend( previousHashValues ) )
                {
                    samplerCount++;
                    XMLParser::saveToXML( sampler, Util::StringFormat( "{}.SamplerDescriptors.{}", entryName, samplerCount ).c_str(), pt );
                    previousHashValues[samplerHash] = samplerCount;
                }
                pt.put( textureNode + ".Sampler.id", previousHashValues[samplerHash] );
                pt.put( textureNode + ".UseForGeometry", _textures[to_base( usage )]._useInGeometryPasses );
            }
        }
    }

    void Material::loadTextureDataFromXML( const std::string& entryName, const boost::property_tree::ptree& pt )
    {
        hashMap<U32, SamplerDescriptor> previousSamplers;

        for ( U8 i = 0u; i < to_U8( TextureSlot::COUNT ); ++i )
        {
            const TextureSlot usage = static_cast<TextureSlot>(i);

            if ( pt.get_child_optional( entryName + ".texture." + TypeUtil::TextureSlotToString( usage ) + ".name" ) )
            {
                const std::string textureNode = entryName + ".texture." + TypeUtil::TextureSlotToString( usage );

                const std::string texName = pt.get<std::string>( textureNode + ".name", "" );
                const ResourcePath texPath = ResourcePath( pt.get<std::string>( textureNode + ".path", "" ).c_str() );
                // May be a procedural texture
                if ( texPath.empty() )
                {
                    continue;
                }

                if ( !texName.empty() )
                {
                    LockGuard<SharedMutex> w_lock( _textureLock );

                    const bool useInGeometryPasses = pt.get<bool>( textureNode + ".UseForGeometry", _textures[to_base( usage )]._useInGeometryPasses );
                    const U32 index = pt.get<U32>( textureNode + ".Sampler.id", 0 );
                    const auto& it = previousSamplers.find( index );

                    SamplerDescriptor sampler;
                    if ( it != cend( previousSamplers ) )
                    {
                        sampler = it->second;
                    }
                    else
                    {
                        sampler = XMLParser::loadFromXML( Util::StringFormat( "{}.SamplerDescriptors.{}", entryName, index ).c_str(), pt );
                        previousSamplers[index] = sampler;
                    }

                    setSampler( usage, sampler );

                    TextureOperation& op = _textures[to_base( usage )]._operation;
                    bool& srgb = _textures[to_base(usage)]._srgb;

                    op = TypeUtil::StringToTextureOperation( pt.get<std::string>( textureNode + ".usage", TypeUtil::TextureOperationToString( op ) ) );
                    srgb = pt.get<bool>( textureNode + ".srgb", srgb);

                    Handle<Texture> crtTex = _textures[to_base( usage )]._ptr;
                    if ( crtTex == INVALID_HANDLE<Texture> )
                    {
                        op = TextureOperation::NONE;
                    }
                    else if ( (Get(crtTex)->assetLocation() / Get(crtTex)->assetName()) == (texPath / texName) )
                    {
                        continue;
                    }

                    _textures[to_base( usage )]._useInGeometryPasses = useInGeometryPasses;

                    ResourceDescriptor<Texture> texture( texName );
                    texture.assetName( texName.c_str() );
                    texture.assetLocation( texPath );
                    texture.waitForReady( true );

                    TextureDescriptor& texDesc = texture._propertyDescriptor;
                    texDesc._texType = TextureType::TEXTURE_2D_ARRAY;
                    if ( srgb )
                    {
                        texDesc._packing = GFXImagePacking::NORMALIZED_SRGB;
                    }

                    setTextureLocked( usage, texture, sampler, op, useInGeometryPasses );
                }
            }
        }
    }

} //namespace Divide
