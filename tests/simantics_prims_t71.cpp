// Differential test for the SimAntics primitive handler of src/simantics/PrimT.h: opcode 0x71, SnapInto (0x5a6e40).
// Framework: tests/simantics_prims_mid.h, tests/simantics_prims_r.h, tests/simantics_prims_t.h.
//
// Everything is a recording fake except the framework message the handler posts (a real cRZMessage2Standard block
// from the allocator, its AddRef/Release/SetId/slot-12 slots recording) and the cached message server (a fake the
// global 0x10f519c points at). The original callees run as logging stand-ins on both sides (code redirected in the
// mapped image): the snap-state constructor 0x7f02f0 and destructor 0x7f04e0, the snap check 0x7e8aa0, the report
// builder 0x7f0ca0 and the string-table lookup 0x7f0c00. Their answers come from the world's script.
#include "game_allocator.h"
#include "simantics_prims_t.h"

#include "hook/hooks.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "simantics/SnapReport.h"

#include <cstdlib>

namespace {

int32_t gVbTable[3];

struct Report {
    uint32_t mCode;
    uint8_t mData[0xc];
    void *mObject;
    uint32_t mKind;
};

// The script of a stand-in: a value (or an object) kept on the context fake under a high slot number.
uint32_t StubValue(int key)
{
    return Scripted(&gW->fk[0], key);
}

void *StubObject(int key)
{
    return reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(&gW->fk[0], key)));
}

void __fastcall StubStateCtor(void *, void *)
{
    Tick();
    gTrace.push_back(0x7102u);
}

void __fastcall StubStateDtor(void *, void *)
{
    gTrace.push_back(0x7104u); // runs while errors unwind: never throws
}

uint32_t __cdecl StubSnapCheck(void *a, void *b, int32_t slot, void *state)
{
    Tick();
    gTrace.push_back(0x7e8au);
    gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))));
    gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b))));
    gTrace.push_back(static_cast<uint32_t>(slot));
    gTrace.push_back(state ? 1u : 0u);
    return StubValue(902); // al is the answer; the rest of eax is noise
}

void *__fastcall StubReport(void *state, void *, Report *out)
{
    Tick();
    gTrace.push_back(0x7f0cu);
    gTrace.push_back(state ? 1u : 0u);
    out->mCode = StubValue(903);
    out->mObject = StubObject(904);
    out->mKind = StubValue(907);
    return out;
}

uint32_t __fastcall StubEntry(void *report, void *, void **out)
{
    Tick();
    gTrace.push_back(0x7f0du);
    gTrace.push_back(report ? 1u : 0u);
    gTrace.push_back(static_cast<Report *>(report)->mKind);
    *out = StubObject(905);
    return StubValue(906);
}

// Slot 9 of the second object: (id, id, holder*); writes the object the script names into the holder.
uint32_t __fastcall Slot9(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(9u << 8 | 3u);
    gTrace.push_back(a);
    gTrace.push_back(b);
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 9)));
    return 1;
}

// The message server's slot 3: the message is heap memory, so only its presence is logged.
uint32_t __fastcall Server3(void *self, void *, const uint8_t *message)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(3u << 8 | 1u);
    gTrace.push_back(message ? 1u : 0u);
    return 0;
}

uint32_t __fastcall MsgAddRef(void *, void *)
{
    gTrace.push_back(0x4d01u);
    return 1;
}

uint32_t __fastcall MsgRelease(void *, void *)
{
    gTrace.push_back(0x4d02u);
    return 0;
}

void __fastcall MsgSetId(void *, void *, uint32_t id)
{
    gTrace.push_back(0x4d05u);
    gTrace.push_back(id);
}

void __fastcall MsgSlot12(void *, void *, uint32_t value)
{
    gTrace.push_back(0x4d0cu);
    gTrace.push_back(Norm(value));
}

void *__cdecl TestMalloc(uint32_t bytes)
{
    void *p = std::malloc(bytes);
    if (p) std::memset(p, 0xcd, bytes);
    return p;
}

