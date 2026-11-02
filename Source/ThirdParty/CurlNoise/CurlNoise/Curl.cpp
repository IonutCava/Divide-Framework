// This implementation of Curl Noise is based on the following (fantastic) references:
//R1: http://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf
//R2: http://prideout.net/blog/?p=63
//R3: http://catlikecoding.com/unity/tutorials/noise-derivatives/

#include "Curl.h"
#include "Noise.h"
#include <float.h> // FLT_MAX

// For writing to Unity console
#include <string>
using namespace std;

// do nothing
void DebugLog([[maybe_unused]] const char* str) {}

void DebugLog([[maybe_unused]] string str) {}

using namespace Vectormath::Aos;
//string to_string(Vector3 v) {}

//-------------------------------------------------------------------------------------------------------------
namespace // unnamed to hide implementation functions & separate them from API code
{
	using namespace Vectormath::Aos;
	using CurlNoise::Volume;
	using CurlNoise::CurlSettings;

	static CurlSettings s_Settings;

	struct DistanceGradient
	{
		float	distance;
		Vector3 gradient; // normal
	};

	// Computes world space distance from 'p' (world space) to nearest obstacle primitive and returns a hacky approximation
	// of the normal at that point
	DistanceGradient SampleObstacleDistanceAndGradientCheap(Vector3 p, const Volume *pColliders, unsigned int length)
	{
		float d = FLT_MAX;
		unsigned int cminId = 0;
		// TODO This is a naive brute force way. We could do a space partitioning approach to
		// quickly eliminate those obstacles that are farther away
		for (unsigned int cId = 0; cId < length; cId++)
		{
			float dc = pColliders[cId].DistanceToSurface(p);
			if (dc < d)
			{
				d = dc;
				cminId = cId;
			}
		}

		DistanceGradient dg;
		dg.distance = d;

		if (length > 0)
			dg.gradient = p - pColliders[cminId].GetWorldPos(); // xform to object space w/o scaling
		else
			dg.gradient = Vector3(0.f, 0.f, 0.f);

		return dg;
	}


	//-------------------------------------------------------------------------------------------------------------
	// Blend noise based potential with distance gradient when near an obstacle	
	// Based on sec 2.4 of https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf
	Vector3 ConstrainedPotential(Vector3 potential, Vector3 distanceGradient, float alpha)
	{
		float dp = dot(potential, distanceGradient);
		return alpha * potential + (1 - alpha) * distanceGradient * dp;
	}


	//-------------------------------------------------------------------------------------------------------------
	// Computes world space distance to the nearest obstacle from a world space position 'p' and the gradient at that point
	// This function determines the gradient by moving 'p' and calculating the change in the distance field and is
	// expensive as a result. 
	// Use SampleObstacleDistanceAndGradientCheap(..) for a cheaper alternative
	DistanceGradient ComputeDistanceAndGradientExpensive(Vector3 p, const Volume *pColliders, unsigned int length)
	{
		const float e = 0.01f;
		Vector3 dx(e, 0, 0);
		Vector3 dy(0, e, 0);
		Vector3 dz(0, 0, e);

		float d		= SampleObstacleDistanceAndGradientCheap(p, pColliders, length).distance;			// distance of nearest obstacle
		float dfdx	= SampleObstacleDistanceAndGradientCheap(p + dx, pColliders, length).distance - d;	// distance change wrt x
		float dfdy	= SampleObstacleDistanceAndGradientCheap(p + dy, pColliders, length).distance - d;	// distance change wrt y
		float dfdz	= SampleObstacleDistanceAndGradientCheap(p + dz, pColliders, length).distance - d;	// distance change wrt z

		DistanceGradient dg;
		dg.distance = d;
		dg.gradient = normalize(Vector3(dfdx, dfdy, dfdz));
		return dg;
	}


	//-------------------------------------------------------------------------------------------------------------
	// Higher degree polynomial (used by Ken Perlin) that returns a value in the range [0,1]
	// 'r' is the obstacle distance in noise-space (which is equivalent to world space at the moment) TODO fix this
	inline float smooth_step(float r)
	{
		if (r < 0) return 0.f;
		else if (r>1) return 1.f;
		return r*r*r*(10 + r*(-15 + r * 6)); 
		// NOTE 6r^5 - 15r^4 + 10r^3 has a zero 1st and 2nd order derivative at r=0 and r=1
		// The paper by Bridson uses a different function.
	}

	
	// Returns [-1,1]
	inline float ramp(float r)
	{
		float rSmooth = smooth_step((r + 1) / 2);
		return rSmooth*2 - 1; // [0,1] -> [-1,1]
	}


