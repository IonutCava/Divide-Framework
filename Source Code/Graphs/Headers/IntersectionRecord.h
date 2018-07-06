/*
Copyright (c) 2016 DIVIDE-Studio
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

#ifndef _OCTREE_INTERSECTION_RECORD_H_
#define _OCTREE_INTERSECTION_RECORD_H_

#include "Core/Math/Headers/Ray.h"
#include "Core/Math/Headers/MathVectors.h"

namespace Divide {

class Octree;
class SceneGraphNode;
typedef std::weak_ptr<SceneGraphNode> SceneGraphNode_wptr;

class IntersectionRecord
{
  public:
    IntersectionRecord();
    IntersectionRecord(const vec3<F32>& hitPos,
                       const vec3<F32>& hitNormal,
                       const Ray& ray,
                       D32 distance);
    /// Creates a new intersection record indicating whether there was a hit or not and the object which was hit.
    IntersectionRecord(SceneGraphNode_wptr hitObject);

    /// This is the exact point in 3D space which has an intersection.
    vec3<F32> _position;
    /// This is the normal of the surface at the point of intersection
    vec3<F32> _normal;
    /// This is the ray which caused the intersection
    Ray _ray;
    /// This is the object which is being intersected
    SceneGraphNode_wptr _intersectedObject1;
    /// This is the other object being intersected (may be null, as in the case of a ray-object intersection)
    SceneGraphNode_wptr _intersectedObject2;

    /// this is a reference to the current node within the octree for where the collision occurred. In some cases, the collision handler
    /// will want to be able to spawn new objects and insert them into the tree. This node is a good starting place for inserting these objects
    /// since it is a very near approximation to where we want to be in the tree.
    std::shared_ptr<const Octree> _treeNode;
    /// This is the distance from the ray to the intersection point. 
    /// You'll usually want to use the nearest collision point if you get multiple intersections.
    D32 _distance;

    bool _hasHit;

    /// check the object identities between the two intersection records. If they match in either order, we have a duplicate.
    bool operator==(const IntersectionRecord& otherRecord);

    inline bool isEmpty() const {
        return !_intersectedObject1.lock() &&
               !_intersectedObject2.lock();
    }
};
}; //namespace Divide
#endif //_OCTREE_INTERSECTION_RECORD_H_