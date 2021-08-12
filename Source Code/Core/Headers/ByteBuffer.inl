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

#ifndef _CORE_BYTE_BUFFER_INL_
#define _CORE_BYTE_BUFFER_INL_

namespace Divide {

template <typename T>
void ByteBuffer::put(const size_t pos, const T& value) {
    put(pos, (Byte*)&value, sizeof(T));
}


template <typename T>
ByteBuffer& ByteBuffer::operator<<(const T& value) {
    append<T>(value);
    return *this;
}


template <typename T>
ByteBuffer& ByteBuffer::operator>>(T& value) {
    value = read<T>();
    return *this;
}

template<>
inline ByteBuffer& ByteBuffer::operator>>(bool& value) {
    value = read<I8>() == to_I8(1);
    return *this;
}

template<>
inline ByteBuffer& ByteBuffer::operator>>(stringImpl& value) {
    value.clear();
    // prevent crash at wrong string format in packet
    while (rpos() < size()) {
        const char c = read<char>();
        if (c == to_U8(0)) {
            break;
        }
        value += c;
    }
    return *this;
}

template<>
inline ByteBuffer& ByteBuffer::operator>>(ResourcePath& value) {
    stringImpl temp{};

    *this >> temp;
     value = ResourcePath(temp);

    return *this;
}

template <typename T>
void ByteBuffer::read_noskip(T& value) {
    value = read<T>(_rpos);
}

template <>
inline void ByteBuffer::read_noskip(bool& value) {
    value = read<I8>(_rpos) == to_I8(1);
}

template <>
inline void ByteBuffer::read_noskip(stringImpl& value) {
    value.clear();
    size_t inc = 0;
    // prevent crash at wrong string format in packet
    while (rpos() < size()) {
        const char c = read<char>(); ++inc;
        if (c == to_U8(0)) {
            break;
        }
        value += c;
    }
    _rpos -= inc;
}

template <>
inline void ByteBuffer::read_noskip(ResourcePath& value) {
    stringImpl temp{};
    read_noskip(temp);
    value = ResourcePath(temp);
}

template <typename U>
ByteBuffer& ByteBuffer::operator>>(Unused<U>& value) {
    ACKNOWLEDGE_UNUSED(value);

    read_skip<U>();
    return *this;
}

template <typename T>
void ByteBuffer::read_skip() {
    read_skip(sizeof(T));
}

template <>
inline void ByteBuffer::read_skip<char *>() {
    stringImpl temp;
    *this >> temp;
}

template <>
inline void ByteBuffer::read_skip<char const *>() {
    read_skip<char *>();
}

template <>
inline void ByteBuffer::read_skip<stringImpl>() {
    read_skip<char *>();
}

inline void ByteBuffer::read_skip(const size_t skip) {
    if (_rpos + skip > size()) {
        DIVIDE_UNEXPECTED_CALL();
    }
    _rpos += skip;
}

template <typename T>
T ByteBuffer::read() {
    T r = read<T>(_rpos);
    _rpos += sizeof(T);
    return r;
}

template <typename T>
T ByteBuffer::read(const size_t pos) const {
    if (pos + sizeof(T) > size()) {
        DIVIDE_UNEXPECTED_CALL();
    }

    T val = *(T const *)&_storage[pos];
    //EndianConvert(val);
    return val;
}

inline void ByteBuffer::read(Byte *dest, const size_t len) {
    if (_rpos + len > size()) {
        DIVIDE_UNEXPECTED_CALL();
    }

    memcpy(dest, &_storage[_rpos], len);
    _rpos += len;
}

inline void ByteBuffer::readPackXYZ(F32& x, F32& y, F32& z) {
    U32 packed = 0;
    *this >> packed;
    x = ((packed & 0x7FF) << 21 >> 21) * 0.25f;
    y = ((packed >> 11 & 0x7FF) << 21 >> 21) * 0.25f;
    z = (packed >> 22 << 22 >> 22) * 0.25f;
}

inline U64 ByteBuffer::readPackGUID() {
    U64 guid = 0;
    U8 guidmark = 0;
    *this >> guidmark;

    for (I32 i = 0; i < 8; ++i) {
        if (guidmark & to_U8(1) << i) {
            U8 bit;
            *this >> bit;
            guid |= static_cast<U64>(bit) << i * 8;
        }
    }

    return guid;
}


inline void ByteBuffer::appendPackXYZ(const F32 x, const F32 y, const F32 z) {
    U32 packed = 0u;
    packed |= to_I32(x / 0.25f) & 0x7FF;
    packed |= (to_I32(y / 0.25f) & 0x7FF) << 11;
    packed |= (to_I32(z / 0.25f) & 0x3FF) << 22;
    *this << packed;
}

inline void ByteBuffer::appendPackGUID(U64 guid) {
    U8 packGUID[8 + 1];
    packGUID[0] = 0u;
    size_t size = 1;
    for (U8 i = 0; guid != 0; ++i) {
        if (guid & 0xFF) {
            packGUID[0] |= to_U8(1 << i);
            packGUID[size] = to_U8(guid & 0xFF);
            ++size;
        }

        guid >>= 8;
    }

    append(packGUID, size);
}

inline Byte ByteBuffer::operator[](const size_t pos) const {
    return read<Byte>(pos);
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

inline size_t ByteBuffer::size() const noexcept {
    return _storage.size();
}

inline bool ByteBuffer::empty() const noexcept {
    return _storage.empty();
}

inline void ByteBuffer::resize(const size_t newsize) {
    _storage.resize(newsize);
    _rpos = 0;
    _wpos = size();
}

inline void ByteBuffer::reserve(const size_t resize) {
    if (resize > size()) {
        _storage.reserve(resize);
    }
}

inline const Byte* ByteBuffer::contents() const noexcept {
    return _storage.data();
}

inline void ByteBuffer::put(const size_t pos, const Byte *src, const size_t cnt) {
    if (pos + cnt > size()) {
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
inline void ByteBuffer::append(const stringImpl& str) {
    append(str.c_str(), str.length());
    append(to_U8(0));
}

template<>
inline void ByteBuffer::append(const ResourcePath& str) {
    append(str.c_str(), str.str().length());
    append(to_U8(0));
}

template<>
inline void ByteBuffer::append(const bool& value) {
    append(value ? to_I8(1) : to_I8(0));
}

template<>
inline void ByteBuffer::append(const ByteBuffer& buffer) {
    if (buffer.wpos()) {
        append(buffer.contents(), buffer.wpos());
    }
}


//specializations
template <typename T>
inline ByteBuffer& operator<<(ByteBuffer& b, vec2<T> const& v) {
    b << v.x;
    b << v.y;
    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, vec2<T> &v) {
    b >> v.x;
    b >> v.y;
    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, vec3<T> const &v) {
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
inline ByteBuffer &operator<<(ByteBuffer &b, vec4<T> const &v) {
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
inline ByteBuffer& operator<<(ByteBuffer& b, Quaternion<T> const& q) {
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
inline ByteBuffer &operator<<(ByteBuffer &b, mat2<T> const &m) {
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
inline ByteBuffer &operator<<(ByteBuffer &b, mat3<T> const &m) {
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
inline ByteBuffer &operator<<(ByteBuffer &b, mat4<T> const &m) {
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
inline ByteBuffer &operator<<(ByteBuffer &b, const std::array<stringImpl, N>& a) {
    b << static_cast<U64>(N);
    for (const stringImpl& str : a) {
        b << str;
    }

    return b;
}

template <size_t N>
inline ByteBuffer &operator>>(ByteBuffer &b, std::array<stringImpl, N>& a) {
    U64 size;
    b >> size;
    assert(size == static_cast<U64>(N));
    for (stringImpl& str : a) {
        b >> str;
    }

    return b;
}

template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, vectorEASTL<T> const &v) {
    b << to_U32(v.size());
    b.append(v.data(), v.size());

    return b;
}

template <typename T>
inline ByteBuffer &operator>>(ByteBuffer &b, vectorEASTL<T> &v) {
    U32 vsize;
    b >> vsize;
    v.resize(vsize);
    b.read((Byte*)v.data(), vsize * sizeof(T));
    return b;
}

template <>
inline ByteBuffer &operator<<(ByteBuffer &b, vectorEASTL<stringImpl> const &v) {
    b << to_U32(v.size());
    for (const stringImpl& str : v) {
        b << str;
    }

    return b;
}

template <>
inline ByteBuffer &operator>>(ByteBuffer &b, vectorEASTL<stringImpl> &v) {
    U32 vsize;
    b >> vsize;
    v.resize(vsize);
    for (stringImpl& str : v) {
        b >> str;
    }
    return b;
}
template <typename T>
inline ByteBuffer &operator<<(ByteBuffer &b, std::list<T> const &v) {
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
inline ByteBuffer &operator<<(ByteBuffer &b, std::map<K, V> &m) {
    b << to_U32(m.size());
    for (typename std::map<K, V>::value_type i : m) {
        b << i.first;
        b << i.second;
    }
    return b;
}

template <typename K, typename V>
inline ByteBuffer &operator>>(ByteBuffer &b, std::map<K, V> &m) {
    K k;
    V v;
    U32 msize;
    b >> msize;
    m.clear();
    while (msize--) {
        b >> k >> v;
        m.insert(std::make_pair(k, v));
    }
    return b;
}

}

#endif //_CORE_BYTE_BUFFER_INL_