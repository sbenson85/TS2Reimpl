#pragma once
#include <cstdint>

// Helpers of the UtilitiesE group. RVAs are 1.18.0.187.

// The base destructor body of nTSLua::cTSLuaWrapper (vtable 0xe88584; 0xb65870 is its deleting
// wrapper). Only the cRZUnknown base words are declared here: the rest of the object is not known.
struct TSLuaWrapperBase {
    const void *mVTable;
    uint32_t mRefCount;

    // 0xb65860 (__thiscall, no arguments, plain ret): sets the cTSLuaWrapper vtable, then runs
    // cRZUnknown::Destruct (0x1b9096), which sets the cRZUnknown vtable. The original reaches the
    // base through a tail jump; the stack is the same either way.
    void Destruct();
};

static_assert(sizeof(TSLuaWrapperBase) == 8, "TSLuaWrapperBase layout");

// 0xae8900 (__cdecl, no arguments): returns game global 0xa10f8fcc (a cache). When it is null, a
// service object is asked (GetServiceObject, game global 0x10c9378) through vtable slot 5 for a
// holder, with identifiers 0xe5074b6b (first parameter) and 0x25074b4d (second) and the holder as
// the output. Slot 5 of the holder gives the value that is cached, and the holder is released with
// ScopedRef::ReleaseObject (0x1cbd79). If the query or slot 5 throws, the original does not release
// the holder (a leak, marked ORIGINAL-BUG in the source; the test checks it). With no service object
// the original dereferences a null holder (an access violation); the replacement does the same, and
// the tests do not cover it.
void *GetCachedQueriedObject();
// 0xae8980: the same query, cached in 0x10f8fd0, the result being the holder's slot 11 (the material parser / registry).
void *GetMaterialParserCached();

// 0xb6dfb0 (__cdecl, two arguments, ret 0): if object is null, returns false. Otherwise it calls
// vtable slot 6 of object (no arguments), and if that gives a non-null target, vtable slot 13 of
// the target as (object, key), where key points to 16 bytes. Returns true only then. The original
// sets only al: the upper bits of eax are left over from earlier code on some paths, so callers and
// tests use al alone.
bool NotifySlot6ResultWithKey(void *object, const void *key);

// 0xb6dfe0 (__cdecl, five arguments, ret 0): copies the four words of the key into a local block and
// calls NotifySlot6ResultWithKey(object, block). Returns its result (al only, as above).
bool NotifySlot6ResultWithKeyWords(void *object, uint32_t k0, uint32_t k1, uint32_t k2, uint32_t k3);
