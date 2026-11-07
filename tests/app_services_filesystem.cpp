// Differential tests for cGZFileSystem (src/app/AppServices.*): constructor, destructors,
// QueryInterface and its thunks, the directory getters, the CD getters, and the small setters.
//
// Objects are heap blocks (with a canary tail) built by the original constructor on one side and by
// ours (the constructor hook's adapter, our vtables) on the other, then given the same random strings
// and flags. Each side carries copies of its own vtables (the game's; ours) with the slots the code
// calls through `this` replaced by the same recording fakes (AddRef, slot 13 of the primary vtable,
// slots 3, 4 and 7..13 of the file-system face); the other slots stay the side's own. Strings hold
// heap buffers when long, so blocks are compared field by field: the vtable words through a code map,
// the strings by size, capacity and text, the other bytes exactly.
#include "app/AppServices.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "service_shape.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kSize = cGZFileSystem::kSize;
constexpr size_t kTail = 16;
constexpr uint32_t kStringOffsets[] = {0x24, 0x44, 0x64, 0x84, 0xa4, 0xc4, 0xe4, 0x108, 0x128, 0x148, 0x168, 0x188, 0x1a8};
constexpr uint32_t kVTableOffsets[] = {0, 4, cGZFileSystem::kFace};

std::vector<std::string> gLog;
unsigned char *gBase = nullptr;

