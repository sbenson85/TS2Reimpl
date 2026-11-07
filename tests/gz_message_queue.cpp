// Differential tests for cGZMessageQueue (src/gz/cGZMessageQueue.size.cpp, standalone shape): the original side builds
// its queue with the original constructor 0xc74c32 (game vtable 0xea03d4), ours through the constructor hook's adapter
// (our vtable); both run the same random script of slot calls through their own vtables on impfx's event fakes (the
// queue's arrival signal: CreateEventA, PulseEvent, WaitForSingleObject, CloseHandle). After each call the result, the
// out message, the queue's messages in order, the list's size word, the lock's count and the reference count are noted
// in one log, which also holds the event calls. The Wait* slots meet an empty queue through a WaitForSingleObject
// handler that pushes the next message of a per-side supply (through slot 3) and answers WAIT_OBJECT_0.
#include "difftest.h"
#include "free_recorder.h"
#include "impfx.h"

#include "gz/cGZMessageQueue.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0xc74c32, kDtor = 0xc74cea, kDeleting = 0xc74e42;
// Slot RVAs (the game's vtable 0xea03d4), for the record: tests reach them through each side's vtable.
constexpr uint32_t kQueryInterface = 0xc75806, kRelease = 0xc75864, kPushBack = 0xc75792, kPushFront = 0xc757cb,
                   kRemoveFirstWithId = 0xc7595d, kRemoveFirst = 0xc7523c, kUnsupported = 0xc759a9,
                   kRemoveAllWithId = 0xc75909, kRemoveAll = 0xc758a2, kClear = 0xc7587b, kFrontId = 0xc75767,
                   kBackId = 0xc75710, kContainsId = 0xc756a8, kFront = 0xc7573c, kBack = 0xc756e4, kContains = 0xc75663,
                   kPopFrontId = 0xc753fd, kPopBackId = 0xc75319, kPopFront = 0xc75376, kPopBack = 0xc75290,
                   kWaitPopFrontId = 0xc75430, kWaitPopBackId = 0xc7534d, kWaitPopFront = 0xc753c8,
                   kWaitPopBack = 0xc752e4, kSize = 0xc675c3;
constexpr uint32_t kSlotRvas[] = {kQueryInterface, kRelease, kPushBack, kPushFront, kRemoveFirstWithId, kRemoveFirst,
                                  kUnsupported, kRemoveAllWithId, kRemoveAll, kClear, kFrontId, kBackId, kContainsId,
                                  kFront, kBack, kContains, kPopFrontId, kPopBackId, kPopFront, kPopBack, kWaitPopFrontId,
                                  kWaitPopBackId, kWaitPopFront, kWaitPopBack, kSize, kDeleting};

using F0 = uint32_t(__thiscall *)(void *);
using F1 = uint32_t(__thiscall *)(void *, uint32_t);
using F2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

uint32_t Slot0(void *q, unsigned slot) { return reinterpret_cast<F0>((*static_cast<void ***>(q))[slot])(q); }
uint32_t Slot1(void *q, unsigned slot, uint32_t a) { return reinterpret_cast<F1>((*static_cast<void ***>(q))[slot])(q, a); }
uint32_t Slot2(void *q, unsigned slot, uint32_t a, uint32_t b)
{
    return reinterpret_cast<F2>((*static_cast<void ***>(q))[slot])(q, a, b);
}

// Slots by argument: none, a message id, a message pointer, an out message.
enum class Arg : uint8_t { None, Id, Message, Out };
struct SlotInfo {
    unsigned slot;
    Arg arg;
    bool boolResult; // al only
};
constexpr SlotInfo kSlots[] = {
    {3, Arg::Message, false},  {4, Arg::Message, false},  {5, Arg::Id, true},       {6, Arg::Message, true},
    {7, Arg::Id, true},        {8, Arg::Id, true},        {9, Arg::Id, false},      {10, Arg::Message, false},
    {11, Arg::None, false},    {12, Arg::None, false},    {13, Arg::None, false},   {14, Arg::Id, true},
    {15, Arg::None, false},    {16, Arg::None, false},    {17, Arg::Message, true}, {18, Arg::None, false},
    {19, Arg::None, false},    {20, Arg::Id, true},       {21, Arg::Out, true},     {22, Arg::Out, true},
    {23, Arg::Message, true},  {24, Arg::None, false},    {25, Arg::None, false},   {26, Arg::Out, true},
    {27, Arg::Out, true},      {28, Arg::None, false},    {1, Arg::None, false},    {2, Arg::None, false},
    {0, Arg::Id, true},
};