const char gText[] = "snap text";

uint32_t NoiseAl(std::mt19937 &r, bool nonzero)
{
    const uint32_t high = r() & ~0xffu;
    return nonzero ? (high | (1 + r() % 255)) : high;
}

// A fake that answers the "who is it" questions the diagnostic walk asks (slot 61 the namer, slot 47 the id).
void StackLike(World &w, Fake &f, Fake &namer, std::mt19937 &r)
{
    Use(f.vt, 61, 0);
    Use(f.vt, 47, 0);
    Use(f.vt, 1, 0);
    Use(f.vt, 2, 0);
    RetObject(&f, 61, &namer, w);
    RetValue(&f, 47, Wide16(r), w);
}

void Build71(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 71);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[2], &objA = w.fk[3], &objB = w.fk[4], &query = w.fk[5], &base = w.fk[6],
         &entry = w.fk[7], &namedA = w.fk[8], &chainOwner = w.fk[9], &namer = w.fk[10], &namedB = w.fk[11], &holderObj = w.fk[12],
         &stackObj = w.fk[13];
    Fake &server = w.sub[0], &reportObj = w.sub[1];

    // The parameter: flag byte +0xf (bit 0 slot from this, bit 1 skip slot 174, bit 2 the query), slot byte +0xa.
    if (r() % 3 == 0) w.param[0xf] = static_cast<uint8_t>(r() & 7);

    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gReadQ = {Wide16(r), Wide16(r)};
    gRead.hasTarget = false;

    Use(w.vtThis, 19, 1);
    Use(w.vtThis, 14, 2);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    Use(ctx.vt, 31, 0);
    RetValue(&ctx, 31, NoiseAl(r, r() % 2 == 0), w);

    table.vt[30] = reinterpret_cast<void *>(&Q1<30>);
    QObj(30, (r() % 14 == 0) ? nullptr : &objA, w);
    QObj(30, (r() % 14 == 0) ? nullptr : &objB, w);
    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    QObj(29, &holderObj, w);
    QObj(29, (r() % 6 == 0) ? nullptr : &stackObj, w);

    // The stand-ins' script.
    RetValue(&ctx, 902, NoiseAl(r, r() % 100 < 45), w);
    RetValue(&ctx, 903, (r() % 6 == 0) ? Pick(r, {0xfffffffe, 0xffffffff, 0, 1}) : r() % 50, w);
    RetObject(&ctx, 904, (r() % 3 == 0) ? nullptr : &reportObj, w);
    RetValue(&ctx, 907, r(), w);
    RetObject(&ctx, 905, (r() % 4 == 0) ? nullptr : &entry, w);
    RetValue(&ctx, 906, NoiseAl(r, r() % 4 != 0), w);
    Use(reportObj.vt, 2, 0);

    // Object A: the interface query for the diagnostics.
    objA.vt[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&objA, 0, (r() % 4 == 0) ? nullptr : &namedA, w);

    // Object B.
    Use(objB.vt, 174, 3);
    objB.vt[9] = reinterpret_cast<void *>(&Slot9);
    RetObject(&objB, 9, (r() % 3 == 0) ? nullptr : &query, w);
    Use(objB.vt, 98, 0);
    RetValue(&objB, 98, (r() % 3 == 0) ? static_cast<uint32_t>(r()) : (r() % 2 ? 0x13u : (0x13u | (r() & 0xffffff00u | 0x100))), w);
    Use(objB.vt, 72, 0);
    RetValue(&objB, 72, NoiseAl(r, r() % 2 == 0), w);
    Use(objB.vt, 73, 0);
    RetValue(&objB, 73, Wide16(r), w);

    // The query result: slot 85, released through its virtual base (fk6 slot 2).
    Use(query.vt, 85, 0);
    query.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVbTable));
    Use(base.vt, 2, 0);

    // The string-table entry.
    Use(entry.vt, 7, 0);
    Use(entry.vt, 5, 0);
    Use(entry.vt, 2, 0);
    RetValue(&entry, 7, (r() % 3 == 0) ? 0u : r() | 1u, w);
    RetValue(&entry, 5, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gText)), w);

    // The interface objects and their chain.
    for (Fake *f : {&namedA, &namedB}) {
        Use(f->vt, 8, 0);
        Use(f->vt, 2, 0);
        RetObject(f, 8, &chainOwner, w);
    }
    Use(chainOwner.vt, 61, 0);
    Use(chainOwner.vt, 47, 0);
    RetObject(&chainOwner, 61, &namer, w);
    RetValue(&chainOwner, 47, Wide16(r), w);
    Use(namer.vt, 8, 0);
    RetValue(&namer, 8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gText)), w);
    holderObj.vt[0] = reinterpret_cast<void *>(&Qi0);
    RetObject(&holderObj, 0, (r() % 4 == 0) ? nullptr : &namedB, w);
    StackLike(w, holderObj, namer, r);
    StackLike(w, stackObj, namer, r);

    Use(frame.vt, 12, 0);
    RetValue(&frame, 12, Wide16(r), w);
    server.vt[3] = reinterpret_cast<void *>(&Server3);
}

} // namespace

