// Differential tests of nGZSceneGraph::cTextureImage (lane SJ, milestone O row M4a part 4; src/sg/cTextureImage.h).
//
//   SG_TextureImageSlots  the object's life on each side: made by the director's factory (the original's 0xab4e50 /
//       0xab4d30, with the constructor 0xab4400; ours NewTexture / NewCube), a 2D texture or a cube map, with a device
//       (its device texture made through the COM getter) or without one for the whole seed. Then random calls through
//       the interface: QueryInterface 0xab5aa0, AddRef / Release (shared_vf_244060), Restore 0xab5610 (creating the
//       device texture through the description chooser and 0xab57d0, uploading through 0xab4780, letting a keyed image
//       go), ReleaseResource 0xab5e00, GetKind, IsCreated (shared_vf_ab46a0), GetDeviceTextureCount, GetDeviceTexture
//       (shared_vf_ab4b50), Load 0xab5d20, SetImage 0xab6430, GetImage 0xab5e30 (loaded from the key's group, then the
//       game's, the service finding it or not and its result's low byte), the device's readiness and the images'
//       resource answers changing between calls. The whole state is noted after every call; at the end the last
//       Release (the deleting destructor 0xab4680 with the destructor 0xab45d0), the fakes' counts and the heap.
//       Upload's memory blocks are noted level by level as the image manipulator receives them.
//   SG_TextureImage_CreateDeviceTexture  0xab57d0 called directly: kinds 4, 6 and others, random descriptions and
//       qualities.
//
// Stood in on both sides: the device getter 0x18ae40, the COM getter 0x1ae18d (handing out the device texture fake)
// and the description chooser 0xab5f50 (ours, tested in sg_texture_description.cpp; here it notes its arguments,
// writes a description and answers true four times in five). The framework (cell 0x10c9378) answers the resource service fake;
// the image manipulator is a fake in its cache cell 0x10ca990.
#include "sgfx.h"

#include "code_patch.h"

#include "gz/GraphicServices.h"
#include "gz/cMemBlock.h"
#include "rz/StaticGetters.h"
#include "sg/TextureDescription.h"
#include "sg/cTextureImage.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kDeviceAt[] = {{35, {0, Ret::Int, true, "IsDeviceReady", ""}}};
constexpr auto kDeviceSlots = Table<36>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 36, kDeviceSlots.data()};

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

// The device texture's cITextureBase face: 3 ReleaseResource, 6 IsCreated.
constexpr At kBaseAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}}, {6, {0, Ret::Int, true, "IsCreated", ""}}};
constexpr auto kBaseSlots = Table<7>(kBaseAt);
constexpr drawfx::Iface kBase{"TextureBase", 7, kBaseSlots.data()};

// Its 2D and cube faces: slot 22 Create.
constexpr At kFace2DAt[] = {{22, {7, Ret::Int, true, "Create", "auuuufu"}}};
constexpr auto kFace2DSlots = Table<23>(kFace2DAt);
constexpr drawfx::Iface kFace2D{"Texture2D", 23, kFace2DSlots.data()};
constexpr At kFaceCubeAt[] = {{22, {6, Ret::Int, true, "Create", "auuufu"}}};
constexpr auto kFaceCubeSlots = Table<23>(kFaceCubeAt);
constexpr drawfx::Iface kFaceCube{"CubeTexture", 23, kFaceCubeSlots.data()};

// The image (cImageData's face).
constexpr At kImageAt[] = {{5, {0, Ret::Int, true, "Resource", ""}},     {7, {1, Ret::Int, true, "SelectEntry", "u"}},
                           {9, {1, Ret::Int, true, "Bits", "u"}},        {25, {0, Ret::Int, true, "EntryCount", ""}},
                           {26, {1, Ret::Int, true, "Width", "u"}},      {27, {1, Ret::Int, true, "Height", "u"}},
                           {28, {1, Ret::Int, true, "Pitch", "u"}},      {31, {0, Ret::Int, true, "LevelCount", ""}},
                           {32, {0, Ret::Int, true, "Kind", ""}},        {33, {0, Ret::Int, true, "Format", ""}},
                           {34, {0, Ret::Float, true, "Quality", ""}},   {36, {0, Ret::Float, true, "Amplitude", ""}}};
