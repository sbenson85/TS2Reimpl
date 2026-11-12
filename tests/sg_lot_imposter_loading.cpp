// Differential test for the lot imposter manager's lot resource leaves (lane 261111r, scenegraph row LT2b2a;
// src/sg/cLotImposterManager.h): OpenLotResource 0x368e20, LoadLotResources 0x366460 and SetImposterName 0x36c510; and
// (lane 261111s) the manager's slots 6 (LoadNamedLot 0x369960: the text as a string on the stack) and 9 (LoadCurrentLot
// 0x369a30: the slot-23 service's lot, its slot 19 copied into the string, when there is one).
//
// LotImposterManager_Resources: recording fakes (tests/ts_services_fixture.h) for the app singleton (cell 0x10cc288; its
// slot 6 the app or null), the app (slot 56: the lot or null), the cached slot-23 service (slot 9 the lot or null, slot 6
// the current lot whose slot 8 is its name), the lot (slot 8 its name: slot 7 a length, slot 6 a text, sometimes longer
// than the length, sometimes over 15 characters so the string allocates), the persist system (cell 0x10faf6c, empty on
// half the steps so it is fetched through the service object 0x10c9378's slot 5, logged where it happens; slot 11
// logs the string it is given (vtable, text, refcount) and answers a group id; slot 34 logs the key and type, writes the
// opened resource fake into *out or not and answers a word whose low byte decides), the resource manager (0x20deb5 stood
// in; slot 5 logs the key, writes the list fake or not and answers likewise; slot 10 logs the key it is given), the list
// (slot 7: a {begin, end} of 0-4 sixteen-byte keys, shrunk or grown on a chosen call so a count read once shows) and
// the opened resource (slot 26 logs each key). The manager image's +0x80 string has a test vtable whose slot 16 logs its
// argument and assigns a scripted text; +0x1c (the "!" or "_" choice) comes with junk. Random steps on both sides,
// compared after each: the event log (every call in order, with its arguments), the result and out words, the +0x80
// string's text, size and vtable, the fakes' reference counts; at the end each side's heap blocks.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "hook/hooks.h"
#include "rz/cRZString.h"
#include "sg/cLotImposterManager.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace lotimpres_test {

using namespace svcfx;
using nTSSG::cLotImposterManager;
using Mgr = cLotImposterManager;

enum : unsigned {
    fSingleton = 0, fApp, fSlot23, fCurrent, fCurName, fLot, fLotName, fPersist, fResources, fList, fOpened, fNameArg,
    fService,
};

const char *const kTexts[] = {"", "lot", "Lot_0123456789abcdefg", "N001_Lot7", "0123456789abcde", "0123456789abcdef"};
constexpr unsigned kTextCount = 6;

struct Script {
    bool hasApp, appLot, svcLot;
    unsigned lotText, curText;
    uint32_t lotLenCut, curLenCut; // subtracted from the text's length (when it fits)
    uint32_t group;
    uint32_t openResult; // persist slot 34's answer
    bool openWrites;     // writes *out even when it fails
    uint32_t listResult; // resources slot 5's answer
    bool listWrites;
    unsigned keys;       // keys in the list
    int changeAt;        // the list's slot 7 call that changes the count (-1 none)
    unsigned changeTo;
    unsigned assignText; // what m080's slot 16 assigns
    bool persistCached;  // the persist cell already set (else fetched through the service object, logged in order)
};
Script gS;
const void *gStringVT;
uint32_t gListCalls;
uint32_t gKeyWords[8][4];
struct Range {
    const uint32_t *begin, *end;
} gRange;

uint32_t Length(unsigned text, uint32_t cut)
{
    const uint32_t n = uint32_t(std::strlen(kTexts[text]));
    return cut <= n ? n - cut : n;
}

void LogText(const char *text, uint32_t size)
{
    Ev(size);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; ++i) h = (h ^ uint8_t(text[i])) * 16777619u;
    Ev(h);
}
void LogString(const void *p)
{
    const auto *s = static_cast<const cRZString *>(p);
    Ev(0x57a1), Ev(s->mVTable == gStringVT), Ev(s->mRefCount);
    LogText(s->mString.data(), uint32_t(s->mString.size()));
    Ev(s->mString.capacity() > 15);
}
void LogKey(const void *p)
{
    const auto *k = static_cast<const uint32_t *>(p);
    // inside the list's keys: its index; elsewhere (the caller's own key): 0xff
    const auto *base = &gKeyWords[0][0];
    Ev(k >= base && k < base + 32 ? uint32_t(k - base) : 0xff);
    for (int i = 0; i < 4; ++i) Ev(k[i]);
}

