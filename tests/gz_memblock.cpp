// Differential tests for the memory surfaces and textures as whole classes (lane RN, milestone O row O-c2):
// src/gz/cMemBlock.h (nRZGraphic4::cMemBlockAsSurface / cMemBlockAsTexture and their Direct3D sides).
//
// Each seed builds a world of two surfaces (one by the full constructor, one empty and Init-ed later) and one texture
// (SetTopLevel, then up to four AddLevel), then runs a seeded sequence of calls: every slot of both faces through the
// object's own vtable (RawSlot), every slot of both Direct3D sides through the interface the face's QueryInterface hands
// out, the levels' faces through the texture's GetSurface, and the members (Init, SetTopLevel, AddLevel) as the game
// calls them. Three routes, compared against the first:
//   0: the original constructors, members, destructors and slots (the game's code throughout);
//   1: ours: our adapters (the hook entries) and members, every call through our vtables (the slots test);
//   2: the adapters route: objects from the original constructors (the game's vtables), our Init / SetTopLevel /
//      AddLevel / the level getter (0x18c700, also called directly) / Destruct on them (what runs when the game made the object before our hooks were in, or an old entry
//      reaches ours). The members' own virtual calls (ReleaseResource, as the originals) go to the game's slots; before
//      our destructor adapter runs, the object's deleting destructor (slot 29, which in game reaches the hooked
//      destructor) is a logging trap, so an adapter that dispatched virtually shows up.
// The RVAs this file verifies: constructors 0x18d1a0 0x18d1e0 0x18c300 (and 0x18d200 0x18c320 inlined into ours),
// destructors 0x18d2c0 0x18c350 (deleting 0x18d310 0x18c3a0; the Impls' 0x18d300 0x18d330 0x18c390 0x18c3c0), Init
// 0x18d500, SetTopLevel 0x18c5b0, AddLevel 0x18c3e0, the faces' slots (0x18d880 0x18c720 QI, 0x178b40 Release,
// 0x18c7d0, 0x18c6b0, 0x18d900 0x18d980 0x18d4f0 0x18d350, 0x18d580 0x18d710 0x18d910 0x18d870, 0x18c710 0x18ce50
// 0x18c580 0x18c4e0 0x18c700, Create 0x18c5a0, and the constant slots), the Impls' slots (0x18c4c0, 0x18d8e0 0x18c7b0, 0x18d480 0x18c4f0,
// 0x18d360 0x18d430 0x18d7f0 0x18d960, 0x18c570 0x18c510 0x18c520 0x18c540 0x18c6d0 0x18c970, and the constant slots).
//
// The world (the same for every route):
// - Four pixel formats (fakes with logging slots 0-3, 6, 7, 9): two block-compressed (answering the DXT interface,
//   whose slot 16 gives the block bytes), one plain (QueryInterface writes null, answers false) and one whose
//   QueryInterface writes the DXT face but answers false (the face is released anyway).
// - The framework (game global 0x10c9378) hands out a graphic system whose slot 29 is the null device; the null device
//   and a container stand-in (poked into a surface's +0x10 for one call) answer QueryInterface with a seeded result.
// - The pixels are one static block, so the bits compare as they are; the game's operator new / sized delete
//   (0x824632 / 0x824624) are ours on both routes.
// Compared: one record per route: every call on every fake with its arguments, each call's result and out-parameters,
// and after every call every live object's words (face, Impl, the level list's size, capacity and elements, each level's
// face and Impl) and both lock records. Heap pointers are named by their place in a walk of the live objects (the
// sides allocate alike but not at the same addresses); vtable words are left out (each route has its own).
// Not observable: the lock flags the faces pass to their Impl's LockRect (no Impl reads them).
// Uninitialised stack the original reads: the faces' LockRect empty case and Unlock copy a 16-byte local nobody set
// into the lock record's rectangle; ours writes zeros, and the stack below the call is zeroed first (ZeroStackBelow).
#include "code_patch.h"
#include "difftest.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/PixelFormat.h"
#include "gz/cMemBlock.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nRZGraphic4;

