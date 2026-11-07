#pragma once
// nTSWorld::cWallSection (lane VR, campaign world-lot WL-0d): four wall edges in a row plus an id, the unit cWallGraph
// (WL-2) allocates. RVAs 1.18.0.187. Layout (0x68 bytes): vtable +0, id +4 (-1 until set), the four cWallEdge records
// +8 (0x18 each). The constructor (0x81fc70) builds the edges, gives each its owner (the section) and index (0-3), and
// links their Onext pointers in the quad-edge starting shape: edge 0 to itself, edge 1 to edge 3, edge 2 to itself, edge
// 3 to edge 1.
//
// Standalone shape (docs/standalone-shape.md): a real class on cIWallSection, our vtable, the four edges as members
// (ours; the game's edge constructor 0x820be0 and body 0x820c10 are ours too). Slots 0 (SetId, 0x1489e0) and 3 (Id,
// 0x147a30) are shared one-word bodies, ours only in our vtable; slots 1 and 2 are the one body 0x81fd80.
#include "world/cWallEdge.h"

#include <cstddef>
#include <cstdint>

namespace nTSWorld {

class cIWallSection {
public:
    virtual void SetId(int32_t id) = 0;               // 0 0x1489e0 (shared): +4
    virtual cIWallEdge *EdgeAt(uint32_t index) = 0;   // 1 0x81fd80: this + 8 + 24 * index, unchecked
    virtual cIWallEdge *EdgeAtC(uint32_t index) = 0;  // 2 the same body
    virtual int32_t Id() = 0;                         // 3 0x147a30 (shared): +4
    virtual ~cIWallSection() = default;               // 4 0x81fd60 deleting destructor (0x68)
};

class cWallSection : public cIWallSection {
public:
    static constexpr uint32_t kVTable = 0xe05300;

    cWallSection(); // 0x81fc70
    cWallSection(const cWallSection &) = delete;
    cWallSection &operator=(const cWallSection &) = delete;
    ~cWallSection() override; // slot 4 0x81fd60 (deleting); the body 0x81fd40 destroys the edges, last first

    void SetId(int32_t id) override { mId = id; }
    cIWallEdge *EdgeAt(uint32_t index) override;
    cIWallEdge *EdgeAtC(uint32_t index) override;
    int32_t Id() override { return mId; }

    int32_t mId = -1;     // +4
    cWallEdge mEdges[4];  // +8
};
static_assert(sizeof(cWallSection) == 0x68 && offsetof(cWallSection, mId) == 4 && offsetof(cWallSection, mEdges) == 8);

// The hooks' entry points. Construct places ours over the block; the destructors run ours.
struct cWallSectionAdapters {
    using Self = cWallSection;
    static Self *__fastcall Construct(Self *self, void *);
    static void __fastcall DestructBody(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static cIWallEdge *__fastcall EdgeAt(Self *self, void *, uint32_t index);
};

} // namespace nTSWorld
