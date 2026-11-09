// Copyright (c) 2026 DIVIDE-Studio

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
#include "Graphs/Headers/SceneGraphNode.h"
#include "Rendering/RenderPass/Headers/NodeBufferedData.h"

namespace Divide
{

    namespace
    {
        constexpr size_t g_materialXMLVersion = 1u;

        [[nodiscard]] bool HasDefine( const ModuleDefines& defines, const std::string_view name ) noexcept
        {
            return std::ranges::any_of( defines, [name]( const ModuleDefine& define )
            {
                const std::string_view text{ define._define.c_str() };
                return define._addPrefix && text.starts_with( name ) &&
                       ( text.size() == name.size() || text[name.size()] == ' ' || text[name.size()] == '\t' );
            } );
        }

        [[nodiscard]] bool HasShaderDefine( const ShaderProgramDescriptor& descriptor, const std::string_view name ) noexcept
        {
            if ( HasDefine( descriptor._globalDefines, name ) )
            {
                return true;
            }

            return std::ranges::any_of( descriptor._modules, [name]( const ShaderModuleDescriptor& module )
            {
                return HasDefine( module._defines, name );
            } );
        }

        [[nodiscard]] bool HasRawShaderText( const ShaderProgramDescriptor& descriptor ) noexcept
        {
            const auto hasRaw = []( const ModuleDefines& defines )
            {
                return std::ranges::any_of( defines, []( const ModuleDefine& define )
                {
                    return !define._addPrefix && define._define != "DEFINE_PLACEHOLDER";
                } );
            };
            if ( hasRaw( descriptor._globalDefines ) )
            {
                return true;
            }
            return std::ranges::any_of( descriptor._modules, [&hasRaw]( const ShaderModuleDescriptor& module )
            {
                return hasRaw( module._defines );
            } );
        }

