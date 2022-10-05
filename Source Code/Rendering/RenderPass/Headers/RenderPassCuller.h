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
#ifndef _RENDER_PASS_CULLER_H_
#define _RENDER_PASS_CULLER_H_

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Headers/ClipPlanes.h"

/// This class performs all the necessary visibility checks on the scene's
/// SceneGraph to decide what get's rendered and what not
namespace Divide {

class SceneState;
class SceneRenderState;

struct Task;
class Camera;
class SceneNode;
class SceneGraph;
class SceneGraphNode;
class PlatformContext;
enum class RenderStage : U8;

enum class CullOptions : U16 {
    CULL_STATIC_NODES = toBit(1),
    CULL_DYNAMIC_NODES = toBit(2),
    CULL_AGAINST_CLIPPING_PLANES = toBit(3),
    CULL_AGAINST_FRUSTUM = toBit(4),
    CULL_AGAINST_LOD = toBit(5),
    KEEP_SKY_NODES = toBit(6), //Even if we cull agains frustum, lods, dynamic/static, etc, we always render at least the sky
    DEFAULT_CULL_OPTIONS = CULL_AGAINST_CLIPPING_PLANES | CULL_AGAINST_FRUSTUM | CULL_AGAINST_LOD
};

class Frustum;
struct GUIDList {
    I64* _guids = nullptr;
    size_t _count = 0u;
};

struct NodeCullParams {
    FrustumClipPlanes _clippingPlanes;
    vec4<U16> _lodThresholds = {1000u};
    vec3<F32> _minExtents = { 0.0f };
    GUIDList _ignoredGUIDS = {};
    vec3<F32> _cameraEyePos;
    const Frustum* _frustum = nullptr;
    F32 _cullMaxDistance = F32_MAX;
    I32 _maxLoD = -1;
    RenderStage _stage = RenderStage::COUNT;
};

struct VisibleNode {
    SceneGraphNode* _node = nullptr;
    F32 _distanceToCameraSq = 0.f;
    bool _materialReady = true;
};

using FeedBackContainer = vector_fast<VisibleNode>;

template<typename T = VisibleNode, size_t N = Config::MAX_VISIBLE_NODES>
struct VisibleNodeList
{
    using Container = std::array<T, N>;

    void append(const VisibleNodeList& other) noexcept
    {
        assert(_index + other._index < _nodes.size());

        std::memcpy(_nodes.data() + _index, other._nodes.data(), other._index * sizeof(T));
        _index += other._index;
    }

    void append(const T& node) noexcept
    {
        _nodes[_index.fetch_add(1)] = node;
    }

    void remove(const size_t idx)
    {
        const size_t lastPos = _index.fetch_sub(1);
        assert(idx <= lastPos);
        _nodes[idx] = _nodes[lastPos - 1];
        _nodes[lastPos - 1] = {};
    }

                  void      reset()       noexcept { _index.store(0); }
    [[nodiscard]] size_t    size()  const noexcept { return _index.load(); }
    [[nodiscard]] bufferPtr data()  const noexcept { return (bufferPtr)_nodes.data(); }

    [[nodiscard]] const T& node(const size_t idx) const noexcept
    { 
        assert(idx < _index.load());
        return _nodes[idx]; 
    }

    [[nodiscard]] T& node(const size_t idx) noexcept
    {
        assert(idx < _index.load());
        return _nodes[idx];
    }

    VisibleNodeList& operator=(const VisibleNodeList& other)
    {
        reset();
        append(other);
        return *this;
    }

private:
    Container _nodes;
    std::atomic_size_t _index = 0;
};

struct RenderPassCuller {
    enum class EntityFilter : U8
    {
        PRIMITIVES = toBit( 0 ),
        MESHES = toBit( 1 ),
        TERRAIN = toBit( 2 ),
        VEGETATION = toBit( 3 ),
        WATER = toBit( 4 ),
        SKY = toBit( 5 ),
        PARTICLES = toBit( 6 ),
        DECALS = toBit( 7 ),
        COUNT = 8
    };

    static void FrustumCull(const NodeCullParams& params, U16 cullFlags, const SceneGraph& sceneGraph, const SceneState& sceneState, PlatformContext& context, VisibleNodeList<>& nodesOut);
    static void FrustumCull(const PlatformContext& context, const NodeCullParams& params, const U16 cullFlags, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut);
    static void ToVisibleNodes(const Camera* camera, const vector<SceneGraphNode*>& nodes, VisibleNodeList<>& nodesOut);

private:
    static [[nodiscard]] U32 FilterMask( const PlatformContext& context ) noexcept;

    static void PostCullNodes(const PlatformContext& context, const NodeCullParams& params, U16 cullFlags, U32 filterMask, VisibleNodeList<>& nodesInOut);
    static void FrustumCullNode(SceneGraphNode* currentNode, const NodeCullParams& params, U16 cullFlags, U8 recursionLevel, VisibleNodeList<>& nodes);
};

}  // namespace Divide
#endif
