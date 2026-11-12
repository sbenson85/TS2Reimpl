// Differential tests for nTSSG::cBlurBrush (lane 261108d, scenegraph row LT1): src/sg/cBlurBrush.h.
//
// RVAs this file verifies: the constructor 0x3c9e50, the destructor 0x3c9f40, the deleting destructor 0x3c9ff0,
// QueryInterface 0x3cab10, GetStateBlock 0x3ca920, Draw 0x3ca2a0, SetOrigin 0x3cad10, SetOffset 0x3cacf0, SetTexture
// 0x3cad50 (and through the vtable the shared slots it is not claimed for: AddRef 0x148fb0, Release 0x2c1170, SetScale
// shared_vf_3cad30, SetRadius shared_vf_1f9ac0).
//
// Each seed runs a sequence of calls on one brush and records everything: the results of every call, every call the brush
// makes on its fakes (the 2D renderer's ApplyDrawState, LockBatch with its arguments and DrawBatch with a hash of the
// vertex and index words it was left; the device's GetStateConst, Clear with what the colour pointer points at, and
// IsDeviceReady; the state cache's commands with their defined fields, SetTexture and CreateStateBlock; the state block's
// IsCreated and counts; the texture manager's AddRefId / ReleaseId / GetTexture; the scene graph texture's Restore,
// IsCreated and GetDeviceTexture; the device texture's GetWidth / GetHeight) and the object's fields after each call
// (pointers by the fake's name). Two routes, compared:
//   0: the game's code (game new, the constructor 0x3c9e50, then every slot through the game's vtable);
//   1: ours (game new, cBlurBrushAdapters::Construct, then every slot through our vtable).
// The end is the last Release (the deleting destructor, recorded by a shape::DeleteRecorder: the block and the size) or,
// one seed in four, the destructor 0x3c9f40 itself (route 1: the Destruct adapter) and then game delete.
// The device getter 0x18ae40 is a stand-in on both sides; the texture manager is a fake planted in the cache 0x10f8f94
// that both sides' texture handles read.
#include "code_patch.h"
#include "difftest.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/RenderStates.h"
#include "sg/cBlurBrush.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

std::vector<std::string> *gRecord;
shape::DeleteRecorder *gDeletes;

void Note(const char *fmt, ...)
{
    char buffer[400];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    gRecord->emplace_back(buffer);
}

#define FN(f) reinterpret_cast<void *>(&f)

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(object) + offset, 4);
    return w;
}

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// ---- the 2D renderer fake: slots 35 (ApplyDrawState), 36 (LockBatch) and 37 (DrawBatch) ----
constexpr uint32_t kMaxVertices = 16;
constexpr uint32_t kMaxIndices = 48;
constexpr uint32_t kSentinel = 0xa5a5a5a5;

struct FakeDraw2D {
    void *const *vtable;
    uint32_t lockResult; // 1 gives room, 0 refuses
    uint32_t vertices[kMaxVertices * 5];
    uint16_t indices[kMaxIndices];
    bool locked;
};

bool __fastcall FakeLockBatch(FakeDraw2D *self, void *, uint32_t vertices, uint32_t indices, float **positions,
                              uint32_t **colours, float **uvs, uint16_t **indexOut)
{
    Note("LockBatch %u %u -> %u", vertices, indices, self->lockResult);
    if (!self->lockResult) return false;
    for (uint32_t &w : self->vertices) w = kSentinel;
    for (uint16_t &w : self->indices) w = 0xa5a5;
    // positions at +0, texture coordinates at +8, colours at +0x10 of each 20-byte vertex
    *positions = reinterpret_cast<float *>(self->vertices);
    *uvs = reinterpret_cast<float *>(self->vertices) + 2;
    *colours = self->vertices + 4;
    *indexOut = self->indices;
    self->locked = true;
    return true;
}

void __fastcall FakeApplyDrawState(FakeDraw2D *, void *) { Note("ApplyDrawState"); }

