// Differential tests for cTSNeighborhoodInfo (src/ts/cTSNeighborhoodInfo.h): construction and destruction, the interface
// slots, Read/Write against scripted fake streams, the accessors and predicates, the describe face and the four members
// that reach the persist system, the framework and the string table resources.
//
// A world is one record in a guarded buffer whose main vtable and persist-face vtable are tables of recording fakes (the slots
// its own members call through `this`), plus generic recording fakes for everything it reaches: the persist system (cache
// 0x10faf6c) with its record table, string table resources, the framework behind the director (0x10c9390), the slot-23
// service (cache 0x10cc2a8) and its record manager, the second language service (cache 0x10cc2dc), streams. Every call a
// fake receives is appended to `events` (pointers to fakes become ids, pointers into the record their offset, stack addresses a
// marker; keys and texts are logged by content). Two worlds built from one seed start the same; one runs the original, the
// other ours, and the results, the record's canonical bytes, the fakes' reference counts, the event logs and the sizes of the
// blocks freed are compared. Fakes that give back 8-bit answers fill the rest of eax with junk, as the original callees may.
//
// Standalone shape (docs/standalone-shape.md, lane EA): the original side's record is built by the original constructor 0x4a6350
// (game vtables) and the code is called at its RVA; ours is built by our constructor (the constructor hook's adapter) and called
// through our own vtable slot, as the game's callers reach it. The world's five vtable words point to copies of the side's own
// vtables (the recording slots patched in); the vtable words are the only bytes normalised (VtWord): the copy, the side's own
// vtable of that face, or (+0x10, after destruction) the side's cRZUnknown vtable. NeighborhoodInfo_Slots covers the slots no
// other test calls (folded bodies, the faces' AddRef/Release thunks, cRZUnknown's own slots), NeighborhoodInfo_Adapters the
// RVA hooks' entry points on a record built by the original constructor, with a trap in the slot each adapter forwards to.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSNeighborhoodInfo.h"

#include <windows.h>

#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {
using Info = cTSNeighborhoodInfo;
using Blob = std::vector<uint32_t>;
constexpr unsigned kSlots = 40;

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);
struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
};

enum Fake : unsigned {
    kPersist, kRecords, kResource, kSerial, kFramework, kManager, kSlot23, kLanguage, kStream, kTarget, kRecordNew, kRecordOld,
    kFakeCount
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Info)];
    uint8_t after[16];
};

struct World {
    Guarded work;
    SObj fakes[kFakeCount];
    Blob events;
    uint32_t junk = 0;           // the upper bits of eax in 8-bit answers
    uint32_t objAnswer = 0;      // what the record's own slot 14 answers
    std::vector<uint64_t> script; // stream reads: value, bit 32 = the read fails (writes nothing)
    size_t scriptPos = 0;
};
World gW;
Info *N() { return reinterpret_cast<Info *>(gW.work.bytes); }
SObj &S(Fake f) { return gW.fakes[f]; }

enum : uint32_t { evCall = 0x60, evObj = 0x61, evRead = 0x62, evKey = 0x63, evText = 0x64, evWrite = 0x65, evFace = 0x66 };
void Ev(std::initializer_list<uint32_t> items) { for (uint32_t v : items) gW.events.push_back(v); }

uint32_t Norm(uint32_t value)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (value == uint32_t(uintptr_t(&gW.fakes[i]))) return 0x1000 + i;
    const uint32_t base = uint32_t(uintptr_t(gW.work.bytes));
    if (value >= base && value < base + sizeof(Info)) return 0x2000 + (value - base);
    uint32_t local = 0;
    const uint32_t sp = uint32_t(uintptr_t(&local));
    if (value - sp < 0x10000 || sp - value < 0x10000) return 0x57ac;
    return value;
}
uint32_t P(const void *p) { return Norm(uint32_t(uintptr_t(p))); }

uint32_t Hash(const std::string &s)
{
    uint32_t h = 0x811c9dc5;
    for (unsigned char c : s) h = (h ^ c) * 0x1000193;
    return h ^ uint32_t(s.size());
}
void LogKey(const uint32_t *key) { Ev({evKey, key[0], key[1], key[2], key[3]}); }
void LogString(const cRZString *s) { Ev({evText, Hash(s->mString), uint32_t(s->mString.size())}); }

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
    for (unsigned i = 0; i < kSlots; ++i) o.ret[i] = 0, o.custom[i] = nullptr, o.table[i] = nullptr;
    Set(o, 1, 0);
    Set(o, 2, 0);
}

uint32_t Junk8(uint32_t low8) { return (low8 & 0xff) | (gW.junk << 8); }

std::string MakeText(uint32_t seed)
{
    std::string s;
    const unsigned len = (seed >> 3) % 40;
    for (unsigned i = 0; i < len; ++i) s.push_back(char('a' + (seed + i * 7) % 26));
    return s;
}

// ---- custom handlers ----
uint32_t StreamGet(SObj *, uint32_t *args, unsigned width)
{
    uint64_t item = gW.scriptPos < gW.script.size() ? gW.script[gW.scriptPos] : uint64_t(0x01010101u) * (gW.scriptPos & 0x7f);
    ++gW.scriptPos;
    Ev({evRead, width, uint32_t(item), uint32_t(item >> 32)});
    if (item >> 32) return 0; // a failing read writes nothing
    if (width == 1) *reinterpret_cast<uint8_t *>(args[0]) = uint8_t(item);
    else *reinterpret_cast<uint32_t *>(args[0]) = uint32_t(item);
    return 1;
}
uint32_t StreamByte(SObj *s, uint32_t *a)
{
    if (!s->value[1]) return StreamGet(s, a, 1);
    Ev({evWrite, 1, a[0]});
    return 1;
}
uint32_t StreamWord(SObj *s, uint32_t *a)
{
    if (!s->value[1]) return StreamGet(s, a, 4);
    Ev({evWrite, 4, a[0]});
    return 1;
}
uint32_t StreamText(SObj *s, uint32_t *a)
{
    auto *text = reinterpret_cast<cRZString *>(a[0]);
    if (s->value[1]) {
        LogString(text);
        return 1;
    }
    uint64_t item = gW.scriptPos < gW.script.size() ? gW.script[gW.scriptPos] : uint64_t(0x01010101u) * (gW.scriptPos & 0x7f);
    ++gW.scriptPos;
    Ev({evRead, 15, uint32_t(item), uint32_t(item >> 32)});
    if (item >> 32) return 0;
    text->mString = MakeText(uint32_t(item));
    return 1;
}
uint32_t StreamError(SObj *self, uint32_t *) { return Junk8(self->value[0]); }

