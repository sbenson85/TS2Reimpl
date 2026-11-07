// Differential tests for cRZNetManager (src/rz/cRZNetManager.*, lane PZ): the original object (the game's constructor
// 0xc65932, its own vtables) against ours (cRZNetManagerAdapters::Construct, our vtables).
//
// Isolation (docs/lane/test.md): Winsock, the registry, kernel32's library and global-memory calls and rasapi32 are library
// code reached through the image's import slots, which both sides use, so the slots are patched with fakes for the test
// (the harness traps every import otherwise): the fakes log what they are asked (arguments, and for the RAS calls the
// fields of the records the code builds) and answer from a scripted world. rasapi32 itself is a fake module: the fake
// GetProcAddress hands out the fake RAS functions (some left out by the script). The CRT's _time64 (the image's 0x82fbf6 and
// ours) is stood in on both sides (the three-second waits after a hang-up spin on it), and the two socket constructors
// the manager calls (0x9998e2 / 0x999970, not ours yet) are code patches making fake sockets that log their AddRef.
//
// Sequences: random steps on every slot of every face; the world's answers are re-scripted before each step; after each
// step a snapshot of the object (the service fields, the Winsock state, the dial flags, the dial target, the RAS table) and
// the message target's reference count are logged. The RAS notifier each side handed rasapi32 is called as the library
// would (the dial events). Each case ends through one destruction route (Release to zero on each face, the deleting
// destructor on the +8 face, the destructor), and every heap block allocated minus freed over the whole case is compared.
//
// The slots the sequences reach through each side's vtables (the original's at these RVAs): QueryInterface 0xc66368
// (thunks 0xc663b5 +4, 0xc663bd +8), Init 0xc6625a (thunk 0xc662d9), Shutdown 0xc66609 (thunk 0xc6662f), GetMaxSockets
// 0xc660f7, IsNetworkAvailable 0xc662e8, GetLocalAddress 0xc660ff, GetHostName 0xc65ed5, CreateSocket 0xc65c54,
// IsAutodialEnabled 0xc65e74, SetAutodialEnabled 0xc6649a, GetRasOnly 0xc660f0, SetRasOnly 0xc66535, GetDialParams
// 0xc66021, Dial 0xc65cd2, HangUp 0xc65af0, IsDialing 0xc662e1, NotifyDialResult 0xc66542, Disconnect 0xc65b61,
// EnumEntries 0xc65f0c, GetConnectedAddress 0xc66128, the deleting destructor 0xc65ace (thunk 0xc65ac6) and the destructor
// 0xc65a7d; the RAS callback 0xc663c5 is called through the pointer each side handed the fake RasDial. GetLastError
// (0x15f390) and GetDialTarget (0x530460) are shared bodies reached through the vtable too.
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include "difftest.h"
#include "code_patch.h"
#include "drawfx_core.h"
#include "service_shape.h"
#include "free_scope.h"

#include "rz/RZMemory.h"
#include "rz/cRZNetManager.h"
#include "rz/cRZString.h"
#include "gz/cIGZMessage2Standard.h"


#include <cstdint>
#include <functional>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using Manager = cRZNetManager;
using drawfx::Note;
using drawfx::P;

constexpr uint32_t kCtor = 0xc65932;
constexpr uint32_t kDtor = 0xc65a7d;
constexpr uint32_t kManagerGlobal = 0x110304c; // the RAS callback's manager
constexpr uint32_t kOriginalCallback = 0xc663c5;
const uint32_t kFaces[] = {0, 4, 8};

// The import slots the manager's code calls through.
constexpr uint32_t kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124, kIatFreeLibrary = 0xd9a120;
constexpr uint32_t kIatGlobalAlloc = 0xd9a0f8, kIatGlobalFree = 0xd9a0f4;
constexpr uint32_t kIatRegOpenKeyExA = 0xd9a028, kIatRegCloseKey = 0xd9a02c, kIatRegQueryValueExA = 0xd9a054;
constexpr uint32_t kIatRegSetValueExA = 0xd9a024;
constexpr uint32_t kIatInetNtoa = 0xd9a684, kIatWSAStartup = 0xd9a688, kIatWSACleanup = 0xd9a68c;
constexpr uint32_t kIatWSAGetLastError = 0xd9a6a0, kIatGetHostName = 0xd9a6a4, kIatGetHostByName = 0xd9a6a8;
constexpr uint32_t kIatSocket = 0xd9a6ac, kIatCloseSocket = 0xd9a6b0;

constexpr uint32_t kTargetTag = 0x7a000000;
constexpr uint32_t kModuleHandle = 0x22220000;
constexpr uint32_t kMaxConnections = 5;
constexpr uint32_t kRecordBytes = 0x2d0;
constexpr uint32_t kEntryBytes = 0x214;

const char *const kRasNames[7] = {"RasEnumConnectionsA", "RasEnumEntriesA", "RasGetProjectionInfoA", "RasGetEntryDialParamsA",
                                  "RasDialA",            "RasGetConnectStatusA", "RasHangUpA"};

const char *const kAddresses[] = {"10.1.2.3", "127.0.0.1", "0.5.6.7", "192.168.0.1", "", "127", "0.", "172.16.254.1-plus-a-long-tail-012345678"};
const char *const kHostNames[] = {"somehost", "", "a-long-host-name.with.several.labels.example.com"};
const char *const kEntries[] = {"Home", "", "Work-Dialup-With-A-Very-Long-Name-Over-Thirty-Two", nullptr};
// The last of each is longer than the 0x100 bytes Dial copies (its strncpy length: no terminator within the field).
const char *const kUsers[] = {nullptr, "", "bob", "a-user-name-that-is-fairly-long",
                              "user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/user-name-over-256-bytes/"};
const char *const kPasswords[] = {nullptr, "", "p@ssw0rd", "another-password-value",
                                  "password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/password-over-256-bytes/"};
const char *const kEntryNames[] = {"Office", "Home dial-up", "A connection name that is longer than thirty-two bytes", "", "x"};

// ---- the world: the fakes' answers ------------------------------------------------------------------------------------

