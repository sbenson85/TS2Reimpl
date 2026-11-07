// Differential test for nTSSG::cTSSGSystem::CapturePostEffects (src/sg/cTSSGSystemCapture.cpp).
// Hook shape: the original runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object starts as random bytes with mCaptureSuspended, mRef2C8 and mRef2CC
// drawn. The texture manager is a fake in 0xae8750's cache 0x10f8f94 (slots 17-19 / 23 / 25 logged), so the texture
// handle's functions (0xae8280, 0xae8730, 0xae82b0) run for real on both sides (the image's and ours); the image service
// (0x18d120) is stood in on both. The handle's object, the render target, its description object and level surface, the existing and the newly
// made surfaces (the framework's slot 3) and the image service are fakes; the surfaces' copy slot (15) logs the point
// and rectangle it is given and fails on a drawn call.
//
// Compared: the result, the event log, the object's 0x3b8 bytes (normalised), every fake's reference count, the rect.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/GraphicServices.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kRectTag = 0x6c000000;
enum : uint32_t { evStand = 0x83, evCopy, evImage };

enum : unsigned {
    fFramework = 0,
    fTexService,
    fSource,
    fTexture,
    fDesc,
    fLevel,
    fFull,
    fTile,
    fNewFull,
    fNewTile,
    fImage,
};

struct Draw {
    uint32_t has, suspended, sourceGiven, textureGiven, textureValid;
    uint32_t width, height, format, kind;
    bool fullThere, tileThere;
    uint32_t fullValid, tileValid, newFullValid, newTileValid;
    bool fullSame[4], tileSame[2];
    int32_t rect[4];
    unsigned failAt;
    uint32_t directOk, imageOk;
};
Draw gD;
unsigned gCopies;
alignas(16) int32_t gRect[4];
alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd ddppdddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

// ---- stand-ins ----
void *__cdecl StandImageService()
{
    Ev(evStand), Ev(0x18d120);
    return &F(fImage);
}
// ---- customs ----
uint32_t TextureOf(Obj *, uint32_t *args) // the texture manager's slot 23 (id): the handle's object
{
    return args[0] != 0xffffffffu && gD.sourceGiven ? P(&F(fSource)) : 0u;
}
uint32_t SourceQuery(Obj *, uint32_t *args) // slot 0 (iid, &out)
{
    if (gD.textureGiven) *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = P(&F(fTexture));
    return gD.textureGiven;
}
uint32_t FrameworkMake(Obj *, uint32_t *args) // slot 3 (class, iid, &out)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = P(&F(args[0] == 0x3b75b71b ? fNewFull : fNewTile));
    return 1;
}
uint32_t Copy(Obj *, uint32_t *args) // slot 15 (const point *, surface, const rect *)
{
    const auto *point = reinterpret_cast<const int32_t *>(uintptr_t(args[0]));
    const auto *rect = reinterpret_cast<const int32_t *>(uintptr_t(args[2]));
    Ev(evCopy), Ev(uint32_t(point[0])), Ev(uint32_t(point[1]));
    for (int i = 0; i < 4; ++i) Ev(uint32_t(rect[i]));
    return ++gCopies == gD.failAt ? 0x100u : 1u;
}
uint32_t ImageSave(Obj *, uint32_t *args) // slot 3 (surface, rect, a, b, &out)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[4]));
    Ev(evImage), Ev(*out);
    *out = P(&F(fDesc));
    return gD.imageOk;
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::StandIn imageService{0x18d120, &nGZGraphic4::GetImageManipCached, &StandImageService};
    Fixture()
    {
        gFrameworkIndex = fFramework;
        globals.Set(0x10f8f94, &F(fTexService)); // the texture manager's cache (0xae8750)
    }
};

void Surface(unsigned index, uint32_t valid, const bool *same, bool tile)
{
    Obj &s = F(index);
    s.Set(6, 0, valid);
    s.Set(20, 0, same[0] ? gD.width : gD.width + 1);
    s.Set(21, 0, same[1] ? gD.height : gD.height ^ 0x40);
    s.Set(22, 0, same[tile ? 0 : 2] ? gD.format : gD.format + 7);
    s.Set(5, 0, same[tile ? 1 : 3] ? gD.kind : ~gD.kind);
    s.Set(3, 0);
    s.Set(29, index == fFull || index == fNewFull ? 5 : 4), s.Data(29, 0x1f);
    s.Set(15, "ppp", 0u, &Copy);
    s.Set(16, "p", gD.directOk);
}

