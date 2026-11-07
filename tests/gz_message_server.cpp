// Differential test for the first message server (lane KU, src/gz/cGZMessageServer.h), in the standalone shape.
//
// GZMessageServer_Run: a server built by the original constructor 0xc74c5d on 0xc5-filled memory (every seventh seed by
// the original creator 0xc751d0) against one built by ours (placement new / Create), each driven through its own vtables
// by the same random operations, then destroyed: by the last Release (the service's), or by the +4 face's slot 3 (thunk
// 0xc74e3a -> deleting destructor 0xc74e61 -> destructor 0xc74d0c) with flags 1 or 0.
// Operations, through the cIGZMessageServer face (+0x1c, game vtable 0xea04a4) unless said: MessageSend 0xc754cb (the
// pending changes applied first: 0xc7508a), MessagePost 0xc75497 (urgent as a raw word: garbage above the low byte),
// AddNotification 0xc74e83 / RemoveNotification 0xc759c1 (both replaying the pending list: 0xc75005),
// GeneralMessagePostToTarget 0xc751ef, CancelGeneralMessagePostsToTarget 0xc74f80 (both returning the general lock's
// count after their unlock: every third call is made holding that lock, so it is 1), OnTick 0xc7558d (+0 slot 10, and the
// +0x1c slot 9 thunk 0xc7565b; the general posts: 0xc75a9b), GetMessageQueueSize 0xc75459, SetAlwaysClearQueueOnTick
// (folded 0x9afde0) with raw words, QueryInterface 0xc7582a (+0; thunks 0xc75854 on +4, 0xc7585c on +0x1c), Init 0xc7547d
// and Shutdown 0xc75b61 with the framework (cell 0x10c9378, a recording fake: slots 9 / 10) present or not, a mass
// registration of 1, 381, 382 or 383 targets on one id (the send's 383-target array: 382 and 383 reach its last slots), and
// the processed counter set near 2^32 so the tick's 64-bit stop count carries.
// Re-entrancy: the targets are recording fakes whose DoMessage may (within a budget, nesting up to three deep) send, post,
// register, unregister (themselves too), post to a target, cancel, tick or change the clear flag, and whose AddRef (called
// while a send collects its targets, or a registration is made) may post, register, unregister, post to a target, cancel
// or change the flag. Release never re-enters (the original releases while walking its pending list). Left out, as the
// original misbehaves there: more than 383 targets on one id (its send array overruns), and a cancel that erases the last
// general post (its walk runs past the deque's end; the test simulates each cancel first and skips those).
// Compared after every operation: the return value; every call the fakes saw (with each message's words); the scalar
// fields; the three locks' counts; the queue (built by 0xc74c32 on the original side, our cGZMessageQueue on ours: its
// vtable as a code, its list, lock count, event handle, count word); the pending list; the registrations in the map's order (bucket count, load factor); the
// general posts (the deque's map size, offset, size, each element); every target's reference count. After destruction:
// the blocks freed (sorted sizes) and the targets' counts.
// Every Win32 call is a logging fake in its import slot: CreateEventA 0xd9a244, CloseHandle 0xd9a17c, SetEvent 0xd9a3dc
// (and 0xd9a10c, the manual-reset event's pulse). The game's operator new / delete (0x824632 / 0x824624) are ours.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gzlog_support.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZMessageQueue.h"
#include "gz/cGZMessageServer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace msgserver_test {

using svcfx::Blob;
using Server = cGZMessageServer;

constexpr uint32_t kTag = 0x7d000000; // the server under test
constexpr unsigned kTargetCount = 400;
constexpr uint32_t kBigId = 0x5000;
constexpr uint32_t kIds[] = {0, 1, 7, 0x10, 0x11, 0x64063387, 0xffffffff, kBigId};

// ---- the targets ----

struct Target {
    void **vt;
    uint32_t index;
    int32_t refs;
};
Target gTargets[kTargetCount];
Blob gLog;
std::mt19937 gHandlerRng;
uint8_t *gObject; // the server the handlers re-enter
int gNest;
int gBudget;
bool gQuiet; // no re-entry (the mass registration)

