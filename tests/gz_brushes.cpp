// Differential tests for the 2D brushes (lanes WK and WN, rendering rows RB, RD): src/gz/cBrushes.h (nGZGraphic4::cColorBrush,
// cOutlineColorBrush and cPatternBrush).
//
// RVAs this file verifies: cColorBrush creator 0x1538b0, QueryInterface 0x153ec0, Draw 0x153090, Resize 0x153fe0, SetColor
// 0x153fb0, GetColor 0x153070, deleting destructor 0x152f00 (and the shared slots: AddRef 0x148fb0, Release 0x153f50,
// GetStateBlock 0x6c6a0, Size 0x153ad0); cOutlineColorBrush creator 0x1538f0, QueryInterface 0x153ef0, Draw 0x1533b0,
// 0x153fa0 0x153fd0 0x153060 0x1538a0, deleting destructor 0x152f70; cPatternBrush creator 0x153930, QueryInterface
// 0x153f20, GetStateBlock 0x153ae0, Draw 0x153770, SetImage 0x153a40, ResetCache 0x154210, SetAngle 0x154150, SetOffset
// 0x154050, deleting destructor 0x152fa0 (and the shared slots: Release 0x153f70, GetAngle 0x153f90, GetOffset 0x147810).
//
// Each seed runs a sequence of calls on one brush and records everything: the results and out-parameters of every call,
// every call the brush makes on its fakes (the 2D renderer's LockBatch with its arguments and DrawBatch with a hash of
// the vertex and index words it was left; the pattern brush's texture, surfaces, state block, device, enumeration,
// framework and image manipulator) and, for the pattern brush, the whole object after each call (the bitmap as size,
// capacity and a hash of its bytes, pointers by the fake's name). Two routes, compared:
//   0: the game's code (the creator, then every slot through the game's vtable);
//   1: ours (our creator and every slot through our vtable).
// The game's operator new / delete (0x824632 / 0x824624) are ours on both routes, so the brushes' heap blocks (the
// objects, the colour tables, the bitmaps) come and go alike. A shape::DeleteRecorder records the blocks the last Release
// frees and those SetImage's growth frees, by what they are and the size handed to the sized delete: the object first or
// last, the table or bitmap (and over 0x1000 bytes the aligned block's size), the allocator and its order compared.
// The 2D renderer fake hands out one static vertex buffer filled with a sentinel before each lock, so a write outside the
// vertices or indices the brush was asked for, or one missing, shows in the hash.
#include "code_patch.h"
#include "difftest.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/GraphicModeSearch.h"
#include "gz/GraphicServices.h"
#include "gz/RenderStates.h"
#include "gz/cBrushes.h"
#include "gz/cDeviceEnum.h"
#include "rz/StaticGetters.h"
#include "sg/TextureDescription.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

std::vector<std::string> *gRecord;
difftest::Context *gCtx;
shape::DeleteRecorder *gDeletes;

void Note(const char *fmt, ...)
{
    char buffer[256];
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

float FromBits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(object) + offset, 4);
    return w;
}

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// ---- the 2D renderer fake: slots 36 (LockBatch) and 37 (DrawBatch) ----
constexpr uint32_t kMaxVertices = 64;
constexpr uint32_t kMaxIndices = 192;
constexpr uint32_t kSentinel = 0xa5a5a5a5;

struct FakeDraw2D {
    void *const *vtable;
    uint32_t lockResult; // 1 gives room, 0 refuses
    uint32_t vertices[kMaxVertices * 5];
    uint16_t indices[kMaxIndices];
    uint32_t lockedVertices, lockedIndices;
    bool locked;
};

bool __fastcall FakeLockBatch(FakeDraw2D *self, void *, uint32_t vertices, uint32_t indices, float **positions,
                              uint32_t **colours, float **uvs, uint16_t **indexOut)
{
    Note("LockBatch %u %u", vertices, indices);
    if (!self->lockResult) return false;
    for (uint32_t &w : self->vertices) w = kSentinel;
    for (uint16_t &w : self->indices) w = 0xa5a5;
    // positions at +0, texture coordinates at +8, colours at +0x10 of each 20-byte vertex
    *positions = reinterpret_cast<float *>(self->vertices);
    *uvs = reinterpret_cast<float *>(self->vertices) + 2;
    *colours = self->vertices + 4;
    *indexOut = self->indices;
    self->lockedVertices = vertices < kMaxVertices ? vertices : kMaxVertices;
    self->lockedIndices = indices < kMaxIndices ? indices : kMaxIndices;
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
    Note("DrawBatch locked %d vertex words %u index words %u hash %08x", int(self->locked), touched, touchedIndices, hash);
    // The first words, readable when a hash differs.
    char line[400] = "";
    int length = 0;
    for (uint32_t i = 0; i < 30 && i < self->lockedVertices * 5; ++i)
        length += std::snprintf(line + length, sizeof(line) - size_t(length), "%08x ", self->vertices[i]);
    Note("  %s", line);
    self->locked = false;
}

