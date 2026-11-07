#pragma once
#include "game/owned.h"
#include "rz/Singleton_20ddfb.h"

#include <cstddef>
#include <cstdint>

// Cached service objects (RVAs are 1.18.0.187). These are plain caches in game globals, not
// thread-safe statics: the first call acquires the object from the service object (GetServiceObject,
// 0x1427b5, reached through the thunk 0x14323f) and stores it; later calls return the stored value.
//
//   0x20dcb6  AcquireServiceRefB: ScopedRef acquire with identifiers 0xe92db716 / 0x292db38c
//   0x20dd00  AcquireServiceRefC: ScopedRef acquire with identifiers 0xdae3783f / 0xdae3783e
//   0x20de51  GetCachedSingletonB: cache at 0x10cc28c, filled through AcquireServiceRefB
//   0x20df6f  GetCachedSingletonC: cache at 0x10cc290, filled through AcquireServiceRefC
//   0x20dde0  GetCachedSlot23: cache at 0x10cc2a8, filled with slot 23 of GetCachedSingleton()
//   0xaeb820  GetCachedObjectB: cache at 0x10f9070, filled with slot 8 of the object acquired with
//             identifiers 0xe5074b6b / 0x25074b4d
//   0xaeb800  Dispatch8 (__thiscall, no arguments): slot 8 of GetCachedObjectB() with the dword at
//             this as its argument
//   0xa3c4b0  Dispatch8Adjusted: a secondary-base thunk (this - 0x10, then Dispatch8)
//   0xaeb730, 0xaeb790, 0xaeb7e0, 0xaeb760: the one-dword cell operations of ServiceCell

// 0x20dcb6 (__thiscall, no arguments, ret 0): clears the holder, then asks the service for an
// object through slot 5. Returns the holder.
ScopedRef *__fastcall AcquireServiceRefB(ScopedRef *self);

// 0x20dd00 (__thiscall, no arguments, ret 0): as AcquireServiceRefB with the other identifiers.
ScopedRef *__fastcall AcquireServiceRefC(ScopedRef *self);

// 0x20de51 (__cdecl, no arguments): returns the object cached at 0x10cc28c, acquiring it first.
void *GetCachedSingletonB();

// 0x20df6f (__cdecl, no arguments): returns the object cached at 0x10cc290, acquiring it first.
void *GetCachedSingletonC();

// 0x20dde0 (__cdecl, no arguments): returns the value cached at 0x10cc2a8. When it is null, the
// value is slot 23 of GetCachedSingleton()'s object (which is returned unchanged, null included).
void *GetCachedSlot23();

// 0xaeb820 (__cdecl, no arguments): returns the object cached at 0x10f9070: when that is null, the
// object is acquired from the service (slot 5) and slot 8 of it is cached. The reference that
// holds the acquired object is released after the store, and also when slot 8 throws.
void *GetCachedObjectB();

// GetCachedObjectB's cache (owned.h): the image's global in the hooked game, standalone/owned_statics.cpp's otherwise.
TS2_OWNED_STATIC(void *, 10f9070);

// 0xaeb800 (__thiscall, no arguments, ret 0): returns slot 8 of GetCachedObjectB() called with the
// dword at this as its argument. The slot's result is returned unchanged (all of eax).
uint32_t __fastcall Dispatch8(void *self);

// 0xa3c4b0 (__thiscall, no arguments): the thunk for a secondary base at +0x10. Subtracts 0x10 from
// this and continues as Dispatch8.
uint32_t __fastcall Dispatch8Adjusted(void *self);

// A one-dword cell (0xffffffff when empty) whose operations go through the cached object's slots.
struct ServiceCell {
    uint32_t mValue;

    // 0xaeb710 (__thiscall, one stack argument, ret 4; lane VS): the cell made to hold `value`, which is then counted
    // (slot 6 of the cached object called with the stored value). Returns this.
    ServiceCell *ConstructFrom(uint32_t value);

    // 0xaeb6f0 (__thiscall, one stack argument, ret 4; lane VS): the copy constructor: *src's value stored, then counted
    // as ConstructFrom. Returns this.
    ServiceCell *ConstructCopy(const ServiceCell *src);

    // 0xaeb730 (__thiscall, two stack arguments, ret 8). Stores slot 5 of the cached object called
    // with (a, b) into the cell, then calls slot 6 of the cached object with the new value. Returns
    // this.
    ServiceCell *Open(uint32_t a, uint32_t b);

    // 0xaeb790 (__thiscall, one stack argument, ret 4): calls slot 6 with *src, then slot 7 with the
    // old cell value, then stores *src in the cell. Returns this.
    ServiceCell *Assign(const uint32_t *src);

    // 0xaeb7e0 (__thiscall, no arguments): when the cell is not empty, calls slot 7 with its value
    // and empties the cell (0xffffffff).
    void Reset();

    // 0xaeb760 (__thiscall, no arguments, noexcept frame): Reset.
    void Close() noexcept;
};

static_assert(sizeof(ServiceCell) == 4, "ServiceCell layout");
