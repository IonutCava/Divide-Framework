/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef _DIVIDE_CONFIG_H_
#define _DIVIDE_CONFIG_H_

namespace Divide {
namespace Config {

namespace Build {
#if defined(_DEBUG)
    constexpr bool IS_DEBUG_BUILD   = true;

    constexpr bool IS_PROFILE_BUILD = false;
    constexpr bool IS_RELEASE_BUILD = false;
#elif defined(_PROFILE)
    constexpr bool IS_PROFILE_BUILD = true;

    constexpr bool IS_DEBUG_BUILD   = false;
    constexpr bool IS_RELEASE_BUILD = false;
#else
    constexpr bool IS_RELEASE_BUILD = true;

    constexpr bool IS_DEBUG_BUILD   = false;
    constexpr bool IS_PROFILE_BUILD = false;
#endif

    // Set IS_SHIPPING_BUILD to true to disable non-required functionality for shipped games: editors, debug code, etc
    constexpr bool IS_SHIPPING_BUILD = IS_RELEASE_BUILD && false;
#if defined(_START_IN_EDITOR)
    constexpr bool IS_EDITOR_BUILD = true;
    constexpr bool ENABLE_EDITOR = true;
#else //_START_IN_EDITOR
    constexpr bool IS_EDITOR_BUILD = false;
    constexpr bool ENABLE_EDITOR = !IS_SHIPPING_BUILD;
#endif //_START_IN_EDITOR

} //namespace Build

namespace Assert {
    /// Log assert fails messages to the error log file
    constexpr bool LOG_ASSERTS = !Build::IS_RELEASE_BUILD;

    /// Popup a GUI Message Box on asserts;
    constexpr bool SHOW_MESSAGE_BOX = LOG_ASSERTS;

    /// Do not call the platform "assert" function in order to continue application execution
    constexpr bool CONTINUE_ON_ASSERT = false;
} // namespace Assert

namespace Profile {
    /// How many profiling timers are we allowed to use in our applications. We allocate them upfront, so a max limit is needed
    constexpr unsigned int MAX_PROFILE_TIMERS = 1 << 10;

    /// Enable function level profiling
    constexpr bool ENABLE_FUNCTION_PROFILING = !Build::IS_SHIPPING_BUILD;
} // namespace Profile


constexpr float Z_TEST_SIGMA = 0.00001f;// 1.f / U8_MAX;
constexpr float ALPHA_DISCARD_THRESHOLD = 1.f - 0.05f;

/// Application desired framerate for physics and input simulations
constexpr unsigned int TARGET_FRAME_RATE = 60;

/// Application update rate divisor (how many many times should we update our state per second)
/// e.g. For TARGET_FRAME_RATE = 60, TICK_DIVISOR = 2 => update at 30Hz, render at 60Hz.
constexpr unsigned int TICK_DIVISOR = 2;

/// The minimum threshold needed for a threaded loop to use sleep. Update intervals bellow this threshold will not use sleep!
constexpr unsigned int MIN_SLEEP_THRESHOLD_MS = 5;

/// Minimum required RAM size (in bytes) for the current build. We will probably fail to initialise if this is too low.
/// Per frame memory arena and GPU object arena both depend on this value for up-front allocations
constexpr size_t REQUIRED_RAM_SIZE_IN_BYTES = 2u * (1024u * 1024 * 1024); //2Gb

/// How many tasks should we keep in a per-thread pool to avoid using new/delete (must be power of two)
constexpr unsigned int MAX_POOLED_TASKS = 1 << 14;

/// Maximum number of bones available per node
constexpr unsigned int MAX_BONE_COUNT_PER_NODE = 1 << 7;

/// Estimated maximum number of visible objects per render pass (this includes debug primitives)
constexpr unsigned int MAX_VISIBLE_NODES = 1 << 13;

/// Estimated maximum number of materials used in a single frame by all passes combined
constexpr unsigned int MAX_CONCURRENT_MATERIALS = 1 << 14;

/// Maximum number of concurrent clipping planes active at any time (can be changed per-call)
/// High numbers for CLIP/CULL negatively impact number of threads run on the GPU. Max of 6 for each (ref: nvidia's "GPU-DRIVEN RENDERING" 2016)
constexpr unsigned int MAX_CLIP_DISTANCES = 3;

/// Same as MAX_CLIP_DISTANCES, but for culling instead. This number counts towards the maximum combined clip&cull distances the GPU supports
/// Value is capped between [0, GPU_MAX_DISTANCES - MAX_CLIP_DISTANCES)
constexpr unsigned int MAX_CULL_DISTANCES = 1;

/// How many reflective objects with custom draw pass are we allowed to display on screen at the same time (e.g. Water, mirrors, etc)
/// SSR and Environment mapping do not count towards this limit. Should probably be removed in the future.
constexpr unsigned int MAX_REFLECTIVE_NODES_IN_VIEW = Build::IS_DEBUG_BUILD ? 3 : 5;

/// Similar to MAX_REFLECTIVE_NODES_IN_VIEW but for custom refraction passes (e.g. Water, special glass, etc). Also a candidate for removal
constexpr unsigned int MAX_REFRACTIVE_NODES_IN_VIEW = Build::IS_DEBUG_BUILD ? 2 : 4;

/// Maximum number of environment probes we are allowed to update per frame
constexpr unsigned int MAX_REFLECTIVE_PROBES_PER_PASS = 6;

/// Maximum number of players we support locally. We store per-player data such as key-bindings, camera positions, etc.
constexpr unsigned int MAX_LOCAL_PLAYER_COUNT = 4;

/// Use the coloured version of WOIT as detailed here: http://casual-effects.blogspot.com/2015/03/colored-blended-order-independent.html (Note: Not yet working!)
constexpr bool USE_COLOURED_WOIT = false;

namespace Lighting {
    /// How many lights (in order as passed to the shader for the node) should cast shadows
    constexpr unsigned short MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS = 2;
    constexpr unsigned short MAX_SHADOW_CASTING_POINT_LIGHTS = 4;
    constexpr unsigned short MAX_SHADOW_CASTING_SPOT_LIGHTS = 6;

