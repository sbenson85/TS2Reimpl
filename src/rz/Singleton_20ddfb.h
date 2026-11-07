#pragma once
#include <cstdint>

// A lazily initialised singleton (0x20ddfb) and the code it uses. The object is obtained from a
// service object held in game global 0x10c9378 (getter 0x1427b5, reached through the thunk at
// 0x14323f), by calling its vtable slot 5 with two identifiers and an output pointer. The class of
// the service and of the cached object is not identified yet. RVAs are 1.18.0.187.

// Returns the service object in game global 0x10c9378 (0x1427b5, __cdecl, no arguments).
void *GetServiceObject();

// The local reference in 0x20ddfb. It has no destructor: 0x1cbd79 releases it explicitly.
struct ScopedRef {
    void *mObject;

    // 0x20dc91 (__thiscall): sets mObject to null, then asks the service object for one through
    // slot 5 (identifiers 0x7a3e667d, 0x7a3e566d, output mObject). Returns this.
    ScopedRef *AcquireFromService();

    // 0x1cbd79 (__thiscall): if mObject is non-null, calls its Release (slot 2). Exceptions from
    // Release propagate. mObject is not cleared.
    void ReleaseObject();
};

static_assert(sizeof(ScopedRef) == 4, "ScopedRef layout");

// 0x20ddfb (__cdecl, no arguments): returns game global 0x10cc288. When it is null, the object is
// acquired from the service, stored there, and the local reference is released (the stored pointer
// is not kept alive by it).
void *GetCachedSingleton();
