// Differential tests for the property-set resources and cGZPropertySetFactory (src/gz/cGZPropertySetResourceString.cpp,
// src/gz/cGZPropertySetResource.size.cpp, lane DU; the string resource and the factory in the standalone shape: lanes EL, EF):
//   string resource 0x5519c0 Construct, 0x5553b0 QueryInterface, 0x5520e0 DeletingDestructor; the thunks 0x5553ed / 0x5553f5
//                   (QI), 0x5522bd / 0x55609d (AddRef / Release of the dispatcher face), 0x5520c3 / 0x5520cb (destructor)
//   uint32 resource 0xc2bb25 Construct, 0xc2c126 QueryInterface, 0xc2bcb3 Destruct, 0xc2bcf0 DeletingDestructor; the thunks
//                   0xc2c163 / 0xc2c16b (QI), 0xc2bd2e / 0xc2c2b6 (AddRef / Release of the dispatcher face), 0xc2bce0 /
//                   0xc2bce8 (destructor) (standalone shape: lane EQ)
//   factory         0xc2bb7a Construct, 0xc2be41 Create, 0xc2bd36 CreateInstance, 0xc2bd7c CreateInstance2,
//                   0xc2c173 Read, 0xc2c2be Write
// Objects are built on random-filled memory and compared byte for byte (vtable words as "the side's own"); the factory's
// record, resource and serializer are recording fakes (tests/ts_services_fixture.h), and CreateInstance2 makes real resources
// (compared by contents, reference count and whether they were freed). The string resource's original side is the game's
// constructor (game vtables), ours our constructor through the constructor hook's adapter, every call through its own vtable.
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/vtable.h"
#include "gz/cGZPropertySetResource.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include <windows.h>

using namespace svcfx;

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {2, "ddd"}, {5, "-dd"}};

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }

constexpr uint32_t kTypes[] = {0xebfee33f, 0xebfee342, 0xebfee345, 0xebfee349};

// A fake delegate in the resource's serializer: Release is logged.
struct Delegate {
    void **vt;
    uint32_t id;
};
Blob gLog;
uint32_t __fastcall DlRelease(Delegate *self, void *) { gLog.push_back(self->id); return 0; }
void *gDelegateVT[3] = {nullptr, reinterpret_cast<void *>(&DlRelease), reinterpret_cast<void *>(&DlRelease)};

std::vector<uint8_t> Bytes(const void *p, size_t n) { return {static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n}; }
uint32_t GameWord(uint32_t rva) { return uint32_t(uintptr_t(GameAddress<void>(rva))); }
uint32_t &WordAt(void *object, uint32_t offset) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + offset); }

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// ---- the string resource in the standalone shape (lane EL) ----

using Res = cGZPropertySetResourceString;
using ResAdapters = cGZPropertySetResourceStringAdapters;
using Disp = cGZPropertySetSerializerString;
constexpr uint32_t kFaces[3] = {0, Res::kSetFace, Res::kSerializerFace};

// Each face's vtable while the object lives, and what its destructor leaves there (the bases' vtables), per side.
struct ResVTables {
    uint32_t live[3], dead[3];
};
ResVTables GameResVTables()
{
    // Dead: cGZPersistResource 0xe603ac, the set's RTTI base 0xda3dc0 (0x1bed22), the dispatcher 0xdd4270.
    return {{GameWord(Res::kOriginalVTable), GameWord(Res::kOriginalSetVTable), GameWord(Res::kOriginalSerializerVTable)},
            {GameWord(cGZPersistResource::kOriginalVTable), GameWord(cGZPropertySetString::kOriginalBaseVTable), GameWord(Disp::kOriginalVTable)}};
}
ResVTables OurResVTables()
{
    alignas(8) uint8_t raw[sizeof(Res)];
    auto *r = ::new (raw) Res;
    ResVTables v;
    for (int f = 0; f < 3; ++f) v.live[f] = WordAt(raw, kFaces[f]);
    r->~Res();
    for (int f = 0; f < 3; ++f) v.dead[f] = WordAt(raw, kFaces[f]);
    return v;
}

// The three vtable words of a resource as codes: 1 the side's own vtable of that face, 2 the base's vtable the side's destructor
// leaves there, anything else raw (then it differs between the sides). Ours must not be the game's.
void ResWords(Blob &b, int side, const void *object)
{
    const ResVTables game = GameResVTables(), ours = OurResVTables();
    const ResVTables &own = side == 0 ? game : ours;
    for (int f = 0; f < 3; ++f) {
        const uint32_t word = WordAt(const_cast<void *>(object), kFaces[f]);
        b.push_back(word == own.live[f] && ours.live[f] != game.live[f] ? 1u : word == own.dead[f] && ours.dead[f] != game.dead[f] ? 2u : word);
    }
}
// The object's bytes with the vtable words zeroed (they are compared by ResWords).
std::vector<uint8_t> ResBytes(const void *object)
{
    std::vector<uint8_t> b = Bytes(object, sizeof(Res));
    for (uint32_t f : kFaces) std::memset(b.data() + f, 0, 4);
    return b;
}

// The side's resource: the game's constructor (original side) or ours through the constructor hook's adapter, on `memory`.
void *BuildRes(int side, void *memory)
{
    return side == 0 ? Game<void *(__thiscall *)(void *)>(0x5519c0)(memory) : static_cast<void *>(ResAdapters::Construct(memory, nullptr));
}

// ---- the uint32 resource in the standalone shape (lane EQ) ----

using Res32 = cGZPropertySetResourceUint32;
using Res32Adapters = cGZPropertySetResourceUint32Adapters;
using Disp32 = cGZPropertySetSerializerUint32;
constexpr uint32_t kFaces32[3] = {0, Res32::kSetFace, Res32::kSerializerFace};

ResVTables GameRes32VTables()
{
    // Dead: what 0xc2bcb3's three base destructors leave: cGZPersistResource 0xe603ac (0x9b6030), the uint32 set 0xda3e24
    // (0x1bed39), the uint32 dispatcher 0xdadc24 (0x241f2c).
    return {{GameWord(Res32::kOriginalVTable), GameWord(Res32::kOriginalSetVTable), GameWord(Res32::kOriginalSerializerVTable)},
            {GameWord(cGZPersistResource::kOriginalVTable), GameWord(cGZPropertySetUint32::kOriginalVTable), GameWord(Disp32::kOriginalVTable)}};
}
ResVTables OurRes32VTables()
{
    alignas(8) uint8_t raw[sizeof(Res32)];
    auto *r = ::new (raw) Res32;
    ResVTables v;
    for (int f = 0; f < 3; ++f) v.live[f] = WordAt(raw, kFaces32[f]);
    r->~Res32();
    for (int f = 0; f < 3; ++f) v.dead[f] = WordAt(raw, kFaces32[f]);
    return v;
}
void Res32Words(Blob &b, int side, const void *object)
{
    const ResVTables game = GameRes32VTables(), ours = OurRes32VTables();
    const ResVTables &own = side == 0 ? game : ours;
    for (int f = 0; f < 3; ++f) {
        const uint32_t word = WordAt(const_cast<void *>(object), kFaces32[f]);
        b.push_back(word == own.live[f] && ours.live[f] != game.live[f] ? 1u : word == own.dead[f] && ours.dead[f] != game.dead[f] ? 2u : word);
    }
}
std::vector<uint8_t> Res32Bytes(const void *object)
{
    std::vector<uint8_t> b = Bytes(object, sizeof(Res32));
    for (uint32_t f : kFaces32) std::memset(b.data() + f, 0, 4);
    return b;
}
// The original's constructor 0xc2bb25, or ours through its hook's adapter.
void *BuildRes32(int side, void *memory)
{
    return side == 0 ? Game<void *(__thiscall *)(void *)>(0xc2bb25)(memory) : static_cast<void *>(Res32Adapters::Construct(memory, nullptr));
}

