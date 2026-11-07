// Differential tests for nGZEvent::cConnectionManager and its iterators (src/gz/cConnectionManager.cpp, standalone shape).
// The original side builds its manager with the original constructor 0x1aa630 (game vtable 0xda233c), ours through the
// constructor hook's adapter; both run the same random script through their own vtables (the manager and the iterators
// are called through our interface declarations, whose slots are the game's). The connected objects are drawfx fakes of
// the behaviour interface: most answer QueryInterface 0xfb859324 with themselves, fake 5 refuses (it is its own
// behaviour), fake 6 answers fake 2 (a proxy: connections made through it are fake 2's). OnEvent (slot 3) is logged
// with the event's contents; fake 4, when it is the source, rewrites the event's target to fake 1 (the original still
// tells its own copy of the target next). After each call the result is noted; ranges are walked to
// their end (the entries, the null iterators' extra slots, the counts and the frees on release). The script disconnects
// only connections it made (the original corrupts its tree otherwise: see GZConnectionManager_DisconnectMissing).
#include "difftest.h"
#include "free_recorder.h"
#include "impfx.h"

#include "gz/cConnectionManager.h"

#include <cstdint>
#include <random>
#include <vector>

using namespace nGZEvent;

namespace {

constexpr uint32_t kCtor = 0x1aa630, kDtor = 0x1aa69a;
using F0 = uint32_t(__thiscall *)(void *);
using F1 = uint32_t(__thiscall *)(void *, uint32_t);

// The original functions the tests reach through the game's vtables (checked against them by GZConnectionManager_VTables
// below): the manager's (0xda233c), the iterator's (0xda2378) and the null iterator's (0xda23bc) own slots, with the
// helpers and constructors they call.
struct VSlot {
    uint32_t vtable, slot, rva;
};
constexpr VSlot kVSlots[] = {
    {0xda233c, 0, 0x1aaf72},  {0xda233c, 3, 0x1aaabd},  {0xda233c, 4, 0x1aa84e},  {0xda233c, 5, 0x1aa9ca},
    {0xda233c, 6, 0x1aac7d},  {0xda233c, 7, 0x1aac40},  {0xda233c, 8, 0x1aae49},  {0xda233c, 9, 0x1aadfe},
    {0xda233c, 10, 0x1aae94}, {0xda233c, 11, 0x1aacba}, {0xda233c, 12, 0x1aa7a7}, {0xda233c, 13, 0x1aa788},
    {0xda2378, 3, 0x1aaa7b},  {0xda2378, 5, 0x1aac0d},  {0xda2378, 6, 0x1aadf6},  {0xda2378, 7, 0x1aa769},
    {0xda23bc, 3, 0x1aaa61},  {0xda23bc, 5, 0x1aaba9},  {0xda23bc, 10, 0x1aa706},
};
// Reached through those: the behaviour-face helper 0x1aaa98, the iterator's constructors 0x1aa618 / 0x1aa5ff and its
// destructor 0x1aa693.
[[maybe_unused]] constexpr uint32_t kHelpers[] = {0x1aaa98, 0x1aa618, 0x1aa5ff, 0x1aa693};

inline constexpr drawfx::Slot kBehaviorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {4, drawfx::Ret::Int, true, "OnEvent", "uuiu"},
};
inline constexpr drawfx::Iface kBehavior{"cIAbstractBehaviorType", 4, kBehaviorSlots};

constexpr unsigned kPool = 7;
drawfx::Fake gPool[kPool];

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

uint32_t OnEvent(drawfx::Fake &self, const uint32_t *args)
{
    auto *event = reinterpret_cast<cConnectionChangedEvent *>(uintptr_t(args[2]));
    const uint32_t vtable = Word(event->mVTable) == Word(GameAddress<void>(cConnectionChangedEvent::kVTable))
                                ? 1u
                                : Word(event->mVTable);
    drawfx::Note("event", {vtable, event->mDisconnected, drawfx::Norm(Word(event->mSource)),
                           drawfx::Norm(Word(event->mTarget)), event->mSourceEvent, event->mTargetEvent});
    if (&self == &gPool[4] && Word(event->mSource) == Word(&self))
        event->mTarget = reinterpret_cast<cIAbstractBehaviorType *>(&gPool[1]);
    return 0x5a;
}

void ResetPool()
{
    for (unsigned i = 0; i < kPool; ++i) {
        gPool[i].Reset<kBehavior>(0x900 + i);
        gPool[i].On(3, &OnEvent);
    }
    for (unsigned i = 0; i < kPool; ++i) {
        if (i == 5) continue;
        gPool[i].AddFace(cIAbstractBehaviorType::kIid, gPool[i == 6 ? 2 : i]);
    }
}