uint32_t PersistGetResource(SObj *self, uint32_t *args)
{
    LogKey(reinterpret_cast<const uint32_t *>(args[0]));
    // The callers use the resource unchecked once the lookup says it was found: found implies it is given.
    if (self->value[1]) *reinterpret_cast<void **>(args[2]) = &S(kResource);
    return Junk8(self->value[1] ? self->value[0] : 0);
}
uint32_t PersistIsLoaded(SObj *self, uint32_t *) { return Junk8(self->value[2]); }
uint32_t KeyTaker(SObj *, uint32_t *args)
{
    LogKey(reinterpret_cast<const uint32_t *>(args[0]));
    return 0;
}
uint32_t ResourceGetText(SObj *self, uint32_t *args)
{
    reinterpret_cast<cRZString *>(args[0])->mString = MakeText(self->value[2] + args[1] * 31 + args[2]);
    return 0;
}
uint32_t ResourceSetText(SObj *, uint32_t *args)
{
    LogString(reinterpret_cast<const cRZString *>(args[0]));
    return 0;
}
uint32_t ResourceFlush(SObj *self, uint32_t *) { return self->value[3]; }
uint32_t StringTaker(SObj *, uint32_t *args)
{
    LogString(reinterpret_cast<const cRZString *>(args[0]));
    return 0;
}
uint32_t FrameworkCreate(SObj *self, uint32_t *args)
{
    if (args[0] == 0xdb4cb0f9)
        *reinterpret_cast<void **>(args[2]) = self->value[0] ? &S(kResource) : nullptr;
    else
        *reinterpret_cast<void **>(args[2]) = self->value[1] ? &S(kRecordNew) : nullptr;
    return 1;
}
uint32_t ManagerGet(SObj *self, uint32_t *args)
{
    LogKey(reinterpret_cast<const uint32_t *>(args[0]));
    if (self->value[0]) *reinterpret_cast<void **>(args[2]) = &S(kRecordNew);
    return 1;
}
uint32_t Slot23Manager(SObj *self, uint32_t *) { return self->value[0] ? uint32_t(uintptr_t(&S(kManager))) : 0; }

// ---- the record's vtables ----
// Faces in RTTI order: offset, slot count, the game's vtable.
constexpr unsigned kFaces = 5;
constexpr unsigned kFaceOffset[kFaces] = {0, nhinfo::kSerialFace, nhinfo::kPersistFace, nhinfo::kDescribeFace, nhinfo::kUnknownFace};
constexpr unsigned kFaceSlots[kFaces] = {34, 6, 12, 5, 6};
constexpr uint32_t kGameVT[kFaces] = {Info::kOriginalVTable, Info::kOriginalSerialVTable, Info::kOriginalPersistVTable,
                                      Info::kOriginalDescribeVTable, Info::kOriginalUnknownVTable};
constexpr uint32_t kGameRZUnknownVT = 0xda2fd0; // what the original's cRZUnknown destructor 0x1b9096 stores
enum : unsigned { fMain, fSerial, fPersist, fDescribe, fUnknown };

// Our vtables, from a record built by our constructor; our cRZUnknown vtable, from what our destructor leaves at +0x10.
void *const *gOurs[kFaces];
const void *gOurRZ;
void InitOurs()
{
    alignas(8) uint8_t raw[sizeof(Info)];
    Info *o = cTSNeighborhoodInfoAdapters::Construct(reinterpret_cast<Info *>(raw), nullptr);
    for (unsigned f = 0; f < kFaces; ++f) gOurs[f] = *reinterpret_cast<void *const *const *>(raw + kFaceOffset[f]);
    o->~Info();
    gOurRZ = *reinterpret_cast<const void *const *>(raw + kFaceOffset[fUnknown]);
}
void *const *SideVT(int side, unsigned f) { return side == 0 ? GameAddress<void *const>(kGameVT[f]) : gOurs[f]; }
const void *SideRZ(int side) { return side == 0 ? GameAddress<void>(kGameRZUnknownVT) : gOurRZ; }

// The world's copies of the side's own vtables, with the recording slots: AddRef (QueryInterface, GetRecord), slot 4 (the name:
// GetRecord, Describe), slot 14 (the flag test: the category predicates); the persist face's GetKey, GetGroup, GetInstance.
void *gVT[kFaces][34];
uint32_t __fastcall ObjAddRef(Info *n, void *)
{
    Ev({evObj, 1});
    return ++n->mRefCount;
}
void *__fastcall ObjSlot4(Info *n, void *)
{
    Ev({evObj, 4});
    return &n->mName;
}
uint32_t __fastcall ObjSlot14(Info *, void *, uint32_t arg)
{
    Ev({evObj, 14, arg});
    return Junk8(gW.objAnswer);
}
void *__fastcall FaceGetKey(void *face, void *, void *out)
{
    Ev({evFace, 3, P(out)});
    std::memcpy(out, static_cast<char *>(face) + 0xa8, 16);
    return out;
}
uint32_t __fastcall FaceGetGroup(void *face, void *)
{
    Ev({evFace, 6});
    return *reinterpret_cast<uint32_t *>(static_cast<char *>(face) + 0xac);
}
uint32_t __fastcall FaceGetInstance(void *face, void *)
{
    Ev({evFace, 7});
    return *reinterpret_cast<uint32_t *>(static_cast<char *>(face) + 0xb0);
}
// The adapters test's trap: an adapter that dispatched through the object's vtable (in game: back into its own hook) logs it.
uint32_t __fastcall Trap(void *, void *)
{
    Ev({0x7a7a7a7au});
    return 0;
}

// A vtable word as one value: the world's copy, the side's own vtable of that face, the side's cRZUnknown vtable (+0x10 after
// destruction); anything else as it is (then it differs between the sides).
uint32_t VtWord(int side, unsigned f, uint32_t word)
{
    if (word == uint32_t(uintptr_t(gVT[f]))) return 0x7700 + f;
    if (word == uint32_t(uintptr_t(SideVT(side, f)))) return 0x7800 + f;
    if (f == fUnknown && word == uint32_t(uintptr_t(SideRZ(side)))) return 0x7900;
    return word;
}