// A cIGZString stand-in for KeyToString: slots 4 FromChar, 5 ToChar.
struct FakeOut {
    void **vt;
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
void *gFakeOutVT[6] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FoFromChar), reinterpret_cast<void *>(&FoToChar)};

// A value of a type the set doesn't hold inline: the set keeps it as an AnyAny around this fake's Clone, and deletes the clone
// (slot 0, flag 1) when the entry goes. The delete logs how many blocks were freed before it: the order of the set destructor's
// steps (key text first, then the entries' values).
const difftest::FreeRecorder *gRecorder;
struct FakeAny {
    void **vt;
};
void *__fastcall FaDelete(FakeAny *self, void *, uint32_t flags)
{
    gLog.push_back(0xc1000000u | (flags << 16) | uint32_t(gRecorder ? gRecorder->Count() : 0xffff));
    return self;
}
void *__fastcall FaClone(FakeAny *self, void *) { return self; }
uint32_t __fastcall FaType(FakeAny *, void *) { return 0x77777777; }
void *gFakeAnyVT[6] = {reinterpret_cast<void *>(&FaDelete), nullptr, nullptr, reinterpret_cast<void *>(&FaClone), reinterpret_cast<void *>(&FaType),
                       nullptr};

} // namespace

DIFF_TEST(PropSetRes_StringConstructDestroy, 0x5519c0)
{
    AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        std::mt19937 rng(seed);
        auto *a = static_cast<uint8_t *>(::operator new(0x50));
        auto *b = static_cast<uint8_t *>(::operator new(0x50));
        for (int i = 0; i < 0x50; ++i) a[i] = b[i] = uint8_t(rng());
        uint8_t *objs[2] = {a, b};
        Blob built[2];
        for (int side = 0; side < 2; ++side) {
            built[side].push_back(BuildRes(side, objs[side]) == objs[side]);
            ResWords(built[side], side, objs[side]);
            const std::vector<uint8_t> rest = ResBytes(objs[side]);
            built[side].insert(built[side].end(), rest.begin(), rest.end());
        }
        ++ctx.cases;
        if (built[0] != built[1]) ctx.Fail("seed %u: constructed objects differ", seed);

        // Destroy: delegates present or not, a property in the set or not, flags 0 / 1, through each face's destructor slot
        // (primary slot 12 0x5520e0, the set face's slot 13 thunk 0x5520c3, the dispatcher face's slot 5 thunk 0x5520cb).
        const bool withDelegates = seed & 1, withProperty = seed & 2;
        const uint32_t flags = (seed >> 2) & 1;
        const int face = int(seed % 3);
        const int slots[3] = {12, 13, 5};
        Delegate xmlA{gDelegateVT, 1}, binA{gDelegateVT, 2}, xmlB{gDelegateVT, 1}, binB{gDelegateVT, 2};
        Blob logs[2];
        size_t sizes[2][16] = {}, counts[2];
        bool freed[2];
        Blob after[2];
        for (int side = 0; side < 2; ++side) {
            uint8_t *r = objs[side];
            auto *disp = reinterpret_cast<Disp *>(r + Res::kSerializerFace);
            if (withDelegates) {
                disp->mXml = side ? &xmlB : &xmlA;
                disp->mBinary = side ? &binB : &binA;
            }
            if (withProperty) {
                InlineAny value{GameAddress<const void>(gzany::kUint32VTable), seed};
                GZServiceString key("some key longer than sixteen");
                CallSlot<uint8_t>(r + Res::kSetFace, 3, static_cast<const void *>(&key), static_cast<void *>(&value));
            }
            static FakeAny fakeAny{gFakeAnyVT};
            if (seed > 6) {
                GZServiceString key("other");
                CallSlot<uint8_t>(r + Res::kSetFace, 3, static_cast<const void *>(&key), static_cast<void *>(&fakeAny));
            }
            gLog.clear();
            gLog.reserve(64); // no reallocation (a free) inside the recorded call
            difftest::FreeRecorder rec;
            gRecorder = &rec;
            void *x = CallSlot<void *>(r + kFaces[face], slots[face], flags);
            rec.Stop(); gRecorder = nullptr;
            freed[side] = rec.Freed(r);
            counts[side] = rec.SortedSizes(sizes[side], 16);
            logs[side] = gLog;
            logs[side].push_back(x == r);
            if (!freed[side]) {
                ResWords(after[side], side, r);
                std::vector<uint8_t> rest = ResBytes(r);
                // The set's vector pointers are left dangling (heap addresses, different per side): compare the rest.
                if (withProperty || seed > 6) std::memset(rest.data() + 0x1c + 8, 0, 12), std::memset(rest.data() + 0x1c + 0x18, 0, 12);
                // The delegate pointers are each side's own stack fakes (same ids; their Release calls are in the log).
                if (withDelegates) std::memset(rest.data() + 0x44, 0, 8);
                after[side].insert(after[side].end(), rest.begin(), rest.end());
            }
        }
        ++ctx.cases;
        if (logs[0] != logs[1] || freed[0] != freed[1] || counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], sizeof(sizes[0])) != 0 ||
            after[0] != after[1])
            ctx.Fail("seed %u destroy (face %d): log %zu/%zu freed %d/%d frees %zu/%zu after %s", seed, face, logs[0].size(), logs[1].size(),
                     freed[0], freed[1], counts[0], counts[1], after[0] == after[1] ? "same" : "differs");
        if (!freed[0]) ::operator delete(a);
        if (!freed[1]) ::operator delete(b);
    }
}

