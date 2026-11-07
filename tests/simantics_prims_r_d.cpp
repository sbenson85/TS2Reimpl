// Differential test for the SimAntics primitive handler of opcode 0x0d (0x5a0560, PrimRObject::Prim0D).
// Framework and notes: tests/simantics_prims_mid.h, tests/simantics_prims_r.h.
//
// The handler builds a cEdithObjectTestSim (ours now: constructor, destructor, Begin and the check-tree runner run for
// real on both sides, over fakes that play the check tree's world: see SetupCheckWorld) and asks the director's framework
// object (GetDirectorFrameworkObject, set up as in the Prim30 test) for a service; every other object is a recording fake.
#include "simantics_prims_r.h"

#include "simantics/SimTraceLog.h"

namespace {

int gWordCount = 0; // how many of the argument words the handler sets (frame slot 26's count, at most 4)

// Slot 0 of the caller person and of this: (iid, void **out) -> the scripted interface object (null: none).
uint32_t __fastcall QiPlain(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0x500)));
    return Scripted(self, 0x501);
}

// The director's slot 3: (class id, interface id, void **out).
uint32_t __fastcall Director3b(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 3u);
    gTrace.insert(gTrace.end(), {a1, a2, out ? 1u : 0u});
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0x500)));
    return Scripted(self, 3);
}

// This's slot 19 (one argument): answers differ per argument.
uint32_t __fastcall This19b(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(arg);
    return Mix(gSeed, 19, arg);
}

// The frame's slots 31 and 30 (one argument): per-argument answers.
template <int S> uint32_t __fastcall FrameArg(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
    gTrace.push_back(arg);
    return Mix(gSeed, S, arg);
}

// The interface's slot 147 (three out pointers): always writes all three.
uint32_t __fastcall Iface147(void *self, void *, uint32_t *a, uint32_t *b, uint32_t *c)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(147u << 8 | 3u);
    *a = Mix(gSeed, 147, 0);
    *b = Mix(gSeed, 147, 1);
    *c = (Mix(gSeed, 147, 2) % 3 == 0) ? Mix(gSeed, 147, 3) : 0xdead0000u + gHitCount % 4;
    return 0;
}

// The notifier's slot 27: either the argument words (a pointer into the handler's frame) or an object.
uint32_t __fastcall Notifier27(void *self, void *, const uint16_t *p)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(27u << 8 | 1u);
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    if ((v >= base && v < base + sizeof(World)) || v == 0) {
        gTrace.push_back(Norm(static_cast<uint32_t>(v)));
    } else {
        gTrace.push_back(0x2000u);
        for (int i = 0; i < gWordCount; ++i) gTrace.push_back(p[i]);
    }
    return Scripted(self, 27);
}

// ---- the world of the check-tree runner (cEdithObjectTestSim::RunCheckTree), played by the handler's fakes ------------------
// Roles: interaction = notifier (7), actor person interface = iface (4), table entry = record (6), stack object = entry (11),
// the actor's Sim object = inner2 (10), tree sim = own (8), settings singleton = ctx (0), trace sink = table (2), recorder =
// target (5). Slots used by the handler itself are left alone.

uint32_t gLogMode = 0; // 0: trace log off, 1: on without a sink, 2: on with the fake sink
uint32_t gCheckRuns = 0, gTreeRuns = 0; // check-tree runs that reached their verdict / ran the tree (both sides count)

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

uint32_t __fastcall WriteTextFake(void *self, void *, const char *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(14u << 8 | 1u);
    gTrace.push_back(text ? TextHash(text) : 0u);
    return Scripted(self, 14);
}

// The interaction's SetCheckPassed (slot 40): the low byte, and whether the upper three bytes are the interaction's own address.
uint32_t __fastcall SetCheckPassedFake(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    ++gCheckRuns;
    gTrace.push_back(40u << 8 | 1u);
    gTrace.push_back(arg & 0xffu);
    gTrace.push_back((arg >> 8) == (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)) >> 8) ? 1u : 0u);
    return Scripted(self, 40);
}

uint32_t __fastcall CallTreeFake(void *self, void *, uint32_t owner, uint32_t id1a, uint32_t treeId, uint32_t id18, uint32_t id1c,
                                 const uint16_t *args, uint32_t result, uint32_t flag16)
{
    Tick();
    gTrace.push_back(Off(self));
    ++gTreeRuns;
    gTrace.push_back(15u << 8 | 8u);
    for (uint32_t v : {owner, id1a, treeId, id18, id1c, result, flag16}) gTrace.push_back(Norm(v));
    for (int i = 0; i < 4; ++i) gTrace.push_back(args[i]);
    return Scripted(self, 15);
}

