// Differential tests for cGZDrawSystem and its buffer engine factory as whole classes (src/gz/cGZDrawSystem.h; lane NM,
// rendering R5).
//
// Each case builds one draw system per side: the original by the COM director's creator 0x8c0920 (odd seeds) or operator
// new and the constructor 0x8bfdb0 (even seeds; the game's vtables), ours by cGZDrawSystem::Create or the constructor's
// adapter (our vtables). Every slot is called through the object's own face vtables (+0 cIGZDrawSystem, +4
// cIGZSystemService, +8 cRZUnknownMT), so the original side runs the game's slot functions and ours our members. The
// world is drawfx fakes:
// - the framework (game global 0x10c9378) logs AddToTick / RemoveFromTick and answers GetSystemService for the draw system
//   with a draw system fake (the draw contexts CreateBuffer makes ask it for their engine: an engine fake);
// - the graphics system (cell 0x10f51a4): CreateBuffer hands out one of the buffer fakes (counted) or none, and answers
//   the step's bool;
// - eight buffers (cIGZBuffer) answer Init (the step's bool), the case's bounds, bytes per pixel, native colour type and
//   IsReady; the run changes bounds, IsReady, lock totals and cache answers between calls;
// - twelve transient buffers (cIGZTransientBuffer) answer GetBuffer (one of the buffers) and a lock total, and log Commit;
// - six hardware caches answer IsLoaded, GetLastUseTick and GetBuffer, and log Unload;
// - two engine factories (AddFactory) make an engine fake or not, by case; two CreateEngine targets, one answering the
//   cIGZBuffer query (so the system's own factory makes a real cGZDrawContextRenderToBuffer: the game's on the original side, ours on ours).
// A case is an AddRef and (mostly) Init, then 90 random calls: every slot of the three faces with random arguments,
// transient buffers registered in bulk (up to 48 entries, so Trim's sort sees more than 32 candidates, with ties), OnTick
// often (Trim every fifth tick), small budgets. CreateBuffer's drawable buffer (a cGZDrawContextAsBuffer from the pool
// page, over a new draw context) and CreateEngine's engine are released at once through their own vtables. After every
// call the log gets the result, the out-parameters and the object: the service fields, the factory list (fakes by id, the
// system's own factory by its count), the lock's vtable and count, and each vector's size, capacity and contents (Trim's
// weights bit for bit). At the end the count is released to zero (the deleting destructor) and the log gets the fakes'
// reference counts, the pool page and the side's heap balance (difftest::AllocationCounter).
#include "difftest.h"
#include "drawfx.h"

#include "gz/cGZDrawContextAsBuffer.h"
#include "gz/cGZDrawContextRenderToBuffer.h"
#include "gz/cGZDrawSystem.h"
#include "gz/cIGZSystemService.h"
#include "rz/RZFixedAllocator.h"
#include "rz/cRZCriticalSection.h"

#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>

namespace {

using drawfx::Ret;
constexpr drawfx::Slot S0{0, Ret::Int, true, "", ""};
constexpr drawfx::Slot S1{1, Ret::Int, true, "", ""};
constexpr drawfx::Slot S2{2, Ret::Int, true, "", ""};
constexpr drawfx::Slot S3{3, Ret::Int, true, "", ""};
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

// The engine the draw contexts get (as tests/gz_draw_context.cpp's).
inline constexpr drawfx::Slot kEngineSlots[] = {
    kQI, kAddRef, kRelease,
    {1, Ret::Int, true, "SetTarget", "i"},
    {0, Ret::Int, true, "Detach", ""},
    {1, Ret::Int, true, "GetBounds", "W0"}, // into a side's own draw context: its presence only
    {1, Ret::Int, true, "SetClip", "r"},
    {3, Ret::Int, true, "SetColors", "uuW4"},
    {1, Ret::Int, true, "vf8", "u"},
    {1, Ret::Int, true, "vf9", "b"},
    {1, Ret::Int, true, "vf10", "b"},
    {1, Ret::Int, true, "vf11", "b"},
    {1, Ret::Int, true, "SetLineWidth", "f"},
    {3, Ret::Int, true, "SetGradient", "F12ff"},
    {6, Ret::Int, true, "SetTexture", "iffffi"},
    {0, Ret::Int, true, "ClearTextureClip", ""},
    {1, Ret::Int, true, "SetTextureClip", "r"},
    {0, Ret::Int, true, "vf17", ""},
    {0, Ret::Int, true, "vf18", ""},
    {0, Ret::Int, true, "vf19", ""},
    {2, Ret::Int, true, "LockSurface", "uu"},
    {0, Ret::Int, true, "UnlockSurface", ""},
    {3, Ret::Int, true, "ChooseRoute", "u@2x2u"},
    {2, Ret::Int, true, "DrawSpans", "@1x2u"},
    S1, S0, S0, S0, S1, S1, S1, S2, S3, S2, S2,
};
inline constexpr drawfx::Iface kEngine{"cIGZDrawContextEngine", 35, kEngineSlots};

inline constexpr drawfx::Slot kSystemSlots[] = {
    // CreateEngine's out-pointer is into a side's own draw context (a heap block): only its presence is logged.
    kQI, kAddRef, kRelease, {1, Ret::Int, true, "AddFactory", "i"}, {2, Ret::Int, true, "CreateEngine", "iW0"},
};
inline constexpr drawfx::Iface kSystem{"cIGZDrawSystem", 5, kSystemSlots};

inline constexpr drawfx::Slot kFrameworkSlots[] = {
    kQI, kAddRef, kRelease, U, U, {3, Ret::Int, true, "GetSystemService", "uui"}, U, U, U,
    {1, Ret::Int, true, "AddToTick", "i"},
    {1, Ret::Int, true, "RemoveFromTick", "i"},
};
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 11, kFrameworkSlots};

