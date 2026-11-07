// Differential tests for cTSNeighbor (src/ts/cTSNeighbor.h): construction and destruction, the interface slots, Read/Write
// against scripted fake streams, the attribute accessors and predicates, the person data / record / relationship queries
// that walk fakes, and the persist face.
//
// A world is one neighbour in a guarded buffer whose vtable is a table of recording fakes (the slots its own members call
// through `this`), plus generic recording fakes for everything it reaches: the app singleton (cache 0x10cc288) and what it
// hands out, the persist service (0x10faf6c), the director framework (0x10c9390), person data objects, the relationship
// table, the other neighbour, streams. Every call a fake receives is appended to `events` (pointers to fakes become ids,
// pointers into the neighbour its offset, stack addresses a marker). Two worlds built from one seed start the same; one runs
// the original, the other ours, and the results, the neighbour's bytes, the fakes' reference counts and the event logs are
// compared. Fakes that give back 16-bit answers fill the upper half of eax with junk, as the original callees may.
#include "difftest.h"
#include "game/game.h"
#include "shape.h"
#include "ts/cTSNeighbor.h"
#include "ts/cTSRelationshipTable.h"

#include <Windows.h>
#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <utility>
#include <vector>

namespace {
using Nb = cTSNeighbor;
constexpr unsigned kSlots = 120;

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);
struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint16_t attrs[0x100];
    void *aux[4];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
};

enum Fake : unsigned {
    kStream, kRel, kPersonOld, kPersonNew, kApp, kCache, kListHolder, kList, kItem0, kItem1, kItem2, kItem3, kPeople, kPersonA,
    kPersonB, kDef, kTuning, kPersist, kScore, kFramework, kRecordA, kRecordB, kReader, kOwner, kKind, kOther, kOtherRel,
    kTarget, kFakeCount
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Nb)];
    uint8_t after[16];
};

struct World {
    Guarded work;
    SObj fakes[kFakeCount];
    std::vector<uint32_t> events;
    uint32_t junk = 0;          // the upper half of eax in 16-bit answers
    std::vector<uint64_t> script; // stream reads: value, bit 32 = the read fails (writes nothing)
    size_t scriptPos = 0;
    void *iterItems[8] = {};
    void *iterEnd = nullptr;
};
World gW;
Nb *N() { return reinterpret_cast<Nb *>(gW.work.bytes); }
SObj &S(Fake f) { return gW.fakes[f]; }

enum : uint32_t { evCall = 0x60, evNb = 0x61, evRead = 0x62, evKey = 0x63, evBegin = 0x64 };
void Ev(std::initializer_list<uint32_t> items) { for (uint32_t v : items) gW.events.push_back(v); }

uint32_t Norm(uint32_t value)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (value == uint32_t(uintptr_t(&gW.fakes[i]))) return 0x1000 + i;
    const uint32_t base = uint32_t(uintptr_t(gW.work.bytes));
    if (value >= base && value < base + sizeof(Nb)) return 0x2000 + (value - base);
    uint32_t local = 0;
    const uint32_t sp = uint32_t(uintptr_t(&local));
    if (value - sp < 0x10000 || sp - value < 0x10000) return 0x57ac;
    return value;
}
uint32_t P(const void *p) { return Norm(uint32_t(uintptr_t(p))); }

// ---- the generic recording fakes ----
uint32_t Dispatch(SObj *self, unsigned slot, uint32_t *args, unsigned count)
{
    Ev({evCall, self->id, slot, count});
    for (unsigned i = 0; i < count; ++i) Ev({Norm(args[i])});
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}
template <size_t> using Word = uint32_t;
void *gTable[kSlots][6];
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(SObj *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, Slot, args, unsigned(sizeof...(I)));
    }
};
template <unsigned Slot> void FillSlot()
{
    gTable[Slot][0] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<0>>::Fn);
    gTable[Slot][1] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<1>>::Fn);
    gTable[Slot][2] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<2>>::Fn);
    gTable[Slot][3] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<3>>::Fn);
    gTable[Slot][4] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<4>>::Fn);
    gTable[Slot][5] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<5>>::Fn);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }

void Set(SObj &o, unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr)
{
    o.table[slot] = gTable[slot][args];
    o.ret[slot] = result;
    o.custom[slot] = fn;
}
void Set(SObj &o, unsigned slot, unsigned args, const void *result) { Set(o, slot, args, uint32_t(uintptr_t(result))); }

void ResetFake(SObj &o, uint32_t id)
{
    o.vt = o.table;
    o.id = id;
    o.refs = 1000;
    for (auto &v : o.value) v = 0;
    for (auto &v : o.attrs) v = 0;
    for (auto &v : o.aux) v = nullptr;
    for (unsigned i = 0; i < kSlots; ++i) o.ret[i] = 0, o.custom[i] = nullptr, o.table[i] = nullptr;
    Set(o, 1, 0);
    Set(o, 2, 0);
}

uint32_t Junk(uint32_t low16) { return low16 | (gW.junk << 16); }

// ---- custom handlers ----
uint32_t StreamGet(SObj *, uint32_t *args, unsigned width)
{
    uint64_t item = gW.scriptPos < gW.script.size() ? gW.script[gW.scriptPos] : uint64_t(0x01010101u) * (gW.scriptPos & 0x7f);
    ++gW.scriptPos;
    Ev({evRead, width, uint32_t(item), uint32_t(item >> 32)});
    if (item >> 32) return 0; // a failing read writes nothing
    if (width == 1) *reinterpret_cast<uint8_t *>(args[0]) = uint8_t(item);
    else if (width == 2) *reinterpret_cast<uint16_t *>(args[0]) = uint16_t(item);
    else *reinterpret_cast<uint32_t *>(args[0]) = uint32_t(item);
    return 1;
}
uint32_t StreamGet8(SObj *s, uint32_t *a) { return StreamGet(s, a, 1); }
uint32_t StreamGet16(SObj *s, uint32_t *a) { return StreamGet(s, a, 2); }
uint32_t StreamGet32(SObj *s, uint32_t *a) { return StreamGet(s, a, 4); }
uint32_t StreamQuery(SObj *self, uint32_t *args)
{
    if (!self->value[0]) return 0;
    *reinterpret_cast<void **>(args[1]) = &S(kReader);
    return 1;
}
uint32_t RelValue(SObj *self, uint32_t *args)
{
    const uint32_t kind = args[1];
    return kind == 1 ? self->value[0] : kind == 8 ? Junk(self->value[1]) : kind == 3 ? self->value[2] : self->value[3];
}
uint32_t AttrByIndex(SObj *self, uint32_t *args) { return Junk(self->attrs[args[0] & 0xff]); }
uint32_t TuningValue(SObj *self, uint32_t *args) { return Junk(self->attrs[args[0] & 0xff]); }
uint32_t DefTuning(SObj *self, uint32_t *args)
{
    const uint32_t which = self->value[7]++ == 0 ? self->value[0] : self->value[1];
    *reinterpret_cast<void **>(args[2]) = which ? &S(kTuning) : nullptr;
    return 0;
}
uint32_t CacheLookup(SObj *self, uint32_t *args)
{
    if (args[0] == 2) return uint32_t(uintptr_t(&S(kPersonOld)));
    return (args[0] & 1) || self->value[0] ? uint32_t(uintptr_t(&S(kPersonNew))) : 0;
}
uint32_t PeopleFind(SObj *self, uint32_t *args)
{
    if (args[0] == self->value[0]) return self->value[1] ? uint32_t(uintptr_t(&S(kPersonA))) : 0;
    return self->value[2] ? uint32_t(uintptr_t(&S(kPersonB))) : 0;
}
uint32_t PersonAttr(SObj *self, uint32_t *args) { return Junk(self->attrs[args[0] & 0xff]); }
uint32_t ListBegin(SObj *, uint32_t *args)
{
    *reinterpret_cast<void **>(args[0]) = &gW.iterItems[0];
    return 0;
}
uint32_t ListEnd(SObj *self, uint32_t *)
{
    gW.iterEnd = &gW.iterItems[self->value[0]];
    return uint32_t(uintptr_t(&gW.iterEnd));
}
uint32_t ItemValueRef(SObj *self, uint32_t *args)
{
    // The callee writes into the caller's argument slot (a 4-byte local here).
    *reinterpret_cast<uint32_t *>(args[0]) = 0xdeadbeef;
    return uint32_t(uintptr_t(&self->aux[args[1] & 3]));
}
uint32_t PersistGet(SObj *self, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev({evKey, key[0], key[1], key[2], key[3]});
    if (!self->value[0]) return 0;
    // The score object (interface 0xac598ea8) is used unchecked by the caller: always given.
    *reinterpret_cast<void **>(args[2]) = args[1] == 0xac598ea8 ? &S(kScore) : self->value[1] ? &S(kRecordA) : nullptr;
    return 1;
}
uint32_t FrameworkCreate(SObj *, uint32_t *args)
{
    *reinterpret_cast<void **>(args[2]) = &S(kRecordB);
    return 1;
}
uint32_t ScoreResult(SObj *self, uint32_t *) { return Junk(self->value[0]); }
uint32_t PersonSimDef(SObj *self, uint32_t *) { return self->value[2] ? uint32_t(uintptr_t(&S(kDef))) : 0; }

