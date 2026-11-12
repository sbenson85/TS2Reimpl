// Differential tests for nTSSG::cImposterImageWriter (lane 261111q, src/sg/cImposterImageWriter.h): the constructor
// 0xb401c0, Write 0xb402a0, WriteTop 0xb40540 and SetCodecFormat 0xb40970.
//
// The writer is a 12-byte block the test lays out (file, codec, ready byte, three pad bytes kept at a pattern). Every
// object it reaches is a logging fake with its own vtable and a unique id: the image (cIImageData slots 9, 26, 27, 28,
// 32), two files (cIGZFile 1, 2, 3, 5, 28), two codecs (cIGZGimexWriter 1, 2, 4, 6, 7, 8) and the framework
// (GetClassObject, slot 3). One ordered log per side holds every call with its arguments (pointers as fake ids, the
// path by identity and text, the settings block by where it lives), the writer's words and every fake's count at each
// call, and, at the codec's Write, the whole 0x2a70-byte settings block (the stack is filled with a pattern before each
// call). The image's answers change with each call (format, sizes, pitch, the row pointer), so a value read once
// instead of each time shows; a fake can swap the writer's file or codec during any call (the members re-read), and
// the slots answer with junk above al.
//
// ImposterImageWriter_Construct: the framework (GetDirectorFrameworkObject 0x1ae18d stood in on both sides) gives a
// codec or none, with junk results; the file's allocation (the image's operator new 0x824632 and ours) fails or gives a
// block the stand-in cRZFile constructor (0x1c0d1d, both sides) makes a fake file in.
// ImposterImageWriter_SetCodecFormat: modes -3..9 and random words over a settings block pre-filled with a pattern,
// compared byte for byte.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "gz/Gimex.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFile.h"
#include "sg/cImageData.h"
#include "sg/cImposterImageWriter.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace imgwriter_test {

using nTSSG::cImposterImageWriter;
using Log = std::vector<uint32_t>;

Log gLog;
void L(uint32_t v)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back(v);
}

enum : uint32_t { kImage, kFile, kFile2, kCodec, kCodec2, kCom, kFakes };
constexpr uint32_t kIds[kFakes] = {0x1100, 0x1200, 0x1210, 0x1300, 0x1310, 0x1400};

struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
};
Fake gFakes[kFakes];
void *gImageVT[48], *gFileVT[40], *gCodecVT[16], *gComVT[8];

// The writer block the test lays out.
struct WriterBox {
    void *file;
    void *codec;
    uint8_t ready;
    uint8_t pad[3];
};
static_assert(sizeof(WriterBox) == sizeof(cImposterImageWriter));
WriterBox *gWriter;

const char gPath[] = "C:\\imposters\\lot_0001.jpg";
uint8_t gPixels[0x400];
alignas(8) uint8_t gFileBlock[0x9c];

bool IsStack(const void *p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= tib->StackLimit && p < tib->StackBase;
}

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (uint32_t i = 0; i < kFakes; ++i)
        if (p == &gFakes[i]) return kIds[i];
    if (p == gFileBlock) return 0x1500;
    if (p == gPath) return 0x9a7;
    if (p >= gPixels && p < gPixels + sizeof(gPixels)) return 0x9d000 + uint32_t(static_cast<const uint8_t *>(p) - gPixels);
    if (gWriter && p == &gWriter->codec) return 0xc0de4;
    if (IsStack(p)) return 0x57ac;
    return 0xbad00000;
}

// ---- the script ----
struct Script {
    int32_t formats[4];
    int32_t width, height, pitch;
    uint32_t fileOpen, fileClose, codecStream, codecOpen, codecWrite, codecClose;
    uint32_t swapAt, swapKind;
    // Construct
    bool giveCodec, failNew;
    uint32_t classResult;
};
Script gScript;
uint32_t gCall, gFormatCalls, gWidthCalls, gHeightCalls, gPitchCalls, gDataCalls;

