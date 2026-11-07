// The application's free helpers (lane MB): ts/cTSAppFWHelpers.h.
#include "ts/cTSAppFWHelpers.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#define NOMINMAX
#include <windows.h>

#include <process.h>
#include <psapi.h>

namespace appfw {

namespace {

// Imports, called through the game's import table (the tests point these slots at fakes).
constexpr uint32_t kIatGetVersion = 0xd9a11c;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatGetCurrentProcess = 0xd9a1b8;
constexpr uint32_t kIatGetCurrentThread = 0xd9a190;
constexpr uint32_t kIatGetLastError = 0xd9a118;
constexpr uint32_t kIatOpenThreadToken = 0xd9a04c;
constexpr uint32_t kIatOpenProcessToken = 0xd9a05c;
constexpr uint32_t kIatDuplicateToken = 0xd9a014;
constexpr uint32_t kIatAllocateAndInitializeSid = 0xd9a018;
constexpr uint32_t kIatLocalAlloc = 0xd9a3e8;
constexpr uint32_t kIatLocalFree = 0xd9a130;
constexpr uint32_t kIatInitializeSecurityDescriptor = 0xd9a004;
constexpr uint32_t kIatGetLengthSid = 0xd9a00c;
constexpr uint32_t kIatInitializeAcl = 0xd9a008;
constexpr uint32_t kIatAddAccessAllowedAce = 0xd9a01c;
constexpr uint32_t kIatSetSecurityDescriptorDacl = 0xd9a050;
constexpr uint32_t kIatSetSecurityDescriptorGroup = 0xd9a038;
constexpr uint32_t kIatSetSecurityDescriptorOwner = 0xd9a034;
constexpr uint32_t kIatIsValidSecurityDescriptor = 0xd9a060;
constexpr uint32_t kIatAccessCheck = 0xd9a020;
constexpr uint32_t kIatFreeSid = 0xd9a010;
constexpr uint32_t kIatCloseHandle = 0xd9a17c;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

constexpr int kLanguageSlotId = 7;              // the language service's current language id
constexpr uint32_t kBodyShopNames = 0x10fb08c;  // twenty cRZString globals (the game's)
constexpr uint32_t kBodyShopNameStride = 0x20;

// The Body Shop name globals' index for a language id: 3..11 and 14..20 in order after the first, then 26, 28, 35;
// every other id has the first one.
uint32_t BodyShopNameIndex(uint32_t language)
{
    if (language >= 3 && language <= 11) return language - 2;
    if (language >= 14 && language <= 20) return language - 4;
    if (language == 26) return 17;
    if (language == 28) return 18;
    if (language == 35) return 19;
    return 0;
}

const char *EditionSuffix(uint32_t language)
{
    switch (language) {
    case 3: return " H\xe9ritage";
    case 4: return " Verm\xe4" "chtnis";
    case 6:
    case 14: return " Legado";
    case 7: return " Erfenis";
    case 10: return " Arv";
    case 16: return " Dziedzictwo";
    case 17:
    case 18: return " ??";
    case 19: return " ????";
    default: return " Legacy";
    }
}

// What IsUserAdministrator holds; everything still held is given back on every way out (the original's __finally,
// 0xc28b39).
struct AccessCheckResources {
    HANDLE token = nullptr;
    HANDLE impersonation = nullptr;
    PSID administrators = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;

    AccessCheckResources() = default;
    AccessCheckResources(const AccessCheckResources &) = delete;
    AccessCheckResources &operator=(const AccessCheckResources &) = delete;
    ~AccessCheckResources()
    {
        if (acl) Import<HLOCAL(WINAPI *)(HLOCAL)>(kIatLocalFree)(acl);
        if (descriptor) Import<HLOCAL(WINAPI *)(HLOCAL)>(kIatLocalFree)(descriptor);
        if (administrators) Import<PVOID(WINAPI *)(PSID)>(kIatFreeSid)(administrators);
        if (impersonation) Import<BOOL(WINAPI *)(HANDLE)>(kIatCloseHandle)(impersonation);
        if (token) Import<BOOL(WINAPI *)(HANDLE)>(kIatCloseHandle)(token);
    }
};

constexpr DWORD kTokenAccess = TOKEN_DUPLICATE | TOKEN_QUERY;
constexpr DWORD kCheckedAccess = 1;
constexpr DWORD kGrantedAccess = 3;
constexpr DWORD kPrivilegeSetSize = 0x14;

} // namespace

// ---- 0xc27961

void __cdecl AppendEditionSuffix(const cIGZString *source, cIGZString *out, uint32_t language)
{
    const int32_t two = source->Find("2", 0, true);
    out->Copy(*source);
    out->Resize(static_cast<uint32_t>(two + 1));
    out->Append(EditionSuffix(language));
}

// ---- 0xb417c0

bool __cdecl BodyShopProductName(cIGZString *out)
{
    const uint32_t language = VirtualCall<uint32_t(__thiscall *)(void *), kLanguageSlotId>(appinit::GetLanguageService());
    const auto *name = GameAddress<cRZString>(kBodyShopNames + kBodyShopNameStride * BodyShopNameIndex(language));
    cRZString copy;
    copy.Construct(*name, 0, 0xffffffffu);
    *out = *copy.AsInterface();
    copy.Destruct();
    return true;
}

// ---- 0xc27b49

uint32_t __cdecl ProcessPagefileUsage()
{
    using GetProcessMemoryInfoFn = BOOL(WINAPI *)(HANDLE, PROCESS_MEMORY_COUNTERS *, DWORD);
    static const HMODULE psapi = static_cast<int32_t>(Import<DWORD(WINAPI *)()>(kIatGetVersion)()) < 0
                                     ? nullptr
                                     : Import<HMODULE(WINAPI *)(const char *)>(kIatLoadLibraryA)("psapi.dll");
    static const auto getProcessMemoryInfo =
        psapi ? reinterpret_cast<GetProcessMemoryInfoFn>(Import<FARPROC(WINAPI *)(HMODULE, const char *)>(kIatGetProcAddress)(psapi, "GetProcessMemoryInfo"))
              : nullptr;
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!getProcessMemoryInfo) return 0;
    HANDLE process = Import<HANDLE(WINAPI *)()>(kIatGetCurrentProcess)();
    if (!getProcessMemoryInfo(process, &counters, counters.cb)) return 0;
    return static_cast<uint32_t>(counters.PagefileUsage);
}

