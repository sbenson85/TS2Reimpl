// Differential test of the composition Impl's slot-51 builder and its null iterator (lane OM, scenegraph N0 part 3
// step 4; src/sg/cNullArrayAttributeEntryIterator.h): the game's builder 0xa37d80 (its iterators carry the game's vtable
// 0xe68db0: Clone 0xa37630, Get 0xa37720, the deleting destructor 0xa37080 and the folded slots) against ours.
//
//   SG_EmptyAttributeEntryRange  per seed: the range built (the result pointer, each iterator's count, which of them
//       are the same object), then 6-40 random operations on the iterators through their slots (Clone, IsEqual, Get,
//       At, Next / Previous / Advance, QueryInterface with iid 1 / other / a null out, AddRef, Release while more
//       than one reference is held), each result and the touched iterator's count noted, then every reference
//       released (the release results and the 8-byte blocks freed, counted).
#include "sgfx.h"

#include "sg/cNullArrayAttributeEntryIterator.h"

#include <random>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::P;

using BuildFn = cArrayAttributeEntryRange *(__thiscall *)(void *, cArrayAttributeEntryRange *, uint32_t);

uint32_t Count(const cIArrayAttributeEntryIterator *it) { return reinterpret_cast<const uint32_t *>(it)[1]; }

void NoteEntry(const char *label, const cArrayAttributeEntryName &e) { drawfx::Note(label, {e.mWord0, e.mWord1}); }

} // namespace

DIFF_TEST(SG_EmptyAttributeEntryRange, 0xa37d80)
{
    const BuildFn original = ctx.Original<BuildFn>(0xa37d80);
    sgfx::CheckSides(ctx, "EmptyAttributeEntryRange", 64, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         cArrayAttributeEntryRange range{};
                         void *const impl = reinterpret_cast<void *>(uintptr_t(0x5a5a0000u | (rng() & 0xfff0)));
                         const uint32_t a = rng();
                         cArrayAttributeEntryRange *const result =
                             isOriginal ? original(impl, &range, a)
                                        : MakeEmptyAttributeEntryRange(impl, nullptr, &range, a);
                         drawfx::Note("range", {uint32_t(result == &range), Count(range.mCurrent), Count(range.mBegin),
                                                Count(range.mEnd), uint32_t(range.mCurrent == range.mBegin),
                                                uint32_t(range.mBegin == range.mEnd),
                                                uint32_t(range.mCurrent == range.mEnd)});
                         // One entry per reference the test holds.
                         std::vector<cIArrayAttributeEntryIterator *> held{range.mCurrent, range.mBegin, range.mEnd};
                         const unsigned ops = 6 + rng() % 35;
                         for (unsigned k = 0; k < ops; ++k) {
                             const unsigned i = rng() % held.size();
                             cIArrayAttributeEntryIterator *const it = held[i];
                             const unsigned op = rng() % 10;
                             switch (op) {
                             case 0: {
                                 cIArrayAttributeEntryIterator *const clone = it->Clone();
                                 drawfx::Note("clone", {uint32_t(clone != nullptr), Count(clone), uint32_t(clone != it)});
                                 drawfx::Note("addref", clone->AddRef());
                                 held.push_back(clone);
                                 break;
                             }
                             case 1:
                                 drawfx::Note("equal", uint32_t(it->IsEqual(held[rng() % held.size()])));
                                 break;
                             case 2: NoteEntry("get", it->Get()); break;
                             case 3: NoteEntry("at", it->At(int32_t(rng() % 7) - 3)); break;
                             case 4: it->Next(); break;
                             case 5: it->Previous(); break;
                             case 6: it->Advance(int32_t(rng() % 9) - 4); break;
                             case 7: {
                                 const uint32_t kinds[] = {1, 2, rng()};
                                 const uint32_t riid = kinds[rng() % 3];
                                 void *out = reinterpret_cast<void *>(uintptr_t(0x77));
                                 const bool nullOut = rng() % 4 == 0;
                                 const bool ok = it->QueryInterface(riid, nullOut ? nullptr : &out);
                                 drawfx::Note("query", {riid, uint32_t(nullOut), uint32_t(ok), uint32_t(out == it),
                                                        uint32_t(out == reinterpret_cast<void *>(uintptr_t(0x77)))});
                                 if (ok) held.push_back(it);
                                 break;
                             }
                             case 8:
                                 drawfx::Note("addref", it->AddRef());
                                 held.push_back(it);
                                 break;
                             default:
                                 if (Count(it) < 2) break;
                                 drawfx::Note("release", it->Release());
                                 held.erase(held.begin() + i);
                                 break;
                             }
                             drawfx::Note("count", {uint32_t(i), Count(it)});
                         }
                         difftest::FreeRecorder rec;
                         for (cIArrayAttributeEntryIterator *it : held) drawfx::Note("final release", it->Release());
                         rec.Stop();
                         size_t sizes[256];
                         const size_t n = rec.SortedSizes(sizes, 256);
                         uint32_t blocks = 0;
                         for (size_t j = 0; j < n && j < 256; ++j) blocks += sizes[j] == 8;
                         drawfx::Note("freed", {uint32_t(n), blocks});
                     });
}