void Wire()
{
    F(fFramework).Set(3, "ddp", 0u, &FrameworkMake);
    F(fTexService).Set(25, "p", gD.has);
    F(fTexService).Set(17, "d", 0x4d2u); // FindOrLoad (the name's address is logged as data: both sides pass the image's)
    F(fTexService).Set(18, "d");         // AddRefId
    F(fTexService).Set(19, "d");         // ReleaseId
    F(fTexService).Set(23, "d", 0u, &TextureOf);
    F(fSource).Set(0, "dp", 0u, &SourceQuery);
    F(fTexture).Set(6, 0, gD.textureValid);
    F(fTexture).Set(8, 1, &F(fDesc));
    F(fTexture).Set(17, 1, &F(fLevel));
    F(fDesc).Set(17, 1, gD.width);
    F(fDesc).Set(18, 1, gD.height);
    F(fDesc).Set(19, 0, gD.format);
    F(fDesc).Set(5, 0, gD.kind);
    const bool all[4] = {true, true, true, true};
    Surface(fFull, gD.fullValid, gD.fullSame, false);
    Surface(fTile, gD.tileValid, gD.tileSame, true);
    Surface(fNewFull, gD.newFullValid, all, false);
    Surface(fNewTile, gD.newTileValid, all, true);
    F(fImage).Set(3, "ppddp", 0u, &ImageSave);
}

int32_t Extent(std::mt19937 &gen)
{
    static const int32_t kExtents[] = {0, 1, 100, 127, 128, 129, 200, 256, 300, 383, 384, 600, 1000};
    return kExtents[gen() % (sizeof(kExtents) / sizeof(kExtents[0]))];
}

uint32_t Flag(std::mt19937 &gen, unsigned falseOneIn)
{
    const uint32_t r = gen() % falseOneIn;
    return r == 0 ? (gen() % 2 ? 0u : 0x100u) : 1u; // 0x100: the byte is clear
}

} // namespace

// 0x248810 CapturePostEffects: the texture service without the target, capture suspended, no handle object or no
// texture or an invalid one; existing surfaces missing, invalid, matching or not in each compared value; new surfaces
// valid or not; rectangles under and over the tiling limit (0 to 1000 a side), a tile copy failing at a drawn call, the
// direct copy and the image service succeeding or not.
DIFF_TEST(sg_tssg_system_capture_post_effects, 0x248810)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const int32_t *, uint32_t, uint32_t)>(0x248810);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x248810u);
        gD = Draw{};
        gD.has = Flag(gen, 8), gD.suspended = gen() % 8 == 0 ? 1 + gen() % 255 : 0;
        gD.sourceGiven = gen() % 8 != 0, gD.textureGiven = gen() % 8 != 0, gD.textureValid = Flag(gen, 8);
        gD.width = 1 + gen() % 2048, gD.height = 1 + gen() % 2048, gD.format = gen() % 64, gD.kind = gen();
        gD.fullThere = gen() % 4 != 0, gD.tileThere = gen() % 4 != 0;
        gD.fullValid = Flag(gen, 4), gD.tileValid = Flag(gen, 4);
        gD.newFullValid = Flag(gen, 10), gD.newTileValid = Flag(gen, 10);
        for (bool &same : gD.fullSame) same = gen() % 6 != 0;
        for (bool &same : gD.tileSame) same = gen() % 6 != 0;
        gD.rect[0] = int32_t(gen() % 64) - 16, gD.rect[1] = int32_t(gen() % 64) - 16;
        gD.rect[2] = gD.rect[0] + Extent(gen), gD.rect[3] = gD.rect[1] + Extent(gen);
        gD.failAt = gen() % 3 == 0 ? 1 + gen() % 24 : 0;
        gD.directOk = Flag(gen, 5), gD.imageOk = Flag(gen, 3);
        const uint32_t a = gen(), b = gen();

        uint8_t start[kObjectSize];
        for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
        start[0x16c] = uint8_t(gD.suspended);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            Wire();
            gCopies = 0;
            std::memcpy(gObject, start, kObjectSize);
            void *full = gD.fullThere ? &F(fFull) : nullptr, *tile = gD.tileThere ? &F(fTile) : nullptr;
            std::memcpy(gObject + 0x2c8, &full, 4);
            std::memcpy(gObject + 0x2cc, &tile, 4);
            std::memcpy(gRect, gD.rect, sizeof(gRect));
            AddRange(gObject, kObjectSize, kObjectTag);
            AddRange(gRect, sizeof(gRect), kRectTag);
            ctx.Mark(side ? "ours" : "original");
            uint8_t result;
            if (side == 0) result = original(gObject, gRect, a, b);
            else result = reinterpret_cast<cTSSGSystem *>(gObject)->CapturePostEffects(gRect, a, b);
            Blob &o = outcome[side];
            o = gEv;
            o.push_back(0xeeee);
            o.push_back(result);
            svcfx::DumpWords(o, gObject, sizeof(gObject), kObjectKinds);
            CaptureFakes(o);
            for (int32_t v : gRect) o.push_back(uint32_t(v));
        }
        Same(ctx, outcome[0], outcome[1], "capture_post_effects", seed);
    }
}
