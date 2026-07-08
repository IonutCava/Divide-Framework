
#include "Headers/NRIWrapper.h"

#include "Core/Headers/Application.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/LockManager.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Utility/Headers/Localization.h"

#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRISwapChain.h>
#include <Extensions/NRIWrapperVK.h>
#if defined(WINDOWS_OS_BUILD)
#   include <Extensions/NRIWrapperD3D11.h>
#   include <Extensions/NRIWrapperD3D12.h>
#endif

#if defined(WINDOWS_OS_BUILD)
#   include <SDL3/SDL_properties.h>
#elif defined(LINUX_OS_BUILD)
#   include <SDL3/SDL_properties.h>
#endif

// Platform-specific window handle extraction requires SDL properties
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>

namespace Divide
{

// ---------------------------------------------------------------------------
// Format conversion helpers (Divide <-> NRI)
// ---------------------------------------------------------------------------
namespace
{
    nri::Format InternalFormat( const GFXImageFormat baseFormat,
                                const GFXDataFormat  dataType,
                                const GFXImagePacking packing ) noexcept
    {
        const bool sRGB       = (packing == GFXImagePacking::NORMALIZED_SRGB);
        const bool normalized = (packing == GFXImagePacking::NORMALIZED || sRGB);
        const bool floating   = (dataType == GFXDataFormat::FLOAT_16 || dataType == GFXDataFormat::FLOAT_32);
        const bool signed_    = (dataType == GFXDataFormat::SIGNED_BYTE   ||
                                 dataType == GFXDataFormat::SIGNED_SHORT  ||
                                 dataType == GFXDataFormat::SIGNED_INT    ||
                                 floating);

        switch ( baseFormat )
        {
            case GFXImageFormat::RED:
            {
                if ( normalized )
                {
                    if ( dataType == GFXDataFormat::UNSIGNED_BYTE  )  return sRGB ? nri::Format::UNKNOWN       : nri::Format::R8_UNORM;
                    if ( dataType == GFXDataFormat::SIGNED_BYTE    )  return nri::Format::R8_SNORM;
                    if ( dataType == GFXDataFormat::UNSIGNED_SHORT )  return nri::Format::R16_UNORM;
                    if ( dataType == GFXDataFormat::SIGNED_SHORT   )  return nri::Format::R16_SNORM;
                }
                else
                {
                    if ( dataType == GFXDataFormat::UNSIGNED_BYTE  )  return nri::Format::R8_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_BYTE    )  return nri::Format::R8_SINT;
                    if ( dataType == GFXDataFormat::UNSIGNED_SHORT )  return nri::Format::R16_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_SHORT   )  return nri::Format::R16_SINT;
                    if ( dataType == GFXDataFormat::UNSIGNED_INT   )  return nri::Format::R32_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_INT     )  return nri::Format::R32_SINT;
                    if ( dataType == GFXDataFormat::FLOAT_16       )  return nri::Format::R16_SFLOAT;
                    if ( dataType == GFXDataFormat::FLOAT_32       )  return nri::Format::R32_SFLOAT;
                }
                break;
            }
            case GFXImageFormat::RG:
            {
                if ( normalized )
                {
                    if ( dataType == GFXDataFormat::UNSIGNED_BYTE  )  return nri::Format::RG8_UNORM;
                    if ( dataType == GFXDataFormat::SIGNED_BYTE    )  return nri::Format::RG8_SNORM;
                    if ( dataType == GFXDataFormat::UNSIGNED_SHORT )  return nri::Format::RG16_UNORM;
                    if ( dataType == GFXDataFormat::SIGNED_SHORT   )  return nri::Format::RG16_SNORM;
                }
                else
                {
                    if ( dataType == GFXDataFormat::UNSIGNED_BYTE  )  return nri::Format::RG8_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_BYTE    )  return nri::Format::RG8_SINT;
                    if ( dataType == GFXDataFormat::UNSIGNED_SHORT )  return nri::Format::RG16_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_SHORT   )  return nri::Format::RG16_SINT;
                    if ( dataType == GFXDataFormat::UNSIGNED_INT   )  return nri::Format::RG32_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_INT     )  return nri::Format::RG32_SINT;
                    if ( dataType == GFXDataFormat::FLOAT_16       )  return nri::Format::RG16_SFLOAT;
                    if ( dataType == GFXDataFormat::FLOAT_32       )  return nri::Format::RG32_SFLOAT;
                }
                break;
            }
            case GFXImageFormat::BGRA:
            {
                if ( dataType == GFXDataFormat::UNSIGNED_BYTE )
                    return sRGB ? nri::Format::BGRA8_SRGB : nri::Format::BGRA8_UNORM;
                break;
            }
            case GFXImageFormat::RGBA:
            {
                if ( normalized )
                {
                    if ( dataType == GFXDataFormat::UNSIGNED_BYTE  )  return sRGB ? nri::Format::RGBA8_SRGB : nri::Format::RGBA8_UNORM;
                    if ( dataType == GFXDataFormat::SIGNED_BYTE    )  return nri::Format::RGBA8_SNORM;
                    if ( dataType == GFXDataFormat::UNSIGNED_SHORT )  return nri::Format::RGBA16_UNORM;
                    if ( dataType == GFXDataFormat::SIGNED_SHORT   )  return nri::Format::RGBA16_SNORM;
                }
                else
                {
                    if ( dataType == GFXDataFormat::UNSIGNED_BYTE  )  return nri::Format::RGBA8_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_BYTE    )  return nri::Format::RGBA8_SINT;
                    if ( dataType == GFXDataFormat::UNSIGNED_SHORT )  return nri::Format::RGBA16_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_SHORT   )  return nri::Format::RGBA16_SINT;
                    if ( dataType == GFXDataFormat::UNSIGNED_INT   )  return nri::Format::RGBA32_UINT;
                    if ( dataType == GFXDataFormat::SIGNED_INT     )  return nri::Format::RGBA32_SINT;
                    if ( dataType == GFXDataFormat::FLOAT_16       )  return nri::Format::RGBA16_SFLOAT;
                    if ( dataType == GFXDataFormat::FLOAT_32       )  return nri::Format::RGBA32_SFLOAT;
                }
                break;
            }
            // Block-compressed formats
            case GFXImageFormat::BC1:     return nri::Format::BC1_RGBA_UNORM;
            case GFXImageFormat::BC1a:    return nri::Format::BC1_RGBA_UNORM;
            case GFXImageFormat::BC2:     return nri::Format::BC2_RGBA_UNORM;
            case GFXImageFormat::BC3:     return sRGB ? nri::Format::BC3_RGBA_SRGB : nri::Format::BC3_RGBA_UNORM;
            case GFXImageFormat::BC4u:    return nri::Format::BC4_R_UNORM;
            case GFXImageFormat::BC4s:    return nri::Format::BC4_R_SNORM;
            case GFXImageFormat::BC5u:    return nri::Format::BC5_RG_UNORM;
            case GFXImageFormat::BC5s:    return nri::Format::BC5_RG_SNORM;
            case GFXImageFormat::BC6u:    return nri::Format::BC6H_RGB_UFLOAT;
            case GFXImageFormat::BC6s:    return nri::Format::BC6H_RGB_SFLOAT;
            case GFXImageFormat::BC7:     return sRGB ? nri::Format::BC7_RGBA_SRGB : nri::Format::BC7_RGBA_UNORM;

            default: break;
        }

        return nri::Format::UNKNOWN;
    }

    nri::Format InternalDepthFormat( const GFXImageFormat baseFormat, const bool stencil ) noexcept
    {
        // Depth / depth-stencil formats
        if ( stencil )
        {
            return nri::Format::D24_UNORM_S8_UINT; // most commonly supported
        }
        return nri::Format::D32_SFLOAT;
    }

    // Populate the NRI window descriptor from an SDL window
    nri::Window MakeNRIWindow( SDL_Window* sdlWindow ) noexcept
    {
        nri::Window win{};

#if defined(WINDOWS_OS_BUILD)
        win.windows.hwnd = SDL_GetPointerProperty(
            SDL_GetWindowProperties( sdlWindow ),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr );
#elif defined(LINUX_OS_BUILD)
        // Try Wayland first, fall back to X11
        void* wlSurface = SDL_GetPointerProperty(
            SDL_GetWindowProperties( sdlWindow ),
            SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr );
        if ( wlSurface != nullptr )
        {
            win.wayland.surface  = wlSurface;
            win.wayland.display  = SDL_GetPointerProperty(
                SDL_GetWindowProperties( sdlWindow ),
                SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr );
        }
        else
        {
            win.x11.window = static_cast<uint64_t>(
                SDL_GetNumberProperty(
                    SDL_GetWindowProperties( sdlWindow ),
                    SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0 ));
            win.x11.dpy = SDL_GetPointerProperty(
                SDL_GetWindowProperties( sdlWindow ),
                SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr );
        }
#elif defined(MAC_OS_BUILD)
        // NRI expects a CAMetalLayer pointer on Apple platforms.
        // SDL3 exposes the NSWindow; a CAMetalLayer can be created from it at startup.
        // TODO: obtain CAMetalLayer from SDL_PROP_WINDOW_COCOA_WINDOW_POINTER
        win.metal.caMetalLayer = nullptr; // placeholder – requires Obj-C bridge
#endif
        return win;
    }

    void NRIMessageCallback( nri::Message messageType, const char* file, uint32_t line,
                             const char* message, [[maybe_unused]] void* userArg ) noexcept
    {
        switch ( messageType )
        {
            case nri::Message::INFO:
                Console::printfn( "[NRI] {}({}): {}", file, line, message );
                break;
            case nri::Message::WARNING:
                Console::warnfn( "[NRI] {}({}): {}", file, line, message );
                break;
            case nri::Message::ERROR:
                Console::errorfn( "[NRI] {}({}): {}", file, line, message );
                break;
            default:
                break;
        }
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// Public format-conversion interface (declared in nriResources.h)
// ---------------------------------------------------------------------------
nri::Format DivideFormatToNRI( GFXImageFormat baseFormat,
                               GFXDataFormat  dataType,
                               GFXImagePacking packing ) noexcept
{
    return InternalFormat( baseFormat, dataType, packing );
}

nri::Format DivideDepthFormatToNRI( GFXImageFormat baseFormat, bool stencil ) noexcept
{
    return InternalDepthFormat( baseFormat, stencil );
}

// ---------------------------------------------------------------------------
// NVIDIA_RENDER_INTERFACE_API
// ---------------------------------------------------------------------------
NVIDIA_RENDER_INTERFACE_API::NVIDIA_RENDER_INTERFACE_API( GFXDevice& context, const RenderAPI API ) noexcept
    : _context( context )
{
    switch ( API )
    {
#       if defined(WINDOWS_OS_BUILD)
            case RenderAPI::NRI_D3D12:  _nriAPI = nri::GraphicsAPI::D3D12; break;
            case RenderAPI::NRI_D3D11:  _nriAPI = nri::GraphicsAPI::D3D11; break;
#       endif
        case RenderAPI::NRI_Vulkan: _nriAPI = nri::GraphicsAPI::VK;   break;
        case RenderAPI::NRI_None:   _nriAPI = nri::GraphicsAPI::NONE; break;
        default:                    DIVIDE_UNEXPECTED_CALL();          break;
    }
}

// ---------------------------------------------------------------------------
// RenderAPIWrapper – lifecycle
// ---------------------------------------------------------------------------
ErrorCode NVIDIA_RENDER_INTERFACE_API::initRenderingAPI( [[maybe_unused]] I32 argc,
                                                          [[maybe_unused]] char** argv,
                                                          Configuration& config ) noexcept
{
    DIVIDE_GPU_ASSERT( _device == nullptr );

    const Configuration::Debug& dbg = config.debug;

    // -----------------------------------------------------------------
    // 1. Device creation
    // -----------------------------------------------------------------
    nri::DeviceCreationDesc deviceDesc{};
    deviceDesc.graphicsAPI                    = _nriAPI;
    deviceDesc.enableNRIValidation            = dbg.validation;
    deviceDesc.enableGraphicsAPIValidation    = dbg.validation;
    deviceDesc.enableMemoryZeroInitialization = false;

    // Callback so NRI messages appear in Divide's console
    nri::CallbackInterface callbacks{};
    callbacks.MessageCallback  = NRIMessageCallback;
    callbacks.AbortExecution   = nullptr;
    deviceDesc.callbackInterface = callbacks;

    // VK binding offsets: all at 0 because Divide generates SPIRV with
    // explicit binding decorations and does not rely on register remapping.
    deviceDesc.vkBindingOffsets = {};

    if ( nriCreateDevice( deviceDesc, _device ) != nri::Result::SUCCESS || _device == nullptr )
    {
        Console::errorfn( LOCALE_STR( "ERROR_GFX_DEVICE_INIT" ) );
        return ErrorCode::GFX_NON_SPECIFIED;
    }

    // -----------------------------------------------------------------
    // 2. Retrieve interface tables
    // -----------------------------------------------------------------
    // NRI interfaces are identified by plain name (without namespace).
    // Use "CoreInterface" etc. directly; sizeof supplies the version guard.
    if ( nriGetInterface( *_device, "CoreInterface", sizeof( nri::CoreInterface ), &_nri.core )
            != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] Failed to obtain CoreInterface" );
        return ErrorCode::GFX_NON_SPECIFIED;
    }
    if ( nriGetInterface( *_device, "HelperInterface", sizeof( nri::HelperInterface ), &_nri.helper )
            != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] Failed to obtain HelperInterface" );
        return ErrorCode::GFX_NON_SPECIFIED;
    }
    if ( nriGetInterface( *_device, "SwapChainInterface", sizeof( nri::SwapChainInterface ), &_nri.swapChain )
            != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] Failed to obtain SwapChainInterface" );
        return ErrorCode::GFX_NON_SPECIFIED;
    }

    // -----------------------------------------------------------------
    // 3. Graphics queue
    // -----------------------------------------------------------------
    if ( _nri.core.GetQueue( *_device, nri::QueueType::GRAPHICS, 0, _graphicsQueue )
            != nri::Result::SUCCESS || _graphicsQueue == nullptr )
    {
        Console::errorfn( "[NRI] Failed to obtain GRAPHICS queue" );
        return ErrorCode::GFX_NON_SPECIFIED;
    }

    // -----------------------------------------------------------------
    // 4. Per-frame command resources (triple-buffering)
    // -----------------------------------------------------------------
    for ( U32 i = 0u; i < NRI_BUFFERED_FRAME_COUNT; ++i )
    {
        NRIFrameResources& frame = _frameData[i];

        if ( _nri.core.CreateCommandAllocator( *_graphicsQueue, frame._commandAllocator )
                != nri::Result::SUCCESS )
        {
            Console::errorfn( "[NRI] Failed to create CommandAllocator for frame {}", i );
            return ErrorCode::GFX_NON_SPECIFIED;
        }

        if ( _nri.core.CreateCommandBuffer( *frame._commandAllocator, frame._commandBuffer )
                != nri::Result::SUCCESS )
        {
            Console::errorfn( "[NRI] Failed to create CommandBuffer for frame {}", i );
            return ErrorCode::GFX_NON_SPECIFIED;
        }

        if ( _nri.core.CreateFence( *_device, 0u, frame._frameFence )
                != nri::Result::SUCCESS )
        {
            Console::errorfn( "[NRI] Failed to create frame Fence for frame {}", i );
            return ErrorCode::GFX_NON_SPECIFIED;
        }
        frame._fenceValue = 0u;
    }

    // -----------------------------------------------------------------
    // 5. Swapchain for the main window
    // -----------------------------------------------------------------
    DisplayWindow* mainWindow = _context.context().app().windowManager().mainWindow();
    DIVIDE_GPU_ASSERT( mainWindow != nullptr );

    NRIPerWindowState& ws = _perWindowState[mainWindow->getGUID()];
    ws._window = mainWindow;
    initStatePerWindow( ws );

    // -----------------------------------------------------------------
    // 6. Populate DeviceInformation from the NRI device descriptor
    // -----------------------------------------------------------------
    const nri::DeviceDesc& nriDesc = _nri.core.GetDeviceDesc( *_device );

    DeviceInformation& info = GFXDevice::s_deviceInformation;
    info._maxTextureSize              = nriDesc.dimensions.texture2DMaxDim;
    info._max3DTextureSize            = nriDesc.dimensions.texture3DMaxDim;
    info._maxRTColourAttachments      = nriDesc.shaderStage.fragment.attachmentMaxNum;
    info._maxAnisotropy               = static_cast<U32>( nriDesc.other.samplerAnisotropyMax );
    info._maxSSBOBufferBindings       = nriDesc.descriptorSet.storageBufferMaxNum;
    info._offsetAlignmentBytesUBO     = nriDesc.memoryAlignment.constantBufferOffset;
    info._offsetAlignmentBytesSSBO    = nriDesc.memoryAlignment.bufferShaderResourceOffset;
    info._maxSizeBytesUBO             = nriDesc.memory.constantBufferMaxRange;
    info._maxWorkgroupCount[0]        = nriDesc.shaderStage.compute.workGroupMaxNum[0];
    info._maxWorkgroupCount[1]        = nriDesc.shaderStage.compute.workGroupMaxNum[1];
    info._maxWorkgroupCount[2]        = nriDesc.shaderStage.compute.workGroupMaxNum[2];
    info._maxWorkgroupSize[0]         = nriDesc.shaderStage.compute.workGroupMaxDim[0];
    info._maxWorkgroupSize[1]         = nriDesc.shaderStage.compute.workGroupMaxDim[1];
    info._maxWorkgroupSize[2]         = nriDesc.shaderStage.compute.workGroupMaxDim[2];
    info._maxWorkgroupInvocations     = nriDesc.shaderStage.compute.workGroupInvocationMaxNum;
    info._maxDrawIndirectCount        = nriDesc.other.drawIndirectMaxNum;

    // Vendor / renderer string
    const GPUVendor vendor = [&]() noexcept
    {
        switch ( nriDesc.adapterDesc.vendor )
        {
            case nri::Vendor::NVIDIA: return GPUVendor::NVIDIA;
            case nri::Vendor::AMD:    return GPUVendor::AMD;
            case nri::Vendor::INTEL:  return GPUVendor::INTEL;
            default:                  return GPUVendor::COUNT;
        }
    }();
    info._vendor = vendor;

    return ErrorCode::NO_ERR;
}