uint32_t TN(uint32_t v, std::source_location where = std::source_location::current())
{
    const uint32_t lo = svcfx::P(&gTargets[0]), hi = svcfx::P(&gTargets[kTargetCount]);
    if (v >= lo && v < hi) return 0x7a000000 | ((v - lo) / sizeof(Target)) << 4 | (v - lo) % sizeof(Target);
    return svcfx::Norm(v, where);
}
uint32_t TN(const void *p, std::source_location where = std::source_location::current()) { return TN(svcfx::P(p), where); }

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint8_t *MsgFace() { return Face(gObject, 0x1c); }

// The state the handlers decide by, read through the same STL types (the game's layouts).
Server::TargetMap &MapOf(uint8_t *o) { return *reinterpret_cast<Server::TargetMap *>(o + 0x70); }
std::list<Server::PendingChange> &PendingOf(uint8_t *o) { return *reinterpret_cast<std::list<Server::PendingChange> *>(o + 0x68); }
std::deque<Server::GeneralPost> &PostsOf(uint8_t *o) { return *reinterpret_cast<std::deque<Server::GeneralPost> *>(o + 0x94); }
std::list<cGZMessage> &QueueListOf(uint8_t *o) { return *reinterpret_cast<std::list<cGZMessage> *>(o + 0x3c); }

// Targets registered or waiting to be on an id (a registration of one already there adds none: this is an upper bound).
size_t CountFor(uint8_t *o, uint32_t id)
{
    size_t n = 0;
    const auto found = MapOf(o).find(id);
    if (found != MapOf(o).end() && found->second) n = found->second->size();
    for (const Server::PendingChange &c : PendingOf(o))
        if (c.add && c.id == id) ++n;
    return n;
}

// A cancel the original survives: no erased post is the last one at the time (see the file comment).
bool CancelSafe(uint8_t *o, uint32_t target)
{
    std::vector<uint32_t> v;
    for (const Server::GeneralPost &p : PostsOf(o)) v.push_back(svcfx::P(p.target));
    for (size_t i = 0; i < v.size();) {
        if (v[i] != target) {
            ++i;
            continue;
        }
        if (i == v.size() - 1) return false;
        const bool frontMoves = i < v.size() - i - 1;
        v.erase(v.begin() + static_cast<ptrdiff_t>(i));
        if (!frontMoves) ++i;
    }
    return true;
}

void LogMessage(const cGZMessage *m)
{
    gLog.push_back(TN(m));
    gLog.push_back(m->mId);
    gLog.push_back(m->mWord4);
    gLog.push_back(TN(svcfx::P(m->mObject)));
    gLog.push_back(m->mWordC);
}

cGZMessage RandomMessage(std::mt19937 &rng)
{
    cGZMessage m;
    m.mId = kIds[rng() % 8];
    m.mWord4 = rng() % 3 ? rng() % 16 : rng();
    m.mObject = rng() % 3 == 0 ? static_cast<void *>(&gTargets[rng() % 16]) : reinterpret_cast<void *>(uintptr_t(rng() % 64));
    m.mWordC = rng();
    return m;
}

void ReEnter(std::mt19937 &rng, bool fromAddRef);

uint32_t __fastcall TargetQuery(Target *self, void *, uint32_t iid, void **out)
{
    gLog.push_back(0x71);
    gLog.push_back(self->index);
    gLog.push_back(iid);
    *out = nullptr;
    return 0;
}
uint32_t __fastcall TargetAddRef(Target *self, void *)
{
    gLog.push_back(0x72);
    gLog.push_back(self->index);
    gLog.push_back(uint32_t(gNest));
    const uint32_t refs = uint32_t(++self->refs);
    if (!gQuiet && gNest < 3 && gBudget > 0 && gHandlerRng() % 6 == 0) ReEnter(gHandlerRng, true);
    return refs;
}
uint32_t __fastcall TargetRelease(Target *self, void *)
{
    gLog.push_back(0x73);
    gLog.push_back(self->index);
    gLog.push_back(uint32_t(gNest));
    return uint32_t(--self->refs);
}
uint32_t __fastcall TargetDoMessage(Target *self, void *, cGZMessage *message)
{
    gLog.push_back(0x74);
    gLog.push_back(self->index);
    gLog.push_back(uint32_t(gNest));
    LogMessage(message);
    if (!gQuiet && gNest < 3 && gBudget > 0 && gHandlerRng() % 3 == 0) ReEnter(gHandlerRng, false);
    return gHandlerRng() % 2 ? 0x101u : 0x100u;
}
void *gTargetVT[4] = {reinterpret_cast<void *>(&TargetQuery), reinterpret_cast<void *>(&TargetAddRef),
                      reinterpret_cast<void *>(&TargetRelease), reinterpret_cast<void *>(&TargetDoMessage)};

