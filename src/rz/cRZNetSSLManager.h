#pragma once
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>

// The SSL manager service (lane PW, milestone I): service 0x2c805c54, priority 0, registered by cTSAppFW. It owns the
// OpenSSL context (SSL_CTX) the game's SSL sockets share, the OpenSSL thread-lock callbacks, and a cache of SSL sessions
// by name. RTTI cRZNetSSLManager; bases cIGZNetSSLManager at +0, cRZSystemService at +4 (its cRZUnknownMT at +8). 0x60
// bytes. Game vtables (1.18.0.187): 0xe9df88 (+0, 18 slots), 0xe9dfd4 (+4, 13), 0xe9e00c (+8, 6); the constructor stores
// 0xe9df40 (the interface's own) at +0 first.
//
// OpenSSL 0.9.7d is third-party library code statically linked into the image: we call it at its RVAs through
// OriginalFunction (listed in the status entries' original_calls), never reimplement it.

struct ssl_ctx_st;     // OpenSSL SSL_CTX (opaque here)
struct ssl_session_st; // OpenSSL SSL_SESSION; its reference count is the int at +0x9c (0.9.7's `references`)

// cIGZNetSSLManager (game vtable 0xe9df40, slots 3-17 _purecall).
class cIGZNetSSLManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x0c7f8b3d;
    static constexpr uint32_t kIidAlternate = 0x223b8812; // answered with this face too

    // slot 3: set up OpenSSL once (locks, library, context, verification, ciphers). True but for a failed load of the
    // certificate file or of the default verify paths (then nothing is marked done, so a later Init tries again).
    virtual bool Init() = 0;
    // slot 4: drop the sessions, free the context, unregister the lock callbacks (when Init had finished). True.
    virtual bool Shutdown() = 0;
    virtual ssl_ctx_st *GetContext() = 0;           // slot 5
    virtual bool SetContext(ssl_ctx_st *context) = 0; // slot 6 (an Init after it uses this context): true
    // slot 7: a new SSL stream socket (cRZNetStreamSSLSocket, 0xa4 bytes), counted once and told this manager.
    virtual void *CreateSocket() = 0;
    virtual uint32_t GetMode() = 0;                  // slot 8: 1 client, 2 server, 3 both (the default once Init ran)
    virtual void SetMode(uint32_t mode) = 0;         // slot 9: modes above 4 become 3
    // slot 10: seed OpenSSL's generator with `length` bytes at `data`; without both, with the time, the process id and
    // a stack address.
    virtual void SeedRandom(const void *data, int32_t length) = 0;
    // slot 11: use `path` as the certificate file (CA locations) when the file exists; returns whether it does.
    virtual bool SetCertificateFile(const char *path) = 0;
    // slot 12: the cached session of `name` (lowercased for the lookup), or null. `take`: removed from the cache and
    // handed over with the cache's reference; else one more reference for the caller.
    virtual ssl_session_st *GetSession(const char *name, bool take) = 0;
    // slot 13: cache `session` under `name` (one reference added), after RemoveSession(name). True.
    virtual bool AddSession(ssl_session_st *session, const char *name) = 0;
    // slot 14: drop the cache entry of `name` (lowercased) without freeing its session. Returns false.
    virtual bool RemoveSession(const char *name) = 0;
    // slot 15: free and drop the first entry holding `session`; true if there was one.
    virtual bool FreeSession(ssl_session_st *session) = 0;
    virtual void ClearSessions() = 0;                // slot 16: every session freed, the cache emptied
    virtual void Destroy() = 0;                      // slot 17: Shutdown, then delete (Release's last reference)
};

namespace netssl {
// A session cache key: a cRZString that can live in a std::map (the copy is the game's cRZString(const cRZString &, 0,
// npos), 0x141eb3; the destructor 0x141193). 0x20 bytes: the original's map node holds the key at +0x10 and the session
// at +0x30 (0x34-byte nodes).
struct SessionKey : cRZString {
    explicit SessionKey(const char *text) { Construct(text); }
    SessionKey(const SessionKey &other) { Construct(other, 0, 0xffffffffu); }
    SessionKey &operator=(const SessionKey &) = delete;
    ~SessionKey() { Destruct(); }
};
static_assert(sizeof(SessionKey) == 0x20);

// The keys compare as their std::strings, case-sensitively (the original's std::less of the text).
struct SessionKeyLess {
    bool operator()(const SessionKey &a, const SessionKey &b) const { return a.mString < b.mString; }
};
using SessionMap = std::map<SessionKey, ssl_session_st *, SessionKeyLess>;
static_assert(sizeof(SessionMap) == 8, "the map is a head pointer and a size");

// OpenSSL's thread locks (the original: 0xc64de3 sets up, 0xc64d7a tears down, the global array at 0x110303c, the
// callbacks 0xc64ebd/0xc64ec3/0xc64e68/0xc64e9e/0xc64e88). One Win32 mutex per OpenSSL lock, plus mutexes made on demand
// for OpenSSL's dynamic locks. Ours: our array, our callbacks (registered with OpenSSL instead of the original's).
struct Locks {
    static void **sMutexes; // CRYPTO_num_locks() mutex handles (malloc'd), or null

    static bool Setup();    // false when the array can't be allocated
    static bool Teardown(); // the callbacks unregistered, the mutexes closed, the array freed; true

