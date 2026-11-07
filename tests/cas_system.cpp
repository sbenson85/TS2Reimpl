// Differential test for nTSCASCore::cTSCASSystem (lanes IU, JI; src/cas/cTSCASSystem.h): the class in the standalone shape.
//
// CASSystem_Run: a system built by the original constructor (0x4ff850, game vtables 0xdce5f4 / 0xdce62c / 0xdce648 /
// 0xdce6ec) or the original creator (0x502860) against one built by ours (the constructor hook's adapter, or Create), each
// filled with the same state (written through our layout on both sides: the maps, lists and vectors are MSVC 14.40
// containers like the game's) and driven through its own vtables by the same random sequence of operations, then destroyed
// by the last Release (cRZUnknownMT's Release through the +4 face's slot 3: the thunk 0x50067a, the deleting destructor
// 0x5006d0). Operations (every slot of every face): QueryInterface 0x50a910 (through the +4 / +0x1c / +0x20 faces the
// thunks 0x50a94d / 0x50a955 / 0x50a95d), AddRef / Release (folded; their thunks), the service slots 3..7, 11, 12 (the base's,
// folded), Init 0x5049d0, Shutdown 0x50bb10, OnTick 0x506720, DoMessage 0x5015c0, and the cITSCASSystem slots 3..39:
// GetEditedSimId / SetEditedSimId (folded 0x145770 / 0x149c60), SetEditedSim 0x50b590, RunOutfitJob 0x50b750, IsIdle
// 0x5008b0, GetResult 0x50b3d0, IsNotDefault 0x504aa0, IsCustomOrNotDefault 0x500cd0, Slot16 0x50a980, ResetCaches
// 0x500d00, LoadResource (thunk 0x50a970, body 0x502dc0; the body also called directly: the hook's adapter against the
// original), Slot37 0x501d20; lane JL's: FillTypeCaches (slot 18) 0x506a10, ReloadOutfits (21) 0x500d30, HasFragments
// (22) 0x504910, QueryFragments (23) 0x5039b0 (cache hits through replayed queries and near misses), FilterFragments (25)
// 0x50a590, ApplyParts (33) 0x505de0, and the bodies game code also calls directly, each through its hook's adapter
// against the original (ops 60..63) as well as from the slots: AllowCustomContent 0x5008e0, GatherByType 0x509b90,
// GatherFromBin 0x50a1d0, FillTypeCache 0x50bc00, LoadByKeyType 0x502c00; reached only from those: GatherItem 0x506c80, the
// bin filter cTSCASBinFilter (constructor 0x4ff7c0, Accept 0x504a70) and cTSDuplicateObjectFragmentFilter's Accept 0x505660
// (the bin catalog's fake calls both filters' slot 3 on each bin and fragment and logs the answers).
// Lane JM's: the filter slots GetCosmetics (26) 0x50a0c0, GetArchetypes (27) 0x509f60, GetModifiers (28) 0x50a4f0,
// GetSkinTones (29) 0x50a790, GetBodyOptions (30) 0x50a000, GetSkinFragments (31) 0x50a840 (QueryFragments reaches it too),
// with the filters they build, reached from them: cTSAGSFragFilter's AcceptProperties 0x505750 (one body in the four AGS
// vtables) and AcceptFragment 0x504fa0 (also called directly, op 64), the Accept / AcceptItem pairs of
// cTSArchetypesFragFilter 0x505080 / 0x504ce0, cTSModifierFragFilter 0x505680 / 0x504e30, cTSSkinTonesFragFilter 0x5056c0 /
// 0x504e80, cTSBodyOptionsFragFilter 0x5052f0 / 0x504d80, cTSBinSkinFragFilter 0x5050c0 / 0x504d30 (MatchesPart 0x505a60,
// also called directly, op 68), cTSCosmeticsFragFilter 0x505330 / 0x504de0 (GetFilter 0x500cc0, MatchesProperties
// 0x505bc0; it and AcceptItem also called directly, op 69), the overlay filter cTSOverlayFragmentFilter (constructor 0xb645f0, Accept 0xb64a90, Prepare 0xb64c90) and its
// parts cTSOutfitDataFragmentFilterBase (constructor 0xb646e0, SetDropDuplicates 0xb648d0, Matches 0xb647d0: also called
// directly, op 66), the texture filter's Accept 0xb64b10 and the mesh filter's 0xb64960 (also called directly, op 67), and
// IsZeroKey 0x504cb0 (also called directly, op 65).
// Lane JN's: FindBinByName (19) 0x501dd0 with its bin filter cTSBinNameFilter (Accept 0x504a30; the bin filters' shared
// QueryInterface 0xb64c40, AddRef 0x148fb0 and Release 0xb64c70 reached through it and cTSCASBinFilter), FillNames (20)
// 0x506ee0 (both name styles, the fixed-names branch, 44 entries per kind), FindSkinPart (32) 0x502e40 (also from ApplyParts)
// and FindMeshOverlay (34) 0x503580 (the named-key sets filled and answered across operations).
// Lane JO's: PickRandomOutfit (39) 0x5028d0 (no definition, 0 / 1 / 2 / 3 / 6 keys, loads failing, masks, seeds kept
// through Seed's stand-in for that op), ApplySpecialOutfit (10) 0x5009b0 (stand-ins for 0x54a130 / 0x54ac80 / 0x503e20,
// the options object counted through a virtual base), ShowFace (36) 0x501730 (also from slots 37 and 10; stand-ins for
// 0x20df26 / 0xaeba70, Sprintf's "%d:%d:%d:") and FindNamedPart 0x501f80 (from ShowFace, and directly: op 70).
// Lane JP's: FindFaceOverlay (38) 0x504230 (both flag modes, the face search as ShowFace's, the overlay count and entries
// through cRZString::Sprintf's "%s%d" stand-in, GUID texts in four shapes, overlays found by their texture or mesh face) and
// ApplyMeshOverlay (35) 0x505fb0 (zero and non-zero keys, parts 13 and single bits, meshes of type 8 or not, slot 34 from it).
// Lane JQ's: RefreshContent (14) 0x50a9f0 (content keys of every sorted type and their neighbours, parts and outfit records,
// the not-default and type caches, also from Slot16) and FixOutfitParts (9) 0x502490 with FixOutfitPart 0x502120 (both
// pick modes, misfits by each test, the bit 0x100 draw, the dialog); stand-ins for 0x549f80 / 0x54b250 / 0x54a990 /
// 0x20e031 and the match filter's constructor 0xb64d90. CASSystem_Refresh draws those ops more often.
// Lane JS's: CreateSim (5) 0x5011d0 and CreateChild (6) 0x500e50 (the app's factory, world and the parents' descriptions as
// the test's own fakes; kinds by age and by a parent drawn at random; queued, or stepped at once with the outfit job, whose
// constructor 0x4ff710 and destructor 0x500180 they and OnTick / RunOutfitJob reach, as the queued new sim's copy 0x4ff570);
// CASSystem_Create draws them more often. No slot is forwarded any more.
//
// Every object the system reaches is a recording fake (tests/ts_services_fixture.h): the CAS UI (cell 0x10faf70; its slot
// 84 through a wider table), the resource service (0x10faf74), the persist system (0x10faf6c, and LoadResource's own cache
// 0x10dac1c, empty now and then), the bin catalog (0x10faf78, empty now and then: the getter asks the framework, which has
// none), the skin service (0x10faf80), resources and their keyed faces, messages, sim descriptions, job objects (their slot
// 112 through a wider table) and the framework. Stood in for on both sides (game code our class still calls): the job
// steppers 0x507600 / 0x5083b0 / 0x508c60 (all three ours now, each with its own stepper test below), the app options getter 0x20df9a (lane JS: CAS mode's EnterCASMode 0x50b960 / LeaveCASMode 0x50b830 are ours, from DoMessage), the custom-content test 0x54d600,
// the zero key 0x4f8e50, a key's text 0x4fdd30; and code that is ours and verified elsewhere: cRZRandom::Seed 0x1b8ecb, the message walkers
// 0x1c0458 / 0x1c081e, the framework object 0x1ae18d, cTSString's loading constructor 0x1d0df0 and cRZString::Sprintf
// 0x1ad6f7 (FillNames' callees). The image's clock() (0x1ba00b, library code; its runtime is not initialised in the harness) gives a
// constant: Init's seed is the runtime's clock reading on each side, so Seed's stand-in files every non-default seed as one
// value (only the call and its object are compared). The game's operator new / delete (0x824632 / 0x824624) are ours.
//
// Compared after every operation: the result (al where the slot returns a bool), the event log, every field (maps and lists
// in order with their keys and values, vectors with their capacity), the out-parameters and every fake's count and words;
// at the end the counts after the destruction.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "cas/cTSCASSystem.h"
#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZRandom.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSSimDNA.h"
#include "ts/cTSString.h"

#include <cstdarg>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <random>
#include <utility>
#include <vector>

namespace cassys_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{2, "dd"}, {9, "dd"}, {11, "d"}, {12, "d"}, {13, "d"}};

using namespace nTSCASCore;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSCASSystem;

constexpr uint32_t kTag = 0x7c000000;    // the system
constexpr uint32_t kKeyTag = 0x6b000000; // the key pool

enum : unsigned {
    kFw = 0, kUI, kRes, kPersist, kCatalog, kSkin, kResource, kKeyed, kMapped, kMsg, kMsgFace,
    kSim0 = 11,   // 11..13
    kRef0 = 14,   // 14..16 (job objects: slot 28, and slot 112 through the wide table)
    kObj0 = 17,   // 17..18 (what a sim's slot 14 hands out)
    kOutfit0 = 19, // 19..20 (outfits, never counted; ReloadOutfits: 19's / 20's slot 10 the part, 20's slot 67 kAnswer)
    kTarget = 21,  // ApplyParts' target (slots 22, 23, 28, 29; 73 and 82 through a wider table)
    kAnswer = 22,  // what slot 32 answers (its slot 3: kInner; slot 11: ReloadOutfits' instance)
    kInner = 23,   // its slot 3(buffer): a key
    // Second roles: kRes is also the app (cell 0x10cc288; its slot 10: the outfit table kKeyed, whose slot 17 finds an
    // outfit), kSkin also the settings service (its slot 9: "allowCustomContent").
};
constexpr unsigned kSims = 3, kRefs = 3;

// CreateSim / CreateChild (slots 5 / 6, lane JS): fakes of the test's own (the fixture's 24 are all used): the app's
// factory (slot 16: a parent's description; slot 20: a new description), the app's world (slot 5: its kind) and the
// parents' descriptions (slot 19: a kind bit). Pointers to them log as kMakeTag | offset; their counts are dumped.
// Also CAS mode's (EnterCASMode / LeaveCASMode): the app options (0x20df9a's stand-in; slots 15 / 16) and the display
// settings (the app's slot 21; slots 13 / 14, raw: they log the names' texts).
enum : unsigned { kFactory, kWorld, kParentA, kParentB, kOptions, kDisplay, kMakes };
bool gModeOp; // DoMessage (enter / leave CAS): the settings service is always there (the original uses it unchecked)
constexpr uint32_t kMakeTag = 0x6d000000;
Obj gMake[kMakes];
bool gCreateOp;  // ops 5 / 6: the app's slot 10 is the factory; the outfit stand-in may give the job a sim
void *gWorld;    // the app's slot 23 during the operation (the same on every call)

SkinKey gKeys[8];
SkinKey gZero; // the stand-in's zero key (inside the pool's range)
std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }
cIGZUnknown *U(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&F(i)); }
uint32_t Self(const void *p, std::source_location where = std::source_location::current()) { return svcfx::Norm(P(p), where); }
void **RawRef(CASRef &ref) { return reinterpret_cast<void **>(&ref); }

// ---- the wide tables (slots past the fixture's 80) ----

void *gWide[6 + kRefs][120];
uint32_t __fastcall TargetSlot73(Obj *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(73);
    Ev(a);
    Ev(b);
    Ev(svcfx::Norm(c));
    if (svcfx::Norm(c) == 0x5ac00000) // a key on the caller's stack (FixOutfitPart, lane JQ): by its contents
        for (unsigned i = 0; i < 4; ++i) Ev(reinterpret_cast<const uint32_t *>(uintptr_t(c))[i]);
    return 0;
}
uint32_t __fastcall TargetSlot82(Obj *self, void *, uint32_t a, uint32_t b)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(82);
    Ev(a);
    Ev(b);
    return 0;
}
uint32_t __fastcall UISlot84(Obj *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(84);
    Ev(a); // data (the test's random words)
    Ev(b);
    Ev(c);
    return 0;
}
// A caller's filter's AcceptItem (kMapped's slot 1): the item is an element of a type cache (0x509b90 passes the
// cache entry's own address), a heap address whose value depends on the allocation history, so the item's contents
// are logged (its nine references and flag), not its address.
uint32_t __fastcall MappedAcceptItem(Obj *self, void *, const CASItem *item)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(1);
    for (const CASRef &ref : item->refs) Ev(svcfx::Norm(P(ref.Get())));
    Ev(item->flag);
    return 0x100u | Draw(2);
}
uint32_t __fastcall RefSlot112(Obj *self, void *)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(112);
    return 0;
}
void Widen(unsigned fake, void **table, unsigned slot, const void *fn)
{
    std::memcpy(table, F(fake).table, sizeof(F(fake).table));
    for (unsigned i = svcfx::kSlots; i < 120; ++i) table[i] = nullptr;
    table[slot] = const_cast<void *>(fn);
    F(fake).vt = table;
}

// ---- fake slots ----

uint32_t DrawBool(Obj *, uint32_t *) { return 0x100u | Draw(2); }
// No service (the out-parameter stays null), but the settings now and then (kSkin's second role).
uint32_t FrameworkQuery(Obj *, uint32_t *a)
{
    if (a[0] != 0x0ac1887eu || a[1] != 0x4bb0d269u || (!gModeOp && Draw(4) == 0)) return 0;
    ++F(kSkin).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kSkin);
    return 1;
}
uint32_t SettingValue(Obj *, uint32_t *a) // slot 9: (name, AnyBoolean): the byte at +4, not always found
{
    Ev(*reinterpret_cast<const GZServiceString *>(uintptr_t(a[0])) == "allowCustomContent");
    if (Draw(4) == 0) return 0x200;
    reinterpret_cast<uint8_t *>(uintptr_t(a[1]))[4] = uint8_t(Draw(3)); // 0, 1, or 2 (a raw byte)
    return 1;
}
// ---- the gathers' world: the catalog's slots 19 / 30 / 32, fragments and bins (kRef0..2, kObj0..1), their keys ----
constexpr unsigned kPool[] = {kRef0, kRef0 + 1, kRef0 + 2, kObj0, kObj0 + 1};
SkinKey gFragKeys[12]; // fragments' keys: every type LoadByKeyType knows, and others
uint32_t CallAccept(uint32_t filter, Obj *o) // a filter's slot 3 (bin or fragment)
{
    void *const f = reinterpret_cast<void *>(uintptr_t(filter));
    return reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>((*static_cast<void ***>(f))[3])(f, o) & 0xff;
}
uint32_t CatalogTypeBin(Obj *, uint32_t *) { return Draw(3) ? P(&F(kPool[Draw(5)])) : 0; } // slot 19(type)
bool gOurs;
uint32_t CatalogByType(Obj *, uint32_t *a) // slot 30: (&fragments, bin filter, fragment filter)
{
    if (svcfx::gTrace) fprintf(stderr, "catalog slot 30 (%s)\n", gOurs ? "ours" : "orig");
    for (unsigned n = Draw(6), i = 0; i < n; ++i) {
        Obj *const o = &F(kPool[Draw(5)]);
        const uint32_t bin = CallAccept(a[1], o);
        const uint32_t fragment = CallAccept(a[2], o);
        Ev(0xca7a0000 | bin << 8 | fragment);
        if (bin && fragment) reinterpret_cast<ServiceRefVector *>(uintptr_t(a[0]))->PushBack(o);
    }
    return 0;
}
uint32_t CatalogByBin(Obj *, uint32_t *a) // slot 32: (bin, &fragments, fragment filter)
{
    for (unsigned n = Draw(6), i = 0; i < n; ++i) {
        Obj *const o = &F(kPool[Draw(5)]);
        const uint32_t fragment = CallAccept(a[2], o);
        Ev(0xca7b0000 | fragment);
        if (fragment) reinterpret_cast<ServiceRefVector *>(uintptr_t(a[1]))->PushBack(o);
    }
    return 0;
}
// FindBinByName (slot 19, lane JN): the catalog's slot 14 (&bins, the name filter): the bins the filter accepts (and now
// and then one it did not); a bin's key (slot 16: zero, one word set, or a pool key), its id (slot 29: DrawParts, bits
// above the short now and then), its QueryInterface (kRef0..2's slot 0), and its string resource's entry (kMsgFace's
// slot 7: (&string, id, -1); one string serves every bin, so what it held before is logged).
uint32_t CatalogBins(Obj *, uint32_t *a)
{
    for (unsigned n = Draw(6), i = 0; i < n; ++i) {
        Obj *const o = &F(kPool[Draw(5)]);
        const uint32_t accepted = CallAccept(a[1], o);
        Ev(0xca7c0000 | accepted);
        if (accepted || Draw(4) == 0) reinterpret_cast<ServiceRefVector *>(uintptr_t(a[0]))->PushBack(o);
    }
    return 0;
}
SkinKey gBinKeys[11];
uint32_t BinKey(Obj *, uint32_t *) { return Draw(4) ? P(&gBinKeys[Draw(6)]) : P(&gKeys[Draw(8)]); }
// A part's slot 6 key (slots 32 / 34): zero, a pool key, or one of the strict test's edges ({1..3 or 5 or 6, 0, 0, 0}, and
// small types with another word set).
uint32_t PartKey6(Obj *, uint32_t *)
{
    static const unsigned kEdges[] = {1, 6, 7, 8, 9, 10};
    return Draw(2) == 0 ? P(&gBinKeys[kEdges[Draw(6)]]) : Draw(3) == 0 ? P(&gZero) : P(&gKeys[Draw(8)]);
}
uint32_t BinQuery(Obj *, uint32_t *a) // a bin's slot 0: (iid, &out)
{
    if (Draw(3) == 0) return 0x200;
    ++F(kObj0).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kObj0);
    return 1;
}
const char *const kBinNames[] = {"name", "NaME", "names", "", "a bin name longer than fifteen", "nam"};
uint32_t BinNameAt(Obj *, uint32_t *a)
{
    std::string &text = reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString;
    Ev(uint32_t(text.size()));
    Ev(text.empty() ? 0u : uint32_t(uint8_t(text[0])));
    text = kBinNames[Draw(6)];
    return 0x100;
}
// FindSkinPart / FindMeshOverlay (slots 32 / 34, lane JN): the skin catalog's (kRes's) FindType1 (slot 11), FindType3
// (17) and FindType5 (22), through kRes's wide table, each appending candidates (kOutfit0..1, kAnswer: slot 3 kInner, the
// set: 19 / 20 masks, 25 flags, 3(buffer) its key; 4..8 keys, 7 / 10 parts) to the vector it is handed (its size before
// is logged: slot 32 hands FindType1 one vector again for every type). FindType1 and FindType3 take the masks as bytes
// (uint8_t in cITSSkinCatalog, src/cas/cTSSkinCatalog.h; the catalog's 0xb7d700 / 0xb7b780 read only those): slot 32
// pushes registers whose other bytes are left over (0x5032c3 pushes edx after `mov dl, [ebp + 0x14]`; 0x503440 a dword
// whose low byte alone was written at 0x503418), so only the bytes are compared.
constexpr unsigned kCandidates[] = {kOutfit0, kOutfit0 + 1, kAnswer};
bool gPartMode; // ops 32 / 33: kResource is a skin part whose slot 3 is kInner (its set), not itself (the filters' view)
uint32_t ResourceSet(Obj *, uint32_t *) { return gPartMode ? P(&F(kInner)) : P(&F(kResource)); }
void AppendCandidates(void *out)
{
    auto *v = static_cast<SkinPtrVector *>(out);
    Ev(uint32_t(v->size()));
    for (unsigned n = Draw(4), i = 0; i < n; ++i) v->push_back(&F(kCandidates[Draw(3)]));
}
uint32_t __fastcall SkinFindType1(Obj *self, void *, void *out, uint32_t ages, uint32_t genders, uint32_t keyA, uint32_t keyB,
                                  uint32_t flags, uint32_t groups, uint32_t mask, uint32_t key)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 11u, svcfx::Norm(P(out)), ages & 0xff, genders & 0xff,
                       svcfx::Norm(keyA), svcfx::Norm(keyB), flags, groups, mask, key})
        Ev(w);
    AppendCandidates(out);
    return 0;
}
uint32_t __fastcall SkinFindType3(Obj *self, void *, void *out, uint32_t ages, uint32_t genders, uint32_t flags, uint32_t mask,
                                  uint32_t key)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 17u, svcfx::Norm(P(out)), ages, genders & 0xff, flags, mask, key})
        Ev(w);
    AppendCandidates(out);
    return 0;
}
uint32_t __fastcall SkinFindType5(Obj *self, void *, void *out, uint32_t ages, uint32_t genders, uint32_t flags, uint32_t keyA,
                                  uint32_t keyB, uint32_t id, uint32_t mask22, uint32_t mask)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 22u, svcfx::Norm(P(out)), ages, genders, flags, svcfx::Norm(keyA),
                       svcfx::Norm(keyB), svcfx::Norm(id), mask22, mask})
        Ev(w);
    AppendCandidates(out);
    return 0;
}
uint32_t TypeMask(Obj *, uint32_t *) // slot 12 of a bin
{
    static const uint32_t kMasks[] = {1,    8,      0x10,   0x80,    0x20000, 0x8000, 0,      0xffffffff,
                                      0x200, 0x20,  0x40,   0x2000,  0x4000,  0x10000, 0x21};
    return kMasks[Draw(15)];
}
bool gMeshKeys; // op 67: half the fragments mesh overlays (gFragKeys[2]), so a key comes round again
// RefreshContent (slot 14, lane JQ; ops 14 / 16): the content's keys (every type it sorts, the switch's neighbours, others),
// which outfit records also name (a record's slot 21 during those ops).
constexpr unsigned kContentKeys = 24;
SkinKey gContentKeys[kContentKeys];
bool gContentOp;   // ops 14 / 16
bool gNoContent;   // op 16 without a bin catalog: the content does not load (the original would use the null catalog)
// Half the time one of the four kept-type keys (outfits 8 / 11, parts 10 / 12), so two outfit records can share a kept key
// (slot 14 erases a matched key: the second record is skipped). Records' keys only: from the record listing (kKeyed's slot
// 19 with the record type) to the skin service's slot 22 after it; GatherByType's fragments keep the plain draw.
bool gRecordPhase;
const SkinKey *ContentKey()
{
    static const unsigned kKept[] = {8, 10, 11, 12};
    return &gContentKeys[Draw(2) ? kKept[Draw(4)] : Draw(kContentKeys)];
}
uint32_t FragmentKey(Obj *, uint32_t *) // slot 21
{
    if (gContentOp) return P(gRecordPhase ? ContentKey() : &gContentKeys[Draw(kContentKeys)]);
    if (gMeshKeys && Draw(2)) return P(&gFragKeys[2]);
    return P(&gFragKeys[Draw(12)]);
}
uint32_t Flags27(Obj *, uint32_t *)                                         // slot 27: a short (and a dword past it)
{
    static const uint32_t kFlags[] = {0, 1, 2, 3, 4, 0x10000, 0x100};
    return kFlags[Draw(7)];
}
uint32_t DrawMask(Obj *, uint32_t *) { return Draw(6) ? 1u << Draw(4) : 0u; } // a property set's age / gender / flags
// The resource service's slot 37 (key, iid, &out): not found now and then; found, the resource (kResource: a fragment's
// property set, a skin part, whose slot 3 is its own property set), or now and then found without one.
// The overlays' mesh / texture resources (iids 0x0c1fe23e / 0x2c1fd89d) are kAnswer, and always an object when found: the
// mesh and texture filters read the out-parameter without a null test (0xb649d9, 0xb64b89).
uint32_t ResourceLoad(Obj *, uint32_t *a)
{
    if (Draw(4) == 0) return 0x200;
    const bool overlay = a[1] == 0x0c1fe23eu || a[1] == 0x2c1fd89du;
    if (!overlay && Draw(8) == 0) return 1;
    const unsigned which = overlay ? kAnswer : kResource;
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(which);
    return 1;
}
uint32_t DrawParts(Obj *, uint32_t *) // a skin part's slot 7: a short (bits above it now and then)
{
    static const uint32_t kParts[] = {0, 1, 2, 0x8000, 0xffff, 0x10000, 0x10004};
    return kParts[Draw(7)];
}
uint32_t DrawFlags25(Obj *, uint32_t *) { return Draw(3) ? 0u : Draw(2) ? 1u << Draw(4) : 3u << Draw(3); } // slot 25

