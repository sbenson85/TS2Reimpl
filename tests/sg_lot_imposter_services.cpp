// Differential tests for LT2b2a's left rows of the lot imposter manager (lane 261111s, scenegraph row LT2b2a;
// src/sg/cLotImposterManager.h): the design modes' constructor 0x362950, ApplyDefinition 0x365410,
// CreateGenerationServices 0x364940, the imposter description adder 0x36b2b0 and cObjectImposterCommand::Execute
// 0x366f10.
//
// LotImposterServices_Create (0x364940): a manager image (junk but for +0x38..+0x48) whose +0x38 holds a virtual-base box
// or null and whose +0x3c / +0x40 / +0x44 / +0x48 hold old fakes or null. The director framework (fixture) logs each time
// it is asked; its slot 3 (Create) logs the class, the face and the out cell (and what the cell holds then), then writes
// the scripted object: a box (its virtual base a fake) or null for the two virtual-base classes, a fresh fake or a second
// one for the four counted members. Every Release logs the five cells as they are then (a cell nulled before or after its
// release). The wiring slots (32, 41, 94, 45) log their arguments; on a scripted call one of them swaps a member cell for
// another fake, so a member read once instead of at each use shows.
// LotImposterServices_Desc (0x36b2b0): the framework's Create writes the description fake or null; its QI writes the face
// (or not, sometimes even when it fails) and answers a word whose low byte decides; the face's slot 3 writes a key; the key
// vector starts with 0-4 keys at a chosen capacity (so the push grows it or not); the persist cell is cached or fetched
// through the service object (0x10c9378, logged); slot 16 answers a word whose low byte decides. Compared: the log, the
// result's low byte, the vector, the fakes' counts.
// LotImposterServices_Command (0x366f10): a command whose state is a real cImposterDefinitions; the arguments fake's slot 10
// logs the count word it finds, writes a scripted count (0, 1, 2, 3, 5, 6, 7, -1, 0x102, 0x106) and answers a vector of
// texts, or throws a cError (cp::ThrowFormatted) or another exception (caught by the test on both sides). The definitions
// commands run for real: the original's 0x3f7890 / 0x3f76e0 on its side, ours (lane 261111u) on ours, so this also checks
// them in their caller; compared: what escaped, the definitions (every key, name and entry text), the heap blocks.
// LotImposterServices_Apply (0x365410, 0x362950): ApplyDefinition with its four callees stood in on both sides (the
// image's and ours, lane 261111u's, shape::StandIn; their own tests: sg_lot_imposter_definitions.cpp and
// sg_lot_imposter_design.cpp) (0x3f7f40 and
// 0x3f7c80 need a resource node tree and 0x3f5b80 / 0x3f83d0 a model's design mode lists: the stand-ins log their `this`,
// their arguments, whether a design mode set is the one loaded or the one mapped into, and dump each set given, fresh or as
// the previous stand-in filled it; they fill the sets so both are destroyed with content); and the constructor alone into
// junk memory (the strings' words, the map head's links and flags, then destroyed by the original 0x335a60).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "gz/CommandParser.h"
#include "hook/hooks.h"
#include "rz/GameAllocator.h"
#include "sg/cLotImposterDesignModes.h"
#include "sg/cLotImposterManager.h"
#include "ts/PersistKey.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace lotimpsvc_test {

using namespace svcfx;
using nTSSG::cDesignModes;
using nTSSG::cLotImposterManager;
using Mgr = cLotImposterManager;
namespace cp = nGZCommandParser;

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t W(const void *o, uint32_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(o) + off, 4);
    return v;
}
void PutW(void *o, uint32_t off, uint32_t v) { std::memcpy(static_cast<uint8_t *>(o) + off, &v, 4); }

uint32_t TextHash(const char *text, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ uint8_t(text[i])) * 16777619u;
    return h;
}

// ---- CreateGenerationServices ----

