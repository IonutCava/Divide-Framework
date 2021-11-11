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
#ifndef _NAV_MESH_CONFIG_H_
#define _NAV_MESH_CONFIG_H_

#include "NavMeshDefines.h"

/*http://www.ogre3d.org/forums/viewtopic.php?f=11&t=69781&sid=2439989b4c0208780a353e4c90f9864b
 */

namespace Divide {
namespace AI {

class NavigationMeshConfig {
   public:
    NavigationMeshConfig()
    {
        eval();
    }

    /*****************
      * Rasterization
     *****************/
    void setCellSize(const F32 cellSize) {
        this->_cellSize = cellSize;
        eval();
    }

    void setCellHeight(const F32 cellHeight) {
        this->_cellHeight = cellHeight;
        eval();
    }

    void setTileSize(const I32 tileSize) noexcept { 
        this->_tileSize = tileSize;
    }

    /*****************
      * Agent
     *****************/
    void setAgentHeight(const F32 agentHeight) {
        this->_agentHeight = agentHeight;
        eval();
    }

    void setAgentRadius(const F32 agentRadius) {
        this->_agentRadius = agentRadius;
        eval();
    }

    void setAgentMaxClimb(const F32 agentMaxClimb) {
        this->_agentMaxClimb = agentMaxClimb;
        eval();
    }

    void setAgentMaxSlope(const F32 agentMaxSlope) noexcept {
        this->_agentMaxSlope = agentMaxSlope;
    }

    /*****************
      * Region
     *****************/
    void setRegionMinSize(const I32 regionMinSize) {
        this->_regionMinSize = regionMinSize;
        eval();
    }

    void setRegionMergeSize(const I32 regionMergeSize) {
        this->_regionMergeSize = regionMergeSize;
        eval();
    }

    /*****************
      * Polygonization
     *****************/
    void setEdgeMaxLen(const I32 edgeMaxLength) {
        this->_edgeMaxLen = edgeMaxLength;
        eval();
    }

    void setEdgeMaxError(const F32 edgeMaxError) noexcept {
        this->_edgeMaxError = edgeMaxError;
    }

    void setVertsPerPoly(const I32 vertsPerPoly) noexcept {
        this->_vertsPerPoly = vertsPerPoly;
    }

    /*****************
      * Detail mesh
     *****************/
    void setDetailSampleDist(const F32 detailSampleDist) {
        this->_detailSampleDist = detailSampleDist;
        eval();
    }

    void setDetailSampleMaxError(const F32 detailSampleMaxError) {
        this->_detailSampleMaxError = detailSampleMaxError;
        eval();
    }

    void setKeepInterResults(const bool keepInterResults) noexcept {
        this->_keepInterResults = keepInterResults;
    }

    /*********************************************************************
      * Override derived parameters (params set in the eval function)
     *********************************************************************/

    void base_setWalkableHeight(const I32 walkableHeight) noexcept {
        this->_walkableHeight = walkableHeight;
    }

    void base_setWalkableClimb(const I32 walkableClimb) noexcept {
        this->_walkableClimb = walkableClimb;
    }

    void base_setWalkableRadius(const I32 walkableRadius) noexcept {
        this->_walkableRadius = walkableRadius;
    }

    void base_setMaxEdgeLen(const I32 maxEdgeLen) noexcept {
        this->_maxEdgeLen = maxEdgeLen;
    }

    void base_setMinRegionArea(const I32 minRegionArea) noexcept {
        this->_minRegionArea = minRegionArea;
    }

    void base_setMergeRegionArea(const I32 mergeRegionArea) noexcept {
        this->_mergeRegionArea = mergeRegionArea;
    }

    void base_setDetailSampleDist(const F32 detailSampleDist) noexcept {
        this->_base_detailSampleDist = detailSampleDist;
    }

    void base_setDetailSampleMaxError(const F32 detailSampleMaxError) noexcept {
        this->_base_detailSampleMaxError = detailSampleMaxError;
    }