	//-------------------------------------------------------------------------------------------------------------
	using CurlNoise::NoiseSample;
	using CurlNoise::PerlinNoise3;
	// Returns the 3D potential at 'p' in the presence of obstacles
	Vector3 SamplePotential(Vector3 p, const Volume *pColliders, unsigned int length)
	{
		Vector3 psi(0, 0, 0);
		DistanceGradient dg;
		
		if (s_Settings.m_bCheapGradient)
			dg = SampleObstacleDistanceAndGradientCheap(p, pColliders, length);
		else
			dg = ComputeDistanceAndGradientExpensive(p, pColliders, length);

		const auto& obstacleDistance = dg.distance;
		const auto& normal = dg.gradient;

		// Smoothly ramp down the potential based on distance to the obstacle so that it is 0 at its boundary
		float alpha = ramp(obstacleDistance);

		// evaluate 3D noise to get the potential field at p
		NoiseSample ns0 = PerlinNoise3::Noise(p, 1.f, 1, 0.f, 0.f);
		NoiseSample ns1 = PerlinNoise3::Noise(p, 1.f, 1, 0.f, 0.f);
		NoiseSample ns2 = PerlinNoise3::Noise(p, 1.f, 1, 0.f, 0.f);
		psi = Vector3(ns0.value, ns1.value, ns2.value); // noise based 3D potential

		// modulate the calculated potential to handle the inviscid boundary condition
		// i.e., v . n = 0 at the boundary (component of velocity along normal is 0)
		Vector3 psi_new = ConstrainedPotential(psi, normal, alpha);

		return psi_new;
	}

	//-------------------------------------------------------------------------------------------------------------
	// helpers
	Vector3 abs(const Vector3& v)
	{
		return Vector3(fabsf(v.getX()), fabsf(v.getY()), fabsf(v.getZ()));
	}

	Vector3 max(const Vector3& v1, const Vector3& v2)
	{
		return Vector3(fmaxf(v1.getX(), v2.getX()),
			fmaxf(v1.getY(), v2.getY()),
			fmaxf(v1.getZ(), v2.getZ())
			);
	}

	//-------------------------------------------------------------------------------------------------------------
	// Ref: http://iquilezles.org/www/articles/distfunctions/distfunctions.htm
	// Signed distance in world space units from a point 'p' to the surface of a sphere of radius 'r' centered at the origin
	static float sdSphere(Vector3 p, float r)
	{
		return length(p) - r;
	}

	//-------------------------------------------------------------------------------------------------------------
	// Signed distance in world space units from a point 'p' to the surface of a box with extents 'b' centered at the origin
	static float sdBox(Vector3 p, Vector3 b)
	{
		Vector3 d = abs(p) - b;
		float dmax = fmaxf(d.getX(), fmaxf(d.getY(), d.getZ()));
		return fminf(dmax, 0.f) + length(max(d, Vector3(0.f, 0.f, 0.f)));
	}


	//-------------------------------------------------------------------------------------------------------------
	// Signed distance from a point 'p' to the surface of a cylinder of radius 'r' and height 'h' centered at the origin and
	// with its axis along the (object's) Y axis
	// Note: h is not the half-height
	static float sdCappedCylinder(Vector3 p, float r, float h)
	{
		Vector3 pXZ = Vector3(p.getX(), 0.f, p.getZ()); // todo: replace with Vec2

		float d1 = length(pXZ) - r; // xz
		float d2 = fabsf(p.getY()) - h*0.5f; // y

		float d = fminf(fmaxf(d1, d2), 0.f);
		float l = length(Vector3(fmaxf(d1, 0.f), fmaxf(d2, 0.f), 0.f));

		return d + l;
	}	