        [[nodiscard]] bool HasBinding( const DescriptorSet& descriptor, const U8 slot ) noexcept
        {
            for ( U8 i = 0u; i < descriptor._bindingCount; ++i )
            {
                if ( descriptor._bindings[i]._slot == slot )
                {
                    return true;
                }
            }
            return false;
        }
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
        // Could just direct copy the arrays, but this looks cool
        for ( U8 s = 0u; s < to_U8( RenderStage::COUNT ); ++s )
        {
            auto& perPassStates = _defaultRenderStates[s];

            for ( U8 p = 0u; p < to_U8( RenderPassType::COUNT ); ++p )
            {
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
        cloneMat->_baseShaderData = srcMat->_baseShaderData;
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
                          if ( variantMapDst[v]._shaderQueued.load( std::memory_order_acquire ) )
                          {
                              cloneMat->_context->shaderComputeQueue().cancel( variantMapDst[v] );
                          }
                          if ( variantMapDst[v]._shaderRef != INVALID_HANDLE<ShaderProgram> )
                          {
                              if ( ResourceCache::Inspect( variantMapDst[v]._shaderRef )._state != ResourceState::RES_LOADED )
                              {
                                  cloneMat->_context->shaderComputeQueue().waitForPendingLoads();
                              }
                          }
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
            {
                LockGuard<SharedMutex> w_lock( _textureLock );
                const TranslucencySource previousSource = properties().translucencySource();
                updateTransparency();
                if ( previousSource != properties().translucencySource() )
                {
                    invalidateDescriptorSets();
                }
            }
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

        if ( ret != to_base(MaterialUpdateResult::OK) )
        {
            _coverageRevision.fetch_add(1u, std::memory_order_relaxed);
        }
        return ret;
    }

    void Material::clearRenderStates()
    {
        _coverageRevision.fetch_add(1u, std::memory_order_relaxed);
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
        LockGuard<SharedMutex> lock( _textureLock );
        auto& current = _textures[to_U32( textureUsageSlot )]._sampler;
        if ( current != sampler )
        {
            invalidateDescriptorSets();
            current = sampler;
        }

        return true;
    }

    bool Material::setTextureLocked( const TextureSlot textureUsageSlot, const ResourceDescriptor<Texture>& texture, const SamplerDescriptor sampler, const TextureOperation op, const bool useInGeometryPasses )
    {
        return setTextureLocked(textureUsageSlot, CreateResource(texture), sampler, op, useInGeometryPasses);
    }

    bool Material::setTextureLocked( const TextureSlot textureUsageSlot, const Handle<Texture> texture, const SamplerDescriptor sampler, const TextureOperation op, const bool useInGeometryPasses )
    {
        invalidateDescriptorSets();

        const U32 slot = to_U32( textureUsageSlot );

        TextureInfo& texInfo = _textures[slot];

        texInfo._sampler = sampler;

        setTextureOperationLocked( textureUsageSlot, texture != INVALID_HANDLE<Texture> ? op : TextureOperation::NONE );

        DestroyResource( texInfo._ptr );
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
            const TranslucencySource previousSource = properties().translucencySource();
            updateTransparency();
            properties()._transparencyUpdated |= previousSource != properties().translucencySource();
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
        LockGuard<SharedMutex> lock( _textureLock );
        setTextureOperationLocked( textureUsageSlot, op );
    }

    void Material::setTextureOperationLocked( const TextureSlot textureUsageSlot, const TextureOperation op )
    {
        TextureOperation& crtOp = _textures[to_base( textureUsageSlot )]._operation;

        if ( crtOp != op )
        {
            invalidateDescriptorSets();
            crtOp = op;
            properties()._needsNewShader = true;
            if ( textureUsageSlot == TextureSlot::UNIT0 || textureUsageSlot == TextureSlot::OPACITY )
            {
                properties()._transparencyUpdated = true;
            }
        }
    }

    void Material::reportShaderFailure( ShaderProgramInfo& info, const RenderStagePass stagePass, const ResourceState state )
    {
        info._shaderCompStage = ShaderBuildStage::FAILED;
        info._shaderKeyCache = I64_LOWEST;
        info._readyNotificationPending = false;
        Console::errorfn( LOCALE_STR( "MATERIAL_SHADER_LOAD_FAILED" ),
                          resourceName().c_str(),
                          TypeUtil::RenderStageToString( stagePass._stage ),
                          TypeUtil::RenderPassTypeToString( stagePass._passType ),
                          to_U32( to_base( stagePass._variant ) ),
                          to_U32( to_base( state ) ),
                          to_U32( info._shaderRef ) );
    }

    void Material::setShaderProgramInternal( const RenderStagePass stagePass )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        ShaderProgramInfo& info = shaderInfo( stagePass );
        ShaderComputeQueue& shaderQueue = _context->shaderComputeQueue();
        if ( info._shaderQueued.load( std::memory_order_acquire ) )
        {
            shaderQueue.cancel( info );
        }

        ShaderProgram* previous = nullptr;
        if ( info._shaderRef != INVALID_HANDLE<ShaderProgram> )
        {
            const ResourceSnapshot snapshot = ResourceCache::Inspect( info._shaderRef );
            const ShaderResourceStatus status = ClassifyShaderResource( snapshot._state );
            if ( status == ShaderResourceStatus::PENDING )
            {
                return;
            }
            if ( status == ShaderResourceStatus::FAILED )
            {
                reportShaderFailure( info, stagePass, snapshot._state );
                return;
            }
            previous = Get( info._shaderRef );
            if ( previous == nullptr )
            {
                reportShaderFailure( info, stagePass, ResourceState::RES_UNKNOWN );
                return;
            }
        }

        ShaderProgramDescriptor shaderDescriptor = _computeShaderCBK( this, stagePass );
        computeAndAppendShaderDefines( shaderDescriptor, stagePass );

        if ( previous != nullptr )
        {
            if ( previous->descriptor() != shaderDescriptor )
            {
                Console::printfn( LOCALE_STR( "REPLACE_SHADER" ),
                                  previous->resourceName().c_str(),
                                  shaderDescriptor._name.c_str(),
                                  TypeUtil::RenderStageToString( stagePass._stage ),
                                  TypeUtil::RenderPassTypeToString( stagePass._passType ),
                                  to_base(stagePass._variant) );
            }
            else
            {
                info._shaderCompStage = ShaderBuildStage::COMPUTED;
                return;
            }
        }

        ShaderComputeQueue::ShaderQueueElement shaderElement{ &info, shaderDescriptor };
        if ( updatePriorirty() == UpdatePriority::High )
        {
            shaderQueue.process( shaderElement );
            info._shaderCompStage = ShaderBuildStage::COMPUTED;
        }
        else
        {
            if ( updatePriorirty() == UpdatePriority::Medium )
            {
                shaderQueue.addToQueueFront( shaderElement );
            }
            else
            {
                shaderQueue.addToQueueBack( shaderElement );
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
                    shaderInfo._readyNotificationPending = false;
                }
            }
        }

