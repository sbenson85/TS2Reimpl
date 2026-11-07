// Differential tests for cTSAudioEvent and nGZNumerics::AnyVector3 (lane IY, src/audio/cTSAudioEvent.h): the whole class in
// the standalone shape.
//
// AudioEvent_Run: an event allocated by the original operator new (0x210087) and built by the original constructor (0x20ff24,
// or the default one 0x20ff8d) against one allocated by ours and built by our adapters, each driven through its own vtables
// by the same random operations, then destroyed by the last Release (the deleting destructor 0x2101c5 through the set face's
// thunk 0x21019d, the destructor 0x21002f, operator delete 0x2100f1 back to the pool). The block comes from the event pool
// (0x10cc3e8, real 4 KB pages, a fake vtable whose NewPage hands out a prepared page and whose FreePage records) under its
// lock (0x10cc3f4, a fake whose Lock / Unlock record). Operations, on every face by slot number: QueryInterface (0x21071b;
// thunks 0x21078c / 0x210794), AddRef (0x2101f2; 0x2101fc / 0x210204), Release (0x21079c; 0x2107bc / 0x2107c4); the event
// face's IsEvent, GetEventId / SetEventId (0x2108e0), AsMessage, the Has slots (0x210544), GetFloat 0x21048e / SetFloat
// 0x21095d, GetUint32 0x210677 / SetUint32 0x210a99, GetSint32 0x210572 / SetSint32 0x2109ba, GetVector3 0x2106b0 / SetVector3
// 0x210ae1, GetText 0x210615 / SetText 0x2109f2, GetFirst 0x2102f0, GetNext 0x21070b, SetProperty 0x210ad1, GetPropertyValue
// 0x210b35, GetProperty 0x210b2d, HasProperty 0x2106fb, the user word (SetUserWord 0x2109b0); the set face's thunks 0x210ad9,
// 0x2102f8, 0x210713, 0x210b3d, 0x210b45, 0x210703 and its own Remove / Clear / GetCount; the message face's Create (shared_vf 0x1474b0), GetType /
// SetType, IsEqual 0x21015d, IsLess 0x21017d, the four data words (Has 0x2104c7 / 0x2104e0 / 0x2104f9 / 0x210512, Get
// 0x210338 / 0x21037a / 0x2103bc / 0x2103fe, Set 0x2107cc / 0x21080f / 0x210852 / 0x210895), the string (HasString
// 0x210559, GetString 0x2105ab, SetString 0x210a3e), the counted object (Has, shared_vf 0x210551; Get / Set 0x210999) and the extra object
// (HasExtra 0x21052b, GetExtra 0x210440, SetExtra 0x2108ea). Keys include the message face's, so the two views meet; the
// values hold every type (vectors as the set's AnyAny clones, 0x21020c), so the typed getters also miss on the wrong type.
// Compared after every operation: the result, the event log (fakes, lock), every field (the never-written +0x24 too: both
// blocks start as 0xc5), the properties by content (key, value type, payload, string text, vector floats), the cursor as an
// index, the pool's rings and pages.
//
// AudioEvent_Allocator: operator new (0x210087) and operator delete (0x2100f1) alone, for the pool size and others (the heap),
// with null, through a pool whose current page has 0..3 free blocks (so the allocation takes the next page).
//
// AnyVector3_Run: the value class's slots (0x2101a5 destructor, 0x210285 Assign, 0x210232 Compare with NaN and equal and mixed
// components, 0x21020c Clone, 0x210b21 GetType, 0x210b27 GetTypeName) on objects with the game's vtable against ours.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "gz/cGZPropertySet.h"

#include "audio/cTSAudioEvent.h"
#include "game/game.h"
#include "rz/RZFixedAllocator.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace audioevent_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {1, "dd"}, {2, "dd"}, {3, "dd"}, {4, "dd"}, {5, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Event = cTSAudioEvent;

constexpr uint32_t kTag = 0x7e000000;  // the event under test
constexpr uint32_t kPageTag = 0x7d000000; // the pool's pages
constexpr uint32_t kFaceCode = 0x0fac0000;

