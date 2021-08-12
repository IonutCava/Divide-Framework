#ifndef MANGOSSERVER_WORLDPACKET_H
#define MANGOSSERVER_WORLDPACKET_H

#include "OPCodesTpl.h"

#include <boost/serialization/vector.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace Divide {

class WorldPacket : public ByteBuffer {
   public:
    // just container for later use
    WorldPacket() noexcept : WorldPacket(OPCodes::MSG_NOP, 0)
    {
    }

    explicit WorldPacket(const OPCodes::ValueType opcode, const size_t res = 200) noexcept
        : ByteBuffer(res),
          m_opcode(opcode)
    {
    }

    void Initialize(const U16 opcode, const size_t newres = 200) {
        clear();
        _storage.reserve(newres);
        m_opcode = opcode;
    }

    [[nodiscard]] OPCodes::ValueType opcode() const noexcept { return m_opcode; }
    void opcode(const OPCodes::ValueType opcode) noexcept { m_opcode = opcode; }

   private:
    friend class boost::serialization::access;

    template <typename Archive>
    void load(Archive& ar, const unsigned int version) {
        ACKNOWLEDGE_UNUSED(version);
        size_t storageSize = 0;

        ar& _rpos;
        ar& _wpos;
        ar& storageSize;

        _storage.resize(storageSize);
        for (size_t i = 0; i < storageSize; ++i) {
            ar & _storage[i];
        }
        ar& m_opcode;
    }

    template <typename Archive>
    void save(Archive& ar, const unsigned int version) const {
        ACKNOWLEDGE_UNUSED(version);
        const size_t storageSize = _storage.size();

        ar & _rpos;
        ar & _wpos;
        ar & storageSize;
        for (size_t i = 0; i < storageSize; ++i) {
            ar & _storage[i];
        }
        ar & m_opcode;
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

   protected:
    OPCodes::ValueType m_opcode;
};

};  // namespace Divide

#endif