cIGZUnknown *Object(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&gPool[i]); }
unsigned FaceOf(unsigned i) { return i == 6 ? 2 : i; }

void NotePoolRefs()
{
    for (const drawfx::Fake &f : gPool) drawfx::Note("pool refs", uint32_t(f.refs));
}

void *BuildOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cConnectionManager::kSize);
    ctx.Original<F0>(kCtor)(memory);
    return memory;
}

void *BuildOurs() { return cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr); }

cIConnectionManager *Manager(void *m) { return static_cast<cIConnectionManager *>(m); }

// Walks a range to its end (at most 40 steps), checks the iterators' own slots, then releases the three references
// (the frees noted by size). `null`: the object had no entry, so both ends are null iterators.
void NoteRange(cConnectionRange range, bool null)
{
    drawfx::Note("range present", {range.mCurrent ? 1u : 0u, range.mBegin ? 1u : 0u, range.mEnd ? 1u : 0u});
    // An empty run's begin may be the tree's head, whose value is uninitialised memory on both sides: read only a null
    // range's (its fixed {null, 0}) or a non-empty run's.
    if (null || !range.mBegin->IsEqual(range.mEnd)) {
        const cConnectionInfo first = range.mBegin->Get();
        drawfx::Note("begin", {drawfx::Norm(Word(first.mObject)), first.mEvent});
    }
    unsigned steps = 0;
    cIConnectionIterator *it = range.mCurrent;
    while (steps < 40 && !it->IsEqual(range.mEnd)) {
        const cConnectionInfo info = it->Get();
        drawfx::Note("entry", {drawfx::Norm(Word(info.mObject)), info.mEvent});
        // A clone mid-way reads the same entry and starts with no references.
        if (steps == 1) {
            cIConnectionIterator *clone = it->Clone();
            const cConnectionInfo again = clone->Get();
            drawfx::Note("clone", {drawfx::Norm(Word(again.mObject)), again.mEvent, clone->IsEqual(it) ? 1u : 0u,
                                   clone->AddRef()});
            difftest::FreeRecorder rec;
            drawfx::Note("clone release", clone->Release());
            rec.Stop();
            drawfx::Note("clone freed", {uint32_t(rec.Count()), rec.Count() ? uint32_t(rec.Size(0)) : 0u});
        }
        it->Next();
        ++steps;
    }
    drawfx::Note("steps", steps);
    drawfx::Note("begin == end", range.mBegin->IsEqual(range.mEnd) ? 1u : 0u);
    void *face = reinterpret_cast<void *>(0x1234);
    drawfx::Note("qi 1", {range.mBegin->QueryInterface(1, &face) ? 1u : 0u, face == range.mBegin ? 1u : Word(face)});
    face = reinterpret_cast<void *>(0x1234);
    drawfx::Note("qi other", {range.mBegin->QueryInterface(cIConnectionManager::kIid, &face) ? 1u : 0u, Word(face)});
    drawfx::Note("qi null", range.mEnd->QueryInterface(1, nullptr) ? 1u : 0u);
    drawfx::Note("begin release", range.mBegin->Release()); // the qi 1 reference
    if (null) {
        auto *begin = static_cast<cIConnectionRandomAccessIterator *>(range.mBegin);
        begin->Previous();
        begin->Advance(3);
        const cConnectionInfo current = begin->At(5);
        drawfx::Note("null slots", {drawfx::Norm(Word(current.mObject)), current.mEvent});
        cIConnectionIterator *clone = begin->Clone();
        drawfx::Note("null clone", {clone->AddRef(), clone->IsEqual(range.mBegin) ? 1u : 0u});
        clone->Release();
    }
    difftest::FreeRecorder rec;
    const uint32_t a = range.mCurrent->Release();
    const uint32_t b = range.mBegin->Release();
    const uint32_t c = range.mEnd->Release();
    rec.Stop();
    size_t sizes[8];
    const size_t n = rec.SortedSizes(sizes, 8);
    drawfx::Note("releases", {a, b, c, uint32_t(n), n ? uint32_t(sizes[0]) : 0u, n > 2 ? uint32_t(sizes[2]) : 0u});
}

struct Conn {
    unsigned s, se, d, de; // faces (pool indices) and events
};

