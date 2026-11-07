// The SSL manager service, cRZNetSSLManager (lane PW). The original is speed-built (/O2) code: a .cpp.
#include "rz/cRZNetSSLManager.h"

#include "game/import.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZCriticalSection.h"
#include "rz/RZTextConvert.h"
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "rz/cRZFile.h"
#include "rz/cRZRefCount.h"

#include <cstdlib>
#include <ctime>
#include <new>

#include <windows.h>

namespace {

// ---- OpenSSL 0.9.7d, statically linked into the image (third-party library code: called, never reimplemented) ----------

struct ssl_method_st;
using Method = ssl_method_st *(__cdecl *)();
using VerifyCallback = int(__cdecl *)(int, void *);
using DhCallback = void *(__cdecl *)(void *, int, int);

int SslLibraryInit() { return OriginalFunction<int(__cdecl *)(), 0xfa570>()(); }
void RandSeed(const void *data, int length) { OriginalFunction<void(__cdecl *)(const void *, int), 0xf2ee0>()(data, length); }
int CryptoNumLocks() { return OriginalFunction<int(__cdecl *)(), 0xf2010>()(); }
int CryptoAddLock(int *count, int amount, int type, const char *file, int line)
{
    return OriginalFunction<int(__cdecl *)(int *, int, int, const char *, int), 0xf22a0>()(count, amount, type, file, line);
}
void SetIdCallback(unsigned long(__cdecl *cb)()) { OriginalFunction<void(__cdecl *)(unsigned long(__cdecl *)()), 0xf21c0>()(cb); }
void SetLockingCallback(void(__cdecl *cb)(int, int, const char *, int))
{
    OriginalFunction<void(__cdecl *)(void(__cdecl *)(int, int, const char *, int)), 0xf21b0>()(cb);
}
void SetDynlockCreateCallback(void *(__cdecl *cb)(const char *, int))
{
    OriginalFunction<void(__cdecl *)(void *(__cdecl *)(const char *, int)), 0xf2180>()(cb);
}
void SetDynlockLockCallback(void(__cdecl *cb)(int, void *, const char *, int))
{
    OriginalFunction<void(__cdecl *)(void(__cdecl *)(int, void *, const char *, int)), 0xf2190>()(cb);
}
void SetDynlockDestroyCallback(void(__cdecl *cb)(void *, const char *, int))
{
    OriginalFunction<void(__cdecl *)(void(__cdecl *)(void *, const char *, int)), 0xf21a0>()(cb);
}
ssl_method_st *ClientMethod() { return OriginalFunction<Method, 0xfa4f0>()(); } // mode 1
ssl_method_st *ServerMethod() { return OriginalFunction<Method, 0xf9ca0>()(); } // mode 2 (SSLv23_server_method)
ssl_method_st *AnyMethod() { return OriginalFunction<Method, 0xf9480>()(); }    // otherwise (SSLv23_method)
ssl_ctx_st *CtxNew(ssl_method_st *method) { return OriginalFunction<ssl_ctx_st *(__cdecl *)(ssl_method_st *), 0xf8500>()(method); }
void CtxFree(ssl_ctx_st *ctx) { OriginalFunction<void(__cdecl *)(ssl_ctx_st *), 0xf7840>()(ctx); }
int CtxLoadVerifyLocations(ssl_ctx_st *ctx, const char *file, const char *path)
{
    return OriginalFunction<int(__cdecl *)(ssl_ctx_st *, const char *, const char *), 0xf7fb0>()(ctx, file, path);
}
int CtxSetDefaultVerifyPaths(ssl_ctx_st *ctx) { return OriginalFunction<int(__cdecl *)(ssl_ctx_st *), 0xf7fa0>()(ctx); }
void CtxSetVerify(ssl_ctx_st *ctx, int mode, VerifyCallback cb)
{
    OriginalFunction<void(__cdecl *)(ssl_ctx_st *, int, VerifyCallback), 0xf7930>()(ctx, mode, cb);
}
void CtxSetTmpDhCallback(ssl_ctx_st *ctx, DhCallback cb)
{
    OriginalFunction<void(__cdecl *)(ssl_ctx_st *, DhCallback), 0xf7ff0>()(ctx, cb);
}
void CtxSetVerifyDepth(ssl_ctx_st *ctx, int depth) { OriginalFunction<void(__cdecl *)(ssl_ctx_st *, int), 0xf7950>()(ctx, depth); }
long CtxCtrl(ssl_ctx_st *ctx, int cmd, long larg, void *parg)
{
    return OriginalFunction<long(__cdecl *)(ssl_ctx_st *, int, long, void *), 0xf73e0>()(ctx, cmd, larg, parg);
}
int CtxSetCipherList(ssl_ctx_st *ctx, const char *list)
{
    return OriginalFunction<int(__cdecl *)(ssl_ctx_st *, const char *), 0xf7650>()(ctx, list);
}
void SessionFree(ssl_session_st *session) { OriginalFunction<void(__cdecl *)(ssl_session_st *), 0xf8940>()(session); }

constexpr int kCryptoLockSslSession = 14; // CRYPTO_LOCK_SSL_SESSION
constexpr int kCtrlSetOptions = 32;       // SSL_CTRL_OPTIONS
constexpr int kCtrlSetMode = 33;          // SSL_CTRL_MODE
constexpr long kOptions = 0x1100fff;      // SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_SINGLE_DH_USE
constexpr long kModeAutoRetry = 4;        // SSL_MODE_AUTO_RETRY
constexpr int kVerifyPeer = 1;            // SSL_VERIFY_PEER
constexpr int kVerifyPeerRequired = 3;    // SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
constexpr int kVerifyDepth = 4;
constexpr const char kCipherList[] = "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH";
// The source file the original names to CRYPTO_add_lock (only a lock callback would read it; ours ignores it).
constexpr const char kSourceFile[] = "C:\\dev\\sims2remaster\\DL\\GZFrame\\Net\\RZNetSSL\\source\\RZNetSSLManager.cpp";

int *SessionReferences(ssl_session_st *session) { return reinterpret_cast<int *>(reinterpret_cast<char *>(session) + 0x9c); }

// The game's callbacks handed to OpenSSL (not ours yet: reached only from OpenSSL during handshakes). Addresses handed on
// as data, not called here.
VerifyCallback GameVerifyCallback() { return reinterpret_cast<VerifyCallback>(GameAddress<void>(0xc64f6f)); } // logs a failed certificate
DhCallback GameDhCallback() { return reinterpret_cast<DhCallback>(GameAddress<void>(0xc64ee9)); }             // the PEM DH parameters

// The cRZNetStreamSSLSocket constructor (0xc6524c, thiscall; not ours yet).
void *ConstructSslSocket(void *memory) { return OriginalFunction<void *(__thiscall *)(void *), 0xc6524c>()(memory); }

// The import slots the original calls through.
constexpr uint32_t kIatCreateMutexA = 0xd9a3ec;
constexpr uint32_t kIatReleaseMutex = 0xd9a3f0;
constexpr uint32_t kIatWaitForSingleObject = 0xd9a184;
constexpr uint32_t kIatCloseHandle = 0xd9a17c;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194;
constexpr uint32_t kIatGetCurrentProcessId = 0xd9a1bc;

HANDLE NewMutex() { return ImportFunction<decltype(&CreateMutexA), kIatCreateMutexA>()(nullptr, FALSE, nullptr); }
void Acquire(HANDLE mutex) { ImportFunction<decltype(&WaitForSingleObject), kIatWaitForSingleObject>()(mutex, INFINITE); }
void Free(HANDLE mutex) { ImportFunction<decltype(&ReleaseMutex), kIatReleaseMutex>()(mutex); }
void Close(HANDLE handle) { ImportFunction<decltype(&CloseHandle), kIatCloseHandle>()(handle); }

// The cache's lock, held for a scope (through the lock's vtable, slots 1 and 2, as the original).
struct Locked {
    explicit Locked(cRZCriticalSection *lock) : mLock(As<ifaces::cIGZCriticalSection>(lock)) { mLock->Lock(); }
    ~Locked() { mLock->Unlock(); }
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;
    ifaces::cIGZCriticalSection *mLock;
};

// A cache key for a lookup: the name lowercased (AddSession stores names as given).
struct LowerKey : netssl::SessionKey {
    explicit LowerKey(const char *name) : SessionKey(name) { reinterpret_cast<cRZStringHelpers *>(this)->LowerCase(); }
};

} // namespace