// The game's vtables of the event's faces and of the values (kept here: our code must not hold them, tools/callers.py).
constexpr uint32_t kEventVTables[3] = {0xda8200, 0xda827c, 0xda8314};
constexpr uint32_t kVector3VTable = 0xda8354;
constexpr uint32_t kAnyVTable = 0xd9c654;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *At(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

// ---- stand-ins for the game's heap ----
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

// ---- the pool and its lock ----
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kBlocks = 78; // 0x34-byte blocks after the 16-byte header
uint8_t *gPages; // 3 pages: 0 current, 1 handed out by NewPage, 2 spare
RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(gPages + kPageSize * uint32_t(i)); }
uint8_t *Block(int page, uint32_t k) { return gPages + kPageSize * uint32_t(page) + 16 + k * 0x34; }

void ChainPage(int p, uint32_t freeBlocks)
{
    RZFixedPage *page = Page(p);
    uint8_t *first = nullptr;
    for (uint32_t k = freeBlocks; k-- > 0;) {
        *reinterpret_cast<uint8_t **>(Block(p, k)) = first;
        first = Block(p, k);
    }
    page->freeList = first;
    page->used = kBlocks - freeBlocks;
    page->next = page->prev = page;
}

bool gNewPage; // the pool built page 1 (PoolNewPage): until then its header is the test's random fill
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    Ev(0x77000002);
    gNewPage = true;
    ChainPage(1, kBlocks);
    Page(1)->used = 0;
    return Page(1);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *page)
{
    Ev(0x77000003);
    Ev(svcfx::Norm(P(page)));
}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};

int32_t __fastcall LockLock(void *self, void *) { Ev(0x77000011); Ev(P(self) == P(GameAddress<void>(Event::kPoolLock))); return 1; }
int32_t __fastcall LockUnlock(void *self, void *) { Ev(0x77000012); Ev(P(self) == P(GameAddress<void>(Event::kPoolLock))); return 0; }
void *gLockVT[6] = {nullptr, reinterpret_cast<void *>(&LockLock), reinterpret_cast<void *>(&LockUnlock), nullptr, nullptr, nullptr};

void BuildPool(uint32_t seed, uint32_t freeBlocks)
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPageSize * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::mt19937 r(seed * 0x9e3779b1u + 5);
    for (uint32_t i = 0; i < kPageSize * 3; ++i) gPages[i] = uint8_t(r());
    gNewPage = false;
    ChainPage(0, freeBlocks);
    uint32_t *pool = GameAddress<uint32_t>(Event::kPool);
    pool[0] = P(gPoolVT);
    pool[1] = P(Page(0));
    pool[2] = 0;
    *GameAddress<uint32_t>(Event::kPoolLock) = P(gLockVT);
}

void DumpPool(Blob &b)
{
    const uint32_t *pool = GameAddress<uint32_t>(Event::kPool);
    b.push_back(svcfx::Norm(pool[1]));
    b.push_back(svcfx::Norm(pool[2]));
    for (int p = 0; p < 2; ++p) {
        const RZFixedPage *page = Page(p);
        if (p == 1 && !gNewPage) { // the fill, as data (the same on both sides)
            b.insert(b.end(), {P(page->next), P(page->prev), P(page->freeList), page->used});
            continue;
        }
        b.push_back(svcfx::Norm(P(page->next)));
        b.push_back(svcfx::Norm(P(page->prev)));
        b.push_back(svcfx::Norm(P(page->freeList)));
        b.push_back(page->used);
    }
}

