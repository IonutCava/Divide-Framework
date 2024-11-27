

#include "Headers/Object3D.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/ProjectManager.h"
#include "Physics/Headers/PXDevice.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/RenderPackage.h"

#include "ECS/Components/Headers/AnimationComponent.h"
#include "ECS/Components/Headers/RenderingComponent.h"

namespace Divide {

Object3D::Object3D( const ResourceDescriptorBase& descriptor, const SceneNodeType type)
    : SceneNode(descriptor,
                type,
                to_base(ComponentType::TRANSFORM) | to_base(ComponentType::BOUNDS) | to_base(ComponentType::RENDERING))
{
    _geometryPartitionIDs.fill(VertexBuffer::INVALID_PARTITION_ID);
    _geometryPartitionIDs[0] = 0u;
}

void Object3D::rebuildInternal()
{
    NOP();
}

const VertexBuffer_ptr& Object3D::geometryBuffer()
{
    DIVIDE_ASSERT(_geometryBuffer != nullptr);

    if (geometryDirty())
    {
        geometryDirty(false);
        rebuildInternal();
    }

    return _geometryBuffer;
}

void Object3D::setMaterialTpl(const Handle<Material> material)
{
    SceneNode::setMaterialTpl(material);

    if (_materialTemplate != INVALID_HANDLE<Material> && geometryBuffer() != nullptr)
    {
        Get<Material>(material)->setPipelineLayout(GetGeometryBufferType(type()), geometryBuffer()->generateAttributeMap());
    }
}

void Object3D::prepareRender(SceneGraphNode* sgn,
                             RenderingComponent& rComp,
                             RenderPackage& pkg,
                             GFX::MemoryBarrierCommand& postDrawMemCmd,
                             RenderStagePass renderStagePass,
                             const CameraSnapshot& cameraSnapshot,
                             bool refreshData)
{
    if (geometryBuffer() != nullptr)
    {
        rComp.setIndexBufferElementOffset(geometryBuffer()->firstIndexOffsetCount());
    }

    SceneNode::prepareRender(sgn, rComp, pkg, postDrawMemCmd, renderStagePass, cameraSnapshot, refreshData);
}

void Object3D::buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut)
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (geometryBuffer() != nullptr)
    {
        if (cmdsOut.size() == 0u)
        {
            GenericDrawCommand& cmd = cmdsOut.emplace_back();
            toggleOption( cmd, CmdRenderOptions::RENDER_INDIRECT );

            cmd._sourceBuffer = geometryBuffer()->handle();
            cmd._cmd.indexCount = to_U32(geometryBuffer()->getPartitionIndexCount(_geometryPartitionIDs[0]));
            cmd._cmd.firstIndex = to_U32(geometryBuffer()->getPartitionOffset(_geometryPartitionIDs[0]));
            cmd._cmd.instanceCount = sgn->instanceCount();
        }
        
        U16 prevID = 0;
        RenderingComponent* rComp = sgn->get<RenderingComponent>();
        assert(rComp != nullptr);

        for (U8 i = 0; i < to_U8(_geometryPartitionIDs.size()); ++i)
        {
            U16 id = _geometryPartitionIDs[i];
            if (id == U16_MAX)
            {
                assert(i > 0);
                id = prevID;
            }

            rComp->setLoDIndexOffset(i, geometryBuffer()->getPartitionOffset(id), geometryBuffer()->getPartitionIndexCount(id));
            prevID = id;
        }
    }

    SceneNode::buildDrawCommands(sgn, cmdsOut);
}