// Re-entrancy: a release that fills one of the neighbour's fields again (the destructor's member passes).
uint32_t RefillOnRelease(SObj *self, uint32_t *)
{
    const uint32_t which = self->value[6];
    if (which == 1 && !N()->mRecord) N()->mRecord = &S(kRecordA);
    if (which == 2 && !N()->mRelationships) N()->mRelationships = &S(kRel);
    if (which == 3 && !N()->mPersonData) N()->mPersonData = &S(kPersonNew);
    self->value[6] = 0;
    return uint32_t(--self->refs);
}

// ---- the neighbour's own vtable ----
void *gNbVT[56];
uint32_t __fastcall NbAddRef(Nb *n, void *) { Ev({evNb, 1}); return ++n->mRefCount; }
uint32_t __fastcall NbRelease(Nb *n, void *) { Ev({evNb, 2}); return --n->mRefCount; }
uint32_t __fastcall NbGetId(Nb *n, void *) { Ev({evNb, 8}); return Junk(uint16_t(n->mId)); }
void *__fastcall NbGetPersonData(Nb *n, void *) { Ev({evNb, 10}); return n->mPersonData; }
void *__fastcall NbGetRelationships(Nb *n, void *) { Ev({evNb, 12}); return n->mRelationships; }
uint32_t __fastcall NbGetDataId(Nb *n, void *) { Ev({evNb, 15}); return n->mPersonDataId; }
uint32_t __fastcall NbGetAttribute(Nb *n, void *, int32_t index)
{
    Ev({evNb, 16, uint32_t(index)});
    return Junk(index < int32_t(Nb::kAttributeCount) ? n->mAttributes[index] : 0);
}
void *__fastcall NbDelete(Nb *n, void *, uint32_t flags)
{
    Ev({evNb, 53, flags});
    return n;
}

// ---- the standalone shape (docs/standalone-shape.md): our vtables, two routes, traps ----
// The vtable route: the neighbour carries a copy of our vtables with the recording fakes above patched over it, and the
// function under test is called through our pristine vtable, in the slot where the game's vtable of that face holds the
// original RVA (a slot-order mistake calls the wrong function). The adapter route: the neighbour carries a copy of the game's
// vtables (as when the Create hook is off) with the same fakes, plus an arity-matched trap wherever the game's vtable holds the
// RVA under test, and the RVA hook's adapter is called (an adapter dispatching virtually would hit the trap; in game it would be
// its own hook, an endless loop). The original side always carries the game's vtables with the fakes. Vtable words are the only
// bytes normalised: the game's, ours and the copy of face f compare as one code.
constexpr unsigned kFaces = 2;
constexpr uint32_t kFaceOffset[kFaces] = {0, neighbor::kPersistFace};
constexpr uint32_t kGameVTable[kFaces] = {Nb::kOriginalVTable, Nb::kOriginalPersistVTable};
constexpr unsigned kSlotCount[kFaces] = {54, 12};

struct OurVTables {
    void **const *vt;
};
OurVTables Ours() { return {shape::Ours<Nb>(kFaceOffset).face}; }

enum Route { kVTableRoute, kAdapterRoute };
Route gRoute = kVTableRoute;
bool gOurSide = false;
int gTrapHits = 0;
void *gCopies[kFaces][64];

void *TrapFor(unsigned words) { return shape::TrapFor<shape::Count<gTrapHits>>(words); }

void **GameVTable(unsigned face) { return shape::GameVTable(kGameVTable[face]); }

// The side's vtables, copied, with the recording fakes patched over the +0 face.
void InstallCopies(Nb *n)
{
    const bool ours = gOurSide && gRoute == kVTableRoute;
    for (unsigned f = 0; f < kFaces; ++f) {
        void **base = ours ? Ours().vt[f] : GameVTable(f);
        for (unsigned s = 0; s < kSlotCount[f]; ++s) gCopies[f][s] = base[s];
        *reinterpret_cast<void ***>(reinterpret_cast<uint8_t *>(n) + kFaceOffset[f]) = gCopies[f];
    }
    for (unsigned s = 0; s < kSlotCount[0]; ++s)
        if (gNbVT[s]) gCopies[0][s] = gNbVT[s];
}

void InstallTrap(uint32_t rva, unsigned words)
{
    const void *target = GameAddress<void>(rva);
    for (unsigned f = 0; f < kFaces; ++f)
        for (unsigned s = 0; s < kSlotCount[f]; ++s)
            if (GameVTable(f)[s] == target) gCopies[f][s] = TrapFor(words);
}

uint32_t VtCode(uint32_t word) { return shape::VtCode(word, kFaces, shape::GameTables{kGameVTable}, Ours().vt, gCopies); }

unsigned SlotOf(unsigned face, uint32_t rva) { return shape::SlotOf(kGameVTable[face], kSlotCount[face], rva); }

using shape::Words;

// The function at `rva` (a slot of `face`) called the side's and route's way.
template <class Fn, class... A>
auto Call(difftest::Context &ctx, bool orig, unsigned face, uint32_t rva, void *adapter, A... args)
{
    auto self = shape::SelfAt<Fn>(N(), kFaceOffset[face]);
    // A dropped adapter (lane GP; NB_NA): the original runs on the game-shaped object on the adapter route too.
    if (orig || (gRoute == kAdapterRoute && !adapter)) return ctx.Original<Fn>(rva)(self, args...);
    if (gRoute == kAdapterRoute) {
        InstallTrap(rva, Words<A...>());
        return reinterpret_cast<Fn>(adapter)(self, args...);
    }
    unsigned slot = SlotOf(face, rva);
    if (slot == ~0u) {
        ctx.Fail("rva %x is in no slot of face %u", rva, face);
        slot = 0;
    }
    return reinterpret_cast<Fn>(Ours().vt[face][slot])(self, args...);
}

// A slot no adapter covers (folded bodies, thunks): the game's vtable's function on the original side, our pristine vtable's
// on ours (both routes alike).
template <class Fn, class... A> auto CallSlot(bool orig, unsigned face, unsigned slot, A... args)
{
    auto self = shape::SelfAt<Fn>(N(), kFaceOffset[face]);
    void **vt = orig ? GameVTable(face) : Ours().vt[face];
    return reinterpret_cast<Fn>(vt[slot])(self, args...);
}

