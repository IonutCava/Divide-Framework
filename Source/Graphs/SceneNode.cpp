

#include "Headers/SceneNode.h"

#include "Core/Headers/ByteBuffer.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Core/Headers/PlatformContext.h"
#include "Geometry/Material/Headers/Material.h"
#include "Geometry/Shapes/Headers/Object3D.h"
#include "Managers/Headers/ProjectManager.h"

#include "ECS/Components/Headers/BoundsComponent.h"

namespace Divide
{

constexpr U16 BYTE_BUFFER_VERSION = 1u;

namespace Names
{
    const char* sceneNodeType[to_base(SceneNodeType::COUNT) + 1u] = 
    {
        "SPHERE_3D", "BOX_3D", "QUAD_3D", "MESH", "SUBMESH", "TERRAIN", "TRANSFORM",
        "WATER", "PARTICLE_EMITTER", "SKY", "INFINITE_PLANE", "VEGETATION_GRASS", "UNKNOWN"
    };
}
SceneNode::SceneNode( const ResourceDescriptorBase& descriptor, const SceneNodeType type, const U32 requiredComponentMask)
    : CachedResource( descriptor, Names::sceneNodeType[to_base( type )] )
    , _type(type)
{
    _requiredComponentMask |= requiredComponentMask;
}

SceneNode::~SceneNode()
{
    DIVIDE_ASSERT(_materialTemplate == INVALID_HANDLE<Material>);
}

void SceneNode::registerEditorComponent( PlatformContext& context )
{
    _editorComponent = std::make_unique<EditorComponent>( context, ComponentType::COUNT, typeName() );
}

void SceneNode::sceneUpdate([[maybe_unused]] const U64 deltaTimeUS,
                            [[maybe_unused]] SceneGraphNode* sgn,
                            [[maybe_unused]] SceneState& sceneState)
{
}

void SceneNode::prepareRender([[maybe_unused]] SceneGraphNode* sgn,
                              [[maybe_unused]] RenderingComponent& rComp,
                              [[maybe_unused]] RenderPackage& pkg,
                              [[maybe_unused]] GFX::MemoryBarrierCommand& postDrawMemCmd,
                              [[maybe_unused]] const RenderStagePass renderStagePass,
                              [[maybe_unused]] const CameraSnapshot& cameraSnapshot,
                              [[maybe_unused]] bool refreshData)
{
    assert(getState() == ResourceState::RES_LOADED);
}

void SceneNode::postLoad(SceneGraphNode* sgn)
{
    sgn->postLoad();
}

void SceneNode::setBounds(const BoundingBox& aabb)
{
    _boundsChanged = true;
    _boundingBox.set(aabb);
}

Handle<Material> SceneNode::getMaterialTemplate() const
{
    return _materialTemplate;
}

PrimitiveTopology SceneNode::GetGeometryTopology() const noexcept
{
    if (!Is3DObject(_type)) [[unlikely]]
    {
        return PrimitiveTopology::COUNT;
    }
    if (_type == SceneNodeType::TYPE_BOX_3D ||
        _type == SceneNodeType::TYPE_MESH ||
        _type == SceneNodeType::TYPE_SUBMESH)
    {
        return PrimitiveTopology::TRIANGLES;
    }

    return PrimitiveTopology::TRIANGLE_STRIP;
}

void SceneNode::setMaterialTemplate(const Handle<Material> material, const AttributeMap& geometryAttributes )
{
    const bool materialChanged = _materialTemplate != material;
    const bool geometryChanged = _geometryAttributes != geometryAttributes;

    if ( !materialChanged && !geometryChanged)
    {
        return;
    }

    if ( materialChanged && _materialTemplate != INVALID_HANDLE<Material> )
    {
        if ( material != INVALID_HANDLE<Material> )
        {
            Console::printfn( LOCALE_STR( "REPLACE_MATERIAL" ), to_U32(_materialTemplate._index), to_U32(material._index));
        }

        DestroyResource( _materialTemplate );
    }

    _materialTemplate = material;
    _geometryAttributes = geometryAttributes;

    if (_materialTemplate != INVALID_HANDLE<Material>)
    {
        Material* mat = Get<Material>(_materialTemplate);
        PrimitiveTopology topology = mat->topology();
        if ( topology == PrimitiveTopology::COUNT )
        {
            topology = GetGeometryTopology();
        }

        DIVIDE_ASSERT(topology != PrimitiveTopology::COUNT);
        Get<Material>(_materialTemplate)->setPipelineLayout( topology, _geometryAttributes);
    }
}

bool SceneNode::load( PlatformContext& context )
{
    return CachedResource::load( context );
}

bool SceneNode::postLoad() 
{
    return CachedResource::postLoad();
}

bool SceneNode::unload()
{
    setMaterialTemplate( INVALID_HANDLE<Material> );
    _editorComponent.reset();
    return CachedResource::unload();
}

void SceneNode::buildDrawCommands([[maybe_unused]] SceneGraphNode* sgn,
                                  [[maybe_unused]] GenericDrawCommandContainer& cmdsOut)
{
}

void SceneNode::onNetworkSend([[maybe_unused]] SceneGraphNode* sgn, [[maybe_unused]] Networking::NetworkPacket& dataOut) const
{
}

void SceneNode::onNetworkReceive([[maybe_unused]] SceneGraphNode* sgn, [[maybe_unused]] Networking::NetworkPacket& dataIn) const
{
}

bool SceneNode::saveCache(ByteBuffer& outputBuffer) const
{
    outputBuffer << BYTE_BUFFER_VERSION;
    if ( _editorComponent != nullptr )
    {
        return Attorney::EditorComponentSceneGraphNode::saveCache( *_editorComponent, outputBuffer );
    }

    return true;
}

bool SceneNode::loadCache(ByteBuffer& inputBuffer)
{
    auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
    inputBuffer >> tempVer;
    if ( tempVer == BYTE_BUFFER_VERSION )
    {
        if ( _editorComponent != nullptr )
        {
            return Attorney::EditorComponentSceneGraphNode::loadCache( *_editorComponent, inputBuffer );
        }

        return true;
    }

    return false;
}

void SceneNode::saveToXML(boost::property_tree::ptree& pt) const
{
    if ( _editorComponent != nullptr )
    {
        Attorney::EditorComponentSceneGraphNode::saveToXML( *_editorComponent, pt );
    }
}

void SceneNode::loadFromXML(const boost::property_tree::ptree& pt)
{
    if ( _editorComponent != nullptr )
    {
        Attorney::EditorComponentSceneGraphNode::loadFromXML( *_editorComponent, pt );
    }
}

TransformNode::TransformNode( const ResourceDescriptor<TransformNode>& descriptor )
    : SceneNode( descriptor, 
                 GetSceneNodeType<TransformNode>(),
                 to_base( ComponentType::TRANSFORM ) | to_base( ComponentType::BOUNDS ) )
{
}

} //namespace Divide
