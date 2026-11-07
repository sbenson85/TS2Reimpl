// Differential tests for the property-set core (src/gz/cGZPropertySet.size.cpp, lane DP).
//
// Covered, each run on the original by RVA and on ours:
//   entries     0x1bec9b Construct, 0x1bee0a Value, 0x1bf210 Destroy
//   uint32 set  0x1bec5f Construct, 0x1bed39 Destruct, 0x1bedac DeletingDestructor, 0x1bf44d QueryInterface,
//               0x1bf485 Release, 0x1bf49c SetProperty, 0x1bee49 RemoveProperty, 0x1bee23 Clear, 0x1bf239 GetFirst,
//               0x1bf389 GetNext, 0x1bf5e9 GetProperty, 0x1bf626 GetPropertyValue, 0x1bf2cc HasProperty,
//               0x1bf41f GetCount, 0x1bf148 CopyFrom, 0x1bf336 KeyToString, 0x1bf349 FormatKey
//   string set  0x1bf515 SetProperty, 0x1bee9c RemoveProperty, 0x1bf26d GetFirst, 0x1bf3c0 GetNext, 0x1bf670
//               GetProperty, 0x1bf69a GetPropertyValue, 0x1bf306 HasProperty, 0x1bf36a KeyToString, 0x1bf0d1 CopyFrom,
//               0x1bf429 QueryInterface, 0x1bf191 InternKey, 0x1befff FindKey, 0x1777e0 Destruct, 0x177a70
//               DeletingDestructor, 0x1bed22 DestroyEntries, 0x1bed8d DeletingDestructorBase, 0x22b157 Construct
//
// Standalone shape (lane EQ): ours runs two ways, by seed parity: our constructor and every slot through our vtable, or the
// hook adapters on a set built by the original's constructor whose vtable is a trap copy (slots with adapters trapped). Vtable
// words are compared as "the side's own" (see VtClass below); nothing else is normalised.
//
// Method: two worlds (A runs the original, B ours), each with its own fake text objects (counted cIGZString stand-ins
// held by AnyStrings) and its own log of what the test Any type (an unknown type id, so the set wraps it in AnyAny)
// was asked to do. Random sequences of operations over small key pools (duplicates, case variants, the empty key, a
// key with an embedded NUL, keys long enough to leave the small-string buffer) run on both; after every operation the
// results, the whole set (entries, capacity, cursor, key text), the fake texts' reference counts and the logs are
// compared. Values: every built-in type (AnyUint32, AnySint32, AnyFloat32, AnyBoolean, AnyString) plus the test type
// (cloned into AnyAny; one value clones to null).
//
// Left out of the comparison, with the reason:
// - an entry's last dword and a Boolean entry's upper three payload bytes: SetProperty (0x1bf4e9-0x1bf502) builds the
//   entry in an uninitialised stack slot ([esp+0x18], 16 bytes) and 0x1bec9b never writes +0xc, nor more than the
//   low payload byte of a Boolean (0x1becf4), so they hold whatever the stack did. The direct entry test pre-fills
//   both entries with the same bytes and compares all 16.
// - the cursor after an operation that moves or removes entries, until the next GetFirst: it may point into a freed
//   buffer (heap addresses differ between the worlds). GetNext is only called while the cursor is inside the entries:
//   past the end it reads beyond the vector (ORIGINAL-BUG in the source).
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h" // AllocatorScope

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPropertySet.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Blob32 = std::vector<uint32_t>;

constexpr uint32_t kCustomType = 0x5eed0a11;
constexpr uint32_t kNullCloneValue = 0xdead; // a test Any with this value clones to null
constexpr uint32_t kStrings = 4;

struct World;

// ---- fakes ---------------------------------------------------------------------------------------------------

// The text object an AnyString holds: only AddRef / Release are used by the Any code the set reaches.
struct FakeText {
    const void *vt;
    uint32_t refs;
    uint32_t id;
};
uint32_t __fastcall FtQI(FakeText *, void *, uint32_t, void **) { return 0; }
uint32_t __fastcall FtAddRef(FakeText *self, void *) { return ++self->refs; }
uint32_t __fastcall FtRelease(FakeText *self, void *) { return --self->refs; }
void *gFakeTextVT[3] = {reinterpret_cast<void *>(&FtQI), reinterpret_cast<void *>(&FtAddRef), reinterpret_cast<void *>(&FtRelease)};

// The test Any (vtable slots 0-5 like every cIGZAny), an unknown type to the set.
struct CustomAny {
    const void *vt;
    uint32_t value;
    uint32_t serial;
    World *world;
};

struct World {
    FakeText texts[kStrings];
    Blob32 log;
    uint32_t nextSerial = 1;
    std::vector<CustomAny *> made;
};

uint32_t TypeOf(void *any) { return VirtualSlot<uint32_t(__thiscall *)(void *)>(any, 4)(any); }

void *__fastcall CaDelete(CustomAny *self, void *, uint32_t flags)
{
    self->world->log.insert(self->world->log.end(), {1u, self->serial, flags});
    if (flags & 1) {
        for (auto &m : self->world->made)
            if (m == self) m = nullptr;
        delete self;
    }
    return self;
}
uint32_t __fastcall CaAssign(CustomAny *self, void *, void *source)
{
    const uint32_t type = TypeOf(source);
    const bool same = type == kCustomType;
    self->world->log.insert(self->world->log.end(), {2u, self->serial, same ? static_cast<CustomAny *>(source)->value : type});
    if (same) self->value = static_cast<CustomAny *>(source)->value;
    return same ? 0x101 : 0x100; // al is the result; the upper bytes are noise both sides return alike
}
int32_t __fastcall CaCompare(CustomAny *, void *, void *) { return 0; }
void *CaCloneOf(CustomAny *self);
void *__fastcall CaClone(CustomAny *self, void *) { return CaCloneOf(self); }
uint32_t __fastcall CaType(CustomAny *, void *) { return kCustomType; }
const char *__fastcall CaName(CustomAny *, void *) { return "custom"; }
void *gCustomVT[6] = {reinterpret_cast<void *>(&CaDelete), reinterpret_cast<void *>(&CaAssign), reinterpret_cast<void *>(&CaCompare),
                      reinterpret_cast<void *>(&CaClone),  reinterpret_cast<void *>(&CaType),   reinterpret_cast<void *>(&CaName)};

void *CaCloneOf(CustomAny *self)
{
    World &w = *self->world;
    if (self->value == kNullCloneValue) {
        w.log.insert(w.log.end(), {3u, self->serial, 0u});
        return nullptr;
    }
    auto *copy = new CustomAny{gCustomVT, self->value, w.nextSerial++, &w};
    w.made.push_back(copy);
    w.log.insert(w.log.end(), {3u, self->serial, copy->serial});
    return copy;
}

// A cIGZString stand-in for KeyToString: slots 4 FromChar, 5 ToChar, 31 Sprintf.
struct FakeOut {
    const void *vt;
    char text[64];
    uint32_t calls;
};
void __fastcall FoFromChar(FakeOut *self, void *, const char *text)
{
    std::snprintf(self->text, sizeof(self->text), "%s", text);
    self->calls = self->calls * 10 + 4;
}
const char *__fastcall FoToChar(FakeOut *self, void *)
{
    self->calls = self->calls * 10 + 5;
    return self->text;
}
void __cdecl FoSprintf(FakeOut *self, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(self->text, sizeof(self->text), format, args);
    va_end(args);
    self->calls = self->calls * 10 + 3;
}
void *gFakeOutVT[33] = {};
void InitFakeOut(FakeOut &out)
{
    gFakeOutVT[4] = reinterpret_cast<void *>(&FoFromChar);
    gFakeOutVT[5] = reinterpret_cast<void *>(&FoToChar);
    gFakeOutVT[31] = reinterpret_cast<void *>(&FoSprintf);
    out.vt = gFakeOutVT;
    std::memset(out.text, 0, sizeof(out.text));
    out.calls = 0;
}