// Logs the writer and every count, then swaps the writer's members when the script says this call is the one.
void Tick(uint32_t what)
{
    ++gCall;
    L(0xe7e70000 | what);
    if (gWriter) L(Id(gWriter->file)), L(Id(gWriter->codec)), L(gWriter->ready);
    for (const Fake &f : gFakes) L(uint32_t(f.refs));
    L(reinterpret_cast<const Fake *>(gFileBlock)->refs);
    if (gWriter && gCall == gScript.swapAt) {
        if (gScript.swapKind != 1) gWriter->codec = &gFakes[kCodec2];
        if (gScript.swapKind != 0) gWriter->file = &gFakes[kFile2];
        L(0x5a9);
    }
}

uint32_t Self(void *self) { return static_cast<Fake *>(self)->id; }

// ---- shared COM slots ----
uint32_t __fastcall AddRef(void *self, void *)
{
    Tick(1), L(Self(self));
    return uint32_t(++static_cast<Fake *>(self)->refs);
}
uint32_t __fastcall Release(void *self, void *)
{
    Tick(2), L(Self(self));
    return uint32_t(--static_cast<Fake *>(self)->refs);
}
void __fastcall BadSlot(void *self, void *)
{
    std::fprintf(stderr, "imposter writer test: an unexpected slot on fake %x\n", Self(self));
    std::abort();
}

// ---- the image ----
uint8_t *__fastcall ImageLevelData(void *self, void *, uint32_t level)
{
    Tick(9), L(Self(self)), L(level);
    return gPixels + 0x10 * (gDataCalls++ % 8) + level % 4;
}
int32_t __fastcall ImageWidth(void *self, void *, uint32_t level)
{
    Tick(26), L(Self(self)), L(level);
    return gScript.width + 5 * int32_t(gWidthCalls++);
}
int32_t __fastcall ImageHeight(void *self, void *, uint32_t level)
{
    Tick(27), L(Self(self)), L(level);
    return gScript.height + 7 * int32_t(gHeightCalls++);
}
int32_t __fastcall ImagePitch(void *self, void *, uint32_t level)
{
    Tick(28), L(Self(self)), L(level);
    return gScript.pitch + 3 * int32_t(gPitchCalls++);
}
int32_t __fastcall ImageFormat(void *self, void *)
{
    Tick(32), L(Self(self));
    return gScript.formats[gFormatCalls++ % 4];
}