void Log(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

int Off(const void *p)
{
    return static_cast<int>(static_cast<const unsigned char *>(p) - gBase);
}

uint32_t __fastcall RecAddRef(void *self, void *)
{
    Log("addref@%d", Off(self));
    return 0x4242;
}

template <int N> uint32_t __fastcall LogSlot(void *self, void *)
{
    Log("s%d@%d", N, Off(self));
    return 0x31;
}

// Per side: the vtables as built (the game's; ours from an object of our class) and the patched copies.
struct SideVTables {
    void *real[3] = {};
    void *primary[15], *unknown[6], *face[23];
};
SideVTables gGame, gOurs;
svc::CodeMap gGameCodes, gOurCodes;

void Patch(SideVTables &v)
{
    std::memcpy(v.primary, v.real[0], sizeof(v.primary));
    std::memcpy(v.unknown, v.real[1], sizeof(v.unknown));
    std::memcpy(v.face, v.real[2], sizeof(v.face));
    v.primary[1] = v.unknown[1] = v.face[1] = reinterpret_cast<void *>(&RecAddRef);
    v.primary[13] = reinterpret_cast<void *>(&LogSlot<13>);
    v.face[7] = reinterpret_cast<void *>(&LogSlot<7>);
    v.face[8] = reinterpret_cast<void *>(&LogSlot<8>);
    v.face[9] = reinterpret_cast<void *>(&LogSlot<9>);
    v.face[13] = reinterpret_cast<void *>(&LogSlot<13>);
    // The CD getters' own slots too, so a call to the wrong neighbour of slot 9 shows in the log.
    v.face[10] = reinterpret_cast<void *>(&LogSlot<10>);
    v.face[11] = reinterpret_cast<void *>(&LogSlot<11>);
    v.face[12] = reinterpret_cast<void *>(&LogSlot<12>);
    v.face[3] = reinterpret_cast<void *>(&LogSlot<3>);
    v.face[4] = reinterpret_cast<void *>(&LogSlot<4>);
}

void SetupVTables(difftest::Context &ctx)
{
    gGame.real[0] = ctx.image.At(cGZFileSystem::kVTable);
    gGame.real[1] = ctx.image.At(cGZFileSystem::kUnknownVTable);
    gGame.real[2] = ctx.image.At(cGZFileSystem::kFileSystemVTable);
    alignas(8) static unsigned char probe[kSize];
    cGZFileSystem *ours = cGZFileSystemAdapters::Construct(probe, nullptr);
    for (int i = 0; i < 3; ++i) gOurs.real[i] = svc::VTableAt(probe, kVTableOffsets[i]);
    ours->cGZFileSystem::~cGZFileSystem();
    Patch(gGame);
    Patch(gOurs);
    gGameCodes = {};
    gOurCodes = {};
    for (uint32_t i = 0; i < 3; ++i) {
        gGameCodes.Add(gGame.real[i], 1 + i);
        gOurCodes.Add(gOurs.real[i], 1 + i);
    }
    gGameCodes.Add(gGame.primary, 1);
    gGameCodes.Add(gGame.unknown, 2);
    gGameCodes.Add(gGame.face, 3);
    gOurCodes.Add(gOurs.primary, 1);
    gOurCodes.Add(gOurs.unknown, 2);
    gOurCodes.Add(gOurs.face, 3);
    svc::AddServiceBaseCodes(gGameCodes, gOurCodes, 10);
}

// The slot of `vtable` (among `count` words) holding the game function at `rva`; -1 if none.
int SlotOf(void *const *vtable, int count, uint32_t rva)
{
    for (int i = 0; i < count; ++i)
        if (vtable[i] == GameAddress<void>(rva)) return i;
    return -1;
}

// Our side's destructor (non-virtual) and the original's, for freeing a pair.
void DestroyOriginal(difftest::Context &ctx, unsigned char *block)
{
    ctx.Original<void(__thiscall *)(void *)>(0xc75d9b)(block);
}

void DestroyOurs(unsigned char *block)
{
    reinterpret_cast<cGZFileSystem *>(block)->cGZFileSystem::~cGZFileSystem();
}

// The global the constructor reads the plug-in directory name from.
char gNameText[64] = "Plugins";
struct NameGlobal {
    difftest::Context &ctx;
    const char *saved;
    NameGlobal(difftest::Context &c, const char *text) : ctx(c)
    {
        saved = *ctx.image.At<const char *>(cGZFileSystem::kPluginDirectoryNameGlobal);
        std::snprintf(gNameText, sizeof(gNameText), "%s", text);
        *ctx.image.At<const char *>(cGZFileSystem::kPluginDirectoryNameGlobal) = gNameText;
    }
    ~NameGlobal() { *ctx.image.At<const char *>(cGZFileSystem::kPluginDirectoryNameGlobal) = saved; }
};

unsigned char *RawBlock(int fill)
{
    auto *p = static_cast<unsigned char *>(::operator new(kSize + kTail));
    std::memset(p, fill, kSize + kTail);
    return p;
}

cRZString &StringAt(unsigned char *block, uint32_t off)
{
    return *reinterpret_cast<cRZString *>(block + off);
}

// `a` is the original's block, `b` ours: the vtable words through each side's code map, the strings by size, capacity and
// text; everything else byte for byte.
bool SameFs(const unsigned char *a, const unsigned char *b, std::string *why)
{
    std::vector<bool> skip(kSize + kTail, false);
    for (uint32_t off : kStringOffsets)
        for (uint32_t i = off + 4; i < off + 0x1c; ++i) skip[i] = true;
    for (uint32_t off : kVTableOffsets) {
        for (uint32_t i = off; i < off + 4; ++i) skip[i] = true;
        uint32_t wa, wb;
        std::memcpy(&wa, a + off, 4);
        std::memcpy(&wb, b + off, 4);
        if (gGameCodes.Code(wa) != gOurCodes.Code(wb)) {
            if (why) *why = "vtable word at 0x" + std::to_string(off);
            return false;
        }
    }
    for (size_t i = 0; i < kSize + kTail; ++i) {
        if (!skip[i] && a[i] != b[i]) {
            if (why) *why = "byte at 0x" + std::to_string(i);
            return false;
        }
    }
    for (uint32_t off : kStringOffsets) {
        const std::string &sa = reinterpret_cast<const cRZString *>(a + off)->mString;
        const std::string &sb = reinterpret_cast<const cRZString *>(b + off)->mString;
        if (sa != sb || sa.capacity() != sb.capacity()) {
            if (why) *why = "string at 0x" + std::to_string(off);
            return false;
        }
    }
    return true;
}

std::string RandomText(std::mt19937 &rng)
{
    static const char alphabet[] = "aBz_:\\/.";
    int len = std::uniform_int_distribution<int>(0, 40)(rng);
    if (rng() % 3 == 0) len = std::uniform_int_distribution<int>(13, 17)(rng);
    std::string s;
    for (int i = 0; i < len; ++i) s += alphabet[rng() % (sizeof(alphabet) - 1)];
    return s;
}

struct Pair {
    unsigned char *a;
    unsigned char *b;
};

uint8_t RandomFlag(std::mt19937 &rng)
{
    switch (rng() % 3) {
    case 0: return 0;
    case 1: return 1;
    default: return static_cast<uint8_t>(rng() | 2);
    }
}

// Original constructor on a, ours on b, over the same random bytes; then the same strings and
// flags on both, and each side's patched vtables.
Pair MakePair(difftest::Context &ctx, std::mt19937 &rng, bool withStrings)
{
    Pair p{RawBlock(0), RawBlock(0)};
    for (size_t i = 0; i < kSize + kTail; ++i) p.a[i] = static_cast<unsigned char>(rng());
    std::memcpy(p.b, p.a, kSize + kTail);
    ctx.Original<void *(__thiscall *)(void *)>(0xc75ccd)(p.a);
    cGZFileSystemAdapters::Construct(p.b, nullptr);
    if (withStrings) {
        for (uint32_t off : kStringOffsets) {
            std::string text = RandomText(rng);
            StringAt(p.a, off).mString.assign(text);
            StringAt(p.b, off).mString.assign(text);
        }
        for (uint32_t off : {0x20u, 0x21u, 0x22u, 0x104u}) p.a[off] = p.b[off] = RandomFlag(rng);
    }
    void *game[3] = {gGame.primary, gGame.unknown, gGame.face};
    void *ours[3] = {gOurs.primary, gOurs.unknown, gOurs.face};
    for (int i = 0; i < 3; ++i) {
        std::memcpy(p.a + kVTableOffsets[i], &game[i], sizeof(void *));
        std::memcpy(p.b + kVTableOffsets[i], &ours[i], sizeof(void *));
    }
    return p;
}

void Free(difftest::Context &ctx, Pair p)
{
    DestroyOriginal(ctx, p.a);
    DestroyOurs(p.b);
    ::operator delete(p.a);
    ::operator delete(p.b);
}

} // namespace

