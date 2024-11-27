

#include "Headers/MeshImporter.h"
#include "Headers/DVDConverter.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Geometry/Shapes/Headers/Mesh.h"
#include "Geometry/Shapes/Headers/SubMesh.h"
#include "Utility/Headers/Localization.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"

namespace Divide {

namespace {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;
    const char* g_parsedAssetGeometryExt = "DVDGeom";
    const char* g_parsedAssetAnimationExt = "DVDAnim";
};

GeometryFormat GetGeometryFormatForExtension(const char* extension) noexcept
{
    if (Util::CompareIgnoreCase(extension, "3ds"))
    {
        return GeometryFormat::_3DS;
    }

    if (Util::CompareIgnoreCase(extension, "ase"))
    {
        return GeometryFormat::ASE;
    }

    if (Util::CompareIgnoreCase(extension, "fbx"))
    {
        return GeometryFormat::FBX;
    }

    if (Util::CompareIgnoreCase(extension, "md2"))
    {
        return GeometryFormat::MD2;
    }

    if (Util::CompareIgnoreCase(extension, "md5mesh"))
    {
        return GeometryFormat::MD5;
    }

    if (Util::CompareIgnoreCase(extension, "obj"))
    {
        return GeometryFormat::OBJ;
    }

    if (Util::CompareIgnoreCase(extension, "x"))
    {
        return GeometryFormat::X;
    }

    if (Util::CompareIgnoreCase(extension, "dae"))
    {
        return GeometryFormat::DAE;
    }

    if (Util::CompareIgnoreCase(extension, "gltf") ||
        Util::CompareIgnoreCase(extension, "glb"))
    {
        return GeometryFormat::GLTF;
    }

    if (Util::CompareIgnoreCase(extension, g_parsedAssetAnimationExt))
    {
        return GeometryFormat::DVD_ANIM;
    }

    if (Util::CompareIgnoreCase(extension, g_parsedAssetGeometryExt))
    {
        return GeometryFormat::DVD_GEOM;
    }

    return GeometryFormat::COUNT;
}

namespace Import
{
    bool ImportData::saveToFile([[maybe_unused]] PlatformContext& context, const ResourcePath& path, const std::string_view fileName)
    {
        ByteBuffer tempBuffer;
        assert(_vertexBuffer != nullptr);
        tempBuffer << BYTE_BUFFER_VERSION;
        tempBuffer << _ID("BufferEntryPoint");
        tempBuffer << _modelName;
        tempBuffer << _modelPath;
        tempBuffer << _animations.size();

        if (_vertexBuffer->serialize(tempBuffer))
        {
            tempBuffer << to_U32(_subMeshData.size());
            for (const SubMeshData& subMesh : _subMeshData)
            {
                if (!subMesh.serialize(tempBuffer))
                {
                    //handle error
                }
            }
            if (!_nodeData.serialize(tempBuffer))
            {
                //handle error
            }

            // Animations are handled by the SceneAnimator I/O
            return tempBuffer.dumpToFile(path, Util::StringFormat("{}.{}", fileName, g_parsedAssetGeometryExt));
        }

        return false;
    }

    bool ImportData::loadFromFile(PlatformContext& context, const ResourcePath& path, const std::string_view fileName)
    {
        ByteBuffer tempBuffer;
        if (tempBuffer.loadFromFile(path, Util::StringFormat( "{}.{}", fileName, g_parsedAssetGeometryExt ) ))
        {
            auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
            tempBuffer >> tempVer;
            if (tempVer == BYTE_BUFFER_VERSION)
            {
                U64 signature;
                tempBuffer >> signature;
                if (signature != _ID("BufferEntryPoint"))
                {
                    return false;
                }
                size_t animCount = 0u;
                tempBuffer >> _modelName;
                tempBuffer >> _modelPath;
                tempBuffer >> animCount;

                _animations.reserve(animCount);

                // The descriptor is updated from the input file but the name isn't, so that's all that we have to set here
                _vertexBuffer = context.gfx().newVB(VertexBuffer::Descriptor{ ._name = _modelName });
                if (_vertexBuffer->deserialize(tempBuffer))
                {
                    U32 subMeshCount = 0;
                    tempBuffer >> subMeshCount;
                    _subMeshData.resize(subMeshCount);
                    for (SubMeshData& subMesh : _subMeshData)
                    {
                        if (!subMesh.deserialize(tempBuffer))
                        {
                            //handle error
                            DIVIDE_UNEXPECTED_CALL();
                        }
                    }

                    if (!_nodeData.deserialize(tempBuffer))
                    {
                        //handle error
                        DIVIDE_UNEXPECTED_CALL();
                    }

                    _loadedFromFile = true;
                    return true;
                }
            }
        }

        return false;
    }

