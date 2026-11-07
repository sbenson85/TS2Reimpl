// The network manager service, cRZNetManager (lane PZ). The original is speed-built (/O2) code: a .cpp.
#define _CRT_SECURE_NO_WARNINGS // the original uses the unchecked strncpy / strcpy-style copies: so do we
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include "rz/cRZNetManager.h"

#include "game/game.h"
#include "game/import.h"
#include "game/original.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZString.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"


#include <cstring>
#include <ctime>
#include <new>

namespace {

// ---- the import slots the original calls through ----------------------------------------------------------------------

constexpr uint32_t kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124, kIatFreeLibrary = 0xd9a120;
constexpr uint32_t kIatGlobalAlloc = 0xd9a0f8, kIatGlobalFree = 0xd9a0f4;
constexpr uint32_t kIatRegOpenKeyExA = 0xd9a028, kIatRegCloseKey = 0xd9a02c, kIatRegQueryValueExA = 0xd9a054;
constexpr uint32_t kIatRegSetValueExA = 0xd9a024;
constexpr uint32_t kIatInetNtoa = 0xd9a684, kIatWSAStartup = 0xd9a688, kIatWSACleanup = 0xd9a68c;
constexpr uint32_t kIatWSAGetLastError = 0xd9a6a0, kIatGetHostName = 0xd9a6a4, kIatGetHostByName = 0xd9a6a8;
constexpr uint32_t kIatSocket = 0xd9a6ac, kIatCloseSocket = 0xd9a6b0;

constexpr char kLibraryName[] = "rasapi32.dll";
constexpr const char *kFunctionNames[7] = {"RasEnumConnectionsA", "RasEnumEntriesA", "RasGetProjectionInfoA", "RasGetEntryDialParamsA",
                                           "RasDialA",            "RasGetConnectStatusA", "RasHangUpA"};

// Indices into cRZRasFunctions::mFunctions.
enum Ras : int { kEnumConnections, kEnumEntries, kGetProjectionInfo, kGetEntryDialParams, kDial, kGetConnectStatus, kHangUp };

// rasapi32's functions as the table holds them (all stdcall; handles and pointers are 32-bit here).
using EnumConnectionsFn = DWORD(WINAPI *)(void *connections, DWORD *bytes, DWORD *count);
using EnumEntriesFn = DWORD(WINAPI *)(const char *reserved, const char *phonebook, void *entries, DWORD *bytes, DWORD *count);
using GetProjectionInfoFn = DWORD(WINAPI *)(void *connection, uint32_t projection, void *info, DWORD *bytes);
using GetEntryDialParamsFn = DWORD(WINAPI *)(const char *phonebook, void *params, BOOL *passwordSaved);
using DialFn = DWORD(WINAPI *)(void *extensions, const char *phonebook, void *params, DWORD notifierType, void *notifier,
                               void **connection);
using GetConnectStatusFn = DWORD(WINAPI *)(void *connection, void *status);
using HangUpFn = DWORD(WINAPI *)(void *connection);

template <typename Fn> Fn RasFunction(const cRZRasFunctions *ras, Ras which) { return reinterpret_cast<Fn>(ras->mFunctions[which]); }

constexpr uint32_t kProjectionIp = 0x8021;       // RASP_PppIp
constexpr uint32_t kConnectionBytes = 0x2d0;     // sizeof(RASCONNA) in this build
constexpr uint32_t kConnectionSlots = 0x14;
constexpr uint32_t kProjectionBytes = 0x30;      // RASPPPIPA: dwSize, dwError, szIpAddress[16], ...
constexpr uint32_t kEntryNameBytes = 0x214;      // RASENTRYNAMEA in this build
constexpr uint32_t kDialParamBytes = 0x42c;      // RASDIALPARAMSA in this build
constexpr uint32_t kStatusBytes = 0x14c;         // RASCONNSTATUSA
constexpr uint32_t kStateConnected = 0x2000;     // RASCS_Connected
constexpr uint32_t kStateDisconnected = 0x2001;  // RASCS_Disconnected
constexpr uint32_t kErrorBufferTooSmall = 0x25b; // ERROR_BUFFER_TOO_SMALL
constexpr uint32_t kMessageDial = 0xcccd;        // the RasDial notifier's WM_RASDIALEVENT
constexpr uint32_t kStateDeviceConnected = 3;    // RASCS_DeviceConnected: the dial is under way

// The message types the dial sends its target: under way, and the result (data 1: connected).
constexpr uint32_t kMessageDialing = 0x65783920;
constexpr uint32_t kMessageDialResult = 0x65783921;

// Offsets in the dial parameters (RASDIALPARAMSA): the first byte of each text field, and the two dwords at the end.
constexpr uint32_t kParamEntry = 4, kParamPhone = 0x105, kParamCallback = 0x186, kParamUser = 0x207, kParamPassword = 0x308,
                   kParamDomain = 0x409, kParamSubEntry = 0x41c, kParamCallbackId = 0x420;

// Where the dial parameters are built: the size, the first byte of every text field empty, the two trailing dwords zero.
// The other bytes are left as they are (the original builds this on the stack and leaves them).
void StartDialParams(uint8_t *params)
{
    *reinterpret_cast<uint32_t *>(params) = kDialParamBytes;
    params[kParamEntry] = 0;
    params[kParamPhone] = 0;
    params[kParamCallback] = 0;
    params[kParamUser] = 0;
    params[kParamPassword] = 0;
    params[kParamDomain] = 0;
    *reinterpret_cast<uint32_t *>(params + kParamSubEntry) = 0;
    *reinterpret_cast<uint32_t *>(params + kParamCallbackId) = 0;
}

// Waits until three seconds have passed on the clock (the original's busy loop after a hang-up; it spins on _time64). The
// clock is read as a 64-bit time; a negative one is read again, and the wait ends when the low 32 bits reach the start's + 3
// or the time is past 32 bits.
void WaitThreeSeconds()
{
    const uint32_t start = static_cast<uint32_t>(_time64(nullptr));
    uint64_t now;
    do {
        do {
            now = static_cast<uint64_t>(_time64(nullptr));
        } while (static_cast<int64_t>(now) < 0);
    } while (static_cast<int64_t>(now) < 0x100000000LL && static_cast<uint32_t>(now) < start + 3u);
}

// A new standard message (RZMemory 0x2c bytes, counted once) of `type` with its first data word `data`. Used unchecked for
// a failed allocation, as the original does.
cIGZMessage2Standard *NewMessage(uint32_t type, uint32_t data)
{
    auto *const memory = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    auto *const message = reinterpret_cast<cIGZMessage2Standard *>(memory ? memory->Construct() : nullptr);
    if (message) message->AddRef();
    message->SetType(type);
    message->SetData1(data);
    return message;
}

// RegOpenKeyEx on the Internet Settings key; returns the key or null.
HKEY OpenInternetSettings()
{
    HKEY key = nullptr;
    const LSTATUS status = ImportFunction<decltype(&RegOpenKeyExA), kIatRegOpenKeyExA>()(
        HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0, 0xf003f, &key);
    return status == ERROR_SUCCESS ? key : nullptr;
}

// ---- the connections RAS reports ----------------------------------------------------------------------------------

// One RasEnumConnections call: the connection records (0x2d0 bytes each, the first one's size set) and their number.
struct Connections {
    alignas(4) uint8_t mRecords[kConnectionSlots * kConnectionBytes];
    DWORD mBytes = kConnectionSlots * kConnectionBytes;
    DWORD mCount = kConnectionSlots;
    // True when the call succeeded (and has records).
    bool Enumerate(EnumConnectionsFn enumerate)
    {
        *reinterpret_cast<uint32_t *>(mRecords) = kConnectionBytes;
        return enumerate(mRecords, &mBytes, &mCount) == 0 && mCount != 0;
    }
    void *Handle(uint32_t index) const
    {
        return *reinterpret_cast<void *const *>(mRecords + index * kConnectionBytes + 4);
    }
};

// RasGetProjectionInfo(PPP IP) on a connection: the IP projection record (0x30 bytes: size, error, the address text at +8).
struct Projection {
    alignas(4) uint8_t mBytes[kProjectionBytes];
    DWORD mSize = kProjectionBytes;
    DWORD Query(GetProjectionInfoFn query, void *connection)
    {
        *reinterpret_cast<uint32_t *>(mBytes) = kProjectionBytes;
        mSize = kProjectionBytes;
        return query(connection, kProjectionIp, mBytes, &mSize);
    }
    uint32_t Error() const { return *reinterpret_cast<const uint32_t *>(mBytes + 4); }
    const char *Address() const { return reinterpret_cast<const char *>(mBytes + 8); }
};

} // namespace