struct World {
    // construction
    bool moduleOk = true;
    uint32_t nullMask = 0; // bit i: GetProcAddress answers null for RAS function i
    // Winsock
    int wsaResult[2] = {0, 0};
    uint32_t wsaCalls = 0;
    uint16_t wsaVersion = 0x0002; // wVersion as the library reports it
    uint16_t wsaHigh = 0x0202;
    uint16_t maxSockets = 64;
    int cleanupResult = 0;
    int lastError = 0;
    int hostResult = 0;
    const char *hostName = "somehost";
    bool hostFound = true;
    int hostAddrType = AF_INET;
    int hostLength = 4;
    uint8_t hostAddr[4] = {10, 0, 0, 7};
    uint64_t socketResult = 0x77;
    // registry
    uint32_t regOpen = 0, regQuery = 0, regSet = 0;
    uint32_t autodial = 0;
    // global memory
    uint32_t allocCalls = 0;
    bool allocFirstFails = false;
    // RAS
    uint32_t enumConnResult = 0;
    uint32_t connCount = 0;
    uint32_t handles[kMaxConnections] = {};
    uint32_t projResult[kMaxConnections] = {};
    uint32_t projError[kMaxConnections] = {};
    const char *addr[kMaxConnections] = {};
    uint32_t enumEntriesError = 0; // returned by the calls that would succeed
    uint32_t entryCount = 0;
    uint32_t paramsResult = 0;
    const char *paramsUser = "saved-user";
    const char *paramsPassword = "saved-password";
    uint32_t dialResult = 0;
    uint32_t dialHandle = 0x4444;
    bool dialSetsHandle = true;
    uint32_t statusResult = 0;
    uint32_t connectState = 0x2000;
    uint32_t hangResult = 0;
    void *notifier = nullptr;
    // the clock
    uint32_t clockMode = 0;
    uint32_t clockCalls = 0;
    // the objects
    void *lastSocket = nullptr;
    void *object = nullptr;
};
World gWorld;

struct Target {
    void **vtable;
    int refs;
};
Target gTarget;
bool gTargetHandles = true;

uint32_t TextHash(const char *s)
{
    if (!s) return 0xffffffffu;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

uint32_t BytesHash(const void *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
    return h;
}

uint32_t U32At(const void *base, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(base) + off); }

// The callback the side handed RasDial: 1 for the original's or ours, 0 for none.
uint32_t CallbackCode(const void *cb)
{
    if (!cb) return 0;
    if (cb == GameAddress<void>(kOriginalCallback) || cb == reinterpret_cast<const void *>(&Manager::RasCallback)) return 1;
    return 0xbad;
}

// ---- fakes: kernel32 / advapi32 / Winsock (stdcall) --------------------------------------------------------------------

HMODULE WINAPI FakeLoadLibraryA(const char *name)
{
    difftest::NotCounted nc;
    Note("LoadLibraryA", TextHash(name));
    return gWorld.moduleOk ? reinterpret_cast<HMODULE>(uintptr_t(kModuleHandle)) : nullptr;
}

DWORD WINAPI FakeRasEnumConnectionsA(void *, DWORD *, DWORD *);
DWORD WINAPI FakeRasEnumEntriesA(const char *, const char *, void *, DWORD *, DWORD *);
DWORD WINAPI FakeRasGetProjectionInfoA(void *, uint32_t, void *, DWORD *);
DWORD WINAPI FakeRasGetEntryDialParamsA(const char *, void *, BOOL *);
DWORD WINAPI FakeRasDialA(void *, const char *, void *, DWORD, void *, void **);
DWORD WINAPI FakeRasGetConnectStatusA(void *, void *);
DWORD WINAPI FakeRasHangUpA(void *);

void *const kRasFakes[7] = {reinterpret_cast<void *>(&FakeRasEnumConnectionsA), reinterpret_cast<void *>(&FakeRasEnumEntriesA),
                            reinterpret_cast<void *>(&FakeRasGetProjectionInfoA), reinterpret_cast<void *>(&FakeRasGetEntryDialParamsA),
                            reinterpret_cast<void *>(&FakeRasDialA), reinterpret_cast<void *>(&FakeRasGetConnectStatusA),
                            reinterpret_cast<void *>(&FakeRasHangUpA)};

void *WINAPI FakeGetProcAddress(HMODULE module, const char *name)
{
    difftest::NotCounted nc;
    Note("GetProcAddress", {P(module), TextHash(name)});
    for (int i = 0; i < 7; ++i)
        if (std::strcmp(name, kRasNames[i]) == 0) return (gWorld.nullMask >> i) & 1 ? nullptr : kRasFakes[i];
    return reinterpret_cast<void *>(0xdead);
}

BOOL WINAPI FakeFreeLibrary(HMODULE module)
{
    difftest::NotCounted nc;
    Note("FreeLibrary", P(module));
    return 1;
}

HGLOBAL WINAPI FakeGlobalAlloc(UINT flags, SIZE_T bytes)
{
    difftest::NotCounted nc;
    const uint32_t call = gWorld.allocCalls++;
    Note("GlobalAlloc", {flags, uint32_t(bytes)});
    if (call == 0 && gWorld.allocFirstFails) return nullptr;
    void *p = ::operator new(bytes);
    if (flags & GMEM_ZEROINIT) std::memset(p, 0, bytes);
    return p;
}

HGLOBAL WINAPI FakeGlobalFree(HGLOBAL block)
{
    difftest::NotCounted nc;
    Note("GlobalFree", block ? 1u : 0u);
    ::operator delete(block);
    return nullptr;
}

LSTATUS WINAPI FakeRegOpenKeyExA(HKEY key, const char *sub, DWORD options, REGSAM sam, HKEY *out)
{
    difftest::NotCounted nc;
    Note("RegOpenKeyExA", {P(key), TextHash(sub), options, sam});
    if (gWorld.regOpen == 0) *out = reinterpret_cast<HKEY>(uintptr_t(0x4b4b));
    return gWorld.regOpen;
}

LSTATUS WINAPI FakeRegCloseKey(HKEY key)
{
    difftest::NotCounted nc;
    Note("RegCloseKey", P(key));
    return 0;
}

LSTATUS WINAPI FakeRegQueryValueExA(HKEY key, const char *name, DWORD *reserved, DWORD *type, BYTE *data, DWORD *size)
{
    difftest::NotCounted nc;
    Note("RegQueryValueExA", {P(key), TextHash(name), P(reserved), P(type), data ? 1u : 0u, size ? *size : 0xffffffffu});
    if (gWorld.regQuery == 0) {
        *reinterpret_cast<uint32_t *>(data) = gWorld.autodial;
        *size = 4;
        if (type) *type = REG_BINARY;
    }
    return gWorld.regQuery;
}

