

#include "Headers/Session.h"
#include "Headers/Server.h"
#include "Headers/OPCodesImpl.h"
#include "Headers/Patch.h"

#include "Networking/Headers/ASIO.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/Resource.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    Session::Session( boost::asio::io_context& io_context, channel& ch )
        : tcp_session_tpl( io_context, ch )
    {
    }

    void Session::handlePacket( WorldPacket& p )
    {
        switch ( p.opcode() )
        {
            case OPCodesEx::CMSG_GEOMETRY_LIST:
                HandleGeometryListOpCode( p );
                break;
            case OPCodesEx::CMSG_REQUEST_GEOMETRY:
                HandleRequestGeometry( p );
                break;
            default:
                tcp_session_tpl::handlePacket( p );
                break;
        };
    }

    void Session::HandleGeometryListOpCode( WorldPacket& p )
    {
        PatchData dataIn;
        p >> dataIn.sceneName;
        p >> dataIn.size;
        ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_RECEIVE_GEOMETRY_LIST"), dataIn.size ).c_str() );
        for ( U32 i = 0; i < dataIn.size; i++ )
        {
            string name, modelname;
            p >> name;
            p >> modelname;
            dataIn.name.push_back( name );
            dataIn.modelName.push_back( modelname );
        }

        if ( !Patch::compareData( dataIn ) )
        {
            WorldPacket r( OPCodesEx::SMSG_GEOMETRY_APPEND );

            const auto& patchData = Patch::modelData();
            r << patchData.size();
            for ( const FileData& dataOut : patchData )
            {
                r << dataOut.ItemName;
                r << dataOut.ModelName;
                r << dataOut.Orientation.x;
                r << dataOut.Orientation.y;
                r << dataOut.Orientation.z;
                r << dataOut.Position.x;
                r << dataOut.Position.y;
                r << dataOut.Position.z;
                r << dataOut.Scale.x;
                r << dataOut.Scale.y;
                r << dataOut.Scale.z;
            }
            ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_SEND_GEOMETRY_APPEND"), patchData.size() ).c_str() );

            sendPacket( r );
            Patch::clearModelData();
        }
    }

    void Session::HandleRequestGeometry( WorldPacket& p )
    {
        string file;
        p >> file;

        ASIO::LOG_PRINT( Util::StringFormat(LOCALE_STR("ASIO_SEND_FILE"), file).c_str() );
        WorldPacket r( OPCodesEx::SMSG_SEND_FILE );
        r << (U8)0;
        sendPacket( r );
        sendFile( file );
    }

};  // namespace Divide
