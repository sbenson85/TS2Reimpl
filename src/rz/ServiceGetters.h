#pragma once
#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"

#include <cstdint>

// Cached service getters. Each one returns a game global that caches an object from the service
// object (GetServiceObject, game global 0x10c9378). Eight getters of this shape exist (RVAs
// 0xb390c0, 0xb39110, 0xb38ea0, 0xb38e50, 0xb38fb0, 0xb8bcb0, 0xb9a990, 0xb9a940); they differ only
// in the cache global and the two identifiers. Purposes are unknown; the names say what they do.
//
// Original shape (0xb390c0, __cdecl, no arguments):
//   if (cache == null) {
//       held = null;                      // the local in the stack frame
//       service = GetServiceObject();     // 0x14323f -> 0x1427b5
//       if (service) service->Query(FirstId, SecondId, &held);   // vtable slot 5, args pushed in
//                                                                // the order: &held, SecondId, FirstId
//       cache = held;
//       ReleaseHeldObject(held);          // 0x1466e0, inline here
//   }
//   return cache;                         // re-read after the release
//
// The identifiers are the arguments the callee sees: FirstId is the first parameter (the last one
// pushed).

// Releases a temporary holder: if object is non-null, calls its Release (vtable slot 2) inside
// try/catch(...) as 0x1466e0 does (FuncInfo flags 5, EHs): C++ exceptions are swallowed, SEH
// exceptions propagate (RZAutoRef::ReleaseObject, 0x1466e0).
void ReleaseHeldObject(void *object);

// The getter for the global at RVA CacheRva, with the identifiers of the original.
template <uint32_t CacheRva, uint32_t FirstId, uint32_t SecondId> void *CachedServiceGetter()
{
    void **cache = GameAddress<void *>(CacheRva);
    if (*cache == nullptr) {
        void *held = nullptr;
        void *service = GetServiceObject();
        if (service) {
            using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Query, 5>(service, FirstId, SecondId, &held);
        }
        *cache = held;
        ReleaseHeldObject(held);
    }
    return *cache;
}
