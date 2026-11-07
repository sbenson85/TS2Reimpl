// Differential tests for cTSSaveGameInfo (src/ts/cTSSaveGameInfo.cpp, lane DV).
//
// Covered, each run on the original by RVA and on ours:
//   0xcd7a10 Construct, 0xcd7aa0 Create, 0xcd7a30 Destruct, 0xcd7a80 DeletingDestructor, 0xcd8720 QueryInterface,
//   0xcd8750 Release, 0xcd7e30 EnsureProperties, 0xcd8a50 SetUint32, 0xcd88c0 SetString, 0xcd7b10 GetUint32,
//   0xcd7c20 GetString, 0xcd7d50 GetIndices, 0xcd8770 Save, 0xcd7ed0 Load, 0xcd7fa0 Fill
//
// Method. The accessors run on a real property set: EnsureProperties builds a cGZPropertySetResourceString with the
// original constructor (0x5519c0) on both sides, so the set code is the original's on both. Random sequences of
// Set/Get/GetIndices over indices 0..12 and a few large ones (0x830972, atoi, parses them: "12" -> 12) compare the
// results, the out-values and a dump of the whole set (key text, value type, payload or text and the text object's
// reference count) after every step. Save / Load / Fill run against recording fakes (tests/ts_services_fixture.h):
// the app service (GetServiceObject cell 0x10c9378), the resource manager, the property set's persist face and its
// key, the game-name object and the language service; the event logs and reference counts are compared. Fill's
// cRZVersion is redirected on both sides (the original's 0x1c98d6 / 0x1c9b6d / 0x1c9901 and our appbase::cRZVersion
// members) to fakes: the real one reads the executable's version resource through trapped imports.
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "app/ReportFinish.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPropertySet.h"
#include "rz/cRZString.h"
#include "ts/cTSSaveGameInfo.h"

#include <Windows.h>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {2, "dd"}, {4, "--dddd"}, {7, "dd"}};

using svcfx::Blob;
using svcfx::F;
using Info = cTSSaveGameInfo;

constexpr uint32_t kServiceCell = 0x10c9378; // GetServiceObject's global
constexpr uint32_t kLanguageCell = 0x10f51c8;

// ---- the two sides ----------------------------------------------------------------------------------------------

struct Side {
    difftest::Context &ctx;
    bool orig;

    void Construct(Info *i) { orig ? (void)ctx.Original<void *(__thiscall *)(void *)>(0xcd7a10)(i) : (void)i->Construct(); }
    Info *Create() { return orig ? static_cast<Info *>(ctx.Original<void *(__cdecl *)()>(0xcd7aa0)()) : Info::Create(); }
    void Destruct(Info *i) { orig ? ctx.Original<void(__thiscall *)(void *)>(0xcd7a30)(i) : i->Destruct(); }
    void *DeletingDestructor(Info *i, uint32_t f)
    {
        return orig ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xcd7a80)(i, f) : i->DeletingDestructor(f);
    }
    uint32_t QI(Info *i, uint32_t iid, void **out)
    {
        return orig ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xcd8720)(i, iid, out) : i->QueryInterface(iid, out);
    }
    uint32_t Release(Info *i) { return orig ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xcd8750)(i) : i->Release(); }
    uint32_t Ensure(Info *i) { return orig ? ctx.Original<uint8_t(__thiscall *)(void *)>(0xcd7e30)(i) : i->EnsureProperties(); }
    void Fill(Info *i) { orig ? ctx.Original<void(__thiscall *)(void *)>(0xcd7fa0)(i) : i->Fill(); }
    void SetUint32(Info *i, uint32_t k, uint32_t v)
    {
        orig ? ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xcd8a50)(i, k, v) : i->SetUint32(k, v);
    }
    void SetString(Info *i, uint32_t k, cIGZString *v)
    {
        orig ? ctx.Original<void(__thiscall *)(void *, uint32_t, void *)>(0xcd88c0)(i, k, v) : i->SetString(k, v);
    }
    uint32_t GetUint32(Info *i, uint32_t k, uint32_t *o)
    {
        return orig ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t *)>(0xcd7b10)(i, k, o) : i->GetUint32(k, o);
    }
    uint32_t GetString(Info *i, uint32_t k, cIGZString *o)
    {
        return orig ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *)>(0xcd7c20)(i, k, o) : i->GetString(k, o);
    }
    void GetIndices(Info *i, Info::IndexList *o)
    {
        orig ? ctx.Original<void(__thiscall *)(void *, void *)>(0xcd7d50)(i, o) : i->GetIndices(o);
    }
    uint32_t Save(Info *i, uint32_t g, uint32_t n)
    {
        return orig ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0xcd8770)(i, g, n) : i->Save(g, n);
    }
    uint32_t Load(Info *i, uint32_t g, uint32_t n)
    {
        return orig ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0xcd7ed0)(i, g, n) : i->Load(g, n);
    }
};