DIFF_TEST(AppServices_FileSystem_Construct, 0xc75ccd)
{
    SetupVTables(ctx);
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xc75ccd);
    const char *texts[] = {"Plugins", "", "AShortName", "aVeryLongPluginDirectoryNameThatDoesNotFit"};
    for (const char *text : texts) {
        NameGlobal global(ctx, text);
        for (int fill : {0xCD, 0x00, 0xFF}) {
            unsigned char *a = RawBlock(fill), *b = RawBlock(fill);
            void *ra = orig(a);
            void *rb = cGZFileSystemAdapters::Construct(b, nullptr);
            ++ctx.cases;
            std::string why;
            if (ra != a || rb != b || !SameFs(a, b, &why))
                ctx.Fail("fill %02x text '%s': constructor differs (%s)", fill, text, why.c_str());
            // Each side's own vtables (codes 1..3), not the copies.
            for (uint32_t i = 0; i < 3; ++i)
                if (svc::VTableAt(a, kVTableOffsets[i]) != gGame.real[i] || svc::VTableAt(b, kVTableOffsets[i]) != gOurs.real[i]) ctx.Fail("fill %02x: vtable %u", fill, i);
            auto *fs = reinterpret_cast<cGZFileSystem *>(b);
            uint32_t serviceId;
            std::memcpy(&serviceId, a + 0xc, 4);
            if (serviceId != cGZFileSystem::kServiceId || fs->mPluginDirectoryName.mString != text || fs->mUseCDIniFile) ctx.Fail("fill %02x: unexpected state", fill);
            DestroyOriginal(ctx, a);
            DestroyOurs(b);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

DIFF_TEST(AppServices_FileSystem_Destruct, 0xc75d9b)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xc75d9b);
    std::mt19937 rng(0xc75d9b);
    for (int run = 0; run < 150; ++run) {
        Pair p = MakePair(ctx, rng, true);
        // Heap buffers of the long strings: both sides must free all of them.
        void *buffersA[13], *buffersB[13];
        int n = 0;
        for (uint32_t off : kStringOffsets) {
            const std::string &sa = StringAt(p.a, off).mString;
            buffersA[n] = sa.capacity() > 15 ? const_cast<char *>(sa.data()) : nullptr;
            buffersB[n] = StringAt(p.b, off).mString.capacity() > 15 ? const_cast<char *>(StringAt(p.b, off).mString.data()) : nullptr;
            ++n;
        }
        gLog.clear();
        gBase = p.a;
        difftest::FreeRecorder recA;
        orig(p.a);
        recA.Stop();
        bool freedA = true;
        for (void *q : buffersA) freedA = freedA && (!q || recA.Freed(q));
        gBase = p.b;
        difftest::FreeRecorder recB;
        DestroyOurs(p.b);
        recB.Stop();
        bool freedB = true;
        for (void *q : buffersB) freedB = freedB && (!q || recB.Freed(q));
        ++ctx.cases;
        std::string why;
        if (!freedA || !freedB) ctx.Fail("run %d: string buffers freed %d/%d", run, freedA, freedB);
        if (!SameFs(p.a, p.b, &why)) ctx.Fail("run %d: state after destruction differs (%s)", run, why.c_str());
        ::operator delete(p.a);
        ::operator delete(p.b);
    }
}