    /// Maximum number of shadow casting lights processed per frame
    constexpr unsigned short MAX_SHADOW_CASTING_LIGHTS = MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS + MAX_SHADOW_CASTING_POINT_LIGHTS + MAX_SHADOW_CASTING_SPOT_LIGHTS;

    /// Used for CSM or PSSM to determine the maximum number of frustum splits
    constexpr unsigned short MAX_CSM_SPLITS_PER_LIGHT = 4;

    /// Maximum possible number of shadow passes per frame
    static constexpr U32 MAX_SHADOW_PASSES = MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS * MAX_CSM_SPLITS_PER_LIGHT +
                                             MAX_SHADOW_CASTING_POINT_LIGHTS * 6 +
                                             MAX_SHADOW_CASTING_SPOT_LIGHTS;

    /// Maximum number of lights we process per frame. We need this upper bound for pre-allocating arrays and setting up shaders. Increasing it shouldn't hurt performance in a linear fashion
    constexpr unsigned short MAX_ACTIVE_LIGHTS_PER_FRAME = 1 << 12;

    /// These settings control the clustered renderer and how we batch lights per tile & cluster
    namespace ClusteredForward {
        /// Upper limit of lights used in a cluster. The lower, the better performance at the cost of pop-in/glitches. At ~100, any temporal issues should remain fairly hidden
        constexpr unsigned short MAX_LIGHTS_PER_CLUSTER = 100u;

        /// Controls compute shader dispatch. e.g. Dispatch Z count = CLUSTERS_Z / CLUSTERS_Z_THREADS
        constexpr unsigned short CLUSTERS_X_THREADS = 16u;
        constexpr unsigned short CLUSTERS_Y_THREADS = 8u;
        constexpr unsigned short CLUSTERS_Z_THREADS = 4u;
        constexpr unsigned short MAX_COMPUTE_THREADS = 1024u; //Value from some random, old D3D spec, I think ...

        // This may not be a hard requirement but for now it works fine
        static_assert(CLUSTERS_X_THREADS * CLUSTERS_Y_THREADS * CLUSTERS_Z_THREADS <= MAX_COMPUTE_THREADS);
    } // namespace ClusteredForward
} // namespace Lighting

namespace Networking {
    /// How often should the client send messages to the server
    constexpr unsigned int NETWORK_SEND_FREQUENCY_HZ = 20;

    /// How many times should we try to send an update to the server before giving up?
    constexpr unsigned int NETWORK_SEND_RETRY_COUNT = 3;
} // namespace Networking

/// Error callbacks, validations, buffer checks, etc. are controlled by this flag. Heavy performance impact!
constexpr bool ENABLE_GPU_VALIDATION = !Build::IS_SHIPPING_BUILD;

/// Scan changes to shader source files, script files, etc to hot-reload assets. Isn't needed in shipping builds or without the editor enabled
constexpr bool ENABLE_LOCALE_FILE_WATCHER = !Build::IS_SHIPPING_BUILD && Build::ENABLE_EDITOR;

constexpr auto DEFAULT_SCENE_NAME = "DefaultScene";

constexpr auto ENGINE_NAME = "Divide Framework";
constexpr auto ENGINE_VERSION_MAJOR = 0u;
constexpr auto ENGINE_VERSION_MINOR = 1u;
constexpr auto ENGINE_VERSION_PATCH = 0u;

}  // namespace Config
}  // namespace Divide

constexpr auto OUTPUT_LOG_FILE = "console.log";
constexpr auto ERROR_LOG_FILE = "errors.log";

#endif  //_DIVIDE_CONFIG_H_