// ---- helpers ----------------------------------------------------------------------------------------------------

void PushText(Blob &b, const char *text)
{
    if (!text) {
        b.push_back(0xffffffffu);
        return;
    }
    const size_t n = std::strlen(text);
    b.push_back(uint32_t(n));
    for (size_t i = 0; i < n; ++i) b.push_back(uint8_t(text[i]));
}

template <typename R, typename... A> R Slot(void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(object, slot)(object, args...);
}

// The whole set, in its order: key text, value type, and the payload (AnyUint32) or the text and its object's count.
void DumpSet(Blob &b, Info *info)
{
    void *face = info->mProperties;
    b.push_back(face ? 1u : 0u);
    if (!face) return;
    GZServiceString key;
    void *value = nullptr;
    unsigned guard = 0;
    for (bool more = Slot<uint8_t>(face, 6, &key, &value) != 0; more && guard < 64; ++guard) {
        PushText(b, key.c_str());
        const uint32_t type = Slot<uint32_t>(value, 4);
        b.push_back(type);
        const uint32_t payload = static_cast<uint32_t *>(value)[1];
        if (type == gzany::kTypeString) {
            if (payload) {
                PushText(b, Slot<const char *>(reinterpret_cast<void *>(uintptr_t(payload)), 5));
                b.push_back(reinterpret_cast<const cRZString *>(uintptr_t(payload))->mRefCount);
            } else {
                b.push_back(0xeeeeeeeeu);
            }
        } else {
            b.push_back(payload);
        }
        more = Slot<uint8_t>(face, 7, &key, &value) != 0;
    }
}

void ReleaseSet(Info *info)
{
    if (info->mProperties) Slot<uint32_t>(info->mProperties, 2);
    info->mProperties = nullptr;
}

const char *const kTexts[] = {"", "The Sims 2", "1.18.0.187", "a text that is longer than fifteen characters", "x"};
const uint32_t kIndices[] = {0, 1, 2, 3, 5, 9, 10, 11, 12, 99, 123, 0xffffffffu};

// cRZString's vtable with slot 6 (Data) answering another text: SetString must read slot 5 (ToChar).
const char *__fastcall OddData(void *, void *) { return "from Data"; }
const void *OddStringVTable()
{
    static void *table[40];
    if (!table[0]) {
        std::memcpy(table, GameAddress<void *>(cRZString::kVTable), sizeof(table));
        table[6] = reinterpret_cast<void *>(&OddData);
    }
    return table;
}

uint32_t PickIndex(std::mt19937 &rng)
{
    return kIndices[rng() % (sizeof(kIndices) / sizeof(kIndices[0]))];
}

// ---- Fill's cRZVersion fakes ------------------------------------------------------------------------------------

const char *gVersionText = nullptr;
uint32_t gVersionResult = 1;

