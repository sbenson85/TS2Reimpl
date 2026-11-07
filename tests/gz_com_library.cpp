// cGZCOMLibrary: constructors, destructors, QueryInterface, Release, GetPath, ordering, Unload and
// Initialize (see gz_com_support.h). Initialize runs against scripted import fakes. Side 0 is built by the original
// constructors (the game's vtable), side 1 by ours (our vtable); slots go through each side's vtable.
#include "free_recorder.h"
#include "gz_com_support.h"

namespace {

cGZCOMLibrary *RawLibrary(int fill)
{
    return RawObject<cGZCOMLibrary>(sizeof(cGZCOMLibrary), fill);
}

// The object's bytes, with the string's heap pointer masked when it is out of line and the vtable word named.
std::vector<unsigned char> CanonLib(const cGZCOMLibrary *lib, int side)
{
    std::vector<unsigned char> bytes(sizeof(cGZCOMLibrary));
    std::memcpy(bytes.data(), lib, sizeof(cGZCOMLibrary));
    if (lib->mPath.mString.capacity() > 15) std::memset(bytes.data() + 0x14, 0, 4);
    uint32_t word = VtCode(Word<uint32_t>(lib, 0), side);
    std::memcpy(bytes.data(), &word, 4);
    return bytes;
}

std::string DescribeLib(const cGZCOMLibrary *lib, int side)
{
    char buf[200];
    std::snprintf(buf, sizeof(buf), "lib[%s init=%d ref=%d dir=%s mod=%p vt=%s pathvt=%s] ", lib->mPath.mString.c_str(), Word<uint8_t>(lib, 4), lib->mRefCount,
                  static_cast<void *>(lib->mDirector) == &gDirA ? "A" : lib->mDirector ? "other" : "null", lib->mModule,
                  Word<void **>(lib, 0) == gVt[side].lib ? "own" : "fake", lib->mPath.mVTable == GameAddress(cRZString::kVTable) ? "game" : "fake");
    return buf;
}

std::vector<std::string> TestPaths()
{
    return {"", "a", "x.dll", "exactly15chars!", "sixteen_chars_ab", std::string(100, 'q'), "caf\xc3\xa9.dll", "Plugins\\Some Library.dll"};
}

void *gStrVt[40];
const char *__fastcall FakePathToChar(void *self, void *)
{
    Log("path.toChar");
    return static_cast<cRZString *>(self)->mString.c_str();
}

void InstallPathVtable(difftest::Context &ctx, cGZCOMLibrary *lib)
{
    std::memcpy(gStrVt, ctx.image.At<void *>(cRZString::kVTable), sizeof(gStrVt));
    gStrVt[5] = reinterpret_cast<void *>(&FakePathToChar);
    lib->mPath.mVTable = gStrVt;
}

// Builds a library on raw memory with the side's path constructor.
cGZCOMLibrary *ConstructLib(difftest::Context &ctx, int side, cGZCOMLibrary *memory, const cIGZString *path)
{
    if (side == kOriginal) return static_cast<cGZCOMLibrary *>(ctx.Original<void *(__thiscall *)(void *, const cIGZString *)>(kLibCtor)(memory, path));
    return cGZCOMLibraryAdapters::Construct(memory, nullptr, *path);
}

// Tears a library down with its side's destructor body (no free).
void DestroyLib(difftest::Context &ctx, int side, cGZCOMLibrary *lib)
{
    Word<void **>(lib, 0) = gVt[side].lib;
    if (side == kOriginal) {
        ctx.Original<void(__thiscall *)(void *)>(kLibDtor)(lib);
    } else {
        lib->cGZCOMLibrary::~cGZCOMLibrary();
    }
}

} // namespace

DIFF_TEST(GZCOMLibrary_Construct, 0x144eff)
{
    InitVtables();
    for (const std::string &path : TestPaths()) {
        for (int fill : {0xCD, 0x00}) {
            cGZCOMLibrary *libs[2] = {RawLibrary(fill), RawLibrary(fill)};
            cRZString text;
            text.Construct(path.c_str());
            cGZCOMLibrary *rets[2];
            for (int side = 0; side < 2; ++side) rets[side] = ConstructLib(ctx, side, libs[side], text.AsInterface());
            ++ctx.cases;
            if (rets[0] != libs[0] || rets[1] != libs[1]) ctx.Fail("library construct: wrong return");
            ExpectSameBytes(ctx, "library construct", CanonLib(libs[0], kOriginal), CanonLib(libs[1], kOurs));
            ExpectSame(ctx, "library construct: path", libs[0]->mPath.mString, libs[1]->mPath.mString);
            for (int side = 0; side < 2; ++side) {
                DestroyLib(ctx, side, libs[side]);
                ::operator delete(libs[side]);
            }
            text.Destruct();
        }
    }
}