uint32_t AppOf(Obj *, uint32_t *) { return gS.hasApp ? P(&F(fApp)) : 0; }
uint32_t AppLot(Obj *, uint32_t *args)
{
    // the argument is the name fake (OpenLotResource) or a string on the stack (SetImposterName)
    if (args[0] == P(&F(fNameArg))) Ev(0x4a4d);
    else LogString(reinterpret_cast<const void *>(args[0]));
    return gS.appLot ? P(&F(fLot)) : 0;
}
uint32_t SvcLot(Obj *, uint32_t *) { return gS.svcLot ? P(&F(fLot)) : 0; }
uint32_t LotLength(Obj *, uint32_t *) { return Length(gS.lotText, gS.lotLenCut); }
uint32_t LotText(Obj *, uint32_t *) { return P(kTexts[gS.lotText]); }
uint32_t CurLength(Obj *, uint32_t *) { return Length(gS.curText, gS.curLenCut); }
uint32_t CurText(Obj *, uint32_t *) { return P(kTexts[gS.curText]); }
uint32_t PersistGroup(Obj *, uint32_t *args)
{
    LogString(reinterpret_cast<const void *>(args[0]));
    return gS.group;
}
// The caller's group out word while opOpen runs (null otherwise): its value when slot 34 is asked shows whether the group
// was stored before the open (it is, 0x368eff before 0x368f22).
uint32_t *gGroupCell = nullptr;
// The side's heap counter while it runs: the resources stand-in logs the live blocks, so a lot-name string held past
// OpenLotResource (freed before 0x20deb5 in the original) shows.
difftest::AllocationCounter *gCounter = nullptr;
uint32_t PersistOpen(Obj *, uint32_t *args)
{
    LogKey(reinterpret_cast<const void *>(args[0]));
    if (gGroupCell) Ev(0x34c0), Ev(*gGroupCell);
    if ((gS.openResult & 0xff) || gS.openWrites) *reinterpret_cast<void **>(args[2]) = &F(fOpened);
    return gS.openResult;
}
uint32_t ResourceList(Obj *, uint32_t *args)
{
    LogKey(reinterpret_cast<const void *>(args[0]));
    if ((gS.listResult & 0xff) || gS.listWrites) *reinterpret_cast<void **>(args[2]) = &F(fList);
    return gS.listResult;
}
uint32_t ResourceLoad(Obj *, uint32_t *args)
{
    LogKey(reinterpret_cast<const void *>(args[0]));
    return 0x10ad;
}
uint32_t ListKeys(Obj *, uint32_t *)
{
    if (int(gListCalls++) == gS.changeAt) gRange.end = gRange.begin + 4 * gS.changeTo;
    return P(&gRange);
}
uint32_t OpenedAdd(Obj *, uint32_t *args)
{
    LogKey(reinterpret_cast<const void *>(args[0]));
    return 0xadd;
}
// The service object's (0x10c9378) slot 5 query, asked when the persist cell is empty: logged, answers the persist fake.
uint32_t ServiceQuery(Obj *, uint32_t *args)
{
    Ev(0x5e7c), Ev(args[0]), Ev(args[1]);
    *reinterpret_cast<void **>(args[2]) = &F(fPersist);
    return 1;
}
void *__cdecl StandResources()
{
    Ev(0xdeb5);
    if (gCounter) Ev(0xb10c), Ev(uint32_t(gCounter->Blocks()));
    return &F(fResources);
}

// m080's test vtable: slot 16 (Assign) logs its argument and assigns the scripted text.
void *gNameVT[34];
cRZString *__fastcall NameAssign(cRZString *self, void *, void *other)
{
    Ev(0x1600), Ev(Norm(P(other)));
    self->mString.assign(kTexts[gS.assignText]);
    return self;
}
void __fastcall NameBadSlot(void *, void *)
{
    std::fprintf(stderr, "resources test: an unexpected string slot\n");
    std::abort();
}

