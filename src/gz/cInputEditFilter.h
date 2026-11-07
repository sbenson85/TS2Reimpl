#pragma once
// cInputEditFilter (lane MC, foundations F5): the message filter an input edit window installs (built inline by
// 0xcaaed0, which is not ours: new 0xc bytes, cRZRefCount's constructor, then the vtables). It hands the close
// messages to the window's parent: message type 6, and type 4 (a key) for Escape (0x1b) and the back quote (0x60).
// 0xc bytes: +0 cIGZWinMessageFilter (game vtable 0xeb5e4c: QueryInterface, AddRef / Release through the +4 base,
// Filter), +4 cRZRefCount (0xeb5e60: the deleting destructor thunk 0xca9fd9, Release, AddRef, RemoveRef, GetRefCount
// folded with cRZRefCount's). The builder is not ours, so the objects carry the game's vtables: the three bodies of
// its own are hooked at their RVAs.
#include "rz/cRZRefCount.h"

#include <cstdint>

namespace ifaces {
class cIGZWin;
}

class cInputEditFilter {
public:
    static constexpr uint32_t kVTable = 0xeb5e4c; // the game's
    static constexpr uint32_t kRefVTable = 0xeb5e60;
    static constexpr uint32_t kIid = 0xc6ae7085; // cIGZWinMessageFilter
    static constexpr uint32_t kSize = 0xc;

    // A window message: the type, then (keys) the key code.
    struct Message {
        uint32_t type;
        uint32_t key;
    };

    // 0xcabbc0 (slot 0): ids 1 and 0xc6ae7085: this, counted (slot 1).
    bool QueryInterface(uint32_t riid, void **out);
    // 0xcaa120 (slot 3, ret 8): type 6 to the window's parent (DoMessage), false; type 4 with key 0x1b or 0x60 the same,
    // true; anything else false, not passed on.
    bool Filter(ifaces::cIGZWin *window, Message *message);
    // 0xcaa030 (the +4 face's slot 0 through the thunk 0xca9fd9): the vtables reset, cRZRefCount's destructor, freed
    // with flag 1.
    cInputEditFilter *DeletingDestructor(uint32_t flags);

    const void *mVTable;
    cRZRefCount mRef; // +4
};
static_assert(sizeof(cInputEditFilter) == cInputEditFilter::kSize, "cInputEditFilter size");
