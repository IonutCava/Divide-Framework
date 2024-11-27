

#include "Headers/BoundsComponent.h"
#include "Headers/RenderingComponent.h"
#include "Headers/TransformComponent.h"

#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {

BoundsComponent::BoundsComponent(SceneGraphNode* sgn, PlatformContext& context)
    : BaseComponentType<BoundsComponent, ComponentType::BOUNDS>(sgn, context)
{
    _refBoundingBox.set(sgn->getNode().getBounds());
    _worldOffset.set(sgn->getNode().getWorldOffset());

    _boundingBox.set(_refBoundingBox);
    _boundingSphere.fromBoundingBox(_boundingBox);

    _transformUpdatedMask.store(to_base(TransformType::ALL));

    EditorComponentField bbField = {};
    bbField._name = "Bounding Box";
    bbField._data = &_boundingBox;
    bbField._type = EditorComponentFieldType::BOUNDING_BOX;
    bbField._readOnly = true;
    bbField._serialise = false;
    _editorComponent.registerField(MOV(bbField));

    EditorComponentField rbbField = {};
    rbbField._name = "Ref Bounding Box";
    rbbField._data = &_refBoundingBox;
    rbbField._type = EditorComponentFieldType::BOUNDING_BOX;
    rbbField._readOnly = true;
    rbbField._serialise = false;
    _editorComponent.registerField(MOV(rbbField));

    EditorComponentField bsField = {};
    bsField._name = "Bounding Sphere";
    bsField._data = &_boundingSphere;
    bsField._type = EditorComponentFieldType::BOUNDING_SPHERE;
    bsField._readOnly = true;
    bsField._serialise = false;
    _editorComponent.registerField(MOV(bsField));

    EditorComponentField obsField = {};
    obsField._name = "Oriented Bouding Box";
    obsField._data = &_obb;
    obsField._type = EditorComponentFieldType::ORIENTED_BOUNDING_BOX;
    obsField._readOnly = true;
    obsField._serialise = false;
    _editorComponent.registerField(MOV(obsField));

    EditorComponentField vbbField = {};
    vbbField._name = "Show AABB";
    vbbField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) { *static_cast<bool*>(dataOut) = _showAABB; };
    vbbField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { showAABB(*static_cast<const bool*>(data)); };
    vbbField._type = EditorComponentFieldType::SWITCH_TYPE;
    vbbField._basicType = PushConstantType::BOOL;
    vbbField._readOnly = false;

    _editorComponent.registerField(MOV(vbbField));

    EditorComponentField vobbField = {};
    vobbField._name = "Show OBB";
    vobbField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) { *static_cast<bool*>(dataOut) = _showOBB; };
    vobbField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { showOBB(*static_cast<const bool*>(data)); };
    vobbField._type = EditorComponentFieldType::SWITCH_TYPE;
    vobbField._basicType = PushConstantType::BOOL;
    vobbField._readOnly = false;

    _editorComponent.registerField(MOV(vobbField));

    EditorComponentField vbsField = {};
    vbsField._name = "Show Bounding Sphere";
    vbsField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) { *static_cast<bool*>(dataOut) = _showBS; };
    vbsField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { showBS(*static_cast<const bool*>(data)); };
    vbsField._type = EditorComponentFieldType::SWITCH_TYPE;
    vbsField._basicType = PushConstantType::BOOL;
    vbsField._readOnly = false;
    _editorComponent.registerField(MOV(vbsField));

    EditorComponentField recomputeBoundsField = {};
    recomputeBoundsField._name = "Recompute Bounds";
    recomputeBoundsField._range = { to_F32(recomputeBoundsField._name.length()) * 10, 20.0f };//dimensions
    recomputeBoundsField._type = EditorComponentFieldType::BUTTON;
    recomputeBoundsField._readOnly = false; //disabled/enabled
    _editorComponent.registerField(MOV(recomputeBoundsField));

    _editorComponent.onChangedCbk([this](const std::string_view field) {
        if (field == "Recompute Bounds")
        {
            flagBoundingBoxDirty(to_base(TransformType::ALL), true);
        }
    });
}

void BoundsComponent::showAABB(const bool state) {
    if (_showAABB != state) {
        _showAABB = state;

        _parentSGN->SendEvent(
            {
                ._type = ECS::CustomEvent::Type::DrawBoundsChanged,
                ._sourceCmp = this
            });
    }
}

