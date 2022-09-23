/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _CAMERA_H_
#define _CAMERA_H_

#include "CameraSnapshot.h"
#include "Frustum.h"

#include "Core/Resources/Headers/Resource.h"

namespace Divide
{
    enum class FStops : U8
    {
        F_1_4,
        F_1_8,
        F_2_0,
        F_2_8,
        F_3_5,
        F_4_0,
        F_5_6,
        F_8_0,
        F_11_0,
        F_16_0,
        F_22_0,
        F_32_0,
        COUNT
    };

    namespace Names
    {
        static const char* fStops[] = {
            "f/1.4", "f/1.8", "f/2", "f/2.8", "f/3.5", "f/4", "f/5.6", "f/8", "f/11", "f/16", "f/22", "f/32", "NONE"
        };
    }
    static_assert(ArrayCount( Names::fStops ) == to_base( FStops::COUNT ) + 1, "Camera::FStops name array out of sync!");

    static constexpr std::array<F32, to_base( FStops::COUNT )> g_FStopValues = {
        1.4f, 1.8f, 2.0f, 2.8f, 3.5f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f, 32.0f
    };

    namespace TypeUtil
    {
        [[nodiscard]] const char* FStopsToString( FStops stop ) noexcept;
        [[nodiscard]] FStops StringToFStops( const string& name );
    };

    using CameraListener = DELEGATE<void, const Camera&>;
    using CameraListenerMap = hashMap<U32, CameraListener>;

    class GFXDevice;
    class TransformComponent;

    struct SceneStatePerPlayer;

    class Camera final : public Resource
    {
        public:
        static constexpr F32 s_minNearZ = 0.1f;

        enum class Mode : U8
        {
            FREE_FLY = 0,
            STATIC,
            FIRST_PERSON,
            THIRD_PERSON,
            ORBIT,
            SCRIPTED,
            COUNT
        };

        enum class UtilityCamera : U8
        {
            _2D = 0,
            _2D_FLIP_Y,
            DEFAULT,
            CUBE,
            DUAL_PARABOLOID,
            COUNT
        };

        public:
        ~Camera() = default;
        explicit Camera( const Str256& name, Mode mode, const vec3<F32>& eye = VECTOR3_ZERO );