void __fastcall FakeDrawBatch(FakeDraw2D *self, void *)
{
    uint32_t hash = 2166136261u;
    uint32_t touched = 0;
    for (uint32_t w : self->vertices) {
        hash = (hash ^ w) * 16777619u;
        touched += w != kSentinel;
    }
    uint32_t touchedIndices = 0;
    for (uint16_t w : self->indices) {
        hash = (hash ^ w) * 16777619u;
        touchedIndices += w != 0xa5a5;
    }
    Note("DrawBatch locked %d vertex words %u index words %u hash %08x first %08x %08x %08x %08x %08x", int(self->locked),
         touched, touchedIndices, hash, self->vertices[0], self->vertices[1], self->vertices[2], self->vertices[3],
         self->vertices[4]);
    self->locked = false;
}

void *gDrawVtable[38];
FakeDraw2D gDraw;

// ---- counted, named fakes ----
struct FakeBase {
    void *const *vtable;
    char name[12];
    int refs;
};

const char *Name(const void *object) { return object ? static_cast<const FakeBase *>(object)->name : "null"; }

uint32_t __fastcall FakeAddRef(FakeBase *self, void *)
{
    Note("%s.AddRef -> %d", self->name, self->refs + 1);
    return uint32_t(++self->refs);
}
uint32_t __fastcall FakeRelease(FakeBase *self, void *)
{
    Note("%s.Release -> %d", self->name, self->refs - 1);
    return uint32_t(--self->refs);
}

void SetName(FakeBase *fake, void *const *vtable, const char *format, int index = 0)
{
    fake->vtable = vtable;
    std::snprintf(fake->name, sizeof(fake->name), format, index);
    fake->refs = 0;
}

// What the fakes answer, drawn by the sequence before each call.
struct Script {
    bool blockOk, restoreOk, haveDevice, deviceReady;
};
Script gScript;

// The state blocks.
struct FakeBlock : FakeBase {
    uint32_t created;
};
constexpr int kBlocks = 64;
FakeBlock gBlocks[kBlocks];
int gNextBlock;

bool __fastcall FakeBlockIsCreated(FakeBlock *self, void *)
{
    Note("%s.IsCreated -> %u", self->name, self->created);
    return self->created != 0;
}

// The device textures (slots 17 GetWidth, 18 GetHeight) and the scene graph textures (3 Restore, 6 IsCreated, 8
// GetDeviceTexture) the manager hands out.
struct FakeDeviceTexture : FakeBase {
    uint32_t width, height;
};
struct FakeSgTexture : FakeBase {
    uint32_t created;
    FakeDeviceTexture device;
};
constexpr int kTextures = 4;
FakeSgTexture gTextures[kTextures];

uint32_t __fastcall FakeGetWidth(FakeDeviceTexture *self, void *, uint32_t level)
{
    Note("%s.GetWidth %u -> %u", self->name, level, self->width);
    return self->width;
}
uint32_t __fastcall FakeGetHeight(FakeDeviceTexture *self, void *, uint32_t level)
{
    Note("%s.GetHeight %u -> %u", self->name, level, self->height);
    return self->height;
}
void __fastcall FakeRestore(FakeSgTexture *self, void *)
{
    Note("%s.Restore -> %d", self->name, int(gScript.restoreOk));
    if (gScript.restoreOk) self->created = 1;
}
bool __fastcall FakeSgIsCreated(FakeSgTexture *self, void *)
{
    Note("%s.IsCreated -> %u", self->name, self->created);
    return self->created != 0;
}
void *__fastcall FakeGetDeviceTexture(FakeSgTexture *self, void *, uint32_t face)
{
    Note("%s.GetDeviceTexture %u", self->name, face);
    return &self->device;
}

// The texture manager (planted in the cache 0x10f8f94): slots 18 AddRefId, 19 ReleaseId, 23 GetTexture.
FakeBase gManager;
void __fastcall FakeAddRefId(FakeBase *, void *, uint32_t id) { Note("manager.AddRefId %08x", id); }
void __fastcall FakeReleaseId(FakeBase *, void *, uint32_t id) { Note("manager.ReleaseId %08x", id); }
void *__fastcall FakeGetTexture(FakeBase *, void *, uint32_t id)
{
    FakeSgTexture *const texture = &gTextures[id % kTextures];
    Note("manager.GetTexture %08x -> %s", id, texture->name);
    return texture;
}