uint32_t OutfitTable(Obj *, uint32_t *) { return gCreateOp ? P(&gMake[kFactory]) : Draw(5) ? P(&F(kKeyed)) : 0; } // app 10
uint32_t FindOutfit(Obj *, uint32_t *) { return Draw(4) ? P(&F(kOutfit0 + Draw(2))) : 0; } // the table's slot 17
uint32_t OutfitPart(Obj *, uint32_t *) { return Draw(4) ? P(&F(kOutfit0 + 1)) : 0; }      // an outfit's slot 10
uint32_t PersistOutfit(Obj *, uint32_t *a) // slot 34: (key, iid, &out); the key logged
{
    const auto *key = reinterpret_cast<const SkinKey *>(uintptr_t(a[0]));
    Ev(key->type);
    Ev(key->group);
    Ev(key->instance);
    Ev(key->resource);
    if (a[1] == 0x3b2271bbu) { // RefreshContent's content (lane JQ): kMsg (its slot 3 the key lister kKeyed)
        if (gNoContent || Draw(5) == 0) return 0x200;
        ++F(kMsg).refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kMsg);
        return 1;
    }
    if (Draw(3) == 0) return 0;
    ++F(kResource).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kResource);
    return 1;
}
extern bool gMesh35;
extern SkinKey gFaceKeys[4];
bool InFaceKeys(uint32_t key) { return key >= P(gFaceKeys) && key < P(gFaceKeys + 4); }
uint32_t PersistLoad(Obj *, uint32_t *a) // slot 5: (key, iid, &out, 0, 0)
{
    if (Draw(3) == 0) return 0x200;
    if (gContentOp && (a[1] == 0xebcf3e20u || a[1] == 0x0c560f33u)) { // ops 14 / 16: a listed part (a sim: slots 3, 7), a
        Obj &o = a[1] == 0xebcf3e20u ? F(kSim0 + Draw(kSims)) : F(kPool[Draw(5)]); // record (the pool: slot 21)
        ++o.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &o;
        return 1;
    }
    if (a[1] == 0xfb4cb0deu) { // a bin's string resource (FindBinByName): kMsgFace's second role
        ++F(kMsgFace).refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kMsgFace);
        return 1;
    }
    if (a[1] == 0xac598ea8u) { // an outfit (PickRandomOutfit): one of three fakes by the key's instance (so the pick shows)
        static const unsigned kOutfits[] = {kResource, kObj0, kObj0 + 1};
        const unsigned which = kOutfits[reinterpret_cast<const SkinKey *>(uintptr_t(a[0]))->instance % 3];
        ++F(which).refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(which);
        return 1;
    }
    if (gMesh35 && a[1] == 0x0c1fe23eu && InFaceKeys(a[0]) && Draw(2)) { // op 35's listed overlays: kObj0..1 too (slot 6)
        Obj &other = F(kObj0 + Draw(2));
        ++other.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &other;
        return 1;
    }
    if (a[1] == 0x0c1fe23eu || a[1] == 0xe519c92cu) { // a mesh overlay (FindMeshOverlay), a face mesh (ShowFace): kAnswer, as the resource service hands out
        ++F(kAnswer).refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kAnswer);
        return 1;
    }
    ++F(kResource).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kResource);
    return 1;
}
// PickRandomOutfit (slot 39, lane JO): the app's slot 24 is kMsg (the definitions; its slot 14(id): a sim, whose slot 67 is
// kAnswer: its slot 11 the group), and the persist system's slot 36 (&keys, group, type) lists 0, 1, 2, 3 or 6 keys (the
// instances pick the loaded fake: PersistLoad).
// Op 9's clean outfit (lane JQ): every part present (slot 44 true), no lists (slot 42), the resource service's searches
// (slots 38 / 12) find nothing, no definition to draw from, so FixOutfitParts changes nothing and answers false before
// saving.
bool gFixClean;
uint32_t FindDefinition(Obj *, uint32_t *) { return !gFixClean && Draw(5) ? P(&F(kSim0 + Draw(kSims))) : 0; }
uint32_t OutfitHas(Obj *self, uint32_t *a) { return gFixClean ? 0x101u : DrawBool(self, a); } // the outfit's slot 44
uint32_t PersistOutfitKeys(Obj *, uint32_t *a)
{
    static const unsigned kCounts[] = {0, 1, 2, 2, 3, 6};
    auto *keys = reinterpret_cast<std::vector<SkinKey, GZServiceAllocator<SkinKey>> *>(uintptr_t(a[0]));
    for (unsigned i = 0, n = kCounts[Draw(6)]; i < n; ++i) keys->push_back(SkinKey{a[2], a[1], Draw(9), Draw(2)});
    return 0;
}
// ApplySpecialOutfit (slot 10, lane JO): the sim object is kMsg (its slot 8 the factory kMsgFace, whose slot 9 makes the
// options object gVb now and then; its slot 10(id, 1) shorts), its outfit kKeyed (slots 70 / 79 log the part's key by
// contents), the items' overlays kObj0..1 (slot 17) and parts kOutfit0..1 / kAnswer (slot 3 kInner: its slot 3 writes a key,
// slot 22; slot 7), the CAS UI's slots 12 / 37 / 39, the bin catalog's slot 42.
void LogKey(const SkinKey &k);
uint32_t DrawKey(Obj *, uint32_t *);
uint32_t DrawShort(Obj *, uint32_t *) // a sim's data (slot 10): shorts of each sign, and bits above the short
{
    static const uint32_t kShorts[] = {0, 1, 0xffff, 0x7fff, 0x8000, 0x12345, 0xffff0000};
    return kShorts[Draw(7)];
}
uint32_t DrawWord(Obj *, uint32_t *) { return Draw(3) ? 0x3000u + Draw(64) : 0u; }
uint32_t InnerKey(Obj *self, uint32_t *a) // kInner's slot 3(buffer): the key copied there, its address answered
{
    const uint32_t key = DrawKey(self, a);
    *reinterpret_cast<SkinKey *>(uintptr_t(a[0])) = *reinterpret_cast<const SkinKey *>(uintptr_t(key));
    return key;
}
uint32_t PartToOutfit(Obj *, uint32_t *a) // the outfit's slot 70 / 79: (value, parts, &key)
{
    LogKey(*reinterpret_cast<const SkinKey *>(uintptr_t(a[2])));
    return 0;
}
uint32_t CatalogSlot42(Obj *, uint32_t *a) // (simId, &out, 0): an object now and then
{
    if (Draw(3) == 0) return 0;
    ++F(kObj0).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kObj0);
    return 1;
}
// The options object: its cIGZUnknown a virtual base (vbtable pointer at +4, offset 4 from there: the base at +8).
uint32_t __fastcall VbSlot8(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    for (uint32_t w : {0x0b5e0008u, Self(self), a, b, c, d, e}) Ev(w);
    return 0;
}
uint32_t __fastcall VbRelease(void *self, void *)
{
    Ev(0x0b5e0002u);
    Ev(Self(self));
    return 0;
}
void *gVbTable[9] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&VbSlot8)};
void *gVbBaseTable[3] = {nullptr, nullptr, reinterpret_cast<void *>(&VbRelease)};
const int32_t gVbOffsets[2] = {-4, 4};
struct VbFake {
    void **vt = gVbTable;
    const int32_t *vbptr = gVbOffsets;
    void **baseVt = gVbBaseTable;
} gVb;
uint32_t MakeOptions(Obj *, uint32_t *a) // the factory's slot 9: (class, iid, &out)
{
    if (Draw(4) == 0) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gVb;
    return 1;
}
// The callees still game code, stood in on both sides: 0x54a130 (the sim object for an id), 0x54ac80 (its outfit) and
// 0x503e20 (the special-outfit items: 0, 1, 2, 3 or 5, each with an overlay, a part, both or neither).
uint32_t __cdecl StandSimObject(uint32_t id)
{
    Ev(0x54a130);
    Ev(id);
    return Draw(6) ? P(&F(kMsg)) : 0;
}
uint32_t __cdecl StandSimOutfit(void *sim, void **out)
{
    Ev(0x54ac80);
    Ev(Self(sim));
    Ev(Self(out));
    if (Draw(6) == 0) return 0x100;
    if (Draw(6) == 0) return 1;
    ++F(kTarget).refs;
    *out = &F(kTarget);
    return 1;
}
uint32_t __fastcall StandSpecialItems(void *self, void *, uint32_t key, uint32_t outfit, uint32_t flag, CASItemVector *out)
{
    for (uint32_t w : {0x503e20u, Self(self), svcfx::Norm(key), Self(reinterpret_cast<void *>(uintptr_t(outfit))), flag, Self(out)})
        Ev(w);
    static const unsigned kCounts[] = {0, 1, 2, 3, 5};
    static const unsigned kParts[] = {kOutfit0, kOutfit0 + 1, kAnswer};
    for (unsigned i = 0, n = kCounts[Draw(5)]; i < n; ++i) {
        CASItem item{};
        const unsigned shape = Draw(4);
        if (shape & 1) item.refs[2].Assign(U(kObj0 + Draw(2)));
        if (shape & 2) item.refs[1].Assign(U(kParts[Draw(3)]));
        item.flag = Draw(2) != 0;
        out->push_back(item);
    }
    return Draw(6) ? 1u : 0x100u;
}
// ShowFace (slot 36, lane JO) and FindNamedPart 0x501f80: the sim kTarget (slot 24 its kind, 41 / 42 its part keys
// gFaceKeys, 28 / 29 keys), a part kResource (slot 10 writes the mesh key), the mesh kAnswer (iid 0xe519c92c; slot 52
// ready, 62 -> kInner, whose 8 is itself and 3 writes a key), the model kObj0 (0xaeba70; slot 93 through a wide table: a
// range over kOutfit0..1), the parts' name lists kSkin (slot 50; its 42 opens the name cursor kCatalog), the face's
// cursor kRes (a part's slot 54; slot 5 writes {name, id}), the name tables kSkin (0x20df26's object, its slot 5; slot 33
// answers the id), and the CAS UI's slots 5 / 48 (raw entries: the text by its contents), 28, 29, 54 / 74 (keys), 71.
uint32_t Hash(const std::string &text);
SkinKey gFaceKeys[4];
const SkinKey *gFaceList[2];
const char *const kFaceNames[] = {"face", "FACE", "Face", "hair", "fac", ""};
void *gParts[4] = {&F(kOutfit0), &F(kOutfit0 + 1), &F(kOutfit0), &F(kOutfit0 + 1)};
uint32_t gFaceId;
uint32_t SimKind(Obj *, uint32_t *) { return Draw(3) ? 1u : Draw(2) ? 0u : 2u; } // slot 24
bool gFixOp; // op 9 (FixOutfitParts, lane JQ): kTarget is the outfit
uint32_t FaceKeyList(Obj *, uint32_t *a)                                       // slot 42(2, value)
{
    // Op 9 asks slot 42(1, 1) again when FixOutfitPart answered true and reads the list unchecked (0x5025ea): none there.
    if (gFixClean) return 0;
    if (Draw(6) == 0 && !(gFixOp && a[0] == 1 && a[1] == 1)) return 0;
    static const unsigned kCounts[] = {0, 1, 2, 4};
    gFaceList[0] = gFaceKeys;
    gFaceList[1] = gFaceKeys + kCounts[Draw(4)];
    return P(gFaceList);
}
uint32_t WriteMeshKey(Obj *, uint32_t *a) // a part's slot 10(&key)
{
    *reinterpret_cast<SkinKey *>(uintptr_t(a[0])) = gKeys[Draw(8)];
    return 0;
}
uint32_t SelfRef(Obj *self, uint32_t *) { return P(self); }
uint32_t NamesOf(Obj *, uint32_t *) { return Draw(4) ? P(&F(kSkin)) : 0; } // a part's slot 50
void OpenCursor(Obj &cursor, uint32_t *out, unsigned owner)            // three counted words; a fresh walk
{
    cursor.value[0] = 0;
    cursor.value[1] = Draw(4);
    Obj *const words[] = {&cursor, Draw(2) ? &F(owner) : nullptr, &cursor};
    for (unsigned i = 0; i < 3; ++i) {
        if (words[i]) ++words[i]->refs;
        reinterpret_cast<Obj **>(uintptr_t(out[0]))[i] = words[i];
    }
}
uint32_t OpenNameCursor(Obj *, uint32_t *a) { OpenCursor(F(kMsg), a, kSkin); return 0; }  // kSkin's slot 42(&cursor)
uint32_t OpenFaceCursor(Obj *self, uint32_t *a) { OpenCursor(F(kRes), a, self->id - 200); return 0; } // slot 54
uint32_t CursorAtEnd(Obj *self, uint32_t *) { return 0x100u | uint32_t(self->value[0] >= self->value[1]); }
uint32_t CursorNext(Obj *self, uint32_t *) { return ++self->value[0]; }
uint32_t CursorName(Obj *, uint32_t *) { return P(kFaceNames[Draw(6)]); }
uint32_t CursorEntry(Obj *, uint32_t *a) // the face cursor's slot 5(&entry): {name, id}
{
    auto *entry = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    entry[0] = P(kFaceNames[Draw(6)]);
    entry[1] = 0x500u + Draw(8);
    return 0;
}
uint32_t FaceId(Obj *, uint32_t *a) // the name tables' slot 33(buffer, id): a pointer to the id (0 now and then)
{
    reinterpret_cast<uint32_t *>(uintptr_t(a[0]))[1] = P(&F(kInner)); // the entry's properties (slot 38, lane JP)
    gFaceId = Draw(3) ? 0x4000u + Draw(16) : 0u;
    return P(&gFaceId);
}
uint32_t KeyArg(Obj *, uint32_t *a) // the UI's slots 54 / 74(&key): a stack copy, by contents
{
    LogKey(*reinterpret_cast<const SkinKey *>(uintptr_t(a[0])));
    return 0;
}
uint32_t __fastcall UISlot5(Obj *self, void *, uint32_t thumbnail, const char *text) // (thumbnail, text)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 5u, Self(reinterpret_cast<void *>(uintptr_t(thumbnail))), Hash(text)})
        Ev(w);
    return 0;
}
uint32_t __fastcall UISlot48(Obj *self, void *, const char *text, uint32_t scale, uint32_t flag) // (text, 1.0f, 1)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 48u, Hash(text), scale, flag}) Ev(w);
    return 0;
}
uint32_t __fastcall ModelRange(Obj *self, void *, uint32_t out) // the model's slot 93(&range): {begin, -, end}
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 93u, Self(reinterpret_cast<void *>(uintptr_t(out)))}) Ev(w);
    static const unsigned kCounts[] = {0, 1, 2, 4};
    auto *range = reinterpret_cast<uint32_t *>(uintptr_t(out));
    range[0] = P(gParts);
    range[1] = 0xdeadu;
    range[2] = P(gParts + kCounts[Draw(4)]);
    return 0;
}
uint32_t __cdecl StandNameIdApp() // 0x20df26
{
    Ev(0x20df26);
    return P(&F(kSkin));
}
uint32_t __cdecl StandModelOf(void *mesh) // 0xaeba70
{
    Ev(0xaeba70);
    Ev(Self(mesh));
    return Draw(5) ? P(&F(kObj0)) : 0;
}
// FindFaceOverlay (slot 38, lane JP), past ShowFace's world: the face part's slot 53("face") (kOutfit0..1, raw: the name
// by contents) gives an id or none; the name tables' (kSkin) slot 19(id) a word, their slot 33 entry's +4 the properties
// kInner (FaceId), whose slot 24(name) (raw: the name by contents) answers the overlay count ("0", "1", "2", "3", "5", "6",
// "12", "-1", "x", or none) and each "cafOverlay<i>" (none one time in four, else a GUID text of a pool key or the zero key: plain, braced
// with dashes, two groups only, or with no comma, which leaves the GUID zero); the resource service's (kRes) slot 20(&vector,
// ages, genders, kind, keyA, keyB, 0, 0xffff: the sim kTarget's slots 22, 23, 24, 28, 29) lists 0..4 overlays (kObj0..1,
// kResource: their QueryInterface gives kAnswer, slot 5 a key, slot 3 kInner, whose slot 18 answers).
uint32_t __fastcall PartFaceId(Obj *self, void *, const char *name)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 53u, Hash(name)}) Ev(w);
    return Draw(5) ? 0x6100u + Draw(8) : 0u;
}
uint32_t NameTablesSlot19(Obj *, uint32_t *) { return 0x7100u + Draw(8); }
// ApplyMeshOverlay (slot 35, lane JP): the meshes are kAnswer (persist iid 0x0c1fe23e), whose slot 4 is its type during op
// 35 (8, an overlay, three times in four; else a key, as slot 32 reads it) and slot 6 its id (PartKey6: pointers that
// repeat; the listed keys' meshes are kObj0..1 half the time, so the two slot 6 calls reach different fakes); the sim
// kTarget's slot 42(0x20, bit) lists gFaceKeys, slot 82(0x20, bit) is logged (TargetSlot82) and slot
// 72(0x20, bit, &keys) logs the keys by contents (raw, wide table).
bool gMesh35;
uint32_t MeshKind(Obj *self, uint32_t *a) { return gMesh35 ? (Draw(4) ? 8u : 9u) : DrawKey(self, a); }
uint32_t __fastcall TargetSlot72(Obj *self, void *, uint32_t kind, uint32_t bit, const std::vector<SkinKey, GZServiceAllocator<SkinKey>> *keys)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 72u, kind, bit, uint32_t(keys->size())}) Ev(w);
    for (const SkinKey &k : *keys) LogKey(k);
    return 0;
}
char gOverlayText[4][80];
unsigned gOverlayNext;
const char *__fastcall OverlaySetting(Obj *self, void *, const char *name)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 24u, Hash(name)}) Ev(w);
    if (std::strcmp(name, "cafNumOverlays") == 0) {
        static const char *const kCounts[] = {"0", "1", "3", "12", "-1", "x", nullptr, "2", "5", "6"};
        return kCounts[Draw(10)];
    }
    if (Draw(4) == 0) return nullptr;
    const SkinKey &k = Draw(3) == 0 ? gZero : gKeys[Draw(8)];
    char *const text = gOverlayText[gOverlayNext++ % 4];
    switch (Draw(4)) {
    case 0: std::snprintf(text, 80, "%08x%08x%08x%08x,rest", k.type, k.group, k.instance, k.resource); break;
    case 1: std::snprintf(text, 80, "{%08x-%08x-%08x-%08x},x", k.type, k.group, k.instance, k.resource); break;
    case 2: std::snprintf(text, 80, "%08x%08x%08x%08x", k.type, k.group, k.instance, k.resource); break;
    default: std::snprintf(text, 80, "%08x%08x,", k.type, k.group); break;
    }
    return text;
}
uint32_t __fastcall OverlayList(Obj *self, void *, void *out, uint32_t ages, uint32_t genders, uint32_t kind, uint32_t keyA,
                                 uint32_t keyB, uint32_t a7, uint32_t a8) // the resource service's slot 20 (raw: 8 dwords)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 20u, Self(out), ages, genders, kind, svcfx::Norm(keyA),
                       svcfx::Norm(keyB), a7, a8})
        Ev(w);
    static const unsigned kOverlays[] = {kObj0, kObj0 + 1, kResource};
    auto *v = static_cast<std::vector<cIGZUnknown *, GZServiceAllocator<cIGZUnknown *>> *>(out);
    for (unsigned i = 0, n = Draw(5); i < n; ++i) v->push_back(U(kOverlays[Draw(3)]));
    return 0;
}
void LogKey(const SkinKey &k);
// The persist system's slot 5 through its wide table: slots 32 / 34 hand it keys inside the named-key sets' nodes (heap
// addresses that differ between the sides), so a key outside the fixture's ranges is logged by its contents.
uint32_t __fastcall PersistSlot5(Obj *self, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(5);
    const uint32_t norm = svcfx::Norm(key);
    if ((norm != key && norm != 0x5ac00000) || !key) { // a stack key (FillNames') by its contents too
        Ev(norm);
    } else {
        Ev(0x4e0de000);
        LogKey(*reinterpret_cast<const SkinKey *>(uintptr_t(key)));
    }
    for (uint32_t w : {iid, svcfx::Norm(out), a4, a5}) Ev(w);
    uint32_t args[] = {key, iid, out, a4, a5};
    return PersistLoad(self, args);
}
// FillNames (slot 20, lane JN): the sim's info (slot 4: kKeyed, none now and then; its slot 3(id): the language 0x29, the
// name style 0xcb, the fixed-names word 0x30), slot 67 (kAnswer: its slot 11 the group), 28 and 55; the description's age
// (slot 16(0x41): a short, bits above it now and then) and slot 22. The name set and the name tables are kMsgFace (the
// persist system's iid 0xfb4cb0de; a new set from the framework's slot 3, its slot 3 kSkin, whose slot 4 takes the key):
// the tables' row count (slot 5) and entries (slot 7: BinNameAt), the set's entries (slot 10: the text by its contents).
uint32_t Hash(const std::string &text)
{
    uint32_t h = 2166136261u;
    for (char c : text) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}
uint32_t SimInfo(Obj *, uint32_t *) { return Draw(6) ? P(&F(kKeyed)) : 0; }
uint32_t InfoValue(Obj *, uint32_t *a) // the info's slot 3(id)
{
    static const uint32_t kLanguages[] = {0, 1, 0xffff, 0x12345};
    static const uint32_t kStyles[] = {0x8ea1056c, 0xcf99cb2e, 0, 0x8ea1056d};
    if (a[0] == 0x29) return kLanguages[Draw(4)];
    if (a[0] == 0xcb) return kStyles[Draw(4)];
    if (a[0] == 0x30) return Draw(4) ? 0u : Draw(2) ? 1u : 0x100u;
    return 0x0bad0000u | a[0];
}
uint32_t DescriptionAge(Obj *, uint32_t *) // slot 16(0x41)
{
    static const uint32_t kAges[] = {1, 0, 2, 0x10001, 0xffff0002};
    return kAges[Draw(5)];
}
uint32_t NewNameSet(Obj *, uint32_t *a) // the framework's slot 3: (class, iid, &out)
{
    ++F(kMsgFace).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kMsgFace);
    return 1;
}
uint32_t NameSetKey(Obj *, uint32_t *a) // kSkin's slot 4: the new set's key (on the stack)
{
    LogKey(*reinterpret_cast<const SkinKey *>(uintptr_t(a[0])));
    return 0;
}
uint32_t TableRows(Obj *, uint32_t *) // a table's slot 5(0)
{
    static const uint32_t kRows[] = {0, 1, 3, 40, 0xffffffff};
    return kRows[Draw(5)];
}
uint32_t NameEntry(Obj *, uint32_t *a) // the set's slot 10: (&text, kind, index)
{
    const std::string &text = reinterpret_cast<const cRZString *>(uintptr_t(a[0]))->mString;
    Ev(uint32_t(text.size()));
    Ev(Hash(text));
    return 0;
}
uint32_t SkinAnswer(Obj *, uint32_t *a) // slot 13: (key, &short)
{
    if (Draw(3)) return 0x200;
    *reinterpret_cast<int16_t *>(uintptr_t(a[1])) = Draw(2) ? int16_t(0) : int16_t(-3);
    return 1;
}
uint32_t ResourceQuery(Obj *, uint32_t *a) // slot 0: (iid, &out)
{
    unsigned which = ~0u;
    if (a[0] == 0x8c1571d6u && Draw(2)) which = kKeyed;
    if (a[0] == 0xac598ea8u && Draw(3)) which = kMapped;
    if ((a[0] == 0x2c1fd89du && Draw(2)) || (a[0] == 0x0c1fe23eu && Draw(3))) which = kAnswer; // an overlay's faces
    if (which == ~0u) return 0x300;
    ++F(which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(which);
    return 1;
}
uint32_t DrawKey(Obj *, uint32_t *) { return Draw(3) == 0 ? P(&gZero) : P(&gKeys[Draw(8)]); }
uint32_t MessageQuery(Obj *, uint32_t *a) // slot 0: (iid, &out)
{
    if (Draw(6) == 0) return 0x200;
    ++F(kMsgFace).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kMsgFace);
    return 1;
}
uint32_t MessageId(Obj *, uint32_t *)
{
    static const uint32_t kIds[] = {0xd9, 0xda, 0xdb, 0xb5, 0x91a4f0d0, 0xd8, 0xdc, 0xb4, 0x91a4f0d1, 0};
    return kIds[Draw(10)];
}
uint32_t SimIdOf(Obj *self, uint32_t *) { return self->value[0]; }
uint32_t SimObjects(Obj *, uint32_t *a) // slot 14: (&vector) filled with counted objects
{
    auto *v = reinterpret_cast<ServiceRefVector *>(uintptr_t(a[0]));
    const unsigned n = Draw(4);
    for (unsigned i = 0; i < n; ++i) v->PushBack(&F(kObj0 + Draw(2)));
    return 0;
}
uint32_t OutfitCount(Obj *, uint32_t *) // slot 24(3): the outfit count
{
    static const uint32_t kCounts[] = {0, 1, 2, 3, 4};
    return kCounts[Draw(5)];
}
uint32_t OutfitAt(Obj *, uint32_t *a) { return a[0] % 3 == 2 ? 0x0f0f0000u + a[0] : P(&F(kOutfit0 + a[0] % 2)); }
uint32_t SourceOf(Obj *self, uint32_t *) { return 0x7000u + self->id * 16 + Draw(16); } // slot 28: the outfit source

