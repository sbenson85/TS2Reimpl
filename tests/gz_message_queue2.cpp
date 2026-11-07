// Differential tests for cGZMessageQueue2 (src/gz/cGZMessageQueue2.size.cpp, standalone shape): the original side builds
// its queue with the original constructor 0xc66b0d (game vtable 0xe9e588), ours through the constructor hook's adapter;
// both run the same random script of slot calls through their own vtables. The messages are drawfx fakes of cIGZMessage2
// (a pool of six, two with equal contents): GetType (slot 4) answers the fake's type, IsEqual (slot 6) compares two
// fakes' type and payload; every call on them (AddRef, Release, GetType, IsEqual, in order) is logged with the event
// calls (impfx). After each call the result, the out message, the queue's messages in order, the list's size word, the
// lock's count and the reference count are noted. The Wait* slots meet an empty queue through a WaitForSingleObject
// handler that pushes the next pool message (slot 3) and answers WAIT_OBJECT_0.
#include "difftest.h"
#include "free_recorder.h"
#include "impfx.h"

#include "gz/cGZMessageQueue.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0xc66b0d, kDtor = 0xc66c91;
// Slot RVAs (the game's vtable 0xe9e588), for the record: tests reach them through each side's vtable.
constexpr uint32_t kSlotRvas[] = {0xc67a98, 0xc67b34, 0xc679e6, 0xc67a3e, 0xc67cb7, 0xc67c63, 0xc67d5e,
                                  0xc67d09, 0xc67bd3, 0xc67b72, 0xc67b4b, 0xc679b4, 0xc6793f, 0xc678ba,
                                  0xc67972, 0xc678fc, 0xc67862, 0xc6753f, 0xc674b9, 0xc6758e, 0xc6750a,
                                  0xc675c3, 0xc66e59, 0xc67453};

using F0 = uint32_t(__thiscall *)(void *);
using F1 = uint32_t(__thiscall *)(void *, uint32_t);
using F2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

uint32_t Slot0(void *q, unsigned slot) { return reinterpret_cast<F0>((*static_cast<void ***>(q))[slot])(q); }
uint32_t Slot1(void *q, unsigned slot, uint32_t a) { return reinterpret_cast<F1>((*static_cast<void ***>(q))[slot])(q, a); }
uint32_t Slot2(void *q, unsigned slot, uint32_t a, uint32_t b)
{
    return reinterpret_cast<F2>((*static_cast<void ***>(q))[slot])(q, a, b);
}

inline constexpr drawfx::Slot kMessageSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ii"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {2, drawfx::Ret::Int, true, "Create", "ui"},
    {0, drawfx::Ret::Int, true, "GetType", ""},
    {1, drawfx::Ret::Int, true, "SetType", "u"},
    {1, drawfx::Ret::Int, true, "IsEqual", "i"},
    {1, drawfx::Ret::Int, true, "IsLess", "i"},
};
inline constexpr drawfx::Iface kMessage{"cIGZMessage2", 8, kMessageSlots};

constexpr unsigned kPool = 6;
drawfx::Fake gPool[kPool];

uint32_t GetType(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t IsEqual(drawfx::Fake &self, const uint32_t *args)
{
    const auto *other = reinterpret_cast<const drawfx::Fake *>(uintptr_t(args[0]));
    return self.value[0] == other->value[0] && self.value[1] == other->value[1];
}

// Types 1, 2, 1, 3, 2, 1; payloads make fakes 0 and 5 equal (type 1, payload 0), the others distinct.
void ResetPool()
{
    constexpr uint32_t types[kPool] = {1, 2, 1, 3, 2, 1};
    constexpr uint32_t payloads[kPool] = {0, 0, 1, 0, 1, 0};
    for (unsigned i = 0; i < kPool; ++i) {
        gPool[i].Reset<kMessage>(0x700 + i);
        gPool[i].value[0] = types[i];
        gPool[i].value[1] = payloads[i];
        gPool[i].On(4, &GetType);
        gPool[i].On(6, &IsEqual);
    }
}

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

// A message word as the fake's id (0 null, the word itself otherwise: the out sentinel).
uint32_t IdOf(uint32_t word)
{
    for (const drawfx::Fake &f : gPool)
        if (word == Word(&f)) return f.id;
    return word;
}

// The list at +4 read raw: (head, size); nodes {next, prev, counted pointer}.
struct RawNode {
    RawNode *next, *prev;
    void *message;
};
RawNode *Head(void *q) { return *reinterpret_cast<RawNode **>(static_cast<uint8_t *>(q) + 4); }
uint32_t SizeWord(void *q) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(q) + 8); }