// The state cache: slots 10 CreateStateBlock, 12 ApplyStateCommand, 13 SetTexture.
void __fastcall FakeApplyStateCommand(void *, void *, uint32_t word)
{
    const auto *const header = reinterpret_cast<const cStateHeader *>(uintptr_t(word));
    const uint32_t vtable = Word(header, 0) - uint32_t(uintptr_t(GameAddress<void>(0)));
    if (header->mId == 0x14) {
        const auto *const c = reinterpret_cast<const cDepthTestState *>(header);
        Note("command depth vtable %08x size %u test %u write %u", vtable, c->mSize, c->mTestEnabled, c->mWriteEnabled);
    } else if (header->mId == 0x15) {
        const auto *const c = reinterpret_cast<const cDepthTestFunction *>(header);
        Note("command depth function vtable %08x size %u function %u", vtable, c->mSize, c->mFunction);
    } else if (header->mId == 0x1f) {
        const auto *const c = reinterpret_cast<const cTextureMipFilterState *>(header);
        Note("command mip vtable %08x size %u sampler %u filter %u bias %u", vtable, c->mSize, Word(c, 8), c->mMipFilter,
             c->mLodBias);
    } else if (header->mId == 0x19) {
        const auto *const c = reinterpret_cast<const cTextureBlendState *>(header);
        Note("command blend vtable %08x size %u stage %u colour %u %u %u %u alpha %u %u %u %u result %u", vtable, c->mSize,
             Word(c, 8), c->mColorOp, c->mColorArg1, c->mColorArg2, c->mColorArg0, c->mAlphaOp, c->mAlphaArg1, c->mAlphaArg2,
             c->mAlphaArg0, c->mResultArg);
    } else if (header->mId == 0x11) {
        const auto *const c = reinterpret_cast<const cAlphaBlendState *>(header);
        Note("command alpha vtable %08x size %u enabled %u source %u dest %u operation %u", vtable, c->mSize, c->mEnabled,
             c->mSource, c->mDest, c->mOperation);
    } else {
        Note("command id %u size %u", header->mId, header->mSize);
    }
}
void __fastcall FakeSetTexture(void *, void *, uint32_t stage, void *texture) { Note("SetTexture %u %s", stage, Name(texture)); }
uint32_t __fastcall FakeCreateStateBlock(void *, void *, uint32_t groups, uint32_t out)
{
    void **const slot = reinterpret_cast<void **>(uintptr_t(out));
    Note("CreateStateBlock %u slot %s", groups, Name(*slot));
    if (!gScript.blockOk) return 0;
    FakeBlock *const block = &gBlocks[gNextBlock++ % kBlocks];
    ++block->refs;
    block->created = 1;
    *slot = block;
    return 1;
}

// The device: slots 20 GetStateConst, 27 Clear, 35 IsDeviceReady.
struct FakeDevice : FakeBase {
    FakeBase state;
};
FakeDevice gDevice;

void *__fastcall FakeGetState(FakeDevice *self, void *)
{
    Note("device.GetStateConst");
    return &self->state;
}
void __fastcall FakeClear(FakeDevice *, void *, uint32_t flags, const float *colour, float z, uint32_t stencil)
{
    Note("device.Clear %08x colour %s %08x %08x %08x %08x z %08x stencil %u", flags,
         colour == GameAddress<const float>(0x10da49c) ? "global" : "other", Bits(colour[0]), Bits(colour[1]), Bits(colour[2]),
         Bits(colour[3]), Bits(z), stencil);
}
bool __fastcall FakeIsDeviceReady(FakeDevice *, void *)
{
    Note("device.IsDeviceReady -> %d", int(gScript.deviceReady));
    return gScript.deviceReady;
}