// RefreshContent (slot 14, lane JQ): the persist system's slot 34(key, 0x3b2271bb) gives the content kMsg (PersistOutfit),
// whose slot 3 is the lister kKeyed; kKeyed's slot 19(&keys, type) lists 0..9 content keys (the content's, type 0, and the
// outfit records', 0x0c560f39, from the persist system's slot 3: kKeyed too); a listed part (0xebcf3e20) is a sim (slot 3
// kKeyed, whose slot 22 masks; slot 7 shorts around 0x80), a record (0x0c560f33) a pool object (slot 21 a content key). The
// resource service's slots 8 / 41 / 42, the bin catalog's 5 / 6 / 26, the persist system's 17 and the skin service's 22.
uint32_t ContentKeys(Obj *, uint32_t *a)
{
    auto *keys = reinterpret_cast<std::vector<SkinKey, GZServiceAllocator<SkinKey>> *>(uintptr_t(a[0]));
    gRecordPhase = a[1] == 0x0c560f39u; // the outfit records' listing (slot 14's second use)
    static const unsigned kCounts[] = {0, 1, 2, 3, 4, 6, 9};
    Ev(uint32_t(keys->size()));
    for (unsigned i = 0, n = kCounts[Draw(7)]; i < n; ++i) keys->push_back(gContentKeys[Draw(kContentKeys)]);
    return 0;
}
uint32_t EndRecords(Obj *, uint32_t *) // the skin service's slot 22(content): slot 14's record loop is over
{
    gRecordPhase = false;
    return 0;
}
uint32_t PartShort(Obj *, uint32_t *)
{
    static const uint32_t kShorts[] = {0x80, 0, 1, 0x10080, 0x81, 0x8080, 0x100};
    return kShorts[Draw(7)];
}
// FixOutfitParts (slot 9, lane JQ): 0x549f80's object kMsgFace (slot 23 now and then true), 0x54b250, 0x54a990's outfit
// kTarget (counted), 0x20e031's dialog service kInner (slot 4: kOutfit0, whose slots 20 / 15 take the sim id); kTarget's
// slots 22 / 23 masks during op 9 (0x10 its kind now and then), 29 a key for the resource service's slot 19, 42 its part
// keys (gFaceKeys), 44 / 76 / 77 / 82 / 56 (the GUID by contents) / 73 / 96 / 101; FixOutfitPart's filter is stood in for
// (0xb64d90: a counted block that logs its arguments), the resource service's slots 38 / 12 log it and answer a key or
// candidates (their slot 3 kInner, whose slot 3 writes a key).
uint32_t TargetMask(Obj *self, uint32_t *a)
{
    if (!gFixOp) return DrawKey(self, a);
    static const uint32_t kMasks[] = {0x10, 0, 1, 2, 0xffffffff, 0x11, 8, 0x30};
    return kMasks[Draw(8)];
}
uint32_t Rarely(Obj *, uint32_t *) { return Draw(4) ? 0x100u : 1u; }
uint32_t DialogAnswer(Obj *, uint32_t *a) // kOutfit0's slot 15: (simId, id, &out)
{
    if (Draw(2)) {
        ++F(kRef0).refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kRef0);
    }
    return 0;
}
uint32_t __cdecl StandObjectForId(uint32_t id) // 0x549f80
{
    Ev(0x549f80);
    Ev(id);
    return Draw(8) ? P(&F(kMsgFace)) : 0;
}
uint32_t __cdecl StandOutfitFixed(void *object) // 0x54b250
{
    Ev(0x54b250);
    Ev(Self(object));
    return Draw(6) ? 0x100u : 1u;
}
uint32_t __cdecl StandObjectOutfit(void *object, void **out) // 0x54a990
{
    Ev(0x54a990);
    Ev(Self(object));
    Ev(Self(out));
    if (Draw(8) == 0) return 0x100;
    ++F(kTarget).refs;
    *out = &F(kTarget);
    return 1;
}
uint32_t __cdecl StandDialogs() // 0x20e031
{
    Ev(0x20e031);
    return P(&F(kInner));
}
struct MatchFilter { // the stand-in's cTSOutfitMatchSkinFilter (0x80 bytes)
    void **vt;
    int32_t refs;
    uint32_t magic, outfit, kind, bit;
    uint8_t rest[0x54 - 0x18];
    void **faceVt; // +0x54
    uint8_t tail[0x7c - 0x58];
    uint8_t any; // +0x7c
    uint8_t pad[3];
};
static_assert(sizeof(MatchFilter) == 0x80 && offsetof(MatchFilter, faceVt) == 0x54 && offsetof(MatchFilter, any) == 0x7c);
uint32_t __fastcall FilterAddRef(MatchFilter *self, void *)
{
    Ev(0xf17e0001);
    return uint32_t(++self->refs);
}
uint32_t __fastcall FilterRelease(MatchFilter *self, void *)
{
    const int32_t left = --self->refs;
    Ev(0xf17e0002);
    Ev(uint32_t(left));
    if (left == 0) ::operator delete(self);
    return uint32_t(left);
}
void *gFilterVt[3] = {nullptr, reinterpret_cast<void *>(&FilterAddRef), reinterpret_cast<void *>(&FilterRelease)};
void *gFilterFaceVt[1] = {nullptr};
MatchFilter *__fastcall StandMatchFilter(MatchFilter *self, void *, void *outfit, uint32_t kind, uint32_t bit) // 0xb64d90
{
    for (uint32_t w : {0xb64d90u, Self(outfit), kind, bit}) Ev(w);
    std::memset(self, 0xee, sizeof(*self));
    self->vt = gFilterVt;
    self->refs = 0;
    self->magic = 0xf17e0000;
    self->outfit = P(outfit);
    self->kind = kind;
    self->bit = bit;
    self->faceVt = gFilterFaceVt;
    self->any = 0;
    return self;
}
void LogFilterFace(uint32_t face) // what the resource service gets: the filter's +0x54 face
{
    const auto *f = reinterpret_cast<const MatchFilter *>(uintptr_t(face) - 0x54);
    for (uint32_t w : {f->magic, Self(reinterpret_cast<void *>(uintptr_t(f->outfit))), f->kind, f->bit, uint32_t(f->any),
                       uint32_t(f->refs)})
        Ev(w);
}
uint32_t __fastcall ResSlot38(Obj *self, void *, SkinKey *out, uint32_t face) // (&key, filter face): one match
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 38u, Self(out)}) Ev(w);
    LogKey(*out);
    LogFilterFace(face);
    if (gFixClean || Draw(3) == 0) return 0x200;
    if (Draw(4)) *out = gKeys[Draw(8)];
    return 1;
}
uint32_t __fastcall ResSlot12(Obj *self, void *, void *out, uint32_t face, uint32_t a3, uint32_t a4) // (&vector, face, 0, -1)
{
    auto *v = static_cast<std::vector<void *, GZServiceAllocator<void *>> *>(out);
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 12u, Self(out), a3, a4, uint32_t(v->size())}) Ev(w);
    LogFilterFace(face);
    for (unsigned i = 0, n = gFixClean ? 0 : Draw(4); i < n; ++i) v->push_back(&F(kCandidates[Draw(3)]));
    return 0;
}
uint32_t __fastcall TargetSlot96(Obj *self, void *)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 96u}) Ev(w);
    return 0;
}
uint32_t __fastcall TargetSlot101(Obj *self, void *, uint32_t list) // (the part-key list slot 42 answered)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 101u, svcfx::Norm(list)}) Ev(w);
    return Draw(4) ? 1u : 0x100u;
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void LogKey(const SkinKey &k)
{
    Ev(k.type);
    Ev(k.group);
    Ev(k.instance);
    Ev(k.resource);
}

uint32_t gStepCalls;
void GiveRef(CASRef &ref) // a job object (counted) set by a stepper's stand-in
{
    Obj &o = F(kRef0 + Draw(kRefs));
    ++o.refs;
    *RawRef(ref) = &o;
}
// A step: still running now and then (always done by the fourth call).
bool StepDone() { return ++gStepCalls % 4 == 0 || Draw(3) != 0; }

uint32_t __fastcall StandStepChild(void *self, void *, ChildJob *job) // 0x507600
{
    Ev(0x507600);
    Ev(Self(self));
    Ev(Self(job) == 0x5ac00000); // a stack job (RunOutfitJob) or a queued one (a list node)
    Ev(job->state);
    Ev(Self(job->parentA));
    Ev(Self(job->parentB));
    Ev(job->kind);
    Ev(Self(job->sim.Get()));
    Ev(Self(job->ref18.Get()));
    Ev(Self(job->ref1c.Get()));
    if (!StepDone()) {
        ++job->state;
        return 0x101;
    }
    job->done = Draw(3) != 0;
    if (job->done && !job->ref18) GiveRef(job->ref18); // a new child's job (slot 6, lane JS): OnTick asks its slot 28
    return 0x100;
}
uint32_t __fastcall StandStepNewSim(void *self, void *, NewSimJob *job) // 0x5083b0
{
    Ev(0x5083b0);
    Ev(Self(self));
    Ev(Self(job) == 0x5ac00000); // a stack job (RunOutfitJob) or a queued one (a list node)
    Ev(job->state);
    Ev(uint32_t(job->age));
    Ev(job->arg2);
    Ev(job->arg3);
    Ev(job->arg4);
    Ev(job->outfitKind);
    Ev(job->done);
    for (const CASRef &e : job->extras) Ev(Self(e.Get()));
    Ev(Self(job->sim.Get()));
    Ev(Self(job->ref20.Get()));
    Ev(Self(job->ref24.Get()));
    Ev(uint32_t(job->extras.size()));
    if (!StepDone()) {
        ++job->state;
        return 0x101;
    }
    job->done = Draw(3) != 0;
    if (job->done && !job->ref20) GiveRef(job->ref20); // a new sim's job (slot 5, lane JS): OnTick asks its slot 28
    return 0x100;
}
uint32_t __fastcall StandStepOutfit(void *self, void *, OutfitJob *job) // 0x508c60
{
    Ev(0x508c60);
    Ev(Self(self));
    Ev(Self(job) == 0x5ac00000); // a stack job (RunOutfitJob) or a queued one (a list node)
    Ev(job->state);
    Ev(uint32_t(job->step));
    Ev(job->source);
    Ev(job->result);
    Ev(job->kind);
    Ev(job->ageIndex);
    Ev(job->genderIndex);
    Ev(Self(job->sim.Get()));
    Ev(Self(job->ref20.Get()));
    Ev(Self(job->ref24.Get()));
    Ev(job->hasKeyA);
    Ev(job->hasKeyB);
    if (job->hasKeyA) LogKey(job->keyA);
    if (job->hasKeyB) LogKey(job->keyB);
    Ev(job->reloadRecord);
    if (!StepDone()) {
        ++job->state;
        if (!job->ref24 && Draw(2)) { // the job takes an object (counted)
            Obj &o = F(kRef0 + Draw(kRefs));
            ++o.refs;
            *RawRef(job->ref24) = &o;
        }
        if (gCreateOp && !job->sim && Draw(3) == 0) { // slots 5 / 6 (lane JS): the job gets a sim (CreateSim asks its slot 10)
            Obj &o = F(kSim0 + Draw(kSims));
            ++o.refs;
            *RawRef(job->sim) = &o;
        }
        return 0x101;
    }
    job->result = Draw(2) != 0;
    return 0x100;
}

// CreateSim / CreateChild's world (lane JS).
uint32_t AppWorld(Obj *, uint32_t *) { return P(gWorld); }                                       // the app's slot 23
uint32_t WorldKind(Obj *, uint32_t *) { static const uint32_t k[] = {0x0bea28c2, 0x2c1805b0, 0x0bea28c3}; return k[Draw(3)]; }
uint32_t ParentOf(Obj *, uint32_t *) { return Draw(8) ? P(&gMake[kParentA + Draw(2)]) : 0; }   // the factory's 16(parent, 1)
uint32_t ParentKind(Obj *, uint32_t *) // a parent's slot 19: the four kinds, and the others the jump table refuses
{
    static const uint32_t k[] = {1, 2, 4, 8, 1, 2, 4, 8, 0, 3, 5, 6, 7, 9, 16};
    return k[Draw(15)];
}
uint32_t DescriptionPart(Obj *, uint32_t *) { return P(&F(kRef0 + Draw(kRefs))); } // a sim's slot 10 (its slot 28: a source)
// The factory's slot 20 (&out, kind, marked, 1, 0): `marked` is a bool pushed as a dword whose upper bytes are whatever the
// caller's stack held there (CreateChild: its kind argument's), so only its low byte is logged. A counted sim, or none.
void __fastcall MakeDescription(Obj *self, void *, void **out, uint32_t kind, uint32_t marked, uint32_t a4, uint32_t a5)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 20u, svcfx::Norm(P(out)), svcfx::Norm(P(*out)), kind, marked & 0xff, a4, a5})
        Ev(w);
    Obj *const made = Draw(6) ? &F(kSim0 + Draw(kSims)) : nullptr;
    if (made) ++made->refs;
    *out = made;
}

// CAS mode (lane JS). 0x20df9a's stand-in (both sides): the app options.
void *__cdecl StandAppOptions()
{
    Ev(0x20df9a);
    return &gMake[kOptions];
}
uint32_t OptionWord(Obj *, uint32_t *) { return (Draw(2) ? 0xabcd0000u : 0u) | (Draw(4) ? Draw(0x40) : 0xfff9u); } // slot 15
uint32_t AppDisplay(Obj *, uint32_t *) { return Draw(5) ? P(&gMake[kDisplay]) : 0; }                               // app 21
uint32_t SettingSet(Obj *, uint32_t *a) // the settings' slot 3: (name, AnyBoolean): the name and the raw byte
{
    Ev(Hash(reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]))->c_str()));
    Ev(reinterpret_cast<const uint8_t *>(uintptr_t(a[1]))[4]);
    return 1;
}
void __fastcall DisplayGet(Obj *self, void *, const char *name, int32_t *out) // slot 14: (name, &value)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 14u, Hash(name), svcfx::Norm(P(out))}) Ev(w);
    static const int32_t kValues[] = {3, 1, 0, 2, -1};
    *out = kValues[Draw(5)];
}
void __fastcall DisplaySet(Obj *self, void *, const char *name, int32_t value) // slot 13: (name, value)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 13u, Hash(name), uint32_t(value)}) Ev(w);
}
template <uint32_t Rva> void __fastcall StandThis(void *self, void *)
{
    Ev(Rva);
    Ev(Self(self));
}
uint32_t __cdecl StandIsCustom(const SkinKey *key) // 0x54d600
{
    Ev(0x54d600);
    Ev(Self(key));
    return 0x100u | Draw(2);
}
const SkinKey *__cdecl StandZeroKey() // 0x4f8e50
{
    Ev(0x4f8e50);
    return &gZero;
}
uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(Self(self));
    Ev(svcfx::Norm(arg));
    Ev(Self(obj));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(Self(self));
    Ev(svcfx::Norm(arg));
    Ev(Self(obj));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}
constexpr uint32_t kClockSeed = 0x5eed1234;
bool gSeedThrough; // op 39: the caller's seed is kept (no clock reading there; UINT32_MAX still comes from the seed control)
void __fastcall StandSeed(cRZRandom *self, void *, uint32_t seed)
{
    // Not logged: our constructor's seeding may not go through Seed (the seed control gives it kClockSeed directly). A
    // clock reading (Init: each side's own runtime) becomes one value.
    if (!gSeedThrough || seed == UINT32_MAX) seed = seed == UINT32_MAX || seed == kClockSeed ? kClockSeed : kClockSeed + 1;
    self->mIntegerSeed = seed;
    self->mRealSeed = int32_t(seed);
}
// The seed control's stand-in: a UINT32_MAX seed gets kClockSeed. Init's clock reading at the original's site 0x5049ef
// (rz/cRZRandom.h ClockSeedAt: ours asks the seed control there, as the host replaces the original's reading there) is a
// clock reading, so it gets the value StandSeed gives the original's clock reading.
uint32_t ClockSeed(int, uint32_t site) { return site == 0x5049ef ? kClockSeed + 1 : kClockSeed; }
long __cdecl ImageClock() { return 12345; }

// 0x4fdd30 (thiscall on the key, ret 4; game code): the key as text, here one of Populate's names (so the named-key maps
// answer now and then) or a new one, from the key's type and instance.
const char *const kPartNames[] = {"hair", "tops", "a", "accessory-glasses", "a name no map has before this"};
void __fastcall StandKeyName(const SkinKey *key, void *, GZServiceString *out)
{
    Ev(0x4fdd30);
    Ev(Self(key));
    *out = kPartNames[(key->type + key->instance) % 5];
}
// FillNames' callees (lane JN), on both sides: the director's framework object (0x1ae18d and ours: kFw), cTSString's loading
// constructor (0x1d0df0 and ours: a format text from the arguments; the flags are a byte, uint8_t in src/ts/cTSString.h)
// and cRZString::Sprintf (0x1ad6f7 and ours: the format and its argument by contents, then the text formatted).
void *__cdecl StandFramework()
{
    Ev(0x1ae18d);
    return &F(kFw);
}
cTSString *__fastcall StandFormatText(cTSString *self, void *, uint32_t table, uint32_t entry, uint32_t group, uint32_t flags,
                                      uint32_t type)
{
    for (uint32_t w : {0x1d0df0u, Self(self), table & 0xffff, entry & 0xffff, group, flags & 0xff, type}) Ev(w);
    static const char *const kFormats[] = {"%s", "Dr. %s", "%s the Third", ""};
    self->ConstructEmpty();
    self->mString.FromChar(kFormats[(table + entry + (flags & 0xff)) % 4]);
    return self;
}
// ShowFace's (lane JO) "%d:%d:%d:" takes three words, logged as they are.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    Ev(0x1ad6f7);
    Ev(Self(self));
    Ev(Hash(fmt));
    char text[128];
    va_list args;
    va_start(args, fmt);
    if (std::strcmp(fmt, "%s%d") == 0) { // FindFaceOverlay's (lane JP) "cafOverlay" and index
        const char *const name = va_arg(args, const char *);
        const int index = va_arg(args, int);
        Ev(Hash(name));
        Ev(uint32_t(index));
        std::snprintf(text, sizeof(text), fmt, name, index);
    } else if (std::strcmp(fmt, "%d:%d:%d:") == 0) {
        const uint32_t a = va_arg(args, uint32_t), b = va_arg(args, uint32_t), c = va_arg(args, uint32_t);
        for (uint32_t w : {a, b, c}) Ev(w);
        std::snprintf(text, sizeof(text), fmt, a, b, c);
    } else {
        const char *const arg = va_arg(args, const char *);
        Ev(Hash(arg));
        std::snprintf(text, sizeof(text), fmt, arg);
    }
    va_end(args);
    self->mString = text;
    return self;
}

// ---- the world for a seed ----

