#pragma once
#include "game/owned.h"
#include "gz/cIGZCriticalSection.h"

#include <cstdint>

struct _RTL_CRITICAL_SECTION;

// Recursive lock wrapping a Win32 critical section, kept in a separate heap block with a lock count.
// Implements cIGZCriticalSection (not a cIGZUnknown). RVAs are 1.18.0.187.
//
// Hook-shaped: the members are hooked at the game's RVAs, so the game's vtable 0xda2708 runs them. The vtable is an
// owned static (docs/standalone-shape.md, "Owned statics"): our constructor and destructor store VTable(), the game's
// table in hooked builds and rzface::cRZCriticalSection's (below) in an image-free program.
class cRZCriticalSection {
public:
    static constexpr uint32_t kVTable = 0xda2708; // the game's; VTable() is its owned symbol
    // Set by a static initialiser (0x1af60a) to kernel32's TryEnterCriticalSection, or null on
    // pre-NT4 Windows; TryLock falls back to a blocking lock when it's null. Owned: ts2_rva_10caea0 below.
    static constexpr uint32_t kTryEnterCriticalSection = 0x10caea0;
    using TryEnterFn = int(__stdcall *)(_RTL_CRITICAL_SECTION *);

    struct Block {
        unsigned char criticalSection[24]; // CRITICAL_SECTION
        int32_t lockCount;
    };

    cRZCriticalSection *Construct();                     // 0x1af64e (constructor body)
    void Destruct();                                     // 0x1af68a (destructor body)
    cRZCriticalSection *DeletingDestructor(uint32_t flags); // 0x1af6a8, slot 6
    int32_t Release();                                   // 0x1af6e2, slot 0: deletes this
    int32_t Lock();                                      // 0x1af6ce, slot 1: returns the new count
    int32_t Unlock();                                    // 0x1af715, slot 2: returns the new count
    int32_t TryLock();                                   // 0x1af6f0, slot 3: 0 if busy, else the new count
    int32_t IsValid() { return 1; }                      // slot 4 (the game's: shared_vf_886f4, folded; not hooked)
    int32_t IsLocked();                                  // 0x1af6c7, slot 5: the count

    // The word the constructor and destructor store: ts2_rva_da2708 (the game's table in hooked builds, OwnVTable() in
    // an image-free program).
    static const void *VTable();
    // rzface::cRZCriticalSection's vtable, our own table (the image-free program's VTable(); tests call it by slot).
    static const void *OwnVTable();

    const void *mVTable;
    Block *mBlock;
};

static_assert(sizeof(cRZCriticalSection) == 8, "cRZCriticalSection layout");
static_assert(sizeof(cRZCriticalSection::Block) == 0x1c, "cRZCriticalSection block layout");

// The TryEnterCriticalSection pointer (docs/standalone-shape.md, "Owned statics"): the image's in the hooked game, set
// by the XC loader rz::TryEnterCriticalSectionLoader (src/rz/WinApiTables.h); standalone/owned_statics.cpp's otherwise.
TS2_OWNED_STATIC(cRZCriticalSection::TryEnterFn, 10caea0);
// The vtable (owned.h, OwnedVTable).
TS2_OWNED_STATIC(const OwnedVTable, da2708);

namespace rzface {

// cRZCriticalSection's own vtable (the image-free program's): an interface face over the hook-shaped class (no data; `this` is the object). Each
// slot runs the member of the same name (src/rz/cRZCriticalSection.size.cpp); slot 6 is the deleting destructor.
// Namespaced so that its RTTI name is still cRZCriticalSection (tools/matchcheck.py ties our vtable to 0xda2708).
class cRZCriticalSection final : public cIGZCriticalSection {
public:
    int32_t Release() override;
    int32_t Lock() override;
    int32_t Unlock() override;
    int32_t TryLock() override;
    int32_t IsValid() override;
    int32_t IsLocked() override;
    virtual void *DeletingDestructor(uint32_t flags); // slot 6
};

} // namespace rzface

inline const void *cRZCriticalSection::VTable() { return &ts2_rva_da2708; }

// The address of rzface::cRZCriticalSection's vtable as a link-time constant: the compiler folds the local face's
// vptr into `offset ??_7cRZCriticalSection@rzface@@6B@` (no object is built).
inline const void *cRZCriticalSection::OwnVTable()
{
    rzface::cRZCriticalSection face;
    return *reinterpret_cast<const void *const *>(&face);
}