void Build(uint32_t seed)
{
    static bool filled = false;
    if (!filled) {
        FillAll(std::make_index_sequence<kSlots>());
        gNbVT[1] = reinterpret_cast<void *>(&NbAddRef);
        gNbVT[2] = reinterpret_cast<void *>(&NbRelease);
        gNbVT[8] = reinterpret_cast<void *>(&NbGetId);
        gNbVT[10] = reinterpret_cast<void *>(&NbGetPersonData);
        gNbVT[12] = reinterpret_cast<void *>(&NbGetRelationships);
        gNbVT[15] = reinterpret_cast<void *>(&NbGetDataId);
        gNbVT[16] = reinterpret_cast<void *>(&NbGetAttribute);
        gNbVT[53] = reinterpret_cast<void *>(&NbDelete);
        filled = true;
    }
    World &w = gW;
    std::mt19937 r(seed * 2654435761u + 3);
    w.events.clear();
    w.script.clear();
    w.scriptPos = 0;
    w.junk = r() & 0xffff;
    for (auto &p : w.iterItems) p = nullptr;
    w.iterEnd = nullptr;
    for (auto &b : w.work.before) b = uint8_t(r());
    for (auto &b : w.work.after) b = uint8_t(r());
    for (auto &b : w.work.bytes) b = uint8_t(r());
    for (unsigned i = 0; i < kFakeCount; ++i) ResetFake(w.fakes[i], 0x100 + i);

    // The relationship tables: flags (kind 1), value (kind 8), the known value (kind 3).
    for (Fake f : {kRel, kOtherRel}) {
        SObj &t = S(f);
        t.value[0] = (r() % 5 ? 0x8000u : 0) | (r() % 2 ? 8u : 0) | (r() & 0xff0000u) | (r() % 3 ? 0 : r() & 0x7f00);
        t.value[1] = uint32_t(int32_t(int(r() % 301) - 100)) & 0xffff;
        t.value[2] = r() % 3 ? r() % 2 : r();
        t.value[3] = r();
        Set(t, 9, 2, 0, RelValue);
        Set(t, 3, 2, r());
        Set(t, 4, 2, r());
        Set(t, 8, 1);
        Set(t, 16, 0, r() % 3 == 0 ? 1 + r() % 0xff : (r() & 0xffffff00));
        Set(t, 17, 1, r());
    }
    // Person data objects.
    for (Fake f : {kPersonOld, kPersonNew}) {
        SObj &p = S(f);
        p.value[0] = r() % 5 ? r() % 50 : r();
        p.value[1] = r() % 2 ? 1 + r() % 0xff : (r() & 0xffffff00);
        p.value[2] = r() % 6 != 0;
        Set(p, 18, 0, 0, PersonSimDef);
        Set(p, 28, 0, p.value[0]);
        Set(p, 4, 0, r() % 3 ? &S(kKind) : nullptr);
        Set(p, 65, 0, p.value[1]);
        Set(p, 67, 0, r() % 5 ? &S(kOwner) : &S(kOwner));
    }
    Set(S(kKind), 3, 1, r() % 3 == 0 ? 0x11 : r() % 0x20);
    Set(S(kOwner), 11, 0, r() % 4 ? r() % 0x1000 : r());
    S(kDef).value[0] = r() % 5 != 0;
    S(kDef).value[1] = r() % 3 != 0;
    Set(S(kDef), 29, 3, 0, DefTuning);
    {
        SObj &t = S(kTuning);
        for (unsigned i = 0; i < 6; ++i) t.attrs[i] = uint16_t(int(r() % 120) - 30);
        t.attrs[3] = uint16_t(int(r() % 60) - 10);
        t.attrs[0x2d] = uint16_t(r() % 30);
        Set(t, 11, 1, 0, TuningValue);
    }
    // The app singleton and what it hands out.
    Set(S(kApp), 6, 0, r() % 6 ? &S(kListHolder) : nullptr);
    Set(S(kApp), 24, 0, &S(kCache));
    Set(S(kApp), 26, 0, &S(kPeople));
    S(kCache).value[0] = r() % 2;
    Set(S(kCache), 14, 1, 0, CacheLookup);
    Set(S(kListHolder), 39, 1, r() % 5 ? &S(kList) : nullptr);
    S(kList).value[0] = r() % 5;
    Set(S(kList), 28, 1, 0, ListBegin);
    Set(S(kList), 29, 1, 0, ListEnd);
    const Fake items[4] = {kItem0, kItem1, kItem2, kItem3};
    for (unsigned i = 0; i < 4; ++i) {
        SObj &it = S(items[i]);
        w.iterItems[i] = &it;
        const uint32_t types[] = {0xcf505ab0, 0x8fcd5705, 0x1234, 0xcf505ab0, 0x8fcd5705};
        it.value[0] = types[r() % 5];
        it.attrs[0] = uint16_t(r() % 4 ? r() % 6 : r());
        it.attrs[1] = uint16_t(r() % 4 ? r() % 40 : r() % 3 ? 0 : r());
        it.attrs[2] = uint16_t(r() % 3 ? r() % 100 : r());
        for (unsigned k = 0; k < 3; ++k) it.aux[k] = &it.attrs[k];
        Set(it, 6, 0, it.value[0]);
        Set(it, 18, 0, Junk(r() % 4));
        Set(it, 24, 2, 0, ItemValueRef);
    }
    S(kPeople).value[0] = r() % 3 ? r() % 50 : 0xffffffffu;
    S(kPeople).value[1] = r() % 4 != 0;
    S(kPeople).value[2] = r() % 3 != 0;
    Set(S(kPeople), 43, 1, 0, PeopleFind);
    for (Fake f : {kPersonA, kPersonB, kOther, kTuning}) {
        SObj &o = S(f);
        if (f != kTuning) {
            for (unsigned i = 0; i < 0x100; ++i) o.attrs[i] = uint16_t(r() % 3 ? r() % 6 : r());
            o.attrs[0x3a] = uint16_t(r() % 2 ? 14 + r() % 8 : r() % 60);
            o.attrs[0x16] = uint16_t(r() % 3 ? int(r() % 5) - 1 : 0);
            o.attrs[0x17] = uint16_t(r() % 3 ? int(r() % 5) - 1 : 0);
        }
    }
    Set(S(kPersonA), 10, 2, 0, PersonAttr);
    Set(S(kPersonB), 10, 2, 0, PersonAttr);
    {
        SObj &o = S(kOther);
        o.value[0] = r() % 4 ? r() % 40 : r();
        o.value[1] = r() % 50;
        Set(o, 8, 0, 0, [](SObj *self, uint32_t *) { return Junk(self->value[0]); });
        Set(o, 12, 0, &S(kOtherRel));
        Set(o, 15, 0, o.value[1]);
        Set(o, 16, 1, 0, AttrByIndex);
    }
    // Persist service, framework, streams.
    S(kPersist).value[0] = r() % 3 != 0;
    S(kPersist).value[1] = r() % 4 != 0;
    Set(S(kPersist), 5, 5, 0, PersistGet);
    Set(S(kFramework), 3, 3, 0, FrameworkCreate);
    Set(S(kRecordB), 15, 1, r());
    S(kScore).value[0] = r() % 3 ? r() % 0x1000 : r() & 0xffff;
    Set(S(kScore), 113, 2, 0, ScoreResult);
    Set(S(kReader), 20, 1);
    Set(S(kStream), 0, 2, 0, StreamQuery);
    S(kStream).value[0] = r() % 2;
    Set(S(kStream), 18, 0, r() % 2 ? 0 : (r() % 3 ? 1 : r() & 0xffffff00));

    // The neighbour: random bytes, then the fields the members read.
    Nb *n = N();
    InstallCopies(n);
    n->mRefCount = r() % 4;
    n->mId = int16_t(r() % 4 == 0 ? 0 : r() % 3 ? int(r() % 60) : int(r() % 65536) - 32768);
    n->mPersonDataId = r() % 4 ? r() % 50 : r() % 2 ? 0 : r();
    n->mPersonData = r() % 3 ? static_cast<void *>(&S(kPersonOld)) : (r() % 2 ? nullptr : &S(kPersonNew));
    n->mRelationships = &S(kRel);
    n->mRecord = r() % 3 == 0 ? static_cast<void *>(&S(kRecordA)) : nullptr;
    n->mFlag20 = uint8_t(r());
    n->mModified = uint8_t(r() % 3 ? r() % 2 : r());
    n->mLayoutVersion = r() % 3 ? 0x36 : r();
    n->mField24 = r();
    for (auto &k : n->mKey) k = r() % 4 ? r() % 100 : r();
    n->mExtra20C = r();
    n->mExtra210 = r();
    static const uint16_t ages[] = {0, 1, 2, 3, 4, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x31, 0x32, 0x33, 0x34, 0x7fff, 0x8000, 0xffff, 0xfff3};
    static const uint16_t species[] = {0, 0, 0, 1, 2, 3, 4, 0xffff, 0x100};
    for (unsigned i = 0; i < Nb::kAttributeCount; ++i) n->mAttributes[i] = uint16_t(r() % 3 ? r() % 8 : r());
    n->mAttributes[Nb::kAgeAttribute] = ages[r() % (sizeof(ages) / 2)];
    n->mAttributes[Nb::kSpeciesAttribute] = species[r() % (sizeof(species) / 2)];
    n->mAttributes[Nb::kGenderAttribute] = uint16_t(r() % 5 ? r() % 3 : r());
    n->mAttributes[Nb::kFlagAttribute] = uint16_t(r() % 2 ? 0 : r());
    if (r() % 2) n->mAttributes[Nb::kAgeAttribute] = uint16_t(14 + r() % 8); // around the teen / adult edges of vf45
    n->mAttributes[0x16] = uint16_t(r() % 3 ? int(r() % 5) - 1 : 0);
    n->mAttributes[0x17] = uint16_t(r() % 3 ? int(r() % 5) - 1 : 0);
    // The other neighbour: the same species (index 0xba) most of the time, mixed genders (0x41).
    S(kOther).attrs[Nb::kSpeciesAttribute] = r() % 5 ? n->mAttributes[Nb::kSpeciesAttribute] : uint16_t(r() % 3);
    S(kOther).attrs[Nb::kGenderAttribute] = uint16_t(r() % 3 ? 0 : r() % 3);
    w.events.clear();
}

