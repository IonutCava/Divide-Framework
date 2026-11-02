// Copyright (c) 2026 DIVIDE-Studio
#include "UnitTests/unitTestCommon.h"

#if defined(IS_MACOS_BUILD)
#include "Environment/Sky/Headers/Sun.h"

#include <cstdlib>
#include <ctime>
#include <string>

namespace
{
    class ScopedTimeZone
    {
    public:
        ScopedTimeZone()
        {
            if (const char* zone = std::getenv("TZ"))
            {
                _hadPrevious = true;
                _previous = zone;
            }
        }

        ~ScopedTimeZone()
        {
            if (_hadPrevious)
            {
                (void)::setenv("TZ", _previous.c_str(), 1);
            }
            else
            {
                (void)::unsetenv("TZ");
            }
            ::tzset();
        }

        void set(const char* zone)
        {
            REQUIRE(::setenv("TZ", zone, 1) == 0);
            ::tzset();
        }

    private:
        bool _hadPrevious = false;
        std::string _previous;
    };
}

TEST_CASE("Sun uses the local UTC offset for the requested instant", "[sun][timezone]")
{
    ScopedTimeZone timeZone;

    const auto verifyOffset = [&timeZone](const char* zone, const int month, const int day,
                                          const int hour, const int minute, const int offsetMinutes)
    {
        std::tm dateTime{};
        dateTime.tm_year = 2024 - 1900;
        dateTime.tm_mon = month - 1;
        dateTime.tm_mday = day;
        dateTime.tm_hour = hour;
        dateTime.tm_min = minute;

        timeZone.set("UTC0");
        const auto utc = Divide::SunPosition::CalculateSunPosition(dateTime, 0.f, 0.f);
        timeZone.set(zone);
        const auto local = Divide::SunPosition::CalculateSunPosition(dateTime, 0.f, 0.f);

        const auto minutes = [](const Divide::SimpleTime time)
        {
            return static_cast<int>(time._hour) * 60 + static_cast<int>(time._minutes);
        };
        const int actual = (minutes(local.noonTime) - minutes(utc.noonTime) + 1440) % 1440;
        const int expected = (offsetMinutes + 1440) % 1440;
        const int difference = std::abs(actual - expected);
        CAPTURE(zone, month, day, hour, minute, actual, expected);
        CHECK((difference <= 1 || difference >= 1439));
    };

    verifyOffset("UTC0", 1, 1, 0, 1, 0);
    verifyOffset("EST5", 1, 1, 0, 1, -300);
    verifyOffset("NPT-5:45", 1, 15, 12, 0, 345);
    verifyOffset("LINT-14", 12, 31, 23, 59, 840);

    constexpr const char* eastern = "EST5EDT,M3.2.0/2,M11.1.0/2";
    verifyOffset(eastern, 1, 15, 12, 0, -300);
    verifyOffset(eastern, 7, 15, 12, 0, -240);
    verifyOffset(eastern, 3, 10, 6, 59, -300);
    verifyOffset(eastern, 3, 10, 7, 0, -240);
    verifyOffset(eastern, 11, 3, 5, 59, -240);
    verifyOffset(eastern, 11, 3, 6, 0, -300);
}
#endif //IS_MACOS_BUILD
