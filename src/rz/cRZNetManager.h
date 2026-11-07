#pragma once
#include "gz/cIGZMessageTarget2.h"
#include "rz/RZSystemServiceBase.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

// The network manager service (lane PZ, milestone I): service 0x654383c9, priority 0, application service 4 in
// cTSAppFW::AddApplicationServicesHere. It starts Winsock, answers "is there a network" and "what is the local address",
// makes datagram / stream sockets, and drives Windows dial-up networking (RAS, rasapi32.dll loaded at construction) for the
// game's connection dialogs. RTTI cRZNetManager; bases cIGZNetManager at +0, cRZSystemService at +4 (its cRZUnknownMT at
// +8). 0x1cc bytes. Game vtables (1.18.0.187): 0xe9e37c (+0, 24 slots), 0xe9e3e0 (+4, 13), 0xe9e418 (+8, 6); the
// constructor stores 0xe9e31c (the interface's own) at +0 first.
//
// Winsock, the registry and rasapi32 are library code: called through the image's import slots, never reimplemented.

// The table the constructor fills: the module handle at +0, the current connection (a HRASCONN, 0 when none) at +4, and
// seven function pointers at +8..+0x20, in this order: RasEnumConnectionsA, RasEnumEntriesA, RasGetProjectionInfoA,
// RasGetEntryDialParamsA, RasDialA, RasGetConnectStatusA, RasHangUpA. All zero when the library does not load.
struct cRZRasFunctions {
    void *mModule;        // 0x00 LoadLibraryA("rasapi32.dll")
    void *mConnection;    // 0x04 the HRASCONN of the dial in progress / made by Dial
    void *mFunctions[7];  // 0x08..0x20
};
static_assert(sizeof(cRZRasFunctions) == 0x24, "cRZRasFunctions layout");

// The block at +0x2c of cRZNetManager: a WSADATA (0x190 bytes) that Init hands to WSAStartup. 0xc65a40 (__thiscall, `this` =
// the block) clears parts of it: dword 0, the bytes 4..0x105, 0x105..0x187 (the two ranges overlap by one byte), the dword
// at 0x186 and the dword at 0x18c. Bytes 0x18a/0x18b of the dword at 0x186 and everything else stay as they were.
struct cRZNetConnectionState {
    uint8_t mBytes[0x190];
    cRZNetConnectionState *Clear();
};
static_assert(sizeof(cRZNetConnectionState) == 0x190, "cRZNetConnectionState layout");

// cIGZNetManager (game vtable 0xe9e31c, slots 3-23 _purecall). The names follow what the slots do.
class cIGZNetManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x94af390c;
    static constexpr uint32_t kIidAlternate = 0x223b8812; // answered with this face too

    // slot 3 (also the service's Init): WSAStartup. Version 2.0 asked for; on failure, "1.1" is recorded and it asks again
    // (still for 2). Once started, true without asking again.
    virtual bool Init() = 0;
    // slot 4 (also the service's Shutdown): WSACleanup when started; false (and the error recorded) if that fails.
    virtual bool Shutdown() = 0;
    virtual uint32_t GetMaxSockets() = 0;                                    // slot 5: the WSADATA's iMaxSockets
    virtual uint32_t GetLastError() = 0;                                     // slot 6: the last Winsock error Init/Shutdown saw
    virtual bool IsNetworkAvailable() = 0;                                   // slot 7: a connected, non-loopback address and a socket
    virtual bool GetLocalAddress(cIGZString *out) = 0;                       // slot 8: GetConnectedAddress as a string
    virtual bool GetHostName(cIGZString *out) = 0;                           // slot 9
    virtual void *CreateSocket(uint32_t type) = 0;                           // slot 10: 1 datagram (0x68), 2 stream (0x6c), else null
    virtual bool IsAutodialEnabled() = 0;                                    // slot 11: HKCU Internet Settings "EnableAutodial"
    virtual bool SetAutodialEnabled(bool enabled) = 0;                       // slot 12
    virtual bool GetRasOnly() = 0;                                           // slot 13: byte +0x1c1
    virtual void SetRasOnly(bool value) = 0;                                 // slot 14
    // slot 15: the saved user name / password of the dial-up entry `entry` (both outputs cleared first). False without RAS.
    virtual bool GetDialParams(const char *entry, char *user, char *password) = 0;
    // slot 16: dial `entry` (user / password when given). `target` (counted) gets the 0x65783920 / 0x65783921 messages.
    virtual bool Dial(cIGZMessageTarget2 *target, const char *entry, const char *user, const char *password) = 0;
    virtual bool HangUp() = 0;                                               // slot 17: hang up the dial, release the target
    virtual bool IsDialing() = 0;                                            // slot 18: byte +0x1c2
    virtual bool NotifyDialResult(bool connected) = 0;                       // slot 19: the RAS callback's end of a dial
    virtual bool Disconnect() = 0;                                           // slot 20: hang up the first connection that has an address
    virtual bool EnumEntries(char (*names)[0x20], uint32_t *count) = 0;      // slot 21: dial-up entry names; `*count` in/out
    virtual cIGZMessageTarget2 *GetDialTarget() = 0;                                // slot 22: shared 0x530460 (+0x1c4)
    virtual bool GetConnectedAddress(char *out) = 0;                         // slot 23: dotted address text, 0x40 bytes at most
};