LSTATUS WINAPI FakeRegSetValueExA(HKEY key, const char *name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    difftest::NotCounted nc;
    Note("RegSetValueExA", {P(key), TextHash(name), reserved, type, U32At(data, 0), size});
    if (gWorld.regSet == 0) gWorld.autodial = U32At(data, 0);
    return gWorld.regSet;
}

int WSAAPI FakeWSAStartup(WORD version, WSADATA *data)
{
    difftest::NotCounted nc;
    const uint32_t call = gWorld.wsaCalls++;
    const int result = gWorld.wsaResult[call < 2 ? call : 1];
    Note("WSAStartup", {version, uint32_t(result)});
    if (result == 0) {
        data->wVersion = gWorld.wsaVersion;
        data->wHighVersion = gWorld.wsaHigh;
        data->iMaxSockets = gWorld.maxSockets;
        data->iMaxUdpDg = 512;
        data->lpVendorInfo = nullptr;
        std::strcpy(data->szDescription, "Fake sockets");
        std::strcpy(data->szSystemStatus, "Running");
    }
    return result;
}

int WSAAPI FakeWSACleanup()
{
    difftest::NotCounted nc;
    Note("WSACleanup", uint32_t(gWorld.cleanupResult));
    return gWorld.cleanupResult;
}

int WSAAPI FakeWSAGetLastError()
{
    difftest::NotCounted nc;
    Note("WSAGetLastError", uint32_t(gWorld.lastError));
    return gWorld.lastError;
}

int WSAAPI FakeGetHostName(char *name, int length)
{
    difftest::NotCounted nc;
    Note("gethostname", {uint32_t(length), uint32_t(gWorld.hostResult)});
    // Written whether or not it "fails": the original reads the buffer back after a failure (ORIGINAL-BUG in the source), and
    // a real library leaves it undefined, so the test fixes it.
    std::strncpy(name, gWorld.hostName, size_t(length) - 1);
    name[length - 1] = 0;
    return gWorld.hostResult;
}

hostent gHost;
char *gHostAddrs[2];
char gHostNameStore[] = "fake";
hostent *WSAAPI FakeGetHostByName(const char *name)
{
    difftest::NotCounted nc;
    Note("gethostbyname", TextHash(name));
    if (!gWorld.hostFound) return nullptr;
    gHostAddrs[0] = reinterpret_cast<char *>(gWorld.hostAddr);
    gHostAddrs[1] = nullptr;
    gHost.h_name = gHostNameStore;
    gHost.h_aliases = nullptr;
    gHost.h_addrtype = short(gWorld.hostAddrType);
    gHost.h_length = short(gWorld.hostLength);
    gHost.h_addr_list = gHostAddrs;
    return &gHost;
}

char *WSAAPI FakeInetNtoa(in_addr addr)
{
    difftest::NotCounted nc;
    static char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", addr.S_un.S_un_b.s_b1, addr.S_un.S_un_b.s_b2, addr.S_un.S_un_b.s_b3, addr.S_un.S_un_b.s_b4);
    Note("inet_ntoa", TextHash(text));
    return text;
}

SOCKET WSAAPI FakeSocket(int af, int type, int protocol)
{
    difftest::NotCounted nc;
    Note("socket", {uint32_t(af), uint32_t(type), uint32_t(protocol)});
    return SOCKET(gWorld.socketResult);
}

int WSAAPI FakeCloseSocket(SOCKET s)
{
    difftest::NotCounted nc;
    Note("closesocket", uint32_t(s));
    return 0;
}

// ---- fakes: rasapi32 ---------------------------------------------------------------------------------------------------

DWORD WINAPI FakeRasEnumConnectionsA(void *connections, DWORD *bytes, DWORD *count)
{
    difftest::NotCounted nc;
    Note("RasEnumConnectionsA", {U32At(connections, 0), *bytes, *count});
    if (gWorld.enumConnResult != 0) return gWorld.enumConnResult;
    for (uint32_t i = 0; i < gWorld.connCount && i < kMaxConnections; ++i)
        *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(connections) + i * kRecordBytes + 4) = gWorld.handles[i];
    *bytes = gWorld.connCount * kRecordBytes;
    *count = gWorld.connCount;
    return 0;
}

DWORD WINAPI FakeRasEnumEntriesA(const char *reserved, const char *phonebook, void *entries, DWORD *bytes, DWORD *count)
{
    difftest::NotCounted nc;
    Note("RasEnumEntriesA", {P(reserved), P(phonebook), U32At(entries, 0), *bytes, *count});
    const uint32_t wanted = gWorld.entryCount;
    if (wanted * kEntryBytes > *bytes) {
        *bytes = wanted * kEntryBytes;
        *count = wanted;
        return 0x25b;
    }
    if (gWorld.enumEntriesError != 0) return gWorld.enumEntriesError;
    for (uint32_t i = 0; i < wanted; ++i) {
        char *entry = static_cast<char *>(entries) + i * kEntryBytes;
        *reinterpret_cast<uint32_t *>(entry) = kEntryBytes;
        std::strncpy(entry + 4, kEntryNames[i % 5], 0x100);
    }
    *count = wanted;
    return 0;
}

DWORD WINAPI FakeRasGetProjectionInfoA(void *connection, uint32_t type, void *info, DWORD *size)
{
    difftest::NotCounted nc;
    Note("RasGetProjectionInfoA", {P(connection), type, U32At(info, 0), *size});
    uint32_t index = 0;
    for (uint32_t i = 0; i < kMaxConnections; ++i)
        if (gWorld.handles[i] == P(connection)) {
            index = i;
            break;
        }
    auto *bytes = static_cast<uint8_t *>(info);
    *reinterpret_cast<uint32_t *>(bytes + 4) = gWorld.projError[index];
    std::strncpy(reinterpret_cast<char *>(bytes + 8), gWorld.addr[index], 0x27);
    bytes[0x2f] = 0;
    return gWorld.projResult[index];
}