void ConfigureFakes(uint32_t seed)
{
    std::mt19937 r(seed * 7 + 3);
    svcfx::ResetAll();
    for (unsigned i = 0; i < kMakes; ++i) gMake[i].Reset(300 + i); // CreateSim / CreateChild (lane JS)
    svcfx::AddRange(gMake, sizeof(gMake), kMakeTag);
    gMake[kFactory].Set(16, "pd", 0, &ParentOf);
    gMake[kFactory].table[20] = reinterpret_cast<void *>(&MakeDescription);
    gMake[kWorld].Set(5, 0, 0, &WorldKind);
    for (unsigned i : {unsigned(kParentA), unsigned(kParentB)}) gMake[i].Set(19, 0, 0, &ParentKind);
    gMake[kOptions].Set(15, "d", 0, &OptionWord);
    gMake[kOptions].Set(16, "dd");
    gMake[kDisplay].table[13] = reinterpret_cast<void *>(&DisplaySet);
    gMake[kDisplay].table[14] = reinterpret_cast<void *>(&DisplayGet);
    F(kSkin).Set(3, "pp", 0, &SettingSet); // the settings' set (kSkin's second role)
    gCreateOp = gModeOp = false;
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeyTag);
    svcfx::AddRange(&gZero, sizeof(gZero), kKeyTag | 0x800);
    svcfx::AddRange(gFragKeys, sizeof(gFragKeys), kKeyTag | 0x1000);
    svcfx::AddRange(gBinKeys, sizeof(gBinKeys), kKeyTag | 0x2000);
    svcfx::AddRange(gContentKeys, sizeof(gContentKeys), kKeyTag | 0x5000);
    // Outfit tokens 0x0f0f0000 + n (OutfitAt, the edited outfits): opaque pointers, registered as an object of their own.
    svcfx::AddRange(reinterpret_cast<const void *>(uintptr_t(0x0f0f0000)), 0x100, 0x0f0f0000);
    F(kFw).Set(5, "ddp", 0, &FrameworkQuery);
    F(kFw).Set(9, "p", 1u);
    F(kUI).Set(3, "d", 0u);
    for (unsigned s : {12u, 39u}) F(kUI).Set(s, "d", 0u); // ApplySpecialOutfit (lane JO)
    F(kUI).Set(37, "dd-", 0u);
    F(kUI).Set(71, "d", 0u); // ShowFace (lane JO; slots 5 / 48 raw in the wide table)
    F(kUI).Set(28, "dd", 0u);
    F(kUI).Set(29, "pdddddd", 0u);
    for (unsigned s : {54u, 74u}) F(kUI).Set(s, "p", 0, &KeyArg);
    Widen(kUI,gWide[0], 84, reinterpret_cast<const void *>(&UISlot84));
    F(kRes).Set(37, "pdp", 0, &ResourceLoad);
    F(kPersist).Set(5, 5, 0, &PersistLoad);
    F(kCatalog).Set(46, "p", 0, &DrawBool);
    F(kSkin).Set(13, "pp", 0, &SkinAnswer);
    F(kResource).Set(0, "dp", 0, &ResourceQuery);
    F(kKeyed).Set(16, 0, 0, &DrawKey);
    F(kMapped).Set(17, 0, 0, &DrawKey);
    F(kMsg).Set(0, "dp", 0, &MessageQuery);
    F(kMsgFace).Set(4, 0, 0, &MessageId);
    static const uint32_t kIds[] = {5, 0xabcd0007, 0xfffe, 0x8001, 0x10005, 7};
    for (unsigned i = 0; i < kSims; ++i) {
        Obj &o = F(kSim0 + i);
        o.value[0] = kIds[r() % 6];
        o.Set(8, 0, 0, &SimIdOf);
        o.Set(41, 0, uint32_t(r()));
        o.Set(14, "p", 0, &SimObjects);
        o.Set(13, "p", 0u);
        o.Set(24, "d", 0, &OutfitCount);
        o.Set(27, "d", 0, &OutfitAt);
        o.Set(29, "d", 0, &DrawBool);
        o.Set(10, 0, 0, &DescriptionPart); // CreateSim / CreateChild (lane JS)
    }
    for (unsigned o : kPool) { // fragments and bins (set before the job objects' tables are widened: Widen copies)
        F(o).Set(12, 0, 0, &TypeMask);
        F(o).Set(21, 0, 0, &FragmentKey);
        F(o).Set(27, 0, 0, &Flags27);
        F(o).Set(16, 0, 0, &BinKey);
    }
    for (unsigned i = 0; i < kRefs; ++i) { // FindBinByName's bins (kObj0..1's slot 0 is ResourceQuery, slot 29 DrawKey)
        F(kRef0 + i).Set(0, "dp", 0, &BinQuery);
        F(kRef0 + i).Set(29, 0, 0, &DrawParts);
    }
    F(kCatalog).Set(14, "pp", 0, &CatalogBins);
    F(kMsgFace).Set(7, "pdd", 0, &BinNameAt);
    for (unsigned i = 0; i < kRefs; ++i) {
        F(kRef0 + i).Set(28, 0, 0, &SourceOf);
        Widen(kRef0 + i, gWide[1 + i], 112, reinterpret_cast<const void *>(&RefSlot112));
    }
    // ReloadOutfits: the app (kRes), its outfit table (kKeyed), the outfits and their part, the persist system's slot 34.
    F(kRes).Set(10, 0, 0, &OutfitTable);
    F(kRes).Set(23, 0, 0, &AppWorld); // CreateSim / CreateChild (lane JS): the app's world; 26 / 27 asked, not used
    F(kRes).Set(26, 0, 0x26262626u);
    F(kRes).Set(27, 0, 0x27272727u);
    F(kRes).Set(21, 0, 0, &AppDisplay); // CAS mode (lane JS): the display settings
    F(kKeyed).Set(17, "d", 0, &FindOutfit);
    for (unsigned i = 0; i < 2; ++i) F(kOutfit0 + i).Set(10, 0, 0, &OutfitPart);
    F(kOutfit0 + 1).Set(67, 0, P(&F(kAnswer)));
    F(kAnswer).Set(11, 0, uint32_t(r()));
    F(kPersist).Set(34, "pdp", 0, &PersistOutfit);
    F(kResource).Set(7, 0, 0u);
    // PickRandomOutfit (slot 39, lane JO): the definitions (kMsg's second role) and the outfit keys.
    F(kRes).Set(24, 0, P(&F(kMsg)));
    F(kMsg).Set(14, "d", 0, &FindDefinition);
    F(kPersist).Set(36, "pdd", 0, &PersistOutfitKeys);
    // ApplySpecialOutfit (slot 10, lane JO): the sim object kMsg, its factory kMsgFace, the outfit kKeyed, the overlays.
    svcfx::AddRange(&gVb, sizeof(gVb), kKeyTag | 0x3000);
    F(kMsg).Set(8, 0, P(&F(kMsgFace)));
    F(kMsg).Set(10, "dd", 0, &DrawShort);
    F(kMsgFace).Set(9, "ddp", 0, &MakeOptions);
    for (unsigned s : {70u, 79u}) F(kTarget).Set(s, "ddp", 0, &PartToOutfit);
    for (unsigned o : {unsigned(kObj0), unsigned(kObj0 + 1)}) F(o).Set(17, 0, 0, &DrawWord);
    F(kCatalog).Set(42, "dp-", 0, &CatalogSlot42);
    // ShowFace / FindNamedPart (slot 36, lane JO): set before kTarget's and kRes's tables are widened.
    svcfx::AddRange(gFaceKeys, sizeof(gFaceKeys), kKeyTag | 0x4000);
    for (SkinKey &k : gFaceKeys) k = SkinKey{uint32_t(r()), r() % 2 ? uint32_t(r()) : 0u, uint32_t(r()), r() % 3};
    F(kTarget).Set(24, 0, 0, &SimKind);
    F(kTarget).Set(41, 0, 0, &DrawWord);
    F(kTarget).Set(42, "dd", 0, &FaceKeyList);
    F(kResource).Set(10, "p", 0, &WriteMeshKey);
    F(kAnswer).Set(52, 0, 0, &DrawBool);
    F(kAnswer).Set(62, 0, P(&F(kInner)));
    F(kInner).Set(8, 0, 0, &SelfRef);
    for (unsigned o : {unsigned(kOutfit0), unsigned(kOutfit0 + 1)}) {
        F(o).Set(50, 0, 0, &NamesOf);
        F(o).Set(54, "p-", 0, &OpenFaceCursor);
    }
    F(kSkin).Set(42, "p", 0, &OpenNameCursor);
    F(kSkin).Set(5, 0, 0, &SelfRef);
    F(kSkin).Set(33, "pd", 0, &FaceId);
    F(kMsg).Set(4, "p", 0, &CursorAtEnd); // the name cursor (lane JQ: was kCatalog, whose slots 5 / 6 slot 14 now asks)
    F(kMsg).Set(5, 0, 0, &CursorName);
    F(kMsg).Set(6, 0, 0, &CursorNext);
    F(kRes).Set(4, "p", 0, &CursorAtEnd);
    F(kRes).Set(5, "p", 0, &CursorEntry);
    F(kRes).Set(6, 0, 0, &CursorNext);
    // FindFaceOverlay (slot 38, lane JP): set before kRes's table is widened; raw entries on tables never widened.
    F(kSkin).Set(19, "d", 0, &NameTablesSlot19);
    F(kInner).Set(18, 0, 0, &DrawBool);
    for (unsigned o : {unsigned(kObj0), unsigned(kObj0 + 1)}) F(o).Set(6, 0, 0, &PartKey6); // slot 35's other meshes
    F(kInner).table[24] = reinterpret_cast<void *>(&OverlaySetting);
    for (unsigned o : {unsigned(kOutfit0), unsigned(kOutfit0 + 1)}) F(o).table[53] = reinterpret_cast<void *>(&PartFaceId);
    gWide[0][5] = reinterpret_cast<void *>(&UISlot5);
    gWide[0][48] = reinterpret_cast<void *>(&UISlot48);
    // AllowCustomContent: the settings (kSkin's slot 9).
    F(kSkin).Set(9, "pp", 0, &SettingValue);
    // ApplyParts: the target, slot 32's answer and its inner object.
    for (unsigned s : {28u, 29u}) F(kTarget).Set(s, 0, 0, &DrawKey);
    for (unsigned s : {22u, 23u}) F(kTarget).Set(s, 0, 0, &TargetMask); // DrawKey but during op 9 (lane JQ)
    // FixOutfitParts (slot 9, lane JQ): kTarget as the outfit; its object kMsgFace, the dialogs kInner -> kOutfit0.
    F(kTarget).Set(44, "dd-", 0, &OutfitHas);
    F(kTarget).Set(56, "p", 0, &KeyArg);
    F(kTarget).Set(76, "dd-d");
    F(kTarget).Set(77, "pddd-");
    F(kMsgFace).Set(23, 0, 0, &Rarely);
    F(kInner).Set(4, 0, P(&F(kOutfit0)));
    F(kOutfit0).Set(20, "d");
    F(kOutfit0).Set(15, "ddp", 0, &DialogAnswer);
    Widen(kTarget, gWide[1 + kRefs], 73, reinterpret_cast<const void *>(&TargetSlot73));
    gWide[1 + kRefs][96] = reinterpret_cast<void *>(&TargetSlot96);
    gWide[1 + kRefs][101] = reinterpret_cast<void *>(&TargetSlot101);
    gWide[1 + kRefs][82] = reinterpret_cast<void *>(&TargetSlot82);
    gWide[1 + kRefs][72] = reinterpret_cast<void *>(&TargetSlot72); // ApplyMeshOverlay (slot 35, lane JP)
    F(kAnswer).Set(3, 0, P(&F(kInner)));
    F(kInner).Set(3, "p", 0, &InnerKey);
    // The gathers: the catalog, the fragments / bins, the property sets (kResource as loaded, kObj0..1 in the caches),
    // and a caller's filter (kMapped's second role: its slot 0 the fragment filter, itself; slot 1 an item, slot 3 a
    // fragment).
    F(kCatalog).Set(19, "d", 0, &CatalogTypeBin);
    F(kCatalog).Set(30, "ppp", 0, &CatalogByType);
    F(kCatalog).Set(32, "ppp", 0, &CatalogByBin);
    for (unsigned o : {unsigned(kResource), unsigned(kObj0), unsigned(kObj0 + 1)}) {
        for (unsigned s : {22u, 23u, 24u}) F(o).Set(s, 0, 0, &DrawMask);
        for (unsigned s : {28u, 29u}) F(o).Set(s, 0, 0, &DrawKey);
        F(o).Set(14, 0, 0u);
    }
    // The filters of slots 26 to 31 (lane JM): kResource is the property set (slots 19..22, 25, 16) and the skin part (its
    // slot 3 the set, 5 / 6 keys, 7 the parts); a cached item's resources (kObj0..1, kResource) hand out the set (slot 3).
    for (unsigned s : {19u, 20u, 21u, 22u}) F(kResource).Set(s, 0, 0, &DrawMask);
    F(kResource).Set(25, 0, 0, &DrawFlags25);
    F(kResource).Set(16, 0, 0, &DrawKey);
    for (unsigned s : {5u, 6u}) F(kResource).Set(s, 0, 0, &DrawKey);
    F(kResource).Set(7, 0, 0, &DrawParts);
    for (unsigned o : {unsigned(kResource), unsigned(kObj0), unsigned(kObj0 + 1)}) F(o).Set(3, 0, P(&F(kResource)));
    // Slot 26's overlays: kAnswer is the mesh / texture resource and face (keys 5, 7, 8, 9; parts 10, 13; slot 3 kInner, the
    // set the mesh and texture filters read: 19..22).
    for (unsigned s : {5u, 7u, 8u, 9u}) F(kAnswer).Set(s, 0, 0, &DrawKey);
    for (unsigned s : {10u, 13u}) F(kAnswer).Set(s, 0, 0, &DrawParts);
    for (unsigned s : {19u, 20u, 21u, 22u}) F(kInner).Set(s, 0, 0, &DrawMask);
    // A cached cosmetics item's +0x08 may be kObj0..1 (FillItems): the set's slots there too (21 stays the fragment key).
    for (unsigned o : {unsigned(kObj0), unsigned(kObj0 + 1)}) {
        for (unsigned s : {19u, 20u}) F(o).Set(s, 0, 0, &DrawMask);
        F(o).Set(25, 0, 0, &DrawFlags25);
        F(o).Set(0, "dp", 0, &ResourceQuery);
    }
    F(kMapped).Set(0, 0, P(&F(kMapped)));
    F(kMapped).Set(3, "p", 0, &DrawBool);
    Widen(kMapped, gWide[2 + kRefs], 1, reinterpret_cast<const void *>(&MappedAcceptItem));
    // Slots 32 / 34 (lane JN): kResource as a part (ops 32 / 33: its set kInner), the candidates (kOutfit0..1: a part's
    // slots 3..7; kAnswer: also 4 and 6), the set kInner (25 flags), and the skin catalog's finds (kRes's wide table).
    F(kResource).Set(3, 0, 0, &ResourceSet);
    F(kResource).Set(4, 0, 0, &DrawKey);
    for (unsigned i = 0; i < 2; ++i) {
        F(kOutfit0 + i).Set(3, 0, P(&F(kInner)));
        for (unsigned s : {4u, 5u}) F(kOutfit0 + i).Set(s, 0, 0, &DrawKey);
        F(kOutfit0 + i).Set(6, 0, 0, &PartKey6);
        F(kOutfit0 + i).Set(7, 0, 0, &DrawParts);
    }
    F(kAnswer).Set(4, 0, 0, &MeshKind); // DrawKey but during op 35 (lane JP)
    F(kAnswer).Set(6, 0, 0, &PartKey6);
    F(kInner).Set(25, 0, 0, &DrawFlags25);
    // RefreshContent / FixOutfitParts (slots 14 / 9, lane JQ): the resource service's slots 8 / 41 / 42 / 19 (12 / 38 raw),
    // the persist system's 3 / 16 / 17, the bin catalog's 5 / 6 / 26, the skin service's 22, the content kMsg (slot 3 the
    // lister kKeyed: slots 19 / 22), the listed parts (sims: slots 3 / 7).
    F(kRes).Set(8, "p");
    F(kRes).Set(41, "p");
    F(kRes).Set(42, "p", 0, &DrawBool);
    F(kRes).Set(19, "p", 0, &DrawBool);
    F(kPersist).Set(3, 0, P(&F(kKeyed)));
    F(kPersist).Set(16, "p-");
    F(kPersist).Set(17, "p");
    F(kCatalog).Set(5, "p");
    F(kCatalog).Set(6, "p", 0, &DrawBool);
    F(kCatalog).Set(26, "p");
    F(kSkin).Set(22, "p", 0, &EndRecords);
    F(kMsg).Set(3, 0, P(&F(kKeyed)));
    F(kKeyed).Set(19, "pd", 0, &ContentKeys);
    F(kKeyed).Set(22, 0, 0, &DrawMask);
    for (unsigned i = 0; i < kSims; ++i) {
        F(kSim0 + i).Set(3, 0, P(&F(kKeyed)));
        F(kSim0 + i).Set(7, 0, 0, &PartShort);
    }
    Widen(kRes, gWide[3 + kRefs], 11, reinterpret_cast<const void *>(&SkinFindType1));
    gWide[3 + kRefs][12] = reinterpret_cast<void *>(&ResSlot12);
    gWide[3 + kRefs][38] = reinterpret_cast<void *>(&ResSlot38);
    gWide[3 + kRefs][17] = reinterpret_cast<void *>(&SkinFindType3);
    gWide[3 + kRefs][22] = reinterpret_cast<void *>(&SkinFindType5);
    gWide[3 + kRefs][20] = reinterpret_cast<void *>(&OverlayList); // FindFaceOverlay (slot 38, lane JP)
    Widen(kPersist, gWide[4 + kRefs], 5, reinterpret_cast<const void *>(&PersistSlot5));
    // FillNames (slot 20, lane JN): the sims (also descriptions), the info kKeyed, the name set / tables kMsgFace.
    for (unsigned i = 0; i < kSims; ++i) {
        Obj &o = F(kSim0 + i);
        o.Set(4, 0, 0, &SimInfo);
        o.Set(67, 0, P(&F(kAnswer)));
        o.Set(28, 0, 0u);
        o.Set(55, 1, 0u);
        o.Set(16, "d", 0, &DescriptionAge);
        o.Set(22, 0, 0, &DrawBool);
    }
    F(kKeyed).Set(3, "d", 0, &InfoValue);
    F(kFw).Set(3, "ddp", 0, &NewNameSet);
    F(kMsgFace).Set(3, 0, P(&F(kSkin)));
    F(kSkin).Set(4, "p", 0, &NameSetKey);
    F(kMsgFace).Set(5, 1, 0, &TableRows);
    F(kMsgFace).Set(10, "pdd", 0, &NameEntry);
    F(kMsgFace).Set(21, 0, 0u);
    // The model's slot 93 (ShowFace / FindNamedPart, lane JO): last, as Widen copies the table.
    Widen(kObj0, gWide[5 + kRefs], 93, reinterpret_cast<const void *>(&ModelRange));
}

void MakeKeys(uint32_t seed)
{
    std::mt19937 r(seed * 5 + 11);
    static const uint32_t kGroups[] = {0, 1, 0x7f00, 0xabcdef12};
    for (SkinKey &k : gKeys) k = {uint32_t(r() % 3), kGroups[r() % 4], uint32_t(r() % 4), uint32_t(r() % 2)};
    gZero = {};
    // FindBinByName's bin keys: zero, each word alone non-zero, and one full key (the name filter tests every word).
    gBinKeys[0] = {};
    gBinKeys[1] = {uint32_t(1 + r() % 3), 0, 0, 0};
    gBinKeys[2] = {0, 0x80000000u, 0, 0};
    gBinKeys[3] = {0, 0, uint32_t(1 + r() % 3), 0};
    gBinKeys[4] = {0, 0, 0, 1};
    gBinKeys[5] = {0xfb4cb0de, 1, 2, 3};
    gBinKeys[6] = {5, 0, 0, 0}; // FindSkinPart's strict test: {1..5, 0, 0, 0} keys stop a part, {6, 0, 0, 0} does not
    gBinKeys[7] = {6, 0, 0, 0};
    gBinKeys[8] = {2, 0, 0, 1}; // a small type with another word set: lets the part through
    gBinKeys[9] = {3, 0, 1, 0};
    gBinKeys[10] = {4, 7, 0, 0};
    static const uint32_t kTypes[] = {0x0c93e3de, 0x2c1fd8a1, 0x0c1fe246, 0x4c158081, 0x8c1580b5, 0x8c93e35c,
                                      0xac598eac, 0xd1954460, 0xebcf3e27, 0x12345678, 0, 0xebcf3e20};
    for (unsigned i = 0; i < 12; ++i) gFragKeys[i] = {kTypes[i], uint32_t(r() % 2), uint32_t(r() % 3), 0}; // repeats: dupes
    // RefreshContent's content keys (lane JQ): each type it sorts, the switch's neighbours (one off), others; the groups
    // the not-default cache holds (gKeys'); words that repeat, so a key comes twice and a record names a kept one.
    static const uint32_t kContentTypes[] = {0x8c1580b5, 0x4c158081, 0x2c1fd8a1, 0x0c1fe246, 0x0c93e3de, 0x6c93b566,
                                             0x8c93bf6c, 0x8c93e35c, 0xac598eac, 0xd1954460, 0xebcf3e27, 0xac598eac,
                                             0xebcf3e27, 0x8c1580b6, 0xac598ead, 0xac598eab, 0xebcf3e26, 0x8c93e35d,
                                             0x0c1fe247, 0xd1954461, 0x2c1fd8a0, 0, 0xebcf3e20, 0x12345678};
    static_assert(sizeof(kContentTypes) / sizeof(kContentTypes[0]) == kContentKeys);
    for (unsigned i = 0; i < kContentKeys; ++i)
        gContentKeys[i] = {kContentTypes[i], kGroups[r() % 4], uint32_t(r() % 2), uint32_t(r() % 2)};
}

CASRef Ref(unsigned fake) { return CASRef(U(fake)); }

void FillItems(CASItemVector &items, std::mt19937 &r)
{
    const unsigned n = r() % 3;
    for (unsigned i = 0; i < n; ++i) {
        CASItem item{};
        for (unsigned k = 0; k < 9; ++k)
            if (r() % 3 == 0) item.refs[k].Assign(U(kObj0 + r() % 2));
        item.flag = r() % 2;
        items.push_back(item);
    }
}

// The system's state (the same on both sides: written through our layout).
void Populate(Sys *s, uint32_t seed)
{
    std::mt19937 r(seed * 13 + 1);
    s->mCachesFilled = r() % 2;
    s->mCacheTypes = r() % 2;
    s->mEditedSimId = r() % 3 ? uint32_t(r()) : 0xffffffffu;
    for (unsigned n = r() % 3, i = 0; i < n; ++i) {
        const uint32_t kind = uint32_t(r() % 4);
        std::list<FragmentQuery> &list = s->mQueries[kind];
        for (unsigned m = r() % 3, k = 0; k < m; ++k) {
            FragmentQuery q{};
            if (r() % 4 == 0) q.bin = Ref(kRef0 + r() % kRefs); // the game files only bin-less queries
            q.kind = r() % 5 ? kind : uint32_t(r() % 4);       // its list's kind (as the game files them), or not
            q.arg4 = int32_t(r() % 5) - 2;
            q.keyA = gKeys[r() % 8];
            q.keyB = gKeys[r() % 8];
            for (uint32_t &a : q.args) a = uint32_t(r());
            q.allowCustom = r() % 2;
            FillItems(q.items, r);
            q.count = int32_t(r() % 4);
            list.push_back(q);
        }
    }
    for (unsigned n = r() % 3, i = 0; i < n; ++i) FillItems(s->mTypes[1u << (r() % 6)], r);
    static const char *const kNames[] = {"hair", "tops", "a", "accessory-glasses"};
    for (NamedKeys *keys : {&s->mNamedKeysA, &s->mNamedKeysB}) {
        for (unsigned n = r() % 3, i = 0; i < n; ++i) {
            auto &set = (*keys)[kNames[r() % 4]];
            for (unsigned m = r() % 3, k = 0; k < m; ++k) set.insert(gKeys[r() % 8]);
        }
    }
    if (r() % 2) s->mEditedSim.Assign(U(kSim0 + r() % kSims));
    for (unsigned n = r() % 4, i = 0; i < n; ++i)
        s->mEditedOutfits.push_back(r() % 3 ? static_cast<void *>(&F(kOutfit0 + r() % 2)) : reinterpret_cast<void *>(uintptr_t(0x0f0f0010 + i)));
    for (unsigned n = r() % 4, i = 0; i < n; ++i) s->mNotDefault[gKeys[r() % 8].group] = r() % 2;
    for (unsigned n = r() % 3, i = 0; i < n; ++i) {
        NewSimJob job{};
        job.state = r() % 3;
        job.age = int32_t(r() % 4) - 1;
        job.arg2 = uint32_t(r());
        job.arg3 = uint32_t(r());
        job.arg4 = uint32_t(r());
        job.outfitKind = r() % 5;
        job.done = r() % 2;
        job.sim = Ref(kSim0 + r() % kSims);
        job.ref20 = Ref(kRef0 + r() % kRefs);
        if (r() % 2) job.ref24 = Ref(kRef0 + r() % kRefs);
        for (unsigned m = r() % 3, k = 0; k < m; ++k) job.extras.push_back(Ref(kObj0 + r() % 2));
        s->mNewSims.push_back(job);
    }
    for (unsigned n = r() % 3, i = 0; i < n; ++i) {
        ChildJob job{};
        job.state = r() % 3;
        job.parentA = &F(kSim0 + r() % kSims);
        job.parentB = &F(kSim0 + r() % kSims);
        job.kind = 1u << (r() % 4);
        job.done = r() % 2;
        job.sim = Ref(kSim0 + r() % kSims);
        job.ref18 = Ref(kRef0 + r() % kRefs);
        if (r() % 2) job.ref1c = Ref(kRef0 + r() % kRefs);
        s->mChildren.push_back(job);
    }
    for (unsigned n = r() % 3, i = 0; i < n; ++i) {
        OutfitJob job(uint32_t(r()), r() % 4);
        job.state = r() % 3;
        job.result = r() % 2;
        job.sim = Ref(kSim0 + r() % kSims);
        if (r() % 2) job.ref20 = Ref(kRef0 + r() % kRefs);
        if (r() % 2) job.ref24 = Ref(kRef0 + r() % kRefs);
        job.hasKeyA = r() % 2;
        job.keyA = gKeys[r() % 8];
        s->mOutfitJobs.push_back(job);
    }
    for (unsigned n = r() % 4, i = 0; i < n; ++i) {
        const uint32_t id = r() % 3 ? uint32_t(int32_t(int16_t(F(kSim0 + r() % kSims).value[0]))) : uint32_t(r() % 9);
        s->mResults[id] = r() % 2;
    }
    s->mObjectDetail = int32_t(r() % 4);
    s->mReflection = int32_t(r() % 2);
    s->mFreeWill = r() % 2;
    s->mInCASMode = r() % 2;
    s->mSavedOption21 = uint16_t(r() % 3);
}

// ---- dumping ----

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void DumpKey(Blob &b, const SkinKey &k) { b.insert(b.end(), {k.type, k.group, k.instance, k.resource}); }
void DumpItems(Blob &b, const CASItemVector &items)
{
    b.push_back(uint32_t(items.size()));
    b.push_back(uint32_t(items.capacity()));
    for (const CASItem &item : items) {
        for (const CASRef &ref : item.refs) b.push_back(Self(ref.Get()));
        b.push_back(item.flag);
    }
}
void DumpOutfitJob(Blob &b, const OutfitJob &j)
{
    b.insert(b.end(), {j.state, uint32_t(j.step), j.source, uint32_t(j.result), j.kind, j.ageIndex, j.genderIndex,
                       Self(j.sim.Get()), Self(j.ref20.Get()), Self(j.ref24.Get()), uint32_t(j.hasKeyA), uint32_t(j.hasKeyB)});
    DumpKey(b, j.keyA);
    DumpKey(b, j.keyB);
    b.push_back(j.reloadRecord);
}

void Dump(Blob &b, Sys *s, bool ours)
{
    static const uint32_t kFaces[4][2] = {
        {0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kCASVTable}, {0x20, Sys::kMessageVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(s, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(s, off));
    b.push_back(Word(s, 0x18) & 0xff);
    b.push_back(s->mCachesFilled);
    b.push_back(s->mCacheTypes);
    b.push_back(s->mEditedSimId);
    b.push_back(uint32_t(s->mQueries.size()));
    for (const auto &[kind, list] : s->mQueries) {
        b.insert(b.end(), {kind, uint32_t(list.size())});
        for (const FragmentQuery &q : list) {
            b.insert(b.end(), {Self(q.bin.Get()), q.kind, uint32_t(q.arg4)});
            DumpKey(b, q.keyA);
            DumpKey(b, q.keyB);
            b.insert(b.end(), std::begin(q.args), std::end(q.args));
            DumpKey(b, q.keyC);
            b.push_back(q.allowCustom);
            DumpItems(b, q.items);
            b.push_back(uint32_t(q.count));
        }
    }
    b.push_back(uint32_t(s->mTypes.size()));
    for (const auto &[type, items] : s->mTypes) {
        b.push_back(type);
        DumpItems(b, items);
    }
    for (const NamedKeys *keys : {&s->mNamedKeysA, &s->mNamedKeysB}) {
        b.push_back(uint32_t(keys->size()));
        for (const auto &[name, set] : *keys) {
            b.push_back(uint32_t(name.size()));
            for (char c : name) b.push_back(uint8_t(c));
            b.push_back(uint32_t(set.size()));
            for (const SkinKey &k : set) DumpKey(b, k);
        }
    }
    b.push_back(Self(s->mEditedSim.Get()));
    b.push_back(uint32_t(s->mEditedOutfits.size()));
    for (void *o : s->mEditedOutfits) b.push_back(Self(o));
    b.insert(b.end(), {s->mRandom.mIntegerSeed, uint32_t(s->mRandom.mRealSeed), s->mRandom.mUnknown});
    b.push_back(uint32_t(s->mMessageIds.size()));
    b.insert(b.end(), s->mMessageIds.begin(), s->mMessageIds.end());
    for (const FlagCache *cache : {&s->mNotDefault, &s->mResults}) {
        b.push_back(uint32_t(cache->size()));
        for (const auto &[k, v] : *cache) b.insert(b.end(), {k, uint32_t(v)});
    }
    b.push_back(uint32_t(s->mNewSims.size()));
    for (const NewSimJob &j : s->mNewSims) {
        b.insert(b.end(), {j.state, uint32_t(j.age), j.arg2, j.arg3, j.arg4, j.outfitKind, uint32_t(j.done),
                           Self(j.sim.Get()), Self(j.ref20.Get()), Self(j.ref24.Get()), uint32_t(j.extras.size())});
        for (const CASRef &e : j.extras) b.push_back(Self(e.Get()));
    }
    b.push_back(uint32_t(s->mChildren.size()));
    for (const ChildJob &j : s->mChildren)
        b.insert(b.end(), {j.state, Self(j.parentA), Self(j.parentB), j.kind, uint32_t(j.done), Self(j.sim.Get()),
                           Self(j.ref18.Get()), Self(j.ref1c.Get())});
    b.push_back(uint32_t(s->mOutfitJobs.size()));
    for (const OutfitJob &j : s->mOutfitJobs) DumpOutfitJob(b, j);
    b.insert(b.end(), {uint32_t(s->mObjectDetail), uint32_t(s->mReflection), uint32_t(s->mFreeWill), uint32_t(s->mInCASMode),
                       uint32_t(s->mSavedOption21)});
    for (const Obj &o : gMake) b.push_back(uint32_t(o.refs)); // never counted (lane JS)
}

// ---- driving a system ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}

bool gCreator;
Sys *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gCreator) {
        void *made = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x502860)();
        b.push_back(made != nullptr);
        return static_cast<Sys *>(made);
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSCASSystemAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0x4ff850)(memory);
    b.push_back(r == memory);
    return static_cast<Sys *>(memory);
}

const uint32_t kIids[] = {1, Sys::kIid, Sys::kMessageTargetIid, 0x287fb697 /* cIGZSystemService */, 0x12345678};
constexpr uint32_t kFaceAt[] = {0, 4, 0x1c, 0x20};

