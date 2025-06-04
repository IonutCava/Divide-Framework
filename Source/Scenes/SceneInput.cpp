

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
    }


    void SceneInput::onSetActive()
    {

    }

    void SceneInput::onRemoveActive()
    {

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

    U8 SceneInput::getPlayerIndexForDevice( const U8 deviceIndex ) const
    {
        return deviceIndex;
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

    bool SceneInput::onKeyDownInternal( Input::KeyEvent& argInOut)
    {
        if ( g_recordInput )
        {
            _keyLog[argInOut._deviceIndex].emplace_back(
                KeyLogState{
                    argInOut._key,
                    Input::InputState::PRESSED
                }
            );
        }

        PressReleaseActionCbks cbks;
        if ( getKeyMapping(argInOut._key, cbks ) )
        {
            return handleCallbacks( cbks,
                                    InputParams(argInOut._deviceIndex,
                                                 to_base(argInOut._key),
                                                 {{
                                                    argInOut._modMask,
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1,
                                                    -1
                                                }}),
                                    true );
        }

        return false;
    }

    bool SceneInput::onKeyUpInternal( Input::KeyEvent& argInOut)
    {
        if ( g_recordInput )
        {
            _keyLog[argInOut._deviceIndex].emplace_back(
                KeyLogState{
                    argInOut._key,
                    Input::InputState::RELEASED
                }
            );
        }

        PressReleaseActionCbks cbks;
        if ( getKeyMapping(argInOut._key, cbks ) )
        {
            return handleCallbacks( cbks,
                                    InputParams(argInOut._deviceIndex,
                                                 to_base(argInOut._key ),
                                                 {{
                                                     argInOut._modMask,
                                                     -1,
                                                     -1,
                                                     -1,
                                                     -1,
                                                     -1
                                                }}),
                                    false );
        }

        return false;
    }

    bool SceneInput::joystickButtonPressedInternal( Input::JoystickEvent& argInOut)
    {
        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);
        PressReleaseActionCbks cbks;


        if ( getJoystickMapping( joy, argInOut._element._type, argInOut._element._elementIndex, cbks ) )
        {
            return handleCallbacks( cbks, InputParams(argInOut._deviceIndex, argInOut._element._elementIndex), true );
        }

        return false;
    }

    bool SceneInput::joystickButtonReleasedInternal( Input::JoystickEvent& argInOut)
    {
        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if ( getJoystickMapping( joy, argInOut._element._type, argInOut._element._elementIndex, cbks ) )
        {
            return handleCallbacks( cbks, InputParams(argInOut._deviceIndex, argInOut._element._elementIndex ), false );
        }

        return false;
    }

    bool SceneInput::joystickAxisMovedInternal( Input::JoystickEvent& argInOut)
    {
        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if ( getJoystickMapping( joy, argInOut._element._type, argInOut._element._elementIndex, cbks ) )
        {
            const InputParams params( argInOut._deviceIndex,
                                      argInOut._element._elementIndex, // axis index
                                      {{
                                        argInOut._element._data._gamePad ? 1 : 0, // is gamepad
                                        argInOut._element._data._deadZone, // dead zone
                                        argInOut._element._data._dataSigned, // move value
                                        -1, // not used
                                        -1, // not used
                                        -1 // not used
                                      }});
            return handleCallbacks( cbks, params, true );
        }

        return false;
    }

    bool SceneInput::joystickPovMovedInternal( Input::JoystickEvent& argInOut)
    {
        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if ( getJoystickMapping( joy, argInOut._element._type, argInOut._element._elementIndex, cbks ) )
        {
            const InputParams params( argInOut._deviceIndex,
                                      argInOut._element._elementIndex,
                                      {{
                                          to_I32(argInOut._element._data._data), //explicit cast for reference
                                          -1,
                                          -1,
                                          -1,
                                          -1,
                                          -1
                                      }});
            return handleCallbacks( cbks, params, true );
        }

        return false;
    }

    bool SceneInput::joystickBallMovedInternal( Input::JoystickEvent& argInOut)
    {
        const Input::Joystick joy = static_cast<Input::Joystick>(argInOut._deviceIndex);

        PressReleaseActionCbks cbks;
        if ( getJoystickMapping( joy, argInOut._element._type, argInOut._element._elementIndex, cbks ) )
        {
            const InputParams params( argInOut._deviceIndex,
                                      argInOut._element._elementIndex,
                                      {{
                                          argInOut._element._data._gamePad ? 1 : 0,
                                          argInOut._element._data._smallDataSigned[0],
                                          argInOut._element._data._smallDataSigned[1],
                                          -1,
                                          -1,
                                          -1
                                      }});
            return handleCallbacks( cbks, params, true );
        }

        return false;
    }

    bool SceneInput::joystickAddRemoveInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
    {
        return false;
    }

    bool SceneInput::joystickRemapInternal( [[maybe_unused]] Input::JoystickEvent& argInOut)
    {
        return false;
    }

    bool SceneInput::mouseMovedInternal( Input::MouseMoveEvent& argInOut)
    {
        const PlayerIndex idx = getPlayerIndexForDevice(argInOut._deviceIndex );
        SceneStatePerPlayer& state = _parentScene.state()->playerState( idx );

        if (argInOut._wheelEvent )
        {
            const I32 wheel = argInOut.state().VWheel;
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

    bool SceneInput::mouseButtonPressedInternal( Input::MouseButtonEvent& argInOut)
    {
        if ( g_recordInput )
        {
            _mouseBtnLog[argInOut._deviceIndex].emplace_back(
                MouseLogState{
                    ._position = {argInOut.state().X.abs, argInOut.state().Y.abs},
                    ._button = argInOut.button(),
                    ._state = Input::InputState::PRESSED
                }
            );
        }

        PressReleaseActionCbks cbks = {};
        if ( getMouseMapping(argInOut.button(), cbks ) )
        {
            InputParams params( argInOut._deviceIndex,
                                to_base(argInOut.button() ),
                                {{
                                    argInOut.state().X.abs,
                                    argInOut.state().Y.abs,
                                    -1,
                                    -1,
                                    -1,
                                    -1
                                }});

            return handleCallbacks( cbks, params, true );
        }

        return false;
    }

    bool SceneInput::mouseButtonReleasedInternal( Input::MouseButtonEvent& argInOut)
    {
        if ( g_recordInput )
        {
            _mouseBtnLog[argInOut._deviceIndex].emplace_back(
                MouseLogState{
                    ._position = {argInOut.state().X.abs, argInOut.state().Y.abs},
                    ._button = argInOut.button(),
                    ._state = Input::InputState::RELEASED
                }
            );
        }

        PressReleaseActionCbks cbks = {};
        if ( getMouseMapping( argInOut.button(), cbks ) )
        {
            InputParams params( argInOut._deviceIndex, 
                                to_base(argInOut.button() ),
                                {{
                                    argInOut.state().X.abs,
                                    argInOut.state().Y.abs,
                                    -1,
                                    -1,
                                    -1,
                                    -1
                                }});
            return handleCallbacks( cbks, params, false );
        }

        return false;
    }

    bool SceneInput::onTextInputInternal( [[maybe_unused]] Input::TextInputEvent& argInOut)
    {
        if ( g_recordInput )
        {
           NOP();
        }

        return false;
    }

    bool SceneInput::onTextEditInternal( [[maybe_unused]] Input::TextEditEvent& argInOut )
    {
        if (g_recordInput)
        {
            NOP();
        }

        return false;
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