// ---- texts ----
std::vector<cRZString *> gTexts; // every string the test made (released at the end of a side)
cRZString *NewText(const char *text)
{
    auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    s->Construct(text);
    Slot<uint32_t>(s, 1);
    gTexts.push_back(s);
    return s;
}
void FreeTexts()
{
    for (cRZString *s : gTexts) Slot<uint32_t>(s, 2);
    gTexts.clear();
}
// A string's text (slot 5) as words: its length, then a hash.
void DumpText(Blob &b, const void *text)
{
    if (!text) {
        b.push_back(0xdead0000);
        return;
    }
    const char *chars = Slot<const char *>(const_cast<void *>(text), 5);
    uint32_t h = 2166136261u;
    for (const char *c = chars; *c; ++c) h = (h ^ uint8_t(*c)) * 16777619u;
    b.push_back(uint32_t(std::strlen(chars)));
    b.push_back(h);
}

// ---- dumps ----
// A cIGZAny by content: its type (slot 4), then the payload by type.
void DumpAny(Blob &b, void *any)
{
    if (!any) {
        b.push_back(0xdead0001);
        return;
    }
    const uint32_t type = Slot<uint32_t>(any, 4);
    b.push_back(type);
    if (type == gzany::kTypeString) {
        DumpText(b, reinterpret_cast<void *>(uintptr_t(Word(any, 4))));
    } else if (type == nGZNumerics::AnyVector3::kType) {
        for (uint32_t i = 0; i < 3; ++i) b.push_back(Word(any, 4 + 4 * i));
    } else if (type == gzany::kTypeBoolean) {
        b.push_back(Word(any, 4) & 0xff);
    } else if (type == gzany::kTypeUint32 || type == gzany::kTypeSint32 || type == gzany::kTypeFloat32) {
        b.push_back(Word(any, 4)); // numbers: data
    } else {
        b.push_back(svcfx::Norm(Word(any, 4))); // a string set, a wrapped clone: a pointer
    }
}

const uint32_t *OurVTables()
{
    static uint32_t words[3];
    static bool done = false;
    if (!done) {
        alignas(8) static unsigned char buffer[sizeof(Event)];
        auto *e = ::new (buffer) Event(0);
        for (int f = 0; f < 3; ++f) words[f] = Word(e, 4 * f);
        e->~Event();
        done = true;
    }
    return words;
}

uint32_t VtCode(uint32_t word, int f, bool ours)
{
    const uint32_t expected = ours ? OurVTables()[f] : P(GameAddress<void>(kEventVTables[f]));
    return word == expected ? kFaceCode | uint32_t(f) : word;
}

// The object's words as Dump logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] = "-------- ddddp";
void Dump(Blob &b, void *object, bool ours)
{
    for (int f = 0; f < 3; ++f) b.push_back(VtCode(Word(object, 4 * f), f, ours));
    for (uint32_t off = 0xc; off < 0x34; off += 4) {
        if (off >= 0x10 && off < 0x1c) continue; // the entries' vector, by content below
        if (off == 0x1c) continue;                // the cursor, as an index below
        svcfx::Field(b, Word(object, off), kObjectKinds, off);
    }
    const uint8_t *begin = reinterpret_cast<const uint8_t *>(uintptr_t(Word(object, 0x10)));
    const uint8_t *end = reinterpret_cast<const uint8_t *>(uintptr_t(Word(object, 0x14)));
    const uint8_t *cursor = reinterpret_cast<const uint8_t *>(uintptr_t(Word(object, 0x1c)));
    b.push_back(uint32_t((end - begin) / 16));
    // (A cursor left in storage the vector has since moved from is the set's own dangling pointer: marked, not compared.)
    b.push_back(!cursor ? 0xffffffffu : cursor >= begin && cursor <= end ? uint32_t((cursor - begin) / 16) : 0xeeeeeeeeu);
    for (const uint8_t *e = begin; e != end; e += 16) {
        b.push_back(Word(e, 0));
        const uint32_t vt = Word(e, 4);
        b.push_back(vt);
        if (vt == P(GameAddress<void>(gzany::kAnyAnyVTable))) DumpAny(b, reinterpret_cast<void *>(uintptr_t(Word(e, 8))));
        else DumpAny(b, const_cast<uint8_t *>(e + 4));
    }
}

// ---- the operations ----
constexpr uint32_t kKeys[] = {Event::kData1Key, Event::kData2Key, Event::kData3Key, Event::kData4Key, Event::kStringKey,
                              Event::kExtraKey, 1, 7, 0x100, 0xffffffff};