// ---- the connection state block -----------------------------------------------------------------------------------

// 0xc65a40.
cRZNetConnectionState *cRZNetConnectionState::Clear()
{
    std::memset(mBytes + 0x18c, 0, 4);
    std::memset(mBytes, 0, 4);
    std::memset(mBytes + 0x186, 0, 4);
    std::memset(mBytes + 4, 0, 0x102);
    std::memset(mBytes + 0x105, 0, 0x82);
    return this;
}

// ---- construction -----------------------------------------------------------------------------------------------------

// 0xc65932. The 0x24-byte table is not checked for allocation failure (the zeroing would fault on null). The padding bytes
// (+0x25..0x27, +0x1c3) and the parts of the WSADATA block Clear leaves are not touched.
cRZNetManager::cRZNetManager() : rz::cRZSystemService(kServiceId, kPriority)
{
    mWsaData.Clear();
    cRZRasFunctions *const ras = static_cast<cRZRasFunctions *>(::operator new(sizeof(cRZRasFunctions)));
    mRas = ras;
    std::memset(ras, 0, sizeof(*ras));
    ras->mModule = ImportFunction<decltype(&LoadLibraryA), kIatLoadLibraryA>()(kLibraryName);
    if (ras->mModule) {
        const auto getProc = ImportFunction<decltype(&GetProcAddress), kIatGetProcAddress>();
        for (int i = 0; i < 7; ++i)
            ras->mFunctions[i] = reinterpret_cast<void *>(getProc(static_cast<HMODULE>(ras->mModule), kFunctionNames[i]));
    }
}

