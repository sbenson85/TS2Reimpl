// Differential test for the counted holders of the grid's sparse matrices (src/rz/cRZUnknownAutoPtr.h; lane FC, the
// standalone shape): three classes, each with three slots (QueryInterface, AddRef, Release) and an out-of-line destructor
// that deletes the held matrix when the holder owns it.
//
// The original side is a 0x10-byte block with the game's vtable (0xe49b9c / 0xe49bac / 0xe49bbc; the game's own code only
// ever builds them inline in the grid handler, so there is no constructor to call), driven through that vtable: the folded
// QueryInterface 0x1b945c, AddRef 0x145260 and Release 0x86de32 / 0x86de56 / 0x86de7a, and, through Release, the destructors
// 0x85d7c8 / 0x85d7e9 / 0x85d80a and the matrix destructors behind them. Ours is a holder built by our constructor and
// driven through our own vtable, slot by slot. Both sides hold a matrix built by ours (the original's holders destroy ours
// already, tests/gz_legacy_handlers.cpp), with a few values in it, so a destructor that skips the matrix, or deletes one it
// must keep, changes the frees. The frees are recorded (FreeRecorder): the block, the matrix, and how many in all.
//
// Registered under 0x86de32; covers the Release slots 0x86de32, 0x86de56 and 0x86de7a, QueryInterface 0x1b945c (as these
// classes' slot 0) and AddRef 0x145260 (slot 1), and the three destructors.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/cRZUnknownAutoPtr.h"
#include "shape.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>