DWORD WINAPI FakeRasGetEntryDialParamsA(const char *phonebook, void *params, BOOL *saved)
{
    difftest::NotCounted nc;
    auto *p = static_cast<uint8_t *>(params);
    Note("RasGetEntryDialParamsA", {P(phonebook), U32At(p, 0), TextHash(reinterpret_cast<const char *>(p + 4)),
                                    TextHash(reinterpret_cast<const char *>(p + 0x207)), TextHash(reinterpret_cast<const char *>(p + 0x308)),
                                    p[0x105], p[0x186], p[0x409], U32At(p, 0x41c), U32At(p, 0x420)});
    // The rest of the user and password fields (0x101 bytes each) is the caller's uninitialised stack, different on the
    // two sides; Dial's 0x100-byte copies of a longer name leave the last byte as it was. Give those bytes a known value
    // (each field's first byte, which Dial sets, is kept) so RasDialA's note compares what Dial wrote.
    const uint8_t user0 = p[0x207], password0 = p[0x308];
    std::memset(p + 0x207, 0xab, 0x101);
    std::memset(p + 0x308, 0xab, 0x101);
    p[0x207] = user0;
    p[0x308] = password0;
    if (gWorld.paramsResult == 0) {
        std::strcpy(reinterpret_cast<char *>(p + 0x207), gWorld.paramsUser);
        std::strcpy(reinterpret_cast<char *>(p + 0x308), gWorld.paramsPassword);
        *saved = 1;
    }
    return gWorld.paramsResult;
}

DWORD WINAPI FakeRasDialA(void *extensions, const char *phonebook, void *params, DWORD type, void *notifier, void **connection)
{
    difftest::NotCounted nc;
    auto *p = static_cast<uint8_t *>(params);
    Note("RasDialA", {P(extensions), P(phonebook), U32At(p, 0), TextHash(reinterpret_cast<const char *>(p + 4)),
                      TextHash(reinterpret_cast<const char *>(p + 0x207)), TextHash(reinterpret_cast<const char *>(p + 0x308)), type,
                      CallbackCode(notifier), P(*connection)});
    gWorld.notifier = notifier;
    if (gWorld.dialSetsHandle) *connection = reinterpret_cast<void *>(uintptr_t(gWorld.dialHandle));
    return gWorld.dialResult;
}

DWORD WINAPI FakeRasGetConnectStatusA(void *connection, void *status)
{
    difftest::NotCounted nc;
    Note("RasGetConnectStatusA", {P(connection), U32At(status, 0)});
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(status) + 4) = gWorld.connectState;
    return gWorld.statusResult;
}

DWORD WINAPI FakeRasHangUpA(void *connection)
{
    difftest::NotCounted nc;
    Note("RasHangUpA", P(connection));
    return gWorld.hangResult;
}

// ---- stand-ins -----------------------------------------------------------------------------------------------------------

__time64_t __cdecl StandTime64(__time64_t *out)
{
    difftest::NotCounted nc;
    const uint32_t n = gWorld.clockCalls++;
    __time64_t t = 0;
    switch (gWorld.clockMode) {
    case 0: t = 0x123456789abcLL + n / 2; break;
    case 1: t = 0xfffffffeLL + n; break;
    case 2: t = n % 3 == 1 ? -1 : 0x7000 + n / 3; break;
    case 3: t = 0x12345678LL + n * 2; break;
    default: t = n % 3 == 0 ? -1 : 0x7000 + n / 3; break;
    }
    Note("_time64", {n, uint32_t(t), uint32_t(t >> 32)});
    if (out) *out = t;
    return t;
}

// The sockets: a block with its vtable at +0 and +4 whose slot 1 (AddRef) logs.
uint32_t __fastcall SocketAddRef(void *self, void *)
{
    difftest::NotCounted nc;
    Note("socket.AddRef", (static_cast<uint8_t *>(self) - static_cast<uint8_t *>(gWorld.lastSocket)) == 4 ? 1u : 0u);
    return 1;
}
void *gSocketVTable[3] = {nullptr, reinterpret_cast<void *>(&SocketAddRef), nullptr};
template <uint32_t Kind> void *__fastcall StandSocketCtor(void *self, void *)
{
    difftest::NotCounted nc;
    Note("socket.ctor", {Kind, static_cast<uint32_t>(_msize(self))}); // the block's size: what the manager asked for
    static_cast<void **>(self)[0] = gSocketVTable;
    static_cast<void **>(self)[1] = gSocketVTable;
    gWorld.lastSocket = self;
    return self;
}

// The game's global allocator is unconstructed in the harness: both sides' messages come from the process heap.
void *__cdecl StandMalloc(uint32_t bytes) { return ::operator new(bytes); }
void __cdecl StandFree(void *block) { ::operator delete(block); }

struct StandIns {
    shape::CodePatch datagram{0x9998e2, reinterpret_cast<void *>(&StandSocketCtor<1>)};
    shape::CodePatch stream{0x999970, reinterpret_cast<void *>(&StandSocketCtor<2>)};
    shape::StandIn time{0x82fbf6, &::_time64, &StandTime64};
    shape::StandIn malloc{0x1be3b0, &RZMemory::Malloc, &StandMalloc};
    shape::StandIn free{0x1be3a1, &RZMemory::Free, &StandFree};
};

// The message target: slot 1 AddRef, slot 2 Release, slot 3 DoMessage.
uint32_t __fastcall TargetAddRef(Target *self, void *)
{
    difftest::NotCounted nc;
    ++self->refs;
    Note("target.AddRef", uint32_t(self->refs));
    return uint32_t(self->refs);
}
uint32_t __fastcall TargetRelease(Target *self, void *)
{
    difftest::NotCounted nc;
    --self->refs;
    Note("target.Release", uint32_t(self->refs));
    return uint32_t(self->refs);
}
bool __fastcall TargetQI(Target *, void *, uint32_t, void **) { return false; }
bool __fastcall TargetDoMessage(Target *, void *, cIGZMessage2Standard *message)
{
    difftest::NotCounted nc;
    Note("target.DoMessage", {message->GetType(), message->HasData1() ? 1u : 0u, message->GetData1(), message->HasData2() ? 1u : 0u,
                             message->HasString() ? 1u : 0u});
    return gTargetHandles;
}
void *gTargetVTable[4] = {reinterpret_cast<void *>(&TargetQI), reinterpret_cast<void *>(&TargetAddRef),
                          reinterpret_cast<void *>(&TargetRelease), reinterpret_cast<void *>(&TargetDoMessage)};

// ---- the patched import slots --------------------------------------------------------------------------------------------