void ResetTargets()
{
    for (uint32_t i = 0; i < kTargetCount; ++i) gTargets[i] = {gTargetVT, i, 1000};
}

// ---- the server's calls, at the top level or from a handler ----

enum : unsigned {
    opSend, opPost, opAdd, opRemove, opGeneral, opCancel, opTick, opSize, opClear, opQuery, opInit, opShutdown,
    opMassAdd, opProcessed, opCount
};

struct Op {
    unsigned kind;
    uint32_t a, b, c;
    cGZMessage msg;
};

Op RandomOp(std::mt19937 &rng, bool top)
{
    Op op = {};
    static const unsigned kWeights[opCount] = {14, 12, 16, 9, 9, 6, 9, 2, 3, 2, 1, 1, 1, 1};
    unsigned total = 0;
    for (unsigned k = 0; k < opCount; ++k) total += kWeights[k];
    unsigned pick = rng() % total;
    op.kind = 0;
    while (pick >= kWeights[op.kind]) pick -= kWeights[op.kind++];
    if (!top && op.kind >= opSize) op.kind = rng() % 2 ? opClear : opTick;
    op.msg = RandomMessage(rng);
    op.a = rng();
    op.b = rng() % 8 ? rng() % 16 : rng() % kTargetCount;
    op.c = kIds[rng() % 8];
    switch (op.kind) {
    case opPost: {
        static const uint32_t kUrgent[] = {0, 1, 0x100, 0xffffff00, 0xffffff01, 0x80};
        op.a = kUrgent[rng() % 6];
        break;
    }
    case opClear: {
        static const uint32_t kClear[] = {0, 1, 0x100, 0xffffff00, 0x7f};
        op.a = kClear[rng() % 5];
        break;
    }
    case opQuery: {
        static const uint32_t kIids[] = {cIGZMessageServer::kIid, 0x298b2f5e, 0, 1, 0x287fb697, 0xe98b2f57, 0x12345678};
        op.a = kIids[rng() % 7];
        op.b = rng() % 3;
        break;
    }
    case opTick: op.b = rng() % 2; break;
    case opInit:
    case opShutdown: op.b = rng() % 4; op.c = rng() % 2 ? 0x101 : 0x100; break;
    case opMassAdd: {
        static const uint32_t kN[] = {1, 381, 382, 383};
        op.a = kN[rng() % 4];
        break;
    }
    case opProcessed: {
        static const uint32_t kLow[] = {0xffffffff, 0xfffffffe, 0xfffffffd, 0xfffffff0, 0};
        op.a = kLow[rng() % 5];
        op.b = rng() % 3 ? 0 : rng() % 2;
        break;
    }
    default: break;
    }
    return op;
}

