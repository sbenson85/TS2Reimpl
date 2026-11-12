// Differential tests of nGZSceneGraph's free helpers (src/sg/SGUtilities.h), part 3 (lane 261111c): the choreography
// queue helpers, on a few node fakes with queue and handler fakes.
//
//   SG_Utilities_CreateChoreoQueue   0xae2dc0
//   SG_Utilities_AttachChoreoQueue   0xae39c0 (AnimatedFaceOf 0xae1ed0, SetNodeTag 0xae78f0, CreateChoreoQueue: ours on
//                                    our side, the image's on the original's)
//   SG_Utilities_DetachChoreoQueue   0xae3c00
//
// Fakes: the framework (0x1ae18d stood in on both sides; slot 3 CreateInstance hands out spare queues and handlers by
// class, or fails without writing, as each seed's script says), nodes (sgfx::kNode: QueryInterface answers a camera
// face for 0x5ca76db6 on some nodes, slot 19 SetTag, 22 AddExtension and 23 RemoveExtension keep the node's queue and
// the queue's owner, 24 FindExtension answers the queue counted, or on a "dirty" node a counted stray queue with
// false), queues (a cIChoreoQueue face embedded in a fake, slots 0, 17, 40, 54, 59, with a vbptr at +4 to a
// cIExtension fake whose slot 18 answers the owner, alternating between two nodes on some queues), handlers (a face
// with only a vbptr at +0: the cIExtension fake and a second base fake) and anim managers (slots 6, 7, 15; slot 15
// answers a scripted index, negative on some). Every slot call is logged with its arguments; after each call the
// result and every count, link and owner are noted.
#include "sgfx.h"

#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"

#include <array>
#include <cstdlib>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kQueueClass = 0xdaff3bda, kQueueIid = 0x1aff3bd4;
constexpr uint32_t kAnimClass = 0xa9de6c9c, kAnimIid = 0xa9de6c97;
constexpr uint32_t kEventClass = 0xc8ebf859, kEventIid = 0xc8ebf854;
constexpr uint32_t kCameraIid = 0x5ca76db6;

constexpr unsigned kQueueSlotCount = 60;
constexpr std::array<Slot, kQueueSlotCount> QueueSlots()
{
    std::array<Slot, kQueueSlotCount> r{};
    for (unsigned i = 0; i < kQueueSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {1, Ret::Int, true, "Slot0", "u"};          // 0x9d5390, ret 4
    r[17] = {1, Ret::Int, true, "AddHandler", "i"};    // 0x9d22d0, ret 4
    r[40] = {1, Ret::Int, true, "Slot40", "u"};        // 0x9d3260, ret 4
    r[54] = {0, Ret::Int, true, "GetManager", ""};     // 0x9d29f0, ret
    r[59] = {3, Ret::Int, true, "FindHandler", "uii"}; // 0x9d4010, ret 0xc
    return r;
}
constexpr auto kQueueSlots = QueueSlots();
constexpr drawfx::Iface kQueue{"ChoreoQueue", kQueueSlotCount, kQueueSlots.data()};

constexpr unsigned kManagerSlotCount = 16;
constexpr std::array<Slot, kManagerSlotCount> ManagerSlots()
{
    std::array<Slot, kManagerSlotCount> r{};
    for (unsigned i = 0; i < kManagerSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[6] = {1, Ret::Int, true, "AddQueue", "i"};     // 0xa255f0, ret 4
    r[7] = {1, Ret::Int, true, "RemoveQueue", "i"};  // 0xa257c2, ret 4
    r[15] = {1, Ret::Int, true, "IndexOfQueue", "i"}; // 0xa25a20, ret 4
    return r;
}
constexpr auto kManagerSlots = ManagerSlots();
constexpr drawfx::Iface kManager{"AnimManager", kManagerSlotCount, kManagerSlots.data()};

constexpr Slot kFwSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "CreateInstance", "uui"}, // 3
};
constexpr drawfx::Iface kFw{"Framework", 4, kFwSlots};

// A handler: a face whose vbptr is at +0 (entry 1: the cIExtension, entry 2: the handler interface the queue takes).
struct HandlerFace {
    const int32_t *vbptr;
    int32_t vbtable[3];
};
struct Handler {
    HandlerFace face;
    Fake base;   // cIExtension (counted)
    Fake second; // what the queue's AddHandler gets
    uint32_t iid;
};
struct Queue {
    sgfx::TagFace face; // {vt, vbptr, vbtable}: the cIChoreoQueue face
    Fake faceFake;
    Fake base; // cIExtension (counted; slot 18 the owner)
    Handler *anim, *event;
    Fake *manager;
    bool dirtyAnim, dirtyEvent; // FindHandler misses that still write a counted stray handler
    bool swapOwner;             // slot 18 alternates between the owner and the other node
    unsigned ownerAsks;
};