namespace {

using legacygrid::CellMatrix;
using legacygrid::GridCell;
using legacygrid::GridLineData;
using legacygrid::LineMatrix;

constexpr uint32_t kUntouched = 0xface0000u;

void Fill(CellMatrix *m, uint32_t seed)
{
    std::mt19937 rng(seed);
    const uint32_t n = rng() % 5;
    for (uint32_t i = 0; i < n; ++i) {
        GridCell cell;
        cell.mFlags = rng();
        m->Set(int32_t(rng() % 6), int32_t(rng() % 6), &cell, nullptr);
    }
}

void Fill(LineMatrix *m, uint32_t seed)
{
    std::mt19937 rng(seed);
    const uint32_t n = rng() % 5;
    for (uint32_t i = 0; i < n; ++i) {
        const GridLineData data = {int32_t(rng() % 9), int32_t(rng() % 9), int32_t(rng())};
        m->Set(int32_t(rng() % 6), int32_t(rng() % 6), &data, nullptr);
    }
}

struct Observed {
    uint32_t result = 0;
    uint32_t out = 0; // the out pointer: 1 (the holder), 0 (left alone)
    bool freed = false;
    bool matrixFreed = false;
    size_t frees = 0;
    uint32_t words[4] = {}; // survivor: vtable code, matrix (null or not), count, owning flag
};

enum Op { kQuery, kAddRef, kRelease };

template <class Holder, class Matrix> struct Rig {
    uint32_t gameRva;

    uint32_t Code(uint32_t word, const uint32_t oursWord) const
    {
        return word == uint32_t(uintptr_t(GameAddress<void>(gameRva))) || word == oursWord ? 0xd0000000u : word;
    }

    Observed Run(bool original, int op, uint32_t iid, uint32_t count, bool owns, bool hasMatrix, uint32_t seed) const
    {
        Observed o;
        Matrix *matrix = nullptr;
        if (hasMatrix) {
            matrix = new Matrix;
            Fill(matrix, seed);
        }
        auto *block = static_cast<uint8_t *>(::operator new(Holder::kSize));
        Holder *ours = nullptr;
        if (original) {
            std::memset(block, 0, Holder::kSize);
            *reinterpret_cast<const void **>(block) = GameAddress<void>(gameRva);
            *reinterpret_cast<Matrix **>(block + 4) = matrix;
            *reinterpret_cast<uint32_t *>(block + 8) = count;
            block[0xc] = owns;
        } else {
            ours = new (block) Holder(matrix);
            ours->mRefCount = count;
            ours->mOwnsMatrix = owns;
        }
        void *out = reinterpret_cast<void *>(uintptr_t(kUntouched));
        difftest::FreeRecorder rec;
        if (original) {
            void *const *vt = shape::GameVTable(gameRva);
            if (op == kQuery) o.result = reinterpret_cast<bool(__thiscall *)(void *, uint32_t, void **)>(vt[0])(block, iid, &out) & 0xff;
            else if (op == kAddRef) o.result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(vt[1])(block);
            else o.result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(vt[2])(block);
        } else {
            cIGZUnknown *face = ours;
            if (op == kQuery) o.result = face->QueryInterface(iid, &out) ? 1 : 0;
            else if (op == kAddRef) o.result = face->AddRef();
            else o.result = face->Release();
        }
        rec.Stop();
        o.freed = rec.Freed(block);
        o.matrixFreed = matrix && rec.Freed(matrix);
        o.frees = rec.Count();
        o.out = out == block ? 1 : out == reinterpret_cast<void *>(uintptr_t(kUntouched)) ? 0 : 2;
        if (!o.freed) {
            const uint32_t oursWord = *reinterpret_cast<const uint32_t *>(&OursTemplate());
            o.words[0] = Code(*reinterpret_cast<uint32_t *>(block), oursWord);
            o.words[1] = *reinterpret_cast<uint32_t *>(block + 4) != 0;
            o.words[2] = *reinterpret_cast<uint32_t *>(block + 8);
            o.words[3] = block[0xc];
            // Tidy up: the block, and a matrix the holder kept (it did not own it, or the call did not destroy it).
            block[0xc] = 0;
            if (ours) ours->mOwnsMatrix = false;
            if (matrix && !o.matrixFreed) delete matrix;
            ::operator delete(block);
        } else if (matrix && !o.matrixFreed) {
            delete matrix;
        }
        return o;
    }

    static const Holder &OursTemplate()
    {
        static Holder h;
        return h;
    }
};

template <class Holder, class Matrix>
void RunKind(difftest::Context &ctx, const char *name, uint32_t gameRva, uint32_t release, uint32_t seed)
{
    (void)release;
    Rig<Holder, Matrix> rig{gameRva};
    std::mt19937 rng(seed);
    for (int i = 0; i < 3000; ++i) {
        uint32_t count;
        switch (i % 6) {
        case 0: count = 0; break;
        case 1: count = 1; break;
        case 2: count = 2; break;
        case 3: count = 0xffffffffu; break;
        case 4: count = 0x80000000u; break;
        default: count = rng() % 8; break;
        }
        const int op = (i / 6) % 3;
        const uint32_t iids[] = {1, 0, 2, 0x12e, 0x80000001u, uint32_t(rng())};
        const uint32_t iid = iids[rng() % 6];
        const bool owns = rng() % 2 == 1;
        const bool hasMatrix = rng() % 4 != 0;
        const uint32_t matrixSeed = uint32_t(rng());
        const Observed a = rig.Run(true, op, iid, count, owns, hasMatrix, matrixSeed);
        const Observed b = rig.Run(false, op, iid, count, owns, hasMatrix, matrixSeed);
        ++ctx.cases;
        const bool same = a.result == b.result && a.out == b.out && a.freed == b.freed && a.matrixFreed == b.matrixFreed &&
                          a.frees == b.frees && std::memcmp(a.words, b.words, sizeof a.words) == 0;
        if (!same)
            ctx.Fail("%s op %d iid 0x%x count 0x%x owns %d matrix %d: original result %u out %u freed %d matrix freed %d (%zu frees) "
                     "words %x %x %x %x, ours result %u out %u freed %d matrix freed %d (%zu frees) words %x %x %x %x",
                     name, op, iid, count, owns, hasMatrix, a.result, a.out, a.freed, a.matrixFreed, a.frees, a.words[0],
                     a.words[1], a.words[2], a.words[3], b.result, b.out, b.freed, b.matrixFreed, b.frees, b.words[0], b.words[1],
                     b.words[2], b.words[3]);
    }
}

} // namespace

DIFF_TEST(RZUnknownAutoPtr_Release, 0x86de32)
{
    RunKind<cRZUnknownAutoPtrWinGridCellData, CellMatrix>(ctx, "WinGridCellData", 0xe49b9c, 0x86de32, 0x48470003);
    RunKind<cRZUnknownAutoPtrWinGridColData, LineMatrix>(ctx, "WinGridColData", 0xe49bac, 0x86de56, 0x48470004);
    RunKind<cRZUnknownAutoPtrWinGridRowData, LineMatrix>(ctx, "WinGridRowData", 0xe49bbc, 0x86de7a, 0x48470005);
}