// ---- the files ----
uint32_t __fastcall FileOpen(void *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    Tick(0x103), L(Self(self)), L(access), L(creation), L(sharing);
    return gScript.fileOpen;
}
uint32_t __fastcall FileClose(void *self, void *)
{
    Tick(0x105), L(Self(self));
    return gScript.fileClose;
}
void __fastcall FileSetPath(void *self, void *, const char *path)
{
    Tick(0x11c), L(Self(self)), L(Id(path));
    if (path) {
        uint32_t h = 2166136261u;
        for (const char *s = path; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
        L(h);
    }
}

// ---- the codecs ----
uint32_t __fastcall CodecStream(void *self, void *, void *file)
{
    Tick(0x204), L(Self(self)), L(Id(file));
    return gScript.codecStream;
}
uint32_t __fastcall CodecOpen(void *self, void *, uint32_t format, int32_t flag)
{
    Tick(0x206), L(Self(self)), L(format), L(uint32_t(flag));
    return gScript.codecOpen;
}
uint32_t __fastcall CodecWrite(void *self, void *, const gimex::Info *info, const void *data, int32_t pitch)
{
    Tick(0x207), L(Self(self)), L(Id(info)), L(Id(data)), L(uint32_t(pitch));
    if (info && IsStack(info)) {
        const auto *words = reinterpret_cast<const uint32_t *>(info);
        for (size_t i = 0; i < gimex::kWriteInfoSize / 4; ++i) L(words[i]);
    }
    return gScript.codecWrite;
}
uint32_t __fastcall CodecClose(void *self, void *)
{
    Tick(0x208), L(Self(self));
    return gScript.codecClose;
}

// ---- the framework, the file's construction and the allocation ----
uint32_t __fastcall ComGetClassObject(void *self, void *, uint32_t classId, uint32_t iid, void **out)
{
    Tick(0x303), L(Self(self)), L(classId), L(iid), L(Id(out));
    if (out) L(Id(*out));
    if (gScript.giveCodec) {
        *out = &gFakes[kCodec];
        ++gFakes[kCodec].refs;
    }
    return gScript.classResult;
}
void *__cdecl StandDirector()
{
    Tick(0x400);
    return &gFakes[kCom];
}
bool gArmed; // the next allocation is the writer's file
void *__cdecl StandNew(size_t size)
{
    if (!gArmed) return std::malloc(size);
    gArmed = false;
    Tick(0x401), L(uint32_t(size));
    return gScript.failNew ? nullptr : static_cast<void *>(gFileBlock);
}
void *__fastcall StandFileConstruct(void *self, void *)
{
    Tick(0x402), L(Id(self));
    auto *f = static_cast<Fake *>(self);
    f->vt = gFileVT;
    f->id = 0x1500;
    f->refs = 0;
    return self;
}

void Setup()
{
    for (void *&s : gImageVT) s = reinterpret_cast<void *>(&BadSlot);
    for (void *&s : gFileVT) s = reinterpret_cast<void *>(&BadSlot);
    for (void *&s : gCodecVT) s = reinterpret_cast<void *>(&BadSlot);
    for (void *&s : gComVT) s = reinterpret_cast<void *>(&BadSlot);
    for (void **vt : {gImageVT, gFileVT, gCodecVT, gComVT}) {
        vt[1] = reinterpret_cast<void *>(&AddRef);
        vt[2] = reinterpret_cast<void *>(&Release);
    }
    gImageVT[9] = reinterpret_cast<void *>(&ImageLevelData);
    gImageVT[26] = reinterpret_cast<void *>(&ImageWidth);
    gImageVT[27] = reinterpret_cast<void *>(&ImageHeight);
    gImageVT[28] = reinterpret_cast<void *>(&ImagePitch);
    gImageVT[32] = reinterpret_cast<void *>(&ImageFormat);
    gFileVT[3] = reinterpret_cast<void *>(&FileOpen);
    gFileVT[5] = reinterpret_cast<void *>(&FileClose);
    gFileVT[28] = reinterpret_cast<void *>(&FileSetPath);
    gCodecVT[4] = reinterpret_cast<void *>(&CodecStream);
    gCodecVT[6] = reinterpret_cast<void *>(&CodecOpen);
    gCodecVT[7] = reinterpret_cast<void *>(&CodecWrite);
    gCodecVT[8] = reinterpret_cast<void *>(&CodecClose);
    gComVT[3] = reinterpret_cast<void *>(&ComGetClassObject);
    void **const vts[kFakes] = {gImageVT, gFileVT, gFileVT, gCodecVT, gCodecVT, gComVT};
    for (uint32_t i = 0; i < kFakes; ++i) gFakes[i] = {vts[i], kIds[i], int32_t(i + 1)};
    gCall = gFormatCalls = gWidthCalls = gHeightCalls = gPitchCalls = gDataCalls = 0;
    std::memset(gFileBlock, 0x6b, sizeof(gFileBlock));
    gArmed = false;
}

// Fills the stack below the caller with a pattern, so a settings field left unset would show as the pattern.
__declspec(noinline) void Poison(uint8_t pattern)
{
    volatile uint8_t block[0x4000];
    for (size_t i = 0; i < sizeof(block); ++i) block[i] = uint8_t(pattern + i * 31);
}

// A result with junk above al: `ok` decides the low byte.
uint32_t Answer(std::mt19937 &r, bool ok) { return (r() & 0xffffff00u) | (ok ? 1u + (r() % 2) * 0x80u : 0u); }

void LogWriter(const WriterBox &w)
{
    L(0xe0d), L(Id(w.file)), L(Id(w.codec)), L(w.ready), L(w.pad[0]), L(w.pad[1]), L(w.pad[2]);
    for (const Fake &f : gFakes) L(uint32_t(f.refs));
    L(reinterpret_cast<const Fake *>(gFileBlock)->refs);
}

} // namespace imgwriter_test

