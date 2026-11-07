#pragma once
#include <cstdint>
#include <map>

// Helpers of the UtilitiesC group. RVAs are 1.18.0.187. Most owners are not identified; the names
// say what each function does.

// An 8-byte object: a vtable at +0 and a dword at +4.
struct UtilObjectA {
    void *mVTable;
    uint32_t mValue;

    // 0x1b9035 (__thiscall, plain ret): sets the vtable to the game's table at RVA 0xda2fa8 (the
    // 0x1b9042 copy uses 0xda2fc0), clears mValue, returns this.
    UtilObjectA *Construct();
};

static_assert(sizeof(UtilObjectA) == 8, "UtilObjectA layout");

// A 0x14-byte object: a vtable at +0, a dword argument at +4, zero dwords at +8 and +0x10, and two
// words at +0xc (zero) and +0xe (6).
struct UtilObjectB {
    void *mVTable;
    uint32_t mArg;
    uint32_t mZero8;
    uint16_t mWordC;
    uint16_t mWordE;
    uint32_t mZero10;

    // 0x9c7b3d (__thiscall, ret 4): sets the fields above (vtable at RVA 0xe6180c), returns this.
    UtilObjectB *Construct(uint32_t arg);
};

static_assert(sizeof(UtilObjectB) == 0x14, "UtilObjectB layout");

// A holder with a single object pointer, filled by the service object through one of two queries
// with their own identifiers. The same 4-byte layout as ServiceHolder and ScopedRef.
struct ServiceQueryHolder {
    void *mObject;

    // 0x20dbd8 (__thiscall, ret 0): clears mObject, queries the service with the identifiers
    // 0xa417445e (first parameter) and 0x5a4 (second), output mObject. Returns this. Nothing happens
    // beyond the clear when there is no service object.
    ServiceQueryHolder *QueryWithIdsA();

    // 0x2365e3 (__thiscall, ret 0): as QueryWithIdsA, with the identifiers 0xcba95d2d (first) and
    // 0x4ba8123a (second).
    ServiceQueryHolder *QueryWithIdsB();
};

static_assert(sizeof(ServiceQueryHolder) == 4, "ServiceQueryHolder layout");

// An object with an std::map<uint32_t, uint32_t> at its start (head pointer at +0), not identified.
struct KeyMapSlot5Walker {
    std::map<uint32_t, uint32_t> mMap;

    // 0x1c0458 (__thiscall, ret 8): when obj is null it gets one from the service (ServiceHolder::
    // QueryFromService, released through 0x1466e0 right after) without a null check after that.
    // For each key of the map, in ascending order, it calls vtable slot 5 of obj as (arg, key).
    // The map is not changed. ORIGINAL-BUG: with no service object and a non-empty map, the walk
    // calls through a null pointer; see the note in utilities_c.cpp.
    void CallSlot5ForEachKey(uint32_t arg, void *obj);
};

// 0x2366b8 (__cdecl, no arguments): returns game global 0x10cc648 (a cached object). When it is null,
// the object is queried with ServiceQueryHolder::QueryWithIdsB (0x2365e3), stored in the global, and
// the local reference is released with ScopedRef::ReleaseObject (0x1cbd79). Re-reads the global.
void *GetCachedServiceObjectC();

// 0x9c5bd6 (__cdecl, no arguments): the same as GetCachedServiceObjectC with the cache global 0x10f51ac
// and ServiceQueryHolder::QueryWithIdsA (0x20dbd8).
void *GetCachedServiceObjectD();

// A 3x3 matrix with a translation, as stored in an object: a dword at +0 (not touched here), the
// matrix at +4 (row-major, nine floats) and the translation at +0x28.
struct AffineTransform3 {
    uint32_t mUnknown0;
    float mMatrix[9];
    float mOffset[3];

    // 0x19c820 (__thiscall, ret 8): out = matrix * in + offset, in single precision. Operands are
    // read before out is written, so out may be in. Each component is computed as
    // ((y*m[i][1] + x*m[i][0]) + z*m[i][2]) + offset[i].
    void TransformPoint(float *out, const float *in);
};

static_assert(sizeof(AffineTransform3) == 0x34, "AffineTransform3 layout");
