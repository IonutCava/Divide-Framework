

#include "Headers/Texture.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    bool IsEmpty( const TextureLayoutChanges& changes ) noexcept
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
    Texture_ptr Texture::s_defaultTexture2D = nullptr;
    Texture_ptr Texture::s_defaultTexture2DArray = nullptr;
    bool Texture::s_useDDSCache = true;

    void Texture::OnStartup( GFXDevice& gfx )
    {
        ImageTools::OnStartup( gfx.renderAPI() != RenderAPI::OpenGL );

        TextureDescriptor textureDescriptor( TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, GFXImageFormat::RGBA );
        textureDescriptor.baseFormat( GFXImageFormat::RGBA );

        {
            ResourceDescriptor textureResourceDescriptor( "defaultEmptyTexture2D" );
            textureResourceDescriptor.propertyDescriptor( textureDescriptor );
            textureResourceDescriptor.waitForReady( true );
            s_defaultTexture2D = CreateResource<Texture>( gfx.context().kernel().resourceCache(), textureResourceDescriptor);
        }
        {
            textureDescriptor.texType(TextureType::TEXTURE_2D_ARRAY);
            ResourceDescriptor textureResourceDescriptor( "defaultEmptyTexture2DArray" );
            textureResourceDescriptor.propertyDescriptor( textureDescriptor );
            textureResourceDescriptor.waitForReady( true );
            s_defaultTexture2DArray = CreateResource<Texture>( gfx.context().kernel().resourceCache(), textureResourceDescriptor );
        }
        Byte* defaultTexData = MemoryManager_NEW Byte[1u * 1u * 4];
        defaultTexData[0] = defaultTexData[1] = defaultTexData[2] = to_byte( 0u ); //RGB: black
        defaultTexData[3] = to_byte( 1u ); //Alpha: 1

        ImageTools::ImageData imgDataDefault = {};
        if ( !imgDataDefault.loadFromMemory( defaultTexData, 4, 1u, 1u, 1u, 4 ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
        s_defaultTexture2D->createWithData( imgDataDefault, {});
        s_defaultTexture2DArray->createWithData( imgDataDefault, {});
        MemoryManager::DELETE_ARRAY( defaultTexData );

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
        s_defaultTexture2D.reset();
        s_defaultTexture2DArray.reset();
        ImageTools::OnShutdown();
    }

    bool Texture::UseTextureDDSCache() noexcept
    {
        return s_useDDSCache;
    }

    const Texture_ptr& Texture::DefaultTexture2D() noexcept
    {
        return s_defaultTexture2D;
    }
    
    const Texture_ptr& Texture::DefaultTexture2DArray() noexcept
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

    Texture::Texture( GFXDevice& context,
                      const size_t descriptorHash,
                      const std::string_view name,
                      std::string_view assetNames,
                      const ResourcePath& assetLocations,
                      const TextureDescriptor& texDescriptor,
                      ResourceCache& parentCache )
       : CachedResource( ResourceType::GPU_OBJECT, descriptorHash, name, assetNames, assetLocations )
       , GraphicsResource( context, Type::TEXTURE, getGUID(), _ID( name ) )
       , _descriptor( texDescriptor )
       , _parentCache( parentCache )
    {
        DIVIDE_ASSERT(_descriptor.packing() != GFXImagePacking::COUNT &&
                      _descriptor.baseFormat() != GFXImageFormat::COUNT &&
                      _descriptor.dataType() != GFXDataFormat::COUNT);
    }

    Texture::~Texture()
    {
        _parentCache.remove( this );
    }

    bool Texture::load()
    {
        Start( *CreateTask( [this]( [[maybe_unused]] const Task& parent )
                            {
                                threadedLoad();
                            }),
                            _context.context().taskPool( TaskPoolType::HIGH_PRIORITY ),
                            TaskPriority::DONT_CARE,
                            [this, tex = shared_from_this()]()
                            {
                                // Hack to keep this texture alive if we immediately discard it after load but before we flush the callback queue
                                DIVIDE_ASSERT(tex != nullptr);
                                postLoad();
                            });

        return true;
    }

    void Texture::postLoad()
    {
        NOP();
    }

    /// Load texture data using the specified file name
    void Texture::threadedLoad()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        if ( !assetLocation().empty() )
        {
            const GFXDataFormat requestedFormat = _descriptor.dataType();
            assert( requestedFormat == GFXDataFormat::UNSIGNED_BYTE ||  // Regular image format
                    requestedFormat == GFXDataFormat::UNSIGNED_SHORT || // 16Bit
                    requestedFormat == GFXDataFormat::FLOAT_16 ||       // 16Bit
                    requestedFormat == GFXDataFormat::FLOAT_32 ||       // HDR
                    requestedFormat == GFXDataFormat::COUNT );          // Auto

            // Each texture face/layer must be in a comma separated list
            istringstream textureFileList( assetName().c_str() );

            ImageTools::ImageData dataStorage = {};
            dataStorage.requestedFormat( requestedFormat );

            bool loadedFromFile = false;
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

                loadedFromFile = true;
            }

            if ( loadedFromFile )
            {
                // Create a new Rendering API-dependent texture object
                _descriptor.baseFormat( dataStorage.format() );
                _descriptor.dataType( dataStorage.dataType() );
                // Uploading to the GPU dependents on the rendering API
                createWithData( dataStorage, {});

                if ( IsCubeTexture( _descriptor.texType()) && ( dataStorage.layerCount() % 6 != 0 || dataStorage.layerCount() / 6 != depth()) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_CUBMAP_INIT_COUNT" ), resourceName().c_str() );
                }
                else if ( IsArrayTexture(_descriptor.texType() ) && !IsCubeTexture( _descriptor.texType() ) && dataStorage.layerCount() != depth() )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_ARRAY_INIT_COUNT" ), resourceName().c_str() );
                }
            }
        }

        CachedResource::load();
    }

    U8 Texture::numChannels() const noexcept
    {
        switch ( descriptor().baseFormat() )
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
        const bool srgb = _descriptor.packing() == GFXImagePacking::NORMALIZED_SRGB;


        if ( !fileExists( path / name ) || !fileData.loadFromFile( _context.context(), srgb, _width, _height, path, name, _descriptor.textureOptions() ) )
        {
            if ( fileData.layerCount() > 0 )
            {
                Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LAYER_LOAD" ), name );
                return false;
            }

            Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOAD" ), name );
            // missing_texture.jpg must be something that really stands out
            _descriptor.dataType(GFXDataFormat::UNSIGNED_BYTE);
            _descriptor.baseFormat(GFXImageFormat::RGBA);
            if ( !fileData.loadFromFile( _context.context(), srgb, _width, _height, Paths::g_texturesLocation, s_missingTextureFileName ) )
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        else
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
        createWithData(data, dataSize, vec3<U16>{dimensions.width, dimensions.height, _descriptor.layerCount()}, pixelUnpackAlignment);
    }

    void Texture::submitTextureData()
    {
        NOP();
    }

    void Texture::createWithData( const Byte* data, const size_t dataSize, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // This should never be called for compressed textures
        assert( !IsCompressed( _descriptor.baseFormat() ) );

        const U16 slices = IsCubeTexture( _descriptor.texType() ) ? dimensions.depth * 6u : dimensions.depth;

        const bool emptyAllocation = dataSize == 0u || data == nullptr;
        prepareTextureData( dimensions.width, dimensions.height, slices, emptyAllocation );

        // We can't manually specify data for msaa textures.
        assert( _descriptor.msaaSamples() == 0u || data == nullptr );
        
        if ( !emptyAllocation )
        {
            ImageTools::ImageData imgData{};
            if ( imgData.loadFromMemory( data, dataSize, dimensions.width, dimensions.height, 1, GetBytesPerPixel( _descriptor.dataType(), _descriptor.baseFormat(), _descriptor.packing() ) ) )
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
        if ( IsCubeTexture(_descriptor.texType()) )
        {
            DIVIDE_ASSERT(slices >= 6u && slices % 6u == 0u);
            slices = slices / 6u;
        }
        else if ( Is3DTexture( _descriptor.texType() ) )
        {
            slices = imageData.dimensions( 0u, 0u ).depth;
        }

        prepareTextureData( imageData.dimensions( 0u, 0u ).width, imageData.dimensions( 0u, 0u ).height, slices, false );

        if ( IsCompressed( _descriptor.baseFormat() ) &&
             _descriptor.mipMappingState() == TextureDescriptor::MipMappingState::AUTO )
        {
            _descriptor.mipMappingState( TextureDescriptor::MipMappingState::MANUAL );
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
                bool hasTransulenctOrOpaquePixels = false;
                // Allow about 4 pixels per partition to be ignored
                constexpr U32 transparentPixelsSkipCount = 4u;

                std::atomic_uint transparentPixelCount = 0u;

                ParallelForDescriptor descriptor = {};
                descriptor._iterCount = width;
                descriptor._partitionSize = std::max( 16u, to_U32( width / 10 ) );
                descriptor._useCurrentThread = true;
                descriptor._cbk = [&]( const Task* /*parent*/, const U32 start, const U32 end )
                {
                    U8 tempA = 0u;
                    for ( U32 i = start; i < end; ++i )
                    {
                        for ( I32 j = 0; j < height; ++j )
                        {
                            if ( _hasTransparency && (_hasTranslucency || hasTransulenctOrOpaquePixels) )
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
                                        hasTransulenctOrOpaquePixels = true;
                                        return;
                                    }
                                }
                            }
                            else if ( tempA > 250 )
                            {
                                hasTransulenctOrOpaquePixels = true;
                            }
                        }
                    }
                };
                if ( _hasTransparency && !_hasTranslucency && !hasTransulenctOrOpaquePixels )
                {
                    // All the alpha values are 0, so this channel is useless.
                    _hasTransparency = _hasTranslucency = false;
                }
                parallel_for( _context.context(), descriptor );
                metadataCache << BYTE_BUFFER_VERSION;
                metadataCache << _hasTransparency;
                metadataCache << _hasTranslucency;
                if ( !metadataCache.dumpToFile( cachePath, cacheName ) )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
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
        if ( _descriptor.msaaSamples() != newSampleCount )
        {
            _descriptor.msaaSamples( newSampleCount );
            createWithData( nullptr, 0u, { width(), height(), depth() }, {});
        }
    }

    void Texture::validateDescriptor()
    {
        if (_descriptor.packing() == GFXImagePacking::NORMALIZED_SRGB )
        {
            bool valid = false;
            switch ( _descriptor.baseFormat() )
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
                    valid = _descriptor.dataType() == GFXDataFormat::UNSIGNED_BYTE;
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

            DIVIDE_ASSERT(valid, "SRGB textures are only supported for RGB/BGR(A) normalized formats!" );
        }
        else if ( IsDepthTexture(_descriptor.packing()) )
        {
            DIVIDE_ASSERT(_descriptor.baseFormat() == GFXImageFormat::RED, "Depth textures only supported for single channel formats");
        }

        // We may have a 1D texture
        DIVIDE_ASSERT( _width > 0u && _height > 0u );
        {
            //http://www.opengl.org/registry/specs/ARB/texture_non_power_of_two.txt
            if ( descriptor().mipMappingState() != TextureDescriptor::MipMappingState::OFF )
            {
                _mipCount = to_U16( std::floorf( std::log2f( std::fmaxf( to_F32( _width ), to_F32( _height ) ) ) ) ) + 1;
            }
            else
            {
                _mipCount = 1u;
            }
        }
    }

    ImageView Texture::getView() const noexcept
    {
        const U16 layerCount = _descriptor.texType() == TextureType::TEXTURE_3D ? 1u : _depth;

        ImageView view{};
        view._srcTexture = this;
        view._descriptor._msaaSamples = _descriptor.msaaSamples();
        view._descriptor._dataType = _descriptor.dataType();
        view._descriptor._baseFormat = _descriptor.baseFormat();
        view._descriptor._packing = _descriptor.packing();
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
