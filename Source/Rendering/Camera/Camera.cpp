

#include "Headers/Camera.h"

#include "Core/Headers/StringHelper.h"
#include "Utility/Headers/Localization.h"
#include "Scenes/Headers/SceneState.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "ECS/Components//Headers/TransformComponent.h"

namespace Divide
{

    namespace TypeUtil
    {
        const char* FStopsToString( const FStops stop ) noexcept
        {
            return Names::fStops[to_base( stop )];
        }

        FStops StringToFStops( const string& name )
        {
            for ( U8 i = 0; i < to_U8( FStops::COUNT ); ++i )
            {
                if ( strcmp( name.c_str(), Names::fStops[i] ) == 0 )
                {
                    return static_cast<FStops>(i);
                }
            }

            return FStops::COUNT;
        }

        const char* CameraModeToString( const Camera::Mode mode ) noexcept
        {
            return Names::cameraMode[to_base( mode )];
        }

        Camera::Mode StringToCameraMode( const string& name )
        {

            for ( U8 i = 0; i < to_U8( Camera::Mode::COUNT ); ++i )
            {
                if ( strcmp( name.c_str(), Names::cameraMode[i] ) == 0 )
                {
                    return static_cast<Camera::Mode>(i);
                }
            }

            return Camera::Mode::COUNT;
        }
    }

    struct CameraEntry
    {
        Camera_uptr _camera;
        std::atomic_size_t _useCount{0u};
    };

    namespace
    {
        using CameraPool = eastl::list<CameraEntry>;

        std::array<Camera*, to_base( Camera::UtilityCamera::COUNT )> _utilityCameras;
        U32 s_changeCameraId = 0u;
        CameraListenerMap s_changeCameraListeners;
        CameraPool s_cameraPool;
        SharedMutex s_cameraPoolLock;

        float3 ExtractCameraPos2( const mat4<F32>& a_modelView ) noexcept
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

            // Get the 3 basis vector planes at the camera origin and transform them into model space.
            //  
            // NOTE: Planes have to be transformed by the inverse transpose of a matrix
            //       Nice reference here: http://www.opengl.org/discussion_boards/showthread.php/159564-Clever-way-to-transform-plane-by-matrix
            //
            //       So for a transform to model space we need to do:
            //            inverse(transpose(inverse(MV)))
            //       This equals : transpose(MV) - see Lemma 5 in http://mathrefresher.blogspot.com.au/2007/06/transpose-of-matrix.html
            //
            // As each plane is simply (1,0,0,0), (0,1,0,0), (0,0,1,0) we can pull the data directly from the transpose matrix.
            //  
            const mat4<F32> modelViewT( a_modelView.getTranspose() );

            // Get plane normals 
            const float4& n1( modelViewT.getRow( 0 ) );
            const float4& n2( modelViewT.getRow( 1 ) );
            const float4& n3( modelViewT.getRow( 2 ) );

            // Get plane distances
            const F32 d1( n1.w );
            const F32 d2( n2.w );
            const F32 d3( n3.w );