// 0xc65a7d. ORIGINAL-BUG: the dial target (+0x1c4) is not released, nor is a dial in progress hung up.
cRZNetManager::~cRZNetManager()
{
    cRZNetManager::Shutdown(); // not through the vtable
    if (mRas) {
        if (mRas->mModule) ImportFunction<decltype(&FreeLibrary), kIatFreeLibrary>()(static_cast<HMODULE>(mRas->mModule));
        delete mRas;
    }
}

// ---- cIGZUnknown ----------------------------------------------------------------------------------------------------

bool cRZNetManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid || riid == kIidAlternate) {
        *out = static_cast<cIGZNetManager *>(this);
    } else if (riid == cIGZSystemService::kIID) {
        *out = static_cast<cIGZSystemService *>(this);
    } else {
        return rz::cRZSystemService::QueryInterface(riid, out);
    }
    AddRef(); // through the vtable, as the original
    return true;
}

uint32_t cRZNetManager::AddRef() { return rz::cRZSystemService::AddRef(); }

uint32_t cRZNetManager::Release() { return rz::cRZSystemService::Release(); }

// ---- Winsock ----------------------------------------------------------------------------------------------------------

bool cRZNetManager::Init()
{
    if (mStarted) return true;
    WSADATA *const data = reinterpret_cast<WSADATA *>(mWsaData.mBytes);
    const auto startup = ImportFunction<decltype(&WSAStartup), kIatWSAStartup>();
    mLastError = 0;
    mVersionMajor = 2;
    mVersionMinor = 0;
    mStarted = true;
    int result = startup(2, data);
    mLastError = result;
    if (result != 0 && mVersionMajor > 1) {
        mVersionMajor = 1;
        mVersionMinor = 1;
        // ORIGINAL-BUG: the 1.1 recorded on the line above is not what is asked for: the call still passes 2.
        result = startup(2, data);
        mLastError = result;
        if (result != 0) return false;
    }
    mVersionMajor = static_cast<uint8_t>(data->wVersion);
    mVersionMinor = static_cast<uint8_t>(data->wVersion >> 8);
    return true;
}

bool cRZNetManager::Shutdown()
{
    if (!mStarted) return true;
    mStarted = false;
    if (ImportFunction<decltype(&WSACleanup), kIatWSACleanup>()() != 0) {
        mLastError = ImportFunction<decltype(&WSAGetLastError), kIatWSAGetLastError>()();
        return false;
    }
    return true;
}

uint32_t cRZNetManager::GetMaxSockets() { return reinterpret_cast<const WSADATA *>(mWsaData.mBytes)->iMaxSockets; }

uint32_t cRZNetManager::GetLastError() { return mLastError; }