        _recomputeShadersCBK();
    }

    Handle<ShaderProgram> Material::computeAndGetProgramHandle( const RenderStagePass renderStagePass )
    {
        bool justFinishedLoading = false;
        if ( prepareShader( renderStagePass, justFinishedLoading, false ) )
        {
            return getProgramHandle( renderStagePass );
        }

        return INVALID_HANDLE<ShaderProgram>;
    }

    Handle<ShaderProgram> Material::getProgramHandle( const RenderStagePass renderStagePass ) const
    {
        const ShaderProgramInfo& info = shaderInfo( renderStagePass );

        if ( info._shaderCompStage == ShaderBuildStage::READY && info._shaderRef != INVALID_HANDLE<ShaderProgram> )
        {
            return info._shaderRef;
        }

        Console::errorfn( LOCALE_STR( "MATERIAL_SHADER_NOT_READY" ),
                          resourceName().c_str(),
                          TypeUtil::RenderStageToString( renderStagePass._stage ),
                          TypeUtil::RenderPassTypeToString( renderStagePass._passType ),
                          to_U32( to_base( renderStagePass._variant ) ) );
        return INVALID_HANDLE<ShaderProgram>;
    }

    bool Material::canDraw( const RenderStagePass renderStagePass, bool& shaderJustFinishedLoading )
    {
        shaderJustFinishedLoading = false;
        if ( properties().needsNewShader() )
        {
            return false;
        }

        return prepareShader( renderStagePass, shaderJustFinishedLoading, true );
    }