constexpr unsigned kNodes = 2, kQueues = 8, kHandlers = 8, kManagers = 2; // queues: 2 + 5 calls' spares + the stray
enum : unsigned { kNodeQueue, kNodeDirty };

struct World {
    Fake nodes[kNodes];
    Fake cameras[kNodes];
    Queue queues[kQueues]; // 0, 1: may start attached; the rest spares for CreateInstance
    unsigned queuesUsed;   // spares handed out from 2
    Handler handlers[kHandlers]; // even: anim, odd: event; 0-3 may start on a queue, 4-7 spares
    unsigned animUsed, eventUsed;
    Fake managers[kManagers];
    Fake framework;
    bool failQueue, failAnim, failEvent;
} gw;

Queue *QueueOfFace(uint32_t face)
{
    for (Queue &q : gw.queues)
        if (drawfx::P(&q.face) == face) return &q;
    return nullptr;
}
Queue *QueueOfBase(uint32_t base)
{
    for (Queue &q : gw.queues)
        if (drawfx::P(&q.base) == base) return &q;
    return nullptr;
}
Queue &QueueOfFake(Fake &f)
{
    for (Queue &q : gw.queues)
        if (&q.faceFake == &f || &q.base == &f) return q;
    std::abort();
}
Handler *HandlerOfSecond(uint32_t second)
{
    for (Handler &h : gw.handlers)
        if (drawfx::P(&h.second) == second) return &h;
    return nullptr;
}

// An identity for the notes: 0 null, 1xx a node, 2xx a queue face, 3xx a handler face, 4xx a manager, else 0xbad.
uint32_t Who(const void *p)
{
    const uint32_t v = drawfx::P(p);
    if (!v) return 0;
    for (unsigned i = 0; i < kNodes; ++i)
        if (v == drawfx::P(&gw.nodes[i])) return 100 + i;
    for (unsigned i = 0; i < kQueues; ++i)
        if (v == drawfx::P(&gw.queues[i].face)) return 200 + i;
    for (unsigned i = 0; i < kHandlers; ++i)
        if (v == drawfx::P(&gw.handlers[i].face)) return 300 + i;
    for (unsigned i = 0; i < kManagers; ++i)
        if (v == drawfx::P(&gw.managers[i])) return 400 + i;
    return 0xbad;
}
template <class T> uint32_t WhoWord(T v) { return Who(reinterpret_cast<const void *>(uintptr_t(v))); }

// ---- nodes ----
// Slot 24 (iid, out, start): the node's queue counted; a dirty node without one writes a counted stray queue (the
// last spare) and answers false.
uint32_t NodeFind(Fake &f, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    if (a[0] != kQueueIid) return 0;
    if (Queue *q = QueueOfFace(f.value[kNodeQueue])) {
        ++q->base.refs;
        *out = &q->face;
        return 1;
    }
    if (f.value[kNodeDirty]) {
        Queue &stray = gw.queues[kQueues - 1];
        ++stray.base.refs;
        *out = &stray.face;
    }
    return 0;
}
// Slot 22 AddExtension(base): a queue's base becomes the node's queue (counted), the node its owner.
uint32_t NodeAdd(Fake &f, const uint32_t *a)
{
    Queue *q = QueueOfBase(a[0]);
    if (!q) return 0;
    ++q->base.refs;
    q->base.value[sgfx::kExtOwnerWord] = drawfx::P(&f);
    f.value[kNodeQueue] = drawfx::P(&q->face);
    return 0;
}
// Slot 23 RemoveExtension(base): the node's queue dropped when it is that one.
uint32_t NodeRemove(Fake &f, const uint32_t *a)
{
    Queue *q = QueueOfFace(f.value[kNodeQueue]);
    if (!q || drawfx::P(&q->base) != a[0]) return 0;
    --q->base.refs;
    q->base.value[sgfx::kExtOwnerWord] = 0;
    f.value[kNodeQueue] = 0;
    return 0;
}