namespace {

namespace g4 = nGZGraphic4;

std::vector<std::string> *gRecord;

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

// ---- engine-side fakes (thiscall slots as __fastcall with the dummy edx) ----
struct FakeObject {
    void *const *vtable;
    const char *name;
};
uint32_t __fastcall FakeAddRef(FakeObject *self, void *)
{
    Note("%s.AddRef", self->name);
    return 2;
}
uint32_t __fastcall FakeRelease(FakeObject *self, void *)
{
    Note("%s.Release", self->name);
    return 1;
}

struct FakeDxt {
    void *const *vtable;
    const char *name;
    uint32_t blockBytes;
};
uint32_t __fastcall DxtBlockBytes(FakeDxt *self, void *)
{
    Note("%s.GetBlockBytes", self->name);
    return self->blockBytes;
}
void *const kDxtVtable[17] = {nullptr, FN(FakeAddRef), FN(FakeRelease), nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, FN(DxtBlockBytes)};

struct FakeFormat {
    void *const *vtable;
    const char *name;
    uint32_t id, bitsPerPixel, blockHeight;
    int dxt; // 0: no (null written), 1: yes, 2: the face written, false answered
    FakeDxt face;
};
bool __fastcall FormatQueryInterface(FakeFormat *self, void *, uint32_t riid, void **out)
{
    Note("%s.QueryInterface %08x", self->name, riid);
    if (self->dxt == 0) {
        *out = nullptr;
        return false;
    }
    *out = &self->face;
    return self->dxt == 1;
}
uint32_t __fastcall FormatId(FakeFormat *self, void *)
{
    Note("%s.GetFormatId", self->name);
    return self->id;
}
uint32_t __fastcall FormatBitsPerPixel(FakeFormat *self, void *)
{
    Note("%s.GetBitsPerPixel", self->name);
    return self->bitsPerPixel;
}
uint32_t __fastcall FormatBlockHeight(FakeFormat *self, void *)
{
    Note("%s.GetBlockHeight", self->name);
    return self->blockHeight;
}
uint32_t __fastcall FormatRowBytes(FakeFormat *self, void *, uint32_t width)
{
    Note("%s.GetRowBytes %u", self->name, width);
    return self->blockHeight == 4 ? (width + 3) / 4 * self->face.blockBytes : (width * self->bitsPerPixel + 7) / 8;
}
void *const kFormatVtable[10] = {FN(FormatQueryInterface), FN(FakeAddRef),     FN(FakeRelease),
                                 FN(FormatId),             nullptr,            nullptr,
                                 FN(FormatBitsPerPixel),   FN(FormatBlockHeight), nullptr,
                                 FN(FormatRowBytes)};

FakeFormat gFormats[6] = {
    {kFormatVtable, "argb", 21, 32, 1, 0, {kDxtVtable, "argb.dxt", 0}},
    {kFormatVtable, "dxt1", 0x31545844, 4, 4, 1, {kDxtVtable, "dxt1.dxt", 8}},
    {kFormatVtable, "r5g6b5", 23, 16, 1, 2, {kDxtVtable, "r5g6b5.dxt", 4}},
    {kFormatVtable, "dxt5", 0x35545844, 8, 4, 1, {kDxtVtable, "dxt5.dxt", 16}},
    // Bits per pixel that are not whole bytes: the byte offset of an odd column rounds down after the multiply.
    {kFormatVtable, "l4", 50, 4, 1, 0, {kDxtVtable, "l4.dxt", 0}},
    {kFormatVtable, "rgb12", 51, 12, 1, 2, {kDxtVtable, "rgb12.dxt", 2}},
};

// ---- Direct3D-side fakes (COM, __stdcall) ----
struct FakeCom {
    void *const *vtable;
    const char *name;
};
void *gQueryAnswer;
HRESULT gQueryResult;
HRESULT __stdcall ComQueryInterface(FakeCom *self, const GUID *riid, void **out)
{
    Note("%s.QueryInterface %08x %p", self->name, riid ? unsigned(riid->Data1) : 0u, static_cast<void *>(out));
    *out = gQueryAnswer;
    return gQueryResult;
}
ULONG __stdcall ComAddRef(FakeCom *self)
{
    Note("%s.AddRef", self->name);
    return 2;
}
ULONG __stdcall ComRelease(FakeCom *self)
{
    Note("%s.Release", self->name);
    return 1;
}
void *const kComVtable[3] = {FN(ComQueryInterface), FN(ComAddRef), FN(ComRelease)};
FakeCom gNullDevice{kComVtable, "nullDevice"};
FakeCom gContainer{kComVtable, "container"};

// ---- the framework and the graphic system ----
void *__fastcall GraphicsNullDevice(FakeObject *self, void *)
{
    Note("%s.GetNullDevice", self->name);
    return &gNullDevice;
}
void *const kGraphicsVtable[30] = {nullptr, FN(FakeAddRef), FN(FakeRelease), nullptr, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, FN(GraphicsNullDevice)};
FakeObject gGraphics{kGraphicsVtable, "graphics"};
bool __fastcall FrameworkSystemService(FakeObject *self, void *, uint32_t service, uint32_t iid, void **out)
{
    Note("%s.GetSystemService %08x %08x", self->name, service, iid);
    *out = &gGraphics;
    return true;
}
void *const kFrameworkVtable[6] = {nullptr, FN(FakeAddRef), FN(FakeRelease), nullptr, nullptr, FN(FrameworkSystemService)};
FakeObject gFramework{kFrameworkVtable, "framework"};

// The adapters route's destructor check: a destructor adapter that dispatched virtually would land here.
void *gTrapVtable[30];
void *__fastcall TrapDeletingDestructor(void *self, void *, uint32_t flags)
{
    Note("trap: deleting destructor %u", flags);
    return self;
}

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

alignas(16) uint8_t gPixels[0x8000];
const GUID kSomeGuid = {0x11223344, 0x5566, 0x7788, {1, 2, 3, 4, 5, 6, 7, 8}};
void *gContainerOut; // GetContainer's out (static: the fakes log its address)

__declspec(noinline, safebuffers) void ZeroStackBelow()
{
    volatile uint8_t pad[4096];
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = 0;
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(object) + offset, 4);
    return w;
}
void *Ptr(const void *object, uint32_t offset) { return reinterpret_cast<void *>(uintptr_t(Word(object, offset))); }

