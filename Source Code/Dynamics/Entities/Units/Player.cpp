#include "stdafx.h"

#include "Headers/Player.h"

#include "Graphs/Headers/SceneNode.h"
#include "Graphs/Headers/SceneGraphNode.h"

#include "Core/Headers/StringHelper.h"
#include "Rendering/Camera/Headers/Camera.h"
#include "Core/Math/BoundingVolumes/Headers/BoundingBox.h"

namespace Divide {

Player::Player(const U8 index)
    : Character(CharacterType::CHARACTER_TYPE_PLAYER),
      _index(index)
{
     _camera = Camera::CreateCamera(Util::StringFormat("Player_Cam_%d", _index), Camera::Mode::FREE_FLY);
}

Player::~Player()
{
    Camera::DestroyCamera(_camera);
}

void Player::setParentNode(SceneGraphNode* node) {
    Character::setParentNode(node);
    if (node != nullptr) {
        Attorney::SceneNodePlayer::setBounds(node->getNode(), BoundingBox{ -0.5f, -0.5f , -0.5f , 0.5f, 0.5f, 0.5f });
    }
}

}