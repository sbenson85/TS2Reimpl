// Contract tests for the Steam stub (src/platform/SteamStub.*, docs/contracts/steam.md) and the
// instance-mode helpers (src/platform/Instance.*): the original's own code runs against them.
//   Steam: the image's eight steam_api.dll import slots are pointed at the stub for the test's
//   duration (as the host does with [Steam] Stub=1), then the original's SteamInit, its two context
//   initialisers (through SteamInternal_ContextInit and the game's static contexts), the interface
//   slots it calls and its callback destructor are run.
//   Instance: the mutex renaming through the original cRZMutex constructor, and the added switches
//   through the original command-line parser (cRZCmdLine, as cTSAppFW reads activeUserDataDir).
#include "difftest.h"
#include "app/AppInit.h"
#include "app/WinMain.h"
#include "game/vtable.h"
#include "platform/Instance.h"
#include "platform/SteamStub.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZMutex.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace {

struct SteamImport {
    uint32_t rva;
    const char *name;
};
constexpr SteamImport kSteamImports[] = {
    {0xd9a6f8, "SteamAPI_RunCallbacks"},         {0xd9a6fc, "SteamAPI_RegisterCallback"},
    {0xd9a700, "SteamInternal_ContextInit"},     {0xd9a704, "SteamInternal_FindOrCreateUserInterface"},
    {0xd9a708, "SteamAPI_Shutdown"},             {0xd9a70c, "SteamInternal_SteamAPI_Init"},
    {0xd9a710, "SteamAPI_UnregisterCallback"},   {0xd9a714, "SteamAPI_GetHSteamUser"},
};
constexpr uint32_t kSteamInit = 0xc2a358, kUserContext = 0x1081a7c, kAppsContext = 0x1081a88;
constexpr uint32_t kCallbackDestructor = 0x4804d0; // CCallbackImpl<12> slot 3: unregisters when flagged
constexpr uint32_t kIatContextInit = 0xd9a700, kIatCreateMutexA = 0xd9a3ec;
constexpr uint32_t kCmdLineCtor = 0x1ae5a3, kCmdLineDtor = 0x14201d, kMutexCtor = 0xc6c85e, kMutexDtor = 0xc6c8c3;

// The stub in the image's import table, and the game's two static contexts saved, for one scope.
struct StubbedSteam {
    difftest::Context &ctx;
    void *saved[8];
    uint8_t contexts[2][12];
    explicit StubbedSteam(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 8; ++i) {
            void **slot = ctx.image.At<void *>(kSteamImports[i].rva);
            saved[i] = *slot;
            *slot = steamstub::Resolve(kSteamImports[i].name);
            if (!*slot) ctx.Fail("stub has no %s", kSteamImports[i].name);
        }
        std::memcpy(contexts[0], ctx.image.At<void>(kUserContext), 12);
        std::memcpy(contexts[1], ctx.image.At<void>(kAppsContext), 12);
    }
    ~StubbedSteam()
    {
        for (int i = 0; i < 8; ++i) *ctx.image.At<void *>(kSteamImports[i].rva) = saved[i];
        std::memcpy(ctx.image.At<void>(kUserContext), contexts[0], 12);
        std::memcpy(ctx.image.At<void>(kAppsContext), contexts[1], 12);
        steamstub::SetLanguage("english");
    }
    // What the game does: SteamInternal_ContextInit through its import slot, on its static context.
    void *Interface(uint32_t contextRva)
    {
        auto init = *ctx.image.At<void **(__cdecl *)(void *)>(kIatContextInit);
        void **result = init(ctx.image.At<void>(contextRva));
        return result ? *result : nullptr;
    }
};

const char *const kSteamLanguages[] = {"english", "french",   "brazilian", "czech",    "russian",  "hungarian", "german",
                                       "italian", "spanish",  "dutch",     "danish",   "swedish",  "norwegian", "finnish",
                                       "portuguese", "japanese", "polish", "tchinese", "schinese", "thai",      "koreana"};

} // namespace

DIFF_TEST(Steam_StubInit, 0xc2a358)
{
    StubbedSteam steam(ctx);
    using InitFn = uint32_t(__cdecl *)();
    uint32_t original = ctx.Original<InitFn>(kSteamInit)() & 0xff;
    uint32_t ours = winmain::SteamInit() & 0xff;
    if (original != 1 || ours != 1) ctx.Fail("SteamInit with the stub: original al=%u, ours al=%u (1 = Steam started)", original, ours);
    char message[1024];
    std::memset(message, 'x', sizeof(message));
    if (steamstub::SteamAPIInit("SteamUtils010", message) != 0 || message[0] != '\0') ctx.Fail("SteamAPI_Init: not OK, or the message is not cleared");
}