struct Globals {
    difftest::Context &ctx;
    static constexpr uint32_t kRvas[] = {0x10faf6c, 0x10c9390, 0x10c93f4, 0x10cc2a8, 0x10cc2dc};
    uint32_t saved[5];
    void *directorVT[16] = {};
    static void *__fastcall DirectorFramework(void *, void *) { return &S(kFramework); }
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (unsigned i = 0; i < 5; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
        directorVT[12] = reinterpret_cast<void *>(&DirectorFramework);
        *ctx.image.At<void *>(0x10faf6c) = &S(kPersist);
        *ctx.image.At<void *>(0x10c9390) = directorVT;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
        *ctx.image.At<void *>(0x10cc2a8) = &S(kSlot23);
        *ctx.image.At<void *>(0x10cc2dc) = &S(kLanguage);
    }
    ~Globals()
    {
        for (unsigned i = 0; i < 5; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
};

// Builds the world for a seed: the fakes, the record (built the side's way: 0 the original constructor, 1 ours through the
// constructor hook's adapter), then the fields the members read, from the seed, and the vtable copies.
void Build(difftest::Context &ctx, uint32_t seed, int side)
{
    static bool tables = false;
    if (!tables) {
        FillAll(std::make_index_sequence<kSlots>());
        InitOurs();
        tables = true;
    }
    World &w = gW;
    std::mt19937 r(seed * 2654435761u + 17);
    w.events.clear();
    w.events.reserve(8192);
    w.junk = r() & 0xffffff;
    w.objAnswer = r() % 3 ? r() % 2 : r();
    w.script.clear();
    w.scriptPos = 0;
    for (unsigned i = 0; i < kFakeCount; ++i) ResetFake(w.fakes[i], i);

    // Persist system, its record table, string table resource, framework, record manager, services.
    Set(S(kPersist), 5, 5, 0, PersistGetResource);
    Set(S(kPersist), 3, 0, &S(kRecords));
    Set(S(kPersist), 16, 2, 0, PersistIsLoaded);
    S(kPersist).value[0] = r() % 3 != 0;
    S(kPersist).value[1] = r() % 4 != 0;
    S(kPersist).value[2] = r() % 3 ? r() % 2 : r();
    Set(S(kRecords), 10, 1, 0, KeyTaker);
    Set(S(kResource), 7, 3, 0, ResourceGetText);
    Set(S(kResource), 10, 3, 0, ResourceSetText);
    Set(S(kResource), 3, 0, &S(kSerial));
    Set(S(kResource), 21, 0, 0, ResourceFlush);
    S(kResource).value[2] = r();
    S(kResource).value[3] = r() % 3 ? 0 : r() % 4;
    Set(S(kSerial), 4, 1, 0, KeyTaker);
    Set(S(kFramework), 3, 3, 0, FrameworkCreate);
    // StoreTexts uses the resource the framework creates without a check (ORIGINAL-BUG in src): always created.
    S(kFramework).value[0] = 1;
    S(kFramework).value[1] = r() % 5 != 0;
    Set(S(kManager), 3, 3, 0, ManagerGet);
    Set(S(kManager), 4, 4);
    S(kManager).value[0] = r() % 2;
    Set(S(kSlot23), 34, 1, 0, Slot23Manager);
    S(kSlot23).value[0] = r() % 5 != 0;
    Set(S(kLanguage), 7, 0, r() % 3 ? 1 : r() % 4);
    Set(S(kRecordNew), 13, 1, 0, StringTaker);
    Set(S(kRecordNew), 4, 1, 0, KeyTaker);
    S(kRecordOld).refs = 2000;
    Set(S(kTarget), 16, 1);
    Set(S(kTarget), 20, 1);
    // Stream: error answer, mode set by the test.
    Set(S(kStream), 4, 1, 0, StreamByte);
    Set(S(kStream), 8, 1, 0, StreamWord);
    Set(S(kStream), 9, 1, 0, StreamWord);
    Set(S(kStream), 15, 1, 0, StreamText);
    Set(S(kStream), 18, 0, 0, StreamError);
    S(kStream).value[0] = r() % 4 == 0 ? (r() % 2 ? 1 : r() & 0xff) : 0;

    std::memset(w.work.before, 0xa5, sizeof(w.work.before));
    std::memset(w.work.after, 0x5a, sizeof(w.work.after));
    std::memset(w.work.bytes, 0xcd, sizeof(w.work.bytes));
    Info *n = N();
    if (side == 0) ctx.Original<Info *(__thiscall *)(void *)>(0x4a6350)(w.work.bytes);
    else cTSNeighborhoodInfoAdapters::Construct(n, nullptr);
    for (cRZString *s : {&n->mName, &n->mDescription, &n->mFilePrefix, &n->mTextD}) {
        s->mString = MakeText(r() % 4 ? r() : r() % 64);
        s->mRefCount = r() % 3;
    }
    n->mRefCount = r() % 4;
    n->mNumber = r() % 3 ? int(r() % 8) - 1 : int(r());
    n->mFlags = r() % 3 ? r() % 16 : r();
    n->mVersion = r() % 4 ? r() % 13 : r();
    static const int32_t categories[] = {0, 1, 2, 10, 12, 13, 13, 14, 15, 16, 16, 17, 0x7fffffff, -1};
    n->mCategory = r() % 5 ? categories[r() % (sizeof(categories) / 4)] : int32_t(r());
    n->mKind = r() % 4 ? r() % 10 : r();
    n->mFieldAC = r() % 3 ? r() % 0x14 : r();
    uint32_t *key = &n->mKey.type;
    for (int i = 0; i < 4; ++i) key[i] = r() % 4 ? r() % 100 : r();
    if (r() % 5 == 0) n->mKey = {};
    n->mRecord = r() % 3 == 0 ? static_cast<void *>(&S(kRecordOld)) : nullptr;
    for (auto &v : n->mSlotValues) v = r() % 3 ? int(r() % 5) - 1 : int(r());

    // The vtable copies: the record's own vtables (the side's), recording slots patched in.
    for (unsigned f = 0; f < kFaces; ++f) {
        void **word = reinterpret_cast<void **>(w.work.bytes + kFaceOffset[f]);
        std::memcpy(gVT[f], *word, kFaceSlots[f] * sizeof(void *));
        *word = gVT[f];
    }
    gVT[fMain][1] = reinterpret_cast<void *>(&ObjAddRef);
    gVT[fMain][4] = reinterpret_cast<void *>(&ObjSlot4);
    gVT[fMain][14] = reinterpret_cast<void *>(&ObjSlot14);
    gVT[fPersist][3] = reinterpret_cast<void *>(&FaceGetKey);
    gVT[fPersist][6] = reinterpret_cast<void *>(&FaceGetGroup);
    gVT[fPersist][7] = reinterpret_cast<void *>(&FaceGetInstance);
    w.events.clear();
}

// A canonical copy of a record's state: the vtable words (VtWord), the plain words, the strings by content, pointers normalised.
void CaptureObject(Blob &b, const uint8_t *bytes, int side)
{
    const Info *o = reinterpret_cast<const Info *>(bytes);
    const uint32_t *w = reinterpret_cast<const uint32_t *>(bytes);
    b.push_back(0xb10b);
    for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtWord(side, f, w[kFaceOffset[f] / 4]));
    b.push_back(w[5]); // the cRZUnknown count
    for (const cRZString *s : {&o->mName, &o->mDescription, &o->mFilePrefix, &o->mTextD}) {
        b.push_back(uint32_t(reinterpret_cast<const uint32_t &>(s->mVTable)));
        b.push_back(uint32_t(s->mString.size()));
        b.push_back(uint32_t(s->mString.capacity()));
        b.push_back(s->mRefCount);
        b.push_back(Hash(s->mString));
    }
    for (unsigned off = 0x98; off < sizeof(Info); off += 4) b.push_back(off == 0xc0 ? Norm(w[off / 4]) : w[off / 4]);
}

void Capture(Blob &b, const uint8_t *object, int side, bool alive = true)
{
    World &w = gW;
    b.push_back(0xc0de);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    if (alive) CaptureObject(b, object, side);
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

// Runs `run(bool original)` in two worlds built from one seed and compares the result, the record and the frees. Each side's
// record is built that side's way, unless `buildSide` says otherwise (the adapters test: both the original's).
template <class Setup, class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Setup setup, Run run, bool alive = true, int buildSide = -1)
{
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        Build(ctx, seed, buildSide < 0 ? side : buildSide);
        std::mt19937 rng(seed * 7919u + 5);
        setup(rng);
        difftest::FreeRecorder rec;
        const uint32_t result = uint32_t(run(side == 0));
        rec.Stop();
        blobs[side].push_back(result);
        size_t sizes[64];
        const size_t count = rec.SortedSizes(sizes, 64);
        blobs[side].push_back(uint32_t(rec.Count()));
        for (size_t i = 0; i < count; ++i) blobs[side].push_back(uint32_t(sizes[i]));
        Capture(blobs[side], gW.work.bytes, side, alive);
    }
    Same(ctx, blobs[0], blobs[1], what, seed);
}
auto NoSetup = [](std::mt19937 &) {};