// ---- values --------------------------------------------------------------------------------------------------

enum Kind { kU32, kS32, kF32, kBool, kStr, kCustom, kKinds };

// A value of the given kind in world w: 8-byte Any (built-in) or a CustomAny on the caller's storage.
struct Value {
    InlineAny any;
    CustomAny custom;
    void *Ptr(Kind kind) { return kind == kCustom ? static_cast<void *>(&custom) : static_cast<void *>(&any); }
};

void MakeValue(Value &v, Kind kind, uint32_t raw, World &w)
{
    std::memset(&v, 0, sizeof(v));
    switch (kind) {
    case kU32: v.any = {GameAddress(gzany::kUint32VTable), raw}; break;
    case kS32: v.any = {GameAddress(gzany::kSint32VTable), raw}; break;
    case kF32: v.any = {GameAddress(gzany::kFloat32VTable), raw}; break;
    case kBool: v.any = {GameAddress(gzany::kBooleanVTable), raw}; break; // upper bytes non-zero on purpose
    case kStr: v.any = {GameAddress(gzany::kStringVTable), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.texts[raw % kStrings]))}; break;
    default: v.custom = {gCustomVT, raw, 0x80000000u | (raw & 0xffff), &w}; break;
    }
}

// An out value after GetPropertyValue: its payload, a text object by index.
uint32_t NormOut(const Value &v, Kind kind, const World &w)
{
    if (kind != kStr) return v.any.mValue;
    for (uint32_t i = 0; i < kStrings; ++i)
        if (v.any.mValue == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.texts[i]))) return 0x5000 + i;
    return 0x5fff;
}

// What a cIGZAny pointer is, the same in both worlds: an entry of the set, a test Any (by serial) or a stack value.
uint32_t NormAny(const void *p, const gz::cGZPropertyTable &set, const World &w, const void *local)
{
    if (!p) return 0;
    if (p == local) return 0xa0000000;
    const auto *begin = reinterpret_cast<const uint8_t *>(set.mEntries.data());
    const auto *at = static_cast<const uint8_t *>(p);
    if (begin && at >= begin && at < begin + set.mEntries.size() * 16) return 0xb0000000 | static_cast<uint32_t>(at - begin);
    for (const CustomAny *m : w.made)
        if (m == p) return 0xc0000000 | m->serial;
    return 0xffffffff;
}

uint32_t NormPayload(const cGZPropertyEntry &e, const World &w)
{
    const uint32_t vt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e.mValue.mVTable));
    if (e.mValue.mVTable == GameAddress(gzany::kBooleanVTable)) return e.mValue.mValue & 0xff; // see the header comment
    if (e.mValue.mVTable == GameAddress(gzany::kStringVTable)) {
        for (uint32_t i = 0; i < kStrings; ++i)
            if (e.mValue.mValue == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.texts[i]))) return 0x5000 + i;
        return 0x5fff;
    }
    if (e.mValue.mVTable == GameAddress(gzany::kAnyAnyVTable)) {
        auto *held = reinterpret_cast<const CustomAny *>(static_cast<uintptr_t>(e.mValue.mValue));
        return held ? 0xc0000000 | held->serial : 0;
    }
    (void)vt;
    return e.mValue.mValue;
}

void SnapWorld(Blob32 &b, const World &w)
{
    for (const FakeText &t : w.texts) b.push_back(t.refs);
    b.push_back(static_cast<uint32_t>(w.log.size()));
    b.insert(b.end(), w.log.begin(), w.log.end());
    for (const CustomAny *m : w.made)
        if (m) b.insert(b.end(), {m->serial, m->value});
}

void SnapSet(Blob32 &b, const gz::cGZPropertyTable &s, uint32_t vtClass, const World &w, bool cursorValid)
{
    b.push_back(vtClass);
    b.push_back(s.mRefCount);
    b.push_back(static_cast<uint32_t>(s.mEntries.size()));
    b.push_back(static_cast<uint32_t>(s.mEntries.capacity()));
    if (cursorValid) b.push_back(s.mCursor ? static_cast<uint32_t>(s.mCursor - s.mEntries.data()) : 0xfffffff0);
    for (const cGZPropertyEntry &e : s.mEntries)
        b.insert(b.end(), {e.mKey, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e.mValue.mVTable)), NormPayload(e, w)});
}

void SnapText(Blob32 &b, const cGZPropertySetString &s)
{
    b.push_back(static_cast<uint32_t>(s.mKeyText.size()));
    b.push_back(static_cast<uint32_t>(s.mKeyText.capacity()));
    for (char c : s.mKeyText) b.push_back(static_cast<uint8_t>(c));
}

bool HasNullAnyAny(const gz::cGZPropertyTable &s)
{
    for (const cGZPropertyEntry &e : s.mEntries)
        if (e.mValue.mVTable == GameAddress(gzany::kAnyAnyVTable) && e.mValue.mValue == 0) return true;
    return false;
}

bool KeyIsNullAnyAny(const gz::cGZPropertyTable &s, uint32_t key)
{
    for (const cGZPropertyEntry &e : s.mEntries)
        if (e.mKey == key) return e.mValue.mVTable == GameAddress(gzany::kAnyAnyVTable) && e.mValue.mValue == 0;
    return false;
}

void FreeWorld(World &w)
{
    for (CustomAny *m : w.made) delete m;
    w.made.clear();
}

bool Same(difftest::Context &ctx, const Blob32 &a, const Blob32 &b, const char *what, uint32_t seed, int step)
{
    if (a == b) return true;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("seed %u step %d: %s differs at word %u (%08x vs %08x, sizes %u/%u)", seed, step, what, static_cast<unsigned>(i),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0, static_cast<unsigned>(a.size()), static_cast<unsigned>(b.size()));
    return false;
}

Kind PickKind(std::mt19937 &rng) { return static_cast<Kind>(rng() % kKinds); }
uint32_t PickRaw(std::mt19937 &rng, Kind kind)
{
    if (kind == kCustom) return rng() % 8 == 0 ? kNullCloneValue : rng() % 5;
    if (kind == kBool) return (rng() & 0xffffff00) | (rng() % 3);
    return rng() % 4 == 0 ? rng() : rng() % 5;
}

// ---- routes, vtable words, trap vtables ------------------------------------------------------------------------
//
// Ours runs two ways (seed parity): the vtable route builds the set with our constructor and calls every slot through our
// vtable; the adapter route builds it with the original's constructor (the game's vtable, as a set the game builds inline) and
// calls the hook adapters, with a trap copy of the game's vtable on the object: every slot that has an adapter is a trap (an
// adapter must run our body, never dispatch), the others (AddRef, Release, Clear, GetCount, the deleting destructor and, for the
// uint32 set, KeyToString) stay the game's, as the bodies call them. CopyFrom calls this set's SetProperty through the vtable,
// so it runs with a second copy whose slot 3 is the game's.
//
// Vtable words compared as a class: 1 for the side's own vtable (the original's 0xda3e24 / 0xd9f718, and 0xda3dc0, the string
// set's abstract RTTI base, which the original's 0x1bed22 stores: not a class of ours; ours: our vtable, or the trap copies),
// anything else raw.

enum Route { kVtableRoute, kAdapterRoute };

uint32_t gTrapHits = 0;
template <int Slot> uint32_t __fastcall Trap1(void *, void *, uint32_t)
{
    gTrapHits |= 1u << Slot;
    return 0;
}
template <int Slot> uint32_t __fastcall Trap2(void *, void *, uint32_t, uint32_t)
{
    gTrapHits |= 1u << Slot;
    return 0;
}

