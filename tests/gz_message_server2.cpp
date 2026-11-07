// Differential test for the second message server (lane KU, src/gz/cGZMessageServer2.h), in the standalone shape.
//
// GZMessageServer2_Run: a server built by the original constructor 0xc66b80 on 0xc5-filled memory (every seventh seed by
// the original creator 0xc673ce, which hands out the +4 face) against one built by ours (placement new / Create), each
// driven through its own vtables by the same random operations, then destroyed: by the last Release (the service's,
// through the +0 face's shared thunk), or by the +8 face's slot 3 (thunk 0xc66e2b -> deleting destructor 0xc66e97 ->
// destructor 0xc66ce6) with flags 1 or 0.
// Operations, through the cIGZMessageServer2 face (+0, game vtable 0xe9e670) unless said: MessageSend 0xc676a8 (the
// pending changes applied first: 0xc67274; the message's type, then type 0), MessagePost 0xc67674 (urgent as a raw word),
// AddNotification 0xc66eb9 / RemoveNotification 0xc67db1 (both replaying the pending list: 0xc67204),
// GeneralMessagePostToTarget 0xc673fe, CancelGeneralMessagePostsToTarget 0xc6712f, OnTick 0xc67792 (+0 slot 9, and the
// +4 slot 10 thunk 0xc6785a; the general posts: 0xc67f61), GetMessageQueueSize 0xc675e0, SetAlwaysClearQueueOnTick
// (shared 0x4e7ed0, its only users this class's vtables) with raw words, RefCount (shared 0x47c6e0), CreateMessage
// 0xc671ae (cRZMessage2Standard's class id or another; the made message's QueryInterface with its interfaces or others),
// QueryInterface 0xc67b02 (+0; thunks 0xc67b24 on +4, 0xc67b2c on +8), Init 0xc6765a and Shutdown 0xc68051 (+4 face;
// the framework, cell 0x10c9378, a recording fake: slots 9 / 10), a mass registration of 1 to 40 targets on one type
// (the send's target vector through its growth steps), and the processed counter set near 2^32 so the tick's 64-bit stop
// count carries.
// Re-entrancy: the targets are recording fakes whose DoMessage may (within a budget, nesting up to three deep) send, post,
// register, unregister (themselves too), post to a target, cancel, tick or change the clear flag, and whose AddRef may
// post, register, unregister or change the flag (not touch the general posts: a target is counted inside the deque's
// copies). Release never re-enters, nor do the messages. Left out, as the original misbehaves there: a cancel that erases
// the last general post (its walk runs past the deque's end; the test simulates each cancel first and skips those), a
// null target or message, Init / Shutdown without a framework (the original does not check).
// Compared after every operation: the return value; every call the fakes saw, in order (every AddRef and Release the
// counted pointers make: the send's vector growth, the deque's moves); the scalar fields; the three locks' counts; the
// queue (built by 0xc66b0d on the original side, our cGZMessageQueue2 on ours: its vtable as a code, its messages, lock
// count, event handle, count word); the pending list; the registrations in the map's order; the general posts (the deque's map size, offset, size,
// each element); every target's and message's reference count. After destruction: the blocks freed (sorted sizes) and
// the counts.
// Every Win32 call is a logging fake in its import slot: CreateEventA 0xd9a244, CloseHandle 0xd9a17c, SetEvent 0xd9a3dc,
// 0xd9a10c. The game's operator new / delete (0x824632 / 0x824624) are ours; the made messages come from the game's
// allocator (svcfx::AllocatorScope; RZMemory 0x1c03b7 / ours, the constructor 0x1c033d on both sides).
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gzlog_support.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZMessageQueue.h"
#include "gz/cGZMessageServer2.h"

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

