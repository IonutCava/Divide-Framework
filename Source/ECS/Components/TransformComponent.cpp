

#include "Headers/TransformComponent.h"

#include "Core/Headers/ByteBuffer.h"
#include "Graphs/Headers/SceneGraphNode.h"

namespace Divide
{
    TransformComponent::TransformComponent(SceneGraphNode* parentSGN, PlatformContext& context)
      : BaseComponentType<TransformComponent, ComponentType::TRANSFORM>(parentSGN, context)
      , _parentUsageContext(parentSGN->usageContext())
    {

        setTransformDirty(TransformType::ALL);

        EditorComponentField transformField = {};
        transformField._name = "Transform";
        transformField._data = this;
        transformField._type = EditorComponentFieldType::TRANSFORM;
        transformField._readOnly = false;

        _editorComponent.registerField(MOV(transformField));

        EditorComponentField worldMatField = {};
        worldMatField._name = "World Matrix";
        worldMatField._dataGetter = [this](void* dataOut) { getWorldMatrix(*static_cast<mat4<F32>*>(dataOut)); };
        worldMatField._type = EditorComponentFieldType::PUSH_TYPE;
        worldMatField._readOnly = true;
        worldMatField._serialise = false;
        worldMatField._basicType = PushConstantType::MAT4;

        _editorComponent.registerField(MOV(worldMatField));

        EditorComponentField localMatField = {};
        localMatField._name = "Local Matrix";
        localMatField._data = _localMatrix;
        localMatField._type = EditorComponentFieldType::PUSH_TYPE;
        localMatField._readOnly = true;
        localMatField._serialise = false;
        localMatField._basicType = PushConstantType::MAT4;

        _editorComponent.registerField(MOV(localMatField));

        if (_transformOffset.first)
        {
            EditorComponentField transformOffsetField = {};
            transformOffsetField._name = "Transform Offset";
            transformOffsetField._data = _transformOffset.second;
            transformOffsetField._type = EditorComponentFieldType::PUSH_TYPE;
            transformOffsetField._readOnly = true;
            transformOffsetField._basicType = PushConstantType::MAT4;

            _editorComponent.registerField(MOV(transformOffsetField));
        }

        EditorComponentField recomputeMatrixField = {};
        recomputeMatrixField._name = "Recompute World Matrix";
        recomputeMatrixField._range = { recomputeMatrixField._name.length() * 10.0f, 20.0f };//dimensions
        recomputeMatrixField._type = EditorComponentFieldType::BUTTON;
        recomputeMatrixField._readOnly = false; //disabled/enabled
        _editorComponent.registerField(MOV(recomputeMatrixField));

        _editorComponent.onChangedCbk([this](const std::string_view field)
        {
            if (field == "Transform")
            {
                setTransformDirty(TransformType::ALL);
            }
            else if (field == "Position Offset")
            {
                // view offset stuff
            }
            else if (field == "Recompute World Matrix")
            {
                setTransformDirty(TransformType::ALL);
            }

            _hasChanged = true;
        });
    }

    void TransformComponent::onParentTransformDirty(const U32 transformMask) noexcept
    {
        if (transformMask != to_base(TransformType::NONE))
        {
            setTransformDirty(transformMask);
        }
    }

    void TransformComponent::onParentUsageChanged(const NodeUsageContext context) noexcept
    {
        _parentUsageContext = context;
    }

    void TransformComponent::resetCache() 
    {
        _cacheDirty = true;
        SharedLock<SharedMutex> r_lock(_lock);
        _prevTransformValues = getLocalValues();
        _transformValuesInterpolated = _prevTransformValues;
    }

    void TransformComponent::reset()
    {
        _localMatrix = MAT4_IDENTITY;
        _localMatrixInterpolated = MAT4_IDENTITY;
        _transformInterface.setTransforms( MAT4_IDENTITY );

        while (!_transformStack.empty())
        {
            _transformStack.pop();
        }

        setTransformDirty(TransformType::ALL);
        resetCache();
    }

    void TransformComponent::setOffset(const bool state, const mat4<F32>& offset) noexcept
    {
        _transformOffset.first = state;
        _transformOffset.second.set(offset);
        setTransformDirty(TransformType::ALL);
    }

    void TransformComponent::setTransformDirty(const TransformType type) noexcept
    {
        setTransformDirty(to_U32(type));
    }

