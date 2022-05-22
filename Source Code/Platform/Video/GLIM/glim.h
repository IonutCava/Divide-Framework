/*
** GLIM - OpenGL Immediate Mode
** Copyright Jan Krassnigg (Jan@Krassnigg.de)
** For more details, see the included Readme.txt.
*/

#ifndef GLIM_GLIM_H
#define GLIM_GLIM_H

#include "Declarations.h"
#include "glimBatch.h"


// Include this Header to get access to all GLIM functions.


// In order to properly use GLIM, don't forget to set the callback
// GLIM_Interface::s_StateChangeCallback
// to a function that applies all your OpenGL states. 
// This is necessary, if you cache state-changes and only want to
// apply them when it is actually necessary. 
// GLIM will call this callback just before querying shader states,
// binding the vertex arrays and making the draw-calls.


// GLIM TODO LIST:
/*

 * Implement support for all primitive types:
    GLIM_TRIANGLE_STRIP
    GLIM_QUAD_STRIP

 * If GL3 adds Quads back in (or one uses the compatibility mode):
    Don't triangulate Quads.

 * Add an additional Index-Buffer to GLIM_BATCH for wireframe rendering.

 
*/

#pragma once

#endif