void NVIDIA_RENDER_INTERFACE_API::closeRenderingAPI() noexcept
{
    if ( _device == nullptr )
    {
        return;
    }

    // Wait for all GPU work to finish before destroying anything
    _nri.core.WaitForIdle( *_graphicsQueue );

    // Destroy per-window state
    for ( auto& [guid, state] : _perWindowState )
    {
        destroyStatePerWindow( state );
    }
    _perWindowState.clear();

    // Destroy descriptor pools
    for ( auto& pool : _descriptorPools )
    {
        if ( pool != nullptr )
        {
            _nri.core.DestroyDescriptorPool( *pool );
            pool = nullptr;
        }
    }

    // Destroy per-frame resources
    for ( NRIFrameResources& frame : _frameData )
    {
        if ( frame._frameFence )        { _nri.core.DestroyFence( *frame._frameFence );              frame._frameFence        = nullptr; }
        if ( frame._commandBuffer )     { _nri.core.DestroyCommandBuffer( *frame._commandBuffer );   frame._commandBuffer     = nullptr; }
        if ( frame._commandAllocator )  { _nri.core.DestroyCommandAllocator( *frame._commandAllocator ); frame._commandAllocator = nullptr; }
    }

    nriDestroyDevice( *_device );
    _device        = nullptr;
    _graphicsQueue = nullptr;
    _nri           = {};
}

