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
#ifndef _PINGPONG_SCENE_H
#define _PINGPONG_SCENE_H

#include "Scenes/Headers/Scene.h"

namespace Divide
{

    class Sphere3D;

    BEGIN_SCENE( PingPongScene )
        explicit PingPongScene( PlatformContext& context, ResourceCache* cache, SceneManager& parent, const Str256& name );

    ~PingPongScene()
    {
    }

    bool load() override;
    void postLoadMainThread() override;
    void processInput( PlayerIndex idx, U64 gameDeltaTimeUS, U64 appDeltaTimeUS ) override;
    U16  registerInputActions() override;

    private:
    void test( std::any a, PushConstantType type );
    void serveBall();
    void resetGame();

    private:
    I8 _score;
    vector<string> _quotes;
    std::shared_ptr<Sphere3D> _ball;
    SceneGraphNode* _ballSGN = nullptr;
    Camera* _paddleCam;

    private:  // Game stuff:
    bool _directionTowardsAdversary;
    bool _upwardsDirection;
    bool _touchedOwnTableHalf;
    bool _touchedAdversaryTableHalf;
    bool _lost;
    bool _freeFly;
    bool _wasInFreeFly;
    F32 _sideDrift;
    END_SCENE( PingPongScene )

}  // namespace Divide

#endif