bool cRZNetManager::IsNetworkAvailable()
{
    bool available = false;
    const bool autodial = IsAutodialEnabled();
    SetAutodialEnabled(false); // no dialing for the test below
    char address[0x40];
    if (GetConnectedAddress(address) && std::strncmp(address, "127.", 4) != 0 && std::strncmp(address, "0.", 2) != 0) {
        const SOCKET probe = ImportFunction<decltype(&socket), kIatSocket>()(AF_INET, SOCK_STREAM, 0);
        if (probe != INVALID_SOCKET) {
            available = true;
            ImportFunction<decltype(&closesocket), kIatCloseSocket>()(probe);
        }
    }
    SetAutodialEnabled(autodial);
    return available;
}

bool cRZNetManager::GetLocalAddress(cIGZString *out)
{
    char address[0x40];
    if (!GetConnectedAddress(address)) return false;
    out->FromChar(address);
    return true;
}

bool cRZNetManager::GetHostName(cIGZString *out)
{
    char name[0x100];
    const int result = ImportFunction<decltype(&gethostname), kIatGetHostName>()(name, 0xff);
    // ORIGINAL-BUG: the test is inverted: the name is handed out (unset) when gethostname failed, and nothing is when it
    // succeeded; the result says so too.
    if (result != 0) out->FromChar(name);
    return result != 0;
}

void *cRZNetManager::CreateSocket(uint32_t type)
{
    void *socket = nullptr;
    if (type == 1) {
        void *const memory = ::operator new(0x68);
        socket = memory ? OriginalFunction<void *(__thiscall *)(void *), 0x9998e2>()(memory) : nullptr; // cRZNetDatagramSocket
    } else if (type == 2) {
        void *const memory = ::operator new(0x6c);
        socket = memory ? OriginalFunction<void *(__thiscall *)(void *), 0x999970>()(memory) : nullptr; // cRZNetStreamSocket
    } else {
        return nullptr;
    }
    if (!socket) return nullptr;
    cIGZUnknown *const face = reinterpret_cast<cIGZUnknown *>(static_cast<uint8_t *>(socket) + 4); // the +4 face
    face->AddRef();
    return face;
}

// ---- autodial -----------------------------------------------------------------------------------------------------

bool cRZNetManager::IsAutodialEnabled()
{
    bool enabled = false;
    if (HKEY key = OpenInternetSettings()) {
        DWORD value = 0, size = 4;
        const LSTATUS status = ImportFunction<decltype(&RegQueryValueExA), kIatRegQueryValueExA>()(
            key, "EnableAutodial", nullptr, nullptr, reinterpret_cast<BYTE *>(&value), &size);
        enabled = status == ERROR_SUCCESS && value != 0;
        ImportFunction<decltype(&RegCloseKey), kIatRegCloseKey>()(key);
    }
    return enabled;
}

bool cRZNetManager::SetAutodialEnabled(bool enabled)
{
    bool done = false;
    if (HKEY key = OpenInternetSettings()) {
        DWORD value = 0, size = 4;
        const LSTATUS status = ImportFunction<decltype(&RegQueryValueExA), kIatRegQueryValueExA>()(
            key, "EnableAutodial", nullptr, nullptr, reinterpret_cast<BYTE *>(&value), &size);
        if (status == ERROR_SUCCESS && value == static_cast<uint8_t>(enabled)) {
            done = true; // already so
        } else {
            value = static_cast<uint8_t>(enabled);
            done = ImportFunction<decltype(&RegSetValueExA), kIatRegSetValueExA>()(key, "EnableAutodial", 0, REG_BINARY,
                                                                                    reinterpret_cast<const BYTE *>(&value), 4) ==
                   ERROR_SUCCESS;
        }
        ImportFunction<decltype(&RegCloseKey), kIatRegCloseKey>()(key);
    }
    return done;
}

bool cRZNetManager::GetRasOnly() { return mRasOnly; }

void cRZNetManager::SetRasOnly(bool value) { mRasOnly = value; }

// ---- dial-up ----------------------------------------------------------------------------------------------------------

bool cRZNetManager::GetDialParams(const char *entry, char *user, char *password)
{
    const cRZRasFunctions *const ras = mRas;
    *user = 0;
    *password = 0;
    const auto getParams = RasFunction<GetEntryDialParamsFn>(ras, kGetEntryDialParams);
    if (!getParams) return false;
    alignas(4) uint8_t params[kDialParamBytes];
    StartDialParams(params);
    BOOL passwordSaved = 0;
    std::strncpy(reinterpret_cast<char *>(params + kParamEntry), entry, 0x100);
    if (getParams(nullptr, params, &passwordSaved) != 0) return false;
    std::strncpy(user, reinterpret_cast<const char *>(params + kParamUser), 0x100);
    std::strncpy(password, reinterpret_cast<const char *>(params + kParamPassword), 0x100);
    return true;
}

