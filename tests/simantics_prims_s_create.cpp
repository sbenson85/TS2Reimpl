// Opcode 0x2a, CreateObject (0x585a60). Framework: tests/simantics_prims_s.h.
//
// The pure helpers (0x45c160, 0x5795f0, 0x7dcb60, 0x7dcc50, 0x7dcde0), the cached service getters (0x20e0b2, 0x9c5a89)
// and the message constructor (0x1c033d) are original code that runs on both sides. The placement helpers that need
// a world of their own (0x7e3690, 0x546af0, 0x7e8aa0, 0x7e8c10, 0x5416a0) are redirected to recording stubs in the
// mapped image, as are the allocator (0x1be3b0) and the message object's AddRef/Release/SetId/slot-12 slots.
#include "game_allocator.h"
#include "simantics_prims_s.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"

#include <windows.h>

namespace {

// ---- the stubs --------------------------------------------------------------------------------------------------------

std::vector<uint32_t> gOthers; // world offsets of the objects 0x7e3690 hands out (0xffffffff: none)
size_t gOtherPos = 0;
std::vector<uint32_t> gBools;
size_t gBoolPos = 0;

uint32_t NextBool()
{
    return gBoolPos < gBools.size() ? gBools[gBoolPos++] : 0u;
}

void *__cdecl Stub7e3690(int32_t index, const int32_t *pos, int32_t one)
{
    Tick();
    gTrace.insert(gTrace.end(), {0x7e3690u, static_cast<uint32_t>(index), static_cast<uint32_t>(pos[0]), static_cast<uint32_t>(pos[1]),
                                 static_cast<uint32_t>(pos[2]), static_cast<uint32_t>(one)});
    const uint32_t e = gOtherPos < gOthers.size() ? gOthers[gOtherPos++] : 0xffffffffu;
    return e == 0xffffffffu ? nullptr : reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(gW) + e);
}

uint8_t __cdecl Stub546af0(const uint8_t *bytes, int32_t direction)
{
    Tick();
    gTrace.insert(gTrace.end(), {0x546af0u, bytes[0], bytes[1], bytes[2], static_cast<uint32_t>(direction)});
    return static_cast<uint8_t>(NextBool());
}

uint8_t __cdecl Stub7e8aa0(void *object, void *source, uint32_t dword, uint32_t zero)
{
    Tick();
    gTrace.insert(gTrace.end(), {0x7e8aa0u, Norm(reinterpret_cast<uint32_t>(object)), Norm(reinterpret_cast<uint32_t>(source)), dword, zero});
    return static_cast<uint8_t>(NextBool());
}

uint8_t __cdecl Stub7e8c10(void *object, const uint32_t *position, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Tick();
    gTrace.insert(gTrace.end(), {0x7e8c10u, Norm(reinterpret_cast<uint32_t>(object)), position[0], position[1], position[2], a, b, c, d});
    return static_cast<uint8_t>(NextBool());
}

void __stdcall Stub5416a0(void *object, const uint32_t *position, uint32_t one)
{
    Tick();
    gTrace.insert(gTrace.end(), {0x5416a0u, Norm(reinterpret_cast<uint32_t>(object)), position[0], position[1], position[2], one});
}

void *__cdecl TestMalloc(uint32_t size)
{
    void *p = HeapAlloc(GetProcessHeap(), 0, size);
    std::memset(p, 0xcd, size);
    return p;
}

// The message object's slots 1, 2, 5 and 12 (patched into the game's vtable for the test).
uint32_t __fastcall MsgAddRef(void *, void *)
{
    return 1;
}
uint32_t __fastcall MsgRelease(void *, void *)
{
    gTrace.push_back(0x1c0804u);
    return 0;
}
void __fastcall MsgSetId(void *, void *, uint32_t id)
{
    gTrace.insert(gTrace.end(), {0x1c0915u, id});
}
void __fastcall MsgSlot12(void *, void *, void *object)
{
    gTrace.insert(gTrace.end(), {0xc012u, Norm(reinterpret_cast<uint32_t>(object))});
}

