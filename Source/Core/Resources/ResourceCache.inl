
namespace Divide {

    template<>
    CachedResource_ptr ResourceLoader::Build<Material>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        Material_ptr ptr( MemoryManager_NEW Material( context, cache, loadingDescriptorHash, descriptor.resourceName() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<ShaderProgram>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {

        if ( descriptor.assetName().empty() )
        {
            descriptor.assetName( descriptor.resourceName() );
        }

        if ( descriptor.assetLocation().empty() )
        {
            descriptor.assetLocation( Paths::g_shadersLocation );
        }

        const std::shared_ptr<ShaderProgramDescriptor>& shaderDescriptor = descriptor.propertyDescriptor<ShaderProgramDescriptor>();
        assert( shaderDescriptor != nullptr );
        ShaderProgram_ptr ptr = context.gfx().newShaderProgram( loadingDescriptorHash, descriptor.resourceName(), descriptor.assetName(), descriptor.assetLocation(), *shaderDescriptor, *cache );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }
        else
        {
            ptr->highPriority( !descriptor.flag() );
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Texture>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        assert( descriptor.enumValue() < to_base( TextureType::COUNT ) );

        const std::shared_ptr<TextureDescriptor>& texDescriptor = descriptor.propertyDescriptor<TextureDescriptor>();
        assert( texDescriptor != nullptr );

        if ( !descriptor.assetName().empty() )
        {

            const bool isCubeMap = IsCubeTexture( texDescriptor->texType() );

            const string resourceLocation = descriptor.assetLocation().string();

            const U16 numCommas = to_U16( std::count( std::cbegin( descriptor.assetName() ), std::cend( descriptor.assetName() ), ',' ) );
            if ( numCommas > 0u )
            {
                const U16 targetLayers = numCommas + 1u;

                if ( isCubeMap )
                {
                    // Each layer needs 6 images
                    DIVIDE_ASSERT( targetLayers >= 6u && targetLayers % 6u == 0u, "TextureLoaderImpl error: Invalid number of source textures specified for cube map!" );

                    if ( texDescriptor->layerCount() == 0u )
                    {
                        texDescriptor->layerCount( targetLayers % 6 );
                    }

                    DIVIDE_ASSERT( texDescriptor->layerCount() == targetLayers % 6 );

                    // We only use cube arrays to simplify some logic in the texturing code
                    if ( texDescriptor->texType() == TextureType::TEXTURE_CUBE_MAP )
                    {
                        texDescriptor->texType( TextureType::TEXTURE_CUBE_ARRAY );
                    }
                }
                else
                {
                    if ( texDescriptor->layerCount() == 0u )
                    {
                        texDescriptor->layerCount( targetLayers );
                    }

                    DIVIDE_ASSERT( texDescriptor->layerCount() == targetLayers, "TextureLoaderImpl error: Invalid number of source textures specified for texture array!" );
                }
            }

            if ( resourceLocation.empty() )
            {
                descriptor.assetLocation( Paths::g_texturesLocation );
            }
            else
            {
                DIVIDE_ASSERT( std::count( std::cbegin( resourceLocation ), std::cend( resourceLocation ), ',' ) == 0u, "TextureLoaderImpl error: All textures for a single array must be loaded from the same location!" );
            }
        }

        if ( texDescriptor->layerCount() == 0u )
        {
            texDescriptor->layerCount( 1u );
        }

        Texture_ptr ptr = context.gfx().newTexture( loadingDescriptorHash,
                                                    descriptor.resourceName(),
                                                    descriptor.assetName(),
                                                    descriptor.assetLocation(),
                                                    *texDescriptor,
                                                    *cache );

        if ( !Load( ptr ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_FILE" ),
                              descriptor.assetLocation(),
                              descriptor.assetName(),
                              descriptor.resourceName() );
            ptr.reset();
        }

        return ptr;
    }

    template <>
    CachedResource_ptr ResourceLoader::Build<AudioDescriptor>( ResourceCache* cache, [[maybe_unused]] PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        AudioDescriptor_ptr ptr( MemoryManager_NEW AudioDescriptor( loadingDescriptorHash, descriptor.resourceName(), descriptor.assetName(), descriptor.assetLocation() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }
        else
        {
            ptr->isLooping( descriptor.flag() );
        }

        return ptr;
    }


    template <>
    CachedResource_ptr ResourceLoader::Build<Box3D>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        constexpr F32 s_minSideLength = 0.0001f;

        const vec3<F32> targetSize
        {
            std::max( Util::UINT_TO_FLOAT( descriptor.data().x ), s_minSideLength ),
            std::max( Util::UINT_TO_FLOAT( descriptor.data().y ), s_minSideLength ),
            std::max( Util::UINT_TO_FLOAT( descriptor.data().z ), s_minSideLength )
        };

        std::shared_ptr<Box3D> ptr( MemoryManager_NEW Box3D( context, cache, loadingDescriptorHash, descriptor.resourceName(), targetSize ), DeleteResource( cache ) );

        if ( !descriptor.flag() )
        {
            const ResourceDescriptor matDesc( "Material_" + descriptor.resourceName() );
            Material_ptr matTemp = CreateResource<Material>( cache, matDesc );
            matTemp->properties().shadingMode( ShadingMode::PBR_MR );
            ptr->setMaterialTpl( matTemp );
        }

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Quad3D>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        constexpr F32 s_minSideLength = 0.0001f;

        const vec3<U32> sizeIn = descriptor.data();

        vec3<F32> targetSize
        {
            Util::UINT_TO_FLOAT( sizeIn.x ),
            Util::UINT_TO_FLOAT( sizeIn.y ),
            Util::UINT_TO_FLOAT( sizeIn.z )
        };

        if ( sizeIn.x == 0u && sizeIn.y == 0u && sizeIn.z == 0u )
        {
            targetSize.xy = { s_minSideLength, s_minSideLength };
        }
        else if ( (sizeIn.x == 0u && sizeIn.y == 0u) ||
                  (sizeIn.x == 0u && sizeIn.z == 0u) )
        {
            targetSize.x = s_minSideLength;
        }
        else if ( sizeIn.y == 0u && sizeIn.z == 0u )
        {
            targetSize.y = s_minSideLength;
        }

        std::shared_ptr<Quad3D> ptr( MemoryManager_NEW Quad3D( context, cache, loadingDescriptorHash, descriptor.resourceName(), descriptor.mask().b[0] == 0, targetSize ), DeleteResource( cache ) );

        if ( !descriptor.flag() )
        {
            const ResourceDescriptor matDesc( "Material_" + descriptor.resourceName() );
            Material_ptr matTemp = CreateResource<Material>( cache, matDesc );
            matTemp->properties().shadingMode( ShadingMode::PBR_MR );
            ptr->setMaterialTpl( matTemp );
        }

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Sphere3D>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        constexpr F32 s_minRadius = 0.0001f;

        std::shared_ptr<Sphere3D> ptr( MemoryManager_NEW Sphere3D( context, cache, loadingDescriptorHash, descriptor.resourceName(), std::max( Util::UINT_TO_FLOAT( descriptor.enumValue() ), s_minRadius ), descriptor.ID() == 0u ? 16u : descriptor.ID() ), DeleteResource( cache ) );

        if ( !descriptor.flag() )
        {
            const ResourceDescriptor matDesc( "Material_" + descriptor.resourceName() );
            Material_ptr matTemp = CreateResource<Material>( cache, matDesc );
            matTemp->properties().shadingMode( ShadingMode::PBR_MR );
            ptr->setMaterialTpl( matTemp );
        }

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<InfinitePlane>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        std::shared_ptr<InfinitePlane> ptr( MemoryManager_NEW InfinitePlane( context.gfx(), cache, loadingDescriptorHash, descriptor.resourceName(), descriptor.data().xy ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Sky>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        std::shared_ptr<Sky> ptr( MemoryManager_NEW Sky( context.gfx(), cache, loadingDescriptorHash, descriptor.resourceName(), descriptor.ID() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Terrain>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        std::shared_ptr<Terrain> ptr( MemoryManager_NEW Terrain( context, cache, loadingDescriptorHash, descriptor.resourceName() ), DeleteResource( cache ) );

        Console::printfn( LOCALE_STR( "TERRAIN_LOAD_START" ), descriptor.resourceName().c_str() );
        const std::shared_ptr<TerrainDescriptor>& terrain = descriptor.propertyDescriptor<TerrainDescriptor>();

        if ( ptr )
        {
            ptr->setState( ResourceState::RES_LOADING );
        }

        if ( !ptr || !TerrainLoader::loadTerrain( ptr, terrain, context, true ) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_TERRAIN_LOAD" ), descriptor.resourceName().c_str() );
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<WaterPlane>( ResourceCache* cache, [[maybe_unused]] PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        std::shared_ptr<WaterPlane> ptr( MemoryManager_NEW WaterPlane( cache, loadingDescriptorHash, descriptor.resourceName() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Trigger>( ResourceCache* cache, [[maybe_unused]] PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        std::shared_ptr<Trigger> ptr( MemoryManager_NEW Trigger( cache, loadingDescriptorHash, descriptor.resourceName() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<ParticleEmitter>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        std::shared_ptr<ParticleEmitter> ptr( MemoryManager_NEW ParticleEmitter( context.gfx(), cache, loadingDescriptorHash, descriptor.resourceName() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<SubMesh>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        SubMesh_ptr ptr( MemoryManager_NEW SubMesh( context, cache, loadingDescriptorHash, descriptor.resourceName() ), DeleteResource( cache ) );

        if ( !Load( ptr ) )
        {
            ptr.reset();
        }

        return ptr;
    }

    template<>
    CachedResource_ptr ResourceLoader::Build<Mesh>( ResourceCache* cache, PlatformContext& context, ResourceDescriptor descriptor, const size_t loadingDescriptorHash )
    {
        Mesh_ptr ptr( MemoryManager_NEW Mesh( context, cache, loadingDescriptorHash, descriptor.resourceName(), descriptor.assetName(), descriptor.assetLocation() ), DeleteResource( cache ) );
        if ( ptr != nullptr )
        {
            _mesh->setState( ResourceState::RES_LOADING );
            return detail::MeshLoadData 
            {
               ._mesh = ptr,
               ._cache = cache,
               ._context = &context,
               ._descriptor = &descriptor
            }.build();
        }

        return ptr;
    }

} //namespace Divide