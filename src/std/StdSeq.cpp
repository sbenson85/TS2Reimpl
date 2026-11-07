#include "std/StdSeq.h"

#include <cstring>
#include <memory>
#include <string>
#include <xutility>

namespace {

// The game's std::basic_string<char> is ours too (same MSVC 14.40 headers), so the append is the
// library's own.
std::string &AsString(StdSeqString *s) { return *reinterpret_cast<std::string *>(s); }

} // namespace

StdSeqString *StdSeqString::Append(const char *s, size_t count)
{
    AsString(this).append(s, count);
    return this;
}

uint8_t *StdSeqCopyBytes(const uint8_t *first, const uint8_t *last, uint8_t *dest)
{
    const size_t count = static_cast<size_t>(last - first);
    std::memcpy(dest, first, count);
    return dest + count;
}

void StdSeqFillU32(uint32_t *first, uint32_t *last, const uint32_t *value)
{
    for (; first != last; ++first) *first = *value;
}

void StdSeqVectorU32::Assign(uint32_t count, uint32_t value)
{
    // ORIGINAL-BUG: when count <= size the original fills every existing element with value and
    // keeps the size, so elements past count are overwritten instead of dropped (std::vector::assign
    // sets the size to count). Reproduced here; any caller assigning fewer elements than the vector
    // holds sees this.
    if (static_cast<size_t>(last - first) >= count) {
        StdSeqFillU32(first, last, &value);
        return;
    }
    if (count > 0x3fffffff) std::_Xlength_error("vector too long");

    std::allocator<uint32_t> alloc;
    uint32_t *block = alloc.allocate(count);
    const size_t oldCapacity = static_cast<size_t>(capEnd - first);
    if (oldCapacity != 0) alloc.deallocate(first, oldCapacity);

    first = block;
    last = block + count;
    capEnd = last;
    StdSeqFillU32(block, last, &value);
}