	//-------------------------------------------------------------------------------------------------------------
	// TODO This function is uber brute force, requiring 12 potential samples for finite differentiation
	// Each potential sample involves:
	// (a) 1 or 4 calls to determine distance and gradient of the nearest obstacle (based on s_Settings.m_bCheapGradient)	
	// (b) 3 perlin noise look ups for the 3D noise
	Vector3 ComputeCurlWithObstacles(Vector3 p, const Volume *pColliders, unsigned int length)
	{
		const float e = 1e-4f;
		Vector3 dx(e, 0, 0);
		Vector3 dy(0, e, 0);
		Vector3 dz(0, 0, e);

		float dfzdy = SamplePotential(p + dy, pColliders, length).getZ() -
					  SamplePotential(p - dy, pColliders, length).getZ();

		float dfydz = SamplePotential(p + dz, pColliders, length).getY() -
					  SamplePotential(p - dz, pColliders, length).getY();

		float dfxdz = SamplePotential(p + dz, pColliders, length).getX() -
					  SamplePotential(p - dz, pColliders, length).getX();

		float dfzdx = SamplePotential(p + dx, pColliders, length).getZ() -
					  SamplePotential(p - dx, pColliders, length).getZ();

		float dfydx = SamplePotential(p + dx, pColliders, length).getY() -
					  SamplePotential(p - dx, pColliders, length).getY();

		float dfxdy = SamplePotential(p + dy, pColliders, length).getX() -
					  SamplePotential(p - dy, pColliders, length).getX();

		return Vector3(dfzdy - dfydz, dfxdz - dfzdx, dfydx - dfxdy) / (2 * e);
	}
} // unnamed namespace


namespace CurlNoise
{
	//-------------------------------------------------------------------------------------------------------------
	// CurlNoise API 
	//-------------------------------------------------------------------------------------------------------------
	// PerlinNoise3 returns a scalar float given a 3D position in space. 
	// To generate a potential force field, we need 3 values, so we use hardcoded offsets
	inline NoiseSample noiseX(Vector3 p) { 
		return PerlinNoise3::Noise(	p, 
									s_Settings.m_Frequency,
									s_Settings.m_NumOctaves,
									s_Settings.m_Lacunarity,
									s_Settings.m_Persistence); 
	}

	inline NoiseSample noiseY(Vector3 p) {
		return PerlinNoise3::Noise(	p + Vector3(31.341f, -43.23f, 12.34f),
									s_Settings.m_Frequency,
									s_Settings.m_NumOctaves,
									s_Settings.m_Lacunarity,
									s_Settings.m_Persistence);
	}

	inline NoiseSample noiseZ(Vector3 p) {
		return PerlinNoise3::Noise(	p + Vector3(-231.341f, 124.23f, -54.34f),
									s_Settings.m_Frequency,
									s_Settings.m_NumOctaves,
									s_Settings.m_Lacunarity,
									s_Settings.m_Persistence);
	}


	//-------------------------------------------------------------------------------------------------------------
	// Computes the curl at 'p' by sampling the potential field generated via 3D noise.
	// The perlin noise calculation takes care of calculating the derivatives analytically
	Vector3 ComputeCurlWithoutObstacles(Vector3 p)
	{
		NoiseSample nsX = noiseX(p);
		NoiseSample nsY = noiseY(p);
		NoiseSample nsZ = noiseZ(p);

		//curl = (dfzdy - dfydz, dfxdz - dfzdx, dfydx - dfxdy)
		Vector3 curl = Vector3(nsZ.derivative.getY() - nsY.derivative.getZ(),
			nsX.derivative.getZ() - nsZ.derivative.getX(),
			nsY.derivative.getX() - nsX.derivative.getY());

		return curl;
	}


	//-------------------------------------------------------------------------------------------------------------
	// Computes the curl at 'p' by sampling the potential field generated via 3D noise, while respecting 
	// obstacle boundaries.
	Vector3 ComputeCurl(Vector3 p, const Volume *pColliders, unsigned int length)
	{
		auto dg = SampleObstacleDistanceAndGradientCheap(p, pColliders, length);
		float rampD = ramp(dg.distance); //TODO: account for what a unit-step in noise dimensions is
		Vector3 curl;

		if (rampD >= 1.f) // no need to account for obstacles to modify the potential
		{
			curl = ComputeCurlWithoutObstacles(p);
		}
		else
		{			
			//DebugLog("Obstacle neat particle at " + to_string(p) + ". dist = " + to_string(obstacleDistance));
			curl = ComputeCurlWithObstacles(p, pColliders, length);
		}

		return curl;		
	}


	//-------------------------------------------------------------------------------------------------------------
	// Set the parameters to control look/cost of noise/curl
	void SetCurlSettings(const CurlSettings& settings)
	{
		s_Settings = settings;
	}

