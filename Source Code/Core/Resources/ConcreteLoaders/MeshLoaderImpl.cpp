#include "stdafx.h"

#include "Core/Headers/StringHelper.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Headers/EngineTaskPool.h"
#include "Core/Resources/Headers/ResourceLoader.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Shapes/Headers/Mesh.h"
#include "Geometry/Importer/Headers/MeshImporter.h"

namespace Divide {

struct MeshLoadData {
    explicit MeshLoadData(Mesh_ptr mesh,
                          ResourceCache* cache,
                          PlatformContext* context,
                          const ResourceDescriptor& descriptor)
        : _mesh(MOV(mesh)),
          _cache(cache),
          _context(context),
          _descriptor(descriptor)
    {
    }

    Mesh_ptr _mesh;
    ResourceCache* _cache;
    PlatformContext* _context;
    ResourceDescriptor _descriptor;

};

void threadedMeshLoad(MeshLoadData loadData, ResourcePath modelPath, ResourcePath modelName) {
    OPTICK_EVENT();

    Import::ImportData tempMeshData(modelPath, modelName);
    if (MeshImporter::loadMeshDataFromFile(*loadData._context, tempMeshData)) {
        if (!MeshImporter::loadMesh(tempMeshData.loadedFromFile(), loadData._mesh, *loadData._context, loadData._cache, tempMeshData)) {
            loadData._mesh.reset();
        }
    } else {
        loadData._mesh.reset();
        //handle error
        const string msg = Util::StringFormat("Failed to import mesh [ %s ]!", modelName.c_str());
        DIVIDE_UNEXPECTED_CALL_MSG(msg.c_str());
    }

    if (!loadData._mesh->load()) {
        loadData._mesh.reset();
    }
}

template<>
CachedResource_ptr ImplResourceLoader<Mesh>::operator()() {
    Mesh_ptr ptr(MemoryManager_NEW Mesh(_context.gfx(),
                                        _cache,
                                        _loadingDescriptorHash,
                                        _descriptor.resourceName(),
                                        _descriptor.assetName(),
                                        _descriptor.assetLocation()),
                                DeleteResource(_cache));

    if (ptr) {
        ptr->setState(ResourceState::RES_LOADING);
    }

    MeshLoadData loadingData(ptr, _cache, &_context, _descriptor);
    if (_descriptor.threaded()) {
        const ResourcePath assetLocaltion = _descriptor.assetLocation();
        const ResourcePath assetName = _descriptor.assetName();
        Task* task = CreateTask([this, assetLocaltion, assetName, loadingData](const Task &) {
                                    threadedMeshLoad(loadingData, assetLocaltion, assetName);
                                });

        Start(*task, _context.taskPool(TaskPoolType::HIGH_PRIORITY));
    } else {
        threadedMeshLoad(loadingData, _descriptor.assetLocation(), _descriptor.assetName());
    }
    return ptr;
}

}