DIFF_TEST(GZCOMLibrary_CopyConstruct, 0x144ec2)
{
    std::mt19937 rng(11);
    for (const std::string &path : TestPaths()) {
        for (int fill : {0xCD, 0x00}) {
            Lib source(path.c_str(), kOurs, rng() & 1, reinterpret_cast<void *>(static_cast<uintptr_t>(rng())), reinterpret_cast<void *>(static_cast<uintptr_t>(rng())),
                       static_cast<int>(rng()));
            source->mPath.mRefCount = rng(); // the copy starts its count at 0
            cGZCOMLibrary *libs[2] = {RawLibrary(fill), RawLibrary(fill)};
            void *ra = ctx.Original<void *(__thiscall *)(void *, const void *)>(kLibCopy)(libs[0], source.p);
            void *rb = cGZCOMLibraryAdapters::CopyConstruct(libs[1], nullptr, *source.p);
            ++ctx.cases;
            if (ra != libs[0] || rb != libs[1]) ctx.Fail("library copy: wrong return");
            ExpectSameBytes(ctx, "library copy", CanonLib(libs[0], kOriginal), CanonLib(libs[1], kOurs));
            for (int side = 0; side < 2; ++side) {
                DestroyLib(ctx, side, libs[side]);
                ::operator delete(libs[side]);
            }
        }
    }
}

DIFF_TEST(GZCOMLibrary_Destruct, 0x1450b1)
{
    InitVtables();
    for (const std::string &path : TestPaths()) {
        cGZCOMLibrary *libs[2];
        for (int side = 0; side < 2; ++side) {
            libs[side] = RawLibrary(0xCD);
            cRZString text;
            text.Construct(path.c_str());
            ConstructLib(ctx, side, libs[side], text.AsInterface());
            text.Destruct();
            Word<void *>(libs[side], 0) = reinterpret_cast<void *>(0x1234); // must be reset
            libs[side]->mPath.mVTable = reinterpret_cast<void *>(0x5678);
        }
        void *heap[2] = {nullptr, nullptr};
        for (int side = 0; side < 2; ++side) {
            bool outOfLine = libs[side]->mPath.mString.capacity() > 15;
            if (outOfLine) heap[side] = Word(libs[side], 0x14);
            difftest::FreeRecorder rec;
            if (side == kOriginal) {
                ctx.Original<void(__thiscall *)(void *)>(kLibDtor)(libs[0]);
            } else {
                libs[1]->cGZCOMLibrary::~cGZCOMLibrary();
            }
            rec.Stop();
            if (outOfLine) {
                ++ctx.cases;
                if (!rec.Freed(heap[side])) ctx.Fail("library destruct (%s): the string block is still allocated", side ? "ours" : "original");
            }
        }
        std::vector<unsigned char> bytes[2];
        for (int side = 0; side < 2; ++side) {
            bytes[side].resize(sizeof(cGZCOMLibrary));
            std::memcpy(bytes[side].data(), libs[side], sizeof(cGZCOMLibrary));
            std::memset(bytes[side].data() + 0x14, 0, 4);
            uint32_t word = VtCode(Word<uint32_t>(libs[side], 0), side);
            std::memcpy(bytes[side].data(), &word, 4);
        }
        ExpectSameBytes(ctx, "library destruct", bytes[0], bytes[1]);
        ::operator delete(libs[0]);
        ::operator delete(libs[1]);
    }
}

DIFF_TEST(GZCOMLibrary_DeletingDestructor, 0x145133)
{
    InitVtables();
    for (uint32_t flags : {0u, 1u, 2u, 3u, 0x80000000u, 0xffffffffu}) {
        cGZCOMLibrary *libs[2];
        void *rets[2];
        bool live[2];
        for (int side = 0; side < 2; ++side) {
            libs[side] = RawLibrary(0xCD);
            cRZString text;
            text.Construct("name.dll");
            ConstructLib(ctx, side, libs[side], text.AsInterface());
            text.Destruct();
            difftest::FreeRecorder rec;
            rets[side] = CallLib<void *>(ctx, side, libs[side], 7, kLibDeleting, flags);
            rec.Stop();
            live[side] = !rec.Freed(libs[side]);
        }
        ++ctx.cases;
        if (rets[0] != libs[0] || rets[1] != libs[1]) ctx.Fail("library deleting destructor %x: wrong return", flags);
        if (live[0] != live[1] || live[0] != ((flags & 1) == 0)) ctx.Fail("library deleting destructor %x: freed original=%d ours=%d", flags, !live[0], !live[1]);
        if (!(flags & 1)) {
            ::operator delete(libs[0]);
            ::operator delete(libs[1]);
        }
    }
}