DIFF_TEST(Steam_StubUserContext, 0xc2a389)
{
    StubbedSteam steam(ctx);
    // WinMain: the user context (0x1081a7c, initialiser 0xc2a389 asks for SteamUser023), slot 2.
    void *user = steam.Interface(kUserContext);
    if (!user) {
        ctx.Fail("the user context gives no interface");
        return;
    }
    uint32_t id[2] = {0xdeadbeef, 0xdeadbeef};
    using GetSteamId = uint32_t *(__thiscall *)(void *, uint32_t *);
    uint32_t *r = VirtualSlot<GetSteamId>(user, 2)(user, id);
    if (r != id || id[0] != steamstub::kSteamIdLow || id[1] != steamstub::kSteamIdHigh)
        ctx.Fail("GetSteamID: %08x %08x (returned %p, buffer %p)", id[1], id[0], static_cast<void *>(r), static_cast<void *>(id));
    // A second ContextInit answers from the context without asking again: same object.
    if (steam.Interface(kUserContext) != user) ctx.Fail("the user context changed on a second ContextInit");
    // The context was filled by the game's own initialiser: counter set, interface stored.
    auto *data = ctx.image.At<void *>(kUserContext);
    if (data[2] != user || data[1] == nullptr) ctx.Fail("user context after init: counter %p, interface %p", data[1], data[2]);
}

DIFF_TEST(Steam_StubAppsLanguage, 0xc2a36e)
{
    StubbedSteam steam(ctx);
    void *apps = steam.Interface(kAppsContext);
    if (!apps) {
        ctx.Fail("the apps context gives no interface");
        return;
    }
    using GetLanguage = const char *(__thiscall *)(void *);
    // Default: English, which the game's table maps to 1.
    std::string name = VirtualSlot<GetLanguage>(apps, 4)(apps);
    if (name != "english" || appinit::LanguageIdFromName(name) != 1) ctx.Fail("default language <%s>", name.c_str());
    // Every name of the game's table comes back as set, and the game maps each to a known id.
    for (const char *l : kSteamLanguages) {
        steamstub::SetLanguage(l);
        name = VirtualSlot<GetLanguage>(apps, 4)(apps);
        if (name != l || appinit::LanguageIdFromName(name) < 0 || !steamstub::IsGameLanguage(l)) ctx.Fail("language %s: stub <%s>", l, name.c_str());
    }
    // A name the game does not list is passed on unchanged (the game maps it to -1, as with Steam).
    steamstub::SetLanguage("latam");
    name = VirtualSlot<GetLanguage>(apps, 4)(apps);
    if (name != "latam" || steamstub::IsGameLanguage("latam") || appinit::LanguageIdFromName(name) != -1) ctx.Fail("unlisted language <%s>", name.c_str());
    if (steamstub::FindOrCreateUserInterface(steamstub::kSteamUser, "SteamFriends017") != nullptr) ctx.Fail("an unknown interface version is answered");
}

DIFF_TEST(Steam_StubCallbacks, 0x4804d0)
{
    StubbedSteam steam(ctx);
    // cTSSimulator's GameOverlayActivated callback: registered (id 331), then the game's callback
    // destructor unregisters it because the stub set the registered flag.
    struct Callback {
        void *vtable;
        uint8_t flags;
        uint8_t pad[3];
        int32_t id;
        uint32_t guard;
    } cb{nullptr, 0, {0, 0, 0}, 0, 0x5a5a5a5a};
    auto reg = *ctx.image.At<void(__cdecl *)(void *, int32_t)>(0xd9a6fc);
    reg(&cb, 331);
    if (cb.flags != 1 || cb.id != 331) ctx.Fail("RegisterCallback: flags %u id %d", cb.flags, cb.id);
    using Dtor = void(__thiscall *)(void *, uint32_t);
    ctx.Original<Dtor>(kCallbackDestructor)(&cb, 0); // flags 0: destroy without freeing
    if (cb.flags & 1) ctx.Fail("the game's callback destructor did not unregister through the stub");
    if (cb.guard != 0x5a5a5a5a) ctx.Fail("the callback object was overwritten past its end");
    steamstub::RunCallbacks();
    steamstub::Shutdown();
    if (steamstub::GetHSteamUser() != steamstub::kSteamUser) ctx.Fail("GetHSteamUser");
}

// ---- instance mode ---------------------------------------------------------------------------

namespace {

int gMutexInstance = 0;
HANDLE WINAPI RenamingCreateMutexA(LPSECURITY_ATTRIBUTES a, BOOL owner, LPCSTR name)
{
    std::string renamed;
    if (instance::MutexNameForInstance(name, gMutexInstance, renamed)) return CreateMutexA(a, owner, renamed.c_str());
    return CreateMutexA(a, owner, name);
}

} // namespace