using Blob = std::vector<uint32_t>;

void Capture(Blob &b, bool alive = true)
{
    World &w = gW;
    b.push_back(0xb10b);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    if (alive) {
        uint8_t copy[sizeof(Nb)];
        std::memcpy(copy, w.work.bytes, sizeof(copy));
        auto word = [&](unsigned off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(copy + off); };
        for (unsigned off : {0x14u, 0x18u, 0x1cu}) word(off) = Norm(word(off));
        for (unsigned f = 0; f < kFaces; ++f) word(kFaceOffset[f]) = VtCode(word(kFaceOffset[f]));
        for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
    } else {
        for (unsigned i = 0; i < 4; ++i) b.push_back(VtCode(reinterpret_cast<uint32_t *>(w.work.bytes)[i]));
    }
    for (auto &o : w.fakes) b.push_back(uint32_t(o.refs));
    b.push_back(0xe7e7);
    for (uint32_t e : w.events) b.push_back(e);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}

// Points the game's globals at the fakes for the scope.
struct Globals {
    difftest::Context &ctx;
    static constexpr uint32_t kRvas[] = {0x10cc288, 0x10faf6c, 0x10c9390, 0x10c93f4};
    uint32_t saved[4];
    void *directorVT[16] = {};
    static void *__fastcall DirectorFramework(void *, void *) { return &S(kFramework); }
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (unsigned i = 0; i < 4; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
        directorVT[12] = reinterpret_cast<void *>(&DirectorFramework);
        *ctx.image.At<void *>(0x10cc288) = &S(kApp);
        *ctx.image.At<void *>(0x10faf6c) = &S(kPersist);
        *ctx.image.At<void *>(0x10c9390) = directorVT;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
    }
    ~Globals()
    {
        for (unsigned i = 0; i < 4; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
};

template <class Setup, class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Setup setup, Run run, bool alive = true)
{
    Ours();
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gOurSide = side == 1;
            Build(seed);
            std::mt19937 rng(seed * 7919u + 5);
            setup(rng);
            gTrapHits = 0;
            const uint32_t result = uint32_t(run(side == 0));
            blobs[side].push_back(result);
            Capture(blobs[side], alive);
            blobs[side].push_back(uint32_t(gTrapHits));
        }
        Same(ctx, blobs[0], blobs[1], what, seed + (route ? 0x80000000u : 0u)); // adapter-route seeds have the top bit
    }
}
auto NoSetup = [](std::mt19937 &) {};

constexpr uint32_t kSeeds = 200;
#define ORIG(Fn, rva) ctx.Original<Fn>(rva)
// The function at `rva` on face `face` (0 = +0, 1 = the persist face +4), the side's and route's way (`original` in scope).
#define NB(Fn, face, rva, adapter, ...)                                                                                       \
    Call<Fn>(ctx, original, face, rva, reinterpret_cast<void *>(&cTSNeighborAdapters::adapter), ##__VA_ARGS__)
// A slot whose adapter was dropped (lane GP: only the game's vtables reach the original, and no object carries them while the
// Create hook is in; its status entry is "in_effect": "callers"): our vtable on the vtable route, the original on both sides of
// the adapter route. `adapter` names the slot for the reader.
#define NB_NA(Fn, face, rva, adapter, ...) Call<Fn>(ctx, original, face, rva, nullptr, ##__VA_ARGS__)
} // namespace

// ---- construction and destruction ----
DIFF_TEST(Neighbor_Create, 0x4b73c0)
{
    Globals globals(ctx);
    Ours();
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Build(seed);
        using Fn = Nb *(__cdecl *)();
        Nb *a = ORIG(Fn, 0x4b73c0)();
        Nb *b = Nb::Create(); // the hook's entry
        ++ctx.cases;
        if (!a || !b) { ctx.Fail("create returned null"); continue; }
        if (HeapSize(GetProcessHeap(), 0, a) < sizeof(Nb) || HeapSize(GetProcessHeap(), 0, b) < sizeof(Nb)) ctx.Fail("create block size");
        auto word = [](const void *n, uint32_t off) { return *reinterpret_cast<void *const *>(static_cast<const uint8_t *>(n) + off); };
        // The game's vtables from the original, ours from ours (and ours are not the game's); the faces where the original's are.
        if (word(a, 0) != GameVTable(0) || word(a, 4) != GameVTable(1) || word(b, 0) != Ours().vt[0] || word(b, 4) != Ours().vt[1] ||
            Ours().vt[0] == GameVTable(0) || Ours().vt[1] == GameVTable(1))
            ctx.Fail("create vtables");
        if (reinterpret_cast<uint8_t *>(static_cast<cIGZPersistResource *>(b)) != reinterpret_cast<uint8_t *>(b) + neighbor::kPersistFace ||
            static_cast<void *>(static_cast<cITSNeighbor *>(b)) != static_cast<void *>(b))
            ctx.Fail("our faces are not at the original's offsets");
        Blob blobs[2];
        for (Nb *n : {a, b}) {
            Blob &out = blobs[n == b];
            uint8_t copy[sizeof(Nb)];
            std::memcpy(copy, n, sizeof(copy));
            // Bytes the constructor leaves alone (+0xe/+0xf, +0x22/+0x23, +0x1fa/+0x1fb), the relationship table address and the
            // two vtable words (checked above).
            copy[0xe] = copy[0xf] = copy[0x22] = copy[0x23] = copy[0x1fa] = copy[0x1fb] = 0;
            *reinterpret_cast<uint32_t *>(copy + 0x18) = 0;
            *reinterpret_cast<uint32_t *>(copy + 0) = 0xd0000000u;
            *reinterpret_cast<uint32_t *>(copy + 4) = 0xd0000001u;
            for (unsigned i = 0; i < sizeof(copy); ++i) out.push_back(copy[i]);
            const uint8_t *table = static_cast<const uint8_t *>(n->mRelationships);
            ++ctx.cases;
            if (!table) { ctx.Fail("create made no relationship table"); continue; }
            // The table's two vtable words (the standalone shape, lane DZ): the game's from the original Create, ours from ours
            // (and ours are not the game's); then every byte up to the modified byte (the padding after it is untouched).
            const void *expected[2] = {ctx.image.At<void>(cTSRelationshipTable::kOriginalVTable),
                ctx.image.At<void>(cTSRelationshipTable::kOriginalSerialVTable)};
            if (n == b) {
                const cTSRelationshipTable ours;
                std::memcpy(expected, &ours, sizeof(expected));
                if (expected[0] == ctx.image.At<void>(cTSRelationshipTable::kOriginalVTable)) ctx.Fail("our table carries the game's vtable");
            }
            out.push_back(std::memcmp(table, expected, sizeof(expected)) == 0);
            for (unsigned i = 8; i < 0x19; ++i) out.push_back(table[i]);
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
        // Bit 0 of the flags frees the block: the original's destructor, ours through our vtable's slot 53 and the original again
        // (on a neighbour from the original creator; its adapter was dropped, lane GP) each free the same blocks.
        Nb *c = ORIG(Fn, 0x4b73c0)();
        size_t sizes[3][8] = {};
        size_t counts[3];
        for (int side = 0; side < 3; ++side) {
            Nb *n = side == 0 ? a : side == 1 ? b : c;
            using Del = Nb *(__thiscall *)(Nb *, uint32_t);
            difftest::FreeRecorder rec;
            Nb *r = side == 1 ? reinterpret_cast<Del>(Ours().vt[0][53])(n, 1) : ORIG(Del, 0x4b72e0)(n, 1);
            rec.Stop();
            ++ctx.cases;
            if (!rec.Freed(n) || r != n) ctx.Fail("DeletingDestructor(1) seed %u side %d kept the block", seed, side);
            counts[side] = rec.SortedSizes(sizes[side], 8);
        }
        for (int side = 1; side < 3; ++side)
            if (counts[0] != counts[side] || std::memcmp(sizes[0], sizes[side], sizeof(sizes[0])) != 0)
                ctx.Fail("DeletingDestructor(1) frees differ (side %d)", side);
    }
}

