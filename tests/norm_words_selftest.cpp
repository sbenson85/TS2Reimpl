// Adversarial self-test of the normalised words (tests/norm_words.h) through svcfx (tests/ts_services_fixture.h): data stays
// as it is whatever its value, a pointer becomes its object and offset, and what cannot be told apart fails loudly.
#include "difftest.h"
#include "norm_words.h"
#include "ts_services_fixture.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using svcfx::Blob;
using svcfx::P;

alignas(16) uint8_t gBlockA[0x40];
alignas(16) uint8_t gBlockB[0x40];
alignas(16) uint8_t gBlockC[0x40];
constexpr uint32_t kTag = 0x7a000000, kOtherTag = 0x7b000000;

bool Contains(const std::vector<std::string> &lines, const char *text)
{
    for (const std::string &l : lines)
        if (l.find(text) != std::string::npos) return true;
    return false;
}

// Calls slot 3 (two arguments) of fake 0 the way the code under test would, and hands back the two logged arguments.
std::pair<uint32_t, uint32_t> Call3(uint32_t a, uint32_t b)
{
    svcfx::Obj &fake = svcfx::F(0);
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    svcfx::gEv.clear();
    reinterpret_cast<Fn>(fake.vt[3])(&fake, a, b);
    const Blob &ev = svcfx::gEv;
    return {ev[ev.size() - 2], ev.back()};
}

} // namespace