void *gDrawVtable[38];

FakeDraw2D *MakeDraw()
{
    static FakeDraw2D draw;
    gDrawVtable[35] = FN(FakeApplyDrawState);
    gDrawVtable[36] = FN(FakeLockBatch);
    gDrawVtable[37] = FN(FakeDrawBatch);
    draw.vtable = gDrawVtable;
    draw.lockResult = 1;
    draw.locked = false;
    return &draw;
}

// ---- the pattern brush's world ----
// Every fake is a counted object with a name; pointers in the record are these names, so both routes read alike.
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

// What the fakes answer this GetStateBlock, drawn by the sequence before the call.
struct Script {
    bool uploadOk, loadOk, drawOk, copyOk, blockOk, nullBegin, nullEnd, nullClone;
    int32_t fitWidth, fitHeight;
    uint32_t fitLevels;
};
Script gScript;

struct FakeSurface : FakeBase {};

struct FakeTexture : FakeBase {
    uint32_t created, width, height;
    FakeSurface surface;
};

struct FakeBlock : FakeBase {
    uint32_t created;
};

bool __fastcall FakeTextureIsCreated(FakeTexture *self, void *)
{
    Note("%s.IsCreated -> %u", self->name, self->created);
    return self->created != 0;
}
uint32_t __fastcall FakeGetWidth(FakeTexture *self, void *, uint32_t level)
{
    Note("%s.GetWidth %u -> %u", self->name, level, self->width);
    return self->width;
}
uint32_t __fastcall FakeGetHeight(FakeTexture *self, void *, uint32_t level)
{
    Note("%s.GetHeight %u -> %u", self->name, level, self->height);
    return self->height;
}
bool __fastcall FakeTextureCreate(FakeTexture *self, void *, void *device, uint32_t width, uint32_t height, void *format,
                                  uint32_t levels, float quality, uint32_t pool)
{
    const bool ok = pool == 1 ? gScript.uploadOk : gScript.drawOk;
    Note("%s.Create %s %u %u %s %u %08x %u -> %d", self->name, Name(device), width, height, Name(format), levels,
         Bits(quality), pool, int(ok));
    self->width = width;
    self->height = height;
    self->created = ok;
    return ok;
}
void *__fastcall FakeTargetSurface(FakeTexture *self, void *, uint32_t level)
{
    Note("%s.GetTargetSurface %u", self->name, level);
    return &self->surface;
}
bool __fastcall FakeSurfaceCopy(FakeSurface *self, void *, void *source)
{
    Note("%s.Copy %s -> %d", self->name, Name(source), int(gScript.copyOk));
    return gScript.copyOk;
}
bool __fastcall FakeBlockIsCreated(FakeBlock *self, void *)
{
    Note("%s.IsCreated -> %u", self->name, self->created);
    return self->created != 0;
}

void *gTextureVtable[30];
void *gSurfaceVtable[20];
void *gBlockVtable[10];

constexpr int kTextures = 128;
constexpr int kBlocks = 64;
FakeTexture gTextures[kTextures];
FakeBlock gBlocks[kBlocks];
int gNextTexture, gNextBlock;

// The framework (GetClassObject) and the image manipulator (LoadSurfaceFromMemory).
FakeBase gCom, gManip;

bool __fastcall FakeGetClassObject(FakeBase *, void *, uint32_t classId, uint32_t iid, void **out)
{
    Note("GetClassObject %08x %08x slot %s", classId, iid, *out ? "full" : "empty");
    FakeTexture *const texture = &gTextures[gNextTexture++ % kTextures];
    ++texture->refs;
    *out = texture;
    return true;
}

uint32_t HashBytes(const void *bytes, size_t size)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ static_cast<const uint8_t *>(bytes)[i]) * 16777619u;
    return hash;
}

