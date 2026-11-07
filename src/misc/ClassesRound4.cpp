// Lane IG: the last virtual slots of nearly complete classes, round 4 (speed-built functions). See ClassesRound4.h.
#include "misc/ClassesRound4.h"

#include <new>

#include "game/game.h"
#include "game/vtable.h"

namespace {
using AddIndexFn = void(__thiscall *)(void *, int32_t);
using AddLayeredIndexFn = void(__thiscall *)(void *, int32_t, int32_t);

// The owner's three index sinks (slots 35, 36, 37).
__forceinline void Add(const nTSSG::cTrianglePolygon *p, int32_t index)
{
    VirtualCall<AddIndexFn, 35>(p->mOwner, index);
}
__forceinline void AddSnow(const nTSSG::cTrianglePolygon *p, int32_t index)
{
    VirtualCall<AddIndexFn, 36>(p->mOwner, index);
}
__forceinline void AddLayered(const nTSSG::cTrianglePolygon *p, int32_t index, int32_t layer)
{
    VirtualCall<AddLayeredIndexFn, 37>(p->mOwner, index, layer);
}
// Corner (dx, dy) of the cell.
__forceinline int32_t Corner(const nTSSG::cTrianglePolygon *p, int32_t dx, int32_t dy)
{
    return (p->mRow + dy) * p->mStride + p->mFirstVertex + p->mColumn + dx;
}
} // namespace

// --- even cells, first triangle: (0,0) (1,1) (0,1) ---

void nTSSG::cBaseEvenTriangleFirst::AddIndices()
{
    if (mFlipped == 0) {
        Add(this, Corner(this, 0, 0));
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 0, 1));
    } else {
        Add(this, Corner(this, 0, 1));
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 0, 0));
    }
}

void nTSSG::cBaseEvenTriangleFirst::AddLayeredIndices()
{
    if (mFlipped != 0) return;
    AddLayered(this, Corner(this, 0, 0), 0);
    AddLayered(this, Corner(this, 1, 1), 0);
    AddLayered(this, Corner(this, 0, 1), 0);
    AddLayered(this, Corner(this, 0, 0), 1);
    AddLayered(this, Corner(this, 1, 1), 1);
    AddLayered(this, Corner(this, 0, 1), 1);
}

void nTSSG::cSnowEvenTriangleFirst::AddSnowIndices()
{
    if (mFlipped != 0) return;
    AddSnow(this, Corner(this, 0, 0));
    AddSnow(this, Corner(this, 1, 1));
    AddSnow(this, Corner(this, 0, 1));
}

// --- even cells, second triangle: (0,0) (1,0) (1,1) ---

void nTSSG::cBaseEvenTriangleSecond::AddIndices()
{
    if (mFlipped == 0) {
        Add(this, Corner(this, 0, 0));
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 1, 1));
    } else {
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 0, 0));
    }
}

void nTSSG::cBaseEvenTriangleSecond::AddLayeredIndices()
{
    if (mFlipped != 0) return;
    AddLayered(this, Corner(this, 0, 0), 0);
    AddLayered(this, Corner(this, 1, 0), 0);
    AddLayered(this, Corner(this, 1, 1), 0);
    AddLayered(this, Corner(this, 0, 0), 1);
    AddLayered(this, Corner(this, 1, 0), 1);
    AddLayered(this, Corner(this, 1, 1), 1);
}

void nTSSG::cSnowEvenTriangleSecond::AddSnowIndices()
{
    if (mFlipped != 0) return;
    AddSnow(this, Corner(this, 0, 0));
    AddSnow(this, Corner(this, 1, 0));
    AddSnow(this, Corner(this, 1, 1));
}

// --- odd cells, first triangle: (0,0) (1,0) (0,1) ---

void nTSSG::cBaseOddTriangleFirst::AddIndices()
{
    if (mFlipped == 0) {
        Add(this, Corner(this, 0, 0));
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 0, 1));
    } else {
        Add(this, Corner(this, 0, 1));
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 0, 0));
    }
}

void nTSSG::cBaseOddTriangleFirst::AddLayeredIndices()
{
    if (mFlipped != 0) return;
    AddLayered(this, Corner(this, 0, 0), 0);
    AddLayered(this, Corner(this, 1, 0), 0);
    AddLayered(this, Corner(this, 0, 1), 0);
    AddLayered(this, Corner(this, 0, 0), 1);
    AddLayered(this, Corner(this, 1, 0), 1);
    AddLayered(this, Corner(this, 0, 1), 1);
}

void nTSSG::cSnowOddTriangleFirst::AddSnowIndices()
{
    if (mFlipped != 0) return;
    AddSnow(this, Corner(this, 0, 0));
    AddSnow(this, Corner(this, 1, 0));
    AddSnow(this, Corner(this, 0, 1));
}

// --- odd cells, second triangle: (1,0) (1,1) (0,1) ---

void nTSSG::cBaseOddTriangleSecond::AddIndices()
{
    if (mFlipped == 0) {
        Add(this, Corner(this, 1, 0));
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 0, 1));
    } else {
        Add(this, Corner(this, 0, 1));
        Add(this, Corner(this, 1, 1));
        Add(this, Corner(this, 1, 0));
    }
}

void nTSSG::cBaseOddTriangleSecond::AddLayeredIndices()
{
    if (mFlipped != 0) return;
    AddLayered(this, Corner(this, 1, 0), 0);
    AddLayered(this, Corner(this, 1, 1), 0);
    AddLayered(this, Corner(this, 0, 1), 0);
    AddLayered(this, Corner(this, 1, 0), 1);
    AddLayered(this, Corner(this, 1, 1), 1);
    AddLayered(this, Corner(this, 0, 1), 1);
}

void nTSSG::cSnowOddTriangleSecond::AddSnowIndices()
{
    if (mFlipped != 0) return;
    AddSnow(this, Corner(this, 1, 0));
    AddSnow(this, Corner(this, 1, 1));
    AddSnow(this, Corner(this, 0, 1));
}

uint32_t __cdecl BitMask(uint32_t bit)
{
    return 1u << bit;
}

cIGZDBPFIndex *cIGZDBPFIndex::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(cIGZDBPFIndex));
    return this;
}

bool cGZBufferFontRenderer::QueryInterface(uint32_t riid, void **out)
{
    if (riid == 1) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this);
        return true;
    }
    if (riid == kIid) {
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this);
        return true;
    }
    return false;
}
