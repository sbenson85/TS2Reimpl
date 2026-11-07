// Release at the edge counts for the want classes (lane IL): cTSWantDefinition::Release 0x5b2720 (slot 2, count at +8) and
// cTSWantNode::Release 0x5b36a0 (slot 2, count at +4). A game-built object (the image's operator new 0x824632, memory 0xcd,
// the original constructor: the game vtable) against ours (`new`, or our constructor adapter on the same kind of memory),
// AddRef'd to 0, 1, 2 or 3, then released once through slot 2. Compared: the return value, whether the object's block was
// freed by that call (difftest::FreeRecorder: every HeapFree from the image and from our code), and the count left when it
// was not; then the rest released and the block's free checked again.
#include "difftest.h"
#include "free_scope.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ts/cTSWantDefinition.h"
#include "ts/cTSWantNode.h"

#include <cstring>

namespace wantrelease {

using svcfx::Blob;

template <int Slot> uint32_t Call(void *object) { return VirtualCall<uint32_t(__thiscall *)(void *), Slot>(object); }

struct Kind {
    uint32_t size, ctor, countOffset;
    void *(*ours)(bool adapter, void *memory);
};

void *OurDefinition(bool adapter, void *memory)
{
    if (!adapter) return static_cast<cITSWantDefinition *>(new cTSWantDefinition);
    return static_cast<cITSWantDefinition *>(cTSWantDefinitionAdapters::Construct(memory, nullptr));
}
void *OurNode(bool adapter, void *memory)
{
    if (!adapter) return static_cast<cITSWantNode *>(new cTSWantNode);
    return static_cast<cITSWantNode *>(cTSWantNodeAdapters::Construct(memory, nullptr));
}

// side 0 the original; side 1 ours by `new`; side 2 ours by the adapter on image memory.
Blob Run(difftest::Context &ctx, const Kind &k, int side, uint32_t refs)
{
    Blob d;
    void *object;
    if (side == 1) {
        object = k.ours(false, nullptr);
    } else {
        void *memory = ctx.Original<void *(__cdecl *)(size_t)>(0x824632)(k.size);
        std::memset(memory, 0xcd, k.size);
        object = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(k.ctor)(memory) : k.ours(true, memory);
    }
    for (uint32_t i = 0; i < refs; ++i) d.push_back(Call<1>(object));
    const int32_t count = *reinterpret_cast<int32_t *>(static_cast<uint8_t *>(object) + k.countOffset);
    d.push_back(uint32_t(count));
    difftest::BeginFrees();
    d.push_back(Call<2>(object));
    const bool freed = difftest::FreedSinceBegin(object);
    d.push_back(freed);
    if (!freed) {
        // Still alive: its count, then released down to destruction (a count of 1 or below destroys).
        int32_t left = *reinterpret_cast<int32_t *>(static_cast<uint8_t *>(object) + k.countOffset);
        d.push_back(uint32_t(left));
        for (;; --left) {
            const bool last = left <= 1;
            if (last) difftest::BeginFrees();
            d.push_back(Call<2>(object));
            if (last) {
                d.push_back(difftest::FreedSinceBegin(object));
                break;
            }
        }
    }
    return d;
}

void Check(difftest::Context &ctx, const Kind &k, const char *what)
{
    for (uint32_t refs = 0; refs <= 3; ++refs) {
        const Blob original = Run(ctx, k, 0, refs);
        for (int side = 1; side <= 2; ++side)
            if (!svcfx::Same(ctx, original, Run(ctx, k, side, refs), what, refs * 10 + uint32_t(side))) return;
    }
}

} // namespace wantrelease

using namespace wantrelease;

DIFF_TEST(WantDefinition_Release, 0x5b2720)
{
    svcfx::AllocatorScope allocator;
    Check(ctx, Kind{cTSWantDefinition::kSize, 0x5b10e0, 8, &OurDefinition}, "WantDefinition_Release");
}

DIFF_TEST(WantNode_Release, 0x5b36a0)
{
    svcfx::AllocatorScope allocator;
    Check(ctx, Kind{cTSWantNode::kSize, 0x5b2750, 4, &OurNode}, "WantNode_Release");
}
