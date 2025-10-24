

#include "Headers/Scene.h"
#include "Headers/SceneInput.h"

#include "Core/Headers/PlatformContext.h"

namespace Divide
{

    namespace
    {
        constexpr bool g_recordInput = true;
    }

    void PressReleaseActionCbks::from( const PressReleaseActions& actions, const InputActionList& actionList )
    {

        _entries.reserve( actions.entries().size() );
        for ( const PressReleaseActions::Entry& entry : actions.entries() )
        {
            Entry& newEntry = _entries.emplace_back();
            newEntry._actions[to_base( PressReleaseActions::Action::PRESS )].reserve( entry.pressIDs().size() );
            newEntry._actions[to_base( PressReleaseActions::Action::RELEASE )].reserve( entry.releaseIDs().size() );
            newEntry._modifiers = entry.modifiers();

            for ( const U16 id : entry.pressIDs() )
            {
                newEntry._actions[to_base( PressReleaseActions::Action::PRESS )].push_back( actionList.getInputAction( id )._action );
            }
            for ( const U16 id : entry.releaseIDs() )
            {
                newEntry._actions[to_base( PressReleaseActions::Action::RELEASE )].push_back( actionList.getInputAction( id )._action );
            }
        }
    }

    SceneInput::SceneInput( Scene& parentScene )
        : _parentScene( parentScene )
    {
        int nMice = 0, nKeyboards = 0, nGamepads = 0, nJoysticks = 0;

        if (SDL_MouseID* mice = SDL_GetMice(&nMice); mice != nullptr && nMice > 0)
        {
            for ( int i = 0; i < nMice && i < Config::MAX_LOCAL_PLAYER_COUNT; ++i )
            {
                //_playerDeviceMap[to_base(Input::InputDeviceType::MOUSE)][i] = mice[i];
            }
            SDL_free(mice);
        }
        if (SDL_KeyboardID* keyboards = SDL_GetKeyboards(&nKeyboards); keyboards != nullptr && nKeyboards > 0)
        {
            for (int i = 0; i < nKeyboards && i < Config::MAX_LOCAL_PLAYER_COUNT; ++i)
            {
                //_playerDeviceMap[to_base(Input::InputDeviceType::KEYBOARD)][i] = keyboards[i];
            }
            SDL_free(keyboards);
        }

        if (SDL_JoystickID* gamepads = SDL_GetGamepads(&nGamepads); gamepads != nullptr && nGamepads > 0)
        {
            for (int i = 0; i < nGamepads && i < Config::MAX_LOCAL_PLAYER_COUNT; ++i)
            {
                //_playerDeviceMap[to_base(Input::InputDeviceType::GAMEPAD)][i] = gamepads[i];
            }
            SDL_free(gamepads);
        }
        if (SDL_JoystickID* joysticks = SDL_GetJoysticks(&nJoysticks); joysticks != nullptr && nJoysticks > 0)
        {
            for (int i = 0; i < nJoysticks && i < Config::MAX_LOCAL_PLAYER_COUNT; ++i)
            {
                //_playerDeviceMap[to_base(Input::InputDeviceType::JOYSTICK)][i] = joysticks[i];
            }
            SDL_free(joysticks);
        }

        for ( auto& players : _playerDeviceMap )
        {
            players.fill(Input::InputEvent::INVALID_DEVICE_INDEX);
        }
    }


    void SceneInput::onSetActive()
    {
        processInput(true);
    }

    void SceneInput::onRemoveActive()
    {
        processInput(false);
    }

    void SceneInput::onPlayerAdd( const U8 index )
    {
        insert( _keyLog, index, KeyLog() );
        insert( _mouseBtnLog, index, MouseBtnLog() );
    }

    void SceneInput::onPlayerRemove( const U8 index )
    {
        _keyLog.find( index )->second.clear();
        _mouseBtnLog.find( index )->second.clear();
    }

    PlayerIndex SceneInput::getPlayerIndexForDevice( const Input::InputDeviceType deviceType, const U32 deviceIndex ) const
    {
        if (deviceIndex != 0 && deviceIndex != Input::InputEvent::INVALID_DEVICE_INDEX)
        {
            DIVIDE_ASSERT(deviceType != Input::InputDeviceType::COUNT);
            const PlayerIdMap& idMap = _playerDeviceMap[to_base(deviceType)];

            for (U8 playerIdx = 0u; playerIdx < Config::MAX_LOCAL_PLAYER_COUNT; ++playerIdx)
            {
                if (idMap[playerIdx] == deviceIndex)
                {
                    return static_cast<PlayerIndex>(playerIdx);
                }
            }
        }

        return static_cast<PlayerIndex>(0);
    }

