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

/*
    OgreCrowd
    ---------

    Copyright (c) 2012 Jonas Hauquier

    Additional contributions by:

    - mkultra333
    - Paul Wilson

    Sincere thanks and to:

    - Mikko Mononen (developer of Recast navigation libraries)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
   deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.

*/

#if 0

#pragma once
#ifndef _DIVIDE_TILE_CACHE_H_
#define _DIVIDE_TILE_CACHE_H_
#include "Platform/Headers/PlatformDefines.h"
#include "AI/PathFinding/NavMeshes/Headers/NavMesh.h"

#include <DetourTileCache/Include/DetourTileCache.h>
#include <DetourTileCache/Include/DetourTileCacheBuilder.h>
#include <RecastContrib/TileCacheCompressor/Headers/DivideTileCacheUtil.h>
#include <RecastContrib/TileCacheCompressor/Headers/DivideTileCacheCompressor.h>

namespace Divide {
namespace Navigation {
/**
  * Maximum layers (floor levels) that 2D navmeshes can have in the tilecache.
  * This determines the domain size of the tilecache pages, as their dimensions
  * are width*height*layers.
  **/
static const I32 MAX_LAYERS = 32;

/**
  * Struct that stores the actual tile data in binary form.
  **/
struct TileCacheData {
    U8* data;
    I32 dataSize;
};

/**
  * Rasterization context stores temporary data used
  * when rasterizing inputGeom into a navmesh.
  **/
struct RasterizationContext {
    RasterizationContext() : solid(0), triareas(0), lset(0), chf(0), ntiles(0) {
        memset(tiles, 0, sizeof(TileCacheData) * MAX_LAYERS);
    }

    ~RasterizationContext() {
        rcFreeHeightField(solid);
        DELETE_ARRAY(triareas);
        rcFreeHeightfieldLayerSet(lset);
        rcFreeCompactHeightfield(chf);
        for (I32 i = 0; i < MAX_LAYERS; ++i) {
            dtFree(tiles[i].data);
            tiles[i].data = 0;
        }
    }

    rcHeightfield* solid;
    U8* triareas;
    rcHeightfieldLayerSet* lset;
    rcCompactHeightfield* chf;
    TileCacheData tiles[MAX_LAYERS];
    I32 ntiles;
};

/**
  * Build context stores temporary data used while
  * building a navmesh tile.
  **/
struct BuildContext {
    inline BuildContext(struct dtTileCacheAlloc* a)
        : layer(0), lcset(0), lmesh(0), alloc(a) {}
    inline ~BuildContext() { purge(); }
    void purge() {
        dtFreeTileCacheLayer(alloc, layer);
        layer = 0;
        dtFreeTileCacheContourSet(alloc, lcset);
        lcset = 0;
        dtFreeTileCachePolyMesh(alloc, lmesh);
        lmesh = 0;
    }
    struct dtTileCacheLayer* layer;
    struct dtTileCacheContourSet* lcset;
    struct dtTileCachePolyMesh* lmesh;
    struct dtTileCacheAlloc* alloc;
};

// TODO put in class context
/**
  * Calculate the memory space used by the tilecache.
  **/
static I32 calcLayerBufferSize(const I32 gridWidth, const I32 gridHeight) {
    const I32 headerSize = dtAlign4(sizeof(dtTileCacheLayerHeader));
    const I32 gridSize = gridWidth * gridHeight;
    return headerSize + gridSize * 4;
}

/**
  * DetourTileCache manages a large grid of individual navmeshes stored in pages
  *to
  * allow managing a navmesh for a very large map. Navmesh pages can be
  *requested
  * when needed or swapped out when they are no longer needed.
  * Using a tilecache the navigation problem is localized to one tile, but
  *pathfinding
  * can still find a path that references to other neighbour tiles on the higher
  *hierarchy
  * level of the tilecache. Localizing the pathfinding problem allows it to be
  *more scalable,
  * also for very large worlds.
  * DetouTileCache stores navmeshes in an intermediary format as 2D heightfields
  * that can have multiple levels. It allows to quickly generate a 3D navmesh
  *from
  * this intermediary format, with the additional option of adding or removing
  * temporary obstacles to the navmesh and regenerating it.
  **/
class DivideTileCache {
   public:
    /**
      * Create a tilecache that will build a tiled recast navmesh stored at the
      *specified
      * NavMesh component. Will use specified tilesize (a multiple of 8 between
      *16 and 128),
      * all other configuration parameters are copied from the OgreRecast
      *component configuration.
      * Tilesize is the number of (recast) cells per tile.
      **/
    DivideTileCache(Navigation::NavigationMesh* recast, I32 tileSize = 48);
    ~DivideTileCache(void);