DIFF_TEST(AppServices_FileSystem_DeletingDestructor, 0xc75e94)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xc75e94);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xc75e8c);
    // Only the +4 face's vtable holds the destructor (its thunk 0xc75e8c; the primary one has none). Ours: the same slot of
    // our +4 vtable, for both routes (the direct one has no entry point of ours: the thunk's target is it).
    const int thunkSlot = SlotOf(static_cast<void *const *>(gGame.real[1]), 6, 0xc75e8c);
    if (thunkSlot < 0) {
        ctx.Fail("deleting destructor slot not found");
        return;
    }
    using DeletingFn = void *(__thiscall *)(void *, uint32_t);
    auto oursThunk = reinterpret_cast<DeletingFn>(static_cast<void **>(gOurs.real[1])[thunkSlot]);
    auto ours = [&](unsigned char *object, uint32_t flags) { return oursThunk(object + 4, flags); };
    std::mt19937 rng(0xc75e94);
    for (int run = 0; run < 48; ++run) {
        uint32_t flags = run % 4;
        bool viaThunk = run % 8 >= 4;
        Pair p = MakePair(ctx, rng, false);
        difftest::FreeRecorder recA;
        void *ra = viaThunk ? thunk(p.a + 4, flags) : orig(p.a, flags);
        recA.Stop();
        bool freedA = recA.Freed(p.a);
        difftest::FreeRecorder recB;
        void *rb = viaThunk ? oursThunk(p.b + 4, flags) : ours(p.b, flags);
        recB.Stop();
        bool freedB = recB.Freed(p.b);
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("run %d: return value is not the object", run);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("run %d flags %u: object freed %d/%d", run, flags, freedA, freedB);
        std::string why;
        if (!freedA) {
            if (!SameFs(p.a, p.b, &why)) ctx.Fail("run %d: state differs (%s)", run, why.c_str());
            ::operator delete(p.a);
            ::operator delete(p.b);
        }
    }
}

DIFF_TEST(AppServices_FileSystem_QueryInterface, 0xc765d7)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xc765d7);
    auto origUnknown = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xc76601);
    auto origFace = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xc76609);
    const uint32_t ids[] = {cGZFileSystem::kInterfaceId, 0x287fb697, 1, 0, 0xe0203661, 0xe020365f, 0xffffffff, 0x089b7dc8};
    std::mt19937 rng(0xc765d7);
    void *const sentinel = reinterpret_cast<void *>(0x5a5a5a5a);
    for (uint32_t id : ids) {
        for (int how = 0; how < 3; ++how) { // direct, through the +4 thunk, through the +0x1c thunk
            Pair p = MakePair(ctx, rng, true);
            void *outA = sentinel, *outB = sentinel;
            gLog.clear();
            gBase = p.a;
            uint8_t ra = how == 0 ? orig(p.a, id, &outA) : how == 1 ? origUnknown(p.a + 4, id, &outA) : origFace(p.a + 0x1c, id, &outA);
            std::string logA = Joined();
            gLog.clear();
            gBase = p.b;
            // Ours: the member, then slot 0 of our +4 and +0x1c vtables (the compiler's thunks).
            auto *fs = reinterpret_cast<cGZFileSystem *>(p.b);
            using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
            uint8_t rb = how == 0 ? fs->cGZFileSystem::QueryInterface(id, &outB)
                : how == 1 ? reinterpret_cast<QueryFn>(static_cast<void **>(gOurs.real[1])[0])(p.b + 4, id, &outB)
                           : reinterpret_cast<QueryFn>(static_cast<void **>(gOurs.real[2])[0])(p.b + 0x1c, id, &outB);
            ++ctx.cases;
            ptrdiff_t offA = outA == sentinel ? -1 : static_cast<unsigned char *>(outA) - p.a;
            ptrdiff_t offB = outB == sentinel ? -1 : static_cast<unsigned char *>(outB) - p.b;
            if (ra != rb || logA != Joined() || offA != offB || !SameFs(p.a, p.b, nullptr))
                ctx.Fail("id 0x%x via %d: result %u/%u, out offset %td/%td, log %s / %s", id, how, ra, rb, offA, offB, logA.c_str(), Joined().c_str());
            if (id == cGZFileSystem::kInterfaceId && (ra != 1 || offA != 0x1c)) ctx.Fail("interface id: result %u offset %td", ra, offA);
            Free(ctx, p);
        }
    }
}