void BeginSide()
{
    ResetAll();
    F(fSingleton).Set(6, "", 0, &AppOf);
    F(fApp).Set(56, "x", 0, &AppLot);
    F(fSlot23).Set(9, "", 0, &SvcLot);
    F(fSlot23).Set(6, "", &F(fCurrent));
    F(fCurrent).Set(8, "", &F(fCurName));
    F(fCurName).Set(7, "", 0, &CurLength);
    F(fCurName).Set(6, "", 0, &CurText);
    F(fLot).Set(8, "", &F(fLotName));
    F(fLot).Set(19, "", &F(fLotName)); // CurrentLotName's (slot 9 of the manager): copied into the caller's string
    F(fLotName).Set(7, "", 0, &LotLength);
    F(fLotName).Set(6, "", 0, &LotText);
    F(fPersist).Set(11, "x", 0, &PersistGroup);
    F(fPersist).Set(34, "xdp", 0, &PersistOpen);
    F(fResources).Set(5, "xdpdd", 0, &ResourceList);
    F(fResources).Set(10, "x", 0, &ResourceLoad);
    F(fList).Set(7, "", 0, &ListKeys);
    F(fOpened).Set(26, "x", 0, &OpenedAdd);
    F(fService).Set(5, "xxx", 0, &ServiceQuery);
    gListCalls = 0;
    for (uint32_t i = 0; i < 8; ++i)
        for (uint32_t k = 0; k < 4; ++k) gKeyWords[i][k] = 0x6b000000u | i << 8 | k;
    gRange = {&gKeyWords[0][0], &gKeyWords[0][0] + 4 * gS.keys};
}

uint8_t *BuildManager(const char *text, uint8_t choice)
{
    auto *o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memset(o, 0xc5, Mgr::kSize);
    auto *name = reinterpret_cast<cRZString *>(o + 0x80);
    name->mVTable = gNameVT;
    std::construct_at(&name->mString, text);
    name->mRefCount = 3;
    o[0x1c] = choice;
    return o;
}
void FreeManager(uint8_t *o)
{
    std::destroy_at(&reinterpret_cast<cRZString *>(o + 0x80)->mString);
    ::operator delete(o);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

} // namespace lotimpres_test

using namespace lotimpres_test;