// A generator seed whose next RandomUint32Uniform(100) is `want` (cRZRandom's LCG: bits 16..47 of seed * 1103515245 +
// 12345, scaled by 100 >> 32), for CreateChild's parent draw (`> 50`).
uint32_t BoundarySeed(uint32_t want)
{
    for (uint32_t seed = 1;; ++seed) {
        const uint64_t next = uint64_t(seed) * 1103515245u + 12345u;
        if (uint32_t((uint64_t(uint32_t(next >> 16)) * 100) >> 32) == want) return seed;
    }
}
void *Sim(std::mt19937 &r) { return r() % 4 ? static_cast<void *>(&F(kSim0 + r() % kSims)) : nullptr; }
const SkinKey *Key(std::mt19937 &r) { return &gKeys[r() % 8]; }
const SkinKey *KeyOrNull(std::mt19937 &r) { return r() % 3 ? Key(r) : nullptr; }
uint32_t Any(std::mt19937 &r) { return r() % 2 ? uint32_t(r() % 5) : uint32_t(r()); }
uint32_t DrawMaskArg(std::mt19937 &r) // all bits half the time (so items get past the masks), else one bit or none
{
    return r() % 2 ? 0xffffffffu : r() % 4 ? (1u << (r() % 4)) | (r() % 2 ? 0xf0u : 0u) : 0u;
}
// Every type the gathers know, and some they do not.
uint32_t GatherType(std::mt19937 &r)
{
    static const uint32_t kTypes[] = {1, 8, 0x10, 0x20, 0x40, 0x80, 0x200, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 2, 0x100, 0x400};
    return kTypes[r() % 15];
}
// A caller's filter (the gathers' +0x14 face; kMapped), or none (only with the type cache on: the original would ask it).
cITSFragEntryFilter *CallerFilter(std::mt19937 &r, const Sys *s)
{
    return r() % 3 || !s->mCacheTypes ? reinterpret_cast<cITSFragEntryFilter *>(&F(kMapped)) : nullptr;
}
using GatherTypeFn = uint32_t(__thiscall *)(void *, CASItemVector *, uint32_t, cITSFragEntryFilter *);
using GatherBinFn = uint32_t(__thiscall *)(void *, CASItemVector *, void *, cITSFragEntryFilter *);
using FillCacheFn = void(__thiscall *)(void *, uint32_t);
using LoadByKeyFn = uint32_t(__thiscall *)(void *, const SkinKey *, CASItem *);
GatherTypeFn gOrigGatherType;
GatherBinFn gOrigGatherBin;
FillCacheFn gOrigFillCache;
LoadByKeyFn gOrigLoadByKey;
using AcceptFragmentFn = uint32_t(__thiscall *)(void *, cIGZUnknown *);
using IsZeroKeyFn = uint32_t(__thiscall *)(const SkinKey *);
AcceptFragmentFn gOrigAcceptFragment;
IsZeroKeyFn gOrigIsZeroKey;
using MatchesFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const SkinKey *, const SkinKey *);
MatchesFn gOrigMatches;
AcceptFragmentFn gOrigMeshAccept;
AcceptFragmentFn gOrigMatchesPart;
AcceptFragmentFn gOrigMatchesProperties;
uint32_t(__thiscall *gOrigCosmeticsItem)(void *, const CASItem *);
void(__cdecl *gOrigFindNamedPart)(void *, const char *, void **, void **);

uint32_t(__thiscall *gOrigLoad)(void *, const SkinKey *, uint32_t, void **);

