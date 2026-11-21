// Copyright (c) 2026 DIVIDE-Studio
#pragma once

namespace Divide
{
    template<typename Value, size_t Capacity>
    class ResolvedBindingCache
    {
        static_assert( Capacity > 0u && ( Capacity & ( Capacity - 1u ) ) == 0u );

    public:
        [[nodiscard]] bool Find( const U64 revision, const U8 slot, Value& valueOut ) const noexcept
        {
            if ( revision == 0u )
            {
                return false;
            }

            const Entry& entry = _entries[IndexFor( revision, slot )];
            if ( entry._revision != revision || entry._slot != slot )
            {
                return false;
            }

            valueOut = entry._value;
            return true;
        }

        void Store( const U64 revision, const U8 slot, const Value& value ) noexcept
        {
            if ( revision != 0u )
            {
                _entries[IndexFor( revision, slot )] = { revision, slot, value };
            }
        }

        void Clear() noexcept
        {
            _entries.fill( Entry{} );
        }

        [[nodiscard]] size_t StorageBytes() const noexcept
        {
            return sizeof( _entries );
        }

        [[nodiscard]] static constexpr size_t IndexFor( const U64 revision, const U8 slot ) noexcept
        {
            U64 hash = revision ^ (to_U64( slot ) * 0x9E3779B97F4A7C15ull);
            hash ^= hash >> 30u;
            hash *= 0xBF58476D1CE4E5B9ull;
            hash ^= hash >> 27u;
            hash *= 0x94D049BB133111EBull;
            hash ^= hash >> 31u;
            return to_size( hash & (Capacity - 1u) );
        }

    private:
        struct Entry
        {
            U64 _revision{ 0u };
            U8 _slot{ 0u };
            Value _value{};
        };

        std::array<Entry, Capacity> _entries{};
    };
}