enum : unsigned {
    fFramework = 0, fOld0, fOld1, fOld2, fOld3, fNew0, fNew1, fNew2, fNew3, fAlt0, fAlt1, fAlt2, fAlt3, fVOld, fVNew,
    fBounds, fDesc, fFace, fPersist, fService, fArgs, fValue, fObject,
};

// A virtual-base box: a vtable word, then the vbtable whose entry 1 is the virtual base's offset from +4 (a fake).
struct Box {
    void *vt;
    int32_t *vbtable;
};
Box gBoxOld, gBoxNew, gBoxBounds;
int32_t gTableOld[2], gTableNew[2], gTableBounds[2];
void PointBox(Box &box, int32_t (&table)[2], unsigned fake)
{
    table[0] = 0;
    table[1] = int32_t(P(&F(fake)) - (P(&box) + 4));
    box.vbtable = table;
    box.vt = nullptr;
}

struct CreateScript {
    unsigned vbaseMade;  // 0 the new box, 1 null, 2 the old box (again)
    unsigned boundsMade; // 0 the bounds box, 1 null
    uint8_t altMade;     // bit k: member k made as its second fake
    int swapAt;          // the wiring call (0..4) that swaps a member cell, -1 none
    unsigned swapMember; // which member
};
CreateScript gC;
uint8_t *gMgr;
unsigned gWiring;

void CellsNow()
{
    Ev(0xce11);
    for (uint32_t off = 0x38; off <= 0x48; off += 4) Ev(Norm(W(gMgr, off)));
}
uint32_t ReleaseLogged(Obj *self, uint32_t *)
{
    const uint32_t left = uint32_t(--self->refs);
    CellsNow();
    return left;
}
uint32_t Create(Obj *, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[2]));
    Ev(Norm(args[2])), Ev(Norm(*out));
    uint32_t made = 0;
    switch (args[0]) {
    case 0x9bffc10du: made = gC.vbaseMade == 0 ? P(&gBoxNew) : gC.vbaseMade == 2 ? P(&gBoxOld) : 0; break;
    case 0x1cfeceb8u: made = gC.boundsMade == 0 ? P(&gBoxBounds) : 0; break;
    case 0x7ba3838cu: made = P(&F(gC.altMade & 1 ? fAlt0 : fNew0)); break;
    case 0xfc6eb1f7u: made = P(&F(gC.altMade & 2 ? fAlt1 : fNew1)); break;
    case 0x65245517u: made = P(&F(gC.altMade & 4 ? fAlt2 : fNew2)); break;
    case 0xe519c933u: made = P(&F(gC.altMade & 8 ? fAlt3 : fNew3)); break;
    default: Ev(0xbad); break;
    }
    if (made) *out = made;
    return 0xc0de;
}
uint32_t Wiring(Obj *, uint32_t *)
{
    if (int(gWiring++) == gC.swapAt) {
        const uint32_t offs[] = {0x3c, 0x40, 0x44, 0x48};
        const unsigned m = gC.swapMember;
        const uint32_t now = W(gMgr, offs[m]);
        PutW(gMgr, offs[m], now == P(&F(fAlt0 + m)) ? P(&F(fNew0 + m)) : P(&F(fAlt0 + m)));
        Ev(0x5a9);
    }
    return 0x3a3a;
}

void BeginCreate()
{
    ResetAll();
    gWiring = 0;
    F(fFramework).Set(3, "ddx", 0, &Create);
    for (unsigned f = fOld0; f <= fBounds; ++f) F(f).Set(2, 0, 0, &ReleaseLogged);
    for (unsigned f = fNew0; f <= fAlt3; ++f) {
        F(f).Set(32, "p", 0, &Wiring);
        F(f).Set(41, "pd", 0, &Wiring);
        F(f).Set(94, "p", 0, &Wiring);
        F(f).Set(45, "p", 0, &Wiring);
    }
}

// ---- the description adder ----