// QueryInterface through each face of each side's own resource (the string one: slot 0 0x5553b0 and the thunks 0x5553ed /
// 0x5553f5; the uint32 one: 0xc2c126 called directly). Compared: result, the face handed out (as an offset), the count.
DIFF_TEST(PropSetRes_QueryInterface, 0x5553b0)
{
    AllocatorScope allocator;
    const uint32_t iids[] = {0x4bb0d269, 0xebb10cdd, 0xebe701f3, 0x456b8f1d, 1, 0, 0x12345678};
    for (int kind = 0; kind < 2; ++kind)
        for (int face = 0; face < 3; ++face)
            for (uint32_t iid : iids) {
                const size_t size = kind ? 0x44 : 0x50;
                uint8_t *objs[2];
                uint32_t results[2][3];
                for (int side = 0; side < 2; ++side) {
                    objs[side] = static_cast<uint8_t *>(::operator new(size));
                    if (kind) BuildRes32(side, objs[side]);
                    else BuildRes(side, objs[side]);
                    void *out = reinterpret_cast<void *>(0x7777);
                    // kind 1: slot 0 0xc2c126 and the thunks 0xc2c163 (set face) / 0xc2c16b (dispatcher face)
                    const uint8_t ok = CallSlot<uint8_t>(objs[side] + (kind ? kFaces32 : kFaces)[face], 0, iid, &out);
                    results[side][0] = ok;
                    results[side][1] = out == reinterpret_cast<void *>(0x7777) ? 0xffffffff : uint32_t(static_cast<uint8_t *>(out) - objs[side]);
                    results[side][2] = reinterpret_cast<uint32_t *>(objs[side])[1];
                    CallSlot<void *>(objs[side], 12, 1u);
                }
                ++ctx.cases;
                if (std::memcmp(results[0], results[1], sizeof(results[0])) != 0)
                    ctx.Fail("kind %d face %d iid %08x: ok %u/%u face %x/%x refs %u/%u", kind, face, iid, results[0][0], results[1][0],
                             results[0][1], results[1][1], results[0][2], results[1][2]);
            }
}

// The string resource's slots no other test calls, through each side's own vtables: AddRef / Release on every face (0x5522b0 /
// 0x556090 folded, their thunks 0x5522b5 / 0x556095 folded and 0x5522bd / 0x55609d; Release at 1 deletes), the persist key
// slots 3-11 (cGZPersistResource's folded bodies), and the set face's slots 3-12 and 14 (the set's bodies on the resource's
// set) in a sequence. Compared: results, out-parameters, the object's bytes (vtable words as codes), frees.
DIFF_TEST(PropSetRes_StringSlots, 0x5522bd)
{
    AllocatorScope allocator;
    // AddRef / Release on each face.
    for (int face = 0; face < 3; ++face)
        for (int slot = 1; slot <= 2; ++slot)
            for (uint32_t count : {0u, 1u, 2u, 9u}) {
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    auto *r = static_cast<uint8_t *>(::operator new(0x50));
                    BuildRes(side, r);
                    WordAt(r, 4) = count;
                    difftest::FreeRecorder rec;
                    runs[side].push_back(CallSlot<uint32_t>(r + kFaces[face], slot));
                    rec.Stop();
                    const bool freed = rec.Freed(r);
                    runs[side].push_back(freed);
                    if (!freed) {
                        runs[side].push_back(WordAt(r, 4));
                        CallSlot<void *>(r, 12, 1u);
                    }
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) ctx.Fail("face %d slot %d count %u differs", face, slot, count);
            }
    // The persist face's key slots.
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t key[4] = {rng(), rng(), rng(), rng()};
        const uint32_t value = rng();
        Blob runs[2];
        for (int side = 0; side < 2; ++side) {
            auto *r = static_cast<uint8_t *>(::operator new(0x50));
            BuildRes(side, r);
            Blob &b = runs[side];
            uint32_t got[4] = {};
            b.push_back(CallSlot<uint8_t>(r, 4, static_cast<const void *>(key)));
            CallSlot<void *>(r, 3, static_cast<void *>(got));
            b.insert(b.end(), got, got + 4);
            for (int s = 5; s <= 7; ++s) b.push_back(CallSlot<uint32_t>(r, s));
            b.push_back(CallSlot<uint8_t>(r, 8 + int(seed % 3), value));
            for (int s = 5; s <= 7; ++s) b.push_back(CallSlot<uint32_t>(r, s));
            b.push_back(CallSlot<uint32_t>(r, 11));
            ResWords(b, side, r);
            const std::vector<uint8_t> rest = ResBytes(r);
            b.insert(b.end(), rest.begin(), rest.end());
            CallSlot<void *>(r, 12, 1u);
        }
        ++ctx.cases;
        if (runs[0] != runs[1]) ctx.Fail("key slots seed %u differ", seed);
    }
    // The set face's slots on each side's resource (a second resource's set as CopyFrom's source).
    using StrPtr = const void *;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        Blob runs[2];
        for (int side = 0; side < 2; ++side) {
            auto *r = static_cast<uint8_t *>(::operator new(0x50));
            auto *other = static_cast<uint8_t *>(::operator new(0x50));
            BuildRes(side, r);
            BuildRes(side, other);
            uint8_t *set = r + Res::kSetFace, *otherSet = other + Res::kSetFace;
            Blob &b = runs[side];
            const GZServiceString k1("alpha"), k2("Beta key longer than sixteen"), k3("ALPHA"), missing("gamma");
            for (uint32_t i = 0; i < seed; ++i) {
                InlineAny value{GameAddress<const void>(gzany::kUint32VTable), i * 7 + seed};
                const GZServiceString key(std::string("k") + char('a' + i));
                CallSlot<uint8_t>(otherSet, 3, static_cast<StrPtr>(&key), static_cast<void *>(&value));
            }
            InlineAny v1{GameAddress<const void>(gzany::kUint32VTable), 11}, v2{GameAddress<const void>(gzany::kUint32VTable), 22};
            b.push_back(CallSlot<uint8_t>(set, 3, static_cast<StrPtr>(&k1), static_cast<void *>(&v1)));
            b.push_back(CallSlot<uint8_t>(set, 3, static_cast<StrPtr>(&k2), static_cast<void *>(&v2)));
            b.push_back(CallSlot<uint8_t>(set, 10, static_cast<StrPtr>(&k3)));
            b.push_back(CallSlot<uint8_t>(set, 10, static_cast<StrPtr>(&missing)));
            b.push_back(CallSlot<void *>(set, 8, static_cast<StrPtr>(&k2)) != nullptr);
            b.push_back(CallSlot<void *>(set, 8, static_cast<StrPtr>(&missing)) != nullptr);
            uint32_t outValue[2] = {GameWord(gzany::kUint32VTable), 0x5a5a};
            b.push_back(CallSlot<uint8_t>(set, 9, static_cast<StrPtr>(&k1), static_cast<void *>(outValue)));
            b.push_back(outValue[1]);
            b.push_back(CallSlot<uint32_t>(set, 11));
            GZServiceString key;
            void *value = nullptr;
            for (bool more = CallSlot<uint8_t>(set, 6, static_cast<void *>(&key), &value) != 0; more;
                 more = CallSlot<uint8_t>(set, 7, static_cast<void *>(&key), &value) != 0) {
                for (char c : key) b.push_back(uint8_t(c));
                b.push_back(value != nullptr);
            }
            FakeOut out{gFakeOutVT, {}, 0};
            const char *text = CallSlot<const char *>(set, 14, 0u, static_cast<void *>(&out));
            b.push_back(text == out.text);
            b.push_back(out.calls);
            for (const char *p = out.text; *p; ++p) b.push_back(uint8_t(*p));
            b.push_back(CallSlot<uint8_t>(set, 4, static_cast<StrPtr>(&k1)));
            b.push_back(CallSlot<uint8_t>(set, 4, static_cast<StrPtr>(&missing)));
            b.push_back(CallSlot<uint32_t>(set, 11));
            CallSlot<void>(set, 12, static_cast<void *>(otherSet));
            b.push_back(CallSlot<uint32_t>(set, 11));
            CallSlot<void>(set, 5);
            b.push_back(CallSlot<uint32_t>(set, 11));
            b.push_back(uint32_t(*reinterpret_cast<std::vector<char> *>(set + 0x18) == std::vector<char>{}));
            ResWords(b, side, r);
            difftest::FreeRecorder rec;
            CallSlot<void *>(r, 12, 1u);
            CallSlot<void *>(other, 12, 1u);
            rec.Stop();
            b.push_back(rec.Freed(r));
            b.push_back(rec.Freed(other));
        }
        ++ctx.cases;
        if (runs[0] != runs[1]) ctx.Fail("set slots seed %u differ (%zu/%zu words)", seed, runs[0].size(), runs[1].size());
    }
}

