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
#ifndef DVD_AI_VISUAL_SENSOR_H_
#define DVD_AI_VISUAL_SENSOR_H_

#include "Sensor.h"

namespace Divide {

FWD_DECLARE_MANAGED_CLASS(SceneGraphNode);

namespace AI {

/// SGN GUID, SGN pointer
using NodeContainer = hashMap<I64, SceneGraphNode*>;
/// Container ID, NodeContainer
using NodeContainerMap = hashMap<U32, NodeContainer>;
/// SGN GUID, Last position
using NodePositions = hashMap<I64, float3 >;
/// Container ID, NodePositions
using NodePositionsMap = hashMap<U32, NodePositions>;

class VisualSensor final : public Sensor
{
   public:
    explicit VisualSensor(AIEntity* parentEntity);
     ~VisualSensor() override;

    void update(U64 deltaTimeUS) override;

    void followSceneGraphNode(U32 containerID, SceneGraphNode* node);
    void unfollowSceneGraphNode(U32 containerID, I64 nodeGUID);

    bool getDistanceToNodeSq(U32 containerID, I64 nodeGUID, F32& distanceOut);

    F32 getDistanceToNode(const U32 containerID, const I64 nodeGUID)
    {
        F32 distanceSq = F32_MAX;
        if (getDistanceToNodeSq(containerID, nodeGUID, distanceSq))
        {
            return Sqrt<F32>(distanceSq);
        }

        return distanceSq;
    }

    float3 getNodePosition(U32 containerID, I64 nodeGUID);
    SceneGraphNode* findClosestNode(U32 containerID);


   protected:
    NodeContainerMap _nodeContainerMap;
    NodePositionsMap _nodePositionsMap;
};

}  // namespace AI
}  // namespace Divide

#endif //DVD_AI_VISUAL_SENSOR_H_
