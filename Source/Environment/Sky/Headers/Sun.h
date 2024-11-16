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
#ifndef DVD_SUN_H_
#define DVD_SUN_H_

//ref: https://gist.github.com/paulhayes/54a7aa2ee3cccad4d37bb65977eb19e2
//ref: https://github.com/jarmol/suncalcs
namespace Divide {
    struct SunInfo
    {
        SimpleTime sunriseTime = {};
        SimpleTime sunsetTime = {};
        SimpleTime noonTime = {};
        Angle::RADIANS_F altitude = 0.f;
        Angle::RADIANS_F azimuth = 0.f;
        Angle::DEGREES_F altitudeMax = 0.f;
        Angle::DEGREES_F declination = 0.f;
    };

    struct SunPosition
    {
        [[nodiscard]] static SunInfo CalculateSunPosition(const struct tm &dateTime, Angle::DEGREES_F latitude, Angle::DEGREES_F longitude);
    };

    struct Sun
    {
        void SetLocation(Angle::DEGREES_F longitude, Angle::DEGREES_F latitude) noexcept;
        void SetDate(struct tm &dateTime) noexcept;
        SimpleTime GetTimeOfDay() const noexcept;
        SimpleLocation GetGeographicLocation() const noexcept;

        [[nodiscard]] const SunInfo& GetDetails() const;

        [[nodiscard]] vec3<F32> GetSunPosition(F32 radius = 1.f) const;
    private:
        mutable SunInfo _cachedDetails;
        Angle::DEGREES_F _longitude = 0.f;
        Angle::DEGREES_F _latitude = 0.f;
        struct tm _dateTime {};
        mutable bool _dirty = true;
    };

    struct Atmosphere
    {
        FColour3 _rayleighColour = { 0.26f, 0.41f, 0.58f };
        FColour3 _mieColour = { 0.63f, 0.77f, 0.92f };
        vec2<F32> _cloudLayerMinMaxHeight = { 1500.f, 4000.f };
        F32 _rayleigh = 2.f;
        F32 _mie = 0.005f;
        F32 _mieEccentricity = 0.8f;
        F32 _turbidity = 10.f;
        F32 _sunDiskSize = 1.f;
        F32 _planetRadius = 6'371'000.f;
        F32 _cloudCoverage = 0.35f;
        F32 _cloudDensity = 0.05f;
    };
}  // namespace Divide

#endif //DVD_SUN_H_
