

#include "Headers/Quad3D.h"

#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"

#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide
{
    Quad3D::Quad3D( const ResourceDescriptor<Quad3D>& descriptor )
        : Object3D( descriptor, GetSceneNodeType<Quad3D>() )
        , _descriptor( descriptor )
    {
    }

    bool Quad3D::load( PlatformContext& context )
    {
        constexpr F32 s_minSideLength = 0.0001f;

        const vec3<U32> sizeIn = _descriptor.data();

        vec3<F32> targetSize{
            Util::UINT_TO_FLOAT( sizeIn.x ),
            Util::UINT_TO_FLOAT( sizeIn.y ),
            Util::UINT_TO_FLOAT( sizeIn.z )
        };
        if ( sizeIn.x == 0u && sizeIn.y == 0u && sizeIn.z == 0u )
        {
            targetSize.xy = { s_minSideLength, s_minSideLength };
        }
        else if ( (sizeIn.x == 0u && sizeIn.y == 0u) ||
                  (sizeIn.x == 0u && sizeIn.z == 0u) )
        {
            targetSize.x = s_minSideLength;
        }
        else if ( sizeIn.y == 0u && sizeIn.z == 0u )
        {
            targetSize.y = s_minSideLength;
        }

        const U16 indices[] = { 2, 0, 1, 1, 2, 3, 1, 0, 2, 2, 1, 3 };

        const F32 halfExtentX = targetSize.x * 0.5f;
        const F32 halfExtentY = targetSize.y * 0.5f;
        const F32 halfExtentZ = targetSize.z * 0.5f;

        const bool doubleSided = _descriptor.mask().b[0] == 0;

        VertexBuffer::Descriptor vbDescriptor{};
        vbDescriptor._name = resourceName();
        vbDescriptor._allowDynamicUpdates = true;
        vbDescriptor._keepCPUData = true;
        vbDescriptor._largeIndices = false;

        auto vb = context.gfx().newVB( vbDescriptor );

        vb->setVertexCount( 4 );
        vb->modifyPositionValue( 0, -halfExtentX, halfExtentY, -halfExtentZ ); // TOP LEFT
        vb->modifyPositionValue( 1, halfExtentX, halfExtentY, -halfExtentZ ); // TOP RIGHT
        vb->modifyPositionValue( 2, -halfExtentX, -halfExtentY, halfExtentZ ); // BOTTOM LEFT
        vb->modifyPositionValue( 3, halfExtentX, -halfExtentY, halfExtentZ ); // BOTTOM RIGHT

        vb->modifyNormalValue( 0, 0, 0, -1 );
        vb->modifyNormalValue( 1, 0, 0, -1 );
        vb->modifyNormalValue( 2, 0, 0, -1 );
        vb->modifyNormalValue( 3, 0, 0, -1 );

        vb->modifyTexCoordValue( 0, 0, 1 );
        vb->modifyTexCoordValue( 1, 1, 1 );
        vb->modifyTexCoordValue( 2, 0, 0 );
        vb->modifyTexCoordValue( 3, 1, 0 );

        const U8 indicesCount = doubleSided ? 12 : 6;
        for ( U8 i = 0; i < indicesCount; i++ )
        {
            // CCW draw order
            vb->addIndex( indices[i] );
            //  v0----v1
            //   |    |
            //   |    |
            //  v2----v3
        }

        vb->computeTangents();
        geometryBuffer( vb );

        recomputeBounds();

        if ( !_descriptor.flag() )
        {
            ResourceDescriptor<Material> matDesc( "Material_" + resourceName() );
            matDesc.waitForReady( true );
            Handle<Material> matTemp = CreateResource( matDesc );
            Get( matTemp )->properties().shadingMode( ShadingMode::PBR_MR );
            setMaterialTpl( matTemp );
        }

        return Object3D::load(context);
    }

    vec3<F32> Quad3D::getCorner( const CornerLocation corner )
    {
        switch ( corner )
        {
            case CornerLocation::TOP_LEFT:     return geometryBuffer()->getPosition( 0 );
            case CornerLocation::TOP_RIGHT:    return geometryBuffer()->getPosition( 1 );
            case CornerLocation::BOTTOM_LEFT:  return geometryBuffer()->getPosition( 2 );
            case CornerLocation::BOTTOM_RIGHT: return geometryBuffer()->getPosition( 3 );
            default:                           break;
        }

        DIVIDE_UNEXPECTED_CALL();
        return geometryBuffer()->getPosition( 0 );
    }

    void Quad3D::setNormal( const CornerLocation corner, const vec3<F32>& normal )
    {
        switch ( corner )
        {
            case CornerLocation::TOP_LEFT:
                geometryBuffer()->modifyNormalValue( 0, normal );
                break;
            case CornerLocation::TOP_RIGHT:
                geometryBuffer()->modifyNormalValue( 1, normal );
                break;
            case CornerLocation::BOTTOM_LEFT:
                geometryBuffer()->modifyNormalValue( 2, normal );
                break;
            case CornerLocation::BOTTOM_RIGHT:
                geometryBuffer()->modifyNormalValue( 3, normal );
                break;
            case CornerLocation::CORNER_ALL:
            {
                geometryBuffer()->modifyNormalValue( 0, normal );
                geometryBuffer()->modifyNormalValue( 1, normal );
                geometryBuffer()->modifyNormalValue( 2, normal );
                geometryBuffer()->modifyNormalValue( 3, normal );
            } break;

            default: DIVIDE_UNEXPECTED_CALL(); break;
        }
    }

    void Quad3D::setCorner( const CornerLocation corner, const vec3<F32>& value )
    {
        switch ( corner )
        {
            case CornerLocation::TOP_LEFT:
                geometryBuffer()->modifyPositionValue( 0, value );
                break;
            case CornerLocation::TOP_RIGHT:
                geometryBuffer()->modifyPositionValue( 1, value );
                break;
            case CornerLocation::BOTTOM_LEFT:
                geometryBuffer()->modifyPositionValue( 2, value );
                break;
            case CornerLocation::BOTTOM_RIGHT:
                geometryBuffer()->modifyPositionValue( 3, value );
                break;
            default:
                break;
        }

        recomputeBounds();
    }

    // rect.xy = Top Left; rect.zw = Bottom right
    // Remember to invert for 2D mode
    void Quad3D::setDimensions( const vec4<F32>& rect )
    {
        geometryBuffer()->modifyPositionValue( 0, rect.x, rect.w, 0 );
        geometryBuffer()->modifyPositionValue( 1, rect.z, rect.w, 0 );
        geometryBuffer()->modifyPositionValue( 2, rect.x, rect.y, 0 );
        geometryBuffer()->modifyPositionValue( 3, rect.z, rect.y, 0 );

        recomputeBounds();
    }

    void Quad3D::recomputeBounds()
    {
        setBounds(
            BoundingBox
            {
                geometryBuffer()->getPosition( 1 ),
                geometryBuffer()->getPosition( 2 ) + vec3<F32>{ 0.0f, 0.0f, 0.0025f }
            }
        );
    }

} //namespace Divide