class Iat {
public:
    Iat()
    {
        Set(kIatLoadLibraryA, &FakeLoadLibraryA);
        Set(kIatGetProcAddress, &FakeGetProcAddress);
        Set(kIatFreeLibrary, &FakeFreeLibrary);
        Set(kIatGlobalAlloc, &FakeGlobalAlloc);
        Set(kIatGlobalFree, &FakeGlobalFree);
        Set(kIatRegOpenKeyExA, &FakeRegOpenKeyExA);
        Set(kIatRegCloseKey, &FakeRegCloseKey);
        Set(kIatRegQueryValueExA, &FakeRegQueryValueExA);
        Set(kIatRegSetValueExA, &FakeRegSetValueExA);
        Set(kIatInetNtoa, &FakeInetNtoa);
        Set(kIatWSAStartup, &FakeWSAStartup);
        Set(kIatWSACleanup, &FakeWSACleanup);
        Set(kIatWSAGetLastError, &FakeWSAGetLastError);
        Set(kIatGetHostName, &FakeGetHostName);
        Set(kIatGetHostByName, &FakeGetHostByName);
        Set(kIatSocket, &FakeSocket);
        Set(kIatCloseSocket, &FakeCloseSocket);
    }
    ~Iat()
    {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) *GameAddress<void *>(it->first) = it->second;
    }
    Iat(const Iat &) = delete;
    Iat &operator=(const Iat &) = delete;

private:
    template <class Fn> void Set(uint32_t rva, Fn *fake)
    {
        void **slot = GameAddress<void *>(rva);
        saved_.emplace_back(rva, *slot);
        *slot = reinterpret_cast<void *>(fake);
    }
    std::vector<std::pair<uint32_t, void *>> saved_;
};

// Each side starts from the same world.
void BeginSide(const World &settings)
{
    difftest::NotCounted nc;
    gWorld = settings;
    gTarget.vtable = gTargetVTable;
    gTarget.refs = 1;
    gTargetHandles = true;
    *GameAddress<void *>(kManagerGlobal) = nullptr;
    drawfx::AddRange(&gTarget, sizeof(gTarget), kTargetTag);
    drawfx::gLog.reserve(1 << 14);
}

// Fills the answers of the world from a script seed (everything but what a constructor has already consumed).
void Script(World &w, uint32_t seed, bool construction)
{
    std::mt19937 r(seed);
    auto chance = [&](unsigned n) { return r() % n == 0; };
    if (construction) {
        w.moduleOk = !chance(4);
        w.nullMask = 0;
        for (int i = 0; i < 7; ++i)
            if (chance(8)) w.nullMask |= 1u << i;
    }
    w.wsaCalls = 0;
    w.wsaResult[0] = chance(3) ? 10091 : 0;
    w.wsaResult[1] = chance(3) ? 10092 : 0;
    w.wsaVersion = chance(2) ? 0x0002 : (chance(2) ? 0x0101 : uint16_t(r()));
    w.wsaHigh = uint16_t(r());
    w.maxSockets = uint16_t(r());
    w.cleanupResult = chance(5) ? -1 : 0;
    w.lastError = int(r() % 20000);
    w.hostResult = chance(3) ? -1 : 0;
    w.hostName = kHostNames[r() % 3];
    w.hostFound = !chance(4);
    w.hostAddrType = chance(5) ? AF_INET6 : AF_INET;
    w.hostLength = chance(5) ? 16 : 4;
    for (uint8_t &b : w.hostAddr) b = uint8_t(r());
    if (chance(3)) w.hostAddr[0] = chance(2) ? 127 : 0;
    w.socketResult = chance(4) ? uint64_t(~0u) : 0x77;
    w.regOpen = chance(6) ? 5 : 0;
    w.regQuery = chance(5) ? 2 : 0;
    w.regSet = chance(5) ? 5 : 0;
    if (chance(2)) w.autodial = r() % 3;
    w.allocCalls = 0;
    w.allocFirstFails = chance(8);
    w.enumConnResult = chance(5) ? 632 : 0;
    w.connCount = r() % (kMaxConnections + 1);
    for (uint32_t i = 0; i < kMaxConnections; ++i) {
        w.handles[i] = chance(5) ? 0 : 0x1000 + i;
        w.projResult[i] = chance(4) ? 623 : 0;
        w.projError[i] = chance(4) ? 676 : 0;
        w.addr[i] = kAddresses[r() % (sizeof(kAddresses) / sizeof(kAddresses[0]))];
    }
    w.enumEntriesError = chance(5) ? 87 : 0;
    w.entryCount = r() % 5;
    w.paramsResult = chance(4) ? 623 : 0;
    w.dialResult = chance(4) ? 668 : 0;
    w.dialHandle = chance(5) ? 0 : 0x4444;
    w.dialSetsHandle = !chance(5);
    w.statusResult = chance(5) ? 6 : 0;
    {
        static const uint32_t states[] = {0x2000, 0x2000, 0x2001, 0x2002, 0, 3};
        w.connectState = states[r() % 6];
    }
    w.hangResult = chance(5) ? 6 : 0;
    w.clockMode = r() % 5;
    w.clockCalls = 0;
    gTargetHandles = !chance(8);
}

// ---- calls ---------------------------------------------------------------------------------------------------------------

uint32_t Call(void *m, uint32_t face, uint32_t slot, unsigned words, const uint32_t *args)
{
    void *self = static_cast<uint8_t *>(m) + face;
    return svc::CallWords(svc::VTableAt(m, face)[slot], self, words, args);
}

enum Op : uint8_t {
    kInit, kShutdown, kGetMaxSockets, kGetLastError, kIsNetworkAvailable, kGetLocalAddress, kGetHostName, kCreateSocket,
    kIsAutodial, kSetAutodial, kGetRasOnly, kSetRasOnly, kGetDialParams, kDial, kHangUp, kIsDialing, kNotify, kDisconnect,
    kEnumEntries, kGetDialTarget, kGetConnectedAddress, kRasEvent, kQI, kRef, kService, kOps
};
const char *const kOpNames[] = {"Init", "Shutdown", "GetMaxSockets", "GetLastError", "IsNetworkAvailable", "GetLocalAddress",
                                "GetHostName", "CreateSocket", "IsAutodial", "SetAutodial", "GetRasOnly", "SetRasOnly",
                                "GetDialParams", "Dial", "HangUp", "IsDialing", "Notify", "Disconnect", "EnumEntries",
                                "GetDialTarget", "GetConnectedAddress", "RasEvent", "QI", "Ref", "Service"};

struct Step {
    Op op;
    uint32_t a, b, c, script;
};