void NoteQueue(void *q)
{
    std::vector<uint32_t> ids;
    RawNode *head = Head(q);
    for (RawNode *node = head->next; node != head && ids.size() < 200; node = node->next) {
        if (node->next->prev != node) ids.push_back(0xbadbad);
        ids.push_back(IdOf(Word(node->message)));
    }
    drawfx::Note("queue size", {SizeWord(q), uint32_t(ids.size())});
    for (uint32_t id : ids) drawfx::Note("message", id);
    const auto *lock = reinterpret_cast<const cRZCriticalSection *>(static_cast<uint8_t *>(q) + 0xc);
    drawfx::Note("lock count, refs",
                 {uint32_t(lock->mBlock->lockCount), *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(q) + 0x1c)});
}

void NotePoolRefs()
{
    for (const drawfx::Fake &f : gPool) drawfx::Note("pool refs", uint32_t(f.refs));
}

void *gQueue = nullptr;
uint32_t gSupplied = 0;
uint32_t WaitPushes(drawfx::Fake &, const uint32_t *)
{
    Slot1(gQueue, 3, Word(&gPool[gSupplied++ % kPool]));
    return WAIT_OBJECT_0;
}

enum class Arg : uint8_t { None, Type, Message, Out, MessageOut };
struct SlotInfo {
    unsigned slot;
    Arg arg;
    bool boolResult;
};
constexpr SlotInfo kSlots[] = {
    {3, Arg::Message, false}, {4, Arg::Message, false},  {5, Arg::Type, true},    {6, Arg::Message, true},
    {7, Arg::Type, true},     {8, Arg::Message, true},   {9, Arg::Type, false},   {10, Arg::Message, false},
    {11, Arg::None, false},   {12, Arg::None, false},    {13, Arg::None, false},  {14, Arg::Type, true},
    {15, Arg::Out, true},     {16, Arg::Out, true},      {17, Arg::MessageOut, true}, {18, Arg::Out, true},
    {19, Arg::Out, true},     {20, Arg::Out, true},      {21, Arg::Out, true},    {22, Arg::None, false},
    {24, Arg::MessageOut, true}, {1, Arg::None, false},  {2, Arg::None, false},   {0, Arg::Type, true},
};

void *BuildOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cGZMessageQueue2::kSize);
    ctx.Original<F0>(kCtor)(memory);
    return memory;
}

void *BuildOurs() { return cGZMessageQueueAdapters::Construct2(::operator new(cGZMessageQueue2::kSize), nullptr); }

void RunScript(void *q, uint32_t seed)
{
    std::mt19937 rng(seed);
    gQueue = q;
    gSupplied = 0;
    uint32_t refs = 0;
    for (int step = 0; step < 60; ++step) {
        const bool push = rng() % 3 == 0;
        const SlotInfo &info = push ? kSlots[rng() % 2] : kSlots[rng() % std::size(kSlots)];
        if (info.slot == 2 && refs < 2) continue;
        const uint32_t message = Word(&gPool[rng() % kPool]);
        const uint32_t type = rng() % 5;
        uint32_t out = 0xa5a5a5a5;
        uint32_t result = 0;
        if (info.slot == 0) {
            const uint32_t riid = std::array<uint32_t, 3>{1, cGZMessageQueue2::kIid, 0x298b2f5e}[rng() % 3];
            result = Slot2(q, 0, riid, Word(&out));
            drawfx::Note("qi out", out == Word(q) ? 1u : out);
            if (result & 0xff) ++refs;
        } else {
            switch (info.arg) {
            case Arg::None: result = Slot0(q, info.slot); break;
            case Arg::Type: result = Slot1(q, info.slot, type); break;
            case Arg::Message: result = Slot1(q, info.slot, message); break;
            case Arg::Out: result = Slot1(q, info.slot, Word(&out)); break;
            case Arg::MessageOut: result = Slot2(q, info.slot, message, Word(&out)); break;
            }
            if (info.slot == 1) ++refs;
            if (info.slot == 2) --refs;
        }
        if (info.slot == 3 || info.slot == 4)
            drawfx::Note("push", info.slot);
        else
            drawfx::Note("slot, result", {info.slot, info.boolResult ? (result & 0xff) : result});
        if (info.arg == Arg::Out || info.arg == Arg::MessageOut) drawfx::Note("out", IdOf(out));
        NoteQueue(q);
    }
    NotePoolRefs();
}

void Destroy(void *q, bool viaFlags, std::vector<size_t> &freed)
{
    size_t sizes[256];
    difftest::FreeRecorder rec;
    if (viaFlags) {
        Slot1(q, 23, 1);
        rec.Stop();
    } else {
        Slot1(q, 23, 0);
        rec.Stop();
        ::operator delete(q);
    }
    freed.assign(sizes, sizes + rec.SortedSizes(sizes, 256));
    NotePoolRefs();
}

} // namespace

