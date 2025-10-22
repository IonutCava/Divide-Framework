

#include "Headers/NRIWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"

#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIResourceAllocator.h>
#include <Extensions/NRISwapChain.h>
#include <Extensions/NRIWrapperVK.h>
#if defined(WINDOWS_OS_BUILD)
#   include <Extensions/NRIWrapperD3D11.h>
#   include <Extensions/NRIWrapperD3D12.h>
#endif

namespace Divide
{
    NVIDIA_RENDER_INTERFACE_API::NVIDIA_RENDER_INTERFACE_API( GFXDevice& context, const RenderAPI API) noexcept
        : _context( context )
    {
        switch( API )
        {
#           if defined(WINDOWS_OS_BUILD)
                case RenderAPI::NRI_D3D12:  _nriAPI = nri::GraphicsAPI::D3D12;  break;
                case RenderAPI::NRI_D3D11:  _nriAPI = nri::GraphicsAPI::D3D11;  break;
#           endif
            case RenderAPI::NRI_Vulkan: _nriAPI = nri::GraphicsAPI::VK; break;
            case RenderAPI::NRI_None:   _nriAPI = nri::GraphicsAPI::NONE;   break;
            default:                    DIVIDE_UNEXPECTED_CALL();           break;
        }
    }

    void NVIDIA_RENDER_INTERFACE_API::idle( [[maybe_unused]] const bool fast ) noexcept
    {
    }

    bool NVIDIA_RENDER_INTERFACE_API::drawToWindow( [[maybe_unused]] DisplayWindow& window )
    {
        return true;
    }

    void NVIDIA_RENDER_INTERFACE_API::onRenderThreadLoopStart()
    {
    }

    void NVIDIA_RENDER_INTERFACE_API::onRenderThreadLoopEnd()
    {
    }

    void NVIDIA_RENDER_INTERFACE_API::prepareFlushWindow( [[maybe_unused]] DisplayWindow& window )
    {
        SDL_RenderClear( _renderer );
    }

    void NVIDIA_RENDER_INTERFACE_API::flushWindow( DisplayWindow& window )
    {
        constexpr U32 ChangeTimerInSeconds = 3;

        thread_local auto beginTimer = std::chrono::high_resolution_clock::now();

        static I32 w = -1.f, offsetX = 5;
        static I32 h = -1.f, offsetY = 5;

        if ( w == -1.f )
        {
            F32 width = 0.f, height = 0.f;
            SDL_GetTextureSize(_texture, &width, &height );
            w = to_I32(std::floor(width));
            h = to_I32(std::floor(height));
        }

        const I32 windowW = to_I32( window.getDimensions().width ), windowH = window.getDimensions().height;
        if ( windowW < w || windowH < h )
        {
            SDL_RenderTexture( _renderer, _texture, NULL, NULL );
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

            SDL_FRect dstrect = { to_F32(offsetX), to_F32(offsetY), to_F32(w), to_F32(h) };
            SDL_RenderTexture( _renderer, _texture, NULL, &dstrect );
        }
        SDL_RenderPresent( _renderer );
    }

    bool NVIDIA_RENDER_INTERFACE_API::frameStarted()
    {
        return true;
    }

    bool NVIDIA_RENDER_INTERFACE_API::frameEnded()
    {
        return true;
    }

    ErrorCode NVIDIA_RENDER_INTERFACE_API::initRenderingAPI( [[maybe_unused]] I32 argc, [[maybe_unused]] char** argv, [[maybe_unused]] Configuration& config ) noexcept
    {
        DIVIDE_ASSERT( _renderer == nullptr );

        const DisplayWindow& window = *_context.context().app().windowManager().mainWindow();
        _renderer = SDL_CreateRenderer( window.getRawWindow(), nullptr );
        if ( _renderer == nullptr )
        {
            return ErrorCode::SDL_WINDOW_INIT_ERROR;
        }

        const string logoPath = (Paths::g_imagesLocation / "divideLogo.bmp").string();
        _background = SDL_LoadBMP( logoPath.c_str() );
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

        SDL_RenderTexture( _renderer, _texture, NULL, NULL );

        return ErrorCode::NO_ERR;
    }

    void NVIDIA_RENDER_INTERFACE_API::closeRenderingAPI() noexcept
    {
        DIVIDE_ASSERT( _renderer != nullptr );

        SDL_DestroyTexture( _texture );
        SDL_DestroySurface( _background );
        SDL_DestroyRenderer( _renderer );
        _texture = nullptr;
        _background = nullptr;
        _renderer = nullptr;
    }

    void NVIDIA_RENDER_INTERFACE_API::flushCommand( [[maybe_unused]] GFX::CommandBase* cmd ) noexcept
    {
    }

    void NVIDIA_RENDER_INTERFACE_API::preFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer )
    {
    }

    void NVIDIA_RENDER_INTERFACE_API::postFlushCommandBuffer( [[maybe_unused]] const Handle<GFX::CommandBuffer> commandBuffer ) noexcept
    {
    }

    bool NVIDIA_RENDER_INTERFACE_API::setViewportInternal( [[maybe_unused]] const Rect<I32>& newViewport ) noexcept
    {
        return true;
    } 
    
    bool NVIDIA_RENDER_INTERFACE_API::setScissorInternal( [[maybe_unused]] const Rect<I32>& newScissor ) noexcept
    {
        return true;
    }

    void NVIDIA_RENDER_INTERFACE_API::onThreadCreated( [[maybe_unused]] const size_t threadIndex, [[maybe_unused]] const std::thread::id& threadID, [[maybe_unused]] const bool isMainRenderThread ) noexcept
    {
    }

    bool NVIDIA_RENDER_INTERFACE_API::bindShaderResources( [[maybe_unused]] const DescriptorSetEntries& descriptorSetEntries )
    {
        return true;
    }

    void NVIDIA_RENDER_INTERFACE_API::initDescriptorSets()
    {

    }

    RenderTarget_uptr NVIDIA_RENDER_INTERFACE_API::newRenderTarget( const RenderTargetDescriptor& descriptor ) const
    {
        return std::make_unique<nriRenderTarget>( _context, descriptor );
    }

    GPUBuffer_ptr NVIDIA_RENDER_INTERFACE_API::newGPUBuffer( U32 ringBufferLength, const std::string_view name ) const
    {
        return std::make_shared<nriGPUBuffer>( _context, ringBufferLength, name );
    }

    ShaderBuffer_uptr NVIDIA_RENDER_INTERFACE_API::newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const
    {
        return std::make_unique<nriUniformBuffer>( _context, descriptor );
    }

} //namespace Divide