using KeyVector = std::vector<PersistKey, GZServiceAllocator<PersistKey>>;
struct DescScript {
    bool made;          // the framework makes the description
    uint32_t qi;        // its QI's answer
    bool qiWrites;      // writes the face even when it fails
    uint32_t key[4];    // the face's key
    uint32_t added;     // persist slot 16's answer
    bool persistCached; // the persist cell set (else the service object's slot 5)
};
DescScript gD;

uint32_t DescCreate(Obj *, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[2]));
    Ev(Norm(*out));
    if (gD.made) *out = P(&F(fDesc));
    return 0xc0de;
}
uint32_t DescQuery(Obj *, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[1]));
    Ev(Norm(*out));
    if ((gD.qi & 0xff) || gD.qiWrites) *out = P(&F(fFace));
    return gD.qi;
}
uint32_t FaceKey(Obj *, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    std::memcpy(out, gD.key, 16);
    return 0x6e7;
}
// The Desc test's key vector while a side runs: slot 17's fake notes its size and keys then (the key is pushed before
// the call), and whether the key it is given lies in the vector's storage (it is the caller's own copy).
const KeyVector *gDescKeys = nullptr;
uint32_t PersistKeyLog(Obj *, uint32_t *args)
{
    const auto *k = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    if (gDescKeys) {
        const PersistKey *data = gDescKeys->data();
        const auto at = reinterpret_cast<uintptr_t>(k), from = reinterpret_cast<uintptr_t>(data);
        Ev(uint32_t(gDescKeys->size()));
        Ev(at >= from && at < from + gDescKeys->size() * sizeof(PersistKey));
        for (const PersistKey &key : *gDescKeys)
            for (int i = 0; i < 4; ++i) Ev(reinterpret_cast<const uint32_t *>(&key)[i]);
    }
    return 0x17;
}
uint32_t ServiceQuery(Obj *, uint32_t *args)
{
    Ev(0x5e7c), Ev(args[0]), Ev(args[1]);
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(fPersist);
    return 1;
}
void LogKeys(Blob &b, const KeyVector &keys)
{
    b.push_back(uint32_t(keys.size())), b.push_back(uint32_t(keys.capacity()));
    for (const PersistKey &k : keys)
        for (uint32_t w : k.mWords) b.push_back(w);
}

// ---- the command ----

const char *const kWords[] = {"lot", "Lot", "chair_cres", "chair_cres", "0123456789abcdef_long_name", "", "a", "b"};
constexpr unsigned kWordCount = 8;
struct CommandScript {
    int32_t count;  // written
    unsigned kind;  // 0 answers, 1 throws a cError, 2 throws something else
    unsigned words[6];
};
CommandScript gK;
const char *gArgv[6];
struct OtherFailure {
    uint32_t code;
};
uint32_t Arguments(Obj *, uint32_t *args)
{
    auto *count = reinterpret_cast<int32_t *>(uintptr_t(args[0]));
    Ev(uint32_t(*count));
    if (gK.kind == 1) cp::ThrowFormatted("bad arguments (%d)", gK.count);
    if (gK.kind == 2) throw OtherFailure{7};
    *count = gK.count;
    for (int i = 0; i < 6; ++i) gArgv[i] = kWords[gK.words[i]];
    return P(gArgv);
}
void LogText(Blob &b, const cRZString &s)
{
    b.push_back(uint32_t(s.mString.size()));
    b.push_back(TextHash(s.mString.data(), s.mString.size()));
}
void DumpDefinitions(Blob &b, const nTSSG::cImposterDefinitions &d)
{
    b.push_back(0xdef5), b.push_back(d.mId), b.push_back(uint32_t(d.mDefinitions.size()));
    for (const auto &[key, def] : d.mDefinitions) {
        LogText(b, key.s);
        LogText(b, def.name.s);
        b.push_back(uint32_t(def.entries.size()));
        for (const auto &[k, v] : def.entries) {
            LogText(b, k.first.s), LogText(b, k.second.s);
            LogText(b, v.first.s), LogText(b, v.second.s);
        }
    }
}

// ---- ApplyDefinition's stand-ins ----