Step RandomStep(std::mt19937 &rng)
{
    static const Op weighted[] = {kInit, kInit, kInit, kShutdown, kGetMaxSockets, kGetLastError, kIsNetworkAvailable,
                                  kIsNetworkAvailable, kGetLocalAddress, kGetLocalAddress, kGetHostName, kCreateSocket,
                                  kIsAutodial, kSetAutodial, kSetAutodial, kGetRasOnly, kSetRasOnly, kSetRasOnly,
                                  kGetDialParams, kDial, kDial, kDial, kDial, kHangUp, kHangUp, kIsDialing, kNotify,
                                  kNotify, kDisconnect, kDisconnect, kEnumEntries, kEnumEntries, kGetDialTarget,
                                  kGetConnectedAddress, kGetConnectedAddress, kRasEvent, kRasEvent, kRasEvent, kQI, kRef, kService};
    Step st{};
    st.op = weighted[rng() % (sizeof(weighted) / sizeof(weighted[0]))];
    st.a = rng();
    st.b = rng();
    st.c = rng();
    st.script = rng();
    return st;
}

const uint32_t kIids[] = {1, cIGZNetManager::kIid, cIGZNetManager::kIidAlternate, cIGZSystemService::kIID, 0x12345678, 0};

// The +4 face's service slots: {slot, stack words, result}.
struct ServiceSlot {
    uint32_t slot, words;
    svc::Ret ret;
};
const ServiceSlot kServiceSlots[] = {{3, 0, svc::kWord}, {4, 1, svc::kVoid}, {5, 0, svc::kWord}, {6, 0, svc::kByte},
                                     {7, 1, svc::kVoid}, {10, 1, svc::kByte}, {11, 1, svc::kByte}, {12, 0, svc::kWord}};

// The object's state as notes. The layouts are the same (the std types are the image's), so the original object reads
// through our class.
void Snap(void *m)
{
    difftest::NotCounted nc;
    const auto *b = static_cast<const uint8_t *>(m);
    const Manager *x = static_cast<const Manager *>(m);
    Note("service", {U32At(b, 0xc), U32At(b, 0x10), U32At(b, 0x14), U32At(b, 0x18), b[0x1c], U32At(b, 0x20)});
    Note("winsock", {x->mStarted ? 1u : 0u, x->mLastError, x->mVersionMajor, x->mVersionMinor, BytesHash(b + 0x2c, 0x190)});
    Note("flags", {x->mFlag1c0, x->mRasOnly ? 1u : 0u, x->mDialing ? 1u : 0u, drawfx::Norm(P(x->mDialTarget))});
    Note("target", uint32_t(gTarget.refs));
    void *global = *GameAddress<void *>(kManagerGlobal);
    Note("global", global == nullptr ? 0u : global == m ? 1u : 2u);
    if (const cRZRasFunctions *ras = x->mRas) {
        Note("ras", {P(ras->mModule), P(ras->mConnection)});
        for (int i = 0; i < 7; ++i) Note("ras.fn", P(ras->mFunctions[i]));
    }
}

void WithString(const std::function<void(void *)> &use)
{
    alignas(8) uint8_t buffer[sizeof(cRZString)];
    auto *s = reinterpret_cast<cRZString *>(buffer);
    s->Construct();
    use(buffer);
    Note("string", TextHash(s->ToChar()));
    s->Destruct();
}

// Whether RAS function `index` was found when the manager was built.
bool Has(void *m, int index) { return static_cast<Manager *>(m)->mRas->mFunctions[index] != nullptr; }