// One random script (`steps` calls) on the manager at `m`, with a model of the live connections that picks the
// connections to disconnect.
void RunScript(void *m, uint32_t seed, int steps)
{
    std::mt19937 rng(seed);
    std::vector<Conn> live;
    cIConnectionManager *mgr = Manager(m);
    auto object = [&](unsigned face) { return Object(face == 2 && rng() % 2 ? 6 : face); };
    auto has = [&](bool from, unsigned face) {
        for (const Conn &c : live)
            if ((from ? c.s : c.d) == face) return true;
        return false;
    };
    for (int step = 0; step < steps; ++step) {
        const unsigned op = rng() % 12;
        const unsigned a = rng() % kPool, b = rng() % kPool;
        const unsigned ea = rng() % 4, eb = rng() % 4;
        switch (op) {
        case 0:
        case 1:
            mgr->Connect(Object(a), ea, Object(b), eb);
            live.push_back({FaceOf(a), ea, FaceOf(b), eb});
            drawfx::Note("connect", {a, ea, b, eb});
            break;
        case 2: {
            if (live.empty()) break;
            const size_t pick = rng() % live.size();
            const Conn c = live[pick];
            mgr->Disconnect(object(c.s), c.se, object(c.d), c.de);
            live.erase(live.begin() + pick);
            drawfx::Note("disconnect", {c.s, c.se, c.d, c.de});
            break;
        }
        case 3: {
            if (rng() % 3) break;
            mgr->DisconnectAll(Object(a));
            const unsigned face = FaceOf(a);
            std::vector<Conn> kept;
            for (const Conn &c : live)
                if (c.s != face && c.d != face) kept.push_back(c);
            live = kept;
            drawfx::Note("disconnect all", a);
            break;
        }
        case 4: drawfx::Note("has from", {a, mgr->HasConnectionsFrom(Object(a)) ? 1u : 0u}); break;
        case 5: drawfx::Note("has to", {a, mgr->HasConnectionsTo(Object(a)) ? 1u : 0u}); break;
        case 6: drawfx::Note("count from", {a, ea, mgr->CountFrom(Object(a), ea)}); break;
        case 7: drawfx::Note("count to", {a, ea, mgr->CountTo(Object(a), ea)}); break;
        case 8: {
            unsigned s = a, se = ea, d = b, de = eb;
            if (!live.empty() && rng() % 2) {
                const Conn &c = live[rng() % live.size()];
                s = c.s, se = c.se, d = c.d, de = c.de;
                if (rng() % 3 == 0) de ^= 1; // a near miss
            }
            drawfx::Note("connected", {s, se, d, de, mgr->IsConnected(object(s), se, object(d), de) ? 1u : 0u});
            break;
        }
        case 9: NoteRange(mgr->ConnectionsFrom(Object(a), ea), !has(true, FaceOf(a))); break;
        case 10: NoteRange(mgr->ConnectionsTo(Object(a), ea), !has(false, FaceOf(a))); break;
        case 11: {
            void *out = reinterpret_cast<void *>(0x1234);
            const uint32_t riid = (rng() % 3 == 0) ? 1u : (rng() % 2 ? cIConnectionManager::kIid : 0xfb859324u);
            const bool r = mgr->QueryInterface(riid, rng() % 5 ? &out : nullptr);
            drawfx::Note("manager qi", {riid, r ? 1u : 0u, out == m ? 1u : Word(out), mgr->AddRef(), mgr->Release()});
            break;
        }
        }
    }
    NotePoolRefs();
}

// Destroys the manager, the frees noted by size. Mode 0: the destructor (0x1aa69a or our adapter), then the block freed
// here; 1: the deleting destructor (slot 13 through the side's own vtable) with flag 1, which frees the block itself;
// 2: slot 13 with flag 0, then the block freed here.
void Destroy(difftest::Context &ctx, void *m, bool ours, unsigned mode = 0)
{
    difftest::FreeRecorder rec;
    if (mode != 0)
        reinterpret_cast<F1>((*static_cast<void ***>(m))[13])(m, mode == 1 ? 1u : 0u);
    else if (ours)
        cConnectionManagerAdapters::Destruct(static_cast<cConnectionManager *>(m), nullptr);
    else
        ctx.Original<F0>(kDtor)(m);
    rec.Stop();
    size_t sizes[256];
    const size_t n = rec.SortedSizes(sizes, 256);
    drawfx::Note("destroy frees", uint32_t(n));
    for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
    if (mode != 1) ::operator delete(m);
}