    bool SceneInput::handleCallbacks( const PressReleaseActionCbks& cbks,
                                      const InputParams& params,
                                      const bool onPress )
    {
        for ( const PressReleaseActionCbks::Entry& entry : cbks._entries )
        {
            //Check modifiers
            for ( const Input::KeyCode modifier : entry._modifiers )
            {
                if ( GetKeyState( params._deviceIndex, modifier ) != Input::InputState::PRESSED )
                {
                    goto next;
                }
            }

            {
                const auto& actions = entry._actions[to_base( onPress ? PressReleaseActions::Action::PRESS
                                                                      : PressReleaseActions::Action::RELEASE )];
                if ( !actions.empty() )
                {
                    for ( const auto& action : actions )
                    {
                        action( params );
                    }

                    return true;
                }
            }

        next:
            //Failed modifier test
            NOP();
        }

        return false;
    }

    bool SceneInput::onKeyInternal( Input::KeyEvent& argInOut)
    {
        onInputEvent(argInOut);

        if ( g_recordInput )
        {
            _keyLog[argInOut._deviceIndex].emplace_back(
                KeyLogState{
                    argInOut._key,
                    argInOut._state
                }
            );
        }

        PressReleaseActionCbks cbks;
        if ( getKeyMapping(argInOut._key, cbks ) )
        {
            InputParams params(argInOut._deviceType,
                               argInOut._deviceIndex,
                               to_base(argInOut._key));
            params._modMask = argInOut._modMask;

            return handleCallbacks( cbks, params, argInOut._state == Input::InputState::PRESSED );
        }

        return false;
    }

    bool SceneInput::onMouseMovedInternal( Input::MouseMoveEvent& argInOut)
    {
        onInputEvent(argInOut);

        const PlayerIndex idx = getPlayerIndexForDevice( argInOut._deviceType, argInOut._deviceIndex );
        SceneStatePerPlayer& state = _parentScene.state()->playerState( idx );

        if (argInOut._wheelEvent )
        {
            const I32 wheel = argInOut.state().Wheel.yTicks;
            if ( wheel == 0 )
            {
                state._zoom.reset();
            }
            else
            {
                const U8 speed = std::abs(wheel) > 1 ? 255u : 128u;
                state._zoom.push( { speed, wheel > 0 ? MoveDirection::POSITIVE : MoveDirection::NEGATIVE} );
            } 
        }
        else if ( state.cameraLockedToMouse() )
        {
            const I32 xRel = argInOut.state().X.rel;
            const I32 yRel = argInOut.state().Y.rel;

            if ( xRel == 0 )
            {
                state._angleLR.reset();
            }
            else
            {
                const U8 speed = std::abs(xRel) > 1 ? 255u : 192u;
                state._angleLR.push( {speed, xRel > 0 ? MoveDirection::POSITIVE : MoveDirection::NEGATIVE} );
            }

            if ( yRel == 0 )
            {
                state._angleUD.reset();
            }
            else
            {
                const U8 speed = std::abs( yRel ) > 1 ? 255u : 192u;
                state._angleUD.push( {speed, yRel > 0 ? MoveDirection::POSITIVE : MoveDirection::NEGATIVE} );
            }
        }

        return Attorney::SceneInput::mouseMoved( &_parentScene, argInOut);
    }

    bool SceneInput::onMouseButtonInternal( Input::MouseButtonEvent& argInOut)
    {
        onInputEvent(argInOut);

        if ( g_recordInput )
        {
            _mouseBtnLog[argInOut._deviceIndex].emplace_back(
                MouseLogState{
                    ._position = {argInOut.state().X.abs, argInOut.state().Y.abs},
                    ._button = argInOut.button(),
                    ._state = argInOut.pressedState()
                }
            );
        }

        PressReleaseActionCbks cbks = {};
        if ( getMouseMapping(argInOut.button(), cbks ) )
        {
            InputParams params(argInOut._deviceType,
                               argInOut._deviceIndex,
                               to_base(argInOut.button()));

            params._coords =
            {
                argInOut.state().X.abs,
                argInOut.state().Y.abs
            };

            return handleCallbacks( cbks, params, argInOut.pressedState() == Input::InputState::PRESSED );
        }

        return false;
    }