constexpr auto kImageSlots = Table<37>(kImageAt);
constexpr drawfx::Iface kImage{"Image", 37, kImageSlots.data()};

// The image's cISGResource face: 8 GetPersistResource, 21 HasKey, 23 GetFlag.
constexpr At kSGResAt[] = {{8, {0, Ret::Int, true, "GetPersistResource", ""}},
                           {21, {0, Ret::Int, true, "HasKey", ""}},
                           {23, {0, Ret::Int, true, "GetFlag", ""}}};
constexpr auto kSGResSlots = Table<24>(kSGResAt);
constexpr drawfx::Iface kSGRes{"SGResource", 24, kSGResSlots.data()};

constexpr At kPersistAt[] = {{3, {1, Ret::Int, true, "GetKey", "a"}}};
constexpr auto kPersistSlots = Table<4>(kPersistAt);
constexpr drawfx::Iface kPersist{"PersistResource", 4, kPersistSlots.data()};

constexpr At kServiceAt[] = {{3, {5, Ret::Int, true, "GetResource", "W4uauu"}}, {10, {1, Ret::Int, true, "ForgetResource", "W4"}}};
constexpr auto kServiceSlots = Table<11>(kServiceAt);
constexpr drawfx::Iface kService{"ResourceService", 11, kServiceSlots.data()};

constexpr At kManipAt[] = {{7, {7, Ret::Int, true, "CopyCubeFaces", "aaaaaaa"}},
                           {8, {2, Ret::Int, true, "CopyTexture", "aa"}},
                           {13, {4, Ret::Int, true, "ComputeNormalMap", "aafb"}}};
constexpr auto kManipSlots = Table<14>(kManipAt);
constexpr drawfx::Iface kManip{"ImageManip", 14, kManipSlots.data()};

// ---- the world ----
constexpr unsigned kImages = 4, kFormats = 2;
constexpr uint32_t kFrameworkCell = 0x10c9378, kManipCell = 0x10ca990;
constexpr uint32_t kResourceService = 0x056b906e, kResourceIid = 0x656b8efc;
constexpr uint32_t kEngineGroup = 0x1c0532fa;
constexpr uint32_t kIidTexture2D = 0x5b891e1e, kIidCube = 0xe9443438;

struct World {
    Fake framework, device, com, base, face2d, faceCube, service, manip;
    Fake images[kImages], sgres[kImages], persist[kImages], formats[kFormats];
    uint32_t lcg = 1;
    bool deviceUp = false, cube = false;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t GetService(Fake &, const uint32_t *a)
{
    if (a[0] != kResourceService || a[1] != kResourceIid) return 0;
    ++gw.service.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.service;
    return 1;
}
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    ++gw.base.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.base;
    return 1;
}
uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t BaseRelease(Fake &self, const uint32_t *)
{
    self.value[0] = 0;
    return 0;
}
// Create on a face: the device texture is created three times in four (and the result says so).
uint32_t FaceCreate(Fake &, const uint32_t *)
{
    gw.base.value[0] = gw.Next() % 4 != 0;
    return gw.base.value[0];
}
// The image: value 0 kind, 1 width, 2 height, 3 pitch, 4 entries, 5 levels, 6 the entry selected.
uint32_t ImageSelect(Fake &self, const uint32_t *a)
{
    self.value[6] = a[0];
    return 0;
}
uint32_t ImageBits(Fake &self, const uint32_t *a) { return 0x40000000u | (self.id << 16) | ((self.value[6] & 0xff) << 8) | (a[0] & 0xff); }
uint32_t ImageEntries(Fake &self, const uint32_t *) { return self.value[4]; }
uint32_t ImageWidth(Fake &self, const uint32_t *a) { return self.value[1] >> (a[0] & 31); }
uint32_t ImageHeight(Fake &self, const uint32_t *a) { return self.value[2] >> (a[0] & 31); }
uint32_t ImagePitch(Fake &self, const uint32_t *a) { return (self.value[3] >> (a[0] & 31)) + self.value[6]; }
uint32_t ImageLevels(Fake &self, const uint32_t *) { return self.value[5]; }
// GetKey(out): the persist fake's key words 0..3 written to out, out returned.
uint32_t PersistKey(Fake &self, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 4; ++i) out[i] = self.value[i];
    return a[0];
}
// GetResource(key, iid, out): one time in three nothing found (0); else an image written (counted) and 1, or 0x100 (a
// result whose low byte says not found), or 1 with nothing written.
uint32_t ServiceGet(Fake &, const uint32_t *a)
{
    const uint32_t pick = gw.Next() % 6;
    if (pick < 2) return pick == 0 ? 0 : 0x200;
    if (pick == 5) return 1;
    Fake *const image = &gw.images[gw.Next() % kImages];
    ++image->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = image;
    return pick == 4 ? 0x100 : 1;
}
uint32_t One(Fake &, const uint32_t *) { return 1; }

