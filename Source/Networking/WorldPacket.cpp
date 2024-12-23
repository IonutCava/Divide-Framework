#include "Headers/WorldPacket.h"
#include <Core/Headers/ByteBuffer.h>

#include <iterator>

namespace Divide
{
    WorldPacket::WorldPacket() noexcept
        : WorldPacket( OPCodes::MSG_NOP )
    {
    }

    WorldPacket::WorldPacket( const OPCodes::ValueType code )
        : ByteBuffer()
        , _opcode( code )
    {
    }

    void WorldPacket::Initialize( const U16 opcode  )
    {
        clear();
        _opcode = opcode;
    }

    bool WorldPacket::loadFromBuffer( boost::asio::streambuf& buf )
    {
        size_t storageSize = 0u;

        std::istream is( &buf );
        is.read(reinterpret_cast<char*>(&_opcode), sizeof(_opcode));
        is.read(reinterpret_cast<char*>(&_wpos), sizeof( _wpos ));
        is.read(reinterpret_cast<char*>(&_rpos), sizeof( _rpos ));
        is.read(reinterpret_cast<char*>(&storageSize), sizeof( storageSize ));

        if (storageSize > 0u)
        {
            _storage.resize(storageSize);
            is.read( reinterpret_cast<char*>(_storage.data()), storageSize * sizeof ( _storage[0] ) );
        }
        else
        {
            efficient_clear(_storage);
        }

        return true;
    }

    bool WorldPacket::saveToBuffer( boost::asio::streambuf& buf ) const
    {
        const size_t storageSize = _storage.size();

        std::ostream os( &buf );
        os.write((const char*)&_opcode, sizeof( _opcode ));
        os.write((const char*)&_wpos, sizeof( _wpos ));
        os.write((const char*)&_rpos, sizeof( _rpos ));
        os.write((const char*)&storageSize, sizeof (storageSize));
        if (storageSize > 0u)
        {
            os.write( (const char*)_storage.data(), storageSize * sizeof( _storage[0] ) );
        }
        return !os.bad();
    }
} // namespace Divide