using namespace imgwriter_test;

DIFF_TEST(ImposterImageWriter_Write, 0xb402a0)
{
    using WriteFn = uint32_t(__thiscall *)(void *, void *, uint32_t, const char *, uint32_t);
    using TopFn = uint32_t(__thiscall *)(void *, void *, const char *, uint32_t);
    gLog.reserve(1u << 22);
    int reached[4] = {}; // the codec wrote; a format 1..5 written; a swap fired; WriteTop ran
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 7919u + 3u);
            Setup();
            Script &s = gScript;
            const int32_t format = int32_t(r() % 13) - 1; // -1..11
            for (int32_t &f : s.formats) f = r() % 4 ? format : int32_t(r() % 13) - 1;
            if (seed % 16 == 0) s.formats[0] = s.formats[1] = s.formats[2] = int32_t(r());
            s.width = int32_t(r() % 600) - 20;
            s.height = int32_t(r() % 600) - 20;
            s.pitch = int32_t(r() % 2400);
            s.fileOpen = Answer(r, r() % 6 != 0);
            s.fileClose = Answer(r, r() % 4 != 0);
            s.codecStream = Answer(r, r() % 2 != 0);
            s.codecOpen = Answer(r, r() % 6 != 0);
            s.codecWrite = Answer(r, r() % 4 != 0);
            s.codecClose = Answer(r, r() % 4 != 0);
            s.swapAt = r() % 3 == 0 ? 1 + r() % 26 : 0;
            s.swapKind = r() % 3;
            WriterBox w{&gFakes[kFile], &gFakes[kCodec], uint8_t(r() % 8 != 0), {0xa5, 0x5a, 0xc3}};
            const char *path = r() % 10 ? gPath : nullptr;
            const uint32_t level = r() % 3 == 0 ? r() : r() % 4;
            const uint32_t codecFormat = r() % 4 == 0 ? r() : r() % 8;
            const bool top = r() % 4 == 0;
            const uint8_t pattern = uint8_t(r());
            gWriter = &w;
            gLog.clear();
            uint32_t result;
            Poison(pattern);
            if (side == 0) {
                result = top ? ctx.Original<TopFn>(0xb40540)(&w, &gFakes[kImage], path, codecFormat)
                             : ctx.Original<WriteFn>(0xb402a0)(&w, &gFakes[kImage], level, path, codecFormat);
            } else {
                auto *ours = reinterpret_cast<cImposterImageWriter *>(&w);
                auto *image = reinterpret_cast<nGZSceneGraph::cIImageData *>(&gFakes[kImage]);
                result = top ? ours->WriteTop(image, path, codecFormat) : ours->Write(image, level, path, codecFormat);
            }
            gWriter = nullptr;
            L(0x7e5), L(result & 0xff);
            LogWriter(w);
            logs[side] = gLog;
            if (side == 0) {
                for (size_t i = 0; i < gLog.size(); ++i)
                    if (gLog[i] == (0xe7e70000u | 0x207)) {
                        ++reached[0];
                        if (format >= 1 && format <= 5) ++reached[1];
                    }
                for (uint32_t v : gLog)
                    if (v == 0x5a9) ++reached[2];
                if (top) ++reached[3];
            }
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t k = 0;
            while (k < logs[0].size() && k < logs[1].size() && logs[0][k] == logs[1][k]) ++k;
            ctx.Fail("seed %u: the logs differ at word %zu of %zu / %zu (original %08x, ours %08x)", seed, k,
                     logs[0].size(), logs[1].size(), k < logs[0].size() ? logs[0][k] : 0u,
                     k < logs[1].size() ? logs[1][k] : 0u);
            if (ctx.failures.size() >= 6) return;
        }
    }
    if (reached[0] < 100 || reached[1] < 60 || reached[2] < 40 || reached[3] < 60)
        ctx.Fail("coverage: codec writes %d, formats 1..5 written %d, swaps %d, WriteTop %d", reached[0], reached[1],
                 reached[2], reached[3]);
}