bool __fastcall FakeLoadSurface(FakeBase *, void *, const void *bits, void *format, uint32_t width, uint32_t height,
                                uint32_t pitch, void *target)
{
    Note("LoadSurfaceFromMemory %s hash %08x %s %u %u %u %s -> %d", bits ? "bits" : "null", HashBytes(bits, size_t(height) * pitch),
         Name(format), width, height, pitch, Name(target), int(gScript.loadOk));
    return gScript.loadOk;
}

// The formats, the range the enumeration hands out, and the state cache.
struct FakeFormat : FakeBase {
    uint32_t classId;
};
uint32_t __fastcall FakeGetClassId(FakeFormat *self, void *)
{
    Note("%s.GetClassId -> %08x", self->name, self->classId);
    return self->classId;
}

FakeBase gIterators[4]; // cursor, begin, end, the cursor's clone

void *__fastcall FakeClone(FakeBase *self, void *)
{
    Note("%s.Clone -> %s", self->name, gScript.nullClone ? "null" : gIterators[3].name);
    return gScript.nullClone ? nullptr : &gIterators[3];
}

cPixelFormatRange *__fastcall FakeColorFormats(void *, void *, cPixelFormatRange *out, uint32_t usage, uint32_t classId)
{
    Note("GetColorFormatsForDisplay %u %08x", usage, classId);
    out->mCurrent = reinterpret_cast<cIPixelFormatIterator *>(&gIterators[0]);
    out->mBegin = reinterpret_cast<cIPixelFormatIterator *>(gScript.nullBegin ? nullptr : &gIterators[1]);
    out->mEnd = reinterpret_cast<cIPixelFormatIterator *>(gScript.nullEnd ? nullptr : &gIterators[2]);
    return out;
}

FakeFormat gFormats[3]; // two the sequence hands SetImage, the third what the lower bound chose

void __fastcall FakeApplyStateCommand(void *, void *, uint32_t word)
{
    const auto *const header = reinterpret_cast<const cStateHeader *>(uintptr_t(word));
    if (header->mId == 0x1e) {
        const auto *const c = reinterpret_cast<const cTextureFilterState *>(header);
        Note("command filter vtable %08x size %u sampler %u min %u mag %u anisotropy %u", Word(c, 0), c->mSize, Word(c, 8),
             c->mMinFilter, c->mMagFilter, c->mMaxAnisotropy);
    } else if (header->mId == 0x1f) {
        const auto *const c = reinterpret_cast<const cTextureMipFilterState *>(header);
        Note("command mip vtable %08x size %u sampler %u filter %u bias %u", Word(c, 0), c->mSize, Word(c, 8), c->mMipFilter,
             c->mLodBias);
    } else if (header->mId == 0x19) {
        const auto *const c = reinterpret_cast<const cTextureBlendState *>(header);
        Note("command blend vtable %08x size %u stage %u colour %u %u %u %u alpha %u %u %u %u result %u", Word(c, 0), c->mSize,
             Word(c, 8), c->mColorOp, c->mColorArg1, c->mColorArg2, c->mColorArg0, c->mAlphaOp, c->mAlphaArg1, c->mAlphaArg2,
             c->mAlphaArg0, c->mResultArg);
    } else {
        Note("command id %u", header->mId);
    }
}
void __fastcall FakeSetTexture(void *, void *, uint32_t stage, void *texture) { Note("SetTexture %u %s", stage, Name(texture)); }
uint32_t __fastcall FakeCreateStateBlock(void *, void *, uint32_t groups, uint32_t out)
{
    void **const slot = reinterpret_cast<void **>(uintptr_t(out));
    Note("CreateStateBlock %u slot %s", groups, *slot ? "full" : "empty");
    if (!gScript.blockOk) return 0;
    FakeBlock *const block = &gBlocks[gNextBlock++ % kBlocks];
    ++block->refs;
    block->created = 1;
    *slot = block;
    return 1;
}

struct FakeDevice : FakeBase {
    FakeBase enumeration, state;
};
FakeDevice gDevice;

void *__fastcall FakeGetEnum(FakeDevice *self, void *)
{
    Note("device.GetDeviceEnumConst");
    return &self->enumeration;
}
void *__fastcall FakeGetState(FakeDevice *self, void *)
{
    Note("device.GetStateConst");
    return &self->state;
}