cIDeviceDX *__cdecl StandDevice()
{
    Note("GetCurrentDeviceCached -> %s", gScript.haveDevice ? "device" : "null");
    return gScript.haveDevice ? reinterpret_cast<cIDeviceDX *>(&gDevice) : nullptr;
}

void InitWorld()
{
    static void *blockVtable[8], *deviceTextureVtable[20], *sgTextureVtable[10], *managerVtable[24], *stateVtable[14],
        *deviceVtable[36];
    gDrawVtable[35] = FN(FakeApplyDrawState);
    gDrawVtable[36] = FN(FakeLockBatch);
    gDrawVtable[37] = FN(FakeDrawBatch);
    gDraw.vtable = gDrawVtable;
    gDraw.lockResult = 1;
    gDraw.locked = false;
    blockVtable[1] = deviceTextureVtable[1] = sgTextureVtable[1] = FN(FakeAddRef);
    blockVtable[2] = deviceTextureVtable[2] = sgTextureVtable[2] = FN(FakeRelease);
    blockVtable[6] = FN(FakeBlockIsCreated);
    deviceTextureVtable[17] = FN(FakeGetWidth);
    deviceTextureVtable[18] = FN(FakeGetHeight);
    sgTextureVtable[3] = FN(FakeRestore);
    sgTextureVtable[6] = FN(FakeSgIsCreated);
    sgTextureVtable[8] = FN(FakeGetDeviceTexture);
    managerVtable[18] = FN(FakeAddRefId);
    managerVtable[19] = FN(FakeReleaseId);
    managerVtable[23] = FN(FakeGetTexture);
    stateVtable[10] = FN(FakeCreateStateBlock);
    stateVtable[12] = FN(FakeApplyStateCommand);
    stateVtable[13] = FN(FakeSetTexture);
    deviceVtable[20] = FN(FakeGetState);
    deviceVtable[27] = FN(FakeClear);
    deviceVtable[35] = FN(FakeIsDeviceReady);
    for (int i = 0; i < kBlocks; ++i) {
        SetName(&gBlocks[i], blockVtable, "block%d", i);
        gBlocks[i].created = 0;
    }
    gNextBlock = 0;
    for (int i = 0; i < kTextures; ++i) {
        SetName(&gTextures[i], sgTextureVtable, "tex%d", i);
        gTextures[i].created = 0;
        SetName(&gTextures[i].device, deviceTextureVtable, "dtex%d", i);
    }
    SetName(&gManager, managerVtable, "manager");
    SetName(&gDevice, deviceVtable, "device");
    SetName(&gDevice.state, stateVtable, "state");
}

enum Route { kOriginal, kOurs };

void ReserveRecord() { gRecord->reserve(gRecord->size() + 64); }

struct Sequence {
    Route route;
    std::mt19937 random;
    difftest::Context *ctx;

    uint32_t Next(uint32_t n) { return static_cast<uint32_t>(random() % n); }
    float Coord()
    {
        const uint32_t kind = Next(8);
        if (kind == 0) return float(int32_t(Next(21)) - 10);
        return float(int32_t(Next(200001)) - 100000) / 1000.0f;
    }
    float Small()
    {
        const uint32_t kind = Next(6);
        if (kind == 0) return 0.0f;
        if (kind == 1) return float(int32_t(Next(9)) - 4);
        return float(int32_t(Next(20001)) - 10000) / 1000.0f;
    }

    void Dump(const void *brush)
    {
        Note("fields count %u texture %08x block %s origin %08x %08x offset %08x %08x scale %08x %08x radius %d", Word(brush, 4),
             Word(brush, 8), Name(reinterpret_cast<void *>(uintptr_t(Word(brush, 0xc)))), Word(brush, 0x10), Word(brush, 0x14),
             Word(brush, 0x18), Word(brush, 0x1c), Word(brush, 0x20), Word(brush, 0x24), int32_t(Word(brush, 0x28)));
    }