DIFF_TEST(GZCOMLibrary_QueryInterface, 0x145b46)
{
    for (uint32_t riid : {cIGZCOMLibrary::kIID, 1u, 0u, 2u, 0x07d4922fu, 0xffffffffu}) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            Lib lib("q.dll", side);
            InstallLibVtable(lib.p, side);
            gLog.clear();
            void *out = reinterpret_cast<void *>(0x5555);
            bool r = CallLib<bool>(ctx, side, lib.p, 0, kLibQI, riid, &out);
            results[side] = std::string(r ? "true " : "false ") + (out == lib.p ? "self " : out == reinterpret_cast<void *>(0x5555) ? "untouched " : "other ") + Joined(gLog);
        }
        ExpectSame(ctx, "library QueryInterface", results[0], results[1]);
    }
}

DIFF_TEST(GZCOMLibrary_Release, 0x145c14)
{
    std::mt19937 rng(13);
    for (int i = 0; i < 20; ++i) {
        int count = i == 0 ? 0 : i == 1 ? 1 : i == 2 ? INT32_MIN : static_cast<int>(rng());
        uint32_t r[2];
        int after[2];
        for (int side = 0; side < 2; ++side) {
            Lib lib("r", side, false, nullptr, nullptr, count);
            r[side] = CallLib<uint32_t>(ctx, side, lib.p, 2, kLibRelease);
            after[side] = lib->mRefCount;
        }
        ++ctx.cases;
        if (r[0] != r[1] || after[0] != after[1]) ctx.Fail("library Release(%d): %x/%d vs %x/%d", count, r[0], after[0], r[1], after[1]);
    }
}

namespace {

void *gCopyVt[40];
void __fastcall FakeCopy(void *, void *, const void *source)
{
    Log("copy +%x", static_cast<unsigned>(static_cast<const char *>(source) - reinterpret_cast<const char *>(gBase)));
}
void __fastcall FakeAssign(void *, void *, const void *source)
{
    Log("assign +%x", static_cast<unsigned>(static_cast<const char *>(source) - reinterpret_cast<const char *>(gBase)));
}

} // namespace

DIFF_TEST(GZCOMLibrary_GetPath, 0x14580d)
{
    std::memcpy(gCopyVt, ctx.image.At<void *>(cRZString::kVTable), sizeof(gCopyVt));
    gCopyVt[17] = reinterpret_cast<void *>(&FakeCopy);
    gCopyVt[16] = reinterpret_cast<void *>(&FakeAssign); // so a call to the neighbouring slot is visible
    std::string results[2];
    for (int side = 0; side < 2; ++side) {
        Lib lib("p.dll", side);
        gBase = reinterpret_cast<unsigned char *>(lib.p);
        struct {
            void **vt;
        } out{gCopyVt};
        gLog.clear();
        CallLib<void>(ctx, side, lib.p, 3, kLibGetPath, static_cast<void *>(&out));
        results[side] = Joined(gLog);
    }
    ExpectSame(ctx, "library GetPath", results[0], results[1]);
    // The real copy: the destination ends up with the path.
    for (int side = 0; side < 2; ++side) {
        Lib lib("real\\path.dll", side);
        cRZString out;
        out.Construct("old");
        CallLib<void>(ctx, side, lib.p, 3, kLibGetPath, static_cast<void *>(out.AsInterface()));
        results[side] = out.mString;
        out.Destruct();
    }
    ExpectSame(ctx, "library GetPath: copied text", results[0], results[1]);
}

DIFF_TEST(GZCOMLibrary_Less, 0x1450c6)
{
    const char *names[] = {"", "a", "A", "b", "ab", "abc", "a.dll", "a.dll ", "B", "plugins\\x.dll", "plugins\\y.dll", "Plugins\\x.dll"};
    for (const char *x : names) {
        for (const char *y : names) {
            Lib a0(x, kOriginal), b0(y, kOriginal), a1(x, kOurs), b1(y, kOurs);
            bool ra = ctx.Original<bool(__thiscall *)(const void *, const void *)>(kLibLess)(a0.p, b0.p);
            bool rb = *a1.p < *b1.p;
            ++ctx.cases;
            if (ra != rb) ctx.Fail("Less(\"%s\", \"%s\"): original %d ours %d", x, y, ra, rb);
        }
    }
}

