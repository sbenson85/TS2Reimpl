#pragma once
#include "gz/cGZPersistResource.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// cRZStringResource: a string that is also a persist resource (RTTI: cRZString at +0, cGZPersistResource at +0x20),
// 0x3c bytes. RVAs 1.18.0.187. Lane LS.
//
// The game builds it inline in four creators that are not ours (cTSResourceFactoryString::vf8 0x1ebc00,
// cTSResourceFactoryStringDescription::vf8 0x1ebd90, cRZStringResourceFactory::vf9 0x9b6330, cPITextCvt::vf4 0xcad960),
// which store the game's vtables 0xda6738 (+0, the string face: cRZString's 34 slots with its own 0, 1, 2 and 33) and
// 0xda67c4 (+0x20, the resource face: cGZPersistResource's 13 with its own 0, 1, 2 and 12). Hook-shaped like its
// cRZString base: the class's own bodies and its four adjustor thunks are hooked at their RVAs, so every object the game
// builds runs ours. The other slots are cRZString's (ours, hooked) and cGZPersistResource's folded bodies.
//
// Reference counting is the resource part's (+0x24): the string face's AddRef / Release forward there, and the
// resource's Release (0x9b6260) deletes the object through the resource face's slot 12 (the thunk 0x1eb109 to 0x1eb120).
struct cRZStringResource {
    static constexpr uint32_t kStringVTable = 0xda6738;
    static constexpr uint32_t kResourceVTable = 0xda67c4;
    static constexpr uint32_t kSize = 0x3c;
    static constexpr uint32_t kResourceOffset = 0x20;

    union {
        cRZString mString; // +0x00: vtable, the std::string (+4), the string's own count (+0x1c, unused here)
    };
    union {
        cGZPersistResource mResource; // +0x20: vtable, the count (+0x24), +0x28, the key (+0x2c)
    };

    cRZStringResource() = delete;
    ~cRZStringResource() = delete;

    // The string face (this in ecx).
    bool QueryInterface(uint32_t riid, void **out);   // slot 0, 0x9b63d0 (ret 8)
    uint32_t AddRef();                                // slot 1, 0x1eb500: the resource part's AddRef
    uint32_t Release();                               // slot 2, 0x1ee2e0: the resource part's Release
    cRZStringResource *DeletingDestructor(uint32_t flags); // slot 33, 0x1eb120 (ret 4)

    static cRZStringResource *FromResource(void *face)
    {
        return reinterpret_cast<cRZStringResource *>(static_cast<uint8_t *>(face) - kResourceOffset);
    }
};
static_assert(sizeof(cRZStringResource) == cRZStringResource::kSize);
static_assert(offsetof(cRZStringResource, mResource) == cRZStringResource::kResourceOffset);

// The resource face's adjustor thunks (this = the +0x20 face, in ecx), hooked at their RVAs.
struct cRZStringResourceThunks {
    static bool __fastcall QueryInterface(void *face, void *, uint32_t riid, void **out);         // 0x1ee218
    static uint32_t __fastcall AddRef(void *face, void *);                                       // 0x1eb508
    static uint32_t __fastcall Release(void *face, void *);                                      // 0x1ee2e8
    static cRZStringResource *__fastcall DeletingDestructor(void *face, void *, uint32_t flags);  // 0x1eb109
};
