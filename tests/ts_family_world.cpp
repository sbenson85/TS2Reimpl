// The fake world of the cTSFamily difftests (see ts_family.h).
#include "ts_family.h"

namespace tsfam {
namespace {
World gWorld;
bool gRecording = true;
bool gBuilt = false;
void *gFamilyVT[80];
void *gObjVT[32];
void *gExtraFamilySlots[80]; // test-specific family slots (SetFamilySlot), applied over the defaults by InstallFakes

struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isNil;
};
} // namespace

World &W() { return gWorld; }
Family *F() { return reinterpret_cast<Family *>(gWorld.work.bytes); }

// ---- the standalone shape: routes, vtable copies, traps (see ts_family.h) ----

Route gRoute = kVTableRoute;
bool gOurSide = false;
int gTrapHits = 0;

namespace {
void *gPersistVT[12]; // the persist face's copy (gFamilyVT is the +0 face's)

// The trap counts and forwards to shape::gForwardTo (one slot under test at a time).
void *TrapFor(unsigned words) { return shape::ForwardTrapFor<shape::Count<gTrapHits>>(words); }
} // namespace

void **GameVTable(unsigned face) { return shape::GameVTable(kGameVTable[face]); }
void **OurVTable(unsigned face) { return shape::Ours<Family>(kFaceOffset).face[face]; }
void **CopyVTable(unsigned face) { return face == 0 ? gFamilyVT : gPersistVT; }

uint32_t VtCode(uint32_t word)
{
    void **copies[kFaces] = {CopyVTable(0), CopyVTable(1)};
    return shape::VtCode(word, kFaces, shape::GameTables{kGameVTable}, shape::Ours<Family>(kFaceOffset).face, copies);
}

unsigned SlotOf(unsigned face, uint32_t rva) { return shape::SlotOf(kGameVTable[face], kSlotCount[face], rva); }

void InstallTrap(uint32_t rva, unsigned words)
{
    const void *target = GameAddress<void>(rva);
    for (unsigned face = 0; face < kFaces; ++face)
        for (unsigned s = 0; s < kSlotCount[face]; ++s)
            if (GameVTable(face)[s] == target) {
                void *&entry = CopyVTable(face)[s];
                if (entry == TrapFor(words)) continue; // already trapped (a test calling the same function again)
                shape::gForwardTo = entry;             // the side's own function there, or the world's fake for it
                entry = TrapFor(words);
            }
}

void Reconstruct()
{
    if (gOurSide && gRoute == kVTableRoute) ::new (gWorld.work.bytes) Family;
    else reinterpret_cast<void(__thiscall *)(void *)>(GameAddress<void>(kConstructRva))(gWorld.work.bytes);
}

bool FacesWhereExpected()
{
    Family *f = F();
    auto at = [&](const void *face) { return uint32_t(static_cast<const uint8_t *>(face) - reinterpret_cast<uint8_t *>(f)); };
    return at(static_cast<cITSFamily *>(f)) == 0 && at(static_cast<cIGZSerializable *>(f)) == 0 &&
           at(static_cast<cIGZPersistResource *>(f)) == kFaceOffset[1];
}

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (unsigned i = 0; i < kObjects; ++i)
        if (p == &gWorld.objs[i]) return 100 + i;
    if (p == gWorld.work.bytes) return 2;
    return 1;
}

void Ev(std::initializer_list<uint32_t> items)
{
    if (!gRecording) return;
    for (uint32_t v : items) gWorld.events.push_back(v);
}