    static unsigned long __cdecl ThreadId();                                         // GetCurrentThreadId
    static void __cdecl Lock(int mode, int index, const char *file, int line);       // mode bit 0: lock, else unlock
    static void *__cdecl CreateDynamic(const char *file, int line);                   // a malloc'd cell holding a mutex
    static void __cdecl LockDynamic(int mode, void *lock, const char *file, int line);
    static void __cdecl DestroyDynamic(void *lock, const char *file, int line);
};
} // namespace netssl

// The class in the standalone shape (docs/standalone-shape.md). The game's constructor 0xc644aa is hooked to
// cRZNetSSLManagerAdapters::Construct (placement new of this class), and our cTSAppFW builds it through that adapter: every
// manager carries our vtables. The original's members are reached only from its vtables and from each other, so they are
// in effect through that one hook (trapped by tools/callers.py --write-traps).
class cRZNetSSLManager : public cIGZNetSSLManager, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x60;
    static constexpr uint32_t kVTable = 0xe9df88;          // +0 (the game's, for tests)
    static constexpr uint32_t kSystemVTable = 0xe9dfd4;    // +4
    static constexpr uint32_t kUnknownVTable = 0xe9e00c;   // +8
    static constexpr uint32_t kInterfaceVTable = 0xe9df40; // the interface's own (stored at +0 during construction)
    static constexpr uint32_t kServiceId = 0x2c805c54;
    static constexpr int32_t kPriority = 0;
    static constexpr uint32_t kSystemFace = 4;

    cRZNetSSLManager(); // 0xc644aa
    // 0xc64538: the cache entries (sessions not freed), the lock, the certificate path, the service part. Deleting: 0xc64582
    // (thunk 0xc6457a, +8 slot 3).
    ~cRZNetSSLManager() override = default;
    cRZNetSSLManager(const cRZNetSSLManager &) = delete;
    cRZNetSSLManager &operator=(const cRZNetSSLManager &) = delete;

    // cIGZUnknown, every face (the +4 / +8 ones through the compiler's thunks: QueryInterface 0xc64a1c / 0xc64a24, Release
    // 0xc64a47 / 0xc64a4f, AddRef the shared 0x145248 / 0x145250).
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc649d3
    uint32_t AddRef() override;                              // shared 0x145240 (the service's)
    uint32_t Release() override;                             // 0xc64a2c: Destroy at a count of 1 or below

    // cIGZNetSSLManager (Init / Shutdown also the service's slots 9 / 10, thunks 0xc6480a / 0xc64d72)
    bool Init() override;                                                  // 0xc64690
    bool Shutdown() override;                                              // 0xc64d44
    ssl_ctx_st *GetContext() override;                                     // shared 0x147770 (+0x2c)
    bool SetContext(ssl_ctx_st *context) override;                         // shared_vf 0x471250
    void *CreateSocket() override;                                         // 0xc645a1
    uint32_t GetMode() override;                                           // shared 0x15f390 (+0x28)
    void SetMode(uint32_t mode) override;                                  // 0xc64c1d
    void SeedRandom(const void *data, int32_t length) override;            // 0xc64bd2
    bool SetCertificateFile(const char *path) override;                    // 0xc64c30
    ssl_session_st *GetSession(const char *name, bool take) override;      // 0xc645f3
    bool AddSession(ssl_session_st *session, const char *name) override;   // 0xc64c9e
    bool RemoveSession(const char *name) override;                         // 0xc64b3f
    bool FreeSession(ssl_session_st *session) override;                    // 0xc64abb
    void ClearSessions() override;                                         // 0xc64a57
    void Destroy() override;                                               // 0x96d040

    struct OwnedString : cRZString {
        OwnedString() { Construct(); }
        ~OwnedString() { Destruct(); }
        OwnedString(const OwnedString &) = delete;
        OwnedString &operator=(const OwnedString &) = delete;
    };
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    uint32_t mUnused20 = 0;           // +0x20 cleared by the constructor, read by nothing
    bool mInitialized = false;        // +0x24 Init finished (Shutdown has work to do)
    uint32_t mMode = 1;               // +0x28 1 client, 2 server, 3 both
    ssl_ctx_st *mContext = nullptr;   // +0x2c
    OwnedString mCertificateFile;     // +0x30 the CA file handed to SSL_CTX_load_verify_locations
    OwnedLock mLock;                  // +0x50 the session cache (GetSession does not take it)
    netssl::SessionMap mSessions;     // +0x58
};
static_assert(sizeof(cRZNetSSLManager) == cRZNetSSLManager::kSize, "cRZNetSSLManager size");
static_assert(offsetof(cRZNetSSLManager, mServiceId) == 0x10, "cRZNetSSLManager layout: the service part at +4");
static_assert(offsetof(cRZNetSSLManager, mInitialized) == 0x24, "cRZNetSSLManager layout");
static_assert(offsetof(cRZNetSSLManager, mMode) == 0x28, "cRZNetSSLManager layout");
static_assert(offsetof(cRZNetSSLManager, mContext) == 0x2c, "cRZNetSSLManager layout");
static_assert(offsetof(cRZNetSSLManager, mCertificateFile) == 0x30, "cRZNetSSLManager layout");
static_assert(offsetof(cRZNetSSLManager, mLock) == 0x50, "cRZNetSSLManager layout");
static_assert(offsetof(cRZNetSSLManager, mSessions) == 0x58, "cRZNetSSLManager layout");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx. The image's one caller is
// cTSAppFW::AddApplicationServicesHere (ours), which calls this directly.
struct cRZNetSSLManagerAdapters {
    static cRZNetSSLManager *__fastcall Construct(void *memory, void *edx); // 0xc644aa
};
