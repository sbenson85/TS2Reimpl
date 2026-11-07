#pragma once
#include "gz/cIGZExceptionNotification.h"

#include <cstdint>

// cRZExceptionNotification (1.18.0.187), in the standalone shape (docs/standalone-shape.md): the framework's exception
// notification object (the framework's slot 39 hands it out). 8 bytes: the vtable (0xda44d4, 5 slots), the count.
// The game's one creation site is the framework constructor 0x141d76 (ours: `new cRZExceptionNotification`), which
// called the constructor 0x1c8c84.
//
//   0 QueryInterface 0x1c8d3f   1 AddRef 0x148fb0 (folded, verified elsewhere)   2 Release 0x1c8d77
//   3 Notify 0x1c8cb7           4 deleting destructor 0x1c8c98 (destructor body 0x1c8c91)
class cRZExceptionNotification : public cIGZExceptionNotification {
public:
    static constexpr uint32_t kVTable = 0xda44d4;

    cRZExceptionNotification() : mRefCount(0) {} // 0x1c8c84

    bool QueryInterface(uint32_t riid, void **ppvObj) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    bool Notify(const char *module, int32_t line) override;
    virtual ~cRZExceptionNotification(); // slot 4

    uint32_t mRefCount; // +4
};

static_assert(sizeof(cRZExceptionNotification) == 8, "cRZExceptionNotification layout");

// The constructor hook's entry point (a one-statement forward, docs/standalone-shape.md step 6). The other adapters are gone
// (in effect through callers: only the game's vtable reached them, and no object carries it while the constructor hook is in).
struct cRZExceptionNotificationAdapters {
    static cRZExceptionNotification *__fastcall Construct(void *memory, void *);
};
