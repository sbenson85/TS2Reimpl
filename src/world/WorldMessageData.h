#pragma once
#include <cstdint>

#include "game/vtable.h"
#include "rz/TopCallersA.h"

// The world database's message data classes (nTSWorld::c*Change, c*Spec, c*Data; RVAs 1.18.0.187). Each derives
// from nTSWorld::cWorldDBMessageData (vtable at +0, reference count at +4; src/rz/TopCallersA.h) and has a three-slot
// vtable: its own QueryInterface (slot 0), the base's AddRef (slot 1) and Release (slot 2, 0x5449e0). The game builds
// these objects inline where it posts the messages (no constructor functions), so they stay hook-shaped: the
// QueryInterface members are hooked at their RVAs and work on objects carrying the game's vtables.
//
// Every QueryInterface is the same body with the class's own interface id (__thiscall, ret 8, bool in al):
//   riid == the class's id: *ppv = this, AddRef through slot 1, true (ppv is not checked for null);
//   else a null ppv: false;
//   else riid == 1 (cIGZUnknown): *ppv = this, AddRef, true; anything else false.
// The speed-built ones (57 bytes) return from each branch; the two size-built ones (48 bytes, WorldMessageData.size.cpp)
// share one AddRef tail.

namespace nTSWorld {

namespace message_data {

inline constexpr uint32_t kIGZUnknownIid = 1;

// The shared body. Inlined into each class's member so each compiles to the original's own copy.
__forceinline bool QueryInterface(cWorldDBMessageData *self, uint32_t classIid, uint32_t riid, void **ppv)
{
    if (riid == classIid) {
        *ppv = self;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(self); // AddRef
        return true;
    }
    if (ppv == nullptr) return false;
    switch (riid) {
    case kIGZUnknownIid:
        *ppv = self;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(self); // AddRef
        return true;
    }
    return false;
}

} // namespace message_data

#define TS2_WORLD_MESSAGE_DATA(Name, Iid, Rva)                                                                         \
    class Name : public cWorldDBMessageData {                                                                          \
    public:                                                                                                            \
        static constexpr uint32_t kIid = Iid;                                                                          \
        static constexpr uint32_t kQueryInterfaceRva = Rva;                                                            \
        bool QueryInterface(uint32_t riid, void **ppv); /* slot 0, ret 8 */                                            \
    };                                                                                                                 \
    static_assert(sizeof(Name) == 8, "layout: the base's fields only (the rest is not touched by slot 0)");

// Speed-built (WorldMessageData.cpp).
TS2_WORLD_MESSAGE_DATA(cSupportChanged, 0x6c7065acu, 0x544380)
TS2_WORLD_MESSAGE_DATA(cRoofAddSpec, 0xcb994ee1u, 0x80f2b0)
TS2_WORLD_MESSAGE_DATA(cRoofRemoveSpec, 0xcb994ef3u, 0x80f2f0)
TS2_WORLD_MESSAGE_DATA(cRoofVisibilityChange, 0x8c6c2fb4u, 0x80f330)
TS2_WORLD_MESSAGE_DATA(cPoolSpec, 0xec9108d4u, 0x811dc0)
TS2_WORLD_MESSAGE_DATA(cFencePostAddSpec, 0x4b4bcfd9u, 0x816c00)
TS2_WORLD_MESSAGE_DATA(cWallAddSpec, 0x2ab8190cu, 0x816c80)
TS2_WORLD_MESSAGE_DATA(cWallPatternChange, 0x4ab81933u, 0x816cc0)
TS2_WORLD_MESSAGE_DATA(cWallRemoveSpec, 0x4ab8192eu, 0x816d00)
TS2_WORLD_MESSAGE_DATA(cWaterElevationChange, 0x4b70433eu, 0x8032a0)
TS2_WORLD_MESSAGE_DATA(cFloorElevationChange, 0x6ab8193du, 0xd11260)
TS2_WORLD_MESSAGE_DATA(cFloorPatternChange, 0x6ab81938u, 0xd112a0)
TS2_WORLD_MESSAGE_DATA(cFloorVisibilityChange, 0x0abaa824u, 0xd112e0)
TS2_WORLD_MESSAGE_DATA(cTerrainElevationChange, 0x6b6afdd1u, 0xd11320)
// Size-built (WorldMessageData.size.cpp).
TS2_WORLD_MESSAGE_DATA(cTerrainRectElevationChange, 0x2ba81065u, 0x6b955c)
TS2_WORLD_MESSAGE_DATA(cAwningUpdateData, 0xf057cc9eu, 0x6bce47)

#undef TS2_WORLD_MESSAGE_DATA

} // namespace nTSWorld