// A memory block (cMemBlockAsTexture) as the manipulator receives it: its levels' bits, size, pitch and format.
void NoteBlock(uint32_t word)
{
    difftest::NotCounted bookkeeping;
    const auto *const block = reinterpret_cast<const nRZGraphic4::cMemBlockAsTexture *>(uintptr_t(word));
    if (!block || !block->mImpl) {
        drawfx::Note("block", 0xffffffffu);
        return;
    }
    const uint32_t levels = block->mImpl->mLevels.Size();
    drawfx::Note("block levels", levels);
    for (uint32_t i = 0; i < levels; ++i) {
        const auto *const level = block->mImpl->Level(i);
        if (!level || !level->mImpl) {
            drawfx::Note("level", 0xffffffffu);
            continue;
        }
        const auto *const s = level->mImpl;
        drawfx::Note("level", {uint32_t(uintptr_t(s->mBits)), s->mWidth, s->mHeight, s->mPitch, drawfx::Ptr(s->mFormat)});
    }
}
uint32_t ManipCube(Fake &, const uint32_t *a)
{
    for (unsigned i = 0; i < 6; ++i) NoteBlock(a[i]);
    return 1;
}
uint32_t ManipCopy(Fake &, const uint32_t *a)
{
    NoteBlock(a[0]);
    return 1;
}

void *__cdecl StandDevice() { return gw.deviceUp ? &gw.device : nullptr; }
void *StandCom() { return &gw.com; }
// 0xab5f50's stand: notes what it is asked, writes a description from it, true four times in five.
bool __cdecl StandChoose(TextureDescription *desc, int32_t width, int32_t height, uint32_t levels,
                         nGZGraphic4::cIPixelFormat *format, bool normalMap, uint32_t kind)
{
    drawfx::Note("choose", {uint32_t(width), uint32_t(height), levels, drawfx::Ptr(format), uint32_t(normalMap), kind});
    desc->width = width + 1;
    desc->height = height + 2;
    desc->levels = levels + 3;
    desc->format = reinterpret_cast<nGZGraphic4::cIPixelFormat *>(uintptr_t(0x3000 + (gw.Next() & 0xff)));
    return gw.Next() % 5 != 0;
}

struct Stands {
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn choose{0xab5f50, &ChooseTextureDescription, &StandChoose};
};