void RunOp(Blob &b, Sys *s, unsigned op, std::mt19937 &r)
{
    void *const primary = s;
    uint8_t *const base = reinterpret_cast<uint8_t *>(s);
    void *const face = base + 0x1c;
    void *const target = base + 0x20;
    switch (op) {
    case 0: { // QueryInterface on a face (+0, the thunks of +4 / +0x1c / +0x20), the result released
        void *at = base + kFaceAt[r() % 4];
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(at, 0, kIids[r() % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 1: case 2: { // AddRef then Release through a face
        void *at = base + kFaceAt[r() % 4];
        b.push_back(Slot<uint32_t>(at, 1));
        b.push_back(Slot<uint32_t>(at, 2));
        break;
    }
    case 3: b.push_back(Slot<uint32_t>(face, 3)); break;
    case 4: Slot<void>(face, 4, Any(r)); break;
    case 5: { // CreateSim 0x5011d0 (lane JS): ages -1..3 and others, kind -1 or given, an extra or none, queued or now
        static const int32_t kAges[] = {-1, 0, 1, 2, 3, 4, -2};
        static const uint32_t kNow[] = {0, 1, 0x100, 0xff01};
        const int32_t age = r() % 5 ? kAges[r() % 7] : int32_t(r());
        const uint32_t kind = r() % 2 ? 0xffffffffu : Any(r);
        const uint32_t a2 = Any(r), a3 = Any(r), a4 = Any(r), outfitKind = Any(r);
        void *const extra = Sim(r);
        const uint32_t now = kNow[r() % 4];
        gCreateOp = true;
        gWorld = Draw(4) ? &gMake[kWorld] : nullptr;
        b.push_back(Slot<uint32_t>(face, 5, uint32_t(age), a2, a3, a4, kind, outfitKind, extra, now));
        gCreateOp = false;
        break;
    }
    case 6: { // CreateChild 0x500e50 (lane JS): parents (or none), kind -1 or given, queued or now
        static const uint32_t kNow[] = {0, 1, 0x100, 0xff01};
        void *const parentA = Sim(r);
        void *const parentB = Sim(r);
        const uint32_t kind = r() % 2 ? 0xffffffffu : Any(r);
        const uint32_t now = kNow[r() % 4];
        if (r() % 3 == 0) s->mRandom.mIntegerSeed = BoundarySeed(49 + uint32_t(r() % 3)); // the draw 49 / 50 / 51 (> 50)
        gCreateOp = true;
        gWorld = Draw(4) ? &gMake[kWorld] : nullptr;
        b.push_back(Slot<uint32_t>(face, 6, parentA, parentB, kind, now));
        gCreateOp = false;
        break;
    }
    case 7: b.push_back(Slot<uint32_t>(face, 7, Sim(r)) & 0xff); break;
    case 8:
        b.push_back(Slot<uint32_t>(face, 8, Any(r), uint32_t(r() % 4), KeyOrNull(r), KeyOrNull(r),
                                   uint32_t(r() % 3) | (r() % 3 ? 0u : 0xab00u)) & 0xff);
        break;
    case 9: { // FixOutfitParts 0x502490 (lane JQ; FixOutfitPart 0x502120 from it): pickOne's low byte only (r drawn as before)
        static const uint32_t kPick[] = {0, 1, 0x100, 0xff01};
        const uint32_t simId = Any(r);
        const uint32_t pick = r() % 4 ? kPick[r() % 4] : 1u;
        gFixOp = gPartMode = true; // the parts (kResource, its set kInner) as ApplyParts (op 33) sees them
        gFixClean = Draw(4) == 0;  // a quarter of the time nothing to fix (the fake stream, so r() is drawn as before)
        b.push_back(Slot<uint32_t>(face, 9, simId, pick) & 0xff);
        gFixOp = gPartMode = gFixClean = false;
        break;
    }
    case 10: { // ApplySpecialOutfit 0x5009b0 (lane JO): the flag's low byte only
        static const uint32_t kFlags[] = {0, 1, 0x100, 0xff01};
        const SkinKey *key = KeyOrNull(r);
        const uint32_t simId = Any(r);
        b.push_back(Slot<uint32_t>(face, 10, key, simId, kFlags[r() % 4]) & 0xff);
        break;
    }
    case 11: b.push_back(Slot<uint32_t>(face, 11) & 0xff); break;
    case 12: { // GetResult: an id filed (a sim's, widened) or not
        uint32_t id = r() % 3 ? uint32_t(int32_t(int16_t(F(kSim0 + r() % kSims).value[0]))) : uint32_t(r() % 9);
        uint8_t out = 0x5a;
        b.push_back(Slot<uint32_t>(face, 12, id, &out) & 0xff);
        b.push_back(out);
        break;
    }
    case 13: case 15: b.push_back(Slot<uint32_t>(face, op, Key(r)) & 0xff); break;
    case 16: { // Slot16: no bin catalog now and then (the getter asks the framework, which has none)
        void *&catalog = *GameAddress<void *>(0x10faf78);
        void *const saved = catalog;
        if (r() % 4 == 0) catalog = nullptr;
        gContentOp = true;
        gNoContent = catalog == nullptr; // slot 14 (from 16) would use the null catalog once the content loaded
        b.push_back(Slot<uint32_t>(face, 16, Key(r)) & 0xff);
        gContentOp = gNoContent = false;
        catalog = saved;
        break;
    }
    case 14: { // RefreshContent 0x50a9f0 (lane JQ): a content key (two in three) or a pool key; one r draw as before
        const uint32_t x = uint32_t(r());
        const SkinKey *key = x % 3 ? &gContentKeys[x / 3 % kContentKeys] : &gKeys[x / 3 % 8];
        gContentOp = true;
        b.push_back(Slot<uint32_t>(face, 14, key) & 0xff);
        gContentOp = false;
        break;
    }
    case 17: case 18: b.push_back(Slot<uint32_t>(face, op) & 0xff); break;
    case 19: { // FindBinByName 0x501dd0 (lane JN): a name some bins have (in another case), or none has
        static const char *const kNames[] = {"name", "NAMES", "", "A BIN NAME LONGER THAN FIFTEEN", "other"};
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t iid = r() % 3 ? 0x8c1571d6u : Any(r); // kObj0..1 answer this one (ResourceQuery)
        b.push_back(Slot<uint32_t>(face, 19, kNames[r() % 5], iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    }
    case 20: b.push_back(Slot<uint32_t>(face, 20, Sim(r), Sim(r)) & 0xff); break;
    case 21: { // ReloadOutfits: a sim (never none: the original asks it unchecked); no persist system now and then
        void *const sim = &F(kSim0 + r() % kSims);
        void *&persist = *GameAddress<void *>(svcfx::kPersistCell);
        void *const saved = persist;
        if (r() % 4 == 0) persist = nullptr; // the getter then asks the framework, which has none
        Slot<void>(face, 21, sim);
        persist = saved;
        break;
    }
    case 22:
        b.push_back(Slot<uint32_t>(face, 22, Any(r), Any(r), Any(r), Any(r), KeyOrNull(r), KeyOrNull(r), Any(r)) & 0xff);
        break;
    case 23: { // QueryFragments: new arguments (a bin now and then), or a filed query's again (answered from the cache
               // when its custom-content byte still matches)
        CASItemVector items;
        if (r() % 3 == 0) FillItems(items, r); // what the caller's vector held before
        const FragmentQuery *known = nullptr;
        if (r() % 2 && !s->mQueries.empty()) {
            auto it = s->mQueries.begin();
            std::advance(it, r() % s->mQueries.size());
            if (!it->second.empty()) {
                auto q = it->second.begin();
                std::advance(q, r() % it->second.size());
                known = &*q;
            }
        }
        if (known) {
            FragmentQuery q = *known; // the list may grow
            switch (r() % 8) { // now and then one argument differs (a near miss)
            case 0: q.keyA.instance ^= 1 + r() % 2; break;
            case 1: q.keyB.group ^= 1 + r() % 2; break;
            case 2: q.args[r() % 4] ^= 1; break;
            case 3: q.arg4 += 1; break;
            default: break;
            }
            b.push_back(Slot<uint32_t>(face, 23, static_cast<void *>(nullptr), &items, q.kind, q.arg4, &q.keyA, &q.keyB,
                                       q.args[0], q.args[1], q.args[2], q.args[3], Any(r)));
        } else {
            b.push_back(Slot<uint32_t>(face, 23, r() % 3 ? nullptr : static_cast<void *>(&F(kPool[r() % 5])), &items, uint32_t(r() % 4), Any(r), KeyOrNull(r),
                                       KeyOrNull(r), Any(r), Any(r), Any(r), Any(r), Any(r)));
        }
        DumpItems(b, items);
        break;
    }
    case 24: { // LoadResource (the +0x1c thunk 0x50a970)
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        b.push_back(Slot<uint32_t>(face, 24, Key(r), Any(r), &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    }
    case 25: { // FilterFragments: a bin or a type (a bin when the type cache is off: the original would ask a null filter)
        // Several calls (the cache state carries over), the masks mostly all bits and the type half the time one whose
        // items carry a property set (0x506c80's +0x14 types), so items reach the key, custom content and slot 14 tests.
        static const uint32_t kPropertyTypes[] = {1, 0x200, 0x2000, 0x4000, 0x10000};
        const auto mask = [&] { return r() % 4 ? 0xffffffffu : DrawMaskArg(r); };
        for (unsigned n = 1 + r() % 3; n; --n) {
            CASItemVector items;
            if (r() % 3 == 0) FillItems(items, r);
            void *const bin = r() % 2 || !s->mCacheTypes ? static_cast<void *>(&F(kPool[r() % 5])) : nullptr;
            const uint32_t ages = mask(), genders = mask(), flags = mask();
            const SkinKey *const keyA = KeyOrNull(r), *const keyB = KeyOrNull(r);
            const uint32_t type = r() % 2 ? kPropertyTypes[r() % 5] : GatherType(r);
            b.push_back(Slot<uint32_t>(face, 25, bin, &items, ages, genders, flags, keyA, keyB, type) & 0xff);
            DumpItems(b, items);
        }
        break;
    }
    case 26: { // GetCosmetics (lane JM): a bin or type 0x10, one to three calls
        const auto mask = [&] { return r() % 4 ? 0xffffffffu : DrawMaskArg(r); };
        for (unsigned n = 1 + r() % 3; n; --n) {
            CASItemVector items;
            if (r() % 3 == 0) FillItems(items, r);
            void *const bin = r() % 3 == 0 ? static_cast<void *>(&F(kPool[r() % 5])) : nullptr;
            const uint32_t ages = mask(), genders = mask(), styles = r() % 2 ? 0u : mask();
            const SkinKey *const keyB = r() % 3 ? nullptr : r() % 2 ? &gZero : Key(r);
            const SkinKey *const keyA = r() % 3 ? nullptr : r() % 2 ? &gZero : Key(r);
            const SkinKey *const keyC = r() % 4 ? nullptr : Key(r);
            static const uint32_t kParts[] = {0, 0, 1, 0xffff, 0x8000};
            const uint32_t parts = kParts[r() % 5], flags = r() % 2 ? 0u : r() % 2 ? 1u : DrawMaskArg(r);
            b.push_back(Slot<uint32_t>(face, 26, bin, &items, ages, genders, styles, keyB, keyA, keyC, parts, flags) & 0xff);
            DumpItems(b, items);
        }
        break;
    }
    case 27: case 28: case 29: case 30: case 31: { // the filter slots (lane JM): a bin or the slot's type, one to three calls
        const auto mask = [&] { return r() % 4 ? 0xffffffffu : DrawMaskArg(r); };
        for (unsigned n = 1 + r() % 3; n; --n) {
            CASItemVector items;
            if (r() % 3 == 0) FillItems(items, r);
            void *const bin = r() % 3 == 0 ? static_cast<void *>(&F(kPool[r() % 5])) : nullptr;
            const uint32_t ages = mask(), genders = mask(), styles = mask();
            if (op <= 28) {
                b.push_back(Slot<uint32_t>(face, op, bin, &items, ages, genders, styles) & 0xff);
            } else if (op <= 30) {
                const uint32_t flags = r() % 2 ? 0u : DrawMaskArg(r);
                b.push_back(Slot<uint32_t>(face, op, bin, &items, ages, genders, styles, flags) & 0xff);
            } else {
                const SkinKey *const keyA = r() % 3 ? nullptr : Key(r), *const keyB = r() % 3 ? nullptr : Key(r);
                const SkinKey *const keyC = r() % 3 ? nullptr : r() % 2 ? &gZero : Key(r);
                static const uint32_t kParts[] = {0, 1, 0xffff, 0x8000, 0xffffffff};
                const uint32_t categories = r() % 2 ? 0xffffffffu : DrawMaskArg(r), parts = kParts[r() % 5];
                const uint32_t allowed = r() % 2 ? 0xffffffffu : r() % 2 ? 0u : 0xfu;
                b.push_back(Slot<uint32_t>(face, 31, bin, &items, ages, genders, keyA, keyB, styles, categories, parts, allowed,
                                           keyC) & 0xff);
            }
            DumpItems(b, items);
        }
        break;
    }
    case 32: case 34: { // FindSkinPart 0x502e40 / FindMeshOverlay 0x503580 (lane JN): a part (kResource as a part, a
                        // candidate) or overlay (kAnswer), or none; part bits (bits above the short now and then), masks,
                        // keys (none now and then; keyB none: slot 32 answers none), slot 32's strict byte (other bytes set)
        static const unsigned kParts32[] = {kResource, kOutfit0, kOutfit0 + 1, kAnswer};
        const uint32_t parts = r() % 4 ? (1u << (r() % 16)) | (r() % 3 ? 0u : 0xffff0000u) : uint32_t(r());
        void *const object = r() % 8 == 0 ? nullptr
                             : op == 32   ? static_cast<void *>(&F(kParts32[r() % 4]))
                                          : static_cast<void *>(&F(kAnswer));
        // every bit three times in four, so the object itself is tested past its masks often
        const uint32_t ages = r() % 4 ? 0xffffffffu : DrawMaskArg(r), genders = r() % 4 ? 0xffffffffu : DrawMaskArg(r);
        const SkinKey *const keyA = r() % 4 ? Key(r) : nullptr;
        const SkinKey *const keyB = r() % 6 ? (r() % 4 ? Key(r) : &gBinKeys[6 + r() % 2]) : nullptr;
        gPartMode = true;
        if (op == 32) {
            const uint32_t strict = uint32_t(r() % 2) | (r() % 2 ? 0x5500u : 0u);
            b.push_back(Self(Slot<void *>(face, 32, parts, object, ages, genders, keyA, keyB, strict)));
        } else {
            b.push_back(Self(Slot<void *>(face, 34, parts, object, ages, genders, keyA, keyB)));
        }
        gPartMode = false;
        break;
    }
    case 33: { // ApplyParts: every part, 13 (all), past 31 (the shift's count is masked)
        static const uint32_t kParts[] = {0, 1, 6, 7, 10, 11, 12, 13, 13, 13, 14, 31, 32, 45};
        void *const target = r() % 6 ? static_cast<void *>(&F(kTarget)) : nullptr;
        gPartMode = true;
        b.push_back(Slot<uint32_t>(face, 33, target, Key(r), kParts[r() % 14]) & 0xff);
        gPartMode = false;
        break;
    }
    case 35: { // ApplyMeshOverlay 0x505fb0 (lane JP): the sim kTarget or none; a zero key, keys one word off zero, others;
               // parts 13 (every bit) often, each side of 0xc80's bits, past 31; the id excluded one the meshes have or not
        static const uint32_t kParts[] = {13, 13, 13, 13, 0, 1, 6, 7, 10, 11, 12, 31, 32, 45};
        void *const sim = r() % 8 ? &F(kTarget) : nullptr;
        const unsigned pick = r() % 4;
        const SkinKey *const key = pick == 0 ? &gZero : pick == 1 ? &gBinKeys[1 + r() % 4] : Key(r);
        const uint32_t exclude = r() % 3 == 0 ? P(&gZero) : r() % 2 ? P(&gKeys[r() % 8]) : P(&gBinKeys[6 + r() % 5]);
        gMesh35 = true;
        b.push_back(Slot<uint32_t>(face, 35, sim, key, kParts[r() % 14], exclude) & 0xff);
        gMesh35 = false;
        break;
    }
    case 36: { // ShowFace 0x501730 (lane JO): the sim kTarget or none; `show`'s low byte only
        static const uint32_t kShow[] = {0, 1, 0x100, 0xff01};
        void *const sim = r() % 6 ? &F(kTarget) : nullptr;
        b.push_back(Slot<uint32_t>(face, 36, sim, kShow[r() % 4]) & 0xff);
        break;
    }
    case 38: { // FindFaceOverlay 0x504230 (lane JP): the sim kTarget (its chain slots with their dword counts) or none;
               // flags with bit 1 (the face check), without it (zero: nothing; else the part keys walk), other bits set
        static const uint32_t kFlags[] = {2, 2, 3, 0xfffffffe, 0x102, 0, 1, 0x100, 0xfffffffd};
        void *const sim = r() % 8 ? &F(kTarget) : nullptr;
        b.push_back(Slot<uint32_t>(face, 38, sim, kFlags[r() % 9]) & 0xff);
        break;
    }
    case 37: { // Slot37: each age bit alone, none, several; each gender
        static const uint32_t kAges[] = {0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x7f, 0x18, 0x48, 0x50, 0x06};
        static const uint32_t kGenders[] = {0, 1, 2, 3, 4};
        void *const sim = r() % 6 ? &F(kTarget) : nullptr; // ShowFace's sim (lane JO; its only use here)
        b.push_back(Slot<uint32_t>(face, 37, Any(r), sim, kAges[r() % 14], kGenders[r() % 5]) & 0xff);
        break;
    }
    case 39: { // PickRandomOutfit 0x5028d0 (lane JO): seeds kept (op only), masks none / all / one bit / several
        static const uint32_t kSeeds[] = {0, 1, 2, 0x7fffffff, 0xfffffffe, UINT32_MAX};
        const uint32_t seed = r() % 4 ? kSeeds[r() % 6] : uint32_t(r());
        const uint32_t ages = DrawMaskArg(r), genders = r() % 3 ? DrawMaskArg(r) : 0u;
        gSeedThrough = true;
        b.push_back(Self(Slot<void *>(face, 39, Any(r), seed, ages, genders)));
        gSeedThrough = false;
        break;
    }
    // The +0 face's service slots.
    case 40: b.push_back(Slot<uint32_t>(primary, 3)); break;
    case 41: Slot<void>(primary, 4, Any(r)); break;
    case 42: b.push_back(Slot<uint32_t>(primary, 5)); break;
    case 43: b.push_back(Slot<uint32_t>(primary, 6) & 0xff); break;
    case 44: Slot<void>(primary, 7, uint32_t(r() % 2)); break;
    case 45: b.push_back(Slot<uint32_t>(primary, 8) & 0xff); break; // Init
    case 46: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 47: case 48: case 49: b.push_back(Slot<uint32_t>(primary, 10, uint32_t(r())) & 0xff); break; // OnTick
    case 50: b.push_back(Slot<uint32_t>(primary, 11, uint32_t(r())) & 0xff); break;
    case 51: b.push_back(Slot<uint32_t>(primary, 12)); break;
    case 52: b.push_back(Slot<uint32_t>(base + 4, 5)); break; // the +4 face's RefCount
    case 53: { // LoadResource's body 0x502dc0 called directly (game code does): the hook's adapter against the original
        void *got = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const SkinKey *key = Key(r);
        const uint32_t iid = Any(r);
        const uint32_t ok = gOurs ? uint32_t(cTSCASSystemAdapters::LoadResource(s, nullptr, key, iid, &got))
                                  : gOrigLoad(s, key, iid, &got);
        b.push_back(ok & 0xff);
        b.push_back(svcfx::NormOut(P(got)));
        break;
    }
    // The gathers, called directly (the forwarded slots and 0x503e20 do): the hooks' adapters against the originals.
    case 60: { // GatherByType 0x509b90
        CASItemVector items;
        if (r() % 3 == 0) FillItems(items, r);
        const uint32_t type = GatherType(r);
        cITSFragEntryFilter *const filter = CallerFilter(r, s);
        b.push_back(gOurs ? uint32_t(cTSCASSystemAdapters::GatherByType(s, nullptr, &items, type, filter))
                          : gOrigGatherType(s, &items, type, filter) & 0xff);
        DumpItems(b, items);
        break;
    }
    case 61: { // GatherFromBin 0x50a1d0
        CASItemVector items;
        if (r() % 3 == 0) FillItems(items, r);
        void *const bin = r() % 6 ? static_cast<void *>(&F(kPool[r() % 5])) : nullptr;
        cITSFragEntryFilter *const filter = r() % 2 ? reinterpret_cast<cITSFragEntryFilter *>(&F(kMapped)) : nullptr;
        b.push_back(gOurs ? uint32_t(cTSCASSystemAdapters::GatherFromBin(s, nullptr, &items, static_cast<cIGZUnknown *>(bin), filter))
                          : gOrigGatherBin(s, &items, bin, filter) & 0xff);
        DumpItems(b, items);
        break;
    }
    case 62: { // FillTypeCache 0x50bc00
        const uint32_t type = GatherType(r);
        if (gOurs) cTSCASSystemAdapters::FillTypeCache(s, nullptr, type);
        else gOrigFillCache(s, type);
        break;
    }
    case 63: { // LoadByKeyType 0x502c00: a fragment key of each type, into an item that holds objects or not
        CASItemVector holder;
        FillItems(holder, r);
        CASItem item{};
        if (!holder.empty()) item = holder.front();
        const SkinKey *const key = &gFragKeys[r() % 12];
        b.push_back(gOurs ? uint32_t(cTSCASSystemAdapters::LoadByKeyType(s, nullptr, key, &item)) : gOrigLoadByKey(s, key, &item) & 0xff);
        for (const CASRef &ref : item.refs) b.push_back(Self(ref.Get()));
        break;
    }
    case 64: { // cTSAGSFragFilter::AcceptFragment 0x504fa0 called directly (cTSSpecialOutfitFragFilter's Accept, game code,
               // does): the hook's adapter against the original, on a skin tones filter (its slot 5 ours on both sides),
               // one to four fragments of the pool (a key seen before now and then)
        cTSSkinTonesFragFilter filter(DrawMaskArg(r), DrawMaskArg(r), DrawMaskArg(r), r() % 2 ? 0u : DrawMaskArg(r),
                                      uint8_t(r() % 3));
        for (unsigned n = 1 + r() % 4; n; --n) {
            auto *const fragment = reinterpret_cast<cIGZUnknown *>(&F(kPool[r() % 5]));
            b.push_back(gOurs ? uint32_t(CASFilterAdapters::AcceptFragment(&filter, nullptr, fragment))
                              : gOrigAcceptFragment(&filter, fragment) & 0xff);
        }
        break;
    }
    case 65: { // IsZeroKey 0x504cb0 called directly (19 callers): the zero key, keys one word off it, others
        static const SkinKey kNear[] = {{0, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}};
        const unsigned pick = r() % 4;
        const SkinKey *const key = pick == 0 ? &gZero : pick == 1 ? &kNear[r() % 5] : pick == 2 ? Key(r) : &gFragKeys[r() % 12];
        b.push_back(gOurs ? uint32_t(CASFilterAdapters::IsZeroKey(key, nullptr)) : gOrigIsZeroKey(key) & 0xff);
        break;
    }
    case 66: { // cTSOutfitDataFragmentFilterBase::Matches 0xb647d0 called directly (slot 26's filters always have every test
               // off; the collar filter's mesh filter, game code, does not): ours against the original on one of our mesh
               // filters, each test on or off, small masks (a superset or not), keys zero, equal or not, one word off
        cTSMeshOverlayFragmentFilter m;
        static const SkinKey kNear[] = {{0, 0, 0, 1}, {1, 0, 0, 0}};
        const auto key = [&]() -> const SkinKey * { const unsigned k = r() % 6; return k == 0 ? &gZero : k == 1 ? &kNear[r() % 2] : Key(r); };
        m.mStyles = r() % 4;
        m.mAges = r() % 4;
        m.mGenders = r() % 4;
        m.mParts = r() % 4;
        m.mFlags = r() % 3 ? 0u : uint32_t(r() % 4);
        m.mKeyA = *key();
        m.mKeyB = *key();
        m.mTests = r() % 4 ? uint32_t(r() % 0x80) : 0xffffffffu;
        for (unsigned n = 1 + r() % 4; n; --n) {
            const uint32_t ages = r() % 4, genders = r() % 4, styles = r() % 4, parts = r() % 4, flags = r() % 4;
            const SkinKey *const keyA = r() % 2 ? &m.mKeyA : key(), *const keyB = r() % 2 ? &m.mKeyB : key();
            b.push_back(gOurs ? uint32_t(m.Matches(ages, genders, styles, parts, flags, keyA, keyB))
                              : gOrigMatches(&m, ages, genders, styles, parts, flags, keyA, keyB) & 0xff);
        }
        break;
    }
    case 67: { // cTSMeshOverlayFragmentFilter::Accept 0xb64960 called directly (the collar filter's Accept, game code, does):
               // the hook's adapter against the original on one of our mesh filters (keys dropped when seen, or not)
        cTSMeshOverlayFragmentFilter m;
        m.SetDropDuplicates(r() % 2 != 0);
        m.mAges = r() % 2;
        m.mGenders = r() % 2;
        m.mTests = r() % 2 ? 0u : uint32_t(r() % 0x80);
        gMeshKeys = true;
        for (unsigned n = 1 + r() % 4; n; --n) {
            auto *const fragment = reinterpret_cast<cIGZUnknown *>(&F(kPool[r() % 5]));
            b.push_back(gOurs ? uint32_t(CASFilterAdapters::MeshAccept(&m, nullptr, fragment)) : gOrigMeshAccept(&m, fragment) & 0xff);
        }
        gMeshKeys = false;
        break;
    }
    case 68: { // cTSBinSkinFragFilter::MatchesPart 0x505a60 called directly (slot 31 reaches it rarely): ours against the
               // original on a skin part (kResource: parts 0, 1, 2, 0x8000, 0xffff and bits past the short), masks mostly
               // all bits (so calls get to the parts) or small, keys mostly absent, else zero or another; the filter's parts
               // a short, a bit past it, or both
        const auto mask = [&] { return r() % 3 ? 0xffffffffu : uint32_t(r() % 16); };
        const auto key = [&]() -> const SkinKey * { const unsigned k = r() % 6; return k < 3 ? nullptr : k == 3 ? &gZero : Key(r); };
        static const uint32_t kParts[] = {0, 1, 4, 0xffff, 0x8000, 0x10000, 0x10004};
        const uint32_t ages = mask(), genders = mask();
        const SkinKey *const keyA = key(), *const keyB = key();
        const uint32_t styles = mask(), categories = mask(), parts = kParts[r() % 7];
        const uint32_t allowed = r() % 3 ? 0xffffffffu : uint32_t(r() % 4);
        cTSBinSkinFragFilter f(ages, genders, keyA, keyB, styles, categories, parts, allowed, key(), 1);
        cIGZUnknown *const part = r() % 8 ? U(kResource) : nullptr;
        for (unsigned n = 1 + r() % 6; n; --n)
            b.push_back(gOurs ? uint32_t(f.MatchesPart(part)) : gOrigMatchesPart(&f, part) & 0xff);
        break;
    }
    case 69: { // cTSCosmeticsFragFilter's MatchesProperties 0x505bc0 and AcceptItem 0x504de0 (through its +0xe8 face) called
               // directly (slot 26 reaches them rarely): ours against the original on one of our filters, masks mostly all
               // bits, keys absent, zero or another, custom content on or off; property sets kObj0..1 (the texture face
               // half the time, else the mesh face two times in three), cached items with a fragment, a set and the flag
        const auto mask = [&] { return r() % 3 ? 0xffffffffu : uint32_t(r() % 16); };
        const auto key = [&]() -> const SkinKey * { const unsigned k = r() % 4; return k < 2 ? nullptr : k == 2 ? &gZero : Key(r); };
        static const uint32_t kParts[] = {0, 1, 4, 0xffff, 0x8000, 0x10000};
        const uint32_t ages = mask(), genders = mask(), styles = r() % 2 ? 0u : mask();
        const SkinKey *const keyB = key(), *const keyA = key(), *const keyC = key();
        const uint32_t parts = kParts[r() % 6], flags = r() % 2 ? 0xffffffffu : uint32_t(r() % 4);
        cTSCosmeticsFragFilter f(ages, genders, styles, keyB, keyA, keyC, parts, flags);
        f.mAllowCustom = uint8_t(r() % 2);
        for (unsigned n = 1 + r() % 4; n; --n) {
            if (r() % 2) {
                cIGZUnknown *const properties = r() % 8 ? U(kObj0 + r() % 2) : nullptr;
                b.push_back(gOurs ? uint32_t(f.MatchesProperties(properties)) : gOrigMatchesProperties(&f, properties) & 0xff);
            } else {
                CASItem item{};
                if (r() % 6) item.refs[0].Assign(U(kPool[r() % 5]));
                if (r() % 6) item.refs[2].Assign(U(kObj0 + r() % 2));
                item.flag = r() % 2;
                cITSFragEntryFilter *const face = &f;
                b.push_back(gOurs ? uint32_t(f.AcceptItem(&item)) : gOrigCosmeticsItem(face, &item) & 0xff);
            }
        }
        break;
    }
    case 70: { // FindNamedPart 0x501f80 called directly (slot 38, game code, does): ours against the original; no model, no
               // name, names found or not; the outs' sentinel shows whether they were nulled
        static const char *const kNames[] = {"face", "Hair", "", nullptr};
        void *const model = r() % 5 ? &F(kObj0) : nullptr;
        const char *const name = kNames[r() % 4];
        void *part = reinterpret_cast<void *>(uintptr_t(0x5e5e0001)), *names = reinterpret_cast<void *>(uintptr_t(0x5e5e0002));
        if (gOurs) FindNamedPart(model, name, &part, &names);
        else gOrigFindNamedPart(model, name, &part, &names);
        b.push_back(svcfx::NormOut(P(part), 0x5e5e0001));
        b.push_back(svcfx::NormOut(P(names), 0x5e5e0002));
        for (void *p : {part, names})
            if (p && P(p) < 0x5e5e0000u) reinterpret_cast<cIGZUnknown *>(p)->Release();
        break;
    }
    default: { // DoMessage (+0x20): a message, or none
        void *message = r() % 8 ? static_cast<void *>(&F(kMsg)) : nullptr;
        gModeOp = true; // entering / leaving CAS mode (lane JS)
        b.push_back(Slot<uint32_t>(target, 3, message) & 0xff);
        gModeOp = false;
        break;
    }
    }
}

// `focus`: ops drawn from that list instead (CASSystem_Refresh), so the shared run's sequence stays as it was.
Blob RunSide(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals,
             const std::vector<unsigned> *focus = nullptr)
{
    ConfigureFakes(seed);
    MakeKeys(seed);
    gDraw.seed(seed * 17 + 5);
    gStepCalls = 0;
    gOurs = ours;
    gOrigLoad = ctx.Original<uint32_t(__thiscall *)(void *, const SkinKey *, uint32_t, void **)>(0x502dc0);
    gOrigGatherType = ctx.Original<GatherTypeFn>(0x509b90);
    gOrigGatherBin = ctx.Original<GatherBinFn>(0x50a1d0);
    gOrigFillCache = ctx.Original<FillCacheFn>(0x50bc00);
    gOrigLoadByKey = ctx.Original<LoadByKeyFn>(0x502c00);
    gOrigAcceptFragment = ctx.Original<AcceptFragmentFn>(0x504fa0);
    gOrigIsZeroKey = ctx.Original<IsZeroKeyFn>(0x504cb0);
    gOrigMatches = ctx.Original<MatchesFn>(0xb647d0);
    gOrigMeshAccept = ctx.Original<AcceptFragmentFn>(0xb64960);
    gOrigMatchesPart = ctx.Original<AcceptFragmentFn>(0x505a60);
    gOrigMatchesProperties = ctx.Original<AcceptFragmentFn>(0x505bc0);
    gOrigCosmeticsItem = ctx.Original<decltype(gOrigCosmeticsItem)>(0x504de0);
    gOrigFindNamedPart = ctx.Original<decltype(gOrigFindNamedPart)>(0x501f80);
    globals.Set(0x10f5194, seed % 2 ? &F(kFw) : nullptr);            // the framework's cache, empty now and then
    globals.Set(0x10dac1c, seed % 3 ? &F(kPersist) : nullptr);       // LoadResource's own cache
    globals.Set(0x10faf78, &F(kCatalog));                             // the bin catalog (none now and then: op 16)
    Blob b;
    Sys *s = Build(ctx, b, ours);
    svcfx::AddRange(s, Sys::kSize, kTag);
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    Dump(b, s, ours);
    Populate(s, seed);
    Slot<uint32_t>(s, 1); // counted twice: QueryInterface's references come and go without destroying it
    Slot<uint32_t>(s, 1);
    std::mt19937 r(seed * 31 + 7);
    const unsigned ops = 3 + unsigned(r() % 14);
    for (unsigned i = 0; i < ops; ++i) {
        unsigned op = unsigned(r() % 90);
        if (op >= 80) op = op < 86 ? 38 : 35;           // lane JP: slots 38 / 35 more often (38's deep paths)
        if (op >= 71) op = op < 73 ? 10 : op < 75 ? 39 : op < 79 ? 36 : 70; // lane JO: slots 10 / 39 / 36, FindNamedPart
        if ((op == 45 || op == 46) && r() % 3) op = 47; // Init / Shutdown rarer
        if (op >= 57 && op < 60) op = 23;               // QueryFragments more often (its cache)
        if (op == 56) op = 25;                          // FilterFragments more often (its filter)
        if (op == 54 || op == 55) op = op == 54 ? 32 : 34; // slots 32 / 34 more often (lane JN: their many paths)
        if (op == 48) op = 20;                          // FillNames more often (lane JN)
        if (focus) op = (*focus)[r() % focus->size()];
        char label[64];
        std::snprintf(label, sizeof(label), "seed %u %s op %u code %u", seed, ours ? "ours" : "orig", i, op);
        ctx.Mark(label);
        if (svcfx::gTrace) fprintf(stderr, "%s\n", label);
        b.push_back(0xa0000000u | op);
        RunOp(b, s, op, r);
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        Dump(b, s, ours);
        svcfx::CaptureFakes(b, kValueKinds);
        b.push_back(Self(*GameAddress<void *>(0x10dac1c)));
        b.push_back(Self(*GameAddress<void *>(0x10f5194)));
    }
    // The last Release destroys it (cRZUnknownMT's Release deletes at a count of 1 or below).
    while (Word(s, 8) > 1) b.push_back(Slot<uint32_t>(s, 2));
    b.push_back(Slot<uint32_t>(s, 2));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    return b;
}

// What both tests stand in for (the comment at the top), and the forwarded bodies: each patched with its stand-in after its
// dword count is checked against the image, except `real` (run as the image's code) and `special` (patched with `specialFn`).
struct Stage {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::CodePatch clock{0x1ba00b, reinterpret_cast<const void *>(&ImageClock)};
    shape::StandIn walk5{0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5};
    shape::StandIn walk6{0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6};
    shape::StandIn seedStand{0x1b8ecb, &cRZRandom::Seed, &StandSeed};
    shape::StandIn stepChild{0x507600, &cTSCASSystem::StepChildJob, &StandStepChild}; // ours: CASSystem_StepChild (lane JU)
    shape::StandIn stepNewSim{0x5083b0, &cTSCASSystem::StepNewSimJob, &StandStepNewSim}; // ours: CASSystem_StepNewSim (lane JT)
    shape::StandIn stepOutfit{0x508c60, &cTSCASSystem::StepOutfitJob, &StandStepOutfit}; // ours: CASSystem_StepOutfit (lane JV)
    shape::CodePatch appOptions{0x20df9a, reinterpret_cast<const void *>(&StandAppOptions)}; // CAS mode (lane JS)
    shape::CodePatch isCustom{0x54d600, reinterpret_cast<const void *>(&StandIsCustom)};
    shape::CodePatch zeroKey{0x4f8e50, reinterpret_cast<const void *>(&StandZeroKey)};
    shape::CodePatch keyName{0x4fdd30, reinterpret_cast<const void *>(&StandKeyName)};
    shape::CodePatch simObject{0x54a130, reinterpret_cast<const void *>(&StandSimObject)};
    shape::CodePatch simOutfit{0x54ac80, reinterpret_cast<const void *>(&StandSimOutfit)};
    shape::CodePatch specialItems{0x503e20, reinterpret_cast<const void *>(&StandSpecialItems)};
    shape::CodePatch nameIdApp{0x20df26, reinterpret_cast<const void *>(&StandNameIdApp)};
    shape::CodePatch modelOf{0xaeba70, reinterpret_cast<const void *>(&StandModelOf)};
    shape::CodePatch objectForId{0x549f80, reinterpret_cast<const void *>(&StandObjectForId)}; // FixOutfitParts (lane JQ)
    shape::CodePatch outfitFixed{0x54b250, reinterpret_cast<const void *>(&StandOutfitFixed)};
    shape::CodePatch objectOutfit{0x54a990, reinterpret_cast<const void *>(&StandObjectOutfit)};
    shape::CodePatch dialogs{0x20e031, reinterpret_cast<const void *>(&StandDialogs)};
    shape::CodePatch matchFilter{0xb64d90, reinterpret_cast<const void *>(&StandMatchFilter)};
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn formatText{0x1d0df0,
                              static_cast<cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint8_t, uint32_t)>(
                                  &cTSString::Construct),
                              &StandFormatText};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    decltype(gRZRandomClockSeedOverride) savedClock = gRZRandomClockSeedOverride;

    explicit Stage(difftest::Context &)
    {
        globals.Set(0x10c9378, &F(kFw)); // the framework (GetServiceObject)
        globals.Set(0x10faf70, &F(kUI));
        globals.Set(0x10faf74, &F(kRes));
        globals.Set(svcfx::kPersistCell, &F(kPersist));
        globals.Set(0x10faf80, &F(kSkin));
        globals.Set(svcfx::kAppCell, &F(kRes)); // the app (GetCachedSingleton)
        gRZRandomClockSeedOverride = &ClockSeed;
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    }
    ~Stage() { gRZRandomClockSeedOverride = savedClock; }
    Stage(const Stage &) = delete;
    Stage &operator=(const Stage &) = delete;
};

// ---- CASSystem_EditorQuery ----

struct EditorQuery {
    void *bin;
    uint32_t kind;
    int32_t a4;
    const SkinKey *keyA, *keyB;
    uint32_t a7, a8, a9, a10, unused;
};

Blob RunEditor(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals)
{
    ConfigureFakes(seed);
    MakeKeys(seed);
    F(kMsgFace).Set(4, 0, 0xd9u); // the message: enter CAS
    gDraw.seed(seed * 17 + 5);
    gStepCalls = 0;
    gOurs = ours;
    globals.Set(0x10f5194, seed % 2 ? &F(kFw) : nullptr);
    globals.Set(0x10dac1c, seed % 3 ? &F(kPersist) : nullptr);
    globals.Set(0x10faf78, &F(kCatalog)); // entering CAS gathers through it
    Blob b;
    Sys *s = Build(ctx, b, ours);
    svcfx::AddRange(s, Sys::kSize, kTag);
    Populate(s, seed);
    Slot<uint32_t>(s, 1); // one more reference: released at the end
    uint8_t *const base = reinterpret_cast<uint8_t *>(s);
    gModeOp = true;
    b.push_back(Slot<uint32_t>(base + 0x20, 3, static_cast<void *>(&F(kMsg))) & 0xff); // DoMessage: enter CAS
    gModeOp = false;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    Dump(b, s, ours);
    // The editor (cTSUISimEditor's base): 0x757705 reads its +0x180 (the system's face) and +0x288 (a filter; none: the
    // query goes straight to slot 23).
    alignas(4) uint8_t editor[0x290] = {};
    *reinterpret_cast<void **>(editor + 0x180) = base + 0x1c;
    const auto query = ctx.Original<int32_t(__thiscall *)(void *, void *, CASItemVector *, uint32_t, int32_t, const SkinKey *,
                                                          const SkinKey *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x757705);
    std::mt19937 r(seed * 37 + 9);
    EditorQuery q{};
    for (unsigned i = 0, n = 1 + unsigned(r() % 8); i < n; ++i) {
        if (i == 0 || r() % 2) { // a new query, else the last one again (answered from the cache when no bin)
            q.bin = r() % 4 ? nullptr : static_cast<void *>(&F(kRef0 + r() % kRefs));
            q.kind = uint32_t(r() % 4);
            q.a4 = int32_t(r() % 5) - 2;
            q.keyA = KeyOrNull(r);
            q.keyB = KeyOrNull(r);
            q.a7 = uint32_t(r() % 2);
            q.a8 = uint32_t(r() % 2);
            q.a9 = uint32_t(r() % 2);
            q.a10 = r() % 4 ? uint32_t(r() % 2) : uint32_t(r());
            q.unused = uint32_t(r());
        }
        CASItemVector out;
        if (r() % 3 == 0) FillItems(out, r); // what the editor's vector held before
        char label[64];
        std::snprintf(label, sizeof(label), "seed %u %s query %u", seed, ours ? "ours" : "orig", i);
        ctx.Mark(label);
        b.push_back(0xa0000000u | i);
        b.push_back(uint32_t(query(editor, q.bin, &out, q.kind, q.a4, q.keyA, q.keyB, q.a7, q.a8, q.a9, q.a10, q.unused)));
        DumpItems(b, out);
        b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
        svcfx::gEv.clear();
        Dump(b, s, ours);
        svcfx::CaptureFakes(b, kValueKinds);
    }
    while (Word(s, 8) > 1) b.push_back(Slot<uint32_t>(s, 2));
    b.push_back(Slot<uint32_t>(s, 2));
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    return b;
}

} // namespace cassys_test

DIFF_TEST(CASSystem_Run, 0x4ff850)
{
    using namespace cassys_test;
    Stage stage(ctx);
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        gCreator = seed % 5 == 4;
        const Blob original = RunSide(ctx, seed, false, stage.globals);
        const Blob ours = RunSide(ctx, seed, true, stage.globals);
        if (!svcfx::Same(ctx, original, ours, "run", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            size_t op = 0; // the operation the difference is in
            for (size_t k = 0; k < i; ++k)
                if ((original[k] & 0xffffff00u) == 0xa0000000u) op = k;
            fprintf(stderr, "seed %u: op at %zu code %u\n", seed, op, original[op] & 0xff);
            for (size_t k = i > 120 && op < i - 120 ? i - 120 : op; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}

// Lane JQ: RefreshContent (slot 14) 0x50a9f0 and FixOutfitParts (slot 9) 0x502490 with FixOutfitPart 0x502120, more often
// than the shared run draws them, between the operations that fill and read the caches they touch (IsNotDefault, Slot16,
// ResetCaches, FillTypeCaches, FilterFragments, QueryFragments, GatherByType, IsIdle).
DIFF_TEST(CASSystem_Refresh, 0x50a9f0)
{
    using namespace cassys_test;
    Stage stage(ctx);
    const std::vector<unsigned> focus = {14, 14, 14, 9, 9, 9, 16, 13, 17, 18, 25, 23, 60, 11};
    for (uint32_t seed = 0; seed < 800; ++seed) {
        gCreator = seed % 5 == 4;
        const Blob original = RunSide(ctx, seed, false, stage.globals, &focus);
        const Blob ours = RunSide(ctx, seed, true, stage.globals, &focus);
        if (!svcfx::Same(ctx, original, ours, "refresh", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            size_t op = 0;
            for (size_t k = 0; k < i; ++k)
                if ((original[k] & 0xffffff00u) == 0xa0000000u) op = k;
            fprintf(stderr, "seed %u: op at %zu code %u\n", seed, op, original[op] & 0xff);
            for (size_t k = i > 60 && op < i - 60 ? i - 60 : op; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}

// Lane JS: CreateSim (slot 5) 0x5011d0 and CreateChild (slot 6) 0x500e50, more often than the shared run draws them, with
// what reads the queues and results they fill: OnTick (47) steps the queued jobs (the copies 0x4ff570 / the list nodes
// made there, the outfit job 0x4ff710 / 0x500180), IsIdle (11), GetResult (12), RunOutfitJob (8); and DoMessage (99) with
// CAS mode's EnterCASMode 0x50b960 / LeaveCASMode 0x50b830 (the settings, the app options, the display settings).
DIFF_TEST(CASSystem_Create, 0x5011d0)
{
    using namespace cassys_test;
    Stage stage(ctx);
    // 99: DoMessage (RunOp's default, which the shared run's op draw seldom if ever reaches): entering / leaving CAS mode.
    const std::vector<unsigned> focus = {5, 5, 5, 5, 6, 6, 6, 6, 47, 47, 47, 11, 12, 8, 99, 99, 99};
    for (uint32_t seed = 0; seed < 800; ++seed) {
        gCreator = seed % 5 == 4;
        const Blob original = RunSide(ctx, seed, false, stage.globals, &focus);
        const Blob ours = RunSide(ctx, seed, true, stage.globals, &focus);
        if (!svcfx::Same(ctx, original, ours, "create", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            size_t op = 0;
            for (size_t k = 0; k < i; ++k)
                if ((original[k] & 0xffffff00u) == 0xa0000000u) op = k;
            fprintf(stderr, "seed %u: op at %zu code %u\n", seed, op, original[op] & 0xff);
            for (size_t k = i > 60 && op < i - 60 ? i - 60 : op; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}

// The Create-a-Sim editor's fragment query, as the game runs it when step 3 opens (lane JJ): the system enters CAS
// (DoMessage 0xd9), then the editor's query 0x757705 (game code) calls slot 23 with eleven dwords, and slot 23's body
// (0x5039b0, run as the image's code on both sides: our forwarder against the game's vtable) answers from the query cache
// or asks slot 31 (through the system's own vtable: our forwarder, or the game's) and files the answer.
DIFF_TEST(CASSystem_EditorQuery, 0x4ff850)
{
    using namespace cassys_test;
    Stage stage(ctx);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        gCreator = seed % 5 == 4;
        const Blob original = RunEditor(ctx, seed, false, stage.globals);
        const Blob ours = RunEditor(ctx, seed, true, stage.globals);
        svcfx::Same(ctx, original, ours, "editor", seed);
    }
}

// ---- CASSystem_StepNewSim (lane JT) ----
//
// The new-sim job stepper 0x5083b0 (cTSCASSystem::StepNewSimJob) called directly, the image's against ours, on a job in
// the test's memory, from a drawn state (0..7 and past it) until it answers false (at most 10 steps). Its world is the
// fixture's fakes, plugged in by cell: the app (0x10cc288; null now and then: the framework's slot 5 finds none), the
// persist system (0x10faf6c; slot 3 the resource manager), the CAS UI (0x10faf70), the framework (the director's slot 12;
// slot 3 makes the outfit object). `this` is a block whose +0x1c is a fake (the cITSCASSystem face: slots 20, 33, 36),
// which is all the stepper reads of the system. Compared after each step: the answer (al), the job's words, the extras
// list, the event log (texts the stepper hands out by content: the outfit's name, the key on its stack) and the fakes'
// counts and words. Boundary values: the age (0, 1..3, 4, 0x10001: the age's short), ages / genders -1 and every value the
// age-kind switch knows (and 3, 0x10, 0x80), arg3 -1 / 0 / 1 / 2, arg4 0..5 and -1 (the key's 1..4), 0..3 extras (null or
// counted, matching the outfit or not), the vector of keys null / empty / one / two, and names of 0, 15 and 16+ chars.
namespace newsim_step {

using namespace nTSCASCore;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

enum : unsigned {
    kFw = 0, kApp, kPersist, kUI, kMgr, kTables, kEntry, kWorld, kHook, kSim, kSource, kSource2, kObj67, kOutfit,
    kX0 = 14, // 14..16 the extras
};
constexpr uint32_t kJobTag = 0x6a000000, kSysTag = 0x73000000, kKeysTag = 0x6b000000;

std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }

struct FakeSystem {
    uint8_t pad[0x1c];
    Obj face; // +0x1c
};

SkinKey gKeys[2];
struct KeyRange {
    const SkinKey *first, *last;
} gRange;

// The outfit object's slots past the fixture's 80.
void *gWideOutfit[120];
template <unsigned Slot> uint32_t __fastcall Wide0(Obj *self, void *)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(Slot), Ev(0);
    return 0;
}
// The wide slots' argument kinds (svcfx::Word): 95 / 109 take an object (a fake or kObj67's 0x1100), 98 two data words.
constexpr char WideKind(unsigned slot) { return slot == 95 || slot == 109 ? 'p' : slot == 98 ? 'd' : 0; }
template <unsigned Slot> uint32_t WideArg(uint32_t v, unsigned i)
{
    return svcfx::Word(WideKind(Slot), v, [&] { return "the outfit's wide slot " + std::to_string(Slot) + " argument " + std::to_string(i); },
                       "in WideKind");
}
template <unsigned Slot> uint32_t __fastcall Wide1(Obj *self, void *, uint32_t a)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(Slot), Ev(1), Ev(WideArg<Slot>(a, 0));
    return 0;
}
template <unsigned Slot> uint32_t __fastcall Wide2(Obj *self, void *, uint32_t a, uint32_t b)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(Slot), Ev(2), Ev(WideArg<Slot>(a, 0)), Ev(WideArg<Slot>(b, 1));
    return 0;
}

uint32_t MakeOutfit(Obj *, uint32_t *a) // the framework's slot 3: (class, iid, &out)
{
    ++F(kOutfit).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kOutfit);
    return 1;
}
uint32_t TableEntry(Obj *, uint32_t *a) // the tables' slot 3: (&out)
{
    ++F(kEntry).refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &F(kEntry);
    return 1;
}
uint32_t SourceOf(Obj *, uint32_t *) { return Draw(3) ? P(&F(kSource)) : P(&F(kSource2)); }
uint32_t SimIdWord(Obj *, uint32_t *) { return Draw(2) ? 0xabcd8123u : 0x00010042u; } // the short is zero-extended
uint32_t SimData(Obj *, uint32_t *) { return 0xbeef0000u | Draw(3); }                // a short: 0, 1, 2
uint32_t Bit(Obj *, uint32_t *) { return 0xff00u | Draw(2); }                        // the comparisons' words
uint32_t Flags(Obj *, uint32_t *) { return Draw(16); }                               // bit 3: flag 8
uint32_t Kind40(Obj *, uint32_t *) { return Draw(4); }                               // 0, 1, 2 or other
uint32_t Shown(Obj *, uint32_t *) { return 0xaaaaaa00u | Draw(2); }                  // ShowFace: al
uint32_t Keys(Obj *, uint32_t *)
{
    switch (Draw(4)) {
    case 0: return 0;
    case 1: gRange = {&gKeys[0], &gKeys[0]}; break;
    case 2: gRange = {&gKeys[0], &gKeys[1]}; break;
    default: gRange = {&gKeys[0], &gKeys[2]}; break;
    }
    return P(&gRange);
}
uint32_t KeyWords(Obj *, uint32_t *a) // slot 55: (&key) on the stepper's stack, by content
{
    for (unsigned i = 0; i < 4; ++i) Ev(reinterpret_cast<const uint32_t *>(uintptr_t(a[0]))[i]);
    return 0;
}
const char *const kNames[] = {"", "Bob", "fifteen chars.!", "a name of sixteen", "Newbie"};
uint32_t NameOut(Obj *, uint32_t *a) // slots 24 / 49: (cRZString *out)
{
    std::string &text = reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString;
    Ev(uint32_t(text.size()));
    text = kNames[Draw(5)];
    return 0;
}
// Slot 9: (const char *), straight in the table: the text (a heap block past 15 chars) is logged by content only.
uint32_t __fastcall NameIn(Obj *self, void *, const char *text)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(9), Ev(1);
    const size_t n = std::strlen(text);
    Ev(uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
    return 0;
}

void Configure(FakeSystem &sys)
{
    svcfx::ResetAll();
    sys.face.Reset(300);
    sys.face.Set(20, "pp", 0x100u); // FillNames
    sys.face.Set(33, "ppd", 1u);     // ApplyParts
    sys.face.Set(36, "pd", 0, &Shown);
    F(kFw).Set(3, "ddp", 0, &MakeOutfit);
    F(kFw).Set(5, "ddp", 0u); // GetCachedSingleton's query: no app
    F(kApp).Set(10, 0), F(kApp).Set(26, 0), F(kApp).Set(27, 0, &F(kTables));
    // The world (slot 4) and the hook (slot 29) are asked twice: the same answer each time (a seed's draw).
    F(kApp).Set(4, 0, Draw(3) ? &F(kWorld) : nullptr), F(kApp).Set(29, 0, Draw(3) ? &F(kHook) : nullptr);
    F(kPersist).Set(3, 0, &F(kMgr));
    F(kUI).Set(71, "d"), F(kUI).Set(12, "d");
    F(kMgr).Set(8, "p");
    F(kTables).Set(3, "p", 0, &TableEntry), F(kTables).Set(6, "dp"), F(kTables).Set(7, "d"), F(kTables).Set(8, "d");
    F(kEntry).Set(15, "pd");
    F(kWorld).Set(39, "p-d");
    F(kHook).Set(4, "d");
    F(kSim).Set(8, 0, 0, &SimIdWord), F(kSim).Set(10, 0, 0, &SourceOf);
    F(kSim).Set(16, "d", 0, &SimData), F(kSim).Set(17, "dd");
    for (unsigned s : {kSource, kSource2}) {
        F(s).Set(67, 0, &F(kObj67)), F(s).Set(28, 0, 0x2800u + s);
        F(s).Set(24, "p", 0, &NameOut), F(s).Set(49, "p", 0, &NameOut);
        F(s).table[9] = reinterpret_cast<void *>(&NameIn);
    }
    F(kObj67).Set(11, 0, 0x1100u);
    // kObj67's object 0x1100 (slot 11) is a token the outfit's slots 95 / 109 get: registered as an object of its own.
    svcfx::AddRange(reinterpret_cast<const void *>(uintptr_t(0x1100)), 4, 0x11000000);
    Obj &o = F(kOutfit);
    for (unsigned s : {8u, 9u, 10u, 50u, 51u, 52u, 68u}) o.Set(s, "d");
    for (unsigned s : {22u, 23u, 24u}) o.Set(s, 0, 0, &Bit);
    o.Set(21, 0, 0, &Flags), o.Set(40, 0, 0, &Kind40), o.Set(41, 0, 0x4100u), o.Set(42, "dd", 0, &Keys);
    o.Set(55, "p", 0, &KeyWords);
    for (unsigned x = kX0; x < kX0 + 3; ++x) {
        for (unsigned s : {22u, 23u, 24u}) F(x).Set(s, 0, 0, &Bit);
        F(x).Set(21, 0, 0, &Flags);
    }
    std::memcpy(gWideOutfit, o.table, sizeof(o.table));
    for (unsigned i = svcfx::kSlots; i < 120; ++i) gWideOutfit[i] = nullptr;
    gWideOutfit[89] = reinterpret_cast<void *>(&Wide0<89>);
    gWideOutfit[95] = reinterpret_cast<void *>(&Wide1<95>);
    gWideOutfit[96] = reinterpret_cast<void *>(&Wide0<96>);
    gWideOutfit[98] = reinterpret_cast<void *>(&Wide2<98>);
    gWideOutfit[109] = reinterpret_cast<void *>(&Wide1<109>);
    o.vt = gWideOutfit;
    svcfx::AddRange(&sys, sizeof(sys), kSysTag);
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeysTag);
}

void DumpJob(Blob &b, const NewSimJob &job)
{
    b.insert(b.end(), {job.state, uint32_t(job.age), job.arg2, job.arg3, job.arg4, job.outfitKind, uint32_t(job.done)});
    b.push_back(svcfx::Norm(P(job.sim.Get())));
    b.push_back(svcfx::Norm(P(job.ref20.Get())));
    b.push_back(svcfx::Norm(P(job.ref24.Get())));
    b.push_back(uint32_t(job.extras.size()));
    for (const CASRef &x : job.extras) b.push_back(svcfx::Norm(P(x.Get())));
}

Blob RunStepper(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals)
{
    Blob b;
    FakeSystem sys{};
    gDraw.seed(seed);
    Configure(sys);
    std::mt19937 r(seed * 7919 + 3);
    static const int32_t kAges[] = {0, 1, 2, 3, 4, 0x10001};
    static const uint32_t kArg2[] = {UINT32_MAX, 1, 2, 4, 8, 0x20, 0x40, 3, 0x10, 0x80};
    static const uint32_t kArg3[] = {UINT32_MAX, 0, 1, 2};
    static const uint32_t kArg4[] = {0, 1, 2, 3, 4, 5, UINT32_MAX};
    globals.Set(svcfx::kAppCell, r() % 12 ? static_cast<void *>(&F(kApp)) : nullptr);
    globals.Set(0x10c9378, &F(kFw));
    globals.Set(svcfx::kPersistCell, &F(kPersist));
    globals.Set(0x10faf70, &F(kUI));
    svcfx::gFrameworkIndex = kFw;
    {
        NewSimJob job{};
        svcfx::AddRange(&job, sizeof(job), kJobTag);
        job.state = r() % 4 ? 0 : uint32_t(r() % 10);
        job.age = kAges[r() % 6];
        job.arg2 = kArg2[r() % 10];
        job.arg3 = kArg3[r() % 4];
        job.arg4 = kArg4[r() % 7];
        job.outfitKind = r() % 2 ? 4u | uint32_t(r() % 4) : 0xfbu & uint32_t(r());
        job.sim.Assign(reinterpret_cast<cIGZUnknown *>(&F(kSim)));
        if (job.state > 0 || r() % 3 == 0) job.ref20.Assign(reinterpret_cast<cIGZUnknown *>(&F(r() % 2 ? kSource : kSource2)));
        if (job.state > 0 || r() % 3 == 0) job.ref24.Assign(reinterpret_cast<cIGZUnknown *>(&F(kOutfit)));
        for (unsigned n = unsigned(r() % 4), i = 0; i < n; ++i)
            job.extras.push_back(CASRef(r() % 4 ? reinterpret_cast<cIGZUnknown *>(&F(kX0 + r() % 3)) : nullptr));
        svcfx::gEv.clear();
        const auto original = ctx.Original<uint32_t(__thiscall *)(void *, NewSimJob *)>(0x5083b0);
        for (unsigned step = 0; step < 10; ++step) {
            char label[64];
            std::snprintf(label, sizeof(label), "seed %u %s step %u", seed, ours ? "ours" : "orig", step);
            ctx.Mark(label);
            const uint32_t more = ours ? uint32_t(reinterpret_cast<cTSCASSystem *>(&sys)->StepNewSimJob(&job))
                                       : original(&sys, &job) & 0xff;
            b.push_back(0xa0000000u | step);
            b.push_back(more);
            DumpJob(b, job);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
            b.push_back(uint32_t(sys.face.refs));
            if (!more) break;
        }
    }
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end()); // the job's destruction
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
    return b;
}

} // namespace newsim_step

DIFF_TEST(CASSystem_StepNewSim, 0x5083b0)
{
    using namespace newsim_step;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&cassys_test::GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&cassys_test::GameDelete)};
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        const Blob original = RunStepper(ctx, seed, false, globals);
        const Blob ours = RunStepper(ctx, seed, true, globals);
        if (!svcfx::Same(ctx, original, ours, "stepnewsim", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            for (size_t k = i > 40 ? i - 40 : 0; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}

// ---- CASSystem_StepChild (lane JU) ----
//
// The child job stepper 0x507600 (cTSCASSystem::StepChildJob) called directly, the image's against ours, on a job in the
// test's memory, from a drawn state (0..8) until it answers false (at most 10 steps). `this` is a block whose +0xb8 is a
// seeded cRZRandom (mRandom, all the stepper reads of the system); its seeds are compared after each step (the draws). The
// world is the fixture's fakes, plugged in by cell: the app (always there: the original uses it unchecked), the persist
// system, the CAS UI, the resource service (0x10faf74), the framework (the director's slot 12). Stood in on both sides: the
// key's text 0x4fdd30, the hair colour key 0xb5cce0 and the default gene GUID 0xb5e990 (game code ours calls through
// OriginalFunction), cRZString::Sprintf 0x1ad6f7 and cTSString's flagless loader 0x1d0e90. The weighted-gene vector's insert
// (0x4f50e0 / ours), its destructor 0x4f5cc0 and the gene's 0x257860 run for real; the genes handed out are logged by
// content. The age index 0x54b6e0 (only 0x507600 calls it; ours inlines it as AgeIndexOfBit) is reached through state 1
// with every kind below. Texts are logged by content. Boundary values: the kind 1 (a baby) against 0, 2, 4, 8, 3, 0x101;
// the description data's short (0, 1, 2, with and without upper bits); the parents the same or not, either description
// missing; the outfit's age kind (0..5, 8, 9, 0x101) and genders (1, 2, 0x102); 0..3 faces (slot 4 1 / 2 / 0x101, the
// props' flag bit 0); genes empty or not; texts of 0, 15 and 16+ chars, and the same name twice.
namespace child_step {

using namespace nTSCASCore;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;
using svcfx::P;

enum : unsigned {
    kFw = 0, kApp, kPersist, kUI, kRes, kMgr, kFactory, kDescA, kDescB, kDescSrc, kObj67, kNames, kTables, kEntry, kWorld,
    kHook, kSim, kRef18, kOutfit, kProps,
    kFace0 = 20, // 20..22 the faces the resource service finds
};
constexpr uint32_t kJobTag = 0x6a000000, kSysTag = 0x73000000, kKeysTag = 0x6b000000, kParentTag = 0x70000000;

std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }

struct FakeSystem {
    uint8_t pad[0xb8];
    cRZRandom random; // +0xb8 (mRandom)
};
static_assert(offsetof(FakeSystem, random) == 0xb8, "mRandom");

SkinKey gKeys[4];
uint8_t gParents[2];
uint32_t gGeneWords[2];
struct Words2 {
    void *begin, *end;
} gGenes; // the outfit's slot 34: only its begin and end are read

void LogText(const char *text)
{
    const size_t n = std::strlen(text);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(text[i])) * 16777619u;
    Ev(0x7e000000u | uint32_t(n));
    Ev(h);
}
void LogKey(const SkinKey *k) { Ev(k->type), Ev(k->group), Ev(k->instance), Ev(k->resource); }
const char *const kTexts[] = {"", "Bob", "fifteen chars.!", "a name of sixteen", "Newbie", "Bob"};

// The outfit object's slots past the fixture's 80 (84, 89, 96 and 98).
void *gWideOutfit[100];
template <unsigned Slot> uint32_t __fastcall Wide0(Obj *self, void *)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(Slot), Ev(0);
    return 0;
}
// The wide slots' argument kinds (svcfx::Word): 95 / 109 take an object (a fake or kObj67's 0x1100), 98 two data words.
constexpr char WideKind(unsigned slot) { return slot == 95 || slot == 109 ? 'p' : slot == 98 ? 'd' : 0; }
template <unsigned Slot> uint32_t WideArg(uint32_t v, unsigned i)
{
    return svcfx::Word(WideKind(Slot), v, [&] { return "the outfit's wide slot " + std::to_string(Slot) + " argument " + std::to_string(i); },
                       "in WideKind");
}
template <unsigned Slot> uint32_t __fastcall Wide2(Obj *self, void *, uint32_t a, uint32_t b)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(Slot), Ev(2), Ev(WideArg<Slot>(a, 0)), Ev(WideArg<Slot>(b, 1));
    return 0;
}

uint32_t FactoryDescription(Obj *, uint32_t *a) // the factory's 16(parent, 1): the parent's description, now and then none
{
    const unsigned which = a[0] == P(&gParents[0]) ? 0 : 1;
    return Draw(6) ? P(&F(kDescA + which)) : 0;
}
uint32_t DescriptionData(Obj *, uint32_t *) { return (Draw(2) ? 0xbeef0000u : 0u) | Draw(3); } // a short: 0, 1, 2
uint32_t SimIdWord(Obj *, uint32_t *) { return Draw(2) ? 0xabcd8123u : 0x00010042u; }         // the short is zero-extended
uint32_t SimSource(Obj *, uint32_t *) { return Draw(3) ? P(&F(kRef18)) : P(&F(kDescSrc)); }   // the sim's slot 10
uint32_t SourceWord(Obj *, uint32_t *)
{
    static const uint32_t k[] = {0, 7, 0x80000000u, 123456789};
    return k[Draw(4)];
}
uint32_t MakeOutfit(Obj *, uint32_t *a) // the framework's slot 3: (class, iid, &out)
{
    ++F(kOutfit).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kOutfit);
    return 1;
}
uint32_t FindEntry(Obj *, uint32_t *a) // the tables' slot 4: (sim id, &entry); al the answer
{
    if (!Draw(4)) return 0xab00u;
    ++F(kEntry).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kEntry);
    return 0xab01u;
}
uint32_t Genders(Obj *, uint32_t *)
{
    static const uint32_t k[] = {1, 2, 2, 0x102};
    return k[Draw(4)];
}
uint32_t AgeKind(Obj *, uint32_t *)
{
    static const uint32_t k[] = {0, 1, 1, 2, 3, 4, 5, 8, 9, 0x101};
    return k[Draw(10)];
}
uint32_t KeyPtr(Obj *, uint32_t *) { return P(&gKeys[Draw(4)]); }
uint32_t GenesOf(Obj *, uint32_t *)
{
    gGenes.begin = &gGeneWords[0];
    gGenes.end = &gGeneWords[Draw(3) ? 0 : 1];
    return P(&gGenes);
}
uint32_t KeyArg(Obj *, uint32_t *a) // persist 6 / UI 74: (&key, ...) on the stepper's stack, by content
{
    LogKey(reinterpret_cast<const SkinKey *>(uintptr_t(a[0])));
    return 0;
}
uint32_t GenesArg(Obj *, uint32_t *a) // outfit 61 / entry 10: (&genes, ...)
{
    const auto *v = reinterpret_cast<const nTSGenetics::DNAWeightedVector *>(uintptr_t(a[0]));
    Ev(uint32_t(v->end - v->begin));
    for (const auto *g = v->begin; g != v->end; ++g) {
        LogText(g->name.mString.c_str());
        LogText(g->value.mString.c_str());
        Ev(uint32_t(g->weight));
    }
    return 0;
}
uint32_t NameOut(Obj *, uint32_t *a) // (cRZString *out)
{
    std::string &text = reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString;
    LogText(text.c_str());
    text = kTexts[Draw(6)];
    return 0;
}
uint32_t NameArg(Obj *, uint32_t *a) // (const cRZString *)
{
    LogText(reinterpret_cast<const cRZString *>(uintptr_t(a[0]))->mString.c_str());
    return 0;
}
uint32_t FaceKind(Obj *, uint32_t *)
{
    static const uint32_t k[] = {1, 1, 2, 0x101};
    return k[Draw(4)];
}
uint32_t FaceFlags(Obj *, uint32_t *) { return 0xff00u | Draw(4); }

// Slots that take a text, straight in the table (the text, a heap block past 15 chars, is logged by content only).
uint32_t __fastcall TextIn9(Obj *self, void *, const char *text)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(9);
    LogText(text);
    return 0;
}
uint32_t __fastcall UiText38(Obj *self, void *, const char *text, uint32_t a, uint32_t b)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(38);
    LogText(text);
    Ev(a), Ev(b);
    return 0;
}
uint32_t __fastcall UiText48(Obj *self, void *, const char *text, uint32_t scale, uint32_t flag)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(48);
    LogText(text);
    Ev(scale), Ev(flag);
    return 0;
}
uint32_t __fastcall TablesFile(Obj *self, void *, uint32_t kind, uint32_t a, uint32_t b, uint32_t id, const char *text,
                               uint32_t one)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 10u, kind, Norm(a), Norm(b), id}) Ev(w);
    LogText(text);
    Ev(one);
    return 0;
}
// The resource service's slot 25 (FindType4, nine words): the faces found, appended to the stepper's vector.
uint32_t __fastcall FindFaces(Obj *self, void *, SkinPtrVector *out, uint32_t ages, uint32_t genders, uint32_t flags,
                              const SkinKey *keyA, const SkinKey *keyB, uint32_t id, uint32_t mask22, uint32_t mask)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 25u, Norm(P(out)), uint32_t(out->size()), ages, genders, flags}) Ev(w);
    LogKey(keyA);
    LogKey(keyB);
    Ev(id), Ev(mask22), Ev(mask);
    for (unsigned n = Draw(4), i = 0; i < n; ++i) out->push_back(&F(kFace0 + Draw(3)));
    return 0;
}