// Like Pair, on a heap copy of the world's record (so the free of the block shows): `run(side, copy)`.
template <class Setup, class Run>
void HeapPair(difftest::Context &ctx, const char *what, uint32_t seed, Setup setup, Run run, int buildSide = -1)
{
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        Build(ctx, seed, buildSide < 0 ? side : buildSide);
        std::mt19937 rng(seed * 7919u + 5);
        setup(rng);
        Info *h = static_cast<Info *>(::operator new(sizeof(Info)));
        std::memcpy(static_cast<void *>(h), gW.work.bytes, sizeof(Info)); // the strings move with it (the world's copy is dropped)
        difftest::FreeRecorder rec;
        const uint32_t result = run(side, h);
        rec.Stop();
        const bool freed = rec.Freed(h);
        Blob &b = blobs[side];
        b.push_back(result);
        b.push_back(freed);
        b.push_back(uint32_t(rec.Count()));
        size_t sizes[64];
        const size_t count = rec.SortedSizes(sizes, 64);
        for (size_t i = 0; i < count; ++i) b.push_back(uint32_t(sizes[i]));
        Capture(b, reinterpret_cast<uint8_t *>(h), side, !freed);
        if (!freed) ::operator delete(h, sizeof(Info));
    }
    Same(ctx, blobs[0], blobs[1], what, seed);
}

// Slot `slot` of our vtable of face `f`, called with this = that face of the world's record (our side of every test).
template <class R, class... A> R Ours(unsigned f, int slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>(gOurs[f][slot])(gW.work.bytes + kFaceOffset[f], args...);
}
template <class R, class... A> R OursOn(void *object, unsigned f, int slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>(gOurs[f][slot])(static_cast<uint8_t *>(object) + kFaceOffset[f], args...);
}
void *Face(unsigned f) { return gW.work.bytes + kFaceOffset[f]; }

constexpr uint32_t kSeeds = 200;
#define ORIG(Fn, rva) ctx.Original<Fn>(rva)

using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using Fn0 = uint32_t(__thiscall *)(void *);

// A script for Read: the version first, then words (mostly small, sometimes large), some reads failing.
void MakeReadScript(std::mt19937 &r, uint32_t version, bool failures)
{
    gW.script.clear();
    gW.script.push_back(version);
    for (unsigned i = 0; i < 24; ++i) {
        uint64_t v = r() % 3 == 0 ? r() % 0x13 : (r() % 8 == 0 ? r() : r() % 4);
        if (failures && r() % 12 == 0) v |= uint64_t(1) << 32;
        gW.script.push_back(v);
    }
}
} // namespace

// ---- construction and destruction ----

DIFF_TEST(NeighborhoodInfo_Construct, 0x4a6350)
{
    InitOurs();
    // Our vtables are not the game's, and the faces sit where RTTI puts them.
    ++ctx.cases;
    for (unsigned f = 0; f < kFaces; ++f)
        if (gOurs[f] == SideVT(0, f)) ctx.Fail("face %u: our vtable is the game's", f);
    if (gOurRZ == SideRZ(0)) ctx.Fail("our cRZUnknown vtable is the game's");
    alignas(8) uint8_t probe[sizeof(Info)];
    Info *p = reinterpret_cast<Info *>(probe);
    if (reinterpret_cast<uint8_t *>(static_cast<cIGZSerializable *>(p)) != probe + nhinfo::kSerialFace ||
        reinterpret_cast<uint8_t *>(static_cast<cIGZPersistResource *>(p)) != probe + nhinfo::kPersistFace ||
        reinterpret_cast<uint8_t *>(static_cast<cIGZSelfDescribing *>(p)) != probe + nhinfo::kDescribeFace ||
        reinterpret_cast<uint8_t *>(static_cast<rz::cRZUnknown *>(p)) != probe + nhinfo::kUnknownFace)
        ctx.Fail("face offsets");
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[sizeof(Info)];
            std::memset(raw, int(0x11 * (seed + 1)), sizeof(raw));
            Info *n = reinterpret_cast<Info *>(raw);
            Info *r = side == 0 ? ORIG(Info *(__thiscall *)(Info *), 0x4a6350)(n) : cTSNeighborhoodInfoAdapters::Construct(n, nullptr);
            Blob &b = blobs[side];
            b.push_back(r == n);
            const uint32_t *w = reinterpret_cast<const uint32_t *>(raw);
            for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtWord(side, f, w[kFaceOffset[f] / 4]));
            for (unsigned i = 5; i < sizeof(raw) / 4; ++i) b.push_back(w[i]);
            // The destructor (ours through the destructor slot, flags 0): the words it leaves.
            if (side == 0) ORIG(Fn0, 0x4a6500)(n);
            else OursOn<Info *>(n, fUnknown, 3, uint32_t(0));
            for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtWord(side, f, w[kFaceOffset[f] / 4]));
            for (unsigned i = 5; i < sizeof(raw) / 4; ++i) b.push_back(w[i]);
        }
        Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