void RunOp(Blob &b, const Op &op)
{
    uint8_t *face = MsgFace();
    cGZMessage msg = op.msg;
    Target *target = &gTargets[op.b];
    switch (op.kind) {
    case opSend: b.push_back(Slot<uint32_t>(face, 3, &msg) & 0xff); break;
    case opPost: b.push_back(Slot<uint32_t>(face, 4, &msg, op.a) & 0xff); break;
    case opAdd:
        if (CountFor(gObject, op.c) >= Server::kMaxSendTargets) {
            b.push_back(0xadd0);
            break;
        }
        b.push_back(Slot<uint32_t>(face, 5, target, op.c) & 0xff);
        break;
    case opRemove: b.push_back(Slot<uint32_t>(face, 6, target, op.c) & 0xff); break;
    case opGeneral:
    case opCancel: {
        if (op.kind == opCancel && !CancelSafe(gObject, svcfx::P(target))) {
            b.push_back(0xca0);
            break;
        }
        // Every third call made by a caller holding the general posts' lock (+0x30; recursive): the result, the count
        // the server's unlock leaves, is then 1.
        auto *lock = reinterpret_cast<cRZCriticalSection *>(gObject + 0x30);
        const bool hold = op.a % 3 == 0;
        if (hold) lock->Lock();
        b.push_back(op.kind == opGeneral ? Slot<uint32_t>(face, 7, &msg, target) : Slot<uint32_t>(face, 8, target));
        if (hold) lock->Unlock();
        break;
    }
    case opTick:
        b.push_back(op.b ? Slot<uint32_t>(face, 9, op.a) & 0xff : Slot<uint32_t>(gObject, 10, op.a) & 0xff);
        break;
    case opSize: b.push_back(Slot<uint32_t>(face, 10)); break;
    case opClear: Slot<void>(face, 11, op.a); break;
    case opQuery: {
        static const uint32_t kFaces[] = {0, 4, 0x1c};
        void *out = reinterpret_cast<void *>(uintptr_t(0xabababab));
        b.push_back(Slot<uint32_t>(Face(gObject, kFaces[op.b]), 0, op.a, &out) & 0xff);
        b.push_back(svcfx::P(out) == 0xabababab ? 0xabababab : TN(out)); // left as set, or the face
        break;
    }
    case opInit:
    case opShutdown:
        // b: bit 0 the framework present, bit 1 which of its slots answers op.c's byte.
        *GameAddress<void *>(0x10c9378) = op.b & 1 ? &svcfx::F(0) : nullptr;
        svcfx::F(0).Set(9, "p", op.c);
        svcfx::F(0).Set(10, "p", op.c ^ 1);
        b.push_back(Slot<uint32_t>(gObject, op.kind == opInit ? 8 : 9) & 0xff);
        *GameAddress<void *>(0x10c9378) = nullptr;
        break;
    case opMassAdd:
        gQuiet = true;
        for (uint32_t i = 0; i < op.a; ++i) {
            if (CountFor(gObject, kBigId) >= Server::kMaxSendTargets) break;
            b.push_back(Slot<uint32_t>(face, 5, &gTargets[i], kBigId) & 0xff);
        }
        gQuiet = false;
        break;
    case opProcessed:
        *reinterpret_cast<uint32_t *>(gObject + 0x58) = op.a;
        *reinterpret_cast<uint32_t *>(gObject + 0x5c) = op.b;
        break;
    default: break;
    }
}

void ReEnter(std::mt19937 &rng, bool fromAddRef)
{
    --gBudget;
    ++gNest;
    Op op = RandomOp(rng, false);
    if (fromAddRef && (op.kind == opSend || op.kind == opTick)) op.kind = rng() % 2 ? opPost : opRemove;
    gLog.push_back(0x75);
    gLog.push_back(op.kind);
    Blob result;
    RunOp(result, op);
    gLog.insert(gLog.end(), result.begin(), result.end());
    --gNest;
}

// ---- Win32 fakes (both sides: the queues' calls go through the game's import table) ----

