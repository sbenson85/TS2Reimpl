// Differential test for cPreloadedRecord's deleting destructor (0x1f74b0): this class's vtables, the data block returned to
// the game's allocator, the base destructor (stream destructors, the child release), then the object itself when bit 0 of
// the flags is set. The other slots of the class (QueryInterface, GetFieldVoid, SeekAbsolute, SeekRelative) are verified by
// byte match. The game's allocator is built for the run when the image has none (as tests/ts_persist_speculative_loader.cpp).
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "ts/cPreloadedRecord.h"

#include <windows.h>

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using DestructorFn = void *(__thiscall *)(void *, uint32_t);

int gChildReleases = 0;
uint32_t __fastcall ChildRelease(void *, void *)
{
    ++gChildReleases;
    return 0;
}
void *gChildVTable[8];
struct Child {
    void *vtable;
};

struct Result {
    void *returned;
    std::vector<uint8_t> bytes;
    int childReleases;
    bool objectFreed;
    bool blockReused;
};

} // namespace

DIFF_TEST(cPreloadedRecord_DeletingDestructor, 0x1f74b0)
{
    auto orig = ctx.Original<DestructorFn>(0x1f74b0);
    auto *allocator = GameAddress<GeneralAllocator>(0x10cb098);
    bool built = false;
    {
        const auto *raw = reinterpret_cast<const uint8_t *>(allocator);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && raw[i] == 0;
        if (zero) {
            allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
            built = true;
        }
    }
    gChildVTable[2] = reinterpret_cast<void *>(&ChildRelease);
    Child child{gChildVTable};
    std::mt19937 rng(0x1f74);

    for (int trial = 0; trial < 400; ++trial) {
        const uint32_t flags = (trial % 4 == 0) ? 0 : (trial % 4 == 1) ? 1 : (trial % 4 == 2) ? 0x100 : 0xff;
        const bool withData = (trial / 4) % 2 == 0;
        const bool withChild = (trial / 8) % 2 == 0;
        cGZPersistResourceKey key{uint32_t(rng()), uint32_t(rng()), uint32_t(rng()), uint32_t(rng())};
        auto Run = [&](bool ours) {
            // The object on the heap (so a free is observable), built by the class's own constructor.
            void *memory = ::operator new(cPreloadedRecord::kSize);
            std::memset(memory, 0xC3, cPreloadedRecord::kSize);
            auto *rec = static_cast<cPreloadedRecord *>(memory);
            rec->Construct(key);
            void *block = nullptr;
            if (withData) {
                block = RZMemory::Malloc(64);
                rec->mData = static_cast<uint8_t *>(block);
                rec->mSize = 64;
            }
            if (withChild) rec->mChild = &child;
            gChildReleases = 0;
            Result r{};
            std::vector<uint8_t> before(static_cast<uint8_t *>(memory), static_cast<uint8_t *>(memory) + cPreloadedRecord::kSize);
            {
                difftest::FreeRecorder freed;
                if (ours) r.returned = rec->DeletingDestructor(flags);
                else r.returned = orig(rec, flags);
                freed.Stop();
                r.objectFreed = freed.Freed(memory);
            }
            r.childReleases = gChildReleases;
            if (!r.objectFreed) {
                r.bytes.assign(static_cast<uint8_t *>(memory), static_cast<uint8_t *>(memory) + cPreloadedRecord::kSize);
                std::memset(r.bytes.data() + 0x48, 0, 4); // mData: the block's address differs between the runs
                ::operator delete(memory, cPreloadedRecord::kSize);
            }
            if (withData) { // a freed block of this size is the first one handed out again
                void *again = RZMemory::Malloc(64);
                r.blockReused = again == block;
                RZMemory::Free(again);
            }
            r.returned = r.returned == memory ? nullptr : r.returned; // compare "returns this", not the address
            return r;
        };
        Result a = Run(false);
        Result b = Run(true);
        ++ctx.cases;
        if (a.returned != b.returned) ctx.Fail("flags %x: returned value differs", flags);
        if (a.bytes != b.bytes) ctx.Fail("flags %x: object bytes after differ", flags);
        if (a.childReleases != b.childReleases) ctx.Fail("flags %x: child released %d / %d times", flags, a.childReleases, b.childReleases);
        if (a.objectFreed != b.objectFreed) ctx.Fail("flags %x: object freed %d / %d", flags, a.objectFreed, b.objectFreed);
        if (a.blockReused != b.blockReused) ctx.Fail("flags %x: data block reuse %d / %d", flags, a.blockReused, b.blockReused);
        if (withData && !b.blockReused) ctx.Fail("flags %x: the data block was not returned to the allocator", flags);
    }
    if (built) {
        difftest_alloc::ShutdownAndRelease(allocator);
    }
}
