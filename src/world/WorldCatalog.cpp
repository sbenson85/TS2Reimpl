// The catalog accessors of nTSWorld's helper module (lane RY): src/world/WorldCatalog.h. Built for speed (/O2).
#include "world/WorldCatalog.h"

#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"

namespace worldcatalog {

namespace {

// The entry slots by vtable offset / 4.
// The wall-part face of a catalog product (vtable at +0 of the pointer FindWallPartByKey returns): slot 24 is at +0x60.

template <int Slot> uint8_t FlagOr(const uint32_t *id, uint8_t missing)
{
    void *const entry = Entry(id);
    if (!entry) return missing;
    return RawSlot<Slot, uint8_t>(entry);
}

} // namespace

void *__cdecl Entry(const uint32_t *id)
{
    void *const catalog = RawSlot<16, void *>(GetCachedSingleton()); // receiver: cITSGlobals (GetCatalog, slot 16, no stack arguments)
    return RawSlot<19, void *>(catalog, *id); // receiver: cITSCatalog (cTSCatalog::FindWallPartByKey 0xc142e8, slot 19, ret 4; not cITSSkinCatalog::HasType3)
}

uint8_t __cdecl FlagSlot94OrTrue(const uint32_t *id) { return FlagOr<0x94 / 4>(id, 1); }
uint8_t __cdecl FlagSlot98OrTrue(const uint32_t *id) { return FlagOr<0x98 / 4>(id, 1); }
uint8_t __cdecl FlagSlot8cOrTrue(const uint32_t *id) { return FlagOr<0x8c / 4>(id, 1); }
uint8_t __cdecl FlagSlot90OrTrue(const uint32_t *id) { return FlagOr<0x90 / 4>(id, 1); }

bool __cdecl Exists(const uint32_t *id) { return Entry(id) != nullptr; }

bool __cdecl IsKind1Or3AndSlot94(const uint32_t *id)
{
    void *const entry = Entry(id);
    if (!entry || !IsKind1Or3(id)) return false;
    return RawSlot<0x94 / 4, uint8_t>(entry) != 0;
}

bool __cdecl IsKind1Or3(const uint32_t *id) { return Kind(id) == 1 || Kind(id) == 3; }
bool __cdecl IsKind2(const uint32_t *id) { return Kind(id) == 2; }
bool __cdecl IsKind3(const uint32_t *id) { return Kind(id) == 3; }
bool __cdecl IdIs1d(const uint32_t *id) { return *id == 0x1d; }
bool __cdecl IdIs12d(const uint32_t *id) { return *id == 0x12d; }
bool __cdecl IsKind0Or3(const uint32_t *id) { return Kind(id) == 0 || Kind(id) == 3; }

uint8_t __cdecl FlagSlotA0(const uint32_t *id) { return FlagOr<0xa0 / 4>(id, 0); }
uint8_t __cdecl FlagSlot70(const uint32_t *id) { return FlagOr<0x70 / 4>(id, 0); }
uint8_t __cdecl FlagSlot74(const uint32_t *id) { return FlagOr<0x74 / 4>(id, 0); }
uint8_t __cdecl FlagSlot68(const uint32_t *id) { return FlagOr<0x68 / 4>(id, 0); }
uint8_t __cdecl FlagSlot6c(const uint32_t *id) { return FlagOr<0x6c / 4>(id, 0); }
uint8_t __cdecl FlagSlot64(const uint32_t *id) { return FlagOr<0x64 / 4>(id, 0); }

int32_t __cdecl Slot80(const uint32_t *id)
{
    void *const entry = Entry(id);
    return entry ? RawSlot<0x80 / 4, int32_t>(entry) : 0;
}

bool __cdecl Slot80Is1(const uint32_t *id) { return Slot80(id) == 1; }
bool __cdecl Slot80Is4(const uint32_t *id) { return Slot80(id) == 4; }
bool __cdecl Slot80Is2(const uint32_t *id) { return Slot80(id) == 2; }

int32_t __cdecl Kind(const uint32_t *id)
{
    void *const entry = Entry(id);
    return entry ? RawSlot<24, int32_t>(entry) : -1; // slot 24 = +0x60, no stack arguments
}

} // namespace worldcatalog
