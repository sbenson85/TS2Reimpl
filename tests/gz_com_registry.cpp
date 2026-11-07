// cGZCOM with libraries: class lookup, AddLibrary and its helpers, unloading, enumeration, RealInit
// and RealShutdown (see gz_com_support.h). Slots go through our vtable; the helpers are our members.
#include "gz_com_support.h"

namespace {

// A COM object with four libraries and a few registered classes, the section's locks logged.
// alpha is loaded (director A), beta is not, gamma is loaded (module 0x2000, director A), delta is not.
cGZCOM *BuildCom(difftest::Context &ctx, int side)
{
    cGZCOM *com = MakeCom(ctx, side);
    cGZCOMLibrary *alpha = Add(com, side, "alpha.dll", true, &gDirA, reinterpret_cast<void *>(0x1000));
    cGZCOMLibrary *beta = Add(com, side, "beta.dll");
    cGZCOMLibrary *gamma = Add(com, side, "gamma.dll", true, &gDirA, reinterpret_cast<void *>(0x2000), 2);
    Add(com, side, "delta.dll");
    com->mClasses.insert({5, cGZCOMClassEntry{1, alpha}});
    com->mClasses.insert({7, cGZCOMClassEntry{3, beta}});
    com->mClasses.insert({9, cGZCOMClassEntry{2, gamma}});
    com->mClasses.insert({0x3e8, cGZCOMClassEntry{1, alpha}});
    com->mFramework = FakeFrameworkPtr();
    com->mDirector = AsDirector(gDirB);
    InstallCritVtable(ctx, com);
    gCurrentCom = com;
    return com;
}

void FinishCom(difftest::Context &ctx, cGZCOM *com, int side)
{
    gCurrentCom = nullptr;
    DestroyCom(ctx, com, side);
}

void SetupDirectors()
{
    InitDirector(gDirA, "A");
    InitDirector(gDirB, "B");
    InitFramework();
}

} // namespace

DIFF_TEST(GZCOM_GetClassObject, 0x145702)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    SetupDirectors();
    // The framework answers through its own vtable.
    for (uint32_t clsid : {5u, 6u, 7u, 9u, 0x3e8u, 0x3e9u, 0u, 0xffffffffu}) {
        for (bool dirResult : {true, false}) {
            for (bool loadOk : {true, false}) {
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    cGZCOM *com = BuildCom(ctx, side);
                    gGetClassResult = dirResult;
                    gLoadable.clear();
                    if (loadOk) gLoadable["beta.dll"] = reinterpret_cast<void *>(0x3000);
                    gExportPresent = true;
                    gDirectorReturned = &gDirA;
                    gLog.clear();
                    void *out = nullptr;
                    // Only the low byte is compared: the result is the callee's bool.
                    bool r = Call<bool>(ctx, side, com, 0, 3, kGetClassObject, clsid, 0x1234u, &out);
                    results[side] = std::string(r ? "true " : "false ") + Describe(com) + " | " + Joined(gLog);
                    FinishCom(ctx, com, side);
                }
                char what[64];
                std::snprintf(what, sizeof(what), "GetClassObject %x dir=%d load=%d", clsid, dirResult, loadOk);
                ExpectSame(ctx, what, results[0], results[1]);
            }
        }
    }
    gLoadable.clear();
    gGetClassResult = true;
}

DIFF_TEST(GZCOM_LoadAndGetClassObject, 0x1457d4)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    SetupDirectors();
    for (const char *name : {"alpha.dll", "beta.dll", "gamma.dll", "delta.dll"}) {
        for (bool loadOk : {true, false}) {
            for (bool dirOk : {true, false}) {
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    cGZCOM *com = BuildCom(ctx, side);
                    gLoadable.clear();
                    if (loadOk) gLoadable[name] = reinterpret_cast<void *>(0x3000);
                    gDirectorReturned = &gDirA;
                    gGetClassResult = dirOk;
                    cGZCOMLibrary *lib = FindLib(com, name);
                    gLog.clear();
                    void *out = nullptr;
                    bool r = side == kOriginal ? ctx.Original<bool(__thiscall *)(void *, void *, uint32_t, uint32_t, void **)>(kLoadAndGet)(com, lib, 0x44, 0x55, &out)
                                               : com->LoadAndGetClassObject(lib, 0x44, 0x55, &out);
                    // Only the low byte of the result is defined.
                    results[side] = std::string(r ? "true " : "false ") + Describe(com) + " | " + Joined(gLog);
                    FinishCom(ctx, com, side);
                }
                ExpectSame(ctx, name, results[0], results[1]);
            }
        }
    }
    gLoadable.clear();
    gGetClassResult = true;
}