uint32_t gEvents;
HANDLE __stdcall FakeCreateEventA(void *attributes, BOOL manual, BOOL initial, const char *name)
{
    gLog.push_back(0x81);
    gLog.push_back(svcfx::P(attributes));
    gLog.push_back(uint32_t(manual));
    gLog.push_back(uint32_t(initial));
    gLog.push_back(svcfx::P(name));
    return reinterpret_cast<HANDLE>(uintptr_t(0xe7e70000 + ++gEvents));
}
BOOL __stdcall FakeCloseHandle(HANDLE h)
{
    gLog.push_back(0x82);
    gLog.push_back(svcfx::P(h));
    return TRUE;
}
BOOL __stdcall FakeSetEvent(HANDLE h)
{
    gLog.push_back(0x83);
    gLog.push_back(svcfx::P(h));
    return TRUE;
}
BOOL __stdcall FakePulseEvent(HANDLE h)
{
    gLog.push_back(0x84);
    gLog.push_back(svcfx::P(h));
    return TRUE;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- building and dumping ----

void *Build(difftest::Context &ctx, bool ours, bool creator)
{
    if (creator) return ours ? static_cast<void *>(Server::Create()) : ctx.Original<void *(__cdecl *)()>(0xc751d0)();
    void *object = ::operator new(Server::kSize);
    std::memset(object, 0xc5, Server::kSize);
    if (ours) ::new (object) Server;
    else ctx.Original<void *(__thiscall *)(void *)>(0xc74c5d)(object);
    return object;
}

uint32_t W(const uint8_t *o, uint32_t off) { return *reinterpret_cast<const uint32_t *>(o + off); }
uint32_t LockCount(const uint8_t *o, uint32_t off)
{
    const auto *lock = reinterpret_cast<const cRZCriticalSection *>(o + off);
    return uint32_t(lock->mBlock->lockCount);
}

uint32_t gVTables[2][3]; // each side's face vtables (+0, +4, +0x1c)
uint32_t gQueueVTables[2]; // each side's queue vtable (+0x38)

void Dump(Blob &b, uint8_t *o, int side)
{
    static const uint32_t kFaces[3] = {0, 4, 0x1c};
    for (unsigned i = 0; i < 3; ++i) b.push_back(W(o, kFaces[i]) == gVTables[side][i] ? 1u : W(o, kFaces[i]));
    b.push_back(W(o, 8));
    b.push_back(W(o, 0xc));
    b.push_back(W(o, 0x10));
    b.push_back(W(o, 0x14));
    b.push_back(o[0x18]);
    for (uint32_t off : {0x20u, 0x28u, 0x30u}) b.push_back(LockCount(o, off));
    // The queue (the game's on the original side, ours on ours: its vtable word as a code).
    b.push_back(W(o, 0x38) == gQueueVTables[side] ? 1u : W(o, 0x38));
    b.push_back(uint32_t(QueueListOf(o).size()));
    for (const cGZMessage &m : QueueListOf(o)) {
        b.push_back(m.mId);
        b.push_back(m.mWord4);
        b.push_back(TN(svcfx::P(m.mObject)));
        b.push_back(m.mWordC);
    }
    b.push_back(LockCount(o, 0x44));
    b.push_back(W(o, 0x4c));
    const uint8_t *event = *reinterpret_cast<uint8_t *const *>(o + 0x50);
    b.push_back(W(event, 0));
    b.push_back(W(event, 4));
    b.push_back(W(o, 0x54));
    // The scalars.
    b.push_back(W(o, 0x58));
    b.push_back(W(o, 0x5c));
    b.push_back(W(o, 0x60));
    b.push_back(o[0x64]);
    b.push_back(W(o, 0x90));
    // The pending registrations.
    b.push_back(uint32_t(PendingOf(o).size()));
    for (const Server::PendingChange &c : PendingOf(o)) {
        b.push_back(c.add);
        b.push_back(TN(c.target));
        b.push_back(c.id);
    }
    // The registrations, in the map's order.
    Server::TargetMap &map = MapOf(o);
    b.push_back(uint32_t(map.size()));
    b.push_back(uint32_t(map.bucket_count()));
    float load = map.max_load_factor();
    uint32_t loadBits;
    std::memcpy(&loadBits, &load, 4);
    b.push_back(loadBits);
    for (const auto &entry : map) {
        b.push_back(entry.first);
        b.push_back(entry.second ? uint32_t(entry.second->size()) : 0xffffffffu);
        if (entry.second)
            for (cIGZMessageTarget *t : *entry.second) b.push_back(TN(t));
    }
    // The general posts: the deque's map size, offset and size words, then each post.
    b.push_back(W(o, 0x9c));
    b.push_back(W(o, 0xa0));
    b.push_back(W(o, 0xa4));
    for (const Server::GeneralPost &p : PostsOf(o)) {
        b.push_back(TN(p.target));
        b.push_back(p.message.mId);
        b.push_back(p.message.mWord4);
        b.push_back(TN(svcfx::P(p.message.mObject)));
        b.push_back(p.message.mWordC);
    }
    for (const Target &t : gTargets)
        if (t.refs != 1000) b.push_back(t.index << 16 | uint32_t(t.refs) & 0xffff);
}

} // namespace msgserver_test

using namespace msgserver_test;