// Slots the test sim's noexcept destructor and its saved-arguments loop call: they log but never fail.
uint32_t __fastcall GetSimObjectFake(void *self, void *)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(8u << 8 | 0u);
    return Scripted(self, 8);
}
uint32_t __fastcall GetArgumentFake(void *self, void *, uint32_t index)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(index);
    return Scripted(self, 19);
}
uint32_t __fastcall SetArgumentFake(void *self, void *, uint32_t index, uint32_t value)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(20u << 8 | 2u);
    gTrace.push_back(index);
    gTrace.push_back(value);
    return Scripted(self, 20);
}

uint32_t Al(std::mt19937 &r, bool nonzero)
{
    const uint32_t high = r() & ~0xffu;
    return nonzero ? (high | (1 + r() % 255)) : high;
}

void SetupCheckWorld(World &w, std::mt19937 &r)
{
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    Fake &settings = w.fk[0], &sink = w.fk[2], &recorder = w.fk[5], &table = w.fk[6], &actor = w.fk[4], &interaction = w.fk[7],
         &treeSim = w.fk[8], &actorObject = w.fk[10], &stackObject = w.fk[11];

    // The interaction (the notifier): its id (slot 18) is the handler's, the rest is new.
    interaction.vt[40] = reinterpret_cast<void *>(&SetCheckPassedFake);
    for (int slot : {11, 16, 17}) Use(interaction.vt, slot, 0);
    for (int slot : {38, 42, 56, 75, 77}) Use(interaction.vt, slot, 1);
    RetValue(&interaction, 11, chance(50) ? 1u : static_cast<uint32_t>(r()), w);
    RetObject(&interaction, 17, chance(6) ? nullptr : &actor, w);
    RetObject(&interaction, 16, chance(6) ? nullptr : &stackObject, w);

    // The actor's person interface.
    actor.vt[8] = reinterpret_cast<void *>(&GetSimObjectFake);
    RetObject(&actor, 8, &actorObject, w);
    for (int slot : {9, 20, 30, 38, 52}) Use(actor.vt, slot, 0);
    RetObject(&actor, 9, &treeSim, w);
    RetValue(&actor, 20, r() % 17, w);
    RetValue(&actor, 30, Al(r, chance(35)), w);
    RetValue(&actor, 38, Al(r, chance(50)), w);
    RetValue(&actor, 52, chance(75) ? 0x1u << (r() % 6) : 0u, w);

    // The table entry (the handler's record fake, whose slots 13 and 16 stay).
    Use(table.vt, 11, 1);
    for (int slot : {18, 33, 39, 78}) Use(table.vt, slot, 0);
    RetValue(&table, 11, Al(r, chance(80)), w);
    RetValue(&table, 18, chance(75) ? 0xffffffffu : 0x1u << (r() % 6), w);
    RetValue(&table, 33, Al(r, chance(25)), w);
    RetValue(&table, 39, Al(r, chance(25)), w);
    RetValue(&table, 78, chance(15) ? (r() & 0xffff0000u) : (r() & 0xffff0000u) | (1 + r() % 0xffff), w);

    // The stack object.
    for (int slot : {47, 89, 92, 131, 135}) Use(stackObject.vt, slot, 0);
    for (int slot : {25, 190}) Use(stackObject.vt, slot, 1);
    RetValue(&stackObject, 47, (r() & 0xffff0000u) | (r() % 0x400), w);
    RetObject(&stackObject, 89, &treeSim, w);
    RetObject(&stackObject, 92, w.bytes, w);
    RetValue(&stackObject, 131, (r() & 0xffff0000u) | (r() % 3), w);
    RetValue(&stackObject, 135, Al(r, chance(12)), w);
    RetValue(&stackObject, 190, Al(r, chance(12)), w);
    RetValue(&stackObject, 25, Al(r, chance(60)), w);
    std::memcpy(w.bytes, "Stack Object", 13);

    // The actor's Sim object.
    Use(actorObject.vt, 13, 1);
    Use(actorObject.vt, 14, 2);
    actorObject.vt[19] = reinterpret_cast<void *>(&GetArgumentFake);
    actorObject.vt[20] = reinterpret_cast<void *>(&SetArgumentFake);
    Use(actorObject.vt, 131, 0);
    RetValue(&actorObject, 13, (r() & 0xffff0000u) | (chance(40) ? 0u : r() % 4), w);
    RetValue(&actorObject, 131, (r() & 0xffff0000u) | (r() % 3), w);

    // The tree sim, the settings (which are also the tuning object) and the target's key and recorder slots.
    treeSim.vt[15] = reinterpret_cast<void *>(&CallTreeFake);
    RetValue(&treeSim, 15, Al(r, chance(55)), w);
    Use(settings.vt, 3, 0);
    RetObject(&settings, 3, &settings, w);
    Use(settings.vt, 31, 0);
    RetValue(&settings, 31, Al(r, chance(60)), w);
    Use(settings.vt, 15, 1);
    RetValue(&settings, 15, chance(50) ? (r() & 0xffff0000u) : (r() & 0xffff0000u) | (1 + r() % 0xffff), w);
    Use(recorder.vt, 95, 0);
    RetValue(&recorder, 95, static_cast<uint32_t>(r()), w);
    Use(recorder.vt, 7, 1);
    Use(recorder.vt, 57, 4);
    uint16_t *record = reinterpret_cast<uint16_t *>(w.bytes + 0x20);
    const int16_t counters[] = {0, 1, -1, 5, -7, 32767, -32768};
    record[3] = static_cast<uint16_t>(counters[r() % 7]);
    record[4] = static_cast<uint16_t>(r());
    RetObject(&recorder, 57, chance(20) ? nullptr : static_cast<const void *>(record), w);

    // The trace sink.
    sink.vt[14] = reinterpret_cast<void *>(&WriteTextFake);
    Use(sink.vt, 9, 1);
    gLogMode = chance(35) ? (chance(70) ? 2u : 1u) : 0u;
}