// ---- recorders with special shapes ---------------------------------------------------------------------------------------

// A slot that fills a position (x, y) from the script: (SimFixedPoint *out).
void __fastcall WritePos34(void *self, void *, SimFixedPoint *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(34u << 8 | 1u);
    out->mX = static_cast<int32_t>(Scripted(self, 1034));
    out->mY = static_cast<int32_t>(Scripted(self, 1035));
    gTrace.push_back(static_cast<uint32_t>(out->mX));
    gTrace.push_back(static_cast<uint32_t>(out->mY));
}

// The maker's slot 16: (definition, flag dword, 0, 0): the flag's upper bytes are stack leftovers in the original, so
// only its low byte is recorded.
uint32_t __fastcall Maker16(void *self, void *, void *definition, uint32_t flag, uint32_t a, uint32_t b)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(16u << 8 | 4u);
    gTrace.insert(gTrace.end(), {Norm(reinterpret_cast<uint32_t>(definition)), flag & 0xff, a, b});
    return Scripted(self, 16);
}

// slot 170: (int *facing, one): records the value the pointer holds.
uint32_t __fastcall Slot170(void *self, void *, const uint32_t *facing, uint32_t one)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(170u << 8 | 2u);
    gTrace.insert(gTrace.end(), {*facing, one});
    return Scripted(self, 170);
}

// slot 171: (float position *, 0, 1): records the three floats.
uint32_t __fastcall Slot171(void *self, void *, const uint32_t *position, uint32_t a, uint32_t b)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(171u << 8 | 3u);
    gTrace.insert(gTrace.end(), {position[0], position[1], position[2], a, b});
    return Scripted(self, 171);
}

// The verifier's slot 80: (float position *, 2, 1): records the floats.
uint32_t __fastcall Verifier80(void *self, void *, const uint32_t *position, uint32_t a, uint32_t b)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(80u << 8 | 3u);
    gTrace.insert(gTrace.end(), {position[0], position[1], position[2], a, b});
    return Scripted(self, 80);
}

// The placement service's slots 65 (count of objects at a spot) and 71 (is the spot free): (tile position *).
uint32_t __fastcall Placement65(void *self, void *, const int32_t *tile)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.insert(gTrace.end(), {65u << 8 | 1u, static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]), static_cast<uint32_t>(tile[2])});
    return Scripted(self, 65);
}
uint32_t __fastcall Placement71(void *self, void *, const int32_t *tile)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.insert(gTrace.end(), {71u << 8 | 1u, static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]), static_cast<uint32_t>(tile[2])});
    return Scripted(self, 71);
}

// The cached dispatcher's slot 3: (message *): records the message's fields, not its heap address.
uint32_t __fastcall Dispatch3(void *self, void *, const cRZMessage2Standard *message)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.insert(gTrace.end(), {3u << 8 | 1u, message->mRefCount, message->mFlags, message->mId, message->mValue[0]});
    return Scripted(self, 3);
}

// ---- the world -----------------------------------------------------------------------------------------------------------

void SetCreateGlobals(World &w)
{
    *GameAddress<void *>(0x10cc8ec) = &w.fk[3];
    *GameAddress<void *>(0x10cc298) = &w.fk[8];
    *GameAddress<void *>(0x10f519c) = &w.fk[9];
    gReadPos = 0;
    gOtherPos = 0;
    gBoolPos = 0;
}