DIFF_TEST(NeighborhoodInfo_Create, 0x4a6720)
{
    InitOurs();
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Info *o = side == 0 ? ORIG(Info *(__cdecl *)(), 0x4a6720)() : Info::Create();
            ++ctx.cases;
            if (!o) { ctx.Fail("create returned null"); return; }
            if (HeapSize(GetProcessHeap(), 0, o) < sizeof(Info)) ctx.Fail("create block size");
            Blob &b = blobs[side];
            const uint32_t *w = reinterpret_cast<const uint32_t *>(o);
            for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtWord(side, f, w[kFaceOffset[f] / 4]));
            for (unsigned i = 5; i < sizeof(Info) / 4; ++i) b.push_back(w[i]);
            // Freed through the destructor slot with 1 (ours) / the deleting destructor (the original's).
            difftest::FreeRecorder rec;
            Info *r = side == 0 ? ORIG(Info *(__thiscall *)(void *, uint32_t), 0x4a66c0)(o, 1) : OursOn<Info *>(o, fUnknown, 3, uint32_t(1));
            rec.Stop();
            b.insert(b.end(), {uint32_t(r == o), uint32_t(rec.Freed(o)), uint32_t(rec.Count())});
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

DIFF_TEST(NeighborhoodInfo_Destruct, 0x4a6500)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Destruct", seed, NoSetup, [&](bool original) {
            if (original) ORIG(Fn0, 0x4a6500)(N());
            else Ours<Info *>(fUnknown, 3, uint32_t(0)); // the destructor slot with flags 0: the destructor alone
            return 0;
        });
}

DIFF_TEST(NeighborhoodInfo_DeletingDestructor, 0x4a66c0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const uint32_t flags = seed % 3;
        // The original's body 0x4a66c0 and its +0x10 thunk 0x4a66ac; ours through the +0x10 face's slot 3.
        const bool viaThunk = seed % 2;
        HeapPair(ctx, "DeletingDestructor", seed, NoSetup, [&](int side, Info *h) {
            Info *r;
            if (side == 1) r = OursOn<Info *>(h, fUnknown, 3, flags);
            else if (viaThunk) r = ORIG(Info *(__thiscall *)(void *, uint32_t), 0x4a66ac)(reinterpret_cast<uint8_t *>(h) + 0x10, flags);
            else r = ORIG(Info *(__thiscall *)(void *, uint32_t), 0x4a66c0)(h, flags);
            return uint32_t(r == h);
        });
    }
}

DIFF_TEST(NeighborhoodInfo_QueryInterface, 0x4a6d00)
{
    Globals globals(ctx);
    const uint32_t ids[] = {0, 1, 2, 0xac8a7a29, 0xac8a7a2a, 0xac8a7a28, 0x456b8f1d, 0x456b8f1c, 0x51f85085, 0x51f85086,
                            0xe4fda3d4, 0xe4fda3d5, 0xe4fda3d3, 0xffffffff};
    // The original's entry by face: +0 0x4a6d00, the thunks +4 0x4a6d7c, +8 0x4a6d84, +0xc 0x4a6d94, +0x10 0x4a6d8c.
    const uint32_t entries[] = {0x4a6d00, 0x4a6d7c, 0x4a6d84, 0x4a6d94, 0x4a6d8c};
    for (unsigned f = 0; f < kFaces; ++f)
        for (uint32_t id : ids) {
            Pair(ctx, "QueryInterface", id * 8 + f, NoSetup, [&](bool original) {
                void *out = reinterpret_cast<void *>(0x1234);
                const uint8_t r = original ? uint8_t(ORIG(uint32_t(__thiscall *)(void *, uint32_t, void **), entries[f])(Face(f), id, &out))
                                           : Ours<uint8_t>(f, 0, id, &out);
                gW.events.push_back(P(out));
                return r;
            });
        }
}

// ---- accessors ----

DIFF_TEST(NeighborhoodInfo_Text, 0x4a71c0)
{
    // 0x4a71c0 (slot 3) assigns the name, 0x4a71e0 (slot 9) the second saved text: strings from empty to well past the
    // small-string size.
    std::string text; // built in the setup, before the frees are recorded
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "SetName", seed, [&](std::mt19937 &) { text = MakeText(seed * 5 + 1); }, [&](bool original) {
            if (original) ORIG(Fn1, 0x4a71c0)(N(), uint32_t(uintptr_t(text.c_str())));
            else Ours<void>(fMain, 3, text.c_str());
            return 0;
        });
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "SetTextD", seed, [&](std::mt19937 &) { text = MakeText(seed * 5 + 2); }, [&](bool original) {
            if (original) ORIG(Fn1, 0x4a71e0)(N(), uint32_t(uintptr_t(text.c_str())));
            else Ours<void>(fMain, 9, text.c_str());
            return 0;
        });
}

DIFF_TEST(NeighborhoodInfo_Number, 0x4a7180)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "SetNumber/IsNumber", seed, NoSetup, [&](bool original) {
            std::mt19937 r(seed);
            const int32_t value = r() % 3 ? int(r() % 6) - 1 : int(r());
            uint32_t out = 0;
            if (original) {
                ORIG(Fn1, 0x4a7180)(N(), uint32_t(value));
                for (int32_t probe : {value, value + 1, -1, 0, int32_t(r())}) out = out * 3 + (uint8_t(ORIG(Fn1, 0x4a6ae0)(N(), uint32_t(probe))));
            } else {
                Ours<void>(fMain, 11, value);
                for (int32_t probe : {value, value + 1, -1, 0, int32_t(r())}) out = out * 3 + Ours<uint8_t>(fMain, 15, probe);
            }
            return out;
        });
}

DIFF_TEST(NeighborhoodInfo_Flags, 0x4a7140)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "SetFlags/HasFlags", seed, NoSetup, [&](bool original) {
            std::mt19937 r(seed + 1000);
            uint32_t out = 0;
            for (int step = 0; step < 6; ++step) {
                const uint32_t mask = r() % 3 ? 1u << (r() % 6) : r();
                const uint32_t onWord = r() % 3 ? r() % 2 : (r() | 0x100); // the original tests the low byte only
                using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
                if (original) ORIG(Fn2, 0x4a7140)(N(), mask, onWord);
                else Ours<void>(fMain, 13, mask, onWord);
                const uint32_t probe = r() % 2 ? mask : r();
                out = out * 5 + (original ? uint8_t(ORIG(Fn1, 0x4a6700)(N(), probe)) : Ours<uint8_t>(fMain, 14, probe));
            }
            return out;
        });
}

DIFF_TEST(NeighborhoodInfo_MatchesName, 0x4a6af0)
{
    // Equal to the name, to the first saved text, to neither; with other case; empty strings.
    std::string probe;
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "MatchesName", seed,
            [&](std::mt19937 &) {
                std::mt19937 r(seed + 3);
                Info *n = N();
                probe.clear();
                switch (r() % 5) {
                case 0: probe = n->mName.mString; break;
                case 1: probe = n->mFilePrefix.mString; break;
                case 2: probe = MakeText(r()); break;
                case 3:
                    probe = n->mName.mString;
                    for (auto &c : probe) c = char(c >= 'a' && c <= 'z' ? c - 32 : c);
                    break;
                default: break;
                }
            },
            [&](bool original) {
                return original ? uint8_t(ORIG(Fn1, 0x4a6af0)(N(), uint32_t(uintptr_t(probe.c_str())))) : Ours<uint8_t>(fMain, 16, probe.c_str());
            });
}