constexpr uint32_t kIids[] = {1, 0x0bc7957d, 0x5b523ba4, 0x65297976, 0xa52294b4, 0xebb10cdd, 0x12345678, 0};
constexpr uint32_t kStringIids[] = {1, 0x12345678};
const char *const kWords[] = {"", "a", "Sound", "music_tension", "0123456789abcdef0123456789abcdef"};

enum Kind : uint32_t { kNone, kU32, kFloat, kSint, kBool, kVec, kString, kExtra };
std::map<uint32_t, Kind> gKinds; // what each key holds (the same on both sides: the operations are)
float gFloats[] = {0.0f, 1.5f, -2.25f, 1e30f, -0.0f};

uint32_t QI(Obj *self, uint32_t *a)
{
    if (a[0] != 1 && a[0] != self->value[0]) return 0;
    if (self->value[1]) return 0; // refuses everything
    ++self->refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self;
    return 1;
}

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    std::mt19937 r(seed + 99);
    for (unsigned i = 0; i < 6; ++i) {
        Obj &o = F(i);
        o.value[0] = 0x4000 + i;
        o.value[1] = r() % 5 == 0;
        o.Set(0, "dp", 0, &QI);
        o.Set(4, 0, r() % 2 ? Event::kMessageType : uint32_t(r())); // a message type (sometimes the event's)
    }
    F(2).ret[4] = Event::kMessageType;
}

// A set on a key that holds another type changes nothing (the stored value's Assign refuses it).
void NoteKey(uint32_t key, Kind kind)
{
    auto it = gKinds.find(key);
    const auto cls = [](Kind k) { return k == kExtra ? kU32 : k; };
    if (it == gKinds.end()) gKinds[key] = kind;
    else if (cls(it->second) == cls(kind)) it->second = kind;
}
Kind KindOf(uint32_t key)
{
    auto it = gKinds.find(key);
    return it == gKinds.end() ? kNone : it->second;
}

