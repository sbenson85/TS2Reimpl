// The RVA hooks' adapters (cGZCOMAdapters, cGZCOMLibraryAdapters), called the way the game reaches them: on objects
// built by the original constructors (the game's vtables), against the original at the same RVA on a twin object.
// The adapter side's object carries copies of the game's vtables with the slot the adapter forwards to replaced by a
// trap of the slot's arity, so an adapter that dispatched virtually (in game: back into its own hook) logs TRAP and
// fails the comparison. See gz_com_support.h.
#include "free_recorder.h"
#include "gz_com_support.h"

namespace {

void __fastcall Trap0(void *, void *)
{
    Log("TRAP");
}
void __fastcall Trap1(void *, void *, uint32_t)
{
    Log("TRAP");
}
void __fastcall Trap2(void *, void *, uint32_t, uint32_t)
{
    Log("TRAP");
}
void __fastcall Trap3(void *, void *, uint32_t, uint32_t, uint32_t)
{
    Log("TRAP");
}
void *TrapOf(int arity)
{
    switch (arity) {
    case 0: return reinterpret_cast<void *>(&Trap0);
    case 1: return reinterpret_cast<void *>(&Trap1);
    case 2: return reinterpret_cast<void *>(&Trap2);
    default: return reinterpret_cast<void *>(&Trap3);
    }
}

void *gTrapVt[3][14];
void *gTrapLibVt[8];

// The game's vtables copied onto a game-built object, one slot trapped (face -1: none).
void InstallTrap(cGZCOM *com, int face, int slot, int arity)
{
    for (int f = 0; f < 3; ++f) {
        std::memcpy(gTrapVt[f], gVt[kOriginal].com[f], kFaceSlots[f] * sizeof(void *));
        Word<void **>(com, kFaceOffset[f]) = gTrapVt[f];
    }
    if (face >= 0) gTrapVt[face][slot] = TrapOf(arity);
}

void InstallLibTrap(cGZCOMLibrary *lib, int slot, int arity)
{
    std::memcpy(gTrapLibVt, gVt[kOriginal].lib, sizeof(gTrapLibVt));
    gTrapLibVt[slot] = TrapOf(arity);
    Word<void **>(lib, 0) = gTrapLibVt;
}

cGZCOM *BuildGameCom(difftest::Context &ctx)
{
    cGZCOM *com = MakeCom(ctx, kOriginal);
    cGZCOMLibrary *alpha = Add(com, kOriginal, "alpha.dll", true, &gDirA, reinterpret_cast<void *>(0x1000));
    Add(com, kOriginal, "beta.dll");
    Add(com, kOriginal, "gamma.dll", true, &gDirA, reinterpret_cast<void *>(0x2000), 2);
    com->mClasses.insert({5, cGZCOMClassEntry{1, alpha}});
    com->mFramework = FakeFrameworkPtr();
    com->mDirector = AsDirector(gDirB);
    InstallCritVtable(ctx, com);
    gCurrentCom = com;
    return com;
}

bool __cdecl CountingCallback(cIGZCOMLibrary *lib, void *context)
{
    Log("enum %s", PathOf(lib).c_str());
    return ++*static_cast<int *>(context) < 2;
}

using Thiscall0 = uint32_t(__thiscall *)(void *);
using Thiscall1 = uint32_t(__thiscall *)(void *, uint32_t);
using Thiscall2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Thiscall3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Thiscall4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);

// One adapter: its RVA, the adapter (ABI-identical to the thiscall slot), the slot it forwards to (face -1: a
// non-virtual member), the arguments.
struct ComCase {
    const char *name;
    uint32_t rva;
    void *adapter;
    int face, slot, arity;
    uint32_t args[4];
    int result; // 0: eax, 1: only al is defined, 2: none (a void slot: eax is a leftover)
};

} // namespace