    [[nodiscard]] F32 getCellSize()             const noexcept { return _cellSize; }
    [[nodiscard]] F32 getCellHeight()           const noexcept { return _cellHeight; }
    [[nodiscard]] I32 getTileSize()             const noexcept { return _tileSize; }
    [[nodiscard]] F32 getAgentMaxSlope()        const noexcept { return _agentMaxSlope; }
    [[nodiscard]] F32 getAgentHeight()          const noexcept { return _agentHeight; }
    [[nodiscard]] F32 getAgentMaxClimb()        const noexcept { return _agentMaxClimb; }
    [[nodiscard]] F32 getAgentRadius()          const noexcept { return _agentRadius; }
    [[nodiscard]] I32 getEdgeMaxLen()           const noexcept { return _edgeMaxLen; }
    [[nodiscard]] F32 getEdgeMaxError()         const noexcept { return _edgeMaxError; }
    [[nodiscard]] I32 getRegionMinSize()        const noexcept { return _regionMinSize; }
    [[nodiscard]] I32 getRegionMergeSize()      const noexcept { return _regionMergeSize; }
    [[nodiscard]] I32 getVertsPerPoly()         const noexcept { return _vertsPerPoly; }
    [[nodiscard]] F32 getDetailSampleDist()     const noexcept { return _detailSampleDist; }
    [[nodiscard]] F32 getDetailSampleMaxError() const noexcept { return _detailSampleMaxError; }
    [[nodiscard]] bool getKeepInterResults()    const noexcept { return _keepInterResults; }

    [[nodiscard]] I32 base_getWalkableHeight()       const noexcept { return _walkableHeight; }
    [[nodiscard]] I32 base_getWalkableClimb()        const noexcept { return _walkableClimb; }
    [[nodiscard]] I32 base_getWalkableRadius()       const noexcept { return _walkableRadius; }
    [[nodiscard]] I32 base_getMaxEdgeLen()           const noexcept { return _maxEdgeLen; }
    [[nodiscard]] I32 base_getMinRegionArea()        const noexcept { return _minRegionArea; }
    [[nodiscard]] I32 base_getMergeRegionArea()      const noexcept { return _mergeRegionArea; }
    [[nodiscard]] I32 base_getDetailSampleDist()     const noexcept { return to_I32(_base_detailSampleDist); }
    [[nodiscard]] I32 base_getDetailSampleMaxError() const noexcept { return to_I32(_base_detailSampleMaxError); }

   private:
    /**
      * Derive non-directly set parameters
      * This is the default behaviour and these parameters can be overridden using
      * base_ setters.
      **/
    void eval() {
        _walkableHeight = to_I32(ceilf(_agentHeight / _cellHeight));
        _walkableClimb = to_I32(floorf(_agentMaxClimb / _cellHeight));
        _walkableRadius = to_I32(ceilf(_agentRadius / _cellSize));
        _maxEdgeLen = to_I32(_edgeMaxLen / _cellSize);
        _minRegionArea = to_I32(rcSqr(_regionMinSize));  // Note: area = size*size
        _mergeRegionArea = to_I32(rcSqr(_regionMergeSize));  // Note: area = size*size
        _base_detailSampleDist = _detailSampleDist < 0.9f ? 0 : _cellSize * _detailSampleDist;
        _base_detailSampleMaxError = _cellHeight * _detailSampleMaxError;
    }

    /** Tilesize is the number of (recast) cells per tile. (a multiple of 8 between 16 and 128) */
    I32 _tileSize = 48;

    /**
      * Cellsize (cs) is the width and depth resolution used when sampling the source geometry.
      * The width and depth of the cell columns that make up voxel fields.
      * Cells are laid out on the width/depth plane of voxel fields.
      * Width is associated with the x-axis of the source geometry. Depth is associated with the z-axis.
      * A lower value allows for the generated meshes to more closely match the source geometry,
      * but at a higher processing and memory cost.
      * The xz-plane cell size to use for fields. [Limit: > 0] [Units: wu].
      * cs and ch define voxel/grid/cell size.
      * So their values have significant side effects on all parameters defined in voxel units.
      * The minimum value for this parameter depends on the platform's floating point accuracy,
      * with the practical minimum usually around 0.05.
      **/
    F32 _cellSize = 0.3f;

