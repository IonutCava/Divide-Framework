#include "stdafx.h"

#include "Headers/Player.h"

#include "Core/Headers/StringHelper.h"
#include "Graphs/Headers/SceneGraphNode.h"
#include "Rendering/Camera/Headers/FreeFlyCamera.h"

namespace Divide {

Player::Player(const U8 index)
    : Character(CharacterType::CHARACTER_TYPE_PLAYER),
      _index(index)
{
     _camera = Camera::createCamera<FreeFlyCamera>(Util::StringFormat("Player_Cam_%d", _index));
}

Player::~Player()
{
    Camera::destroyCamera(_camera);
}

void Player::setParentNode(SceneGraphNode* node) {
    Character::setParentNode(node);
    if (node != nullptr) {
        BoundingBox bb;
        bb.setMin(-0.5f);
        bb.setMax(0.5f);
        Attorney::SceneNodePlayer::setBounds(node->getNode(), bb);
    }
}

}