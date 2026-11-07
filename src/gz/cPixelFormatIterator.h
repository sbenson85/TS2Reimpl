#pragma once
// nGZGraphic4::cPixelFormatIterator (RTTI 0x1096678; vftable 0xd9d084, 15 slots; 0x14 bytes from the fixed pool 0xfec088):
// the iterator the pixel formats manager and the device enumeration hand out (docs/subsystems/rendering.md, R2). It walks
// a sorted set of pixel formats (cLessPxf: slot 10 of cIPixelFormat, strict false) that it shares, counted, with its
// clones; a class-id filter keeps formats of one class only.
//
// The set is cRZRefCountedT<std::set<const cIPixelFormat *, cLessPxf>> (vftable 0xd9d070; 0x10 bytes: vtable, the set's
// head and size, the count). Its nodes are std::set's (0x14 bytes: links, colour, nil flag, the value at +0x10); the
// original's tree code is this toolchain's std::set (the same MSVC STL), so ours inserts, erases and walks alike and
// calls the comparator in the same order.
//
// Standalone shape (docs/standalone-shape.md): our class, our vtable. The game makes iterators in three places:
//   * the COM factory 0x15ea00 (class 0xbb782354, registered by address in COMDirectorData; an empty set, no filter):
//     hooked, CreateEmpty;
//   * the constructor 0x15d040 (thiscall on a pool block: formats, count, filter), called only from the manager's
//     CreateIterator 0x15e860, which is hooked (CreateRange), so the constructor is in effect through its caller;
//   * the device enumeration's slots, ours (gz/cDeviceEnumDX.h).
// So the game's vtable 0xd9d084 is never stored and every slot is in effect through its callers.
#include <cstddef>
#include <cstdint>
#include <set>

#include "gz/cDeviceEnum.h"
#include "gz/cIGZUnknown.h"

namespace nGZGraphic4 {

class cPixelFormatsManagerDX;

// The set's order: a before b when a->Less(b, false).
struct cLessPxf {
    bool operator()(const cIPixelFormat *a, const cIPixelFormat *b) const
    {
        return const_cast<cIPixelFormat *>(a)->Less(b, false);
    }
};

// cRZRefCountedT<T>: T with a count, as an engine object (slots 0-3: QueryInterface, AddRef, Release, the deleting
// destructor). Built with count 0. Layout: vtable, T at +4, the count after it.
template <class T> class cRZRefCountedT : public cIGZUnknown {
public:
    // slot 0 (shared 0x148350): only cIGZUnknown (1); the out pointer is not checked.
    bool QueryInterface(uint32_t riid, void **ppvObj) override
    {
        if (riid != 1) return false;
        *ppvObj = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; } // slot 1
    // slot 2: above 1, counts down; otherwise deletes (through slot 3) and answers 0.
    uint32_t Release() override
    {
        if (mRefCount > 1) return --mRefCount;
        delete this;
        return 0;
    }
    virtual ~cRZRefCountedT() = default; // slot 3

    T mValue;               // +4
    uint32_t mRefCount = 0; // after it
};

using cPixelFormatSet = std::set<const cIPixelFormat *, cLessPxf>;
using cCountedPixelFormatSet = cRZRefCountedT<cPixelFormatSet>; // vftable 0xd9d070: 0x148350, 0x15e520, 0x15fca0, 0x15e030
static_assert(sizeof(cPixelFormatSet) == 8, "std::set: head and size");
static_assert(sizeof(cCountedPixelFormatSet) == 0x10, "cRZRefCountedT<set<const cIPixelFormat *>>: 0x10 bytes");

class cPixelFormatIterator : public cIPixelFormatIterator {
public:
    static constexpr uint32_t kSize = 0x14;
    static constexpr uint32_t kPool = 0xfec088;     // cRZFastFixedAllocator2<0x14> instance the original uses
    static constexpr uint32_t kClassId = 0xbb782354; // the COM class (COMDirectorData)
    static constexpr uint32_t kIid = 0x3b6739f5;     // cIPixelFormatIterator

    // 0x15d040's work: the formats of `formats[0..count)` that pass `filter` (0: all), in a new set; the cursor at the
    // first. Count 0.
    cPixelFormatIterator(const cIPixelFormat *const *formats, int32_t count, uint32_t filter);

    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    // 0x15ea00 (cdecl, no arguments; the COM factory): an empty iterator, no filter, count 0.
    static cPixelFormatIterator *CreateEmpty();
    // 0x15e860 (cPixelFormatsManagerDX slot 3, thiscall, ret 8): a range over the manager's 56 static formats (the
    // current pixel format store's table) that pass `filter`: {a clone of the iterator, the iterator, a clone set to
    // the end}, each counted once.
    static cPixelFormatRange *CreateRange(cPixelFormatsManagerDX *manager, cPixelFormatRange *out, uint32_t filter);

    bool QueryInterface(uint32_t riid, void **ppvObj) override;        // slot 0 (0x15fc40): 1 and kIid; null out: false
    uint32_t AddRef() override;                                         // slot 1 (shared 0x148fb0)
    uint32_t Release() override;                                        // slot 2 (shared 0x151920): slot 14 at 0
    cIPixelFormatIterator *Clone() override;                            // slot 3 (shared 0x15e580: jumps to slot 8)
    bool IsEqual(const cIPixelFormatIterator *other) override;          // slot 4 (shared 0x15e840): the cursors
    cIPixelFormat *Get() override;                                      // slot 5 (0x15e810): the cursor node's value
    void Next() override;                                               // slot 6 (0x15f380)
    void Prev() override;                                               // slot 7 (0x15f910)
    cIPixelFormatIterator *CloneFormatIterator() override;              // slot 8 (0x15e590)
    void SetToBegin() override;                                         // slot 9 (0x15e530)
    void SetToEnd() override;                                           // slot 10 (0x15e830)
    void SetFilter(uint32_t classId) override;                          // slot 11 (0x15fcc0)
    void Insert(const cIPixelFormat *format) override;                  // slot 12 (0x15e3f0)
    uint32_t GetFilter() override;                                      // slot 13 (shared 0x46390)
    ~cPixelFormatIterator() override;                                   // slot 14 (0x15e290)

private:
    explicit cPixelFormatIterator(const cPixelFormatIterator &other); // slot 8's copy (the set counted again)

    uint32_t mRefCount;                    // +0x04
    uint32_t mFilter;                      // +0x08: a class id (cIPixelFormat slot 5), 0 for none
    cCountedPixelFormatSet *mFormats;      // +0x0c: counted
    cPixelFormatSet::const_iterator mCursor; // +0x10: a node of the set (its head at the end)
};
static_assert(sizeof(cPixelFormatSet::const_iterator) == 4, "set iterator: one node pointer");

// The hooked entry points (src/hook/tables/cPixelFormatIterator.cpp): CreateEmpty at 0x15ea00 and this at 0x15e860.
struct cPixelFormatIteratorAdapters {
    static cPixelFormatRange *__fastcall CreateRange(cPixelFormatsManagerDX *manager, void *, cPixelFormatRange *out,
                                                     uint32_t filter); // 0x15e860
};

} // namespace nGZGraphic4