// Create a list of triangles from the vertices + indices lists based on primitive type
bool Object3D::computeTriangleList(const U16 partitionID, const bool force) {
    if ( _geometryBuffer == nullptr )
    {
        return true;
    }

    if ( _geometryBuffer->getIndexCount() == 0)
    {
        return false;
    }

    // We can't have a VB without vertex positions
    DIVIDE_ASSERT(!_geometryBuffer->getVertices().empty(), "Object3D error: computeTriangleList called with no position data available!");

    if(partitionID >= _geometryTriangles.size())
    {
        _geometryTriangles.resize(partitionID + 1);
    }

    vector<uint3>& triangles = _geometryTriangles[partitionID];
    if (!force && !triangles.empty())
    {
        return true;
    }

    efficient_clear( triangles );

    const size_t partitionOffset = _geometryBuffer->getPartitionOffset(_geometryPartitionIDs[0]);
    const size_t partitionCount = _geometryBuffer->getPartitionIndexCount(_geometryPartitionIDs[0]);
    const PrimitiveTopology topology = GetGeometryBufferType(type());

    size_t indiceCount = partitionCount;
    if ( topology == PrimitiveTopology::TRIANGLE_STRIP)
    {
        const size_t indiceStart = 2 + partitionOffset;
        const size_t indiceEnd = indiceCount + partitionOffset;
        uint3 curTriangle;
        triangles.reserve(indiceCount / 2);
        const vector<U32>& indices = _geometryBuffer->getIndices();
        for (size_t i = indiceStart; i < indiceEnd; i++)
        {
            curTriangle.set(indices[i - 2], indices[i - 1], indices[i]);
            // Check for correct winding
            if (i % 2 != 0)
            {
                std::swap(curTriangle.y, curTriangle.z);
            }
            triangles.push_back(curTriangle);
        }
    }
    else if ( topology == PrimitiveTopology::TRIANGLES)
    {
        indiceCount /= 3;
        triangles.reserve(indiceCount);
        const vector<U32>& indices = _geometryBuffer->getIndices();
        for (size_t i = 0; i < indiceCount; i += 3)
        {
            triangles.push_back(uint3(indices[i + 0],
                                          indices[i + 1],
                                          indices[i + 2]));
        }
    }

    // Check for degenerate triangles
    triangles.erase(
        eastl::partition(
            begin(triangles), end(triangles),
            [](const uint3& triangle) -> bool
            {
                return triangle.x != triangle.y && triangle.x != triangle.z &&
                    triangle.y != triangle.z;
            }),
        end(triangles));

    DIVIDE_ASSERT(!triangles.empty(), "Object3D error: computeTriangleList() failed to generate any triangles!");
    return true;
}

bool Object3D::saveCache(ByteBuffer& outputBuffer) const
{
    if (SceneNode::saveCache(outputBuffer))
    {
        if (IsPrimitive(type()))
        {
            outputBuffer << to_U8(type());
        }
        else
        {
            outputBuffer << string(resourceName().c_str());
        }
        return true;
    }

    return false;
};

bool Object3D::loadCache(ByteBuffer& inputBuffer)
{
    if (SceneNode::loadCache(inputBuffer))
    {
        if (IsPrimitive(type()))
        {
            U8 index = 0u;
            inputBuffer >> index;
            if (index != to_U8(type()))
            {
                return false;
            }
        }
        else
        {
            string tempName = {};
            inputBuffer >> tempName;
            assert(tempName == resourceName().c_str());
        }

        return true;
    }

    return false;
}

void Object3D::saveToXML(boost::property_tree::ptree& pt) const
{
    if (IsPrimitive(type()))
    {
        pt.put("model", Names::sceneNodeType[to_base(type())]);
    }
    else
    {
        pt.put("model", resourceName().c_str());
    }

    SceneNode::saveToXML(pt);
}

void Object3D::loadFromXML(const boost::property_tree::ptree& pt)
{
    string temp;
    if (IsPrimitive(type()))
    {
        temp = pt.get("model", Names::sceneNodeType[to_base( type() )]);
        assert(temp == Names::sceneNodeType[to_base( type() )]);
    }
    else
    {
        temp = pt.get("model", resourceName().c_str());
        assert(temp == resourceName().c_str());
    }

    SceneNode::loadFromXML(pt);
}

};
