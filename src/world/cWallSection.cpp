#include "world/cWallSection.h"

#include <new>

namespace nTSWorld {

cWallSection::cWallSection()
{
    for (int i = 0; i < 4; ++i) {
        mEdges[i].SetOwner(this);
        mEdges[i].SetIndex(i);
    }
    // The starting shape: edge 0 and edge 2 loop to themselves, edges 1 and 3 point at each other.
    mEdges[0].SetOnext(&mEdges[0]);
    mEdges[1].SetOnext(&mEdges[3]);
    mEdges[2].SetOnext(&mEdges[2]);
    mEdges[3].SetOnext(&mEdges[1]);
}

cWallSection::~cWallSection() = default;

// this + 8 + 24 * index in 32 bits, unchecked, as the original's lea chain (the block's edges are 0x18 apart).
cIWallEdge *cWallSection::EdgeAt(uint32_t index)
{
    return reinterpret_cast<cIWallEdge *>(reinterpret_cast<uintptr_t>(this) + 8u + 24u * index);
}
cIWallEdge *cWallSection::EdgeAtC(uint32_t index) { return cWallSection::EdgeAt(index); }

cWallSection *__fastcall cWallSectionAdapters::Construct(Self *self, void *) { return new (self) Self(); }
void __fastcall cWallSectionAdapters::DestructBody(Self *self, void *) { self->Self::~Self(); }
cWallSection *__fastcall cWallSectionAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
cIWallEdge *__fastcall cWallSectionAdapters::EdgeAt(Self *self, void *, uint32_t index) { return self->Self::EdgeAt(index); }

} // namespace nTSWorld
