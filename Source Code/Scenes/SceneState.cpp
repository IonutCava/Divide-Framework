#include "stdafx.h"

#include "Headers/SceneState.h"
#include "Platform/Video/Headers/RenderStagePass.h"

namespace Divide
{
    [[nodiscard]] bool operator==( const WaterBodyData& lhs, const WaterBodyData& rhs ) noexcept
    {
        return lhs._positionW == rhs._positionW &&
            lhs._extents == rhs._extents;
    }

    [[nodiscard]] bool operator!=( const WaterBodyData& lhs, const WaterBodyData& rhs ) noexcept
    {
        return lhs._positionW != rhs._positionW ||
            lhs._extents != rhs._extents;
    }

    [[nodiscard]] bool operator==( const FogDetails& lhs, const FogDetails& rhs ) noexcept
    {
        return lhs._colourAndDensity == rhs._colourAndDensity &&
            lhs._colourSunScatter == rhs._colourSunScatter;
    }

    [[nodiscard]] bool operator!=( const FogDetails& lhs, const FogDetails& rhs ) noexcept
    {
        return lhs._colourAndDensity != rhs._colourAndDensity ||
            lhs._colourSunScatter != rhs._colourSunScatter;
    }

    void SceneStatePerPlayer::resetMoveDirections( const bool keepMovement ) noexcept
    {
        if ( !keepMovement )
        {
            _moveFB = _moveLR = _moveUD = MoveDirection::NONE;
        }

        _angleUD = _angleLR = _roll = _zoom = MoveDirection::NONE;
    }

    void SceneStatePerPlayer::resetAll() noexcept
    {
        resetMoveDirections(false);
        _cameraUnderwater = false;
        _cameraUpdated = false;
        _overrideCamera = nullptr;
        _cameraLockedToMouse = false;
    }

    SceneState::SceneState( Scene& parentScene )
        : SceneComponent( parentScene ),
        _renderState( parentScene )
    {
    }

    SceneRenderState::SceneRenderState( Scene& parentScene ) noexcept
        : SceneComponent( parentScene )
    {
        enableOption( RenderOptions::RENDER_GEOMETRY );
        _lodThresholds.set( 25, 45, 85, 165 );
    }

    void SceneRenderState::renderMask( const U16 mask )
    {
        constexpr bool validateRenderMask = false;

        if constexpr( validateRenderMask )
        {
            const auto validateMask = [mask]() -> U16
            {
                U16 validMask = 0;
                for ( U16 stateIt = 1u; stateIt <= to_base( RenderOptions::COUNT ); ++stateIt )
                {
                    const U16 bitState = toBit( stateIt );

                    if ( mask & bitState )
                    {
                        validMask |= to_base(bitState );
                    }
                }
                return validMask;
            };

            const U16 parsedMask = validateMask();
            DIVIDE_ASSERT( parsedMask != 0 && parsedMask == mask,
                           "SceneRenderState::renderMask error: Invalid state specified!" );
            _stateMask = parsedMask;
        }
        else
        {
            _stateMask = mask;
        }
    }

    bool SceneRenderState::isEnabledOption( const RenderOptions option ) const noexcept
    {
        return _stateMask & to_base(option);
    }

    void SceneRenderState::enableOption( const RenderOptions option ) noexcept
    {
        _stateMask |= to_base(option);
    }

    void SceneRenderState::disableOption( const RenderOptions option ) noexcept
    {
        _stateMask &= ~to_base(option);
    }

    void SceneRenderState::toggleOption( const RenderOptions option ) noexcept
    {
        toggleOption( option, !isEnabledOption( option ) );
    }

    void SceneRenderState::toggleOption( const RenderOptions option, const bool state ) noexcept
    {
        if ( state )
        {
            enableOption( option );
        }
        else
        {
            disableOption( option );
        }
    }

    vec4<U16> SceneRenderState::lodThresholds( const RenderStage stage ) const noexcept
    {
        // Hack dumping ground. Scene specific lod management can be tweaked here to keep the components clean
        if ( stage == RenderStage::REFLECTION || stage == RenderStage::REFRACTION )
        {
            // cancel out LoD Level 0
            return {
                0u,
                _lodThresholds.y,
                _lodThresholds.z,
                _lodThresholds.w };
        }
        if ( stage == RenderStage::SHADOW )
        {
            return _lodThresholds * 3u;
        }

        return _lodThresholds;
    }
}  // namespace Divide