DIFF_TEST(GZCOM_RegisterClassCallback, 0x145152)
{
    for (uint32_t clsid : {5u, 6u, 7u, 9u, 0x3e8u}) {
        for (uint32_t version : {0u, 1u, 2u, 3u, 4u, 0xffffffffu}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                com->mCurrentLibrary = FindLib(com, "delta.dll");
                if (side == kOriginal) {
                    ctx.Original<void(__cdecl *)(uint32_t, uint32_t, void *)>(kRegisterCallback)(clsid, version, com);
                } else {
                    cGZCOM::RegisterClassCallback(clsid, version, com);
                }
                results[side] = Describe(com);
                FinishCom(ctx, com, side);
            }
            char what[64];
            std::snprintf(what, sizeof(what), "RegisterClassCallback %x v%u", clsid, version);
            ExpectSame(ctx, what, results[0], results[1]);
        }
    }
}

namespace {

// Runs a script of AddLibrary calls on one side and describes everything that happened.
std::string AddLibraryScript(difftest::Context &ctx, int side)
{
    cGZCOM *com = MakeCom(ctx, side);
    InstallCritVtable(ctx, com);
    gCurrentCom = com;
    std::string out;
    struct Step {
        const char *path;
        bool loadable;
        bool initResult;
        std::vector<std::pair<uint32_t, uint32_t>> classes;
    };
    const Step steps[] = {
        {"a.dll", true, true, {{1, 5}, {2, 1}}},
        {"a.dll", true, true, {{1, 9}}},   // already registered: no second initialisation
        {"b.dll", true, true, {{1, 3}, {3, 2}}}, // older version of class 1: kept at a.dll
        {"c.dll", false, true, {}},              // cannot be loaded: stays in the set, uninitialised
        {"d.dll", true, false, {{4, 1}}},        // the director refuses: stays in the set, uninitialised
        {"A.dll", true, true, {{1, 7}}},         // different spelling: another library, newer class 1
        {"", true, true, {{9, 1}}},
        {"a.dll", true, true, {}},
    };
    for (const Step &step : steps) {
        gLoadable.clear();
        if (step.loadable) gLoadable[step.path] = reinterpret_cast<void *>(0x1000 + out.size());
        gDirInitResult = step.initResult;
        gRegisterScript = step.classes;
        gLog.clear();
        cRZString text;
        text.Construct(step.path);
        bool r = Call<bool>(ctx, side, com, 0, 5, kAddLibrary, static_cast<const cIGZString *>(text.AsInterface()));
        text.Destruct();
        out += std::string(r ? "true " : "false ") + Describe(com) + " | " + Joined(gLog) + "\n";
        // The libraries the code inserted carry the side's own library vtable.
        for (auto &lib : com->mLibraries) {
            if (Word<void **>(&lib, 0) != gVt[side].lib) out += "foreign library vtable\n";
        }
    }
    gRegisterScript.clear();
    gDirInitResult = true;
    gLoadable.clear();
    FinishCom(ctx, com, side);
    return out;
}

} // namespace

DIFF_TEST(GZCOM_AddLibrary, 0x1451a3)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    SetupDirectors();
    gExportPresent = true;
    gDirectorReturned = &gDirA;
    std::string a = AddLibraryScript(ctx, kOriginal);
    std::string b = AddLibraryScript(ctx, kOurs);
    ExpectSame(ctx, "AddLibrary script", a, b);
    // Without a director the library is still added.
    gDirectorReturned = nullptr;
    a = AddLibraryScript(ctx, kOriginal);
    b = AddLibraryScript(ctx, kOurs);
    ExpectSame(ctx, "AddLibrary script, no director", a, b);
    gDirectorReturned = &gDirA;
}