namespace {
uint8_t __fastcall ObjQI(Obj *self, void *, uint32_t id, void **out)
{
    Ev({evQI, self->id, id});
    if (!self->answer) return 0;
    *out = self;
    ++self->refs;
    return 1;
}
uint32_t __fastcall ObjAddRef(Obj *self, void *)
{
    Ev({evAddRef, self->id});
    return uint32_t(++self->refs);
}
uint32_t __fastcall ObjRelease(Obj *self, void *)
{
    Ev({evRelease, self->id});
    return uint32_t(--self->refs);
}
const FamilyKey *__fastcall ObjGetKey(Obj *self, void *)
{
    Ev({evGetKey, self->id});
    return &self->key;
}

uint32_t __fastcall FamAddRef(Family *self, void *)
{
    Ev({evFamAddRef, self->mRefCount});
    return ++self->mRefCount;
}
void *__fastcall FamDelete(Family *self, void *, uint32_t flags)
{
    Ev({evFamDelete, flags});
    return self;
}
uint8_t __fastcall FamLoad1(Family *self, void *)
{
    Ev({evLoad1});
    self->mGroup1Loaded = 1;
    return 1;
}
uint8_t __fastcall FamLoad2(Family *self, void *)
{
    Ev({evLoad2});
    self->mGroup2Loaded = 1;
    return 1;
}
uint32_t __fastcall FamClassId(Family *, void *)
{
    Ev({evService, 5});
    return W().config[0];
}
int32_t __fastcall FamGetFunds(Family *, void *)
{
    Ev({evService, 53});
    return int32_t(W().config[1]);
}
void __fastcall FamSetBit0(Family *, void *, uint32_t arg) { Ev({evService, 66, arg}); }
void __fastcall FamSetBit1(Family *, void *, uint32_t arg) { Ev({evService, 70, arg}); }
} // namespace

void InstallFakes(Family *f)
{
    // The side's own vtables, copied (ours on the vtable route, the game's otherwise), with the fakes patched over the copy.
    const bool ours = gOurSide && gRoute == kVTableRoute;
    for (unsigned face = 0; face < kFaces; ++face) {
        void **base = ours ? OurVTable(face) : GameVTable(face);
        for (unsigned s = 0; s < kSlotCount[face]; ++s) CopyVTable(face)[s] = base[s];
    }
    gFamilyVT[5] = reinterpret_cast<void *>(&FamClassId);
    gFamilyVT[53] = reinterpret_cast<void *>(&FamGetFunds);
    gFamilyVT[66] = reinterpret_cast<void *>(&FamSetBit0);
    gFamilyVT[70] = reinterpret_cast<void *>(&FamSetBit1);
    gFamilyVT[1] = reinterpret_cast<void *>(&FamAddRef);
    gFamilyVT[33] = reinterpret_cast<void *>(&FamLoad1);
    gFamilyVT[77] = reinterpret_cast<void *>(&FamDelete);
    gFamilyVT[78] = reinterpret_cast<void *>(&FamLoad2);
    for (unsigned i = 0; i < 80; ++i)
        if (gExtraFamilySlots[i]) gFamilyVT[i] = gExtraFamilySlots[i];
    *reinterpret_cast<void ***>(f) = gFamilyVT;
    *reinterpret_cast<void ***>(reinterpret_cast<uint8_t *>(f) + kFaceOffset[1]) = gPersistVT;
}

void SetFamilySlot(unsigned slot, void *fn) { gExtraFamilySlots[slot] = fn; }
void ClearFamilySlots()
{
    for (auto &slot : gExtraFamilySlots) slot = nullptr;
}

namespace {
void InstallObjectFakes()
{
    for (auto &slot : gObjVT) slot = nullptr;
    gObjVT[0] = reinterpret_cast<void *>(&ObjQI);
    gObjVT[1] = reinterpret_cast<void *>(&ObjAddRef);
    gObjVT[2] = reinterpret_cast<void *>(&ObjRelease);
    gObjVT[0x54 / 4] = reinterpret_cast<void *>(&ObjGetKey);
}

std::string RandomText(std::mt19937 &rng)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    std::string text;
    const unsigned length = rng() % 3 == 0 ? 0 : rng() % 40;
    for (unsigned i = 0; i < length; ++i) text.push_back(letters[rng() % 26]);
    return text;
}
FamilyKey RandomKey(std::mt19937 &rng)
{
    // A small range so the keys collide and order by every word.
    return {0x10u + rng() % 2, 0x20u + rng() % 2, 0x30u + rng() % 3, 0x40u + rng() % 2};
}
} // namespace