DIFF_TEST(Neighbor_DeletingDestructor, 0x4b72e0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "DeletingDestructor", seed,
            [&](std::mt19937 &rng) {
                Nb *n = N();
                if (rng() % 4 == 0) n->mPersonData = nullptr;
                if (rng() % 4 == 0) n->mRecord = nullptr; else if (!n->mRecord) n->mRecord = &S(kRecordA);
                if (rng() % 6 == 0) n->mRelationships = nullptr;
                // A release that fills a field again, so the member passes of the destructor find it.
                SObj *victims[] = {&S(kPersonOld), &S(kPersonNew), &S(kRecordA), &S(kRel)};
                for (SObj *v : victims) {
                    v->value[6] = rng() % 3 == 0 ? 1 + rng() % 3 : 0;
                    if (v->value[6]) v->custom[2] = RefillOnRelease;
                }
            },
            [&](bool original) {
                using Fn = Nb *(__thiscall *)(Nb *, uint32_t);
                const uint32_t flags = (seed % 3) * 2; // bit 0 (freeing) is covered by Create
                Nb *r = NB_NA(Fn, 0, 0x4b72e0, DeletingDestructor, flags);
                return P(r);
            });
}

DIFF_TEST(Neighbor_ReleaseAndQuery, 0x4b8770)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "Release", seed, [&](std::mt19937 &rng) { N()->mRefCount = rng() % 4 == 0 ? rng() : rng() % 4; },
            [&](bool original) { return NB(uint32_t(__thiscall *)(Nb *), 0, 0x4b8cf0, Release); });
        constexpr uint32_t ids[] = {1, 0xe4fda3d4, 0x3b43bf8a, 0x456b8f1d, 0, 2, 0x456b8f1c, 0x456b8f1e, 0x3b43bf89, 0x3b43bf8b,
            0xffffffff, 0xe4fda3d5, 0xaace2efb};
        const uint32_t id = ids[seed % (sizeof(ids) / 4)];
        Pair(ctx, "QueryInterface", seed, NoSetup, [&](bool original) {
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const uint8_t r = NB_NA(uint8_t(__thiscall *)(Nb *, uint32_t, void **), 0, 0x4b8770, QueryInterface, id, &out);
            return uint32_t(r) | (P(out) << 8);
        });
        Pair(ctx, "GetGZCLSID", seed, NoSetup,
            [&](bool original) { return NB(uint32_t(__thiscall *)(Nb *), 0, 0x4b74d0, GetGZCLSID); });
    }
}

// ---- serialisation ----
DIFF_TEST(Neighbor_Write, 0x4b8ed0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Write", seed,
            [&](std::mt19937 &rng) {
                SObj &s = S(kStream);
                for (unsigned slot : {5u, 6u, 8u, 9u}) Set(s, slot, 1, rng() % 2);
                Set(s, 18, 0, rng() % 2 ? 0 : (rng() % 3 ? 1 : rng() & 0xffffff00));
            },
            [&](bool original) {
                using Fn = uint8_t(__thiscall *)(Nb *, void *);
                return NB(Fn, 0, 0x4b8ed0, Write, static_cast<void *>(&S(kStream)));
            });
}

DIFF_TEST(Neighbor_Read, 0x4b87d0)
{
    Globals globals(ctx);
    static const uint32_t versions[] = {0, 1, 0x1d, 0x1e, 0x1f, 0x3e, 0x3f, 0x40, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0xffffffffu, 0x80000000u};
    static const int32_t layouts[] = {-5, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
        0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
        0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x100, 0x7fffffff, INT32_MIN};
    constexpr size_t kLayoutCount = sizeof(layouts) / sizeof(layouts[0]);
    for (uint32_t seed = 0; seed < kSeeds * 6; ++seed) {
        Pair(ctx, "Read", seed,
            [&](std::mt19937 &rng) {
                const uint32_t version = rng() % 5 == 0 ? rng() : versions[rng() % (sizeof(versions) / 4)];
                const int32_t layout = rng() % 9 == 0 ? int32_t(rng()) : layouts[(seed / 3 + rng() % 3) % kLayoutCount];
                gW.script = {version, uint32_t(layout)};
                if (layout >= 2 && version < 0x6f) gW.script.push_back(rng());
                gW.script.push_back(rng() % 6 == 0 ? 0 : (rng() % 2 ? 0x36 : rng()));
                for (unsigned i = 0; i < 0xe8 + 8; ++i) gW.script.push_back(rng() & 0xffff);
                gW.script.push_back(rng() % 4 ? rng() % 50 : rng());
                for (unsigned i = 0; i < 6; ++i) gW.script.push_back(rng());
                // The id and the data id sit after the attributes; random words follow.
                if (rng() % 3 == 0) gW.script[rng() % gW.script.size()] |= uint64_t(1) << 32; // one failing read
                SObj &s = S(kStream);
                Set(s, 5, 1, 0, StreamGet8);
                Set(s, 6, 1, 0, StreamGet16);
                Set(s, 8, 1, 0, StreamGet32);
                Set(s, 9, 1, 0, StreamGet32);
                Set(s, 15, 1, 1);
                // Often a short script, so the failing/default deliveries after it are covered too.
                if (rng() % 8 == 0) gW.script.resize(rng() % 12 + 2);
            },
            [&](bool original) {
                using Fn = uint8_t(__thiscall *)(Nb *, void *);
                return NB_NA(Fn, 0, 0x4b87d0, Read, static_cast<void *>(&S(kStream)));
            });
    }
}

