/*Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _VK_LOCK_MANAGER_H_
#define _VK_LOCK_MANAGER_H_

#include "vkBufferImpl.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"
#include "Platform/Video/Headers/LockManager.h"

namespace Divide {

    struct vkSyncObject final : SyncObject {
        void reset() noexcept override;

        VkFence _fence{ VK_NULL_HANDLE };
    };

    class vkLockManager final : public LockManager {
    public:
        ~vkLockManager();

        static bool InitLockPoolEntry( BufferLockPoolEntry& entry );

        inline [[nodiscard]] static SyncObjectHandle CreateSyncObject( const U8 flag = DEFAULT_SYNC_FLAG_INTERNAL )
        {
            return LockManager::CreateSyncObject(RenderAPI::Vulkan, flag);
        }
    protected:
        bool waitForLockedRangeLocked(const SyncObject_uptr& sync, const BufferRange& testRange, const BufferLockInstance& lock) override;
    };

    FWD_DECLARE_MANAGED_CLASS(vkLockManager);

}; //namespace Divide

#endif //_VK_LOCK_MANAGER_H_