DIFF_TEST(LotImposterManager_Resources, 0x366460)
{
    AllocatorScope allocator;
    Globals globals;
    globals.Set(kAppCell, &F(fSingleton));
    globals.Set(kSlot23Cache, &F(fSlot23));
    globals.Set(kPersistCell, &F(fPersist));
    globals.Set(0x10c9378, &F(fService));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch resources(0x20deb5, shape::CodeOf(&StandResources));
    gStringVT = ctx.image.At(0xd9b470);
    for (void *&s : gNameVT) s = reinterpret_cast<void *>(&NameBadSlot);
    gNameVT[16] = reinterpret_cast<void *>(&NameAssign);

    using OpenFn = uint32_t(__stdcall *)(void *, void *, uint32_t *, void **);
    using LoadFn = void(__thiscall *)(void *, void *);
    using NameFn = void(__thiscall *)(void *, const char *);
    const OpenFn openO = ctx.Original<OpenFn>(0x368e20), openU = reinterpret_cast<OpenFn>(&Mgr::OpenLotResource);
    const LoadFn loadO = ctx.Original<LoadFn>(0x366460),
                 loadU = reinterpret_cast<LoadFn>(MemberAddress(&Mgr::LoadLotResources));
    const NameFn nameO = ctx.Original<NameFn>(0x36c510),
                 nameU = reinterpret_cast<NameFn>(MemberAddress(&Mgr::SetImposterName));

    // lane 261111s: the manager's slots 6 (0x369960, LoadNamedLot) and 9 (0x369a30, LoadCurrentLot)
    using NamedFn = void(__thiscall *)(void *, const char *);
    using CurrentFn = void(__thiscall *)(void *);
    const NamedFn namedO = ctx.Original<NamedFn>(0x369960),
                  namedU = reinterpret_cast<NamedFn>(MemberAddress(&Mgr::LoadNamedLot));
    const CurrentFn currentO = ctx.Original<CurrentFn>(0x369a30),
                    currentU = reinterpret_cast<CurrentFn>(MemberAddress(&Mgr::LoadCurrentLot));

    enum : unsigned { opOpen, opLoad, opName, opNamed, opCurrent, kOps };
    static const char *const kNames[] = {"open", "load", "name", "named", "current"};
    static const uint32_t kAnswers[] = {0, 1, 0x100, 0xffffff01, 0xff00, 0x5a5a5a01};
    int reached[kOps] = {}, opened = 0, listed = 0, looped = 0, named = 0;

    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x366460u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        const unsigned startText = draw(kTextCount);
        const uint8_t choice = uint8_t(draw(3) == 0 ? 0 : draw(2) ? 1 : 0x80);
        struct Step {
            unsigned op;
            Script s;
            unsigned nameText;
        };
        std::vector<Step> steps(1 + draw(4));
        for (Step &st : steps) {
            st.op = draw(kOps);
            Script &s = st.s;
            s.hasApp = draw(2) != 0;
            s.appLot = draw(4) != 0;
            s.svcLot = draw(4) != 0;
            s.lotText = draw(kTextCount), s.curText = draw(kTextCount);
            s.lotLenCut = draw(3) ? 0 : draw(4), s.curLenCut = draw(3) ? 0 : draw(4);
            s.group = gen();
            s.openResult = kAnswers[draw(6)];
            s.openWrites = draw(2) != 0;
            s.listResult = kAnswers[draw(6)];
            s.listWrites = draw(2) != 0;
            s.keys = draw(5);
            s.changeAt = draw(3) ? -1 : int(draw(6));
            s.changeTo = draw(8);
            s.assignText = draw(kTextCount);
            st.nameText = draw(kTextCount);
            s.persistCached = draw(2) != 0;
        }

        Blob outcome[2][4];
        long long blocks[2] = {};
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            difftest::AllocationCounter counter;
            gCounter = &counter;
            uint8_t *o;
            {
                difftest::NotCounted bookkeeping;
                o = BuildManager(kTexts[startText], choice);
            }
            for (size_t i = 0; i < steps.size(); ++i) {
                const Step &st = steps[i];
                gS = st.s;
                BeginSide();
                *ctx.image.At<void *>(kPersistCell) = st.s.persistCached ? &F(fPersist) : nullptr;
                uint32_t result = 0, group = 0x5e5e5e5e;
                void *out = reinterpret_cast<void *>(0x5e5e5e5e);
                switch (st.op) {
                case opOpen: {
                    alignas(4) uint8_t unused[0x20];
                    std::memset(unused, 0x77, sizeof(unused));
                    gGroupCell = &group;
                    result = (side ? openU : openO)(&F(fNameArg), unused, &group, &out) & 0xff;
                    gGroupCell = nullptr;
                    break;
                }
                case opLoad: (side ? loadU : loadO)(o, &F(fNameArg)); break;
                case opName: (side ? nameU : nameO)(o, kTexts[st.nameText]); break;
                case opNamed: (side ? namedU : namedO)(o, kTexts[st.nameText]); break;
                case opCurrent: (side ? currentU : currentO)(o); break;
                default: break;
                }
                difftest::NotCounted bookkeeping;
                Blob &b = outcome[side][i];
                b = gEv;
                gEv.clear();
                b.push_back(0xeeee), b.push_back(result), b.push_back(group), b.push_back(NormOut(P(out))),
                    b.push_back(gListCalls);
                const auto *name = reinterpret_cast<const cRZString *>(o + 0x80);
                b.push_back(name->mVTable == gNameVT), b.push_back(name->mRefCount);
                b.push_back(uint32_t(name->mString.size()));
                for (char c : name->mString) b.push_back(uint8_t(c));
                CaptureFakes(b);
                if (side) {
                    ++reached[st.op];
                    for (size_t k = 0; k < b.size(); ++k) {
                        if (b[k] == evCall && k + 2 < b.size() && b[k + 1] == 200 + fOpened && b[k + 2] == 26) ++opened;
                        if (b[k] == evCall && k + 2 < b.size() && b[k + 1] == 200 + fResources && b[k + 2] == 10) ++looped;
                        if (b[k] == evCall && k + 2 < b.size() && b[k + 1] == 200 + fList && b[k + 2] == 7) ++listed;
                        if (b[k] == 0x1600) ++named;
                    }
                }
            }
            {
                difftest::NotCounted bookkeeping;
                FreeManager(o);
            }
            blocks[side] = counter.Blocks();
            gCounter = nullptr;
        }
        for (size_t i = 0; i < steps.size(); ++i)
            if (!Same(ctx, outcome[0][i], outcome[1][i], kNames[steps[i].op], seed)) break;
        if (blocks[0] != blocks[1]) ctx.Fail("seed %u: heap blocks left %lld vs %lld", seed, blocks[0], blocks[1]);
        if (ctx.failures.size() >= 6) return;
    }
    for (unsigned k = 0; k < kOps; ++k)
        if (reached[k] < 500) ctx.Fail("coverage: %s ran %d times", kNames[k], reached[k]);
    if (opened < 300 || listed < 300 || looped < 300 || named < 300)
        ctx.Fail("coverage: added %d, listed %d, loaded %d, renamed %d", opened, listed, looped, named);
}