void Build(World &w, uint32_t seed, uint32_t fill)
{
    gRecording = false;
    if (gBuilt) {
        F()->Family::~Family(); // (layouts alike: also for a family the original constructor built)
    }
    gBuilt = true;
    InstallObjectFakes();
    std::mt19937 rng(seed * 2654435761u + 17);
    w.seed = seed;
    w.events.clear();
    for (auto &byte : w.work.before) byte = uint8_t(rng());
    for (auto &byte : w.work.bytes) byte = uint8_t(rng());
    for (auto &byte : w.work.after) byte = uint8_t(rng());
    for (unsigned i = 0; i < kObjects; ++i) {
        Obj &o = w.objs[i];
        o.vt = gObjVT;
        o.id = 100 + i;
        o.refs = 1;
        o.key = RandomKey(rng);
        o.answer = rng() % 4 != 0;
        for (auto &v : o.out) v = rng();
    }
    for (auto &c : w.config) c = rng();
    Family *f = F();
    Reconstruct();
    InstallFakes(f);
    f->mRefCount = rng() % 4;
    f->mField0C = rng();
    f->mIdCount = rng();
    f->mFamilyId = rng() % 3 ? rng() % 32001 : rng();
    f->mHomeLot = rng() % 3 ? rng() % 5 : rng();
    f->mVacationLot = rng() % 3 ? rng() % 5 : rng();
    f->mBusinessLot = rng();
    f->mUniqueId = rng() % 2 ? 0 : rng();
    f->mField68 = rng();
    f->mFunds = int32_t(rng());
    f->mField70 = rng();
    f->mModified = uint8_t(rng() % 2);
    f->mGroup1Dirty = uint8_t(rng() % 2);
    f->mGroup1Loaded = uint8_t(rng() % 2);
    f->mGroup2Dirty = uint8_t(rng() % 2);
    f->mGroup2Loaded = uint8_t(rng() % 2);
    f->mFlag79 = uint8_t(rng());
    f->mFlags = rng();
    for (auto &v : f->mRecordKey) v = rng();
    if (fill & 1) {
        const unsigned n = rng() % 7;
        for (unsigned i = 0; i < n; ++i) f->mMemberIds.push_back(rng() % 5 ? 0x100 + rng() % 6 : rng());
    }
    if (fill & 2) {
        const unsigned n = rng() % 6;
        for (unsigned i = 0; i < n; ++i) f->mIdSet.insert(rng() % 8);
    }
    if (fill & 4) {
        FamilyObjectMap *maps[4] = {&f->mGroup1First, &f->mGroup1Second, &f->mGroup2First, &f->mGroup2Second};
        for (auto *map : maps) {
            const unsigned n = rng() % 5;
            for (unsigned i = 0; i < n; ++i) {
                Obj &o = w.objs[rng() % kObjects];
                if (rng() % 4 == 0) (*map)[RandomKey(rng)].Assign(nullptr);
                else (*map)[RandomKey(rng)].Assign(&o);
            }
        }
        if (rng() % 3 == 0) f->mRecord = &w.objs[rng() % kObjects], ++static_cast<Obj *>(f->mRecord)->refs;
    }
    if (fill & 8) {
        f->mName.mString = RandomText(rng);
        f->mDescription.mString = RandomText(rng);
    }
    // Build's own references are not part of the experiment.
    for (auto &o : w.objs) o.refs = 1000;
    gRecording = true;
}