// The string resource's adapters (what runs for a resource built by the game's constructor, or a direct call): both sides hold
// the game's resource with its three vtables copied and the slots the adapters forward to (QueryInterface 0, the destructor 12)
// replaced by traps of the slot's arity; QueryInterface's AddRef (slot 1) stays the game's. The original side calls the RVA,
// ours the adapter. The constructor adapter is PropSetRes_StringConstructDestroy's (our side of every test above).
namespace {
Blob gResTraps;
uint32_t ResTrapHit(void *, unsigned words) { gResTraps.push_back(0x7a7a0000 | words); return 0; }
void *ResTrap(unsigned words) { return shape::TrapFor<ResTrapHit>(words); }
void *gResTrapVT[3][15];
} // namespace

// Lane GT: both adapters are gone (only the game's vtables reach 0x5553b0 / 0x5520e0, trapped under the constructor's switch);
// the original runs on both sides, which keeps the game-built resource's handling covered. Ours: PropSetRes_QueryInterface and
// the destruction tests above.
DIFF_TEST(PropSetRes_StringAdapters, 0x5520e0)
{
    AllocatorScope allocator;
    const uint32_t games[3] = {Res::kOriginalVTable, Res::kOriginalSetVTable, Res::kOriginalSerializerVTable};
    const int counts[3] = {13, 15, 6};
    for (int f = 0; f < 3; ++f) std::memcpy(gResTrapVT[f], GameAddress<void>(games[f]), counts[f] * sizeof(void *));
    gResTrapVT[0][0] = gResTrapVT[1][0] = gResTrapVT[2][0] = ResTrap(2);
    gResTrapVT[0][12] = gResTrapVT[1][13] = gResTrapVT[2][5] = ResTrap(1);
    const uint32_t iids[] = {0x4bb0d269, 0xebe701f3, 0x456b8f1d, 1, 0x12345678};
    for (int which = 0; which < 2; ++which)
        for (uint32_t arg = 0; arg < 5; ++arg) {
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                auto *r = static_cast<uint8_t *>(::operator new(0x50));
                Game<void *(__thiscall *)(void *)>(0x5519c0)(r); // the game's, on both sides
                for (int f = 0; f < 3; ++f) *reinterpret_cast<void **>(r + kFaces[f]) = gResTrapVT[f];
                Delegate xml{gDelegateVT, 1}, bin{gDelegateVT, 2};
                gResTraps.clear();
                gLog.clear();
                Blob &b = runs[side];
                if (which == 0) {
                    void *out = reinterpret_cast<void *>(0x7777);
                    b.push_back(Game<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x5553b0)(r, iids[arg], &out));
                    b.push_back(out == reinterpret_cast<void *>(0x7777) ? 0xffffffff : uint32_t(static_cast<uint8_t *>(out) - r));
                    b.push_back(WordAt(r, 4));
                    for (int f = 0; f < 3; ++f) *reinterpret_cast<const void **>(r + kFaces[f]) = GameAddress<const void>(games[f]);
                    Game<void *(__thiscall *)(void *, uint32_t)>(0x5520e0)(r, 1);
                } else {
                    auto *disp = reinterpret_cast<Disp *>(r + Res::kSerializerFace);
                    if (arg & 1) disp->mXml = &xml, disp->mBinary = &bin;
                    if (arg & 2) {
                        InlineAny value{GameAddress<const void>(gzany::kUint32VTable), arg};
                        GZServiceString key("some key longer than sixteen");
                        Game<uint8_t(__thiscall *)(void *, const void *, void *)>(0x1bf515)(r + Res::kSetFace, &key, &value);
                    }
                    const uint32_t flags = arg >= 3;
                    difftest::FreeRecorder rec;
                    void *x = Game<void *(__thiscall *)(void *, uint32_t)>(0x5520e0)(r, flags);
                    rec.Stop();
                    const bool freed = rec.Freed(r);
                    b.push_back(x == r);
                    b.push_back(freed);
                    b.insert(b.end(), gLog.begin(), gLog.end());
                    if (!freed) {
                        // The original leaves the game's base vtables (it runs on both sides: the adapter is dropped, lane GT).
                        ResWords(b, 0, r);
                        ::operator delete(r);
                    }
                }
                b.insert(b.end(), gResTraps.begin(), gResTraps.end());
            }
            ++ctx.cases;
            if (runs[0] != runs[1]) ctx.Fail("adapter %d arg %u differs (%zu/%zu words)", which, arg, runs[0].size(), runs[1].size());
        }
}

// ===== the uint32 resource in the standalone shape (lane EQ) =====
// Ours: built by the constructor hook's adapter 0xc2bb25 (our vtables), every call through its own vtable slot. Original side: the
// game's constructor.

