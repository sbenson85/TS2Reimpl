#include "std/GZServiceString.h"

#include "app/AppServices2.h"

char *__cdecl GZServiceString::AllocateForCapacity(void *, uint32_t *capacity)
{
    ++*capacity;
    char *block = static_cast<char *>(as2::AllocateViaAllocatorService(*capacity, 0));
    --*capacity;
    return block;
}
