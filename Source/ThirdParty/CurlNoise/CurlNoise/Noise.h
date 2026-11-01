#pragma once
#include "vmath.hpp"

namespace CurlNoise
{
	using namespace Vectormath::Aos;

	//-----------------------------------------------------------------------------------------------
	// Return structure for noise sampling containing scalar float and 3D derivative
	struct NoiseSample
	{
	public:
		float value;
		Vector3 derivative;

		NoiseSample& operator+= (const NoiseSample& s)
		{
			value += s.value;
			derivative += s.derivative;
			return *this;
		}
	};

	inline NoiseSample operator+(NoiseSample a, float b) {
		a.value += b;
		return a;
	}

	inline NoiseSample operator + (float a, NoiseSample b) {
		b.value += a;
		return b;
	}

	inline NoiseSample operator + (NoiseSample a, NoiseSample b) {
		a.value += b.value;
		a.derivative += b.derivative;
		return a;
	}

	inline NoiseSample operator - (NoiseSample a, float b) {
		a.value -= b;
		return a;
	}

	inline NoiseSample operator - (float a, NoiseSample b) {
		b.value = a - b.value;
		b.derivative = -b.derivative;
		return b;
	}

	inline NoiseSample operator - (NoiseSample a, NoiseSample b) {
		a.value -= b.value;
		a.derivative -= b.derivative;
		return a;
	}

	inline NoiseSample operator * (NoiseSample a, float b) {
		a.value *= b;
		a.derivative *= b;
		return a;
	}

	inline NoiseSample operator * (float a, NoiseSample b) {
		b.value *= a;
		b.derivative *= a;
		return b;
	}

	inline NoiseSample operator * (NoiseSample a, NoiseSample b) {
		a.derivative = a.derivative * b.value + b.derivative * a.value;
		a.value *= b.value;
		return a;
	}

	
	//-----------------------------------------------------------------------------------------------
	struct PerlinNoise3
	{
	public:
		static NoiseSample Noise(Vector3 point, 
							 	 float frequency = 1.f, 
								 int octaves = 1, 
								 float lacunarity = 0.f, 
								 float persistence = 0.f);

		//inline float operator()(float x, float y, float z) const;
		//inline float operator()(const vmath::Vector3 &x) const { return (*this)(x[0], x[1], x[2]); }

	protected:
		static NoiseSample EvaluateNoise(const Vector3& point, float frequency);
		
		inline static float Dot(Vector3 g, float x, float y, float z) {
			return g.getX() * x + g.getY() * y + g.getZ() * z;
		}

		inline static float Smooth(float t) {
			// 6t^5 - 15t^4 + 10t^3
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
		}

		inline static float SmoothDerivative(float t) {
			// 30t^4 - 60t^3 + 30t^2
			return 30.0f * t * t * (t * (t - 2.0f) + 1.0f);
		}
	};	

} // namespace CurlNoise