struct TrapTables {
    void *plain[15];
    void *copying[15]; // slot 3 the game's (CopyFrom calls it)
};

void BuildTraps(TrapTables &t, uint32_t gameVTable, bool stringSet)
{
    std::memcpy(t.plain, GameAddress<void *>(gameVTable), sizeof(t.plain));
    t.plain[0] = reinterpret_cast<void *>(&Trap2<0>);  // QueryInterface (riid, out)
    t.plain[3] = reinterpret_cast<void *>(&Trap2<3>);  // SetProperty (key, value)
    t.plain[4] = reinterpret_cast<void *>(&Trap1<4>);  // RemoveProperty (key)
    t.plain[6] = reinterpret_cast<void *>(&Trap2<6>);  // GetFirst (key, value)
    t.plain[7] = reinterpret_cast<void *>(&Trap2<7>);  // GetNext (key, value)
    t.plain[8] = reinterpret_cast<void *>(&Trap1<8>);  // GetProperty (key)
    t.plain[9] = reinterpret_cast<void *>(&Trap2<9>);  // GetPropertyValue (key, out)
    t.plain[10] = reinterpret_cast<void *>(&Trap1<10>); // HasProperty (key)
    t.plain[12] = reinterpret_cast<void *>(&Trap1<12>); // CopyFrom (other)
    if (stringSet) t.plain[14] = reinterpret_cast<void *>(&Trap2<14>); // KeyToString (offset, out)
    std::memcpy(t.copying, t.plain, sizeof(t.plain));
    t.copying[3] = GameAddress<void *>(gameVTable)[3];
}

const void *VtWord(const void *object) { return *static_cast<const void *const *>(object); }

uint32_t VtClass(const void *object, std::initializer_list<const void *> own)
{
    const void *word = VtWord(object);
    for (const void *o : own)
        if (word == o) return 1;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(word));
}

// Our vtables, read from objects our constructors built.
template <class Set> const void *OurVTable()
{
    alignas(8) static unsigned char buffer[sizeof(Set)];
    static const void *vt = [] {
        auto *s = ::new (buffer) Set;
        const void *word = VtWord(s);
        s->~Set();
        return word;
    }();
    return vt;
}

bool TrapsQuiet(difftest::Context &ctx, const char *what, uint32_t seed, int step)
{
    if (!gTrapHits) return true;
    ctx.Fail("seed %u step %d: %s dispatched through the vtable (trap slots %08x)", seed, step, what, gTrapHits);
    gTrapHits = 0;
    return false;
}

// ---- the uint32 set: original and ours behind one shape ------------------------------------------------------

using U32Set = cGZPropertySetUint32;
using U32Face = cIGZPropertySetUint32;
using U32Adapters = cGZPropertySetUint32Adapters;

struct OrigU32 {
    difftest::Context &ctx;
    void Construct(U32Set *s) { ctx.Original<void *(__thiscall *)(void *)>(0x1bec5f)(s); }
    void Destruct(U32Set *s) { ctx.Original<void(__thiscall *)(void *)>(0x1bed39)(s); }
    void *DeletingDestructor(U32Set *s, uint32_t f) { return ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1bedac)(s, f); }
    uint8_t QI(U32Set *s, uint32_t iid, void **out) { return ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x1bf44d)(s, iid, out); }
    uint32_t Release(U32Set *s) { return ctx.Original<uint32_t(__thiscall *)(void *)>(0x1bf485)(s); }
    uint8_t Set(U32Set *s, const uint32_t *k, void *v) { return ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *, void *)>(0x1bf49c)(s, k, v); }
    uint8_t Remove(U32Set *s, const uint32_t *k) { return ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *)>(0x1bee49)(s, k); }
    void Clear(U32Set *s) { ctx.Original<void(__thiscall *)(void *)>(0x1bee23)(s); }
    uint8_t First(U32Set *s, uint32_t *k, void **v) { return ctx.Original<uint8_t(__thiscall *)(void *, uint32_t *, void **)>(0x1bf239)(s, k, v); }
    uint8_t Next(U32Set *s, uint32_t *k, void **v) { return ctx.Original<uint8_t(__thiscall *)(void *, uint32_t *, void **)>(0x1bf389)(s, k, v); }
    void *Get(U32Set *s, const uint32_t *k) { return ctx.Original<void *(__thiscall *)(void *, const uint32_t *)>(0x1bf5e9)(s, k); }
    uint8_t GetValue(U32Set *s, const uint32_t *k, void *o) { return ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *, void *)>(0x1bf626)(s, k, o); }
    uint8_t Has(U32Set *s, const uint32_t *k) { return ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *)>(0x1bf2cc)(s, k); }
    uint32_t Count(U32Set *s) { return ctx.Original<uint32_t(__thiscall *)(void *)>(0x1bf41f)(s); }
    void CopyFrom(U32Set *s, void *o) { ctx.Original<void(__thiscall *)(void *, void *)>(0x1bf148)(s, o); }
    const char *KeyToString(U32Set *s, uint32_t k, void *o) { return ctx.Original<const char *(__thiscall *)(void *, uint32_t, void *)>(0x1bf336)(s, k, o); }
    const char *FormatKey(const uint32_t *k, void *o) { return ctx.Original<const char *(__cdecl *)(const uint32_t *, void *)>(0x1bf349)(k, o); }
    uint32_t Class(const U32Set *s) { return VtClass(s, {GameAddress(U32Set::kOriginalVTable)}); }
};

