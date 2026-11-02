#include "Noise.h"

using namespace CurlNoise;

static const unsigned int s_kHashIndexMask = 255;
static const unsigned int s_kGradientIndexMask = 15;

// Repeat perlin's permutation of [0, 255] twice so we don't need to use the mod operator while indexing
static const int s_Hash[] =
{
	151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
	140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
	247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
	57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
	74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
	60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
	65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
	200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
	52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
	207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
	119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
	129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
	218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
	81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
	184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
	222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,

	151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
	140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
	247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
	57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
	74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
	60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
	65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
	200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
	52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
	207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
	119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
	129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
	218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
	81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
	184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
	222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
};


static const Vector3 s_Gradients3D[]  =
{
	// center of cube to center of each of the 12 edges of a cube
	Vector3(1.0f, 1.0f, 0.0f),
	Vector3(-1.0f, 1.0f, 0.0f),
	Vector3(1.0f,-1.0f, 0.0f),
	Vector3(-1.0f,-1.0f, 0.0f),
	Vector3(1.0f, 0.0f, 1.0f),
	Vector3(-1.0f, 0.0f, 1.0f),
	Vector3(1.0f, 0.0f,-1.0f),
	Vector3(-1.0f, 0.0f,-1.0f),
	Vector3(0.0f, 1.0f, 1.0f),
	Vector3(0.0f,-1.0f, 1.0f),
	Vector3(0.0f, 1.0f,-1.0f),
	Vector3(0.0f,-1.0f,-1.0f),

	// Repeat some to skew distribution
	Vector3(1.0f, 1.0f, 0.0f),
	Vector3(-1.0f, 1.0f, 0.0f),
	Vector3(0.0f,-1.0f, 1.0f),
	Vector3(0.0f,-1.0f,-1.0f)
};


//-----------------------------------------------------------------------------------------------
// The PerlinNoise3 implementation calculates a noise value in the range [-1f, 1f] given a 3D position & frequency.
// It also calculates the analytical derivative (change wrt x, y & z) which can be used to quickly calculate the curl,
// when the potential field doesn't need to be modified.
NoiseSample 
PerlinNoise3::EvaluateNoise(const Vector3& point, float frequency)
{
	Vector3 p = point * frequency;
	int ix0 = static_cast<int> ( floorf(p.getX()) );
	int iy0 = static_cast<int> ( floorf(p.getY()) );
	int iz0 = static_cast<int> ( floorf(p.getZ()) );
	float tx0 = p.getX() - ix0;
	float ty0 = p.getY() - iy0;
	float tz0 = p.getZ() - iz0;
	float tx1 = tx0 - 1.0f;
	float ty1 = ty0 - 1.0f;
	float tz1 = tz0 - 1.0f;
	ix0 &= s_kHashIndexMask;
	iy0 &= s_kHashIndexMask;
	iz0 &= s_kHashIndexMask;
	int ix1 = ix0 + 1;
	int iy1 = iy0 + 1;
	int iz1 = iz0 + 1;

	int h0 = s_Hash[ix0];
	int h1 = s_Hash[ix1];
	int h00 = s_Hash[h0 + iy0];
	int h10 = s_Hash[h1 + iy0];
	int h01 = s_Hash[h0 + iy1];
	int h11 = s_Hash[h1 + iy1];
	
	// gradients at each of the 8 lattice points
	Vector3 g000 = s_Gradients3D[s_Hash[h00 + iz0] & s_kGradientIndexMask];
	Vector3 g100 = s_Gradients3D[s_Hash[h10 + iz0] & s_kGradientIndexMask];
	Vector3 g010 = s_Gradients3D[s_Hash[h01 + iz0] & s_kGradientIndexMask];
	Vector3 g110 = s_Gradients3D[s_Hash[h11 + iz0] & s_kGradientIndexMask];
	Vector3 g001 = s_Gradients3D[s_Hash[h00 + iz1] & s_kGradientIndexMask];
	Vector3 g101 = s_Gradients3D[s_Hash[h10 + iz1] & s_kGradientIndexMask];
	Vector3 g011 = s_Gradients3D[s_Hash[h01 + iz1] & s_kGradientIndexMask];
	Vector3 g111 = s_Gradients3D[s_Hash[h11 + iz1] & s_kGradientIndexMask];

	float v000 = Dot(g000, tx0, ty0, tz0);
	float v100 = Dot(g100, tx1, ty0, tz0);
	float v010 = Dot(g010, tx0, ty1, tz0);
	float v110 = Dot(g110, tx1, ty1, tz0);
	float v001 = Dot(g001, tx0, ty0, tz1);
	float v101 = Dot(g101, tx1, ty0, tz1);
	float v011 = Dot(g011, tx0, ty1, tz1);
	float v111 = Dot(g111, tx1, ty1, tz1);

	float dtx = SmoothDerivative(tx0);
	float dty = SmoothDerivative(ty0);
	float dtz = SmoothDerivative(tz0);
	float tx = Smooth(tx0);
	float ty = Smooth(ty0);
	float tz = Smooth(tz0);

	float a = v000;
	float b = v100 - v000;
	float c = v010 - v000;
	float d = v001 - v000;
	float e = v110 - v010 - v100 + v000;
	float f = v101 - v001 - v100 + v000;
	float g = v011 - v001 - v010 + v000;
	float h = v111 - v011 - v101 + v001 - v110 + v010 + v100 - v000;

	Vector3 da = g000;
	Vector3 db = g100 - g000;
	Vector3 dc = g010 - g000;
	Vector3 dd = g001 - g000;
	Vector3 de = g110 - g010 - g100 + g000;
	Vector3 df = g101 - g001 - g100 + g000;
	Vector3 dg = g011 - g001 - g010 + g000;
	Vector3 dh = g111 - g011 - g101 + g001 - g110 + g010 + g100 - g000;

	NoiseSample sample;
	sample.value = a + b * tx + (c + e * tx) * ty + (d + f * tx + (g + h * tx) * ty) * tz;
	
	// calculate derivative analytically
	sample.derivative = da + db * tx + (dc + de * tx) * ty + (dd + df * tx + (dg + dh * tx) * ty) * tz;

	Vector3 delta = Vector3((b + e * ty + (f + h * ty) * tz) * dtx,
							(c + e * tx + (g + h * tx) * tz) * dty,
							(d + f * tx + (g + h * tx) * ty) * dtz);

	sample.derivative += delta;
	sample.derivative *= frequency;
	return sample;
}

//-----------------------------------------------------------------------------------------------
// Wrapper that sums multiple octaves at noise; see decl for defaults
NoiseSample 
PerlinNoise3::Noise(Vector3 point, float frequency, int octaves, float lacunarity, float persistence)
{
	NoiseSample sum = EvaluateNoise(point, frequency);
	float amplitude = 1.0f;
	float range = 1.0f;
	for (int o = 1; o < octaves; o++) {
		frequency *= lacunarity;
		amplitude *= persistence;
		range += amplitude;
		sum += EvaluateNoise(point, frequency) * amplitude;
	}
	return sum * (1.0f / range);
}