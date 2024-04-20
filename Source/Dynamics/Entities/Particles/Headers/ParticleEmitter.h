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
#ifndef DVD_PARTICLE_EMITTER_H_
#define DVD_PARTICLE_EMITTER_H_

#include "ParticleSource.h"
#include "ParticleUpdater.h"
#include "Graphs/Headers/SceneNode.h"

/// original source code:
/// https://github.com/fenbf/particles/blob/public/particlesCode
namespace Divide {

FWD_DECLARE_MANAGED_CLASS(Texture);
FWD_DECLARE_MANAGED_CLASS(GenericVertexData);

/// A Particle emitter scene node. Nothing smarter to say, sorry :"> - Ionut
class ParticleEmitter final : public SceneNode {
   public:
    explicit ParticleEmitter(GFXDevice& context, ResourceCache* parentCache, size_t descriptorHash, const std::string_view name);
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

    [[nodiscard]] bool updateData();
    [[nodiscard]] bool initData(const std::shared_ptr<ParticleData>& particleData);

    /// SceneNode concrete implementations
    bool unload() override;

    void addUpdater(const std::shared_ptr<ParticleUpdater>& updater) {
        _updaters.push_back(updater);
    }

    void addSource(const std::shared_ptr<ParticleSource>& source) {
        _sources.push_back(source);
    }

    [[nodiscard]] U32 getAliveParticleCount() const noexcept;

   protected:

    /// preprocess particles here
    void sceneUpdate(U64 deltaTimeUS,
                     SceneGraphNode* sgn,
                     SceneState& sceneState) override;

    void buildDrawCommands(SceneGraphNode* sgn, GenericDrawCommandContainer& cmdsOut) override;

    [[nodiscard]] GenericVertexData& getDataBuffer(RenderStage stage, PlayerIndex idx);

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "ParticleEmitter"; }

   private:
    static constexpr U8 s_MaxPlayerBuffers = 4;

    GFXDevice& _context;
    std::shared_ptr<ParticleData> _particles;

    vector<std::shared_ptr<ParticleSource>> _sources;
    vector<std::shared_ptr<ParticleUpdater>> _updaters;

    /// create particles
    bool _enabled = false;
    /// draw the impostor?
    bool _drawImpostor = false;

    using BuffersPerStage = std::array<GenericVertexData_ptr, to_base(RenderStage::COUNT)>;
    using BuffersPerPlayer = std::array<BuffersPerStage, s_MaxPlayerBuffers>;
    BuffersPerPlayer _particleGPUBuffers{};
    std::array<bool, to_base(RenderStage::COUNT)> _buffersDirty{};

    Task* _bufferUpdate = nullptr;
    Task* _bbUpdate = nullptr;
    Texture_ptr _particleTexture = nullptr;
};

TYPEDEF_SMART_POINTERS_FOR_TYPE(ParticleEmitter);

}  // namespace Divide

#endif //DVD_PARTICLE_EMITTER_H_