    void *NewBrush()
    {
        void *const memory = GameNew(nTSSG::cBlurBrush::kSize);
        std::memset(memory, 0xcd, nTSSG::cBlurBrush::kSize);
        if (route == kOriginal) return ctx->Original<void *(__thiscall *)(void *)>(0x3c9e50)(memory);
        return nTSSG::cBlurBrushAdapters::Construct(static_cast<nTSSG::cBlurBrush *>(memory), nullptr);
    }

    void RandomScript()
    {
        gScript.blockOk = Next(10) != 0;
        gScript.restoreOk = Next(3) != 0;
        gScript.haveDevice = Next(12) != 0;
        gScript.deviceReady = Next(10) != 0;
    }

    void Draw(void *brush)
    {
        const uint32_t n = Next(9) == 0 ? Next(3) : 3 + Next(8);
        std::vector<float> points;
        for (uint32_t i = 0; i < n; ++i) {
            points.push_back(Coord());
            points.push_back(Coord());
        }
        if (n >= 3 && Next(5) == 0) points[4] = points[0]; // a zero span
        while (points.size() < 6) points.push_back(Coord()); // Draw reads the third point whatever the count
        std::vector<float> values(n + 1, 0.0f);
        gDraw.lockResult = Next(10) != 0;
        const uint32_t kind = Next(2);
        Note("Draw kind %u n %u", kind, n);
        RawSlot<4, void>(brush, static_cast<void *>(&gDraw), kind, n, points.data(), values.data());
    }

    void Run(uint32_t seed)
    {
        InitWorld();
        RandomScript();
        // Texture sizes: small powers of two, odd sizes, now and then zero.
        for (int i = 0; i < kTextures; ++i) {
            gTextures[i].device.width = Next(8) == 0 ? 0 : 1u << Next(11);
            gTextures[i].device.height = Next(8) == 0 ? 0 : (Next(3) == 0 ? 1 + Next(1000) : 1u << Next(11));
            gTextures[i].created = Next(2);
        }
        void *const brush = NewBrush();
        Dump(brush);
        uint32_t held = 1;
        RawSlot<1>(brush);
        for (int op = 0; op < 60; ++op) {
            const uint32_t what = Next(16);
            if (what == 0) {
                const uint32_t ids[] = {1, nTSSG::cBlurBrush::kIidBrush, 0, 2, 0x3c0ac4a4, 0x9c012573, 0xdc004963};
                const uint32_t id = ids[Next(7)];
                void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                const bool nullOut = Next(6) == 0;
                const bool answer = RawSlot<0, bool>(brush, id, nullOut ? nullptr : &out);
                Note("QueryInterface %08x null=%d -> %d out=%s", id, int(nullOut), int(answer),
                     out == brush ? "self" : (out == reinterpret_cast<void *>(uintptr_t(0x1234)) ? "untouched" : "other"));
                if (answer && !nullOut) ++held;
            } else if (what == 1) {
                Note("AddRef -> %u", RawSlot<1>(brush));
                ++held;
            } else if (what == 2) {
                if (held > 1) {
                    Note("Release -> %u", RawSlot<2>(brush));
                    --held;
                }
            } else if (what == 3) {
                const float x = Small(), y = Small();
                Note("SetOrigin %08x %08x", Bits(x), Bits(y));
                RawSlot<6, void>(brush, x, y);
            } else if (what == 4) {
                const float x = Small(), y = Small();
                Note("SetOffset %08x %08x", Bits(x), Bits(y));
                RawSlot<7, void>(brush, x, y);
            } else if (what == 5) {
                const float x = Small(), y = Small();
                Note("SetScale %08x %08x", Bits(x), Bits(y));
                RawSlot<8, void>(brush, x, y);
            } else if (what == 6) {
                const uint32_t id = Next(4) == 0 ? 0xffffffffu : (Next(3) == 0 ? 0x80000000u + Next(8) : Next(12));
                Note("SetTexture %08x", id);
                RawSlot<9, void>(brush, id);
            } else if (what == 7) {
                const int32_t radius = Next(6) == 0 ? -int32_t(Next(3)) : int32_t(Next(4));
                Note("SetRadius %d", radius);
                RawSlot<10, void>(brush, radius);
            } else if (what == 8) {
                if (Next(2) && gNextBlock > 0) gBlocks[(gNextBlock - 1) % kBlocks].created = 0;
                else gTextures[Next(kTextures)].created = 0;
                Note("lost a resource");
            } else if (what < 12) {
                RandomScript();
                Note("GetStateBlock");
                const uint32_t block = RawSlot<3>(brush, static_cast<void *>(&gDraw), static_cast<void *>(&gDevice));
                Note("GetStateBlock -> %s", Name(reinterpret_cast<void *>(uintptr_t(block))));
            } else {
                RandomScript();
                Draw(brush);
            }
            Dump(brush);
        }
        while (held > 1) {
            Note("Release -> %u", RawSlot<2>(brush));
            --held;
        }
        ReserveRecord();
        if (seed % 4 == 0) {
            // The destructor alone (hooked at 0x3c9f40), then the block back to game delete.
            if (route == kOriginal) ctx->Original<void(__thiscall *)(void *)>(0x3c9f40)(brush);
            else nTSSG::cBlurBrushAdapters::Destruct(static_cast<nTSSG::cBlurBrush *>(brush), nullptr);
            Note("destructed");
            GameDelete(brush);
        } else {
            gDeletes->Begin();
            const uint32_t count = RawSlot<2>(brush);
            gDeletes->End();
            Note("final Release -> %u", count);
            for (int i = 0; i < gDeletes->Count(); ++i) {
                const shape::DeleteRecorder::Call &call = gDeletes->At(i);
                Note("delete %d %s size %u", i, call.block == brush ? "object" : "other", call.size);
            }
            Note("deletes %d overflow %d", gDeletes->Count(), int(gDeletes->Overflowed()));
            gDeletes->Release();
        }
        // What the brush still holds: nothing, once it is gone.
        char line[200];
        int length = 0;
        for (const FakeBlock &block : gBlocks)
            if (block.refs && length < 150) length += std::snprintf(line + length, sizeof(line) - size_t(length), "%s=%d ", block.name, block.refs);
        Note("left %s", length ? line : "none");
    }
};