// The vtable route: our constructor, every slot through our vtable (a slot the game reaches only through its folded body is
// still ours: Clear, GetCount, Release, KeyToString). The adapter route: the adapters on a game-built set with trap vtables;
// slots without an adapter (folded: Clear 0x1bee23, GetCount 0x1bf41f, Release 0x1bf485, KeyToString 0x1bf336) run the
// original there in game, so here their body is checked by a qualified call (KeyToString: the original's 0x1bf336 calls our
// hooked FormatKey 0x1bf349, called directly).
struct OursU32 {
    difftest::Context &ctx;
    Route route;
    TrapTables *traps;
    static U32Face *Face(U32Set *s) { return s; }
    void Construct(U32Set *s)
    {
        if (route == kVtableRoute) {
            ::new (s) U32Set;
        } else {
            ctx.Original<void *(__thiscall *)(void *)>(0x1bec5f)(s);
            *reinterpret_cast<void ***>(s) = traps->plain;
        }
    }
    void Destruct(U32Set *s) { route == kVtableRoute ? s->~U32Set() : U32Adapters::Destruct(s, nullptr); }
    void DeletingDestructor(U32Set *s, uint32_t f)
    {
        if (route == kVtableRoute) {
            VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(s, 13)(s, f);
        } else {
            U32Adapters::DeletingDestructor(s, nullptr, f); // 0x1bedac
        }
    }
    uint8_t QI(U32Set *s, uint32_t iid, void **out) { return route == kVtableRoute ? Face(s)->QueryInterface(iid, out) : U32Adapters::QueryInterface(s, nullptr, iid, out); }
    uint32_t Release(U32Set *s) { return route == kVtableRoute ? Face(s)->Release() : s->U32Set::Release(); }
    uint8_t Set(U32Set *s, const uint32_t *k, void *v) { return route == kVtableRoute ? Face(s)->SetProperty(k, v) : U32Adapters::SetProperty(s, nullptr, k, v); }
    uint8_t Remove(U32Set *s, const uint32_t *k) { return route == kVtableRoute ? Face(s)->RemoveProperty(k) : U32Adapters::RemoveProperty(s, nullptr, k); }
    void Clear(U32Set *s) { route == kVtableRoute ? Face(s)->Clear() : s->U32Set::Clear(); }
    uint8_t First(U32Set *s, uint32_t *k, void **v) { return route == kVtableRoute ? Face(s)->GetFirst(k, v) : U32Adapters::GetFirst(s, nullptr, k, v); }
    uint8_t Next(U32Set *s, uint32_t *k, void **v) { return route == kVtableRoute ? Face(s)->GetNext(k, v) : U32Adapters::GetNext(s, nullptr, k, v); }
    void *Get(U32Set *s, const uint32_t *k) { return route == kVtableRoute ? Face(s)->GetProperty(k) : U32Adapters::GetProperty(s, nullptr, k); }
    uint8_t GetValue(U32Set *s, const uint32_t *k, void *o) { return route == kVtableRoute ? Face(s)->GetPropertyValue(k, o) : U32Adapters::GetPropertyValue(s, nullptr, k, o); }
    uint8_t Has(U32Set *s, const uint32_t *k) { return route == kVtableRoute ? Face(s)->HasProperty(k) : U32Adapters::HasProperty(s, nullptr, k); }
    uint32_t Count(U32Set *s) { return route == kVtableRoute ? Face(s)->GetCount() : s->U32Set::GetCount(); }
    void CopyFrom(U32Set *s, void *o)
    {
        auto *other = static_cast<U32Face *>(static_cast<U32Set *>(o));
        if (route == kVtableRoute) return Face(s)->CopyFrom(other);
        *reinterpret_cast<void ***>(s) = traps->copying;
        U32Adapters::CopyFrom(s, nullptr, other);
        *reinterpret_cast<void ***>(s) = traps->plain;
    }
    const char *KeyToString(U32Set *s, uint32_t k, void *o)
    {
        return route == kVtableRoute ? Face(s)->KeyToString(k, static_cast<cIGZString *>(o)) : U32Set::FormatKey(&k, static_cast<cIGZString *>(o));
    }
    const char *FormatKey(const uint32_t *k, void *o) { return U32Set::FormatKey(k, static_cast<cIGZString *>(o)); }
    uint32_t Class(const U32Set *s) { return VtClass(s, {OurVTable<U32Set>(), traps->plain, traps->copying}); }
};

const uint32_t kU32Keys[] = {0, 1, 2, 3, 7, 0x10000001, 0x80000000, 0xffffffff};

// One operation on a uint32 set; the result words go to r. `op` and every random choice come from the caller.
template <typename Impl>
void U32Op(Impl impl, U32Set *s, World &w, int op, uint32_t key, Kind kind, uint32_t raw, U32Set *source, Blob32 &r)
{
    Value v;
    MakeValue(v, kind, raw, w);
    switch (op) {
    case 0: r.push_back(impl.Set(s, &key, v.Ptr(kind))); break;
    case 1: r.push_back(impl.Remove(s, &key)); break;
    case 2: r.push_back(NormAny(impl.Get(s, &key), *s, w, nullptr)); break;
    case 3: {
        r.push_back(impl.GetValue(s, &key, v.Ptr(kind)));
        r.insert(r.end(), {NormOut(v, kind, w), v.custom.value});
        break;
    }
    case 4: r.push_back(impl.Has(s, &key)); break;
    case 5:
    case 6: {
        uint32_t k = 0x11111111;
        void *value = reinterpret_cast<void *>(0x22222222);
        const uint8_t ok = op == 5 ? impl.First(s, &k, &value) : impl.Next(s, &k, &value);
        r.insert(r.end(), {ok, k, value == reinterpret_cast<void *>(0x22222222) ? 0x22222222u : NormAny(value, *s, w, nullptr)});
        break;
    }
    case 7: impl.Clear(s); break;
    case 8: r.push_back(impl.Count(s)); break;
    case 9: impl.CopyFrom(s, source); break;
    case 10: {
        FakeOut out;
        InitFakeOut(out);
        const char *text = raw & 1 ? impl.KeyToString(s, key, &out) : impl.FormatKey(&key, &out);
        r.insert(r.end(), {text == out.text, out.calls});
        for (char c : std::string(out.text)) r.push_back(static_cast<uint8_t>(c));
        break;
    }
    default: { // null arguments to GetFirst / GetNext: refused before the cursor moves
        uint32_t k = 5;
        void *value = nullptr;
        r.insert(r.end(), {impl.First(s, &k, nullptr), impl.First(s, nullptr, &value), impl.First(s, nullptr, nullptr),
                           impl.Next(s, &k, nullptr), impl.Next(s, nullptr, &value), k});
        break;
    }
    }
}

// A source set for CopyFrom, built with the original code in world w (its vtable calls are the original's).
U32Set *MakeU32Source(difftest::Context &ctx, World &w, uint32_t seed)
{
    OrigU32 o{ctx};
    auto *s = static_cast<U32Set *>(::operator new(sizeof(U32Set)));
    o.Construct(s);
    std::mt19937 rng(seed);
    for (int i = 0, n = rng() % 5; i < n; ++i) {
        Kind kind = PickKind(rng);
        uint32_t raw = PickRaw(rng, kind);
        if (kind == kCustom && raw == kNullCloneValue) raw = 1;
        Value v;
        MakeValue(v, kind, raw, w);
        const uint32_t key = kU32Keys[rng() % 8];
        if (!KeyIsNullAnyAny(*s, key)) o.Set(s, &key, v.Ptr(kind));
    }
    return s;
}

// Destroys s (how 0: Destruct, 1 / 2: DeletingDestructor with flag 0 / 1) and describes the frees and what is left.
template <typename Impl>
Blob32 EndU32(Impl impl, U32Set *s, uint32_t how)
{
    void *entries = s->mEntries.data();
    difftest::FreeRecorder rec; // one recorder at a time: its results are read before the next starts
    if (how == 0) impl.Destruct(s);
    else impl.DeletingDestructor(s, how - 1);
    rec.Stop();
    const bool objectFreed = rec.Freed(s);
    Blob32 out{static_cast<uint32_t>(rec.Count()), objectFreed, entries ? rec.Freed(entries) : 2u};
    if (!objectFreed) {
        const auto *w = reinterpret_cast<const uint32_t *>(s);
        out.insert(out.end(), {impl.Class(s), w[1], w[2], w[3], w[4]}); // vtable, count, the emptied vector (+0x14 is stale)
        ::operator delete(s);
    }
    return out;
}