void RandomResource(unsigned i, uint32_t r)
{
    static constexpr uint32_t kBytes[] = {0, 1, 0x100, 1};
    gw.sgres[i].Returns(21, kBytes[r % 4]);
    gw.sgres[i].Returns(23, kBytes[(r >> 2) % 4] & (r >> 4 & 1 ? 0x1ff : 0x100));
    Fake &p = gw.persist[i];
    p.value[0] = (r >> 5) % 4 == 0 ? 0 : 0x1c0e0000u + i;
    p.value[1] = (r >> 7) % 3 == 0 ? kEngineGroup : 0x7f000000u + i;
    p.value[2] = 0x500 + i;
    p.value[3] = 0x600 + (r & 0xff);
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 7;
    gw.framework.Reset<sgfx::kFramework>(460);
    gw.framework.On(5, &GetService);
    gw.device.Reset<kDevice>(520);
    gw.device.On(35, &Word0);
    gw.device.value[0] = 1;
    gw.com.Reset<kCom>(521);
    gw.com.On(3, &GetClassObject);
    gw.base.Reset<kBase>(522);
    gw.base.On(3, &BaseRelease);
    gw.base.On(6, &Word0);
    gw.face2d.Reset<kFace2D>(523);
    gw.face2d.On(22, &FaceCreate);
    gw.faceCube.Reset<kFaceCube>(524);
    gw.faceCube.On(22, &FaceCreate);
    gw.base.AddFace(kIidTexture2D, gw.face2d);
    gw.base.AddFace(kIidCube, gw.faceCube);
    gw.service.Reset<kService>(525);
    gw.service.On(3, &ServiceGet);
    gw.service.On(10, &One);
    gw.manip.Reset<kManip>(526);
    gw.manip.On(7, &ManipCube);
    gw.manip.On(8, &ManipCopy);
    gw.manip.On(13, &ManipCopy);
    for (unsigned i = 0; i < kFormats; ++i) gw.formats[i].Reset<sgfx::kUnknown>(530 + i);
    static constexpr uint32_t kKinds[] = {0, 6, 7, 1, 2};
    for (unsigned i = 0; i < kImages; ++i) {
        Fake &image = gw.images[i];
        image.Reset<kImage>(700 + i);
        image.Returns(5, &gw.sgres[i]);
        image.On(7, &ImageSelect);
        image.On(9, &ImageBits);
        image.On(25, &ImageEntries);
        image.On(26, &ImageWidth);
        image.On(27, &ImageHeight);
        image.On(28, &ImagePitch);
        image.On(31, &ImageLevels);
        image.On(32, &Word0);
        image.Returns(33, &gw.formats[(seed + i) % kFormats]);
        image.ReturnsFloat(34, 0.25f * float((seed + i) % 5));
        image.ReturnsFloat(36, 1.5f + float(i));
        const uint32_t r = gw.Next();
        image.value[0] = kKinds[(r + i) % 5];
        image.value[1] = 16u << (r % 5);
        image.value[2] = 8u << ((r >> 3) % 5);
        image.value[3] = image.value[1] * 4;
        image.value[4] = (r >> 6) % 8; // entries (0: the faces use their own index)
        image.value[5] = (r >> 9) % 5; // levels
        image.value[6] = 0;
        gw.sgres[i].Reset<kSGRes>(710 + i);
        gw.sgres[i].Returns(8, &gw.persist[i]);
        gw.persist[i].Reset<kPersist>(720 + i);
        gw.persist[i].On(3, &PersistKey);
        RandomResource(i, gw.Next());
    }
    gw.deviceUp = seed % 5 != 0;
    gw.cube = seed % 2 == 1;
    cells.Set(kFrameworkCell, &gw.framework);
    cells.Set(kManipCell, &gw.manip);
}

// ---- noting ----
void NoteState(const cITextureImage *t)
{
    difftest::NotCounted bookkeeping;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(t);
    drawfx::Note("refs, kind, levels", {w[1], w[2], w[3]});
    drawfx::Note("image, key, texture", {drawfx::Ptr(w[4]), w[5], w[6], w[7], w[8], drawfx::Ptr(w[9])});
    drawfx::Note("created, ready", {gw.base.value[0], gw.device.value[0]});
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.device, &gw.com, &gw.base, &gw.face2d, &gw.faceCube, &gw.service, &gw.manip, &gw.framework})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.images) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.formats) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

cIGZUnknown *PickImage(std::mt19937 &rng, bool allowNull)
{
    if (allowNull && rng() % 6 == 0) return nullptr;
    return reinterpret_cast<cIGZUnknown *>(&gw.images[rng() % kImages]);
}