void Apply(void *m, const Step &st)
{
    Script(gWorld, st.script, false);
    switch (st.op) {
    case kInit: {
        const bool service = st.b % 2;
        Note("Init", Call(m, service ? 4 : 0, service ? 8 : 3, 0, nullptr) & 0xff);
        break;
    }
    case kShutdown: {
        const bool service = st.b % 2;
        Note("Shutdown", Call(m, service ? 4 : 0, service ? 9 : 4, 0, nullptr) & 0xff);
        break;
    }
    case kGetMaxSockets: Note("GetMaxSockets", Call(m, 0, 5, 0, nullptr)); break;
    case kGetLastError: Note("GetLastError", Call(m, 0, 6, 0, nullptr)); break;
    case kIsNetworkAvailable: Note("IsNetworkAvailable", Call(m, 0, 7, 0, nullptr) & 0xff); break;
    case kGetLocalAddress:
        WithString([&](void *s) {
            const uint32_t a[1] = {P(s)};
            Note("GetLocalAddress", Call(m, 0, 8, 1, a) & 0xff);
        });
        break;
    case kGetHostName:
        WithString([&](void *s) {
            const uint32_t a[1] = {P(s)};
            Note("GetHostName", Call(m, 0, 9, 1, a) & 0xff);
        });
        break;
    case kCreateSocket: {
        gWorld.lastSocket = nullptr;
        const uint32_t a[1] = {st.b % 4};
        void *face = reinterpret_cast<void *>(uintptr_t(Call(m, 0, 10, 1, a)));
        Note("CreateSocket", face ? uint32_t(static_cast<uint8_t *>(face) - static_cast<uint8_t *>(gWorld.lastSocket)) : 0xffffffffu);
        ::operator delete(gWorld.lastSocket);
        break;
    }
    case kIsAutodial: Note("IsAutodialEnabled", Call(m, 0, 11, 0, nullptr) & 0xff); break;
    case kSetAutodial: {
        const uint32_t a[1] = {st.b % 2};
        Note("SetAutodialEnabled", Call(m, 0, 12, 1, a) & 0xff);
        Note("autodial", gWorld.autodial);
        break;
    }
    case kGetRasOnly: Note("GetRasOnly", Call(m, 0, 13, 0, nullptr) & 0xff); break;
    case kSetRasOnly: {
        const uint32_t a[1] = {st.b % 2};
        Call(m, 0, 14, 1, a);
        break;
    }
    case kGetDialParams: {
        char user[0x120], password[0x120];
        std::memset(user, 0xcd, sizeof(user));
        std::memset(password, 0xcd, sizeof(password));
        const uint32_t a[3] = {P(kEntries[st.b % 3]), P(user), P(password)};
        Note("GetDialParams", Call(m, 0, 15, 3, a) & 0xff);
        Note("user", BytesHash(user, sizeof(user)));
        Note("password", BytesHash(password, sizeof(password)));
        break;
    }
    case kDial: {
        // The original calls RasGetEntryDialParams (and RasHangUp after a failed dial) without testing them: with one
        // missing it crashes (ORIGINAL-BUG), so such a dial is not made.
        if (!Has(m, 3) || !Has(m, 6)) {
            Note("Dial.skipped", 0);
            break;
        }
        const uint32_t a[4] = {st.b % 3 == 0 ? 0u : P(&gTarget), P(kEntries[st.c % 4]), P(kUsers[(st.c >> 4) % 5]),
                               P(kPasswords[(st.c >> 8) % 5])};
        Note("Dial", Call(m, 0, 16, 4, a) & 0xff);
        break;
    }
    case kHangUp:
        // With a dial in progress the original hangs up the connection without testing the function (ORIGINAL-BUG).
        if (static_cast<Manager *>(m)->mDialing && static_cast<Manager *>(m)->mRas->mConnection && !Has(m, 6)) {
            Note("HangUp.skipped", 0);
            break;
        }
        Note("HangUp", Call(m, 0, 17, 0, nullptr) & 0xff);
        break;
    case kIsDialing: Note("IsDialing", Call(m, 0, 18, 0, nullptr) & 0xff); break;
    case kNotify: {
        const uint32_t a[1] = {st.b % 2};
        Note("NotifyDialResult", Call(m, 0, 19, 1, a) & 0xff);
        break;
    }
    case kDisconnect:
        // The original calls RasGetProjectionInfo here without testing it (ORIGINAL-BUG).
        if (!Has(m, 2) && Has(m, 0) && Has(m, 6)) {
            Note("Disconnect.skipped", 0);
            break;
        }
        Note("Disconnect", Call(m, 0, 20, 0, nullptr) & 0xff);
        break;
    case kEnumEntries: {
        char names[6][0x20];
        std::memset(names, 0xcd, sizeof(names));
        uint32_t count = st.b % 7;
        if (count > 6) count = 6;
        const uint32_t capacity = count > 6 ? 6 : count;
        const uint32_t a[2] = {P(names), P(&count)};
        Note("EnumEntries", Call(m, 0, 21, 2, a) & 0xff);
        Note("EnumEntries.count", {capacity, count});
        Note("EnumEntries.names", BytesHash(names, sizeof(names)));
        break;
    }
    case kGetDialTarget: Note("GetDialTarget", drawfx::Norm(Call(m, 0, 22, 0, nullptr))); break;
    case kGetConnectedAddress: {
        char out[0x60];
        std::memset(out, 0xcd, sizeof(out));
        const uint32_t a[1] = {P(out)};
        Note("GetConnectedAddress", Call(m, 0, 23, 1, a) & 0xff);
        Note("address", BytesHash(out, sizeof(out)));
        break;
    }
    case kRasEvent: {
        // What rasapi32 does on its own thread: call the notifier the side handed RasDial.
        if (!gWorld.notifier) break;
        static const uint32_t states[] = {3, 0x2000, 0x2001, 7, 0};
        const uint32_t message = st.b % 5 == 0 ? 0x1234 : 0xcccd;
        reinterpret_cast<void(__stdcall *)(uint32_t, uint32_t, uint32_t)>(gWorld.notifier)(message, states[st.c % 5], st.a & 0xff);
        break;
    }
    case kQI: {
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t a[2] = {kIids[st.b % (sizeof(kIids) / sizeof(kIids[0]))], P(&out)};
        const uint32_t got = Call(m, kFaces[st.c % 3], 0, 2, a) & 0xff;
        Note("QI", {got, out == reinterpret_cast<void *>(0x5a5a5a5a) ? 0xffffffffu : P(out) - P(m)});
        // The face's own Release gives the reference back (the test holds one, so the count stays above 1).
        if (got) Note("QI.Release", svc::CallWords(svc::VTableAt(out, 0)[2], out, 0, nullptr));
        break;
    }
    case kRef: {
        const uint32_t refs = U32At(m, 0xc);
        const bool release = refs > 2 && st.b % 2;
        Note("Ref", Call(m, kFaces[st.c % 3], release ? 2 : 1, 0, nullptr));
        break;
    }
    case kService: {
        const ServiceSlot &s = kServiceSlots[st.b % (sizeof(kServiceSlots) / sizeof(kServiceSlots[0]))];
        const uint32_t a[1] = {s.slot == 7 ? st.c % 2 : st.c % 1000};
        Note("Service", {s.slot, svc::Masked(s.ret, Call(m, 4, s.slot, s.words, a))});
        break;
    }
    default: break;
    }
}

// ---- construction --------------------------------------------------------------------------------------------------------

void *NewOriginal(difftest::Context &ctx, int fill)
{
    void *memory = ::operator new(Manager::kSize);
    std::memset(memory, fill, Manager::kSize);
    return ctx.Original<void *(__thiscall *)(void *)>(kCtor)(memory);
}
void *NewOurs(int fill)
{
    void *memory = ::operator new(Manager::kSize);
    std::memset(memory, fill, Manager::kSize);
    return cRZNetManagerAdapters::Construct(memory, nullptr);
}

// Each side's vtable words as codes (the three faces 0..2) and what a destructor leaves (the service part's bases).
struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        alignas(8) static uint8_t buffer[Manager::kSize];
        Iat iat;
        BeginSide(World());
        Manager *ours = ::new (buffer) Manager;
        a.AddRva(Manager::kVTable, 0);
        a.AddRva(Manager::kSystemVTable, 1);
        a.AddRva(Manager::kUnknownVTable, 2);
        for (uint32_t f = 0; f < 3; ++f) b.Add(svc::VTableAt(ours, kFaces[f]), f);
        ours->~Manager();
        svc::AddServiceBaseCodes(a, b, 0x20);
    }
    // From `from` on; the RAS table pointer (+0x1c8) is masked (the callers compare the table by content).
    std::string Compare(const void *x, const void *y, uint32_t from) const
    {
        std::vector<uint32_t> wa = svc::Words(x, Manager::kSize, a), wb = svc::Words(y, Manager::kSize, b);
        for (uint32_t i = 0; i < from / 4; ++i) wa[i] = wb[i] = 0;
        wa[0x1c8 / 4] = wb[0x1c8 / 4] = 0;
        return svc::Diff(wa, wb);
    }
};

bool CheckGameVTables(difftest::Context &ctx, const void *a)
{
    if (svc::VTableAt(a, 0) != GameAddress<void *>(Manager::kVTable) || svc::VTableAt(a, 4) != GameAddress<void *>(Manager::kSystemVTable) ||
        svc::VTableAt(a, 8) != GameAddress<void *>(Manager::kUnknownVTable)) {
        ctx.Fail("the original object does not carry the game's vtables");
        return false;
    }
    return true;
}

} // namespace

// ---- tests ---------------------------------------------------------------------------------------------------------------