struct ApplyScript {
    uint32_t found;  // 0x3f7f40's answer
    uint32_t mapped; // 0x3f83d0's
    uint32_t taken;  // 0x3f7c80's
    unsigned loadEntries, mapEntries;
};
ApplyScript gA;
uint32_t gLoaded, gMapped;

void DumpModes(const cDesignModes *m)
{
    auto text = [](const nTSWorld::cMapString &s) {
        Ev(Norm(P(s.mVTable))), Ev(s.mRefCount), Ev(uint32_t(s.mString.size()));
        Ev(TextHash(s.mString.data(), s.mString.size()));
    };
    text(m->mTag), text(m->mName);
    Ev(uint32_t(m->mModes.size()));
    for (const auto &[k, v] : m->mModes) text(k), text(v);
}
void FillModes(cDesignModes *m, const char *name, unsigned entries)
{
    m->mName.mString.assign(name);
    for (unsigned i = 0; i < entries; ++i) {
        nTSWorld::cMapString k, v;
        k.mString = std::string("subset_with_a_long_name_") + char('a' + i);
        v.mString = std::string("material_") + char('a' + i);
        m->mModes.emplace(k, v);
    }
}
// The Apply test's heap counter while a side runs: 0x3f7f40's stand-in notes the live blocks then, so the two local
// cDesignModes (each a heap head node) are seen built after the lookup, not before it.
const difftest::AllocationCounter *gApplyCounter = nullptr;
uint32_t __fastcall Stand7f40(void *self, void *, void *object)
{
    Ev(0x7f40), Ev(Norm(P(self))), Ev(Norm(P(object)));
    if (gApplyCounter) Ev(uint32_t(gApplyCounter->Blocks()));
    return gA.found;
}
uint32_t __fastcall Stand5b80(cDesignModes *self, void *, void *object)
{
    Ev(0x5b80), Ev(Norm(P(object))), Ev(Norm(P(self)));
    DumpModes(self);
    gLoaded = P(self);
    FillModes(self, "loaded_model_name_over_fifteen", gA.loadEntries);
    return 0x5b5b5b00;
}
uint32_t __fastcall Stand83d0(void *self, void *, cDesignModes *from, cDesignModes *to)
{
    Ev(0x83d0), Ev(Norm(P(self))), Ev(P(from) == gLoaded), Ev(P(to) == gLoaded), Ev(P(to) == P(from));
    DumpModes(from), DumpModes(to);
    gMapped = P(to);
    FillModes(to, "mapped", gA.mapEntries);
    return gA.mapped;
}
uint32_t __fastcall Stand7c80(void *self, void *, cDesignModes *modes)
{
    Ev(0x7c80), Ev(Norm(P(self))), Ev(P(modes) == gMapped), Ev(P(modes) == gLoaded);
    DumpModes(modes);
    return gA.taken;
}

const uint32_t kAnswers[] = {0, 1, 0x100, 0xffffff01, 0xff00, 0x5a5a5a01, 0x80};

} // namespace lotimpsvc_test

using namespace lotimpsvc_test;