inline constexpr drawfx::Slot kGraphicsSlots[] = {kQI, kAddRef, kRelease, {1, Ret::Int, true, "CreateBuffer", "i"}};
inline constexpr drawfx::Iface kGraphics{"cIGZGraphicSystem", 4, kGraphicsSlots};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease,
    {4, Ret::Int, true, "Init", "uuuu"},
    U, U, U, U, U, U, U, U,
    {0, Ret::Int, true, "GetBoundsPtr", ""},
    U, U,
    {1, Ret::Int, true, "GetNativeColorType", "i"}, // the hidden result pointer
    U,
    {0, Ret::Int, true, "GetBytesPerPixel", ""},
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, Ret::Int, true, "IsReady", ""},
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 40, kBufferSlots};

inline constexpr drawfx::Slot kTransientSlots[] = {
    kQI, kAddRef, kRelease, {0, Ret::Int, true, "GetBuffer", ""}, U, {0, Ret::Int, true, "Commit", ""}, U, U, U,
    {0, Ret::Int, true, "GetLockTotal", ""},
};
inline constexpr drawfx::Iface kTransient{"cIGZTransientBuffer", 10, kTransientSlots};

inline constexpr drawfx::Slot kCacheSlots[] = {
    kQI, kAddRef, kRelease, U, U, U,
    {0, Ret::Int, true, "Unload", ""},
    {0, Ret::Int, true, "GetBuffer", ""},
    U, U,
    {0, Ret::Int, true, "IsLoaded", ""},
    U,
    {0, Ret::Int, true, "GetLastUseTick", ""},
};
inline constexpr drawfx::Iface kCache{"cIGZBufferHardwareCache", 13, kCacheSlots};

inline constexpr drawfx::Slot kFactorySlots[] = {kQI, kAddRef, kRelease, {2, Ret::Int, true, "CreateEngine", "ii"}};
inline constexpr drawfx::Iface kFactory{"cIGZDrawContextEngineFactory", 4, kFactorySlots};

constexpr uint32_t kFrameworkCell = 0x10c9378, kGraphicsCell = 0x10f51a4;
constexpr uint32_t kCreator = 0x8c0920, kConstructor = 0x8bfdb0, kDestructor = 0x8bfe70;
constexpr uint32_t kEngineVTable = 0xe4f4e4; // cGZDrawContextRenderToBuffer's (the original side's engines)
constexpr uint32_t kGameVTables[3] = {0xe4eea8, 0xe4ef00, 0xe4ef38};
constexpr uint32_t kFaceOffsets[3] = {0, 4, 8};
constexpr uint32_t kObjectTag = 0x0c000000, kPageTag = 0x0e000000;

// ---- the case ----

constexpr unsigned kBuffers = 8, kTransients = 12, kCaches = 6;

struct Case {
    cGZRect bounds[kBuffers];
    uint32_t bytes[kBuffers];
    uint32_t ready[kBuffers];
    uint32_t initOk[kBuffers];
    uint32_t format[kBuffers], bits[kBuffers];
    uint32_t lockTotal[kTransients];
    uint32_t transientBuffer[kTransients];
    uint32_t cacheBuffer[kCaches];
    uint32_t loaded[kCaches];
    uint32_t lastUse[kCaches];
    uint32_t factoryMakes[2];
    // CreateBuffer's graphics system answer (set by the step before the call).
    uint32_t graphicsOk;
    int32_t graphicsBuffer; // the buffer it hands out, or -1
};
Case gCase;

drawfx::Fake gFramework, gDrawSystem, gEngine, gFactoryEngine, gGraphics;
drawfx::Fake gBuffers[kBuffers], gTransients[kTransients], gCaches[kCaches], gFactories[2], gTargets[2];

