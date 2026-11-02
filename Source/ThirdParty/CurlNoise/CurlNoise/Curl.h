#pragma once

#include "vmath.hpp" // PORTME: vmath can be replaced with IG's internal math equivalents

#define DLLEXPORT 


namespace CurlNoise
{
	using namespace Vectormath::Aos;

	// This class encapsulates different volume collider primitives for the purpose of distance field estimation
	// It does NOT handle irregular scaling for curved primitives (for ex, ellipsoids, ellipsoid cylinders..) at the moment.
	// PORTME: IG Core already defines different Volume types. It seems to have a distance field function GetSignedDistancePoint(..)
	//         It handles all the primitive shapes defined below, but approximates some of them 
	//		   (for ex, box distance is Manhattan distance to the corner point)
	//		   I'd guess that being tied to SceneObject, that would end up being shared with the vfx system.
	//		   This is just a place holder for proof of concept and testing.	
	class Volume
	{
		enum eShape
		{
			kSphere,
			kCylinder,
			kBox,

			kCount
		};

	public:
		// constructor overloads for various primitive types
		Volume(const Matrix4& worldToObjectNoScale, float r); // sphere
		Volume(const Matrix4& worldToObjectNoScale, const Vector3& extents); // box
		Volume(const Matrix4& worldToObjectNoScale, float r, float h); // cylinder

		float DistanceToSurface(const Vector3& wsPos) const;
		inline void SetWorldToObjectTransform(const Matrix4& m);
		inline Vector3 GetWorldPos() const;

	protected:
		eShape m_Shape;
		Vector3 m_Extents; // world space units; interpreted based on the shape
		Matrix4 m_WorldToObject; // inverse of the object to world matrix consisting of only translation and rotation
	};


	struct CurlSettings
	{
		// true => the gradient is calculated as vector from center of nearest collider to the particle
		// false => the gradient is calculated based on the change of the distance field wrt xyz
		bool			m_bCheapGradient = true; 
		
		//--- See PerlinNoise3::Noise for how the following fields are used ---

		// base frequency to control how often the noise changes per world-space unit		
		float			m_Frequency = 1.f;

		// number of octaves to sum up. each successive octave scales the previous octave's frequency by the lacunarity
		unsigned int	m_NumOctaves = 2;

		// factor by which frequency changes in successive octaves
		float			m_Lacunarity = 2.0f;

		// factor by which amplitude changes in successive octaves
		float			m_Persistence = 0.5f;		
	};

	struct float3
	{
		float val[3];		
	};
	
	float3 ComputeCurlBruteForce(Vector3 wsPos, Volume *pColliders, unsigned int length);

	float3 ComputeCurlNoBoundaries(Vector3 wsPos);

	float3 ComputeCurlNonBruteForce(Vector3 wsPos, Volume *pColliders, unsigned int length);

	void SetCurlSettings(	bool bCheapGradient, 
				float frequency, 
				unsigned int numOctaves, 
				float lacunarity, 
				float persistence);
};