    /**
      * Configure the tilecache for building navmesh tiles from the specified
      *input geometry.
      * The inputGeom is mainly used for determining the bounds of the world for
      *which a navmesh
      * will be built, so at least bmin and bmax of inputGeom should be set to
      *your world's outer
      * bounds. This world bounding box is used to calculate the grid size that
      *the tilecache has
      * to initialize.
      * This method has to be called once after construction, and before any
      *tile builds happen.
      **/
    bool configure(SceneGraphNode* inputGeom);

    /**
      * Find tiles that (partially or completely) intersect the specified
      *bounding area.
      * The selectionArea has to be in world units.
      * TileCache needs to be configured before this method can work (needs to
      *know world size
      * of tilecache).
      * TileSelection contains bounding box aligned to the tile bounds and tx ty
      *index ranges
      * for the affected tiles. Note that tile ranges are inclusive! (eg. if
      *minTx=1 and maxTx=1
      * then tile at x=1 has to be rebuilt)
      * It is not necessary for tiles to be already built in order for
      * them to be included in the selection.
      * Make sure you use the included bounding box instead of an arbitrary
      *selection bounding
      * box to bound inputGeom used for rebuilding tiles. Or you might not
      *include all geometry
      * needed to rebuild all affected tiles correctly.
      **/
    TileSelection getTileSelection(const BoundingBox& selectionArea);

    /**
      * Returns a bounding box that matches the tile bounds of this cache and
      *that is at least
      * as large as the specified selectionArea bounding box. Height (y)
      *coordinates will be set
      * to the min and max height of this tilecache. (tile selection only
      *happens in x-z plane).
      * Use this function to get correct bounding boxes to cull your inputGeom
      *or scene geometry
      * with for tile rebuilding.
      **/
    BoundingBox getTileAlignedBox(const BoundingBox& selectionArea);

    /**
      * Returns the world-space bounds of the tile at specified grid position.
      * Make sure that tx and ty satisfy isWithinBounds(tx, ty).
      **/
    BoundingBox getTileBounds(I32 tx, I32 ty);

    /**
      * The size of one tile in world units.
      * This equals the number of cells per tile, multiplied with the cellsize.
      **/
    inline F32 getTileSize(void) { return m_tileSize * m_cellSize; }

    Navigation::NavigationMesh* getRecast(void) { return m_recast; }

    /**
      * Build all tiles of the tilecache and construct a recast navmesh from the
      * specified entities. These entities need to be already added to the scene
      *so that
      * their world position and orientation can be calculated.
      *
      * This is an Ogre adaptation of Sample_TempObstacles::handleBuild()
      * First init the OgreRecast module like you would construct a simple
      *single
      * navmesh, then invoke this method instead of OgreRecast::NavMeshBuild()
      *to create
      * a tileCache from the specified ogre geometry.
      * The specified ogre entities need to be added to a scenenode in the scene
      *before this
      * method is called.
      * The resulting navmesh will be created in the OgreRecast module, at
      *OgreRecast::m_navMesh;
      *
      * Will issue a configure() call so the entities specified will determine
      *the world bounds
      * of the tilecache.
      **/
    // bool TileCacheBuild(vector<Ogre::Entity*> srcMeshes);

    /**
      * Build all navmesh tiles from specified input geom.
      *
      * Will issue a configure() call so the inputGeom specified will determine
      *the world bounds
      * of the tilecache. Therefore you must specify the inputGeom for the
      *entire world.
      *
      * @see OgreDetourTileCache::TileCacheBuild(vector<Ogre::Entity*>)
      **/
    bool TileCacheBuild(SceneGraphNode* inputGeom);

    // TODO maybe provide isLoaded(tx, ty) method

    // TODO create better distinction between loading compressed tiles in cache
    // and building navmesh from them?

    // TODO are both updateFromGeometry() and buildTiles() necessary, or can
    // update be dropped? It might be confusing.

    /**
      * Build or rebuild a cache tile at the specified x and y position in the
      *tile grid.
      * Tile is built or rebuilt no matter whether there was already a tile at
      *that position in the grid
      * or not. If there previously was a tile in the specified grid position,
      *it is first removed from the
      * tilecache and replaced with the new one.
      *
      * At the moment this will issue an immediate update of the navmesh at the
      * corresponding tiles. (the alternative is adding a request that is
      *processed as deferred command)
      *
      * Note that you can speed this up by building an inputGeom from only the
      *area that is rebuilt.
      * Don't use an arbitrary bounding box for culling the inputGeom, but use
      *getTileAlignedBox() instead!
      **/
    bool buildTile(const I32 tx, const I32 ty, SceneGraphNode* inputGeom);