namespace {
void DumpMap(Blob &b, const FamilyObjectMap &map)
{
    b.push_back(uint32_t(map.size()));
    for (const auto &entry : map) {
        b.push_back(entry.first.mType);
        b.push_back(entry.first.mGroup);
        b.push_back(entry.first.mInstance);
        b.push_back(entry.first.mInstanceHigh);
        b.push_back(Id(entry.second.Get()));
    }
    // The tree shape (colours of an in-order walk with depths) must match as well.
    const RawNode *head = *reinterpret_cast<RawNode *const *>(&map);
    struct Walk {
        static void Go(Blob &b, const RawNode *n, uint32_t depth)
        {
            if (n->isNil) return;
            Go(b, n->left, depth + 1);
            b.push_back(depth * 4 + n->color);
            Go(b, n->right, depth + 1);
        }
    };
    Walk::Go(b, head->parent, 0);
}
} // namespace

void Capture(Blob &b, bool zeroTextBuffers)
{
    World &w = W();
    auto *f = F();
    uint32_t words[0xc8 / 4];
    std::memcpy(words, w.work.bytes, sizeof(words));
    const bool destroyed = zeroTextBuffers;
    // The vtable words, the only bytes that differ by design: as codes per face.
    words[0] = VtCode(words[0]);
    words[kFaceOffset[1] / 4] = VtCode(words[kFaceOffset[1] / 4]);
    // Pointers into the heap differ between the two worlds: they are replaced by what they stand for below.
    for (unsigned i = 0x7c / 4; i < 0x88 / 4; ++i) words[i] = 0;
    words[0x9c / 4] = 0;
    words[0xa4 / 4] = Id(f->mRecord);
    for (unsigned off : {0xa8u, 0xb0u, 0xb8u, 0xc0u}) words[off / 4] = 0;
    for (unsigned base : {0x14u, 0x34u}) {
        if (destroyed || words[(base + 0x18) / 4] > 15)
            for (unsigned i = (base + 4) / 4; i < (base + 0x14) / 4; ++i) words[i] = 0;
    }
    if (destroyed) {
        // A destroyed family keeps stale container pointers: only the plain fields are compared.
        for (unsigned off : {0xa0u, 0xacu, 0xb4u, 0xbcu, 0xc4u}) words[off / 4] = 0;
    }
    b.insert(b.end(), words, words + 0xc8 / 4);
    for (unsigned i = 0; i < 4; ++i)
        b.push_back(reinterpret_cast<uint32_t *>(w.work.before)[i]), b.push_back(reinterpret_cast<uint32_t *>(w.work.after)[i]);
    if (!destroyed) {
        b.push_back(uint32_t(f->mMemberIds.size()));
        b.push_back(uint32_t(f->mMemberIds.capacity()));
        for (uint32_t id : f->mMemberIds) b.push_back(id);
        b.push_back(uint32_t(f->mIdSet.size()));
        for (uint32_t id : f->mIdSet) b.push_back(id);
        DumpMap(b, f->mGroup1First);
        DumpMap(b, f->mGroup1Second);
        DumpMap(b, f->mGroup2First);
        DumpMap(b, f->mGroup2Second);
        for (const std::string *s : {&f->mName.mString, &f->mDescription.mString}) {
            b.push_back(uint32_t(s->size()));
            b.push_back(uint32_t(s->capacity()));
            for (char c : *s) b.push_back(uint8_t(c));
        }
    } else {
        for (const std::string *s : {&f->mName.mString, &f->mDescription.mString}) {
            b.push_back(uint32_t(s->size()));
            b.push_back(uint32_t(s->capacity()));
            b.push_back(uint8_t(s->c_str()[0]));
        }
    }
    for (const Obj &o : w.objs) b.push_back(uint32_t(o.refs));
    b.push_back(0xe7e7e7e7);
    b.insert(b.end(), w.events.begin(), w.events.end());
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return true;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    ctx.Fail("%s seed %u: differs at word %zu (original %08x, ours %08x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0u, at < b.size() ? b[at] : 0u, a.size(), b.size());
    return false;
}
} // namespace tsfam