DIFF_TEST(PrimT_Prim71, 0x5a6e40)
{
    CodePatch p1(ctx, 0x7f02f0, reinterpret_cast<void *>(&StubStateCtor));
    CodePatch p2(ctx, 0x7f04e0, reinterpret_cast<void *>(&StubStateDtor));
    CodePatch p3(ctx, 0x7e8aa0, reinterpret_cast<void *>(&StubSnapCheck));
    CodePatch p4(ctx, 0x7f0ca0, reinterpret_cast<void *>(&StubReport));
    CodePatch p5(ctx, 0x7f0c00, reinterpret_cast<void *>(&StubEntry));
    // The report builder and the message lookup are ours now (SnapStateReports::BuildReport, SnapErrorReport::LookupMessage) and
    // reached directly: the same stand-ins sit over them.
    CodePatch p7(MemberAddress(&SnapStateReports::BuildReport), reinterpret_cast<void *>(&StubReport));
    CodePatch p8(MemberAddress(&SnapErrorReport::LookupMessage), reinterpret_cast<void *>(&StubEntry));
    CodePatch p6(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc));
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    void **msgVtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
    void *savedSlots[4] = {msgVtable[1], msgVtable[2], msgVtable[5], msgVtable[12]};
    {
        DWORD old;
        VirtualProtect(msgVtable, 64, PAGE_READWRITE, &old);
    }
    msgVtable[1] = reinterpret_cast<void *>(&MsgAddRef);
    msgVtable[2] = reinterpret_cast<void *>(&MsgRelease);
    msgVtable[5] = reinterpret_cast<void *>(&MsgSetId);
    msgVtable[12] = reinterpret_cast<void *>(&MsgSlot12);
    void **server = ctx.image.At<void *>(0x10f519c);
    void *const savedServer = *server;

    gBeforeRun = [&](World &w) {
        ResetQueues();
        *server = &w.sub[0];
        gVbTable[0] = 0;
        gVbTable[1] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.fk[6]) - reinterpret_cast<uint8_t *>(&w.fk[5]) - 4);
    };
    Stats st = RunHandler(ctx, "Prim71", 0x5a6e40, 80000, Build71,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) { return SelfT(o)->Prim71(f, p); });
    gBeforeRun = nullptr;
    *server = savedServer;
    msgVtable[1] = savedSlots[0];
    msgVtable[2] = savedSlots[1];
    msgVtable[5] = savedSlots[2];
    msgVtable[12] = savedSlots[3];
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
    if (st.throws < 3000 || st.byRet[0] < 5000 || st.byRet[1] < 5000) ctx.Fail("coverage: %u throws, returns 0/1: %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}
