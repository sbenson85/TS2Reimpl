#include "rz/cRZStringSub.h"

namespace {
constexpr uint32_t kWhole = 0xffffffffu;
}

cRZString *cRZStringSub::Mid(cRZString *out, uint32_t start, uint32_t count) const
{
    const uint32_t size = Strlen();
    if (start >= size) return out->Construct();
    const uint32_t available = size - start;
    return out->Construct(*this, start, count < available ? count : available);
}

cRZString *cRZStringSub::Left(cRZString *out, uint32_t count) const
{
    if (count > Strlen()) return out->Construct(*this, 0, kWhole);
    return Mid(out, 0, count);
}

cRZString *cRZStringSub::Right(cRZString *out, uint32_t count) const
{
    const uint32_t size = Strlen();
    if (count > size) return out->Construct(*this, 0, kWhole);
    return Mid(out, size - count, count);
}