// ---- queues ----
uint32_t QueueFind(Fake &f, const uint32_t *a)
{
    Queue &q = QueueOfFake(f);
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    const bool anim = a[0] == kAnimIid;
    Handler *h = anim ? q.anim : a[0] == kEventIid ? q.event : nullptr;
    if (h) {
        ++h->base.refs;
        *out = &h->face;
        return 1;
    }
    if (anim ? q.dirtyAnim : q.dirtyEvent) { // a stray handler of the other kind, counted
        Handler &stray = gw.handlers[anim ? 3 : 2];
        ++stray.base.refs;
        *out = &stray.face;
    }
    return 0;
}
uint32_t QueueAdd(Fake &f, const uint32_t *a)
{
    Queue &q = QueueOfFake(f);
    if (Handler *h = HandlerOfSecond(a[0])) {
        ++h->base.refs;
        (h->iid == kAnimIid ? q.anim : q.event) = h;
    }
    return 0;
}
uint32_t QueueManager(Fake &f, const uint32_t *) { return drawfx::P(QueueOfFake(f).manager); }
// Slot 18 on the queue's cIExtension: the owner; a swapping queue answers the other node every second time.
uint32_t QueueOwner(Fake &f, const uint32_t *)
{
    Queue &q = QueueOfFake(f);
    const uint32_t owner = q.base.value[sgfx::kExtOwnerWord];
    if (!q.swapOwner || !owner || (q.ownerAsks++ % 2) == 0) return owner;
    return owner == drawfx::P(&gw.nodes[0]) ? drawfx::P(&gw.nodes[1]) : drawfx::P(&gw.nodes[0]);
}

// ---- managers ----
uint32_t ManagerIndex(Fake &f, const uint32_t *) { return f.value[0]; }

// ---- framework ----
// Slot 3 (class, iid, out): a spare by class, counted, or a failure without writing.
uint32_t Create(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (a[0] == kQueueClass) {
        if (gw.failQueue || gw.queuesUsed == kQueues - 1) return 0; // the last queue is the stray
        Queue &q = gw.queues[gw.queuesUsed++];
        ++q.base.refs;
        *out = &q.face;
        return 1;
    }
    const bool anim = a[0] == kAnimClass;
    if (!anim && a[0] != kEventClass) return 0;
    unsigned &used = anim ? gw.animUsed : gw.eventUsed;
    if ((anim ? gw.failAnim : gw.failEvent) || used >= kHandlers) return 0;
    Handler &h = gw.handlers[used];
    used += 2;
    ++h.base.refs;
    *out = &h.face;
    return 1;
}
void *StandFramework() { return &gw.framework; }

void MakeQueue(Queue &q, uint32_t id)
{
    sgfx::MakeExtension(q.base, id);
    q.base.On(18, &QueueOwner);
    q.faceFake.Reset<kQueue>(id + 1000);
    q.face.vbptr = q.face.vbtable;
    q.face.vbtable[0] = -4;
    q.face.vbtable[1] = int32_t(drawfx::P(&q.base) - drawfx::P(&q.face.vbptr));
    q.faceFake.Embed(&q.face);
    q.faceFake.On(17, &QueueAdd);
    q.faceFake.On(54, &QueueManager);
    q.faceFake.On(59, &QueueFind);
    drawfx::AddRange(&q.face, sizeof(q.face), 0x7d000000 | (id << 8));
    q.anim = q.event = nullptr;
    q.manager = nullptr;
    q.dirtyAnim = q.dirtyEvent = q.swapOwner = false;
    q.ownerAsks = 0;
}

void MakeHandler(Handler &h, uint32_t id, uint32_t iid)
{
    sgfx::MakeExtension(h.base, id);
    h.second.Reset<sgfx::kUnknown>(id + 1000);
    h.iid = iid;
    h.face.vbptr = h.face.vbtable;
    h.face.vbtable[0] = 0;
    h.face.vbtable[1] = int32_t(drawfx::P(&h.base) - drawfx::P(&h.face));
    h.face.vbtable[2] = int32_t(drawfx::P(&h.second) - drawfx::P(&h.face));
    drawfx::AddRange(&h.face, sizeof(h.face), 0x7c000000 | (id << 8));
}

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 3u);
    gw.framework.Reset<kFw>(80);
    gw.framework.On(3, &Create);
    for (unsigned i = 0; i < kManagers; ++i) {
        gw.managers[i].Reset<kManager>(90 + i);
        gw.managers[i].On(15, &ManagerIndex);
        const uint32_t indices[] = {0, 1, 7, 0xffffffffu, 0x80000000u, 0x7fffffffu};
        gw.managers[i].value[0] = indices[rng() % 6];
    }
    for (unsigned i = 0; i < kHandlers; ++i) MakeHandler(gw.handlers[i], 300 + i, i % 2 ? kEventIid : kAnimIid);
    gw.animUsed = 4, gw.eventUsed = 5;
    for (unsigned i = 0; i < kQueues; ++i) {
        Queue &q = gw.queues[i];
        MakeQueue(q, 200 + i);
        if (rng() % 3) q.anim = &gw.handlers[(i % 2) * 2];
        if (rng() % 3) q.event = &gw.handlers[(i % 2) * 2 + 1];
        q.manager = rng() % 4 ? &gw.managers[rng() % kManagers] : nullptr;
        q.dirtyAnim = rng() % 3 == 0;
        q.dirtyEvent = rng() % 3 == 0;
        q.swapOwner = rng() % 3 == 0;
    }
    gw.queuesUsed = 2;
    for (unsigned i = 0; i < kNodes; ++i) {
        Fake &n = gw.nodes[i];
        n.Reset<sgfx::kNode>(100 + i);
        gw.cameras[i].Reset<sgfx::kUnknown>(110 + i);
        if (rng() % 2) n.AddFace(kCameraIid, gw.cameras[i]);
        n.On(22, &NodeAdd);
        n.On(23, &NodeRemove);
        n.On(24, &NodeFind);
        n.value[kNodeDirty] = rng() % 3 == 0;
        if (rng() % 2) { // starts with queue i attached
            Queue &q = gw.queues[i];
            q.base.value[sgfx::kExtOwnerWord] = drawfx::P(&n);
            n.value[kNodeQueue] = drawfx::P(&q.face);
        }
    }
}