namespace msgserver2_test {

using svcfx::Blob;
using Server = cGZMessageServer2;

constexpr uint32_t kTag = 0x7d000000; // the server under test
constexpr unsigned kTargetCount = 48;
constexpr unsigned kMessageCount = 24;
constexpr uint32_t kBigType = 0x5000;
constexpr uint32_t kTypes[] = {0, 1, 7, 0x10, 0x11, 0x4fa845b, 0xffffffff, kBigType};

// ---- the targets and messages ----

struct Fake {
    void **vt;
    uint32_t index;
    int32_t refs;
    uint32_t type; // a message's
};
Fake gTargets[kTargetCount];
Fake gMessages[kMessageCount];
Blob gLog;
std::mt19937 gHandlerRng;
uint8_t *gObject; // the server the handlers re-enter
int gNest;
int gBudget;
bool gQuiet; // no re-entry (the mass registration)

uint32_t TN(uint32_t v, std::source_location where = std::source_location::current())
{
    const uint32_t lo = svcfx::P(&gTargets[0]), hi = svcfx::P(&gTargets[kTargetCount]);
    if (v >= lo && v < hi) return 0x7a000000 | ((v - lo) / sizeof(Fake)) << 4 | (v - lo) % sizeof(Fake);
    const uint32_t mlo = svcfx::P(&gMessages[0]), mhi = svcfx::P(&gMessages[kMessageCount]);
    if (v >= mlo && v < mhi) return 0x7b000000 | ((v - mlo) / sizeof(Fake)) << 4 | (v - mlo) % sizeof(Fake);
    return svcfx::NormOut(v, 0xabababab, where);
}
uint32_t TN(const void *p, std::source_location where = std::source_location::current()) { return TN(svcfx::P(p), where); }

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// The state, read through the same STL types (the game's layouts).
struct RawPending {
    bool add;
    void *target;
    uint32_t type;
};
struct RawPost {
    void *target;
    void *message;
};
using RawMap = std::unordered_map<uint32_t, std::list<void *> *>;
RawMap &MapOf(uint8_t *o) { return *reinterpret_cast<RawMap *>(o + 0x70); }
std::list<RawPending> &PendingOf(uint8_t *o) { return *reinterpret_cast<std::list<RawPending> *>(o + 0x68); }
std::deque<RawPost> &PostsOf(uint8_t *o) { return *reinterpret_cast<std::deque<RawPost> *>(o + 0x94); }
std::list<void *> &QueueListOf(uint8_t *o) { return *reinterpret_cast<std::list<void *> *>(o + 0x3c); }
static_assert(sizeof(RawMap) == sizeof(Server::TargetMap) && sizeof(std::deque<RawPost>) == sizeof(std::deque<Server::GeneralPost>));

// A cancel the original survives: no erased post is the last one at the time (see the file comment).
bool CancelSafe(uint8_t *o, uint32_t target)
{
    std::vector<uint32_t> v;
    for (const RawPost &p : PostsOf(o)) v.push_back(svcfx::P(p.target));
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

void ReEnter(std::mt19937 &rng, bool fromAddRef, const Fake *self = nullptr);
// A target's AddRef sometimes registers the target again on the type it was last registered with (Fake::type, set by
// every add of it). From ApplyPendingChanges (depth 0) that add runs while the pending add's list node is counted but
// not yet linked, so the presence check misses it and a list ends up holding the target twice: the first-copy erase of
// RemoveNotification and the every-copy removal of a pending remove then show. From AddNotification (depth 1) it is
// queued and replayed after the store.

uint32_t __fastcall FakeQuery(Fake *self, void *, uint32_t iid, void **out)
{
    gLog.push_back(0x71);
    gLog.push_back(TN(self));
    gLog.push_back(iid);
    *out = nullptr;
    return 0;
}
uint32_t __fastcall TargetAddRef(Fake *self, void *)
{
    gLog.push_back(0x72);
    gLog.push_back(self->index);
    gLog.push_back(uint32_t(gNest));
    const uint32_t refs = uint32_t(++self->refs);
    if (!gQuiet && gNest < 3 && gBudget > 0 && gHandlerRng() % 6 == 0) ReEnter(gHandlerRng, true, self);
    return refs;
}
uint32_t __fastcall FakeRelease(Fake *self, void *)
{
    gLog.push_back(0x73);
    gLog.push_back(TN(self));
    gLog.push_back(uint32_t(gNest));
    return uint32_t(--self->refs);
}
uint32_t __fastcall TargetDoMessage(Fake *self, void *, void *message)
{
    gLog.push_back(0x74);
    gLog.push_back(self->index);
    gLog.push_back(uint32_t(gNest));
    gLog.push_back(TN(message));
    if (!gQuiet && gNest < 3 && gBudget > 0 && gHandlerRng() % 3 == 0) ReEnter(gHandlerRng, false);
    return gHandlerRng() % 2 ? 0x101u : 0x100u;
}
uint32_t __fastcall MessageAddRef(Fake *self, void *)
{
    gLog.push_back(0x75);
    gLog.push_back(self->index);
    return uint32_t(++self->refs);
}
uint32_t __fastcall MessageGetType(Fake *self, void *)
{
    gLog.push_back(0x76);
    gLog.push_back(self->index);
    return self->type;
}
uint32_t __fastcall MessageUnexpected(Fake *self, void *)
{
    gLog.push_back(0x77);
    gLog.push_back(self->index);
    return 0;
}
void *gTargetVT[4] = {reinterpret_cast<void *>(&FakeQuery), reinterpret_cast<void *>(&TargetAddRef),
                      reinterpret_cast<void *>(&FakeRelease), reinterpret_cast<void *>(&TargetDoMessage)};
void *gMessageVT[8] = {reinterpret_cast<void *>(&FakeQuery),         reinterpret_cast<void *>(&MessageAddRef),
                       reinterpret_cast<void *>(&FakeRelease),       reinterpret_cast<void *>(&MessageUnexpected),
                       reinterpret_cast<void *>(&MessageGetType),    reinterpret_cast<void *>(&MessageUnexpected),
                       reinterpret_cast<void *>(&MessageUnexpected), reinterpret_cast<void *>(&MessageUnexpected)};

void ResetFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 3);
    for (uint32_t i = 0; i < kTargetCount; ++i) gTargets[i] = {gTargetVT, i, 1000, 0};
    for (uint32_t i = 0; i < kMessageCount; ++i) gMessages[i] = {gMessageVT, i, 1000, kTypes[rng() % 8]};
}

