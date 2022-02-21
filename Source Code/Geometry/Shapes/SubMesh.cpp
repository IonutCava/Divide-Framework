#include "stdafx.h"

#include "Headers/SubMesh.h"


#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Headers/Mesh.h"

#include "Core/Resources/Headers/ResourceCache.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"

namespace Divide {

SubMesh::SubMesh(GFXDevice& context, ResourceCache* parentCache, const size_t descriptorHash, const Str256& name, const ObjectFlag flag)
    : Object3D(context, parentCache, descriptorHash, name, {}, {}, ObjectType::SUBMESH, to_base(flag))
{
}

void SubMesh::setParentMesh(Mesh* const parentMesh) {
    assert(_parentMesh == nullptr);

    _parentMesh = parentMesh;
    setGeometryVB(_parentMesh->getGeometryVB());
}

};