// ---- person data, the record object ----
DIFF_TEST(Neighbor_Attach, 0x4b8400)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const bool useNew = seed % 3 != 0;
        Pair(ctx, "Attach", seed, NoSetup, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(Nb *, int16_t, void *);
            void *person = useNew ? static_cast<void *>(&S(kPersonNew)) : static_cast<void *>(&S(kPersonOld));
            const int16_t id = int16_t(seed * 977u);
            return NB_NA(Fn, 0, 0x4b8400, Attach, id, person);
        });
    }
}

DIFF_TEST(Neighbor_Detach, 0x4b8e80)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Detach", seed,
            [&](std::mt19937 &rng) {
                Nb *n = N();
                if (rng() % 4 == 0) n->mPersonData = nullptr;
                if (rng() % 4 == 0) n->mRecord = nullptr; else if (!n->mRecord) n->mRecord = &S(kRecordA);
                if (rng() % 4 == 0) n->mRelationships = nullptr;
                // The reset of the relationship table clears the field itself (second check in the original).
                if (rng() % 5 == 0) S(kRel).custom[8] = [](SObj *, uint32_t *) { N()->mRelationships = nullptr; return 0u; };
            },
            [&](bool original) {
                NB(void(__thiscall *)(Nb *), 0, 0x4b8e80, Detach);
                return 0u;
            });
}

DIFF_TEST(Neighbor_GetPersonData, 0x4b8170)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "GetPersonData", seed,
            [&](std::mt19937 &rng) {
                Nb *n = N();
                if (rng() % 2) n->mPersonData = nullptr;
                if (rng() % 4 == 0) n->mPersonDataId = 0;
                else if (rng() % 3 == 0) n->mPersonDataId = 2;
            },
            [&](bool original) {
                using Fn = void *(__thiscall *)(Nb *);
                return P(NB_NA(Fn, 0, 0x4b8170, GetPersonData));
            });
}

DIFF_TEST(Neighbor_GetRecord, 0x4b7950)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed)
        Pair(ctx, "GetRecord", seed,
            [&](std::mt19937 &rng) {
                Nb *n = N();
                if (rng() % 3 != 0) n->mRecord = nullptr;
                if (rng() % 6 == 0) n->mId = 0;
                else if (rng() % 6 == 0) n->mId = int16_t(-int(rng() % 100) - 1);
                else if (rng() % 4 == 0) n->mId = int16_t(0x100 * (rng() % 4));
                if (rng() % 6 == 0) n->mKey[1] = 0;
            },
            [&](bool original) {
                using Fn = void *(__thiscall *)(Nb *, uint8_t);
                const uint8_t create = seed % 4 == 0 ? 0 : seed % 4 == 1 ? 1 : uint8_t(seed | 0x80);
                return P(NB_NA(Fn, 0, 0x4b7950, GetRecord, create));
            });
}

DIFF_TEST(Neighbor_GetScore, 0x4b7fc0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        // The person data object is used unchecked.
        Pair(ctx, "GetScore", seed, [&](std::mt19937 &rng) { N()->mPersonData = rng() % 2 ? &S(kPersonOld) : &S(kPersonNew); }, [&](bool original) {
            using Fn = uint16_t(__thiscall *)(Nb *, uint16_t, uint16_t);
            const uint16_t a = uint16_t(seed * 40503u), b = uint16_t(seed * 7001u + 3);
            return uint32_t(NB_NA(Fn, 0, 0x4b7fc0, GetScore, a, b)) & 0xffff;
        });
}

// ---- attributes ----
DIFF_TEST(Neighbor_Attributes, 0x4b80a0)
{
    Globals globals(ctx);
    constexpr int32_t indexes[] = {0, 1, 0x42, 0x49, 0xb3, 0xbe, 0xe5, 0xe6, 0xe7, 0xe8, 0x100, 0x7fffffff, -1, -2, -21, -22, 0x1f};
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const int32_t index = indexes[seed % (sizeof(indexes) / 4)];
        Pair(ctx, "GetAttribute", seed, NoSetup, [&](bool original) {
            using Fn = uint16_t(__thiscall *)(Nb *, int32_t);
            return uint32_t(NB_NA(Fn, 0, 0x4b80a0, GetAttribute, index)) & 0xffff;
        });
        const uint16_t value = seed % 4 == 0 ? N()->mAttributes[0] : uint16_t(seed * 31);
        Pair(ctx, "SetAttribute", seed, NoSetup, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, int32_t, uint16_t);
            const uint16_t v = seed % 4 == 0 ? N()->mAttributes[index >= 0 && index < 0xe7 ? index : 0] : value;
            if (index < 0 && index != -1) return 0u; // negative indexes are only read (SetAttribute would write the header)
            NB(Fn, 0, 0x4b8e10, SetAttribute, index, v);
            return 0u;
        });
        Pair(ctx, "GetAttributes", seed, NoSetup, [&](bool original) {
            using Fn = uint16_t *(__thiscall *)(Nb *, uint8_t);
            const uint8_t keep = uint8_t(seed % 3 == 0 ? 0 : seed % 3 == 1 ? 1 : 0xfe | (seed & 1));
            return P(NB(Fn, 0, 0x4b80c0, GetAttributes, keep));
        });
    }
}

DIFF_TEST(Neighbor_Predicates, 0x4b8140)
{
    Globals globals(ctx);
    using F32 = uint32_t(__thiscall *)(Nb *);
    using F8 = uint8_t(__thiscall *)(Nb *);
    for (uint32_t seed = 0; seed < kSeeds * 8; ++seed) {
        Pair(ctx, "Predicates", seed, NoSetup, [&](bool original) {
            Blob mixed;
            auto add32 = [&](uint32_t v) { mixed.push_back(v); };
            auto add8 = [&](uint32_t v) { mixed.push_back(v & 0xff); };
            add32(NB_NA(F32, 0, 0x4b8140, GetSpeciesMask));
            add32(NB(F32, 0, 0x4b8130, GetGenderMask));
            add32(NB(F32, 0, 0x4b80d0, GetAgeMask));
            add32(NB_NA(F32, 0, 0x4b81d0, GetLifeStage));
            add8(NB(F8, 0, 0x4b8690, IsHuman));
            add8(NB(F8, 0, 0x4b8490, IsPet));
            add8(NB(F8, 0, 0x4b86a0, IsGender0));
            add8(NB(F8, 0, 0x4b8550, IsGender1));
            add8(NB(F8, 0, 0x4b84a0, IsBaby));
            add8(NB_NA(F8, 0, 0x4b86e0, IsToddler));
            add8(NB(F8, 0, 0x4b84e0, IsChild));
            add8(NB(F8, 0, 0x4b86b0, IsTeen));
            add8(NB(F8, 0, 0x4b8740, IsAdultWithFlag));
            add8(NB(F8, 0, 0x4b8460, IsAdult));
            add8(NB(F8, 0, 0x4b8530, IsElder));
            uint32_t sum = 0;
            for (uint32_t v : mixed) sum = sum * 1000003u + v + 1;
            return sum;
        });
    }
}