std::vector<std::string> RunRoute(difftest::Context &ctx, Route route, uint32_t seed)
{
    std::vector<std::string> record;
    record.reserve(4096);
    gRecord = &record;
    shape::DeleteRecorder deletes;
    gDeletes = &deletes;
    Sequence sequence;
    sequence.route = route;
    sequence.ctx = &ctx;
    sequence.random.seed(seed);
    sequence.Run(seed);
    gRecord = nullptr;
    gDeletes = nullptr;
    return record;
}

struct PlantManager {
    void **cache;
    void *saved;
    explicit PlantManager(difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10f8f94)), saved(*cache) { *cache = &gManager; }
    ~PlantManager() { *cache = saved; }
};

void Compare(difftest::Context &ctx, uint32_t seeds)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    PlantManager manager(ctx);
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        ++ctx.cases;
        char mark[64];
        std::snprintf(mark, sizeof(mark), "seed %u original", unsigned(seed));
        ctx.Mark(mark);
        const std::vector<std::string> expected = RunRoute(ctx, kOriginal, seed);
        std::snprintf(mark, sizeof(mark), "seed %u ours", unsigned(seed));
        ctx.Mark(mark);
        const std::vector<std::string> actual = RunRoute(ctx, kOurs, seed);
        const size_t n = expected.size() < actual.size() ? expected.size() : actual.size();
        size_t i = 0;
        while (i < n && expected[i] == actual[i]) ++i;
        if (i == n && expected.size() == actual.size()) continue;
        ctx.Fail("seed %u line %u: original '%s' ours '%s' (previous '%s')", unsigned(seed), unsigned(i),
                 i < expected.size() ? expected[i].c_str() : "<end>", i < actual.size() ? actual[i].c_str() : "<end>",
                 i > 0 ? expected[i - 1].c_str() : "");
    }
}

} // namespace

DIFF_TEST(BlurBrush, 0x3ca2a0) { Compare(ctx, 300); }
