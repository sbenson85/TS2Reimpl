#pragma once
#include <cstdint>

// Small helpers from the nGZSceneGraph cluster near RVA 0xa3c000. Only the leaf functions whose callees are all
// verified (or reached through vtables) are reimplemented here. The secondary-subobject slot bodies of cSGResource
// (0xa3c3c0 to 0xa3cdd0) belong to the cSGResource group (src/gz/cSGResource.cpp). The cluster's cSGSecondary
// slots (0xa3c750, 0xa3c790, 0xa3c890, 0xa3ca00, 0xa3cbf0) are all here: their callees are verified (the static
// getters in src/rz/StaticGetters2.*, the key parser in src/rz/StringKeyParser.*, IsInvalid in src/sg/SGResourceBlockers.*).
namespace SGResourceHelpers {

// 0xa3bdb0 (7 bytes, `mov eax,[ecx-8]; shr eax,31; ret`): a __thiscall helper with no stack arguments that
// returns bit 31 of the dword 8 bytes before `this`, as 0 or 1 in the full eax. Thirty callers use it.
class cSignBitBeforeThis {
public:
    uint32_t SignBit();
};

// The secondary subobject of nGZSceneGraph::cSGResource (vtable 0xe695dc, at object + 0x30): `this` is the
// subobject, as the thunks in that vtable pass it. Relative to this (see src/gz/cSGResource.h for the layout):
//   -0x30 object (primary vtable 0xe695a4)   -0x14 vbptr to the virtual base   -0x0c resource (refcounted)
//   -0x08 flag (checked by the key code)     -0x07 flag (cleared by SubmitKeyToService)
// The class has no data members: `this` always points into an existing cSGResource.
class cSGSecondary {
public:
    // 0xa3c790 (no arguments, 208 bytes): returns 0 when the flag at -0x08 is zero. Otherwise, if the virtual
    // base's slot 23 returns zero, returns 1. Otherwise it clears the flag at -0x07, takes a holder from the
    // service (GetServiceObject, slot 5), copies the key (primary slot 3) and returns the holder's slot 14
    // result for (object, key, resource), releasing the holder afterwards. The name is provisional.
    uint8_t SubmitKeyToService();

    // 0xa3c750 (no arguments, 20 bytes): the virtual base when the object (this - 0x30) is not null, else null.
    void *VirtualBaseIfObject();

    // 0xa3c890 (`ret 8`, 354 bytes, EHs frame with four unwind states): the object at this + 0x20 (a ServiceCell,
    // whose Reset this calls) is reset first. Then, when flag A (-0x08) is set, a holder is taken from the
    // service (slot 5, the same identifiers as SubmitKeyToService), the key is copied, the holder's slot 10 is
    // called with it, and the holder is released. After that: the virtual base's slot 4 gives an object X
    // (out) and a pointer R; the object's primary slot 8 gets the dword at R+4; X is released (slot 2); the
    // primary slots 9 and 10 get the two arguments; flag A is set to 1. Last, a second holder is taken from the
    // service into the first argument's slot, its key is copied, and the holder's slot 9 result (al) is
    // returned after the holder is released. The name is provisional.
    uint8_t ResetAndSubmit(void *a1, void *a2);

    // 0xa3ca00 (`ret 0xc`, 485 bytes, EHs frame with five unwind states). Flag A (-0x08) set: the first holder block
    // (as ResetAndSubmit's). Then a1 is a ServiceCell: Open(a1, 1), Assign into the cell at this - 0x10, Close (also on a
    // throw from Assign). The virtual base's slot 15 gets a3. Flag A becomes !IsInvalid() of the cell at this - 0x10;
    // when that is 0, or a3 is null, the result is 0. Otherwise the key parse (ParseBaseKey: base slot 4 gives X and R,
    // ParseKey(out, base slot 17, the dword at R + 4, null), X is released), primary slot 4 gets the Result, primary slot
    // 9 gets a2, and the second holder (service slot 5, key from primary slot 3, holder slot 9 with (key, object))
    // gives the result. The name is provisional.
    uint8_t OpenAssignAndSubmit(uint32_t a1, void *a2, void *a3);

    // 0xa3cbf0 (`ret 8`, 472 bytes, the same frame shape). Open(a1, 1), Assign, Close, then the virtual base's slot 15
    // with a2; the flag-A holder block; the flag is !IsInvalid(); a null a2 or an invalid cell returns 0. Otherwise
    // ParseBaseKey, primary slot 4 gets the Result, and the second holder gives the result (no primary slot 9 call).
    // The name is provisional.
    uint8_t AssignAndSubmit(uint32_t a1, void *a2);
};

} // namespace SGResourceHelpers
