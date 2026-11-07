// Differential tests for nTSCASCore::cTSCASBodyPartSystem, lane JZ: the material builders, slots 26 (CreateSkinMaterial,
// 0xcea630) and 27 (CreateHairMaterial, 0xcea2f0). Fakes: the queried object (GetCachedQueriedObject's cache 0x10f8fcc),
// its property set, the material, the material's resource and its owner, the persist service (cache 0x10faf6c) and
// the registry it hands out; 0xae8750 and the key builder 0xae7340 are redirected. cRZString::Sprintf (0x1ad6f7 and
// ours), atoi (0x830972) and the image's operator new / sized delete stand in on both sides (the harness never
// initialises the image's CRT).
#include "sg/cTextureManager.h"
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace bpt_materials {

using namespace bpt;

uint32_t TextHash(const char *s)
{
    if (s == nullptr) return 0xdead;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

uint32_t gScriptSeed;
uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

struct Obj {
    const void *const *vtable;
};
using Vt = std::array<const void *, 40>;
Vt gQueriedVt, gPropsVt, gMaterialVt, gResourceVt, gOwnerVt, gPersistVt, gRegistryVt;
Obj gQueried, gProps, gMaterial, gResource, gOwner, gPersist, gRegistry;

// ---- the queried object ----
bool gPropsNull;
int gNamesTaken; // how many generated names the queried object already knows
uint32_t __fastcall QueriedKey(Obj *, void *, uint32_t id)
{
    gEvents.push_back({0x6013, id});
    return id * 5 + 2;
}
void __fastcall QueriedLookUp(Obj *, void *, void **out, uint32_t key)
{
    // Models the real callee (cMaterialManager slot 33, 0xacc410): it writes all five words of the entry, the last three
    // zero in the game's lot-load call. The game crashed when ours passed an 8-byte out (the zeros landed on `this`).
    gEvents.push_back({0x6021, key});
    out[0] = reinterpret_cast<void *>(0x5678);
    out[1] = gPropsNull ? nullptr : &gProps;
    out[2] = nullptr;
    out[3] = nullptr;
    out[4] = nullptr;
}
uint8_t __fastcall QueriedNameTaken(Obj *, void *, const char *name)
{
    gEvents.push_back({0x601f, TextHash(name)});
    return gNamesTaken-- > 0 ? 1 : 0;
}
void __fastcall QueriedAdd(Obj *, void *, void *material, uint32_t flags)
{
    gEvents.push_back({0x6017, static_cast<uint32_t>(material == &gMaterial), flags});
}

// ---- the property set ----
uint32_t gPropCount;
const char *gAge, *gGender, *gOverlayCount;
uint32_t __fastcall PropsCount(Obj *, void *)
{
    gEvents.push_back({0x7014});
    return gPropCount;
}
const char *__fastcall PropsGet(Obj *, void *, const char *name)
{
    gEvents.push_back({0x7018, TextHash(name)});
    if (std::strcmp(name, "paramAge") == 0) return gAge;
    if (std::strcmp(name, "paramGender") == 0) return gGender;
    if (std::strcmp(name, "cafNumOverlays") == 0) return gOverlayCount;
    static const char *const kValues[] = {"overlay_value", nullptr, "", "a_long_overlay_value_past_sixteen"};
    return kValues[Pick() % 4];
}
void *__fastcall PropsMaterial(Obj *, void *)
{
    gEvents.push_back({0x7007});
    return &gMaterial;
}

// ---- the material, its resource and the resource's owner ----
uint32_t __fastcall MaterialAddRef(Obj *, void *)
{
    gEvents.push_back({0x8001});
    return 2;
}
uint32_t __fastcall MaterialRelease(Obj *, void *)
{
    gEvents.push_back({0x8002});
    return 1;
}
void __fastcall MaterialName(Obj *, void *, const char *name) { gEvents.push_back({0x800a, TextHash(name)}); }
void __fastcall MaterialSet(Obj *, void *, const char *name, const char *value)
{
    gEvents.push_back({0x800d, TextHash(name), TextHash(value)});
}
void *__fastcall MaterialResource(Obj *, void *)
{
    gEvents.push_back({0x8009});
    return &gResource;
}
uint32_t gResult;
uint32_t __fastcall MaterialResult(Obj *, void *)
{
    gEvents.push_back({0x8013});
    return gResult;
}
void __fastcall ResourceKey(Obj *, void *, const char *key, uint32_t arg) { gEvents.push_back({0x900c, TextHash(key), arg}); }
void __fastcall ResourceFlag(Obj *, void *, uint32_t flag) { gEvents.push_back({0x9010, flag}); }
void __fastcall ResourceCommit(Obj *, void *) { gEvents.push_back({0x900a}); }
void *__fastcall ResourceOwner(Obj *, void *)
{
    gEvents.push_back({0x9008});
    return &gOwner;
}
void *__fastcall OwnerKey(Obj *, void *, uint32_t *out)
{
    gEvents.push_back({0xa003});
    out[0] = 0x11111111;
    out[1] = 0x22222222;
    out[2] = 0x33333333;
    return out;
}
void *__fastcall PersistRegistry(Obj *, void *)
{
    gEvents.push_back({0xb003});
    return &gRegistry;
}
void __fastcall RegistryAdd(Obj *, void *, const uint32_t *key) { gEvents.push_back({0xc00a, key[0], key[1], key[2]}); }

// ---- redirected functions ----
void *__cdecl F_ae8750()
{
    gEvents.push_back({0xae8750});
    return &gPersist;
}
uint32_t __cdecl F_ae7340(cRZString *out, const char *name, uint32_t arg, uint32_t zero)
{
    gEvents.push_back({0xae7340, TextHash(name), arg, zero});
    out->mString = std::string("key:") + name;
    return 1;
}
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    gEvents.push_back({0x1ad6f7, TextHash(fmt), TextHash(text)});
    self->mString = text;
    return self;
}
int __cdecl StandAtoi(const char *text) { return std::atoi(text); }
void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

template <int kSlot> void __fastcall Unexpected(void *self, void *)
{
    std::fprintf(stderr, "unexpected call of slot %d on a material fake %p\n", kSlot, self);
    std::abort();
}
template <size_t... k> void FillUnexpected(Vt &vt, std::index_sequence<k...>)
{
    ((vt[k] = reinterpret_cast<const void *>(&Unexpected<static_cast<int>(k)>)), ...);
}
void Fill(Vt &vt, Obj &obj)
{
    FillUnexpected(vt, std::make_index_sequence<40>());
    obj.vtable = vt.data();
}

struct Fixture {
    VTable vt;
    Env env;
    void **queriedCache, **persistCache;
    void *savedQueried, *savedPersist;
    Redirect atoiStand, newStand, deleteStand;
    shape::StandIn r1, r2, sprintfStand; // r1, r2: the image's callee and ours (nGZSceneGraph) both stood in
    explicit Fixture(difftest::Context &ctx)
        : vt(CopyVTable(ctx)), env(ctx), queriedCache(ctx.image.At<void *>(0x10f8fcc)), persistCache(ctx.image.At<void *>(0x10faf6c)),
          savedQueried(*queriedCache), savedPersist(*persistCache), r1(0xae8750, &nGZSceneGraph::TextureManager, &F_ae8750),
          r2(0xae7340, &nGZSceneGraph::ResolveResourceName, &F_ae7340), atoiStand(ctx, 0x830972, reinterpret_cast<const void *>(&StandAtoi)),
          newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew)),
          deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete)),
          sprintfStand(0x1ad6f7, &cRZString::Sprintf, &StandSprintf)
    {
        Fill(gQueriedVt, gQueried);
        gQueriedVt[19] = reinterpret_cast<const void *>(&QueriedKey);
        gQueriedVt[33] = reinterpret_cast<const void *>(&QueriedLookUp);
        gQueriedVt[31] = reinterpret_cast<const void *>(&QueriedNameTaken);
        gQueriedVt[23] = reinterpret_cast<const void *>(&QueriedAdd);
        Fill(gPropsVt, gProps);
        gPropsVt[20] = reinterpret_cast<const void *>(&PropsCount);
        gPropsVt[24] = reinterpret_cast<const void *>(&PropsGet);
        gPropsVt[7] = reinterpret_cast<const void *>(&PropsMaterial);
        Fill(gMaterialVt, gMaterial);
        gMaterialVt[1] = reinterpret_cast<const void *>(&MaterialAddRef);
        gMaterialVt[2] = reinterpret_cast<const void *>(&MaterialRelease);
        gMaterialVt[10] = reinterpret_cast<const void *>(&MaterialName);
        gMaterialVt[13] = reinterpret_cast<const void *>(&MaterialSet);
        gMaterialVt[9] = reinterpret_cast<const void *>(&MaterialResource);
        gMaterialVt[19] = reinterpret_cast<const void *>(&MaterialResult);
        Fill(gResourceVt, gResource);
        gResourceVt[12] = reinterpret_cast<const void *>(&ResourceKey);
        gResourceVt[16] = reinterpret_cast<const void *>(&ResourceFlag);
        gResourceVt[10] = reinterpret_cast<const void *>(&ResourceCommit);
        gResourceVt[8] = reinterpret_cast<const void *>(&ResourceOwner);
        Fill(gOwnerVt, gOwner);
        gOwnerVt[3] = reinterpret_cast<const void *>(&OwnerKey);
        Fill(gPersistVt, gPersist);
        gPersistVt[3] = reinterpret_cast<const void *>(&PersistRegistry);
        Fill(gRegistryVt, gRegistry);
        gRegistryVt[10] = reinterpret_cast<const void *>(&RegistryAdd);
        *queriedCache = &gQueried;
        *persistCache = &gPersist;
        gEvents.reserve(512);
    }
    ~Fixture()
    {
        *queriedCache = savedQueried;
        *persistCache = savedPersist;
    }
};