void NVIDIA_RENDER_INTERFACE_API::idle( [[maybe_unused]] const bool fast ) noexcept
{
    if ( _graphicsQueue != nullptr )
    {
        _nri.core.WaitForIdle( *_graphicsQueue );
    }
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------
bool NVIDIA_RENDER_INTERFACE_API::frameStarted()
{
    LockManager::CleanExpiredSyncObjects( _context.renderAPI(), GFXDevice::FrameCount() );

    NRIFrameResources& frame = currentFrame();

    // Wait until this buffered slot is no longer used by the GPU
    if ( frame._fenceValue > 0u )
    {
        _nri.core.Wait( *frame._frameFence, frame._fenceValue );
    }

    // Reset command allocator (implicitly resets all command buffers allocated from it)
    _nri.core.ResetCommandAllocator( *frame._commandAllocator );

    return true;
}

bool NVIDIA_RENDER_INTERFACE_API::frameEnded()
{
    return true;
}

bool NVIDIA_RENDER_INTERFACE_API::drawToWindow( [[maybe_unused]] DisplayWindow& window )
{
    auto it = _perWindowState.find( window.getGUID() );
    if ( it == _perWindowState.end() )
    {
        return false;
    }

    NRIPerWindowState& ws = it->second;
    if ( ws._swapChain == nullptr )
    {
        return false;
    }

    // Acquire the next swapchain texture
    const nri::Result result = _nri.swapChain.AcquireNextTexture(
        *ws._swapChain,
        *ws._acquireSemaphore,
        ws._currentTextureIndex );

    if ( result == nri::Result::OUT_OF_DATE )
    {
        recreateSwapChain( ws );
        ws._skipEndFrame = true;
        return false;
    }

    if ( result != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] AcquireNextTexture failed (result {})", to_I32( result ) );
        return false;
    }

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
}

