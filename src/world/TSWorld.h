#pragma once
#include <cstdint>

// nTSWorld functions (RVAs 1.18.0.187). Each original is __thiscall, so these are written as
// __fastcall with a dummy edx: `this` arrives in ecx, the stack arguments follow, and the callee pops
// them, the same as the original. That makes them plain functions a hook entry can point at.
//
// Interface pointers are stored with the offset given per function, and the object is AddRef'd
// through its own vtable (slot 1) when a pointer is stored. The original's `neg/sbb/and` mask turns
// a null `this` into a null pointer; a valid object is never null, and the AddRef would fault
// first, so the mask only matters for the stored value.
//
// This header has no classes: `this` is void *, as in src/rz/QueryInterfaces.h.

namespace tsworld {

// 0x8032e0 (cWorldTerrain, slot 0): ret 8. Null ppv fails. 0xe4fda3d4 stores this + 0xc, 0xeb589762
// stores this + 8, both masked and AddRef'd; anything else goes to cRZUnknown::QueryInterface
// (0x1b945c) on this.
bool __fastcall WorldTerrainQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x80f370 (cWorldRoof, slot 0): ret 8. Null ppv fails. 0xab9406b1 stores this + 8, 0xe4fda3d4
// stores this + 0xc (masked, AddRef); else cRZUnknown::QueryInterface on this.
bool __fastcall WorldRoofQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x811e00 (cWorldPool, slot 0): ret 8. Null ppv fails. 0x2c900fe6 stores this + 8, 0xe4fda3d4
// stores this + 0xc (masked, AddRef); else cRZUnknown::QueryInterface on this.
bool __fastcall WorldPoolQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x812f60 (cWorldCurvedPool, slot 0): ret 8. Null ppv fails. 0x916da151 stores this + 8, 0xe4fda3d4
// stores this + 0xc (masked, AddRef); else cRZUnknown::QueryInterface on this.
bool __fastcall WorldCurvedPoolQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x806c70 (cWallGraph, slot 0): ret 8. Null ppv fails. riid 1 and 0x2a284d16 store this (no mask),
// 0xe4fda3d4 stores this + 4 (masked); each AddRefs. Anything else returns false: no fall-through.
bool __fastcall WallGraphQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x807240 (cWallGraph, Release on the primary subobject): ret, no stack arguments. Decrements the
// count at this + 8; when it reaches zero, calls the deleting destructor through slot 24 (0x60) with
// flag 1. Returns the new count, or 0 after a delete (the original clears eax then).
uint32_t __fastcall WallGraphRelease(void *self, void *edx);

// 0x81df10 (cWorldOutsideSupport, slot 0): ret 8. Null ppv fails. 0xb21f9b25 stores this + 8
// (masked, AddRef); else cRZUnknown::QueryInterface on this.
bool __fastcall WorldOutsideSupportQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x816c40 (cPartitionManager, slot 0): ret 8. Null ppv fails. 0x6ca2a642 and 0x6ca2a64e store this
// (no mask) and AddRef it. Anything else adds 4 to this and tail-jumps to cRZUnknown::QueryInterface
// (0x1b945c), so the base is called on this + 4.
bool __fastcall PartitionManagerQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x81bbe0 (cProxyManager, slot 0): ret 8. Null ppv fails. 0x8ce45d17 stores this (no mask) and
// AddRefs it. Anything else adds 4 to this and tail-jumps to cRZUnknown::QueryInterface on this + 4.
bool __fastcall ProxyManagerQueryInterface(void *self, void *edx, uint32_t riid, void **ppv);

// 0x801ff0 (cWorldTerrain, GetGZCLSID): `mov eax, 0x4b58975b; ret`.
uint32_t __fastcall WorldTerrainGetGZCLSID(void *self, void *edx);

// 0x806a90 (cWallGraph, GetGZCLSID): `mov eax, 0x0a284d0b; ret`.
uint32_t __fastcall WallGraphGetGZCLSID(void *self, void *edx);

// 0x80e640 (cWorldRoof, GetGZCLSID): `mov eax, 0xab9406aa; ret`.
uint32_t __fastcall WorldRoofGetGZCLSID(void *self, void *edx);

// 0x811280 (cWorldPool, GetGZCLSID): `mov eax, 0x0c900fdb; ret`.
uint32_t __fastcall WorldPoolGetGZCLSID(void *self, void *edx);

// 0x812f30 (cWorldCurvedPool, GetGZCLSID): `mov eax, 0x916da14d; ret`.
uint32_t __fastcall WorldCurvedPoolGetGZCLSID(void *self, void *edx);



// 0x8188e0 (cPartitionManager, vf6): ret 8. Stores arg1 at this+0x18 and arg2 at this+0x24.
void __fastcall PartitionManagerSetPair(void *self, void *edx, uint32_t first, uint32_t second);

// 0x818900 (cPartitionManager, vf5): ret 0x10. Stores arg1 at this+0x10, arg2 at this+0x14, arg3 at
// this+0x1c and arg4 at this+0x20.
void __fastcall PartitionManagerSetFour(void *self, void *edx, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

} // namespace tsworld