// Stand-ins for the callees with their own tests.
void *StandCom()
{
    Note("GetDirectorFrameworkObject");
    return &gCom;
}
void *__cdecl StandManip()
{
    Note("GetImageManipCached");
    return &gManip;
}
cIPixelFormat *__cdecl StandLowerBound(cPixelFormatRange range, cIPixelFormat *wanted)
{
    Note("FindFormatLowerBound %s %s %s %s", Name(range.mCurrent), Name(range.mBegin), Name(range.mEnd), Name(wanted));
    if (range.mEnd) RawSlot<2>(range.mEnd);
    if (range.mBegin) RawSlot<2>(range.mBegin);
    if (range.mCurrent) RawSlot<2>(range.mCurrent);
    return reinterpret_cast<cIPixelFormat *>(&gFormats[2]);
}
void __cdecl StandFit(nGZGraphic4::cIDeviceDX *device, cIPixelFormat *format, int32_t *width, int32_t *height, uint32_t *levels,
                      bool allowNonPow2)
{
    Note("AdjustTextureSize %s %s %d %d %u %d", Name(device), Name(format), *width, *height, *levels, int(allowNonPow2));
    *width = gScript.fitWidth;
    *height = gScript.fitHeight;
    *levels = gScript.fitLevels;
}

void SetName(FakeBase *fake, void *const *vtable, const char *format, int index = 0)
{
    fake->vtable = vtable;
    std::snprintf(fake->name, sizeof(fake->name), format, index);
    fake->refs = 0;
}

void InitPatternWorld()
{
    static void *comVtable[4], *manipVtable[7], *formatVtable[6], *iteratorVtable[4], *enumVtable[10], *stateVtable[14],
        *deviceVtable[21];
    gTextureVtable[1] = gSurfaceVtable[1] = gBlockVtable[1] = iteratorVtable[1] = FN(FakeAddRef);
    gTextureVtable[2] = gSurfaceVtable[2] = gBlockVtable[2] = iteratorVtable[2] = FN(FakeRelease);
    gTextureVtable[6] = FN(FakeTextureIsCreated);
    gTextureVtable[17] = FN(FakeGetWidth);
    gTextureVtable[18] = FN(FakeGetHeight);
    gTextureVtable[22] = FN(FakeTextureCreate);
    gTextureVtable[26] = FN(FakeTargetSurface);
    gSurfaceVtable[16] = FN(FakeSurfaceCopy);
    gBlockVtable[6] = FN(FakeBlockIsCreated);
    comVtable[3] = FN(FakeGetClassObject);
    manipVtable[6] = FN(FakeLoadSurface);
    formatVtable[5] = FN(FakeGetClassId);
    iteratorVtable[3] = FN(FakeClone);
    enumVtable[9] = FN(FakeColorFormats);
    stateVtable[10] = FN(FakeCreateStateBlock);
    stateVtable[12] = FN(FakeApplyStateCommand);
    stateVtable[13] = FN(FakeSetTexture);
    deviceVtable[16] = FN(FakeGetEnum);
    deviceVtable[20] = FN(FakeGetState);
    for (int i = 0; i < kTextures; ++i) {
        SetName(&gTextures[i], gTextureVtable, "tex%d", i);
        gTextures[i].created = gTextures[i].width = gTextures[i].height = 0;
        SetName(&gTextures[i].surface, gSurfaceVtable, "surf%d", i);
    }
    for (int i = 0; i < kBlocks; ++i) {
        SetName(&gBlocks[i], gBlockVtable, "block%d", i);
        gBlocks[i].created = 0;
    }
    gNextTexture = gNextBlock = 0;
    SetName(&gCom, comVtable, "com");
    SetName(&gManip, manipVtable, "manip");
    for (int i = 0; i < 3; ++i) {
        SetName(&gFormats[i], formatVtable, "format%d", i);
        gFormats[i].classId = 0x100 + uint32_t(i);
    }
    const char *const iterators[] = {"cursor", "begin", "end", "clone"};
    for (int i = 0; i < 4; ++i) SetName(&gIterators[i], iteratorVtable, iterators[i]);
    SetName(&gDevice, deviceVtable, "device");
    SetName(&gDevice.enumeration, enumVtable, "enum");
    SetName(&gDevice.state, stateVtable, "state");
}

enum Route { kOriginal, kOurs };

void *Pointer(const void *object, uint32_t offset) { return reinterpret_cast<void *>(uintptr_t(Word(object, offset))); }