cGZMessage RandomMessage(std::mt19937 &rng)
{
    // Few distinct values, so equal messages and ids are common.
    return {rng() % 4, rng() % 2, reinterpret_cast<void *>(uintptr_t(rng() % 2 ? 7 : 0)), rng() % 2 ? 9u : 0u};
}

// The list at +4 read raw: (head, size); nodes {next, prev, message}.
struct RawNode {
    RawNode *next, *prev;
    cGZMessage message;
};
RawNode *Head(void *q) { return *reinterpret_cast<RawNode **>(static_cast<uint8_t *>(q) + 4); }
uint32_t SizeWord(void *q) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(q) + 8); }

void NoteQueue(void *q)
{
    std::vector<uint32_t> words;
    RawNode *head = Head(q);
    unsigned n = 0;
    for (RawNode *node = head->next; node != head && n < 200; node = node->next, ++n) {
        if (node->next->prev != node) words.push_back(0xbadbad);
        words.push_back(node->message.mId);
        words.push_back(node->message.mWord4);
        words.push_back(uint32_t(uintptr_t(node->message.mObject)));
        words.push_back(node->message.mWordC);
    }
    drawfx::Note("queue size", {SizeWord(q), n});
    for (size_t i = 0; i < words.size(); i += 4)
        drawfx::Note("message", {words[i], words[i + 1], words[i + 2], words[i + 3]});
    const auto *lock = reinterpret_cast<const cRZCriticalSection *>(static_cast<uint8_t *>(q) + 0xc);
    drawfx::Note("lock count, refs", {uint32_t(lock->mBlock->lockCount), *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(q) + 0x1c)});
}

// Where a returned message pointer points: 0 null, 1 the front node's message, 2 the back's (when it is not the front),
// 3 elsewhere.
uint32_t PointerCode(void *q, uint32_t p)
{
    if (!p) return 0;
    RawNode *head = Head(q);
    if (p == uint32_t(uintptr_t(&head->next->message))) return 1;
    if (p == uint32_t(uintptr_t(&head->prev->message))) return 2;
    return 3;
}

// The Wait* slots' supply: each wait pushes the next message (nonzero id) through slot 3.
void *gQueue = nullptr;
uint32_t gSupplied = 0;
uint32_t WaitPushes(drawfx::Fake &, const uint32_t *)
{
    ++gSupplied;
    cGZMessage next{0x100 + gSupplied, gSupplied, nullptr, 0x55};
    Slot1(gQueue, 3, uint32_t(uintptr_t(&next)));
    return WAIT_OBJECT_0;
}

void *BuildOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cGZMessageQueue::kSize);
    ctx.Original<F0>(kCtor)(memory);
    return memory;
}

void *BuildOurs()
{
    void *memory = ::operator new(cGZMessageQueue::kSize);
    return cGZMessageQueueAdapters::Construct(memory, nullptr);
}

