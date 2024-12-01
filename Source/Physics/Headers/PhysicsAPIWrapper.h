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
#ifndef DVD_PHYSICS_API_WRAPPER_H_
#define DVD_PHYSICS_API_WRAPPER_H_

#include "Core/Headers/FrameListener.h"
#include "Core/Headers/PlatformContextComponent.h"

namespace Divide
{

struct SGNRayResult;
struct Ray;

enum class RigidBodyShape : U8 {
    SHAPE_SPHERE = 0,
    SHAPE_PLANE,
    SHAPE_CAPSULE,
    SHAPE_BOX,
    SHAPE_CONVEXMESH,
    SHAPE_TRIANGLEMESH,
    SHAPE_HEIGHTFIELD,
    SHAPE_COUNT
};

class Scene;
class PhysicsAsset;

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);

enum class ErrorCode : I8;
enum class PhysicsGroup : U8;
class RigidBodyComponent;

NOINITVTABLE_CLASS(PhysicsAPIWrapper) : public PlatformContextComponent
{
   public:
    explicit PhysicsAPIWrapper( PlatformContext& context );

    virtual bool convertActor(PhysicsAsset* actor, PhysicsGroup newGroup) = 0;

   protected:
    friend class PXDevice;
    virtual ErrorCode initPhysicsAPI(U8 targetFrameRate, F32 simSpeed) = 0;
    virtual bool closePhysicsAPI() = 0;
    virtual void idle() = 0;
    virtual bool initPhysicsScene(Scene& scene) = 0;
    virtual bool destroyPhysicsScene(const Scene& scene) = 0;

   
    virtual void frameStartedInternal( const U64 deltaTimeGameUS ) = 0;
    virtual void frameEndedInternal( const U64 deltaTimeGameUS ) = 0;

    virtual PhysicsAsset* createRigidActor(SceneGraphNode* node, RigidBodyComponent& parentComp) = 0;

    virtual bool intersect(const Ray& intersectionRay, float2 range, vector<SGNRayResult>& intersectionsOut) const = 0;

    void frameStarted(const U64 deltaTimeGameUS);
    void frameEnded(const U64 deltaTimeGameUS);

    void updateTimeStep(U8 simulationFrameRate, F32 simSpeed);

   protected:
       F32 _timeStepSec = 0.f;
       F32 _accumulatorSec = 0.f;
       U8  _simulationFrameRate = 1u;
};

FWD_DECLARE_MANAGED_CLASS(PhysicsAPIWrapper);

};  // namespace Divide

#endif //DVD_PHYSICS_API_WRAPPER_H_