DIFF_TEST(GZCOM_InitLibrary, 0x145c2c)
{
    IatFakes iat(ctx);
    FakeStaticDirector staticDirector(ctx);
    SetupDirectors();
    gExportPresent = true;
    gDirectorReturned = &gDirA;
    for (const char *name : {"alpha.dll", "beta.dll", "gamma.dll", "delta.dll"}) {
        for (bool loadOk : {true, false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                gLoadable.clear();
                if (loadOk) gLoadable[name] = reinterpret_cast<void *>(0x3000);
                gRegisterScript = {{5, 8}, {0x70, 1}};
                cGZCOMLibrary *lib = FindLib(com, name);
                gLog.clear();
                bool r = side == kOriginal ? ctx.Original<bool(__thiscall *)(void *, void *)>(kInitLibrary)(com, lib) : com->InitLibrary(lib);
                results[side] = std::string(r ? "true " : "false ") + Describe(com) + " | " + Joined(gLog);
                FinishCom(ctx, com, side);
            }
            ExpectSame(ctx, name, results[0], results[1]);
        }
    }
    gRegisterScript.clear();
    gLoadable.clear();
}

DIFF_TEST(GZCOM_CanUnloadLibrary, 0x1452ff)
{
    SetupDirectors(); // the libraries' director (gDirA) is asked through its vtable: set up here, not by an earlier test
    for (const char *name : {"alpha.dll", "beta.dll", "gamma.dll"}) {
        for (bool answer : {true, false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                gCanUnloadResult = answer;
                cGZCOMLibrary *lib = FindLib(com, name);
                gLog.clear();
                bool r = side == kOriginal ? ctx.Original<bool(__thiscall *)(void *, void *)>(kCanUnload)(com, lib) : com->CanUnloadLibrary(lib);
                results[side] = std::string(r ? "true " : "false ") + Describe(com) + " | " + Joined(gLog);
                FinishCom(ctx, com, side);
            }
            ExpectSame(ctx, name, results[0], results[1]);
        }
    }
    gCanUnloadResult = true;
}

DIFF_TEST(GZCOM_FreeUnusedLibraries, 0x1455a6)
{
    IatFakes iat(ctx);
    SetupDirectors();
    for (bool answer : {true, false}) {
        for (bool throws : {false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                gCanUnloadResult = answer;
                gThrowOnFree = throws;
                gLog.clear();
                Call<void>(ctx, side, com, 0, 6, kFreeUnused);
                results[side] = Describe(com) + " | " + Joined(gLog);
                FinishCom(ctx, com, side);
            }
            gThrowOnFree = false;
            ExpectSame(ctx, "FreeUnusedLibraries", results[0], results[1]);
        }
    }
    // An empty set: only the lock pair.
    std::string results[2];
    for (int side = 0; side < 2; ++side) {
        cGZCOM *com = MakeCom(ctx, side);
        InstallCritVtable(ctx, com);
        gLog.clear();
        Call<void>(ctx, side, com, 0, 6, kFreeUnused);
        results[side] = Joined(gLog);
        DestroyCom(ctx, com, side);
    }
    ExpectSame(ctx, "FreeUnusedLibraries (empty)", results[0], results[1]);
    gCanUnloadResult = true;
}

DIFF_TEST(GZCOM_UnloadLibraries, 0x145556)
{
    IatFakes iat(ctx);
    SetupDirectors();
    for (bool throws : {false}) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            cGZCOM *com = BuildCom(ctx, side);
            gThrowOnFree = throws;
            gLog.clear();
            if (side == kOriginal) {
                ctx.Original<void(__thiscall *)(void *)>(kUnloadAll)(com);
            } else {
                com->UnloadLibraries();
            }
            results[side] = Describe(com) + " | " + Joined(gLog);
            FinishCom(ctx, com, side);
        }
        gThrowOnFree = false;
        ExpectSame(ctx, "UnloadLibraries", results[0], results[1]);
    }
}

namespace {

bool __cdecl EnumCallback(cIGZCOMLibrary *lib, void *context)
{
    int *budget = static_cast<int *>(context);
    Log("enum %s budget=%d", PathOf(lib).c_str(), *budget);
    return --*budget > 0;
}

} // namespace