// The constructor: fixed and random fills; the library calls it makes (log), the result is the block, every word compares
// (the vtables as codes, the padding included), the table it builds has the same content and heap size, the 16 bytes past
// the object are untouched, and our faces carry our vtables.
DIFF_TEST(cRZNetManager_Construct, kCtor)
{
    constexpr size_t kTail = 16;
    Iat iat;
    Maps maps;
    for (uint32_t seed = 1; seed <= 80; ++seed) {
        World settings;
        Script(settings, seed * 31, true);
        uint8_t *ma = static_cast<uint8_t *>(::operator new(Manager::kSize + kTail));
        uint8_t *mb = static_cast<uint8_t *>(::operator new(Manager::kSize + kTail));
        std::mt19937 rng(seed);
        for (size_t i = 0; i < Manager::kSize + kTail; ++i)
            ma[i] = mb[i] = seed <= 4 ? static_cast<uint8_t>(0x55 * (seed - 1)) : static_cast<uint8_t>(rng());
        void *ra = nullptr;
        Manager *rb = nullptr;
        auto a = drawfx::Side([&] { BeginSide(settings); }, [&] { ra = ctx.Original<void *(__thiscall *)(void *)>(kCtor)(ma); });
        auto b = drawfx::Side([&] { BeginSide(settings); }, [&] { rb = cRZNetManagerAdapters::Construct(mb, nullptr); });
        if (!drawfx::Same(ctx, a.first, b.first, "constructor", seed, a.second.empty() ? b.second : a.second)) return;
        ++ctx.cases;
        if (ra != ma || reinterpret_cast<uint8_t *>(rb) != mb) ctx.Fail("seed %u: wrong return", seed);
        const std::string diff = maps.Compare(ma, mb, 0);
        if (!diff.empty()) ctx.Fail("seed %u: object differs at %s", seed, diff.c_str());
        if (std::memcmp(ma + Manager::kSize, mb + Manager::kSize, kTail) != 0) ctx.Fail("seed %u: bytes past the object differ", seed);
        CheckGameVTables(ctx, ma);
        for (uint32_t f = 0; f < 3; ++f)
            if (svc::VTableAt(rb, kFaces[f]) == GameAddress<void *>(f == 0 ? Manager::kVTable : f == 1 ? Manager::kSystemVTable : Manager::kUnknownVTable))
                ctx.Fail("our face %u has the game's vtable", f);
        const auto *rasA = reinterpret_cast<const cRZRasFunctions *>(U32At(ma, 0x1c8)), *rasB = rb->mRas;
        if (!rasA || !rasB || std::memcmp(rasA, rasB, sizeof(cRZRasFunctions)) != 0) ctx.Fail("seed %u: function tables differ", seed);
        if (rasA && HeapSize(GetProcessHeap(), 0, rasA) != HeapSize(GetProcessHeap(), 0, rasB)) ctx.Fail("seed %u: table heap sizes differ", seed);
        ::operator delete(const_cast<cRZRasFunctions *>(rasA));
        ::operator delete(const_cast<cRZRasFunctions *>(rasB));
        ::operator delete(ma);
        ::operator delete(mb);
    }
}

// The sequences (see the top of the file); each case ends through one destruction route. Per side, the heap blocks and
// bytes allocated minus freed over the whole case (the test's own bookkeeping left out) compare too.
DIFF_TEST(cRZNetManager_Sequences, 0xc65cd2)
{
    Iat iat;
    StandIns stands;
    Maps maps;
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        World settings;
        std::mt19937 rng(seed * 7919);
        Script(settings, seed * 104729, true);
        settings.autodial = rng() % 3;
        std::vector<Step> steps(10 + rng() % 50);
        for (Step &st : steps) st = RandomStep(rng);
        const uint32_t route = rng() % 5, flags = rng() % 2;
        long long blocks[2] = {}, bytes[2] = {};
        bool freed[2] = {};
        void *objects[2] = {};
        auto run = [&](int side) {
            const bool original = side == 0;
            difftest::AllocationCounter counter;
            void *m = original ? NewOriginal(ctx, 0xcd) : NewOurs(0xcd);
            objects[side] = m;
            gWorld.object = m;
            if (original && !CheckGameVTables(ctx, m)) return;
            Call(m, 0, 1, 0, nullptr); // the test's reference
            for (size_t i = 0; i < steps.size(); ++i) {
                Note(kOpNames[steps[i].op], uint32_t(i));
                Apply(m, steps[i]);
                Snap(m);
            }
            // The end: 0..2 Release to zero on a face, 3 the +8 face's deleting destructor (flags), 4 the destructor (then
            // the block freed by the test).
            Script(gWorld, seed, false);
            difftest::BeginFrees();
            if (route < 3) {
                for (int n = 0; n < 64; ++n) {
                    const uint32_t r = Call(m, kFaces[route], 2, 0, nullptr);
                    Note("Release", r);
                    if (r == 0) break;
                }
            } else if (route == 3) {
                const uint32_t a[1] = {flags};
                Note("delete", Call(m, 8, 3, 1, a) - P(m));
            } else if (original) {
                ctx.Original<void(__thiscall *)(void *)>(kDtor)(m);
            } else {
                static_cast<Manager *>(m)->~Manager();
            }
            freed[side] = difftest::FreedSinceBegin(m);
            Note("target.end", uint32_t(gTarget.refs));
            counter.Stop();
            blocks[side] = counter.Blocks();
            bytes[side] = counter.Bytes();
        };
        auto a = drawfx::Side([&] { BeginSide(settings); }, [&] { run(0); });
        auto b = drawfx::Side([&] { BeginSide(settings); }, [&] { run(1); });
        if (!drawfx::Same(ctx, a.first, b.first, "sequence", seed, a.second.empty() ? b.second : a.second)) return;
        ++ctx.cases;
        if (freed[0] != freed[1]) ctx.Fail("seed %u route %u: freed %d/%d", seed, route, freed[0], freed[1]);
        if (blocks[0] != blocks[1] || bytes[0] != bytes[1])
            ctx.Fail("seed %u route %u: heap balance %lld blocks %lld bytes / %lld blocks %lld bytes", seed, route, blocks[0], bytes[0], blocks[1], bytes[1]);
        if (!freed[0] && !freed[1]) {
            const std::string diff = maps.Compare(objects[0], objects[1], 4);
            if (!diff.empty()) ctx.Fail("seed %u route %u: words after destruction differ at %s", seed, route, diff.c_str());
        }
        for (int j = 0; j < 2; ++j)
            if (!freed[j]) ::operator delete(objects[j]);
        *GameAddress<void *>(kManagerGlobal) = nullptr;
    }
}