// ---- OpenSSL's thread locks -----------------------------------------------------------------------------------------

void **netssl::Locks::sMutexes = nullptr;

bool netssl::Locks::Setup()
{
    sMutexes = static_cast<void **>(std::malloc(CryptoNumLocks() * sizeof(void *)));
    if (!sMutexes) return false;
    const int count = CryptoNumLocks();
    for (int i = 0; i < count; ++i) sMutexes[i] = NewMutex();
    SetIdCallback(&ThreadId);
    SetLockingCallback(&Lock);
    SetDynlockCreateCallback(&CreateDynamic);
    SetDynlockLockCallback(&LockDynamic);
    SetDynlockDestroyCallback(&DestroyDynamic);
    return true;
}

bool netssl::Locks::Teardown()
{
    if (sMutexes) {
        SetIdCallback(nullptr);
        SetLockingCallback(nullptr);
        SetDynlockCreateCallback(nullptr);
        SetDynlockLockCallback(nullptr);
        SetDynlockDestroyCallback(nullptr);
        const int count = CryptoNumLocks();
        for (int i = 0; i < count; ++i) Close(sMutexes[i]);
        std::free(sMutexes);
        sMutexes = nullptr;
    }
    return true;
}

unsigned long __cdecl netssl::Locks::ThreadId()
{
    return ImportFunction<decltype(&GetCurrentThreadId), kIatGetCurrentThreadId>()();
}