void *__fastcall FakeVersionConstruct(void *self, void *)
{
    std::memset(self, 0, appbase::cRZVersion::kSize);
    svcfx::Ev(svcfx::evNote);
    svcfx::Ev(1);
    return self;
}
uint32_t __fastcall FakeVersionGetFileVersion(void *, void *, cRZString *out)
{
    svcfx::Ev(svcfx::evNote);
    svcfx::Ev(2);
    if (gVersionText)
        out->mString.assign(gVersionText);
    else
        out->mString.erase(0);
    return gVersionResult;
}
void __fastcall FakeVersionDestruct(void *, void *)
{
    svcfx::Ev(svcfx::evNote);
    svcfx::Ev(3);
}

// Five bytes at `site` become a jmp to `target` while alive (an original function in the mapped image, or one of ours).
struct Patch {
    uint8_t *site;
    uint8_t saved[5];
    Patch(void *where, const void *target) : site(static_cast<uint8_t *>(where))
    {
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved, site, 5);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Patch()
    {
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Patch(const Patch &) = delete;
    Patch &operator=(const Patch &) = delete;
};

template <typename M> void *Ours(M member)
{
    void *p;
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

// ---- fakes for Save / Load / Fill (svcfx objects) ------------------------------------------------------------------
// F(0) app service: slot 5 GetSystemService(id, iid, out) hands out F(1) when value[0] is set; slot 37 the game-name
//      object (ret).
// F(1) persist system: slot 16 SaveResource (ret), slot 3 GetResourceManager -> F(7); F(7) slot 3 GetResource(key, iid,
//      out, 0, 0): logs the key, hands out F(3)
//      when value[0] is set, returns value[1].
// F(2) property set face (Save): slot 0 QI(iid, out): hands out F(4) when value[0] is set, returns value[1].
// F(4) persist resource: slot 3 GetKey(out) writes {value[2..5]}, slot 4 SetKey(key) logs the key.
// F(5) game-name object: slot 5 returns ret (a text or null). F(6) language service: slot 7.
uint32_t AppGetService(svcfx::Obj *self, uint32_t *args)
{
    if (self->value[0]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(1);
        ++F(1).refs;
    }
    return self->value[1];
}
uint32_t RmGetResource(svcfx::Obj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) svcfx::Ev(key[i]);
    svcfx::Ev(svcfx::Norm(*reinterpret_cast<uint32_t *>(uintptr_t(args[2]))));
    if (self->value[0]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(3);
        ++F(3).refs;
    }
    return self->value[1];
}
uint32_t SetQI(svcfx::Obj *self, uint32_t *args)
{
    if (self->value[0]) {
        *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(4);
        ++F(4).refs;
    }
    return self->value[1];
}
uint32_t ResGetKey(svcfx::Obj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) out[i] = self->value[2 + i];
    return args[0];
}
uint32_t ResSetKey(svcfx::Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) svcfx::Ev(key[i]);
    return 1;
}

// A vtable for an Info under test: slot 1 AddRef and slot 12 the deleting destructor log their calls.
uint32_t __fastcall InfoAddRef(Info *self, void *)
{
    svcfx::Ev(0x71);
    svcfx::Ev(self->mRefCount);
    return 0x55;
}
void *__fastcall InfoDelete(Info *self, void *, uint32_t flags)
{
    svcfx::Ev(0x72);
    svcfx::Ev(self->mRefCount);
    svcfx::Ev(flags);
    return self;
}
void *gInfoVT[13] = {nullptr, reinterpret_cast<void *>(&InfoAddRef), nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&InfoDelete)};

} // namespace

