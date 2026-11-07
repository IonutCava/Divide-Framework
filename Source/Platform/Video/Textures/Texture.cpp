// Copyright (c) 2026 DIVIDE-Studio
#include "Headers/Texture.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/DisplayManager.h"
#include "Core/Headers/Kernel.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Utility/Headers/DDSCache.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    namespace
    {
        constexpr U16 TRANSPARENCY_CACHE_VERSION = 2u;
        constexpr size_t TRANSPARENCY_CACHE_SIZE = sizeof(U16) + sizeof(U64) + sizeof(U8) * 4u;
        Mutex s_transparencyCacheMutex;
    }

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
    std::array<Handle<Texture>, to_base( TextureType::COUNT ) * 4u> Texture::s_defaultTextures;
    bool Texture::s_useDDSCache = true;

    void Texture::OnStartup()
    {
        ImageTools::OnStartup( false );

        s_defaultTextures.fill( INVALID_HANDLE<Texture> );
        for ( U8 type = 0u; type < to_base( TextureType::COUNT ); ++type )
        {
            for ( U8 format = 0u; format < 4u; ++format )
            {
                if ( format == 3u && static_cast<TextureType>(type) == TextureType::TEXTURE_3D )
                {
                    // GLSL has no sampler3DShadow.
                    continue;
                }
                const GFXDataFormat sampledDataType = format == 1u ? GFXDataFormat::SIGNED_INT
                                                   : format == 2u ? GFXDataFormat::UNSIGNED_INT : GFXDataFormat::UNSIGNED_BYTE;
                const TextureDescriptor descriptor = DefaultTextureDescriptor( static_cast<TextureType>(type), sampledDataType, format == 3u );
                const size_t index = type * 4u + format;
                s_defaultTextures[index] = CreateResource( ResourceDescriptor<Texture>(
                    Util::StringFormat("defaultEmptyTexture_{}_{}", type, format).c_str(), descriptor ) );
                Get(s_defaultTextures[index])->createDefaultData();
            }
        }
        s_defaultTexture2D = DefaultTexture( TextureType::TEXTURE_2D, GFXDataFormat::UNSIGNED_BYTE, false );
        s_defaultTexture2DArray = DefaultTexture( TextureType::TEXTURE_2D_ARRAY, GFXDataFormat::UNSIGNED_BYTE, false );

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
        for ( Handle<Texture>& texture : s_defaultTextures )
        {
            if ( texture != INVALID_HANDLE<Texture> )
            {
                DestroyResource(texture);
            }
        }
        s_defaultTexture2D = s_defaultTexture2DArray = INVALID_HANDLE<Texture>;
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

    Handle<Texture> Texture::DefaultTexture( const TextureType type, const GFXDataFormat sampledDataType, const bool depth ) noexcept
    {
        DIVIDE_ASSERT( type < TextureType::COUNT );
        const U8 format = depth ? 3u : sampledDataType == GFXDataFormat::SIGNED_INT ? 1u
                                    : sampledDataType == GFXDataFormat::UNSIGNED_INT ? 2u : 0u;
        const Handle<Texture> texture = s_defaultTextures[to_base(type) * 4u + format];
        DIVIDE_ASSERT( texture != INVALID_HANDLE<Texture> );
        return texture;
    }

    TextureDescriptor Texture::DefaultTextureDescriptor( const TextureType type, const GFXDataFormat sampledDataType, const bool depth ) noexcept
    {
        DIVIDE_ASSERT( type < TextureType::COUNT );
        DIVIDE_ASSERT( !depth || type != TextureType::TEXTURE_3D );
        TextureDescriptor descriptor{};
        descriptor._texType = type;
        descriptor._layerCount = 1u;
        descriptor._mipMappingState = MipMappingState::OFF;
        if ( depth )
        {
            descriptor._dataType = GFXDataFormat::FLOAT_32;
            descriptor._baseFormat = GFXImageFormat::RED;
            descriptor._packing = GFXImagePacking::DEPTH;
        }
        else if ( sampledDataType == GFXDataFormat::SIGNED_INT || sampledDataType == GFXDataFormat::UNSIGNED_INT )
        {
            descriptor._dataType = sampledDataType;
            descriptor._packing = GFXImagePacking::UNNORMALIZED;
        }
        return descriptor;
    }

    void Texture::createDefaultData()
    {
        const U16 faces = IsCubeTexture(_descriptor._texType) ? 6u : 1u;
        const U8 bytesPerPixel = GetBytesPerPixel( _descriptor._dataType, _descriptor._baseFormat, _descriptor._packing );
        vector<Byte> data(bytesPerPixel, to_byte(0u));
        if ( _descriptor._packing == GFXImagePacking::NORMALIZED )
        {
            data[3u] = to_byte(1u);
        }
        else if ( _descriptor._packing == GFXImagePacking::UNNORMALIZED )
        {
            const U32 alpha = 1u;
            std::memcpy( data.data() + 3u * sizeof(U32), &alpha, sizeof(alpha) );
        }
        ImageTools::ImageData image{};
        DIVIDE_EXPECTED_CALL( image.loadFromMemory( data, vec3<U16>(1u), faces, numChannels() ) );
        ImageUsage usage = prepareTextureData( vec3<U16>(1u), 1u, true );
        loadDataInternal( image, {._alignment = 1u} );
        usage = ImageUsage::SHADER_READ;
        submitTextureData(usage);
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
        if ( HasUsageFlagSet(_descriptor, ImageUsage::SHADER_READ_WRITE) )
        {
            AddImageUsageFlag(_descriptor, ImageUsage::SHADER_READ);
        }

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
                        _descriptor._layerCount = TextureLayersForImageLayers( _descriptor._texType, targetLayers );
                    }

                    DIVIDE_ASSERT( _descriptor._layerCount == TextureLayersForImageLayers( _descriptor._texType, targetLayers ) );

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

    bool Texture::HasValidFileLayerCount( const TextureDescriptor& descriptor, const U16 imageLayers ) noexcept
    {
        if ( IsCubeTexture( descriptor._texType ) )
        {
            return imageLayers > 0u && imageLayers % 6u == 0u;
        }

        return IsArrayTexture( descriptor._texType ) ? imageLayers == descriptor._layerCount : imageLayers == 1u;
    }

    U16 Texture::TextureLayersForImageLayers( const TextureType type, const U16 imageLayers ) noexcept
    {
        return IsCubeTexture( type ) ? to_U16( imageLayers / 6u ) : imageLayers;
    }

    U16 Texture::ResolveMipCount( const vec3<U16>& dimensions, const MipMappingState state, const U16 suppliedMipCount ) noexcept
    {
        if ( state == MipMappingState::OFF )
        {
            return 1u;
        }

        const U16 longestSide = std::max( { dimensions.width, dimensions.height, dimensions.depth } );
        const U16 fullMipCount = MipCount( longestSide, U16{ 1u } );
        return state == MipMappingState::MANUAL && suppliedMipCount > 0u
             ? std::min( suppliedMipCount, fullMipCount )
             : fullMipCount;
    }

    bool Texture::GetViewLayerRange( const TextureType sourceType, TextureType targetType, const U16 textureLayers,
                                     const SubRange requested, SubRange& rangeOut ) noexcept
    {
        if ( targetType == TextureType::COUNT )
        {
            targetType = sourceType;
        }

        U32 availableLayers = sourceType == TextureType::TEXTURE_3D ? 1u : textureLayers;
        if ( IsCubeTexture( sourceType ) && !IsCubeTexture( targetType ) )
        {
            availableLayers *= 6u;
        }

        if ( availableLayers >= ALL_LAYERS || requested._offset >= availableLayers || requested._count == 0u )
        {
            return false;
        }

        const bool arrayView = IsArrayTexture( targetType );
        const U32 selectedCount = requested._count == ALL_LAYERS
                                ? (arrayView ? availableLayers - requested._offset : 1u)
                                : requested._count;
        if ( selectedCount > availableLayers - requested._offset || (!arrayView && selectedCount != 1u) )
        {
            return false;
        }

        rangeOut = { requested._offset, to_U16( selectedCount ) };
        return true;
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
            TextureTransparencyInfo transparency{};

            bool hasValidEntries = false;
            GFXImageFormat layerFormat = GFXImageFormat::COUNT;
            GFXDataFormat layerDataType = GFXDataFormat::COUNT;
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
                if ( !loadFile( currentTextureFullPath, Util::Trim(currentTextureFile), dataStorage, transparency ) )
                {
                    return false;
                }

                if ( hasValidEntries && (dataStorage.format() != layerFormat || dataStorage.dataType() != layerDataType) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_LAYER_FORMAT" ), resourceName().c_str(), currentTextureFile );
                    return false;
                }
                layerFormat = dataStorage.format();
                layerDataType = dataStorage.dataType();
                hasValidEntries = true;
            }

            if ( !hasValidEntries )
            {
                return false;
            }

            if ( !HasValidFileLayerCount( _descriptor, dataStorage.layerCount() ) )
            {
                Console::errorfn( LOCALE_STR( IsCubeTexture( _descriptor._texType ) ?
                                             "ERROR_TEXTURE_LOADER_CUBMAP_INIT_COUNT" :
                                             "ERROR_TEXTURE_LOADER_ARRAY_INIT_COUNT" ), resourceName().c_str() );
                return false;
            }

            _hasTransparency = transparency.hasTransparency();
            _hasTranslucency = transparency.hasTranslucency();
            _descriptor._baseFormat = dataStorage.format();
            _descriptor._dataType = dataStorage.dataType();
            if ( createWithData( dataStorage, {} ) != ImageUsage::SHADER_READ )
            {
                return false;
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

    bool Texture::loadFile( const ResourcePath& path, const std::string_view name, ImageTools::ImageData& fileData,
                            TextureTransparencyInfo& transparency )
    {
        const bool srgb = _descriptor._packing == GFXImagePacking::NORMALIZED_SRGB;
        const U16 firstLayer = fileData.layerCount();

        if ( !fileExists( path / name ) || !fileData.loadFromFile( _context.context(), srgb, _width, _height, path, name, _descriptor._textureOptions ) )
        {
            if ( fileData.layerCount() > 0 )
            {
                Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LAYER_LOAD" ), name );
                return false;
            }

            Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOAD" ), name );
            if ( !fileData.loadFromFile( _context.context(), srgb, _width, _height, Paths::g_texturesLocation, s_missingTextureFileName ) )
            {
                return false;
            }
        }

        return CheckTextureTransparency( _context.context(), _descriptor._textureOptions._alphaChannelTransparency,
                                         path, name, fileData, firstLayer, transparency );
    }

    ImageUsage Texture::prepareTextureData( const vec3<U16>& dimensions, const U16 layers, const bool makeImmutable, const U16 suppliedMipCount )
    {
        _width  = dimensions.x;
        _height = dimensions.y;
        _depth  = dimensions.z;
        _layerCount = layers;

        DIVIDE_ASSERT( _width > 0u && _height > 0u && _depth > 0u && _layerCount > 0u, "Texture error: Invalid texture dimensions!" );

        validateDescriptor( makeImmutable, suppliedMipCount );
        return ImageUsage::UNDEFINED;
    }

    void Texture::submitTextureData(ImageUsage& crtUsageInOut)
    {
        DIVIDE_ASSERT(crtUsageInOut != ImageUsage::UNDEFINED);
    }

    ImageUsage Texture::createWithData(const ImageTools::ImageData& imageData, const PixelAlignment& pixelUnpackAlignment)
    {
        PROFILE_SCOPE_AUTO(Profiler::Category::Graphics);

        if ( !ImageTools::HaveCompatibleMipChains( imageData.imageLayers() ) ||
             (IsCubeTexture( _descriptor._texType ) && imageData.layerCount() % 6u != 0u) )
        {
            Console::errorfn( LOCALE_STR( "ERROR_TEXTURE_LOADER_MIP_CHAINS" ), resourceName().c_str() );
            return ImageUsage::UNDEFINED;
        }

        const bool uploadsImageData = !IsRenderTargetAttachment( descriptor() );
        const vec3<U16> dimensions = imageData.dimensions( 0u, 0u );
        const bool compressed = IsCompressed( _descriptor._baseFormat );
        const bool autoCompressed = uploadsImageData && compressed && _descriptor._mipMappingState == MipMappingState::AUTO;
        if ( autoCompressed )
        {
            const U16 fullMipCount = ResolveMipCount( dimensions, MipMappingState::AUTO, 0u );
            if ( imageData.mipCount() < fullMipCount && !_descriptor._textureOptions._skipMipMaps )
            {
                Console::warnfn( "Compressed texture '{}' supplies {} of {} mip levels; sampling is limited to the supplied levels.",
                                 resourceName().c_str(), imageData.mipCount(), fullMipCount );
            }
            _descriptor._mipMappingState = MipMappingState::MANUAL;
        }

        const bool writableMips = _descriptor._allowRegionUpdates ||
                                  HasUsageFlagSet( _descriptor, ImageUsage::SHADER_WRITE ) ||
                                  HasUsageFlagSet( _descriptor, ImageUsage::SHADER_READ_WRITE );
        const bool useSuppliedMipCount = uploadsImageData && !imageData.hasDummyData() &&
                                         (autoCompressed || (!writableMips && (_loadedFromFile || compressed)));
        const U16 textureLayers = TextureLayersForImageLayers( _descriptor._texType, imageData.layerCount() );
        ImageUsage ret = prepareTextureData( dimensions, textureLayers, true, useSuppliedMipCount ? imageData.mipCount() : 0u );
        DIVIDE_UNUSED(ret);

        if ( uploadsImageData )
        {
            loadDataInternal(imageData, pixelUnpackAlignment);

            ret = ImageUsage::SHADER_READ;
        }

        submitTextureData(ret);

        return ret;
    }

    ImageUsage Texture::createWithData( const std::span<const Byte> data, const vec3<U16>& dimensions, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        // This should never be called for compressed textures
        DIVIDE_ASSERT( !IsCompressed( _descriptor._baseFormat ) );
        // We can't manually specify data for msaa textures.
        DIVIDE_ASSERT( _descriptor._msaaSamples == 0u || data.empty());
        // We need a depth of at least 1 and at least 1 layer
        DIVIDE_ASSERT(dimensions.depth > 0u && _descriptor._layerCount > 0u);

        // ImageData uses a layer per cube face so we need to adjust the layer count accordingly
        const U16 faces = _descriptor._layerCount * (IsCubeTexture(_descriptor._texType) ? 6u : 1u);

        ImageTools::ImageData imgData{};
        if ( imgData.loadFromMemory(data, vec3<U16>(dimensions.width, dimensions.height, dimensions.depth), faces, GetBytesPerPixel(_descriptor._dataType, _descriptor._baseFormat, _descriptor._packing)))
        {
            return createWithData(imgData, pixelUnpackAlignment);
        }

        return ImageUsage::UNDEFINED;
    }

    bool Texture::IsValidUpdateRegion( const TextureType type, const vec3<U16>& dimensions, const U16 layers, const U16 mipCount,
                                       const U16 targetMip, const vec3<U16>& offset, const vec3<U16>& range ) noexcept
    {
        if ( type >= TextureType::COUNT || targetMip == ALL_MIPS || targetMip >= mipCount || targetMip >= 16u ||
             range.x == 0u || range.y == 0u || range.z == 0u )
        {
            return false;
        }

        const U32 width = std::max<U32>( 1u, dimensions.x >> targetMip );
        const U32 height = Is1DTexture( type ) ? 1u : std::max<U32>( 1u, dimensions.y >> targetMip );
        const U32 depth = Is3DTexture( type ) ? std::max<U32>( 1u, dimensions.z >> targetMip )
                        : SupportsZOffsetTexture( type ) ? U32( layers ) * (IsCubeTexture( type ) ? 6u : 1u) : 1u;
        return dimensions.x > 0u && dimensions.y > 0u && dimensions.z > 0u &&
               U32( offset.x ) + range.x <= width &&
               U32( offset.y ) + range.y <= height &&
               U32( offset.z ) + range.z <= depth;
    }

    bool Texture::GetUpdateLayout( const TextureDescriptor& descriptor, const vec3<U16>& range,
                                  const PixelAlignment& alignment, TextureUpdateLayout& layout ) noexcept
    {
        layout = {};
        if ( range.x == 0u || range.y == 0u || range.z == 0u ||
             (alignment._alignment != 1u && alignment._alignment != 2u &&
              alignment._alignment != 4u && alignment._alignment != 8u) )
        {
            return false;
        }

        size_t rowBytes = 0u;
        size_t rowCount = range.y;
        size_t rowStride = 0u;
        size_t sourceOffset = 0u;
        if ( IsCompressed( descriptor._baseFormat ) )
        {
            // OpenGL's compressed sub-image calls consume tightly packed blocks, not pixel-store strides.
            if ( Is1DTexture( descriptor._texType ) || alignment._rowLength != 0u ||
                 alignment._skipPixels != 0u || alignment._skipRows != 0u )
            {
                return false;
            }
            const GFXImageFormat format = descriptor._baseFormat;
            const size_t blockBytes = format == GFXImageFormat::BC1 || format == GFXImageFormat::BC1a ||
                                      format == GFXImageFormat::BC4s || format == GFXImageFormat::BC4u ? 8u : 16u;
            rowBytes = ((size_t( range.x ) + 3u) / 4u) * blockBytes;
            rowCount = (rowCount + 3u) / 4u;
            rowStride = rowBytes;
        }
        else
        {
            const size_t bytesPerPixel = GetBytesPerPixel( descriptor._dataType, descriptor._baseFormat, descriptor._packing );
            const size_t rowLength = alignment._rowLength == 0u ? range.x : alignment._rowLength;
            // Pixel-store parameters must also fit OpenGL's signed integer API.
            constexpr size_t maxPixelStore = std::numeric_limits<I32>::max();
            if ( bytesPerPixel == 0u || rowLength > maxPixelStore ||
                 alignment._skipPixels > maxPixelStore || alignment._skipRows > maxPixelStore ||
                 alignment._skipPixels + range.x > rowLength ||
                 rowLength > (SIZE_MAX - (alignment._alignment - 1u)) / bytesPerPixel )
            {
                return false;
            }
            rowBytes = size_t( range.x ) * bytesPerPixel;
            rowStride = (rowLength * bytesPerPixel + alignment._alignment - 1u) & ~(alignment._alignment - 1u);
            const size_t pixelSkip = alignment._skipPixels * bytesPerPixel;
            const size_t skipRows = descriptor._texType == TextureType::TEXTURE_1D ? 0u : alignment._skipRows;
            if ( skipRows > (SIZE_MAX - pixelSkip) / rowStride )
            {
                return false;
            }
            sourceOffset = skipRows * rowStride + pixelSkip;
        }

        if ( rowCount > SIZE_MAX / rowStride )
        {
            return false;
        }
        const size_t sliceStride = rowCount * rowStride;
        if ( size_t( range.z ) > (SIZE_MAX - sourceOffset) / sliceStride )
        {
            return false;
        }
        layout._rowBytes = rowBytes;
        layout._rowStride = rowStride;
        layout._sliceStride = sliceStride;
        layout._sourceOffset = sourceOffset;
        layout._rowCount = rowCount;
        layout._stagingSize = rowBytes * rowCount * range.z;
        layout._requiredSize = sourceOffset + sliceStride * (range.z - 1u) + rowStride * (rowCount - 1u) + rowBytes;
        return true;
    }

    void Texture::replaceData( const std::span<const Byte> data, const vec3<U16>& offset, const vec3<U16>& range, const U16 targetMipLevel, const PixelAlignment& pixelUnpackAlignment )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( data.empty() || range.x == 0u || range.y == 0u || range.z == 0u )
        {
            return;
        }

        if ( targetMipLevel == ALL_MIPS && offset.x == 0u && offset.y == 0u && offset.z == 0u &&
             (range.x > _width || range.y > _height || range.z > _depth) )
        {
            DIVIDE_ASSERT(targetMipLevel == ALL_MIPS, "Texture::replaceData: ALL_MIPS must be used when replacing the entire texture data!");
            _descriptor._layerCount = range.z;
            createWithData(data, range, pixelUnpackAlignment);
        }
        else
        {
            TextureUpdateLayout layout;
            const bool valid = _descriptor._allowRegionUpdates && _descriptor._msaaSamples == 0u &&
                               IsValidUpdateRegion( _descriptor._texType, {_width, _height, _depth}, _layerCount, _mipCount, targetMipLevel, offset, range ) &&
                               GetUpdateLayout( _descriptor, range, pixelUnpackAlignment, layout ) && data.size() >= layout._requiredSize;
            DIVIDE_ASSERT( valid, "Texture::replaceData: invalid region or insufficient source data!" );
            if ( !valid )
            {
                return;
            }
            if ( IsCompressed( _descriptor._baseFormat ) )
            {
                const U32 mipWidth = std::max<U32>( 1u, _width >> targetMipLevel );
                const U32 mipHeight = std::max<U32>( 1u, _height >> targetMipLevel );
                const bool blockAligned = offset.x % 4u == 0u && offset.y % 4u == 0u &&
                                          (range.x % 4u == 0u || U32( offset.x ) + range.x == mipWidth) &&
                                          (range.y % 4u == 0u || U32( offset.y ) + range.y == mipHeight);
                DIVIDE_ASSERT( blockAligned, "Texture::replaceData: compressed regions must align to blocks or mip edges!" );
                if ( !blockAligned )
                {
                    return;
                }
            }

            loadDataInternal( IsCompressed( _descriptor._baseFormat ) ? data.first( layout._stagingSize ) : data,
                              targetMipLevel, offset, range, pixelUnpackAlignment );
        }
    }

    bool CheckTextureTransparency( PlatformContext& context, const bool inspectAlpha, const ResourcePath& path,
                                   const std::string_view name, const ImageTools::ImageData& fileData,
                                   const U16 firstLayer, TextureTransparencyInfo& transparency )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if ( !inspectAlpha )
        {
            return true;
        }

        if ( firstLayer >= fileData.layerCount() )
        {
            Console::errorfn( "Texture '{}' did not provide an image layer for alpha inspection.", name );
            return false;
        }

        TextureTransparencyInfo fileTransparency{};
        if ( fileData.hasDummyAlphaChannel() || !HasAlphaChannel( fileData.format() ) )
        {
            fileTransparency._hasVisiblePixel = true;
            transparency.merge( fileTransparency );
            return true;
        }

        const ResourcePath cachePath = Paths::Textures::g_metadataLocation / path;
        ByteBuffer metadataCache;
        std::error_code fileError;
        const auto sourceTime = std::filesystem::last_write_time( fileData.decodedPath().fileSystemPath(), fileError );
        U64 sourceSize = 0u;
        if ( !fileError )
        {
            sourceSize = to_U64( std::filesystem::file_size( fileData.decodedPath().fileSystemPath(), fileError ) );
        }
        const bool cacheAvailable = !fileError;
        U64 sourceFingerprint = 0u;
        string cacheName;
        bool cacheHit = false;
        if ( cacheAvailable )
        {
            const vec3<U16>& dimensions = fileData.dimensions( firstLayer, 0u );
            const string signature = Util::StringFormat( "{}|{}|{}|{}|{}|{}|{}|{}|{}",
                                                         fileData.decodedPath(), sourceTime.time_since_epoch().count(), sourceSize,
                                                         to_base( fileData.format() ), to_base( fileData.dataType() ),
                                                         dimensions.width, dimensions.height, dimensions.depth,
                                                         fileData.layerCount() - firstLayer );
            sourceFingerprint = _ID( signature.c_str() );
            cacheName = Util::StringFormat( "{}.alpha-{}-{}.cache", name, TRANSPARENCY_CACHE_VERSION, sourceFingerprint );

            {
                LockGuard<Mutex> lock( s_transparencyCacheMutex );
                if ( fileExists( cachePath / cacheName ) )
                {
                    if ( metadataCache.loadFromFile( cachePath, cacheName ) &&
                         metadataCache.storageSize() == TRANSPARENCY_CACHE_SIZE )
                    {
                        U16 version = 0u;
                        U64 fingerprint = 0u;
                        metadataCache >> version >> fingerprint >> fileTransparency._nonOpaquePixels
                                      >> fileTransparency._hasVisiblePixel >> fileTransparency._hasTranslucentPixel;
                        cacheHit = version == TRANSPARENCY_CACHE_VERSION && fingerprint == sourceFingerprint &&
                                   fileTransparency._nonOpaquePixels <= TextureTransparencyInfo::PIXEL_THRESHOLD &&
                                   (!fileTransparency._hasTranslucentPixel ||
                                    (fileTransparency._hasVisiblePixel && fileTransparency._nonOpaquePixels > 0u));
                    }
                    if ( !cacheHit )
                    {
                        Console::warnfn( "Ignoring invalid transparency metadata '{}'.", cachePath / cacheName );
                        const FileError error = deleteFile( cachePath, cacheName );
                        if ( error != FileError::NONE && error != FileError::FILE_NOT_FOUND )
                        {
                            Console::warnfn( "Could not remove invalid transparency metadata '{}'.", cachePath / cacheName );
                        }
                    }
                }
            }
        }
        else
        {
            Console::warnfn( "Could not inspect texture source '{}' for transparency caching: {}.",
                             fileData.decodedPath(), fileError.message() );
        }

        if ( !cacheHit )
        {
            fileTransparency = {};
            for ( U32 layer = firstLayer; layer < fileData.layerCount() && !fileTransparency.hasTranslucency(); ++layer )
            {
                const vec3<U16>& dimensions = fileData.dimensions( layer, 0u );
                if ( dimensions.width == 0u || dimensions.height == 0u || dimensions.depth == 0u )
                {
                    Console::errorfn( "Texture '{}' has an empty image layer for alpha inspection.", name );
                    return false;
                }
                const U32 partitionSize = std::max( 16u, to_U32( dimensions.width / 10u ) );
                std::array<TextureTransparencyInfo, 11> partitions{};

                Parallel_For
                (
                    context.taskPool( TaskPoolType::HIGH_PRIORITY ),
                    ParallelForDescriptor
                    {
                        ._iterCount = dimensions.width,
                        ._partitionSize = partitionSize,
                        ._useCurrentThread = true
                    },
                    [&]( const Task* /*parent*/, const U32 start, const U32 end )
                    {
                        TextureTransparencyInfo local{};
                        for ( U32 x = start; x < end && !local.hasTranslucency(); ++x )
                        {
                            for ( U16 slice = 0u; slice < dimensions.depth && !local.hasTranslucency(); ++slice )
                            {
                                for ( I32 y = 0; y < dimensions.height; ++y )
                                {
                                    U8 alpha = 0u;
                                    fileData.getAlpha( to_I32( x ), y, alpha, layer, 0u, slice );
                                    local.add( alpha );
                                    if ( local.hasTranslucency() )
                                    {
                                        break;
                                    }
                                }
                            }
                        }
                        partitions[start / partitionSize] = local;
                    }
                );

                for ( const TextureTransparencyInfo& partition : partitions )
                {
                    fileTransparency.merge( partition );
                }
            }

            if ( cacheAvailable )
            {
                metadataCache.clear();
                metadataCache << TRANSPARENCY_CACHE_VERSION << sourceFingerprint << fileTransparency._nonOpaquePixels
                              << fileTransparency._hasVisiblePixel << fileTransparency._hasTranslucentPixel;
                LockGuard<Mutex> lock( s_transparencyCacheMutex );
                const ImageTools::DDSCache::WriteResult result = ImageTools::DDSCache::Write
                (
                    cachePath, cacheName, [&]( const ResourcePath& tempPath )
                    {
                        const FileNameAndPath temp = splitPathToNameAndLocation( tempPath );
                        return metadataCache.dumpToFile( temp._path, temp._fileName );
                    }
                );
                if ( result._status != ImageTools::DDSCache::WriteStatus::CREATED &&
                     result._status != ImageTools::DDSCache::WriteStatus::ALREADY_EXISTS )
                {
                    Console::warnfn( "Could not write transparency metadata '{}' (status {}, error {}).",
                                     cachePath / cacheName, to_base( result._status ), to_base( result._error ) );
                }
            }
        }

        transparency.merge( fileTransparency );
        Console::printfn( LOCALE_STR( "TEXTURE_HAS_TRANSPARENCY_TRANSLUCENCY" ),
                          name,
                          transparency.hasTransparency() ? "yes" : "no",
                          transparency.hasTranslucency() ? "yes" : "no" );

        return true;
    }

    void Texture::setSampleCount( U8 newSampleCount )
    {
        CLAMP( newSampleCount, U8_ZERO, DisplayManager::MaxMSAASamples() );
        if ( _descriptor._msaaSamples != newSampleCount )
        {
            _descriptor._msaaSamples = newSampleCount;
            createWithData( {}, { width(), height(), depth() }, {});
        }
    }

    void Texture::validateDescriptor( const bool makeImmutable, const U16 suppliedMipCount )
    {
        DIVIDE_UNUSED(makeImmutable);

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
                default: DIVIDE_UNEXPECTED_CALL();
            }

            DIVIDE_GPU_ASSERT(valid, "SRGB textures are only supported for RGB/BGR(A) normalized formats!" );
        }
        else if ( IsDepthTexture( _descriptor._packing) )
        {
            DIVIDE_ASSERT( _descriptor._baseFormat == GFXImageFormat::RED, "Depth textures only supported for single channel formats");
        }

        // We may have a 1D texture
        DIVIDE_ASSERT( _width > 0u && _height > 0u );
        _mipCount = ResolveMipCount( {_width, _height, _depth}, _descriptor._mipMappingState, suppliedMipCount );
    }

    namespace
    {
        SubRange ResolveViewLayers( const Texture& texture, const TextureType targetType, const SubRange requested )
        {
            SubRange range{};
            if ( !Texture::GetViewLayerRange( texture.descriptor()._texType, targetType, texture.layerCount(), requested, range ) )
            {
                Console::errorfn( "Invalid texture view layers for '{}': offset {}, count {}, available {}",
                                  texture.resourceName().c_str(), requested._offset, requested._count, texture.layerCount() );
                Console::Flush();
                std::abort();
            }
            return range;
        }
    }

    ImageView Texture::getView() const noexcept
    {
        const SubRange layerRange = ResolveViewLayers( *this, _descriptor._texType, { 0u, ALL_LAYERS } );

        return ImageView
        {
            ._descriptor = 
            {
                ._msaaSamples = _descriptor._msaaSamples,
                ._dataType    = _descriptor._dataType,
                ._baseFormat  = _descriptor._baseFormat,
                ._packing     = _descriptor._packing,
            },
            ._srcTexture = this,
            ._subRange = 
            {
                ._mipLevels = 
                {
                    ._count = _mipCount
                },
                ._layerRange = layerRange
            }
        };
    }

    ImageView Texture::getView( const TextureType targetType ) const noexcept
    {
        ImageView ret = getView();
        ret._targetType = targetType;
        ret._subRange._layerRange = ResolveViewLayers( *this, targetType, { 0u, ALL_LAYERS } );
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
        ret._subRange._layerRange = ResolveViewLayers( *this, _descriptor._texType, layerRange );
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
        ret._subRange._layerRange = ResolveViewLayers( *this, targetType, layerRange );
        return ret;
    }
};