// One script on one side's queue.
void RunScript(void *q, uint32_t seed)
{
    std::mt19937 rng(seed);
    gQueue = q;
    gSupplied = 0;
    uint32_t refs = 0; // AddRefs made (Release is called only while the count stays above 1)
    for (int step = 0; step < 60; ++step) {
        const bool push = rng() % 3 == 0;
        const SlotInfo &info = push ? kSlots[rng() % 2] : kSlots[rng() % std::size(kSlots)];
        if (info.slot == 2 && refs < 2) continue; // Release only while it can't delete
        const cGZMessage message = RandomMessage(rng);
        const uint32_t id = std::array<uint32_t, 5>{0, 1, 2, 3, 0x101}[rng() % 5];
        uint32_t out[4] = {0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5};
        uint32_t result = 0;
        if (info.slot == 0) { // QueryInterface(riid, out)
            const uint32_t riid = std::array<uint32_t, 3>{1, cIGZMessageQueue::kIid, 0x298b2f5f}[rng() % 3];
            result = Slot2(q, 0, riid, uint32_t(uintptr_t(out)));
            drawfx::Note("qi out", out[0] == uint32_t(uintptr_t(q)) ? 1u : out[0]);
            if (result & 0xff) ++refs;
        } else {
            switch (info.arg) {
            case Arg::None: result = Slot0(q, info.slot); break;
            case Arg::Id: result = Slot1(q, info.slot, id); break;
            case Arg::Message: result = Slot1(q, info.slot, uint32_t(uintptr_t(&message))); break;
            case Arg::Out: result = Slot1(q, info.slot, uint32_t(uintptr_t(out))); break;
            }
            if (info.slot == 1) ++refs;
            if (info.slot == 2) --refs;
        }
        if (info.slot == 15 || info.slot == 16) {
            drawfx::Note("pointer", PointerCode(q, result));
            if (result) {
                const auto *m = reinterpret_cast<const uint32_t *>(uintptr_t(result));
                drawfx::Note("pointed", {m[0], m[1], m[2], m[3]});
            }
        } else if (info.slot == 3 || info.slot == 4) {
            drawfx::Note("push", info.slot); // void: eax is whatever was left
        } else {
            drawfx::Note("slot, result", {info.slot, info.boolResult ? (result & 0xff) : result});
        }
        if (info.arg == Arg::Out) drawfx::Note("out", {out[0], out[1], out[2], out[3]});
        NoteQueue(q);
    }
}

// Destroys a queue through its deleting destructor (slot 29) with flags 1, or 0 and a free of our own, recording the
// blocks freed.
void Destroy(void *q, bool viaFlags, std::vector<size_t> &freed)
{
    size_t sizes[256];
    difftest::FreeRecorder rec;
    if (viaFlags) {
        Slot1(q, 29, 1);
    } else {
        Slot1(q, 29, 0);
        rec.Stop();
        ::operator delete(q);
    }
    rec.Stop();
    const size_t n = rec.SortedSizes(sizes, 256);
    freed.assign(sizes, sizes + n);
}

} // namespace

DIFF_TEST(GZMessageQueue_Script, 0xc75792)
{
    (void)kSlotRvas;
    impfx::Imports imp(ctx);
    imp.Sync();
    imp.Get("WaitForSingleObject").On(&WaitPushes);
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        const bool viaFlags = seed % 2 == 0;
        void *qo = nullptr, *qu = nullptr;
        std::vector<size_t> freedO, freedU;
        auto a = drawfx::Side([&] { imp.BeginSide(); qo = BuildOriginal(ctx); },
                              [&] {
                                  RunScript(qo, seed);
                                  Destroy(qo, viaFlags, freedO);
                              });
        auto b = drawfx::Side([&] { imp.BeginSide(); qu = BuildOurs(); },
                              [&] {
                                  RunScript(qu, seed);
                                  Destroy(qu, viaFlags, freedU);
                              });
        if (!drawfx::Same(ctx, a.first, b.first, "script", seed, a.second.empty() ? b.second : a.second)) return;
        // The original frees the node blocks (0x18), the lock's block and the signal's state block (and the queue with
        // flags 1); ours the same blocks.
        if (freedO != freedU) {
            ctx.Fail("seed %u: freed %zu blocks vs %zu", seed, freedO.size(), freedU.size());
            return;
        }
    }
}