DIFF_TEST(GZCOM_Adapters, 0x145b14)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    InitDirector(gDirA, "A");
    InitDirector(gDirB, "B");
    InitFramework();
    gExportPresent = true;
    gDirectorReturned = &gDirA;
    InitVtables();
    void *savedFramework = *ctx.image.At<void *>(kFrameworkGlobal);
    *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
    static void *out[8];
    static uint32_t count = 2, guids[4];
    static int budget = 0;
    cRZString path;
    path.Construct("new.dll");
    gLoadable["new.dll"] = reinterpret_cast<void *>(0x4000);
    gLoadable["beta.dll"] = reinterpret_cast<void *>(0x3000);
    const uint32_t pathArg = reinterpret_cast<uint32_t>(path.AsInterface());
    const ComCase cases[] = {
        {"QueryInterface", kQI, reinterpret_cast<void *>(&cGZCOMAdapters::QueryInterface), 0, 0, 2, {cIGZCOM::kIID, reinterpret_cast<uint32_t>(&out[0])}, 1},
        {"QueryInterface other", kQI, reinterpret_cast<void *>(&cGZCOMAdapters::QueryInterface), 0, 0, 2, {0x287fb697, reinterpret_cast<uint32_t>(&out[0])}, 1},
        {"Release", kRelease, reinterpret_cast<void *>(&cGZCOMAdapters::Release), 0, 2, 0, {}, 0},
        {"GetClassObject", kGetClassObject, reinterpret_cast<void *>(&cGZCOMAdapters::GetClassObject), 0, 3, 3, {7, 0x1234, reinterpret_cast<uint32_t>(&out[0])}, 1},
        {"GetClassObject 0x3e8", kGetClassObject, reinterpret_cast<void *>(&cGZCOMAdapters::GetClassObject), 0, 3, 3, {0x3e8, 0x1234, reinterpret_cast<uint32_t>(&out[0])}, 1},
        {"GetClassObject mapped", kGetClassObject, reinterpret_cast<void *>(&cGZCOMAdapters::GetClassObject), 0, 3, 3, {5, 0x1234, reinterpret_cast<uint32_t>(&out[0])}, 1},
        {"AddLibrary", kAddLibrary, reinterpret_cast<void *>(&cGZCOMAdapters::AddLibrary), 0, 5, 1, {pathArg}, 1},
        {"FreeUnusedLibraries", kFreeUnused, reinterpret_cast<void *>(&cGZCOMAdapters::FreeUnusedLibraries), 0, 6, 0, {}, 2},
        {"CreateGuids 0", kCreateGuids, reinterpret_cast<void *>(&cGZCOMAdapters::CreateGuids), 0, 8, 2, {reinterpret_cast<uint32_t>(guids), 0}, 1},
        {"RealInit", kRealInit, reinterpret_cast<void *>(&cGZCOMAdapters::RealInit), 0, 9, 0, {}, 1},
        {"RealShutdown", kRealShutdown, reinterpret_cast<void *>(&cGZCOMAdapters::RealShutdown), 0, 10, 0, {}, 2},
        {"SetServiceRunning", kSetRunning, reinterpret_cast<void *>(&cGZCOMAdapters::SetServiceRunning), 0, 11, 1, {0x80}, 2},
        {"EnumerateLibraries", kEnumCallback, reinterpret_cast<void *>(&cGZCOMAdapters::EnumerateLibraries), 0, 12, 2,
         {reinterpret_cast<uint32_t>(&CountingCallback), reinterpret_cast<uint32_t>(&budget)}, 2},
        {"EnumerateLibrariesToArray", kEnumArray, reinterpret_cast<void *>(&cGZCOMAdapters::EnumerateLibrariesToArray), 0, 13, 2,
         {reinterpret_cast<uint32_t>(out), reinterpret_cast<uint32_t>(&count)}, 0},
        {"CanUnloadLibrary", kCanUnload, reinterpret_cast<void *>(&cGZCOMAdapters::CanUnloadLibrary), -1, 0, 1, {0}, 1},
        {"UnloadLibraries", kUnloadAll, reinterpret_cast<void *>(&cGZCOMAdapters::UnloadLibraries), -1, 0, 0, {}, 2},
        {"InitLibrary", kInitLibrary, reinterpret_cast<void *>(&cGZCOMAdapters::InitLibrary), -1, 0, 1, {0}, 1},
        {"LoadAndGetClassObject", kLoadAndGet, reinterpret_cast<void *>(&cGZCOMAdapters::LoadAndGetClassObject), -1, 0, 4, {0, 0x44, 0x55, reinterpret_cast<uint32_t>(&out[0])}, 1},
    };
    for (const ComCase &c : cases) {
        std::string results[2];
        for (int route = 0; route < 2; ++route) {
            cGZCOM *com = BuildGameCom(ctx);
            if (route == 1) InstallTrap(com, c.face, c.slot, c.arity);
            uint32_t args[4] = {c.args[0], c.args[1], c.args[2], c.args[3]};
            // The helpers take a library: beta (not loaded) for the loaders, alpha for CanUnload.
            if (c.face < 0 && c.arity >= 1 && c.rva != kCanUnload) args[0] = reinterpret_cast<uint32_t>(FindLib(com, "beta.dll"));
            if (c.rva == kCanUnload) args[0] = reinterpret_cast<uint32_t>(FindLib(com, "alpha.dll"));
            std::memset(out, 0x5a, sizeof(out));
            count = 2;
            budget = 0;
            gRegisterScript = {{9, 1}};
            *ctx.image.At<uint8_t>(0x10caeb5) = 0; // GetModulePath looks its import up again on each route
            *ctx.image.At<void *>(0x10caeb8) = nullptr;
            gLog.clear();
            void *fn = route == 0 ? ctx.image.At<void>(c.rva) : c.adapter;
            uint32_t r = 0;
            switch (c.arity) {
            case 0: r = reinterpret_cast<Thiscall0>(fn)(com); break;
            case 1: r = reinterpret_cast<Thiscall1>(fn)(com, args[0]); break;
            case 2: r = reinterpret_cast<Thiscall2>(fn)(com, args[0], args[1]); break;
            case 3: r = reinterpret_cast<Thiscall3>(fn)(com, args[0], args[1], args[2]); break;
            default: r = reinterpret_cast<Thiscall4>(fn)(com, args[0], args[1], args[2], args[3]); break;
            }
            if (c.result == 1) r &= 0xff;
            if (c.result == 2) r = 0;
            char text[64];
            std::snprintf(text, sizeof(text), "ret=%x out=%s ", r, out[0] == reinterpret_cast<void *>(0x5a5a5a5a) ? "-" : "set");
            results[route] = text + Describe(com) + " | " + Joined(gLog);
            // Only the vtable words differ by design (the trap copy): restore before tearing down.
            gCurrentCom = nullptr;
            DestroyCom(ctx, com, kOriginal);
        }
        ExpectSame(ctx, c.name, results[0], results[1]);
    }
    gRegisterScript.clear();
    gLoadable.clear();
    path.Destruct();
    *ctx.image.At<void *>(kFrameworkGlobal) = savedFramework;
}