template <class Run> bool Compare(difftest::Context &ctx, impfx::Imports &imp, const char *what, uint32_t seed, Run run,
                                  bool originalBuildsOurs = false)
{
    void *mo = nullptr, *mu = nullptr;
    auto a = drawfx::Side(
        [&] {
            imp.BeginSide();
            ResetPool();
            mo = BuildOriginal(ctx);
        },
        [&] { run(mo, false); });
    auto b = drawfx::Side(
        [&] {
            imp.BeginSide();
            ResetPool();
            mu = originalBuildsOurs ? BuildOriginal(ctx) : BuildOurs();
        },
        [&] { run(mu, true); });
    return drawfx::Same(ctx, a.first, b.first, what, seed, a.second.empty() ? b.second : a.second);
}

} // namespace

DIFF_TEST(GZConnectionManager_Script, 0x1aaabd)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        const int steps = seed % 10 == 0 ? 200 : 60;
        if (!Compare(ctx, imp, "script", seed, [&](void *m, bool ours) {
                RunScript(m, seed, steps);
                Destroy(ctx, m, ours, seed % 3);
            }))
            return;
    }
}

// The slots the script reaches are the game vtables' own (kVSlots), so the script runs each of them on the original side.
DIFF_TEST(GZConnectionManager_VTables, 0xda233c)
{
    for (const VSlot &v : kVSlots) {
        ++ctx.cases;
        void *const *table = GameAddress<void *const>(v.vtable);
        if (table[v.slot] != GameAddress<void>(v.rva))
            ctx.Fail("vtable 0x%x slot %u is not 0x%x", v.vtable, v.slot, v.rva);
    }
}

// The constructor and destructor adapters: a fresh manager answers empty everywhere; the destructor (route 1: ours on a
// manager the original constructor built) frees what the connections hold.
DIFF_TEST(GZConnectionManager_ConstructDestruct, 0x1aa630)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t route = 0; route < 2; ++route) {
        for (uint32_t connections = 0; connections < 6; ++connections) {
            const uint32_t seed = route * 10 + connections;
            if (!Compare(
                    ctx, imp, "construct/destruct", seed,
                    [&](void *m, bool ours) {
                        cIConnectionManager *mgr = Manager(m);
                        for (unsigned i = 0; i < kPool; ++i)
                            drawfx::Note("fresh", {i, mgr->HasConnectionsFrom(Object(i)) ? 1u : 0u,
                                                   mgr->HasConnectionsTo(Object(i)) ? 1u : 0u});
                        for (uint32_t i = 0; i < connections; ++i) mgr->Connect(Object(i % 3), i, Object((i + 1) % 5), 7);
                        Destroy(ctx, m, ours);
                        NotePoolRefs();
                    },
                    route == 1))
                return;
        }
    }
}

// Disconnecting a connection that is not there: the original erases the connection after the key's run in each map
// (when there is one; with none it erases the tree's head and corrupts it, which only ours guards against and the test
// does not reach). Afterwards the two maps disagree; counts, presence and ranges are compared everywhere.
DIFF_TEST(GZConnectionManager_DisconnectMissing, 0x1aa84e)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    for (uint32_t variant = 0; variant < 4; ++variant) {
        if (!Compare(ctx, imp, "disconnect missing", variant, [&](void *m, bool ours) {
                cIConnectionManager *mgr = Manager(m);
                mgr->Connect(Object(0), 1, Object(1), 1);
                mgr->Connect(Object(0), 2 + variant, Object(2), 1);
                mgr->Connect(Object(3), 5, Object(2), 2 + variant);
                if (variant & 1) mgr->Connect(Object(0), 1, Object(2), 3);
                // (0,1)->(2,1) is not there; the runs after key 1 under 0 (from) and under 2 (to) are erased instead.
                mgr->Disconnect(Object(0), 1, Object(2), 1);
                for (unsigned i = 0; i < 4; ++i) {
                    drawfx::Note("has", {i, mgr->HasConnectionsFrom(Object(i)) ? 1u : 0u,
                                         mgr->HasConnectionsTo(Object(i)) ? 1u : 0u});
                    for (unsigned e = 0; e < 6; ++e) {
                        drawfx::Note("counts", {i, e, mgr->CountFrom(Object(i), e), mgr->CountTo(Object(i), e)});
                        if (mgr->CountFrom(Object(i), e)) NoteRange(mgr->ConnectionsFrom(Object(i), e), false);
                        if (mgr->CountTo(Object(i), e)) NoteRange(mgr->ConnectionsTo(Object(i), e), false);
                    }
                }
                Destroy(ctx, m, ours);
                NotePoolRefs();
            }))
            return;
    }
}