void RunOp(Blob &b, void *object, uint32_t kind)
{
    void *const ev = object;
    void *const msg = At(object, 4);
    void *const set = At(object, 8);
    void *const faces[3] = {ev, msg, set};
    const uint32_t key = kKeys[Draw(std::size(kKeys))];
    b.push_back(kind);
    b.push_back(key);
    if (svcfx::gTrace) fprintf(stderr, "  key %08x extra kind %u\n", key, unsigned(KindOf(Event::kExtraKey)));
    switch (kind) {
    case 0: { // QueryInterface on any face
        void *out = reinterpret_cast<void *>(0x1234);
        const uint32_t iid = kIids[Draw(std::size(kIids))];
        const bool ok = Slot<bool>(faces[Draw(3)], 0, iid, &out);
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(out), 0x1234));
        if (ok) b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    case 1: { // AddRef then Release on any face
        b.push_back(Slot<uint32_t>(faces[Draw(3)], 1));
        b.push_back(Slot<uint32_t>(faces[Draw(3)], 2));
        break;
    }
    case 2: b.push_back(Slot<bool>(ev, 3)); b.push_back(Slot<uint32_t>(ev, 4)); break;
    case 3: b.push_back(Slot<uint32_t>(ev, 5, gRng())); b.push_back(Slot<uint32_t>(ev, 4)); break;
    case 4: b.push_back(svcfx::Norm(P(Slot<void *>(ev, 6)))); break;
    case 5: b.push_back(Slot<bool>(ev, 7 + 3 * Draw(5), key)); break; // the Has slots 7 / 10 / 13 / 16 / 19
    case 6: { // GetFloat
        const float f = Slot<float>(ev, 8, key);
        uint32_t w;
        std::memcpy(&w, &f, 4);
        b.push_back(w);
        break;
    }
    case 7: Slot<void>(ev, 9, key, gFloats[Draw(5)]); NoteKey(key, kFloat); break;
    case 8: b.push_back(Slot<uint32_t>(ev, 11, key)); break;
    case 9: {
        const uint32_t v = Draw(3) ? gRng() : P(&F(Draw(6)));
        Slot<void>(ev, 12, key, v);
        NoteKey(key, key == Event::kExtraKey && v >= P(&F(0)) && v < P(&F(6)) ? kExtra : kU32);
        break;
    }
    case 10: b.push_back(uint32_t(Slot<int32_t>(ev, 14, key))); break;
    case 11: Slot<void>(ev, 15, key, int32_t(gRng())); NoteKey(key, kSint); break;
    case 12: { // GetVector3
        float out[3] = {7.0f, 8.0f, 9.0f};
        b.push_back(Slot<bool>(ev, 17, key, out));
        for (float f : out) {
            uint32_t w;
            std::memcpy(&w, &f, 4);
            b.push_back(w);
        }
        break;
    }
    case 13: {
        const float v[3] = {gFloats[Draw(5)], gFloats[Draw(5)], float(Draw(100))};
        Slot<void>(ev, 18, key, v);
        NoteKey(key, kVec);
        break;
    }
    case 14: { // GetText into a string of ours
        cRZString *out = NewText("previous");
        b.push_back(Slot<bool>(ev, 20, key, out));
        DumpText(b, out);
        break;
    }
    case 15: Slot<void>(ev, 21, key, NewText(kWords[Draw(5)])); NoteKey(key, kString); break;
    case 16: { // GetFirst / GetNext (event face or the set face's thunks) over every entry
        const bool viaSet = Draw(2);
        uint32_t k = 0xabababab;
        void *value = nullptr;
        bool ok = Slot<bool>(viaSet ? set : ev, viaSet ? 6 : 22, &k, &value);
        for (int n = 0; n < 12; ++n) {
            b.push_back(ok);
            b.push_back(k);
            if (ok) DumpAny(b, value);
            if (!ok) break;
            ok = Slot<bool>(viaSet ? set : ev, viaSet ? 7 : 23, &k, &value);
        }
        break;
    }
    case 17: { // SetProperty through either face, a uint32 or a boolean value
        const bool viaSet = Draw(2);
        AnyUint32 u{GameAddress<const void>(gzany::kUint32VTable), gRng()};
        AnyBoolean bo{GameAddress<const void>(gzany::kBooleanVTable), uint8_t(gRng())};
        const bool boolean = Draw(2);
        b.push_back(Slot<bool>(viaSet ? set : ev, viaSet ? 3 : 24, &key, boolean ? static_cast<void *>(&bo) : &u));
        NoteKey(key, boolean ? kBool : kU32);
        break;
    }
    case 18: { // GetPropertyValue into a uint32 (either face)
        const bool viaSet = Draw(2);
        AnyUint32 u{GameAddress<const void>(gzany::kUint32VTable), 0x5555};
        b.push_back(Slot<bool>(viaSet ? set : ev, viaSet ? 9 : 25, &key, &u));
        b.push_back(u.mValue);
        break;
    }
    case 19: { // GetProperty / HasProperty (either face)
        const bool viaSet = Draw(2);
        DumpAny(b, Slot<void *>(viaSet ? set : ev, viaSet ? 8 : 26, &key));
        b.push_back(Slot<bool>(viaSet ? set : ev, viaSet ? 10 : 27, &key));
        break;
    }
    case 20: b.push_back(Slot<uint32_t>(ev, 29, gRng())); b.push_back(Slot<uint32_t>(ev, 28)); break;
    case 21: { // the set's own Remove / Clear / Count
        const uint32_t which = Draw(6);
        if (which == 0) {
            Slot<void>(set, 5);
            gKinds.clear();
        } else {
            b.push_back(Slot<bool>(set, 4, &key));
            gKinds.erase(key);
        }
        b.push_back(Slot<uint32_t>(set, 11));
        break;
    }
    // ---- the message face ----
    case 22: {
        void *out = reinterpret_cast<void *>(0x1234);
        b.push_back(Slot<bool>(msg, 3, gRng(), &out));
        b.push_back(P(out));
        b.push_back(Slot<uint32_t>(msg, 4));
        break;
    }
    case 23: {
        const uint32_t types[] = {Event::kMessageType, 0, 0x4000, 0xffffffff, gRng()};
        b.push_back(Slot<uint32_t>(msg, 5, types[Draw(5)]));
        b.push_back(Slot<uint32_t>(msg, 4));
        break;
    }
    case 24: { // IsEqual / IsLess against a fake or itself
        void *other = Draw(4) ? static_cast<void *>(&F(Draw(6))) : msg;
        b.push_back(Slot<bool>(msg, 6 + Draw(2), other));
        break;
    }
    case 25: { // a data word: Has, Get (uint or void), Set (uint or void)
        const unsigned word = Draw(4);
        const unsigned base = 8 + 5 * word;
        const uint32_t dataKey = kKeys[word];
        switch (Draw(3)) {
        case 0: b.push_back(Slot<bool>(msg, base)); break;
        case 1: svcfx::FakeOrData(b, Slot<uint32_t>(msg, base + 1 + Draw(2))); break; // as Set it: a fake or data
        default: {
            const uint32_t v = Draw(3) ? gRng() : P(&F(Draw(6)));
            Slot<void>(msg, base + 3 + Draw(2), v);
            NoteKey(dataKey, kU32);
            break;
        }
        }
        break;
    }
    case 26: b.push_back(Slot<bool>(msg, 28)); break;
    case 27: { // GetString: only with a string under the key (the original calls through null otherwise)
        if (KindOf(Event::kStringKey) != kString) break;
        void *out = reinterpret_cast<void *>(0x1234);
        const bool ok = Slot<bool>(msg, 29, kStringIids[Draw(2)], &out);
        b.push_back(ok);
        if (ok) {
            DumpText(b, out);
            b.push_back(Slot<uint32_t>(out, 2) > 0);
        }
        break;
    }
    case 28: {
        const bool none = Draw(4) == 0;
        Slot<void>(msg, 30, none ? nullptr : NewText(kWords[Draw(5)]));
        if (!none) NoteKey(Event::kStringKey, kString);
        break;
    }
    case 29: b.push_back(Slot<bool>(msg, 31)); b.push_back(svcfx::Norm(P(Slot<void *>(msg, 32)))); break;
    case 30: Slot<void>(msg, 33, Draw(3) ? static_cast<void *>(&F(Draw(6))) : nullptr); break;
    case 31: b.push_back(Slot<bool>(msg, 34)); break;
    case 32: { // GetExtra: only with an object's address under the key (the original calls through it)
        if (KindOf(Event::kExtraKey) != kExtra) break;
        void *out = reinterpret_cast<void *>(0x1234);
        const bool ok = Slot<bool>(msg, 35, Draw(2) ? 1u : 0x4000u + Draw(6), &out);
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(out), 0x1234));
        if (ok) b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    case 33: {
        Obj &o = F(Draw(6));
        Slot<void>(msg, 36, &o);
        if (!o.value[1]) NoteKey(Event::kExtraKey, kExtra);
        break;
    }
    }
}
constexpr uint32_t kKinds = 34;

