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
/*
Copyright (c) 2018 DIVIDE-Studio
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
#ifndef DVD_CORE_BYTE_BUFFER_H_
#define DVD_CORE_BYTE_BUFFER_H_

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

/// Helper for using >> instead of readSkip()
template <typename T>
struct Unused {
    Unused()
    {
    }
};

class ByteBuffer {
   public:
    static const U8 BUFFER_FORMAT_VERSION { 10u };
    static const size_t DEFAULT_SIZE { 0x1000 };

    ByteBuffer() noexcept = default;

    /// Resets the entire storage and the read and write positions
    void clear() noexcept;

    /// Add a specific list of items in order to mark a special point in the buffer. Can be used for skipping entire blocks later
    template<class T, size_t N>
    void addMarker(const T (&pattern)[N]);

    /// Skip (consume) everything in the buffer until we find the specified list of items. If the markers aren't found, the buffer will skip to the end!
    template<class T, size_t N>
    void readSkipToMarker(const T ( &pattern )[N]);

    /// Inserts 'value' at the specified position overriding any existing data at that location. Does not allocate storage!
    template <typename T>
    void put(size_t pos, const T& value);

    /// Appends 'value' to the buffer
    template <typename T>
    ByteBuffer& operator<<(const T& value);

    /// Reads sizeof(T) data from the buffer and saves its contents into 'value'. Reading moves the read head forward!
    template <typename T>
    ByteBuffer& operator>>(T& value);

    /// Can be used as an alternative for readSkip<U>() to preserve local coding conventions.
    template <typename U>
    ByteBuffer& operator>>(Unused<U>& value);

    /// Moves the read head sizeof(T) units forward
    template <typename T>
    void readSkip();

    /// Moves the read head 'skip' units forward
    void readSkip(size_t skip) noexcept;

    /// Reads sizeof(T) data from the buffer and saves it into 'value' but does NOT move the read head forward!
    template <typename T>
    void readNoSkip(T& value);

    /// Reads sizeof(T) data from the buffer and returns it. Reading moves the read head forward!
    template <typename T>
    void read(T& out);

    /// Reads sizeof(T) data from the buffer and returns it but does NOT move the read head forward!
    template <typename T>
    void readNoSkipFrom(size_t pos, T& out) const;

    /// Reads 'len' bytes of data from the buffer and memcpy's it into dest. Reading moves the read head forward!
    void read(Byte *dest, size_t len);

    /// Reads a packed U32 from the buffer and unpackes it into x,y,z. Reading moves the read head forward!
    void readPackXYZ(F32& x, F32& y, F32& z);
    /// Packes x,y and z into a single U32 and appends it to the buffer
    void appendPackXYZ(F32 x, F32 y, F32 z);

    /// Reads a packed U64 from the buffer and returns it. Reading moves the read head forward!
    U64  readPackGUID();
    /// Packes guid into a multiple I32s and appends them to the buffer
    void appendPackGUID(U64 guid);

    /// Appends 'cnt' bytes from 'src' to the buffer
    void append(const Byte *src, size_t cnt);

    /// Appends (sizeof(T) * 'cnt') bytes from 'src' to the buffer
    template <typename T>
    void append(const T* src, const size_t cnt);

    /// Read the byte at position 'pos' from the buffer without moving the read head
    [[nodiscard]] Byte operator[](size_t pos) const;

    /// Returns the current read head position
    [[nodiscard]] size_t rpos() const noexcept;
    /// Returns the current read head position
    /// Returns the current read head position
    [[nodiscard]] size_t rpos(size_t rpos_) noexcept;
    /// Returns the current write head position
    [[nodiscard]] size_t wpos() const noexcept;
    /// Returns the current write head position
    [[nodiscard]] size_t wpos(size_t wpos_) noexcept;
    /// Returns the size (in bytes) of the data inside of the buffer (wpos - rpos)
    [[nodiscard]] size_t bufferSize() const noexcept;
    /// Returns true if the read position and the write position are identical
    [[nodiscard]] bool   bufferEmpty() const noexcept;
    /// Returns the total size (in bytes) of the underlying storage, regardles of wpos and rpos
    [[nodiscard]] size_t storageSize() const noexcept;
    /// Returns true if the underlying storage is empty, regardless of wpos and rpos
    [[nodiscard]] bool   storageEmpty() const noexcept;

    /// Resizes the underlying storage to 'newsize' bytes and resets the read and write head positions
    void resize(size_t newsize);
    /// Reserves 'resize' bytes of additional storage in the underlying storage structure without changing the read and write head positions
    void reserve(size_t resize);

    /// Returns a raw pointer to the underlying storage data. Does NOT depend on the read head position! (use contents() + rpos() for that)
    [[nodiscard]] const Byte* contents() const noexcept;

    /// Overrides existing data in the buffer starting ad position 'pos' by memcpy-ing 'cnt' bytes from 'src'
    void put(size_t pos, const Byte *src, size_t cnt);

    /// Saves the entire buffer contents to file. Always appends the version at the end of the file
    [[nodiscard]] bool dumpToFile(const ResourcePath& path, std::string_view fileName, const U8 version = BUFFER_FORMAT_VERSION);
    /// Reads the specified file and loads its contents as raw data into the buffer. Returns FALSE if reading of the file failed OR if the version doesn't match
    /// To skip version checking, pass 0u as the version!
    /// This will erase any existing data inside of the buffer
    [[nodiscard]] bool loadFromFile(const ResourcePath& path, std::string_view fileName, const U8 version = BUFFER_FORMAT_VERSION);

   private:
    /// Limited for internal use because can "append" any unexpected type (e.g. a pointer) with hard detection problem
    template <typename T>
    void append(const T& value);

   protected:
    size_t _rpos = 0u, _wpos = 0u;
    vector<Byte> _storage;
};

}  // namespace Divide
#endif //DVD_CORE_BYTE_BUFFER_H_

#include "ByteBuffer.inl"
