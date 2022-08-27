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
    if (MeshImporter::loadMeshDataFromFile(*loadData._context, tempMeshData) &&
        MeshImporter::loadMesh(tempMeshData.loadedFromFile(), loadData._mesh.get(), *loadData._context, loadData._cache, tempMeshData) &&
        loadData._mesh->load())
    {
        NOP();
    } else {
        loadData._cache->remove(loadData._mesh.get());
        loadData._mesh.reset();
        Console::errorfn(Locale::Get(_ID("ERROR_IMPORTER_MESH")), modelName.c_str());
        return;
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
    const ResourcePath assetLocaltion = _descriptor.assetLocation();
    const ResourcePath assetName = _descriptor.assetName();
    Task* task = CreateTask([this, assetLocaltion, assetName, loadingData](const Task &) {
                                threadedMeshLoad(loadingData, assetLocaltion, assetName);
                            });

    Start(*task, _context.taskPool(TaskPoolType::HIGH_PRIORITY));

    return ptr;
}

}