// ---- the routes ----
enum Route { kOriginal, kOurs, kAdapters };

struct Root {
    void *object;
    bool texture;
};

struct World {
    Route route;
    std::mt19937 random;
    std::vector<Root> roots;
    g4::cSurfaceLock locks[2] = {};

    uint32_t Next(uint32_t n) { return static_cast<uint32_t>(random() % n); }
    bool Originals() const { return route == kOriginal; }       // members and destructors
    bool OriginalCtors() const { return route != kOurs; }       // constructors

    // Heap pointers named by their place in a walk of what is live.
    std::vector<const void *> known;
    void Add(const void *p)
    {
        if (!p) return;
        for (const void *k : known)
            if (k == p) return;
        known.push_back(p);
    }
    void AddFace(const void *face)
    {
        Add(face);
        if (face) Add(Ptr(face, 8));
    }
    void Walk()
    {
        known.clear();
        for (const Root &root : roots) {
            AddFace(root.object);
            if (!root.texture) continue;
            void *const impl = Ptr(root.object, 8);
            if (!impl) continue;
            void *const *first = static_cast<void *const *>(Ptr(impl, 8));
            void *const *last = static_cast<void *const *>(Ptr(impl, 0xc));
            Add(first);
            for (void *const *e = first; e != last; ++e) AddFace(*e);
        }
        for (const g4::cSurfaceLock &lock : locks) AddFace(lock.surface);
    }
    std::string Name(uint32_t word)
    {
        Walk();
        char buffer[16];
        for (size_t i = 0; i < known.size(); ++i)
            if (uintptr_t(known[i]) == word) {
                std::snprintf(buffer, sizeof(buffer), "#%u", unsigned(i));
                return buffer;
            }
        std::snprintf(buffer, sizeof(buffer), "%08x", word);
        return buffer;
    }
    std::string Name(const void *p) { return Name(uint32_t(uintptr_t(p))); }

    std::string Words(const void *object, uint32_t from, uint32_t to)
    {
        std::string s;
        for (uint32_t offset = from; offset < to; offset += 4) s += " " + Name(Word(object, offset));
        return s;
    }
    void Dump()
    {
        Walk();
        for (size_t i = 0; i < roots.size(); ++i) {
            const Root &root = roots[i];
            void *const impl = Ptr(root.object, 8);
            std::string s = std::string(root.texture ? "T" : "S") + " refs " + std::to_string(Word(root.object, 4)) +
                            " impl " + Name(impl);
            if (impl && !root.texture) s += " |" + Words(impl, 4, 0x2c);
            if (impl && root.texture) {
                const uint32_t first = Word(impl, 8), last = Word(impl, 0xc), end = Word(impl, 0x10);
                s += " | refs " + std::to_string(Word(impl, 4)) + " list " + Name(first) + " size " +
                     std::to_string((last - first) / 4) + " capacity " + std::to_string((end - first) / 4);
                for (uint32_t e = first; e != last; e += 4) {
                    const void *const level = reinterpret_cast<const void *>(uintptr_t(Word(reinterpret_cast<void *>(uintptr_t(e)), 0)));
                    s += " [" + Name(level) + Words(level, 4, 12) + " |" + Words(Ptr(level, 8), 4, 0x2c) + "]";
                }
            }
            Note("%s", s.c_str());
        }
        for (const g4::cSurfaceLock &lock : locks) Note("lock%s", Words(&lock, 0, sizeof(lock)).c_str());
    }