// Room in the record, so that the fakes' notes inside a recorded window free nothing of the harness's.
void ReserveRecord() { gRecord->reserve(gRecord->size() + 64); }

// What the window freed: the object, the table or bitmap whose begin was `begin`, or another block (over 0x1000 bytes a
// table's block is below its aligned begin), each with the size handed to the delete.
void NoteDeletes(const void *brush, uint32_t begin)
{
    for (int i = 0; i < gDeletes->Count(); ++i) {
        const shape::DeleteRecorder::Call &call = gDeletes->At(i);
        const char *const what = call.block == brush ? "object" : uint32_t(uintptr_t(call.block)) == begin ? "storage" : "aligned";
        Note("delete %d %s size %u", i, what, call.size);
    }
    Note("deletes %d overflow %d", gDeletes->Count(), int(gDeletes->Overflowed()));
    gDeletes->Release();
}

// The last Release: the object's blocks freed through the deleting destructor.
void FinalRelease(void *brush)
{
    const uint32_t begin = Word(brush, 8);
    ReserveRecord();
    gDeletes->Begin();
    const uint32_t count = RawSlot<2>(brush);
    gDeletes->End();
    Note("final Release -> %u", count);
    NoteDeletes(brush, begin);
}

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
    template <class Fn> Fn Orig(uint32_t rva) { return ctx->Original<Fn>(rva); }

    std::vector<float> Points(uint32_t n)
    {
        std::vector<float> points;
        for (uint32_t i = 0; i < n; ++i) {
            if (i > 0 && Next(25) == 0) { // a repeated point (a zero-length edge)
                points.push_back(points[points.size() - 2]);
                points.push_back(points[points.size() - 2]);
                continue;
            }
            points.push_back(Coord());
            points.push_back(Coord());
        }
        return points;
    }

    // ---- the colour brush ----
    void *NewColorBrush()
    {
        if (route == kOriginal) return Orig<void *(__cdecl *)()>(0x1538b0)();
        return cColorBrush::Create();
    }
    void *NewOutlineBrush()
    {
        if (route == kOriginal) return Orig<void *(__cdecl *)()>(0x1538f0)();
        return cOutlineColorBrush::Create();
    }

    void CommonSlots(void *brush, uint32_t iidA, uint32_t iidB, uint32_t &held)
    {
        switch (Next(5)) {
        case 0: {
            const uint32_t ids[] = {1, iidA, iidB, 0, 2, 0x3c0ac4a4, 0xfc1d3c6b, 0x9c012573, 0xdc004962};
            const uint32_t id = ids[Next(9)];
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            const bool nullOut = Next(6) == 0;
            const bool answer = RawSlot<0, bool>(brush, id, nullOut ? nullptr : &out);
            Note("QueryInterface %08x null=%d -> %d out=%s count %u", id, int(nullOut), int(answer),
                 out == brush ? "self" : (out == reinterpret_cast<void *>(uintptr_t(0x1234)) ? "untouched" : "other"),
                 Word(brush, 4));
            if (answer && !nullOut) ++held;
            break;
        }
        case 1:
            Note("AddRef -> %u", RawSlot<1>(brush));
            ++held;
            break;
        case 2:
            if (held > 1) {
                Note("Release -> %u", RawSlot<2>(brush));
                --held;
            }
            break;
        case 3: {
            Note("GetStateBlock -> %u", RawSlot<3>(brush, static_cast<void *>(nullptr), static_cast<void *>(nullptr)));
            break;
        }
        default: Note("count %u", Word(brush, 4)); break;
        }
    }

    void RunColor(uint32_t seed)
    {
        *GameAddress<uint32_t>(0xfeb9f4) = 0x10203040u * (seed + 1);
        void *const brush = NewColorBrush();
        Note("created count %u", Word(brush, 4));
        FakeDraw2D *const draw = MakeDraw();
        uint32_t held = 1;
        RawSlot<1>(brush);
        uint32_t size = 0;
        for (int op = 0; op < 80; ++op) {
            const uint32_t what = Next(12);
            if (what < 2) {
                CommonSlots(brush, kIidColorBrush, kIidBrush, held);
            } else if (what == 2) {
                const uint32_t count = Next(8) == 0 ? Next(1600) : Next(12);
                RawSlot<5, void>(brush, count);
                size = count;
                Note("Resize %u -> size %u", count, RawSlot<7>(brush));
            } else if (what == 3 && size > 0) {
                const uint32_t index = Next(size);
                uint32_t colour = random();
                RawSlot<6, void>(brush, index, &colour);
                uint32_t out = 0xdeadbeef;
                RawSlot<8, void>(brush, &out, index);
                Note("SetColor %u %08x -> %08x", index, colour, out);
            } else if (what == 4 && size > 0) {
                uint32_t out = 0xdeadbeef;
                const uint32_t index = Next(size);
                RawSlot<8, void>(brush, &out, index);
                Note("GetColor %u -> %08x", index, out);
            } else if (what == 5) {
                Note("Size -> %u", RawSlot<7>(brush));
            } else {
                const uint32_t n = Next(14);
                const std::vector<float> points = Points(n);
                std::vector<float> values;
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t kind = Next(6);
                    const float span = float(size + 3);
                    float t;
                    if (kind == 0) t = float(Next(size + 3));
                    else if (kind == 1) t = -float(Next(3000)) / 1000.0f;
                    else if (kind == 2) t = float(Next(100000)) / 100000.0f * span * 3.0f;
                    else t = float(Next(100000)) / 100000.0f * span;
                    values.push_back(t);
                }
                draw->lockResult = Next(8) != 0;
                Note("Draw kind %u n %u size %u", Next(2), n, size);
                RawSlot<4, void>(brush, static_cast<void *>(draw), Next(2), n, points.data(), values.data());
            }
        }
        while (held > 1) {
            Note("Release -> %u", RawSlot<2>(brush));
            --held;
        }
        FinalRelease(brush);
    }

    void RunOutline(uint32_t seed)
    {
        *GameAddress<uint32_t>(0xfeb9f4) = 0x55667788u + seed;
        void *const brush = NewOutlineBrush();
        Note("created count %u words %08x %08x", Word(brush, 4), Word(brush, 8), Word(brush, 0xc));
        FakeDraw2D *const draw = MakeDraw();
        uint32_t held = 1;
        RawSlot<1>(brush);
        for (int op = 0; op < 60; ++op) {
            const uint32_t what = Next(10);
            if (what < 2) {
                CommonSlots(brush, kIidOutlineColorBrush, kIidBrush, held);
            } else if (what == 2) {
                uint32_t colour = random();
                RawSlot<5, void>(brush, &colour);
                Note("SetOutline %08x words %08x %08x", colour, Word(brush, 8), Word(brush, 0xc));
            } else if (what == 3) {
                uint32_t colour = random();
                RawSlot<6, void>(brush, &colour);
                Note("SetFill %08x words %08x %08x", colour, Word(brush, 8), Word(brush, 0xc));
            } else if (what == 4) {
                uint32_t out = 0xdeadbeef;
                RawSlot<7, void>(brush, &out);
                uint32_t out2 = 0xdeadbeef;
                RawSlot<8, void>(brush, &out2);
                Note("Get %08x %08x", out, out2);
            } else {
                const uint32_t n = Next(10);
                const std::vector<float> points = Points(n);
                std::vector<float> values(n, 0.0f);
                const uint32_t kind = Next(5) == 0 ? 1 + Next(3) : 0;
                draw->lockResult = Next(8) != 0;
                Note("Draw kind %u n %u", kind, n);
                RawSlot<4, void>(brush, static_cast<void *>(draw), kind, n, points.data(), values.data());
            }
        }
        while (held > 1) {
            Note("Release -> %u", RawSlot<2>(brush));
            --held;
        }
        FinalRelease(brush);
    }

    // ---- the pattern brush ----
    void *NewPatternBrush()
    {
        if (route == kOriginal) return Orig<void *(__cdecl *)()>(0x153930)();
        return cPatternBrush::Create();
    }

    void DumpPattern(const void *brush)
    {
        const uint8_t *const begin = static_cast<const uint8_t *>(Pointer(brush, 8));
        const uint32_t size = Word(brush, 0xc) - Word(brush, 8);
        Note("count %u bitmap %s size %u capacity %u hash %08x format %s", Word(brush, 4), begin ? "set" : "null", size,
             Word(brush, 0x10) - Word(brush, 8), HashBytes(begin, size), Name(Pointer(brush, 0x14)));
        char line[300];
        int length = 0;
        for (uint32_t offset = 0x18; offset < 0x50; offset += 4) {
            if (offset == 0x30 || offset == 0x34) continue;
            length += std::snprintf(line + length, sizeof(line) - size_t(length), "%08x ", Word(brush, offset));
        }
        Note("words %s texture %s block %s", line, Name(Pointer(brush, 0x30)), Name(Pointer(brush, 0x34)));
    }

    void RandomScript()
    {
        gScript.uploadOk = Next(8) != 0;
        gScript.loadOk = Next(8) != 0;
        gScript.drawOk = Next(8) != 0;
        gScript.copyOk = Next(4) != 0;
        gScript.blockOk = Next(10) != 0;
        gScript.nullBegin = Next(5) == 0;
        gScript.nullEnd = Next(5) == 0;
        gScript.nullClone = Next(6) == 0;
        gScript.fitWidth = int32_t(1 + Next(512));
        gScript.fitHeight = int32_t(1 + Next(512));
        gScript.fitLevels = 1 + Next(4);
    }

    void SetImage(void *brush)
    {
        static uint8_t source[0x8000];
        for (uint8_t &byte : source) byte = uint8_t(random());
        const bool big = Next(4) == 0;
        const uint32_t height = Next(10) == 0 ? 0 : (big ? 40 + Next(80) : 1 + Next(16));
        const uint32_t pitch = Next(10) == 0 ? 0 : (big ? 100 + Next(200) : 1 + Next(32));
        const uint32_t width = Next(300);
        FakeFormat *const format = &gFormats[Next(2)];
        Note("SetImage %s %u %u %u", format->name, width, height, pitch);
        const uint32_t begin = Word(brush, 8);
        ReserveRecord();
        gDeletes->Begin();
        RawSlot<5, void>(brush, static_cast<const void *>(source), static_cast<void *>(format), width, height, pitch);
        gDeletes->End();
        NoteDeletes(brush, begin);
    }

    void RunPattern()
    {
        InitPatternWorld();
        void *const brush = NewPatternBrush();
        FakeDraw2D *const draw = MakeDraw();
        DumpPattern(brush);
        uint32_t held = 1;
        RawSlot<1>(brush);
        SetImage(brush);
        DumpPattern(brush);
        for (int op = 0; op < 70; ++op) {
            const uint32_t what = Next(14);
            if (what == 0) {
                const uint32_t ids[] = {1, 0x3c0ac4a4, 0xdc004962, 0, 2, 0x9c012573, 0xfc1d3c6b};
                const uint32_t id = ids[Next(7)];
                void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                const bool nullOut = Next(6) == 0;
                const bool answer = RawSlot<0, bool>(brush, id, nullOut ? nullptr : &out);
                Note("QueryInterface %08x null=%d -> %d out=%s", id, int(nullOut), int(answer),
                     out == brush ? "self" : (out == reinterpret_cast<void *>(uintptr_t(0x1234)) ? "untouched" : "other"));
                if (answer && !nullOut) ++held;
            } else if (what == 1) {
                const float angle = Next(3) == 0 ? 0.0f : float(int32_t(Next(40001)) - 20000) / 1000.0f;
                Note("SetAngle %08x", Bits(angle));
                RawSlot<7, void>(brush, angle);
            } else if (what == 2) {
                const float x = Next(4) == 0 ? float(int32_t(Next(2001)) - 1000) : float(int32_t(Next(2000001)) - 1000000) / 1000.0f;
                const float y = Next(4) == 0 ? float(int32_t(Next(2001)) - 1000) : float(int32_t(Next(2000001)) - 1000000) / 1000.0f;
                Note("SetOffset %08x %08x", Bits(x), Bits(y));
                RawSlot<8, void>(brush, x, y);
            } else if (what == 3) {
                Note("GetAngle -> %08x", Bits(RawSlot<9, float>(brush)));
                float out[2] = {FromBits(0xdeadbeef), FromBits(0xdeadbeef)};
                float *const answer = RawSlot<10, float *>(brush, static_cast<float *>(out));
                Note("GetOffset -> %s %08x %08x", answer == out ? "out" : "other", Bits(out[0]), Bits(out[1]));
            } else if (what == 4) {
                Note("ResetCache");
                RawSlot<6, void>(brush);
            } else if (what == 5) {
                SetImage(brush);
            } else if (what == 6) {
                if (Next(2) && gNextTexture > 0) gTextures[(gNextTexture - 1) % kTextures].created = 0;
                else if (gNextBlock > 0) gBlocks[(gNextBlock - 1) % kBlocks].created = 0;
                Note("lost a resource");
            } else if (what == 7) {
                Note("AddRef -> %u", RawSlot<1>(brush));
                ++held;
            } else if (what == 8 && held > 1) {
                Note("Release -> %u", RawSlot<2>(brush));
                --held;
            } else if (what < 12) {
                RandomScript();
                Note("GetStateBlock");
                const uint32_t block = RawSlot<3>(brush, static_cast<void *>(draw), static_cast<void *>(&gDevice));
                Note("GetStateBlock -> %s", Name(reinterpret_cast<void *>(uintptr_t(block))));
            } else {
                const uint32_t n = Next(10);
                const std::vector<float> points = Points(n);
                std::vector<float> values(n, 0.0f);
                const uint32_t kind = Next(2);
                draw->lockResult = Next(8) != 0;
                Note("Draw kind %u n %u", kind, n);
                RawSlot<4, void>(brush, static_cast<void *>(draw), kind, n, points.data(), values.data());
            }
            DumpPattern(brush);
        }
        while (held > 1) {
            Note("Release -> %u", RawSlot<2>(brush));
            --held;
        }
        FinalRelease(brush);
        // What the brush still holds: nothing, once it is gone.
        char line[200];
        int length = 0;
        for (const FakeTexture &texture : gTextures)
            if (texture.refs && length < 150) length += std::snprintf(line + length, sizeof(line) - size_t(length), "%s=%d ", texture.name, texture.refs);
        for (const FakeBlock &block : gBlocks)
            if (block.refs && length < 150) length += std::snprintf(line + length, sizeof(line) - size_t(length), "%s=%d ", block.name, block.refs);
        Note("left %s iterators %d %d %d %d", length ? line : "none", gIterators[0].refs, gIterators[1].refs, gIterators[2].refs,
             gIterators[3].refs);
    }
};