void Build0D(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 404);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[2], &caller = w.fk[3], &iface = w.fk[4], &target = w.fk[5], &record = w.fk[6],
         &notifier = w.fk[7], &own = w.fk[8], &inner = w.fk[9], &inner2 = w.fk[10], &entry = w.fk[11], &owner = w.fk[13];
    w.obj.mOwner = &owner;

    // The parameter bytes: small numbers where they index, random bits elsewhere.
    w.param[5] = static_cast<uint8_t>(r() % 6);
    w.param[4] = static_cast<uint8_t>(r() % 6);
    w.param[6] = static_cast<uint8_t>(Pick(r, {0, 0, 1, 2, 3, 3, 4, 255}));
    if (r() % 3) w.param[7] &= static_cast<uint8_t>(r());   // fewer bits set
    if (r() % 3) w.param[0x12] &= static_cast<uint8_t>(r());
    w.param[8] = static_cast<uint8_t>(r() % 6);

    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    Use(w.vtThis, 14, 2);
    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, Wide16(r), w);
    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    QObj(29, (r() % 10 == 0) ? nullptr : &caller, w);
    QObj(29, (r() % 10 == 0) ? nullptr : &target, w);
    QObj(29, (r() % 3 == 0) ? nullptr : &entry, w);
    QObj(29, (r() % 3 == 0) ? nullptr : &entry, w);

    // The caller person and its interface.
    caller.vt[0] = reinterpret_cast<void *>(&QiPlain);
    RetObject(&caller, 0x500, (r() % 10 == 0) ? nullptr : &iface, w);
    Use(caller.vt, 98, 0);
    RetValue(&caller, 98, (r() % 8 == 0) ? static_cast<uint32_t>(r()) : 2u, w);
    Use(caller.vt, 13, 1);
    RetValue(&caller, 13, Wide16(r), w);
    Use(iface.vt, 2, 0);
    Use(iface.vt, 10, 2);
    RetValue(&iface, 10, (r() % 5 == 0) ? 0u : Wide16(r), w);
    Use(iface.vt, 132, 0);
    RetObject(&iface, 132, (r() % 6 == 0) ? nullptr : &inner, w);
    Use(iface.vt, 127, 0);
    RetValue(&iface, 127, Pick(r, {0, 4, 5, 6, 0xffff, 0x8000, static_cast<uint32_t>(r())}), w);
    Use(iface.vt, 126, 0);
    RetValue(&iface, 126, Pick(r, {0, 6, 7, 8, 0xffff, 0x8000, static_cast<uint32_t>(r())}), w);
    Use(iface.vt, 128, 1);
    RetValue(&iface, 128, (r() % 2) ? 0u : Pick(r, {1, 0x100, 0xff}), w);
    Use(iface.vt, 122, 1);
    RetValue(&iface, 122, (r() % 3 == 0) ? Pick(r, {0, 0x100}) : Pick(r, {1, 0xff}), w);
    Use(inner.vt, 19, 0);
    RetValue(&inner, 19, Pick(r, {0, 1, 0x10000, 0xffff, static_cast<uint32_t>(r())}), w);

    // The target.
    Use(target.vt, 61, 0);
    RetObject(&target, 61, (r() % 8 == 0) ? nullptr : &entry, w);
    Use(target.vt, 87, 1);
    RetObject(&target, 87, (r() % 8 == 0) ? nullptr : &record, w);
    Use(record.vt, 13, 1);
    RetValue(&record, 13, (r() % 6 == 0) ? 0u : static_cast<uint32_t>(r() | 1), w);
    Use(record.vt, 16, 0);
    RetValue(&record, 16, Pick(r, {0, 1, 0x100, 0xff}), w);

    // Frame cells.
    Use(frame.vt, 27, 0);
    Use(frame.vt, 26, 0);
    RetValue(&frame, 27, Pick(r, {0, 1, 2, 3, 4, 5, 6, 0x105, static_cast<uint32_t>(r())}), w);
    const uint32_t count26 = Pick(r, {0, 1, 2, 3, 4, 5, 6, 0x103, static_cast<uint32_t>(r())});
    RetValue(&frame, 26, count26, w);
    gWordCount = static_cast<int>((count26 & 0xff) < 4 ? (count26 & 0xff) : 4);
    frame.vt[31] = reinterpret_cast<void *>(&FrameArg<31>);
    frame.vt[30] = reinterpret_cast<void *>(&FrameArg<30>);

    // This.
    w.vtThis[0] = reinterpret_cast<void *>(&QiPlain);
    // The own-interface block that ends the handler dereferences the query's answer unchecked: a missing interface
    // goes with a state other than 2 (which skips that block).
    const bool ownMissing = r() % 5 == 0;
    RetObject(&w.obj, 0x500, ownMissing ? nullptr : &own, w);
    w.vtThis[19] = reinterpret_cast<void *>(&This19b);
    Use(w.vtThis, 98, 0);
    RetValue(&w.obj, 98, ownMissing ? 7u : ((r() % 4 == 0) ? 3u : 2u), w);
    Use(own.vt, 2, 0);
    Use(own.vt, 10, 2);
    RetValue(&own, 10, (r() % 4 == 0) ? 0u : Wide16(r), w);
    Use(own.vt, 132, 0);
    RetObject(&own, 132, &inner2, w);
    own.vt[147] = reinterpret_cast<void *>(&Iface147);
    Use(own.vt, 148, 0);
    RetObject(&own, 148, &record, w);
    Use(inner2.vt, 47, 0);
    RetValue(&inner2, 47, static_cast<uint32_t>(r()), w);
    // The notifier the director hands out.
    RetObject(&w.fk[12], 0x500, &notifier, w);
    w.fk[12].vt[3] = reinterpret_cast<void *>(&Director3b);
    Use(notifier.vt, 2, 0);
    Use(notifier.vt, 8, 5);
    Use(notifier.vt, 46, 1);
    notifier.vt[27] = reinterpret_cast<void *>(&Notifier27);
    Use(notifier.vt, 20, 0);
    RetValue(&notifier, 20, Pick(r, {0, 1, 0x10000, 0xffff, static_cast<uint32_t>(r())}), w);
    Use(notifier.vt, 39, 0);
    RetValue(&notifier, 39, (r() % 6 == 0) ? Pick(r, {0, 0x100}) : Pick(r, {1, 0xff}), w);
    Use(notifier.vt, 18, 0);
    RetValue(&notifier, 18, Pick(r, {0, 1, 0xffffffffu, 0xfffffffeu, 0x80000000u, static_cast<uint32_t>(r())}), w);
    Use(notifier.vt, 24, 0);
    RetObject(&notifier, 24, (r() % 6 == 0) ? nullptr : &record, w);
    Use(notifier.vt, 34, 1);
    Use(notifier.vt, 36, 1);
    Use(notifier.vt, 33, 0);
    RetValue(&notifier, 33, (r() % 3 == 0) ? Pick(r, {1, 0xff}) : Pick(r, {0, 0x100}), w);
    Use(notifier.vt, 65, 1);
    Use(notifier.vt, 72, 2);
    Use(notifier.vt, 71, 1);
    RetObject(&notifier, 71, (r() % 3 == 0) ? nullptr : &entry, w);
    Use(notifier.vt, 70, 1);
    Use(notifier.vt, 66, 0);
    RetValue(&notifier, 66, Wide16(r), w);
    Use(notifier.vt, 68, 0);
    RetValue(&notifier, 68, (r() % 3 == 0) ? Pick(r, {0, 0x10000}) : Wide16(r), w);
    Use(notifier.vt, 48, 1);
    Use(notifier.vt, 50, 1);
    Use(entry.vt, 69, 0);
    RetValue(&entry, 69, Wide16(r), w);
    Use(entry.vt, 70, 1);

    // The owner object (this + 0x48).
    Use(owner.vt, 53, 0);
    RetValue(&owner, 53, static_cast<uint32_t>(r()), w);
    Use(owner.vt, 63, 0);
    RetObject(&owner, 63, (r() % 4 == 0) ? nullptr : &target, w); // the recorder of the check trees
    SetupCheckWorld(w, r);

    // Operand reads: index, entry number, and the destination cell.
    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gReadQ = {Wide16(r), Wide16(r), Wide16(r)};
    gRead.hasTarget = r() % 8 != 0;
    gRead.cell = static_cast<int>(r() % 16);
    switch (r() % 3) {
    case 0: gRead.min = -32768; gRead.max = 32767; break;
    case 1: gRead.min = 0; gRead.max = 100; break;
    default: gRead.min = static_cast<int16_t>(r()); gRead.max = static_cast<int16_t>(r()); break;
    }
}

} // namespace

