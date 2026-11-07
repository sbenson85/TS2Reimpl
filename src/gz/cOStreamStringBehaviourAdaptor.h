#pragma once
#include "game/owned.h"
#include "rz/cRZOStream.h"

#include <cstddef>
#include <cstdint>

// cOStreamStringBehaviourAdaptor (vtable 0xe6c5e4, 24 slots; 0x14 bytes): a cRZOStream over another cIGZOStream, the
// output twin of cIStreamStringBehaviourAdaptor. The scene-graph factory's Write (nGZSceneGraph::cSceneGraphFactory::
// Write, 0xa4da90) wraps a record's cIGZOStream in one before it hands it to the binary writer's WriteRoot, so that the
// typed setters and SetGZStr are cRZOStream's (a packed length, then the characters) and not the record's own.
//
// Its own slots: 3 (Flush, 0xa4d5f0: the inner stream's slot 3), 17 (the raw write, folded 0xa4d650: the inner stream's
// slot 17), 18 (GetError, folded 0xa4d640: the inner stream's slot 18) and 21 (0xa4d510: the deleting destructor, which
// releases the inner stream). Everything else is cRZOStream's. The constructor is inlined into the factory's Write
// (cRZOStream's 0x1b4f60: count 0, native order, no owner; then the table, the inner stream and its AddRef).
// RVAs are 1.18.0.187.
class cOStreamStringBehaviourAdaptor : public cRZOStream {
public:
    static constexpr uint32_t kVTable = 0xe6c5e4;
    static const void *VTable();    // the word Construct stores (the image's table; ours in an image-free program)
    static const void *OwnVTable(); // rzface::cOStreamStringBehaviourAdaptor's

    // The inlined constructor: cRZOStream's, the table, then `inner` held with a reference when there is one (its slot
    // 1). Returns this.
    cOStreamStringBehaviourAdaptor *Construct(void *inner);

    uint32_t Flush();                              // slot 3, 0xa4d5f0: the inner stream's slot 3
    uint32_t SetVoid(const void *src, uint32_t n); // slot 17, 0xa4d650 (folded): the inner stream's slot 17
    uint32_t GetError();                           // slot 18, 0xa4d640 (folded): the inner stream's slot 18
    // Slot 21, 0xa4d510 (ret 4): releases the inner stream (slot 2) when it has one, cRZOStream's Destruct, then
    // (flags & 1) sized operator delete(this, 0x14). Returns this.
    cOStreamStringBehaviourAdaptor *DeletingDestructor(uint32_t flags);

    void *mInner; // +0x10: the cIGZOStream written through
};

TS2_OWNED_STATIC(const OwnedVTable, e6c5e4);
inline const void *cOStreamStringBehaviourAdaptor::VTable() { return &ts2_rva_e6c5e4; }

static_assert(sizeof(cOStreamStringBehaviourAdaptor) == 0x14, "cOStreamStringBehaviourAdaptor is 0x14 bytes");
static_assert(offsetof(cOStreamStringBehaviourAdaptor, mInner) == 0x10, "cOStreamStringBehaviourAdaptor: inner at +0x10");