// Every count, link and owner.
void NoteState()
{
    for (unsigned i = 0; i < kNodes; ++i)
        drawfx::Note("node", {gw.nodes[i].id, uint32_t(gw.nodes[i].refs), WhoWord(gw.nodes[i].value[kNodeQueue]),
                              uint32_t(gw.cameras[i].refs)});
    for (const Queue &q : gw.queues)
        drawfx::Note("queue", {q.base.id, uint32_t(q.base.refs), WhoWord(q.base.value[sgfx::kExtOwnerWord]),
                               Who(q.anim ? &q.anim->face : nullptr), Who(q.event ? &q.event->face : nullptr)});
    for (const Handler &h : gw.handlers) drawfx::Note("handler", {h.base.id, uint32_t(h.base.refs)});
}

// Per call: the framework's failures and fresh manager indices drawn from the run's stream (alike on both sides).
void Script(std::mt19937 &rng)
{
    gw.failQueue = rng() % 4 == 0;
    gw.failAnim = rng() % 4 == 0;
    gw.failEvent = rng() % 4 == 0;
    drawfx::Note("script", {gw.failQueue, gw.failAnim, gw.failEvent});
}

constexpr unsigned kSeeds = 96;

template <class Call> void Check(difftest::Context &ctx, const char *what, Call &&call)
{
    sgfx::CheckSides(ctx, what, kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const unsigned calls = 1 + rng() % 5;
                         for (unsigned k = 0; k < calls; ++k) {
                             Script(rng);
                             call(isOriginal, rng);
                             NoteState();
                         }
                     });
}

cICompositionTreeNode *AsNode(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }

} // namespace

DIFF_TEST(SG_Utilities_CreateChoreoQueue, 0xae2dc0)
{
    using Fn = void *(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae2dc0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    Check(ctx, "CreateChoreoQueue", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = gw.nodes[rng() % kNodes];
        drawfx::Note("call", node.id);
        drawfx::Note("queue", Who(isOriginal ? original(&node) : CreateChoreoQueue(AsNode(node))));
    });
}

DIFF_TEST(SG_Utilities_AttachChoreoQueue, 0xae39c0)
{
    using Fn = void *(__cdecl *)(void *, void *);
    const Fn original = ctx.Original<Fn>(0xae39c0);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    Check(ctx, "AttachChoreoQueue", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = gw.nodes[rng() % kNodes];
        Fake &manager = gw.managers[rng() % kManagers];
        // A node without a queue gets a new one: its creation must work (a failed one is called through null on both
        // sides: 0xae3a69, 0xae3a78).
        if (!node.value[kNodeQueue]) gw.failQueue = false;
        drawfx::Note("call", {node.id, manager.id, gw.failQueue});
        drawfx::Note("queue", Who(isOriginal ? original(&node, &manager) : AttachChoreoQueue(AsNode(node), &manager)));
    });
}

DIFF_TEST(SG_Utilities_DetachChoreoQueue, 0xae3c00)
{
    using Fn = void(__cdecl *)(void *);
    const Fn original = ctx.Original<Fn>(0xae3c00);
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    Check(ctx, "DetachChoreoQueue", [&](bool isOriginal, std::mt19937 &rng) {
        Fake &node = gw.nodes[rng() % kNodes];
        // Now and then a queue first, so that new queues (owned, with a manager or none) are taken down too.
        if (rng() % 3 == 0) {
            if (!node.value[kNodeQueue]) gw.failQueue = false;
            Fake &manager = gw.managers[rng() % kManagers];
            drawfx::Note("attach", {node.id, manager.id});
            if (isOriginal)
                ctx.Original<void *(__cdecl *)(void *, void *)>(0xae39c0)(&node, &manager);
            else
                AttachChoreoQueue(AsNode(node), &manager);
            NoteState();
        }
        drawfx::Note("call", node.id);
        if (isOriginal)
            original(&node);
        else
            DetachChoreoQueue(AsNode(node));
    });
}