// Shapes the buffers' bounds come from (repeats make Trim's weights tie).
const cGZRect kShapes[] = {{0, 0, 16, 16}, {0, 0, 64, 32}, {-8, 4, 120, 70}, {10, 10, 10, 90}, {0, 0, 256, 256},
                           {5, 5, 4, 9}, {0, 0, 300, 200}, {-100, -100, 100, 100}};

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 17);
    for (unsigned i = 0; i < kBuffers; ++i) {
        gCase.bounds[i] = kShapes[rng() % 8];
        const uint32_t bytes[] = {1, 2, 3, 4, 4, 0};
        gCase.bytes[i] = bytes[rng() % 6];
        gCase.ready[i] = rng() % 5 ? 1 : 0;
        gCase.initOk[i] = rng() % 4 ? 1 : 0;
        gCase.format[i] = rng() % 12;
        gCase.bits[i] = rng() % 40;
    }
    for (unsigned i = 0; i < kTransients; ++i) {
        gCase.lockTotal[i] = rng() % 3;
        gCase.transientBuffer[i] = rng() % kBuffers;
    }
    for (unsigned i = 0; i < kCaches; ++i) {
        gCase.cacheBuffer[i] = rng() % kBuffers;
        gCase.loaded[i] = rng() % 3 ? 1 : 0;
        gCase.lastUse[i] = rng() % 30;
    }
    for (unsigned i = 0; i < 2; ++i) gCase.factoryMakes[i] = rng() % 2;
    gCase.graphicsOk = 0;
    gCase.graphicsBuffer = -1;
}

uint32_t FrameworkGetService(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] != cIGZDrawSystem::kServiceId || args[1] != cIGZDrawSystem::kIid) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &gDrawSystem;
    ++gDrawSystem.refs;
    return 1;
}
uint32_t SystemCreateEngine(drawfx::Fake &, const uint32_t *args)
{
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &gEngine;
    ++gEngine.refs;
    return 1;
}
uint32_t GraphicsCreateBuffer(drawfx::Fake &, const uint32_t *args)
{
    if (gCase.graphicsBuffer >= 0) {
        drawfx::Fake &b = gBuffers[gCase.graphicsBuffer];
        *reinterpret_cast<void **>(uintptr_t(args[0])) = &b;
        ++b.refs;
    }
    return gCase.graphicsOk;
}
uint32_t BufferNativeColorType(drawfx::Fake &self, const uint32_t *args)
{
    const cGZColorType type{gCase.format[self.value[0]], gCase.bits[self.value[0]]};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &type, sizeof(type));
    return args[0];
}
uint32_t FactoryCreateEngine(drawfx::Fake &self, const uint32_t *args)
{
    if (!gCase.factoryMakes[self.value[0]]) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &gFactoryEngine;
    ++gFactoryEngine.refs;
    return 1;
}

void BuildWorld()
{
    gFramework.Reset<kFramework>(501);
    gFramework.On(5, &FrameworkGetService);
    gFramework.Returns(9, 1u);
    gFramework.Returns(10, 1u);
    gDrawSystem.Reset<kSystem>(502);
    gDrawSystem.On(4, &SystemCreateEngine);
    gEngine.Reset<kEngine>(503);
    gFactoryEngine.Reset<kEngine>(504);
    gGraphics.Reset<kGraphics>(505);
    gGraphics.On(3, &GraphicsCreateBuffer);
    for (unsigned i = 0; i < kBuffers; ++i) {
        drawfx::Fake &b = gBuffers[i];
        b.Reset<kBuffer>(510 + i);
        b.value[0] = i;
        b.Returns(3, gCase.initOk[i]);
        b.Returns(12, &gCase.bounds[i]);
        b.On(15, &BufferNativeColorType);
        b.Returns(17, gCase.bytes[i]);
        b.Returns(39, gCase.ready[i]);
    }
    for (unsigned i = 0; i < kTransients; ++i) {
        drawfx::Fake &t = gTransients[i];
        t.Reset<kTransient>(530 + i);
        t.Returns(3, &gBuffers[gCase.transientBuffer[i]]);
        t.Returns(9, gCase.lockTotal[i]);
    }
    for (unsigned i = 0; i < kCaches; ++i) {
        drawfx::Fake &c = gCaches[i];
        c.Reset<kCache>(550 + i);
        c.Returns(7, &gBuffers[gCase.cacheBuffer[i]]);
        c.Returns(10, gCase.loaded[i]);
        c.Returns(12, gCase.lastUse[i]);
    }
    for (unsigned i = 0; i < 2; ++i) {
        gFactories[i].Reset<kFactory>(560 + i);
        gFactories[i].value[0] = i;
        gFactories[i].On(3, &FactoryCreateEngine);
        gTargets[i].Reset<kBuffer>(570 + i);
    }
    // The first target is a cIGZBuffer to the system's own factory (its query answers itself).
    gTargets[0].AddFace(cIGZBuffer::kIid, gTargets[0]);
}

// ---- the pool (CreateBuffer's cGZDrawContextAsBuffer) ----

constexpr uint32_t kPageSize = 0x2000;
constexpr uint32_t kBlocks = (kPageSize - 16) / cGZDrawContextAsBuffer::kSize;
uint8_t *gPage;