// ---- the server's calls, at the top level or from a handler ----

enum : unsigned {
    opSend, opPost, opAdd, opRemove, opGeneral, opCancel, opTick, opSize, opClear, opRefCount, opCreate, opQuery, opInit,
    opShutdown, opMassAdd, opProcessed, opCount
};

struct Op {
    unsigned kind;
    uint32_t a, b, c;
    uint32_t message;
};

Op RandomOp(std::mt19937 &rng, bool top)
{
    Op op = {};
    static const unsigned kWeights[opCount] = {14, 12, 16, 9, 9, 6, 9, 2, 3, 1, 2, 2, 1, 1, 2, 1};
    unsigned total = 0;
    for (unsigned k = 0; k < opCount; ++k) total += kWeights[k];
    unsigned pick = rng() % total;
    op.kind = 0;
    while (pick >= kWeights[op.kind]) pick -= kWeights[op.kind++];
    if (!top && op.kind >= opSize) op.kind = rng() % 2 ? opClear : opTick;
    op.message = rng() % kMessageCount;
    op.a = rng();
    op.b = rng() % 8 ? rng() % 16 : rng() % kTargetCount;
    op.c = kTypes[rng() % 8];
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
    case opCreate: {
        static const uint32_t kIids[] = {0xa52294b4, 0x65297976, 1, 0, 0x12345678};
        op.a = rng() % 4 ? Server::kStandardMessageClsid : rng() % 2 ? 0 : 0x46df45e6;
        op.c = kIids[rng() % 5];
        break;
    }
    case opQuery: {
        static const uint32_t kIids[] = {cIGZMessageServer2::kIid, 0xc1085722, 0, 1, 0x287fb697, 0x12345678};
        op.a = kIids[rng() % 6];
        op.b = rng() % 3;
        break;
    }
    case opTick: op.b = rng() % 2; break;
    case opInit:
    case opShutdown: op.c = rng() % 2 ? 0x101 : 0x100; break;
    case opMassAdd: {
        static const uint32_t kN[] = {1, 2, 3, 4, 5, 7, 10, 40};
        op.a = kN[rng() % 8];
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
    uint8_t *face = gObject;
    Fake *message = &gMessages[op.message];
    Fake *target = &gTargets[op.b];
    switch (op.kind) {
    case opSend: b.push_back(Slot<uint32_t>(face, 3, message) & 0xff); break;
    case opPost: b.push_back(Slot<uint32_t>(face, 4, message, op.a) & 0xff); break;
    case opAdd:
        target->type = op.c;
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
        b.push_back(op.kind == opGeneral ? Slot<uint32_t>(face, 7, message, target) : Slot<uint32_t>(face, 8, target));
        if (hold) lock->Unlock();
        break;
    }
    case opTick:
        b.push_back(op.b ? Slot<uint32_t>(face, 9, op.a) & 0xff : Slot<uint32_t>(Face(gObject, 4), 10, op.a) & 0xff);
        break;
    case opSize: b.push_back(Slot<uint32_t>(face, 10)); break;
    case opClear: Slot<void>(face, 11, op.a); break;
    case opRefCount: b.push_back(Slot<uint32_t>(face, 12)); break;
    case opCreate: {
        void *out = reinterpret_cast<void *>(uintptr_t(0xabababab));
        b.push_back(Slot<uint32_t>(face, 13, op.a, op.c, &out) & 0xff);
        if (out == reinterpret_cast<void *>(uintptr_t(0xabababab)) || !out) {
            b.push_back(svcfx::P(out));
            break;
        }
        // The made message: its vtable, count, flags and id words, then let go of (its own Release frees it).
        const auto *made = static_cast<const uint32_t *>(out);
        b.push_back(made[0] == svcfx::P(GameAddress<void>(0xda4064)) ? 1u : made[0]);
        b.push_back(made[1]);
        b.push_back(made[2] & 0xff);
        for (unsigned i = 3; i < 11; ++i) b.push_back(made[i]);
        b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    case opQuery: {
        static const uint32_t kFaces[] = {0, 4, 8};
        void *out = reinterpret_cast<void *>(uintptr_t(0xabababab));
        b.push_back(Slot<uint32_t>(Face(gObject, kFaces[op.b]), 0, op.a, &out) & 0xff);
        b.push_back(TN(out));
        break;
    }
    case opInit:
    case opShutdown:
        *GameAddress<void *>(0x10c9378) = &svcfx::F(0);
        svcfx::F(0).Set(9, "p", op.c);
        svcfx::F(0).Set(10, "p", op.c ^ 1);
        b.push_back(Slot<uint32_t>(Face(gObject, 4), op.kind == opInit ? 8 : 9) & 0xff);
        *GameAddress<void *>(0x10c9378) = nullptr;
        break;
    case opMassAdd:
        gQuiet = true;
        for (uint32_t i = 0; i < op.a; ++i) b.push_back(Slot<uint32_t>(face, 5, &gTargets[i], kBigType) & 0xff);
        gQuiet = false;
        break;
    case opProcessed:
        *reinterpret_cast<uint32_t *>(gObject + 0x58) = op.a;
        *reinterpret_cast<uint32_t *>(gObject + 0x5c) = op.b;
        break;
    default: break;
    }
}

void ReEnter(std::mt19937 &rng, bool fromAddRef, const Fake *self)
{
    --gBudget;
    ++gNest;
    Op op = RandomOp(rng, false);
    if (fromAddRef && (op.kind == opSend || op.kind == opTick || op.kind == opGeneral || op.kind == opCancel))
        op.kind = rng() % 2 ? opPost : opRemove;
    if (fromAddRef && self && rng() % 2 == 0) {
        op.kind = opAdd;
        op.b = self->index;
        op.c = self->type;
    }
    gLog.push_back(0x78);
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
    if (creator) {
        void *face = ours ? static_cast<void *>(Server::Create()) : ctx.Original<void *(__cdecl *)()>(0xc673ce)();
        return static_cast<uint8_t *>(face) - 4;
    }
    void *object = ::operator new(Server::kSize);
    std::memset(object, 0xc5, Server::kSize);
    if (ours) ::new (object) Server;
    else ctx.Original<void *(__thiscall *)(void *)>(0xc66b80)(object);
    return object;
}

uint32_t W(const uint8_t *o, uint32_t off) { return *reinterpret_cast<const uint32_t *>(o + off); }
uint32_t LockCount(const uint8_t *o, uint32_t off)
{
    const auto *lock = reinterpret_cast<const cRZCriticalSection *>(o + off);
    return uint32_t(lock->mBlock->lockCount);
}

uint32_t gVTables[2][3]; // each side's face vtables (+0, +4, +8)
uint32_t gQueueVTables[2]; // each side's queue vtable (+0x38)

void DumpCounts(Blob &b)
{
    for (const Fake &t : gTargets)
        if (t.refs != 1000) b.push_back(t.index << 16 | uint32_t(t.refs) & 0xffff);
    for (const Fake &m : gMessages)
        if (m.refs != 1000) b.push_back(0x8000 | m.index << 16 | uint32_t(m.refs) & 0xffff);
}

void Dump(Blob &b, uint8_t *o, int side)
{
    for (unsigned i = 0; i < 3; ++i) b.push_back(W(o, 4 * i) == gVTables[side][i] ? 1u : W(o, 4 * i));
    b.push_back(W(o, 0xc));
    b.push_back(W(o, 0x10));
    b.push_back(W(o, 0x14));
    b.push_back(W(o, 0x18));
    b.push_back(o[0x1c]);
    for (uint32_t off : {0x20u, 0x28u, 0x30u}) b.push_back(LockCount(o, off));
    // The queue (the game's on the original side, ours on ours: its vtable word as a code).
    b.push_back(W(o, 0x38) == gQueueVTables[side] ? 1u : W(o, 0x38));
    b.push_back(uint32_t(QueueListOf(o).size()));
    for (void *m : QueueListOf(o)) b.push_back(TN(m));
    b.push_back(LockCount(o, 0x44));
    b.push_back(W(o, 0x4c));
    const uint8_t *event = *reinterpret_cast<uint8_t *const *>(o + 0x50);
    b.push_back(W(event, 0));
    b.push_back(W(event, 4));
    b.push_back(W(o, 0x54));
    // The scalars.
    b.push_back(W(o, 0x58));
    b.push_back(W(o, 0x5c));
    b.push_back(o[0x60]);
    b.push_back(W(o, 0x64));
    b.push_back(W(o, 0x90));
    // The pending registrations.
    b.push_back(uint32_t(PendingOf(o).size()));
    for (const RawPending &c : PendingOf(o)) {
        b.push_back(c.add);
        b.push_back(TN(c.target));
        b.push_back(c.type);
    }
    // The registrations, in the map's order.
    RawMap &map = MapOf(o);
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
            for (void *t : *entry.second) b.push_back(TN(t));
    }
    // The general posts: the deque's map size, offset and size words, then each post.
    b.push_back(W(o, 0x9c));
    b.push_back(W(o, 0xa0));
    b.push_back(W(o, 0xa4));
    for (const RawPost &p : PostsOf(o)) {
        b.push_back(TN(p.target));
        b.push_back(TN(p.message));
    }
    DumpCounts(b);
}

} // namespace msgserver2_test

