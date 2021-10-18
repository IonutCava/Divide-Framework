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
#ifndef _WAR_SCENE_H
#define _WAR_SCENE_H

#include "Scenes/Headers/Scene.h"

namespace Divide {
class NPC;
class RigidBodyComponent;
class SkinnedSubMesh;

namespace AI {
class AITeam;
class AIEntity;
class WarSceneOrder;
}

BEGIN_SCENE(WarScene)
    explicit WarScene(PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name);
    ~WarScene();

    bool load(const Str256& name) override;
    bool unload() override;
    void postLoadMainThread() override;
    void processTasks(U64 deltaTimeUS) override;
    void processGUI(U64 deltaTimeUS) override;
    void updateSceneStateInternal(U64 deltaTimeUS) override;
    U16  registerInputActions() override;

    void registerPoint(U16 teamID, const string& unitName);
    void printMessage(U8 eventId, const string& unitName) const;
    void debugDraw(const Camera* activeCamera, RenderStagePass stagePass, GFX::CommandBuffer& bufferInOut) override;

   private:
    void onSetActive() override;
    void startSimulation(I64 btnGUID);
    void toggleCamera(InputParams param);
    bool removeUnits();
    bool addUnits();
    bool resetUnits();
    void checkGameCompletion();
    void weaponCollision(const RigidBodyComponent& collider);
    AI::AIEntity* findAI(SceneGraphNode* node);
    bool initializeAI(bool continueOnErrors);
    bool deinitializeAI(bool continueOnErrors);

    void toggleTerrainMode();

   private:
    GUIMessageBox* _infoBox = nullptr;
    vector<TransformComponent*> _lightNodeTransforms;
    vector<std::pair<SceneGraphNode*, bool>> _lightNodes2;
    vector<SceneGraphNode*> _lightNodes3;

   private:  // Game
    U32  _timeLimitMinutes;
    U32  _scoreLimit;
    U32  _runCount = 0;
    U64  _elapsedGameTime = 0;
    bool _sceneReady = false;
    bool _resetUnits = false;
    bool _terrainMode = false;
    U64 _lastNavMeshBuildTime = 0UL;
    /// NPC's are the actual game entities
    vector<SceneGraphNode*> _armyNPCs[2];
    IMPrimitive* _targetLines = nullptr;
    SceneGraphNode* _flag[2]{nullptr, nullptr};
    SceneGraphNode* _particleEmitter = nullptr;
    SceneGraphNode* _firstPersonWeapon = nullptr;
    /// Teams are factions for AIEntites so they can manage friend/foe situations
    AI::AITeam* _faction[2]{nullptr, nullptr};
END_SCENE(WarScene)

}  // namespace Divide

#endif