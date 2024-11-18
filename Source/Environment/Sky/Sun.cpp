

#include "Headers/Sun.h"

namespace Divide {

namespace {
    constexpr D64 g_numSecondsUpdateInterval = 30;

    // Sun computation coefficients
    const Angle::RADIANS_D Eccentricity_A = 0.016709;
    const Angle::RADIANS_D Eccentricity_B = 1.151E-9;
    const Angle::DEGREES_D Mean_A = 356.047;
    const Angle::DEGREES_D Mean_B = 0.9856002585;
    const Angle::DEGREES_D Longitude_A = 282.9404;
    const Angle::DEGREES_D Longitude_B = 4.70935E-5;
    const Angle::DEGREES_D SunDia = 0.53;         // Sun radius degrees
    const Angle::DEGREES_D AirRefr = 34.0 / 60.0; // Atmospheric refraction degrees
    const Angle::RADIANS_D Oblique_A  = Angle::to_RADIANS(Angle::DEGREES_D(23.4393));
    const Angle::RADIANS_D Oblique_B  = Angle::to_RADIANS(Angle::DEGREES_D(3.563E-7));
    const Angle::RADIANS_D Ecliptic_A = Angle::to_RADIANS(Angle::DEGREES_D(1.915));
    const Angle::RADIANS_D Ecliptic_B = Angle::to_RADIANS(Angle::DEGREES_D(.02));

    D64 FNrange(const Angle::RADIANS_D x) noexcept
    {
        const D64 b = x.value / M_PI_MUL_2;
        const D64 a = M_PI_MUL_2 * (b - to_I32(b));
        return a < 0 ? M_PI_MUL_2 + a : a;
    }

    // Calculating the hourangle
    Angle::RADIANS_D f0(const Angle::DEGREES_D lat, const D64 declin) noexcept
    {
        Angle::RADIANS_D dfo = Angle::to_RADIANS(Angle::DEGREES_D(0.5 * SunDia + AirRefr));
        if (lat < 0.0)
        {
            dfo = -dfo;	// Southern hemisphere
        }

        const Angle::RADIANS_D fo = std::min(std::tan(declin + dfo) * std::tan(Angle::to_RADIANS(lat)), 1.0);// to avoid overflow //
        return std::asin(fo) + M_PI_DIV_2;
    }