namespace {

struct GetterEntry {
    uint32_t rva;
    const char *(*ours)(cGZFileSystem *);
};

// Our members, called without dispatch (the patched vtables hold fakes in some of these slots).
const GetterEntry kGetters[] = {
    {0xc75ec3, [](cGZFileSystem *f) { return f->cGZFileSystem::AppDirectory(); }},
    {0xc75ee5, [](cGZFileSystem *f) { return f->cGZFileSystem::AppDrive(); }},
    {0xc75f07, [](cGZFileSystem *f) { return f->cGZFileSystem::AppExtension(); }},
    {0xc75f29, [](cGZFileSystem *f) { return f->cGZFileSystem::AppName(); }},
    {0xc75f4b, [](cGZFileSystem *f) { return f->cGZFileSystem::CDAppDirectory(); }},
    {0xc75f6c, [](cGZFileSystem *f) { return f->cGZFileSystem::CDAppDrive(); }},
    {0xc75fb7, [](cGZFileSystem *f) { return f->cGZFileSystem::CDDataDirectory(); }},
    {0xc75fd8, [](cGZFileSystem *f) { return f->cGZFileSystem::CDPlugInDirectory(); }},
};

std::string Describe(const void *result, const unsigned char *base)
{
    auto p = static_cast<const unsigned char *>(result);
    bool inside = p >= base && p < base + kSize;
    return (inside ? "in-block@" + std::to_string(p - base) : std::string("heap")) + ":" + static_cast<const char *>(result);
}

} // namespace

DIFF_TEST(AppServices_FileSystem_Getters, 0xc75ec3)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    std::mt19937 rng(0xc75ec3);
    for (const GetterEntry &e : kGetters) {
        auto orig = ctx.Original<const char *(__thiscall *)(void *)>(e.rva);
        for (int run = 0; run < 80; ++run) {
            Pair p = MakePair(ctx, rng, true);
            // Cover each combination of the flags the getters read.
            if (run < 16) {
                p.a[0x22] = p.b[0x22] = run & 1;
                p.a[0x104] = p.b[0x104] = (run >> 1) & 1;
                p.a[0x20] = p.b[0x20] = (run >> 2) & 1;
                p.a[0x21] = p.b[0x21] = (run >> 3) & 1;
            }
            gLog.clear();
            gBase = p.a;
            std::string resA = Describe(orig(p.a + 0x1c), p.a);
            std::string logA = Joined();
            gLog.clear();
            gBase = p.b;
            std::string resB = Describe(e.ours(reinterpret_cast<cGZFileSystem *>(p.b)), p.b);
            ++ctx.cases;
            std::string why;
            if (resA != resB || logA != Joined() || !SameFs(p.a, p.b, &why))
                ctx.Fail("0x%x run %d: result %s / %s, log %s / %s, state %s", e.rva, run, resA.c_str(), resB.c_str(), logA.c_str(), Joined().c_str(), why.c_str());
            Free(ctx, p);
        }
    }
}

namespace {

struct Dest {
    void **vtable;
};
void *gDestVT[8];

uint32_t __fastcall DestFromChar(Dest *, void *, const char *text, uint32_t size)
{
    Log("3:%s:%u", std::string(text, size).c_str(), size);
    return 0xABCD0000u | size;
}

} // namespace