// Construct, Create, QueryInterface, Release, Destruct, DeletingDestructor.
DIFF_TEST(SaveGameInfo_Lifetime, 0xcd7a10)
{
    Side sides[2] = {{ctx, true}, {ctx, false}};
    // Construct over the same random bytes.
    for (uint32_t seed = 0; seed < 20; ++seed) {
        alignas(4) uint8_t raw[2][0x14];
        std::mt19937 rng(seed);
        for (auto &b : raw[0]) b = uint8_t(rng());
        std::memcpy(raw[1], raw[0], sizeof(raw[0]));
        for (int s = 0; s < 2; ++s) sides[s].Construct(reinterpret_cast<Info *>(raw[s]));
        if (std::memcmp(raw[0], raw[1], sizeof(raw[0])) != 0) ctx.Fail("seed %u: Construct differs", seed);
        ++ctx.cases;
    }
    // Create: a constructed object of 0xc bytes.
    for (int s = 0; s < 2; ++s) {
        Info *made = sides[s].Create();
        if (!made || made->mVTable != GameAddress<const void>(Info::kVTable) || made->mRefCount || made->mProperties)
            ctx.Fail("side %d: Create differs", s);
        ++ctx.cases;
        operator delete(made);
    }
    // QueryInterface and Release through a recording vtable (slot 1 AddRef, slot 12 the deleting destructor).
    svcfx::ResetAll();
    for (uint32_t iid : {0u, 1u, 2u, Info::kIid, Info::kIid + 1, Info::kClassId}) {
        for (int nullOut = 0; nullOut < 2; ++nullOut) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                svcfx::ResetAll();
                Info info{};
                info.mVTable = gInfoVT;
                info.mRefCount = 4;
                void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                const uint32_t r = sides[s].QI(&info, iid, nullOut ? nullptr : &out);
                result[s] = {r & 0xff, out == &info ? 1u : svcfx::P(out)};
                result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
            }
            svcfx::Same(ctx, result[0], result[1], "QueryInterface", iid);
        }
    }
    for (uint32_t refs : {0u, 1u, 2u, 3u, 0x80000000u}) {
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            svcfx::ResetAll();
            Info info{};
            info.mVTable = gInfoVT;
            info.mRefCount = refs;
            const uint32_t r = sides[s].Release(&info);
            result[s] = {r, info.mRefCount};
            result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
        }
        svcfx::Same(ctx, result[0], result[1], "Release", refs);
    }
    // Destruct / DeletingDestructor: the set's Release, the vtable written, the object freed only on flags & 1.
    for (uint32_t flags : {0u, 1u, 2u, 3u}) {
        for (int withSet = 0; withSet < 2; ++withSet) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                svcfx::ResetAll();
                auto *info = static_cast<Info *>(operator new(sizeof(Info)));
                info->mVTable = nullptr;
                info->mRefCount = 7;
                info->mProperties = withSet ? reinterpret_cast<cGZPropertySetString *>(&F(2)) : nullptr;
                Info copy{};
                bool freed;
                void *r;
                {
                    difftest::FreeRecorder rec;
                    r = sides[s].DeletingDestructor(info, flags);
                    rec.Stop();
                    freed = rec.Freed(info);
                }
                if (!freed) copy = *info;
                result[s] = {r == info, freed, uint32_t(F(2).refs)};
                if (!freed) result[s].insert(result[s].end(), {svcfx::P(copy.mVTable), copy.mRefCount, svcfx::Norm(svcfx::P(copy.mProperties))});
                result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
                if (!freed) operator delete(info);
                // Destruct alone.
                svcfx::ResetAll();
                Info plain{nullptr, 3, withSet ? reinterpret_cast<cGZPropertySetString *>(&F(2)) : nullptr};
                sides[s].Destruct(&plain);
                result[s].insert(result[s].end(), {svcfx::P(plain.mVTable), plain.mRefCount, svcfx::Norm(svcfx::P(plain.mProperties)),
                                                   uint32_t(F(2).refs)});
                result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
            }
            svcfx::Same(ctx, result[0], result[1], "DeletingDestructor", flags * 2 + withSet);
        }
    }
}