void BoundsComponent::showOBB(const bool state) {
    if (_showOBB != state) {
        _showOBB = state;

        _parentSGN->SendEvent(
            {
                ._type = ECS::CustomEvent::Type::DrawBoundsChanged,
                ._sourceCmp = this
            });
    }
}

void BoundsComponent::showBS(const bool state) {
    if (_showBS != state) {
        _showBS = state;

        _parentSGN->SendEvent(
            {
                ._type = ECS::CustomEvent::Type::DrawBoundsChanged,
                ._sourceCmp = this
            });
    }
}

void BoundsComponent::flagBoundingBoxDirty(const U32 transformMask, const bool recursive) {
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (_transformUpdatedMask.exchange(transformMask) != 0u) {
        // already dirty
        return;
    }

    if (recursive) {
        const SceneGraphNode* parent = _parentSGN->parent();
        if (parent != nullptr) {
            // We stop if the parent sgn doesn't have a bounds component.
            if (parent->HasComponents(ComponentType::BOUNDS)) {
                parent->get<BoundsComponent>()->flagBoundingBoxDirty(transformMask, true);
            }
        }
    }
}

void BoundsComponent::OnData(const ECS::CustomEvent& data) {
    SGNComponent::OnData(data);

    if (data._type == ECS::CustomEvent::Type::TransformUpdated) {
        flagBoundingBoxDirty(data._flag, true);
    }
}

void BoundsComponent::setRefBoundingBox(const BoundingBox& nodeBounds) noexcept {
    // All the parents should already be dirty thanks to the bounds system
    _refBoundingBox.set(nodeBounds);
    _transformUpdatedMask.store(to_base(TransformType::ALL));
}

void BoundsComponent::updateBoundingBoxTransform() {
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (_transformUpdatedMask == 0u) {
        return;
    }

    const mat4<F32> mat = _parentSGN->get<TransformComponent>()->getWorldMatrix();
    _boundingBox.transform(_refBoundingBox, mat);
}

void BoundsComponent::appendChildRefBBs() {
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();
    SharedLock<SharedMutex> r_lock(children._lock);
    const U32 childCount = children._count;
    BoundingBox temp{};
    for (U32 i = 0u; i < childCount; ++i)
    {
        if (children._data[i]->HasComponents(ComponentType::BOUNDS))
        {
            BoundsComponent* const bComp = children._data[i]->get<BoundsComponent>();
            bComp->appendChildRefBBs();
            temp = bComp->_refBoundingBox;
            temp.translate(bComp->_worldOffset);
            _refBoundingBox.add(temp);
        }
    }
}

void BoundsComponent::appendChildBBs()
{
    PROFILE_SCOPE_AUTO( Profiler::Category::Scene );

    if (_transformUpdatedMask.exchange(0u) == 0u)
    {
        return;
    }

    const SceneGraphNode::ChildContainer& children = _parentSGN->getChildren();

    SharedLock<SharedMutex> r_lock(children._lock);
    const U32 childCount = children._count;
    for (U32 i = 0u; i < childCount; ++i)
    {
        if (children._data[i]->HasComponents(ComponentType::BOUNDS))
        {
            BoundsComponent* const bComp = children._data[i]->get<BoundsComponent>();
            // This will also clear our transform flag so subsequent calls will be fast
            bComp->appendChildBBs();
            _boundingBox.add(bComp->_boundingBox);
        }
    }
    _boundingSphere.fromBoundingBox(_boundingBox);
    _obbDirty.store(true);
}

const OBB& BoundsComponent::getOBB()
{
    if (_obbDirty.exchange(false))
    {
        const TransformComponent* transform = _parentSGN->get<TransformComponent>();
        _obb.fromBoundingBox(_refBoundingBox, transform->getWorldMatrix());
        //_obb.fromBoundingBox(_refBoundingBox, transform->getWorldPosition(), transform->getWorldOrientation(), transform->getWorldScale());
        _boundingSphere = _obb.toEnclosingSphere();
    }

    return _obb;
}

bool Collision( const BoundsComponent& lhs, const BoundsComponent& rhs ) noexcept
{
    return  lhs.parentSGN()->getGUID() != rhs.parentSGN()->getGUID() &&
            lhs.getBoundingSphere().collision( rhs.getBoundingSphere() ) &&
            lhs.getBoundingBox().collision( rhs.getBoundingBox() );
}

}