// As tests/gz_draw_context_as_buffer.cpp's: a page of seeded bytes with a few free blocks, the pool's current page, so
// neither the allocation nor the free reaches the pool's page bookkeeping (game code not under test).
void BuildPool(uint32_t seed)
{
    if (!gPage) gPage = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::mt19937 r(seed * 0x9e3779b1u + 5);
    for (uint32_t i = 0; i < kPageSize; ++i) gPage[i] = uint8_t(r());
    auto *page = reinterpret_cast<RZFixedPage *>(gPage);
    const uint32_t freeBlocks = 3 + r() % 5;
    uint8_t *first = nullptr;
    for (uint32_t k = 0; k < freeBlocks; ++k) {
        uint8_t *block = gPage + 16 + ((r() % kBlocks + k * 7) % kBlocks) * cGZDrawContextAsBuffer::kSize;
        bool seen = false;
        for (uint8_t *b = first; b; b = *reinterpret_cast<uint8_t **>(b)) seen |= b == block;
        if (seen) continue;
        *reinterpret_cast<uint8_t **>(block) = first;
        first = block;
    }
    page->freeList = first;
    page->used = kBlocks - freeBlocks;
    page->next = page->prev = page;
}

struct PoolScope {
    drawfx::Cells cells;
    PoolScope()
    {
        cells.Set(cGZDrawContextAsBuffer::kPool, 0u);
        cells.Set(cGZDrawContextAsBuffer::kPool + 4, 0u);
        cells.Set(cGZDrawContextAsBuffer::kPool + 8, 0u);
    }
};

void NotePool()
{
    const auto *page = reinterpret_cast<const RZFixedPage *>(gPage);
    const uint32_t *pool = GameAddress<uint32_t>(cGZDrawContextAsBuffer::kPool);
    drawfx::Note("pool", {drawfx::Norm(pool[1]), drawfx::Norm(pool[2]), drawfx::Norm(drawfx::P(page->next)),
                          drawfx::Norm(drawfx::P(page->prev)), drawfx::Norm(drawfx::P(page->freeList)), page->used});
}

// ---- calls through the object's own faces ----

template <class R, class... A> R CallFace(void *object, uint32_t face, unsigned slot, A... args)
{
    void *const self = static_cast<uint8_t *>(object) + face;
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(self))[slot])(self, args...);
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, 4);
    return v;
}

// A pointer the log can compare: a fake's id, an address in a registered range, else (a side's own heap object: the
// system's factory, a real engine) 0xf0000000.
uint32_t Ref(uint32_t p)
{
    const uint32_t n = drawfx::Norm(p);
    return n == p && p > 0x10000 ? 0xf0000000u : n;
}

// A std::vector at `offset`: size and capacity in elements, then its words (pointers as Ref).
void NoteVector(drawfx::Call &c, const void *object, uint32_t offset, uint32_t elementWords, bool contents)
{
    const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(Word(object, offset)));
    const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(Word(object, offset + 4)));
    const auto *end = reinterpret_cast<const uint32_t *>(uintptr_t(Word(object, offset + 8)));
    c.words.push_back(uint32_t(last - first) / elementWords);
    c.words.push_back(uint32_t(end - first) / elementWords);
    if (!contents) return;
    for (const uint32_t *p = first; p != last; p += elementWords) {
        c.words.push_back(Ref(p[0]));
        for (uint32_t k = 1; k < elementWords; ++k) c.words.push_back(p[k]);
    }
}

// The object but its vtable words and its heap pointers (the list's head node, the lock's block, the vectors' arrays):
// the service fields, the factory list in order (the system's own factory by its count), the lock's vtable word and
// count, the caches, the watched transient buffers (buffer, lock total, tick, weight bits), Trim's list (empty between
// calls: size and capacity), the tick, +0x58, the budget and the flag byte.
void NoteObject(const void *object)
{
    difftest::NotCounted bookkeeping; // the log is the test's (difftest::AllocationCounter)
    drawfx::Call c;
    c.note = "object";
    // The service's id, priorities and running flag: data, as they are (lane RK; they went through the guess).
    for (uint32_t off = 0xc; off < 0x1c; off += 4) c.words.push_back(Word(object, off));
    c.words.push_back(Word(object, 0x1c) & 0xff);
    c.words.push_back(Word(object, 0x24));
    const auto *head = reinterpret_cast<const uint32_t *>(uintptr_t(Word(object, 0x20)));
    unsigned n = 0;
    for (auto *node = reinterpret_cast<const uint32_t *>(uintptr_t(head[0])); node != head && n < 64;
         node = reinterpret_cast<const uint32_t *>(uintptr_t(node[0])), ++n) {
        const uint32_t ref = Ref(node[2]);
        c.words.push_back(ref == 0xf0000000u ? ref | Word(reinterpret_cast<const void *>(uintptr_t(node[2])), 8) : ref);
    }
    const auto *lock = reinterpret_cast<const cRZCriticalSection *>(static_cast<const uint8_t *>(object) + 0x28);
    c.words.push_back(drawfx::P(lock->mVTable));
    c.words.push_back(uint32_t(lock->mBlock->lockCount));
    NoteVector(c, object, 0x30, 1, true);
    c.words.push_back(Word(object, 0x3c));
    NoteVector(c, object, 0x40, 4, true);
    NoteVector(c, object, 0x4c, 1, false);
    c.words.push_back(Word(object, 0x58));
    c.words.push_back(Word(object, 0x5c));
    c.words.push_back(Word(object, 0x60) & 0xff);
    drawfx::gLog.push_back(std::move(c));
}