            // Get the intersection of these 3 planes 
            // (using math from RealTime Collision Detection by Christer Ericson)
            const float3 n2n3 = Cross( n2.xyz, n3.xyz );
            const F32 denom = Dot( n1.xyz, n2n3 );
            const float3 top = n2n3 * d1 + Cross( n1.xyz, d3 * n2.xyz - d2 * n3.xyz );
            return top / -denom;
        }
    }

    void Camera::Update( const U64 deltaTimeUS )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        s_lastFrameTimeSec = Time::MicrosecondsToSeconds<F32>( deltaTimeUS );

        SharedLock<SharedMutex> r_lock( s_cameraPoolLock );
        for ( CameraEntry& cameEntry : s_cameraPool )
        {
            cameEntry._camera->update();
        }
    }

    Camera* Camera::GetUtilityCamera( const UtilityCamera type )
    {
        if ( type != UtilityCamera::COUNT )
        {
            return _utilityCameras[to_base( type )];
        }

        return nullptr;
    }

    void Camera::InitPool()
    {
        _utilityCameras[to_base( UtilityCamera::DEFAULT )] = CreateCamera( "DefaultCamera", Mode::FREE_FLY );
        _utilityCameras[to_base( UtilityCamera::_2D )] = CreateCamera( "2DRenderCamera", Mode::STATIC );
        _utilityCameras[to_base( UtilityCamera::_2D_FLIP_Y )] = CreateCamera( "2DRenderCameraFlipY", Mode::STATIC );
        _utilityCameras[to_base( UtilityCamera::CUBE )] = CreateCamera( "CubeCamera", Mode::STATIC );
        _utilityCameras[to_base( UtilityCamera::DUAL_PARABOLOID )] = CreateCamera( "DualParaboloidCamera", Mode::STATIC );
    }

    void Camera::DestroyPool()
    {
        Console::printfn( LOCALE_STR( "CAMERA_MANAGER_DELETE" ) );
        _utilityCameras.fill( nullptr );
        LockGuard<SharedMutex> w_lock( s_cameraPoolLock );
        s_cameraPool.clear();
    }

    Camera* Camera::CreateCamera( const Str<256>& cameraName, const Camera::Mode mode )
    {
        const U64 targetHash = _ID( cameraName.c_str() );

        CameraEntry* camera = FindCameraEntry( targetHash );
        if ( camera != nullptr )
        {
            camera->_useCount.fetch_add( 1u );
            return camera->_camera.get();
        }

        // Cache miss
        LockGuard<SharedMutex> w_lock( s_cameraPoolLock );
        // Search again in case another thread created it in the meantime
        camera = FindCameraEntryLocked( targetHash );
        if ( camera != nullptr )
        {
            camera->_useCount.fetch_add( 1u );
            return camera->_camera.get();
        }

        // No such luck. Create it ourselves.
        s_cameraPool.emplace_back( nullptr, 1u);
        s_cameraPool.back()._camera.reset( new Camera( cameraName, mode ));

        return s_cameraPool.back()._camera.get();
    }

    bool Camera::DestroyCamera( Camera*& camera )
    {
        if ( camera == nullptr )
        {
            return true; //??
        }

        const U64 targetHash = _ID( camera->resourceName().c_str() );
        camera = nullptr;

        LockGuard<SharedMutex> w_lock( s_cameraPoolLock );

        return erase_if( s_cameraPool,
                         [targetHash]( CameraEntry& camEntry )
                         {
                            if (_ID( camEntry._camera->resourceName().c_str() ) == targetHash)
                            {
                                camEntry._useCount.fetch_sub( 1u );
                                return camEntry._useCount == 0u;
                            }

                            return false;
                         }) > 0;
    }

    CameraEntry* Camera::FindCameraEntry( const U64 nameHash )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        SharedLock<SharedMutex> r_lock( s_cameraPoolLock );
        return FindCameraEntryLocked( nameHash );
    }

    CameraEntry* Camera::FindCameraEntryLocked( const U64 nameHash )
    {
        auto it = eastl::find_if( begin( s_cameraPool ),
                                  end( s_cameraPool ),
                                  [nameHash]( CameraEntry& camEntry )
                                  {
                                      return _ID( camEntry._camera->resourceName().c_str() ) == nameHash;
                                  } );
        if ( it != std::end( s_cameraPool ) )
        {
            return &(*it);
        }

        return nullptr;
    }

    Camera* Camera::FindCamera( U64 nameHash )
    {
        CameraEntry* entry = FindCameraEntry( nameHash );
        if ( entry != nullptr )
        {
            return entry->_camera.get();
        }

        return nullptr;
    }

    bool Camera::RemoveChangeListener( const U32 id )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const auto it = s_changeCameraListeners.find( id );
        if ( it != std::cend( s_changeCameraListeners ) )
        {
            s_changeCameraListeners.erase( it );
            return true;
        }

        return false;
    }

    U32 Camera::AddChangeListener( const CameraListener& f )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        insert( s_changeCameraListeners, ++s_changeCameraId, f );
        return s_changeCameraId;
    }

    Camera::Camera( const std::string_view name, const Mode mode, const float3& eye )
        : Resource( name, "Camera" )
        , _mode( mode )
    {
        _data._eye.set( eye );
        _data._fov = 60.0f;
        _data._aspectRatio = 1.77f;
        _data._viewMatrix.identity();
        _data._invViewMatrix.identity();
        _data._projectionMatrix.identity();
        _data._invProjectionMatrix.identity();
        _data._zPlanes.set( 0.1f, 1000.0f );
        _data._orientation.identity();
    }

    void Camera::fromCamera( const Camera& camera )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _reflectionPlane = camera._reflectionPlane;
        _reflectionActive = camera._reflectionActive;
        _accumPitch = camera._accumPitch;
        _maxRadius = camera._maxRadius;
        _minRadius = camera._minRadius;
        _curRadius = camera._curRadius;
        _currentRotationX = camera._currentRotationX;
        _currentRotationY = camera._currentRotationY;
        _rotationDirty = true;
        _offsetDir.set( camera._offsetDir );
        _cameraRotation.set( camera._cameraRotation );
        _targetTransform = camera._targetTransform;
        _speedFactor = camera._speedFactor;
        _orthoRect.set( camera._orthoRect );
        setFixedYawAxis( camera._yawFixed, camera._fixedYawAxis );
        rotationLocked( camera._rotationLocked );
        movementLocked( camera._movementLocked );
        frustumLocked( camera._frustumLocked );
        fromSnapshot( camera.snapshot() );
    }

    void Camera::fromSnapshot( const CameraSnapshot& snapshot )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        setEye( snapshot._eye );
        setRotation( snapshot._orientation );
        setAspectRatio( snapshot._aspectRatio );
        setVerticalFoV( snapshot._fov );
        if ( _data._isOrthoCamera )
        {
            setProjection( _orthoRect, snapshot._zPlanes );
        }
        else
        {

            setProjection( snapshot._aspectRatio, snapshot._fov, snapshot._zPlanes );
        }
        updateLookAt();
    }

    void Camera::update() noexcept
    {
        if ( (mode() == Mode::ORBIT || mode() == Mode::THIRD_PERSON) && _targetTransform != nullptr )
        {
            PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

            if (/*trans->changedLastFrame() || */ _rotationDirty/* || true*/ )
            {
                vec3<Angle::RADIANS_F> newTargetOrientation = _targetTransform->getWorldOrientation().getEuler();
                newTargetOrientation.yaw = M_PI_f - newTargetOrientation.yaw;
                _data._orientation = Quaternion<F32>(_cameraRotation) * Quaternion<F32>(newTargetOrientation);
                _rotationDirty = false;
            }

            _minRadius = std::max( _minRadius, 0.01f );
            if ( _minRadius > _maxRadius )
            {
                std::swap( _minRadius, _maxRadius );
            }
            CLAMP<F32>( _curRadius, _minRadius, _maxRadius );

            const float3 targetPos = _targetTransform->getWorldPosition() + _offsetDir;
            setEye( _data._orientation.zAxis() * _curRadius + targetPos );
            _viewMatrixDirty = true;
        }
    }

    void Camera::setTarget( TransformComponent* tComp, const float3& offsetDirection ) noexcept
    {
        _targetTransform = tComp;
        _offsetDir = Normalized( offsetDirection );
    }

    const mat4<F32>& Camera::lookAt( const mat4<F32>& viewMatrix )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _data._eye.set( ExtractCameraPos2( viewMatrix ) );
        _data._orientation.fromMatrix( viewMatrix );
        _viewMatrixDirty = true;
        _frustumDirty = true;
        updateViewMatrix();

        return _data._viewMatrix;
    }

    const mat4<F32>& Camera::lookAt( const float3& eye,
                                     const float3& target,
                                     const float3& up )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _data._eye.set( eye );
        _data._orientation.fromMatrix( LookAt( eye, target, up ) );
        _viewMatrixDirty = true;
        _frustumDirty = true;

        updateViewMatrix();

        return _data._viewMatrix;
    }

    /// Tell the rendering API to set up our desired PoV
    bool Camera::updateLookAt()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        bool cameraUpdated = updateViewMatrix();
        cameraUpdated = updateProjection() || cameraUpdated;
        cameraUpdated = updateFrustum() || cameraUpdated;

        if ( cameraUpdated )
        {
            mat4<F32>::Multiply( _data._projectionMatrix, _data._viewMatrix, _viewProjectionMatrix );

            for ( const auto& it : _updateCameraListeners )
            {
                it.second( *this );
            }
        }

        return cameraUpdated;
    }

    void Camera::setGlobalRotation( const Angle::DEGREES_F yaw, const Angle::DEGREES_F pitch, const Angle::DEGREES_F roll ) noexcept
    {
        if ( _rotationLocked )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const Quaternion<F32> pitchRot( WORLD_X_AXIS, Angle::to_RADIANS(-pitch) );
        const Quaternion<F32> yawRot( WORLD_Y_AXIS, Angle::to_RADIANS(-yaw ) );

        if ( !IS_ZERO( roll ) )
        {
            setRotation( yawRot * pitchRot * Quaternion<F32>( WORLD_Z_AXIS, Angle::to_RADIANS(-roll) ) );
        }
        else
        {
            setRotation( yawRot * pitchRot );
        }
    }

    void Camera::rotate( const Quaternion<F32>& q )
    {
        if ( _rotationLocked )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        if ( mode() == Mode::FIRST_PERSON )
        {
            const vec3<Angle::DEGREES_F> euler = Angle::to_DEGREES( q.getEuler() );
            rotate( euler.yaw, euler.pitch, euler.roll );
        }
        else
        {
            _data._orientation = q * _data._orientation;
            _data._orientation.normalize();
        }

        _viewMatrixDirty = true;
    }

    bool Camera::removeUpdateListener( const U32 id )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const auto& it = _updateCameraListeners.find( id );
        if ( it != std::cend( _updateCameraListeners ) )
        {
            _updateCameraListeners.erase( it );
            return true;
        }

        return false;
    }

    U32 Camera::addUpdateListener( const CameraListener& f )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        insert( _updateCameraListeners, ++_updateCameraId, f );
        return _updateCameraId;
    }

    void Camera::setReflection( const Plane<F32>& reflectionPlane ) noexcept
    {
        _reflectionPlane = reflectionPlane;
        _reflectionActive = true;
        _viewMatrixDirty = true;
    }

    void Camera::clearReflection() noexcept
    {
        _reflectionActive = false;
        _viewMatrixDirty = true;
    }

    bool Camera::updateProjection() noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        if ( _projectionDirty )
        {
            if ( _data._isOrthoCamera )
            {
                _data._projectionMatrix = Ortho( _orthoRect.left,
                                                 _orthoRect.right,
                                                 _orthoRect.bottom,
                                                 _orthoRect.top,
                                                 _data._zPlanes.x,
                                                 _data._zPlanes.y );
            }
            else
            {
                _data._projectionMatrix = Perspective( _data._fov,
                                                       _data._aspectRatio,
                                                       _data._zPlanes.x,
                                                       _data._zPlanes.y );
            }
            _data._projectionMatrix.getInverse( _data._invProjectionMatrix );
            _frustumDirty = true;
            _projectionDirty = false;
            return true;
        }

        return false;
    }

    const mat4<F32>& Camera::setProjection( const float2 zPlanes )
    {
        return setProjection( _data._fov, zPlanes );
    }

    const mat4<F32>& Camera::setProjection( const Angle::DEGREES_F verticalFoV, const float2 zPlanes )
    {
        return setProjection( _data._aspectRatio, verticalFoV, zPlanes );
    }

    const mat4<F32>& Camera::setProjection( const F32 aspectRatio, const Angle::DEGREES_F verticalFoV, const float2 zPlanes )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        setAspectRatio( aspectRatio );
        setVerticalFoV( verticalFoV );

        _data._zPlanes = zPlanes;
        _data._isOrthoCamera = false;
        _projectionDirty = true;
        updateProjection();

        return projectionMatrix();
    }

    const mat4<F32>& Camera::setProjection( const float4& rect, const float2 zPlanes )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _data._zPlanes = zPlanes;
        _orthoRect = rect;
        _data._isOrthoCamera = true;
        _projectionDirty = true;
        updateProjection();

        return projectionMatrix();
    }

    const mat4<F32>& Camera::setProjection( const mat4<F32>& projection, const float2 zPlanes, const bool isOrtho ) noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _data._projectionMatrix.set( projection );
        _data._projectionMatrix.getInverse( _data._invProjectionMatrix );
        _data._zPlanes = zPlanes;
        _projectionDirty = false;
        _frustumDirty = true;
        _data._isOrthoCamera = isOrtho;

        return _data._projectionMatrix;
    }

    void Camera::setAspectRatio( const F32 ratio ) noexcept
    {
        _data._aspectRatio = ratio;
        _projectionDirty = true;
    }

    void Camera::setVerticalFoV( const Angle::DEGREES_F verticalFoV ) noexcept
    {
        _data._fov = verticalFoV;
        _projectionDirty = true;
    }

    void Camera::setHorizontalFoV( const Angle::DEGREES_F horizontalFoV ) noexcept
    {
        _data._fov = Angle::to_VerticalFoV( horizontalFoV, to_D64( _data._aspectRatio ) );
        _projectionDirty = true;
    }

    Angle::DEGREES_F Camera::getHorizontalFoV() const noexcept
    {
        const Angle::RADIANS_F halfFoV = Angle::to_RADIANS( _data._fov ) * 0.5f;
        return Angle::to_DEGREES(Angle::RADIANS_F(2.0f * std::atan( tan( halfFoV ) * _data._aspectRatio )));
    }

    void Camera::setRotation( const Angle::DEGREES_F yaw, const Angle::DEGREES_F pitch, const Angle::DEGREES_F roll ) noexcept
    {
        setRotation( Quaternion<F32>(Angle::to_RADIANS(pitch), Angle::to_RADIANS(yaw), Angle::to_RADIANS(roll)) );
    }

    void Camera::rotate( Angle::DEGREES_F yaw, Angle::DEGREES_F pitch, Angle::DEGREES_F roll ) noexcept
    {
        if ( _rotationLocked )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const F32 turnSpeed = speedFactor().turn * s_lastFrameTimeSec;
        yaw = -yaw * turnSpeed;
        pitch = -pitch * turnSpeed;
        roll = -roll * turnSpeed;

        Quaternion<F32> tempOrientation;
        if ( mode() == Mode::FIRST_PERSON )
        {
            _accumPitch += pitch;

            if ( _accumPitch > 90.0f )
            {
                pitch = 90.0f - (_accumPitch - pitch);
                _accumPitch = 90.0f;
            }

            if ( _accumPitch < -90.0f )
            {
                pitch = -90.0f - (_accumPitch - pitch);
                _accumPitch = -90.0f;
            }

            // Rotate camera about the world y axis.
            // Note the order the quaternions are multiplied. That is important!
            if ( !IS_ZERO( yaw ) )
            {
                tempOrientation.fromAxisAngle( WORLD_Y_AXIS, Angle::to_RADIANS(yaw) );
                _data._orientation = tempOrientation * _data._orientation;
            }

            // Rotate camera about its local x axis.
            // Note the order the quaternions are multiplied. That is important!
            if ( !IS_ZERO( pitch ) )
            {
                tempOrientation.fromAxisAngle( WORLD_X_AXIS, Angle::to_RADIANS(pitch) );
                _data._orientation = _data._orientation * tempOrientation;
            }
        }
        else
        {
            tempOrientation.fromEuler( Angle::to_RADIANS(pitch), Angle::to_RADIANS(yaw), Angle::to_RADIANS(roll) );
            _data._orientation *= tempOrientation;
        }

        _viewMatrixDirty = true;
    }

    Quaternion<F32> Camera::rotationYaw( const Angle::DEGREES_F angle ) const
    {
        return Quaternion<F32>(_yawFixed ? _fixedYawAxis : _data._orientation * WORLD_Y_AXIS, -Angle::to_RADIANS(angle * speedFactor().turn * s_lastFrameTimeSec));
    }

    Quaternion<F32> Camera::rotationRoll( const Angle::DEGREES_F angle ) const
    {
        return Quaternion<F32>(_data._orientation * WORLD_Z_AXIS, -Angle::to_RADIANS(angle * speedFactor().turn * s_lastFrameTimeSec));
    }

    Quaternion<F32> Camera::rotationPitch( const Angle::DEGREES_F angle ) const
    {
        return Quaternion<F32>(_data._orientation * WORLD_X_AXIS, -Angle::to_RADIANS(angle * speedFactor().turn * s_lastFrameTimeSec));
    }

    void Camera::rotateYaw( const Angle::DEGREES_F angle )
    {
        rotate(rotationYaw(angle));
    }

    void Camera::rotateRoll( const Angle::DEGREES_F angle )
    {
        rotate(rotationRoll(angle));
    }

    void Camera::rotatePitch( const Angle::DEGREES_F angle )
    {
        rotate(rotationPitch(angle));
    }

    void Camera::move( F32 dx, F32 dy, F32 dz ) noexcept
    {
        if ( _movementLocked )
        {
            return;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const F32 moveSpeed = speedFactor().move * s_lastFrameTimeSec;
        dx *= moveSpeed;
        dy *= moveSpeed;
        dz *= moveSpeed;

        const mat4<F32>& viewMat = viewMatrix();
        const float3 rightDir = viewMat.getRightDirection();

        _data._eye += rightDir * dx;
        _data._eye += WORLD_Y_AXIS * dy;

        if ( mode() == Mode::FIRST_PERSON )
        {
            // Calculate the forward direction. Can't just use the camera's local
            // z axis as doing so will cause the camera to move more slowly as the
            // camera's view approaches 90 degrees straight up and down.
            const float3 forward = Normalized( Cross( WORLD_Y_AXIS, rightDir ) );
            _data._eye += forward * dz;
        }
        else
        {
            _data._eye += viewMat.getForwardDirection() * dz;
        }

        _viewMatrixDirty = true;
    }

    bool Camera::moveFromPlayerState( const SceneStatePerPlayer& playerState )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        bool updated = false;
        if ( mode() == Mode::FREE_FLY )
        {
            updated = moveRelative(
            {
                playerState._moveFB.topValue(),
                playerState._moveLR.topValue(),
                playerState._moveUD.topValue()
            }) || updated;
        }

        updated = rotateRelative( 
        {
            playerState._angleUD.topValue(),
            playerState._angleLR.topValue(),
            playerState._roll.topValue()
        }) || updated;

        if ( mode() == Mode::ORBIT || mode() == Mode::THIRD_PERSON )
        {
            updated = zoom( playerState._zoom.topValue() ) || updated;
        }
        else if (playerState._zoom.top()._direction != MoveDirection::NONE)
        {
            move(0.f, 0.f, playerState._zoom.topValue());
            updated = true;
        }

        return updated;
    }

    bool Camera::moveRelative( const float3& relMovement )
    {
        if ( relMovement.lengthSquared() > 0 )
        {
            move( relMovement.y, relMovement.z, relMovement.x );
            return true;
        }

        return false;
    }

    bool Camera::rotateRelative( const vec3<Angle::DEGREES_F>& relRotation )
    {
        if (_rotationLocked || relRotation.lengthSquared() <= 0.f )
        {
            return false;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        if ( mode() == Mode::THIRD_PERSON || mode() == Mode::ORBIT )
        {
            const vec3<Angle::DEGREES_F> rotation( relRotation * speedFactor().turn * s_lastFrameTimeSec );

            const Angle::RADIANS_F oneDegreeInRad = Angle::to_RADIANS(Angle::DEGREES_F(1.f));

            const Angle::RADIANS_F rotationLimitRollLower = M_PI_f * 0.30f  - oneDegreeInRad;
            const Angle::RADIANS_F rotationLimitRollUpper = M_PI_f * 0.175f - oneDegreeInRad;
            const Angle::RADIANS_F rotationLimitPitch     = M_PI_f          - oneDegreeInRad;

            if ( !IS_ZERO( rotation.yaw ) )
            {
                const Angle::RADIANS_F yawRad = Angle::to_RADIANS( rotation.yaw );

                const F32 targetYaw = _cameraRotation.yaw - yawRad;
                if ( mode() == Mode::ORBIT || (targetYaw > -rotationLimitRollLower && targetYaw < rotationLimitRollUpper) )
                {
                    _cameraRotation.yaw -= yawRad;
                    _rotationDirty = true;
                }
            }

            if ( !IS_ZERO( rotation.pitch ) )
            {
                const Angle::RADIANS_F pitchRad = Angle::to_RADIANS( rotation.pitch );

                const F32 targetPitch = _cameraRotation.yaw - pitchRad;
                if ( mode() == Mode::ORBIT || (targetPitch > -rotationLimitPitch && targetPitch < rotationLimitPitch) )
                {
                    _cameraRotation.pitch -= pitchRad;
                    _rotationDirty = true;
                }
            }

            if ( _rotationDirty )
            {
                Util::Normalize( _cameraRotation, true, false, true );
            }
        }
        else
        {
            rotate( rotationYaw(relRotation.yaw) * rotationPitch(relRotation.pitch) * rotationRoll(relRotation.roll) );
            _rotationDirty = true;
        }

        return _rotationDirty;
    }

    bool Camera::zoom( const F32 zoomFactor ) noexcept
    {
        if ( !IS_ZERO( zoomFactor ) )
        {
            curRadius( _curRadius += zoomFactor * speedFactor().zoom * s_lastFrameTimeSec * -0.01f );
            return true;
        }

        return false;
    }


    bool Camera::updateViewMatrix() noexcept
    {
        if ( !_viewMatrixDirty )
        {
            return false;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _data._orientation.normalize();

        //_target = -zAxis + _data._eye;

        // Reconstruct the view matrix.
        _data._viewMatrix.set( GetMatrix( _data._orientation ) );
        _data._viewMatrix.setRow( 3,
                                  -_data._orientation.xAxis().dot( _data._eye ),
                                  -_data._orientation.yAxis().dot( _data._eye ),
                                  -_data._orientation.zAxis().dot( _data._eye ),
                                  1.f );

        _euler = Angle::to_DEGREES( _data._orientation.getEuler() );

        // Extract the pitch angle from the view matrix.
        _accumPitch = Angle::to_DEGREES( Angle::RADIANS_F(asinf( _data._viewMatrix.getForwardDirection().y ) ));

        if ( _reflectionActive )
        {
            _data._viewMatrix.reflect( _reflectionPlane );
            _data._eye.set( mat4<F32>( _reflectionPlane ).transformNonHomogeneous( _data._eye ) );
        }
        _data._viewMatrix.getInverse( _data._invViewMatrix );
        _viewMatrixDirty = false;
        _frustumDirty = true;

        return true;
    }

    bool Camera::updateFrustum()
    {
        if ( _frustumLocked )
        {
            return true;
        }
        if ( !_frustumDirty )
        {
            return false;
        }

        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        _frustumLocked = true;
        updateLookAt();
        _frustumLocked = false;

        _data._frustumPlanes = _frustum.computePlanes( _viewProjectionMatrix );
        _frustumDirty = false;

        return true;
    }

    float3 Camera::unProject( const F32 winCoordsX, const F32 winCoordsY, const Rect<I32>& viewport ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const F32 offsetWinCoordsX = winCoordsX - viewport.x;
        const F32 offsetWinCoordsY = winCoordsY - viewport.y;
        const I32 winWidth = viewport.z;
        const I32 winHeight = viewport.w;

        const float2 ndcSpace = 
        {
            offsetWinCoordsX / (winWidth  * 0.5f) - 1.0f,
            offsetWinCoordsY / (winHeight * 0.5f) - 1.0f
        };

        const float4 clipSpace = {
            ndcSpace.x,
            ndcSpace.y,
            0.0f, //z
            1.0f  //w
        };

        const mat4<F32> invProjMatrix = GetInverse( projectionMatrix() );

        const float2 tempEyeSpace = (invProjMatrix * clipSpace).xy;

        const float4 eyeSpace = {
            tempEyeSpace.x,
            tempEyeSpace.y,
            -1.0f, // z
             0.0f  // w
        };

        const float3 worldSpace = (worldMatrix() * eyeSpace).xyz;

        return Normalized( worldSpace );
    }

    float2 Camera::project( const float3& worldCoords, const Rect<I32>& viewport ) const noexcept
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GameLogic );

        const float2 winOffset = viewport.xy;

        const float2 winSize = viewport.zw;

        const float4 viewSpace = viewMatrix() * float4( worldCoords, 1.0f );

        const float4 clipSpace = projectionMatrix() * viewSpace;

        const F32 clampedClipW = std::max( clipSpace.w, EPSILON_F32 );

        const float2 ndcSpace = clipSpace.xy / clampedClipW;

        const float2 winSpace = (ndcSpace + 1.0f) * 0.5f * winSize;

        return winOffset + winSpace;
    }

    mat4<F32> Camera::LookAt( const float3& eye, const float3& target, const float3& up ) noexcept
    {
        const float3 zAxis( Normalized( eye - target ) );
        const float3 xAxis( Normalized( Cross( up, zAxis ) ) );
        const float3 yAxis( Normalized( Cross( zAxis, xAxis ) ) );

        mat4<F32> ret;

        ret.m[0][0] = xAxis.x;
        ret.m[1][0] = xAxis.y;
        ret.m[2][0] = xAxis.z;
        ret.m[3][0] = -xAxis.dot( eye );

        ret.m[0][1] = yAxis.x;
        ret.m[1][1] = yAxis.y;
        ret.m[2][1] = yAxis.z;
        ret.m[3][1] = -yAxis.dot( eye );

        ret.m[0][2] = zAxis.x;
        ret.m[1][2] = zAxis.y;
        ret.m[2][2] = zAxis.z;
        ret.m[3][2] = -zAxis.dot( eye );

        ret.m[0][3] = 0;
        ret.m[1][3] = 0;
        ret.m[2][3] = 0;
        ret.m[3][3] = 1;

        return ret;
    }

    void Camera::saveToXML( boost::property_tree::ptree& pt, const std::string prefix ) const
    {
        const float4 orientation = _data._orientation.asVec4();

        std::string savePath = (prefix.empty() ? "camera." : (prefix + ".camera."));
        savePath.append(Util::MakeXMLSafe(resourceName()));

        pt.put( savePath + ".reflectionPlane.normal.<xmlattr>.x", _reflectionPlane._normal.x );
        pt.put( savePath + ".reflectionPlane.normal.<xmlattr>.y", _reflectionPlane._normal.y );
        pt.put( savePath + ".reflectionPlane.normal.<xmlattr>.z", _reflectionPlane._normal.z );
        pt.put( savePath + ".reflectionPlane.distance", _reflectionPlane._distance );
        pt.put( savePath + ".reflectionPlane.active", _reflectionActive );
        pt.put( savePath + ".accumPitchDegrees", _accumPitch );
        pt.put( savePath + ".frustumLocked", _frustumLocked );
        pt.put( savePath + ".euler.<xmlattr>.x", _euler.x );
        pt.put( savePath + ".euler.<xmlattr>.y", _euler.y );
        pt.put( savePath + ".euler.<xmlattr>.z", _euler.z );
        pt.put( savePath + ".eye.<xmlattr>.x", _data._eye.x );
        pt.put( savePath + ".eye.<xmlattr>.y", _data._eye.y );
        pt.put( savePath + ".eye.<xmlattr>.z", _data._eye.z );
        pt.put( savePath + ".orientation.<xmlattr>.x", orientation.x );
        pt.put( savePath + ".orientation.<xmlattr>.y", orientation.y );
        pt.put( savePath + ".orientation.<xmlattr>.z", orientation.z );
        pt.put( savePath + ".orientation.<xmlattr>.w", orientation.w );
        pt.put( savePath + ".aspectRatio", _data._aspectRatio );
        pt.put( savePath + ".zPlanes.<xmlattr>.min", _data._zPlanes.min );
        pt.put( savePath + ".zPlanes.<xmlattr>.max", _data._zPlanes.max );
        pt.put( savePath + ".FoV", _data._fov );
        pt.put( savePath + ".speedFactor.<xmlattr>.turn", _speedFactor.turn );
        pt.put( savePath + ".speedFactor.<xmlattr>.move", _speedFactor.move );
        pt.put( savePath + ".speedFactor.<xmlattr>.zoom", _speedFactor.zoom );
        pt.put( savePath + ".fixedYawAxis.<xmlattr>.x", _fixedYawAxis.x );
        pt.put( savePath + ".fixedYawAxis.<xmlattr>.y", _fixedYawAxis.y );
        pt.put( savePath + ".fixedYawAxis.<xmlattr>.z", _fixedYawAxis.z );
        pt.put( savePath + ".yawFixed", _yawFixed );
        pt.put( savePath + ".rotationLocked", _rotationLocked );
        pt.put( savePath + ".movementLocked", _movementLocked );
        pt.put( savePath + ".maxRadius", maxRadius() );
        pt.put( savePath + ".minRadius", minRadius() );
        pt.put( savePath + ".curRadius", curRadius() );
        pt.put( savePath + ".cameraRotation.<xmlattr>.x", _cameraRotation.x );
        pt.put( savePath + ".cameraRotation.<xmlattr>.y", _cameraRotation.y );
        pt.put( savePath + ".cameraRotation.<xmlattr>.z", _cameraRotation.z );
        pt.put( savePath + ".offsetDir.<xmlattr>.x", _offsetDir.x );
        pt.put( savePath + ".offsetDir.<xmlattr>.y", _offsetDir.y );
        pt.put( savePath + ".offsetDir.<xmlattr>.z", _offsetDir.z );
    }

    void Camera::loadFromXML( const boost::property_tree::ptree& pt, const std::string prefix )
    {
        const float4 orientation = _data._orientation.asVec4();

        std::string savePath = (prefix.empty() ? "camera." : (prefix + ".camera."));
        savePath.append(Util::MakeXMLSafe(resourceName()));

        _reflectionPlane.set(
            pt.get( savePath + ".reflectionPlane.normal.<xmlattr>.x", _reflectionPlane._normal.x ),
            pt.get( savePath + ".reflectionPlane.normal.<xmlattr>.y", _reflectionPlane._normal.y ),
            pt.get( savePath + ".reflectionPlane.normal.<xmlattr>.z", _reflectionPlane._normal.z ),
            pt.get( savePath + ".reflectionPlane.distance", _reflectionPlane._distance )
        );
        _reflectionActive = pt.get( savePath + ".reflectionPlane.active", _reflectionActive );

        _accumPitch = pt.get( savePath + ".accumPitchDegrees", _accumPitch );
        _frustumLocked = pt.get( savePath + ".frustumLocked", _frustumLocked );
        _euler.set(
            pt.get( savePath + ".euler.<xmlattr>.x", _euler.x ),
            pt.get( savePath + ".euler.<xmlattr>.y", _euler.y ),
            pt.get( savePath + ".euler.<xmlattr>.z", _euler.z )
        );
        _data._eye.set(
            pt.get( savePath + ".eye.<xmlattr>.x", _data._eye.x ),
            pt.get( savePath + ".eye.<xmlattr>.y", _data._eye.y ),
            pt.get( savePath + ".eye.<xmlattr>.z", _data._eye.z )
        );
        _data._orientation.set(
            pt.get( savePath + ".orientation.<xmlattr>.x", orientation.x ),
            pt.get( savePath + ".orientation.<xmlattr>.y", orientation.y ),
            pt.get( savePath + ".orientation.<xmlattr>.z", orientation.z ),
            pt.get( savePath + ".orientation.<xmlattr>.w", orientation.w )
        );
        _data._zPlanes.set(
            pt.get( savePath + ".zPlanes.<xmlattr>.min", _data._zPlanes.min ),
            pt.get( savePath + ".zPlanes.<xmlattr>.max", _data._zPlanes.max )
        );
        _data._aspectRatio = pt.get( savePath + ".aspectRatio", _data._aspectRatio );
        _data._fov = pt.get( savePath + ".FoV", _data._fov );


        _speedFactor.turn = pt.get( savePath + ".speedFactor.<xmlattr>.turn", _speedFactor.turn );
        _speedFactor.move = pt.get( savePath + ".speedFactor.<xmlattr>.move", _speedFactor.move );
        _speedFactor.zoom = pt.get( savePath + ".speedFactor.<xmlattr>.zoom", _speedFactor.zoom );
        _fixedYawAxis.set(
            pt.get( savePath + ".fixedYawAxis.<xmlattr>.x", _fixedYawAxis.x ),
            pt.get( savePath + ".fixedYawAxis.<xmlattr>.y", _fixedYawAxis.y ),
            pt.get( savePath + ".fixedYawAxis.<xmlattr>.z", _fixedYawAxis.z )
        );
        _yawFixed = pt.get( savePath + ".yawFixed", _yawFixed );
        _rotationLocked = pt.get( savePath + ".rotationLocked", _rotationLocked );
        _movementLocked = pt.get( savePath + ".movementLocked", _movementLocked );
        maxRadius( pt.get( savePath + ".maxRadius", maxRadius() ) );
        minRadius( pt.get( savePath + ".minRadius", minRadius() ) );
        curRadius( pt.get( savePath + ".curRadius", curRadius() ) );
        _cameraRotation.set(
            pt.get( savePath + ".cameraRotation.<xmlattr>.x", _cameraRotation.x ),
            pt.get( savePath + ".cameraRotation.<xmlattr>.y", _cameraRotation.y ),
            pt.get( savePath + ".cameraRotation.<xmlattr>.z", _cameraRotation.z )
        );
        _offsetDir.set(
            pt.get( savePath + ".offsetDir.<xmlattr>.x", _offsetDir.x ),
            pt.get( savePath + ".offsetDir.<xmlattr>.y", _offsetDir.y ),
            pt.get( savePath + ".offsetDir.<xmlattr>.z", _offsetDir.z )
        );
        _viewMatrixDirty = _projectionDirty = _frustumDirty = true;
    }

} //namespace Divide