DIFF_TEST(LotImposterServices_Create, 0x364940)
{
    Globals globals;
    gFrameworkIndex = fFramework;
    gOnDirectorFramework = [] { Ev(0xf00d); };
    struct Unhook {
        ~Unhook() { gOnDirectorFramework = nullptr; }
    } unhook;
    PointBox(gBoxOld, gTableOld, fVOld);
    PointBox(gBoxNew, gTableNew, fVNew);
    PointBox(gBoxBounds, gTableBounds, fBounds);
    using Fn = void(__thiscall *)(void *);
    const Fn orig = ctx.Original<Fn>(0x364940), ours = reinterpret_cast<Fn>(MemberAddress(&Mgr::CreateGenerationServices));
    int swapped = 0, vnull = 0, bnull = 0, oldNull = 0;

    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x364940u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        CreateScript script{draw(3), draw(3) == 0 ? 1u : 0u, uint8_t(draw(16)), draw(2) ? int(draw(6)) : -1, draw(4)};
        const unsigned oldV = draw(3); // 0 the old box, 1 null, 2 the new box already there
        const uint8_t oldMask = uint8_t(draw(16));
        const unsigned runs = 1 + draw(2);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            gC = script;
            BeginCreate();
            gMgr = static_cast<uint8_t *>(::operator new(Mgr::kSize));
            std::memset(gMgr, 0xc5, Mgr::kSize);
            PutW(gMgr, 0x38, oldV == 0 ? P(&gBoxOld) : oldV == 2 ? P(&gBoxNew) : 0);
            const uint32_t offs[] = {0x3c, 0x40, 0x44, 0x48};
            for (unsigned k = 0; k < 4; ++k) PutW(gMgr, offs[k], oldMask >> k & 1 ? P(&F(fOld0 + k)) : 0);
            for (unsigned r = 0; r < runs; ++r) {
                gWiring = 0;
                (side ? ours : orig)(gMgr);
                Ev(0xe0d);
                CellsNow();
            }
            Blob &b = outcome[side];
            b = gEv;
            CaptureFakes(b);
            for (uint32_t off = 0; off < Mgr::kSize; off += 4)
                if (off < 0x38 || off > 0x48) b.push_back(W(gMgr, off)); // the rest untouched (junk words)
            if (side) {
                swapped += gC.swapAt >= 0 && gWiring > unsigned(gC.swapAt);
                vnull += script.vbaseMade == 1, bnull += script.boundsMade == 1, oldNull += oldMask == 0;
            }
            ::operator delete(gMgr);
            gMgr = nullptr;
        }
        Same(ctx, outcome[0], outcome[1], "create", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (swapped < 300 || vnull < 200 || bnull < 200 || oldNull < 40)
        ctx.Fail("coverage: swapped %d, vbase null %d, bounds null %d, no old members %d", swapped, vnull, bnull, oldNull);
}