DIFF_TEST(NormWords_SelfTest, 0)
{
    bool hooked = false;
    for (const difftest::FailureNotes &s : difftest::FailureNoteSources()) hooked |= s.errors == &normwords::Errors;
    if (!hooked) ctx.Fail("the normalised words' errors are not registered with the runner");
    normwords::TakeErrors();
    auto expect = [&](bool ok, const char *what) {
        ++ctx.cases;
        if (!ok) ctx.Fail("%s", what);
    };
    auto quiet = [&](const char *what) {
        const std::vector<std::string> errors = normwords::TakeErrors();
        if (!errors.empty()) ctx.Fail("%s: unexpected error: %s", what, errors[0].c_str());
    };

    // 1. Data equal to a live registered address, or to a fake's address, stays as it is (slot argument, value word).
    svcfx::ResetAll();
    svcfx::AddRange(gBlockA, sizeof(gBlockA), kTag);
    svcfx::F(0).Set(3, "dd");
    {
        const uint32_t inside = P(gBlockA) + 8, fake = P(&svcfx::F(1));
        const auto [a, b] = Call3(inside, fake);
        expect(a == inside && b == fake, "a data argument equal to a registered address or a fake was rewritten");
        svcfx::F(2).Values("dd");
        svcfx::F(2).value[0] = inside;
        svcfx::F(2).value[1] = fake;
        Blob blob;
        svcfx::CaptureFakes(blob);
        expect(blob[2 * 9 + 1] == inside && blob[2 * 9 + 2] == fake, "a data value word was rewritten");
    }
    quiet("data words");

    // 1b. Pointers into the runner's own image (statics no range registers) keep their offset: two different statics
    // log different records, the same static the same one (coordinator mutant: the offset dropped survived without this).
    svcfx::ResetAll();
    svcfx::F(0).Set(3, "pp");
    {
        const auto [a, b] = Call3(P(gBlockC), P(gBlockC) + 8);
        const auto [c, d] = Call3(P(gBlockC), P(gBlockC));
        expect(a != b, "two different statics in the runner image logged the same record");
        expect(c == d && c == a, "the same static in the runner image logged different records");
    }
    quiet("runner image statics");

    // 2. The same object at different addresses on the two sides logs the same record (block, fake, stack).
    uint32_t record[2][3];
    for (int side = 0; side < 2; ++side) {
        svcfx::ResetAll();
        uint8_t *block = side ? gBlockB : gBlockA;
        svcfx::AddRange(block, sizeof(gBlockA), kTag);
        svcfx::F(0).Set(3, "pp");
        const uint32_t local = 0;
        const auto [a, b] = Call3(P(block) + 0xc, P(&svcfx::F(5)));
        record[side][0] = a;
        record[side][1] = b;
        record[side][2] = svcfx::Norm(P(&local));
    }
    expect(record[0][0] == (kTag | 0xc) && record[1][0] == (kTag | 0xc), "one object at two addresses: records differ");
    expect(record[0][1] == 205 && record[1][1] == 205, "a fake's record is not its id");
    expect(record[0][2] == normwords::kStack && record[1][2] == normwords::kStack, "a stack pointer's record");
    quiet("same object");

    // 3. Different objects at the same offset, and one object at different offsets, log different records.
    svcfx::ResetAll();
    svcfx::AddRange(gBlockA, sizeof(gBlockA), kTag);
    svcfx::AddRange(gBlockC, sizeof(gBlockC), kOtherTag);
    expect(svcfx::Norm(P(gBlockA) + 4) != svcfx::Norm(P(gBlockC) + 4), "two objects at one offset compare equal");
    expect(svcfx::Norm(P(gBlockA) + 4) != svcfx::Norm(P(gBlockA) + 8), "one object at two offsets compares equal");
    expect(svcfx::Norm(P(&svcfx::F(1))) != svcfx::Norm(P(&svcfx::F(2))), "two fakes compare equal");
    expect(svcfx::Norm(P(&svcfx::F(1)) + 4) != svcfx::Norm(P(&svcfx::F(1))), "a fake and a word inside it compare equal");
    quiet("different objects");

    // 4. Null differs from an object at offset 0 (a block, a fake, the original image's base, the runner's image).
    expect(svcfx::Norm(0) == 0, "null is not 0");
    expect(svcfx::Norm(P(gBlockA)) != 0, "a block at offset 0 compares equal to null");
    expect(svcfx::Norm(P(&svcfx::F(0))) != 0, "fake 0 compares equal to null");
    expect(svcfx::Norm(uint32_t(kGameBase)) != 0, "the original image's base compares equal to null");
    expect(svcfx::Norm(P(GetModuleHandleW(nullptr))) != 0, "the runner image's base compares equal to null");
    quiet("null");

    // 5. An unregistered pointer is an error naming the address and the call site or slot; never collapsed. (A block from
    // before the side started: the heap blocks the side allocates are registered, section 9.)
    std::unique_ptr<uint32_t[]> before = std::make_unique<uint32_t[]>(16);
    svcfx::ResetAll();
    svcfx::AddRange(gBlockA, sizeof(gBlockA), kTag);
    {
        const std::unique_ptr<uint32_t[]> &heap = before;
        char want[16];
        std::snprintf(want, sizeof(want), "%08x", P(heap.get()) + 4);
        const uint32_t got = svcfx::Norm(P(heap.get()) + 4);
        std::vector<std::string> errors = normwords::TakeErrors();
        expect(got == normwords::kUnregistered, "an unregistered pointer logged as something else");
        expect(errors.size() == 1 && Contains(errors, want) && Contains(errors, "norm_words_selftest.cpp:"),
               "no error with the address and the call site for an unregistered pointer (Norm)");
        svcfx::F(0).Set(3, "pd");
        Call3(P(heap.get()) + 4, 9);
        errors = normwords::TakeErrors();
        expect(errors.size() == 1 && Contains(errors, want) && Contains(errors, "fake #200 slot 3") &&
                   Contains(errors, "argument 0"),
               "no error with the address and the slot for an unregistered pointer argument");
        // A small integer passed as a pointer is not a pointer to anything.
        svcfx::Norm(5);
        expect(normwords::TakeErrors().size() == 1, "a small integer passed through Norm was accepted");
    }

    // 6. An undeclared word: zero is accepted (null and 0 log alike), anything else is an error saying what to declare.
    svcfx::F(0).Set(3, 2);
    {
        const auto [a, b] = Call3(0, 0);
        expect(a == 0 && b == 0, "undeclared zeros");
        quiet("undeclared zeros");
        const auto [u, v] = Call3(7, P(gBlockA));
        const std::vector<std::string> errors = normwords::TakeErrors();
        expect(u == normwords::kUndeclared && v == normwords::kUndeclared, "undeclared words logged as something else");
        expect(errors.size() == 2 && Contains(errors, "argument 0 = 00000007 has no declared kind") &&
                   Contains(errors, "Set(3, \"...\")"),
               "an undeclared argument did not ask for its kind");
        svcfx::F(0).Data(3, 1);
        svcfx::F(0).Ptr(3, 2);
        const auto [c, d] = Call3(7, P(gBlockA) + 2);
        expect(c == 7 && d == (kTag | 2), "Data / Ptr declarations");
        quiet("Data / Ptr");
        svcfx::F(3).value[4] = 0x1234;
        Blob blob;
        svcfx::CaptureFakes(blob);
        const std::vector<std::string> valueErrors = normwords::TakeErrors();
        expect(Contains(valueErrors, "fake #203 value[4] (CaptureFakes at norm_words_selftest.cpp:") &&
                   Contains(valueErrors, "= 00001234 has no declared kind") && blob[3 * 9 + 5] == normwords::kUndeclared,
               "an undeclared value word did not ask for its kind");
    }

    // 7. Tags that would let two objects log the same word are refused.
    svcfx::ResetAll();
    svcfx::AddRange(gBlockA, 0x40, 0x7c000020); // offsets reach into the tag's bits
    expect(Contains(normwords::TakeErrors(), "the offsets reach into the tag's own bits"), "a narrow tag was accepted");
    svcfx::AddRange(gBlockA, 0x40, 0x7d000000);
    svcfx::AddRange(gBlockB, 0x40, 0x7d000000); // the same tag (one object, as on the other side): allowed
    quiet("same tag");
    svcfx::ResetAll();
    svcfx::AddRange(gBlockA, 0x100000, 0x7e000000);
    svcfx::AddRange(gBlockB, 0x40, 0x7e010000); // inside the first tag's words
    expect(Contains(normwords::TakeErrors(), "overlap tag 7e000000"), "overlapping tags were accepted");
    svcfx::AddRange(gBlockC, 0x40, 0x100);
    expect(Contains(normwords::TakeErrors(), "below 0x10000"), "a tag aliasing the fakes' ids was accepted");
    svcfx::AddRange(gBlockC, 0x40, uint32_t(kGameBase) + 0x1000);
    expect(Contains(normwords::TakeErrors(), "original image"), "a tag aliasing the original image was accepted");

    // 8. The errors reach the runner: Errors() hands them over as failures and leaves none behind.
    svcfx::Norm(3);
    std::vector<std::string> out;
    normwords::Errors(out);
    expect(out.size() == 1 && Contains(out, "normalised words:"), "Errors() did not hand the error over");
    quiet("after Errors()");

    // 9. Heap blocks a side allocates are registered by the allocator, numbered as the records first reach them: the same
    // block on two sides (other addresses) logs the same record; another block, or another offset, a different one; a
    // freed block is no longer registered; one past the end belongs to the block.
    uint32_t heapRecord[2][4];
    for (int side = 0; side < 2; ++side) {
        std::unique_ptr<uint8_t[]> spacer;
        if (side) spacer = std::make_unique<uint8_t[]>(0x30); // moves the side's blocks (before the side: not counted)
        svcfx::ResetAll();
        auto first = std::make_unique<uint8_t[]>(0x20);
        auto second = std::make_unique<uint8_t[]>(0x20);
        heapRecord[side][0] = svcfx::Norm(P(first.get()) + 4);
        heapRecord[side][1] = svcfx::Norm(P(second.get()) + 4);
        heapRecord[side][2] = svcfx::Norm(P(second.get()) + 8);
        heapRecord[side][3] = svcfx::Norm(P(second.get()) + 0x20);
        quiet("heap blocks");
        const uint32_t gone = P(first.get());
        first.reset();
        expect(svcfx::Norm(gone) == normwords::kUnregistered && normwords::TakeErrors().size() == 1,
               "a freed heap block is still registered");
    }
    expect(heapRecord[0][0] == heapRecord[1][0] && heapRecord[0][1] == heapRecord[1][1] &&
               heapRecord[0][3] == heapRecord[1][3],
           "one heap block at two addresses: records differ");
    expect(heapRecord[0][0] != heapRecord[0][1], "two heap blocks at one offset compare equal");
    expect(heapRecord[0][1] != heapRecord[0][2], "one heap block at two offsets compares equal");
    expect(heapRecord[0][3] == (heapRecord[0][1] - 4 + 0x20), "one past a heap block's end is not its end");
    // Blocks are numbered in the order the records first reach them: blocks allocated in another order, or another block
    // no record reaches (a temporary, a cache), leave the records unchanged; reaching another block than the first time,
    // or a new block where the other side reaches a known one, changes them.
    uint32_t order[5][3];
    for (int side = 0; side < 5; ++side) {
        svcfx::ResetAll();
        std::unique_ptr<uint8_t[]> a, b, unseen;
        if (side == 1) unseen = std::make_unique<uint8_t[]>(0x40);
        if (side == 2) b = std::make_unique<uint8_t[]>(0x20);
        a = std::make_unique<uint8_t[]>(0x20);
        if (side != 2) b = std::make_unique<uint8_t[]>(0x20);
        order[side][0] = svcfx::Norm(P(b.get()));
        order[side][1] = svcfx::Norm(P(a.get()) + 4);
        order[side][2] = svcfx::Norm(P(side == 3 ? a.get() : side == 4 ? (unseen = std::make_unique<uint8_t[]>(8)).get() : b.get()));
    }
    expect(!std::memcmp(order[0], order[1], sizeof(order[0])), "a block no record reaches changed the records");
    expect(!std::memcmp(order[0], order[2], sizeof(order[0])), "blocks allocated in another order changed the records");
    expect(order[0][0] != order[0][1], "two blocks reached in turn got one number");
    expect(order[0][2] != order[3][2], "reaching another known block than the other side compares equal");
    expect(order[0][2] != order[4][2], "reaching a new block where the other side reaches a known one compares equal");
    quiet("heap numbering");
    // A block allocated under NotCounted (the test's bookkeeping) is not the code's: not registered, not numbered.
    {
        std::unique_ptr<uint8_t[]> bookkeeping;
        {
            difftest::NotCounted pause;
            bookkeeping = std::make_unique<uint8_t[]>(0x10);
        }
        svcfx::Norm(P(bookkeeping.get()));
        expect(normwords::TakeErrors().size() == 1, "a bookkeeping block was registered");
    }
    // Adjacent blocks (a pool allocator's, here carved from gBlockC): the next block's start is that block at offset 0,
    // whichever block the allocator registered first; a block allocated again at a live block's address replaces it.
    for (int order = 0; order < 2; ++order) {
        svcfx::ResetAll();
        uint8_t *const lower = gBlockC, *const upper = gBlockC + 0x10;
        if (order) normwords::HeapAllocated(upper, 0x10), normwords::HeapAllocated(lower, 0x10);
        else normwords::HeapAllocated(lower, 0x10), normwords::HeapAllocated(upper, 0x10);
        const uint32_t start = svcfx::Norm(P(upper)), inside = svcfx::Norm(P(upper) + 4);
        expect(start + 4 == inside, "the start of a block adjacent to another logged as the other's end");
        expect(svcfx::Norm(P(upper) + 0x10) == inside + 0xc, "one past the last block's end is not its end");
        const uint32_t old = svcfx::Norm(P(lower) + 4);
        const size_t live = normwords::gHeapLive;
        normwords::HeapAllocated(lower, 0x10); // the same address again (the old block freed unseen): a new block
        expect(normwords::gHeapLive == live, "a block allocated at a live block's address was added beside it");
        expect(svcfx::Norm(P(lower) + 4) != old, "a new block at an old block's address logged as the old block");
        quiet("adjacent heap blocks");
    }
    // The registry's index (an open-addressing table by start address): twenty thousand blocks, freed in a scrambled
    // order with others allocated in between, each live one found and each freed one gone (collisions and the deletions'
    // shifts included). The addresses are words of a static buffer, registered by hand (nothing is allocated).
    {
        static uint32_t space[24000 * 2];
        auto at = [](unsigned i) { return static_cast<void *>(&space[i * 2]); };
        svcfx::ResetAll();
        for (unsigned i = 0; i < 20000; ++i) normwords::HeapAllocated(at(i), 8);
        bool live[24000] = {};
        for (unsigned i = 0; i < 20000; ++i) live[i] = true;
        for (unsigned k = 0; k < 20000; ++k) {
            const unsigned i = (k * 7919u) % 20000u; // every block once, scrambled
            if (k % 3 == 0) continue;
            normwords::HeapFreed(at(i));
            live[i] = false;
            if (k % 5 == 0) normwords::HeapAllocated(at(20000 + k % 4000), 8), live[20000 + k % 4000] = true;
        }
        size_t count = 0, wrong = 0;
        for (unsigned i = 0; i < 24000; ++i) {
            count += live[i];
            const normwords::HeapBlock *b = normwords::HeapAt(uintptr_t(at(i)));
            wrong += live[i] ? !b || b->lo != uintptr_t(at(i)) : b != nullptr;
        }
        expect(wrong == 0 && normwords::gHeapLive == count, "the heap registry's index lost or kept a block");
        svcfx::ResetAll();
        expect(!normwords::HeapAt(uintptr_t(at(0))) && !normwords::HeapAt(uintptr_t(at(20001))),
               "a block outlived the side's start (StartHeap)");
    }
    // Consecutive addresses hash apart, so the churn above hardly collides: here forty blocks whose start addresses all
    // hash into the table's last twelve entries make one cluster that wraps past its end, and freeing them one at a time
    // (scrambled) must keep every other one findable. The addresses are only looked up, never read; nothing here
    // allocates (an allocation would be a registered block too).
    {
        svcfx::ResetAll();
        constexpr size_t kCluster = 40;
        uintptr_t lo[kCluster];
        size_t n = 0;
        for (uintptr_t a = 0x10000000; n < kCluster; a += 8)
            if (normwords::HeapHash(a) >= normwords::kHeapIndexSize - 12) lo[n++] = a;
        for (uintptr_t a : lo) normwords::HeapAllocated(reinterpret_cast<void *>(a), 8);
        bool live[kCluster];
        for (bool &l : live) l = true;
        size_t wrong = 0;
        for (size_t k = 0; k < kCluster; ++k) {
            const size_t i = (k * 17) % kCluster; // every block once, scrambled
            normwords::HeapFreed(reinterpret_cast<void *>(lo[i]));
            live[i] = false;
            for (size_t j = 0; j < kCluster; ++j) wrong += (normwords::HeapAt(lo[j]) != nullptr) != live[j];
        }
        expect(wrong == 0 && normwords::gHeapLive == 0, "the heap registry's index lost a colliding block when another went");
        svcfx::ResetAll();
    }

    // 10. A union the test made (one of its fakes or data): tagged by identity, so data equal to a fake's record differs
    // from the fake, and data equal to a fake's address is that fake on both sides alike.
    svcfx::ResetAll();
    {
        Blob fake, data, same;
        svcfx::FakeOrData(fake, P(&svcfx::F(0)));
        svcfx::FakeOrData(data, 200);
        svcfx::FakeOrData(same, P(&svcfx::F(0)));
        expect(fake != data, "a fake and data equal to its record compare equal");
        expect(fake == same, "one fake logged twice differs");
        Blob big;
        svcfx::FakeOrData(big, 0x8badf00d);
        expect(big.size() == 2 && big[1] == 0x8badf00d, "data in a union was rewritten");
        // An out word: left as set (the sentinel) differs from null and from any object.
        expect(svcfx::NormOut(0x5e5e5e5e) != svcfx::NormOut(0) &&
                   svcfx::NormOut(0x5e5e5e5e) != svcfx::NormOut(P(&svcfx::F(0))),
               "an untouched out word compares equal to null or an object");
        quiet("unions");
    }

    svcfx::ResetAll();
    before.reset();
}
