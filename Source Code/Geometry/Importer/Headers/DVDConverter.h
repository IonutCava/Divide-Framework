/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

/*
Copyright (c) 2006-2012 assimp team
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.
    Neither the name of the assimp team nor the names of its contributors may be
used to endorse or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL,SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once
#ifndef _DIVIDE_FORMAT_CONVERTER_H_
#define _DIVIDE_FORMAT_CONVERTER_H_

#include "Geometry/Material/Headers/Material.h"
#include "Platform/Video/GLIM/Declarations.h"

struct aiMesh;
struct aiScene;
struct aiMaterial;

namespace Divide {
    enum class GeometryFormat : U8;

    namespace Import {
    struct ImportData;
    struct SubMeshData;
    struct MaterialData;
};

class SubMesh;
class VertexBuffer;
class PlatformContext;
class DVDConverter {
  public:

    DVDConverter() = default;

    explicit DVDConverter(PlatformContext& context, Import::ImportData& target, bool& result);

    bool load(PlatformContext& context, Import::ImportData& target) const;
   private:

    void loadSubMeshGeometry(const aiMesh* source, 
                             Import::SubMeshData& subMeshData) const;

    void loadSubMeshMaterial(Import::MaterialData& material,
                             const aiScene* source,
                             U16 materialIndex,
                             const Str128& materialName,
                             GeometryFormat format,
                             bool convertHeightToBumpMap) const;

  private:
    static void   BuildGeometryBuffers(PlatformContext& context, Import::ImportData& target);

    static hashMap<U32, TextureWrap> fillTextureWrapMap();
    static hashMap<U32, ShadingMode> fillShadingModeMap();
    static hashMap<U32, TextureOperation> fillTextureOperationMap();

  private:
    static hashMap<U32, TextureWrap> aiTextureMapModeTable;
    static hashMap<U32, ShadingMode> aiShadingModeInternalTable;
    static hashMap<U32, TextureOperation> aiTextureOperationTable;
};

};  // namespace Divide

#endif
