#include "unitTestCommon.h"

#include <catch2/catch_session.hpp>

CATCH_REGISTER_LISTENER( platformInitRunListener )

int main( int argc, char* argv[] )
{
    return Catch::Session().run( argc, argv );
}