    // ---- construction and the members ----
    void *NewSurface(bool full, void *bits, void *format, uint32_t w, uint32_t h, uint32_t pitch)
    {
        void *const memory = ::operator new(12);
        if (OriginalCtors()) {
            if (full)
                ctxOriginal<void *(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t)>(0x18d1a0)(
                    memory, bits, format, w, h, pitch);
            else
                ctxOriginal<void *(__thiscall *)(void *)>(0x18d1e0)(memory);
        } else if (full) {
            cMemBlockAsSurface::Construct(memory, nullptr, bits, format, w, h, pitch);
        } else {
            cMemBlockAsSurface::ConstructEmpty(memory, nullptr);
        }
        return memory;
    }
    void *NewTexture()
    {
        void *const memory = ::operator new(12);
        if (OriginalCtors())
            ctxOriginal<void *(__thiscall *)(void *)>(0x18c300)(memory);
        else
            cMemBlockAsTexture::Construct(memory, nullptr);
        return memory;
    }
    void Init(void *surface, void *bits, void *format, uint32_t w, uint32_t h, uint32_t pitch)
    {
        if (Originals())
            ctxOriginal<void(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t)>(0x18d500)(
                surface, bits, format, w, h, pitch);
        else
            static_cast<cMemBlockAsSurface *>(surface)->Init(bits, format, w, h, pitch);
    }
    void SetTopLevel(void *texture, void *bits, void *format, uint32_t w, uint32_t h, uint32_t pitch)
    {
        if (Originals())
            ctxOriginal<void(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t)>(0x18c5b0)(
                texture, bits, format, w, h, pitch);
        else
            static_cast<cMemBlockAsTexture *>(texture)->SetTopLevel(bits, format, w, h, pitch);
    }
    void AddLevel(void *texture, void *bits, uint32_t w, uint32_t h, uint32_t pitch)
    {
        if (Originals())
            ctxOriginal<void(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t)>(0x18c3e0)(texture, bits, w, h,
                                                                                                    pitch);
        else
            static_cast<cMemBlockAsTexture *>(texture)->AddLevel(bits, w, h, pitch);
    }
    void Destroy(const Root &root)
    {
        if (route == kAdapters) { // the game's deleting destructor trapped: in game it would reach the hooked entry
            std::memcpy(gTrapVtable, *static_cast<void ***>(root.object), sizeof(gTrapVtable));
            gTrapVtable[29] = FN(TrapDeletingDestructor);
            *static_cast<void ***>(root.object) = gTrapVtable;
        }
        if (Originals())
            ctxOriginal<void(__thiscall *)(void *)>(root.texture ? 0x18c350 : 0x18d2c0)(root.object);
        else if (root.texture)
            cMemBlockAsTexture::Destruct(static_cast<cMemBlockAsTexture *>(root.object), nullptr);
        else
            cMemBlockAsSurface::Destruct(static_cast<cMemBlockAsSurface *>(root.object), nullptr);
        ::operator delete(root.object);
    }

    difftest::Context *ctx;
    template <class Fn> Fn ctxOriginal(uint32_t rva) { return ctx->Original<Fn>(rva); }

    // ---- seeded arguments ----
    struct Block {
        void *bits;
        FakeFormat *format;
        uint32_t w, h, pitch;
    };
    Block NewBlock(uint32_t maxSide)
    {
        Block b;
        b.format = &gFormats[Next(6)];
        b.w = 1 + Next(maxSide);
        b.h = 1 + Next(maxSide);
        b.pitch = (b.w * 4 + 3) / 4 * 4 + 4 * Next(4);
        b.bits = gPixels + 16 * Next(64);
        return b;
    }
    uint32_t SomeId()
    {
        static const uint32_t kIds[] = {1u,
                                        g4::kIidDeviceResource,
                                        g4::kIidAllocDeviceResource,
                                        g4::kIidSurface,
                                        g4::kIidLockableSurface,
                                        g4::kIidD3DSurface,
                                        g4::kIidD3DResource,
                                        g4::kIidTextureBase,
                                        g4::kIidTexture,
                                        g4::kIidD3DBaseTexture,
                                        g4::kIidD3DTexture,
                                        g4::kIidImageSurface,
                                        0x12345678u,
                                        0u};
        return kIds[Next(sizeof(kIds) / sizeof(kIds[0]))];
    }
    RECT SomeRect(uint32_t w, uint32_t h)
    {
        // a quarter of the corners on the far edges (one short, on, one past), where the bound tests turn
        const LONG left = Next(4) == 0 ? LONG(w) - 1 + LONG(Next(3)) : LONG(Next(w + 8)) - 4;
        const LONG top = Next(4) == 0 ? LONG(h) - 1 + LONG(Next(3)) : LONG(Next(h + 8)) - 4;
        return RECT{left, top, left + LONG(Next(w + 4)), top + LONG(Next(h + 4))};
    }
    void SeedQuery()
    {
        static const HRESULT kResults[] = {S_OK, E_NOINTERFACE, S_FALSE};
        gQueryResult = kResults[Next(3)];
        gQueryAnswer = Next(2) ? &gContainer : nullptr;
    }

    // ---- the faces' slots ----
    void Common(void *o, unsigned slot)
    {
        switch (slot) {
        case 0: {
            const uint32_t id = SomeId();
            const bool nullOut = Next(8) == 0;
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const bool b = RawSlot<0, bool>(o, id, nullOut ? nullptr : &out);
            Note("QueryInterface %08x %d -> %d %s", id, nullOut, b, Name(out).c_str());
            break;
        }
        case 1: Note("AddRef -> %u", RawSlot<1>(o)); break;
        case 2: { // never the count its creator (or the level list) and the lock records hold
            uint32_t held = 1;
            for (const g4::cSurfaceLock &lock : locks) held += lock.surface == o;
            if (Word(o, 4) > held) Note("Release -> %u", RawSlot<2>(o));
            break;
        }
        case 4: Note("GetDevice -> %s", Name(RawSlot<4, void *>(o)).c_str()); break;
        case 5: Note("GetDeviceConst -> %s", Name(RawSlot<5, void *>(o)).c_str()); break;
        case 6: Note("IsCreated -> %d", RawSlot<6, bool>(o)); break;
        case 7: Note("GetMemorySize -> %u", RawSlot<7>(o)); break;
        case 8: Note("SurvivesReset -> %d", RawSlot<8, bool>(o)); break;
        case 9: Note("SurvivesModeChange -> %d", RawSlot<9, bool>(o)); break;
        case 10: RawSlot<10, void>(o, Next(5)); Note("SetPriority"); break; // `ret 4`, eax as it was
        case 11: RawSlot<11, void>(o); Note("PreLoad"); break;
        case 12: Note("GetPool -> %u", RawSlot<12>(o)); break;
        case 13: Note("GetVideoMemorySize -> %u", RawSlot<13>(o)); break;
        case 14: Note("GetPriority -> %u", RawSlot<14>(o)); break;
        default: break;
        }
    }