// Construction on random-filled memory, then destruction through each face's destructor slot (primary slot 12 0xc2bcf0, the set
// face's slot 13 thunk 0xc2bce0, the dispatcher face's slot 5 thunk 0xc2bce8; flags 0 / 1), with fake delegates (their Release
// order logged) and a property in the set or not. Also AddRef / Release on every face (0x5522b0 / 0x556090 folded, the set face's
// folded thunks 0x5522b5 / 0x556095, the dispatcher face's 0xc2bd2e / 0xc2c2b6), Release at 1 deleting.
DIFF_TEST(PropSetRes_Uint32ConstructDestroy, 0xc2bb25)
{
    AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        std::mt19937 rng(seed);
        auto *a = static_cast<uint8_t *>(::operator new(0x44));
        auto *b = static_cast<uint8_t *>(::operator new(0x44));
        for (int i = 0; i < 0x44; ++i) a[i] = b[i] = uint8_t(rng());
        uint8_t *objs[2] = {a, b};
        Blob built[2];
        for (int side = 0; side < 2; ++side) {
            built[side].push_back(BuildRes32(side, objs[side]) == objs[side]);
            Res32Words(built[side], side, objs[side]);
            const std::vector<uint8_t> rest = Res32Bytes(objs[side]);
            built[side].insert(built[side].end(), rest.begin(), rest.end());
            // AddRef / Release through each face (counts 1, 2, 3, then down to 1).
            for (int f = 0; f < 3; ++f) built[side].push_back(CallSlot<uint32_t>(objs[side] + kFaces32[f], 1));
            for (int f = 1; f < 3; ++f) built[side].push_back(CallSlot<uint32_t>(objs[side] + kFaces32[f], 2));
            built[side].push_back(WordAt(objs[side], 4));
        }
        ++ctx.cases;
        if (built[0] != built[1]) ctx.Fail("seed %u: constructed objects differ", seed);

        const bool withDelegates = seed & 1, withProperty = seed & 2;
        const uint32_t flags = (seed >> 2) & 1;
        const int face = int(seed % 4); // 3: Release at 1 through the dispatcher face (0xc2c2b6), which deletes
        const int slots[3] = {12, 13, 5};
        Delegate xmlA{gDelegateVT, 1}, binA{gDelegateVT, 2}, xmlB{gDelegateVT, 1}, binB{gDelegateVT, 2};
        Blob logs[2];
        size_t sizes[2][16] = {}, counts[2];
        bool freed[2];
        Blob after[2];
        for (int side = 0; side < 2; ++side) {
            uint8_t *r = objs[side];
            auto *disp = reinterpret_cast<Disp32 *>(r + Res32::kSerializerFace);
            if (withDelegates) {
                disp->mXml = side ? &xmlB : &xmlA;
                disp->mBinary = side ? &binB : &binA;
            }
            if (withProperty) {
                InlineAny value{GameAddress<const void>(gzany::kUint32VTable), seed};
                const uint32_t key = seed * 7;
                CallSlot<uint8_t>(r + Res32::kSetFace, 3, &key, static_cast<void *>(&value));
            }
            gLog.clear();
            gLog.reserve(64);
            difftest::FreeRecorder rec;
            void *x = face < 3 ? CallSlot<void *>(r + kFaces32[face], slots[face], flags)
                               : reinterpret_cast<void *>(uintptr_t(CallSlot<uint32_t>(r + Res32::kSerializerFace, 2)));
            rec.Stop();
            freed[side] = rec.Freed(r);
            counts[side] = rec.SortedSizes(sizes[side], 16);
            logs[side] = gLog;
            logs[side].push_back(face < 3 ? x == r : uint32_t(uintptr_t(x)));
            if (!freed[side]) {
                Res32Words(after[side], side, r);
                std::vector<uint8_t> rest = Res32Bytes(r);
                if (withProperty) std::memset(rest.data() + 0x1c + 8, 0, 12); // the set's freed vector (heap addresses)
                if (withDelegates) std::memset(rest.data() + 0x38, 0, 8);     // each side's own stack fakes (logged)
                after[side].insert(after[side].end(), rest.begin(), rest.end());
            }
        }
        ++ctx.cases;
        if (logs[0] != logs[1] || freed[0] != freed[1] || counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], sizeof(sizes[0])) != 0 ||
            after[0] != after[1])
            ctx.Fail("seed %u destroy (face %d): log %zu/%zu freed %d/%d frees %zu/%zu after %s", seed, face, logs[0].size(), logs[1].size(),
                     freed[0], freed[1], counts[0], counts[1], after[0] == after[1] ? "same" : "differs");
        if (!freed[0]) ::operator delete(a);
        if (!freed[1]) ::operator delete(b);
    }
}

// The uint32 resource's adapters (QueryInterface 0xc2c126, Destruct 0xc2bcb3, DeletingDestructor 0xc2bcf0) on a resource the game
// built (both sides), whose vtables are copies of the game's with QueryInterface and the destructor slots trapped (the adapters
// must run our bodies, not dispatch; AddRef stays the game's: QI calls it).
DIFF_TEST(PropSetRes_Uint32Adapters, 0xc2bcf0)
{
    AllocatorScope allocator;
    const uint32_t games[3] = {Res32::kOriginalVTable, Res32::kOriginalSetVTable, Res32::kOriginalSerializerVTable};
    const int counts[3] = {13, 15, 6};
    static void *trapVT[3][15];
    for (int f = 0; f < 3; ++f) std::memcpy(trapVT[f], GameAddress<void>(games[f]), counts[f] * sizeof(void *));
    trapVT[0][0] = trapVT[1][0] = trapVT[2][0] = ResTrap(2);
    trapVT[0][12] = trapVT[1][13] = trapVT[2][5] = ResTrap(1);
    const uint32_t iids[] = {0xebb10cdd, 0xebe701f3, 0x456b8f1d, 1, 0x4bb0d269};
    for (int which = 0; which < 3; ++which)
        for (uint32_t arg = 0; arg < 5; ++arg) {
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                auto *r = static_cast<uint8_t *>(::operator new(0x44));
                Game<void *(__thiscall *)(void *)>(0xc2bb25)(r); // the game's, on both sides
                for (int f = 0; f < 3; ++f) *reinterpret_cast<void **>(r + kFaces32[f]) = trapVT[f];
                Delegate xml{gDelegateVT, 1}, bin{gDelegateVT, 2};
                gResTraps.clear();
                gLog.clear();
                Blob &b = runs[side];
                auto *self = reinterpret_cast<Res32 *>(r);
                if (which == 0) {
                    void *out = reinterpret_cast<void *>(0x7777);
                    // Adapter dropped (lane GT: only the game's vtable reaches 0xc2c126): the original on both sides.
                    b.push_back(Game<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xc2c126)(r, iids[arg], &out));
                    b.push_back(out == reinterpret_cast<void *>(0x7777) ? 0xffffffff : uint32_t(static_cast<uint8_t *>(out) - r));
                    b.push_back(WordAt(r, 4));
                    for (int f = 0; f < 3; ++f) *reinterpret_cast<const void **>(r + kFaces32[f]) = GameAddress<const void>(games[f]);
                    Game<void *(__thiscall *)(void *, uint32_t)>(0xc2bcf0)(r, 1);
                } else {
                    auto *disp = reinterpret_cast<Disp32 *>(r + Res32::kSerializerFace);
                    if (arg & 1) disp->mXml = &xml, disp->mBinary = &bin;
                    if (arg & 2) {
                        InlineAny value{GameAddress<const void>(gzany::kUint32VTable), arg};
                        const uint32_t key = arg;
                        Game<uint8_t(__thiscall *)(void *, const void *, void *)>(0x1bf49c)(r + Res32::kSetFace, &key, &value);
                    }
                    const uint32_t flags = arg >= 3;
                    difftest::FreeRecorder rec;
                    void *x = r;
                    if (which == 1)
                        side == 0 ? Game<void(__thiscall *)(void *)>(0xc2bcb3)(r) : Res32Adapters::Destruct(self, nullptr);
                    else
                        x = Game<void *(__thiscall *)(void *, uint32_t)>(0xc2bcf0)(r, flags); // adapter dropped (lane GT)
                    rec.Stop();
                    const bool freed = rec.Freed(r);
                    b.push_back(x == r);
                    b.push_back(freed);
                    b.insert(b.end(), gLog.begin(), gLog.end());
                    if (!freed) {
                        Res32Words(b, which == 2 ? 0 : side, r); // 2: the original on both sides (lane GT)
                        ::operator delete(r);
                    }
                }
                b.insert(b.end(), gResTraps.begin(), gResTraps.end());
            }
            ++ctx.cases;
            if (runs[0] != runs[1]) ctx.Fail("uint32 adapter %d arg %u differs (%zu/%zu words)", which, arg, runs[0].size(), runs[1].size());
        }
}