DIFF_TEST(NeighborhoodInfo_SlotValues, 0x4a71f0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "SlotValues", seed, NoSetup, [&](bool original) {
            std::mt19937 r(seed + 9);
            uint32_t out = 0;
            using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
            for (int step = 0; step < 6; ++step) {
                const uint32_t value = r() % 4 ? r() % 6 : r();
                const uint32_t index = r() % 4 ? r() % 6 : r();
                if (original) ORIG(Fn2, 0x4a71f0)(N(), value, index);
                else Ours<void>(fMain, 30, value, index);
                const uint32_t get = r() % 4;
                out = out * 7 + (original ? ORIG(Fn1, 0x4a6950)(N(), get) : Ours<uint32_t>(fMain, 29, get));
            }
            return out;
        });
}

DIFF_TEST(NeighborhoodInfo_Predicates, 0x4a6b70)
{
    auto kind = [&](bool original) { return original ? uint8_t(ORIG(Fn0, 0x4a6b70)(N())) : Ours<uint8_t>(fMain, 31); };
    auto cat13 = [&](bool original) { return original ? uint8_t(ORIG(Fn0, 0x4a6b30)(N())) : Ours<uint8_t>(fMain, 32); };
    auto cat16 = [&](bool original) { return original ? uint8_t(ORIG(Fn0, 0x4a6b50)(N())) : Ours<uint8_t>(fMain, 33); };
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "IsKind567", seed, NoSetup, kind);
        Pair(ctx, "IsCategory13WithFlag4", seed, NoSetup, cat13);
        Pair(ctx, "IsCategory16WithFlag4", seed, NoSetup, cat16);
    }
    // Kinds 0..9 each, categories 13 and 16 with answers of 0, 1 and bytes past 1 (normalised to 1 by the original).
    for (uint32_t k = 0; k < 10; ++k) Pair(ctx, "IsKind567 kind", k, [&](std::mt19937 &) { N()->mKind = k; }, kind);
    for (uint32_t cat : {13, 16, 12, 14, 15, 17})
        for (uint32_t answer : {0u, 1u, 0x100u, 0x80u})
            Pair(ctx, "Category x answer", cat * 8 + answer, [&](std::mt19937 &) { N()->mCategory = int(cat); gW.objAnswer = answer; },
                [&](bool original) { return cat13(original) * 4u + cat16(original); });
}

// ---- the persist face and the identity ----

DIFF_TEST(NeighborhoodInfo_Key, 0x4a6910)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Key", seed, NoSetup, [&](bool original) {
            std::mt19937 r(seed + 77);
            uint32_t key[4] = {r(), r(), r(), r()};
            const uint32_t instance = r();
            uint32_t out[8];
            std::memset(out, 0xee, sizeof(out));
            uint32_t acc = 0;
            using SetFn = uint32_t(__thiscall *)(void *, const void *);
            using GetFn = void *(__thiscall *)(void *, void *);
            void *face = Face(fPersist);
            if (original) {
                acc = uint8_t(ORIG(SetFn, 0x4a71a0)(face, key));
                acc = acc * 3 + uint8_t(ORIG(Fn1, 0x4a7190)(face, instance));
                acc = acc * 3 + (ORIG(GetFn, 0x4a6910)(face, out) == out);
            } else {
                acc = Ours<uint8_t>(fPersist, 4, static_cast<const void *>(key));
                acc = acc * 3 + Ours<uint8_t>(fPersist, 10, instance);
                acc = acc * 3 + (Ours<void *>(fPersist, 3, static_cast<void *>(out)) == out);
            }
            for (uint32_t v : out) gW.events.push_back(v);
            return acc;
        });
    Pair(ctx, "GetGZCLSID", 0, NoSetup, [&](bool original) {
        return original ? ORIG(Fn0, 0x4a6900)(Face(fSerial)) : Ours<uint32_t>(fSerial, 5);
    });
}

DIFF_TEST(NeighborhoodInfo_Describe, 0x4a6930)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 40; ++seed)
        Pair(ctx, "Describe", seed, NoSetup, [&](bool original) {
            void *target = &S(kTarget);
            return original ? uint8_t(ORIG(uint32_t(__thiscall *)(void *, void *), 0x4a6930)(Face(fDescribe), target))
                            : Ours<uint8_t>(fDescribe, 3, target);
        });
}

// ---- serialization ----

DIFF_TEST(NeighborhoodInfo_Read, 0x4a6da0)
{
    Globals globals(ctx);
    const uint32_t versions[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0xffffffffu, 0x10000};
    for (uint32_t seed = 0; seed < 4 * kSeeds; ++seed)
        Pair(ctx, "Read", seed, [&](std::mt19937 &r) {
            S(kStream).value[1] = 0;
            const uint32_t version = r() % 5 ? versions[seed % 13] : versions[r() % 15];
            MakeReadScript(r, version, seed % 3 != 0);
        },
        [&](bool original) {
            void *stream = &S(kStream);
            return original ? uint8_t(ORIG(uint32_t(__thiscall *)(void *, void *), 0x4a6da0)(Face(fSerial), stream))
                            : Ours<uint8_t>(fSerial, 4, stream);
        });
}

DIFF_TEST(NeighborhoodInfo_Write, 0x4a7310)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Write", seed, [&](std::mt19937 &) { S(kStream).value[1] = 1; },
        [&](bool original) {
            void *stream = &S(kStream);
            return original ? uint8_t(ORIG(uint32_t(__thiscall *)(void *, void *), 0x4a7310)(Face(fSerial), stream))
                            : Ours<uint8_t>(fSerial, 3, stream);
        });
}

// ---- the members that reach the persist system, the framework and the string tables ----

DIFF_TEST(NeighborhoodInfo_LoadTexts, 0x4a6b90)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 4 * kSeeds; ++seed)
        Pair(ctx, "LoadTexts", seed, [&](std::mt19937 &r) { N()->mVersion = r() % 3 ? seed % 6 : r(); },
        [&](bool original) {
            if (original) ORIG(Fn0, 0x4a6b90)(N());
            else Ours<void>(fMain, 25);
            return 0;
        });
}

DIFF_TEST(NeighborhoodInfo_Unload, 0x4a7220)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Unload", seed, NoSetup, [&](bool original) {
            if (original) ORIG(Fn0, 0x4a7220)(N());
            else Ours<void>(fMain, 26);
            return 0;
        });
}

DIFF_TEST(NeighborhoodInfo_StoreTexts, 0x4a6fb0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 4 * kSeeds; ++seed)
        Pair(ctx, "StoreTexts", seed, NoSetup, [&](bool original) {
            return original ? uint8_t(ORIG(Fn0, 0x4a6fb0)(N())) : Ours<uint8_t>(fMain, 27);
        });
}

