

#include "Headers/ByteBuffer.h"

#include "Core/Headers/StringHelper.h"

#include "Utility/Headers/Localization.h"

#include "Platform/File/Headers/FileManagement.h"

namespace Divide {

ByteBufferException::ByteBufferException(const bool add, const size_t pos, const size_t esize, const size_t size)
  : _add(add), _pos(pos), _esize(esize), _size(size)
{
    printPosError();
}

void ByteBufferException::printPosError() const {

    Console::errorfn(LOCALE_STR("BYTE_BUFFER_ERROR"),
                     _add ? "append" : "read", 
                     _pos,
                     _esize,
                     _size);
}

void ByteBuffer::clear() noexcept {
    _storage.clear();
    _rpos = _wpos = 0u;
}

void ByteBuffer::append(const Byte *src, const size_t cnt) {
    if (src != nullptr && cnt > 0) {
        _storage.reserve(DEFAULT_SIZE);

        if (_storage.size() < _wpos + cnt) {
            _storage.resize(_wpos + cnt);
        }

        memcpy(&_storage[_wpos], src, cnt);
        _wpos += cnt;
    }
}


bool ByteBuffer::dumpToFile(const ResourcePath& path, const std::string_view fileName, const U8 version)
{
    if (!_storage.empty() && to_U8(_storage.back()) != version)
    {
        append(version);
    }

    return writeFile(path, fileName, _storage.data(), _storage.size(), FileType::BINARY) == FileError::NONE;
}

bool ByteBuffer::loadFromFile(const ResourcePath& path, const std::string_view fileName, const U8 version)
{
    clear();
    _storage.reserve(DEFAULT_SIZE);
    if (readFile(path, fileName, _storage, FileType::BINARY) == FileError::NONE)
    {
        _wpos = storageSize();
        return version == 0u || to_U8(_storage.back()) == version;
    }

    return false;
}

}  // namespace Divide