DIFF_TEST(Neighbor_PredicateTable, 0x4b81d0)
{
    // Every species / age / gender / flag combination of the interesting values, one result word per function.
    Globals globals(ctx);
    using F32 = uint32_t(__thiscall *)(Nb *);
    using F8 = uint8_t(__thiscall *)(Nb *);
    static const uint16_t species[] = {0, 1, 2, 3, 4, 0xffff, 0x8000};
    static const uint16_t ages[] = {0, 1, 2, 3, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x31, 0x32, 0x33, 0x34, 0x7fff, 0x8000, 0xfff3, 0xffff};
    static const uint16_t genders[] = {0, 1, 2, 0x100, 0xffff};
    static const uint16_t flags[] = {0, 1, 0x8000};
    Ours();
    Build(1);
    Nb *n = N();
    for (int route = 0; route < 2; ++route)
    for (uint16_t sp : species)
        for (uint16_t age : ages)
            for (uint16_t gender : genders)
                for (uint16_t flag : flags) {
                    gRoute = Route(route);
                    n->mAttributes[Nb::kSpeciesAttribute] = sp;
                    n->mAttributes[Nb::kAgeAttribute] = age;
                    n->mAttributes[Nb::kGenderAttribute] = gender;
                    n->mAttributes[Nb::kFlagAttribute] = flag;
                    uint32_t o32[4], r32[4];
                    uint8_t o8[11], r8[11];
                    for (int side = 0; side < 2; ++side) {
                        const bool original = side == 0;
                        uint32_t *w = original ? o32 : r32;
                        uint8_t *b = original ? o8 : r8;
                        w[0] = NB_NA(F32, 0, 0x4b8140, GetSpeciesMask);
                        w[1] = NB(F32, 0, 0x4b8130, GetGenderMask);
                        w[2] = NB(F32, 0, 0x4b80d0, GetAgeMask);
                        w[3] = NB_NA(F32, 0, 0x4b81d0, GetLifeStage);
                        b[0] = NB(F8, 0, 0x4b8690, IsHuman);
                        b[1] = NB(F8, 0, 0x4b8490, IsPet);
                        b[2] = NB(F8, 0, 0x4b86a0, IsGender0);
                        b[3] = NB(F8, 0, 0x4b8550, IsGender1);
                        b[4] = NB(F8, 0, 0x4b84a0, IsBaby);
                        b[5] = NB_NA(F8, 0, 0x4b86e0, IsToddler);
                        b[6] = NB(F8, 0, 0x4b84e0, IsChild);
                        b[7] = NB(F8, 0, 0x4b86b0, IsTeen);
                        b[8] = NB(F8, 0, 0x4b8740, IsAdultWithFlag);
                        b[9] = NB(F8, 0, 0x4b8460, IsAdult);
                        b[10] = NB(F8, 0, 0x4b8530, IsElder);
                    }
                    ++ctx.cases;
                    if (std::memcmp(o32, r32, sizeof(o32)) != 0)
                        ctx.Fail("masks species %x age %x gender %x flag %x: original %x/%x/%x/%x, replacement %x/%x/%x/%x", sp, age, gender,
                            flag, o32[0], o32[1], o32[2], o32[3], r32[0], r32[1], r32[2], r32[3]);
                    if (std::memcmp(o8, r8, sizeof(o8)) != 0) ctx.Fail("predicates species %x age %x gender %x flag %x", sp, age, gender, flag);
                }
}

DIFF_TEST(Neighbor_PersonDataQueries, 0x4b8700)
{
    Globals globals(ctx);
    using F8 = uint8_t(__thiscall *)(Nb *);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "HasKindInfo11", seed, [&](std::mt19937 &rng) { if (rng() % 5 == 0) N()->mPersonData = nullptr; },
            [&](bool original) { return uint32_t(NB_NA(F8, 0, 0x4b8700, HasKindInfo11)) & 0xff; });
        Pair(ctx, "IsPersonDataFlagged", seed, [&](std::mt19937 &rng) { if (rng() % 5 == 0) N()->mPersonData = nullptr; },
            [&](bool original) { return uint32_t(NB(F8, 0, 0x4b84c0, IsPersonDataFlagged)) & 0xff; });
        Pair(ctx, "IsModified", seed, NoSetup,
            [&](bool original) { return uint32_t(NB_NA(F8, 0, 0x4b8510, IsModified)) & 0xff; });
        const uint32_t flag = seed % 3 == 0 ? 0 : seed % 3 == 1 ? 1 : seed * 0x01010101u;
        Pair(ctx, "SetModified", seed, NoSetup, [&](bool original) {
            using Fn = uint32_t(__thiscall *)(Nb *, uint32_t);
            return NB_NA(Fn, 0, 0x4b8d50, SetModified, flag);
        });
        const int32_t which = seed % 5 == 0 ? 0 : seed % 5 == 1 ? 1 : seed % 5 == 2 ? 2 : seed % 5 == 3 ? 3 : -1;
        Pair(ctx, "GetExtraWord", seed, NoSetup, [&](bool original) {
            using Fn = uint32_t(__thiscall *)(Nb *, int32_t);
            return NB(Fn, 0, 0x4b7f90, GetExtraWord, which);
        });
        Pair(ctx, "SetExtraWord", seed, NoSetup, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, int32_t, uint32_t);
            NB(Fn, 0, 0x4b8de0, SetExtraWord, which, seed * 77u);
            return 0u;
        });
    }
}

// ---- the relationship level ----
DIFF_TEST(Neighbor_RelationshipLevel, 0x4b74e0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 30; ++seed)
        Pair(ctx, "GetRelationshipLevel", seed,
            [&](std::mt19937 &rng) {
                if (rng() % 3 == 0) S(kRel).value[0] |= 0x8000;
                if (rng() % 4 == 0) N()->mPersonData = nullptr;
                if (rng() % 5 == 0) N()->mPersonData = &S(kPersonNew);
                if (rng() % 3 == 0) S(kPersonOld).value[2] = 1;
                if (rng() % 4 == 0) N()->mAttributes[0xba] = S(kOther).attrs[0xba];
                if (rng() % 3 == 0) N()->mPersonDataId = S(kPeople).value[0];
                // Put one of the six thresholds exactly on the averaged value (the edges of the level ladder), with no
                // low-relationship adjustment.
                if (rng() % 2 == 0) {
                    // The ladder is 5, 4, 3 (level <= t), 0, 1 (level < t), 2 (level >= t): the thresholds before the chosen
                    // step let the level through, the chosen one sits exactly on the level, the later ones are near it.
                    SObj &t = S(kTuning);
                    const int avg = (int16_t(S(kRel).value[1]) + int16_t(S(kOtherRel).value[1])) / 2;
                    constexpr unsigned order[6] = {5, 4, 3, 0, 1, 2};
                    const unsigned target = rng() % 6;
                    for (unsigned q = 0; q < 6; ++q) {
                        int d;
                        if (q < target) d = order[q] >= 3 ? -1 : -int(rng() % 2);
                        else if (q == target) d = 0;
                        else d = int(rng() % 5) - 2;
                        t.attrs[order[q]] = uint16_t(avg + d);
                    }
                    t.attrs[0x2d] = 0;
                }
                // The later checks all pass: the same species, adult ages, positive values.
                if (rng() % 2 == 0) {
                    S(kRel).value[2] = 0;
                    N()->mAttributes[Nb::kSpeciesAttribute] = S(kOther).attrs[Nb::kSpeciesAttribute];
                    for (uint16_t *a : {N()->mAttributes, S(kPersonA).attrs, S(kPersonB).attrs, S(kOther).attrs}) {
                        a[0x3a] = uint16_t(20 + rng() % 10);
                        a[0x16] = uint16_t(1 + rng() % 3);
                        a[0x17] = uint16_t(1 + rng() % 3);
                    }
                }
            },
            [&](bool original) {
                using Fn = int32_t(__thiscall *)(Nb *, void *, int32_t);
                void *other = seed % 40 == 0 ? nullptr : static_cast<void *>(&S(kOther));
                const int32_t kind = seed % 7 == 0 ? 0 : seed % 7 == 6 ? 2 + int32_t(seed % 3) : 1;
                return uint32_t(NB_NA(Fn, 0, 0x4b74e0, GetRelationshipLevel, other, kind));
            });
}

DIFF_TEST(Neighbor_ValueRatio, 0x4b8560)
{
    Globals globals(ctx);
    using Fn = uint8_t(__thiscall *)(Nb *, uint16_t, float *);
    for (uint32_t seed = 0; seed < kSeeds * 6; ++seed) {
        const uint16_t key = uint16_t(seed % 5 ? seed % 6 : seed * 91);
        const bool useOut = seed % 9 != 0;
        for (int variant = 0; variant < 2; ++variant)
            Pair(ctx, variant ? "GetValueRatioB" : "GetValueRatioA", seed, NoSetup, [&](bool original) {
                float out = -1234.5f;
                float *dest = useOut ? &out : nullptr;
                const uint32_t r = variant == 0 ? NB_NA(Fn, 0, 0x4b8560, GetValueRatioA, key, dest)
                                                : NB_NA(Fn, 0, 0x4b82d0, GetValueRatioB, key, dest);
                uint32_t bits;
                std::memcpy(&bits, &out, 4);
                return (r & 0xff) ^ (bits * 31u);
            });
    }
}

