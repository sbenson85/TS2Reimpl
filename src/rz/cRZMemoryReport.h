#pragma once
// The memory report and its hook (lane HQ): two statics of the framework's memory-tracking module, each built by an XC
// entry. Only their constructors are ours so far; the objects keep the game's vtables (cRZMemoryReport's slots are
// still the image's), as the image's constructors store them, so owning the entries changes nothing the game sees.
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>

// cRZMemoryHook (RTTI; one slot): the static at 0x10fda50 (XC slot 754, entry 0x140856: the constructor with the text
// at 0xe98100, then atexit(0xd9898d: `mov ecx, object; jmp 0xc69881`, the destructor)).
struct cRZMemoryHook {
    static constexpr uint32_t kVTable = 0xe9e7ac;
    static constexpr uint32_t kStatic = 0x10fda50;
    void *vtable;
    cRZMemoryHook *Construct(const char *name); // 0xc696ce (thiscall, ret 4): the vtable only; the argument is unused
    void Destruct();                            // 0xc69881: the vtable stored again
};

// A 4-byte object whose constructor (0x1baa70) sets its one word to 1; the memory report allocates one (the pointer in
// 0x1103060, read by the report's slots 2 and 9), and about fifteen functions build one on the stack. Its meaning is
// not known yet (tentative: a count starting at one).
struct CountFromOne {
    uint32_t value;
    CountFromOne *Construct(); // 0x1baa70 (thiscall, returns this)
};

// cRZMemoryReport (RTTI; 10 slots, still the image's): the static at 0x10fda2c (XC slot 755, entry 0x140871: the
// constructor, then atexit(0xd98997: `mov ecx, object; jmp 0xc69888`, the destructor, not ours)).
struct cRZMemoryReport {
    static constexpr uint32_t kVTable = 0xe9e780;
    static constexpr uint32_t kStatic = 0x10fda2c;
    static constexpr uint32_t kCountRva = 0x1103060;  // CountFromOne *, allocated by the constructor
    static constexpr uint32_t kReportRva = 0x1103064; // cRZMemoryReport *, the constructed object (GetMemoryReport)

    void *vtable;             // +0x00
    uint8_t flag4;            // +0x04 (0)
    uint8_t pad5[3];
    cRZCriticalSection lock;  // +0x08
    uint8_t enabled;          // +0x10 (1; slot 6 reads it, slot 8 clears it)
    uint8_t pad11[3];
    uint32_t words14[3];      // +0x14 (zero: a {first, last, end} triple)
    uint32_t word20;          // +0x20 (0)

    // 0xc696d9 (thiscall, returns this): the vtable, the lock, the fields; a CountFromOne into 0x1103060, the DbgHelp
    // table loaded (0x1bcc62), and the object's address into 0x1103064.
    cRZMemoryReport *Construct();
};
static_assert(offsetof(cRZMemoryReport, lock) == 0x08);
static_assert(offsetof(cRZMemoryReport, enabled) == 0x10);
static_assert(offsetof(cRZMemoryReport, word20) == 0x20);

// 0x1bcc62 (cdecl, no arguments): the global DbgHelp table at 0x10cb000 loaded, `mov ecx, 0x10cb000; jmp 0x1bc8a7`.
uint8_t __cdecl LoadGlobalDbgHelp();