void *Build(difftest::Context &ctx, bool ours, bool byDefault, uint32_t id)
{
    void *memory = ours ? Event::operator new(Event::kSize) : ctx.Original<void *(__cdecl *)(size_t)>(0x210087)(Event::kSize);
    std::memset(memory, 0xc5, Event::kSize);
    if (ours) {
        if (byDefault) cTSAudioEventAdapters::ConstructDefault(memory, nullptr);
        else cTSAudioEventAdapters::Construct(memory, nullptr, id);
    } else if (byDefault) {
        ctx.Original<void *(__thiscall *)(void *)>(0x20ff8d)(memory);
    } else {
        ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x20ff24)(memory, id);
    }
    return memory;
}

} // namespace audioevent_test

using namespace audioevent_test;

DIFF_TEST(AudioEvent_Run, 0x20ff24)
{
    // Also reached (see the file comment): 0x20ff8d, 0x210087, 0x2100f1, 0x21002f, 0x2101c5, 0x21019d, 0x21071b, 0x21078c,
    // 0x210794, 0x2101f2, 0x2101fc, 0x210204, 0x21079c, 0x2107bc, 0x2107c4, 0x2108e0, 0x210544, 0x21048e, 0x21095d, 0x210677,
    // 0x210a99, 0x210572, 0x2109ba, 0x2106b0, 0x210ae1, 0x210615, 0x2109f2, 0x2102f0, 0x21070b, 0x210ad1, 0x210b35, 0x210b2d,
    // 0x2106fb, 0x2109b0, 0x210ad9, 0x2102f8, 0x210713, 0x210b3d, 0x210b45, 0x210703, 0x21015d, 0x21017d, 0x2104c7, 0x2104e0,
    // 0x2104f9, 0x210512, 0x210338, 0x21037a, 0x2103bc, 0x2103fe, 0x2107cc, 0x21080f, 0x210852, 0x210895, 0x210559, 0x2105ab,
    // 0x210a3e, 0x210999, 0x21052b, 0x210440, 0x2108ea, 0x21020c, 0x2101a5, 0x210285.
    svcfx::gTrace = std::getenv("TS2_TEST_TRACE") != nullptr;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.SetWord(Event::kPool, 0);
    globals.SetWord(Event::kPool + 4, 0);
    globals.SetWord(Event::kPool + 8, 0);
    globals.SetWord(Event::kPoolLock, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch gameFree(0x82540f, shape::CodeOf(&GameFree));

    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 plan(seed);
        std::vector<uint32_t> ops(10 + plan() % 50);
        for (uint32_t &k : ops) k = plan() % kKinds;
        const bool byDefault = seed % 5 == 0;
        const uint32_t id = plan();
        const uint32_t freeBlocks = 1 + plan() % 4;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            BuildPool(seed, freeBlocks);
            svcfx::AddRange(gPages, kPageSize * 3, kPageTag);
            gKinds.clear();
            gRng.seed(seed * 7 + 1);
            void *object = Build(ctx, side == 1, byDefault, id);
            svcfx::AddRange(object, Event::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i]);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                    DumpPool(b);
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            FreeTexts();
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioEvent_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i] : 99u);
                return;
            }
        }
    }
}