void Step(cITextureImage *t, std::mt19937 &rng)
{
    const unsigned op = rng() % 16;
    drawfx::Note("op", op);
    // Without a device the object has no device texture: the slots that call it unchecked are not called.
    const bool hasTexture = gw.deviceUp;
    switch (op) {
    case 0: {
        static constexpr uint32_t kIids[] = {1, cITextureImage::kIid, cTextureImage::kIidTexture, kIidTexture2D, 0, 0xdeadbeef};
        const uint32_t iid = kIids[rng() % 6];
        void *out = nullptr;
        const bool got = t->QueryInterface(iid, rng() % 5 ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(got), drawfx::Ptr(out)});
        if (got) t->Release();
        break;
    }
    case 1: {
        const uint32_t a = t->AddRef();
        const uint32_t b = t->Release();
        drawfx::Note("addref, release", {a, b});
        break;
    }
    case 2:
    case 3:
        t->Restore();
        break;
    case 4:
        t->ReleaseResource();
        break;
    case 5:
        drawfx::Note("kind", t->GetKind());
        if (hasTexture) drawfx::Note("created", uint32_t(uint8_t(t->IsCreated())));
        break;
    case 6:
        drawfx::Note("device texture count", t->GetDeviceTextureCount());
        drawfx::Note("device texture", drawfx::Ptr(t->GetDeviceTexture(rng() % 3)));
        break;
    case 7:
    case 8:
        t->Load(PickImage(rng, true), rng() % 13);
        break;
    case 9:
        if (hasTexture) t->SetImage(PickImage(rng, gw.base.value[0] == 0));
        break;
    case 10:
    case 11:
        drawfx::Note("image", drawfx::Ptr(t->GetImage()));
        break;
    case 12:
        gw.device.value[0] = rng() % 4 != 0;
        break;
    case 13:
        RandomResource(rng() % kImages, rng());
        break;
    case 14:
        gw.base.value[0] = rng() % 2; // the device lost or regained it behind the texture's back
        break;
    default: {
        Fake &image = gw.images[rng() % kImages];
        static constexpr uint32_t kKinds[] = {0, 6, 7, 1};
        image.value[0] = kKinds[rng() % 4];
        image.value[4] = rng() % 8;
        image.value[5] = rng() % 5;
        break;
    }
    }
    NoteState(t);
}

void Life(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *made;
    if (original) made = ctx.Original<void *(__cdecl *)()>(gw.cube ? 0xab4d30 : 0xab4e50)();
    else made = gw.cube ? cTextureImage::NewCube() : cTextureImage::NewTexture();
    cITextureImage *const t = static_cast<cITextureImage *>(made);
    drawfx::Note("made", drawfx::Ptr(made));
    NoteState(t);
    t->AddRef();
    const unsigned steps = 4 + rng() % 30;
    for (unsigned s = 0; s < steps; ++s) Step(t, rng);
    sgfx::MarkStep(ctx, "last release");
    drawfx::Note("last release", t->Release());
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the factories 0xab4e50 and 0xab4d30, the constructor 0xab4400, the destructor 0xab45d0, the deleting
// destructor 0xab4680, QueryInterface 0xab5aa0, Restore 0xab5610, ReleaseResource 0xab5e00, Load 0xab5d20, SetImage
// 0xab6430, GetImage 0xab5e30, Upload 0xab4780, and the folded slots shared_vf_244060 (Release), shared_vf_ab46a0
// (IsCreated) and shared_vf_ab4b50 (GetDeviceTexture) as this class's vtable has them.
DIFF_TEST(SG_TextureImageSlots, 0xab5610)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "texture image", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            Life(ctx, original, rng);
            ++ctx.cases;
        });
}

// Verifies: 0xab57d0 (CreateDeviceTexture).
DIFF_TEST(SG_TextureImage_CreateDeviceTexture, 0xab57d0)
{
    Stands stands;
    using Fn = void(__cdecl *)(nGZGraphic4::cITextureBase *, const uint32_t *, float, uint32_t);
    const Fn original = ctx.Original<Fn>(0xab57d0);
    sgfx::CheckSides(
        ctx, "create device texture", 400, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool isOriginal, std::mt19937 &rng) {
            static constexpr uint32_t kKinds[] = {4, 6, 0, 5, 7};
            const uint32_t desc[4] = {rng() % 2048, rng() % 2048, rng() % 12, rng()};
            const float quality = float(rng() % 9) * 0.125f;
            const uint32_t kind = kKinds[rng() % 5];
            auto *const texture = reinterpret_cast<nGZGraphic4::cITextureBase *>(&gw.base);
            if (isOriginal) original(texture, desc, quality, kind);
            else cTextureImage::CreateDeviceTexture(texture, desc, quality, kind);
            NoteFakes();
            drawfx::Note("created", gw.base.value[0]);
            ++ctx.cases;
        });
}
