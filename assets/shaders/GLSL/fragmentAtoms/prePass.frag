#ifndef _PRE_PASS_FRAG_
#define _PRE_PASS_FRAG_

#if !defined(PRE_PASS)
#   define PRE_PASS
#endif //!PRE_PASS

// This will also do our check for us
#include "velocityCalc.frag"
#include "nodeBufferedInput.cmn"

#if defined(HAS_VELOCITY)
layout(location = TARGET_VELOCITY) out vec2 _velocityOut;
#endif //HAS_VELOCITY

void writeGBuffer() {
#if defined(HAS_VELOCITY)
    _velocityOut = velocityCalc();
#endif //HAS_VELOCITY
}

#endif //_PRE_PASS_FRAG_
