#include "stdafx.h"

#include "Headers/NoneWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

namespace Divide
{
    NONE_API::NONE_API( GFXDevice& context ) noexcept
        : _context( context )
    {
    }

    void NONE_API::idle( [[maybe_unused]] const bool fast ) noexcept
    {
    }

    bool NONE_API::drawToWindow( DisplayWindow& window )
    {
        SDL_RenderClear( _renderer );
        return true;
    }

    void NONE_API::flushWindow( DisplayWindow& window, [[maybe_unused]] const bool isRenderThread )
    {
        static constexpr U32 ChangeTimerInSeconds = 3;

        static auto beginTimer = std::chrono::high_resolution_clock::now();

        static I32 w = -1, offsetX = 5;
        static I32 h = -1, offsetY = 5;

        if ( w == -1 )
        {
            SDL_QueryTexture( _texture, NULL, NULL, &w, &h );
        }

        const I32 windowW = to_I32( window.getDimensions().width ), windowH = window.getDimensions().height;
        if ( windowW < w || windowH < h )
        {
            SDL_RenderCopy( _renderer, _texture, NULL, NULL );
        }
        else
        {
            const auto currentTimer = std::chrono::high_resolution_clock::now();

            if ( std::chrono::duration_cast<std::chrono::seconds>(currentTimer - beginTimer).count() >= ChangeTimerInSeconds )
            {
                beginTimer = currentTimer;

                offsetX = Random( 5, windowW - w );
                offsetY = Random( 5, windowH - h );
            }

            SDL_Rect dstrect = { offsetX, offsetY, w, h };
            SDL_RenderCopy( _renderer, _texture, NULL, &dstrect );
        }
        SDL_RenderPresent( _renderer );
    }

    bool NONE_API::frameStarted()
    {
        return true;
    }

    bool NONE_API::frameEnded()
    {
        return true;
    }

    ErrorCode NONE_API::initRenderingAPI( [[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config ) noexcept
    {
        DIVIDE_ASSERT( _renderer == nullptr );

        const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();
        _renderer = SDL_CreateRenderer( window.getRawWindow(), -1, 0u );
        if ( _renderer == nullptr )
        {
            return ErrorCode::SDL_WINDOW_INIT_ERROR;
        }

        _background = SDL_LoadBMP( (Paths::g_assetsLocation + Paths::g_imagesLocation + "/divideLogo.bmp").c_str() );
        if ( _background == nullptr )
        {
            return ErrorCode::PLATFORM_INIT_ERROR;
        }

        _texture = SDL_CreateTextureFromSurface( _renderer, _background );
        if ( _texture == nullptr )
        {
            return ErrorCode::PLATFORM_INIT_ERROR;
        }

        SDL_SetRenderDrawColor( _renderer,
                                DefaultColours::DIVIDE_BLUE_U8.r,
                                DefaultColours::DIVIDE_BLUE_U8.g,
                                DefaultColours::DIVIDE_BLUE_U8.b,
                                DefaultColours::DIVIDE_BLUE_U8.a );

        SDL_RenderCopy( _renderer, _texture, NULL, NULL );

        return ErrorCode::NO_ERR;
    }

    void NONE_API::closeRenderingAPI() noexcept
    {
        DIVIDE_ASSERT( _renderer != nullptr );

        SDL_DestroyTexture( _texture );
        SDL_FreeSurface( _background );
        SDL_DestroyRenderer( _renderer );
        _texture = nullptr;
        _background = nullptr;
        _renderer = nullptr;
    }

    void NONE_API::flushCommand( [[maybe_unused]] GFX::CommandBase* cmd ) noexcept
    {
    }

    void NONE_API::preFlushCommandBuffer( [[maybe_unused]] const GFX::CommandBuffer& commandBuffer )
    {
    }

    void NONE_API::postFlushCommandBuffer( [[maybe_unused]] const GFX::CommandBuffer& commandBuffer ) noexcept
    {
    }

    bool NONE_API::setViewportInternal( [[maybe_unused]] const Rect<I32>& newViewport ) noexcept
    {
        return true;
    } 
    
    bool NONE_API::setScissorInternal( [[maybe_unused]] const Rect<I32>& newScissor ) noexcept
    {
        return true;
    }

    void NONE_API::onThreadCreated( [[maybe_unused]] const std::thread::id& threadID, [[maybe_unused]] const bool isMainRenderThread ) noexcept
    {
    }

    bool NONE_API::bindShaderResources( [[maybe_unused]] const DescriptorSetEntries& descriptorSetEntries )
    {
        return true;
    }

    void NONE_API::initDescriptorSets()
    {

    }

    RenderTarget_uptr NONE_API::newRT( const RenderTargetDescriptor& descriptor ) const
    {
        return eastl::make_unique<noRenderTarget>( _context, descriptor );
    }

    GenericVertexData_ptr NONE_API::newGVD( U32 ringBufferLength, bool renderIndirect, const Str256& name ) const
    {
        return std::make_shared<noGenericVertexData>( _context, ringBufferLength, renderIndirect, name.c_str() );
    }

    Texture_ptr NONE_API::newTexture( size_t descriptorHash, const Str256& resourceName, const ResourcePath& assetNames, const ResourcePath& assetLocations, const TextureDescriptor& texDescriptor, ResourceCache& parentCache ) const
    {
        return std::make_shared<noTexture>( _context, descriptorHash, resourceName, assetNames, assetLocations, texDescriptor, parentCache );
    }

    ShaderProgram_ptr NONE_API::newShaderProgram( size_t descriptorHash, const Str256& resourceName, const Str256& assetName, const ResourcePath& assetLocation, const ShaderProgramDescriptor& descriptor, ResourceCache& parentCache ) const
    {
        return std::make_shared<noShaderProgram>( _context, descriptorHash, resourceName, assetName, assetLocation, descriptor, parentCache );
    }

    ShaderBuffer_uptr NONE_API::newSB( const ShaderBufferDescriptor& descriptor ) const
    {
        return eastl::make_unique<noUniformBuffer>( _context, descriptor );
    }

}; //namespace Divide