        /// Copies all of the internal data from the specified camera to the current one
        void fromCamera( const Camera& camera );
        /// Sets the internal snapshot data (eye, orientation, etc) to match the specified value
        void fromSnapshot( const CameraSnapshot& snapshot );
        /// Return true if the cached camera state wasn't up-to-date
        bool updateLookAt();
        /// Specify a reflection plane that alters the final view matrix to be a mirror of the internal lookAt matrix
        void setReflection( const Plane<F32>& reflectionPlane ) noexcept;
        /// Clears the reflection plane specified (if any)
        void clearReflection() noexcept;
        /// Global rotations are applied relative to the world axis, not the camera's
        void setGlobalRotation( F32 yaw, F32 pitch, F32 roll = 0.0f ) noexcept;
        /// Global rotations are applied relative to the world axis, not the camera's
        void setGlobalRotation( const vec3<Angle::DEGREES<F32>>& euler ) noexcept;
        /// Sets the camera's view matrix to specify the specified value by extracting the eye position, orientation and other data from it 
        const mat4<F32>& lookAt( const mat4<F32>& viewMatrix );
        /// Sets the camera's position, target and up directions
        const mat4<F32>& lookAt( const vec3<F32>& eye, const vec3<F32>& target, const vec3<F32>& up );
        /// Sets the camera to point at the specified target point
        const mat4<F32>& lookAt( const vec3<F32>& target );
        /// Sets the camera to point at the specified target point from the specified eye position
        const mat4<F32>& lookAt( const vec3<F32>& eye, const vec3<F32>& target );
        /// Sets the camera's Yaw angle.
        /// This creates a new orientation quaternion for the camera and extracts the Euler angles
        void setYaw( const Angle::DEGREES<F32> angle ) noexcept;
        /// Sets the camera's Pitch angle. Yaw and Roll are previous extracted values
        void setPitch( const Angle::DEGREES<F32> angle ) noexcept;
        /// Sets the camera's Roll angle. Yaw and Pitch are previous extracted values
        void setRoll( const Angle::DEGREES<F32> angle ) noexcept;
        /// Sets the camera's Yaw angle.
        /// This creates a new orientation quaternion for the camera and extracts the Euler angles
        void setGlobalYaw( const Angle::DEGREES<F32> angle ) noexcept;
        /// Sets the camera's Pitch angle. Yaw and Roll are previous extracted values
        void setGlobalPitch( const Angle::DEGREES<F32> angle ) noexcept;
        /// Sets the camera's Roll angle. Yaw and Pitch are previous extracted values
        void setGlobalRoll( const Angle::DEGREES<F32> angle ) noexcept;
        /// Sets the camera's eye position
        void setEye( const F32 x, const F32 y, const F32 z ) noexcept;
        /// Sets the camera's eye position
        void setEye( const vec3<F32>& position ) noexcept;
        /// Sets the camera's orientation
        void setRotation( const Quaternion<F32>& q ) noexcept;
        /// Sets the camera's orientation
        void setRotation( const Angle::DEGREES<F32> yaw, const Angle::DEGREES<F32> pitch, const Angle::DEGREES<F32> roll = 0.0f ) noexcept;
        /// Rotates the camera (changes its orientation) by the specified quaternion (_orientation *= q)
        void rotate( const Quaternion<F32>& q );
        /// Sets the camera's orientation to match the specified yaw, pitch and roll values;
        /// Creates a quaternion based on the specified Euler angles and calls "rotate" to change the orientation
        void rotate( Angle::DEGREES<F32> yaw, Angle::DEGREES<F32> pitch, Angle::DEGREES<F32> roll ) noexcept;
        /// Creates a quaternion based on the specified axis-angle and calls "rotate" to change the orientation
        void rotate( const vec3<F32>& axis, const Angle::DEGREES<F32> angle );
        /// Yaw, Pitch and Roll call "rotate" with a appropriate quaternion for  each rotation.
        /// Because the camera is facing the -Z axis, a positive angle will create a positive Yaw
        /// behind the camera and a negative one in front of the camera (so we invert the angle - left will turn left when facing -Z)
        void rotateYaw( Angle::DEGREES<F32> angle );
        /// Change camera's roll.
        void rotateRoll( Angle::DEGREES<F32> angle );
        /// Change camera's pitch
        void rotatePitch( Angle::DEGREES<F32> angle );
        /// Moves the camera by the specified offsets in each direction
        void move( F32 dx, F32 dy, F32 dz ) noexcept;
        /// Moves the camera forward or backwards
        void moveForward( const F32 factor ) noexcept;
        /// Moves the camera left or right
        void moveStrafe( const F32 factor ) noexcept;
        /// Moves the camera up or down
        void moveUp( const F32 factor ) noexcept;
        /// Exactly as in Ogre3D: locks the yaw movement to the specified axis
        void setFixedYawAxis( const bool useFixed, const vec3<F32>& fixedAxis = WORLD_Y_AXIS ) noexcept;
        bool moveRelative( const vec3<F32>& relMovement );
        bool rotateRelative( const vec3<F32>& relRotation );
        bool zoom( F32 zoomFactor ) noexcept;
        /// Set the camera's rotation to match the specified euler angles
        void setEuler( const vec3<Angle::DEGREES<F32>>& euler ) noexcept;
        /// Set the camera's rotation to match the specified euler angles
        void setEuler( const Angle::DEGREES<F32>& pitch, const Angle::DEGREES<F32>& yaw, const Angle::DEGREES<F32>& roll ) noexcept;
        void setAspectRatio( F32 ratio ) noexcept;
        void setVerticalFoV( Angle::DEGREES<F32> verticalFoV ) noexcept;
        void setHorizontalFoV( Angle::DEGREES<F32> horizontalFoV ) noexcept;

        const mat4<F32>& setProjection( const vec2<F32>& zPlanes );
        const mat4<F32>& setProjection( F32 verticalFoV, const vec2<F32>& zPlanes );
        const mat4<F32>& setProjection( F32 aspectRatio, F32 verticalFoV, const vec2<F32>& zPlanes );
        const mat4<F32>& setProjection( const vec4<F32>& rect, const vec2<F32>& zPlanes );
        const mat4<F32>& setProjection( const mat4<F32>& projection, const vec2<F32>& zPlanes, bool isOrtho ) noexcept;

        /// Offset direction is a (eventually normalized) vector that is scaled by curRadius and applied to the camera's eye position
        void setTarget( TransformComponent* tComp, const vec3<F32>& offsetDirection = VECTOR3_ZERO ) noexcept;
        bool moveFromPlayerState( const SceneStatePerPlayer& playerState );

        void saveToXML( boost::property_tree::ptree& pt, string prefix = "" ) const;
        void loadFromXML( const boost::property_tree::ptree& pt, string prefix = "" );

        /// Returns the world space direction for the specified winCoords for this camera
        /// Use snapshot()._eye + unProject(...) * distance for a world-space position
        [[nodiscard]] vec3<F32> unProject( F32 winCoordsX, F32 winCoordsY, const Rect<I32>& viewport ) const noexcept;
        [[nodiscard]] vec3<F32> unProject( const vec3<F32>& winCoords, const Rect<I32>& viewport ) const noexcept;
        [[nodiscard]] vec2<F32> project( const vec3<F32>& worldCoords, const Rect<I32>& viewport ) const noexcept;

        [[nodiscard]] bool removeUpdateListener( U32 id );
        [[nodiscard]] U32  addUpdateListener( const CameraListener& f );