// ===== cGZPropertySetFactory in the standalone shape (lane EF) =====
// Our side: a factory from our Create (our vtables), every call through its own vtable slot. Original side: the game's
// constructor / Create (game vtables). The vtable words are the only bytes that differ by design (FactoryWords).
namespace {
using Factory = cGZPropertySetFactory;
using FactoryAdapters = cGZPropertySetFactoryAdapters;


// Our two vtables (from a factory built by our Create) and our cRZUnknown vtable (what the destructor leaves at +4).
struct FactoryVTables {
    uint32_t face[2];
    uint32_t unknown;
};
FactoryVTables OurFactoryVTables()
{
    Factory *f = Factory::Create();
    FactoryVTables v;
    std::memcpy(v.face, f, sizeof(v.face));
    f->~Factory();
    v.unknown = reinterpret_cast<uint32_t *>(f)[1];
    ::operator delete(f);
    return v;
}

// Each vtable word as a code: 1 the side's own vtable of that face, 2 the side's cRZUnknown vtable (after destruction),
// anything else raw (then it differs between the sides). Ours must not be the game's.
void FactoryWords(Blob &b, int side, const void *object)
{
    const FactoryVTables ours = OurFactoryVTables();
    const uint32_t games[2] = {GameWord(Factory::kOriginalVTable), GameWord(Factory::kOriginalUnknownVTable)};
    const uint32_t gameUnknown = GameWord(0xda2fd0);
    for (int i = 0; i < 2; ++i) {
        const uint32_t word = static_cast<const uint32_t *>(object)[i];
        const uint32_t own = side == 0 ? games[i] : ours.face[i];
        const uint32_t unknown = side == 0 ? gameUnknown : ours.unknown;
        b.push_back(word == own && ours.face[i] != games[i] ? 1u : word == unknown && ours.unknown != gameUnknown ? 2u : word);
    }
}
} // namespace

DIFF_TEST(PropSetFactory_ConstructCreate, 0xc2bb7a)
{
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        uint8_t mem[2][0xc];
        std::mt19937 rng(seed);
        for (int i = 0; i < 0xc; ++i) mem[0][i] = mem[1][i] = uint8_t(rng());
        Blob runs[2];
        for (int side = 0; side < 2; ++side) {
            void *r = side == 0 ? Game<void *(__thiscall *)(void *)>(0xc2bb7a)(mem[side]) : FactoryAdapters::Construct(mem[side], nullptr);
            runs[side].push_back(r == mem[side]);
            FactoryWords(runs[side], side, mem[side]);
            runs[side].push_back(reinterpret_cast<uint32_t *>(mem[side])[2]);
        }
        ++ctx.cases;
        if (runs[0] != runs[1]) ctx.Fail("seed %u: constructed factories differ", seed);
    }
    Blob runs[2];
    for (int side = 0; side < 2; ++side) {
        void *f = side == 0 ? Game<void *(__cdecl *)()>(0xc2be41)() : static_cast<void *>(Factory::Create());
        runs[side].push_back(f != nullptr);
        FactoryWords(runs[side], side, f);
        runs[side].push_back(static_cast<uint32_t *>(f)[2]);
        ::operator delete(f, 0xc);
    }
    ++ctx.cases;
    if (runs[0] != runs[1]) ctx.Fail("Create differs");
}

// The slots no other test calls, on each side's own factory: QueryInterface, AddRef and Release on both faces, the +4 face's
// deleting destructor (flags 0 and 1), RemoveRef and the count. Compared: result, out-parameter (as a face offset or the
// untouched sentinel), count, vtable words and whether the object was freed.
DIFF_TEST(PropSetFactory_Slots, 0xc14428)
{
    struct Case {
        uint32_t face, slot;
    };
    const Case cases[] = {{0, 0}, {0, 1}, {0, 2}, {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}};
    const uint32_t ids[] = {Factory::kFactoryIid, 1, 0x456b8f1d, 0, 0xa56b8f18};
    for (const Case &c : cases)
        for (uint32_t count : {0u, 1u, 2u, 7u})
            for (uint32_t arg = 0; arg < 5; ++arg) {
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    auto *f = static_cast<uint8_t *>(side == 0 ? Game<void *(__cdecl *)()>(0xc2be41)() : static_cast<void *>(Factory::Create()));
                    reinterpret_cast<uint32_t *>(f)[2] = count;
                    const void *face = f + c.face;
                    Blob &b = runs[side];
                    difftest::FreeRecorder rec;
                    void *out = reinterpret_cast<void *>(0x5a5a5a5a);
                    switch (c.slot) {
                    case 0:
                        b.push_back(CallSlot<uint8_t>(face, 0, ids[arg], &out));
                        b.push_back(out == reinterpret_cast<void *>(0x5a5a5a5a) ? 0xeeeeeeeeu : uint32_t(static_cast<uint8_t *>(out) - f));
                        break;
                    case 3: b.push_back(CallSlot<void *>(face, 3, arg & 1) == f); break;
                    default: b.push_back(CallSlot<uint32_t>(face, c.slot)); break;
                    }
                    rec.Stop();
                    const bool freed = rec.Freed(f);
                    b.push_back(freed);
                    if (!freed) {
                        FactoryWords(b, side, f);
                        b.push_back(reinterpret_cast<uint32_t *>(f)[2]);
                        ::operator delete(f, 0xc);
                    }
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) ctx.Fail("face %u slot %u count %u arg %u differ", c.face, c.slot, count, arg);
            }
}

namespace {

uint32_t gKey[4];

// The record fake (F(0)): QI hands out F(1) for the iid in value[0] when value[1] is set; slot 3 GetKey.
uint32_t RecordQI(Obj *self, uint32_t *args)
{
    if (args[0] != self->value[0] || !self->value[1]) return 0;
    *reinterpret_cast<void **>(args[1]) = &F(1);
    return 1;
}
// The resource fake (F(2)): the set face F(3) for value[0] when value[1], the serializer F(4) when value[2].
uint32_t ResourceQI(Obj *self, uint32_t *args)
{
    void **out = reinterpret_cast<void **>(args[1]);
    if (args[0] == self->value[0] && self->value[1]) return *out = &F(3), 1;
    if (args[0] == 0xebe701f3 && self->value[2]) return *out = &F(4), 1;
    return 0;
}

void SetupReadWrite(uint32_t type, uint32_t streamIid, bool stream, uint32_t setIid, bool set, bool serializer, uint32_t result)
{
    ResetAll();
    gKey[0] = type;
    gKey[1] = 0x11;
    gKey[2] = 0x22;
    gKey[3] = 0x33;
    AddRange(gKey, sizeof(gKey), 0x6e000000);
    F(0).Set(0, "dp", 0, &RecordQI);
    F(0).value[0] = streamIid;
    F(0).value[1] = stream;
    F(0).Set(3, 0, gKey);
    F(2).Set(0, "dp", 0, &ResourceQI);
    F(2).value[0] = setIid;
    F(2).value[1] = set;
    F(2).value[2] = serializer;
    F(4).Set(3, "pp", result);
    F(4).Set(4, "ppd", result);
}

Blob Capture(uint32_t ok)
{
    Blob b = gEv;
    CaptureFakes(b, kValueKinds);
    b.push_back(ok);
    return b;
}

} // namespace