void NVIDIA_RENDER_INTERFACE_API::flushWindow( DisplayWindow& window )
{
    auto it = _perWindowState.find( window.getGUID() );
    if ( it == _perWindowState.end() )
    {
        return;
    }

    NRIPerWindowState& ws = it->second;

    if ( ws._skipEndFrame )
    {
        ws._skipEndFrame = false;
        return;
    }

    if ( ws._swapChain == nullptr )
    {
        return;
    }

    NRIFrameResources& frame = currentFrame();

    // ------------------------------------------------------------------
    // End command buffer recording
    // ------------------------------------------------------------------
    _nri.core.EndCommandBuffer( *frame._commandBuffer );

    // ------------------------------------------------------------------
    // Submit
    // ------------------------------------------------------------------
    const nri::CommandBuffer* cmdBuf = frame._commandBuffer;
    const uint64_t nextFenceVal = frame._fenceValue + 1u;

    nri::FenceSubmitDesc waitFenceDesc  = { ws._acquireSemaphore,  0u,           nri::StageBits::ALL };
    nri::FenceSubmitDesc signalFences[] = { { ws._releaseSemaphore, 0u,          nri::StageBits::ALL },
                                            { frame._frameFence, nextFenceVal,   nri::StageBits::ALL } };

    nri::QueueSubmitDesc submitDesc{};
    submitDesc.commandBufferNum = 1;
    submitDesc.commandBuffers   = &cmdBuf;
    submitDesc.waitFenceNum     = 1;
    submitDesc.waitFences       = &waitFenceDesc;
    submitDesc.signalFenceNum   = 2;
    submitDesc.signalFences     = signalFences;

    _nri.core.QueueSubmit( *_graphicsQueue, submitDesc );
    frame._fenceValue = nextFenceVal;

    // ------------------------------------------------------------------
    // Present
    // ------------------------------------------------------------------
    const nri::Result presentResult = _nri.swapChain.QueuePresent( *ws._swapChain, *ws._releaseSemaphore );

    if ( presentResult == nri::Result::OUT_OF_DATE )
    {
        recreateSwapChain( ws );
    }
    else if ( presentResult != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] QueuePresent failed (result {})", to_I32( presentResult ) );
    }

    advanceFrame();
}