    /**
      * Build or rebuild a cache tiles or tiles that cover the specified
      *bounding box area.
      *
      * The tiles are built or rebuilt no matter whether there was already a
      *tile at that position in the grid
      * or not. If there previously was a tile in the specified grid position,
      *it is first removed from the
      * tilecache and replaced with the new one.
      *
      * Make sure that the specified inputGeom is either the inputGeom of the
      *complete scene (inefficient) or is
      * built with a tile aligned bounding box (getTileAlignedBox())! The
      *areaToUpdate value can be arbitrary,
      * but will be converted to a tile aligned box.
      *
      * At the moment this will issue an immediate update of the navmesh at the
      * corresponding tiles. (the alternative is adding a request that is
      *processed as deferred command)
      **/
    void buildTiles(SceneGraphNode* inputGeom,
                    const BoundingBox* areaToUpdate = nullptr);

    /**
      * Build or rebuild tile from list of entities.
      * @see{buildTiles(InputGeom*, const Ogre::AxisAlignedBox*)}
      **/
    // void buildTiles(vector<Ogre::Entity*> srcEntities, const BoundingBox
    // *areaToUpdate = nullptr);

    // TODO maybe also add a unloadAllTilesExcept(boundingBox) method

    /**
      * Unload all tiles that cover the specified bounding box. The tiles are
      *removed from the
      * cache.
      **/
    void unloadTiles(const BoundingBox& areaToUpdate);

    /**
      * Gets grid coordinates of the tile that contains the specified world
      *position.
      **/
    void getTileAtPos(const F32* pos, I32& tx, I32& ty);

    /**
      * Gets grid coordinates of the tile that contains the specified world
      *position.
      **/
    vec2<F32> getTileAtPos(const vec3<F32> pos);

    /**
      * Determines whether there is a tile loaded at the specified grid
      *position.
      **/
    bool tileExists(I32 tx, I32 ty);

    /**
      * Determines whether the specified grid index is within the outer bounds
      *of this tilecache.
      **/
    bool isWithinBounds(I32 tx, I32 ty);

    /**
      * Determines whether the specified world position is within the outer
      *bounds of this tilecache,
      * ie the coordinates are contained within a tile that is within the cache
      *bounds.
      **/
    bool isWithinBounds(vec3<F32> pos);

    BoundingBox getWorldSpaceBounds(void);

    TileSelection getBounds(void);

    bool saveAll(const string& filename);

    bool loadAll(const string& filename);

    /**
      * Update (tick) the tilecache.
      * You must call this method in your render loop continuously to
      *dynamically
      * update the navmesh when obstacles are added or removed.
      * Navmesh rebuilding happens per tile and only where needed. Tile
      *rebuilding is
      * timesliced.
      **/
    void handleUpdate(const F32 dt);

    /**
      * Remove all (cylindrical) temporary obstacles from the tilecache.
      * The navmesh will be rebuilt after the next (one or more) update()
      * call.
      **/
    void clearAllTempObstacles(void);

    /**
      * Add a temporary (cylindrical) obstacle to the tilecache (as a deferred
      *request).
      * The navmesh will be updated correspondingly after the next (one or many)
      * update() call as a deferred command.
      * If m_tileCache->m_params->maxObstacles obstacles are already added, this
      *call
      * will have no effect. Also, at one time only MAX_REQUESTS can be added,
      *or nothing
      * will happen.
      *
      * If successful returns a reference to the added obstacle.
      **/
    dtObstacleRef addTempObstacle(vec3<F32> pos);

    /**
      * Remove temporary (cylindrical) obstacle with specified reference. The
      *affected tiles
      * will be rebuilt. This operation is deferred and will happen in one of
      *the next
      * update() calls. At one time only MAX_REQUESTS obstacles can be removed,
      *or nothing will happen.
      **/
    bool removeTempObstacle(dtObstacleRef obstacleRef);

    /**
      * Remove a temporary (cylindrical) obstacle from the tilecache (as a
      *deferred request).
      * Uses a ray query to find the temp obstacle.
      * The navmesh will be updated correspondingly after the next (one or many)
      * update() call as a deferred command.
      * At one time only MAX_REQUESTS obstacles can be removed, or nothing will
      *happen.
      **/
    dtObstacleRef removeTempObstacle(vec3<F32> raySource, vec3<F32> rayHit);