DIFF_TEST(GZCOM_EnumerateLibrariesCallback, 0x1454e7)
{
    for (int budget : {0, 1, 2, 3, 4, 5, 100}) {
        for (bool hasCallback : {true, false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                int count = budget;
                gLog.clear();
                void *callback = hasCallback ? reinterpret_cast<void *>(&EnumCallback) : nullptr;
                Call<uint32_t>(ctx, side, com, 0, 12, kEnumCallback, callback, static_cast<void *>(&count));
                // The return value is a stray register in the original; not compared.
                results[side] = Describe(com) + " | " + Joined(gLog) + " budget=" + std::to_string(count);
                FinishCom(ctx, com, side);
            }
            ExpectSame(ctx, "EnumerateLibraries(callback)", results[0], results[1]);
        }
    }
    // An empty set: the callback is never called.
    for (int side = 0; side < 2; ++side) {
        cGZCOM *com = MakeCom(ctx, side);
        int count = 3;
        gLog.clear();
        Call<uint32_t>(ctx, side, com, 0, 12, kEnumCallback, reinterpret_cast<void *>(&EnumCallback), static_cast<void *>(&count));
        ++ctx.cases;
        if (!gLog.empty() || count != 3) ctx.Fail("EnumerateLibraries(callback) on an empty set called the callback (%s)", side ? "ours" : "original");
        DestroyCom(ctx, com, side);
    }
}

DIFF_TEST(GZCOM_EnumerateLibrariesArray, 0x14548b)
{
    for (uint32_t request : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0x80000000u, 0xffffffffu}) {
        for (bool withArray : {true, false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                InstallAllLibVtables(com, side);
                void *array[8];
                for (void *&slot : array) slot = reinterpret_cast<void *>(0x5a5a5a5a);
                uint32_t count = request;
                gLog.clear();
                uint32_t r = Call<uint32_t>(ctx, side, com, 0, 13, kEnumArray, withArray ? array : static_cast<void **>(nullptr), &count);
                std::string text = "ret=" + std::to_string(r) + " count=" + std::to_string(count) + " ";
                for (void *slot : array) {
                    if (slot == reinterpret_cast<void *>(0x5a5a5a5a)) text += "- ";
                    else text += PathOf(slot) + " ";
                }
                results[side] = text + Joined(gLog);
                FinishCom(ctx, com, side);
            }
            char what[64];
            std::snprintf(what, sizeof(what), "EnumerateLibraries(array) %x array=%d", request, withArray);
            ExpectSame(ctx, what, results[0], results[1]);
        }
    }
}

DIFF_TEST(GZCOM_RealInit, 0x145b7e)
{
    FakeStaticDirector staticDirector(ctx);
    SetupDirectors();
    void *savedFramework = *ctx.image.At<void *>(kFrameworkGlobal);
    *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
    uint8_t *resolved = ctx.image.At<uint8_t>(0x10caeb5);
    void **cached = ctx.image.At<void *>(0x10caeb8);
    uint8_t savedResolved = *resolved;
    void *savedCached = *cached;
    for (bool already : {false, true}) {
        for (bool initOk : {true, false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = MakeCom(ctx, side);
                gCurrentCom = com;
                com->mInitialized = already;
                com->mFramework = nullptr;
                com->mDirector = nullptr;
                gDirInitResult = initOk;
                *resolved = 0;
                *cached = nullptr;
                gLog.clear();
                bool r = Call<bool>(ctx, side, com, 0, 9, kRealInit);
                results[side] = std::string(r ? "true " : "false ") + Describe(com) + " | " + Joined(gLog);
                // The framework and director pointers are the fakes.
                ++ctx.cases;
                if (!already && (static_cast<void *>(com->mFramework) != &gFramework || static_cast<void *>(com->mDirector) != gStaticDirector))
                    ctx.Fail("RealInit: wrong framework/director pointers");
                FinishCom(ctx, com, side);
            }
            ExpectSame(ctx, "RealInit", results[0], results[1]);
        }
    }
    gDirInitResult = true;
    *resolved = savedResolved;
    *cached = savedCached;
    *ctx.image.At<void *>(kFrameworkGlobal) = savedFramework;
}

DIFF_TEST(GZCOM_RealShutdown, 0x145be4)
{
    IatFakes iat(ctx);
    SetupDirectors();
    for (bool initialized : {false, true}) {
        for (bool throws : {false}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                cGZCOM *com = BuildCom(ctx, side);
                com->mInitialized = initialized;
                gThrowOnFree = throws;
                gLog.clear();
                Call<void>(ctx, side, com, 0, 10, kRealShutdown);
                results[side] = Describe(com) + " | " + Joined(gLog);
                FinishCom(ctx, com, side);
            }
            gThrowOnFree = false;
            ExpectSame(ctx, "RealShutdown", results[0], results[1]);
        }
    }
}