// The constructor, destructor and deleting destructor adapters on game-built objects (the deleting destructor is
// slot 3 of the +8 face: trapped there).
DIFF_TEST(GZCOM_LifecycleAdapters, 0x145075)
{
    InitVtables();
    for (uint32_t flags : {0u, 1u, 3u}) {
        cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOriginal)};
        Add(c[0], kOriginal, "z.dll");
        Add(c[1], kOriginal, "z.dll");
        InstallTrap(c[1], 2, 3, 1);
        gLog.clear();
        difftest::FreeRecorder rec0;
        void *r0 = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(c[0], flags);
        rec0.Stop();
        bool live0 = !rec0.Freed(c[0]);
        difftest::FreeRecorder rec1;
        void *r1 = cGZCOMAdapters::DeletingDestructor(c[1], nullptr, flags);
        rec1.Stop();
        bool live1 = !rec1.Freed(c[1]);
        ++ctx.cases;
        if (r0 != c[0] || r1 != c[1] || live0 != live1 || !gLog.empty()) ctx.Fail("deleting destructor adapter %x: freed %d/%d, log %s", flags, !live0, !live1, Joined(gLog).c_str());
        if (!(flags & 1)) {
            ExpectSameBytes(ctx, "deleting destructor adapter: bytes", Canon(c[0], kOriginal), Canon(c[1], kOurs));
            ::operator delete(c[0]);
            ::operator delete(c[1]);
        }
    }
    cGZCOM *c[2] = {MakeCom(ctx, kOriginal), MakeCom(ctx, kOriginal)};
    ctx.Original<void(__thiscall *)(void *)>(kDtor)(c[0]);
    cGZCOMAdapters::Destruct(c[1], nullptr);
    ExpectSameBytes(ctx, "destruct adapter: bytes", Canon(c[0], kOriginal), Canon(c[1], kOurs));
    ::operator delete(c[0]);
    ::operator delete(c[1]);
}