    /**
      * Execute a ray intersection query to find the first temporary
      *(cylindrical) obstacle that
      * hits the ray, if any.
      **/
    dtObstacleRef hitTestObstacle(const dtTileCache* tc, const F32* sp,
                                  const F32* sq);

    /**
      * Returns a list of tile references to compressed tiles that cover the
      *specified bounding
      * box area.
      **/
    vector<dtCompressedTileRef> getTilesContainingBox(vec3<F32> boxMin,
                                                          vec3<F32> boxMax);

    /**
      * Returns a list of tile references to compressed tiles that cover the
      *area of a circle with
      * specified radius around the specified position.
      **/
    vector<dtCompressedTileRef> getTilesAroundPoint(vec3<F32> point,
                                                        F32 radius);

    /**
      * Add a convex shaped temporary obstacle to the tilecache in pretty much
      *the same way as cylindrical
      * obstacles are added.
      * Currently this is implemented a lot less efficiently than cylindrical
      *obstacles, as it issues a complete
      * rebuild of the affected tiles, instead of just cutting out the poly area
      *of the obstacle.
      * This is a big TODO that I'm holding off because it requires changes to
      *the recast libraries themselves.
      * I wait in hopes that this feature will appear in the original recast
      *code.
      * In the meanwhile, if you are looking for this, someone implemented it
      *and shared it on the mailing list:
      *     http://groups.google.com/group/recastnavigation/msg/92d5f131561ddad1
      * And corresponding ticket:
      *http://code.google.com/p/recastnavigation/issues/detail?id=206
      *
      * The current implementation of convex obstacles is very simple and not
      *deferred. Also obstacles
      * are stored in the inputGeom, which is not really nice.
      **/
    // TODO by adding deferred tasking to add and remove ConvexShapeObstacle
    // one can add multiple shapes at once to the same tile without it being
    // rebuilt multiple times
    int addConvexShapeObstacle(ConvexVolume* obstacle);

    /**
      * Remove convex obstacle from the tileCache. The affected navmesh tiles
      *will be rebuilt.
      **/
    bool removeConvexShapeObstacle(ConvexVolume* convexHull);

    /**
      * Remove convex obstacle with specified id from the tileCache. The
      *affected navmesh tiles will be rebuilt.
      * If removedObstacle is a valid pointer it will contain a reference to the
      *removed obstacle.
      **/
    bool removeConvexShapeObstacleByID(
        I32 obstacleIndex, ConvexVolume** removedObstacle = nullptr);

    /**
      * Raycast the inputGeom and remove the hit convex obstacle. The affected
      *navmesh tiles will be rebuilt.
      * If removedObstacle is a valid pointer it will contain a reference to the
      *removed obstacle.
      **/
    I32 removeConvexShapeObstacle(vec3<F32> raySource, vec3<F32> rayHit,
                                  ConvexVolume** removedObstacle = nullptr);

    /**
      * Returns the id of the specified convex obstacle. Returns -1 if this
      *obstacle is not currently added to the tilecache.
      * Note: IDs are just array indices and can change when obstacles are added
      *or removed. Use with care!
      **/
    I32 getConvexShapeObstacleID(ConvexVolume* convexHull);

    /**
      * Returns the convex obstacle with specified id or index.
      **/
    ConvexVolume* getConvexShapeObstacle(I32 obstacleIndex);

    /**
      * Raycast inputGeom to find intersection with a convex obstacle. Returns
      *the id of the hit
      * obstacle, -1 if none hit.
      **/
    I32 hitTestConvexShapeObstacle(vec3<F32> raySource, vec3<F32> rayHit);

    /**
      * Remove the tile with specified reference from the tilecache. The
      *associated navmesh tile will also
      * be removed.
      **/
    bool removeTile(dtCompressedTileRef tileRef);

    /**
      * Debug draw the tile at specified grid location.
      **/
    void drawDetail(const I32 tx, const I32 ty);

    /**
      * Debug draw all tiles in the navmesh.
      **/
    void drawNavMesh(void);

    /**
      * Unused debug drawing function from the original recast demo.
      * Used for drawing the obstacles in the scene.
      * In this demo application we use the Obstacle class to represent
      *obstacles in the scene.
      **/
    void drawObstacles(const dtTileCache* tc);

    /**
      * Ogre Recast component that holds the recast config and where the navmesh
      *will be built.
      **/
    Navigation::NavigationMesh* m_recast;

    /**
     * Max number of layers a tile can have
     **/
    static const I32 EXPECTED_LAYERS_PER_TILE;