DIFF_TEST(ImposterImageWriter_Construct, 0xb401c0)
{
    using CtorFn = void *(__thiscall *)(void *);
    gLog.reserve(1u << 22);
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn file{0x1c0d1d, static_cast<cRZFile *(cRZFile::*)()>(&cRZFile::Construct), &StandFileConstruct};
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&StandNew)};
    void *const ourNew = reinterpret_cast<void *>(static_cast<void *(__cdecl *)(size_t)>(&::operator new));
    shape::CodePatch ourNewPatch{ourNew, shape::CodeOf(&StandNew)};
    int ready = 0, failed = 0, noCodec = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 104729u + 11u);
            Setup();
            gScript = {};
            gScript.giveCodec = r() % 4 != 0;
            gScript.failNew = r() % 4 == 0;
            gScript.classResult = r() % 3 == 0 ? 0 : r();
            WriterBox w;
            std::memset(&w, int(0x80 | (r() & 0x7f)), sizeof(w));
            gWriter = &w;
            gLog.clear();
            Poison(uint8_t(r()));
            void *result;
            gArmed = true;
            if (side == 0)
                result = ctx.Original<CtorFn>(0xb401c0)(&w);
            else
                result = reinterpret_cast<cImposterImageWriter *>(&w)->Construct();
            gArmed = false;
            gWriter = nullptr;
            L(0x7e5), L(result == &w);
            LogWriter(w);
            logs[side] = gLog;
            if (side == 0) {
                ready += w.ready;
                failed += gScript.failNew;
                noCodec += !gScript.giveCodec;
            }
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t k = 0;
            while (k < logs[0].size() && k < logs[1].size() && logs[0][k] == logs[1][k]) ++k;
            ctx.Fail("seed %u: the logs differ at word %zu of %zu / %zu (original %08x, ours %08x)", seed, k,
                     logs[0].size(), logs[1].size(), k < logs[0].size() ? logs[0][k] : 0u,
                     k < logs[1].size() ? logs[1][k] : 0u);
            if (ctx.failures.size() >= 6) return;
        }
    }
    if (ready < 40 || failed < 20 || noCodec < 20)
        ctx.Fail("coverage: ready %d, failed allocations %d, no codec %d", ready, failed, noCodec);
}

DIFF_TEST(ImposterImageWriter_SetCodecFormat, 0xb40970)
{
    using Fn = void(__cdecl *)(int32_t, gimex::Info *);
    std::vector<uint8_t> blocks[2];
    for (uint32_t seed = 0; seed < 64; ++seed) {
        std::mt19937 r(seed * 31u + 5u);
        const int32_t mode = seed < 13 ? int32_t(seed) - 3 : int32_t(r());
        const uint8_t pattern = uint8_t(r());
        for (int side = 0; side < 2; ++side) {
            blocks[side].assign(gimex::kWriteInfoSize, 0);
            for (size_t i = 0; i < blocks[side].size(); ++i) blocks[side][i] = uint8_t(pattern + i * 7);
            auto *info = reinterpret_cast<gimex::Info *>(blocks[side].data());
            if (side == 0)
                ctx.Original<Fn>(0xb40970)(mode, info);
            else
                cImposterImageWriter::SetCodecFormat(mode, info);
        }
        ++ctx.cases;
        if (blocks[0] != blocks[1]) {
            size_t k = 0;
            while (blocks[0][k] == blocks[1][k]) ++k;
            ctx.Fail("mode %d: the blocks differ at +0x%zx (original %02x, ours %02x)", mode, k, blocks[0][k],
                     blocks[1][k]);
        }
    }
}