void RunU32Case(difftest::Context &ctx, uint32_t seed)
{
    World wa, wb;
    for (uint32_t i = 0; i < kStrings; ++i) wa.texts[i] = wb.texts[i] = {gFakeTextVT, 10 + i, i};
    TrapTables traps;
    BuildTraps(traps, U32Set::kOriginalVTable, false);
    OrigU32 oa{ctx};
    OursU32 ob{ctx, seed & 1 ? kAdapterRoute : kVtableRoute, &traps};
    auto *a = static_cast<U32Set *>(::operator new(0x24));
    auto *b = static_cast<U32Set *>(::operator new(0x24));
    std::memset(a, 0xcd, 0x24);
    std::memset(b, 0xcd, 0x24);
    oa.Construct(a);
    ob.Construct(b);
    gTrapHits = 0;
    std::mt19937 rng(seed);
    bool cursorValid = false;
    int cursorIndex = 0;
    const int steps = 10 + rng() % 50;
    for (int step = 0; step < steps; ++step) {
        int op = rng() % 100;
        op = op < 30 ? 0 : op < 42 ? 1 : op < 50 ? 2 : op < 58 ? 3 : op < 64 ? 4 : op < 72 ? 5 : op < 84 ? 6 : op < 86 ? 7 : op < 89 ? 8
           : op < 92 ? 9 : op < 96 ? 10 : 11;
        const uint32_t key = kU32Keys[rng() % 8];
        Kind kind = PickKind(rng);
        uint32_t raw = PickRaw(rng, kind);
        // States the original crashes on, avoided: Assign into or out of an AnyAny holding null, CopyFrom of such a
        // set, GetNext with the cursor at the end or stale.
        if ((op == 0 || op == 3) && KeyIsNullAnyAny(*a, key)) op = 1;
        if (op == 9 && HasNullAnyAny(*a)) op = 8;
        if (op == 6 && !(cursorValid && cursorIndex < static_cast<int>(a->mEntries.size()))) op = 5;
        U32Set *sa = op == 9 ? MakeU32Source(ctx, wa, seed * 31 + step) : nullptr;
        U32Set *sb = op == 9 ? MakeU32Source(ctx, wb, seed * 31 + step) : nullptr;
        Blob32 ra, rb;
        U32Op(oa, a, wa, op, key, kind, raw, sa, ra);
        U32Op(ob, b, wb, op, key, kind, raw, sb, rb);
        ++ctx.cases;
        if (op == 5) cursorValid = true, cursorIndex = 0;
        else if (op == 6) ++cursorIndex;
        else if (op == 0 || op == 1 || op == 9) cursorValid = false;
        Blob32 snapA = ra, snapB = rb;
        SnapSet(snapA, *a, oa.Class(a), wa, cursorValid);
        SnapSet(snapB, *b, ob.Class(b), wb, cursorValid);
        SnapWorld(snapA, wa);
        SnapWorld(snapB, wb);
        if (sa) {
            SnapSet(snapA, *sa, oa.Class(sa), wa, false);
            SnapSet(snapB, *sb, oa.Class(sb), wb, false);
            oa.DeletingDestructor(sa, 1);
            oa.DeletingDestructor(sb, 1);
        }
        static const char *const kNames[] = {"Set", "Remove", "Get", "GetValue", "Has", "GetFirst", "GetNext", "Clear", "Count", "CopyFrom", "KeyToString", "null args"};
        if (!TrapsQuiet(ctx, kNames[op], seed, step)) break;
        if (!Same(ctx, snapA, snapB, kNames[op], seed, step)) break;
    }
    // The end: Destruct or DeletingDestructor (0: object kept, 1: freed), compared through the logs and frees.
    const uint32_t how = (seed / 2) % 3;
    Blob32 endA = EndU32(oa, a, how);
    SnapWorld(endA, wa);
    Blob32 endB = EndU32(ob, b, how);
    SnapWorld(endB, wb);
    ++ctx.cases;
    TrapsQuiet(ctx, "destruct", seed, -1);
    Same(ctx, endA, endB, "destruct", seed, -1);
    FreeWorld(wa);
    FreeWorld(wb);
}

} // namespace

// Slots (seeds with bit 0 clear) and adapters (bit 0 set) of the uint32 set: 0x1bf49c SetProperty, 0x1bee49 RemoveProperty,
// 0x1bf239 GetFirst, 0x1bf389 GetNext, 0x1bf5e9 GetProperty, 0x1bf626 GetPropertyValue, 0x1bf2cc HasProperty, 0x1bf148 CopyFrom,
// 0x1bed39 Destruct, the folded 0x1bee23 Clear, 0x1bf41f GetCount, 0x1bf336 KeyToString, 0x1bf349 FormatKey.
DIFF_TEST(PropertySet_Uint32_Sequences, 0x1bf49c)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 400; ++seed) RunU32Case(ctx, seed);
}

// The constructor adapters (0x1bec5f, 0x22b157: our class on the game's memory) against the original constructors: every word
// but the vtable pointer, which is the side's own.
DIFF_TEST(PropertySet_Construct, 0x1bec5f)
{
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        const bool stringSet = seed & 1;
        alignas(8) uint8_t a[0x28], b[0x28];
        std::mt19937 rng(seed);
        for (size_t i = 0; i < sizeof(a); ++i) a[i] = b[i] = static_cast<uint8_t>(rng());
        void *ra = ctx.Original<void *(__thiscall *)(void *)>(stringSet ? 0x22b157 : 0x1bec5f)(a);
        void *rb = stringSet ? static_cast<void *>(cGZPropertySetStringAdapters::Construct(b, nullptr))
                             : static_cast<void *>(U32Adapters::Construct(b, nullptr));
        ++ctx.cases;
        const bool vtA = VtWord(a) == GameAddress(stringSet ? cGZPropertySetString::kOriginalVTable : U32Set::kOriginalVTable);
        const bool vtB = VtWord(b) == (stringSet ? OurVTable<cGZPropertySetString>() : OurVTable<U32Set>());
        if (ra != a || rb != b || !vtA || !vtB || std::memcmp(a + 4, b + 4, sizeof(a) - 4) != 0)
            ctx.Fail("seed %u: %s constructor differs", seed, stringSet ? "string" : "uint32");
        if (stringSet) reinterpret_cast<cGZPropertySetString *>(b)->~cGZPropertySetString();
        else reinterpret_cast<U32Set *>(b)->~U32Set();
    }
}

// QueryInterface 0x1bf44d / 0x1bf429 (through our vtable, and the adapters on a game-built set with trap vtables) and the folded
// Release 0x1bf485 (through our vtable; on the adapter route the body by a qualified call): refcounts 0..3 and a high one;
// counts of 0 / 1 delete through slot 13.
DIFF_TEST(PropertySet_QueryRelease, 0x1bf485)
{
    const uint32_t iids[] = {1, 0xebb10cdd, 0x4bb0d269, 0, 2, 0xebb10cdc};
    TrapTables trapsU32, trapsStr;
    BuildTraps(trapsU32, U32Set::kOriginalVTable, false);
    BuildTraps(trapsStr, cGZPropertySetString::kOriginalVTable, true);
    for (uint32_t refs : {0u, 1u, 2u, 3u, 0x80000001u}) {
        for (uint32_t iid : iids) {
            for (int which = 0; which < 2; ++which) {
                for (int route = 0; route < 2; ++route) {
                    // which 0: the uint32 set; 1: the string set.
                    const uint32_t ctor = which ? 0x22b157 : 0x1bec5f;
                    auto *a = ::operator new(0x24);
                    auto *b = ::operator new(0x24);
                    std::memset(a, 0, 0x24);
                    std::memset(b, 0, 0x24);
                    ctx.Original<void *(__thiscall *)(void *)>(ctor)(a);
                    if (route == kVtableRoute) {
                        if (which) ::new (b) cGZPropertySetString;
                        else ::new (b) U32Set;
                    } else {
                        ctx.Original<void *(__thiscall *)(void *)>(ctor)(b);
                        *static_cast<void ***>(b) = which ? trapsStr.plain : trapsU32.plain;
                    }
                    auto *ta = static_cast<gz::cGZPropertyTable *>(static_cast<U32Set *>(a));
                    auto *tb = static_cast<gz::cGZPropertyTable *>(static_cast<U32Set *>(b));
                    ta->mRefCount = tb->mRefCount = refs;
                    auto *u = static_cast<U32Set *>(b);
                    auto *s = static_cast<cGZPropertySetString *>(b);
                    void *oa = reinterpret_cast<void *>(0x1234), *ob = reinterpret_cast<void *>(0x1234);
                    const uint8_t qa = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(which ? 0x1bf429 : 0x1bf44d)(a, iid, &oa);
                    uint8_t qb;
                    if (route == kVtableRoute)
                        qb = which ? static_cast<cIGZPropertySetString *>(s)->QueryInterface(iid, &ob) : static_cast<U32Face *>(u)->QueryInterface(iid, &ob);
                    else
                        qb = which ? cGZPropertySetStringAdapters::QueryInterface(s, nullptr, iid, &ob) : U32Adapters::QueryInterface(u, nullptr, iid, &ob);
                    ++ctx.cases;
                    TrapsQuiet(ctx, "QueryInterface", refs, iid);
                    if (qa != qb || (oa == a) != (ob == b) || (oa == reinterpret_cast<void *>(0x1234)) != (ob == reinterpret_cast<void *>(0x1234)) ||
                        ta->mRefCount != tb->mRefCount)
                        ctx.Fail("QI %d route %d refs %08x iid %08x: %u/%u refs %u/%u", which, route, refs, iid, qa, qb, ta->mRefCount, tb->mRefCount);
                    difftest::FreeRecorder fa;
                    const uint32_t xa = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1bf485)(a);
                    fa.Stop();
                    const uint32_t refsA = fa.Freed(a) ? 0xffffffff : ta->mRefCount;
                    difftest::FreeRecorder fb;
                    uint32_t xb;
                    if (route == kVtableRoute) xb = which ? static_cast<cIGZPropertySetString *>(s)->Release() : static_cast<U32Face *>(u)->Release();
                    else xb = which ? s->cGZPropertySetString::Release() : u->U32Set::Release();
                    fb.Stop();
                    const uint32_t refsB = fb.Freed(b) ? 0xffffffff : tb->mRefCount;
                    ++ctx.cases;
                    if (xa != xb || refsA != refsB) ctx.Fail("Release %d route %d refs %08x: %u/%u, %08x/%08x", which, route, refs, xa, xb, refsA, refsB);
                    if (refsA != 0xffffffff) ::operator delete(a);
                    if (refsB != 0xffffffff) ::operator delete(b);
                }
            }
        }
    }
}

