#include "stdafx.h"

#include "Headers/Sun.h"

namespace Divide {

namespace {
    constexpr D64 g_numSecondsUpdateInterval = 30;

    constexpr D64 SunDia = 0.53;         // Sun radius degrees
    constexpr D64 AirRefr = 34.0 / 60.0; // Atmospheric refraction degrees
    constexpr D64 TwoPi = 2 * M_PI;
    // Sun computation coefficients
    constexpr D64 Longitude_A = 282.9404;
    constexpr D64 Longitude_B = 4.70935E-5;
    constexpr D64 Mean_A = 356.047;
    constexpr D64 Mean_B = 0.9856002585;
    constexpr D64 Eccentricity_A = 0.016709;
    constexpr D64 Eccentricity_B = 1.151E-9;
    constexpr D64 Oblique_A = Angle::DegreesToRadians(23.4393);
    constexpr D64 Oblique_B = Angle::DegreesToRadians(3.563E-7);
    constexpr D64 Ecliptic_A = Angle::DegreesToRadians(1.915);
    constexpr D64 Ecliptic_B = Angle::DegreesToRadians(.02);

    D64 FNrange(const D64 x) noexcept {
        const D64 b = x / TwoPi;
        const D64 a = TwoPi * (b - to_I32(b));
        return a < 0 ? TwoPi + a : a;
    }

    // Calculating the hourangle
    D64 f0(const D64 lat, const D64 declin) noexcept {
        D64 dfo = Angle::DegreesToRadians(0.5 * SunDia + AirRefr);
        if (lat < 0.0) {
            dfo = -dfo;	// Southern hemisphere
        }
        D64 fo = std::tan(declin + dfo) * std::tan(Angle::DegreesToRadians(lat));
        if (fo > 0.99999) {
            fo = 1.0; // to avoid overflow //
        }
        return std::asin(fo) + M_PI_2;
    }