    bool Material::prepareShader( const RenderStagePass renderStagePass,
                                  bool& shaderJustFinishedLoading,
                                  const bool consumeNotification )
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
            setShaderProgramInternal( renderStagePass );
        }

        // A replacement may retain its previous shader until the queue processes it.
        if ( info._shaderCompStage == ShaderBuildStage::QUEUED )
        {
            if ( info._shaderQueued.load( std::memory_order_acquire ) )
            {
                return false;
            }

            if ( info._shaderRef == INVALID_HANDLE<ShaderProgram> )
            {
                reportShaderFailure( info, renderStagePass, ResourceState::RES_UNKNOWN );
                return false;
            }
            info._shaderCompStage = ShaderBuildStage::COMPUTED;
        }

        if ( info._shaderCompStage == ShaderBuildStage::COMPUTED )
        {
            const ResourceSnapshot snapshot = ResourceCache::Inspect( info._shaderRef );
            if ( info.updateReadiness( snapshot._state, snapshot._guid ) == ShaderResourceStatus::FAILED )
            {
                reportShaderFailure( info, renderStagePass, snapshot._state );
            }
        }

        if ( info._shaderCompStage != ShaderBuildStage::READY )
        {
            if ( info._shaderCompStage == ShaderBuildStage::COUNT )
            {
                info._shaderCompStage = ShaderBuildStage::REQUESTED;
            }
            return false;
        }

        if ( consumeNotification )
        {
            shaderJustFinishedLoading = info.takeReadyNotification();
        }
        return true;
    }

    bool Material::shaderPending( const RenderStagePass renderStagePass ) const
    {
        const ShaderBuildStage stage = shaderInfo( renderStagePass )._shaderCompStage;
        return properties().needsNewShader() || (stage != ShaderBuildStage::READY && stage != ShaderBuildStage::FAILED);
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
            default:                       DIVIDE_UNEXPECTED_CALL();
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
            case PrimitiveTopology::LINES_ADJACENCY:                                                                                     break;
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

        for ( ShaderModuleDescriptor& module : shaderDescriptor._modules )
        {
            module._defines.insert( eastl::end( module._defines ), eastl::begin( moduleDefines[to_base( module._moduleType )] ), eastl::end( moduleDefines[to_base( module._moduleType )] ) );
            module._defines.insert( eastl::end( module._defines ), eastl::begin( _extraShaderDefines[to_base( module._moduleType )] ), eastl::end( _extraShaderDefines[to_base( module._moduleType )] ) );
            module._defines.emplace_back( "DEFINE_PLACEHOLDER", false );
        }

        appendTextureShaderDefines( shaderDescriptor, isPrePass, isShadowPass );
    }

    bool Material::UsesStandardMaterialStages( const ShaderProgramDescriptor& shaderDescriptor ) noexcept
    {
        if ( shaderDescriptor._modules.size() != 2u )
        {
            return false;
        }

        const ShaderModuleDescriptor* vertex = nullptr;
        const ShaderModuleDescriptor* fragment = nullptr;
        for ( const ShaderModuleDescriptor& module : shaderDescriptor._modules )
        {
            if ( module._moduleType == ShaderType::VERTEX )
            {
                vertex = &module;
            }
            else if ( module._moduleType == ShaderType::FRAGMENT )
            {
                fragment = &module;
            }
        }
        if ( vertex == nullptr || fragment == nullptr )
        {
            return false;
        }

        const bool standardVertex = vertex->_sourceFile == "baseVertexShaders.glsl" ||
                                    vertex->_sourceFile == "tree.glsl";
        const bool standardColour = fragment->_sourceFile == "material.glsl" && fragment->_variant.empty();
        const bool standardDepth = fragment->_sourceFile == "depthPass.glsl" &&
                                   ( fragment->_variant == "PrePass" ||
                                     fragment->_variant == "Shadow" ||
                                     fragment->_variant == "Shadow.VSM" );
        return standardVertex && ( standardColour || standardDepth );
    }

    bool Material::HasCommonMaterialInputs( const ShaderProgramDescriptor& shaderDescriptor ) noexcept
    {
        return UsesStandardMaterialStages( shaderDescriptor ) &&
               !HasShaderDefine( shaderDescriptor, "NEED_TEXTURE_DATA_ALL_STAGES" ) &&
               !HasRawShaderText( shaderDescriptor ) &&
               HasDefine( shaderDescriptor._globalDefines, "USE_UNIT0_TEXTURE" ) &&
               HasDefine( shaderDescriptor._globalDefines, "USE_OPACITY_TEXTURE" );
    }

    void Material::appendTextureShaderDefines( ShaderProgramDescriptor& shaderDescriptor, const bool isPrePass, const bool isShadowPass ) const
    {
        const bool commonInputs = properties().texturesInFragmentStageOnly() &&
                                   UsesStandardMaterialStages( shaderDescriptor ) &&
                                   !HasShaderDefine( shaderDescriptor, "NEED_TEXTURE_DATA_ALL_STAGES" ) &&
                                   !HasRawShaderText( shaderDescriptor );
        for ( U8 i = 0u; i < to_U8( TextureSlot::COUNT ); ++i )
        {
            const TextureSlot slot = static_cast<TextureSlot>(i);
            if ( ( commonInputs && ( slot == TextureSlot::UNIT0 || slot == TextureSlot::OPACITY ) ) ||
                 usesTextureInShader( slot, isPrePass, isShadowPass ) )
            {
                shaderDescriptor._globalDefines.emplace_back( Util::StringFormat( "USE_{}_TEXTURE", Names::textureSlot[i] ), true );
            }
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
        ShaderComputeQueue& shaderQueue = _context->shaderComputeQueue();
        for ( auto& stage : _shaderInfo )
        {
            for ( auto& pass : stage )
            {
                for ( ShaderProgramInfo& info : pass )
                {
                    if ( info._shaderQueued.load( std::memory_order_acquire ) )
                    {
                        shaderQueue.cancel( info );
                    }
                }
            }
        }
        shaderQueue.waitForPendingLoads();

        {
            LockGuard<SharedMutex> lock( _textureLock );
            invalidateDescriptorSets();
        }
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
                    info = {};
                }
            }
        }

        if ( _baseMaterial != INVALID_HANDLE<Material> )
        {
            Material* baseMat = Get(_baseMaterial);
            DIVIDE_ASSERT(baseMat != nullptr);
            if ( baseMat != nullptr )
            {
                LockGuard<SharedMutex> w_lock( baseMat->_instanceLock );
                erase_if( baseMat->_instances,
                          [guid = getGUID()]( Handle<Material> instance ) noexcept
                          {
                              if ( instance == INVALID_HANDLE<Material> )
                              {
                                  return true;
                              }

                              Material* mat = Get(instance);
                              return mat == nullptr || mat->getGUID() == guid;
                          } );
            }
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
        _coverageRevision.fetch_add(1u, std::memory_order_relaxed);
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

    TranslucencySource Material::SelectTranslucencySource( const TransparencyInputs& inputs ) noexcept
    {
        TranslucencySource source = TranslucencySource::COUNT;
        if ( inputs._enabled )
        {
            const bool useAlbedo = inputs._hasAlbedo && inputs._albedoOperation != TextureOperation::NONE;
            if ( inputs._baseAlpha < 0.95f && ( !useAlbedo || inputs._albedoOperation != TextureOperation::REPLACE ) )
            {
                source = TranslucencySource::ALBEDO_COLOUR;
            }
            if ( useAlbedo && inputs._albedoHasTransparency && !inputs._ignoreAlbedoAlpha )
            {
                source = TranslucencySource::ALBEDO_TEX;
            }
            if ( inputs._hasOpacity && inputs._opacityOperation != TextureOperation::NONE )
            {
                source = inputs._opacityUsesAlpha ? TranslucencySource::OPACITY_MAP_A : TranslucencySource::OPACITY_MAP_R;
            }
        }
        return source;
    }

    void Material::updateTransparency()
    {
        const TranslucencySource oldSource = properties()._translucencySource;
        const TextureInfo& albedo = _textures[to_base( TextureSlot::UNIT0 )];
        const TextureInfo& opacity = _textures[to_base( TextureSlot::OPACITY )];
        const bool hasAlbedo = albedo._ptr != INVALID_HANDLE<Texture>;
        const bool hasOpacity = opacity._ptr != INVALID_HANDLE<Texture>;

        TransparencyInputs inputs{};
        inputs._baseAlpha = properties().baseColour().a;
        inputs._albedoOperation = albedo._operation;
        inputs._opacityOperation = opacity._operation;
        inputs._enabled = properties().overrides().transparencyEnabled();
        inputs._ignoreAlbedoAlpha = properties().overrides().ignoreTexDiffuseAlpha();
        inputs._hasAlbedo = hasAlbedo;
        inputs._albedoHasTransparency = hasAlbedo && Get( albedo._ptr )->hasTransparency();
        inputs._hasOpacity = hasOpacity;
        inputs._opacityUsesAlpha = hasOpacity &&
                                   NumChannels( Get( opacity._ptr )->descriptor()._baseFormat ) == 4u &&
                                   Get( opacity._ptr )->hasTransparency();

        properties()._translucencySource = SelectTranslucencySource( inputs );
        properties()._needsNewShader |= oldSource != properties().translucencySource();
    }

    void Material::invalidateDescriptorSets() noexcept
    {
        _coverageRevision.fetch_add(1u, std::memory_order_relaxed);
        _resourceBindings = {};
        _descriptorSetMainPass._bindingCount = 0u;
        _descriptorSetSecondaryPass._bindingCount = 0u;
        _descriptorSetPrePass._bindingCount = 0u;
        _descriptorSetShadow._bindingCount = 0u;
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

        const TextureInfo& albedoTexture = _textures[to_base( TextureSlot::UNIT0 )];
        const TextureInfo& opacityTexture = _textures[to_base( TextureSlot::OPACITY )];
        const TextureOperation albedoOperation = albedoTexture._ptr != INVALID_HANDLE<Texture> ? albedoTexture._operation : TextureOperation::NONE;
        const TextureOperation opacityOperation = opacityTexture._ptr != INVALID_HANDLE<Texture> ? opacityTexture._operation : TextureOperation::NONE;

        dataOut._textureOperations.x = Util::PACK_UNORM4x8( to_U8( albedoOperation ),
                                                            to_U8( _textures[to_base( TextureSlot::UNIT1 )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::SPECULAR )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::EMISSIVE )]._operation ) );
        dataOut._textureOperations.y = Util::PACK_UNORM4x8( to_U8( _textures[to_base( TextureSlot::OCCLUSION )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::METALNESS )]._operation ),
                                                            to_U8( _textures[to_base( TextureSlot::ROUGHNESS )]._operation ),
                                                            to_U8( opacityOperation ) );
        dataOut._textureOperations.z = Util::PACK_UNORM4x8( useAlbedoTexAlphachannel ? 1.f : 0.f,
                                                            useOpacityAlphaChannel ? 1.f : 0.f,
                                                            properties().specGloss().x,
                                                            properties().specGloss().y );
        dataOut._textureOperations.w = Util::PACK_UNORM4x8( properties().receivesShadows() ? 1.f : 0.f, 0.f, 0.f, 0.f );
    }

    void Material::AddMissingCommonMaterialBindings( DescriptorSet& descriptor, const ImageView& fallback, const SamplerDescriptor sampler )
    {
        for ( const TextureSlot slot : { TextureSlot::UNIT0, TextureSlot::OPACITY } )
        {
            if ( !HasBinding( descriptor, to_U8( slot ) ) )
            {
                DescriptorSetBinding& binding = AddBinding( descriptor, to_U8( slot ), ShaderStageVisibility::FRAGMENT );
                Set( binding._data, fallback, sampler );
            }
        }
    }

    bool Material::getDescriptorSet( const RenderStagePass& renderStagePass,
                                     const ShaderProgramDescriptor& shaderDescriptor,
                                     DescriptorSet& descriptorOut )
    {
        LockGuard<SharedMutex> lock( _textureLock );
        return getDescriptorSetLocked( renderStagePass, shaderDescriptor, descriptorOut );
    }

    ResourceBindingHandle Material::getResourceBindings( const RenderStagePass& renderStagePass,
                                                        const ShaderProgramDescriptor& shaderDescriptor )
    {
        const bool commonInputs = HasCommonMaterialInputs( shaderDescriptor );
        const size_t passIndex = renderStagePass._stage == RenderStage::SHADOW ? 0u
                               : renderStagePass._passType == RenderPassType::PRE_PASS ? 1u
                               : renderStagePass._stage == RenderStage::DISPLAY ? 2u : 3u;
        const size_t visibilityIndex = properties().texturesInFragmentStageOnly() ? 0u : 1u;
        {
            SharedLock<SharedMutex> lock( _textureLock );
            const auto& cached = _resourceBindings[passIndex][commonInputs ? 1u : 0u][visibilityIndex];
            if ( cached != nullptr )
            {
                return cached;
            }
        }
        LockGuard<SharedMutex> lock( _textureLock );
        auto& cached = _resourceBindings[passIndex][commonInputs ? 1u : 0u][visibilityIndex];
        if ( cached == nullptr )
        {
            DescriptorSet bindings{};
            if ( !getDescriptorSetLocked( renderStagePass, shaderDescriptor, bindings ) )
            {
                return {};
            }
            fixed_vector<Handle<Texture>, MAX_BINDINGS_PER_DESCRIPTOR_SET> textures;
            const auto usesTexture = [&]( const Handle<Texture> texture )
            {
                return texture != INVALID_HANDLE<Texture> &&
                       std::ranges::any_of( std::span{ bindings._bindings.data(), bindings._bindingCount }, [&]( const DescriptorSetBinding& binding )
                       {
                           return binding._data._sampledImage._image._srcTexture == Get( texture );
                       } );
            };
            for ( const TextureInfo& texture : _textures )
            {
                if ( usesTexture( texture._ptr ) )
                {
                    textures.push_back( texture._ptr );
                }
            }
            if ( commonInputs && usesTexture( Texture::DefaultTexture2DArray() ) )
            {
                textures.push_back( Texture::DefaultTexture2DArray() );
            }
            cached = CreateResourceBindings( bindings, { textures.data(), textures.size() } );
        }
        return cached;
    }

    bool Material::getDescriptorSetLocked( const RenderStagePass& renderStagePass,
                                           const ShaderProgramDescriptor& shaderDescriptor,
                                           DescriptorSet& descriptorOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

        const ShaderStageVisibility texVisibility = properties().texturesInFragmentStageOnly() ? ShaderStageVisibility::FRAGMENT : ShaderStageVisibility::ALL_DRAW;
        const bool isPrePass = renderStagePass._passType == RenderPassType::PRE_PASS;
        const bool isShadowPass = renderStagePass._stage == RenderStage::SHADOW;
        auto& descriptor = isShadowPass
                                ? _descriptorSetShadow
                                : isPrePass
                                      ? _descriptorSetPrePass
                                      : renderStagePass._stage == RenderStage::DISPLAY
                                                                ? _descriptorSetMainPass
                                                                : _descriptorSetSecondaryPass;

        descriptorOut._bindingCount = 0u;
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
        descriptorOut = descriptor;

        for ( U8 i = 0u; i < descriptorOut._bindingCount; ++i )
        {
            descriptorOut._bindings[i]._shaderStageVisibility = to_U16( texVisibility );
        }

        if ( HasCommonMaterialInputs( shaderDescriptor ) &&
             ( !HasBinding( descriptorOut, to_U8( TextureSlot::UNIT0 ) ) ||
               !HasBinding( descriptorOut, to_U8( TextureSlot::OPACITY ) ) ) )
        {
            const Texture* fallback = Get( Texture::DefaultTexture2DArray() );
            if ( fallback == nullptr )
            {
                Console::errorfn( LOCALE_STR( "MATERIAL_DEFAULT_TEXTURE_MISSING" ), resourceName().c_str() );
                return false;
            }
            AddMissingCommonMaterialBindings( descriptorOut, fallback->getView(), Texture::DefaultSampler() );
        }
        return true;
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
                    if ( ResourceCache::Inspect( info._shaderRef )._state == ResourceState::RES_LOADED )
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

                    invalidateDescriptorSets();
                    _textures[to_base( usage )]._sampler = sampler;

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