bool cRZNetManager::Dial(cIGZMessageTarget2 *target, const char *entry, const char *user, const char *password)
{
    if (mDialing || IsNetworkAvailable()) return false;
    cRZRasFunctions *const ras = mRas;
    const auto dial = RasFunction<DialFn>(ras, kDial);
    if (!dial || !ras->mFunctions[kGetConnectStatus]) return false;

    BOOL passwordSaved = 0;
    mDialing = true;
    *GameAddress<cIGZNetManager *>(0x110304c) = this; // the RAS callback's manager
    if (target) target->AddRef();
    if (mDialTarget) mDialTarget->Release();
    mDialTarget = target;

    alignas(4) uint8_t params[kDialParamBytes];
    StartDialParams(params);
    if (entry && *entry) std::strncpy(reinterpret_cast<char *>(params + kParamEntry), entry, 0x100);
    RasFunction<GetEntryDialParamsFn>(ras, kGetEntryDialParams)(nullptr, params, &passwordSaved); // result unused, as the original
    if (user && *user) std::strncpy(reinterpret_cast<char *>(params + kParamUser), user, 0x100);
    if (password && *password) std::strncpy(reinterpret_cast<char *>(params + kParamPassword), password, 0x100);

    void **const connection = &ras->mConnection;
    *connection = nullptr;
    const auto hangUp = RasFunction<HangUpFn>(ras, kHangUp);
    if (mDialTarget) {
        // Dialing with a notifier: the result comes to RasCallback.
        const DWORD result = dial(nullptr, nullptr, params, 0, reinterpret_cast<void *>(&RasCallback), connection);
        if (result != 0) {
            if (*connection) hangUp(*connection);
            *connection = nullptr;
        }
        return result == 0;
    }
    // Dialing without one: wait for the call, then ask whether it connected.
    const DWORD result = dial(nullptr, nullptr, params, 0, nullptr, connection);
    if (result == 0 && *connection) {
        alignas(4) uint8_t status[kStatusBytes];
        *reinterpret_cast<uint32_t *>(status) = kStatusBytes;
        if (RasFunction<GetConnectStatusFn>(ras, kGetConnectStatus)(*connection, status) != 0) return false;
        return *reinterpret_cast<const uint32_t *>(status + 4) == kStateConnected;
    }
    if (*connection) hangUp(*connection);
    *connection = nullptr;
    return false;
}

bool cRZNetManager::HangUp()
{
    bool hungUp = false;
    if (cRZRasFunctions *const ras = mRas) {
        void **const connection = &ras->mConnection;
        if (mDialing) {
            if (*connection) {
                RasFunction<HangUpFn>(ras, kHangUp)(*connection);
                WaitThreeSeconds();
                *connection = nullptr;
            }
            mDialing = false;
            hungUp = true;
        }
        *connection = nullptr;
    }
    if (mDialTarget) {
        mDialTarget->Release();
        mDialTarget = nullptr;
    }
    return hungUp;
}

bool cRZNetManager::IsDialing() { return mDialing; }

bool cRZNetManager::NotifyDialResult(bool connected)
{
    if (!mDialing) return false;
    if (mDialTarget) {
        cIGZMessage2Standard *const message = NewMessage(kMessageDialResult, connected);
        mDialTarget->DoMessage(message);
        mDialTarget->Release();
        mDialTarget = nullptr;
        message->Release();
    }
    mDialing = false;
    return true;
}

bool cRZNetManager::Disconnect()
{
    cRZRasFunctions *const ras = mRas;
    const auto enumerate = RasFunction<EnumConnectionsFn>(ras, kEnumConnections);
    const auto hangUp = RasFunction<HangUpFn>(ras, kHangUp);
    if (!enumerate || !hangUp) return false;
    Connections connections;
    if (!connections.Enumerate(enumerate)) return false;
    // ORIGINAL-BUG: the projection function is not tested for null here (GetConnectedAddress does).
    const auto project = RasFunction<GetProjectionInfoFn>(ras, kGetProjectionInfo);
    for (uint32_t i = 0; i < connections.mCount; ++i) {
        void *const connection = connections.Handle(i);
        Projection projection;
        if (projection.Query(project, connection) == 0 && projection.Error() == 0 && connection &&
            hangUp(connection) == 0) {
            WaitThreeSeconds();
            if (connection == ras->mConnection) ras->mConnection = nullptr;
            return true;
        }
    }
    return false;
}

