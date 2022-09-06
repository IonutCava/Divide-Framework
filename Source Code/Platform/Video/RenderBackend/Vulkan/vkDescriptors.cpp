#include "stdafx.h"

#include "Headers/vkDescriptors.h"

namespace Divide {
    namespace {
        VkDescriptorPool createPool(VkDevice device, const DescriptorAllocator::PoolSizes& poolSizes, U32 count, VkDescriptorPoolCreateFlags flags)
        {
            vector<VkDescriptorPoolSize> sizes;
            sizes.reserve(poolSizes.sizes.size());
            for (auto [type, sizeFactor] : poolSizes.sizes) {
                sizes.push_back({ type, to_U32(sizeFactor * count)});
            }

            VkDescriptorPoolCreateInfo pool_info = vk::descriptorPoolCreateInfo(sizes, count);
            pool_info.flags = flags;

            VkDescriptorPool descriptorPool;
            vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool);

            return descriptorPool;
        }
    }

    DescriptorAllocator::DescriptorAllocator(const VkDevice device) 
        : _device(device)
    {
    }

    DescriptorAllocator::~DescriptorAllocator()
    {
        //delete every pool held
        for (VkDescriptorPool p : _freePools) {
            vkDestroyDescriptorPool(_device, p, nullptr);
        }
        for (VkDescriptorPool p : _usedPools) {
            vkDestroyDescriptorPool(_device, p, nullptr);
        }
    }

    void DescriptorAllocator::resetPools() {
        for (VkDescriptorPool p : _usedPools) {
            vkResetDescriptorPool(_device, p, 0);
        }

        _freePools = _usedPools;
        _usedPools.clear();
        _currentPool = VK_NULL_HANDLE;
    }

    bool DescriptorAllocator::allocate(VkDescriptorSet* set, const VkDescriptorSetLayout layout) {
        if (_currentPool == VK_NULL_HANDLE) {
            _currentPool = grabPool();
            _usedPools.push_back(_currentPool);
        }

        VkDescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo(_currentPool, &layout, 1);

        VkResult allocResult = vkAllocateDescriptorSets(_device, &allocInfo, set);
        if (allocResult == VK_SUCCESS) {
            return true;
        }

        if (allocResult == VK_ERROR_FRAGMENTED_POOL || allocResult == VK_ERROR_OUT_OF_POOL_MEMORY) {
            //allocate a new pool and retry
            _currentPool = grabPool();
            _usedPools.push_back(_currentPool);
            allocInfo = vk::descriptorSetAllocateInfo(_currentPool, &layout, 1);

            allocResult = vkAllocateDescriptorSets(_device, &allocInfo, set);

            //if it still fails then we have big issues
            if (allocResult == VK_SUCCESS) {
                return true;
            }

            DIVIDE_UNEXPECTED_CALL();
        }

        return false;
    }

    VkDescriptorPool DescriptorAllocator::grabPool() {
        if (!_freePools.empty())  {
            VkDescriptorPool pool = _freePools.back();
            _freePools.pop_back();
            return pool;
        }
        
        return createPool(_device, _descriptorSizes, 1000, 0);
    }

    DescriptorLayoutCache::DescriptorLayoutCache(const VkDevice device)
        : _device(device)
    {
    }

    DescriptorLayoutCache::~DescriptorLayoutCache()
    {
        //delete every descriptor layout held
        for (auto pair : _layoutCache) {
            vkDestroyDescriptorSetLayout(_device, pair.second, nullptr);
        }
    }

    VkDescriptorSetLayout DescriptorLayoutCache::createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info) {
        DescriptorLayoutInfo layoutinfo{};
        layoutinfo.bindings.reserve(info->bindingCount);
        bool isSorted = true;
        I32 lastBinding = -1;
        for (U32 i = 0u; i < info->bindingCount; i++) {
            layoutinfo.bindings.push_back(info->pBindings[i]);

            //check that the bindings are in strict increasing order
            if (to_I32(info->pBindings[i].binding) > lastBinding) {
                lastBinding = info->pBindings[i].binding;
            } else {
                isSorted = false;
            }
        }

        if (!isSorted) {
            eastl::sort(eastl::begin(layoutinfo.bindings),
                        eastl::end(layoutinfo.bindings),
                        [](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b ) {
                            return a.binding < b.binding;
                        });
        }
        
        auto it = _layoutCache.find(layoutinfo);
        if (it != _layoutCache.end()) {
            return (*it).second;
        }

        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(_device, info, nullptr, &layout);
        hashAlg::emplace(_layoutCache, layoutinfo, layout);
        return layout;
    }

    bool DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const {
        if (other.bindings.size() != bindings.size()) {
            return false;
        }

        //compare each of the bindings is the same. Bindings are sorted so they will match
        for (size_t i = 0u; i < bindings.size(); i++) {
            if (other.bindings[i].binding != bindings[i].binding) {
                return false;
            }
            if (other.bindings[i].descriptorType != bindings[i].descriptorType) {
                return false;
            }
            if (other.bindings[i].descriptorCount != bindings[i].descriptorCount) {
                return false;
            }
            if (other.bindings[i].stageFlags != bindings[i].stageFlags) {
                return false;
            }
        }

        return true;
    }

    size_t DescriptorLayoutCache::DescriptorLayoutInfo::GetHash() const
    {
        size_t h = 1337;
        Util::Hash_combine(h, bindings.size());

        for (const VkDescriptorSetLayoutBinding& b : bindings)
        {
            Util::Hash_combine(h, b.binding, 
                                  b.descriptorType,
                                  b.descriptorCount,
                                  b.stageFlags);
            if (b.pImmutableSamplers != nullptr) {
                Util::Hash_combine(h, *b.pImmutableSamplers);
            }
        }

        return h;
    }

    DescriptorBuilder DescriptorBuilder::Begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator) {
        DescriptorBuilder builder{};
        builder.cache = layoutCache;
        builder.alloc = allocator;
        return builder;
    }


    DescriptorBuilder& DescriptorBuilder::bindBuffer(const U32 binding, VkDescriptorBufferInfo* bufferInfo, const VkDescriptorType type, const VkShaderStageFlags stageFlags) {
        VkDescriptorSetLayoutBinding& newBinding = bindings.emplace_back();

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;
        newBinding.pImmutableSamplers = nullptr;

        writes.push_back(vk::writeDescriptorSet(type, binding, bufferInfo, 1u));
        return *this;
    }


    DescriptorBuilder& DescriptorBuilder::bindImage(const U32 binding, VkDescriptorImageInfo* imageInfo, const VkDescriptorType type, const VkShaderStageFlags stageFlags) {
        VkDescriptorSetLayoutBinding& newBinding = bindings.emplace_back();

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;
        newBinding.pImmutableSamplers = nullptr;

        writes.push_back(vk::writeDescriptorSet(type, binding, imageInfo, 1u));
        return *this;
    }

    bool DescriptorBuilder::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout) {
        //build layout first
        VkDescriptorSetLayoutCreateInfo layoutInfo = vk::descriptorSetLayoutCreateInfo(bindings);
        layout = cache->createDescriptorLayout(&layoutInfo);


        //allocate descriptor
        if (!alloc->allocate(&set, layout)) {
            return false;
        }

        //write descriptor
        for (VkWriteDescriptorSet& w : writes) {
            w.dstSet = set;
        }

        vkUpdateDescriptorSets(alloc->device(), to_U32(writes.size()), writes.data(), 0, nullptr);

        return true;
    }


    bool DescriptorBuilder::build(VkDescriptorSet& set) {
        VkDescriptorSetLayout layout;
        return build(set, layout);
    }


} //namespace Divide