void Build2A(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 113);
    Base(w, seed, r);
    SetParam(w, r);
    Fake &ctx = w.fk[0], &frame = w.fk[1], &registry = w.fk[2], &misc = w.fk[3], &list = w.fk[4], &def = w.fk[5], &table = w.fk[6], &t = w.fk[7],
         &placement = w.fk[8], &verifier = w.fk[9], &maker = w.fk[10], &bound = w.fk[11], &k = w.fk[12];

    // the parameter bytes
    const uint32_t kind = Pick(r, {0, 1, 2, 3, 3, 4, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 0x80, 0xff});
    w.param[8] = static_cast<uint8_t>(kind);
    w.param[9] = static_cast<uint8_t>(r());
    w.param[0xa] = static_cast<uint8_t>(Pick(r, {0, 1, 2, 3, 0x7f, 0x80, 0xff, static_cast<uint32_t>(r())}));
    w.param[0xd] = static_cast<uint8_t>(r());
    w.param[0xe] = static_cast<uint8_t>(r());
    const uint8_t flags = w.param[9];
    const uint8_t flagsE = w.param[0xe];

    gReadVals.clear();
    gReadPos = 0;
    gReadVals.push_back(ReadWord(r));
    w.vtSec[7] = reinterpret_cast<void *>(&ReadSeq);

    // positions (this's and the target's): random fixed-point values
    for (int i = 0; i < 2; ++i) {
        w.pos[i].mX = static_cast<int32_t>(r() % 3 ? (r() & 0x00ff0000u) | (r() & 0xffffu) : r());
        w.pos[i].mY = static_cast<int32_t>(r() % 3 ? (r() & 0x00ff0000u) | (r() & 0xffffu) : r());
    }

    // this
    Use(w.vtThis, 13, 1);
    Use(w.vtThis, 19, 1);
    Use(w.vtThis, 36, 0);
    Use(w.vtThis, 47, 0);
    Use(w.vtThis, 63, 0);
    w.vtThis[33] = reinterpret_cast<void *>(&Pos33);
    RetValue(&w.obj, 13, ReadWord(r), w);
    RetValue(&w.obj, 19, ReadWord(r), w);
    RetValue(&w.obj, 36, ReadWord(r), w);
    RetValue(&w.obj, 47, ReadWord(r), w);
    RetObject(&w.obj, 63, &maker, w);
    RetObject(&w.obj, 33, &w.pos[0], w);
    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&w.obj) + 0x1c0) = &misc;

    // the context
    Use(ctx.vt, 10, 0);
    Use(ctx.vt, 24, 0);
    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 10, &registry, w);
    RetObject(&ctx, 24, &list, w);
    RetObject(&ctx, 26, &table, w);

    // the frame
    Use(frame.vt, 12, 0);
    Use(frame.vt, 13, 1);
    Use(frame.vt, 26, 0);
    Use(frame.vt, 27, 0);
    Use(frame.vt, 30, 1);
    Use(frame.vt, 31, 1);
    RetValue(&frame, 12, static_cast<uint32_t>(r()), w);
    RetValue(&frame, 26, Pick(r, {0, 1, 2, 5}), w);
    RetValue(&frame, 27, Pick(r, {0, 1, 2, 3, 4}), w);
    RetValue(&frame, 30, static_cast<uint32_t>(r()), w);
    RetValue(&frame, 31, static_cast<uint32_t>(r()), w);

    // the definition: found through the registry (bit 2), this's holder (bit 6) or the list
    Use(registry.vt, 16, 2);
    Use(misc.vt, 10, 0);
    Use(misc.vt, 15, 0); // no arguments: the 1 pushed before it is slot 9's second argument
    Use(misc.vt, 9, 2);
    Use(misc.vt, 6, 0);
    Use(misc.vt, 3, 1);
    Use(misc.vt, 27, 0);
    Use(list.vt, 14, 1);
    RetObject(&registry, 16, (r() % 6 == 0) ? nullptr : &misc, w);
    RetObject(&misc, 10, (r() % 6 == 0) ? nullptr : &def, w);
    RetValue(&misc, 15, static_cast<uint32_t>(r()), w);
    RetValue(&misc, 6, static_cast<uint32_t>(r()), w);
    RetValue(&misc, 3, Pick(r, {0, 0, 0x10, 0x11, 0x11, 5, 9, 0x100, static_cast<uint32_t>(r())}), w);
    RetObject(&misc, 27, (r() % 6 == 0) ? nullptr : &def, w);
    if (flags & 0x40 && !(flags & 4)) {
        RetObject(&list, 14, &misc, w); // the holder entry (slot 27 gives the definition)
    } else {
        RetObject(&list, 14, (r() % 6 == 0) ? nullptr : &def, w);
    }
    Use(def.vt, 4, 0);
    RetObject(&def, 4, (r() % 3 == 0) ? nullptr : &misc, w);

    // the id table: the lookups in the order the handler makes them
    table.vt[29] = reinterpret_cast<void *>(&Lookup29);
    if (kind == 3 || kind == 4 || kind == 7 || kind == 8 || kind == 10) LookupObj((r() % 6 == 0) ? nullptr : &t, w);
    LookupObj(&t, w); // the new object (the original does not check it)
    if (flags & 0x20) LookupObj((r() % 4 == 0) ? nullptr : &t, w);
    if (flagsE & 2) LookupObj((r() % 4 == 0) ? nullptr : &t, w);

    // the generic object (a target, the new object, other objects at the spot)
    Use(t.vt, 36, 0);
    Use(t.vt, 13, 1);
    Use(t.vt, 131, 0);
    Use(t.vt, 104, 0);
    Use(t.vt, 116, 0);
    Use(t.vt, 98, 0);
    Use(t.vt, 61, 0);
    Use(t.vt, 2, 0);
    t.vt[33] = reinterpret_cast<void *>(&Pos33);
    t.vt[34] = reinterpret_cast<void *>(&WritePos34);
    t.vt[170] = reinterpret_cast<void *>(&Slot170);
    t.vt[171] = reinterpret_cast<void *>(&Slot171);
    Use(t.vt, 174, 3);
    t.vt[0] = reinterpret_cast<void *>(&QiS);
    const uint32_t tiny[] = {0, 1, 2, 3, 7, 0x7fff, 0x8000, 0xffff};
    auto pick = [&]() { return tiny[r() % 8] | (r() & 0xffff0000u); };
    RetValue(&t, 36, ReadWord(r), w);
    RetValue(&t, 13, pick(), w);
    RetValue(&t, 131, pick(), w);
    RetValue(&t, 104, static_cast<uint32_t>(r()), w);
    RetValue(&t, 116, static_cast<uint32_t>(r()), w);
    RetValue(&t, 98, Pick(r, {0x13, 0x13, 0x12, 0, 5}), w);
    RetObject(&t, 61, (r() % 2) ? &def : &misc, w);
    RetObject(&t, 33, &w.pos[1], w);
    RetValue(&t, 1034, static_cast<uint32_t>(r() % 3 ? (r() & 0x00ff0000u) | (r() & 0xffffu) : r()), w);
    RetValue(&t, 1035, static_cast<uint32_t>(r() % 3 ? (r() & 0x00ff0000u) | (r() & 0xffffu) : r()), w);
    RetObject(&t, 0, &bound, w);
    RetValue(&t, 1000, 1u, w);

    // the placement service
    placement.vt[65] = reinterpret_cast<void *>(&Placement65);
    placement.vt[71] = reinterpret_cast<void *>(&Placement71);
    verifier.vt[3] = reinterpret_cast<void *>(&Dispatch3);
    Use(placement.vt, 39, 0);
    RetValue(&placement, 65, Pick(r, {0, 0, 1, 2, 3}), w);
    RetValue(&placement, 71, (r() % 4) ? (static_cast<uint32_t>(r()) | 1u) : (static_cast<uint32_t>(r()) & ~0xffu), w);
    RetObject(&placement, 39, &verifier, w);
    verifier.vt[80] = reinterpret_cast<void *>(&Verifier80);
    // the verifier's answer matches the target's extra word often
    const uint32_t extraWord = Scripted(&t, 131);
    RetValue(&verifier, 80, (r() % 2) ? (extraWord | (r() & 0xffff0000u)) : static_cast<uint32_t>(r()), w);

    // the other objects at the spot and the stub answers
    gOthers.clear();
    for (int i = 0; i < 4; ++i) gOthers.push_back((r() % 3 == 0) ? 0xffffffffu : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&t) - reinterpret_cast<uintptr_t>(&w)));
    gBools.clear();
    for (int i = 0; i < 6; ++i) gBools.push_back((r() % 4 == 0) ? 1u : (r() % 2));

    // the maker
    maker.vt[16] = reinterpret_cast<void *>(&Maker16);
    Use(maker.vt, 17, 1);
    RetValue(&maker, 16, Pick(r, {0, 1, 2, 0x1234, 0xffff, 0x10001, static_cast<uint32_t>(r())}), w);

    // the global cache and the dispatcher
    Use(misc.vt, 9, 2);

    // the bound interface and its sub-interface (at +4, with its own vtable array)
    Use(bound.vt, 2, 0);
    Use(bound.vt, 294, 1);
    bound.pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.vtSub[0]));
    uint8_t *subBound = reinterpret_cast<uint8_t *>(&bound) + 4;
    Use(w.vtSub[0], 23, 0);
    Use(w.vtSub[0], 22, 1);
    RetValue(subBound, 23, Pick(r, {0, 0xffff, 1, 2, 0x10000, 5}), w);
    RetObject(subBound, 22, (r() % 4 == 0) ? nullptr : &k, w);
    Use(k.vt, 32, 2);
    Use(k.vt, 13, 1);
}

} // namespace