DIFF_TEST(GZCOMLibrary_Adapters, 0x145b46)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    InitDirector(gDirA, "A");
    InitFramework();
    InitVtables();
    gExportPresent = true;
    gDirectorReturned = &gDirA;
    gLoadable["lib.dll"] = reinterpret_cast<void *>(0x1000);
    std::string results[2];
    for (int route = 0; route < 2; ++route) {
        gLog.clear();
        std::string s;
        char text[96];
        // QueryInterface (slot 0), Release (2), GetPath (3): the slot trapped on the adapter route.
        {
            Lib lib("lib.dll", kOriginal, false, nullptr, nullptr, 4);
            void *got = nullptr;
            if (route == 1) InstallLibTrap(lib.p, 0, 2);
            bool q = route == 0 ? ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(kLibQI)(lib.p, cIGZCOMLibrary::kIID, &got)
                                : cGZCOMLibraryAdapters::QueryInterface(lib.p, nullptr, cIGZCOMLibrary::kIID, &got);
            if (route == 1) InstallLibTrap(lib.p, 2, 0);
            uint32_t rel = route == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(kLibRelease)(lib.p) : cGZCOMLibraryAdapters::Release(lib.p, nullptr);
            if (route == 1) InstallLibTrap(lib.p, 3, 1);
            cRZString outPath;
            outPath.Construct("old");
            if (route == 0) ctx.Original<void(__thiscall *)(void *, void *)>(kLibGetPath)(lib.p, outPath.AsInterface());
            else cGZCOMLibraryAdapters::GetPath(lib.p, nullptr, outPath.AsInterface());
            std::snprintf(text, sizeof(text), "qi=%d self=%d release=%x count=%d path=%s ", q ? 1 : 0, got == lib.p, rel, lib->mRefCount, outPath.mString.c_str());
            s += text;
            outPath.Destruct();
            Word<void **>(lib.p, 0) = gVt[kOriginal].lib;
        }
        // The ordering, Initialize and Unload (non-virtual), on game-vtable libraries.
        {
            Lib a("a.dll", kOriginal), b("b.dll", kOriginal), lib("lib.dll", kOriginal);
            bool less = route == 0 ? ctx.Original<bool(__thiscall *)(const void *, const void *)>(kLibLess)(a.p, b.p) : cGZCOMLibraryAdapters::Less(a.p, nullptr, *b.p);
            bool more = route == 0 ? ctx.Original<bool(__thiscall *)(const void *, const void *)>(kLibLess)(b.p, a.p) : cGZCOMLibraryAdapters::Less(b.p, nullptr, *a.p);
            bool init = route == 0 ? ctx.Original<bool(__thiscall *)(void *)>(kLibInit)(lib.p) : cGZCOMLibraryAdapters::Initialize(lib.p, nullptr);
            std::snprintf(text, sizeof(text), "less=%d/%d init=%d mod=%p ", less, more, init, lib->mModule);
            s += text;
            bool unload = route == 0 ? ctx.Original<bool(__thiscall *)(void *)>(kLibUnload)(lib.p) : cGZCOMLibraryAdapters::Unload(lib.p, nullptr);
            std::snprintf(text, sizeof(text), "unload=%d init=%d mod=%p ", unload, Word<uint8_t>(lib.p, 4), lib->mModule);
            s += text;
        }
        // The constructors and the destructors.
        {
            cRZString text2;
            text2.Construct("c.dll");
            auto *raw = RawObject<cGZCOMLibrary>(sizeof(cGZCOMLibrary), 0xcd);
            if (route == 0) ctx.Original<void *(__thiscall *)(void *, const cIGZString *)>(kLibCtor)(raw, text2.AsInterface());
            else cGZCOMLibraryAdapters::Construct(raw, nullptr, *text2.AsInterface());
            auto *copy = RawObject<cGZCOMLibrary>(sizeof(cGZCOMLibrary), 0xcd);
            if (route == 0) ctx.Original<void *(__thiscall *)(void *, const void *)>(kLibCopy)(copy, raw);
            else cGZCOMLibraryAdapters::CopyConstruct(copy, nullptr, *raw);
            std::snprintf(text, sizeof(text), "copy=%s vt=%x ", copy->mPath.mString.c_str(), VtCode(Word<uint32_t>(copy, 0), route));
            s += text;
            if (route == 0) ctx.Original<void(__thiscall *)(void *)>(kLibDtor)(raw);
            else cGZCOMLibraryAdapters::Destruct(raw, nullptr);
            if (route == 1) InstallLibTrap(copy, 7, 1);
            difftest::FreeRecorder rec;
            void *r = route == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kLibDeleting)(copy, 1) : cGZCOMLibraryAdapters::DeletingDestructor(copy, nullptr, 1);
            rec.Stop();
            std::snprintf(text, sizeof(text), "deleted=%d freed=%d", r == copy, rec.Freed(copy));
            s += text;
            ::operator delete(raw);
            text2.Destruct();
        }
        results[route] = s + " | " + Joined(gLog);
    }
    ExpectSame(ctx, "library adapters", results[0], results[1]);
    gLoadable.clear();
}