DIFF_TEST(PropSetFactory_ReadWrite, 0xc2c173)
{
    const uint32_t types[] = {0xebfee33f, 0xebfee342, 0xebfee345, 0xebfee349, 0x12345678, 0xebfee340};
    for (int write = 0; write < 2; ++write)
        for (uint32_t type : types)
            for (int mask = 0; mask < 8; ++mask)
                for (uint32_t result : {0u, 1u, 0x81u}) {
                    const uint32_t setIid = (type == 0xebfee345 || type == 0xebfee349) ? 0xebb10cdd : 0x4bb0d269;
                    Blob runs[2];
                    for (int side = 0; side < 2; ++side) {
                        SetupReadWrite(type, write ? 0xc51b11cb : 0x051b11bd, mask & 1, setIid, mask & 2, mask & 4, result);
                        uint8_t ok;
                        if (side == 0) {
                            uint8_t factory[0xc] = {};
                            Game<void *(__thiscall *)(void *)>(0xc2bb7a)(factory);
                            ok = Game<uint8_t(__thiscall *)(void *, void *, void *)>(write ? 0xc2c2be : 0xc2c173)(factory, &F(2), &F(0));
                        } else {
                            Factory *factory = Factory::Create();
                            ok = CallSlot<uint8_t>(factory, write ? 6 : 5, static_cast<void *>(&F(2)), static_cast<void *>(&F(0)));
                            delete factory;
                        }
                        runs[side] = Capture(ok);
                    }
                    ++ctx.cases;
                    if (runs[0] != runs[1])
                        ctx.Fail("%s type %08x mask %d result %x: %zu/%zu words, ok %u/%u", write ? "Write" : "Read", type, mask, result,
                                 runs[0].size(), runs[1].size(), runs[0].back(), runs[1].back());
                }
}

namespace {
// Slot 4 of the fake factory: writes value[0] to *out and answers value[1].
uint32_t FactoryCreate(Obj *self, uint32_t *args)
{
    if (self->value[2]) *reinterpret_cast<void **>(args[2]) = &F(6);
    return self->value[1];
}
} // namespace

DIFF_TEST(PropSetFactory_CreateInstance, 0xc2bd36)
{
    for (int mask = 0; mask < 8; ++mask)
        for (uint32_t type : {0xebfee33fu, 0x5u}) {
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                ResetAll();
                gKey[0] = type;
                AddRange(gKey, sizeof(gKey), 0x6e000000);
                F(0).Set(3, 0, gKey);
                F(5).Set(4, "ddpdd", 0, &FactoryCreate);
                F(5).value[1] = (mask & 1) ? 0x41 : 0;
                F(5).value[2] = mask & 2;
                F(5).Set(5, "dp", (mask & 4) ? 0x80 : 0);
                void *out = reinterpret_cast<void *>(0x4444);
                // Our side: slot 3 of our vtable, with `this` the fake (CreateInstance reaches the object only through its
                // slots 4 and 5).
                using Fn = uint8_t(__thiscall *)(void *, void *, uint32_t, void **, uint32_t, uint32_t);
                const Fn ours = reinterpret_cast<const Fn *>(uintptr_t(OurFactoryVTables().face[0]))[3];
                const uint8_t ok = side == 0 ? Game<Fn>(0xc2bd36)(&F(5), &F(0), 0x99, &out, 0xa4, 0xa5)
                                             : ours(&F(5), &F(0), 0x99, &out, 0xa4, 0xa5);
                runs[side] = Capture(ok);
                runs[side].push_back(svcfx::NormOut(uint32_t(uintptr_t(out)), 0x4444));
            }
            ++ctx.cases;
            if (runs[0] != runs[1]) ctx.Fail("mask %d type %08x differs (ok %u/%u)", mask, type, runs[0][runs[0].size() - 2], runs[1][runs[1].size() - 2]);
        }
}

// Whether the 4 bytes at p lie in committed, readable memory (the face probe below may point before an object).
static bool ReadableWord(const void *p)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const uint8_t *>(p) + 4 <= static_cast<const uint8_t *>(info.BaseAddress) + info.RegionSize;
}

// Real resources for the four types (unknown types crash the original: 0xc2bdf7-0xc2be08 calls slot 8 of a null
// pointer, ORIGINAL-BUG in the source; left out). Compared: result, the face handed out (as an offset), the object's
// bytes and reference count while it lives, and the blocks freed (a refused iid deletes the new resource).
DIFF_TEST(PropSetFactory_CreateInstance2, 0xc2bd7c)
{
    AllocatorScope allocator;
    const uint32_t iids[] = {0x4bb0d269, 0xebb10cdd, 0xebe701f3, 0x456b8f1d, 1, 0x12345678};
    for (uint32_t type : kTypes)
        for (uint32_t iid : iids) {
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                uint8_t factory[0xc] = {};
                Factory *ours = side == 0 ? nullptr : Factory::Create();
                if (side == 0) Game<void *(__thiscall *)(void *)>(0xc2bb7a)(factory);
                void *out = reinterpret_cast<void *>(0x4444);
                difftest::FreeRecorder rec;
                const uint8_t ok = side == 0 ? Game<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(0xc2bd7c)(
                                                   factory, type, iid, &out, 0, 0)
                                             : CallSlot<uint8_t>(ours, 4, type, iid, &out, 0u, 0u);
                rec.Stop();
                delete ours;
                size_t sizes[8] = {};
                const size_t frees = rec.SortedSizes(sizes, 8);
                runs[side] = {ok, uint32_t(frees)};
                runs[side].insert(runs[side].end(), sizes, sizes + 8);
                if (out != reinterpret_cast<void *>(0x4444)) {
                    // Find the object: the face is +0, +0x1c or +0x34/+0x40; its primary vtable sits at the start.
                    const bool stringKind = type == 0xebfee33f || type == 0xebfee342;
                    const uint32_t offsets[] = {0, 0x1c, stringKind ? 0x40u : 0x34u};
                    uint8_t *object = nullptr;
                    uint32_t face = 0xff;
                    // Our side's resources are our classes (our vtables, lanes EL / EQ).
                    const uint32_t primary = side == 1 ? (stringKind ? OurResVTables() : OurRes32VTables()).live[0] : GameWord(stringKind ? 0xdd428c : 0xe992b0);
                    // Probe the smallest offset first and stop at the first match: a probe below the object can fall
                    // off the start of a heap region (an access violation at a page boundary in a full run), so a
                    // candidate whose page isn't committed and readable is skipped.
                    for (uint32_t off : offsets) {
                        auto *candidate = static_cast<uint8_t *>(out) - off;
                        if (!ReadableWord(candidate)) continue;
                        const uint32_t vt = *reinterpret_cast<uint32_t *>(candidate);
                        if (vt == primary) {
                            object = candidate, face = off;
                            break;
                        }
                    }
                    runs[side].push_back(face);
                    if (object) {
                        const size_t size = stringKind ? 0x50 : 0x44;
                        if (stringKind) {
                            ResWords(runs[side], side, object);
                            const std::vector<uint8_t> rest = ResBytes(object);
                            for (size_t i = 0; i < size; i += 4) runs[side].push_back(*reinterpret_cast<const uint32_t *>(rest.data() + i));
                        } else {
                            Res32Words(runs[side], side, object);
                            const std::vector<uint8_t> rest = Res32Bytes(object);
                            for (size_t i = 0; i < size; i += 4) runs[side].push_back(*reinterpret_cast<const uint32_t *>(rest.data() + i));
                        }
                        VirtualSlot<uint32_t(__thiscall *)(void *)>(object, 2)(object); // game Release: deletes it
                    }
                }
            }
            ++ctx.cases;
            if (runs[0] != runs[1]) ctx.Fail("type %08x iid %08x: ok %u/%u frees %u/%u", type, iid, runs[0][0], runs[1][0], runs[0][1], runs[1][1]);
        }
}

