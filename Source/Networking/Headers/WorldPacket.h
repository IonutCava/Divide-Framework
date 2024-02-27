#pragma once
#ifndef MANGOSSERVER_WORLDPACKET_H
#define MANGOSSERVER_WORLDPACKET_H

#include "OPCodesTpl.h"

#include "Utils.h"
#include "Core/Headers/ByteBuffer.h"

#include <boost/serialization/split_member.hpp>

namespace Divide {

class WorldPacket : public ByteBuffer {
   public:
    // just container for later use
    WorldPacket() noexcept 
        : ByteBuffer(),
          m_opcode(OPCodes::MSG_NOP)
    {
    }

    explicit WorldPacket(const OPCodes::ValueType opcode, const size_t res = 200)
        : ByteBuffer(),
          m_opcode(opcode)
    {
        _storage.reserve(res);
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
    void load(Archive& ar, [[maybe_unused]] const unsigned int version) {
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
    void save(Archive& ar, [[maybe_unused]] const unsigned int version) const {
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