// Release at a count of 1 or less deletes the queue through slot 29 (the original's vf29, ours the compiler's deleting
// destructor): the blocks freed and the event closed are compared.
DIFF_TEST(GZMessageQueue_ReleaseDeletes, 0xc75864)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t messages = 0; messages < 4; ++messages) {
        for (uint32_t addRefs = 0; addRefs < 3; ++addRefs) {
            std::vector<size_t> freed[2];
            auto run = [&](void *q, std::vector<size_t> &out) {
                for (uint32_t i = 0; i < messages; ++i) {
                    cGZMessage m{i, i, nullptr, i};
                    Slot1(q, 3, uint32_t(uintptr_t(&m)));
                }
                for (uint32_t i = 0; i < addRefs; ++i) drawfx::Note("addref", Slot0(q, 1));
                size_t sizes[64];
                difftest::FreeRecorder rec;
                // The count starts at 0: the k-th release of k AddRefs (the first, with none) deletes.
                const uint32_t releases = addRefs ? addRefs : 1;
                for (uint32_t i = 0; i < releases; ++i) drawfx::Note("release", Slot0(q, 2));
                rec.Stop();
                out.assign(sizes, sizes + rec.SortedSizes(sizes, 64));
            };
            void *qo = nullptr, *qu = nullptr;
            auto a = drawfx::Side([&] { imp.BeginSide(); qo = BuildOriginal(ctx); }, [&] { run(qo, freed[0]); });
            auto b = drawfx::Side([&] { imp.BeginSide(); qu = BuildOurs(); }, [&] { run(qu, freed[1]); });
            const uint32_t seed = messages * 10 + addRefs;
            if (!drawfx::Same(ctx, a.first, b.first, "release", seed, a.second.empty() ? b.second : a.second)) return;
            if (freed[0] != freed[1]) {
                ctx.Fail("case %u: freed %zu blocks vs %zu", seed, freed[0].size(), freed[1].size());
                return;
            }
        }
    }
}

// The constructor and destructor adapters: each side builds a queue (the original constructor, our Construct adapter),
// the fresh state is compared (the list empty and self-linked, the lock's count, the signal's fake event and its
// manual-reset flag 0, the reference count; the vtable word is the side's own); then each side's destructor (0xc74cea,
// our Destruct adapter) runs on a queue holding messages, also on a queue the original constructor built (the Adapter
// route: what the server's member becomes when its destructor call reaches our hook).
DIFF_TEST(GZMessageQueue_ConstructDestruct, 0xc74c32)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t route = 0; route < 2; ++route) {
        for (uint32_t messages = 0; messages < 5; ++messages) {
            std::vector<size_t> freed[2];
            auto note = [](void *q) {
                RawNode *head = Head(q);
                drawfx::Note("fresh", {head->next == head ? 1u : 0u, head->prev == head ? 1u : 0u, SizeWord(q)});
                const auto *signal = reinterpret_cast<const cRZThreadSignal *>(static_cast<uint8_t *>(q) + 0x14);
                drawfx::Note("signal", {uint32_t(uintptr_t(signal->mState->mEvent)), signal->mState->mManualReset});
                NoteQueue(q);
            };
            auto fill = [&](void *q) {
                for (uint32_t i = 0; i < messages; ++i) {
                    cGZMessage m{i + 1, 0, nullptr, 0};
                    Slot1(q, 4, uint32_t(uintptr_t(&m)));
                }
            };
            auto destroy = [&](void *q, bool ours, std::vector<size_t> &out) {
                size_t sizes[64];
                difftest::FreeRecorder rec;
                if (ours)
                    cGZMessageQueueAdapters::Destruct(static_cast<cGZMessageQueue *>(q), nullptr);
                else
                    ctx.Original<F0>(kDtor)(q);
                rec.Stop();
                out.assign(sizes, sizes + rec.SortedSizes(sizes, 64));
                ::operator delete(q);
            };
            void *qo = nullptr, *qu = nullptr;
            auto a = drawfx::Side([&] { imp.BeginSide(); qo = BuildOriginal(ctx); },
                                  [&] {
                                      note(qo);
                                      fill(qo);
                                      destroy(qo, false, freed[0]);
                                  });
            auto b = drawfx::Side([&] { imp.BeginSide(); qu = route == 0 ? BuildOurs() : BuildOriginal(ctx); },
                                  [&] {
                                      note(qu);
                                      fill(qu);
                                      destroy(qu, true, freed[1]);
                                  });
            const uint32_t seed = route * 10 + messages;
            if (!drawfx::Same(ctx, a.first, b.first, "construct/destruct", seed, a.second.empty() ? b.second : a.second))
                return;
            if (freed[0] != freed[1]) {
                ctx.Fail("case %u: freed %zu blocks vs %zu", seed, freed[0].size(), freed[1].size());
                return;
            }
        }
    }
}