    /**
     * Max number of (temp) obstacles that can be added to the tilecache
     **/
    static const I32 MAX_OBSTACLES;

    /**
     *
     * Extra padding added to the border size of tiles (together with agent
     *radius)
     **/
    static const F32 BORDER_PADDING;

    /**
      * Set to false to disable debug drawing. Improves performance.
      **/
    static bool DEBUG_DRAW;

    /**
      * Set to true to draw the bounding box of the tile areas that were
      *rebuilt.
      **/
    static bool DEBUG_DRAW_REBUILT_BB;

   protected:
    /**
      * Build the 2D navigation grid divided in layers that is the intermediary
      *format stored in the tilecache.
      * Builds the specified tile from the given input geometry. Only the part
      *of the geometry that intersects the
      * needed tile is used.
      * From this format a 3D navmesh can be quickly generated at runtime.
      * This process uses a large part of the recast navmesh building pipeline
      *(implemented in OgreRecast::NavMeshBuild()),
      * up till step 4.
      **/
    I32 rasterizeTileLayers(SceneGraphNode* geom, const I32 tx,
                            const I32 ty, const rcConfig& cfg,
                            TileCacheData* tiles, const I32 maxTiles);

    /**
      * Debug draw a navmesh poly
      **/
    void drawPolyMesh(const string& tileName,
                      const struct dtTileCachePolyMesh& mesh, const F32* orig,
                      const F32 cs, const F32 ch,
                      const struct dtTileCacheLayer& regionLayers,
                      bool colourRegions = true);

    /**
      * Inits the tilecache. Helper used by constructors.
      **/
    bool initTileCache(void);

    /**
      * InputGeom from which the tileCache is initially inited (it's bounding
      *box is considered the bounding box
      * for the entire world that the navmesh will cover). Tile build methods
      *without specific geometry or entity
      * input will build navmesh from this geometry.
      * It also stored the convex temp obstacles. (will be gone in the future)
      * In the future this variable will probably disappear.
      **/
    SceneGraphNode* m_geom;
    // TODO maybe in the future I don't want to store inputgeom anymore, at the
    // moment it's only used for adding convex shapes
    // (what really should be done from compressed tiles instead of rebuilding
    // from input geom)
    // The whole navmesh can be stored as compressed tiles, the input geom does
    // not need to be stored.

    /**
      * Set to true to keep intermediary results from navmesh build for
      *debugging purposes.
      * Set to false to free up memory after navmesh was built.
      * Same as in official recast demo. (it's a checkbox in the gui)
      **/
    bool m_keepInterResults;

    /**
      * The tile cache memory allocator implementation used.
      **/
    struct LinearAllocator* m_talloc;
    /**
      * The tile compression implementation used.
      **/
    struct FastLZCompressor* m_tcomp;

    /**
      * Callback handler that processes right after processing
      * a tile mesh. Adds off-mesh connections to the mesh.
      **/
    struct MeshProcess* m_tmproc;

    /**
      * The detourTileCache component this class wraps.
      **/
    class dtTileCache* m_tileCache;

    /**
      * Recast config (copied from the OgreRecast component).
      **/
    rcConfig m_cfg;

    /**
      * DetourTileCache configuration parameters.
      **/
    dtTileCacheParams m_tcparams;

    /**
      * Context that stores temporary working variables when navmesh building.
      **/
    rcContext* m_ctx;

    /**
      * Metrics for measuring and profiling build times and memory usage.
      **/
    F32 m_cacheBuildTimeMs;
    I32 m_cacheCompressedSize;
    I32 m_cacheRawSize;
    I32 m_cacheLayerCount;
    I32 m_cacheBuildMemUsage;

    /**
      * Configuration parameters.
      **/
    I32 m_maxTiles;
    I32 m_maxPolysPerTile;
    I32 m_tileSize;

    F32 m_cellSize;

    /**
      * Size of the tile grid (x dimension)
      **/
    I32 m_tw;
    /**
      * Size of the tile grid (y dimension)
      **/
    I32 m_th;

    static const I32 TILECACHESET_MAGIC =
        'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';  //'TSET';
    static const I32 TILECACHESET_VERSION = 2;

    struct TileCacheSetHeader {
        I32 magic;
        I32 version;
        I32 numTiles;
        dtNavMeshParams meshParams;
        dtTileCacheParams cacheParams;
        rcConfig recastConfig;
    };

    struct TileCacheTileHeader {
        dtCompressedTileRef tileRef;
        I32 dataSize;
    };
};
};  // namespace Navigation
};  // namespace Divide

#endif
#endif
