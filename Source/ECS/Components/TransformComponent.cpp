

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
        worldMatField._data = _world._matrix;
        worldMatField._type = EditorComponentFieldType::PUSH_TYPE;
        worldMatField._readOnly = true;
        worldMatField._serialise = false;
        worldMatField._basicType = PushConstantType::MAT4;

        _editorComponent.registerField(MOV(worldMatField));

        EditorComponentField localMatField = {};
        localMatField._name = "Local Matrix";
        localMatField._data = _local._matrix;
        localMatField._type = EditorComponentFieldType::PUSH_TYPE;
        localMatField._readOnly = true;
        localMatField._serialise = false;
        localMatField._basicType = PushConstantType::MAT4;

        _editorComponent.registerField(MOV(localMatField));

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

    void TransformComponent::reset()
    {
        _local = _world = {};
        _transformInterface.setTransforms( MAT4_IDENTITY );

        while (!_transformStack.empty())
        {
            _transformStack.pop();
        }

        setTransformDirty(TransformType::ALL);
    }

    void TransformComponent::setTransformDirty(const TransformType type) noexcept
    {
        setTransformDirty(to_U32(type));
    }

    void TransformComponent::setTransformDirty(const U32 typeMask) noexcept
    {
        _transformUpdatedMask |= typeMask;
    }

    void TransformComponent::setPosition(const float3& position)
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

    void TransformComponent::setScale(const F32 amount)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setScale(amount);
            _uniformScaled = true;
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setScale(const float3& amount)
    {
        setScale(amount.x, amount.y, amount.z);
    }

    void TransformComponent::setScale(const F32 X, const F32 Y, const F32 Z)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            if (scalingMode() != ScalingMode::UNIFORM)
            {
                _transformInterface.setScale(X, Y, Z);
                _uniformScaled = IsUniform(X, Y, Z, EPSILON_F32);
            }
            else
            {
                _transformInterface.setScale(X);
                _uniformScaled = true;
            }
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::setRotation(const quatf& quat)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(quat);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotation(const float3& axis, const Angle::DEGREES_F degrees)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(axis, degrees);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotation(const Angle::DEGREES_F pitch, const Angle::DEGREES_F yaw, const Angle::DEGREES_F roll)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotation(pitch, yaw, roll);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::translate(const float3& axisFactors)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.translate(axisFactors);
        }

        setTransformDirty(TransformType::TRANSLATION);
    }

    void TransformComponent::scale(const float3& axisFactors)
    {
        LockGuard<SharedMutex> w_lock(_lock);
        if (scalingMode() != ScalingMode::UNIFORM)
        {
            _transformInterface.scale(axisFactors);
        }
        else
        {
            _transformInterface.scale(float3(axisFactors.x, axisFactors.x, axisFactors.x));
        }

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::rotate(const float3& axis, const Angle::DEGREES_F degrees)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(axis, degrees);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotate(const Angle::DEGREES_F pitch, const Angle::DEGREES_F yaw, const Angle::DEGREES_F roll)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(pitch, yaw, roll);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotate(const quatf& quat)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotate(quat);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateSlerp(const quatf& quat, const D64 deltaTime)
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
        _uniformScaled = IsUniform(_transformInterface._scale, EPSILON_F32);

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
        _uniformScaled = IsUniform(_transformInterface._scale, EPSILON_F32);

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
        _uniformScaled = IsUniform(_transformInterface._scale, EPSILON_F32);

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
        _uniformScaled = IsUniform(_transformInterface._scale, EPSILON_F32);

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
        _uniformScaled = IsUniform(_transformInterface._scale, EPSILON_F32);

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
        _uniformScaled = IsUniform(_transformInterface._scale, EPSILON_F32);

        setTransformDirty(TransformType::SCALE);
    }

    void TransformComponent::rotateX(const Angle::DEGREES_F angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateX(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateY(const Angle::DEGREES_F angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateY(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::rotateZ(const Angle::DEGREES_F angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.rotateZ(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationX(const Angle::DEGREES_F angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationX(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationY(const Angle::DEGREES_F angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationY(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    void TransformComponent::setRotationZ(const Angle::DEGREES_F angle)
    {
        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setRotationZ(angle);
        }

        setTransformDirty(TransformType::ROTATION);
    }

    const float3 TransformComponent::getLocalDirection(const float3& worldForward) const
    {
        return DirectionFromAxis(getLocalOrientation(), worldForward);
    } 
    
    const float3 TransformComponent::getWorldDirection(const float3& worldForward) const
    {
        return DirectionFromAxis(getWorldOrientation(), worldForward);
    }

    void TransformComponent::setDirection(const float3& fwdDirection, const float3& upDirection)
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
        if (_transformStack.empty())
        {
            return false;
        }

        {
            LockGuard<SharedMutex> w_lock(_lock);
            _transformInterface.setValues(_transformStack.top());
        }

        _transformStack.pop();

        setTransformDirty(TransformType::ALL);
        return true;
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
        return TransformValues
        {
            ._orientation = getLocalOrientation(),
            ._translation = getLocalPosition(),
            ._scale       = getLocalScale()
        };
    }

    void TransformComponent::onParentChanged(const SceneGraphNode* oldParent, const SceneGraphNode* newParent)
    {
        // This tries to keep the object's global transform intact when switching from one parent to another

        // Step 1: Embed parrent transform into ourselves
        if (oldParent != nullptr && oldParent->HasComponents(ComponentType::TRANSFORM))
        {
            const mat4<F32>& parentTransform = oldParent->get<TransformComponent>()->getWorldMatrix();
            setTransforms(parentTransform * _local._matrix );
        }

        // Step 2: Undo the new parent transform internally so we don't change transforms when we request the world matrix
        // Ignore root node as that node's transform should be set to identity anyway
        if (newParent != nullptr && newParent->parent() != nullptr && newParent->HasComponents(ComponentType::TRANSFORM))
        {
            const mat4<F32>& parentTransform = newParent->get<TransformComponent>()->getWorldMatrix();
            setTransforms(GetInverse(parentTransform) * _local._matrix );
        }
    }

    void TransformComponent::getWorldMatrix( mat4<F32>& matrixOut) const
    {
        matrixOut.set(getWorldMatrix());
    }

    const mat4<F32>& TransformComponent::getWorldMatrix() const
    {
        return _world._matrix;
    }

    const float3& TransformComponent::getWorldPosition() const
    {
        return _world._values._translation;
    }

    const float3& TransformComponent::getWorldScale() const
    {
        return _world._values._scale;
    }

    const quatf& TransformComponent::getWorldOrientation() const
    {
        return _world._values._orientation;
    }

    const float3& TransformComponent::getLocalPosition() const
    {
        return _local._values._translation;
    }

    const float3& TransformComponent::getLocalScale() const
    {
        return _local._values._scale;
    }

    const quatf& TransformComponent::getLocalOrientation() const
    {
       return _local._values._orientation;
    }

    float3 TransformComponent::getFwdVector() const
    {
        return Rotate(WORLD_Z_NEG_AXIS, getWorldOrientation());
    }

    float3 TransformComponent::getUpVector() const
    {
        return Rotate(WORLD_Y_AXIS, getWorldOrientation());
    }

    float3 TransformComponent::getRightVector() const
    {
        return Rotate(WORLD_X_AXIS, getWorldOrientation());
    }

    // Transform interface access
    void TransformComponent::getScale(float3& scaleOut) const
    {
        scaleOut = getLocalScale();
    }

    void TransformComponent::getPosition(float3& posOut) const
    {
        posOut = getLocalPosition();
    }

    void TransformComponent::getOrientation(quatf& quatOut) const
    {
        quatOut = getLocalOrientation();
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
