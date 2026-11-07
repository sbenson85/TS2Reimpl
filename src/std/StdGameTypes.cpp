// The speed-built members (tools/flagprofile.py): 0x15aa90, 0x1adba0 and 0x1bdc30. The rest is in StdGameTypes.size.cpp.
#include "std/StdGameTypes.h"

namespace StdGameTypes {

std::pair<RegionMapAccess::Node *, bool> RegionMapAccess::Emplace(const value_type &value)
{
    [[msvc::forceinline_calls]] return _Emplace(value);
}

std::string __cdecl AppendCharMove(std::string &&left, char right)
{
    return std::move(left) + right;
}

size_t StringAccess::FindText(const char *text, size_t offset) const
{
    [[msvc::forceinline_calls]] return find(text, offset);
}

} // namespace StdGameTypes