DIFF_TEST(AudioEvent_Allocator, 0x210087)
{
    // Also reached: 0x2100f1 (operator delete).
    svcfx::Globals globals;
    globals.SetWord(Event::kPool, 0);
    globals.SetWord(Event::kPool + 4, 0);
    globals.SetWord(Event::kPool + 8, 0);
    globals.SetWord(Event::kPoolLock, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameFree(0x82540f, shape::CodeOf(&GameFree));
    constexpr size_t kSizes[] = {0x34, 0x34, 0x33, 0x35, 0x10, 0x68, 0};
    for (uint32_t seed = 1; seed <= 500; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed);
            svcfx::ResetAll();
            BuildPool(seed, r() % 4);
            svcfx::AddRange(gPages, kPageSize * 3, kPageTag);
            Blob &b = results[side];
            std::vector<std::pair<void *, size_t>> live;
            for (int n = 0; n < 12; ++n) {
                if (live.empty() || r() % 3) {
                    const size_t size = kSizes[r() % std::size(kSizes)];
                    void *p = side ? Event::operator new(size) : ctx.Original<void *(__cdecl *)(size_t)>(0x210087)(size);
                    const uint32_t code = svcfx::Norm(P(p));
                    b.push_back((code & 0xff000000u) == kPageTag ? code : 0x11111111u); // a pool block, or the heap
                    live.push_back({p, size});
                } else {
                    const size_t at = r() % live.size();
                    auto [p, size] = live[at];
                    live.erase(live.begin() + ptrdiff_t(at));
                    if (side) Event::operator delete(p, size);
                    else ctx.Original<void(__cdecl *)(void *, size_t)>(0x2100f1)(p, size);
                }
                if (r() % 4 == 0) { // null
                    if (side) Event::operator delete(nullptr, 0x34);
                    else ctx.Original<void(__cdecl *)(void *, size_t)>(0x2100f1)(nullptr, 0x34);
                }
                DumpPool(b);
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
            }
            for (auto [p, size] : live)
                if (size != 0x34) ::operator delete(p);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "AudioEvent_Allocator", seed)) return;
    }
}