	//-------------------------------------------------------------------------------------------------------------	
	using CurlNoise::float3;
	float3 Vector3ToFloat3(const Vector3& v)
	{
		float3 f;
		f.val[0] = v.getX();
		f.val[1] = v.getY();
		f.val[2] = v.getZ();

		return f;
	}

	//-------------------------------------------------------------------------------------------------------------
	// Wrappers to use with Unity
	//-------------------------------------------------------------------------------------------------------------
	float3 ComputeCurlBruteForce(Vector3 p, Volume *pColliders, unsigned int length)
	{
		Vector3 curl = ComputeCurlWithObstacles(p, pColliders, length);
		return Vector3ToFloat3(curl);
	}


	// This function calculates the potential derivatives analytically, which are then used to compute the curl.
	// It does not respect the presence of obstacles.
	float3 ComputeCurlNoBoundaries(Vector3 p)
	{
		Vector3 curl = ComputeCurlWithoutObstacles(p);
		return Vector3ToFloat3(curl);

	}
	

	float3 ComputeCurlNonBruteForce(Vector3 p, Volume *pColliders, unsigned int length)
	{
		Vector3 curl = ComputeCurl(p, pColliders, length);		
		return Vector3ToFloat3(curl);		
	}


	void SetCurlSettings(bool bCheapGradient,
						float frequency,
						unsigned int numOctaves,
						float lacunarity,
						float persistence)
	{
		s_Settings.m_bCheapGradient = bCheapGradient;
		s_Settings.m_Frequency = frequency;
		s_Settings.m_NumOctaves = numOctaves;
		s_Settings.m_Lacunarity = lacunarity;
		s_Settings.m_Persistence = persistence;
	}

	//-------------------------------------------------------------------------------------------------------------
	// Volume
	//-------------------------------------------------------------------------------------------------------------
	// Creates a sphere collider primitive
	// The matrix expected is the inverse of the object to world matrix consisting of only translation and rotation. 
	// The radius 'r' is in world space units
	Volume::Volume(const Matrix4& worldToObjectNoScale, float r)
	{
		m_Shape = kSphere;
		m_Extents = Vector3(r, r, r);
		m_WorldToObject = worldToObjectNoScale;
	}


	// Creates a box collider primitive
	// The matrix expected is the inverse of the object to world matrix consisting of only translation and rotation. 
	// The extents are the half-lbh dimensions in world space
	Volume::Volume(const Matrix4& worldToObjectNoScale, const Vector3& extents)
	{
		m_Shape = kBox;
		m_Extents = extents;
		m_WorldToObject = worldToObjectNoScale;
	}


	// Creates a capped cylinder collider primitive
	// The matrix expected is the inverse of the object to world matrix consisting of only translation and rotation. 
	// The radius and height are in world-space units
	Volume::Volume(const Matrix4& worldToObjectNoScale, float r, float h)
	{
		m_Shape = kCylinder;
		m_Extents = Vector3(r, h, 0.f);
		m_WorldToObject = worldToObjectNoScale;
	}


	// Updates the collider's inverse transform (worldToObject)
	// The matrix expected is the inverse of the object to world matrix consisting of only translation and rotation. 
	void Volume::SetWorldToObjectTransform(const Matrix4& worldToObjectNoScale)
	{
		m_WorldToObject = worldToObjectNoScale;
	}


	// Returns distance of 'p' (which is in world space) from the surface of the collider
	// Since the transform doesn't account for scaling, the distance returned is in world space units.
	float Volume::DistanceToSurface(const Vector3& p) const
	{
		float d = 0.f;
		Vector4 osPos = m_WorldToObject * Vector4(p, 1.0f); // xform to object space w/o scaling

		switch (m_Shape)
		{
		case kSphere:
			d = sdSphere(osPos.getXYZ(), m_Extents.getX());
			break;

		default:
		case kBox:
			d = sdBox(osPos.getXYZ(), m_Extents);
			break;

		case kCylinder:
			d = sdCappedCylinder(osPos.getXYZ(), m_Extents.getX(), m_Extents.getY());
			break;
		}

		return d;
	}	


	// Returns the world space position of the collider
	Vector3 Volume::GetWorldPos() const
	{
		Vector4 wsPos = m_WorldToObject.getCol3();
		return Vector3(-wsPos.getX(), -wsPos.getY(), -wsPos.getZ());
	}
}
