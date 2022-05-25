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
#ifndef _SUN_H_
#define _SUN_H_

//ref: https://gist.github.com/paulhayes/54a7aa2ee3cccad4d37bb65977eb19e2
//ref: https://github.com/jarmol/suncalcs
namespace Divide {
    struct SunInfo
    {
        SimpleTime sunriseTime = {};
        SimpleTime sunsetTime = {};
        SimpleTime noonTime = {};
        Angle::RADIANS<F32> altitude = 0.f;
        Angle::RADIANS<F32> azimuth = 0.f;
        Angle::DEGREES<F32> altitudeMax = 0.f;
        Angle::DEGREES<F32> declination = 0.f;
    };

    struct SunPosition
    {
        [[nodiscard]] static SunInfo CalculateSunPosition(const struct tm &dateTime, F32 latitude, F32 longitude);
        [[nodiscard]] static D64 CorrectAngle(D64 angleInRadians) noexcept;
    };

    struct Sun
    {
        void SetLocation(F32 longitude, F32 latitude) noexcept;
        void SetDate(struct tm &dateTime) noexcept;
        SimpleTime GetTimeOfDay() const noexcept;
        SimpleLocation GetGeographicLocation() const noexcept;

        [[nodiscard]] const SunInfo& GetDetails() const;

        [[nodiscard]] vec3<F32> GetSunPosition(F32 radius = 1.f) const;
    private:
        mutable SunInfo _cachedDetails;
        F32 _longitude = 0.f;
        F32 _latitude = 0.f;
        struct tm _dateTime {};
        mutable bool _dirty = true;
    };

    struct Atmosphere {
        vec3<F32> _RayleighCoeff = { 5.5f, 13.0f, 22.4f };     // Rayleigh scattering coefficient
        vec2<F32> _cloudLayerMinMaxHeight = { 1000.f, 2300.f }; // Clouds will be limited between [planerRadius + min - planetRadius + height]
        F32 _sunIntensity = 1.f;        // x 1000. visual size of the sun disc
        F32 _sunPenetrationPower = 30.f;// Factor used to calculate atmosphere transmitance for clour layer
        F32 _planetRadius = 6360e3f;    // radius of the planet in meters
        F32 _cloudSphereRadius = 200e3f;// cloud sphere radius. Does not need to match planet radius
        F32 _atmosphereOffset = 60.f;   // planetRadius + atmoOffset = radius of the atmosphere in meters
        F32 _MieCoeff = 21e-6f;         // Mie scattering coefficient
        F32 _RayleighScale = 7994.f;    // Rayleigh scale height
        F32 _MieScaleHeight = 1200.f;   // Mie scale height
    };
}  // namespace Divide

#endif //_SUN_H_