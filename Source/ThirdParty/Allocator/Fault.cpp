#include "Fault.h"
#include "DataTypes.h"

#include <cassert>

//----------------------------------------------------------------------------
// FaultHandler
//----------------------------------------------------------------------------
void FaultHandler(const char* file, unsigned short line)
{
    (void)(file);
    (void)(line);
	assert(0);
}