// The class in the standalone shape (docs/standalone-shape.md). The game's constructor 0xc65932 is hooked to
// cRZNetManagerAdapters::Construct (placement new of this class), and our cTSAppFW builds it through that adapter: every
// manager carries our vtables. The original's members are reached only from its vtables, from each other and from the RAS
// callback it handed rasapi32 (ours now), so they are in effect through that one hook (trapped by tools/callers.py
// --write-traps).
class cRZNetManager : public cIGZNetManager, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x1cc;
    static constexpr uint32_t kVTable = 0xe9e37c;          // +0 (the game's, for tests)
    static constexpr uint32_t kSystemVTable = 0xe9e3e0;    // +4
    static constexpr uint32_t kUnknownVTable = 0xe9e418;   // +8
    static constexpr uint32_t kInterfaceVTable = 0xe9e31c; // the interface's own (stored at +0 during construction)
    static constexpr uint32_t kServiceId = 0x654383c9;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kSystemFace = 4;

    cRZNetManager(); // 0xc65932
    // 0xc65a7d: Shutdown, FreeLibrary, the RAS table, the service part (the dial target is not released). Deleting: 0xc65ace
    // (thunk 0xc65ac6, +8 slot 3).
    ~cRZNetManager() override;
    cRZNetManager(const cRZNetManager &) = delete;
    cRZNetManager &operator=(const cRZNetManager &) = delete;

    // cIGZUnknown, every face (the +4 / +8 ones through the compiler's thunks: QueryInterface 0xc663b5 / 0xc663bd, AddRef the
    // shared 0x145248 / 0x145250, Release 0x1a85b8 / 0x1a85c0).
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc66368
    uint32_t AddRef() override;                              // shared 0x145240 (the service's)
    uint32_t Release() override;                             // shared 0x1a85b0 (the service's)

    // cIGZNetManager (Init / Shutdown also the service's slots 8 / 9, thunks 0xc662d9 / 0xc6662f)
    bool Init() override;                                                    // 0xc6625a
    bool Shutdown() override;                                                // 0xc66609
    uint32_t GetMaxSockets() override;                                       // 0xc660f7
    uint32_t GetLastError() override;                                        // shared 0x15f390 (+0x28)
    bool IsNetworkAvailable() override;                                      // 0xc662e8
    bool GetLocalAddress(cIGZString *out) override;                          // 0xc660ff
    bool GetHostName(cIGZString *out) override;                              // 0xc65ed5
    void *CreateSocket(uint32_t type) override;                              // 0xc65c54
    bool IsAutodialEnabled() override;                                       // 0xc65e74
    bool SetAutodialEnabled(bool enabled) override;                          // 0xc6649a
    bool GetRasOnly() override;                                              // 0xc660f0
    void SetRasOnly(bool value) override;                                    // 0xc66535
    bool GetDialParams(const char *entry, char *user, char *password) override; // 0xc66021
    bool Dial(cIGZMessageTarget2 *target, const char *entry, const char *user, const char *password) override; // 0xc65cd2
    bool HangUp() override;                                                  // 0xc65af0
    bool IsDialing() override;                                               // 0xc662e1
    bool NotifyDialResult(bool connected) override;                          // 0xc66542
    bool Disconnect() override;                                              // 0xc65b61
    bool EnumEntries(char (*names)[0x20], uint32_t *count) override;         // 0xc65f0c
    cIGZMessageTarget2 *GetDialTarget() override;                                   // shared 0x530460
    bool GetConnectedAddress(char *out) override;                            // 0xc66128

    // The callback handed to RasDialA while a dial has a target (the original's 0xc663c5; stdcall, ret 0xc). It reaches the
    // manager through the global the last Dial stored (RVA 0x110304c).
    static void __stdcall RasCallback(uint32_t message, uint32_t state, uint32_t error);

    uint32_t mUnused20 = 0;              // +0x20 cleared by the constructor, read by nothing
    bool mStarted = false;               // +0x24 WSAStartup succeeded (Shutdown has work to do)
    uint8_t mPad25[3];                   // +0x25 untouched
    uint32_t mLastError = 0;             // +0x28
    cRZNetConnectionState mWsaData;      // +0x2c
    uint16_t mVersionMajor = 0;          // +0x1bc the version Init recorded (2.0 asked for, 1.1 on failure)
    uint16_t mVersionMinor = 0;          // +0x1be
    uint8_t mFlag1c0 = 1;                // +0x1c0 set by the constructor, read by nothing
    bool mRasOnly = true;                // +0x1c1 the connected address comes from RAS only (no host name lookup)
    bool mDialing = false;               // +0x1c2
    uint8_t mPad1c3;                     // +0x1c3 untouched
    cIGZMessageTarget2 *mDialTarget = nullptr;  // +0x1c4 counted; receives the dial messages
    cRZRasFunctions *mRas = nullptr;     // +0x1c8
};
static_assert(sizeof(cRZNetManager) == cRZNetManager::kSize, "cRZNetManager size");
static_assert(offsetof(cRZNetManager, mServiceId) == 0x10, "cRZNetManager layout: the service part at +4");
static_assert(offsetof(cRZNetManager, mUnused20) == 0x20, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mStarted) == 0x24, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mLastError) == 0x28, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mWsaData) == 0x2c, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mVersionMajor) == 0x1bc, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mFlag1c0) == 0x1c0, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mRasOnly) == 0x1c1, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mDialing) == 0x1c2, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mDialTarget) == 0x1c4, "cRZNetManager layout");
static_assert(offsetof(cRZNetManager, mRas) == 0x1c8, "cRZNetManager layout");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx. The image's one caller is
// cTSAppFW::AddApplicationServicesHere (ours), which calls this directly.
struct cRZNetManagerAdapters {
    static cRZNetManager *__fastcall Construct(void *memory, void *edx); // 0xc65932
};
