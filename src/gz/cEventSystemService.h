#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>

#include "game/owned.h"

// The connection manager static (0x44 bytes, as words) and its thread-safe-statics guard.
TS2_OWNED_STATIC(uint32_t, 10cadd8)[0x11];
TS2_OWNED_STATIC(int, 10cae1c);

// nGZEvent::cEventSystemService (1.18.0.187): the event system's service, 0x20 bytes, registered with the framework by
// nGZEvent::cEventCOMDirector as class 0x3b1e8967 (creator 0x1a8553). Its one interface slot hands out the process-wide
// connection manager, a function-local static (0x10cadd8, guard 0x10cae1c) built on first use. Game vtables: 0xda21c4
// (cIEventSystemService, 4 slots), 0xda21d8 (cIGZSystemService part at +4, 13), 0xda2210 (cRZUnknownMT part at +8, 6).
namespace nGZEvent {

// The static connection manager is ours (gz/cConnectionManager.h, lane LR): built in the game's static by our constructor
// adapter, destroyed by our destructor adapter at exit.
struct cConnectionManager;

// The service's interface (its own vtable is stored first by the constructor; RTTI cIEventSystemService, id 0x1b1e895a).
class cIEventSystemService : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x1b1e895a;

    virtual cConnectionManager *GetConnectionManager() = 0; // slot 3 (game: a 5-byte jmp 0x1a854e to 0x1aad98)
};

// The service in the standalone shape (docs/standalone-shape.md): the compiler emits the three vtables and the adjustor
// thunks; the game's creator 0x1a8553 (the COM director's factory for class 0x3b1e8967) and constructor 0x1a84d5 are hooked
// to Create and the adapter, so every service carries our vtables.
class cEventSystemService : public cIEventSystemService, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xda21c4; // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xda21d8;
    static constexpr uint32_t kUnknownVTable = 0xda2210;
    static constexpr uint32_t kSize = 0x20;
    static constexpr uint32_t kServiceId = 0x5b1e896f;
    static constexpr int32_t kPriority = 0x4c4b41;
    static constexpr uint32_t kInterfaceId = cIEventSystemService::kIid;
    static constexpr uint32_t kClassId = 0x3b1e8967; // the COM class id (0x1a8569 returns it)
    static constexpr uint32_t kConnectionManager = 0x10cadd8; // the static instance
    static constexpr uint32_t kConnectionManagerGuard = 0x10cae1c; // its thread-safe-statics guard

    cEventSystemService() : rz::cRZSystemService(kServiceId, kPriority) {} // 0x1a84d5
    // 0x1a850a (nothing of its own); deleting 0x1a852f (+8 slot 3, thunk 0x1a8527).

    static cEventSystemService *__cdecl Create(); // 0x1a8553: a new service (null when new failed)

    // cIGZUnknown (all three faces; the +4 / +8 ones through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0x1a856f (thunks 0x1a85a0, 0x1a85a8)
    uint32_t AddRef() override;                              // folded 0x145240 (the service part's)
    uint32_t Release() override;                             // folded 0x1a85b0

    // cIEventSystemService
    cConnectionManager *GetConnectionManager() override;    // 0x1aad98 (through the jmp 0x1a854e)
};
static_assert(sizeof(cEventSystemService) == cEventSystemService::kSize, "cEventSystemService size");
static_assert(offsetof(cEventSystemService, mServiceId) == 0x10, "cEventSystemService layout: the service part at +4");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx.
struct cEventSystemServiceAdapters {
    static cEventSystemService *__fastcall Construct(void *memory, void *edx); // 0x1a84d5
};

} // namespace nGZEvent