    /**
      * Cellheight (ch) is the height resolution used when sampling the source geometry.
      * The height of the voxels in voxel fields.
      * Height is associated with the y-axis of the source geometry.
      * A smaller value allows for the final meshes to more closely match the source geometry
      * at a potentially higher processing cost.
      * (Unlike cellSize, using a lower value for cellHeight does not significantly increase memory use.)
      *
      * The y-axis cell size to use for fields. [Limit: > 0] [Units: wu].
      * cs and ch define voxel/grid/cell size. So their values have significant side effects
      * on all parameters defined in voxel units.
      * The minimum value for this parameter depends on the platform's floating point accuracy,
      * with the practical minimum usually around 0.05.
      *
      * Setting ch lower will result in more accurate detection of areas the agent can still pass under,
      * as min walkable height is discretisized
      * in number of cells. Also walkableClimb's precision is affected by ch in the same way,
      * along with some other parameters.
      **/
    F32 _cellHeight = 0.2f;

    /**
      * The maximum slope that is considered traversable (in degrees).
      * [Limits: 0 <= value < 90]
      * The practical upper limit for this parameter is usually around 85 degrees.
      *
      * Also called maxTraversableSlope
      **/
    F32 _agentMaxSlope = 20;

    /**
      * The height of an agent. Defines the minimum height that
      * agents can walk under. Parts of the navmesh with lower ceilings
      * will be pruned off.
      *
      * This parameter serves at setting walkableHeight (minTraversableHeight) parameter,
      * precision of this parameter is determined by cellHeight (ch).
      **/
    F32 _agentHeight = 2.5f;

    /**
      * The Maximum ledge height that is considered to still be traversable.
      * This parameter serves at setting walkableClimb (maxTraversableStep) parameter,
      * precision of this parameter is determined by cellHeight (ch).
      * [Limit: >=0]
      * Allows the mesh to flow over low lying obstructions such as curbs and up/down stairways.
      * The value is usually set to how far up/down an agent can step.
      **/
    F32 _agentMaxClimb = 1;

    /**
      * The radius on the xz (ground) plane of the circle that describes the agent (character) size.
      * Serves at setting walkableRadius (traversableAreaBorderSize) parameter,
      * the precision of walkableRadius is affected by cellSize (cs).
      *
      * This parameter is also used by DetourCrowd to determine the area other agents have to avoid
      * in order not to collide with an agent.
      * The distance to erode/shrink the walkable area of the heightfield away from obstructions.
      * [Limit: >=0]
      *
      * In general, this is the closest any part of the final mesh should get to an obstruction
      * in the source geometry. It is usually set to the maximum agent radius.
      * While a value of zero is legal, it is not recommended and can result in odd edge case issues.
      *
      **/
    F32 _agentRadius = 0.5f;

    /**
      * The maximum allowed length for contour edges along the border of the mesh.
      * [Limit: >=0]
      * Extra vertices will be inserted as needed to keep contour edges below this length.
      * A value of zero effectively disables this feature.
      * Serves at setting maxEdgeLen, the precision of maxEdgeLen is affected by cellSize (cs).
      **/
    I32 _edgeMaxLen = 12;

    /**
      * The maximum distance a simplfied contour's border edges should deviate the original raw contour.
      * (edge matching) [Limit: >=0] [Units: wu]
      * The effect of this parameter only applies to the xz-plane.
      *
      * Also called maxSimplificationError or edgeMaxDeviation
      * The maximum distance the edges of meshes may deviate from the source geometry.
      * A lower value will result in mesh edges following the xz-plane geometry contour more accurately
      * at the expense of an increased triangle count.
      * A value to zero is not recommended since it can result in a large increase in the number of
      * polygons in the final meshes at a high processing cost.
      **/
    F32 _edgeMaxError = 1.3f;