DIFF_TEST(LotImposterServices_Desc, 0x36b2b0)
{
    AllocatorScope allocator;
    Globals globals;
    gFrameworkIndex = fFramework;
    gOnDirectorFramework = [] { Ev(0xf00d); };
    struct Unhook {
        ~Unhook() { gOnDirectorFramework = nullptr; }
    } unhook;
    globals.Set(kPersistCell, nullptr);
    globals.Set(0x10c9378, &F(fService));
    using Fn = uint32_t(__cdecl *)(void *, void *, KeyVector *);
    const Fn orig = ctx.Original<Fn>(0x36b2b0), ours = reinterpret_cast<Fn>(&nTSSG::AddImposterDesc);
    int added = 0, grown = 0, fetched = 0, noDesc = 0, refused = 0;

    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 gen(seed * 2246822519u + 0x36b2b0u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        DescScript script{};
        script.made = draw(5) != 0;
        script.qi = kAnswers[draw(7)];
        script.qiWrites = draw(2) != 0;
        for (uint32_t &w : script.key) w = gen();
        script.added = kAnswers[draw(7)];
        script.persistCached = draw(2) != 0;
        const unsigned start = draw(5), spare = draw(3);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            gD = script;
            ResetAll();
            F(fFramework).Set(3, "ddx", 0, &DescCreate);
            F(fDesc).Set(0, "dx", 0, &DescQuery);
            F(fDesc).Set(4, "d");
            F(fFace).Set(9, "p");
            F(fFace).Set(3, "p", 0, &FaceKey);
            F(fPersist).Set(17, "x", 0, &PersistKeyLog);
            F(fPersist).Set(16, "pd", gD.added);
            F(fService).Set(5, "xxx", 0, &ServiceQuery);
            *ctx.image.At<void *>(kPersistCell) = script.persistCached ? &F(fPersist) : nullptr;
            uint32_t result;
            Blob &b = outcome[side];
            {
                KeyVector keys;
                keys.reserve(start + spare);
                for (unsigned i = 0; i < start; ++i) keys.push_back(PersistKey{{0x11u * i, 0x22u * i, 0x33u * i, 0x44u * i}});
                gDescKeys = &keys;
                result = (side ? ours : orig)(reinterpret_cast<void *>(0x5eed), &F(fValue), &keys) & 0xff;
                gDescKeys = nullptr;
                b = gEv;
                b.push_back(0xeeee), b.push_back(result);
                LogKeys(b, keys);
                if (side) {
                    added += keys.size() > start, grown += keys.size() > start && spare == 0;
                    fetched += !script.persistCached && keys.size() > start, noDesc += !script.made;
                    refused += script.made && !(script.qi & 0xff);
                }
            }
            b.push_back(Norm(P(*ctx.image.At<void *>(kPersistCell))));
            CaptureFakes(b);
        }
        Same(ctx, outcome[0], outcome[1], "desc", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (added < 300 || grown < 100 || fetched < 100 || noDesc < 200 || refused < 200)
        ctx.Fail("coverage: added %d, grown %d, fetched %d, no description %d, refused %d", added, grown, fetched, noDesc,
                 refused);
}

DIFF_TEST(LotImposterServices_Command, 0x366f10)
{
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using Fn = void(__thiscall *)(void *, void *);
    const Fn orig = ctx.Original<Fn>(0x366f10),
             ours = reinterpret_cast<Fn>(MemberAddress(&nTSSG::cObjectImposterCommand::Execute));
    static const int32_t kCounts[] = {0, 1, 2, 3, 5, 6, 7, -1, 0x102, 0x106, 2, 6};
    int ran[3] = {}, filled = 0;

    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x366f10u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        std::vector<CommandScript> steps(1 + draw(6));
        for (CommandScript &s : steps) {
            s.count = kCounts[draw(12)];
            s.kind = draw(6) == 0 ? 1 + draw(2) : 0;
            for (unsigned &w : s.words) w = draw(kWordCount);
        }
        Blob outcome[2];
        long long blocks[2] = {};
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            ResetAll();
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                auto *defs = new nTSSG::cImposterDefinitions();
                alignas(4) uint8_t command[0x10];
                std::memset(command, 0xc7, sizeof(command));
                PutW(command, 0xc, P(defs));
                for (const CommandScript &s : steps) {
                    gK = s;
                    F(fArgs).Set(10, "pdd", 0, &Arguments);
                    uint32_t escaped = 0;
                    try {
                        (side ? ours : orig)(command, &F(fArgs));
                    } catch (const OtherFailure &f) {
                        escaped = 0x100 | f.code;
                    } catch (...) {
                        escaped = 0xbad;
                    }
                    difftest::NotCounted bookkeeping;
                    b.insert(b.end(), gEv.begin(), gEv.end());
                    gEv.clear();
                    b.push_back(0xeeee), b.push_back(escaped);
                    for (uint32_t off = 0; off < 0x10; off += 4) b.push_back(off == 0xc ? W(command, off) == P(defs) : W(command, off));
                    DumpDefinitions(b, *defs);
                    if (side) {
                        ++ran[s.kind];
                        filled += !defs->mDefinitions.empty();
                    }
                }
                delete defs;
            }
            blocks[side] = counter.Blocks();
            CaptureFakes(b);
        }
        Same(ctx, outcome[0], outcome[1], "command", seed);
        if (blocks[0] != blocks[1]) ctx.Fail("seed %u: heap blocks left %lld vs %lld", seed, blocks[0], blocks[1]);
        if (ctx.failures.size() >= 6) return;
    }
    if (ran[0] < 1000 || ran[1] < 200 || ran[2] < 200 || filled < 500)
        ctx.Fail("coverage: answered %d, cError %d, other %d, definitions made %d", ran[0], ran[1], ran[2], filled);
}

