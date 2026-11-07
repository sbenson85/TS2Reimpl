#pragma once
#include <cstdint>
#include <map>

// Helpers of the MiscHelpersB group. RVAs are 1.18.0.187.

// A holder whose only field is an object pointer, filled by the service object (the same 4-byte
// layout as RZAutoRef and ScopedRef).
struct ServiceHolder {
    void *mObject;

    // 0x1c02bf (__thiscall, no arguments, plain ret): clears mObject, then asks the service object
    // (GetServiceObject, game global 0x10c9378) for one through its vtable slot 5, with the
    // identifiers 0x04fa845b (first parameter) and 0x652294c7 (second), and mObject as the output.
    // Nothing else happens when there is no service object. Returns this.
    ServiceHolder *QueryFromService();
};

static_assert(sizeof(ServiceHolder) == 4, "ServiceHolder layout");

// The object at `this` is an MSVC std::map<uint32_t, ...> (its head pointer is at +0), not
// identified further. The owner of the map is not known.
struct KeyMapWalker {
    std::map<uint32_t, uint32_t> mMap;

    // 0x1c081e (__thiscall, ret 8): does nothing when arg is 0. Otherwise, when obj is null, it gets
    // an object from the service (ServiceHolder::QueryFromService with the holder released through
    // 0x1466e0 right after) and does nothing when that object is null too. For each key of the map,
    // in ascending order, it calls vtable slot 6 of the object as (arg, key). The map is not changed.
    void CallSlot6ForEachKey(uint32_t arg, void *obj);
};

// 0x9c5baa (__cdecl, no arguments): returns game global 0x10f51b0 (a cached object). When it is
// null, the object is queried from the service (identifiers 0xc2c2eb0f first, 0x22c2eb1f second,
// as 0x20dbb3 does), stored in the global, and the local reference is released with
// ScopedRef::ReleaseObject (0x1cbd79, exceptions propagate). Re-reads the global on return.
void *GetCachedServiceObjectB();