// EnsureProperties, SetUint32, SetString, GetUint32, GetString, GetIndices on real property sets.
DIFF_TEST(SaveGameInfo_Properties, 0xcd8a50)
{
    svcfx::AllocatorScope allocator;
    Side sides[2] = {{ctx, true}, {ctx, false}};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Info infos[2];
        for (int s = 0; s < 2; ++s) infos[s].Construct();
        std::mt19937 rngs[2] = {std::mt19937(seed), std::mt19937(seed)};
        bool ok = true;
        for (int step = 0; step < 24 && ok; ++step) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                std::mt19937 &rng = rngs[s];
                Info *info = &infos[s];
                Side &side = sides[s];
                Blob &r = result[s];
                const uint32_t op = rng() % 7;
                const uint32_t index = PickIndex(rng);
                r.push_back(op);
                switch (op) {
                case 0: { // SetUint32
                    side.SetUint32(info, index, rng() % 3 ? uint32_t(rng() % 100) : uint32_t(rng()));
                    break;
                }
                case 1: { // SetString
                    cRZString text;
                    text.Construct(kTexts[rng() % 5]);
                    if (rng() % 4 == 0) text.mVTable = OddStringVTable(); // Data() differs from ToChar()
                    side.SetString(info, index, text.AsInterface());
                    text.mVTable = GameAddress<const void>(cRZString::kVTable);
                    r.push_back(text.mRefCount);
                    text.Destruct();
                    break;
                }
                case 2: { // GetUint32
                    uint32_t out = 0xabcdef01;
                    r.push_back(side.GetUint32(info, index, &out) & 0xff);
                    r.push_back(out);
                    break;
                }
                case 3: { // GetString
                    cRZString out;
                    out.Construct("previous");
                    r.push_back(side.GetString(info, index, out.AsInterface()) & 0xff);
                    PushText(r, out.mString.c_str());
                    r.push_back(out.mRefCount);
                    out.Destruct();
                    break;
                }
                case 4: { // GetIndices, into an empty or a pre-filled vector
                    Info::IndexList out;
                    for (uint32_t n = rng() % 3; n; --n) out.push_back(-int(n));
                    side.GetIndices(info, &out);
                    r.push_back(uint32_t(out.size()));
                    for (int v : out) r.push_back(uint32_t(v));
                    break;
                }
                case 5: // EnsureProperties
                    r.push_back(side.Ensure(info) & 0xff);
                    break;
                default: // drop the set (the next accessor sees none)
                    if (rng() % 3 == 0) ReleaseSet(info);
                    break;
                }
                DumpSet(r, info);
            }
            ok = svcfx::Same(ctx, result[0], result[1], "Properties", seed * 100 + step);
        }
        for (Info &info : infos) ReleaseSet(&info);
    }
}

// Save and Load against a fake resource manager.
DIFF_TEST(SaveGameInfo_SaveLoad, 0xcd8770)
{
    Side sides[2] = {{ctx, true}, {ctx, false}};
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            std::mt19937 rng(seed);
            svcfx::ResetAll();
            svcfx::Globals globals;
            const bool haveService = rng() % 6 != 0;
            globals.Set(kServiceCell, haveService ? &F(0) : nullptr);
            F(0).Set(5, "pdp", 0, &AppGetService);
            F(0).value[0] = rng() % 5 != 0;           // a resource manager handed out
            F(0).value[1] = rng() % 2;
            const uint32_t saveResults[] = {0, 1, 0x100, 0xffffff01};
            F(1).Set(16, "p-", saveResults[rng() % 4]);
            F(1).Set(3, 0, svcfx::P(&F(7))); // GetResourceManager (never null in the game)
            F(7).Set(3, "pdp--", 0, &RmGetResource);
            F(7).value[0] = rng() % 3 != 0;           // GetResource hands out a set
            F(7).value[1] = rng() % 2 ? 1u : (rng() % 2 ? 0u : 0x200u);
            F(2).Set(0, "dp", 0, &SetQI);
            F(2).value[0] = rng() % 4 != 0;
            F(2).value[1] = F(2).value[0] ? 1u : 0u; // a QI that succeeds hands out the interface
            F(4).Set(3, "p", 0, &ResGetKey);
            F(4).Set(4, "p", 0, &ResSetKey);
            for (int i = 0; i < 4; ++i) F(4).value[2 + i] = rng() % 3 ? uint32_t(rng()) : 0u;
            Info info{GameAddress<const void>(Info::kVTable), 1, nullptr};
            const bool withSet = rng() % 5 != 0;
            info.mProperties = withSet ? reinterpret_cast<cGZPropertySetString *>(&F(2)) : nullptr;
            const uint32_t group = rng() % 2 ? uint32_t(rng()) : 0u, instance = rng() % 3;
            const bool load = rng() % 2;
            if (!load && !withSet) continue; // Save would build a real set: covered by Fill below
            uint32_t r = load ? sides[s].Load(&info, group, instance) : sides[s].Save(&info, group, instance);
            result[s] = {load, r & 0xff, svcfx::Norm(svcfx::P(info.mProperties))};
            result[s].insert(result[s].end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::CaptureFakes(result[s], kValueKinds);
        }
        svcfx::Same(ctx, result[0], result[1], "SaveLoad", seed);
    }
}