DIFF_TEST(LotImposterServices_Apply, 0x365410)
{
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn found(0x3f7f40, &nTSSG::cDesignModeNode::SetFromObject, &Stand7f40);
    shape::StandIn load(0x3f5b80, &cDesignModes::LoadFromObject, &Stand5b80);
    shape::StandIn map(0x3f83d0, &nTSSG::cImposterDefinitions::Map, &Stand83d0);
    shape::StandIn take(0x3f7c80, &nTSSG::cDesignModeNode::Take, &Stand7c80);
    using ApplyFn = uint32_t(__thiscall *)(void *, void *, void *);
    const ApplyFn orig = ctx.Original<ApplyFn>(0x365410),
                  ours = reinterpret_cast<ApplyFn>(MemberAddress(&Mgr::ApplyDefinition));
    using CtorFn = void *(__thiscall *)(void *);
    const CtorFn ctorO = ctx.Original<CtorFn>(0x362950), ctorU = reinterpret_cast<CtorFn>(&nTSSG::ConstructDesignModes);
    using DtorFn = void(__thiscall *)(void *);
    const DtorFn dtorO = ctx.Original<DtorFn>(0x335a60);
    int paths[3] = {};

    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 2246822519u + 0x365410u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        ApplyScript script{kAnswers[draw(7)], kAnswers[draw(7)], kAnswers[draw(7)], draw(4), draw(4)};
        const bool construct = draw(4) == 0;
        Blob outcome[2];
        long long blocks[2] = {};
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            ResetAll();
            gA = script;
            gLoaded = gMapped = 0;
            difftest::AllocationCounter counter;
            gApplyCounter = &counter;
            struct Unset {
                ~Unset() { gApplyCounter = nullptr; }
            } unset;
            uint8_t *o;
            alignas(4) uint8_t target[0x4c];
            o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
            std::memset(o, 0xc5, Mgr::kSize);
            std::memset(target, 0xd3, sizeof(target));
            AddRange(target, sizeof(target), 0x7a000000);
            uint32_t result = 0;
            if (construct) {
                alignas(4) uint8_t raw[0x48];
                std::memset(raw, 0xe1, sizeof(raw));
                void *r = (side ? ctorU : ctorO)(raw);
                Ev(r == raw);
                for (uint32_t s = 0; s < 0x40; s += 0x20) {
                    Ev(Norm(W(raw, s))), Ev(raw[s + 4]), Ev(W(raw, s + 0x14)), Ev(W(raw, s + 0x18)), Ev(W(raw, s + 0x1c));
                }
                const uint32_t head = W(raw, 0x40);
                Ev(Norm(head)), Ev(W(raw, 0x44));
                Ev(W(reinterpret_cast<void *>(uintptr_t(head)), 0) == head), Ev(W(reinterpret_cast<void *>(uintptr_t(head)), 4) == head);
                Ev(W(reinterpret_cast<void *>(uintptr_t(head)), 8) == head);
                Ev(reinterpret_cast<const uint8_t *>(uintptr_t(head))[12]), Ev(reinterpret_cast<const uint8_t *>(uintptr_t(head))[13]);
                dtorO(raw);
            } else {
                result = (side ? ours : orig)(o, &F(fObject), target) & 0xff;
                if (side) ++paths[(script.found & 0xff) ? 0 : (script.mapped & 0xff) ? 2 : 1];
            }
            Blob &b = outcome[side];
            b = gEv;
            b.push_back(0xeeee), b.push_back(result);
            for (uint32_t off = 0; off < 0x4c; off += 4) b.push_back(W(target, off));
            for (uint32_t off = 0; off < Mgr::kSize; off += 4) b.push_back(W(o, off));
            ::operator delete(o);
            blocks[side] = counter.Blocks();
        }
        Same(ctx, outcome[0], outcome[1], construct ? "construct" : "apply", seed);
        if (blocks[0] != blocks[1]) ctx.Fail("seed %u: heap blocks left %lld vs %lld", seed, blocks[0], blocks[1]);
        if (ctx.failures.size() >= 6) return;
    }
    if (paths[0] < 150 || paths[1] < 150 || paths[2] < 150)
        ctx.Fail("coverage: found %d, not mapped %d, mapped %d", paths[0], paths[1], paths[2]);
}
