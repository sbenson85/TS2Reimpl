#pragma once
#include "game/owned.h"
#include "rz/cRZIStream.h"

#include <cstddef>
#include <cstdint>

// cIStreamStringBehaviourAdaptor (vtable 0xe6c648, 24 slots; 0x14 bytes): a cRZIStream over another cIGZIStream. The
// scene-graph factory's Read (nGZSceneGraph::cSceneGraphFactory::Read, 0xa4d750) wraps a record's cIGZIStream in one
// before it hands it to the binary reader's ReadRoot, so that every typed getter and, above all, GetGZStr are
// cRZIStream's (a packed length, then the characters; no byte swap) and not the inner stream's own. A database
// record's own GetGZStr (cGZPersistDBSerialRecord's GetFieldString) takes a 32-bit header and reads text when its low
// two bytes look printable, which misreads a resource name of 32 or more characters; through the adaptor the stream
// reads what the resource writer wrote.
//
// Its own slots: 0 (QueryInterface: the id 0xc56b8f08 goes to the inner stream, every other one to cRZIStream's), 3
// (Skip), 17 (the raw read) and 18 (GetError) forward to the inner stream's same slot with the same arguments, and
// 21 (the deleting destructor) releases the inner stream. Slots 17 and 18 (0xa4d650, 0xa4d640) are folded with
// cOStreamStringBehaviourAdaptor's slots 14 and 15 (shared_vf_*); everything else is cRZIStream's.
//
// The constructor is inlined into the factory's Read (the cRZIStream base constructor 0x1b4d6d: count 0, flag 1,
// owner null; then the table, the inner stream and its AddRef). RVAs are 1.18.0.187.
class cIStreamStringBehaviourAdaptor : public cRZIStream {
public:
    static constexpr uint32_t kVTable = 0xe6c648;
    static const void *VTable();     // the word Construct stores (the image's table; ours in an image-free program)
    static const void *OwnVTable();  // rzface::cIStreamStringBehaviourAdaptor's
    static constexpr uint32_t kInnerId = 0xc56b8f08; // QueryInterface forwards this id to the inner stream

    // The inlined constructor: cRZIStream's (count 0, no byte swap, no owner), the table, then `inner` held with a
    // reference (its slot 1). Returns this.
    cIStreamStringBehaviourAdaptor *Construct(void *inner);

    bool QueryInterface(uint32_t id, void **out);   // slot 0, 0xa4d6d0
    uint32_t Skip(uint32_t count);                  // slot 3, 0xa4da80: the inner stream's slot 3
    uint32_t GetVoid(void *dst, uint32_t size);     // slot 17, 0xa4d650 (folded): the inner stream's slot 17
    uint32_t GetError();                            // slot 18, 0xa4d640 (folded): the inner stream's slot 18
    // Slot 21, 0xa4d4b0 (ret 4): releases the inner stream (slot 2) when it has one, cRZIStream's Destruct, then
    // (flags & 1) sized operator delete(this, 0x14). Returns this.
    cIStreamStringBehaviourAdaptor *DeletingDestructor(uint32_t flags);

    void *mInner; // +0x10: the cIGZIStream read through
};

TS2_OWNED_STATIC(const OwnedVTable, e6c648);
inline const void *cIStreamStringBehaviourAdaptor::VTable() { return &ts2_rva_e6c648; }

static_assert(sizeof(cIStreamStringBehaviourAdaptor) == 0x14, "cIStreamStringBehaviourAdaptor is 0x14 bytes");
static_assert(offsetof(cIStreamStringBehaviourAdaptor, mInner) == 0x10, "cIStreamStringBehaviourAdaptor: inner at +0x10");