    bool SceneInput::onJoystickButtonInternal(Input::JoystickEvent& argInOut)
    {
        onInputEvent(argInOut);

        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);
        PressReleaseActionCbks cbks;

        if (getJoystickMapping(joy, argInOut._type, argInOut._elementIndex, cbks))
        {
            return handleCallbacks(cbks, InputParams(argInOut._deviceType, argInOut._deviceIndex, argInOut._elementIndex), argInOut._state == Input::InputState::PRESSED);
        }

        return false;
    }

    bool SceneInput::onJoystickAxisMovedInternal(Input::JoystickEvent& argInOut)
    {
        onInputEvent(argInOut);

        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if (getJoystickMapping(joy, argInOut._type, argInOut._elementIndex, cbks))
        {
            InputParams params(argInOut._deviceType,
                               argInOut._deviceIndex,
                               argInOut._elementIndex);

            params._signedData[0] = argInOut._axisMovement[0]; // value
            params._signedData[1] = argInOut._axisMovement[1]; // deadzone

            return handleCallbacks(cbks, params, true);
        }

        return false;
    }

    bool SceneInput::onJoystickPovMovedInternal(Input::JoystickEvent& argInOut)
    {
        onInputEvent(argInOut);

        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if (getJoystickMapping(joy, argInOut._type, argInOut._elementIndex, cbks))
        {
            InputParams params(argInOut._deviceType,
                               argInOut._deviceIndex,
                               argInOut._elementIndex);
            params._povMask = argInOut._povMask;
            return handleCallbacks(cbks, params, true);
        }

        return false;
    }

    bool SceneInput::onJoystickBallMovedInternal( Input::JoystickEvent& argInOut )
    {
        onInputEvent(argInOut);

        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if (getJoystickMapping(joy, argInOut._type, argInOut._elementIndex, cbks))
        {
            InputParams params(argInOut._deviceType,
                               argInOut._deviceIndex,
                               argInOut._elementIndex);
            params._signedData[0] = argInOut._ballRelMovement[0]; //xRel
            params._signedData[1] = argInOut._ballRelMovement[1]; //yRel

            return handleCallbacks(cbks, params, true);
        }

        return false;
    }

    bool SceneInput::onJoystickRemapInternal( Input::JoystickEvent& argInOut )
    {
        onInputEvent(argInOut);
        return false;
    }

    bool SceneInput::onTextInputInternal( Input::TextInputEvent& argInOut )
    {
        onInputEvent(argInOut);

        if ( g_recordInput )
        {
           NOP();
        }

        return false;
    }

    bool SceneInput::onTextEditInternal( Input::TextEditEvent& argInOut )
    {
        onInputEvent(argInOut);

        if (g_recordInput)
        {
            NOP();
        }

        return false;
    }

    bool SceneInput::onDeviceAddOrRemoveInternal(Input::InputEvent& argInOut)
    {
        return onDeviceAddOrRemoveInternal(argInOut, false);
    }

    bool SceneInput::onDeviceAddOrRemoveInternal(const Input::InputEvent& argInOut, bool force)
    {
        PlayerIdMap& idMap = _playerDeviceMap[to_base(argInOut._deviceType)];

        if ( force || argInOut._eventType == Input::InputEventType::DEVICE_ADDED)
        {
            for (U8 playerIdx = 0u; playerIdx < Config::MAX_LOCAL_PLAYER_COUNT; ++playerIdx)
            {
                if (idMap[playerIdx] == Input::InputEvent::INVALID_DEVICE_INDEX)
                {
                    idMap[playerIdx] = argInOut._deviceIndex;
                    return true;
                }
            }
        }
        else if (argInOut._eventType == Input::InputEventType::DEVICE_REMOVED)
        {
            for (U8 playerIdx = 0u; playerIdx < Config::MAX_LOCAL_PLAYER_COUNT; ++playerIdx)
            {
                if (idMap[playerIdx] == argInOut._deviceIndex )
                {
                    idMap[playerIdx] = Input::InputEvent::INVALID_DEVICE_INDEX;
                    return true;
                }
            }
        }

        return false;
    }

    void SceneInput::onInputEvent(const Input::InputEvent& event)
    {
        if ( event._deviceType != Input::InputDeviceType::MOUSE || event._deviceIndex > 0 )
        {
            onDeviceAddOrRemoveInternal(event, true);
        }
    }

    bool SceneInput::addKeyMapping( const Input::KeyCode key, const PressReleaseActions::Entry& keyCbks )
    {
        return _keyMap[key].add( keyCbks );
    }

    bool SceneInput::removeKeyMapping( const Input::KeyCode key )
    {
        const KeyMap::iterator it = _keyMap.find( key );
        if ( it != std::end( _keyMap ) )
        {
            _keyMap.erase( it );
            return false;
        }

        return false;
    }

    bool SceneInput::getKeyMapping( const Input::KeyCode key, PressReleaseActionCbks& keyCbksOut )
    {
        const KeyMapCache::const_iterator itCache = _keyMapCache.find( key );
        if ( itCache != std::cend(_keyMapCache) )
        {
            keyCbksOut = itCache->second;
            return true;
        }

        const KeyMap::const_iterator it = _keyMap.find( key );
        if ( it != std::cend( _keyMap ) )
        {
            const PressReleaseActions& actions = it->second;
            keyCbksOut.from( actions, _actionList );
            insert( _keyMapCache, key, keyCbksOut );

            return true;
        }

        return false;
    }

    bool SceneInput::addMouseMapping( const Input::MouseButton button, const PressReleaseActions::Entry& btnCbks )
    {
        return _mouseMap[button].add( btnCbks );
    }

    bool SceneInput::removeMouseMapping( const Input::MouseButton button )
    {
        const MouseMap::iterator it = _mouseMap.find( button );
        if ( it != std::end( _mouseMap ) )
        {
            _mouseMap.erase( it );
            return false;
        }

        return false;
    }

    bool SceneInput::getMouseMapping( const Input::MouseButton button, PressReleaseActionCbks& btnCbksOut )
    {
        const MouseMapCache::const_iterator itCache = _mouseMapCache.find( button );
        if ( itCache != std::cend( _mouseMapCache ) )
        {
            btnCbksOut = itCache->second;
            return true;
        }


        const MouseMap::const_iterator it = _mouseMap.find( button );
        if ( it != std::cend( _mouseMap ) )
        {
            const PressReleaseActions& actions = it->second;
            btnCbksOut.from( actions, _actionList );
            insert( _mouseMapCache, button, btnCbksOut );
            return true;
        }

        return false;
    }

    bool SceneInput::addJoystickMapping( const Input::Joystick device, const Input::JoystickElementType elementType, const U32 id, const PressReleaseActions::Entry& btnCbks )
    {
        const JoystickMapKey key{
            ._id = id,
            ._element = elementType
        };

        return _joystickMap[to_base( device )][key].add( btnCbks );
    }

    bool SceneInput::removeJoystickMapping( const Input::Joystick device, const Input::JoystickElementType elementType, const U32 id )
    {
        JoystickMapEntry& entry = _joystickMap[to_base( device )];

        const JoystickMapEntry::iterator it = entry.find(
            {
            ._id = id,
            ._element = elementType
            }
        );
        if ( it != std::end( entry ) )
        {
            entry.erase( it );
            return false;
        }

        return false;
    }

    bool SceneInput::getJoystickMapping( const Input::Joystick device, const Input::JoystickElementType elementType, U32 id, PressReleaseActionCbks& btnCbksOut )
    {
        JoystickMapCacheEntry& entry = _joystickMapCache[to_base( device )];

        const JoystickMapCacheEntry::const_iterator itCache = entry.find(
            {
                ._id = id,
                ._element = elementType
            } );
        if ( itCache != std::cend( entry ) )
        {
            btnCbksOut = itCache->second;
            return true;
        }

        JoystickMapEntry& entry2 = _joystickMap[to_base( device )];
        const JoystickMapEntry::const_iterator it = entry2.find(
            {
                ._id = id,
                ._element = elementType
            } );
        if ( it != std::cend( entry2 ) )
        {
            const PressReleaseActions& actions = it->second;
            btnCbksOut.from( actions, _actionList );
            entry[JoystickMapKey{ ._id = id, ._element = elementType }] = btnCbksOut;
            return true;
        }

        return false;
    }

    InputActionList& SceneInput::actionList() noexcept
    {
        return _actionList;
    }

    void SceneInput::flushCache()
    {
        _keyMapCache.clear();
        _mouseMapCache.clear();
        _joystickMapCache.clear();
    }
} //namespace Divide