    void SurfaceOp(void *s, bool level)
    {
        const bool impl = Ptr(s, 8) != nullptr;
        const unsigned slot = Next(34);
        Note("surface %s slot %u", Name(s).c_str(), slot);
        if (slot <= 14 && slot != 3) return Common(s, slot);
        if (slot == 3) { // ReleaseResource: not on a level (the texture reads its levels' Impls), and seldom
            if (!level && Next(3) == 0) {
                RawSlot<3, void>(s);
                Note("ReleaseResource");
            }
            return;
        }
        if (slot == 15 || slot == 17) {
            const POINT point{LONG(Next(8)), LONG(Next(8))};
            const RECT rect{0, 0, 4, 4};
            const bool b = slot == 15 ? RawSlot<15, bool>(s, &point, roots[0].object, &rect)
                                      : RawSlot<17, bool>(s, &rect, roots[0].object, &rect);
            Note("%s -> %d", slot == 15 ? "CopyRect" : "StretchRect", b);
            return;
        }
        if (slot == 16) return Note("Copy -> %d", RawSlot<16, bool>(s, roots[0].object));
        if (slot == 18) return Note("Stretch -> %d", RawSlot<18, bool>(s, roots[0].object));
        if (slot == 23) return Note("GetMultiSampleType -> %u", RawSlot<23>(s));
        if (slot == 24) return Note("GetMultiSampleQuality -> %u", RawSlot<24>(s));
        if (slot == 30) { // Init over new memory
            if (level) return;
            const Block b = NewBlock(40);
            Init(s, b.bits, b.format, b.w, b.h, b.pitch);
            Note("Init");
            return;
        }
        if (!impl) return;
        switch (slot) {
        case 19: Note("GetAccessFlags -> %u", RawSlot<19>(s)); break;
        case 20: Note("GetWidth -> %u", RawSlot<20>(s)); break;
        case 21: Note("GetHeight -> %u", RawSlot<21>(s)); break;
        case 22: Note("GetPixelFormat -> %s", Name(RawSlot<22, void *>(s)).c_str()); break;
        case 25: {
            g4::cSurfaceLock *const lock = &locks[Next(2)];
            const uint32_t w = Word(Ptr(s, 8), 0x1c), h = Word(Ptr(s, 8), 0x20);
            const int32_t x = int32_t(Next(w + 8)) - 4, y = int32_t(Next(h + 8)) - 4;
            const int32_t width = int32_t(Next(w + 8)) - 2, height = int32_t(Next(h + 8)) - 2;
            const bool readOnly = Next(2) != 0;
            ZeroStackBelow();
            const uint32_t rows = RawSlot<25>(s, lock, x, y, width, height, readOnly);
            Note("LockRect %d %d %d %d %d -> %u", x, y, width, height, readOnly, rows);
            break;
        }
        case 26: {
            g4::cSurfaceLock *const lock = &locks[Next(2)];
            const bool readOnly = Next(2) != 0;
            ZeroStackBelow();
            Note("Lock %d -> %d", readOnly, RawSlot<26, int32_t>(s, lock, readOnly));
            break;
        }
        case 27: {
            g4::cSurfaceLock *const lock = &locks[Next(2)];
            ZeroStackBelow();
            RawSlot<27, void>(s, lock);
            Note("Unlock");
            break;
        }
        case 28: Note("GetLockCount -> %u", RawSlot<28>(s)); break;
        default: { // 29, 31-33: the Direct3D side
            void *d = nullptr;
            RawSlot<0, bool>(s, g4::kIidD3DSurface, &d);
            SurfaceImplOp(static_cast<IDirect3DSurface9 *>(d), level);
            break;
        }
        }
    }

