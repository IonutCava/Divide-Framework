#include "stdafx.h"

#include "Headers/vkDescriptors.h"

namespace Divide {
   
    DescriptorLayoutCache::DescriptorLayoutCache(const VkDevice device)
        : _device(device)
    {
    }

    DescriptorLayoutCache::~DescriptorLayoutCache()
    {
        //delete every descriptor layout held
        for (const auto& pair : _layoutCache)
        {
            vkDestroyDescriptorSetLayout(_device, pair.second, nullptr);
        }
    }

    VkDescriptorSetLayout DescriptorLayoutCache::createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DescriptorLayoutInfo layoutinfo{};
        layoutinfo.bindings.reserve(info->bindingCount);
        bool isSorted = true;
        I32 lastBinding = -1;

        for (U32 i = 0u; i < info->bindingCount; i++)
        {
            layoutinfo.bindings.push_back(info->pBindings[i]);

            //check that the bindings are in strict increasing order
            if (to_I32(info->pBindings[i].binding) > lastBinding)
            {
                lastBinding = info->pBindings[i].binding;
            }
            else
            {
                isSorted = false;
            }
        }

        if (!isSorted)
        {
            eastl::sort(eastl::begin(layoutinfo.bindings),
                        eastl::end(layoutinfo.bindings),
                        [](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b ) {
                            return a.binding < b.binding;
                        });
        }
        
        const auto it = _layoutCache.find(layoutinfo);
        if (it != _layoutCache.end())
        {
            return (*it).second;
        }

        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(_device, info, nullptr, &layout);
        hashAlg::emplace(_layoutCache, layoutinfo, layout);
        return layout;
    }

    bool DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const 
    {
        if (other.bindings.size() != bindings.size())
        {
            return false;
        }

        //compare each of the bindings is the same. Bindings are sorted so they will match
        for (size_t i = 0u; i < bindings.size(); i++)
        {
            if (other.bindings[i].binding != bindings[i].binding)
            {
                return false;
            }
            if (other.bindings[i].descriptorType != bindings[i].descriptorType)
            {
                return false;
            }
            if (other.bindings[i].descriptorCount != bindings[i].descriptorCount)
            {
                return false;
            }
            if (other.bindings[i].stageFlags != bindings[i].stageFlags)
            {
                return false;
            }
        }

        return true;
    }

    size_t DescriptorLayoutCache::DescriptorLayoutInfo::GetHash() const
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

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

    DescriptorBuilder DescriptorBuilder::Begin(DescriptorLayoutCache* layoutCache, vke::DescriptorAllocatorHandle* allocator)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        DescriptorBuilder builder{};
        builder.cache = layoutCache;
        builder.alloc = allocator;
        return builder;
    }


    DescriptorBuilder& DescriptorBuilder::bindBuffer(const U32 binding, VkDescriptorBufferInfo* bufferInfo, const VkDescriptorType type, const VkShaderStageFlags stageFlags)
    {
        VkDescriptorSetLayoutBinding& newBinding = bindings.emplace_back();

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;
        newBinding.pImmutableSamplers = nullptr;

        writes.push_back(vk::writeDescriptorSet(type, binding, bufferInfo, 1u));
        return *this;
    }


    DescriptorBuilder& DescriptorBuilder::bindImage(const U32 binding, VkDescriptorImageInfo* imageInfo, const VkDescriptorType type, const VkShaderStageFlags stageFlags)
    {
        VkDescriptorSetLayoutBinding& newBinding = bindings.emplace_back();

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = type;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;
        newBinding.pImmutableSamplers = nullptr;

        writes.push_back(vk::writeDescriptorSet(type, binding, imageInfo, 1u));
        return *this;
    }

    bool DescriptorBuilder::buildSetFromLayout(VkDescriptorSet& set, const VkDescriptorSetLayout& layoutIn, VkDevice device )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        {
            PROFILE_SCOPE( "Allocate", Profiler::Category::Graphics);
            //allocate descriptor
            if ( !alloc->Allocate( layoutIn, set ) )
            {
                return false;
            }
        }
        {
            PROFILE_SCOPE( "Update", Profiler::Category::Graphics);

            //write descriptor
            for ( VkWriteDescriptorSet& w : writes )
            {
                w.dstSet = set;
            }

            vkUpdateDescriptorSets( device, to_U32( writes.size() ), writes.data(), 0, nullptr );
        }
        return true;
    }

    bool DescriptorBuilder::buildSetAndLayout(VkDescriptorSet& set, VkDescriptorSetLayout& layoutOut, VkDevice device )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        VkDescriptorSetLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.pBindings = bindings.data();
        layoutInfo.bindingCount = to_U32( bindings.size() );
        layoutOut = cache->createDescriptorLayout(&layoutInfo);

        return buildSetFromLayout(set, layoutOut, device);
    }


} //namespace Divide