DIFF_TEST(AppServices_FileSystem_GetPluginDirectoryName, 0xc763cb)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xc763cb);
    for (void *&s : gDestVT) s = nullptr;
    gDestVT[3] = reinterpret_cast<void *>(&DestFromChar);
    Dest dest{gDestVT};
    std::mt19937 rng(0xc763cb);
    for (int run = 0; run < 100; ++run) {
        Pair p = MakePair(ctx, rng, true);
        gLog.clear();
        gBase = p.a;
        void *ra = orig(p.a + 0x1c, &dest);
        std::string logA = Joined();
        gLog.clear();
        gBase = p.b;
        void *rb = reinterpret_cast<void *>(static_cast<uintptr_t>(reinterpret_cast<cGZFileSystem *>(p.b)->cGZFileSystem::GetPluginDirectoryName(reinterpret_cast<cIGZString *>(&dest))));
        ++ctx.cases;
        if (ra != rb || logA != Joined() || logA.empty() || !SameFs(p.a, p.b, nullptr)) ctx.Fail("run %d: result %p/%p, log %s / %s", run, ra, rb, logA.c_str(), Joined().c_str());
        Free(ctx, p);
    }
}

DIFF_TEST(AppServices_FileSystem_FlagSetters, 0xc75eb6)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    auto origRecursive = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xc75eb6);
    auto origIni = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xc766f5);
    std::mt19937 rng(0xc75eb6);
    for (int run = 0; run < 100; ++run) {
        // The argument's upper bytes are garbage: only the low byte is stored.
        uint32_t value = (rng() & ~0xffu) | (run < 4 ? run * 0x55 : rng() & 0xff);
        Pair p = MakePair(ctx, rng, true);
        origRecursive(p.a + 0x1c, value);
        auto *face = reinterpret_cast<cIGZFileSystem *>(p.b + 0x1c);
        cGZFileSystemAdapters::AllowRecursiveCDSearch(face, nullptr, static_cast<uint8_t>(value));
        origIni(p.a + 0x1c, value >> 3);
        cGZFileSystemAdapters::UseCDIniFile(face, nullptr, static_cast<uint8_t>(value >> 3));
        ++ctx.cases;
        if (!SameFs(p.a, p.b, nullptr) || p.a[0x1c9] != static_cast<uint8_t>(value) || p.a[0x1c8] != static_cast<uint8_t>(value >> 3))
            ctx.Fail("run %d value %x: flags differ", run, value);
        Free(ctx, p);
    }
}

namespace {

struct StringSetter {
    uint32_t rva;
    uint32_t offset;
    void *(__fastcall *ours)(cIGZFileSystem *, void *, const cIGZString *);
};

// The hooked adapters (byte matches).
const StringSetter kStringSetters[] = {
    {0xc76691, 0x1a8, &cGZFileSystemAdapters::SetExpectedCDAppPath},
    {0xc7669c, 0x188, &cGZFileSystemAdapters::SetExpectedCDName},
    {0xc766ea, 0xe4, &cGZFileSystemAdapters::SetPluginDirectoryName},
};

} // namespace

DIFF_TEST(AppServices_FileSystem_StringSetters, 0xc76691)
{
    SetupVTables(ctx);
    NameGlobal global(ctx, "Plugins");
    std::mt19937 rng(0xc76691);
    for (const StringSetter &e : kStringSetters) {
        auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(e.rva);
        for (int run = 0; run < 60; ++run) {
            Pair p = MakePair(ctx, rng, true);
            // The argument is a cRZString (the game's vtable): slot 16 copies its text.
            alignas(8) unsigned char argBytes[sizeof(cRZString)];
            auto *arg = reinterpret_cast<cRZString *>(argBytes);
            arg->Construct(RandomText(rng).c_str());
            gLog.clear();
            gBase = p.a;
            void *ra = orig(p.a + 0x1c, arg);
            gBase = p.b;
            void *rb = e.ours(reinterpret_cast<cIGZFileSystem *>(p.b + 0x1c), nullptr, arg->AsInterface());
            ++ctx.cases;
            std::string why;
            if (static_cast<unsigned char *>(ra) - p.a != static_cast<unsigned char *>(rb) - p.b || static_cast<unsigned char *>(ra) - p.a != e.offset || !SameFs(p.a, p.b, &why) ||
                StringAt(p.a, e.offset).mString != arg->mString)
                ctx.Fail("0x%x run %d: result offset %td/%td, state %s", e.rva, run, static_cast<unsigned char *>(ra) - p.a, static_cast<unsigned char *>(rb) - p.b, why.c_str());
            arg->Destruct();
            Free(ctx, p);
        }
    }
}