// ===== The factory's adapters: our functions behind the old entry points, on a factory built by the game's Create =====
// What runs when the Create hook is off or the game calls an old entry directly. Both sides hold the original's factory; the
// original side calls the RVA, ours the adapter. The game's vtable is copied with a trap (of the slot's arity) in the slot each
// adapter forwards to, so an adapter that dispatched through the object's vtable (in game: back into its own hook) logs it.
// CreateInstance legitimately calls slots 4 and 5: recording fakes there.
namespace {
Blob gTrapLog;
void *gFactoryTrapVT[7];
uint32_t TrapHit(void *, unsigned words) { gTrapLog.push_back(0x7a7a0000 | words); return 0; }
void *Trap(unsigned words) { return shape::TrapFor<TrapHit>(words); }
uint32_t gMadeResource;
uint32_t __fastcall FakeCreateByType(void *, void *, uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    gTrapLog.insert(gTrapLog.end(), {0x44u, type, riid, a4, a5});
    *out = &gMadeResource;
    return type & 0x100 ? 0 : 0x41;
}
uint32_t __fastcall FakeRead(void *, void *, void *resource, void *record)
{
    gTrapLog.insert(gTrapLog.end(), {0x55u, uint32_t(resource == &gMadeResource), uint32_t(record == &F(0))});
    return 0x80;
}
} // namespace

// Lane GT: the four adapters are gone (only the game's vtable reaches CreateInstance, CreateInstance2, Read and Write, trapped
// under the factory constructor's switch): the original runs on both sides. Ours: the PropSetFactory tests above.
DIFF_TEST(PropSetFactory_Adapters, 0xc2c2be)
{
    AllocatorScope allocator;
    for (int which = 0; which < 4; ++which)
        for (uint32_t type : {0xebfee33fu, 0xebfee342u, 0xebfee345u, 0xebfee349u, 0xebfee440u})
            for (int mask = 0; mask < 8; ++mask) {
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    auto *self = static_cast<Factory *>(Game<void *(__cdecl *)()>(0xc2be41)()); // the game's, on both sides
                    std::memcpy(gFactoryTrapVT, GameAddress<void>(Factory::kOriginalVTable), sizeof(gFactoryTrapVT));
                    gFactoryTrapVT[3] = Trap(5);
                    gFactoryTrapVT[4] = Trap(5);
                    gFactoryTrapVT[5] = Trap(2);
                    gFactoryTrapVT[6] = Trap(2);
                    *reinterpret_cast<void **>(self) = gFactoryTrapVT;
                    gTrapLog.clear();
                    Blob &b = runs[side];
                    switch (which) {
                    case 0: { // CreateInstance (slots 4 and 5 recording fakes)
                        gFactoryTrapVT[4] = reinterpret_cast<void *>(&FakeCreateByType);
                        gFactoryTrapVT[5] = reinterpret_cast<void *>(&FakeRead);
                        ResetAll();
                        gKey[0] = type;
                        AddRange(gKey, sizeof(gKey), 0x6e000000);
                        F(0).Set(3, 0, gKey);
                        void *out = nullptr;
                        const auto record = reinterpret_cast<ifaces::cIGZPersistDBRecord *>(&F(0));
                        (void)record;
                        b.push_back(Game<uint8_t(__thiscall *)(void *, void *, uint32_t, void **, uint32_t, uint32_t)>(0xc2bd36)(
                            self, &F(0), 0x99, &out, 0xa4, 0xa5));
                        b.push_back(out == &gMadeResource);
                        break;
                    }
                    case 1: { // CreateInstance2 (real resources; an unknown type crashes the original, left out)
                        if (type == 0xebfee440u) break;
                        void *out = reinterpret_cast<void *>(0x4444);
                        const uint32_t iid = mask & 1 ? 0x4bb0d269u : mask & 2 ? 0xebb10cddu : 0x12345678u;
                        b.push_back(Game<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(0xc2bd7c)(
                            self, type, iid, &out, 0, 0));
                        b.push_back(out != reinterpret_cast<void *>(0x4444));
                        if (out != reinterpret_cast<void *>(0x4444)) VirtualSlot<uint32_t(__thiscall *)(void *)>(out, 2)(out);
                        break;
                    }
                    default: { // Read / Write
                        const bool write = which == 3;
                        const uint32_t setIid = (type == 0xebfee345 || type == 0xebfee349) ? 0xebb10cdd : 0x4bb0d269;
                        SetupReadWrite(type, write ? 0xc51b11cb : 0x051b11bd, true, setIid, mask & 1, mask & 2, mask & 4 ? 0x81 : 0);
                        const auto resource = reinterpret_cast<cIGZUnknown *>(&F(2));
                        const auto record = reinterpret_cast<cIGZUnknown *>(&F(0));
                        (void)resource, (void)record;
                        const uint8_t ok = Game<uint8_t(__thiscall *)(void *, void *, void *)>(write ? 0xc2c2be : 0xc2c173)(self, &F(2), &F(0));
                        b = Capture(ok);
                        break;
                    }
                    }
                    b.insert(b.end(), gTrapLog.begin(), gTrapLog.end());
                    ::operator delete(self, 0xc);
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) {
                    size_t at = 0;
                    while (at < runs[0].size() && at < runs[1].size() && runs[0][at] == runs[1][at]) ++at;
                    ctx.Fail("adapter %d type %08x mask %d differ (%zu/%zu words, first at %zu: %08x/%08x)", which, type, mask, runs[0].size(),
                             runs[1].size(), at, at < runs[0].size() ? runs[0][at] : 0, at < runs[1].size() ? runs[1][at] : 0);
                }
            }
}
