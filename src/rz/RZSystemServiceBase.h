#pragma once
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

// cRZUnknownMT and cRZSystemService as real base classes, for classes in the standalone shape (docs/standalone-shape.md).
// Declarations by lane EC's structure pass (docs/subsystems/startup.md); bodies and adapters by lane EI
// (RZSystemServiceBase.size.cpp, QueryInterface in RZSystemServiceBase.cpp: the game built that one for speed). The
// hook-shaped struct `cRZSystemService` (rz/cGZAllocatorService.h, `mVTable` members, used by ~60 service constructors of
// our own hook-shaped services) holds the global name, so these live in namespace rz, as rz::cRZUnknown
// (rz/RZUnknownBase.h) does.
//
// Creation: the game builds cRZSystemService subobjects only through the two constructors (0x1b9584, 0x1b95bf) and the
// cRZUnknownMT part only through 0x1b9078 (called by those two); the vtables 0xda3058 / 0xda3090 / 0xda2fec are referenced
// by nothing else than those and the destructors 0x1b95fe / 0x1b90ab. With the constructor hooks on, the base part of
// every service the game constructs is built by our constructor (then the derived constructor stores its own vtables).
namespace rz {

// cRZUnknownMT: cRZUnknown with an interlocked count (game vtable 0xda2fec; 6 slots, the same order as
// rz::cRZUnknown). 8 bytes: vtable, count at +4.
class cRZUnknownMT : public cIGZUnknown {
public:
    cRZUnknownMT() : mRefCount(0) {} // 0x1b9078 (the vtable, a zero count)

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0x1b945c (id 1 -> this)
    uint32_t AddRef() override;                              // slot 1, 0x1b914d (interlocked +1, 0x1bdbad)
    uint32_t Release() override;                             // slot 2, 0x1b9517: delete at a count of 1 or below
    virtual ~cRZUnknownMT();                                 // slot 3, deleting destructor 0x1b912e
    virtual uint32_t RemoveRef();                            // slot 4, 0x1b9544
    virtual uint32_t RefCount();                             // slot 5, folded 0x147a30 (+4)

    cRZUnknownMT(const cRZUnknownMT &) = delete;
    cRZUnknownMT &operator=(const cRZUnknownMT &) = delete;

    volatile uint32_t mRefCount; // +4
};
static_assert(sizeof(cRZUnknownMT) == 8, "cRZUnknownMT: vtable, count");

// cRZSystemService: the base of every system service (RTTI bases cIGZSystemService at +0, cRZUnknownMT at +4; game
// vtables 0xda3058 (13 slots) and 0xda3090 (6)). 0x1c bytes. The +4 face's cIGZUnknown slots are the compiler's
// adjustor thunks (0x1b9672, 0x1b9648, 0x1b9682) and its slot 3 the destructor thunk 0x1b9612, so one override of each
// covers both faces, as here. Two constructors exist: (id, priority) 0x1b9584 (~60 callers; tick priority = priority)
// and (id, priority, tickPriority) 0x1b95bf (ret 0xc; 2 callers, 0x96f35b and 0x972aab).
class cRZSystemService : public cIGZSystemService, public cRZUnknownMT {
public:
    cRZSystemService(uint32_t id, int32_t priority);                         // 0x1b9584
    cRZSystemService(uint32_t id, int32_t priority, int32_t tickPriority);   // 0x1b95bf
    ~cRZSystemService() override;                                            // 0x1b95fe; deleting 0x1b961a

    // cIGZUnknown (both faces)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1b9650: kIID -> this, else cRZUnknownMT's
    uint32_t AddRef() override;                              // folded 0x1b9640 (add ecx, 4; jmp 0x1b914d)
    uint32_t Release() override;                             // 0x1b967a

    // cIGZSystemService
    uint32_t GetServiceID() override;               // folded 0x145770
    void SetServiceID(uint32_t id) override;        // folded 0x149c60
    int32_t GetServicePriority() override;          // folded 0x149cf0
    bool IsServiceRunning() override;               // folded 0x156110
    void SetServiceRunning(bool running) override;  // folded 0x1b9690
    bool Init() override;                           // shared 0x144910 (true)
    bool Shutdown() override;                       // shared 0x144910 (true)
    bool OnTick(uint32_t counter) override;         // shared 0x148260 (true)
    bool OnIdle(uint32_t counter) override;         // shared 0x148260 (true)
    int32_t GetServiceTickPriority() override;      // folded 0x149d00

    uint32_t mServiceId;      // +0x0c
    int32_t mPriority;        // +0x10
    int32_t mTickPriority;    // +0x14
    bool mRunning;            // +0x18 (a byte handed out raw by IsServiceRunning: bool, so the getter does not normalise)
};
static_assert(sizeof(cRZSystemService) == 0x1c, "cRZSystemService size");
static_assert(offsetof(cRZSystemService, mRefCount) == 8, "cRZSystemService layout: cRZUnknownMT at +4");
static_assert(offsetof(cRZSystemService, mServiceId) == 0xc, "cRZSystemService layout");
static_assert(offsetof(cRZSystemService, mPriority) == 0x10, "cRZSystemService layout");
static_assert(offsetof(cRZSystemService, mTickPriority) == 0x14, "cRZSystemService layout");
static_assert(offsetof(cRZSystemService, mRunning) == 0x18, "cRZSystemService layout");

// The RVA hooks' entry points (docs/standalone-shape.md, step 6): `this` in ecx (the face the slot is called on), the dummy
// edx, the stack arguments. Reached by the game's direct calls (the derived services' constructors and destructors, the
// cRZUnknownMT slot bodies other classes' vtables hold) and by objects that still carry the game's vtables.
struct cRZUnknownMTAdapters {
    static cRZUnknownMT *__fastcall Construct(void *memory, void *edx);                     // 0x1b9078
    static void __fastcall Destruct(cRZUnknownMT *self, void *edx);                         // 0x1b90ab
    static cRZUnknownMT *__fastcall DeletingDestructor(cRZUnknownMT *self, void *edx, uint32_t flags); // 0x1b912e
    static uint32_t __fastcall Release(cRZUnknownMT *self, void *edx);                      // 0x1b9517
    static uint32_t __fastcall RemoveRef(cRZUnknownMT *self, void *edx);                    // 0x1b9544
};

struct cRZSystemServiceAdapters {
    static cRZSystemService *__fastcall Construct(void *memory, void *edx, uint32_t id, int32_t priority); // 0x1b9584
    static cRZSystemService *__fastcall ConstructWithTickPriority(void *memory, void *edx, uint32_t id, int32_t priority,
                                                                  int32_t tickPriority); // 0x1b95bf
    static void __fastcall Destruct(cRZSystemService *self, void *edx);                     // 0x1b95fe
    static cRZSystemService *__fastcall DeletingDestructor(cRZSystemService *self, void *edx, uint32_t flags); // 0x1b961a
    static bool __fastcall QueryInterface(cRZSystemService *self, void *edx, uint32_t riid, void **out); // 0x1b9650
};

// cRZThreadSafeObject: a non-polymorphic base holding the lock pointer (cGZCOM has it at +0x20, after its polymorphic
// bases, as MSVC places bases without a vfptr after those with one; the pointer is set to the object's own critical
// section member).
struct cRZThreadSafeObject {
    void *mLock; // the cRZCriticalSection the object locks through (slots 1 and 2 of its vtable)
};
static_assert(sizeof(cRZThreadSafeObject) == 4, "cRZThreadSafeObject: the lock pointer");

} // namespace rz
