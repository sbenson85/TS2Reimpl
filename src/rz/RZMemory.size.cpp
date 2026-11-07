#include "rz/RZMemory.h"

#include "game/game.h"
#include "rz/GeneralAllocator.h"

namespace RZMemory {

GeneralAllocator *testGlobalAllocator = nullptr;

GeneralAllocator *GlobalAllocator()
{
    return testGlobalAllocator ? testGlobalAllocator : &ts2_rva_10cb098;
}

void *__cdecl Malloc(uint32_t bytes)
{
    return GlobalAllocator()->Malloc(bytes, 0);
}

void __cdecl Free(void *mem)
{
    GlobalAllocator()->Free(mem);
}

void *__cdecl Realloc(void *mem, uint32_t bytes)
{
    return GlobalAllocator()->Realloc(mem, bytes, 0);
}

void *Service::ServiceMalloc(uint32_t bytes)
{
    return Malloc(bytes);
}

void Service::ServiceFreeIfNotNull(void *mem)
{
    if (mem) Free(mem);
}

void *Service::ServiceRealloc(void *mem, uint32_t bytes)
{
    return Realloc(mem, bytes);
}

void Service::ServiceFree(void *mem)
{
    Free(mem);
}

} // namespace RZMemory
