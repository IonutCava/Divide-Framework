

#include "Headers/ResourcePath.h"
#include "Headers/FileManagement.h"

namespace Divide {

size_t ResourcePath::length() const noexcept
{
    return _fileSystemPath.string().length();
}

bool ResourcePath::empty() const noexcept
{
    return _fileSystemPath.empty();
}

ResourcePath& ResourcePath::append(const std::string_view str)
{
    _fileSystemPath = std::filesystem::path( string().append(str) );
    return *this;
}

ResourcePath ResourcePath::operator+(std::string_view str) const
{
    ResourcePath ret = *this;
    ret.append(str);
    return ret;
}

ResourcePath& ResourcePath::makeRelative( const ResourcePath& base )
{
    _fileSystemPath = std::filesystem::relative( _fileSystemPath, base._fileSystemPath );
    return *this;
}

ResourcePath ResourcePath::getRelative( const ResourcePath& base ) const
{
    ResourcePath ret = *this;
    return ret.makeRelative(base);
}

ResourcePath operator/ ( const ResourcePath& lhs, const ResourcePath& rhs )
{
    return ResourcePath { (lhs.fileSystemPath() / rhs.fileSystemPath()).string() };
}

ResourcePath& operator/=( ResourcePath& lhs, const ResourcePath& rhs )
{
    lhs = lhs / rhs;
    return lhs;
}

ResourcePath  operator/ ( const ResourcePath& lhs, const std::string_view rhs )
{
    return ResourcePath{ (lhs.fileSystemPath() / rhs).string() };
}

ResourcePath& operator/=( ResourcePath& lhs, const std::string_view rhs )
{
    lhs = lhs / rhs;
    return lhs;
}

bool operator== (const ResourcePath& lhs, std::string_view rhs)
{
    return lhs == ResourcePath(rhs);
}

bool operator!= (const ResourcePath& lhs, std::string_view rhs)
{
    return lhs != ResourcePath(rhs);
}

bool operator== (const ResourcePath& lhs, const ResourcePath& rhs)
{
    return lhs.fileSystemPath().lexically_normal().compare(rhs.fileSystemPath().lexically_normal()) == 0;
}

bool operator!= (const ResourcePath& lhs, const ResourcePath& rhs)
{
    return lhs.fileSystemPath().lexically_normal().compare( rhs.fileSystemPath().lexically_normal() ) != 0;
}

bool operator==( const FileNameAndPath& lhs, const FileNameAndPath& rhs )
{
    return lhs._fileName == rhs._fileName &&
           lhs._path == rhs._path;
}

bool operator!=( const FileNameAndPath& lhs, const FileNameAndPath& rhs )
{
    return lhs._fileName != rhs._fileName ||
           lhs._path != rhs._path;
}

}; //namespace Divide