DIFF_TEST(GZMessageServer_Run, 0xc74c5d)
{
    // Also reached (see the file comment): 0xc751d0 (creator), 0xc74d0c, 0xc74e61, 0xc74e3a, 0xc7582a, 0xc75854,
    // 0xc7585c, 0xc7547d, 0xc75b61, 0xc7558d, 0xc7565b, 0xc754cb, 0xc75497, 0xc74e83, 0xc759c1, 0xc751ef, 0xc74f80,
    // 0xc75459, 0x9afde0, 0xc7508a, 0xc75005, 0xc75a9b.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    globals.SetWord(0x1103248, 0);
    gzlog_test::Slots iat(ctx);
    iat.Set(0xd9a244, reinterpret_cast<void *>(&FakeCreateEventA));
    iat.Set(0xd9a17c, reinterpret_cast<void *>(&FakeCloseHandle));
    iat.Set(0xd9a3dc, reinterpret_cast<void *>(&FakeSetEvent));
    iat.Set(0xd9a10c, reinterpret_cast<void *>(&FakePulseEvent));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    gVTables[0][0] = svcfx::P(GameAddress<void>(0xea0450));
    gVTables[0][1] = svcfx::P(GameAddress<void>(0xea0488));
    gVTables[0][2] = svcfx::P(GameAddress<void>(0xea04a4));
    // The queue at +0x38: the game's cGZMessageQueue (vtable 0xea03d4) on the original side, ours (lane LR's, built by
    // its adapter) on ours; our queue's vtable read from a scratch queue.
    gQueueVTables[0] = svcfx::P(GameAddress<void>(0xea03d4));
    {
        alignas(4) uint8_t scratch[cGZMessageQueue::kSize];
        cGZMessageQueue *const queue = cGZMessageQueueAdapters::Construct(scratch, nullptr);
        gQueueVTables[1] = W(scratch, 0);
        cGZMessageQueueAdapters::Destruct(queue, nullptr);
    }

    bool failed = false;
    for (uint32_t seed = 1; seed <= 1500 && !failed; ++seed) {
        std::mt19937 opRng(seed * 2654435761u + 7);
        const bool creator = seed % 7 == 0;
        const unsigned destroy = seed % 3;
        std::vector<Op> ops;
        const unsigned count = 10 + opRng() % 40;
        for (unsigned i = 0; i < count; ++i) ops.push_back(RandomOp(opRng, true));
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            ResetTargets();
            gLog.clear();
            gEvents = 0;
            gHandlerRng.seed(seed);
            gNest = 0;
            gQuiet = false;
            Server::sReplayingPending = false;
            *GameAddress<uint8_t>(0x1103248) = 0;
            Blob first;
            gObject = static_cast<uint8_t *>(Build(ctx, side == 1, creator));
            if (side == 1)
                for (unsigned i = 0; i < 3; ++i) gVTables[1][i] = W(gObject, i == 0 ? 0 : i == 1 ? 4 : 0x1c);
            svcfx::AddRange(gObject, Server::kSize, kTag);
            // RandomMessage's object tokens 1..63 (or a target): registered as an object of their own.
            svcfx::AddRange(nullptr, 64, 0x3f000000); // null itself still logs as 0
            Dump(first, gObject, side);
            first.insert(first.end(), gLog.begin(), gLog.end());
            gLog.clear();
            results[side].push_back(std::move(first));
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    gBudget = 6;
                    RunOp(b, ops[i]);
                    Dump(b, gObject, side);
                } else {
                    difftest::FreeRecorder frees;
                    if (destroy == 0) {
                        // The last reference released: the count raised to one first if it is not.
                        uint32_t refs = W(gObject, 8);
                        if (refs == 0) Slot<uint32_t>(gObject, 1), refs = 1;
                        for (uint32_t r = 0; r < refs; ++r) b.push_back(Slot<uint32_t>(MsgFace(), 2));
                    } else {
                        Slot<void *>(Face(gObject, 4), 3, destroy == 1 ? 1u : 0u);
                        if (destroy == 2) ::operator delete(gObject);
                    }
                    frees.Stop();
                    size_t sizes[4096];
                    const size_t n = frees.SortedSizes(sizes, 4096);
                    b.push_back(uint32_t(n));
                    for (size_t k = 0; k < n && k < 4096; ++k) b.push_back(uint32_t(sizes[k]));
                    for (const Target &t : gTargets)
                        if (t.refs != 1000) b.push_back(t.index << 16 | uint32_t(t.refs) & 0xffff);
                }
                b.insert(b.end(), gLog.begin(), gLog.end());
                gLog.clear();
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "GZMessageServer_Run", seed)) {
                ctx.Fail("  at step %u of %u (kind %u, creator %d, destroy %u)", unsigned(i), unsigned(ops.size() + 1),
                         i >= 1 && i <= ops.size() ? ops[i - 1].kind : 99u, int(creator), destroy);
                for (int side = 0; side < 2; ++side) {
                    std::string line;
                    char word[12];
                    for (uint32_t w : results[side][i]) snprintf(word, sizeof(word), " %x", w), line += word;
                    if (line.size() > 3000) line.resize(3000);
                    ctx.Fail("  %s:%s", side ? "ours" : "orig", line.c_str());
                }
                failed = true;
                break;
            }
        }
    }
}