// ---- 0x1b1a3f

intptr_t __cdecl SpawnAndWait(const char *path, const char *const *argv)
{
    return _spawnv(_P_WAIT, path, argv);
}

// ---- 0xc2896d

uint32_t __cdecl IsUserAdministrator()
{
    BOOL status = FALSE;
    if (static_cast<int32_t>(Import<DWORD(WINAPI *)()>(kIatGetVersion)()) < 0) return TRUE;

    AccessCheckResources held;
    HANDLE thread = Import<HANDLE(WINAPI *)()>(kIatGetCurrentThread)();
    if (!Import<BOOL(WINAPI *)(HANDLE, DWORD, BOOL, PHANDLE)>(kIatOpenThreadToken)(thread, kTokenAccess, TRUE, &held.token)) {
        if (Import<DWORD(WINAPI *)()>(kIatGetLastError)() != ERROR_NO_TOKEN) return status;
        HANDLE process = Import<HANDLE(WINAPI *)()>(kIatGetCurrentProcess)();
        if (!Import<BOOL(WINAPI *)(HANDLE, DWORD, PHANDLE)>(kIatOpenProcessToken)(process, kTokenAccess, &held.token)) return status;
    }
    if (!Import<BOOL(WINAPI *)(HANDLE, SECURITY_IMPERSONATION_LEVEL, PHANDLE)>(kIatDuplicateToken)(held.token, SecurityImpersonation,
                                                                                                    &held.impersonation))
        return status;

    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    using AllocateSidFn = BOOL(WINAPI *)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, PSID *);
    if (!Import<AllocateSidFn>(kIatAllocateAndInitializeSid)(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                                             0, &held.administrators))
        return status;

    using LocalAllocFn = HLOCAL(WINAPI *)(UINT, SIZE_T);
    held.descriptor = Import<LocalAllocFn>(kIatLocalAlloc)(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!held.descriptor) return status;
    if (!Import<BOOL(WINAPI *)(PSECURITY_DESCRIPTOR, DWORD)>(kIatInitializeSecurityDescriptor)(held.descriptor, SECURITY_DESCRIPTOR_REVISION))
        return status;

    const DWORD aclSize = Import<DWORD(WINAPI *)(PSID)>(kIatGetLengthSid)(held.administrators) + sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) -
                          sizeof(DWORD);
    held.acl = static_cast<PACL>(Import<LocalAllocFn>(kIatLocalAlloc)(LPTR, aclSize));
    if (!held.acl) return status;
    if (!Import<BOOL(WINAPI *)(PACL, DWORD, DWORD)>(kIatInitializeAcl)(held.acl, aclSize, ACL_REVISION2)) return status;
    if (!Import<BOOL(WINAPI *)(PACL, DWORD, DWORD, PSID)>(kIatAddAccessAllowedAce)(held.acl, ACL_REVISION2, kGrantedAccess, held.administrators))
        return status;
    if (!Import<BOOL(WINAPI *)(PSECURITY_DESCRIPTOR, BOOL, PACL, BOOL)>(kIatSetSecurityDescriptorDacl)(held.descriptor, TRUE, held.acl, FALSE))
        return status;
    // The group and the owner: their answers are not looked at.
    Import<BOOL(WINAPI *)(PSECURITY_DESCRIPTOR, PSID, BOOL)>(kIatSetSecurityDescriptorGroup)(held.descriptor, held.administrators, FALSE);
    Import<BOOL(WINAPI *)(PSECURITY_DESCRIPTOR, PSID, BOOL)>(kIatSetSecurityDescriptorOwner)(held.descriptor, held.administrators, FALSE);
    if (!Import<BOOL(WINAPI *)(PSECURITY_DESCRIPTOR)>(kIatIsValidSecurityDescriptor)(held.descriptor)) return status;

    GENERIC_MAPPING mapping{};
    mapping.GenericRead = 1;
    mapping.GenericWrite = 2;
    mapping.GenericExecute = 0;
    mapping.GenericAll = 3;
    alignas(PRIVILEGE_SET) uint8_t privileges[kPrivilegeSetSize];
    DWORD privilegesSize = kPrivilegeSetSize;
    DWORD granted = 0;
    using AccessCheckFn = BOOL(WINAPI *)(PSECURITY_DESCRIPTOR, HANDLE, DWORD, PGENERIC_MAPPING, PPRIVILEGE_SET, LPDWORD, LPDWORD, LPBOOL);
    if (!Import<AccessCheckFn>(kIatAccessCheck)(held.descriptor, held.impersonation, kCheckedAccess, &mapping,
                                                reinterpret_cast<PPRIVILEGE_SET>(privileges), &privilegesSize, &granted, &status))
        status = FALSE;
    return status;
}

} // namespace appfw
