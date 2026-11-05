/*
   Copyright (c) 2018-2026 DIVIDE-Studio
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
#ifndef DVD_PARTICLE_EMITTER_H_
#define DVD_PARTICLE_EMITTER_H_

#include "ParticleSource.h"
#include "ParticleUpdater.h"
#include "Graphs/Headers/SceneNode.h"
#include "Platform/Threading/Headers/SharedMutex.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"
#include "Platform/Video/Headers/GenericDrawCommand.h"

/// original source code:
/// https://github.com/fenbf/particles/blob/public/particlesCode
namespace Divide {

class Texture;

/// A Particle emitter scene node. Nothing smarter to say, sorry :"> - Ionut
DEFINE_NODE_TYPE( ParticleEmitter, SceneNodeType::TYPE_PARTICLE_EMITTER )
{
   public:
    explicit ParticleEmitter( const ResourceDescriptor<ParticleEmitter>& descriptor );
    ~ParticleEmitter() override;

    void prepareRender(SceneGraphNode* sgn,
                       RenderingComponent& rComp,
                       RenderPackage& pkg,
                       GFX::MemoryBarrierCommand& postDrawMemCmd,
                       RenderStagePass renderStagePass,
                       const CameraSnapshot& cameraSnapshot,
                       bool refreshData) override;


    /// toggle the particle emitter on or off
    void enableEmitter(const bool state) noexcept { _enabled = state; }

    void setDrawImpostor(const bool state) noexcept { _drawImpostor = state; }

    [[nodiscard]] bool initData(const std::shared_ptr<ParticleData>& particleData);

    /// SceneNode concrete implementations
    bool load( PlatformContext& context ) override;
    bool unload() override;

    void addUpdater(const std::shared_ptr<ParticleUpdater>& updater) {
        _updaters.push_back(updater);
    }

    void addSource(const std::shared_ptr<ParticleSource>& source) {
        _sources.push_back(source);
    }

    [[nodiscard]] U32 getAliveParticleCount() const noexcept;
    [[nodiscard]] bool boundsInWorldSpace() const noexcept override { return true; }

   protected:
    using ParticleBufferHandles = std::array<GPUBuffer::Handle, 4u>; // geometry, position, colour, index
    struct ParticleUpload
    {
        GPUBuffer_ptr _positions;
        GPUBuffer_ptr _colours;
        ParticleBufferHandles _handles{};
        GenericDrawCommandContainer _commands;
        std::array<BufferLock, 2> _locks{};
        float3 _eye{};
        bool _uploaded = false;
    };

    struct ParticleFrameUploads
    {
        vector<std::unique_ptr<ParticleUpload>> _slots;
        U64 _frame = U64_MAX;
        size_t _used = 0u;
    };

    /// preprocess particles here
    void sceneUpdate(U64 deltaTimeUS,
                     SceneGraphNode* sgn,
                     SceneState& sceneState) override;

    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

    [[nodiscard]] ParticleUpload& getUpload(U64 frame, const float3& eye);

   private:
    friend struct ParticleEmitterTestAccess;
    std::shared_ptr<ParticleData> _particles;
    U32 _geometryIndexCount = 0u;

    vector<std::shared_ptr<ParticleSource>> _sources;
    vector<std::shared_ptr<ParticleUpdater>> _updaters;

    /// create particles
    bool _enabled = false;
    /// draw the impostor?
    bool _drawImpostor = false;

    GPUBuffer_ptr _geometryBuffer;
    GPUBuffer_ptr _indexBuffer;
    std::array<ParticleFrameUploads, Config::MAX_FRAMES_IN_FLIGHT> _frameUploads;
    Mutex _particleRenderLock;

};

TYPEDEF_SMART_POINTERS_FOR_TYPE(ParticleEmitter);

}  // namespace Divide

#endif //DVD_PARTICLE_EMITTER_H_