DIFF_TEST(GZCOMLibrary_Unload, 0x145524)
{
    IatFakes iat(ctx);
    for (int init : {0, 1, 2}) {
        for (void *director : {static_cast<void *>(nullptr), static_cast<void *>(&gDirA)}) {
            for (void *module : {static_cast<void *>(nullptr), reinterpret_cast<void *>(0x1234)}) {
                for (bool throws : {false}) {
                    std::string results[2];
                    for (int side = 0; side < 2; ++side) {
                        Lib lib("unload.dll", side, init != 0, director, module);
                        Word<uint8_t>(lib.p, 4) = static_cast<uint8_t>(init == 2 ? 0x80 : init);
                        InstallPathVtable(ctx, lib.p);
                        gThrowOnFree = throws;
                        gLog.clear();
                        bool r = side == kOriginal ? ctx.Original<bool(__thiscall *)(void *)>(kLibUnload)(lib.p) : lib->Unload();
                        results[side] = std::string(r ? "true " : "false ") + DescribeLib(lib.p, side) + Joined(gLog);
                        lib->mPath.mVTable = GameAddress(cRZString::kVTable);
                    }
                    gThrowOnFree = false;
                    ExpectSame(ctx, "Unload", results[0], results[1]);
                }
            }
        }
    }
}

namespace {

struct InitScenario {
    const char *name;
    std::string path = "plugin.dll";
    DWORD version = 5;       // GetVersion (high bit: the ANSI loader)
    bool loadable = true;
    bool exportPresent = true;
    bool directorReturned = true;
    bool initResult = true;
    bool formatSucceeds = true;
    bool throwOnLoad = false;
    bool throwOnGetProc = false;
    bool throwOnFree = false;
    bool alreadyInitialized = false;
};

} // namespace

DIFF_TEST(GZCOMLibrary_Initialize, 0x1458b4)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    InitDirector(gDirA, "A");
    InitFramework();
    std::vector<InitScenario> scenarios;
    scenarios.push_back({"ok"});
    scenarios.push_back({"already", "plugin.dll", 5, true, true, true, true, true, false, false, false, true});
    scenarios.push_back({"ansi loader", "plugin.dll", 0x80000005u});
    scenarios.push_back({"no file", "missing.dll", 5, false});
    scenarios.push_back({"no file, no message", "missing.dll", 5, false, true, true, true, false});
    scenarios.push_back({"no file ansi", "missing.dll", 0x80000005u, false});
    scenarios.push_back({"no export", "plugin.dll", 5, true, false});
    scenarios.push_back({"no director", "plugin.dll", 5, true, true, false});
    scenarios.push_back({"director refuses", "plugin.dll", 5, true, true, true, false});
    scenarios.push_back({"utf8 path", "caf\xc3\xa9\xe2\x82\xac.dll"});
    scenarios.push_back({"utf8 path ansi", "caf\xc3\xa9.dll", 0x80000005u});
    scenarios.push_back({"long path", std::string(300, 'z') + ".dll"});
    scenarios.push_back({"empty path", ""});
    for (const InitScenario &s : scenarios) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLoadable.clear();
            if (s.loadable) gLoadable[s.path] = reinterpret_cast<void *>(0x1000);
            gExportPresent = s.exportPresent;
            gDirectorReturned = s.directorReturned ? static_cast<void *>(&gDirA) : nullptr;
            gDirInitResult = s.initResult;
            gVersion = s.version;
            gFormatSucceeds = s.formatSucceeds;
            gThrowOnLoad = s.throwOnLoad;
            gThrowOnGetProc = s.throwOnGetProc;
            gThrowOnFree = s.throwOnFree;
            // The ANSI loader gets the converted name; the fake maps it back by exact text, so the
            // non-ASCII cases register the converted spelling too.
            if (s.loadable && s.version & 0x80000000u) gLoadable[s.path] = reinterpret_cast<void *>(0x1000);
            Lib lib(s.path.c_str(), side, s.alreadyInitialized, nullptr, nullptr);
            gLog.clear();
            bool r = side == kOriginal ? ctx.Original<bool(__thiscall *)(void *)>(kLibInit)(lib.p) : lib->Initialize();
            results[side] = std::string(r ? "true " : "false ") + DescribeLib(lib.p, side) + Joined(gLog);
        }
        gThrowOnLoad = gThrowOnGetProc = gThrowOnFree = false;
        ExpectSame(ctx, s.name, results[0], results[1]);
    }
    gLoadable.clear();
    gVersion = 5;
}