DIFF_TEST(NeighborhoodInfo_GetRecord, 0x4a6960)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 4 * kSeeds; ++seed)
        Pair(ctx, "GetRecord", seed, NoSetup, [&](bool original) {
            void *out = reinterpret_cast<void *>(0x1234);
            const uint8_t r = original ? uint8_t(ORIG(uint32_t(__thiscall *)(void *, void **), 0x4a6960)(N(), &out))
                                       : Ours<uint8_t>(fMain, 28, &out);
            gW.events.push_back(P(out));
            return r;
        });
}

// ===== Every other slot, through each side's own vtable =====
// The slots no test above calls: the folded accessors, the faces' AddRef / Release thunks, the persist face's key words and
// GetRefCount, the describe face's slot 4, cRZUnknown's own slots 4 and 5. Each side's slot of its own (pristine) vtable on its
// own record: the game's functions on the original side.
namespace {
enum SlotKind { kWord, kPointer, kText, kSetWord, kSetWordByte, kPointerArgByte };
struct SlotCase {
    unsigned face;
    int slot;
    SlotKind kind;
};
constexpr SlotCase kSlotCases[] = {
    {fMain, 1, kWord}, {fMain, 2, kWord}, {fMain, 4, kPointer}, {fMain, 5, kText}, {fMain, 6, kPointer}, {fMain, 7, kText},
    {fMain, 8, kPointer}, {fMain, 10, kPointer}, {fMain, 12, kWord}, {fMain, 17, kSetWord}, {fMain, 18, kWord},
    {fMain, 19, kSetWord}, {fMain, 20, kWord}, {fMain, 21, kSetWord}, {fMain, 22, kWord}, {fMain, 23, kPointer},
    {fMain, 24, kPointer},
    {fSerial, 1, kWord}, {fSerial, 2, kWord},
    {fPersist, 1, kWord}, {fPersist, 2, kWord}, {fPersist, 5, kWord}, {fPersist, 6, kWord}, {fPersist, 7, kWord},
    {fPersist, 8, kSetWordByte}, {fPersist, 9, kSetWordByte}, {fPersist, 11, kWord},
    {fDescribe, 1, kWord}, {fDescribe, 2, kWord}, {fDescribe, 4, kPointerArgByte},
    {fUnknown, 1, kWord}, {fUnknown, 2, kWord}, {fUnknown, 4, kWord}, {fUnknown, 5, kWord},
};
template <class R, class... A> R SideSlot(int side, unsigned f, int slot, void *object, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>(SideVT(side, f)[slot])(static_cast<uint8_t *>(object) + kFaceOffset[f], args...);
}
} // namespace

DIFF_TEST(NeighborhoodInfo_Slots, 0x4a6d7c)
{
    Globals globals(ctx);
    std::string text;
    for (uint32_t seed = 0; seed < 40; ++seed)
        for (const SlotCase &c : kSlotCases) {
            uint32_t arg = 0;
            Pair(ctx, "Slots", seed << 16 | c.face << 8 | uint32_t(c.slot),
                [&](std::mt19937 &r) {
                    // Release only counts down here (at 1 or below it deletes: below, on a heap copy).
                    N()->mRefCount = c.slot == 2 ? 2 + r() % 4 : r() % 4;
                    arg = r() % 3 ? r() % 20 : r();
                    text = MakeText(r());
                },
                [&](bool original) {
                    const int side = original ? 0 : 1;
                    Info *n = N();
                    switch (c.kind) {
                    case kWord: return SideSlot<uint32_t>(side, c.face, c.slot, n);
                    case kPointer: return P(SideSlot<void *>(side, c.face, c.slot, n));
                    case kText: SideSlot<void>(side, c.face, c.slot, n, text.c_str()); return 0u;
                    case kSetWord: SideSlot<void>(side, c.face, c.slot, n, arg); return 0u;
                    case kSetWordByte: return uint32_t(SideSlot<uint8_t>(side, c.face, c.slot, n, arg));
                    default: return uint32_t(SideSlot<uint8_t>(side, c.face, c.slot, n, static_cast<void *>(&S(kTarget))));
                    }
                });
        }
    // Release to zero through each face: the record deletes itself through the destructor slot.
    for (uint32_t seed = 0; seed < 20; ++seed)
        for (unsigned f = 0; f < kFaces; ++f)
            HeapPair(ctx, "Release to zero", seed << 8 | f, [&](std::mt19937 &r) { N()->mRefCount = r() % 2; },
                [&](int side, Info *h) { return SideSlot<uint32_t>(side, f, 2, h); });
}