void *OriginalCreate(bool director)
{
    if (director) return reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kCreator))();
    void *const block = ::operator new(cGZDrawSystem::kSize);
    using Ctor = void *(__thiscall *)(void *);
    return reinterpret_cast<Ctor>(GameAddress<void>(kConstructor))(block);
}
void *OurCreate(bool director)
{
    if (director) return static_cast<cIGZDrawSystem *>(cGZDrawSystem::Create());
    return static_cast<cIGZDrawSystem *>(cGZDrawSystemAdapters::Construct(::operator new(cGZDrawSystem::kSize), nullptr));
}

// The registered pointer arguments.
void *Transient(std::mt19937 &rng) { return &gTransients[rng() % kTransients]; }
void *Cache(std::mt19937 &rng) { return &gCaches[rng() % kCaches]; }

alignas(16) uint8_t gConvertFrom[1024], gConvertTo[1024];
uint32_t gConvertKey[256];

// One side: the object made by the side's creator, then the case's calls.
void RunSide(bool ours, uint32_t seed)
{
    drawfx::Cells cells;
    cells.Set(kFrameworkCell, &gFramework);
    cells.Set(kGraphicsCell, &gGraphics);
    // The log's growth is the test's own: an earlier test may leave its buffer at any capacity (drawfx::Side clears
    // it, keeping the capacity), and the side would count the log's reallocation or not by the suite's order.
    drawfx::gLog.reserve(1 << 14);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = ours ? OurCreate(seed % 2 != 0) : OriginalCreate(seed % 2 != 0);
    {
        difftest::NotCounted bookkeeping; // drawfx's range list keeps its capacity from the side before
        drawfx::AddRange(object, cGZDrawSystem::kSize, kObjectTag);
        drawfx::AddRange(gPage, kPageSize, kPageTag);
    }
    NoteObject(object);
    drawfx::Note("addref", CallFace<uint32_t>(object, 0, 1));
    if (rng() % 4) drawfx::Note("init", CallFace<uint32_t>(object, 4, 8) & 0xff);
    NoteObject(object);

    for (int step = 0; step < 90; ++step) {
        const uint32_t pick = rng() % 40;
        drawfx::Note("pick", pick);
        const uint32_t word = rng(), word2 = rng();
        uint32_t a = 0xdddd0001, b = 0xdddd0002, d = 0xdddd0003;
        if (pick < 6) {
            // Transient buffers in bulk (so Trim's candidates pass 32 now and then) or one.
            const uint32_t count = rng() % 3 ? 1 : 1 + rng() % 24;
            for (uint32_t i = 0; i < count; ++i) CallFace<void>(object, 0, 9, Transient(rng));
        } else if (pick < 12) {
            // OnTick: Trim(5) every fifth tick.
            drawfx::Note("tick", CallFace<uint32_t>(object, 4, 10, word) & 0xff);
        } else if (pick < 14) {
            // The world changes: a buffer's bounds, readiness or bytes, a lock total, a cache's answers.
            const uint32_t i = rng() % kBuffers;
            switch (rng() % 7) {
            case 0: gCase.bounds[i] = kShapes[rng() % 8]; break;
            case 1: gBuffers[i].Returns(39, rng() % 4 ? 1u : 0u); break;
            case 2: gBuffers[i].Returns(17, rng() % 5); break;
            case 3: gTransients[rng() % kTransients].Returns(9, rng() % 3); break;
            case 4: gCaches[rng() % kCaches].Returns(10, rng() % 2); break;
            case 5: {
                // Time passes: the tick (+0x3c) jumps as after 2^31 or 2^32 frames, so idle counts reach 2^31 and more
                // (their float conversion's range) and the tick wraps past the entries' and caches' ticks. The same
                // word on both sides.
                uint32_t &tick = *reinterpret_cast<uint32_t *>(static_cast<char *>(object) + 0x3c);
                tick = (rng() % 2 ? 0x7ffffff0u : 0xfffffff0u) + rng() % 0x20;
                break;
            }
            default: gCaches[rng() % kCaches].Returns(12, rng() % 40); break;
            }
        } else if (pick < 33) {
            // The cIGZDrawSystem face's slots 3..20 (pick 14..31) and its QueryInterface (pick 32).
            const unsigned slot = pick - 11;
            switch (slot) {
            case 3: {
                // Never null: CreateEngine calls every factory in the list without a check (as ours).
                void *const factories[] = {&gFactories[0], &gFactories[1]};
                CallFace<void>(object, 0, slot, factories[rng() % 2]);
                break;
            }
            case 4: {
                // A real engine (the system's own factory) is released at once through its vtable.
                void *engine = reinterpret_cast<void *>(uintptr_t(0xdddd0004));
                const uint32_t made = CallFace<uint32_t>(object, 0, slot, static_cast<void *>(&gTargets[rng() % 2]), &engine) & 0xff;
                const uint32_t ref = Ref(drawfx::P(engine));
                drawfx::Note("engine", {made, ref});
                if (made && ref == 0xf0000000u) {
                    // The engine is the side's cGZDrawContextRenderToBuffer: the game's vtable 0xe4f4e4 on the
                    // original side, our class's on ours (lane NS: the factory makes ours since R5b).
                    const cGZDrawContextRenderToBuffer probe;
                    const uint32_t expected =
                        ours ? Word(&probe, 0) : drawfx::P(GameAddress<void>(kEngineVTable));
                    drawfx::Note("engineIsRenderToBuffer", Word(engine, 0) == expected ? 1u : 0u);
                    drawfx::Note("engineRelease", CallFace<uint32_t>(engine, 0, 2));
                } else if (made) {
                    CallFace<uint32_t>(engine, 0, 2);
                }
                break;
            }
            case 5: {
                gCase.graphicsOk = rng() % 5 ? 1 : 0;
                // A true answer always comes with a buffer: the original Inits whatever it got, null included.
                gCase.graphicsBuffer = gCase.graphicsOk || rng() % 2 ? int32_t(rng() % kBuffers) : -1;
                const uint32_t formats[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x80000002u};
                void *out = reinterpret_cast<void *>(uintptr_t(0xdddd0005));
                const uint32_t made = CallFace<uint32_t>(object, 0, slot, &out, word % 300, word2 % 300, formats[rng() % 12]) & 0xff;
                drawfx::Note("buffer", {made, drawfx::Norm(drawfx::P(out))});
                // The drawable buffer (counted once) released through its cIGZBuffer face: its context goes too.
                if (made) drawfx::Note("bufferRelease", CallFace<uint32_t>(out, 0, 2));
                break;
            }
            case 6: {
                for (uint32_t i = 0; i < sizeof gConvertTo; ++i) gConvertTo[i] = uint8_t(i * 7);
                const uint32_t formats[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
                const int32_t width = rng() % 4 ? int32_t(rng() % 9) : 0, height = rng() % 4 ? int32_t(rng() % 9) : 0;
                const uint32_t answer =
                    CallFace<uint32_t>(object, 0, slot, gConvertTo, 64u, formats[rng() % 12], gConvertFrom, 64u,
                                       formats[rng() % 12], width, height, rng() % 2 ? gConvertKey : nullptr) & 0xff;
                uint32_t hash = 2166136261u;
                for (uint8_t x : gConvertTo) hash = (hash ^ x) * 16777619u;
                drawfx::Note("convert", {answer, hash});
                break;
            }
            case 7: {
                const uint32_t budgets[] = {0, 1, 1000, 30000, 100000, 1000000};
                CallFace<void>(object, 0, slot, rng() % 8 ? budgets[rng() % 6] : word);
                break;
            }
            case 9: CallFace<void>(object, 0, slot, Transient(rng)); break;
            case 10: CallFace<void>(object, 0, slot, Transient(rng)); break;
            case 11: CallFace<void>(object, 0, slot, Cache(rng)); break;
            case 12: CallFace<void>(object, 0, slot, Cache(rng)); break;
            case 13: CallFace<void>(object, 0, slot, rng() % 8 ? rng() % 9 : word); break;
            case 14: CallFace<void>(object, 0, slot); break;
            case 16:
                CallFace<void>(object, 0, slot, &a, &b);
                drawfx::Note("caches", {a, b});
                break;
            case 17:
                CallFace<void>(object, 0, slot, &a, &b, &d);
                drawfx::Note("transients", {a, b, d});
                break;
            case 19: CallFace<void>(object, 0, slot, word); break;
            case 18: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, word, word2) & 0xff); break;
            case 20: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot) & 0xff); break;
            case 21: {
                // QueryInterface on a face.
                const uint32_t iids[] = {1, cIGZDrawSystem::kIid, cIGZSystemService::kIID, 0x6b2ea76c, 0x12345678};
                const uint32_t face = kFaceOffsets[rng() % 3];
                void *out = reinterpret_cast<void *>(uintptr_t(0xdddd0006));
                const uint32_t answer = CallFace<uint32_t>(object, face, 0, iids[rng() % 5], &out) & 0xff;
                drawfx::Note("query", {answer, drawfx::Norm(drawfx::P(out))});
                if (answer) drawfx::Note("queryRelease", CallFace<uint32_t>(out, 0, 2));
                break;
            }
            default: drawfx::Note("result", CallFace<uint32_t>(object, 0, slot)); break; // 8 15
            }
        } else if (pick < 36) {
            // The cIGZSystemService face: the service fields, Init, Shutdown, OnIdle.
            const unsigned slots[] = {3, 4, 5, 6, 7, 8, 9, 11, 12};
            const unsigned slot = slots[rng() % 9];
            switch (slot) {
            case 4: CallFace<void>(object, 4, slot, rng() % 2 ? cIGZDrawSystem::kServiceId : word); break;
            case 7: CallFace<void>(object, 4, slot, word); break;
            case 11: drawfx::Note("idle", CallFace<uint32_t>(object, 4, slot, word) & 0xff); break;
            case 6: case 8: case 9: drawfx::Note("bool", CallFace<uint32_t>(object, 4, slot) & 0xff); break;
            default: drawfx::Note("result", CallFace<uint32_t>(object, 4, slot)); break;
            }
        } else if (pick < 38) {
            // AddRef on a face, or a Release that leaves a reference (the +8 face's are its slots 1 and 2; slot 5 the
            // count).
            const uint32_t face = kFaceOffsets[rng() % 3];
            if (rng() % 2 || Word(object, 0xc) < 2) drawfx::Note("addref", CallFace<uint32_t>(object, face, 1));
            else drawfx::Note("release", CallFace<uint32_t>(object, face, 2));
            drawfx::Note("count", CallFace<uint32_t>(object, 8, 5));
        } else {
            // The system's own factories in the list: QueryInterface, and AddRef / Release / RefCount on their count face.
            const auto *head = reinterpret_cast<const uint32_t *>(uintptr_t(Word(object, 0x20)));
            for (auto *node = reinterpret_cast<const uint32_t *>(uintptr_t(head[0])); node != head;
                 node = reinterpret_cast<const uint32_t *>(uintptr_t(node[0]))) {
                void *const factory = reinterpret_cast<void *>(uintptr_t(node[2]));
                if (Ref(node[2]) != 0xf0000000u) continue;
                const uint32_t iids[] = {1, 0x6b2ea76c, cIGZBuffer::kIid};
                void *out = reinterpret_cast<void *>(uintptr_t(0xdddd0007));
                const uint32_t iid = iids[rng() % 3];
                const uint32_t answer = CallFace<uint32_t>(factory, 0, 0, iid, &out) & 0xff;
                drawfx::Note("factoryQuery", {iid, answer, out == factory ? 1u : out == reinterpret_cast<void *>(uintptr_t(0xdddd0007)) ? 2u : 3u});
                drawfx::Note("factoryAddRef", CallFace<uint32_t>(factory, 4, 2));
                drawfx::Note("factoryRefCount", CallFace<uint32_t>(factory, 4, 4));
                drawfx::Note("factoryRelease", CallFace<uint32_t>(factory, rng() % 2 ? 0 : 4, rng() % 2 ? 2 : 1));
                if (answer) CallFace<uint32_t>(factory, 0, 2);
            }
        }
        NoteObject(object);
    }

    // The last references, one Release each (through each face in turn); the last runs the deleting destructor.
    const uint32_t count = Word(object, 0xc);
    drawfx::Note("count", count);
    for (uint32_t i = 0; i < count && i < 64; ++i) drawfx::Note("release", CallFace<uint32_t>(object, kFaceOffsets[i % 3], 2));
    heap.Stop();
    NotePool();
    {
        drawfx::Call c;
        c.note = "refs";
        for (const drawfx::Fake *f : {&gFramework, &gDrawSystem, &gEngine, &gFactoryEngine, &gGraphics, &gFactories[0],
                                      &gFactories[1], &gTargets[0], &gTargets[1]})
            c.words.push_back(uint32_t(f->refs));
        for (const drawfx::Fake &f : gBuffers) c.words.push_back(uint32_t(f.refs));
        for (const drawfx::Fake &f : gTransients) c.words.push_back(uint32_t(f.refs));
        for (const drawfx::Fake &f : gCaches) c.words.push_back(uint32_t(f.refs));
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

void RunCase(difftest::Context &ctx, uint32_t seed, bool &ok)
{
    NewCase(seed);
    for (uint32_t i = 0; i < sizeof gConvertFrom; ++i) gConvertFrom[i] = uint8_t(i * 13 + seed);
    for (uint32_t i = 0; i < 256; ++i) gConvertKey[i] = i * 0x01010101u ^ seed;
    ctx.Mark("orig");
    auto a = drawfx::Side(&BuildWorld, [&] {
        BuildPool(seed);
        *GameAddress<uint32_t>(cGZDrawContextAsBuffer::kPool + 4) = drawfx::P(gPage);
        RunSide(false, seed);
    });
    NewCase(seed);
    ctx.Mark("ours");
    auto b = drawfx::Side(&BuildWorld, [&] {
        BuildPool(seed);
        *GameAddress<uint32_t>(cGZDrawContextAsBuffer::kPool + 4) = drawfx::P(gPage);
        RunSide(true, seed);
    });
    ok = drawfx::Same(ctx, a.first, b.first, "draw system", seed, a.second);
}

} // namespace