const char *const kGivenNames[] = {"skin_material", "", "a_material_name_well_past_sixteen"};
const char *const kNumbers[] = {"0", "1", "2", "3", "-1", "x", "", "12"};

// The common script: the serial (system bytes), the names taken, the property count, the result.
void Script(std::mt19937 &rng, Scenario &sc)
{
    for (auto &b : sc.bytes) b = static_cast<uint8_t>(rng());
    const uint32_t serial = rng() % 3 == 0 ? 0xffffffffu : static_cast<uint32_t>(rng() % 1000);
    std::memcpy(sc.bytes + kIfaceOffset + 0x12c, &serial, 4);
    gScriptSeed = static_cast<uint32_t>(rng());
    gNamesTaken = static_cast<int>(rng() % 4);
    gPropCount = rng() % 6 == 0 ? 0 : static_cast<uint32_t>(1 + rng() % 5);
    gResult = static_cast<uint32_t>(rng());
}

} // namespace bpt_materials

DIFF_TEST(cTSCASBodyPartSystem_CreateSkinMaterial, 0xcea630)
{
    using namespace bpt_materials;
    std::mt19937 rng(0xcea630);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, const char *, uint32_t, uint8_t)>(0xcea630);
    Fixture fx(ctx);
    for (int i = 0; i < 3000; ++i) {
        Scenario sc;
        Script(rng, sc);
        gPropsNull = false; // slot 26 does not check it
        gAge = rng() % 8 == 0 ? nullptr : kNumbers[rng() % 8];
        gGender = rng() % 8 == 0 ? nullptr : kNumbers[rng() % 8];
        gOverlayCount = rng() % 5 == 0 ? nullptr : kNumbers[rng() % 8];
        const uint32_t id = static_cast<uint32_t>(rng() % 16);
        const uint32_t resourceArg = static_cast<uint32_t>(rng());
        const uint32_t keyArg = static_cast<uint32_t>(rng());
        const char *const name = rng() % 2 ? nullptr : kGivenNames[rng() % 3];
        const uint8_t eye = rng() % 2 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        Compare(ctx, "CreateSkinMaterial", sc, fx.vt, [&](World &w, bool original) -> uint64_t {
            const int taken = gNamesTaken;
            const uint32_t r = original ? orig(w.S(), id, resourceArg, name, keyArg, eye)
                                        : w.S()->CreateSkinMaterial(id, resourceArg, name, keyArg, eye);
            gNamesTaken = taken;
            return r;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_CreateHairMaterial, 0xcea2f0)
{
    using namespace bpt_materials;
    std::mt19937 rng(0xcea2f0);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t)>(0xcea2f0);
    Fixture fx(ctx);
    for (int i = 0; i < 3000; ++i) {
        Scenario sc;
        Script(rng, sc);
        gPropsNull = rng() % 8 == 0;
        const uint32_t id = static_cast<uint32_t>(rng() % 16);
        const uint32_t resourceArg = static_cast<uint32_t>(rng());
        const uint32_t keyArg = static_cast<uint32_t>(rng());
        const char *const name = rng() % 2 ? nullptr : kGivenNames[rng() % 3];
        const uint32_t a = static_cast<uint32_t>(rng() % 7), b = static_cast<uint32_t>(rng() % 3 == 0 ? rng() : rng() % 4);
        const uint32_t species = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
        const uint8_t hair = rng() % 2 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        Compare(ctx, "CreateHairMaterial", sc, fx.vt, [&](World &w, bool original) -> uint64_t {
            const int taken = gNamesTaken;
            const uint32_t r = original ? orig(w.S(), id, resourceArg, name, keyArg, a, b, species, hair)
                                        : w.S()->CreateHairMaterial(id, resourceArg, name, keyArg, a, b, species, hair);
            gNamesTaken = taken;
            return r;
        });
    }
}