    void TransformComponent::setTransformDirty(const U32 typeMask) noexcept
    {
        _transformUpdatedMask |= typeMask;
        _cacheDirty = (typeMask != to_base(TransformType::NONE)) || _cacheDirty;
    }

    void TransformComponent::setPosition(const vec3<F32>& position)
    {
        setPosition(position.x, position.y, position.z);
    }

    void TransformComponent::setPosition(const F32 x, const F32 y, const F32 z)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setPosition(x, y, z);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::setScale(const vec3<F32>& amount)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            if (scalingMode() != ScalingMode::UNIFORM) {
                _transformInterface.setScale(amount);
                _uniformScaled = amount.isUniform();
            } else {
                _transformInterface.setScale(vec3<F32>(amount.x, amount.x, amount.x));
                _uniformScaled = true;
            }
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setRotation(const Quaternion<F32>& quat)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(quat);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotation(const vec3<F32>& axis, const Angle::DEGREES<F32> degrees)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(axis, degrees);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotation(const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> roll)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(pitch, yaw, roll);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::translate(const vec3<F32>& axisFactors)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.translate(axisFactors);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::scale(const vec3<F32>& axisFactors)
    {
        LockGuard<SharedMutex> w_lock(_lock);
        if (scalingMode() != ScalingMode::UNIFORM) {
            _transformInterface.scale(axisFactors);
        } else {
            _transformInterface.scale(vec3<F32>(axisFactors.x, axisFactors.x, axisFactors.x));
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::rotate(const vec3<F32>& axis, const Angle::DEGREES<F32> degrees)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(axis, degrees);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotate(const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> roll)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(pitch, yaw, roll);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotate(const Quaternion<F32>& quat)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(quat);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateSlerp(const Quaternion<F32>& quat, const D64 deltaTime)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateSlerp(quat, deltaTime);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setScaleX(const F32 amount)
    {
        if (scalingMode() == ScalingMode::UNIFORM)
        {
            setScale(amount);
            return;
        }

        LockGuard<SharedMutex> w_lock(_lock);
        _transformInterface.setScaleX(amount);
        _uniformScaled = _transformInterface.isUniformScale();

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setScaleY(const F32 amount)
    {
        if (scalingMode() == ScalingMode::UNIFORM)
        {
            setScale(amount);
            return;
        }

        LockGuard<SharedMutex> w_lock(_lock);
        _transformInterface.setScaleY(amount);
        _uniformScaled = _transformInterface.isUniformScale();

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setScaleZ(const F32 amount)
    {
        if (scalingMode() == ScalingMode::UNIFORM)
        {
            setScale(amount);
            return;
        }

        LockGuard<SharedMutex> w_lock(_lock);
        _transformInterface.setScaleZ(amount);
        _uniformScaled = _transformInterface.isUniformScale();

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::scaleX(const F32 amount)
    {
        if (scalingMode() == ScalingMode::UNIFORM)
        {
            scale(amount);
            return;
        }

        LockGuard<SharedMutex> w_lock(_lock);
        _transformInterface.scaleX(amount);
        _uniformScaled = _transformInterface.isUniformScale();

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::scaleY(const F32 amount)
    {
        if (scalingMode() == ScalingMode::UNIFORM)
        {
            scale(amount);
            return;
        }

        LockGuard<SharedMutex> w_lock(_lock);
        _transformInterface.scaleY(amount);
        _uniformScaled = _transformInterface.isUniformScale();

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::scaleZ(const F32 amount)
    {
        if (scalingMode() == ScalingMode::UNIFORM)
        {
            scale(amount);
            return;
        }
 
        LockGuard<SharedMutex> w_lock(_lock);
        _transformInterface.scaleZ(amount);
        _uniformScaled = _transformInterface.isUniformScale();

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::rotateX(const Angle::DEGREES<F32> angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateX(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateY(const Angle::DEGREES<F32> angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateY(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateZ(const Angle::DEGREES<F32> angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateZ(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationX(const Angle::DEGREES<F32> angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationX(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationY(const Angle::DEGREES<F32> angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationY(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationZ(const Angle::DEGREES<F32> angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationZ(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    const vec3<F32> TransformComponent::getLocalDirection(const vec3<F32>& worldForward) const
    {
        return DirectionFromAxis(getLocalOrientation(), worldForward);
    } 
    
    const vec3<F32> TransformComponent::getWorldDirection(const vec3<F32>& worldForward) const
    {
        return DirectionFromAxis(getWorldOrientation(), worldForward);
    }

    void TransformComponent::setDirection(const vec3<F32>& fwdDirection, const vec3<F32>& upDirection)
    {
        setRotation(RotationFromVToU(WORLD_Z_NEG_AXIS, fwdDirection, upDirection));
    }

    void TransformComponent::setPositionX(const F32 positionX)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setPositionX(positionX);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::setPositionY(const F32 positionY)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setPositionY(positionY);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::setPositionZ(const F32 positionZ)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setPositionZ(positionZ);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::pushTransforms() 
    {
        SharedLock<SharedMutex> r_lock(_lock);
        _transformStack.push(getLocalValues());
    }

    bool TransformComponent::popTransforms()
    {
        if (!_transformStack.empty())
        {
            _prevTransformValues = _transformStack.top();
            _transformValuesInterpolated = _prevTransformValues;
            {
                LockGuard<SharedMutex> w_lock(_lock);
                _transformInterface.setValues(_prevTransformValues);
            }

            _transformStack.pop();

            setTransformDirty(TransformType::ALL);
            return true;
        }

        return false;
    }
    
    void TransformComponent::setTransform(const TransformValues& values)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setValues(values);
        }
        setTransformDirty(TransformType::ALL);
    }

    void TransformComponent::setTransforms(const mat4<F32>& transform)
    {
        {
            LockGuard<SharedMutex> r_lock(_lock);
            _transformInterface.setTransforms(transform);
        }
        setTransformDirty(TransformType::ALL);
    }

    TransformValues TransformComponent::getLocalValues() const
    {
        SharedLock<SharedMutex> r_lock(_lock);

        TransformValues ret{};
        ret._translation = _transformInterface._translation;
        ret._scale = _transformInterface._scale;
        ret._orientation = _transformInterface._orientation;
        return ret;
    }

    void TransformComponent::updateLocalMatrixLocked()
    {
        {
            SharedLock<SharedMutex> r_lock(_lock);
            _localMatrix = mat4<F32>
            {
                _transformInterface._translation,
                _transformInterface._scale,
                GetMatrix(_transformInterface._orientation)
            };
        }

        if (_transformOffset.first)
        {
            _localMatrix *= _transformOffset.second;
        }
    }

    void TransformComponent::updateLocalMatrixInterpolated( const D64 interpolationFactor )
    {
        if ( interpolationFactor == 1.0 )
        {
            _localMatrixInterpolated = _localMatrix;
            return;
        }

        {
            SharedLock<SharedMutex> r_lock(_lock);
            _transformValuesInterpolated = Lerp( _prevTransformValues,  _transformInterface, to_F32( interpolationFactor ) );
            _localMatrixInterpolated = GetMatrix( _transformValuesInterpolated );
        }

        if (_transformOffset.first)
        {
            _localMatrixInterpolated *= _transformOffset.second;
        }
    }

    void TransformComponent::getLocalRotationMatrix( mat4<F32>& matOut ) const
    {
        matOut = _localMatrix;
        
        if ( !isUniformScaled() )
        {
            // Non-uniform scaling requires an inverseTranspose to negate scaling contribution but preserve rotation
            matOut.inverseTranspose();
        }
    }

    void TransformComponent::getLocalRotationMatrixInterpolated( mat4<F32>& matOut ) const
    {
        matOut = _localMatrixInterpolated;

        if ( !isUniformScaled() )
        {
            // Non-uniform scaling requires an inverseTranspose to negate scaling contribution but preserve rotation
            matOut.inverseTranspose();
        }
        
    }

    void TransformComponent::getWorldRotationMatrix( mat4<F32>& matOut ) const
    {
        if ( !isUniformScaled() )
        {
            // Non-uniform scaling requires an inverseTranspose to negate scaling contribution but preserve rotation
            GetInverse( getWorldMatrix(), matOut);
            matOut.transpose();
        }
        else
        {
            matOut.set( getWorldMatrix() );
        }
    }

    void TransformComponent::getWorldRotationMatrixInterpolated( mat4<F32>& matOut ) const
    {
        if ( !isUniformScaled() )
        {
            // Non-uniform scaling requires an inverseTranspose to negate scaling contribution but preserve rotation
            GetInverse(getWorldMatrixInterpolated(), matOut);
            matOut.transpose();
        }
        else
        {
            matOut.set( getWorldMatrixInterpolated() );
        }
    }

    void TransformComponent::updateLocalMatrix( const D64 interpolationFactor )
    {
        LockGuard<SharedMutex> w_lock(_localMatrixLock);
        updateLocalMatrixLocked();
        updateLocalMatrixInterpolated( interpolationFactor );
    }

    void TransformComponent::onParentChanged(const SceneGraphNode* oldParent, const SceneGraphNode* newParent)
    {
        // This tries to keep the object's global transform intact when switching from one parent to another

        // Step 1: Embed parrent transform into ourselves
        if (oldParent != nullptr && oldParent->HasComponents(ComponentType::TRANSFORM))
        {
            const mat4<F32> parentTransform = oldParent->get<TransformComponent>()->getWorldMatrix();
            setTransforms(parentTransform * _localMatrix );
        }

        // Step 2: Undo the new parent transform internally so we don't change transforms when we request the world matrix
        // Ignore root node as that node's transform should be set to identity anyway
        if (newParent != nullptr && newParent->parent() != nullptr && newParent->HasComponents(ComponentType::TRANSFORM))
        {
            const mat4<F32> parentTransform = newParent->get<TransformComponent>()->getWorldMatrix();
            setTransforms(GetInverse(parentTransform) * _localMatrix );
        }
    }

    void TransformComponent::getWorldMatrix(mat4<F32>& matrixOut) const
    {
        {
            SharedLock<SharedMutex> r_lock(_localMatrixLock);
            matrixOut.set(_localMatrix);
        }

        if (_parentSGN->parent() != nullptr) {
            matrixOut *= _parentSGN->parent()->get<TransformComponent>()->getWorldMatrix();
        }
    }

    void TransformComponent::getWorldMatrixInterpolated( mat4<F32>& matrixOut) const 
    {
        matrixOut = _localMatrixInterpolated;

        if (_parentSGN->parent() != nullptr)
        {
            matrixOut *= _parentSGN->parent()->get<TransformComponent>()->getWorldMatrixInterpolated();
        }
    }

    mat4<F32> TransformComponent::getWorldMatrix() const
    {
        mat4<F32> ret;
        getWorldMatrix(ret);
        return ret;
    }

    mat4<F32> TransformComponent::getWorldMatrixInterpolated() const
    {
        mat4<F32> ret;
        getWorldMatrixInterpolated(ret);
        return ret;
    }

    void TransformComponent::getWorldTransforms( vec3<F32>& positionOut, vec3<F32>& scaleOut, Quaternion<F32>& rotationOut )
    {
        positionOut = getWorldPosition();
        scaleOut = getWorldScale();
        rotationOut = getWorldOrientation();
    }

    void TransformComponent::getWorldTransformsInterpolated( vec3<F32>& positionOut, vec3<F32>& scaleOut, Quaternion<F32>& rotationOut )
    {
        positionOut = _transformValuesInterpolated._translation;
        scaleOut = _transformValuesInterpolated._scale;
        rotationOut = _transformValuesInterpolated._orientation;
    }

    vec3<F32> TransformComponent::getWorldPosition() const 
    {
        if (!_cacheDirty)
        {
            return _cachedDerivedTransform._translation;
        }

        return getDerivedPosition();
    }

    vec3<F32> TransformComponent::getDerivedPosition() const
    {
        if (_parentSGN->parent() != nullptr)
        {
            return getLocalPosition() + _parentSGN->parent()->get<TransformComponent>()->getDerivedPosition();
        }

        return getLocalPosition();
    }

    vec3<F32> TransformComponent::getWorldPositionInterpolated() const
    {
        if (_parentSGN->parent() != nullptr)
        {
            return getLocalPositionInterpolated() + _parentSGN->parent()->get<TransformComponent>()->getWorldPositionInterpolated();
        }

        return getLocalPositionInterpolated();
    }

    vec3<F32> TransformComponent::getWorldScale() const
    {
        if (!_cacheDirty)
        {
            return _cachedDerivedTransform._scale;
        }

        return getDerivedScale();
    }

    vec3<F32> TransformComponent::getDerivedScale() const
    {
        if (_parentSGN->parent() != nullptr)
        {
            return getLocalScale() * _parentSGN->parent()->get<TransformComponent>()->getDerivedScale();
        }

        return getLocalScale();
    }

    vec3<F32> TransformComponent::getWorldScaleInterpolated() const
    {
        if (_parentSGN->parent() != nullptr)
        {
            return getLocalScaleInterpolated() * _parentSGN->parent()->get<TransformComponent>()->getWorldScaleInterpolated();
        }

        return getLocalScaleInterpolated();
    }

    Quaternion<F32> TransformComponent::getWorldOrientation() const
    {
        if (!_cacheDirty)
        {
            return _cachedDerivedTransform._orientation;
        }

        return getDerivedOrientation();
    }

    Quaternion<F32> TransformComponent::getDerivedOrientation() const
    {
        if (_parentSGN->parent() != nullptr)
        {
            return _parentSGN->parent()->get<TransformComponent>()->getDerivedOrientation() * getLocalOrientation();
        }

        return getLocalOrientation();
    }

    Quaternion<F32> TransformComponent::getWorldOrientationInterpolated() const
    {
        if (_parentSGN->parent() != nullptr)
        {
            return _parentSGN->parent()->get<TransformComponent>()->getWorldOrientationInterpolated() * getLocalOrientationInterpolated();
        }

        return getLocalOrientationInterpolated();
    }

    vec3<F32> TransformComponent::getLocalPosition() const
    {
        SharedLock<SharedMutex> r_lock(_lock);
        return _transformInterface._translation;
    }

    vec3<F32> TransformComponent::getLocalScale() const
    {
        SharedLock<SharedMutex> r_lock(_lock);
        return _transformInterface._scale;
    }

    Quaternion<F32> TransformComponent::getLocalOrientation() const
    {
        SharedLock<SharedMutex> r_lock(_lock);
        return _transformInterface._orientation;
    }

    vec3<F32> TransformComponent::getLocalPositionInterpolated() const
    {
        return _transformValuesInterpolated._translation;
    }

    vec3<F32> TransformComponent::getLocalScaleInterpolated() const
    {
        return _transformValuesInterpolated._scale;
    }

    vec3<F32> TransformComponent::getFwdVector() const
    {
        return Rotate(WORLD_Z_NEG_AXIS, getWorldOrientation());
    }

    vec3<F32> TransformComponent::getUpVector() const
    {
        return Rotate(WORLD_Y_AXIS, getWorldOrientation());
    }

    vec3<F32> TransformComponent::getRightVector() const
    {
        return Rotate(WORLD_X_AXIS, getWorldOrientation());
    }

    Quaternion<F32> TransformComponent::getLocalOrientationInterpolated() const
    {
        return _transformValuesInterpolated._orientation;
    }

    // Transform interface access
    void TransformComponent::getScale(vec3<F32>& scaleOut) const
    {
        SharedLock<SharedMutex> r_lock(_lock);
        _transformInterface.getScale(scaleOut);
    }

    void TransformComponent::getPosition(vec3<F32>& posOut) const
    {
        SharedLock<SharedMutex> r_lock(_lock);
        _transformInterface.getPosition(posOut);
    }

    void TransformComponent::getOrientation(Quaternion<F32>& quatOut) const
    {
        SharedLock<SharedMutex> r_lock(_lock);
        _transformInterface.getOrientation(quatOut);
    }

    bool TransformComponent::isUniformScaled() const noexcept
    {
        return _uniformScaled;
    }

    void TransformComponent::updateCachedValues()
    {
        if (_cacheDirty)
        {
            _cachedDerivedTransform._translation = getDerivedPosition();
            _cachedDerivedTransform._scale = getDerivedScale();
            _cachedDerivedTransform._orientation = getDerivedOrientation();
            _cacheDirty = false;
        }
    }

    bool TransformComponent::saveCache(ByteBuffer& outputBuffer) const
    {
        if (Parent::saveCache(outputBuffer))
        {
            if (_hasChanged.exchange(false))
            {
                SharedLock<SharedMutex> r_lock(_lock);

                outputBuffer << true;
                outputBuffer << _transformInterface._translation;
                outputBuffer << _transformInterface._scale;
                outputBuffer << _transformInterface._orientation;
            }
            else
            {
                outputBuffer << false;
            }

            return true;
        }

        return false;
    }

    bool TransformComponent::loadCache(ByteBuffer& inputBuffer)
    {
        if (Parent::loadCache(inputBuffer))
        {
            bool isSet = false;
            inputBuffer.read<bool>(isSet);
            if (isSet)
            {
                LockGuard<SharedMutex> w_lock(_lock);
                inputBuffer >> _transformInterface._translation;
                inputBuffer >> _transformInterface._scale;
                inputBuffer >> _transformInterface._orientation;
                setTransformDirty(TransformType::ALL);
            }

            return true;
        }

        return false;
    }
} //namespace Divide