void __cdecl netssl::Locks::Lock(int mode, int index, const char *, int)
{
    if (mode & 1) Acquire(sMutexes[index]);
    else Free(sMutexes[index]);
}

void *__cdecl netssl::Locks::CreateDynamic(const char *, int)
{
    auto *cell = static_cast<HANDLE *>(std::malloc(sizeof(HANDLE)));
    if (cell) *cell = NewMutex();
    return cell;
}

void __cdecl netssl::Locks::LockDynamic(int mode, void *lock, const char *, int)
{
    if (mode & 1) Acquire(*static_cast<HANDLE *>(lock));
    else Free(*static_cast<HANDLE *>(lock));
}

void __cdecl netssl::Locks::DestroyDynamic(void *lock, const char *, int)
{
    Close(*static_cast<HANDLE *>(lock));
    std::free(lock);
}

// ---- construction ---------------------------------------------------------------------------------------------------

cRZNetSSLManager::cRZNetSSLManager() : rz::cRZSystemService(kServiceId, kPriority) {}

// ---- cIGZUnknown ----------------------------------------------------------------------------------------------------

bool cRZNetSSLManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid || riid == kIidAlternate) {
        *out = static_cast<cIGZNetSSLManager *>(this);
    } else if (riid == cIGZSystemService::kIID) {
        *out = static_cast<cIGZSystemService *>(this);
    } else {
        return rz::cRZSystemService::QueryInterface(riid, out);
    }
    AddRef(); // through the vtable, as the original
    return true;
}

uint32_t cRZNetSSLManager::AddRef() { return rz::cRZSystemService::AddRef(); }

uint32_t cRZNetSSLManager::Release()
{
    if (mRefCount > 1) return RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), -1);
    Destroy();
    return 0;
}

// ---- cIGZNetSSLManager ----------------------------------------------------------------------------------------------

bool cRZNetSSLManager::Init()
{
    if (mInitialized) return true;
    // ORIGINAL-BUG: a failed SSL_library_init leaves the lock callbacks registered, and an Init after a failed certificate
    // load sets them up again over the first array (its mutexes leak).
    if (!netssl::Locks::Setup()) return true;
    if (!SslLibraryInit()) return true;
    SeedRandom(nullptr, 0);
    if (mMode != 1 && mMode != 2) mMode = 3;
    ssl_ctx_st *ctx = mContext;
    if (!ctx) {
        ssl_method_st *method = mMode == 1 ? ClientMethod() : mMode == 2 ? ServerMethod() : AnyMethod();
        ctx = CtxNew(method);
        if (!ctx) {
            // ORIGINAL-BUG: marked done without a context.
            mInitialized = true;
            return true;
        }
    }
    mContext = ctx;
    {
        OwnedString file;
        RZText::ConvertStringToString(*mCertificateFile.AsInterface(), *file.AsInterface(), -1);
        if (CtxLoadVerifyLocations(ctx, file.ToChar(), nullptr) != 1 || CtxSetDefaultVerifyPaths(ctx) != 1) return false;
        if (mMode == 2 || mMode == 3) {
            CtxSetVerify(ctx, kVerifyPeer, GameVerifyCallback());
            CtxSetTmpDhCallback(ctx, GameDhCallback());
        } else {
            CtxSetVerify(ctx, kVerifyPeerRequired, GameVerifyCallback());
        }
        CtxSetVerifyDepth(ctx, kVerifyDepth);
        CtxCtrl(ctx, kCtrlSetOptions, kOptions, nullptr);
        CtxSetCipherList(ctx, kCipherList);
        CtxCtrl(ctx, kCtrlSetMode, kModeAutoRetry, nullptr);
    }
    mInitialized = true;
    return true;
}