namespace audioevent_test {

// An AnyVector3 with the game's vtable (the game has no constructor for it: its users build it inline).
struct GameVector3 {
    uint32_t vt;
    float v[3];
};

void *MakeVector(bool ours, const float *v)
{
    if (ours) return new nGZNumerics::AnyVector3(v);
    auto *g = static_cast<GameVector3 *>(::operator new(sizeof(GameVector3)));
    g->vt = P(GameAddress<void>(kVector3VTable));
    std::memcpy(g->v, v, 12);
    return g;
}

uint32_t VectorVt(const void *object, bool ours)
{
    const uint32_t word = Word(object, 0);
    static const uint32_t ourVt = [] {
        nGZNumerics::AnyVector3 probe;
        return Word(&probe, 0);
    }();
    if (word == (ours ? ourVt : P(GameAddress<void>(kVector3VTable)))) return 0x0fac0001;
    if (!ours && word == P(GameAddress<void>(kAnyVTable))) return 0x0fac0002;
    return word;
}

} // namespace audioevent_test

DIFF_TEST(AnyVector3_Run, 0x210232)
{
    // Also reached: 0x2101a5 (the destructor, flags 0 and 1), 0x210285 (Assign), 0x21020c (Clone), 0x210b21 (GetType),
    // 0x210b27 (GetTypeName).
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    const float nan = std::nanf("");
    const float pool[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.5f, nan, 1e-30f, 3.0f};
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            const bool ours = side == 1;
            std::mt19937 r(seed);
            Blob &b = results[side];
            float a[3], c[3];
            for (float &f : a) f = pool[r() % 8];
            for (int i = 0; i < 3; ++i) c[i] = r() % 2 ? a[i] : pool[r() % 8];
            void *x = MakeVector(ours, a);
            void *y = MakeVector(ours, c);
            AnyFloat32 other{GameAddress<const void>(gzany::kFloat32VTable), 4.0f};
            for (int n = 0; n < 6; ++n) {
                switch (r() % 6) {
                case 0: b.push_back(uint32_t(Slot<int32_t>(x, 2, y))); b.push_back(uint32_t(Slot<int32_t>(y, 2, x))); break;
                case 1: b.push_back(Slot<bool>(x, 1, r() % 3 ? y : static_cast<void *>(&other))); break;
                case 2: {
                    void *clone = Slot<void *>(x, 3);
                    b.push_back(VectorVt(clone, ours));
                    for (uint32_t i = 1; i < 4; ++i) b.push_back(Word(clone, 4 * i));
                    Slot<void *>(clone, 0, 1u);
                    break;
                }
                case 3: b.push_back(Slot<uint32_t>(x, 4)); break;
                case 4: b.push_back(uint32_t(std::strcmp(Slot<const char *>(x, 5), "AnyVector3"))); break;
                default: {
                    for (float &f : c) f = pool[r() % 8];
                    std::memcpy(At(y, 4), c, 12);
                    break;
                }
                }
                for (uint32_t i = 0; i < 4; ++i) b.push_back(i ? Word(x, 4 * i) : VectorVt(x, ours));
            }
            // The destructor without delete (vtable reset to the base's), then with.
            Slot<void *>(x, 0, 0u);
            const uint32_t code = VectorVt(x, ours);
            b.push_back(ours && code != 0x0fac0001 ? 0x0fac0002 : code);
            ::operator delete(x);
            Slot<void *>(y, 0, 1u);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "AnyVector3_Run", seed)) return;
    }
}
