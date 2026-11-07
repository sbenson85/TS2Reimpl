#pragma once
// nGZEvent::cAttributeIDIterator (lane VX): the iterator over a counted id vector that every behaviour type's GetEventIds
// (slot 13) and the sound manager's GetRegisteredIds hand out in pairs (begin, end) to the range constructor 0x1a98e0.
// 16 bytes: the vtable (0xd9bd10), its own count (+4, starts at 0), the id vector it holds a reference on (+8, a
// cRZRefCountedT<std::vector<unsigned long>>, any cIGZUnknown), the position (+0xc, a pointer into the vector's ids).
//
// Hook shape: a plain struct whose members are the image's functions, the vtable word the owned static 0xd9bd10 (the
// image's table in hooked builds; ts2_own_vt_d9bd10, our members slot for slot, in an image-free program).
//
// Slots (cIGZUnknown, then cIGZForwardIterator<unsigned long>): 0 QueryInterface 0x1a849d, 1 AddRef 0x148fb0, 2 Release
// 0x1a84c0, 3 Clone 0x1a8475, 4 IsEqual 0x178550, 5 Get 0x1a8492, 6 Next 0x1a8498, 7 the deleting destructor 0x1468b0.
// 0x1a849d, 0x148fb0, 0x1a84c0 and 0x178550 are folded with unrelated classes (shared_vf_*): ours only in our table.
#include "game/owned.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>

TS2_OWNED_STATIC(const OwnedVTable, d9bd10); // nGZEvent::cAttributeIDIterator

namespace nGZEvent {

class cIAttributeIDIterator;

struct cAttributeIDIterator {
    const void *mVTable;  // +0x00
    uint32_t mRefCount;   // +0x04
    cIGZUnknown *mIds;    // +0x08 referenced while held
    const uint32_t *mAt;  // +0x0c

    // 0x1a9942 (ret 8): count 0, our vtable, the ids referenced (when there are any), the position read from *cursor.
    cAttributeIDIterator *Construct(cIGZUnknown *ids, const uint32_t *const *cursor);
    // 0x1a8434 (ret 4): the same from another iterator (its ids, its position).
    cAttributeIDIterator *CopyConstruct(const cAttributeIDIterator &source);
    // 0x1a8462: the vtable reset to ours, the ids released (when there are any); the members otherwise left.
    void Destruct();
    // 0x1468b0 (slot 7, ret 4): Destruct, the 16 bytes freed on flags & 1; returns this.
    void *DeletingDestructor(uint32_t flags);

    // 0x1a849d (slot 0, folded): riid 1 with an out pointer: *out = this, AddRef through the vtable, true; else false.
    bool QueryInterface(uint32_t riid, void **out);
    // 0x148fb0 (slot 1, folded: its entry is cRZFastCompression3::AddRef's): the count incremented and returned.
    uint32_t AddRef();
    // 0x1a84c0 (slot 2, folded): the count decremented; at zero the deleting destructor through the vtable (flags 1)
    // and 0 returned, else the new count.
    uint32_t Release();
    // 0x1a8475 (slot 3): a new iterator at the same place (null when the allocation gives none).
    cAttributeIDIterator *Clone() const;
    // 0x178550 (slot 4, folded): the positions equal (other taken to be an iterator of this layout).
    bool IsEqual(const cAttributeIDIterator *other) const;
    // 0x1a8492 (slot 5): the id at the position.
    uint32_t Get() const;
    // 0x1a8498 (slot 6): the position one id on.
    void Next();
};
static_assert(sizeof(cAttributeIDIterator) == 0x10, "cAttributeIDIterator size");

// What the GetEventIds bodies inline: operator new(0x10), then 0x1a9942 on it (not checked for null, as they do).
inline cIAttributeIDIterator *NewAttributeIDIterator(cIGZUnknown *ids, const uint32_t *cursor)
{
    auto *const it = static_cast<cAttributeIDIterator *>(::operator new(sizeof(cAttributeIDIterator)));
    return reinterpret_cast<cIAttributeIDIterator *>(it->Construct(ids, &cursor));
}

} // namespace nGZEvent