DIFF_TEST(PrimS_Prim2A, 0x585a60)
{
    // The game globals and code the test redirects.
    const uint32_t savedGlobals[3] = {*ctx.image.At<uint32_t>(0x10cc8ec), *ctx.image.At<uint32_t>(0x10cc298), *ctx.image.At<uint32_t>(0x10f519c)};
    struct CodeRedirect {
        uint8_t *site;
        uint8_t saved[5];
        CodeRedirect(difftest::Context &c, uint32_t rva, void *target) : site(c.image.At<uint8_t>(rva))
        {
            std::memcpy(saved, site, 5);
            DWORD old;
            VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
            site[0] = 0xE9;
            std::memcpy(site + 1, &rel, 4);
        }
        ~CodeRedirect() { std::memcpy(site, saved, 5); }
    };
    CodeRedirect r1(ctx, 0x7e3690, reinterpret_cast<void *>(&Stub7e3690));
    CodeRedirect r2(ctx, 0x546af0, reinterpret_cast<void *>(&Stub546af0));
    CodeRedirect r3(ctx, 0x7e8aa0, reinterpret_cast<void *>(&Stub7e8aa0));
    CodeRedirect r4(ctx, 0x7e8c10, reinterpret_cast<void *>(&Stub7e8c10));
    CodeRedirect r5(ctx, 0x5416a0, reinterpret_cast<void *>(&Stub5416a0));
    CodeRedirect r6(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc));
    // Ours allocates through the global allocator pointer: aim it at one on our own arena.
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    // The message object's AddRef, Release, SetId and slot 12 in the game's vtable.
    void **vtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
    void *savedSlots[4] = {vtable[1], vtable[2], vtable[5], vtable[12]};
    DWORD old;
    VirtualProtect(vtable, 64, PAGE_READWRITE, &old);
    vtable[1] = reinterpret_cast<void *>(&MsgAddRef);
    vtable[2] = reinterpret_cast<void *>(&MsgRelease);
    vtable[5] = reinterpret_cast<void *>(&MsgSetId);
    vtable[12] = reinterpret_cast<void *>(&MsgSlot12);

    gBeforeRun = SetCreateGlobals;
    Stats st = RunHandler(ctx, "Prim2A", 0x585a60, 60000, Build2A, [](PrimMidObject *o, void *f, const PrimParamView *p) { return Self(o)->Prim2A(f, p); });
    gBeforeRun = nullptr;

    vtable[1] = savedSlots[0];
    vtable[2] = savedSlots[1];
    vtable[5] = savedSlots[2];
    vtable[12] = savedSlots[3];
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
    *ctx.image.At<uint32_t>(0x10cc8ec) = savedGlobals[0];
    *ctx.image.At<uint32_t>(0x10cc298) = savedGlobals[1];
    *ctx.image.At<uint32_t>(0x10f519c) = savedGlobals[2];
    if (st.throws < 500 || st.byRet[0] < 3000 || st.byRet[1] < 3000) ctx.Fail("coverage: %u throws, %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}
