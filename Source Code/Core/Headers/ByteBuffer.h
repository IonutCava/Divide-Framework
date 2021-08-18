/*
* Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#pragma once
#ifndef _CORE_BYTE_BUFFER_H_
#define _CORE_BYTE_BUFFER_H_

namespace Divide {

class ByteBufferException {
   public:
    ByteBufferException(bool add, size_t pos, size_t esize, size_t size);
    void printPosError() const;

   private:
    bool   _add;
    size_t _pos;
    size_t _esize;
    size_t _size;
};

template <typename T>
struct Unused {
    Unused()
    {
    }
};

class ByteBuffer {
   public:
    static const Byte BUFFER_FORMAT_VERSION { 10u };
    static const size_t DEFAULT_SIZE { 0x1000 };

    // constructor
    ByteBuffer();
    // constructor
    explicit ByteBuffer(size_t res);
    
    void clear() noexcept;

    template <typename T>
    void put(size_t pos, const T& value);

    template <typename T>
    ByteBuffer& operator<<(const T& value);

    template <typename T>
    ByteBuffer& operator>>(T& value);
    template <typename U>
    ByteBuffer& operator>>(Unused<U>& value);

    template <typename T>
    void read_skip();
    void read_skip(size_t skip);
    /// read_noskip calls don't move the read head
    template <typename T>
    void read_noskip(T& value);

    template <typename T>
    T read();
    template <typename T>
    T read(size_t pos) const;
    void read(Byte *dest, size_t len);
    void readPackXYZ(F32& x, F32& y, F32& z);
    U64  readPackGUID();

    void append(const Byte *src, size_t cnt);
    template <typename T>
    void append(const T* src, const size_t cnt);

    void appendPackXYZ(F32 x, F32 y, F32 z);
    void appendPackGUID(U64 guid);

    [[nodiscard]] Byte operator[](size_t pos) const;
    [[nodiscard]] size_t rpos() const noexcept;
    [[nodiscard]] size_t rpos(size_t rpos_) noexcept;
    [[nodiscard]] size_t wpos() const noexcept;
    [[nodiscard]] size_t wpos(size_t wpos_) noexcept;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void resize(size_t newsize);
    void reserve(size_t resize);

    [[nodiscard]] const Byte* contents() const noexcept;

    void put(size_t pos, const Byte *src, size_t cnt);

    [[nodiscard]] bool dumpToFile(const char* path, const char* fileName);
    [[nodiscard]] bool loadFromFile(const char* path, const char* fileName);

   private:
    /// limited for internal use because can "append" any unexpected type (like
    /// pointer and etc) with hard detection problem
    template <typename T>
    void append(const T& value);

   protected:
    size_t _rpos, _wpos;
    vectorEASTL<Byte> _storage;
};

}  // namespace Divide
#endif //_CORE_BYTE_BUFFER_H_

#include "ByteBuffer.inl"