// Stand-ins (both sides).
void __fastcall StandKeyName(const SkinKey *key, void *, GZServiceString *out) // 0x4fdd30
{
    Ev(0x4fdd30), Ev(Norm(P(key)));
    LogKey(key);
    *out = kTexts[(key->type + key->group + key->instance) % 6];
}
SkinKey *__cdecl StandHairColor(SkinKey *out, uint32_t ages, uint32_t genders, const SkinKey *key) // 0xb5cce0
{
    Ev(0xb5cce0), Ev(ages), Ev(genders), Ev(Norm(P(key)));
    *out = {0x4a1c, Draw(5), genders, ages};
    return out;
}
SkinKey *__cdecl StandGeneGuid(SkinKey *out, uint32_t kind, uint32_t index) // 0xb5e990
{
    Ev(0xb5e990), Ev(kind), Ev(index);
    *out = {0x6e6e, kind, index, Draw(5)};
    return out;
}
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...) // 0x1ad6f7: "%d" and its word
{
    Ev(0x1ad6f7);
    LogText(fmt);
    va_list args;
    va_start(args, fmt);
    const int value = va_arg(args, int);
    va_end(args);
    Ev(uint32_t(value));
    char text[32];
    std::snprintf(text, sizeof(text), fmt, value);
    self->mString = text;
    return self;
}
cTSString *__fastcall StandTableText(cTSString *self, void *, uint32_t table, uint32_t entry, uint32_t group, uint32_t type)
{
    for (uint32_t w : {0x1d0e90u, Norm(P(self)), table & 0xffff, entry & 0xffff, group, type}) Ev(w);
    self->ConstructEmpty();
    self->mString.FromChar(kTexts[(table + entry) % 6]);
    return self;
}

void Configure(FakeSystem &sys, uint32_t seed)
{
    svcfx::ResetAll();
    std::memset(&sys, 0, sizeof(sys));
    sys.random.Construct(seed * 2654435761u + 1);
    for (unsigned i = 0; i < 4; ++i) gKeys[i] = {0x100 + Draw(3), i, Draw(2), 0};
    F(kFw).Set(3, "ddp", 0, &MakeOutfit);
    F(kApp).Set(10, 0, &F(kFactory)), F(kApp).Set(27, 0, &F(kTables)), F(kApp).Set(29, 0, &F(kHook));
    F(kApp).Set(4, 0, Draw(3) ? &F(kWorld) : nullptr); // asked twice: one answer a seed
    F(kPersist).Set(3, 0, &F(kMgr)), F(kPersist).Set(6, "p-d--", 0, &KeyArg);
    F(kMgr).Set(8, "p");
    Obj &ui = F(kUI);
    ui.Set(71, "d"), ui.Set(16, 0), ui.Set(54, "p"), ui.Set(74, "p", 0, &KeyArg), ui.Set(29, "pddddd-"), ui.Set(12, "d");
    ui.table[38] = reinterpret_cast<void *>(&UiText38);
    ui.table[48] = reinterpret_cast<void *>(&UiText48);
    F(kRes).table[25] = reinterpret_cast<void *>(&FindFaces);
    F(kFactory).Set(16, "pd", 0, &FactoryDescription);
    for (unsigned d : {kDescA, kDescB}) F(d).Set(16, "d", 0, &DescriptionData), F(d).Set(10, 0, &F(kDescSrc));
    for (unsigned s : {kDescSrc, kRef18}) {
        F(s).Set(67, 0, &F(kObj67)), F(s).Set(54, 0, &F(kNames)), F(s).Set(28, 0, 0, &SourceWord);
        F(s).Set(24, "p", 0, &NameOut);
        F(s).table[9] = reinterpret_cast<void *>(&TextIn9);
    }
    F(kObj67).Set(11, 0, 0, &SourceWord);
    F(kNames).Set(7, "p", 0, &NameOut), F(kNames).Set(11, "p", 0, &NameArg), F(kNames).Set(12, "p", 0, &NameArg);
    F(kTables).Set(4, "dp", 0, &FindEntry);
    F(kTables).table[10] = reinterpret_cast<void *>(&TablesFile);
    F(kEntry).Set(16, "p"), F(kEntry).Set(10, "pd", 0, &GenesArg);
    F(kWorld).Set(39, "p-d");
    F(kHook).Set(4, "d");
    F(kSim).Set(8, 0, 0, &SimIdWord), F(kSim).Set(10, 0, 0, &SimSource), F(kSim).Set(17, "dd");
    Obj &o = F(kOutfit);
    for (unsigned s : {8u, 9u, 10u, 50u, 51u, 52u, 54u, 67u, 68u}) o.Set(s, "d");
    o.Set(23, 0, 0, &Genders), o.Set(24, 0, 0, &AgeKind);
    for (unsigned s : {28u, 29u, 30u}) o.Set(s, 0, 0, &KeyPtr);
    o.Set(34, 0, 0, &GenesOf), o.Set(61, "p", 0, &GenesArg);
    std::memcpy(gWideOutfit, o.table, sizeof(o.table));
    for (unsigned i = svcfx::kSlots; i < 100; ++i) gWideOutfit[i] = nullptr;
    gWideOutfit[84] = reinterpret_cast<void *>(&Wide0<84>);
    gWideOutfit[89] = reinterpret_cast<void *>(&Wide0<89>);
    gWideOutfit[96] = reinterpret_cast<void *>(&Wide0<96>);
    gWideOutfit[98] = reinterpret_cast<void *>(&Wide2<98>);
    o.vt = gWideOutfit;
    for (unsigned f = kFace0; f < kFace0 + 3; ++f) F(f).Set(4, 0, 0, &FaceKind), F(f).Set(3, 0, &F(kProps)), F(f).Set(5, 0, 0, &KeyPtr);
    F(kProps).Set(25, 0, 0, &FaceFlags);
    svcfx::AddRange(&sys, sizeof(sys), kSysTag);
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeysTag);
    svcfx::AddRange(gParents, sizeof(gParents), kParentTag);
}

