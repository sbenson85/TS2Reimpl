#include "world/cWallVertex.h"

#include "world/cWallEdge.h"

#include <cstring>
#include <new>

namespace nTSWorld {

cWallVertex::cWallVertex() = default;

cWallVertex::cWallVertex(const float *pos)
{
    std::memcpy(mPos, pos, sizeof(mPos)); // dword copies, so a NaN keeps its bits
}

uint32_t cWallVertex::SetPosition(const void *position)
{
    // The original's order: read src+0..7, write this+8..15, then read src+8 (an overlapping src can see the write of the
    // first part). The 8 bytes are staged in a local so the copy never overlaps itself.
    const auto *from = static_cast<const uint8_t *>(position);
    auto *to = reinterpret_cast<uint8_t *>(this);
    uint64_t head;
    std::memcpy(&head, from, sizeof(head));
    std::memcpy(to + 8, &head, sizeof(head));
    uint32_t value;
    std::memcpy(&value, from + 8, sizeof(value));
    std::memcpy(to + 0x10, &value, sizeof(value));
    return value;
}

// The first edge is the one with the least angle: a new edge takes over when there is none, or when the first's angle is
// greater than the new edge's (a NaN keeps the first). The new edge's angle is read first.
void cWallVertex::AddEdge(cIWallEdge *edge)
{
    if (mFirst) {
        const float added = edge->Angle();
        const float first = mFirst->Angle();
        if (!(first > added)) { // the original's x87 compare treats a NaN as "less or equal": the first stays
            ++mCount;
            return;
        }
    }
    mFirst = edge;
    ++mCount;
}

// An edge whose Onext is itself was the only one; otherwise the first moves on when it is the one removed.
void cWallVertex::RemoveEdge(cIWallEdge *edge)
{
    cIWallEdge *next = edge->OnextC();
    if (next == edge) {
        --mCount;
        mFirst = nullptr;
        return;
    }
    if (mFirst == edge) mFirst = next;
    --mCount;
}

// The edge that follows the angle: walk round from the first and answer, for the first edge whose angle is greater, that
// edge's RotOnextRot (Oprev); round the whole circle, the first edge's.
cIWallEdge *cWallVertex::EdgeForAngle(float angle)
{
    const int32_t count = EdgeCount();
    cIWallEdge *edge = mFirst;
    if (count == 1) return edge;
    do {
        const float edgeAngle = edge->Angle();
        if (angle < edgeAngle) return edge->RotOnextRotC();
        edge = edge->OnextC();
    } while (edge != mFirst);
    return edge->RotOnextRotC();
}

cWallVertex *__fastcall cWallVertexAdapters::ConstructAt(Self *self, void *, const float *pos) { return new (self) Self(pos); }
cWallVertex *__fastcall cWallVertexAdapters::Construct(Self *self, void *) { return new (self) Self(); }
void __fastcall cWallVertexAdapters::DestructBody(Self *self, void *) { self->Self::~Self(); }
cWallVertex *__fastcall cWallVertexAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
uint32_t __fastcall cWallVertexAdapters::SetPosition(Self *self, void *, const void *position) { return self->Self::SetPosition(position); }
void __fastcall cWallVertexAdapters::AddEdge(Self *self, void *, cIWallEdge *edge) { self->Self::AddEdge(edge); }
void __fastcall cWallVertexAdapters::RemoveEdge(Self *self, void *, cIWallEdge *edge) { self->Self::RemoveEdge(edge); }
cIWallEdge *__fastcall cWallVertexAdapters::EdgeForAngle(Self *self, void *, float angle) { return self->Self::EdgeForAngle(angle); }

} // namespace nTSWorld