DIFF_TEST(PropertySet_Entry, 0x1bec9b)
{
    // 0x1bec9b Construct, 0x1bee0a Value, 0x1bf210 Destroy on pre-filled entries, every value kind.
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        World wa, wb;
        for (uint32_t i = 0; i < kStrings; ++i) wa.texts[i] = wb.texts[i] = {gFakeTextVT, 10 + i, i};
        std::mt19937 rng(seed);
        const Kind kind = static_cast<Kind>(seed % kKinds);
        const uint32_t raw = kind == kCustom && seed % 4 == 0 ? kNullCloneValue : PickRaw(rng, kind);
        Value va, vb;
        MakeValue(va, kind, raw, wa);
        MakeValue(vb, kind, raw, wb);
        alignas(4) uint8_t ea[16], eb[16];
        for (size_t i = 0; i < 16; ++i) ea[i] = eb[i] = static_cast<uint8_t>(rng());
        const uint32_t key = rng();
        void *ca = ctx.Original<void *(__thiscall *)(void *, uint32_t, void *)>(0x1bec9b)(ea, key, va.Ptr(kind));
        void *cb = reinterpret_cast<cGZPropertyEntry *>(eb)->Construct(key, vb.Ptr(kind));
        Blob32 a{ca == ea}, b{cb == eb};
        auto entryWords = [](Blob32 &out, const uint8_t *e, const World &w) {
            const auto *entry = reinterpret_cast<const cGZPropertyEntry *>(e);
            out.insert(out.end(), {entry->mKey, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry->mValue.mVTable)),
                                   entry->mValue.mVTable == GameAddress(gzany::kBooleanVTable) ? entry->mValue.mValue : NormPayload(*entry, w),
                                   entry->mValueExtra});
        };
        entryWords(a, ea, wa);
        entryWords(b, eb, wb);
        void *pa = ctx.Original<void *(__thiscall *)(void *)>(0x1bee0a)(ea);
        void *pb = reinterpret_cast<cGZPropertyEntry *>(eb)->Value();
        auto norm = [](void *p, uint8_t *e, const World &w) -> uint32_t {
            if (p == e + 4) return 1;
            for (const CustomAny *m : w.made)
                if (m == p) return 0xc0000000 | m->serial;
            return p ? 0xffffffff : 0;
        };
        a.push_back(norm(pa, ea, wa));
        b.push_back(norm(pb, eb, wb));
        SnapWorld(a, wa);
        SnapWorld(b, wb);
        ctx.Original<void(__thiscall *)(void *)>(0x1bf210)(ea);
        reinterpret_cast<cGZPropertyEntry *>(eb)->Destroy();
        // What the destroyed entry still holds; a text object's or a freed clone's address is compared through the
        // reference counts and logs instead.
        auto rawWords = [kind](Blob32 &out, const uint8_t *e) {
            const auto *w = reinterpret_cast<const uint32_t *>(e);
            out.insert(out.end(), {w[0], w[1], kind == kStr || kind == kCustom ? 0u : w[2], w[3]});
        };
        rawWords(a, ea);
        rawWords(b, eb);
        SnapWorld(a, wa);
        SnapWorld(b, wb);
        ++ctx.cases;
        Same(ctx, a, b, "entry", seed, static_cast<int>(kind));
        FreeWorld(wa);
        FreeWorld(wb);
    }
}

// ---- the string set ------------------------------------------------------------------------------------------

namespace {

using StrSet = cGZPropertySetString;
using StrFace = cIGZPropertySetString;
using StrAdapters = cGZPropertySetStringAdapters;

struct OrigStr {
    difftest::Context &ctx;
    void Construct(StrSet *s) { ctx.Original<void *(__thiscall *)(void *)>(0x22b157)(s); }
    void Destruct(StrSet *s) { ctx.Original<void(__thiscall *)(void *)>(0x1777e0)(s); }
    void DeletingDestructor(StrSet *s, uint32_t f) { ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x177a70)(s, f); }
    void DestroyEntries(StrSet *s) { ctx.Original<void(__thiscall *)(void *)>(0x1bed22)(s); }
    void DeletingDestructorBase(StrSet *s, uint32_t f) { ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1bed8d)(s, f); }
    uint8_t Set(StrSet *s, const GZServiceString *k, void *v) { return ctx.Original<uint8_t(__thiscall *)(void *, const void *, void *)>(0x1bf515)(s, k, v); }
    uint8_t Remove(StrSet *s, const GZServiceString *k) { return ctx.Original<uint8_t(__thiscall *)(void *, const void *)>(0x1bee9c)(s, k); }
    uint8_t First(StrSet *s, GZServiceString *k, void **v) { return ctx.Original<uint8_t(__thiscall *)(void *, void *, void **)>(0x1bf26d)(s, k, v); }
    uint8_t Next(StrSet *s, GZServiceString *k, void **v) { return ctx.Original<uint8_t(__thiscall *)(void *, void *, void **)>(0x1bf3c0)(s, k, v); }
    void *Get(StrSet *s, const GZServiceString *k) { return ctx.Original<void *(__thiscall *)(void *, const void *)>(0x1bf670)(s, k); }
    uint8_t GetValue(StrSet *s, const GZServiceString *k, void *o) { return ctx.Original<uint8_t(__thiscall *)(void *, const void *, void *)>(0x1bf69a)(s, k, o); }
    uint8_t Has(StrSet *s, const GZServiceString *k) { return ctx.Original<uint8_t(__thiscall *)(void *, const void *)>(0x1bf306)(s, k); }
    void Clear(StrSet *s) { ctx.Original<void(__thiscall *)(void *)>(0x1bee23)(s); }
    uint32_t Count(StrSet *s) { return ctx.Original<uint32_t(__thiscall *)(void *)>(0x1bf41f)(s); }
    void CopyFrom(StrSet *s, void *o) { ctx.Original<void(__thiscall *)(void *, void *)>(0x1bf0d1)(s, o); }
    const char *KeyToString(StrSet *s, uint32_t k, void *o) { return ctx.Original<const char *(__thiscall *)(void *, uint32_t, void *)>(0x1bf36a)(s, k, o); }
    uint32_t Intern(StrSet *s, const GZServiceString *k) { return ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1bf191)(s, k); }
    uint32_t Find(StrSet *s, const GZServiceString *k) { return ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x1befff)(s, k); }
    uint32_t Class(const StrSet *s) { return VtClass(s, {GameAddress(StrSet::kOriginalVTable), GameAddress(StrSet::kOriginalBaseVTable)}); }
};