    void SurfaceImplOp(IDirect3DSurface9 *d, bool level)
    {
        const unsigned slot = Next(17);
        Note("surface impl %s slot %u", Name(d).c_str(), slot);
        switch (slot) {
        case 0: {
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const HRESULT hr = d->QueryInterface(kSomeGuid, &out);
            Note("QueryInterface -> %08x %s", unsigned(hr), Name(out).c_str());
            break;
        }
        case 1:
        case 2: {
            const ULONG a = d->AddRef();
            Note("AddRef %u Release %u", unsigned(a), unsigned(d->Release()));
            break;
        }
        case 3: {
            IDirect3DDevice9 *device = reinterpret_cast<IDirect3DDevice9 *>(0x5a5a5a5a);
            const HRESULT hr = d->GetDevice(&device);
            Note("GetDevice -> %08x %s", unsigned(hr), Name(device).c_str());
            break;
        }
        case 4: Note("SetPrivateData -> %08x", unsigned(d->SetPrivateData(kSomeGuid, gPixels, 4, 0))); break;
        case 5: {
            DWORD size = 4;
            Note("GetPrivateData -> %08x %u", unsigned(d->GetPrivateData(kSomeGuid, gPixels, &size)), unsigned(size));
            break;
        }
        case 6: Note("FreePrivateData -> %08x", unsigned(d->FreePrivateData(kSomeGuid))); break;
        case 7: Note("SetPriority -> %u", unsigned(d->SetPriority(Next(5)))); break;
        case 8: Note("GetPriority -> %u", unsigned(d->GetPriority())); break;
        case 9: d->PreLoad(); Note("PreLoad"); break;
        case 10: Note("GetType -> %u", unsigned(d->GetType())); break;
        case 11: {
            SeedQuery();
            const bool poke = !level && Next(2) != 0; // the container answers instead of the null device
            IUnknown **const container = reinterpret_cast<IUnknown **>(reinterpret_cast<uint8_t *>(d) + 0x10);
            IUnknown *const saved = *container;
            if (poke) *container = reinterpret_cast<IUnknown *>(&gContainer);
            gContainerOut = reinterpret_cast<void *>(0x5a5a5a5a);
            const HRESULT hr = d->GetContainer(kSomeGuid, &gContainerOut);
            if (poke) *container = saved;
            Note("GetContainer %d -> %08x %s", poke, unsigned(hr), Name(gContainerOut).c_str());
            break;
        }
        case 12: {
            D3DSURFACE_DESC desc;
            std::memset(&desc, 0xcc, sizeof(desc));
            const HRESULT hr = d->GetDesc(&desc);
            Note("GetDesc -> %08x%s", unsigned(hr), Words(&desc, 0, sizeof(desc)).c_str());
            break;
        }
        case 13: {
            const uint32_t w = Word(d, 0x1c), h = Word(d, 0x20);
            const RECT rect = SomeRect(w, h);
            const bool whole = Next(4) == 0;
            D3DLOCKED_RECT locked;
            std::memset(&locked, 0xcc, sizeof(locked));
            const HRESULT hr = d->LockRect(&locked, whole ? nullptr : &rect, Next(2) ? D3DLOCK_READONLY : 0);
            Note("LockRect %d %d %d -> %08x %d %s", whole, int(rect.left), int(rect.top), unsigned(hr), locked.Pitch,
                 Name(locked.pBits).c_str());
            break;
        }
        case 14: Note("UnlockRect -> %08x", unsigned(d->UnlockRect())); break;
        case 15: {
            HDC dc = reinterpret_cast<HDC>(0x5a5a5a5a);
            Note("GetDC -> %08x %s", unsigned(d->GetDC(&dc)), Name(dc).c_str());
            break;
        }
        default: Note("ReleaseDC -> %08x", unsigned(d->ReleaseDC(nullptr))); break;
        }
    }

    uint32_t LevelCount(void *t) { return (Word(Ptr(t, 8), 0xc) - Word(Ptr(t, 8), 8)) / 4; }

    void TextureOp(void *t)
    {
        const bool impl = Ptr(t, 8) != nullptr;
        const unsigned slot = Next(36);
        Note("texture %s slot %u", Name(t).c_str(), slot);
        if (slot <= 14 && slot != 3) return Common(t, slot);
        if (slot == 3) {
            if (Next(3) == 0) {
                RawSlot<3, void>(t);
                Note("ReleaseResource");
            }
            return;
        }
        if (slot == 15) {
            RawSlot<15, void>(t, 0.25f * float(Next(5)));
            return Note("SetQuality");
        }
        if (slot == 21) return Note("GetQuality -> %g", double(RawSlot<21, float>(t)));
        if (slot == 20) return Note("GetLOD -> %u", RawSlot<20>(t));
        if (slot == 22)
            return Note("Create -> %d", RawSlot<22, bool>(t, static_cast<void *>(nullptr), 4u, 4u,
                                                           static_cast<void *>(&gFormats[0]), 1u, 0.5f, 0u));
        if (slot == 23 || slot == 24) {
            const bool b = slot == 23 ? RawSlot<23, bool>(t, static_cast<void *>(nullptr), 4u, 4u,
                                                          static_cast<void *>(&gFormats[0]), 0u)
                                      : RawSlot<24, bool>(t, static_cast<void *>(nullptr), 4u, 4u,
                                                          static_cast<void *>(&gFormats[0]), 0u);
            return Note("%s -> %d", slot == 23 ? "CreateRenderTarget" : "CreateDepthStencil", b);
        }
        if (slot == 30 || slot == 29) { // a new top level (the levels it had let go)
            const Block b = NewBlock(40);
            SetTopLevel(t, b.bits, b.format, b.w, b.h, b.pitch);
            return Note("SetTopLevel");
        }
        if (!impl) return;
        const uint32_t level = Next(LevelCount(t));
        switch (slot) {
        case 16: Note("GetLevelCount -> %u", RawSlot<16>(t)); break;
        case 17: Note("GetWidth -> %u", RawSlot<17>(t, Next(4))); break;
        case 18: Note("GetHeight -> %u", RawSlot<18>(t, Next(4))); break;
        case 19: Note("GetPixelFormat -> %s", Name(RawSlot<19, void *>(t)).c_str()); break;
        case 25: // the game's image code also calls 0x18c700 directly: the adapters route calls the hooked member
            if (route == kOurs)
                Note("GetSurface %u -> %s", level, Name(RawSlot<25, void *>(t, level)).c_str());
            else if (route == kOriginal)
                Note("GetSurface %u -> %s", level,
                     Name(ctxOriginal<void *(__thiscall *)(void *, uint32_t)>(0x18c700)(t, level)).c_str());
            else
                Note("GetSurface %u -> %s", level,
                     Name(static_cast<cMemBlockAsTexture *>(t)->LevelSurface(level)).c_str());
            break;
        case 26: Note("GetTargetSurface %u -> %s", level, Name(RawSlot<26, void *>(t, level)).c_str()); break;
        case 27: Note("GetLockableSurface %u -> %s", level, Name(RawSlot<27, void *>(t, level)).c_str()); break;
        case 28: Note("GetTargetLockableSurface %u -> %s", level, Name(RawSlot<28, void *>(t, level)).c_str()); break;
        case 31: {
            const Block b = NewBlock(16);
            AddLevel(t, b.bits, b.w, b.h, b.pitch);
            Note("AddLevel");
            break;
        }
        case 32:
        case 33: SurfaceOp(RawSlot<25, void *>(t, level), true); break;
        default: {
            void *x = nullptr;
            RawSlot<0, bool>(t, g4::kIidD3DTexture, &x);
            TextureImplOp(static_cast<IDirect3DTexture9 *>(x), LevelCount(t));
            break;
        }
        }
    }