DIFF_TEST(PrimR_Prim0D, 0x5a0560)
{
    // The settings singleton (0x10cc288) and the trace log (0x10149e8) the check-tree runner reads.
    void *const savedSettings = *ctx.image.At<void *>(0x10cc288);
    uint8_t savedLog[0x30];
    std::memcpy(savedLog, ctx.image.At<uint8_t>(0x10149e8), sizeof(savedLog));

    // The director singleton in the image (as in the Prim30 test): its framework field points to the world's director.
    uint8_t *director = ctx.image.At<uint8_t>(0x10c9390);
    const uint32_t tlsIndex = *ctx.image.At<uint32_t>(0x10df814);
    int *tls = reinterpret_cast<int **>(__readfsdword(0x2c))[tlsIndex];
    const int savedEpoch = tls[1];
    const int savedGuard = *ctx.image.At<int>(0x10c93f4);
    uint8_t savedBytes[0x64];
    std::memcpy(savedBytes, director, sizeof(savedBytes));
    static void *fakeVTable[32];
    static struct {
        void *vtable;
        uint32_t pad;
    } fakeService;
    fakeVTable[23] = reinterpret_cast<void *>(+[](void *, void *) -> void * { return nullptr; });
    fakeService.vtable = fakeVTable;
    void *const savedService = *ctx.image.At<void *>(0x10c9378);
    *ctx.image.At<void *>(0x10c9378) = &fakeService;
    std::memset(director, 0xCD, 0x64);
    ctx.Original<void *(__thiscall *)(void *)>(0x144897)(director);
    *ctx.image.At<int>(0x10c93f4) = 5;
    tls[1] = 0x7fffffff;

    gBeforeRun = [&](World &w) {
        ResetQueues();
        *reinterpret_cast<void **>(director + 0x30) = &w.fk[12];
        *ctx.image.At<void *>(0x10cc288) = &w.fk[0];
        uint8_t *const log = ctx.image.At<uint8_t>(0x10149e8);
        log[0] = gLogMode != 0;
        log[1] = gLogMode != 0;
        *reinterpret_cast<int32_t *>(log + 4) = 3;
        *reinterpret_cast<void **>(log + 0x2c) = gLogMode == 2 ? static_cast<void *>(&w.fk[2]) : nullptr;
    };
    Stats st = RunHandler(ctx, "Prim0D", 0x5a0560, 60000, Build0D,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) {
                              return reinterpret_cast<PrimRObject *>(o)->Prim0D(f, p);
                          });
    gBeforeRun = nullptr;
    *ctx.image.At<void *>(0x10cc288) = savedSettings;
    std::memcpy(ctx.image.At<uint8_t>(0x10149e8), savedLog, sizeof(savedLog));
    tls[1] = savedEpoch;
    *ctx.image.At<int>(0x10c93f4) = savedGuard;
    std::memcpy(director, savedBytes, sizeof(savedBytes));
    *ctx.image.At<void *>(0x10c9378) = savedService;
    if (st.throws < 500 || st.byRet[0] < 1000 || st.byRet[1] < 300)
        ctx.Fail("coverage: %u throws, returns 0/1: %u/%u", st.throws, st.byRet[0], st.byRet[1]);
    if (gCheckRuns < 4000 || gTreeRuns < 200) ctx.Fail("coverage: check-tree runner reached its verdict %u times and ran the tree %u times", gCheckRuns, gTreeRuns);
}