// The relationship query (0x55cce0) is stood in for on both sides: it records the holder it gets.
namespace {
void __fastcall QueryStandIn(void *holder, void *, uint32_t a, uint32_t b, uint32_t c)
{
    const uint32_t *words = static_cast<const uint32_t *>(holder);
    Ev({0x77, words[0], words[1], P(reinterpret_cast<void *>(words[2])), a, b, c});
}
} // namespace

DIFF_TEST(Neighbor_RunRelationshipQuery, 0x4b7f30)
{
    Globals globals(ctx);
    struct Patch {
        uint8_t *site;
        uint8_t saved[5];
        Patch(const difftest::Context &c, uint32_t rva, void *stand) : site(static_cast<uint8_t *>(const_cast<void *>(static_cast<const void *>(c.image.At(rva)))))
        {
            std::memcpy(saved, site, 5);
            DWORD old;
            VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
            site[0] = 0xE9;
            std::memcpy(site + 1, &rel, 4);
        }
        ~Patch() { std::memcpy(site, saved, 5); }
    } patch(ctx, 0x55cce0, reinterpret_cast<void *>(&QueryStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "RunRelationshipQuery", seed, NoSetup, [&](bool original) {
            using Fn = void(__thiscall *)(Nb *, uint32_t, uint32_t, uint32_t);
            NB_NA(Fn, 0, 0x4b7f30, RunRelationshipQuery, seed, seed * 3, seed * 5 + 1);
            return 0u;
        });
}

// ---- the persist face ----
DIFF_TEST(Neighbor_PersistFace, 0x4b7f10)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "GetKey", seed, NoSetup, [&](bool original) {
            uint32_t out[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
            using Fn = uint32_t *(__thiscall *)(void *, uint32_t *);
            const uint32_t *r = NB(Fn, 1, 0x4b7f10, GetKey, out);
            return (out[0] ^ (out[1] * 3) ^ (out[2] * 5) ^ (out[3] * 7)) + (r == out ? 1u : 0u);
        });
        // The new key: random, or a pointer into the record so that the copy changes the source.
        const unsigned mode = seed % 5;
        Pair(ctx, "SetKey", seed, NoSetup, [&](bool original) {
            uint32_t keyBuffer[4] = {seed * 3u, seed * 5u, seed * 7u, mode == 3 ? 0u : seed};
            uint32_t *key = keyBuffer;
            if (mode == 1) key = N()->mKey;                                           // the key itself
            else if (mode == 2) key = reinterpret_cast<uint32_t *>(N()->mAttributes); // somewhere else in the record
            else if (mode == 3) key = reinterpret_cast<uint32_t *>(&N()->mKey[0]) - 1; // one word before, overlapping
            else if (mode == 4) key = reinterpret_cast<uint32_t *>(&N()->mKey[0]) - 3; // only the last word lands on the key
            using Fn = uint8_t(__thiscall *)(void *, const uint32_t *);
            return uint32_t(NB(Fn, 1, 0x4b8d90, SetKey, static_cast<const uint32_t *>(key))) & 0xff;
        });
        Pair(ctx, "GetTypeGroupInstance", seed, NoSetup, [&](bool original) {
            using Fn = uint32_t(__thiscall *)(void *);
            const uint32_t type = NB(Fn, 1, 0x4b82c0, GetType);
            const uint32_t group = NB(Fn, 1, 0x4b7ef0, GetGroup);
            const uint32_t instance = NB(Fn, 1, 0x4b7f00, GetInstance);
            return type ^ (group * 3) ^ (instance * 5);
        });
        Pair(ctx, "SetTypeGroupInstance", seed, NoSetup, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(void *, uint32_t);
            const uint32_t a = NB(Fn, 1, 0x4b8e70, SetType, seed + 1) & 0xff;
            const uint32_t b = NB(Fn, 1, 0x4b8d70, SetGroup, seed * 3) & 0xff;
            const uint32_t c = NB(Fn, 1, 0x4b8d80, SetInstance, seed * 5) & 0xff;
            return a | (b << 8) | (c << 16);
        });
    }
}

// ---- the slots no adapter covers: folded bodies and the persist face's thunks (QueryInterface 0x4b87c2, the folded AddRef
// 0x1448e9, Release 0x4b8d0a), through each side's own vtable ----
DIFF_TEST(Neighbor_Slots, 0x4b87c2)
{
    Globals globals(ctx);
    using F0 = uint32_t(__thiscall *)(void *);
    using F1 = uint32_t(__thiscall *)(void *, uint32_t);
    using F2 = uint32_t(__thiscall *)(void *, uint32_t, void **);
    struct Slot {
        const char *name;
        unsigned face, slot, args, mask;
        bool pointer; // the result is a pointer (normalised)
    };
    static const Slot slots[] = {
        {"AddRef", 0, 1, 0, ~0u, false},         {"GetId", 0, 8, 0, 0xffff, false},
        {"GetId2", 0, 9, 0, 0xffff, false},       {"SetPersonData", 0, 11, 1, 0, false},
        {"GetRelationshipTable", 0, 12, 0, ~0u, true}, {"SetRecord", 0, 13, 1, 0, false},
        {"GetPersonDataId", 0, 15, 0, ~0u, false}, {"GetFlag20", 0, 36, 0, 0xff, false},
        {"SetFlag20", 0, 37, 1, 0, false},         {"GetRelatedCount", 0, 41, 0, ~0u, false},
        {"SetRelatedCount", 0, 42, 1, 0, false},   {"GetLayoutVersion", 0, 43, 0, ~0u, false},
        {"AsPersistResource", 0, 48, 0, ~0u, true}, {"AnswersNo", 0, 49, 1, 0xff, false},
        {"QueryInterface+4", 1, 0, 2, 0xff, false}, {"AddRef+4", 1, 1, 0, ~0u, false},
        {"Release+4", 1, 2, 0, ~0u, false},        {"GetRefCount+4", 1, 11, 0, ~0u, false},
    };
    constexpr uint32_t ids[] = {1, 0xe4fda3d4, 0x3b43bf8a, 0x456b8f1d, 0, 0x456b8f1c, 0xaace2efb};
    for (const Slot &s : slots)
        for (uint32_t seed = 0; seed < 40; ++seed)
            Pair(ctx, s.name, seed,
                [&](std::mt19937 &rng) {
                    if (s.slot == 2) N()->mRefCount = rng() % 3; // Release+4: also the destroying edge (slot 53, a fake)
                },
                [&](bool original) {
                    // The argument: an object (the fakes, the current one, none) for the counted stores, a word otherwise.
                    void *objects[] = {&S(kPersonNew), &S(kPersonOld), &S(kRecordA), &S(kRecordB), nullptr,
                        s.slot == 11 ? N()->mPersonData : N()->mRecord};
                    const uint32_t word = s.slot == 37 ? (seed % 3 == 0 ? 0x80u : seed * 0x01010101u) : seed * 2654435761u;
                    const uint32_t arg = s.slot == 11 || s.slot == 13 ? uint32_t(uintptr_t(objects[seed % 6])) : word;
                    uint32_t r;
                    if (s.args == 2) {
                        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
                        r = CallSlot<F2>(original, s.face, s.slot, ids[seed % 7], &out) & 0xff;
                        r |= P(out) << 8;
                        return r;
                    }
                    r = s.args == 1 ? CallSlot<F1>(original, s.face, s.slot, arg) : CallSlot<F0>(original, s.face, s.slot);
                    if (s.pointer) return P(reinterpret_cast<void *>(r));
                    return r & s.mask;
                });
}