bool cRZNetSSLManager::Shutdown()
{
    if (mInitialized) {
        mInitialized = false;
        ClearSessions();
        if (mContext) {
            CtxFree(mContext);
            mContext = nullptr;
        }
        netssl::Locks::Teardown();
    }
    return true;
}

ssl_ctx_st *cRZNetSSLManager::GetContext() { return mContext; }

bool cRZNetSSLManager::SetContext(ssl_ctx_st *context)
{
    mContext = context;
    return true;
}

void *cRZNetSSLManager::CreateSocket()
{
    void *memory = ::operator new(0xa4);
    // ORIGINAL-BUG: no test of the constructed socket: a failed allocation crashes on the calls below.
    void *socket = memory ? ConstructSslSocket(memory) : nullptr;
    RawSlot<1>(socket);       // AddRef
    RawSlot<13>(socket, static_cast<cIGZNetSSLManager *>(this)); // the socket's manager
    return socket;
}

uint32_t cRZNetSSLManager::GetMode() { return mMode; }

void cRZNetSSLManager::SetMode(uint32_t mode) { mMode = mode <= 4 ? mode : 3; }

void cRZNetSSLManager::SeedRandom(const void *data, int32_t length)
{
    if (data && length) {
        RandSeed(data, length);
        return;
    }
    // 16 bytes: the time, the process id, and the seed's own address.
    struct {
        __time64_t time;
        DWORD process;
        const void *self;
    } seed;
    seed.time = _time64(nullptr);
    seed.process = ImportFunction<decltype(&GetCurrentProcessId), kIatGetCurrentProcessId>()();
    seed.self = &seed;
    RandSeed(&seed, sizeof(seed));
}

bool cRZNetSSLManager::SetCertificateFile(const char *path)
{
    bool exists;
    {
        netssl::SessionKey text(path);
        exists = cRZFile::PathExists(*text.AsInterface());
    }
    if (exists) mCertificateFile.mString.assign(path);
    return exists;
}

ssl_session_st *cRZNetSSLManager::GetSession(const char *name, bool take)
{
    const LowerKey key(name);
    const auto found = mSessions.find(key);
    if (found == mSessions.end()) return nullptr;
    ssl_session_st *const session = found->second;
    if (!session) return nullptr;
    if (take) mSessions.erase(found);
    else CryptoAddLock(SessionReferences(session), 1, kCryptoLockSslSession, kSourceFile, 0x2e5);
    return session;
}

bool cRZNetSSLManager::AddSession(ssl_session_st *session, const char *name)
{
    const Locked locked(&mLock);
    RemoveSession(name);
    CryptoAddLock(SessionReferences(session), 1, kCryptoLockSslSession, kSourceFile, 0x2ff);
    // ORIGINAL-BUG: the name is stored as given but looked up lowercased, so an entry under a name with capitals is
    // never found, nor removed here; inserting the same name again then keeps the old entry and the reference just added
    // leaks.
    mSessions.insert(netssl::SessionMap::value_type(netssl::SessionKey(name), session));
    return true;
}

bool cRZNetSSLManager::RemoveSession(const char *name)
{
    const Locked locked(&mLock);
    const LowerKey key(name);
    const auto found = mSessions.find(key);
    if (found != mSessions.end()) mSessions.erase(found);
    return false;
}

bool cRZNetSSLManager::FreeSession(ssl_session_st *session)
{
    const Locked locked(&mLock);
    for (auto it = mSessions.begin(); it != mSessions.end(); ++it) {
        if (it->second == session) {
            SessionFree(session);
            mSessions.erase(it);
            return true;
        }
    }
    return false;
}

void cRZNetSSLManager::ClearSessions()
{
    const Locked locked(&mLock);
    for (auto &entry : mSessions) SessionFree(entry.second);
    mSessions.clear();
}

void cRZNetSSLManager::Destroy()
{
    Shutdown();
    delete this;
}

// ---- adapters ---------------------------------------------------------------------------------------------------------

cRZNetSSLManager *__fastcall cRZNetSSLManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZNetSSLManager;
}