std::vector<std::string> RunRoute(difftest::Context &ctx, Route route, uint32_t seed, int which)
{
    std::vector<std::string> record;
    gRecord = &record;
    shape::DeleteRecorder deletes;
    gDeletes = &deletes;
    Sequence sequence;
    sequence.route = route;
    sequence.ctx = &ctx;
    gCtx = &ctx;
    sequence.random.seed(seed);
    if (which == 0) sequence.RunColor(seed);
    else if (which == 1) sequence.RunOutline(seed);
    else sequence.RunPattern();
    gRecord = nullptr;
    gDeletes = nullptr;
    return record;
}

void Compare(difftest::Context &ctx, int which, uint32_t seeds)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn manip{0x18d120, &nGZGraphic4::GetImageManipCached, &StandManip};
    shape::StandIn lowerBound{0x18ad50, &nGZGraphic4::FindFormatLowerBound, &StandLowerBound};
    shape::StandIn fit{0x18c990, &nGZSceneGraph::AdjustTextureSize, &StandFit};
    uint32_t *const defaultColour = GameAddress<uint32_t>(0xfeb9f4);
    const uint32_t savedColour = *defaultColour;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        ++ctx.cases;
        char mark[64];
        std::snprintf(mark, sizeof(mark), "seed %u original", unsigned(seed));
        ctx.Mark(mark);
        const std::vector<std::string> expected = RunRoute(ctx, kOriginal, seed, which);
        std::snprintf(mark, sizeof(mark), "seed %u ours", unsigned(seed));
        ctx.Mark(mark);
        const std::vector<std::string> actual = RunRoute(ctx, kOurs, seed, which);
        const size_t n = expected.size() < actual.size() ? expected.size() : actual.size();
        size_t i = 0;
        while (i < n && expected[i] == actual[i]) ++i;
        if (i == n && expected.size() == actual.size()) continue;
        ctx.Fail("seed %u line %u: original '%s' ours '%s' (previous '%s')", unsigned(seed), unsigned(i),
                 i < expected.size() ? expected[i].c_str() : "<end>", i < actual.size() ? actual[i].c_str() : "<end>",
                 i > 0 ? expected[i - 1].c_str() : "");
    }
    *defaultColour = savedColour;
}

} // namespace

DIFF_TEST(Brushes_Color, 0x153090) { Compare(ctx, 0, 300); }

DIFF_TEST(Brushes_Outline, 0x1533b0) { Compare(ctx, 1, 300); }

DIFF_TEST(Brushes_Pattern, 0x153770) { Compare(ctx, 2, 300); }