// The two routes as for the uint32 set (OursU32). Clear and GetCount have no adapter (folded with the uint32 set's): checked by a
// qualified call on the adapter route.
struct OursStr {
    difftest::Context &ctx;
    Route route;
    TrapTables *traps;
    static StrFace *Face(StrSet *s) { return s; }
    void Construct(StrSet *s)
    {
        if (route == kVtableRoute) {
            ::new (s) StrSet;
        } else {
            ctx.Original<void *(__thiscall *)(void *)>(0x22b157)(s);
            *reinterpret_cast<void ***>(s) = traps->plain;
        }
    }
    void Destruct(StrSet *s) { route == kVtableRoute ? s->~StrSet() : StrAdapters::Destruct(s, nullptr); }
    void DeletingDestructor(StrSet *s, uint32_t f)
    {
        if (route == kVtableRoute) VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(s, 13)(s, f);
        else StrAdapters::DeletingDestructor(s, nullptr, f);
    }
    void DestroyEntries(StrSet *s) { route == kVtableRoute ? s->DestroyEntries() : StrAdapters::DestroyEntries(s, nullptr); }
    // The RTTI base's slot 13 (no class of ours, so no vtable route): the adapter on both routes.
    void DeletingDestructorBase(StrSet *s, uint32_t f) { StrAdapters::DeletingDestructorBase(s, nullptr, f); }
    uint8_t Set(StrSet *s, const GZServiceString *k, void *v) { return route == kVtableRoute ? Face(s)->SetProperty(k, v) : StrAdapters::SetProperty(s, nullptr, k, v); }
    uint8_t Remove(StrSet *s, const GZServiceString *k) { return route == kVtableRoute ? Face(s)->RemoveProperty(k) : StrAdapters::RemoveProperty(s, nullptr, k); }
    uint8_t First(StrSet *s, GZServiceString *k, void **v) { return route == kVtableRoute ? Face(s)->GetFirst(k, v) : StrAdapters::GetFirst(s, nullptr, k, v); }
    uint8_t Next(StrSet *s, GZServiceString *k, void **v) { return route == kVtableRoute ? Face(s)->GetNext(k, v) : StrAdapters::GetNext(s, nullptr, k, v); }
    void *Get(StrSet *s, const GZServiceString *k) { return route == kVtableRoute ? Face(s)->GetProperty(k) : StrAdapters::GetProperty(s, nullptr, k); }
    uint8_t GetValue(StrSet *s, const GZServiceString *k, void *o) { return route == kVtableRoute ? Face(s)->GetPropertyValue(k, o) : StrAdapters::GetPropertyValue(s, nullptr, k, o); }
    uint8_t Has(StrSet *s, const GZServiceString *k) { return route == kVtableRoute ? Face(s)->HasProperty(k) : StrAdapters::HasProperty(s, nullptr, k); }
    void Clear(StrSet *s) { route == kVtableRoute ? Face(s)->Clear() : s->StrSet::Clear(); }
    uint32_t Count(StrSet *s) { return route == kVtableRoute ? Face(s)->GetCount() : s->StrSet::GetCount(); }
    void CopyFrom(StrSet *s, void *o)
    {
        auto *other = static_cast<StrFace *>(static_cast<StrSet *>(o));
        if (route == kVtableRoute) return Face(s)->CopyFrom(other);
        *reinterpret_cast<void ***>(s) = traps->copying;
        StrAdapters::CopyFrom(s, nullptr, other);
        *reinterpret_cast<void ***>(s) = traps->plain;
    }
    const char *KeyToString(StrSet *s, uint32_t k, void *o)
    {
        return route == kVtableRoute ? Face(s)->KeyToString(k, static_cast<cIGZString *>(o)) : StrAdapters::KeyToString(s, nullptr, k, static_cast<cIGZString *>(o));
    }
    uint32_t Intern(StrSet *s, const GZServiceString *k) { return route == kVtableRoute ? s->InternKey(k) : StrAdapters::InternKey(s, nullptr, k); }
    uint32_t Find(StrSet *s, const GZServiceString *k) { return route == kVtableRoute ? s->FindKey(k) : StrAdapters::FindKey(s, nullptr, k); }
    uint32_t Class(const StrSet *s) { return VtClass(s, {OurVTable<StrSet>(), traps->plain, traps->copying}); }
};

GZServiceString KeyText(uint32_t pick)
{
    static const char *const kKeys[] = {"Key", "key", "KEY", "a", "A", "", "b", "1", "0x10000001",
                                        "a key long enough for the heap", "A KEY LONG ENOUGH FOR THE HEAP", "kEy"};
    if (pick == 12) return GZServiceString("b\0c", 3); // embedded NUL: stored as "b" and "c"
    if (pick == 13) return GZServiceString("ke", 2);   // a prefix of another key
    return GZServiceString(kKeys[pick % 12]);
}

uint32_t KeyOffsetIsNullAnyAny(const StrSet &s, uint32_t offset) { return KeyIsNullAnyAny(s, offset); }

template <typename Impl>
void StrOp(Impl impl, StrSet *s, World &w, int op, const GZServiceString &key, Kind kind, uint32_t raw, StrSet *source, Blob32 &r)
{
    Value v;
    MakeValue(v, kind, raw, w);
    switch (op) {
    case 0: r.push_back(impl.Set(s, &key, v.Ptr(kind))); break;
    case 1: r.push_back(impl.Remove(s, &key)); break;
    case 2: r.push_back(NormAny(impl.Get(s, &key), *s, w, nullptr)); break;
    case 3:
        r.push_back(impl.GetValue(s, &key, v.Ptr(kind)));
        r.insert(r.end(), {NormOut(v, kind, w), v.custom.value});
        break;
    case 4: r.push_back(impl.Has(s, &key)); break;
    case 5:
    case 6: {
        // The out key starts with text (short or long) so the assignment's reallocation paths run.
        GZServiceString k(raw & 1 ? "previous text in the out key, long" : "prev");
        void *value = reinterpret_cast<void *>(0x22222222);
        const uint8_t ok = op == 5 ? impl.First(s, &k, &value) : impl.Next(s, &k, &value);
        r.insert(r.end(), {ok, static_cast<uint32_t>(k.size()), value == reinterpret_cast<void *>(0x22222222) ? 0x22222222u : NormAny(value, *s, w, nullptr)});
        for (char c : k) r.push_back(static_cast<uint8_t>(c));
        break;
    }
    case 7: impl.Clear(s); break;
    case 8: r.insert(r.end(), {impl.Find(s, &key), impl.Count(s)}); break;
    case 9: impl.CopyFrom(s, source); break;
    case 10: {
        if (s->mEntries.empty()) break;
        FakeOut out;
        InitFakeOut(out);
        const uint32_t offset = s->mEntries[raw % s->mEntries.size()].mKey;
        const char *text = impl.KeyToString(s, offset, &out);
        r.insert(r.end(), {text == out.text, out.calls});
        for (char c : std::string(out.text)) r.push_back(static_cast<uint8_t>(c));
        break;
    }
    case 11: r.push_back(impl.Intern(s, &key)); break;
    default: { // null arguments to GetFirst / GetNext: refused before the cursor moves
        GZServiceString k("unchanged");
        void *value = nullptr;
        r.insert(r.end(), {impl.First(s, &k, nullptr), impl.First(s, nullptr, &value), impl.First(s, nullptr, nullptr),
                           impl.Next(s, &k, nullptr), impl.Next(s, nullptr, &value), static_cast<uint32_t>(k.size())});
        break;
    }
    }
}

