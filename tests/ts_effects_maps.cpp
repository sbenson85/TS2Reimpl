// cTSEffectsMaps (src/ts/cTSEffectsMaps.h), lane WO: original vs ours.
//
// Mode 0 builds the object with the original Create 0xbd96a0 (seeds alternate with the constructor 0xbd95d0 on a garbage
// block), mode 1 with ours; every slot then runs through the object's own vtables, on both faces. After each step: the result,
// the log of the maps' reference calls, both vtable words, the count, the vector (size, capacity, each element's id) and
// the flag. At the end the destructor, with the frees by size.
//
// The maps are FakeNodes of the effects world (any counted cIGZUnknown). Index -2 and below is left out: the vector's growth
// throws a length error there on both sides, and no caller gets that far.
//
// Covered RVAs: 0xbd96a0 (Create), 0xbd95d0 (constructor), 0xbd9620 (destructor), 0xbd9650 (deleting destructor), 0xbd9640
// (its +4 thunk), 0xbd99a0 (QueryInterface), 0xbd99c7 (its +4 thunk), 0xbd9850 (Init), 0xbd9b00 (Shutdown), 0x751740
// (GetCount), 0xbd9a00 (SetMap), 0xbd9670 (GetMap).
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "shape.h"
#include "ts/cTSEffectsMaps.h"

#include <new>

namespace {

using namespace fxtest;
using Maps = cTSEffectsMaps;

constexpr uint32_t kGameTables[2] = {0xe8e49c, 0xe8e4c4};
const uint32_t kFaces[2] = {0, 4};

uint32_t TableCode(uint32_t word)
{
    return shape::VtCode(word, 2, shape::GameTables{kGameTables}, shape::Ours<Maps>(kFaces).face);
}

void PushMaps(Blob &b, const void *maps)
{
    const auto *w = static_cast<const uint32_t *>(maps);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    b.push_back(w[2]);
    const auto *const *first = reinterpret_cast<const FakeNode *const *>(uintptr_t(w[3]));
    const auto *const *last = reinterpret_cast<const FakeNode *const *>(uintptr_t(w[4]));
    const auto *const *end = reinterpret_cast<const FakeNode *const *>(uintptr_t(w[5]));
    b.push_back(uint32_t(last - first));
    b.push_back(uint32_t(end - first));
    for (const auto *const *p = first; p != last; ++p) b.push_back(IdOf(*p));
    b.push_back(*(reinterpret_cast<const uint8_t *>(maps) + 0x18));
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

} // namespace

DIFF_TEST(EffectsMaps_Slots, 0xbd96a0)
{
    using CreateFn = void *(__cdecl *)();
    using ConstructFn = void *(__thiscall *)(void *);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            EffectsWorld w;
            gEv.clear();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6271u + 5);
            const bool placed = seed % 2 != 0;
            void *block = nullptr;
            if (placed) {
                block = ::operator new(sizeof(Maps));
                for (size_t i = 0; i < sizeof(Maps); ++i) static_cast<uint8_t *>(block)[i] = uint8_t(r());
            }
            void *m;
            if (mode) m = placed ? cTSEffectsMapsAdapters::Construct(block, nullptr) : Maps::Create();
            else m = placed ? ctx.Original<ConstructFn>(0xbd95d0)(block) : ctx.Original<CreateFn>(0xbd96a0)();
            if (placed) b.push_back(uint32_t(m == block));
            auto *face = static_cast<uint8_t *>(m) + 4;
            PushMaps(b, m);
            CallSlot<uint32_t>(m, 1);
            for (uint32_t step = 0; step < 50; ++step) {
                b.push_back(0x0b000000u | step);
                const uint32_t op = r() % 16;
                b.push_back(op);
                switch (op) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4: {
                    const uint32_t index = r() % 8 == 0 ? 0xffffffffu : r() % 3 == 0 ? r() % 20 : r() % 4;
                    FakeNode *map = r() % 4 ? &w.nodes[r() % EffectsWorld::kNodes] : nullptr;
                    b.push_back(index);
                    CallSlot<void>(m, 6, index, static_cast<void *>(map));
                    break;
                }
                case 5:
                case 6:
                case 7: {
                    const int32_t index = int32_t(r() % 24) - 3;
                    b.push_back(uint32_t(index));
                    b.push_back(IdOf(CallSlot<void *>(m, 7, index)));
                    break;
                }
                case 8:
                    b.push_back(CallSlot<uint32_t>(m, 5));
                    break;
                case 9:
                    *(reinterpret_cast<uint8_t *>(m) + 0x18) = uint8_t(r() % 2); // nothing in the game sets it; Init clears it
                    b.push_back(CallSlot<uint8_t>(m, 3));
                    break;
                case 10:
                    if (r() % 4 == 0) b.push_back(CallSlot<uint8_t>(m, 4));
                    break;
                case 11:
                    CallSlot<void>(m, 8, uint32_t(r()), uint32_t(r()), uint32_t(r()), uint32_t(r()));
                    break;
                case 12:
                case 13: { // QueryInterface on either face: what it answers, as an offset, and one reference
                    static const uint32_t kIds[] = {1, 0xac28edfd, 0xac28ee02, 0xe4fda3d4, 2};
                    const uint32_t iid = kIds[r() % 5];
                    void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                    const bool ok = CallSlot<uint8_t>(op == 12 ? m : static_cast<void *>(face), 0, iid, &out) != 0;
                    b.push_back(iid);
                    b.push_back(uint32_t(ok));
                    b.push_back(ok ? uint32_t(static_cast<uint8_t *>(out) - static_cast<uint8_t *>(m)) : uint32_t(uintptr_t(out)));
                    if (ok) b.push_back(CallSlot<uint32_t>(m, 2));
                    break;
                }
                case 14:
                    b.push_back(CallSlot<uint32_t>(m, 1));
                    b.push_back(CallSlot<uint32_t>(m, 1));
                    b.push_back(CallSlot<uint32_t>(face, 1));
                    b.push_back(CallSlot<uint32_t>(face, 4));
                    b.push_back(CallSlot<uint32_t>(m, 2));
                    b.push_back(CallSlot<uint32_t>(face, 2));
                    break;
                case 15:
                    CallSlot<uint32_t>(face, 1);
                    b.push_back(CallSlot<uint32_t>(face, 5));
                    break;
                }
                Drain(b);
                PushMaps(b, m);
            }
            b.push_back(0x0b0000ffu);
            difftest::FreeRecorder rec;
            const uint32_t last = seed % 3;
            static_cast<uint32_t *>(m)[2] = 1;
            if (last == 0) {
                b.push_back(CallSlot<uint32_t>(m, 2));
            } else if (last == 1) {
                b.push_back(CallSlot<uint32_t>(face, 2));
            } else {
                b.push_back(uint32_t(CallSlot<void *>(face, 3, uint32_t(1)) == m));
            }
            rec.Stop();
            Drain(b);
            size_t sizes[8] = {};
            const size_t freed = rec.SortedSizes(sizes, 8);
            b.push_back(uint32_t(freed));
            for (size_t i = 0; i < freed && i < 8; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(uint32_t(rec.Freed(m)));
            for (const FakeNode &n : w.nodes) b.push_back(n.refs);
        }
        Same(ctx, blobs[0], blobs[1], "EffectsMaps_Slots", seed);
    }
}