        /// Returns the internal camera snapshot data (eye, orientation, etc)
        [[nodiscard]] const CameraSnapshot&     snapshot()             const noexcept;
        /// Returns the horizontal field of view, calculated from the vertical FoV and aspect ratio
        [[nodiscard]]       Angle::DEGREES<F32> getHorizontalFoV()     const noexcept;
        /// Returns the most recent/up-to-date view matrix
        [[nodiscard]] const mat4<F32>&          viewMatrix()           const noexcept;
        /// Updates the view matrix and returns the result
        [[nodiscard]] const mat4<F32>&          viewMatrix()                 noexcept;
        /// Returns the most recent/up-to-date projection matrix
        [[nodiscard]] const mat4<F32>&          projectionMatrix()     const noexcept;
        /// Updates the projection matrix and returns the result
        [[nodiscard]] const mat4<F32>&          projectionMatrix()           noexcept;
        /// Returns the most recent/up-to-date viewProjection matrix
        [[nodiscard]] const mat4<F32>&          viewProjectionMatrix() const noexcept;
        /// Updates the viewProjection matrix and returns the result
        [[nodiscard]] const mat4<F32>&          viewProjectionMatrix()       noexcept;
        /// Returns the most recent/up-to-date inverse of the view matrix
        [[nodiscard]] const mat4<F32>&          worldMatrix()          const noexcept;
        /// Updates the view matrix and returns its inverse as the worldMatrix
        [[nodiscard]] const mat4<F32>&          worldMatrix()                noexcept;
        /// Returns the most recent/up-to-date frustum
        [[nodiscard]] const Frustum&            getFrustum()           const noexcept;
        /// Updates the frustum and returns the result
        [[nodiscard]]       Frustum&            getFrustum()                 noexcept;

        PROPERTY_R_IW( vec4<F32>, orthoRect, VECTOR4_UNIT );
        PROPERTY_R_IW( vec3<Angle::DEGREES<F32>>, euler, VECTOR3_ZERO );
        PROPERTY_RW( vec3<F32>, speedFactor, { 5.f } );
        PROPERTY_RW( F32, maxRadius, 10.f );
        PROPERTY_RW( F32, minRadius, 0.1f );
        PROPERTY_RW( F32, curRadius, 8.f );
        PROPERTY_RW( Mode, mode, Mode::COUNT );
        PROPERTY_RW( bool, frustumLocked, false );
        PROPERTY_RW( bool, rotationLocked, false );
        PROPERTY_RW( bool, movementLocked, false );
        PROPERTY_R_IW( bool, reflectionActive, false );

        protected:
        /// Extract the frustum associated with our current PoV
        bool updateFrustum();
        bool updateViewMatrix() noexcept;
        bool updateProjection() noexcept;
        void update() noexcept;

        protected:
        CameraListenerMap _updateCameraListeners;
        CameraSnapshot _data;
        Frustum _frustum;
        TransformComponent* _targetTransform{ nullptr };
        mat4<F32> _viewProjectionMatrix;
        Plane<F32> _reflectionPlane;
        vec3<Angle::RADIANS<F32>> _cameraRotation{ VECTOR3_ZERO };
        vec3<F32> _fixedYawAxis{ WORLD_Y_AXIS };
        vec3<F32> _offsetDir{ WORLD_Z_AXIS };
        Angle::DEGREES<F32> _accumPitchDegrees{ 0.0f };
        F32 _currentRotationX{ 0.0f };
        F32 _currentRotationY{ 0.0f };
        U32 _updateCameraId{ 0u };
        bool _projectionDirty{ true };
        bool _viewMatrixDirty{ false };
        bool _frustumDirty{ true };
        bool _yawFixed{ false };
        bool _rotationDirty{ true };

        // Camera pool
        public:
        static void Update( U64 deltaTimeUS );
        static void InitPool();
        static void DestroyPool();
        static bool DestroyCamera( Camera*& camera );

        static Camera* GetUtilityCamera( const UtilityCamera type );

        static Camera* CreateCamera( const Str256& cameraName, Mode cameraMode );
        static Camera* FindCamera( const U64 nameHash );

        static bool RemoveChangeListener( U32 id );
        static U32  AddChangeListener( const CameraListener& f );
    };

    TYPEDEF_SMART_POINTERS_FOR_TYPE( Camera );

    namespace Names
    {
        static const char* cameraMode[] = {
            "FREE_FLY",
            "STATIC",
            "FIRST_PERSON",
            "THIRD_PERSON",
            "ORBIT",
            "SCRIPTED",
            "UNKNOWN"
        };
    }
    static_assert(ArrayCount( Names::cameraMode ) == to_base( Camera::Mode::COUNT ) + 1, "Camera::Mode name array out of sync!");

    namespace TypeUtil
    {
        [[nodiscard]] const char* CameraModeToString( Camera::Mode mode ) noexcept;
        [[nodiscard]] Camera::Mode StringToCameraMode( const string& name );
    };

};  // namespace Divide
#endif //_CAMERA_H_

#include "Camera.inl"