DIFF_TEST(GZMessageQueue2_Script, 0xc679e6)
{
    (void)kSlotRvas;
    impfx::Imports imp(ctx);
    imp.Sync();
    imp.Get("WaitForSingleObject").On(&WaitPushes);
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        const bool viaFlags = seed % 2 == 0;
        void *qo = nullptr, *qu = nullptr;
        std::vector<size_t> freedO, freedU;
        auto a = drawfx::Side(
            [&] {
                imp.BeginSide();
                ResetPool();
                qo = BuildOriginal(ctx);
            },
            [&] {
                RunScript(qo, seed);
                Destroy(qo, viaFlags, freedO);
            });
        auto b = drawfx::Side(
            [&] {
                imp.BeginSide();
                ResetPool();
                qu = BuildOurs();
            },
            [&] {
                RunScript(qu, seed);
                Destroy(qu, viaFlags, freedU);
            });
        if (!drawfx::Same(ctx, a.first, b.first, "script", seed, a.second.empty() ? b.second : a.second)) return;
        if (freedO != freedU) {
            ctx.Fail("seed %u: freed %zu blocks vs %zu", seed, freedO.size(), freedU.size());
            return;
        }
    }
}

// Release deletes the queue at a count of 1 or less (slot 23 with 1): the messages it held are released, the event
// closed, the blocks freed.
DIFF_TEST(GZMessageQueue2_ReleaseDeletes, 0xc67b34)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t messages = 0; messages < 4; ++messages) {
        for (uint32_t addRefs = 0; addRefs < 3; ++addRefs) {
            std::vector<size_t> freed[2];
            auto run = [&](void *q, std::vector<size_t> &out) {
                for (uint32_t i = 0; i < messages; ++i) Slot1(q, 3, Word(&gPool[i]));
                for (uint32_t i = 0; i < addRefs; ++i) drawfx::Note("addref", Slot0(q, 1));
                size_t sizes[64];
                difftest::FreeRecorder rec;
                // The count starts at 0: the k-th release of k AddRefs (the first, with none) deletes.
                const uint32_t releases = addRefs ? addRefs : 1;
                for (uint32_t i = 0; i < releases; ++i) drawfx::Note("release", Slot0(q, 2));
                rec.Stop();
                out.assign(sizes, sizes + rec.SortedSizes(sizes, 64));
                NotePoolRefs();
            };
            void *qo = nullptr, *qu = nullptr;
            auto a = drawfx::Side(
                [&] {
                    imp.BeginSide();
                    ResetPool();
                    qo = BuildOriginal(ctx);
                },
                [&] { run(qo, freed[0]); });
            auto b = drawfx::Side(
                [&] {
                    imp.BeginSide();
                    ResetPool();
                    qu = BuildOurs();
                },
                [&] { run(qu, freed[1]); });
            const uint32_t seed = messages * 10 + addRefs;
            if (!drawfx::Same(ctx, a.first, b.first, "release", seed, a.second.empty() ? b.second : a.second)) return;
            if (freed[0] != freed[1]) {
                ctx.Fail("case %u: freed %zu blocks vs %zu", seed, freed[0].size(), freed[1].size());
                return;
            }
        }
    }
}

// The constructor and destructor adapters, as in gz_message_queue.cpp: the fresh state, then the destructor (0xc66c91,
// our Destruct2) on a queue holding messages, built by our adapter or (route 1) by the original constructor.
DIFF_TEST(GZMessageQueue2_ConstructDestruct, 0xc66b0d)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t route = 0; route < 2; ++route) {
        for (uint32_t messages = 0; messages < 5; ++messages) {
            std::vector<size_t> freed[2];
            auto run = [&](void *q, bool ours, std::vector<size_t> &out) {
                RawNode *head = Head(q);
                drawfx::Note("fresh", {head->next == head ? 1u : 0u, head->prev == head ? 1u : 0u, SizeWord(q)});
                const auto *signal = reinterpret_cast<const cRZThreadSignal *>(static_cast<uint8_t *>(q) + 0x14);
                drawfx::Note("signal", {Word(signal->mState->mEvent), signal->mState->mManualReset});
                NoteQueue(q);
                for (uint32_t i = 0; i < messages; ++i) Slot1(q, 4, Word(&gPool[i % kPool]));
                size_t sizes[64];
                difftest::FreeRecorder rec;
                if (ours)
                    cGZMessageQueueAdapters::Destruct2(static_cast<cGZMessageQueue2 *>(q), nullptr);
                else
                    ctx.Original<F0>(kDtor)(q);
                rec.Stop();
                out.assign(sizes, sizes + rec.SortedSizes(sizes, 64));
                ::operator delete(q);
                NotePoolRefs();
            };
            void *qo = nullptr, *qu = nullptr;
            auto a = drawfx::Side(
                [&] {
                    imp.BeginSide();
                    ResetPool();
                    qo = BuildOriginal(ctx);
                },
                [&] { run(qo, false, freed[0]); });
            auto b = drawfx::Side(
                [&] {
                    imp.BeginSide();
                    ResetPool();
                    qu = route == 0 ? BuildOurs() : BuildOriginal(ctx);
                },
                [&] { run(qu, true, freed[1]); });
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
