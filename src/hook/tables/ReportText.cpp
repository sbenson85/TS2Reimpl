#include "app/ReportText.h"
#include "hook/hooks.h"

namespace {

using appbase::cRZDate;
using appbase::cRZTime;

const HookSpec kHooks[] = {
    {"cRZDate_IsLeapYear", 0x1b995c, 32, 0xfeba6e6c, reinterpret_cast<void *>(&cRZDate::IsLeapYear)},
    {"cRZDate_IsValid", 0x1b98a4, 32, 0x1ca59fa1, reinterpret_cast<void *>(&cRZDate::IsValid)},
    {"cRZDate_ToJulianDay", 0x1b98e2, 32, 0x88eb0037, reinterpret_cast<void *>(&cRZDate::ToJulianDay)},
    {"cRZDate_Copy", 0x1b969a, 21, 0x7bb2ed62, MemberAddress(&cRZDate::Copy)},
    {"cRZDate_ConstructDays", 0x1b96af, 28, 0x368e2838, MemberAddress(&cRZDate::ConstructDays)},
    {"cRZDate_ConstructDate", 0x1b96cb, 32, 0x2f5238a7, MemberAddress(&cRZDate::ConstructDate)},
    {"cRZDate_ConstructToday", 0x1b96f1, 32, 0xadfce01d, MemberAddress(&cRZDate::ConstructToday)},
    {"cRZDate_Set", 0x1b9a8b, 32, 0xb616603c, MemberAddress(&cRZDate::Set)},
    {"cRZDate_Decompose", 0x1b998a, 32, 0xcd6e345c, MemberAddress(&cRZDate::Decompose)},
    {"cRZDate_GetMonth", 0x1b9a26, 28, 0x7897168b, MemberAddress(&cRZDate::GetMonth)},
    {"cRZDate_GetDay", 0x1b9888, 28, 0xca8fe346, MemberAddress(&cRZDate::GetDay)},
    {"cRZDate_GetYear", 0x1b9ad0, 28, 0xa90a0203, MemberAddress(&cRZDate::GetYear)},
    {"cRZDate_GetWeekday", 0x1b9aba, 22, 0xb9a3f58e, MemberAddress(&cRZDate::GetWeekday)},
    {"cRZDate_GetPreviousWeekday", 0x1b9a53, 32, 0xfc532b11, MemberAddress(&cRZDate::GetPreviousWeekday)},
    {"cRZDate_Format", 0x1b9738, 32, 0xcda19de1, MemberAddress(&cRZDate::Format)},
    {"cRZTime_FromDate", 0x1452d5, 32, 0xbdf613fb, reinterpret_cast<void *>(&cRZTime::FromDate)},
    {"cRZTime_DaylightStart", 0x145268, 32, 0x46e149f0, reinterpret_cast<void *>(&cRZTime::DaylightStart)},
    {"cRZTime_DaylightEnd", 0x145455, 32, 0xaadb34f5, reinterpret_cast<void *>(&cRZTime::DaylightEnd)},
    {"cRZTime_Construct", 0x144f81, 32, 0xb1919a08, MemberAddress(&cRZTime::Construct)},
    {"cRZTime_ConstructNow", 0x144ff4, 32, 0x17c9517e, MemberAddress(&cRZTime::ConstructNow)},
    {"cRZTime_IsDaylightSaving", 0x14583b, 32, 0x56886d1c, MemberAddress(&cRZTime::IsDaylightSaving)},
    {"cRZTime_LocalSeconds", 0x145ad1, 32, 0x51d42fa7, MemberAddress(&cRZTime::LocalSeconds)},
    {"cRZTime_GetHour", 0x145821, 26, 0xc0ceafbe, MemberAddress(&cRZTime::GetHour)},
    {"cRZTime_GetMinute", 0x145afc, 24, 0x068c93d3, MemberAddress(&cRZTime::GetMinute)},
    {"ReportText_DescribeExceptionCode", 0x1bbb10, 32, 0xc6dea968, reinterpret_cast<void *>(&appbase::DescribeExceptionCode)},
    {"ReportText_SecondsSinceGuidEpoch", 0x145774, 32, 0xa4683bb4, reinterpret_cast<void *>(&appbase::SecondsSinceGuidEpoch)},
};
REGISTER_HOOKS(kHooks);

} // namespace
