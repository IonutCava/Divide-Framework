    

    #include "descriptor_allocator.h"

    namespace vke {

    namespace
    {

        inline bool IsMemoryError(const VkResult errorResult) noexcept
        {
            switch (errorResult)
            {
                case VK_ERROR_FRAGMENTED_POOL:
                case VK_ERROR_OUT_OF_POOL_MEMORY: return true;
            }
            return false;
        }

    }

    vke::DescriptorAllocatorPool* vke::DescriptorAllocatorPool::Create(const VkDevice& device, Divide::I32 nFrames)
    {
        DescriptorAllocatorPool* impl = new DescriptorAllocatorPool();
        impl->_device = device;
        impl->_frameIndex = 0;
        impl->_maxFrames = nFrames;
        for ( Divide::I32 i = 0; i < nFrames; i++)
        {
            impl->_descriptorPools.push_back(std::make_unique<PoolStorage>());
        }

        return impl;
    }

    DescriptorAllocatorHandle::~DescriptorAllocatorHandle()
    {
        DescriptorAllocatorPool* implPool = static_cast<DescriptorAllocatorPool*>(ownerPool);
        if (implPool)
        {
            implPool->ReturnAllocator(*this, false);
        }
    }

    DescriptorAllocatorHandle::DescriptorAllocatorHandle(DescriptorAllocatorHandle&& other)
    {
        Return();

        vkPool = other.vkPool;
        poolIdx = other.poolIdx;
        ownerPool = other.ownerPool;

        other.ownerPool = nullptr;
        other.poolIdx = -1;
        other.vkPool = {};
    }

    vke::DescriptorAllocatorHandle& DescriptorAllocatorHandle::operator=(DescriptorAllocatorHandle&& other)
    {
        Return();

        vkPool = other.vkPool;
        poolIdx = other.poolIdx;
        ownerPool = other.ownerPool;

        other.ownerPool = nullptr;
        other.poolIdx = -1;
        other.vkPool = {};

        return *this;
    }

    void DescriptorAllocatorHandle::Return()
    {
        DescriptorAllocatorPool* implPool = static_cast<DescriptorAllocatorPool*>(ownerPool);

        if (implPool)
        {
            implPool->ReturnAllocator(*this, false);
        }

        vkPool = {};
        poolIdx = -1;
        ownerPool = nullptr;
    }

    bool DescriptorAllocatorHandle::Allocate(const VkDescriptorSetLayout& layout, VkDescriptorSet& builtSet, Divide::I8 retryCount)
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Graphics );

        DescriptorAllocatorPool*implPool = static_cast<DescriptorAllocatorPool*>(ownerPool);

        VkDescriptorSetAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = vkPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        const VkResult result = vkAllocateDescriptorSets(implPool->_device, &allocInfo, &builtSet);
        if (result != VK_SUCCESS)
        {
            //we reallocate pools on memory error
            if (IsMemoryError(result))
            {
                //out of space need reallocate
                implPool->ReturnAllocator(*this, true);

                DescriptorAllocatorHandle newHandle = implPool->GetAllocator();
                
                vkPool = newHandle.vkPool;
                poolIdx = newHandle.poolIdx;

                newHandle.vkPool = {};
                newHandle.poolIdx = -1;
                newHandle.ownerPool = nullptr;

                Divide::DIVIDE_ASSERT( retryCount < 250, "DescriptorAllocatorHandle::Allocate failed to allocate descriptor sets: memory error!");
                //could be good idea to avoid infinite loop here
                return Allocate(layout, builtSet, retryCount + 1);
            }
            else
            {
                //stuff is truly broken
                Divide::DIVIDE_UNEXPECTED_CALL();
                return false;
            }
        }

        return true;
    }

    VkDescriptorPool DescriptorAllocatorPool::createPool( Divide::I32 count, VkDescriptorPoolCreateFlags flags)
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Graphics );

        thread_local Divide::vector_fast<VkDescriptorPoolSize> sizes;

        sizes.clear();
        sizes.reserve(_poolSizes.sizes.size());

        for (auto sz : _poolSizes.sizes)
        {
            const Divide::U32 targetSize = Divide::to_U32( sz.multiplier * count );
            if ( targetSize > 0u )
            {
                sizes.push_back( { sz.type, targetSize } );
            }
        }

        VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.flags = flags;
        pool_info.maxSets = count;
        pool_info.poolSizeCount = Divide::to_U32(sizes.size());
        pool_info.pPoolSizes = sizes.data();

        VkDescriptorPool descriptorPool;
        vkCreateDescriptorPool(_device, &pool_info, nullptr, &descriptorPool);

        return descriptorPool;
    }

    DescriptorAllocatorPool::~DescriptorAllocatorPool()
    {
        for (DescriptorAllocator allocator : _clearAllocators)
        {
            vkDestroyDescriptorPool(_device, allocator.pool, nullptr);
        }

        for (auto&& storage : _descriptorPools)
        {
            for (DescriptorAllocator allocator : storage->_fullAllocators)
            {
                vkDestroyDescriptorPool(_device, allocator.pool, nullptr);
            }
            for (DescriptorAllocator allocator : storage->_usableAllocators)
            {
                vkDestroyDescriptorPool(_device, allocator.pool, nullptr);
            }
        }
    }

    void DescriptorAllocatorPool::Flip()
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Graphics );

        _frameIndex = (_frameIndex+1) % _maxFrames;
        {
            PROFILE_SCOPE( "Flip full allocators", Divide::Profiler::Category::Graphics );
            for (auto al :  _descriptorPools[_frameIndex]->_fullAllocators )
            {

                vkResetDescriptorPool(_device, al.pool, VkDescriptorPoolResetFlags{ 0 });

                _clearAllocators.push_back(al);
            }
        }
        {
            PROFILE_SCOPE( "Flip usable allocators", Divide::Profiler::Category::Graphics );
            for (auto al : _descriptorPools[_frameIndex]->_usableAllocators)
            {
                vkResetDescriptorPool(_device, al.pool, VkDescriptorPoolResetFlags{ 0 });
                _clearAllocators.push_back(al);
            }
        }
        _descriptorPools[_frameIndex]->_fullAllocators.clear();
        _descriptorPools[_frameIndex]->_usableAllocators.clear();
    }

    void DescriptorAllocatorPool::SetPoolSizeMultiplier(VkDescriptorType type, float multiplier)
    {
        for (auto& s : _poolSizes.sizes)
        {
            if (s.type == type)
            {
                s.multiplier = multiplier;
                return;
            }
        }

        //not found, so add it
        _poolSizes.sizes.emplace_back(PoolSize{type, multiplier});
    }

    void DescriptorAllocatorPool::ReturnAllocator(DescriptorAllocatorHandle& handle, bool bIsFull)
    {
        Divide::LockGuard<Divide::Mutex> lk(_poolMutex);

        if (bIsFull) 
        {
            _descriptorPools[handle.poolIdx]->_fullAllocators.push_back(DescriptorAllocator{ handle.vkPool });
        }
        else
        {
            _descriptorPools[handle.poolIdx]->_usableAllocators.push_back(DescriptorAllocator{ handle.vkPool });
        }
    }

    vke::DescriptorAllocatorHandle DescriptorAllocatorPool::GetAllocator()
    {
        PROFILE_SCOPE_AUTO( Divide::Profiler::Category::Graphics );

        Divide::LockGuard<Divide::Mutex> lk( _poolMutex );

        bool foundAllocator = false;

        Divide::I32 poolIndex = _frameIndex;

        DescriptorAllocator allocator{};
        //try reuse an allocated pool
        if (_clearAllocators.size() != 0)
        {
            allocator = _clearAllocators.back();
            _clearAllocators.pop_back();
            foundAllocator = true;				
        }
        else if (_descriptorPools[poolIndex]->_usableAllocators.size() > 0)
        {
            allocator = _descriptorPools[poolIndex]->_usableAllocators.back();
            _descriptorPools[poolIndex]->_usableAllocators.pop_back();
            foundAllocator = 1;
        }
        
        //need a new pool
        if (!foundAllocator)
        {
            //static pool has to be free-able
            allocator.pool = createPool(2000, poolIndex == 0 ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0 );
            foundAllocator = true;
        }

        DescriptorAllocatorHandle ret{};
        ret.ownerPool = this;
        ret.vkPool = allocator.pool;
        ret.poolIdx = poolIndex;

        return ret;
    }
}
