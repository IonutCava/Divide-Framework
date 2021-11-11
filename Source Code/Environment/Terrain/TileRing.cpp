// Copyright (c) 2011 NVIDIA Corporation. All rights reserved.
//
// TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
// *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
// OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, NONINFRINGEMENT,IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA 
// OR ITS SUPPLIERS BE  LIABLE  FOR  ANY  DIRECT, SPECIAL,  INCIDENTAL,  INDIRECT,  OR  
// CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS 
// OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY 
// OTHER PECUNIARY LOSS) ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE, 
// EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
//
// Please direct any bugs or questions to SDKFeedback@nvidia.com

#include "stdafx.h"
#include "Headers/TileRing.h"

#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h"
#include "Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h"

namespace Divide {

TileRing::TileRing(const I32 holeWidth, const I32 outerWidth, const F32 tileSize)  noexcept :
	_tileCount(outerWidth*outerWidth - holeWidth*holeWidth),
	_tileSize(tileSize), 
	_holeWidth(holeWidth), // No remainder - see assert below.
	_outerWidth(outerWidth),
	_ringWidth((outerWidth - holeWidth) / 2)
{
	assert((outerWidth - holeWidth) % 2 == 0);
}

bool TileRing::InRing(const I32 x, const I32 y) const noexcept
{
	assert(x >= 0 && x < _outerWidth);
	assert(y >= 0 && y < _outerWidth);
	return x < _ringWidth || y < _ringWidth || x >= _outerWidth - _ringWidth || y >= _outerWidth - _ringWidth;
}

void TileRing::AssignNeighbourSizes(const I32 x, const I32 y, Adjacency* pAdj) const noexcept
{
	pAdj->neighbourPlusX  = 1.0f;
	pAdj->neighbourPlusY  = 1.0f;
	pAdj->neighbourMinusX = 1.0f;
	pAdj->neighbourMinusY = 1.0f;

	// TBD: these aren't necessarily 2x different.  Depends on the relative tiles sizes supplied to ring ctors.
	constexpr F32 innerNeighbourSize = 0.5f;
	constexpr F32 outerNeighbourSize = 2.0f;

	// Inner edges abut tiles that are smaller.  (But not on the inner-most.)
	if (_holeWidth > 0)
	{
		if (y >= _ringWidth && y < _outerWidth-_ringWidth)
		{
			if (x == _ringWidth-1)
			{
				pAdj->neighbourPlusX  = innerNeighbourSize;
			}
			if (x == _outerWidth - _ringWidth)
			{
				pAdj->neighbourMinusX = innerNeighbourSize;
			}
		}
		if (x >= _ringWidth && x < _outerWidth - _ringWidth)
		{
			if (y == _ringWidth-1)
			{
				pAdj->neighbourPlusY  = innerNeighbourSize;
			}
			if (y == _outerWidth - _ringWidth)
			{
				pAdj->neighbourMinusY = innerNeighbourSize;
			}
		}
	}

	// Outer edges abut tiles that are larger.  We could skip this on the outer-most ring.  But it will
	// make almost zero visual or perf difference.
	if (x == 0) 
	{
		pAdj->neighbourMinusX = outerNeighbourSize;
	}
	if (y == 0) 
	{
		pAdj->neighbourMinusY = outerNeighbourSize;
	}
	if (x == _outerWidth - 1) 
	{
		pAdj->neighbourPlusX  = outerNeighbourSize;
	}
	if (y == _outerWidth - 1) 
	{
		pAdj->neighbourPlusY  = outerNeighbourSize;
	}
}

vector<TileRing::InstanceData> TileRing::createInstanceDataVB(const I32 ringID)
{
	vector<InstanceData> ret(tileCount());

	I32 index = 0;
	const F32 halfWidth = 0.5f * to_F32(_outerWidth);
	for (I32 y = 0; y < _outerWidth; ++y)
	{
		for (I32 x = 0; x < _outerWidth; ++x)
		{
			if (InRing(x,y))
			{
				ret[index].data.positionX = tileSize() * (to_F32(x) - halfWidth);
				ret[index].data.positionZ = tileSize() * (to_F32(y) - halfWidth);
				ret[index].data.tileScale = tileSize();
				ret[index].data.ringID = to_F32(ringID);
				AssignNeighbourSizes(x, y, &ret[index].adjacency);
				index++;
			}
		}
	}
	assert(index == tileCount());
	return ret;
}

} //namespace Divide