    void TextureImplOp(IDirect3DTexture9 *x, uint32_t levels)
    {
        const unsigned slot = Next(22);
        const UINT level = Next(levels);
        Note("texture impl %s slot %u level %u", Name(x).c_str(), slot, unsigned(level));
        switch (slot) {
        case 0: {
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const HRESULT hr = x->QueryInterface(kSomeGuid, &out);
            Note("QueryInterface -> %08x %s", unsigned(hr), Name(out).c_str());
            break;
        }
        case 1:
        case 2: {
            const ULONG a = x->AddRef();
            Note("AddRef %u Release %u", unsigned(a), unsigned(x->Release()));
            break;
        }
        case 3: {
            IDirect3DDevice9 *device = reinterpret_cast<IDirect3DDevice9 *>(0x5a5a5a5a);
            const HRESULT hr = x->GetDevice(&device);
            Note("GetDevice -> %08x %s", unsigned(hr), Name(device).c_str());
            break;
        }
        case 4: Note("SetPrivateData -> %08x", unsigned(x->SetPrivateData(kSomeGuid, gPixels, 4, 0))); break;
        case 5: {
            DWORD size = 4;
            Note("GetPrivateData -> %08x %u", unsigned(x->GetPrivateData(kSomeGuid, gPixels, &size)), unsigned(size));
            break;
        }
        case 6: Note("FreePrivateData -> %08x", unsigned(x->FreePrivateData(kSomeGuid))); break;
        case 7: Note("SetPriority -> %u", unsigned(x->SetPriority(Next(5)))); break;
        case 8: Note("GetPriority -> %u", unsigned(x->GetPriority())); break;
        case 9: x->PreLoad(); Note("PreLoad"); break;
        case 10: Note("GetType -> %u", unsigned(x->GetType())); break;
        case 11: Note("SetLOD -> %u", unsigned(x->SetLOD(Next(3)))); break;
        case 12: Note("GetLOD -> %u", unsigned(x->GetLOD())); break;
        case 13: Note("GetLevelCount -> %u", unsigned(x->GetLevelCount())); break;
        case 14: Note("SetAutoGenFilterType -> %08x", unsigned(x->SetAutoGenFilterType(D3DTEXF_LINEAR))); break;
        case 15: Note("GetAutoGenFilterType -> %u", unsigned(x->GetAutoGenFilterType())); break;
        case 16: x->GenerateMipSubLevels(); Note("GenerateMipSubLevels"); break;
        case 17: {
            D3DSURFACE_DESC desc;
            std::memset(&desc, 0xcc, sizeof(desc));
            const HRESULT hr = x->GetLevelDesc(level, &desc);
            Note("GetLevelDesc -> %08x%s", unsigned(hr), Words(&desc, 0, sizeof(desc)).c_str());
            break;
        }
        case 18: {
            IDirect3DSurface9 *surface = reinterpret_cast<IDirect3DSurface9 *>(0x5a5a5a5a);
            const HRESULT hr = x->GetSurfaceLevel(level, &surface);
            Note("GetSurfaceLevel -> %08x %s", unsigned(hr), Name(surface).c_str());
            Note("Release %u", unsigned(surface->Release()));
            break;
        }
        case 19: {
            void *const face = Ptr(Ptr(x, 8), 4 * level); // the level's face
            void *const d = Ptr(face, 8);
            const RECT rect = SomeRect(Word(d, 0x1c), Word(d, 0x20));
            const bool whole = Next(4) == 0;
            D3DLOCKED_RECT locked;
            std::memset(&locked, 0xcc, sizeof(locked));
            const HRESULT hr = x->LockRect(level, &locked, whole ? nullptr : &rect, 0);
            Note("LockRect %d %d %d -> %08x %d %s", whole, int(rect.left), int(rect.top), unsigned(hr), locked.Pitch,
                 Name(locked.pBits).c_str());
            break;
        }
        case 20: Note("UnlockRect -> %08x", unsigned(x->UnlockRect(level))); break;
        default: {
            const RECT rect{0, 0, 2, 2};
            Note("AddDirtyRect -> %08x", unsigned(x->AddDirtyRect(Next(2) ? &rect : nullptr)));
            break;
        }
        }
    }

