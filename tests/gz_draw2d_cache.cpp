// Differential test for nGZGraphic4::cDraw2DRecordRef (src/gz/cDraw2DBufferHardwareCache.h, lane OL): the counted handle
// of an image record that the hardware cache keeps, whose four members the game has at 0x18bf60 (Set), 0x18bfc0 (Assign),
// 0x18c090 (Release) and 0x18bf90 (Destruct). The Draw2D they count on (GetDraw2D 0x18c000) is a patched stand-in that
// answers with a drawfx fake of the cIDraw2D face: every AddRefRecord / ReleaseRecord it receives is logged with its
// argument. Random sequences of the four members run on two handle cells, once through the original and once through
// ours; the log (the calls on the Draw2D, each handle's value after every step, the returned pointers) must be identical.
// The class itself (the cache's slots) is checked by GZDraw2D_Slots.
#include "code_patch.h"
#include "difftest.h"
#include "drawfx.h"

#include "gz/GraphicServices.h"
#include "gz/cDraw2DBufferHardwareCache.h"

#include <cstdint>
#include <random>

namespace {

using nGZGraphic4::cDraw2DRecordRef;
using SetFn = cDraw2DRecordRef *(__thiscall *)(void *, int32_t);
using AssignFn = cDraw2DRecordRef *(__thiscall *)(void *, const void *);
using VoidFn = void(__thiscall *)(void *);

drawfx::Fake gDraw2D;

nGZGraphic4::cIDraw2D *__cdecl StandDraw2D() { return reinterpret_cast<nGZGraphic4::cIDraw2D *>(&gDraw2D); }

// Handles hold -1 (none) or a record index; the indices the draw system would hand out are small.
int32_t RandomIndex(std::mt19937 &rng) { return rng() % 4 == 0 ? -1 : int32_t(rng() % 6); }

} // namespace

DIFF_TEST(GZDraw2DRecordRef_Members, 0x18c090)
{
    const SetFn setOriginal = ctx.Original<SetFn>(0x18bf60);
    const AssignFn assignOriginal = ctx.Original<AssignFn>(0x18bfc0);
    const VoidFn releaseOriginal = ctx.Original<VoidFn>(0x18c090);
    const VoidFn destructOriginal = ctx.Original<VoidFn>(0x18bf90);
    shape::StandIn draw2d{0x18c000, &nGZGraphic4::GetDraw2DCached, &StandDraw2D};
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        auto run = [&](bool ours) {
            return drawfx::Side([&] { gDraw2D.Reset<fx_slots::cIDraw2D>(1); }, [&] {
                std::mt19937 rng(seed);
                // Two handles in raw cells, as members of a cache are; they start as the cache's constructor leaves them.
                int32_t cell[2] = {-1, -1};
                for (int step = 0; step < 40; ++step) {
                    const unsigned which = rng() % 2;
                    void *const self = &cell[which];
                    const unsigned op = rng() % 5;
                    const int32_t index = RandomIndex(rng);
                    cDraw2DRecordRef *const handle = reinterpret_cast<cDraw2DRecordRef *>(self);
                    cDraw2DRecordRef *const other = reinterpret_cast<cDraw2DRecordRef *>(&cell[1 - which]);
                    cDraw2DRecordRef *returned = nullptr;
                    switch (op) {
                    case 0:
                    case 1: returned = ours ? handle->Set(index) : setOriginal(self, index); break;
                    case 2: returned = ours ? handle->Assign(other) : assignOriginal(self, other); break;
                    case 3:
                        if (ours) handle->Release(); else releaseOriginal(self);
                        break;
                    default:
                        if (ours) handle->Destruct(); else destructOriginal(self);
                        break;
                    }
                    drawfx::Note("step", {op, which, uint32_t(index), returned == handle, uint32_t(cell[0]), uint32_t(cell[1])});
                }
            });
        };
        const auto a = run(false);
        const auto b = run(true);
        if (!drawfx::Same(ctx, a.first, b.first, "record ref", seed, a.second)) return;
    }
}
