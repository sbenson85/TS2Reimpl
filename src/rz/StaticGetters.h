#pragma once
#include "game/owned.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// The director singleton chain behind 0x1ae18d (RVAs are 1.18.0.187).
//
//   0x1ae18d  GetDirectorFrameworkObject: GetCOMDirector(), then vtable slot 12 of the director
//   0x14491b  GetCOMDirector: the function-local static at 0x10c9390, constructed once under the
//             thread-safe statics protocol (guard 0x10c93f4, TLS index 0x10df814)
//   0x144897  cGZCOMDirector::ConstructDirector: base constructor, vtables, framework pointer
//   0x1adffa  cRZCOMDllDirector::Construct: base constructor (its string at +0x10, the id set at +0x44)
//   0x1aa5b3  HashNodeList::Construct: the list sentinel of that member
//
// Not reimplemented: 0x1457d0 (4 bytes, folded into 85 classes, so it cannot be hooked). It is
// the framework-pointer getter, slot 12 of cGZCOMDirector (returns the field at +0x30). The
// constructor reads that field inline, and 0x1ae18d reaches it through the vtable.

// The list part of the id set (object offsets +4 and +8): the sentinel node, self-linked, and the
// element count (0 for a fresh set).
struct HashNodeList {
    void *mHead;
    uint32_t mSize;

    // 0x1aa5b3 (__thiscall, one stack argument that is ignored, ret 4). Returns this.
    HashNodeList *Construct(uint32_t unused);

    // 0x1ae301 (__thiscall, no arguments): frees the chain (FreeListNodes), then the sentinel. The
    // head pointer is left as it was (dangling), as the original leaves it.
    void Destruct();
};

// 0x1adf40 (__cdecl, two arguments; the first is not used): frees the nodes after the sentinel
// `head`. The sentinel's link to its predecessor is cleared first, so a sentinel-only list frees
// nothing.
void FreeListNodes(void *list, void *head);

static_assert(offsetof(HashNodeList, mSize) == 4, "HashNodeList layout");

// The id set (0x20 bytes at object offset 0x44): the layout of an unordered_set<uint32_t> with 8
// buckets, max load factor 1.0.
struct HashIdSet {
    float mMaxLoad;      // +0
    HashNodeList mList;  // +4: sentinel head and size
    void **mFirst;       // +0xc: bucket vector (16 entries, all the sentinel)
    void **mLast;        // +0x10
    void **mEnd;         // +0x14
    uint32_t mMask;      // +0x18: 7
    uint32_t mMaxIndex;  // +0x1c: 8
};

static_assert(sizeof(HashIdSet) == 0x20, "HashIdSet layout");

// cRZCOMDllDirector: the base of the director. The vtables are 0xda265c (primary) and 0xda26a4
// (the base subobject at +4).
struct cRZCOMDllDirector {
    void *mVTable;     // +0
    void *mVTableAt4;  // +4
    uint32_t mField08; // +8: 0 after construction
    uint32_t mField0C; // +0xc: 0 after construction
    cRZString mString; // +0x10 (constructed by cRZString::Construct, 0x144f5b)
    void *mFramework;  // +0x30: cached framework object (0 after the base constructor)
    uint32_t mField34; // +0x34: 0 after construction
    void **mVec38First; // +0x38..+0x44: a vector of 4-byte entries (empty after construction)
    void **mVec38Last;
    void **mVec38End;
    HashIdSet mIds;    // +0x44

    // 0x1adffa (__thiscall, no arguments). Returns this.
    cRZCOMDllDirector *Construct();

    // 0x1ae048 (__thiscall, no arguments): the director's destructor body. Restores the base
    // vtables, then destroys the members in order: the id set's buckets, its list (0x1ae301), the
    // vector at +0x38, and the string (the vptr at +0x10 is set, then the string is freed).
    void Destruct();
};

static_assert(offsetof(cRZCOMDllDirector, mString) == 0x10, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mFramework) == 0x30, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mVec38First) == 0x38, "cRZCOMDllDirector layout");
static_assert(offsetof(cRZCOMDllDirector, mIds) == 0x44, "cRZCOMDllDirector layout");
static_assert(sizeof(cRZCOMDllDirector) == 0x64, "cRZCOMDllDirector layout");

// cGZCOMDirector: the static object at 0x10c9390, the 0x64 bytes just before the guard.
struct cGZCOMDirector : cRZCOMDllDirector {
    // The director itself is rz::cGZCOMDirector (rz/RZCOMDllDirectorShape.h, constructor 0x144897); this struct only
    // names the static's type for the hook-shaped callers of GetCOMDirector.
};

static_assert(sizeof(cGZCOMDirector) == 0x64, "cGZCOMDirector layout");

// The static and its init guard as owned statics (game/owned.h): the image's objects in the hooked game, owned_statics.cpp's
// otherwise (the storage is the 0x64 bytes of rz::cGZCOMDirector, built in place by GetCOMDirector).
struct alignas(4) COMDirectorStorage {
    uint8_t bytes[0x64];
};
TS2_OWNED_STATIC(COMDirectorStorage, 10c9390);
TS2_OWNED_STATIC(int, 10c93f4);

// 0x14491b (__cdecl, no arguments): returns the director at 0x10c9390, constructing it on first use
// under the thread-safe statics protocol.
cGZCOMDirector *GetCOMDirector();

// 0x1ae18d (__cdecl, no arguments): returns GetCOMDirector()'s vtable slot 12 (the framework
// pointer, through the vtable as the original's tail jump does).
void *GetDirectorFrameworkObject();

// The exit handler of the static: destroys the director at 0x10c9390 (the original registers
// 0xd8e6e0, which loads the address and jumps to the destructor body 0x1ae048).
void DestroyCOMDirector();

// How GetCOMDirector registers DestroyCOMDirector. atexit by default; the tests replace it to record
// the registration instead of running it at exit.
extern int (*gRegisterExitHandler)(void (*)());