DIFF_TEST(GZDrawSystem_Slots, 0x8c0470)
{
    // Every slot of the three faces (cIGZDrawSystem 0x8bffb0 0x8bff90 0x8bffa0 0x8c0080 0x8c00f0 0x8c0130 0x8c0280
    // 0x8c02c0 0x8c02d0 0x8c02e0 0x8c0380 0x8c03e0 0x8c0430 0x8c0470 0x8c06c0 0x8c0720 0x8c0730 0x8c07e0 0x8c08c0
    // 0x8c08d0 0x8c08e0; cIGZSystemService Init 0x8bffe0, Shutdown 0x8c0060, OnTick 0x8c08f0), the faces' thunks, the
    // director's creator 0x8c0920 (odd seeds) or the constructor 0x8bfdb0 (even seeds), the destructor 0x8bfe70 and the
    // deleting destructor 0x8c12f0 (the last Release), Trim's sort 0x8c0eb0 0x8c0c90 0x8c0e00; the factory cGZDrawContextRenderToBufferFactory (Init's): QueryInterface 0x8c0a20, AddRef 0x8c0a00,
    // Release 0x8c0a10, CreateEngine 0x8c0a50, its count face's thunks and the deleting destructor 0x8c12c0.
    // The adjustor thunks (the originals', unhooked; ours are our compiler's, in our vtables, called through the faces):
    // the system's +4 QueryInterface 0x8c1328, AddRef 0x8c1318, Release 0x8c1340; +8 QueryInterface 0x8c1330, AddRef
    // 0x8c1320, Release 0x8c1348 and the deleting destructor 0x8c12ad (slot 3: rz::cRZSystemService::Release deletes
    // through it, so the last Release runs it); the factory's +4 AddRef 0x8c1310 (slot 2), Release 0x8c1338 (slot 1) and
    // deleting destructor 0x8c12a5 (slot 0: rz::cRZRefCount::Release deletes through it, at the factory's last Release).
    // The destructor's list teardown 0x8c1260 (the factory list; ours is our std::list's) runs at the last Release, the
    // released factories compared. CreateBuffer's drawable comes from cGZDrawContextAsBuffer's pool allocator 0x8c1460
    // (the pool's free list and count are compared).
    PoolScope pool;
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 1500); ++seed) {
        bool ok = true;
        RunCase(ctx, seed, ok);
        if (!ok) break;
    }
}

