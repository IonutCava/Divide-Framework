#include "stdafx.h"

#include "Headers/vkLockManager.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

namespace Divide {

constexpr U64 kOneSecondInNanoSeconds = 1000000000;

void vkSyncObject::reset() noexcept {
    _fence = VK_NULL_HANDLE;
    SyncObject::reset();
}

vkLockManager::~vkLockManager()
{
    waitForLockedRange(0u, SIZE_MAX);
}

bool vkLockManager::initLockPoolEntry(BufferLockPoolEntry& entry) {
    const VkFence crtFence = VK_API::GetStateTracker()._swapChain->getCurrentFence();

    if (entry._ptr == nullptr) {
        entry._ptr = eastl::make_unique<vkSyncObject>();
        static_cast<vkSyncObject*>(entry._ptr.get())->_fence = crtFence;
        return true;
    } else {
        vkSyncObject* vkSync = static_cast<vkSyncObject*>(entry._ptr.get());
        if (vkSync->_fence == VK_NULL_HANDLE) {
            vkSync->_fence = crtFence;
            return true;
        }
    }

    return false;
}

bool Wait(const VkFence sync, U8& retryCount) {
    OPTICK_EVENT();
    
    U64 waitTimeout = 0u;
    while (true) {
        OPTICK_EVENT("Wait - OnLoop");
        OPTICK_TAG("RetryCount", retryCount);

        const VkResult waitRet = vkWaitForFences(VK_API::GetStateTracker()._device->getVKDevice(),
                                                 1,
                                                 &sync,
                                                 VK_FALSE,
                                                 waitTimeout);
        if (waitRet == VK_SUCCESS) {
            return true;
        }

        DIVIDE_ASSERT(waitRet == VK_TIMEOUT, "vkLockManager::wait error: Not sure what to do here. Probably raise an exception or something.");
        waitTimeout = kOneSecondInNanoSeconds;

        if (++retryCount > g_MaxLockWaitRetries) {
            if (waitRet != VK_TIMEOUT) {
                Console::errorfn("vkLockManager::wait error: Lock timeout");
            }

            break;
        }
    }

    return false;
}

bool vkLockManager::waitForLockedRangeLocked(const SyncObject_uptr& sync, const BufferRange& testRange, const BufferLockInstance& lock) {
    OPTICK_EVENT();

    vkSyncObject* vkSync = static_cast<vkSyncObject*>(sync.get());
    if (vkSync->_fence == VK_NULL_HANDLE ||
        vkSync->_frameNumber < VK_API::GetStateTracker()._lastSyncedFrameNumber)
    {
        // Lock expired from underneath us
        return true;
    }

    U8 retryCount = 0u;
    if (Wait(vkSync->_fence, retryCount)) {
        vkSync->reset();

        if (retryCount > g_MaxLockWaitRetries - 1) {
            Console::errorfn("vkLockManager: Wait[%d - %d] %d retries", testRange._startOffset, testRange._length, retryCount);
        }
        return true;
    } 

    return false;
}

}; //namespace Divide