    /**
      * The minimum number of cells allowed to form isolated island areas (size).
      * [Limit: >=0]
      * Any regions that are smaller than this area will be marked as unwalkable.
      * This is useful in removing useless regions that can sometimes form on geometry such as table tops, box tops, etc.
      * Serves at setting minRegionArea, which will be set to the square of this value
      * (the regions are square, thus area=size*size)
      **/
    I32 _regionMinSize = 50;

    /**
      * Any regions with a span count smaller than this value will, if possible, be merged with larger regions.
      * [Limit: >=0] [Units: vx]
      * Serves at setting MergeRegionArea, which will be set to the square of this value
      * (the regions are square, thus area=size*size)
      **/
    I32 _regionMergeSize = 20;

    /**
      * The maximum number of vertices allowed for polygons generated during the contour to polygon conversion process.
      * [Limit: >= 3]
      * If the mesh data is to be used to construct a Detour navigation mesh,
      * then the upper limit is limited to <= DT_VERTS_PER_POLYGON (=6).
      *
      * Also called maxVertsPerPoly
      * The maximum number of vertices per polygon for polygons generated during the voxel to polygon conversion process.
      * Higher values increase processing cost, but can also result in better formed polygons in the final meshes.
      * A value of around 6 is generally adequate with diminishing returns for higher values.
      **/
    I32 _vertsPerPoly = DT_VERTS_PER_POLYGON;

    /**
      * Sets the sampling distance to use when generating the detail mesh.
      * (For height detail only.) [Limits: 0 or >= 0.9] [Units: wu]
      *
      * Also called contourSampleDistance
      * Sets the sampling distance to use when matching the detail mesh to the surface of the original geometry.
      * Impacts how well the final detail mesh conforms to the surface contour of the original geometry.
      * Higher values result in a detail mesh which conforms more closely to the original geometry's surface
      * at the cost of a higher final triangle count and higher processing cost.
      * Setting this argument to less than 0.9 disables this functionality.
      **/
    F32 _detailSampleDist = 6;

    /**
      * The maximum distance the detail mesh surface should deviate from heightfield data.
      * (For height detail only.) [Limit: >=0] [Units: wu]
      *
      * Also called contourMaxDeviation
      * The maximum distance the surface of the detail mesh may deviate from the surface of the original geometry.
      * The accuracy is impacted by contourSampleDistance.
      * The value of this parameter has no meaning if contourSampleDistance is set to zero.
      * Setting the value to zero is not recommended since it can result in a large increase in the number of
      * triangles in the final detail mesh at a high processing cost.
      * Stronly related to detailSampleDist (contourSampleDistance).
      **/
    F32 _detailSampleMaxError = 1;

    /**
      * Determines whether intermediary results are stored in OgreRecast class or whether they are
      * removed after navmesh creation.
      **/
    bool _keepInterResults = false;

    /**
      * Minimum height in number of (voxel) cells that the ceiling needs to be
      * for an agent to be able to walk under. Related to cellHeight (ch) and
      * agentHeight.
      *
      * Minimum floor to 'ceiling' height that will still allow the floor area to be considered walkable.
      * [Limit: >= 3] [Units: vx]
      * Permits detection of overhangs in the source geometry that make the geometry below un-walkable.
      * The value is usually set to the maximum agent height.
      *
      * Also called minTraversableHeight
      * This value should be at least two times the value of cellHeight in order to get good results.
      **/
    I32 _walkableHeight;

    /**
      * Maximum ledge height that is considered to still be traversable, in number of cells (height).
      * [Limit: >=0] [Units: vx].
      * Allows the mesh to flow over low lying obstructions such as curbs and up/down stairways.
      * The value is usually set to how far up/down an agent can step.
      *
      * Also called maxTraversableStep
      * Represents the maximum ledge height that is considered to still be traversable.
      * Prevents minor deviations in height from improperly showing as obstructions.
      * Permits detection of stair-like structures, curbs, etc.
      **/
    I32 _walkableClimb;

