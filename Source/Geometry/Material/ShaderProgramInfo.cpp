#include "Headers/ShaderProgramInfo.h"
#include "Core/Resources/Headers/ResourceCache.h"

namespace Divide
{

ShaderProgramInfo::~ShaderProgramInfo()
{
    DestroyResource( _shaderRef );
}

ShaderProgramInfo ShaderProgramInfo::clone() const
{
    ShaderProgramInfo ret = {};
    ret._shaderKeyCache = _shaderKeyCache;
    ret._shaderCompStage = _shaderCompStage;
    if ( _shaderRef != INVALID_HANDLE<ShaderProgram> )
    {
        ret._shaderRef = GetResourceRef<ShaderProgram>( Get( _shaderRef )->descriptorHash() );
    }

    return ret;
}

} //namespace Divide