// ===== The adapters: our functions behind the old entry points, on a record built by the original constructor =====
// What runs when the game calls an old entry point directly or holds a record with its own vtables (the constructor and Create
// hooks off). Both sides' records are the original's (game vtables, copied with the recording slots); the original side calls
// the RVA, ours the adapter the hook installs (__fastcall with the dummy edx: called as the thiscall the game makes). The slot
// each adapter forwards to is a trap in the copy (slot 0 of every face for the QueryInterface entries), so an adapter that
// dispatched through the object's vtable logs it.
namespace {
using A = cTSNeighborhoodInfoAdapters;
enum AdapterKind {
    aQuery, aText, aWordVoid, aWordByte, aTwoWordsVoid, aWord, aNoneByte, aNoneVoid, aRecord, aStreamRead, aStreamWrite,
    aGetKey, aSetKey, aDescribe, aDestruct, aDeleting, aNoneWord
};
struct AdapterCase {
    uint32_t rva;
    void *adapter;
    unsigned face;
    int slot; // -1: every face's slot 0
    AdapterKind kind;
};
const AdapterCase kAdapterCases[] = {
    {0x4a6d00, reinterpret_cast<void *>(&A::QueryInterface), fMain, -1, aQuery},
    {0x4a6d7c, reinterpret_cast<void *>(&A::SerialQueryInterface), fSerial, -1, aQuery},
    {0x4a6d84, reinterpret_cast<void *>(&A::PersistQueryInterface), fPersist, -1, aQuery},
    {0x4a6d94, reinterpret_cast<void *>(&A::DescribeQueryInterface), fDescribe, -1, aQuery},
    {0x4a6d8c, reinterpret_cast<void *>(&A::UnknownQueryInterface), fUnknown, -1, aQuery},
    {0x4a71c0, reinterpret_cast<void *>(&A::SetName), fMain, 3, aText},
    {0x4a71e0, reinterpret_cast<void *>(&A::SetTextD), fMain, 9, aText},
    {0x4a7180, reinterpret_cast<void *>(&A::SetNumber), fMain, 11, aWordVoid},
    {0x4a6ae0, reinterpret_cast<void *>(&A::IsNumber), fMain, 15, aWordByte},
    {0x4a7140, reinterpret_cast<void *>(&A::SetFlags), fMain, 13, aTwoWordsVoid},
    {0x4a6700, reinterpret_cast<void *>(&A::HasFlags), fMain, 14, aWordByte},
    {0x4a6af0, reinterpret_cast<void *>(&A::MatchesName), fMain, 16, aText},
    {0x4a6950, reinterpret_cast<void *>(&A::GetSlotValue), fMain, 29, aWord},
    {0x4a71f0, reinterpret_cast<void *>(&A::SetSlotValue), fMain, 30, aTwoWordsVoid},
    {0x4a6b70, reinterpret_cast<void *>(&A::IsKind567), fMain, 31, aNoneByte},
    {0x4a6b30, reinterpret_cast<void *>(&A::IsCategory13WithFlag4), fMain, 32, aNoneByte},
    {0x4a6b50, reinterpret_cast<void *>(&A::IsCategory16WithFlag4), fMain, 33, aNoneByte},
    {0x4a6b90, reinterpret_cast<void *>(&A::LoadTexts), fMain, 25, aNoneVoid},
    {0x4a7220, reinterpret_cast<void *>(&A::Unload), fMain, 26, aNoneVoid},
    {0x4a6fb0, reinterpret_cast<void *>(&A::StoreTexts), fMain, 27, aNoneByte},
    {0x4a6960, reinterpret_cast<void *>(&A::GetRecord), fMain, 28, aRecord},
    {0x4a6900, reinterpret_cast<void *>(&A::GetGZCLSID), fSerial, 5, aNoneWord},
    {0x4a7310, reinterpret_cast<void *>(&A::Write), fSerial, 3, aStreamWrite},
    {0x4a6da0, reinterpret_cast<void *>(&A::Read), fSerial, 4, aStreamRead},
    {0x4a6910, reinterpret_cast<void *>(&A::GetKey), fPersist, 3, aGetKey},
    {0x4a71a0, reinterpret_cast<void *>(&A::SetKey), fPersist, 4, aSetKey},
    {0x4a7190, reinterpret_cast<void *>(&A::SetInstance), fPersist, 10, aWordByte},
    {0x4a6930, reinterpret_cast<void *>(&A::Describe), fDescribe, 3, aDescribe},
    {0x4a6500, reinterpret_cast<void *>(&A::Destruct), fMain, -2, aDestruct},
    {0x4a66c0, reinterpret_cast<void *>(&A::DeletingDestructor), fMain, -2, aDeleting},
    {0x4a66ac, reinterpret_cast<void *>(&A::UnknownDeletingDestructor), fUnknown, -2, aDeleting},
};
void SetTraps(const AdapterCase &c)
{
    if (c.slot == -1)
        for (unsigned f = 0; f < kFaces; ++f) gVT[f][0] = reinterpret_cast<void *>(&Trap);
    else if (c.slot == -2) // the destructors: the destructor slot
        gVT[fUnknown][3] = reinterpret_cast<void *>(&Trap);
    else
        gVT[c.face][c.slot] = reinterpret_cast<void *>(&Trap);
}
} // namespace

DIFF_TEST(NeighborhoodInfo_Adapters, 0x4a6d00)
{
    Globals globals(ctx);
    const uint32_t ids[] = {0xac8a7a29, 0x456b8f1d, 0x51f85085, 0xe4fda3d4, 1, 0, 0xac8a7a2e};
    std::string text;
    for (const AdapterCase &c : kAdapterCases)
        for (uint32_t seed = 0; seed < 40; ++seed) {
            uint32_t a1 = 0, a2 = 0;
            auto setup = [&](std::mt19937 &r) {
                SetTraps(c);
                a1 = r();
                a2 = r() % 3 ? r() % 6 : r();
                text = r() % 3 ? MakeText(r()) : N()->mName.mString;
                if (c.kind == aStreamRead) {
                    S(kStream).value[1] = 0;
                    MakeReadScript(r, r() % 12, seed % 3 != 0);
                } else if (c.kind == aStreamWrite) {
                    S(kStream).value[1] = 1;
                }
                if (c.kind == aWordByte || c.kind == aWord || c.kind == aWordVoid) a1 = r() % 3 ? r() % 6 - 1 : a1;
            };
            const uint32_t tag = (c.rva & 0xfff) << 16 | seed;
            if (c.kind == aDeleting) {
                HeapPair(ctx, "Adapters", tag, setup, [&](int side, Info *h) {
                    void *fn = side == 0 ? ctx.image.At(c.rva) : c.adapter;
                    void *self = reinterpret_cast<uint8_t *>(h) + kFaceOffset[c.face];
                    void *r = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(fn)(self, seed % 3);
                    return uint32_t(r == h);
                }, 0);
                continue;
            }
            Pair(ctx, "Adapters", tag, setup, [&](bool original) {
                void *fn = original ? ctx.image.At(c.rva) : c.adapter;
                void *self = Face(c.face);
                switch (c.kind) {
                case aQuery: {
                    void *out = reinterpret_cast<void *>(0x1234);
                    const uint32_t r = uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, void **)>(fn)(self, ids[a1 % 7], &out));
                    gW.events.push_back(P(out));
                    return r;
                }
                case aText:
                    return reinterpret_cast<uint32_t(__thiscall *)(void *, const char *)>(fn)(self, text.c_str()) & (c.slot == 16 ? 0xffu : 0u);
                case aWordVoid: reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(fn)(self, a1); return 0u;
                case aWordByte: return uint32_t(uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, a1)));
                case aTwoWordsVoid: reinterpret_cast<void(__thiscall *)(void *, uint32_t, uint32_t)>(fn)(self, a1, a2); return 0u;
                case aWord: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, a2 % 4);
                case aNoneByte: return uint32_t(uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self)));
                case aNoneVoid: reinterpret_cast<void(__thiscall *)(void *)>(fn)(self); return 0u;
                case aNoneWord: return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
                case aRecord: {
                    void *out = reinterpret_cast<void *>(0x1234);
                    const uint32_t r = uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, void **)>(fn)(self, &out));
                    gW.events.push_back(P(out));
                    return r;
                }
                case aStreamRead:
                case aStreamWrite:
                    return uint32_t(uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>(fn)(self, &S(kStream))));
                case aGetKey: {
                    uint32_t out[6];
                    std::memset(out, 0xee, sizeof(out));
                    void *r = reinterpret_cast<void *(__thiscall *)(void *, void *)>(fn)(self, out + 1);
                    for (uint32_t v : out) gW.events.push_back(v);
                    return uint32_t(r == out + 1);
                }
                case aSetKey: {
                    const uint32_t key[4] = {a1, a2, a1 ^ a2, ~a1};
                    return uint32_t(uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, const void *)>(fn)(self, key)));
                }
                case aDescribe:
                    return uint32_t(uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>(fn)(self, &S(kTarget))));
                default: // aDestruct
                    reinterpret_cast<void(__thiscall *)(void *)>(fn)(self);
                    return 0u;
                }
            }, true, 0);
        }
}

// The constructor hook's adapter is what the Construct test runs on our side (placement new of our class).