    // ---- one seed ----
    void Run(uint32_t seed)
    {
        random.seed(seed);
        // Two surfaces: the full constructor, and the empty one (Init-ed now or by an op later).
        for (int i = 0; i < 2; ++i) {
            const Block b = NewBlock(40);
            void *const s = NewSurface(i == 0, b.bits, b.format, b.w, b.h, b.pitch);
            roots.push_back({s, false});
            RawSlot<1>(s); // the creator's count
            if (i == 1) {
                for (int k = Next(3); k > 0; --k) SurfaceOp(s, false);
                if (Next(2)) {
                    const Block c = NewBlock(40);
                    Init(s, c.bits, c.format, c.w, c.h, c.pitch);
                }
            }
            Dump();
        }
        void *const t = NewTexture();
        roots.push_back({t, true});
        RawSlot<1>(t);
        for (int k = Next(3); k > 0; --k) {
            const unsigned slot = Next(3);
            if (slot == 0) Common(t, 0);
            if (slot == 1) Note("IsCreated -> %d", RawSlot<6, bool>(t));
            if (slot == 2) Note("GetPool -> %u", RawSlot<12>(t));
        }
        const Block top = NewBlock(40);
        SetTopLevel(t, top.bits, top.format, top.w, top.h, top.pitch);
        for (int k = Next(5); k > 0; --k) {
            const Block b = NewBlock(16);
            AddLevel(t, b.bits, b.w, b.h, b.pitch);
        }
        Dump();
        for (int op = 0; op < 60; ++op) {
            const uint32_t which = Next(3);
            if (which == 2)
                TextureOp(t);
            else
                SurfaceOp(roots[which].object, false);
            Dump();
        }
        // The lock records let go of what they hold, then each root goes: by its destructor (the caller's storage) or
        // by its last Release (the deleting destructor).
        for (g4::cSurfaceLock &lock : locks)
            if (lock.surface) {
                void *const held = lock.surface;
                lock.surface = nullptr;
                Note("lock Release -> %u", RawSlot<2>(held));
                Dump();
            }
        while (!roots.empty()) {
            const Root root = roots.back();
            roots.pop_back();
            if (Next(2)) {
                Destroy(root);
                Note("destroyed");
            } else {
                *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(root.object) + 4) = 1;
                Note("last Release -> %u", RawSlot<2>(root.object));
            }
            Dump();
        }
    }
};

std::vector<std::string> RunRoute(difftest::Context &ctx, Route route, uint32_t seed)
{
    std::vector<std::string> record;
    gRecord = &record;
    World world;
    world.route = route;
    world.ctx = &ctx;
    world.Run(seed);
    gRecord = nullptr;
    return record;
}

void Compare(difftest::Context &ctx, Route route, uint32_t seeds)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    void **const framework = GameAddress<void *>(0x10c9378);
    void *const savedFramework = *framework;
    *framework = &gFramework;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        ++ctx.cases;
        char mark[64];
        std::snprintf(mark, sizeof(mark), "seed %u original", unsigned(seed));
        ctx.Mark(mark);
        const std::vector<std::string> expected = RunRoute(ctx, kOriginal, seed);
        std::snprintf(mark, sizeof(mark), "seed %u route %d", unsigned(seed), int(route));
        ctx.Mark(mark);
        const std::vector<std::string> actual = RunRoute(ctx, route, seed);
        const size_t n = expected.size() < actual.size() ? expected.size() : actual.size();
        size_t i = 0;
        while (i < n && expected[i] == actual[i]) ++i;
        if (i == n && expected.size() == actual.size()) continue;
        ctx.Fail("seed %u line %u: original '%s' ours '%s' (previous '%s')", unsigned(seed), unsigned(i),
                 i < expected.size() ? expected[i].c_str() : "<end>", i < actual.size() ? actual[i].c_str() : "<end>",
                 i > 0 ? expected[i - 1].c_str() : "");
    }
    *framework = savedFramework;
}

} // namespace

// Our constructors, members and every slot through our vtables, against the game's.
DIFF_TEST(MemBlock_Classes, 0x18d1a0) { Compare(ctx, kOurs, 400); }

// Our members and destructors on objects the game's constructors made (the game's vtables).
DIFF_TEST(MemBlock_Adapters, 0x18d500) { Compare(ctx, kAdapters, 200); }
