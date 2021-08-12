#include "stdafx.h"

#include "Headers/TransformComponent.h"

#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide {
    TransformComponent::TransformComponent(SceneGraphNode* parentSGN, PlatformContext& context)
      : BaseComponentType<TransformComponent, ComponentType::TRANSFORM>(parentSGN, context),
        _parentUsageContext(parentSGN->usageContext())
    {
        _worldMatrix.fill(MAT4_IDENTITY);

        _transformUpdatedMask.store(to_base(TransformType::ALL));

        EditorComponentField transformField = {};
        transformField._name = "Transform";
        transformField._data = this;
        transformField._type = EditorComponentFieldType::TRANSFORM;
        transformField._readOnly = false;

        _editorComponent.registerField(MOV(transformField));

        EditorComponentField worldMatField = {};
        worldMatField._name = "WorldMat";
        worldMatField._dataGetter = [this](void* dataOut) { getWorldMatrix(*static_cast<mat4<F32>*>(dataOut)); };
        worldMatField._type = EditorComponentFieldType::PUSH_TYPE;
        worldMatField._readOnly = true;
        worldMatField._serialise = false;
        worldMatField._basicType = GFX::PushConstantType::MAT4;

        _editorComponent.registerField(MOV(worldMatField));

        if (_transformOffset.first) {
            EditorComponentField transformOffsetField = {};
            transformOffsetField._name = "Transform Offset";
            transformOffsetField._data = _transformOffset.second;
            transformOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
            transformOffsetField._readOnly = true;
            transformOffsetField._basicType = GFX::PushConstantType::MAT4;

            _editorComponent.registerField(MOV(transformOffsetField));
        }

        EditorComponentField recomputeMatrixField = {};
        recomputeMatrixField._name = "Recompute WorldMatrix";
        recomputeMatrixField._range = { recomputeMatrixField._name.length() * 10.0f, 20.0f };//dimensions
        recomputeMatrixField._type = EditorComponentFieldType::BUTTON;
        recomputeMatrixField._readOnly = false; //disabled/enabled
        _editorComponent.registerField(MOV(recomputeMatrixField));

        _editorComponent.onChangedCbk([this](const std::string_view field) {
            if (field == "Transform") {
                setTransformDirty(to_base(TransformType::ALL));
            } else if (field == "Position Offset") {
                // view offset stuff
            } else if (field == "Recompute WorldMatrix") {
                _transformUpdatedMask.store(to_base(TransformType::ALL));
            }

            _hasChanged = true;
        });
    }

    void TransformComponent::onParentTransformDirty(const U32 transformMask) noexcept {
        if (transformMask != to_base(TransformType::NONE)) {
            setTransformDirty(transformMask);
        }
    }

    void TransformComponent::onParentUsageChanged(const NodeUsageContext context) noexcept {
        _parentUsageContext = context;
    }

    void TransformComponent::resetInterpolation() {
        SharedLock<SharedMutex> r_lock(_lock);
        _prevTransformValues = _transformInterface.getValues();
    }

    void TransformComponent::reset() {
        _worldMatrix.fill(MAT4_IDENTITY);

        while (!_transformStack.empty()) {
            _transformStack.pop();
        }
        _transformUpdatedMask.store(to_base(TransformType::ALL));
        resetInterpolation();
    }

    void TransformComponent::setOffset(const bool state, const mat4<F32>& offset) {
        _transformOffset.first = state;
        _transformOffset.second.set(offset);
        setTransformDirty(TransformType::ALL);
    }

    void TransformComponent::setTransformDirty(const TransformType type) noexcept {
        setTransformDirty(to_U32(type));
    }

    void TransformComponent::setTransformDirty(const U32 typeMask) noexcept {
        SetBit(_transformUpdatedMask, typeMask);
    }

    void TransformComponent::setPosition(const vec3<F32>& position) {
        setPosition(position.x, position.y, position.z);
    }

    void TransformComponent::setPosition(const F32 x, const F32 y, const F32 z) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setPosition(x, y, z);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::setScale(const vec3<F32>& amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setScale(amount);
            _uniformScaled = amount.isUniform();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setRotation(const Quaternion<F32>& quat) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(quat);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotation(const vec3<F32>& axis, const Angle::DEGREES<F32> degrees) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(axis, degrees);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotation(const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> roll) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(pitch, yaw, roll);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::translate(const vec3<F32>& axisFactors) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.translate(axisFactors);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::scale(const vec3<F32>& axisFactors) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.scale(axisFactors);
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::rotate(const vec3<F32>& axis, const Angle::DEGREES<F32> degrees) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(axis, degrees);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotate(const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> roll) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(pitch, yaw, roll);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotate(const Quaternion<F32>& quat) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(quat);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateSlerp(const Quaternion<F32>& quat, const D64 deltaTime) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotateSlerp(quat, deltaTime);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setScaleX(const F32 amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setScaleX(amount);
            _uniformScaled = _transformInterface.isUniformScale();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setScaleY(const F32 amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setScaleY(amount);
            _uniformScaled = _transformInterface.isUniformScale();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setScaleZ(const F32 amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setScaleZ(amount);
            _uniformScaled = _transformInterface.isUniformScale();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::scaleX(const F32 amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.scaleX(amount);
            _uniformScaled = _transformInterface.isUniformScale();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::scaleY(const F32 amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.scaleY(amount);
            _uniformScaled = _transformInterface.isUniformScale();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::scaleZ(const F32 amount) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.scaleZ(amount);
            _uniformScaled = _transformInterface.isUniformScale();
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::rotateX(const Angle::DEGREES<F32> angle) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotateX(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateY(const Angle::DEGREES<F32> angle) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotateY(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateZ(const Angle::DEGREES<F32> angle) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.rotateZ(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationX(const Angle::DEGREES<F32> angle) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationX(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationY(const Angle::DEGREES<F32> angle) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationY(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationZ(const Angle::DEGREES<F32> angle) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationZ(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setPositionX(const F32 positionX) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setPositionX(positionX);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::setPositionY(const F32 positionY) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setPositionY(positionY);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::setPositionZ(const F32 positionZ) {
        {
            ScopedLock<SharedMutex> w_lock(_lock);
            _transformInterface.setPositionZ(positionZ);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::pushTransforms() {
        SharedLock<SharedMutex> r_lock(_lock);
        _transformStack.push(_transformInterface.getValues());
    }

    bool TransformComponent::popTransforms() {
        if (!_transformStack.empty()) {
            _prevTransformValues = _transformStack.top();
            {
                ScopedLock<SharedMutex> w_lock(_lock);
                _transformInterface.setValues(_prevTransformValues);
            }

            _transformStack.pop();

            setTransformDirty(TransformType::ALL);
            return true;
        }
        return false;
    }
    
    void TransformComponent::setTransform(const TransformValues& values) {
        {
            ScopedLock<SharedMutex> r_lock(_lock);
            _transformInterface.setValues(values);
        }
        setTransformDirty(TransformType::ALL);
    }

    void TransformComponent::setTransforms(const mat4<F32>& transform) {
        {
            ScopedLock<SharedMutex> r_lock(_lock);
            _transformInterface.setTransforms(transform);
        }
        setTransformDirty(TransformType::ALL);
    }

    TransformValues TransformComponent::getValues() const {
        SharedLock<SharedMutex> r_lock(_lock);
        return _transformInterface.getValues();
    }

    void TransformComponent::getMatrix(mat4<F32>& matOut) {
        SharedLock<SharedMutex> r_lock(_lock);
        _transformInterface.getMatrix(matOut);

        if (_transformOffset.first) {
            matOut *= _transformOffset.second;
        }
    }

    void TransformComponent::getMatrix(const D64 interpolationFactor, mat4<F32>& matOut) const {
        {
            SharedLock<SharedMutex> r_lock(_lock);
            matOut.set(mat4<F32>
                       {
                            getLocalPositionLocked(interpolationFactor),
                            getLocalScaleLocked(interpolationFactor),
                            GetMatrix(getLocalOrientationLocked(interpolationFactor))
                        });
        }
        if (_transformOffset.first) {
            matOut *= _transformOffset.second;
        }
    }

    vec3<F32> TransformComponent::getLocalPositionLocked(const D64 interpolationFactor) const {
        vec3<F32> pos;
        _transformInterface.getPosition(pos);
        return Lerp(_prevTransformValues._translation, pos, to_F32(interpolationFactor));
    }

    vec3<F32> TransformComponent::getLocalScaleLocked(const D64 interpolationFactor) const {
        vec3<F32> scale;
        _transformInterface.getScale(scale);
        return Lerp(_prevTransformValues._scale, scale, to_F32(interpolationFactor));
    }

    Quaternion<F32> TransformComponent::getLocalOrientationLocked(D64 interpolationFactor) const {
        Quaternion<F32> quat;
        _transformInterface.getOrientation(quat);
        return Slerp(_prevTransformValues._orientation, quat, to_F32(interpolationFactor));
    }

    void TransformComponent::updateWorldMatrix() {
        ScopedLock<SharedMutex> w_lock(_worldMatrixLock);
        getMatrix(_worldMatrix[to_base(WorldMatrixType::CURRENT)]);
        _prevWorldMatrixDirty = true;
    }

    void TransformComponent::getPreviousWorldMatrix(mat4<F32>& matOut) const {
        matOut.set(_worldMatrix[to_base(WorldMatrixType::PREVIOUS)]);

        const SceneGraphNode* grandParentPtr = _parentSGN->parent();
        if (grandParentPtr != nullptr) {
            mat4<F32> parentMat;
            grandParentPtr->get<TransformComponent>()->getPreviousWorldMatrix(parentMat);
            matOut *= parentMat;
        }
    }

    mat4<F32> TransformComponent::getPreviousWorldMatrix() const {
        mat4<F32> ret = _worldMatrix[to_base(WorldMatrixType::PREVIOUS)];

        const SceneGraphNode* grandParentPtr = _parentSGN->parent();
        if (grandParentPtr != nullptr) {
            ret *= grandParentPtr->get<TransformComponent>()->getPreviousWorldMatrix();
        }

        return ret;
    }

    void TransformComponent::getWorldMatrix(mat4<F32>& matOut) const {
        {
            SharedLock<SharedMutex> r_lock(_worldMatrixLock);
            matOut.set(_worldMatrix[to_base(WorldMatrixType::CURRENT)]);
        }

        const SceneGraphNode* grandParentPtr = _parentSGN->parent();
        if (grandParentPtr != nullptr) {
            mat4<F32> parentMat;
            grandParentPtr->get<TransformComponent>()->getWorldMatrix(parentMat);
            matOut *= parentMat;
        }
    }

    mat4<F32> TransformComponent::getWorldMatrix() const {
        mat4<F32> ret;
        {
            SharedLock<SharedMutex> r_lock(_worldMatrixLock);
            ret.set(_worldMatrix[to_base(WorldMatrixType::CURRENT)]);
        }

        const SceneGraphNode* grandParentPtr = _parentSGN->parent();
        if (grandParentPtr != nullptr) {
            ret *= grandParentPtr->get<TransformComponent>()->getWorldMatrix();
        }

        return ret;
    }

    void TransformComponent::getWorldMatrix(D64 interpolationFactor, mat4<F32>& matrixOut) const {
        OPTICK_EVENT();

        if (_parentUsageContext == NodeUsageContext::NODE_STATIC || interpolationFactor > 0.99) {
            getWorldMatrix(matrixOut);
        } else {
            getMatrix(interpolationFactor, matrixOut);

            SceneGraphNode* grandParentPtr = _parentSGN->parent();
            if (grandParentPtr != nullptr) {
                mat4<F32> parentMat;
                grandParentPtr->get<TransformComponent>()->getWorldMatrix(interpolationFactor, parentMat);
                matrixOut *= parentMat;
            }
        }
    }

    vec3<F32> TransformComponent::getPosition() const {
        const SceneGraphNode* grandParent = _parentSGN->parent();
        if (grandParent != nullptr) {
            return getLocalPosition() + grandParent->get<TransformComponent>()->getPosition();
        }

        return getLocalPosition();
    }

    vec3<F32> TransformComponent::getPosition(const D64 interpolationFactor) const {
        const SceneGraphNode* grandParent = _parentSGN->parent();
        if (grandParent != nullptr) {
            return getLocalPosition(interpolationFactor) + grandParent->get<TransformComponent>()->getPosition(interpolationFactor);
        }

        return getLocalPosition(interpolationFactor);
    }

    vec3<F32> TransformComponent::getScale() const {
        const SceneGraphNode* grandParent = _parentSGN->parent();
        if (grandParent != nullptr) {
            return getLocalScale() * grandParent->get<TransformComponent>()->getScale();
        }

        return getLocalScale();
    }

    vec3<F32> TransformComponent::getScale(const D64 interpolationFactor) const {
        const SceneGraphNode* grandParent = _parentSGN->parent();
        if (grandParent != nullptr) {
            return getLocalScale(interpolationFactor) * grandParent->get<TransformComponent>()->getScale(interpolationFactor);
        }

        return getLocalScale(interpolationFactor);
    }

    Quaternion<F32> TransformComponent::getOrientation() const {
        const SceneGraphNode* grandParent = _parentSGN->parent();
        if (grandParent != nullptr) {
            return grandParent->get<TransformComponent>()->getOrientation() * getLocalOrientation();
        }

        return getLocalOrientation();
    }

    Quaternion<F32> TransformComponent::getOrientation(const D64 interpolationFactor) const {
        const SceneGraphNode* grandParent = _parentSGN->parent();
        if (grandParent != nullptr) {
            return grandParent->get<TransformComponent>()->getOrientation(interpolationFactor) * getLocalOrientation(interpolationFactor);
        }

        return getLocalOrientation(interpolationFactor);
    }

    vec3<F32> TransformComponent::getLocalPosition() const {
        vec3<F32> pos;
        getPosition(pos);
        return pos;
    }

    vec3<F32> TransformComponent::getLocalScale() const {
        vec3<F32> scale;
        getScale(scale);
        return scale;
    }

    Quaternion<F32> TransformComponent::getLocalOrientation() const {
        Quaternion<F32> quat;
        getOrientation(quat);
        return quat;
    }

    vec3<F32> TransformComponent::getLocalPosition(const D64 interpolationFactor) const {
        return Lerp(_prevTransformValues._translation, getLocalPosition(), to_F32(interpolationFactor));
    }

    vec3<F32> TransformComponent::getLocalScale(const D64 interpolationFactor) const {
        return Lerp(_prevTransformValues._scale, getLocalScale(), to_F32(interpolationFactor));
    }

    vec3<F32> TransformComponent::getFwdVector() const {
        return Rotate(WORLD_Z_NEG_AXIS, getOrientation());
    }

    vec3<F32> TransformComponent::getUpVector() const {
        return Rotate(WORLD_Y_AXIS, getOrientation());
    }

    vec3<F32> TransformComponent::getRightVector() const {
        return Rotate(WORLD_X_AXIS, getOrientation());
    }

    Quaternion<F32> TransformComponent::getLocalOrientation(const D64 interpolationFactor) const {
        return Slerp(_prevTransformValues._orientation, getLocalOrientation(), to_F32(interpolationFactor));
    }

    // Transform interface access
    void TransformComponent::getScale(vec3<F32>& scaleOut) const {
        SharedLock<SharedMutex> r_lock(_lock);

        _transformInterface.getScale(scaleOut);
    }

    void TransformComponent::getPosition(vec3<F32>& posOut) const {
        SharedLock<SharedMutex> r_lock(_lock);

        _transformInterface.getPosition(posOut);
    }

    void TransformComponent::getOrientation(Quaternion<F32>& quatOut) const {
        SharedLock<SharedMutex> r_lock(_lock);

        _transformInterface.getOrientation(quatOut);
    }

    bool TransformComponent::isUniformScaled() const noexcept {
        return _uniformScaled;
    }

    bool TransformComponent::saveCache(ByteBuffer& outputBuffer) const {
        if (Parent::saveCache(outputBuffer)) {
            outputBuffer << _hasChanged.load();

            if (_hasChanged.exchange(false)) {
                SharedLock<SharedMutex> r_lock(_lock);
                const TransformValues values = _transformInterface.getValues();

                outputBuffer << values._translation;
                outputBuffer << values._scale;
                outputBuffer << values._orientation;
            }
            return true;
        }

        return false;
    }

    bool TransformComponent::loadCache(ByteBuffer& inputBuffer) {
        if (Parent::loadCache(inputBuffer)) {
            bool hasChanged = false;
            inputBuffer >> hasChanged;

            if (hasChanged) {
                TransformValues valuesIn = {};
                inputBuffer >> valuesIn._translation;
                inputBuffer >> valuesIn._scale;
                inputBuffer >> valuesIn._orientation;

                setTransform(valuesIn);
            }
            return true;
        }

        return false;
        
    }
} //namespace