    bool SubMeshData::serialize(ByteBuffer& dataOut) const
    {
        dataOut << _name;
        dataOut << _index;
        dataOut << _boneCount;
        dataOut << _lodCount;
        dataOut << _partitionIDs;
        dataOut << _minPos;
        dataOut << _maxPos;
        dataOut << _worldOffset;
        for (const auto& triangle : _triangles)
        {
            dataOut << triangle;
        }

        return _material.serialize(dataOut);
    }

    bool SubMeshData::deserialize(ByteBuffer& dataIn)
    {
        dataIn >> _name;
        dataIn >> _index;
        dataIn >> _boneCount;
        dataIn >> _lodCount;
        dataIn >> _partitionIDs;
        dataIn >> _minPos;
        dataIn >> _maxPos;
        dataIn >> _worldOffset;
        for (auto& triangle : _triangles)
        {
            dataIn >> triangle;
        }

        return _material.deserialize(dataIn);
    }

    bool MaterialData::serialize(ByteBuffer& dataOut) const
    {
        dataOut << _ignoreTexDiffuseAlpha;
        dataOut << _doubleSided;
        dataOut << _name;
        dataOut << to_U32(_shadingMode);
        dataOut << to_U32(_bumpMethod);
        dataOut << baseColour();
        dataOut << emissive();
        dataOut << ambient();
        dataOut << specular();
        dataOut << specGloss();
        dataOut << CLAMPED_01(metallic());
        dataOut << CLAMPED_01(roughness());
        dataOut << CLAMPED_01(parallaxFactor());
        for (const TextureEntry& texture : _textures)
        {
            if (!texture.serialize(dataOut))
            {
                //handle error
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        return true;
    }

    bool MaterialData::deserialize(ByteBuffer& dataIn)
    {
        FColour3 tempColourRGB = {};
        FColour4 tempColourRGBA = {};
        SpecularGlossiness tempSG = {};
        U32 temp = {};
        F32 temp2 = {};

        dataIn >> _ignoreTexDiffuseAlpha;
        dataIn >> _doubleSided;
        dataIn >> _name;
        dataIn >> temp; _shadingMode = static_cast<ShadingMode>(temp);
        dataIn >> temp; _bumpMethod = static_cast<BumpMethod>(temp);
        dataIn >> tempColourRGBA; baseColour(tempColourRGBA);
        dataIn >> tempColourRGB;  emissive(tempColourRGB);
        dataIn >> tempColourRGB;  ambient(tempColourRGB);
        dataIn >> tempColourRGBA; specular(tempColourRGBA);
        dataIn >> tempSG; specGloss(tempSG);
        dataIn >> temp2; metallic(temp2);
        dataIn >> temp2; roughness(temp2);
        dataIn >> temp2; parallaxFactor(temp2);
        for (TextureEntry& texture : _textures)
        {
            if (!texture.deserialize(dataIn))
            {
                //handle error
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        return true;
    }

    bool TextureEntry::serialize(ByteBuffer& dataOut) const
    {
        dataOut << _textureName;
        dataOut << _texturePath;
        dataOut << _srgb;
        dataOut << _useDDSCache;
        dataOut << _isNormalMap;
        dataOut << _alphaForTransparency;
        dataOut << to_U32(_wrapU);
        dataOut << to_U32(_wrapV);
        dataOut << to_U32(_wrapW);
        dataOut << to_U32(_operation);
        return true;
    }

    bool TextureEntry::deserialize(ByteBuffer& dataIn)
    {
        U32 data = 0u;
        dataIn >> _textureName;
        dataIn >> _texturePath;
        dataIn >> _srgb;
        dataIn >> _useDDSCache;
        dataIn >> _isNormalMap;
        dataIn >> _alphaForTransparency;
        dataIn >> data; _wrapU = static_cast<TextureWrap>(data);
        dataIn >> data; _wrapV = static_cast<TextureWrap>(data);
        dataIn >> data; _wrapW = static_cast<TextureWrap>(data);
        dataIn >> data; _operation = static_cast<TextureOperation>(data);
        return true;
    }
};
    bool MeshImporter::loadMeshDataFromFile( PlatformContext& context, Import::ImportData& dataOut )
    {
        Time::ProfileTimer importTimer = {};
        importTimer.start();

        bool success = false;
        if (!context.config().debug.cache.enabled ||
            !context.config().debug.cache.geometry ||
            !dataOut.loadFromFile( context, Paths::g_geometryCacheLocation, dataOut.modelName() ) )
        {
            Console::printfn(LOCALE_STR("MESH_NOT_LOADED_FROM_FILE"), dataOut.modelName());

            if (DVDConverter::Load(context, dataOut))
            {
                if (dataOut.saveToFile(context, Paths::g_geometryCacheLocation, dataOut.modelName()))
                {
                    Console::printfn(LOCALE_STR("MESH_SAVED_TO_FILE"), dataOut.modelName());
                }
                else
                {
                    Console::printfn(LOCALE_STR("MESH_NOT_SAVED_TO_FILE"), dataOut.modelName());
                }
                success = true;
            }
        }
        else 
        {
            Console::printfn(LOCALE_STR("MESH_LOADED_FROM_FILE"), dataOut.modelName());
            dataOut.fromFile(true);
            success = true;
        }

        importTimer.stop();
        Console::d_printfn(LOCALE_STR("LOAD_MESH_TIME"),
                           dataOut.modelName(),
                           Time::MicrosecondsToMilliseconds<F32>(importTimer.get()));

        return success;
    }

    bool MeshImporter::loadMesh( PlatformContext& context, ResourcePtr<Mesh> mesh )
    {
        Time::ProfileTimer importTimer;
 
        importTimer.start();
        Import::ImportData tempMeshData( mesh->assetLocation(), mesh->assetName() );
        if (!MeshImporter::loadMeshDataFromFile( context, tempMeshData ))
        {
            return false;
        }

        mesh->renderState().drawState(true);
        mesh->geometryBuffer( tempMeshData._vertexBuffer );
        mesh->setAnimationCount(tempMeshData._animations.size());

        std::atomic_uint taskCounter(0u);

        for (const Import::SubMeshData& subMeshData : tempMeshData._subMeshData)
        {
            const size_t boneCount = tempMeshData._animations.empty() ? 0u : subMeshData._boneCount;

            // Submesh is created as a resource when added to the SceneGraph
            ResourceDescriptor<SubMesh> subMeshDescriptor( subMeshData.name().c_str() );
            subMeshDescriptor.data(
            { 
                boneCount,
                subMeshData._index, 
                0u
            });

            Handle<SubMesh> tempSubMeshHandle = CreateResource(subMeshDescriptor );

            SubMesh* tempSubMesh = Get(tempSubMeshHandle);
            // it may already be loaded
            if (!tempSubMesh->parentMesh())
            {
                Attorney::MeshImporter::addSubMesh(*mesh, tempSubMeshHandle, subMeshData._index);
                Attorney::SubMeshMesh::setParentMesh( *tempSubMesh, mesh );

                for (U8 lod = 0u, j = 0u; lod < subMeshData._lodCount; ++lod)
                {
                    if (!subMeshData._triangles[lod].empty())
                    {
                        tempSubMesh->setGeometryPartitionID(j, subMeshData._partitionIDs[j]);
                        tempSubMesh->addTriangles(subMeshData._partitionIDs[j], subMeshData._triangles[j]);
                        ++j;
                    }
                }

                Attorney::SubMeshMeshImporter::setBoundingBox(*tempSubMesh, subMeshData._minPos, subMeshData._maxPos, subMeshData._worldOffset);

                if (tempSubMesh->getMaterialTpl() == INVALID_HANDLE<Material>)
                {
                    tempSubMesh->setMaterialTpl(loadSubMeshMaterial(subMeshData._material, tempMeshData.fromFile(), boneCount > 0, taskCounter));
                }
            }
        }

        Attorney::MeshImporter::setNodeData(*mesh, tempMeshData._nodeData);

        if ( !tempMeshData._animations.empty() )
        {
            SceneAnimator* animator = mesh->getAnimator();
            DIVIDE_ASSERT(animator != nullptr);

            // Animation versioning is handled internally.
            ByteBuffer tempBuffer;

            const string saveFileName = Util::StringFormat( "{}.{}", tempMeshData.modelName(), g_parsedAssetAnimationExt );
            if (context.config().debug.cache.enabled  &&
                context.config().debug.cache.geometry &&
                tempBuffer.loadFromFile(Paths::g_geometryCacheLocation, saveFileName ))
            {
                animator->load(context, tempBuffer);
            }
            else
            {
                if (!tempMeshData.loadedFromFile())
                {
                    // We lose ownership of animations here ...
                    Attorney::SceneAnimatorMeshImporter::registerAnimations(*animator, tempMeshData._animations);
                    DIVIDE_ASSERT(tempMeshData._animations.empty());

                    animator->init(context, MOV(tempMeshData._skeleton));
                    animator->save(context, tempBuffer);
                    if (!tempBuffer.dumpToFile(Paths::g_geometryCacheLocation, saveFileName ))
                    {
                        //handle error
                        DIVIDE_UNEXPECTED_CALL();
                    }
                }
                else
                {
                    //handle error. No ASSIMP animation data available
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
        }

        WAIT_FOR_CONDITION(taskCounter.load() == 0);

        importTimer.stop();
        Console::d_printfn(LOCALE_STR("PARSE_MESH_TIME"),
                           tempMeshData.modelName(),
                           Time::MicrosecondsToMilliseconds<F32>(importTimer.get()));

        return true;
    }

    /// Load the material for the current SubMesh
    Handle<Material> MeshImporter::loadSubMeshMaterial( const Import::MaterialData& importData, const bool loadedFromCache, bool skinned, std::atomic_uint& taskCounter)
    {
        bool wasInCache = false;
        Handle<Material> tempMaterialHandle = CreateResource(ResourceDescriptor<Material>(importData.name().c_str()), wasInCache);
        if (wasInCache)
        {
            return tempMaterialHandle;
        }

        ResourcePtr<Material> tempMaterial = Get(tempMaterialHandle);

        if (!loadedFromCache)
        {
            tempMaterial->ignoreXMLData(true);
        }

        tempMaterial->properties().hardwareSkinning(skinned);
        tempMaterial->properties().emissive(importData.emissive());
        tempMaterial->properties().ambient(importData.ambient());
        tempMaterial->properties().specular(importData.specular().rgb);
        tempMaterial->properties().shininess(importData.specular().a);
        tempMaterial->properties().specGloss(importData.specGloss());
        tempMaterial->properties().metallic(importData.metallic());
        tempMaterial->properties().roughness(importData.roughness());
        tempMaterial->properties().parallaxFactor(importData.parallaxFactor());

        tempMaterial->properties().baseColour(importData.baseColour());
        tempMaterial->properties().ignoreTexDiffuseAlpha(importData.ignoreTexDiffuseAlpha());
        tempMaterial->properties().shadingMode(importData.shadingMode());
        tempMaterial->properties().bumpMethod(importData.bumpMethod());
        tempMaterial->properties().doubleSided(importData.doubleSided());

        SamplerDescriptor textureSampler = {};

        TextureDescriptor textureDescriptor
        {
            ._texType = TextureType::TEXTURE_2D_ARRAY
        };

        for (U32 i = 0; i < to_base(TextureSlot::COUNT); ++i)
        {
            const Import::TextureEntry& tex = importData._textures[i];
            if (!tex.textureName().empty())
            {
                textureSampler._wrapU = tex.wrapU();
                textureSampler._wrapV = tex.wrapV();
                textureSampler._wrapW = tex.wrapW();

                if ( tex.srgb() )
                {
                    textureDescriptor._packing = GFXImagePacking::NORMALIZED_SRGB;
                }

                textureDescriptor._textureOptions._useDDSCache = tex.useDDSCache();
                textureDescriptor._textureOptions._isNormalMap = tex.isNormalMap();
                textureDescriptor._textureOptions._alphaChannelTransparency = tex.alphaForTransparency();

                ResourceDescriptor<Texture> texture(tex.textureName(), textureDescriptor );
                texture.assetName(tex.textureName());
                texture.assetLocation(tex.texturePath());
                // No need to fire off additional threads just to wait on the result immediately after
                texture.waitForReady(true);

                tempMaterial->setTexture(static_cast<TextureSlot>(i),
                                         CreateResource( texture, taskCounter ),
                                         textureSampler,
                                         tex.operation());
            }
        }

        return tempMaterialHandle;
    }
} //namespace Divide