DIFF_TEST(Instance_MutexNames, 0xc6c85e)
{
    std::string r;
    if (instance::MutexNameForInstance("Global\\The Sims 2", 0, r) || instance::MutexNameForInstance(nullptr, 2, r) ||
        instance::MutexNameForInstance("Local\\Other", 2, r) || instance::MutexNameForInstance("Global\\The Sims", 2, r))
        ctx.Fail("a name that must stay was renamed");
    std::string a, b, c;
    instance::MutexNameForInstance("Global\\The Sims 2", 2, a);
    instance::MutexNameForInstance("Global\\The Sims 2", 3, b);
    instance::MutexNameForInstance("Global\\The Sims 2 Body Shop", 2, c);
    if (a == b || a == c || a.rfind("Global\\The Sims 2", 0) != 0 || a == "Global\\The Sims 2")
        ctx.Fail("instance names not distinct: <%s> <%s> <%s>", a.c_str(), b.c_str(), c.c_str());

    // The original cRZMutex constructor, with its CreateMutexA import renaming as the host does: the
    // mutex exists under the instance's name only.
    void **iat = ctx.image.At<void *>(kIatCreateMutexA);
    void *saved = *iat;
    *iat = reinterpret_cast<void *>(&RenamingCreateMutexA);
    void **closeSlot = ctx.image.At<void *>(0xd9a17c);
    void *savedClose = *closeSlot;
    *closeSlot = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "CloseHandle"));
    gMutexInstance = 7;
    const std::string base = "Global\\The Sims 2 DiffTest " + std::to_string(GetCurrentProcessId());
    std::string expected;
    instance::MutexNameForInstance(base.c_str(), 7, expected);
    cRZString name;
    name.Construct(base.c_str());
    auto *m = static_cast<cRZMutex *>(::operator new(cRZMutex::kSize));
    ctx.Original<cRZMutex *(__thiscall *)(void *, const void *)>(kMutexCtor)(m, name.AsInterface());
    HANDLE renamedHandle = OpenMutexA(SYNCHRONIZE, FALSE, expected.c_str());
    HANDLE plainHandle = OpenMutexA(SYNCHRONIZE, FALSE, base.c_str());
    if (!renamedHandle || plainHandle) ctx.Fail("instance mutex: renamed %p, plain %p", renamedHandle, plainHandle);
    if (renamedHandle) CloseHandle(renamedHandle);
    if (plainHandle) CloseHandle(plainHandle);
    ctx.Original<void(__thiscall *)(void *)>(kMutexDtor)(m); // closes through the CloseHandle slot set above
    ::operator delete(m);
    name.Destruct();
    gMutexInstance = 0;
    *iat = saved;
    *closeSlot = savedClose;
}

DIFF_TEST(Instance_CommandLine, 0x1ae5a3)
{
    const std::wstring dir = L"C:\\TS2\\local\\userdata-2\\", all = L"C:\\TS2\\local\\game-2\\allusers\\";
    if (!instance::IsUsableDataDir(dir) || instance::IsUsableDataDir(L"C:\\With Space\\") || instance::IsUsableDataDir(L"C:\\NoSlash") ||
        instance::IsUsableDataDir(L"relative\\") || instance::IsUsableDataDir(L"") || instance::IsUsableDataDir(L"C:\\q\"\\"))
        ctx.Fail("IsUsableDataDir");
    std::wstring line = instance::CommandLineForInstance(L"ts2host.exe -w", dir, all);
    if (line != L"ts2host.exe -w -activeUserDataDir:" + dir + L" -activeAllUsersDataDir:" + all) ctx.Fail("command line <%ls>", line.c_str());
    // An explicit switch is kept, not doubled; no directories, no change.
    if (instance::CommandLineForInstance(L"x -ACTIVEUSERDATADIR:D:\\a\\", dir, L"") != L"x -ACTIVEUSERDATADIR:D:\\a\\") ctx.Fail("explicit switch doubled");
    if (instance::CommandLineForInstance(L"x /activeAllUsersDataDir:D:\\a\\", L"", all) != L"x /activeAllUsersDataDir:D:\\a\\") ctx.Fail("explicit / switch doubled");
    if (instance::CommandLineForInstance(L"x", L"", L"") != L"x") ctx.Fail("no directories changed the line");

    // The game's parser (the original cRZCmdLine) finds both values as cTSAppFW asks for them
    // (slot 8, trimming quotes).
    std::string narrow(line.begin(), line.end());
    auto *cmd = static_cast<cRZCmdLine *>(::operator new(cRZCmdLine::kSize));
    ctx.Original<void *(__thiscall *)(void *, const char *)>(kCmdLineCtor)(cmd, narrow.c_str());
    for (const char *sw : {"activeUserDataDir", "activeAllUsersDataDir"}) {
        cRZString key, value;
        key.Construct(sw);
        value.Construct();
        using Find = bool(__thiscall *)(void *, const void *, void *, bool);
        bool found = VirtualSlot<Find>(cmd, 8)(cmd, key.AsInterface(), value.AsInterface(), true);
        std::string want = std::string(sw) == "activeUserDataDir" ? std::string(dir.begin(), dir.end()) : std::string(all.begin(), all.end());
        if (!found || want != value.ToChar()) ctx.Fail("%s: found %d value <%s>", sw, found, value.ToChar());
        key.Destruct();
        value.Destruct();
    }
    ctx.Original<void(__thiscall *)(void *)>(kCmdLineDtor)(cmd);
    ::operator delete(cmd);
}