DIFF_TEST(GZDrawSystem_Construct, 0x8bfdb0)
{
    // The constructor on a block of seeded bytes: every byte it writes and every byte it leaves, but the three vtable
    // words (the game's on the original side, ours on ours) and its two heap pointers (the list's head node +0x20, the
    // lock's block +0x2c: each side's own allocations, compared by content). Then the destructors (the original's
    // 0x8bfe70) free them.
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    alignas(8) uint8_t a[cGZDrawSystem::kSize], b[cGZDrawSystem::kSize];
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        difftest::AllocationCounter heap;
        void *const ra = original(a);
        void *const rb = cGZDrawSystemAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != static_cast<void *>(b))
            ctx.Fail("seed %u: the constructor returned another address", seed);
        for (unsigned f = 0; f < 3; ++f) {
            const uint32_t game = drawfx::P(GameAddress<void>(kGameVTables[f]));
            if (Word(a, kFaceOffsets[f]) != game) ctx.Fail("seed %u: the original's face +%#x is not the game's", seed, kFaceOffsets[f]);
            if (Word(b, kFaceOffsets[f]) == game) ctx.Fail("seed %u: our face +%#x has the game's vtable", seed, kFaceOffsets[f]);
        }
        const auto *headA = reinterpret_cast<const uint32_t *>(uintptr_t(Word(a, 0x20)));
        const auto *headB = reinterpret_cast<const uint32_t *>(uintptr_t(Word(b, 0x20)));
        if (headA[0] != Word(a, 0x20) || headA[1] != Word(a, 0x20) || headB[0] != Word(b, 0x20) || headB[1] != Word(b, 0x20))
            ctx.Fail("seed %u: the list's head node does not point at itself", seed);
        const auto *lockA = reinterpret_cast<const cRZCriticalSection *>(a + 0x28);
        const auto *lockB = reinterpret_cast<const cRZCriticalSection *>(b + 0x28);
        if (lockA->mBlock->lockCount != lockB->mBlock->lockCount) ctx.Fail("seed %u: the locks' counts differ", seed);
        for (uint32_t off = 0; off < sizeof(a); off += 4) {
            if (off < 0xc || off == 0x20 || off == 0x2c) continue;
            if (Word(a, off) != Word(b, off)) {
                ctx.Fail("seed %u: word +%#x orig %#x ours %#x", seed, off, Word(a, off), Word(b, off));
                break;
            }
        }
        ctx.Original<void(__thiscall *)(void *)>(kDestructor)(a);
        reinterpret_cast<cGZDrawSystem *>(b)->~cGZDrawSystem();
        heap.Stop();
        if (heap.Blocks() != 0) ctx.Fail("seed %u: the constructors and destructors leave %d blocks", seed, int(heap.Blocks()));
    }
}