bool cRZNetManager::EnumEntries(char (*names)[0x20], uint32_t *count)
{
    cRZRasFunctions *const ras = mRas;
    const uint32_t capacity = *count;
    uint32_t found = 0;
    bool ok = false;
    for (uint32_t i = 0; i < capacity; ++i) names[i][0] = 0;
    if (const auto enumerate = RasFunction<EnumEntriesFn>(ras, kEnumEntries)) {
        const auto allocate = ImportFunction<decltype(&GlobalAlloc), kIatGlobalAlloc>();
        const auto release = ImportFunction<decltype(&GlobalFree), kIatGlobalFree>();
        if (uint8_t *entries = static_cast<uint8_t *>(allocate(GMEM_ZEROINIT, kEntryNameBytes))) {
            DWORD bytes = kEntryNameBytes, number = 1;
            *reinterpret_cast<uint32_t *>(entries) = kEntryNameBytes;
            if (enumerate(nullptr, nullptr, entries, &bytes, &number) == kErrorBufferTooSmall) {
                release(entries);
                // ORIGINAL-BUG: the bigger buffer is not tested (a failed allocation crashes on the next line).
                entries = static_cast<uint8_t *>(allocate(GMEM_ZEROINIT, bytes));
                *reinterpret_cast<uint32_t *>(entries) = kEntryNameBytes;
            }
            // Asked again either way (with the buffer as it is).
            if (enumerate(nullptr, nullptr, entries, &bytes, &number) == 0) {
                for (uint32_t i = 0; i < number; ++i) {
                    if (i < capacity) {
                        std::strncpy(names[i], reinterpret_cast<const char *>(entries + i * kEntryNameBytes + 4), 0x20);
                        ++found;
                    }
                }
                ok = true;
            }
            release(entries);
        }
    }
    *count = found;
    return ok;
}

cIGZMessageTarget2 *cRZNetManager::GetDialTarget() { return mDialTarget; }

bool cRZNetManager::GetConnectedAddress(char *out)
{
    cRZRasFunctions *const ras = mRas;
    out[0] = 0;
    const auto enumerate = RasFunction<EnumConnectionsFn>(ras, kEnumConnections);
    const auto project = RasFunction<GetProjectionInfoFn>(ras, kGetProjectionInfo);
    if (enumerate && project) {
        Connections connections;
        if (connections.Enumerate(enumerate)) {
            for (uint32_t i = 0; i < connections.mCount; ++i) {
                Projection projection;
                if (projection.Query(project, connections.Handle(i)) == 0 && projection.Error() == 0) {
                    std::strcpy(out, projection.Address());
                    return true;
                }
            }
        }
    }
    if (!mRasOnly) {
        char name[0x200];
        if (ImportFunction<decltype(&gethostname), kIatGetHostName>()(name, 0x200) == 0) {
            const hostent *const host = ImportFunction<decltype(&gethostbyname), kIatGetHostByName>()(name);
            if (host && host->h_addrtype == AF_INET && host->h_length == 4) {
                const char *const text = ImportFunction<decltype(&inet_ntoa), kIatInetNtoa>()(
                    *reinterpret_cast<const in_addr *>(*host->h_addr_list));
                std::strcpy(out, text);
                return true;
            }
        }
    }
    return false;
}

// 0xc663c5: handed to RasDialA while a dial has a target. Reaches the manager through the global the last Dial stored.
void __stdcall cRZNetManager::RasCallback(uint32_t message, uint32_t state, uint32_t)
{
    if (message != kMessageDial) return;
    cIGZNetManager *const manager = *GameAddress<cIGZNetManager *>(0x110304c);
    if (state == kStateDeviceConnected) {
        // ORIGINAL-BUG: the global is not tested for null here (the other branch does).
        if (cIGZMessageTarget2 *const target = manager->GetDialTarget()) {
            cIGZMessage2Standard *const reply = NewMessage(kMessageDialing, 1);
            target->DoMessage(reply);
            reply->Release();
        }
    } else if ((state == kStateConnected || state == kStateDisconnected) && manager) {
        manager->NotifyDialResult(state == kStateConnected);
    }
}

// ---- adapters ---------------------------------------------------------------------------------------------------------

cRZNetManager *__fastcall cRZNetManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZNetManager;
}
