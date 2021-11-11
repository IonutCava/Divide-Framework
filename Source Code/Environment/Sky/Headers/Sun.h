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

    struct SunDetails
    {
        SunInfo _info = {};
        vec3<Angle::DEGREES<F32>> _eulerDirection = VECTOR3_ZERO;
        F32 _intensity = 0.f;
    };

    struct Sun
    {
        void SetLocation(F32 longitude, F32 latitude) noexcept;
        void SetDate(struct tm &dateTime) noexcept;
        SimpleTime GetTimeOfDay() const noexcept;
        SimpleLocation GetGeographicLocation() const noexcept;

        [[nodiscard]] const SunDetails& GetDetails() const;

    private:
        mutable SunDetails _cachedDetails;
        F32 _longitude = 0.f;
        F32 _latitude = 0.f;
        struct tm _dateTime {};
        mutable bool _dirty = true;
    };
}  // namespace Divide

#endif //_SUN_H_