    /**
      * The distance to erode/shrink the walkable area of the heightfield away from obstructions, in cellsize units.
      * [Limit: >=0] [Units: vx]
      * In general, this is the closest any part of the final mesh should get to an obstruction in the source geometry.
      * It is usually set to the maximum agent radius.
      * While a value of zero is legal, it is not recommended and can result in odd edge case issues.
      *
      * Also called traversableAreaBorderSize
      * Represents the closest any part of a mesh can get to an obstruction in the source geometry.
      * Usually this value is set to the maximum bounding radius of agents utilizing the meshes for navigation decisions.
      *
      * This value must be greater than the cellSize to have an effect.
      * The actual border will be larger around ledges if ledge clipping is enabled.
      * See the clipLedges parameter for more information.
      * The actual border area will be larger if smoothingTreshold is > 0.
      * See the smoothingThreshold parameter for more information.
      **/
    I32 _walkableRadius;

    /**
      * The maximum allowed length for contour edges along the border of the mesh.
      * [Limit: >=0] [Units: vx].
      * Extra vertices will be inserted as needed to keep contour edges below this length.
      * A value of zero effectively disables this feature.
      *
      * Also called maxEdgeLength
      * The maximum length of polygon edges that represent the border of meshes.
      * More vertices will be added to border edges if this value is exceeded for a particular edge.
      * In certain cases this will reduce the number of long thin triangles.
      * A value of zero will disable this feature.
      **/
    I32 _maxEdgeLen;

    /**
      * The minimum number of cells allowed to form isolated island areas.
      * [Limit: >=0] [Units: vx].
      * Any regions that are smaller than this area will be marked as unwalkable.
      * This is useful in removing useless regions that can sometimes form on geometry such as table tops, box tops, etc.
      *
      * Also called minUnconnectedRegionSize
      * The minimum region size for unconnected (island) regions.
      * The value is in voxels.
      * Regions that are not connected to any other region and are smaller than this size will be
      * culled before mesh generation. I.e. They will no longer be considered traversable.
      **/
    I32 _minRegionArea;

    /**
      * Any regions with a span count smaller than this value will, if possible, be merged with larger regions.
      * [Limit: >=0] [Units: vx]
      *
      * Also called mergeRegionSize or mergeRegionArea
      * Any regions smaller than this size will, if possible, be merged with larger regions.
      * Value is in voxels.
      * Helps reduce the number of small regions. This is especially an issue in diagonal path regions
      * where inherent faults in the region generation algorithm can result in unnecessarily small regions.
      * Small regions are left unchanged if they cannot be legally merged with a neighbor region.
      * (E.g. Merging will result in a non-simple polygon.)
      **/
    I32 _mergeRegionArea;

    /**
      * Sets the sampling distance to use when generating the detail mesh.
      * (For height detail only.) [Limits: 0 or >= 0.9] [Units: wu]
      *
      * Also called contourSampleDistance
      * Sets the sampling distance to use when matching the detail mesh to the surface of the original geometry.
      * Impacts how well the final detail mesh conforms to the surface contour of the original geometry.
      * Higher values result in a
      * detail mesh which conforms more closely to the original geometry's surface at the cost of
      * a higher final triangle count and higher processing cost.
      * Setting this argument to less than 0.9 disables this functionality.
      *
      * The difference between this parameter and edge matching (edgeMaxError) is that this parameter
      * operates on the height rather than the xz-plane.
      * It also matches the entire detail mesh surface to the contour of the original geometry.
      * Edge matching only matches edges of meshes to the contour of the original geometry.
      **/
    F32 _base_detailSampleDist;

    /**
      * The maximum distance the detail mesh surface should deviate from heightfield data.
      * (For height detail only.) [Limit: >=0] [Units: wu]
      *
      * Also called contourMaxDeviation
      * The maximum distance the surface of the detail mesh may deviate from the surface of the original geometry.
      * The accuracy is impacted by contourSampleDistance (detailSampleDist).
      * The value of this parameter has no meaning if contourSampleDistance is set to zero.
      * Setting the value to zero is not recommended since it can result in a large increase in the number of
      * triangles in the final detail mesh at a high processing cost.
      * This parameter has no impact if contourSampleDistance is set to zero.
      **/
    F32 _base_detailSampleMaxError;
};

}  // namespace AI
}  // namespace Divide

#endif