    D64 FNsun(const D64 d, D64& RA, D64& delta, D64& L) {
        //   mean longitude of the Sun
        const D64 W_DEG = Longitude_A + Longitude_B * d;
        const D64 W_RAD = Angle::DegreesToRadians(W_DEG);
        const D64 M_DEG = Mean_A + Mean_B * d;
        const D64 M_RAD = Angle::DegreesToRadians(M_DEG);
        //   mean anomaly of the Sun
        const D64 g = FNrange(M_RAD);
        // eccentricity
        const D64 ECC_RAD = Eccentricity_A - Eccentricity_B * d;
        const D64 ECC_DEG = Angle::RadiansToDegrees(ECC_RAD);
        //   Obliquity of the ecliptic
        const D64 obliq = Oblique_A - Oblique_B * d;

        const D64 E_DEG = M_DEG + ECC_DEG * std::sin(g) * (1.0 + ECC_RAD * std::cos(g));
        const D64 E_RAD = FNrange(Angle::DegreesToRadians(E_DEG));
        D64 x = std::cos(E_RAD) - ECC_RAD;
        D64 y = std::sin(E_RAD) * Sqrt(1.0 - SQUARED(ECC_RAD));

        const D64 r = Sqrt(SQUARED(x) + SQUARED(y));
        const D64 v = Angle::RadiansToDegrees(std::atan2(y, x));

        // longitude of sun
        const D64 lonsun = v + W_DEG;
        const D64 lonsun_rad = Angle::DegreesToRadians(lonsun - 360.0 * (lonsun > 360.0 ? 1 : 0));

        // sun's ecliptic rectangular coordinates
        x = r * std::cos(lonsun_rad);
        y = r * std::sin(lonsun_rad);
        const D64 yequat = y * std::cos(obliq);
        const D64 zequat = y * std::sin(obliq);

        // Sun's mean longitude
        L = FNrange(W_RAD + M_RAD);
        delta = std::atan2(zequat, Sqrt(SQUARED(x) + SQUARED(yequat)));
        RA = Angle::RadiansToDegrees(std::atan2(yequat, x));

        //   Ecliptic longitude of the Sun
        return FNrange(L + Ecliptic_A * std::sin(g) + Ecliptic_B * std::sin(2 * g));
    }
}

SunInfo SunPosition::CalculateSunPosition(struct tm *dateTime, const F32 latitude, const F32 longitude) {

    const D64 longit = to_D64(longitude);
    const D64 latit = to_D64(latitude);
    const D64 latit_rad = Angle::DegreesToRadians(latit);

    // this is Y2K compliant method
    const I32 year = dateTime->tm_year + 1900;
    const I32 m = dateTime->tm_mon + 1;
    const I32 day = dateTime->tm_mday;
    // clock time just now
    const D64 h = dateTime->tm_hour + dateTime->tm_min / 60.0;
    const D64 tzone = _timezone;

    // year = 1990; m=4; day=19; h=11.99;	// local time
    const D64 UT = h - tzone;	// universal time

    //   Get the days to J2000
    //   h is UT in decimal hours
    //   FNday only works between 1901 to 2099 - see Meeus chapter 7
    const D64 jd = [year, h, m, day]() noexcept {
        const I32 luku = -7 * (year + (m + 9) / 12) / 4 + 275 * m / 9 + day;
        // type casting necessary on PC DOS and TClite to avoid overflow
        return to_D64(luku + year * 367) - 730530.0 + h / 24.0;
    }();

    //   Use FNsun to find the ecliptic longitude of the Sun
    D64 RA = 0.0, delta = 0.0, L = 0.0;
    const D64 lambda = FNsun(jd, RA, delta, L);
    const D64 cos_delta = std::cos(delta);
    const D64 delta_deg = Angle::RadiansToDegrees(delta);

    //   Obliquity of the ecliptic
    const D64 obliq = Oblique_A - Oblique_B * jd;

    // Sidereal time at Greenwich meridian
    const D64 GMST0 = Angle::RadiansToDegrees(L) / 15.0 + 12.0;	// hours
    const D64 SIDTIME = GMST0 + UT + longit / 15.0;

    // Hour Angle
    D64 ha = FNrange(Angle::DegreesToRadians(15.0 * SIDTIME - RA));// degrees

    const D64 x = std::cos(ha) * cos_delta;
    const D64 y = std::sin(ha) * cos_delta;
    const D64 z = std::sin(delta);
    const D64 xhor = x * std::sin(latit_rad) - z * std::cos(latit_rad);
    const D64 yhor = y;
    const D64 zhor = x * std::cos(latit_rad) + z * std::sin(latit_rad);
    const D64 azim = FNrange(std::atan2(yhor, xhor) + M_PI);
    const D64 altit = std::asin(zhor);

    // delta = asin(sin(obliq) * sin(lambda));
    const D64 alpha = std::atan2(std::cos(obliq) * std::sin(lambda), std::cos(lambda));

    //   Find the Equation of Time in minutes
    const D64 equation = 1440 - Angle::RadiansToDegrees(L - alpha) * 4;

    ha = f0(latit, delta);

    // arctic winter     //

    D64 riset = 12.0 - 12.0 * ha / M_PI + tzone - longit / 15.0 + equation / 60.0;
    D64 settm = 12.0 + 12.0 * ha / M_PI + tzone - longit / 15.0 + equation / 60.0;
    D64 noont = riset + 12.0 * ha / M_PI;
    D64 altmax = 90.0 + delta_deg - latit;
    if (altmax > 90.0) {
        altmax = 180.0 - altmax; //to express as degrees from the N horizon
    }

    noont -= 24 * (noont > 24 ? 1 : 0);
    riset -= 24 * (riset > 24 ? 1 : 0);
    settm -= 24 * (settm > 24 ? 1 : 0);

    const auto calcTime = [](const D64 dhr) noexcept {
        SimpleTime ret;
        ret._hour = to_U8(dhr);
        ret._minutes = to_U8((dhr - to_D64(ret._hour)) * 60);
        return ret;
    };

    SunInfo ret;
    ret.sunriseTime = calcTime(riset);
    ret.sunsetTime = calcTime(settm);
    ret.noonTime = calcTime(noont);
    ret.altitude = to_F32(altit);
    ret.azimuth = to_F32(azim);
    ret.altitudeMax = to_F32(altmax);
    ret.declination = to_F32(delta_deg);

    return ret;
}

D64 SunPosition::CorrectAngle(const D64 angleInRadians) {
    if (angleInRadians < 0) {
        return TwoPi - std::fmod(std::abs(angleInRadians), TwoPi);
    }
    if (angleInRadians > TwoPi) {
        return std::fmod(angleInRadians, TwoPi);
    }

    return angleInRadians;
}

void Sun::SetLocation(const F32 longitude, const F32 latitude) {
    if (!COMPARE(_longitude, longitude)) {
        _longitude = longitude;
        _dirty = true;
    }
    if (!COMPARE(_latitude, latitude)) {
        _latitude = latitude;
        _dirty = true;
    }
}

void Sun::SetDate(struct tm *dateTime) {
    if (_dateTime != nullptr) {
        const time_t t1 = mktime(_dateTime);
        const time_t t2 = mktime(dateTime);
        const D64 diffSecs = difftime(t1, t2);
        if (diffSecs > g_numSecondsUpdateInterval) {
            _dateTime = dateTime;
            _dirty = true;
        }
    } else {
        _dateTime = dateTime;
        _dirty = true;
    }
}

SimpleTime Sun::GetTimeOfDay() const noexcept {
    assert(_dateTime != nullptr);

    return SimpleTime{
        to_U8(_dateTime->tm_hour),
        to_U8(_dateTime->tm_min)
    };
}

SimpleLocation Sun::GetGeographicLocation() const noexcept {
    assert(_dateTime != nullptr);

    return SimpleLocation{
        _latitude,
        _longitude
    };
}

SunDetails Sun::GetDetails() const {
    if (_dirty) {
        _cachedInfo = SunPosition::CalculateSunPosition(_dateTime, _latitude, _longitude);
        _dirty = false;
    }

    SunDetails ret = {};
    ret._info = _cachedInfo;
    ret._eulerDirection.x = -Angle::RadiansToDegrees(ret._info.altitude);
    ret._eulerDirection.y = Angle::RadiansToDegrees(ret._info.azimuth);
    ret._intensity = ret._info.altitude * 100.f;
    return ret;
}

} //namespace Divide