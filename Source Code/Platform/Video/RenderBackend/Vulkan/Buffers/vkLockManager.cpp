#include "stdafx.h"

#include "Headers/vkLockManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    constexpr U64 kOneSecondInNanoSeconds = 1000000000;

    void vkSyncObject::reset() noexcept
    {
        _fence = VK_NULL_HANDLE;
        SyncObject::reset();
    }

    vkLockManager::~vkLockManager()
    {
        waitForLockedRange( 0u, SIZE_MAX );
    }

    bool vkLockManager::InitLockPoolEntry( BufferLockPoolEntry& entry )
    {
        const VkFence crtFence = VK_API::GetStateTracker()._activeWindow->_swapChain->getCurrentFence();

        if ( entry._ptr == nullptr )
        {
            entry._ptr = eastl::make_unique<vkSyncObject>();
            static_cast<vkSyncObject*>(entry._ptr.get())->_fence = crtFence;
            return true;
        }
        else
        {
            vkSyncObject* vkSync = static_cast<vkSyncObject*>(entry._ptr.get());
            if ( vkSync->_fence == VK_NULL_HANDLE )
            {
                vkSync->_fence = crtFence;
                return true;
            }
        }

        return false;
    }

    bool Wait( const VkFence sync, U8& retryCount )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        U64 waitTimeout = 0u;
        while ( true )
        {
            PROFILE_SCOPE( "Wait - OnLoop", Profiler::Category::Graphics );
            PROFILE_TAG( "RetryCount", retryCount );

            const VkResult waitRet = vkWaitForFences( VK_API::GetStateTracker()._device->getVKDevice(),
                                                      1,
                                                      &sync,
                                                      VK_FALSE,
                                                      waitTimeout );
            if ( waitRet == VK_SUCCESS )
            {
                return true;
            }

            DIVIDE_ASSERT( waitRet == VK_TIMEOUT, "vkLockManager::wait error: Not sure what to do here. Probably raise an exception or something." );
            waitTimeout = kOneSecondInNanoSeconds;

            if ( ++retryCount > g_MaxLockWaitRetries )
            {
                if ( waitRet != VK_TIMEOUT )
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_VK_LOCK_WAIT_TIMEOUT" ) ) );
                }

                break;
            }
        }

        return false;
    }

    bool vkLockManager::waitForLockedRangeLocked( const SyncObject_uptr& sync, const BufferRange& testRange, const BufferLockInstance& lock )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        vkSyncObject* vkSync = static_cast<vkSyncObject*>(sync.get());
        if ( vkSync->_fence == VK_NULL_HANDLE )
        {
            // Lock expired from underneath us
            return true;
        }

        U8 retryCount = 0u;
        if ( Wait( vkSync->_fence, retryCount ) )
        {
            vkSync->reset();

            if ( retryCount > g_MaxLockWaitRetries - 1 )
            {
                Console::errorfn( Locale::Get( _ID( "ERROR_VK_LOCK_WAIT_RETRY" ) ), testRange._startOffset, testRange._length, retryCount );
            }
            return true;
        }

        return false;
    }

}; //namespace Divide