// Fill (name, file version, language), with real property sets, a fake app service and Save's empty-set path.
DIFF_TEST(SaveGameInfo_Fill, 0xcd7fa0)
{
    svcfx::AllocatorScope allocator;
    Side sides[2] = {{ctx, true}, {ctx, false}};
    const char *const names[] = {nullptr, "", "The Sims(TM) 2", "a name that is longer than fifteen characters"};
    const char *const versions[] = {nullptr, "", "1.18.0.187"};
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Blob result[2];
        for (int s = 0; s < 2; ++s) {
            std::mt19937 rng(seed);
            svcfx::ResetAll();
            svcfx::Globals globals;
            Patch p1(GameAddress<void>(0x1c98d6), reinterpret_cast<const void *>(&FakeVersionConstruct));
            Patch p2(GameAddress<void>(0x1c9b6d), reinterpret_cast<const void *>(&FakeVersionGetFileVersion));
            Patch p3(GameAddress<void>(0x1c9901), reinterpret_cast<const void *>(&FakeVersionDestruct));
            Patch p4(Ours(&appbase::cRZVersion::Construct), reinterpret_cast<const void *>(&FakeVersionConstruct));
            Patch p5(Ours(&appbase::cRZVersion::GetFileVersion), reinterpret_cast<const void *>(&FakeVersionGetFileVersion));
            Patch p6(Ours(&appbase::cRZVersion::Destruct), reinterpret_cast<const void *>(&FakeVersionDestruct));
            const bool haveService = rng() % 8 != 0;
            globals.Set(kServiceCell, haveService ? &F(0) : nullptr);
            F(0).Set(5, "dpp", 0, &AppGetService); // the language getter's fallback: hands out nothing
            const bool haveName = rng() % 6 != 0;
            F(0).Set(37, 0, haveName ? svcfx::P(&F(5)) : 0u);
            F(5).Set(5, 0, svcfx::P(names[rng() % 4]));
            const bool haveLanguage = rng() % 4 != 0;
            globals.Set(kLanguageCell, haveLanguage ? &F(6) : nullptr);
            F(6).Set(7, 0, rng() % 2 ? uint32_t(rng() % 20) : uint32_t(rng()));
            gVersionText = versions[rng() % 3];
            gVersionResult = rng() % 2;
            Info info;
            info.Construct();
            if (rng() % 3 == 0) {
                // A set that already holds an index.
                cRZString text;
                text.Construct("old");
                info.SetString(1, text.AsInterface());
                text.Destruct();
                svcfx::gEv.clear();
            }
            sides[s].Fill(&info);
            result[s] = svcfx::gEv;
            DumpSet(result[s], &info);
            svcfx::CaptureFakes(result[s]);
            ReleaseSet(&info);
        }
        svcfx::Same(ctx, result[0], result[1], "Fill", seed);
    }
}
