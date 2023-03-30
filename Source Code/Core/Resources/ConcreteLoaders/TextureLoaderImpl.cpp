#include "stdafx.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Textures/Headers/TextureDescriptor.h"

namespace Divide {


template<>
CachedResource_ptr ImplResourceLoader<Texture>::operator()() {
    assert(_descriptor.enumValue() < to_base(TextureType::COUNT));

    const std::shared_ptr<TextureDescriptor>& texDescriptor = _descriptor.propertyDescriptor<TextureDescriptor>();
    assert(texDescriptor != nullptr);

    if ( !_descriptor.assetName().empty() )
    {
        std::string resourceLocation = _descriptor.assetLocation().str();

        const bool isCubeMap = IsCubeTexture( texDescriptor->texType() );

        const U16 numCommas = to_U16(std::count(std::cbegin(_descriptor.assetName().str()),
                                                std::cend(_descriptor.assetName().str()),
                                                ','));
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

                DIVIDE_ASSERT(texDescriptor->layerCount() == targetLayers % 6);

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

                DIVIDE_ASSERT(texDescriptor->layerCount() == targetLayers, "TextureLoaderImpl error: Invalid number of source textures specified for texture array!");
            }
        }

        if (resourceLocation.empty())
        {
            _descriptor.assetLocation( Paths::g_assetsLocation + Paths::g_texturesLocation );
        }
        else
        {
            DIVIDE_ASSERT( std::count( std::cbegin( resourceLocation ), std::cend( resourceLocation ), ',' ) == 0u, "TextureLoaderImpl error: All textures for a single array must be loaded from the same location!");
        }
    }
    
    if ( texDescriptor->layerCount() == 0u )
    {
        texDescriptor->layerCount(1u);
    }

    Texture_ptr ptr = _context.gfx().newTexture(_loadingDescriptorHash,
                                                _descriptor.resourceName(),
                                                _descriptor.assetName(),
                                                _descriptor.assetLocation(),
                                                *texDescriptor,
                                                *_cache);

    if (!Load(ptr)) {
        Console::errorfn(Locale::Get(_ID("ERROR_TEXTURE_LOADER_FILE")),
                         _descriptor.assetLocation().c_str(),
                         _descriptor.assetName().c_str(),
                         _descriptor.resourceName().c_str());
        ptr.reset();
    }

    return ptr;
}

}
