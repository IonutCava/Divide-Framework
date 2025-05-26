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

#ifndef DVD_CORE_BYTE_BUFFER_INL_
#define DVD_CORE_BYTE_BUFFER_INL_

namespace Divide
{

template<class T, size_t N>
void ByteBuffer::addMarker(const T( &pattern )[N])
{
    for (const T& entry : pattern)
    {
        append<T>(entry);
    }
}

template<class T, size_t N>
void ByteBuffer::readSkipToMarker(const T( &pattern )[N])
{
    T tempMarker{0};
    bool found = false;
    do
    {
        while (bufferSize() >= sizeof(T) && tempMarker != pattern[0])
        {
            read<T>( tempMarker );
        }

        if (bufferSize() >= sizeof(T))
        {
            assert(tempMarker == pattern[0]);
            found = true;
            for (size_t i = 1u; i < N; ++i)
            {
                read<T>( tempMarker );
                if (tempMarker != pattern[i])
                {
                    found = false;
                    break;
                }
            }
        }
    } while (bufferSize() >= sizeof(T) && !found);

    // We wanted to skip to a marker. None was found so we need to skip to the end!
    if (!found)
    {
        Byte temp{};
        // Consume a byte at a time until we reach the end.
        while (!bufferEmpty())
        {
            read<Byte>(temp);
        }
    }
}

template <typename T>
void ByteBuffer::put(const size_t pos, const T& value)
{
    put(pos, (Byte*)&value, sizeof(T));
}


template <typename T>
ByteBuffer& ByteBuffer::operator<<(const T& value)
{
    append<T>(value);
    return *this;
}

template <typename T>
ByteBuffer& ByteBuffer::operator>>(T& value)
{
    read<T>(value);
    return *this;
}

template<>
inline ByteBuffer& ByteBuffer::operator>>(bool& value)
{
    I8 temp{};
    read<I8>(temp);
    value = temp == I8_ONE;
    return *this;
}

template<>
inline ByteBuffer& ByteBuffer::operator>>(string& value)
{
    value.clear();

    char c;
    while (rpos() < storageSize())
    {
        read<char>(c);
        if (c == U8_ZERO )
        {
            break;
        }
        value += c;
    }

    return *this;
}

template<>
inline ByteBuffer& ByteBuffer::operator>>(std::string& value)
{
    value.clear();

    char c;
    while (rpos() < storageSize())
    {
        read<char>(c);
        if (c == U8_ZERO )
        {
            break;
        }
        value += c;
    }

    return *this;
}

template <typename T>
void ByteBuffer::readNoSkip(T& value)
{
    readNoSkipFrom<T>(_rpos, value);
}

template <>
inline void ByteBuffer::readNoSkip(bool& value) {
    I8 temp{};
    readNoSkipFrom<I8>(_rpos, temp);
    value = temp == I8_ONE;
}

template <>
inline void ByteBuffer::readNoSkip(string& value)
{
    value.clear();
    size_t inc = 0;

    char c;
    // prevent crash at wrong string format in packet
    while (rpos() < storageSize())
    {
        read<char>(c);
        ++inc;
        if (c == U8_ZERO )
        {
            break;
        }
        value += c;
    }

    _rpos -= inc;
}

template <>
inline void ByteBuffer::readNoSkip(std::string& value)
{
    value.clear();
    size_t inc = 0;

    char c;
    // prevent crash at wrong string format in packet
    while (rpos() < storageSize())
    {
        read<char>(c);
        ++inc;
        if (c == U8_ZERO )
        {
            break;
        }
        value += c;
    }

    _rpos -= inc;
}

template <>
inline void ByteBuffer::readNoSkip(ResourcePath& value)
{
    string temp{};
    readNoSkip(temp);
    value = ResourcePath(temp);
}

template <typename U>
ByteBuffer& ByteBuffer::operator>>([[maybe_unused]] Unused<U>& value)
{
    readSkip<U>();
    return *this;
}

template <typename T>
void ByteBuffer::readSkip()
{
    readSkip(sizeof(T));
}

template <>
inline void ByteBuffer::readSkip<char *>()
{
    string temp;
    *this >> temp;
}

template <>
inline void ByteBuffer::readSkip<char const *>()
{
    readSkip<char *>();
}

template <>
inline void ByteBuffer::readSkip<string>()
{
    readSkip<char *>();
}

inline void ByteBuffer::readSkip(const size_t skip) noexcept
{
    if (_rpos + skip > storageSize())
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    _rpos += skip;
}

template <typename T>
void ByteBuffer::read(T& out)
{
    readNoSkipFrom<T>(_rpos, out);
    _rpos += sizeof(T);
}

template <typename T>
void ByteBuffer::readNoSkipFrom(const size_t pos, T& out) const
{
    if (pos + sizeof(T) > storageSize())
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    std::memcpy(&out, &_storage[pos], sizeof(T));
}

inline void ByteBuffer::read(Byte *dest, const size_t len)
{
    if (_rpos + len > storageSize())
    {
        DIVIDE_UNEXPECTED_CALL();
    }

    memcpy(dest, &_storage[_rpos], len);
    _rpos += len;
}

inline void ByteBuffer::readPackXYZ(F32& x, F32& y, F32& z)
{
    U32 packed = 0;
    *this >> packed;
    x = ((packed & 0x7FF) << 21 >> 21) * 0.25f;
    y = ((packed >> 11 & 0x7FF) << 21 >> 21) * 0.25f;
    z = (packed >> 22 << 22 >> 22) * 0.25f;
}

inline U64 ByteBuffer::readPackGUID()
{
    U64 guid = 0;
    U8 guidmark = 0;
    *this >> guidmark;

    for (I32 i = 0; i < 8; ++i)
    {
        if (guidmark & U8_ONE << i)
        {
            U8 bit;
            *this >> bit;
            guid |= static_cast<U64>(bit) << i * 8;
        }
    }

    return guid;
}


inline void ByteBuffer::appendPackXYZ(const F32 x, const F32 y, const F32 z)
{
    U32 packed = 0u;
    packed |= to_U32( to_I32(x / 0.25f) & 0x7FF);
    packed |= to_U32((to_I32(y / 0.25f) & 0x7FF) << 11);
    packed |= to_U32((to_I32(z / 0.25f) & 0x3FF) << 22);
    *this << packed;
}

inline void ByteBuffer::appendPackGUID(U64 guid)
{
    U8 packGUID[8 + 1];
    packGUID[0] = 0u;
    size_t size = 1;
    for (U8 i = 0; guid != 0; ++i)
    {
        if (guid & 0xFF)
        {
            packGUID[0] |= U8_ONE << i;
            packGUID[size] = to_U8(guid & 0xFF);
            ++size;
        }

        guid >>= 8;
    }

    append(packGUID, size);
}

inline Byte ByteBuffer::operator[](const size_t pos) const{
    Byte ret{};
    readNoSkipFrom<Byte>( pos, ret );
    return ret;
}

inline size_t ByteBuffer::rpos() const noexcept {
    return _rpos;
}

inline size_t ByteBuffer::rpos(const size_t rpos_) noexcept {
    _rpos = rpos_;
    return _rpos;
}

inline size_t ByteBuffer::wpos() const noexcept {
    return _wpos;
}

inline size_t ByteBuffer::wpos(const size_t wpos_) noexcept {
    _wpos = wpos_;
    return _wpos;
}

inline size_t ByteBuffer::bufferSize() const noexcept {
    return _wpos >= _rpos ? _wpos - _rpos : 0u;
}

inline bool ByteBuffer::bufferEmpty() const noexcept {
    return bufferSize() == 0u;
}

inline size_t ByteBuffer::storageSize() const noexcept {
    return _storage.size();
}

inline bool ByteBuffer::storageEmpty() const noexcept {
    return _storage.empty();
}

inline void ByteBuffer::resize(const size_t newsize) {
    _storage.resize(newsize);
    _rpos = 0;
    _wpos = storageSize();
}

inline void ByteBuffer::reserve(const size_t resize) {
    if (resize > storageSize()) {
        _storage.reserve(resize);
    }
}

inline const Byte* ByteBuffer::contents() const noexcept {
    return _storage.data();
}

inline void ByteBuffer::put(const size_t pos, const Byte *src, const size_t cnt) {
    if (pos + cnt > storageSize()) {
        DIVIDE_UNEXPECTED_CALL();
    }
    memcpy(&_storage[pos], src, cnt);
}

template <typename T>
void ByteBuffer::append(const T& value) {
    append((const Byte*)&value, sizeof(T));
}

template <typename T>
void ByteBuffer::append(const T* src, const size_t cnt) {
    return append((const Byte*)src, cnt * sizeof(T));
}


template<>
inline void ByteBuffer::append(const string& str) {
    append(str.c_str(), str.length());
    append(U8_ZERO);
}

template<>
inline void ByteBuffer::append(const std::string& str) {
    append(str.c_str(), str.length());
    append(U8_ZERO);
}

template<>
inline void ByteBuffer::append(const ResourcePath& str) {
    append(str.string() );
}

template<>
inline void ByteBuffer::append(const bool& value) {
    append(value ? I8_ONE : I8_ZERO);
}

template<>
inline void ByteBuffer::append(const ByteBuffer& buffer) {
    if (buffer.wpos()) {
        append(buffer.contents(), buffer.wpos());
    }
}

//specializations
template <typename T>
inline ByteBuffer& operator<<(ByteBuffer& b, const vec2<T>& v) {
    b << v.x;
    b << v.y;
    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, vec2<T>& v) {
    b >> v.x;
    b >> v.y;
    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const vec3<T>& v) {
    b << v.x;
    b << v.y;
    b << v.z;
    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, vec3<T> &v) {
    b >> v.x;
    b >> v.y;
    b >> v.z;
    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const vec4<T>& v) {
    b << v.x;
    b << v.y;
    b << v.z;
    b << v.w;
    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, vec4<T> &v) {
    b >> v.x;
    b >> v.y;
    b >> v.z;
    b >> v.w;
    return b;
}
template <typename T>
inline ByteBuffer& operator<<(ByteBuffer& b, const Quaternion<T>& q) {
    b << q.X();
    b << q.Y();
    b << q.Z();
    b << q.W();
    return b;
}

template <typename T>
inline ByteBuffer& operator>>(ByteBuffer& b, Quaternion<T>& q) {
    vec4<T> elems = {};
    b >> elems.x; q.X(elems.x);
    b >> elems.y; q.Y(elems.y);
    b >> elems.z; q.Z(elems.z);
    b >> elems.w; q.W(elems.w);
    
    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const mat2<T>& m) {
    for (U8 i = 0; i < 4; ++i) {
        b << m[i];
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, mat2<T> &m) {
    for (U8 i = 0; i < 4; ++i) {
        b >> m[i];
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const mat3<T>& m) {
    for (U8 i = 0; i < 9; ++i) {
        b << m[i];
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, mat3<T> &m) {
    for (U8 i = 0; i < 9; ++i) {
        b >> m[i];
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const mat4<T>& m) {
    for (U8 i = 0; i < 16; ++i) {
        b << m[i];
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, mat4<T> &m) {
    for (U8 i = 0; i < 16; ++i) {
        b >> m[i];
    }

    return b;
}
template <typename T, size_t N>
inline ByteBuffer &operator<<(ByteBuffer &b, const std::array<T, N>& v) {
    b << static_cast<U64>(N);
    b.append(v.data(), N);

    return b;
}

template <typename T, size_t N>
inline ByteBuffer &operator>>(ByteBuffer &b, std::array<T, N>& a) {
    U64 size;
    b >> size;
    assert(size == static_cast<U64>(N));
    b.read((Byte*)a.data(), N * sizeof(T));

    return b;
}

template <size_t N>
inline ByteBuffer &operator<<(ByteBuffer &b, const std::array<string, N>& a) {
    b << static_cast<U64>(N);
    for (const string& str : a) {
        b << str;
    }

    return b;
}

template <size_t N>
inline ByteBuffer &operator>>(ByteBuffer &b, std::array<string, N>& a) {
    U64 size;
    b >> size;
    assert(size == static_cast<U64>(N));
    for (string& str : a) {
        b >> str;
    }

    return b;
}

template <size_t N>
inline ByteBuffer &operator<<(ByteBuffer &b, const std::array<std::string, N>& a) {
    b << static_cast<U64>(N);
    for (const std::string& str : a) {
        b << str;
    }

    return b;
}

template <size_t N>
inline ByteBuffer &operator>>(ByteBuffer &b, std::array<std::string, N>& a) {
    U64 size;
    b >> size;
    assert(size == static_cast<U64>(N));
    for (std::string& str : a) {
        b >> str;
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const vector<T>& v) {
    b << to_U32(v.size());
    b.append(v.data(), v.size());

    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, vector<T>& v) {
    U32 vsize;
    b >> vsize;
    v.resize(vsize);
    b.read((Byte*)v.data(), vsize * sizeof(T));
    return b;
}

template <>
inline ByteBuffer &operator<<(ByteBuffer &b, const vector<string>& v) {
    b << to_U32(v.size());
    for (const string& str : v) {
        b << str;
    }

    return b;
}

template <>
inline ByteBuffer &operator>>(ByteBuffer &b, vector<string>& v) {
    U32 vsize;
    b >> vsize;
    v.resize(vsize);
    for (string& str : v) {
        b >> str;
    }
    return b;
}

template <>
inline ByteBuffer &operator<<(ByteBuffer &b, const vector<std::string>& v) {
    b << to_U32(v.size());
    for (const std::string& str : v) {
        b << str;
    }

    return b;
}

template <>
inline ByteBuffer &operator>>(ByteBuffer &b, vector<std::string>& v) {
    U32 vsize;
    b >> vsize;
    v.resize(vsize);
    for (std::string& str : v) {
        b >> str;
    }
    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, const std::list<T>& v) {
    b << to_U32(v.size());
    for (const T& i : v) {
        b << i;
    }
    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, std::list<T> &v) {
    T t;
    U32 vsize;
    b >> vsize;
    v.clear();
    v.reverse(vsize);
    while (vsize--) {
        b >> t;
        v.push_back(t);
    }
    return b;
}

template <typename K, typename V>
inline ByteBuffer &operator<<(ByteBuffer &b, const eastl::map<K, V>& m) {
    b << to_U32(m.size());
    for (typename eastl::map<K, V>::value_type i : m) {
        b << i.first;
        b << i.second;
    }
    return b;
}

template <typename K, typename V>
inline ByteBuffer &operator>>(ByteBuffer &b, eastl::map<K, V>& m) {
    K k;
    V v;
    U32 msize;
    b >> msize;
    m.clear();
    while (msize--) {
        b >> k >> v;
        m.insert(eastl::make_pair(k, v));
    }
    return b;
}

inline ByteBuffer& operator<<( ByteBuffer& b, const ResourcePath& p )
{
    b << p.string();

    return b;
}

inline ByteBuffer& operator>>( ByteBuffer& b, ResourcePath& p )
{
    string temp{};
    b >> temp;
    p = ResourcePath(temp);

    return b;
}

template<size_t N>
inline ByteBuffer& operator<<( ByteBuffer& b, const Str<N>& s )
{
    const string temp{ s.c_str() };
    b << temp;

    return b;
}

template<size_t N>
inline ByteBuffer& operator>>( ByteBuffer& b, Str<N>& s )
{
    string temp{};

    b >> temp;
    s = temp.c_str();

    return b;
}

}

#endif //DVD_CORE_BYTE_BUFFER_INL_
