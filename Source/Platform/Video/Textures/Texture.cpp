

#include "Headers/Texture.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    [[nodiscard]] bool IsEmpty( const TextureLayoutChanges& changes ) noexcept
    {
        for ( const TextureLayoutChange& it : changes )
        {
            if ( it._targetView._srcTexture != nullptr &&
                 it._targetLayout != ImageUsage::COUNT &&
                 it._sourceLayout != it._targetLayout )
            {
                return false;
            }
        }

        return true;
    }

    Str<64> Texture::s_missingTextureFileName( "missing_texture.jpg" );

    SamplerDescriptor Texture::s_defaultSampler;
    Handle<Texture> Texture::s_defaultTexture2D = INVALID_HANDLE<Texture>;
    Handle<Texture> Texture::s_defaultTexture2DArray = INVALID_HANDLE<Texture>;
    bool Texture::s_useDDSCache = true;

    void Texture::OnStartup( GFXDevice& gfx )
    {
        ImageTools::OnStartup( gfx.renderAPI() != RenderAPI::OpenGL );

        TextureDescriptor textureDescriptor{};
        textureDescriptor._dataType = GFXDataFormat::UNSIGNED_BYTE;
        textureDescriptor._baseFormat = GFXImageFormat::RGBA;

        {
            s_defaultTexture2D = CreateResource( ResourceDescriptor<Texture>( "defaultEmptyTexture2D", textureDescriptor ) );
        }
        {
            textureDescriptor._texType = TextureType::TEXTURE_2D_ARRAY;
            s_defaultTexture2DArray = CreateResource( ResourceDescriptor<Texture>( "defaultEmptyTexture2DArray", textureDescriptor ) );
        }

        Byte defaultTexData[1u * 1u * 4];
        defaultTexData[0] = defaultTexData[1] = defaultTexData[2] = to_byte( 0u ); //RGB: black
        defaultTexData[3] = to_byte( 1u ); //Alpha: 1

        ImageTools::ImageData imgDataDefault = {};
        DIVIDE_EXPECTED_CALL( imgDataDefault.loadFromMemory( defaultTexData, 4, 1u, 1u, 1u, 4 ) );

        Get(s_defaultTexture2D)->createWithData( imgDataDefault, {});
        Get(s_defaultTexture2DArray)->createWithData( imgDataDefault, {});

        s_defaultSampler._wrapU = TextureWrap::CLAMP_TO_EDGE;
        s_defaultSampler._wrapV = TextureWrap::CLAMP_TO_EDGE;
        s_defaultSampler._wrapW = TextureWrap::CLAMP_TO_EDGE;
        s_defaultSampler._minFilter = TextureFilter::NEAREST;
        s_defaultSampler._magFilter = TextureFilter::NEAREST;
        s_defaultSampler._mipSampling = TextureMipSampling::NONE;
        s_defaultSampler._anisotropyLevel = 0u;
    }

    void Texture::OnShutdown() noexcept
    {
        DestroyResource(s_defaultTexture2D);
        DestroyResource(s_defaultTexture2DArray);
        ImageTools::OnShutdown();
    }

    bool Texture::UseTextureDDSCache() noexcept
    {
        return s_useDDSCache;
    }

    Handle<Texture> Texture::DefaultTexture2D() noexcept
    {
        return s_defaultTexture2D;
    }
    
    Handle<Texture> Texture::DefaultTexture2DArray() noexcept
    {
        return s_defaultTexture2DArray;
    }

    const SamplerDescriptor Texture::DefaultSampler() noexcept
    {
        return s_defaultSampler;
    }

    U8 Texture::GetBytesPerPixel( const GFXDataFormat format, const GFXImageFormat baseFormat, const GFXImagePacking packing ) noexcept
    {
        if ( packing == GFXImagePacking::RGB_565 || packing == GFXImagePacking::RGBA_4444 )
        {
            return 2u;
        }

        U8 bytesPerChannel = 1u;
        switch ( format )
        {
            case GFXDataFormat::UNSIGNED_SHORT:
            case GFXDataFormat::SIGNED_SHORT:
            case GFXDataFormat::FLOAT_16: bytesPerChannel = 2u; break;

            case GFXDataFormat::UNSIGNED_INT:
            case GFXDataFormat::SIGNED_INT:
            case GFXDataFormat::FLOAT_32: bytesPerChannel = 4u; break;
            default: break;
        };

        return NumChannels( baseFormat ) * bytesPerChannel;
    }

    Texture::Texture( PlatformContext& context, const ResourceDescriptor<Texture>& descriptor )
       : CachedResource( descriptor, "Texture" )
       , GraphicsResource( context.gfx(), Type::TEXTURE, getGUID(), _ID( resourceName() ) )
       , _descriptor( descriptor._propertyDescriptor )
    {
        DIVIDE_ASSERT( descriptor.enumValue() < to_base( TextureType::COUNT ) );
        DIVIDE_ASSERT(_descriptor._packing != GFXImagePacking::COUNT &&
                      _descriptor._baseFormat != GFXImageFormat::COUNT &&
                      _descriptor._dataType != GFXDataFormat::COUNT);

        _loadedFromFile = !descriptor.assetName().empty();

        if ( _loadedFromFile )
        {
            if ( assetLocation().empty() )
            {
                assetLocation( Paths::g_texturesLocation );
            }

            DIVIDE_ASSERT( assetLocation().string().find(',' ) == string::npos, "TextureLoaderImpl error: All textures for a single array must be loaded from the same location!" );

            const bool isCubeMap = IsCubeTexture( _descriptor._texType );

            const U16 numCommas = to_U16( std::count( std::cbegin( descriptor.assetName() ), std::cend( descriptor.assetName() ), ',' ) );
            if ( numCommas > 0u )
            {
                const U16 targetLayers = numCommas + 1u;

                if ( isCubeMap )
                {
                    // Each layer needs 6 images
                    DIVIDE_ASSERT( targetLayers >= 6u && targetLayers % 6u == 0u, "TextureLoaderImpl error: Invalid number of source textures specified for cube map!" );

                    if ( _descriptor._layerCount == 0u )
                    {
                        _descriptor._layerCount = targetLayers % 6;
                    }

                    DIVIDE_ASSERT( _descriptor._layerCount == targetLayers % 6 );

                    // We only use cube arrays to simplify some logic in the texturing code
                    if ( _descriptor._texType == TextureType::TEXTURE_CUBE_MAP )
                    {
                        _descriptor._texType = TextureType::TEXTURE_CUBE_ARRAY;
                    }
                }
                else
                {
                    if ( _descriptor._layerCount == 0u )
                    {
                        _descriptor._layerCount = targetLayers;
                    }

                    DIVIDE_ASSERT( _descriptor._layerCount == targetLayers, "TextureLoaderImpl error: Invalid number of source textures specified for texture array!" );
                }
            }

        }

        if ( _descriptor._layerCount == 0u )
        {
            _descriptor._layerCount = 1u;
        }

    }

    Texture::~Texture()
    {
    }

    bool Texture::load( PlatformContext& context )
    {
        if (!loadInternal())
        {
            Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_FILE" ),
                              assetLocation(),
                              assetName(),
                              resourceName() );
            return false;
        }

        return CachedResource::load( context );
    }

    bool Texture::postLoad()
    {
        return CachedResource::postLoad();
    }

    /// Load texture data using the specified file name
    bool Texture::loadInternal()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        if ( _loadedFromFile )
        {
            const GFXDataFormat requestedFormat = _descriptor._dataType;
            assert( requestedFormat == GFXDataFormat::UNSIGNED_BYTE ||  // Regular image format
                    requestedFormat == GFXDataFormat::UNSIGNED_SHORT || // 16Bit
                    requestedFormat == GFXDataFormat::FLOAT_16 ||       // 16Bit
                    requestedFormat == GFXDataFormat::FLOAT_32 ||       // HDR
                    requestedFormat == GFXDataFormat::COUNT );          // Auto

            // Each texture face/layer must be in a comma separated list
            istringstream textureFileList( assetName().c_str() );

            ImageTools::ImageData dataStorage = {};
            dataStorage.requestedFormat( requestedFormat );

            bool hasValidEntries = false;
            // We loop over every texture in the above list and store it in this temporary string
            const ResourcePath currentTextureFullPath = assetLocation().empty() ? Paths::g_texturesLocation : assetLocation();

            string currentTextureFile;
            while ( Util::GetLine( textureFileList, currentTextureFile, ',' ) )
            {
                // Skip invalid entries
                if ( currentTextureFile.empty() )
                {
                    continue;
                }

                // Attempt to load the current entry
                if ( !loadFile( currentTextureFullPath, Util::Trim(currentTextureFile), dataStorage ) )
                {
                    // Invalid texture files are not handled yet, so stop loading
                    continue;
                }

                hasValidEntries = true;
            }

            if ( hasValidEntries )
            {
                // Create a new Rendering API-dependent texture object
                _descriptor._baseFormat = dataStorage.format();
                _descriptor._dataType = dataStorage.dataType();
                // Uploading to the GPU dependents on the rendering API
                createWithData( dataStorage, {});

                if ( IsCubeTexture( _descriptor._texType ) && ( dataStorage.layerCount() % 6 != 0 || dataStorage.layerCount() / 6 != depth()) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_CUBMAP_INIT_COUNT" ), resourceName().c_str() );
                }
                else if ( IsArrayTexture( _descriptor._texType ) && !IsCubeTexture( _descriptor._texType ) && dataStorage.layerCount() != depth() )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_ARRAY_INIT_COUNT" ), resourceName().c_str() );
                }
            }
        }

        return true;
    }

    U8 Texture::numChannels() const noexcept
    {
        switch ( _descriptor._baseFormat )
        {
            case GFXImageFormat::RED:  return 1u;
            case GFXImageFormat::RG:   return 2u;
            case GFXImageFormat::RGB:  return 3u;
            case GFXImageFormat::RGBA: return 4u;
            default: break;
        }

        return 0u;
    }

    bool Texture::loadFile( const ResourcePath& path, const std::string_view name, ImageTools::ImageData& fileData )
    {
        const bool srgb = _descriptor._packing == GFXImagePacking::NORMALIZED_SRGB;


        if ( !fileExists( path / name ) || !fileData.loadFromFile( _context.context(), srgb, _width, _height, path, name, _descriptor._textureOptions ) )
        {
            if ( fileData.layerCount() > 0 )
            {
                Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LAYER_LOAD" ), name );
                return false;
            }

            Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOAD" ), name );
            // missing_texture.jpg must be something that really stands out
            DIVIDE_EXPECTED_CALL( fileData.loadFromFile( _context.context(), srgb, _width, _height, Paths::g_texturesLocation, s_missingTextureFileName ) );
        }
        else if ( !fileData.hasDummyAlphaChannel() )
        {
            return checkTransparency( path, name, fileData );
        }

        return true;
    }

    void Texture::prepareTextureData( const U16 width, const U16 height, const U16 depth, [[maybe_unused]] const bool emptyAllocation )
    {
        _width = width;
        _height = height;
        _depth = depth;
        DIVIDE_ASSERT( _width > 0 && _height > 0 && _depth > 0, "Texture error: Invalid texture dimensions!" );

        validateDescriptor();
    }

    void Texture::createWithData( const Byte* data, size_t dataSize, const vec2<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
    {
        createWithData(data, dataSize, vec3<U16>{dimensions.width, dimensions.height, _descriptor._layerCount}, pixelUnpackAlignment);
    }

    void Texture::submitTextureData()
    {
        NOP();
    }

    void Texture::createWithData( const Byte* data, const size_t dataSize, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // This should never be called for compressed textures
        DIVIDE_ASSERT( !IsCompressed( _descriptor._baseFormat ) );

        const U16 slices = IsCubeTexture( _descriptor._texType ) ? dimensions.depth * 6u : dimensions.depth;

        const bool emptyAllocation = dataSize == 0u || data == nullptr;
        prepareTextureData( dimensions.width, dimensions.height, slices, emptyAllocation );

        // We can't manually specify data for msaa textures.
        DIVIDE_ASSERT( _descriptor._msaaSamples == 0u || data == nullptr );
        
        if ( !emptyAllocation )
        {
            ImageTools::ImageData imgData{};
            if ( imgData.loadFromMemory( data, dataSize, dimensions.width, dimensions.height, 1, GetBytesPerPixel( _descriptor._dataType, _descriptor._baseFormat, _descriptor._packing ) ) )
            {
                loadDataInternal( imgData, vec3<U16>(0u), pixelUnpackAlignment);
            }
        }

        submitTextureData();
    }

    void Texture::replaceData( const Byte* data, const size_t dataSize, const vec3<U16>& offset, const vec3<U16>& range, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( data == nullptr || dataSize == 0u )
        {
            return;
        }

        if ( offset.x == 0u && offset.y == 0u && offset.z == 0u && (range.x > _width || range.y > _height || range.z > _depth) )
        {
            createWithData(data, dataSize, range, pixelUnpackAlignment);
        }
        else
        {
            DIVIDE_ASSERT( offset.width  + range.width  <= _width &&
                           offset.height + range.height <= _height &&
                           offset.depth  + range.depth  <= _depth);

            loadDataInternal( data, dataSize, 0u, offset, range, pixelUnpackAlignment );
        }
    }

    void Texture::createWithData( const ImageTools::ImageData& imageData, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        U16 slices = imageData.layerCount();
        if ( IsCubeTexture( _descriptor._texType ) )
        {
            DIVIDE_ASSERT(slices >= 6u && slices % 6u == 0u);
            slices = slices / 6u;
        }
        else if ( Is3DTexture( _descriptor._texType ) )
        {
            slices = imageData.dimensions( 0u, 0u ).depth;
        }

        prepareTextureData( imageData.dimensions( 0u, 0u ).width, imageData.dimensions( 0u, 0u ).height, slices, false );

        if ( IsCompressed( _descriptor._baseFormat ) &&
             _descriptor._mipMappingState == MipMappingState::AUTO )
        {
            _descriptor._mipMappingState = MipMappingState::MANUAL;
        }

        loadDataInternal( imageData, vec3<U16>(0u), pixelUnpackAlignment);

        submitTextureData();
    }

    bool Texture::checkTransparency( const ResourcePath& path, const std::string_view name, ImageTools::ImageData& fileData )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( fileData.ignoreAlphaChannelTransparency() || fileData.hasDummyAlphaChannel() )
        {
            _hasTransparency = false;
            _hasTranslucency = false;
            return true;
        }

        const U32 layer = to_U32( fileData.layerCount() - 1 );

        // Extract width, height and bit depth
        const U16 width = fileData.dimensions( layer, 0u ).width;
        const U16 height = fileData.dimensions( layer, 0u ).height;
        // If we have an alpha channel, we must check for translucency/transparency

        const ResourcePath cachePath = Paths::Textures::g_metadataLocation / path;
        const string cacheName = string(name) + ".cache";

        ByteBuffer metadataCache;
        bool skip = false;
        if ( metadataCache.loadFromFile( cachePath, cacheName ) )
        {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            metadataCache >> tempVer;
            if ( tempVer == BYTE_BUFFER_VERSION )
            {
                metadataCache >> _hasTransparency;
                metadataCache >> _hasTranslucency;
                skip = true;
            }
            else
            {
                metadataCache.clear();
            }
        }

        if ( !skip )
        {
            if ( HasAlphaChannel( fileData.format() ) )
            {
                bool hasTranslucentOrOpaquePixels = false;
                // Allow about 4 pixels per partition to be ignored
                constexpr U32 transparentPixelsSkipCount = 4u;

                std::atomic_uint transparentPixelCount = 0u;

                Parallel_For
                (
                    _context.context().taskPool( TaskPoolType::HIGH_PRIORITY ),
                    ParallelForDescriptor
                    {
                        ._iterCount = width,
                        ._partitionSize = std::max(16u, to_U32(width / 10)),
                        ._useCurrentThread = true
                    },
                    [&]( const Task* /*parent*/, const U32 start, const U32 end )
                    {
                        U8 tempA = 0u;
                        for ( U32 i = start; i < end; ++i )
                        {
                            for ( I32 j = 0; j < height; ++j )
                            {
                                if ( _hasTransparency && (_hasTranslucency || hasTranslucentOrOpaquePixels) )
                                {
                                    return;
                                }
                                fileData.getAlpha( i, j, tempA, layer );
                                if ( IS_IN_RANGE_INCLUSIVE( tempA, 0, 250 ) )
                                {
                                    if ( transparentPixelCount.fetch_add( 1u ) >= transparentPixelsSkipCount )
                                    {
                                        _hasTransparency = true;
                                        _hasTranslucency = tempA > 1;
                                        if ( _hasTranslucency )
                                        {
                                            hasTranslucentOrOpaquePixels = true;
                                            return;
                                        }
                                    }
                                }
                                else if ( tempA > 250 )
                                {
                                    hasTranslucentOrOpaquePixels = true;
                                }
                            }
                        }
                    }
                );

                if ( _hasTransparency && !_hasTranslucency && !hasTranslucentOrOpaquePixels)
                {
                    // All the alpha values are 0, so this channel is useless.
                    _hasTransparency = _hasTranslucency = false;
                }

                metadataCache << BYTE_BUFFER_VERSION;
                metadataCache << _hasTransparency;
                metadataCache << _hasTranslucency;
                DIVIDE_EXPECTED_CALL( metadataCache.dumpToFile( cachePath, cacheName ) );
            }
        }

        Console::printfn( LOCALE_STR( "TEXTURE_HAS_TRANSPARENCY_TRANSLUCENCY" ),
                          name,
                          _hasTransparency ? "yes" : "no",
                          _hasTranslucency ? "yes" : "no" );

        return true;
    }

    void Texture::setSampleCount( U8 newSampleCount )
    {
        CLAMP( newSampleCount, U8_ZERO, DisplayManager::MaxMSAASamples() );
        if ( _descriptor._msaaSamples != newSampleCount )
        {
            _descriptor._msaaSamples = newSampleCount;
            createWithData( nullptr, 0u, { width(), height(), depth() }, {});
        }
    }

    void Texture::validateDescriptor()
    {
        if ( _descriptor._packing == GFXImagePacking::NORMALIZED_SRGB )
        {
            bool valid = false;
            switch ( _descriptor._baseFormat )
            {
                case GFXImageFormat::RGB:
                case GFXImageFormat::BGR:
                case GFXImageFormat::RGBA:
                case GFXImageFormat::BGRA:
                case GFXImageFormat::BC1:
                case GFXImageFormat::BC1a:
                case GFXImageFormat::BC2:
                case GFXImageFormat::BC3:
                case GFXImageFormat::BC7:
                {
                    valid = _descriptor._dataType == GFXDataFormat::UNSIGNED_BYTE;
                } break;

                case GFXImageFormat::RED: 
                case GFXImageFormat::RG: 
                case GFXImageFormat::BC3n: 
                case GFXImageFormat::BC4s: 
                case GFXImageFormat::BC4u: 
                case GFXImageFormat::BC5s: 
                case GFXImageFormat::BC5u: 
                case GFXImageFormat::BC6s: 
                case GFXImageFormat::BC6u: break;

                case GFXImageFormat::COUNT:
                default: DIVIDE_UNEXPECTED_CALL(); break;
            }

            DIVIDE_GPU_ASSERT(valid, "SRGB textures are only supported for RGB/BGR(A) normalized formats!" );
        }
        else if ( IsDepthTexture( _descriptor._packing) )
        {
            DIVIDE_ASSERT( _descriptor._baseFormat == GFXImageFormat::RED, "Depth textures only supported for single channel formats");
        }

        // We may have a 1D texture
        DIVIDE_ASSERT( _width > 0u && _height > 0u );
        {
            //http://www.opengl.org/registry/specs/ARB/texture_non_power_of_two.txt
            if ( _descriptor._mipMappingState != MipMappingState::OFF )
            {
                _mipCount = MipCount(_width, _height);
            }
            else
            {
                _mipCount = 1u;
            }
        }
    }

    ImageView Texture::getView() const noexcept
    {
        const U16 layerCount = _descriptor._texType == TextureType::TEXTURE_3D ? 1u : _depth;

        ImageView view{};
        view._srcTexture = this;
        view._descriptor._msaaSamples = _descriptor._msaaSamples;
        view._descriptor._dataType = _descriptor._dataType;
        view._descriptor._baseFormat = _descriptor._baseFormat;
        view._descriptor._packing = _descriptor._packing;
        view._subRange._layerRange._count = layerCount;
        view._subRange._mipLevels._count = _mipCount;

        return view;
    }

    ImageView Texture::getView( const TextureType targetType ) const noexcept
    {
        ImageView ret = getView();
        ret._targetType = targetType;
        return ret;
    }

    ImageView Texture::getView( const SubRange mipRange ) const noexcept
    {
        ImageView ret = getView();
        ret._subRange._mipLevels = { mipRange._offset, std::min( mipRange._count, ret._subRange._mipLevels._count) };
        return ret;
    }

    ImageView Texture::getView( const SubRange mipRange, const SubRange layerRange ) const noexcept
    {
        ImageView ret = getView( mipRange );
        ret._subRange._layerRange = { layerRange._offset, std::min(layerRange._count, ret._subRange._layerRange._count) };
        return ret;
    }

    ImageView Texture::getView( const TextureType targetType, const SubRange mipRange ) const noexcept
    {
        ImageView ret = getView( targetType );
        ret._subRange._mipLevels = { mipRange._offset, std::min( mipRange._count, ret._subRange._mipLevels._count ) };
        return ret;
    }

    ImageView Texture::getView( const TextureType targetType, const SubRange mipRange, const SubRange layerRange ) const noexcept
    {
        ImageView ret = getView( targetType, mipRange );
        ret._subRange._layerRange = { layerRange._offset, std::min( layerRange._count, ret._subRange._layerRange._count ) };
        return ret;
    }
};