StrSet *MakeStrSource(difftest::Context &ctx, World &w, uint32_t seed)
{
    OrigStr o{ctx};
    auto *s = static_cast<StrSet *>(::operator new(sizeof(StrSet)));
    o.Construct(s);
    std::mt19937 rng(seed);
    for (int i = 0, n = rng() % 6; i < n; ++i) {
        Kind kind = PickKind(rng);
        uint32_t raw = PickRaw(rng, kind);
        if (kind == kCustom && raw == kNullCloneValue) raw = 2;
        Value v;
        MakeValue(v, kind, raw, w);
        const GZServiceString key = KeyText(rng() % 14);
        const uint32_t found = o.Find(s, &key);
        if (found == StrSet::kNoKey || !KeyOffsetIsNullAnyAny(*s, found)) o.Set(s, &key, v.Ptr(kind));
    }
    return s;
}

// Destroys s by one of the destructors and describes what happened: the frees (the set object, its key text, the number of
// blocks), and what is left of a kept object. How 1 (DestroyEntries, the RTTI base's destructor 0x1bed22) and 4 / 5 (the RTTI
// base's deleting destructor 0x1bed8d, flag 0 / 1) leave the key text, freed here.
template <typename Impl>
Blob32 EndStr(Impl impl, StrSet *s, uint32_t how)
{
    char *text = s->mKeyText.data();
    difftest::FreeRecorder rec; // one recorder at a time: its results are read before the next starts
    switch (how) {
    case 0: impl.Destruct(s); break;
    case 1: impl.DestroyEntries(s); break;
    case 4: case 5: impl.DeletingDestructorBase(s, how - 4); break;
    default: impl.DeletingDestructor(s, how - 2); break;
    }
    rec.Stop();
    const bool objectFreed = rec.Freed(s), textFreed = text && rec.Freed(text);
    Blob32 out{static_cast<uint32_t>(rec.Count()), objectFreed, text ? textFreed : 2u};
    if (!objectFreed) {
        const auto *w = reinterpret_cast<const uint32_t *>(s);
        // +0x14 (the cursor) is a stale heap pointer; the key text's three pointers are compared as null or not.
        out.insert(out.end(), {impl.Class(s), w[1], w[2], w[3], w[4], w[6] != 0, w[7] != 0, w[8] != 0});
    }
    if (text && !textFreed) ::operator delete(text);
    if (!objectFreed) ::operator delete(s);
    return out;
}

void RunStrCase(difftest::Context &ctx, uint32_t seed)
{
    World wa, wb;
    for (uint32_t i = 0; i < kStrings; ++i) wa.texts[i] = wb.texts[i] = {gFakeTextVT, 10 + i, i};
    TrapTables traps;
    BuildTraps(traps, StrSet::kOriginalVTable, true);
    OrigStr oa{ctx};
    OursStr ob{ctx, seed & 1 ? kAdapterRoute : kVtableRoute, &traps};
    auto *a = static_cast<StrSet *>(::operator new(sizeof(StrSet)));
    auto *b = static_cast<StrSet *>(::operator new(sizeof(StrSet)));
    oa.Construct(a);
    ob.Construct(b);
    gTrapHits = 0;
    std::mt19937 rng(seed);
    bool cursorValid = false;
    int cursorIndex = 0;
    const int steps = 10 + rng() % 50;
    for (int step = 0; step < steps; ++step) {
        int op = rng() % 100;
        op = op < 30 ? 0 : op < 42 ? 1 : op < 49 ? 2 : op < 56 ? 3 : op < 61 ? 4 : op < 68 ? 5 : op < 80 ? 6 : op < 82 ? 7 : op < 86 ? 8
           : op < 89 ? 9 : op < 93 ? 10 : op < 97 ? 11 : 12;
        const GZServiceString key = KeyText(rng() % 14);
        Kind kind = PickKind(rng);
        uint32_t raw = PickRaw(rng, kind);
        const uint32_t found = oa.Find(a, &key);
        if ((op == 0 || op == 3) && found != StrSet::kNoKey && KeyOffsetIsNullAnyAny(*a, found)) op = 1;
        if (op == 9 && HasNullAnyAny(*a)) op = 8;
        if (op == 6 && !(cursorValid && cursorIndex < static_cast<int>(a->mEntries.size()))) op = 5;
        StrSet *sa = op == 9 ? MakeStrSource(ctx, wa, seed * 31 + step) : nullptr;
        StrSet *sb = op == 9 ? MakeStrSource(ctx, wb, seed * 31 + step) : nullptr;
        Blob32 ra, rb;
        StrOp(oa, a, wa, op, key, kind, raw, sa, ra);
        StrOp(ob, b, wb, op, key, kind, raw, sb, rb);
        ++ctx.cases;
        if (op == 5) cursorValid = true, cursorIndex = 0;
        else if (op == 6) ++cursorIndex;
        else if (op == 0 || op == 1 || op == 9) cursorValid = false;
        Blob32 snapA = ra, snapB = rb;
        SnapSet(snapA, *a, oa.Class(a), wa, cursorValid);
        SnapSet(snapB, *b, ob.Class(b), wb, cursorValid);
        SnapText(snapA, *a);
        SnapText(snapB, *b);
        SnapWorld(snapA, wa);
        SnapWorld(snapB, wb);
        if (sa) {
            SnapSet(snapA, *sa, oa.Class(sa), wa, false);
            SnapSet(snapB, *sb, oa.Class(sb), wb, false);
            oa.DeletingDestructor(sa, 1);
            oa.DeletingDestructor(sb, 1);
        }
        static const char *const kNames[] = {"Set", "Remove", "Get", "GetValue", "Has", "GetFirst", "GetNext", "Clear", "FindKey", "CopyFrom", "KeyToString", "InternKey", "null args"};
        if (!TrapsQuiet(ctx, kNames[op], seed, step)) break;
        if (!Same(ctx, snapA, snapB, kNames[op], seed, step)) break;
    }
    // The end: one of the destructors (the deleting ones with flag 0 or 1).
    const uint32_t how = (seed / 2) % 6;
    Blob32 endA = EndStr(oa, a, how), endB = EndStr(ob, b, how);
    SnapWorld(endA, wa);
    SnapWorld(endB, wb);
    ++ctx.cases;
    TrapsQuiet(ctx, "destruct", seed, static_cast<int>(how));
    Same(ctx, endA, endB, "destruct", seed, static_cast<int>(how));
    FreeWorld(wa);
    FreeWorld(wb);
}

} // namespace

// Slots (seeds with bit 0 clear) and adapters (bit 0 set) of the string set: 0x1bf515 SetProperty, 0x1bee9c RemoveProperty,
// 0x1bf26d GetFirst, 0x1bf3c0 GetNext, 0x1bf670 GetProperty, 0x1bf69a GetPropertyValue, 0x1bf306 HasProperty, 0x1bf36a
// KeyToString, 0x1bf0d1 CopyFrom, 0x1bf191 InternKey, 0x1befff FindKey, 0x1777e0 Destruct, 0x177a70 DeletingDestructor,
// 0x1bed22 DestroyEntries, 0x1bed8d DeletingDestructorBase, the folded Clear 0x1bee23 and GetCount 0x1bf41f.
DIFF_TEST(PropertySet_String_Sequences, 0x1bf515)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 400; ++seed) RunStrCase(ctx, seed);
}