void DumpJob(Blob &b, const ChildJob &job)
{
    b.insert(b.end(), {job.state, Norm(P(job.parentA)), Norm(P(job.parentB)), job.kind, uint32_t(job.done)});
    b.push_back(Norm(P(job.sim.Get())));
    b.push_back(Norm(P(job.ref18.Get())));
    b.push_back(Norm(P(job.ref1c.Get())));
}

Blob RunStepper(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals)
{
    Blob b;
    FakeSystem sys;
    gDraw.seed(seed);
    Configure(sys, seed);
    std::mt19937 r(seed * 7919 + 5);
    static const uint32_t kKinds[] = {1, 1, 1, 2, 4, 8, 0, 3, 0x101};
    globals.Set(svcfx::kAppCell, &F(kApp));
    globals.Set(0x10c9378, &F(kFw));
    globals.Set(svcfx::kPersistCell, &F(kPersist));
    globals.Set(0x10faf70, &F(kUI));
    globals.Set(0x10faf74, &F(kRes));
    svcfx::gFrameworkIndex = kFw;
    {
        ChildJob job{};
        svcfx::AddRange(&job, sizeof(job), kJobTag);
        job.state = r() % 4 ? 0 : uint32_t(r() % 9);
        job.parentA = &gParents[r() % 2];
        job.parentB = &gParents[r() % 2];
        job.kind = kKinds[r() % 9];
        job.sim.Assign(reinterpret_cast<cIGZUnknown *>(&F(kSim)));
        if (job.state > 0 || r() % 3 == 0) job.ref18.Assign(reinterpret_cast<cIGZUnknown *>(&F(r() % 2 ? kRef18 : kDescSrc)));
        if (job.state > 1 || r() % 3 == 0) job.ref1c.Assign(reinterpret_cast<cIGZUnknown *>(&F(kOutfit)));
        svcfx::gEv.clear();
        const auto original = ctx.Original<uint32_t(__thiscall *)(void *, ChildJob *)>(0x507600);
        for (unsigned step = 0; step < 10; ++step) {
            char label[64];
            std::snprintf(label, sizeof(label), "seed %u %s step %u", seed, ours ? "ours" : "orig", step);
            ctx.Mark(label);
            const uint32_t more = ours ? uint32_t(reinterpret_cast<cTSCASSystem *>(&sys)->StepChildJob(&job))
                                       : original(&sys, &job) & 0xff;
            b.push_back(0xa0000000u | step);
            b.push_back(more);
            DumpJob(b, job);
            b.push_back(sys.random.mIntegerSeed);
            b.push_back(uint32_t(sys.random.mRealSeed));
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
            if (!more) break;
        }
    }
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end()); // the job's destruction
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
    return b;
}

} // namespace child_step

DIFF_TEST(CASSystem_StepChild, 0x507600)
{
    using namespace child_step;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&cassys_test::GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&cassys_test::GameDelete)};
    shape::CodePatch keyName{0x4fdd30, reinterpret_cast<const void *>(&StandKeyName)};
    shape::CodePatch hairColor{0xb5cce0, reinterpret_cast<const void *>(&StandHairColor)};
    shape::CodePatch geneGuid{0xb5e990, reinterpret_cast<const void *>(&StandGeneGuid)};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    shape::StandIn tableText{0x1d0e90,
                             static_cast<cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint32_t)>(&cTSString::Construct),
                             &StandTableText};
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        const Blob original = RunStepper(ctx, seed, false, globals);
        const Blob ours = RunStepper(ctx, seed, true, globals);
        if (!svcfx::Same(ctx, original, ours, "stepchild", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            for (size_t k = i > 40 ? i - 40 : 0; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}

// ---- CASSystem_StepOutfit (lane JV) ----
//
// The outfit job stepper 0x508c60 (cTSCASSystem::StepOutfitJob) called directly, the image's against ours, on a job in the
// test's memory, from a drawn state (0..8) until it answers false (at most 10 steps). `this` is a block whose +0xac is the
// edited sim (a fake or null), all the stepper reads of the system. The world is the fixture's fakes, plugged in by cell: the
// app (always there: the original uses it unchecked; slot 10 the factory), the persist system (GetResource 5, SaveResource
// 16, DeleteResource 17, FindSegment 34, RegisterSegment 45, the resource manager 3), the CAS UI (0x10faf70), the framework
// (the director's slot 12; slot 3 the object 0xcdcbe0e3). Stood in on both sides: the key's text 0x4fdd30, the hair colour
// key 0xb5cce0, the zero key 0x4f8e50, the dialog service 0x20e031 (game code ours calls through OriginalFunction) and
// cRZString::Sprintf 0x1ad6f7. The original's own helpers run for real: the map of key vectors by bit (0x500480, 0x1463b0,
// 0x148bb0, its erase 0x4fe600), the key vector's growth 0x1d1370 and FreeThroughAllocatorService. Keys the stepper hands
// out from its vectors or its stack are logged by content, texts by content; the bool the outfit's slot 99 gets in its
// fourth word by its low byte (the original stores the byte over a stale word: `mov byte ptr [ebp-0x1c], al`). Boundary
// values: every age value the index switch knows (and 3, 8, 0x80, 0), genders 1 / 2 / 3 / 0x102, the age kind 0 / 1 /
// 0x101, kinds with bits 0..4 each on and off, keys A / B given or not, the zero face key, 0..6 listed keys (the count the
// segment answers equal to the list or one short) of the stale types, the part type and others, parts that load or not
// (0x100), fit or not, part bits 0, single, several, 0xc80, the top bit; bool answers 0 / 1 / 0x100, slot 40's 0 / 1 / 0x100,
// texts of 0, 15 and 16+ chars, no thumbnail, no hook, no edited sim, a description without a source or a false slot 65.
namespace outfit_step {

using namespace nTSCASCore;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;
using svcfx::P;

enum : unsigned {
    kFw = 0, kApp, kPersist, kUI, kFactory, kSim, kRef20, kSrc, kOutfit, kSegment, kRecord, kPart, kProps, kThumb, kMgr,
    kDialogSvc, kDialogs, kHook, kNames, kInfo, kEdited, kGenes,
};
constexpr uint32_t kJobTag = 0x6a000000, kSysTag = 0x73000000, kKeysTag = 0x6b000000;
constexpr uint32_t kOutfitIid = 0xac598ea8, kPartIid = 0xebcf3e20, kThumbnailIid = 0xaccef84c, kSegmentIid = 0x656b8f0c;

std::mt19937 gDraw;
uint32_t Draw(uint32_t n) { return uint32_t(gDraw() % n); }

struct FakeSystem {
    uint8_t pad[0xac];
    void *editedSim; // +0xac (mEditedSim)
    uint8_t rest[0x10];
};
static_assert(offsetof(FakeSystem, editedSim) == 0xac, "mEditedSim");

SkinKey gKeys[5]; // [4] the zero key
SkinKey gZero;
uint32_t gAges, gGenders;
constexpr uint32_t kListedTypes[] = {0xebcf3e27, 0xebcf3e27, 0xebcf3e27, 0xac598eac, 0xe519c933, 0xfc700d40, 0x7ba3838c,
                                     0xfc6eb1f7, 0x65245517, 0x49596978, 0x1c4a276c, 0xac506764, 0x856ddbac, 0x12345678,
                                     0xebcf3e26, 0};

using child_step::LogKey;
using child_step::LogText;
const char *const kTexts[] = {"", "Bob", "fifteen chars.!", "a name of sixteen", "Newbie", "Bob"};

uint32_t Bool(Obj *, uint32_t *)
{
    static const uint32_t k[] = {0, 1, 1, 0x100, 0x101};
    return k[Draw(5)];
}
uint32_t Word(Obj *, uint32_t *)
{
    static const uint32_t k[] = {0, 7, 0x80000000u, 123456789};
    return k[Draw(4)];
}
uint32_t Ages(Obj *, uint32_t *) { return gAges; }
uint32_t Genders(Obj *, uint32_t *) { return gGenders; }
uint32_t PartAges(Obj *, uint32_t *) { return Draw(4) ? gAges : gAges ^ 0x20; }
uint32_t PartGenders(Obj *, uint32_t *) { return Draw(4) ? gGenders : gGenders + 1; }
uint32_t PartBits(Obj *, uint32_t *)
{
    static const uint32_t k[] = {0, 1, 2, 0x81, 0x1004, 0xc80, 0x80000400u, 0x1fff, 0x2000, 0x3c0, 0x1300};
    return k[Draw(11)];
}
uint32_t AgeKind(Obj *, uint32_t *)
{
    static const uint32_t k[] = {1, 1, 0, 0x101, 2};
    return k[Draw(5)];
}
uint32_t KindMask(Obj *, uint32_t *)
{
    // multi-bit masks name no listed bit; single bits (0x40, 0x100, 0x200, 0x1000) the parts' bits past the first
    static const uint32_t k[] = {0, 2, 8, 0x3d40, 0x280, 0xffffffffu, 0x40, 0x100, 0x200, 0x1000};
    return k[Draw(10)];
}
uint32_t KeyPtr(Obj *, uint32_t *) { return P(&gKeys[Draw(5)]); }
uint32_t KeyArg(Obj *, uint32_t *a) // (&key): on the stepper's stack, by content
{
    LogKey(reinterpret_cast<const SkinKey *>(uintptr_t(a[0])));
    return 0;
}
uint32_t OutfitKeyOf(Obj *, uint32_t *a) // the outfit's slot 3(&key): its key written, the buffer answered
{
    *reinterpret_cast<SkinKey *>(uintptr_t(a[0])) = {0xac598eac, Draw(3), 1, 0};
    return a[0];
}
uint32_t SimOf(Obj *, uint32_t *) { return P(&F(kSim)); }                     // the factory's 17(source), uncounted
uint32_t SimSource(Obj *, uint32_t *) { return Draw(5) ? P(&F(kRef20)) : 0; }  // the description's slot 10
uint32_t NameOut(Obj *, uint32_t *a)                                           // (cRZString *out)
{
    std::string &text = reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString;
    LogText(text.c_str());
    text = kTexts[Draw(6)];
    return 0;
}
uint32_t MakeHook(Obj *, uint32_t *a) // the framework's slot 3(class, iid, &out)
{
    if (!Draw(4)) return 0;
    ++F(kHook).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kHook);
    return 1;
}
// The segment's slot 19(&keys, 0): 0..6 keys appended; the count is the list's or one short.
uint32_t ListKeys(Obj *, uint32_t *a)
{
    auto *keys = reinterpret_cast<std::vector<SkinKey, GZServiceAllocator<SkinKey>> *>(uintptr_t(a[0]));
    const uint32_t n = Draw(7);
    for (uint32_t i = 0; i < n; ++i)
        keys->push_back({kListedTypes[Draw(uint32_t(std::size(kListedTypes)))], Draw(4), Draw(2), 0});
    return n && !Draw(4) ? n - 1 : n;
}

// Slots that take a heap pointer or a text, straight in the table (logged by content).
uint32_t __fastcall GetResource(Obj *self, void *, const SkinKey *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 5u, iid, Norm(P(out)), a, b}) Ev(w);
    LogKey(key);
    unsigned give = 0;
    uint32_t answer = 0;
    switch (iid) {
    case kOutfitIid: give = kOutfit, answer = Draw(2) ? 1 : 0x100; break; // the outfit (used unchecked)
    case kPartIid: {
        const uint32_t d = Draw(5);
        if (d) give = kPart, answer = d == 1 ? 0x100 : 1; // loaded but answered false: still released
        break;
    }
    case kThumbnailIid:
        if (Draw(3)) give = kThumb, answer = 1;
        break;
    }
    if (give) {
        ++F(give).refs;
        *out = &F(give);
    }
    return answer;
}
uint32_t __fastcall FindSegment(Obj *self, void *, const SkinKey *key, uint32_t iid, void **out)
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 34u, iid, Norm(P(out))}) Ev(w);
    LogKey(key);
    Obj &o = F(iid == kSegmentIid ? kSegment : kRecord); // both used unchecked
    ++o.refs;
    *out = &o;
    return 1;
}
uint32_t __fastcall DeleteResource(Obj *self, void *, const SkinKey *key)
{
    Ev(svcfx::evCall), Ev(self->id), Ev(17);
    LogKey(key);
    return 1;
}
uint32_t __fastcall SetPart(Obj *self, void *, uint32_t kind, uint32_t bit, const SkinKey *key) // the outfit's slot 70
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 70u, kind, bit}) Ev(w);
    LogKey(key);
    return 0;
}
uint32_t __fastcall ClearParts(Obj *self, void *, uint32_t kind, uint32_t bit) // 82
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 82u, kind, bit}) Ev(w);
    return 0;
}
uint32_t __fastcall SetFromStored(Obj *self, void *, void *segment, uint32_t group, uint32_t zero, uint32_t fresh,
                                  const SkinKey *hair, const SkinKey *face, uint32_t how) // 99
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 99u, Norm(P(segment)), group, zero, fresh & 0xff, how}) Ev(w);
    LogKey(hair);
    LogKey(face);
    return 0;
}
uint32_t __fastcall Untidy(Obj *self, void *, uint32_t parts) // 115
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 115u, parts}) Ev(w);
    return 0;
}
uint32_t __fastcall UiHasFace(Obj *self, void *, uint32_t ageIndex, uint32_t genderIndex, const char *text) // 40
{
    for (uint32_t w : {uint32_t(svcfx::evCall), self->id, 40u, ageIndex, genderIndex}) Ev(w);
    LogText(text);
    static const uint32_t k[] = {0, 0, 1, 0x100};
    return k[Draw(4)];
}
template <unsigned Slot> uint32_t __fastcall UiAsk(Obj *self, void *, const char *text) // 7 / 8
{
    Ev(svcfx::evCall), Ev(self->id), Ev(Slot);
    LogText(text);
    return Bool(self, nullptr);
}
uint32_t __fastcall UiThumbnail(Obj *self, void *, void *thumbnail, const char *text) // 5
{
    Ev(svcfx::evCall), Ev(self->id), Ev(5), Ev(Norm(P(thumbnail)));
    LogText(text);
    return 0;
}

// Stand-ins (both sides).
SkinKey *__cdecl StandHairColor(SkinKey *out, uint32_t ages, uint32_t genders, const SkinKey *key) // 0xb5cce0
{
    Ev(0xb5cce0), Ev(ages), Ev(genders), Ev(Norm(P(key)));
    LogKey(key);
    *out = {0x4a1c, Draw(5), genders, ages};
    return out;
}
const SkinKey *__cdecl StandZeroKey() // 0x4f8e50
{
    Ev(0x4f8e50);
    return &gZero;
}
void *__cdecl StandDialogs() // 0x20e031
{
    Ev(0x20e031);
    return &F(kDialogSvc);
}

void *gWideOutfit[120];

void Configure(FakeSystem &sys, uint32_t seed)
{
    svcfx::ResetAll();
    std::memset(&sys, 0, sizeof(sys));
    static const uint32_t kAges[] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 3, 0x80, 0};
    static const uint32_t kGenders[] = {1, 2, 2, 3, 0x102};
    gAges = kAges[Draw(10)];
    gGenders = kGenders[Draw(5)];
    for (unsigned i = 0; i < 4; ++i) gKeys[i] = {0x100 + Draw(3), i, Draw(2), seed & 3};
    gKeys[4] = {};
    gZero = {};
    sys.editedSim = Draw(3) ? &F(kEdited) : nullptr;
    F(kFw).Set(3, "ddp", 0, &MakeHook);
    F(kApp).Set(10, 0, &F(kFactory)), F(kApp).Set(26, 0), F(kApp).Set(27, 0);
    F(kFactory).Set(17, "d", 0, &SimOf);
    F(kSim).Set(10, 0, 0, &SimSource);
    Obj &r = F(kRef20);
    r.Set(65, 0, 0, &Bool), r.Set(67, 0, &F(kSrc)), r.Set(24, "p", 0, &NameOut), r.Set(49, "p", 0, &NameOut);
    r.table[9] = reinterpret_cast<void *>(&child_step::TextIn9);
    r.Set(54, 0, &F(kNames)), r.Set(4, 0, &F(kInfo)), r.Set(3, 0);
    F(kSrc).Set(11, 0, 0, &Word);
    F(kNames).Set(4, "pd");
    F(kInfo).Set(3, "d", 0, &Word);
    Obj &p = F(kPersist);
    p.Set(3, 0, &F(kMgr)), p.Set(16, "pp"), p.Set(45, "p");
    p.table[5] = reinterpret_cast<void *>(&GetResource);
    p.table[17] = reinterpret_cast<void *>(&DeleteResource);
    p.table[34] = reinterpret_cast<void *>(&FindSegment);
    F(kMgr).Set(8, "p");
    F(kSegment).Set(19, "p-", 0, &ListKeys), F(kSegment).Set(10, 0, 0, &Bool);
    F(kRecord).Set(7, 0);
    F(kPart).Set(3, 0, &F(kProps));
    F(kProps).Set(19, 0, 0, &PartAges), F(kProps).Set(20, 0, 0, &PartGenders), F(kProps).Set(22, 0, 0, &PartBits);
    Obj &ui = F(kUI);
    ui.Set(71, "d"), ui.Set(74, "p", 0, &KeyArg), ui.Set(54, "p", 0, &KeyArg), ui.Set(13, "d", 0, &Bool), ui.Set(34, "p", 0, &Bool);
    ui.Set(33, "p"), ui.Set(22, "ddd---");
    ui.table[40] = reinterpret_cast<void *>(&UiHasFace);
    ui.table[38] = reinterpret_cast<void *>(&child_step::UiText38);
    ui.table[8] = reinterpret_cast<void *>(&UiAsk<8>);
    ui.table[7] = reinterpret_cast<void *>(&UiAsk<7>);
    ui.table[5] = reinterpret_cast<void *>(&UiThumbnail);
    F(kDialogSvc).Set(4, 0, &F(kDialogs));
    F(kDialogs).Set(20, "d"), F(kDialogs).Set(15, "ddp");
    F(kHook).Set(3, 0), F(kHook).Set(10, "d");
    F(kEdited).Set(9, "d");
    Obj &o = F(kOutfit);
    o.Set(3, "p", 0, &OutfitKeyOf), o.Set(6, "d", 0, &Word), o.Set(22, 0, 0, &Ages), o.Set(23, 0, 0, &Genders);
    o.Set(24, 0, 0, &AgeKind), o.Set(25, 0, 0, &KindMask), o.Set(34, 0, &F(kGenes)), o.Set(68, "d");
    for (unsigned s : {28u, 29u, 30u}) o.Set(s, 0, 0, &KeyPtr);
    o.table[70] = reinterpret_cast<void *>(&SetPart);
    std::memcpy(gWideOutfit, o.table, sizeof(o.table));
    for (unsigned i = svcfx::kSlots; i < 120; ++i) gWideOutfit[i] = nullptr;
    gWideOutfit[82] = reinterpret_cast<void *>(&ClearParts);
    gWideOutfit[99] = reinterpret_cast<void *>(&SetFromStored);
    gWideOutfit[115] = reinterpret_cast<void *>(&Untidy);
    o.vt = gWideOutfit;
    svcfx::AddRange(&sys, sizeof(sys), kSysTag);
    svcfx::AddRange(gKeys, sizeof(gKeys), kKeysTag);
}

void DumpJob(Blob &b, const OutfitJob &job)
{
    b.insert(b.end(), {job.state, uint32_t(job.step), job.source, uint32_t(job.result), job.kind, job.ageIndex,
                       job.genderIndex, Norm(P(job.sim.Get())), Norm(P(job.ref20.Get())), Norm(P(job.ref24.Get())),
                       uint32_t(job.hasKeyA), uint32_t(job.hasKeyB), uint32_t(job.reloadRecord)});
    for (const SkinKey *k : {&job.keyA, &job.keyB}) b.insert(b.end(), {k->type, k->group, k->instance, k->resource});
}

Blob RunStepper(difftest::Context &ctx, uint32_t seed, bool ours, svcfx::Globals &globals)
{
    Blob b;
    FakeSystem sys;
    gDraw.seed(seed);
    Configure(sys, seed);
    std::mt19937 r(seed * 7919 + 5);
    static const uint32_t kKinds[] = {0, 1, 2, 3, 9, 0x11, 0x13, 0x19, 0x1b, 0x1f, 0xffffffffu, 0x10, 8, 0x101};
    static const uint32_t kSources[] = {0, 7, 0x80000000u, 123456789};
    globals.Set(svcfx::kAppCell, &F(kApp));
    globals.Set(0x10c9378, &F(kFw));
    globals.Set(svcfx::kPersistCell, &F(kPersist));
    globals.Set(0x10faf70, &F(kUI));
    svcfx::gFrameworkIndex = kFw;
    {
        OutfitJob job(kSources[r() % 4], kKinds[r() % std::size(kKinds)]);
        svcfx::AddRange(&job, sizeof(job), kJobTag);
        job.state = r() % 3 ? 0 : uint32_t(r() % 9);
        job.hasKeyA = r() % 2 != 0;
        job.hasKeyB = r() % 2 != 0;
        job.keyA = {0xa0, uint32_t(r() % 3), 1, 0};
        job.keyB = {0xb0, uint32_t(r() % 3), 2, 0};
        static const uint8_t kReload[] = {0, 1, 0x80};
        job.reloadRecord = kReload[r() % 3];
        // A job past state 0, or with its description, has its source (ref20, used unchecked); the outfit may be missing
        // in state 0 only (then loaded).
        if (job.state > 0 || r() % 3 == 0) {
            job.sim.Assign(reinterpret_cast<cIGZUnknown *>(&F(kSim)));
            job.ref20.Assign(reinterpret_cast<cIGZUnknown *>(&F(kRef20)));
        }
        if (job.state > 0 || r() % 2) job.ref24.Assign(reinterpret_cast<cIGZUnknown *>(&F(kOutfit)));
        svcfx::gEv.clear();
        const auto original = ctx.Original<uint32_t(__thiscall *)(void *, OutfitJob *)>(0x508c60);
        for (unsigned step = 0; step < 10; ++step) {
            char label[64];
            std::snprintf(label, sizeof(label), "seed %u %s step %u", seed, ours ? "ours" : "orig", step);
            ctx.Mark(label);
            const uint32_t more = ours ? uint32_t(reinterpret_cast<cTSCASSystem *>(&sys)->StepOutfitJob(&job))
                                       : original(&sys, &job) & 0xff;
            b.push_back(0xa0000000u | step);
            b.push_back(more);
            DumpJob(b, job);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
            if (!more) break;
        }
    }
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end()); // the job's destruction
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
    return b;
}

} // namespace outfit_step

DIFF_TEST(CASSystem_StepOutfit, 0x508c60)
{
    using namespace outfit_step;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&cassys_test::GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&cassys_test::GameDelete)};
    shape::CodePatch keyName{0x4fdd30, reinterpret_cast<const void *>(&child_step::StandKeyName)};
    shape::CodePatch hairColor{0xb5cce0, reinterpret_cast<const void *>(&StandHairColor)};
    shape::CodePatch zeroKey{0x4f8e50, reinterpret_cast<const void *>(&StandZeroKey)};
    shape::CodePatch dialogs{0x20e031, reinterpret_cast<const void *>(&StandDialogs)};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &child_step::StandSprintf};
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        const Blob original = RunStepper(ctx, seed, false, globals);
        const Blob ours = RunStepper(ctx, seed, true, globals);
        if (!svcfx::Same(ctx, original, ours, "stepoutfit", seed) && ctx.failures.size() <= 3) {
            size_t i = 0;
            while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
            for (size_t k = i > 40 ? i - 40 : 0; k < i + 8 && k < original.size() && k < ours.size(); ++k)
                fprintf(stderr, "%5zu %08x %08x\n", k, original[k], ours[k]);
        }
    }
}