    Angle::RADIANS_D FNsun(const D64 d, Angle::DEGREES_D& RA, Angle::RADIANS_D& delta, Angle::RADIANS_D& L) noexcept
    {
        //   mean longitude of the Sun
        const Angle::DEGREES_D W_DEG = Longitude_A + Longitude_B * d;
        const Angle::DEGREES_D M_DEG = Mean_A + Mean_B * d;

        const Angle::RADIANS_D W_RAD = Angle::to_RADIANS(W_DEG);
        const Angle::RADIANS_D M_RAD = Angle::to_RADIANS(M_DEG);

        //   mean anomaly of the Sun
        const D64 g = FNrange(M_RAD);

        // eccentricity
        const Angle::RADIANS_D ECC_RAD = Eccentricity_A - Eccentricity_B * d;
        const Angle::DEGREES_D ECC_DEG = Angle::to_DEGREES(ECC_RAD);

        //   Obliquity of the ecliptic
        const Angle::RADIANS_D obliq = Oblique_A - Oblique_B * d;

        const Angle::DEGREES_D E_DEG = (M_DEG.value + ECC_DEG.value * std::sin(g) * (1.0 + ECC_RAD.value * std::cos(g)));
        const Angle::RADIANS_D E_RAD = FNrange(Angle::to_RADIANS(E_DEG));

        D64 x = std::cos(E_RAD) - ECC_RAD;
        D64 y = std::sin(E_RAD) * Sqrt(1.0 - SQUARED(ECC_RAD.value));

        const D64 r = Sqrt(SQUARED(x) + SQUARED(y));
        const Angle::DEGREES_D v = Angle::to_DEGREES(Angle::RADIANS_D(std::atan2(y, x)));

        // longitude of sun
        const Angle::DEGREES_D lonsun = v + W_DEG;
        const Angle::RADIANS_D lonsun_rad = Angle::to_RADIANS(lonsun - 360.0 * (lonsun > 360.0 ? 1 : 0));

        // sun's ecliptic rectangular coordinates
        x = r * std::cos(lonsun_rad);
        y = r * std::sin(lonsun_rad);
        const D64 yequat = y * std::cos(obliq);
        const D64 zequat = y * std::sin(obliq);

        // Sun's mean longitude
        L = FNrange(W_RAD + M_RAD);
        delta = std::atan2(zequat, Sqrt(SQUARED(x) + SQUARED(yequat)));
        RA = Angle::to_DEGREES(Angle::RADIANS_D(std::atan2(yequat, x)));

        //   Ecliptic longitude of the Sun
        return FNrange(L + Ecliptic_A * std::sin(g) + Ecliptic_B * std::sin(2 * g));
    }
}

SunInfo SunPosition::CalculateSunPosition(const struct tm &dateTime, const Angle::DEGREES_F latitude, const Angle::DEGREES_F longitude)
{
    const Angle::DEGREES_D longit = to_D64(longitude);
    const Angle::DEGREES_D latit = to_D64(latitude);
    const Angle::RADIANS_D latit_rad = Angle::to_RADIANS(latit);

    // this is Y2K compliant method
    const I32 year = dateTime.tm_year + 1900;
    const I32 m = dateTime.tm_mon + 1;
    const I32 day = dateTime.tm_mday;
    // clock time just now
    const auto h = dateTime.tm_hour + dateTime.tm_min / 60.0;
    const auto tzone = std::chrono::current_zone()->get_info( std::chrono::system_clock::now() ).offset.count() / 3600.0;

    // year = 1990; m=4; day=19; h=11.99;	// local time
    const auto UT = h - tzone;	// universal time

    //   Get the days to J2000
    //   h is UT in decimal hours
    //   FNday only works between 1901 to 2099 - see Meeus chapter 7
    const D64 jd = [year, h, m, day]() noexcept
                   {
                       const I32 luku = -7 * (year + (m + 9) / 12) / 4 + 275 * m / 9 + day;
                       // type casting necessary on PC DOS and TClite to avoid overflow
                       return to_D64(luku + year * 367) - 730530.0 + h / 24.0;
                   }();

    //   Use FNsun to find the ecliptic longitude of the Sun
    Angle::RADIANS_D delta = 0.0, L = 0.0;
    Angle::DEGREES_D RA = 0.0;

    const D64 lambda = FNsun(jd, RA, delta, L);
    const D64 cos_delta = std::cos(delta);
    const Angle::DEGREES_D delta_deg = Angle::to_DEGREES(delta);

    //   Obliquity of the ecliptic
    const Angle::RADIANS_D obliq = Oblique_A - Oblique_B * jd;

    // Sidereal time at Greenwich meridian
    const Angle::DEGREES_D GMST0 = Angle::to_DEGREES(L) / 15.0 + 12.0;	// hours
    const Angle::DEGREES_D SIDTIME = GMST0 + UT + longit / 15.0;

    // Hour Angle
    D64 ha = FNrange(Angle::to_RADIANS(15.0 * SIDTIME - RA));// degrees

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
    const D64 equation = 1440. - Angle::to_DEGREES(L - alpha).value * 4.;

    ha = f0(latit, delta);

    // arctic winter     //
    D64 riset = 12.0  - 12.0 * ha / M_PI + tzone - longit / 15.0 + equation / 60.0;
    D64 settm = 12.0  + 12.0 * ha / M_PI + tzone - longit / 15.0 + equation / 60.0;
    D64 noont = riset + 12.0 * ha / M_PI;
    Angle::DEGREES_D altmax = 90.0 + delta_deg - latit;
    if (altmax > 90.0)
    {
        altmax = 180.0 - altmax; //to express as degrees from the N horizon
    }

    noont -= 24. * (noont > 24. ? 1. : 0.);
    riset -= 24. * (riset > 24. ? 1. : 0.);
    settm -= 24. * (settm > 24. ? 1. : 0.);

    const auto calcTime = [](const D64 dhr) noexcept
    {
        SimpleTime ret;
        ret._hour = to_U8(dhr);
        ret._minutes = to_U8((dhr - to_D64(ret._hour)) * 60);
        return ret;
    };

    return SunInfo
    {
        .sunriseTime = calcTime(riset),
        .sunsetTime  = calcTime(settm),
        .noonTime    = calcTime(noont),
        .altitude    = to_F32(altit),
        .azimuth     = to_F32(azim),
        .altitudeMax = to_F32(altmax),
        .declination = to_F32(delta_deg)
    };
}

void Sun::SetLocation(const Angle::DEGREES_F longitude, const Angle::DEGREES_F latitude) noexcept
{
    if (!COMPARE(_longitude, longitude))
    {
        _longitude = longitude;
        _dirty = true;
    }
    if (!COMPARE(_latitude, latitude))
    {
        _latitude = latitude;
        _dirty = true;
    }
}

void Sun::SetDate(struct tm &dateTime) noexcept
{
    const time_t t1 = mktime(&_dateTime);
    const time_t t2 = mktime(&dateTime);
    const D64 diffSecs = std::abs(difftime(t1, t2));

    if (t1 == -1 || diffSecs > g_numSecondsUpdateInterval)
    {
        _dateTime = dateTime;
        _dirty = true;
    }
}

SimpleTime Sun::GetTimeOfDay() const noexcept
{
    return SimpleTime
    {
        to_U8(_dateTime.tm_hour),
        to_U8(_dateTime.tm_min)
    };
}

SimpleLocation Sun::GetGeographicLocation() const noexcept
{
    return SimpleLocation
    {
        _latitude,
        _longitude
    };
}

const SunInfo& Sun::GetDetails() const
{
    if (_dirty)
    {
        _cachedDetails = SunPosition::CalculateSunPosition(_dateTime, _latitude, _longitude);
        _dirty = false;
    }

    return _cachedDetails;
}

[[nodiscard]] float3 Sun::GetSunPosition(const F32 radius) const
{
    const SunInfo& info = GetDetails();

    const Angle::RADIANS_F phi = M_PI_DIV_2 - info.altitude.value;
    const Angle::RADIANS_F theta = info.azimuth;
    const F32 sinPhiRadius = std::sin(phi) * radius;

    return float3
    {
        sinPhiRadius * std::sin(theta),
        std::cos(phi) * radius,
        sinPhiRadius * std::cos(theta)
    };
}

} //namespace Divide