using namespace msgserver2_test;

DIFF_TEST(GZMessageServer2_Run, 0xc66b80)
{
    // Also reached (see the file comment): 0xc673ce (creator), 0xc66ce6, 0xc66e97, 0xc66e2b, 0xc67b02, 0xc67b24,
    // 0xc67b2c, 0xc6765a, 0xc68051, 0xc67792, 0xc6785a, 0xc676a8, 0xc67674, 0xc66eb9, 0xc67db1, 0xc673fe, 0xc6712f,
    // 0xc675e0, 0x4e7ed0, 0x47c6e0, 0xc671ae, 0xc67274, 0xc67204, 0xc67f61.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    globals.SetWord(0x10c9378, 0);
    globals.SetWord(0x1103050, 0);
    gzlog_test::Slots iat(ctx);
    iat.Set(0xd9a244, reinterpret_cast<void *>(&FakeCreateEventA));
    iat.Set(0xd9a17c, reinterpret_cast<void *>(&FakeCloseHandle));
    iat.Set(0xd9a3dc, reinterpret_cast<void *>(&FakeSetEvent));
    iat.Set(0xd9a10c, reinterpret_cast<void *>(&FakePulseEvent));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    gVTables[0][0] = svcfx::P(GameAddress<void>(0xe9e670));
    gVTables[0][1] = svcfx::P(GameAddress<void>(0xe9e6ac));
    gVTables[0][2] = svcfx::P(GameAddress<void>(0xe9e6e4));
    // The queue at +0x38: the game's cGZMessageQueue2 (vtable 0xe9e588) on the original side, ours (lane LR's, built by
    // its adapter) on ours; our queue's vtable read from a scratch queue.
    gQueueVTables[0] = svcfx::P(GameAddress<void>(0xe9e588));
    {
        alignas(4) uint8_t scratch[cGZMessageQueue2::kSize];
        cGZMessageQueue2 *const queue = cGZMessageQueueAdapters::Construct2(scratch, nullptr);
        gQueueVTables[1] = W(scratch, 0);
        cGZMessageQueueAdapters::Destruct2(queue, nullptr);
    }

    bool failed = false;
    for (uint32_t seed = 1; seed <= 1500 && !failed; ++seed) {
        std::mt19937 opRng(seed * 2654435761u + 11);
        const bool creator = seed % 7 == 0;
        const unsigned destroy = seed % 3;
        std::vector<Op> ops;
        const unsigned count = 10 + opRng() % 40;
        for (unsigned i = 0; i < count; ++i) ops.push_back(RandomOp(opRng, true));
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            ResetFakes(seed);
            gLog.clear();
            gEvents = 0;
            gHandlerRng.seed(seed);
            gNest = 0;
            gQuiet = false;
            Server::sReplayingPending = false;
            *GameAddress<uint8_t>(0x1103050) = 0;
            Blob first;
            gObject = static_cast<uint8_t *>(Build(ctx, side == 1, creator));
            if (side == 1)
                for (unsigned i = 0; i < 3; ++i) gVTables[1][i] = W(gObject, 4 * i);
            svcfx::AddRange(gObject, Server::kSize, kTag);
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
                        uint32_t refs = W(gObject, 0xc);
                        if (refs == 0) Slot<uint32_t>(gObject, 1), refs = 1;
                        for (uint32_t r = 0; r < refs; ++r) b.push_back(Slot<uint32_t>(gObject, 2));
                    } else {
                        Slot<void *>(Face(gObject, 8), 3, destroy == 1 ? 1u : 0u);
                        if (destroy == 2) ::operator delete(gObject);
                    }
                    frees.Stop();
                    size_t sizes[4096];
                    const size_t n = frees.SortedSizes(sizes, 4096);
                    b.push_back(uint32_t(n));
                    for (size_t k = 0; k < n && k < 4096; ++k) b.push_back(uint32_t(sizes[k]));
                    DumpCounts(b);
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
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "GZMessageServer2_Run", seed)) {
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
