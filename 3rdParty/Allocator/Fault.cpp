#include "Fault.h"
#include "DataTypes.h"

#include <cassert>

//----------------------------------------------------------------------------
// FaultHandler
//----------------------------------------------------------------------------
void FaultHandler(const char* file, unsigned short line)
{
#if defined(WIN32)
	// If you hit this line, it means one of the ASSERT macros failed.
    DebugBreak();	
#endif
    (void)(file);
    (void)(line);
	assert(0);
}