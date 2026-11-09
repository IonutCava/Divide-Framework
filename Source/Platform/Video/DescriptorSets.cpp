// Copyright (c) 2026 DIVIDE-Studio
#include "Headers/DescriptorSets.h"
#include "Headers/ResourceBindingMetrics.h"

#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Textures/Headers/Texture.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{
    void ResourceBindingMetrics::beginFrame( const bool enabled )
    {
        _enabled = enabled;
        _current = {};
        if ( _enabled )
        {
            if ( _sets.capacity() < CAPACITY )
            {
                _sets.reserve( CAPACITY );
                ++_current._storageGrowths;
            }
            _sets.clear();
            _setIndex.fill( 0u );
            _materials.fill( {} );
        }
    }

    void ResourceBindingMetrics::observe( const I64 program, const U64 materialRevision, const DescriptorSet& bindings, const U32 activeSlots )
    {
        if ( !_enabled )
        {
            return;
        }
        ++_current._drawSets;
        if ( materialRevision != 0u )
        {
            size_t hash = 1337u;
            Util::Hash_combine( hash, program, materialRevision );
            for ( size_t probe = 0u; probe < _materials.size(); ++probe )
            {
                auto& entry = _materials[(hash + probe) % _materials.size()];
                if ( entry._revision == materialRevision && entry._program == program )
                {
                    break;
                }
                if ( entry._revision == 0u )
                {
                    entry = { program, materialRevision };
                    ++_current._uniqueMaterialLayouts;
                    break;
                }
                if ( probe + 1u == _materials.size() )
                {
                    ++_current._materialOverflow;
                }
            }
        }

        DescriptorSet canonical{};
        std::array<const DescriptorSetBinding*, MAX_BINDINGS_PER_DESCRIPTOR_SET> slots{};
        for ( U8 i = 0u; i < bindings._bindingCount; ++i )
        {
            const auto& binding = bindings._bindings[i];
            if ( binding._slot < MAX_BINDINGS_PER_DESCRIPTOR_SET )
            {
                slots[binding._slot] = &binding;
            }
        }
        size_t hash = 1337u;
        Util::Hash_combine( hash, program );
        for ( U8 slot = 0u; slot < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++slot )
        {
            if ( (activeSlots & (1u << slot)) == 0u )
            {
                continue;
            }
            if ( slots[slot] != nullptr )
            {
                const auto& binding = *slots[slot];
                canonical._bindings[canonical._bindingCount++] = binding;
                Util::Hash_combine( hash, slot, binding._shaderStageVisibility, binding._data._type );
                switch ( binding._data._type )
                {
                    case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                        Util::Hash_combine( hash, binding._data._sampledImage._imageHash, binding._data._sampledImage._samplerHash );
                        break;
                    case DescriptorSetBindingType::IMAGE:
                        Util::Hash_combine( hash, GetHash( binding._data._imageView._image ), binding._data._imageView._usage );
                        break;
                    case DescriptorSetBindingType::COUNT:
                        break;
                    default:
                    {
                        const auto& buffer = binding._data._buffer;
                        Util::Hash_combine( hash, std::hash<ShaderBuffer*>{}( buffer._buffer ),
                                           buffer._queueReadIndex, buffer._range._startOffset, buffer._range._length );
                    } break;
                }
            }
        }
        for ( size_t probe = 0u; probe < _setIndex.size(); ++probe )
        {
            U16& index = _setIndex[(hash + probe) % _setIndex.size()];
            if ( index == 0u )
            {
                if ( _sets.size() == CAPACITY )
                {
                    ++_current._effectiveOverflow;
                    return;
                }
                _sets.push_back( { program, hash, canonical } );
                index = to_U16( _sets.size() );
                ++_current._uniqueEffectiveSets;
                return;
            }
            const auto& sample = _sets[index - 1u];
            if ( sample._hash == hash && sample._program == program && EqualActiveBindings( sample._bindings, canonical ) )
            {
                return;
            }
        }
        ++_current._effectiveOverflow;
    }

    namespace
    {
        std::atomic<U64> s_bindingRevision{ 0u };
        std::atomic<U64> s_liveBindings{ 0u };
        MemoryPool<ResourceBindingSet, 1u << 18u> s_bindingPool;
        Mutex s_bindingPoolLock;
    }

    ResourceBindingSet::ResourceBindingSet( const DescriptorSet& bindings, const std::span<const Handle<Texture>> textures )
        : _bindings( bindings ), _revision( s_bindingRevision.fetch_add( 1u, std::memory_order_relaxed ) + 1u )
    {
        s_liveBindings.fetch_add( 1u, std::memory_order_relaxed );
        for ( const Handle<Texture> texture : textures )
        {
            _textures[_textureCount++] = GetResourceRef( texture );
        }
    }

    ResourceBindingSet::~ResourceBindingSet()
    {
        s_liveBindings.fetch_sub( 1u, std::memory_order_relaxed );
        for ( U8 i = 0u; i < _textureCount; ++i )
        {
            DestroyResource( _textures[i] );
        }
    }

    ResourceBindingStorageStats GetResourceBindingStorageStats() noexcept
    {
        return { s_bindingRevision.load( std::memory_order_relaxed ), s_liveBindings.load( std::memory_order_relaxed ) };
    }

    ResourceBindingHandle CreateResourceBindings( const DescriptorSet& bindings, const std::span<const Handle<Texture>> textures )
    {
        PROFILE_SCOPE( "Publish immutable resource bindings", Profiler::Category::Graphics );
        if ( textures.size() > MAX_BINDINGS_PER_DESCRIPTOR_SET || bindings._bindingCount > MAX_BINDINGS_PER_DESCRIPTOR_SET )
        {
            Console::errorfn( "Resource binding publication failed: too many retained textures." );
            return {};
        }
        for ( const Handle<Texture> texture : textures )
        {
            if ( ResourceCache::Inspect( texture )._state != ResourceState::RES_LOADED || Get( texture ) == nullptr )
            {
                Console::errorfn( "Resource binding publication failed: stale texture handle." );
                return {};
            }
        }
        for ( U8 i = 0u; i < bindings._bindingCount; ++i )
        {
            const auto& binding = bindings._bindings[i];
            if ( binding._slot >= MAX_BINDINGS_PER_DESCRIPTOR_SET ||
                 binding._data._type != DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER ||
                 (binding._data._sampledImage._image._srcTexture != nullptr &&
                  !std::ranges::any_of( textures, [&]( const Handle<Texture> texture )
                  {
                      return Get( texture ) == binding._data._sampledImage._image._srcTexture;
                  } )) )
            {
                Console::errorfn( "Resource binding publication failed: slot {} is not a retained sampled image.", binding._slot );
                return {};
            }
        }
        ResourceBindingSet* record = nullptr;
        {
            LockGuard<Mutex> lock( s_bindingPoolLock );
            record = s_bindingPool.newElement( bindings, textures );
        }
        return ResourceBindingHandle( record, []( ResourceBindingSet* value )
        {
            LockGuard<Mutex> lock( s_bindingPoolLock );
            s_bindingPool.deleteElement( value );
        }, dvd_allocator<ResourceBindingSet>{} );
    }

    bool EqualActiveBindings( const DescriptorSet& lhs, const DescriptorSet& rhs ) noexcept
    {
        if ( lhs._bindingCount != rhs._bindingCount )
        {
            return false;
        }
        for ( U8 i = 0u; i < lhs._bindingCount; ++i )
        {
            if ( lhs._bindings[i] != rhs._bindings[i] )
            {
                return false;
            }
        }
        return true;
    }

    DescriptorSet ComposeResourceBindings( const ResourceBindingHandle& base, const DescriptorSet& overrides )
    {
        DescriptorSet result = base != nullptr ? base->_bindings : DescriptorSet{};
        for ( U8 i = 0u; i < overrides._bindingCount; ++i )
        {
            const DescriptorSetBinding& binding = overrides._bindings[i];
            U8 index = 0u;
            while ( index < result._bindingCount && result._bindings[index]._slot != binding._slot )
            {
                ++index;
            }
            if ( index == result._bindingCount )
            {
                if ( result._bindingCount == MAX_BINDINGS_PER_DESCRIPTOR_SET )
                {
                    DIVIDE_UNEXPECTED_CALL_MSG( "Resource binding composition exceeded the descriptor slot limit." );
                }
                ++result._bindingCount;
            }
            result._bindings[index] = binding;
        }
        return result;
    }

    size_t GetHash( const ImageViewDescriptor& descriptor) noexcept
    {
        size_t hash = 1337;
        Util::Hash_combine(hash,
                           descriptor._msaaSamples,
                           descriptor._dataType,
                           descriptor._baseFormat,
                           descriptor._packing);
        return hash;
    }
    
    size_t GetHash( const ImageView& imageView ) noexcept
    {
        size_t hash = 1337;
        Util::Hash_combine(hash,
                           GetHash(imageView._descriptor),
                           imageView._srcTexture != nullptr ? imageView._srcTexture->getGUID() : 0,
                           TargetType( imageView ),
                           imageView._subRange._layerRange._offset,
                           imageView._subRange._layerRange._count,
                           imageView._subRange._mipLevels._offset,
                           imageView._subRange._mipLevels._count);

        return hash;
    }

    TextureType TargetType( const ImageView& imageView ) noexcept
    {
        if (imageView._targetType == TextureType::COUNT &&
            imageView._srcTexture != nullptr)
        {
            return imageView._srcTexture->descriptor()._texType;
        }

        return imageView._targetType;
    }

    bool operator==(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept
    {
        return lhs._range == rhs._range &&
               lhs._queueReadIndex == rhs._queueReadIndex &&
               Compare(lhs._buffer, rhs._buffer);
    }

    bool operator!=(const ShaderBufferEntry& lhs, const ShaderBufferEntry& rhs) noexcept
    {
        return lhs._range != rhs._range ||
               lhs._queueReadIndex != rhs._queueReadIndex ||
               !Compare(lhs._buffer, rhs._buffer);
    }

    void Set( DescriptorSetBindingData& dataInOut, ShaderBuffer* buffer) noexcept
    {
        assert( buffer != nullptr );

        dataInOut._buffer = { buffer, {0u, buffer->getPrimitiveCount()}, buffer->queueIndex() };
        dataInOut._type = buffer->getUsage() == BufferUsageType::CONSTANT_BUFFER ? DescriptorSetBindingType::UNIFORM_BUFFER_STATIC
                                                                                 : DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC;
    }

    void Set( DescriptorSetBindingData& dataInOut, ShaderBuffer* buffer, const BufferRange<> range ) noexcept
    {
        assert( buffer != nullptr );
        dataInOut._buffer = { buffer, range, buffer->queueIndex() };
        dataInOut._type = buffer->getUsage() == BufferUsageType::CONSTANT_BUFFER ? DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC
                                                                                 : DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC;
    }

    void SetIndirectBuffer( DescriptorSetBindingData& dataInOut, ShaderBuffer* buffer, const BufferRange<> range ) noexcept
    {
        assert( buffer != nullptr && buffer->getUsage() == BufferUsageType::COMMAND_BUFFER );
        dataInOut._buffer = { buffer, range, buffer->queueIndex() };
        dataInOut._type = DescriptorSetBindingType::INDIRECT_BUFFER;
    }

    void Set( DescriptorSetBindingData& dataInOut, const Handle<Texture> defaultTextureView, const SamplerDescriptor sampler ) noexcept
    {
        Set( dataInOut, Get( defaultTextureView )->getView(), sampler );
    }

} //namespace Divide