// ---------------------------------------------------------------------------
// Command recording
// ---------------------------------------------------------------------------
void NVIDIA_RENDER_INTERFACE_API::preFlushCommandBuffer( [[maybe_unused]] Handle<GFX::CommandBuffer> commandBuffer )
{
    // Begin recording into the current frame's command buffer.
    // Each frame resets its command allocator in frameStarted().
    nri::CommandBuffer* cmd = currentCommandBuffer();
    DIVIDE_GPU_ASSERT( cmd != nullptr );

    _nri.core.BeginCommandBuffer( *cmd, nullptr /*descriptorPool*/ );
}

void NVIDIA_RENDER_INTERFACE_API::postFlushCommandBuffer( [[maybe_unused]] Handle<GFX::CommandBuffer> commandBuffer ) noexcept
{
    // Command buffer is ended in flushWindow (after all commands are recorded).
}

void NVIDIA_RENDER_INTERFACE_API::flushCommand( GFX::CommandBase* cmd ) noexcept
{
    DIVIDE_GPU_ASSERT( cmd != nullptr );
    nri::CommandBuffer* nriCmd = currentCommandBuffer();
    if ( nriCmd == nullptr )
    {
        return;
    }

    switch ( cmd->type() )
    {
        case GFX::CommandType::BEGIN_RENDER_PASS:
        {
            const auto* renderPassCmd = cmd->As<GFX::BeginRenderPassCommand>();
            // TODO: translate RenderPassParams to NRI AttachmentsDesc and call
            //       _nri.core.CmdBeginRendering(*nriCmd, attachmentsDesc).
            // This requires nriRenderTarget to expose NRI Texture objects.
            break;
        }
        case GFX::CommandType::END_RENDER_PASS:
        {
            _nri.core.CmdEndRendering( *nriCmd );
            break;
        }
        case GFX::CommandType::SET_VIEWPORT:
        case GFX::CommandType::PUSH_VIEWPORT:
        case GFX::CommandType::POP_VIEWPORT:
        {
            // Handled by GFXDevice before flushCommand; setViewportInternal is called directly.
            break;
        }
        case GFX::CommandType::SET_SCISSOR:
        {
            // Handled by setScissorInternal.
            break;
        }
        case GFX::CommandType::BIND_PIPELINE:
        {
            // TODO: build/cache NRI Pipeline and call _nri.core.CmdSetPipeline
            break;
        }
        case GFX::CommandType::BIND_SHADER_RESOURCES:
        {
            // Handled by bindShaderResources().
            break;
        }
        case GFX::CommandType::SEND_PUSH_CONSTANTS:
        {
            // TODO: _nri.core.CmdSetRootConstants
            break;
        }
        case GFX::CommandType::DRAW_COMMANDS:
        {
            // TODO: translate GenericDrawCommand -> CmdDraw / CmdDrawIndexed
            break;
        }
        case GFX::CommandType::DISPATCH_SHADER_TASK:
        {
            // TODO: _nri.core.CmdDispatch
            break;
        }
        case GFX::CommandType::BLIT_RT:
        {
            // TODO: _nri.core.CmdCopyTexture (or a helper blit)
            break;
        }
        case GFX::CommandType::COPY_TEXTURE:
        {
            // TODO: _nri.core.CmdCopyTexture
            break;
        }
        case GFX::CommandType::CLEAR_TEXTURE:
        {
            // TODO: _nri.core.CmdClearTexture
            break;
        }
        case GFX::CommandType::COMPUTE_MIPMAPS:
        {
            // TODO: generate mips via compute shader or helper
            break;
        }
        case GFX::CommandType::MEMORY_BARRIER:
        {
            // TODO: translate MemoryBarrierCommand -> nri::BarrierDesc and call CmdBarrier
            break;
        }
        case GFX::CommandType::BEGIN_DEBUG_SCOPE:
        {
            const auto* scopeCmd = cmd->As<GFX::BeginDebugScopeCommand>();
            nriBeginAnnotation( scopeCmd->_scopeName.c_str(), 0 );
            _nri.core.CmdBeginAnnotation( *nriCmd, scopeCmd->_scopeName.c_str(), 0 );
            break;
        }
        case GFX::CommandType::END_DEBUG_SCOPE:
        {
            nriEndAnnotation();
            _nri.core.CmdEndAnnotation( *nriCmd );
            break;
        }
        case GFX::CommandType::ADD_DEBUG_MESSAGE:
        {
            const auto* msgCmd = cmd->As<GFX::AddDebugMessageCommand>();
            _nri.core.CmdAnnotation( *nriCmd, msgCmd->_msg.c_str(), 0 );
            break;
        }
        case GFX::CommandType::BEGIN_GPU_QUERY:
        case GFX::CommandType::END_GPU_QUERY:
        {
            // TODO: QueryPool support
            break;
        }
        case GFX::CommandType::READ_TEXTURE:
        case GFX::CommandType::READ_BUFFER_DATA:
        case GFX::CommandType::CLEAR_BUFFER_DATA:
        {
            // TODO
            break;
        }
        case GFX::CommandType::SET_CAMERA:
        case GFX::CommandType::PUSH_CAMERA:
        case GFX::CommandType::POP_CAMERA:
        case GFX::CommandType::SET_CLIP_PLANES:
        {
            // Handled at higher level by GFXDevice; nothing to emit at API level.
            break;
        }
        case GFX::CommandType::COUNT:
        default:
        {
            DIVIDE_UNEXPECTED_CALL();
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Viewport / scissor
// ---------------------------------------------------------------------------
bool NVIDIA_RENDER_INTERFACE_API::setViewportInternal( const Rect<I32>& newViewport ) noexcept
{
    nri::CommandBuffer* cmd = currentCommandBuffer();
    if ( cmd == nullptr )
    {
        return false;
    }

    nri::Viewport vp{};
    vp.x              = to_F32( newViewport.x );
    vp.y              = to_F32( newViewport.y );
    vp.width          = to_F32( newViewport.z );
    vp.height         = to_F32( newViewport.w );
    vp.depthMin       = 0.f;
    vp.depthMax       = 1.f;
    vp.originBottomLeft = false; // NRI default is top-left (D3D convention)

    _nri.core.CmdSetViewports( *cmd, &vp, 1u );
    return true;
}

bool NVIDIA_RENDER_INTERFACE_API::setScissorInternal( const Rect<I32>& newScissor ) noexcept
{
    nri::CommandBuffer* cmd = currentCommandBuffer();
    if ( cmd == nullptr )
    {
        return false;
    }

    nri::Rect rect{};
    rect.x      = to_I16( newScissor.x );
    rect.y      = to_I16( newScissor.y );
    rect.width  = static_cast<nri::Dim_t>( newScissor.z );
    rect.height = static_cast<nri::Dim_t>( newScissor.w );

    _nri.core.CmdSetScissors( *cmd, &rect, 1u );
    return true;
}

// ---------------------------------------------------------------------------
// Thread / descriptor set management
// ---------------------------------------------------------------------------
void NVIDIA_RENDER_INTERFACE_API::onThreadCreated( [[maybe_unused]] const size_t threadIndex,
                                                    [[maybe_unused]] const std::thread::id& threadID,
                                                    [[maybe_unused]] const bool isMainRenderThread ) noexcept
{
}

void NVIDIA_RENDER_INTERFACE_API::initDescriptorSets()
{
    // TODO: create per-usage descriptor pools and pre-allocate sets (similar to VK backend).
    // Each DescriptorSetUsage gets its own pool with appropriate capacities.
    _descriptorPools.fill( nullptr );
}

bool NVIDIA_RENDER_INTERFACE_API::bindShaderResources( [[maybe_unused]] const DescriptorSetEntries& descriptorSetEntries )
{
    // TODO: translate DescriptorSetEntries to NRI descriptor set updates and call
    //       _nri.core.CmdSetDescriptorSet.
    return true;
}

// ---------------------------------------------------------------------------
// Resource factory methods
// ---------------------------------------------------------------------------
RenderTarget_uptr NVIDIA_RENDER_INTERFACE_API::newRenderTarget( const RenderTargetDescriptor& descriptor ) const
{
    return std::make_unique<nriRenderTarget>( _context, descriptor );
}

GPUBuffer_uptr NVIDIA_RENDER_INTERFACE_API::newGPUBuffer( const U32 ringBufferLength, const std::string_view name ) const
{
    return std::make_unique<nriGPUBuffer>( _context, ringBufferLength, name );
}

ShaderBuffer_uptr NVIDIA_RENDER_INTERFACE_API::newShaderBuffer( const ShaderBufferDescriptor& descriptor ) const
{
    return std::make_unique<nriUniformBuffer>( _context, descriptor );
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void NVIDIA_RENDER_INTERFACE_API::initStatePerWindow( NRIPerWindowState& ws )
{
    DIVIDE_GPU_ASSERT( ws._window != nullptr );
    DIVIDE_GPU_ASSERT( ws._swapChain == nullptr );

    const auto& dims     = ws._window->getDimensions();
    const bool  vsync    = (ws._window->flags() & to_base( WindowFlags::VSYNC )) != 0u;
    const bool  adaptive = _context.context().config().runtime.adaptiveSync;

    nri::SwapChainDesc scDesc{};
    scDesc.window      = MakeNRIWindow( ws._window->getRawWindow() );
    scDesc.queue       = _graphicsQueue;
    scDesc.width       = static_cast<nri::Dim_t>( dims.width );
    scDesc.height      = static_cast<nri::Dim_t>( dims.height );
    scDesc.textureNum  = NRI_BUFFERED_FRAME_COUNT;
    scDesc.format      = nri::SwapChainFormat::BT709_G22_8BIT;
    scDesc.flags       = vsync        ? nri::SwapChainBits::VSYNC :
                         adaptive     ? nri::SwapChainBits::ALLOW_TEARING :
                                        nri::SwapChainBits::NONE;

    if ( _nri.swapChain.CreateSwapChain( *_device, scDesc, ws._swapChain )
            != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] Failed to create SwapChain for window '{}' ({}x{})",
                          ws._window->title(), dims.width, dims.height );
        return;
    }

    // Semaphores for acquire/release must be created with the special initial value
    if ( _nri.core.CreateFence( *_device, nri::SWAPCHAIN_SEMAPHORE, ws._acquireSemaphore )
            != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] Failed to create acquire semaphore" );
        return;
    }
    if ( _nri.core.CreateFence( *_device, nri::SWAPCHAIN_SEMAPHORE, ws._releaseSemaphore )
            != nri::Result::SUCCESS )
    {
        Console::errorfn( "[NRI] Failed to create release semaphore" );
        return;
    }
}

void NVIDIA_RENDER_INTERFACE_API::destroyStatePerWindow( NRIPerWindowState& ws ) noexcept
{
    if ( ws._acquireSemaphore )  { _nri.core.DestroyFence( *ws._acquireSemaphore ); ws._acquireSemaphore = nullptr; }
    if ( ws._releaseSemaphore )  { _nri.core.DestroyFence( *ws._releaseSemaphore ); ws._releaseSemaphore = nullptr; }
    if ( ws._swapChain )         { _nri.swapChain.DestroySwapChain( *ws._swapChain ); ws._swapChain = nullptr; }
    ws = {};
}

void NVIDIA_RENDER_INTERFACE_API::recreateSwapChain( NRIPerWindowState& ws )
{
    // Wait for idle before recreation
    _nri.core.WaitForIdle( *_graphicsQueue );

    const auto& dims  = ws._window->getDimensions();
    const bool  vsync = (ws._window->flags() & to_base( WindowFlags::VSYNC )) != 0u;

    // Destroy old semaphores and swapchain
    if ( ws._acquireSemaphore )  { _nri.core.DestroyFence( *ws._acquireSemaphore ); ws._acquireSemaphore = nullptr; }
    if ( ws._releaseSemaphore )  { _nri.core.DestroyFence( *ws._releaseSemaphore ); ws._releaseSemaphore = nullptr; }
    if ( ws._swapChain )         { _nri.swapChain.DestroySwapChain( *ws._swapChain ); ws._swapChain = nullptr; }

    // Recreate
    initStatePerWindow( ws );

    LockManager::CleanExpiredSyncObjects( _context.renderAPI(), U64_MAX );
}

NRIFrameResources& NVIDIA_RENDER_INTERFACE_API::currentFrame() noexcept
{
    return _frameData[_frameIndex % NRI_BUFFERED_FRAME_COUNT];
}

nri::CommandBuffer* NVIDIA_RENDER_INTERFACE_API::currentCommandBuffer() noexcept
{
    return currentFrame()._commandBuffer;
}

void NVIDIA_RENDER_INTERFACE_API::advanceFrame() noexcept
{
    ++_frameIndex;
}

} // namespace Divide

