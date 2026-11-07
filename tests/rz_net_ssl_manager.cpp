// Differential tests for cRZNetSSLManager (src/rz/cRZNetSSLManager.*, lane PW): the original object (the game's
// constructor 0xc644aa, its own vtables) against ours (cRZNetSSLManagerAdapters::Construct, our vtables).
//
// Isolation (docs/lane/test.md): OpenSSL 0.9.7d is library code in the image that both sides call at the same RVAs, so a
// CodePatch over each entry stands in for both sides at once (SSL_library_init, RAND_seed, CRYPTO_num_locks /
// add_lock / the five callback setters, the three SSL methods, SSL_CTX_new / free / load_verify_locations /
// set_default_verify_paths / set_verify / set_tmp_dh_callback / set_verify_depth / ctrl / set_cipher_list,
// SSL_SESSION_free), as does one over the SSL socket's constructor 0xc6524c (a fake socket that logs AddRef and its
// manager). The CRT's _time64 (the image's 0x82fbf6 and ours) and cRZFile::PathExists (0x1c1242 and ours) are stood
// in on both sides; the mutex, thread and process-id imports are impfx fakes. The fakes log through drawfx, and each
// side's log is compared (contexts and sessions are test blocks logged by offset; the lock callbacks each side hands
// OpenSSL are logged as which callback they are, the original's and ours alike).
//
// Sequences: random steps on every slot of every face (Init / Shutdown on both the interface and the service face),
// the fakes' answers scripted per step; after each step a snapshot of the object (the service fields, the flags, the
// mode, the context, the certificate path, every cache entry's key and session, the session reference counts, the lock
// count and the mutex array) is logged. Then the OpenSSL lock callbacks the side registered are called (both mutex
// kinds). Each case ends through one destruction route (Release to zero on each face, the deleting destructor on the
// +8 face, Destroy, the destructor), and every heap block allocated minus freed over the whole case is compared.
//
// The slots the sequences reach through each side's vtables (the original's at these RVAs): QueryInterface 0xc649d3
// (thunks 0xc64a1c +4, 0xc64a24 +8), Release 0xc64a2c (thunks 0xc64a47 +4, 0xc64a4f +8), Init thunk 0xc6480a, Shutdown
// 0xc64d44 (thunk 0xc64d72), SetContext 0x471250 (folded, slot 6), CreateSocket 0xc645a1, SetMode 0xc64c1d, SeedRandom
// 0xc64bd2, SetCertificateFile 0xc64c30, GetSession 0xc645f3, AddSession 0xc64c9e, RemoveSession 0xc64b3f, FreeSession
// 0xc64abb, ClearSessions 0xc64a57, Destroy 0x96d040, the deleting destructor 0xc64582 (thunk 0xc6457a +8); Init and
// Shutdown reach the lock setup 0xc64de3 and teardown 0xc64d7a.
#include "difftest.h"
#include "code_patch.h"
#include "drawfx_core.h"
#include "impfx.h"
#include "service_shape.h"
#include "free_scope.h"

#include "rz/cRZFile.h"
#include "rz/cRZNetSSLManager.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

namespace {

using Manager = cRZNetSSLManager;
using drawfx::Note;
using drawfx::P;

constexpr uint32_t kCtor = 0xc644aa;
constexpr uint32_t kDtor = 0xc64538;
constexpr uint32_t kMutexArray = 0x110303c; // the original's OpenSSL mutex array
const uint32_t kFaces[] = {0, 4, 8};

// ---- the world: the fakes' answers and the test blocks ------------------------------------------------------------------

constexpr uint32_t kSessions = 4;
constexpr uint32_t kSessionSize = 0xa0;
constexpr uint32_t kSessionTag = 0x5e000000;
constexpr uint32_t kContextTag = 0xc7000000;

struct World {
    int numLocks = 3;
    int libInit = 1;
    bool ctxNewOk = true;
    int verifyResult = 1;
    int defaultResult = 1;
    bool exists = true;
    alignas(8) uint8_t sessions[kSessions][kSessionSize];
    alignas(8) uint8_t contexts[2][0x40];
    void *callbacks[5] = {};
    void *object = nullptr; // the manager under test (its +0 face)
};
World gWorld;

int &SessionRefs(uint32_t i) { return *reinterpret_cast<int *>(gWorld.sessions[i] + 0x9c); }
ssl_session_st *Session(uint32_t i) { return reinterpret_cast<ssl_session_st *>(gWorld.sessions[i]); }
ssl_ctx_st *Context(uint32_t i) { return reinterpret_cast<ssl_ctx_st *>(gWorld.contexts[i]); }

uint32_t NormWord(const void *p) { return drawfx::Norm(P(p)); }

// The text of a string (a fixed-size hash keeps the log small).
uint32_t TextHash(const char *s)
{
    if (!s) return 0xffffffffu;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// Which lock callback a pointer is: the original's (0xc64ebd thread id, 0xc64ec3 lock, 0xc64e68 / 0xc64e9e / 0xc64e88
// the dynamic lock's create / lock / destroy) and ours alike, 1..5; 0 for null.
uint32_t CallbackCode(const void *cb)
{
    if (!cb) return 0;
    static const uint32_t original[5] = {0xc64ebd, 0xc64ec3, 0xc64e68, 0xc64e9e, 0xc64e88};
    const void *ours[5] = {reinterpret_cast<const void *>(&netssl::Locks::ThreadId), reinterpret_cast<const void *>(&netssl::Locks::Lock),
                           reinterpret_cast<const void *>(&netssl::Locks::CreateDynamic),
                           reinterpret_cast<const void *>(&netssl::Locks::LockDynamic),
                           reinterpret_cast<const void *>(&netssl::Locks::DestroyDynamic)};
    for (uint32_t i = 0; i < 5; ++i)
        if (cb == GameAddress<void>(original[i]) || cb == ours[i]) return 0x100 + i;
    return 0xbad;
}

// ---- stand-ins (cdecl: OpenSSL's convention) ----------------------------------------------------------------------------

int __cdecl StandLibraryInit()
{
    difftest::NotCounted nc;
    Note("SSL_library_init", 0);
    return gWorld.libInit;
}
void __cdecl StandRandSeed(const void *data, int length)
{
    difftest::NotCounted nc;
    if (length == 16) {
        // The self-seed: the time, the process id, the seed's own address.
        const auto *w = static_cast<const uint32_t *>(data);
        Note("RAND_seed16", {w[0], w[1], w[2], w[3] == P(data) ? 1u : 0u});
    } else {
        Note("RAND_seed", {uint32_t(length), NormWord(data), length > 0 ? *static_cast<const uint8_t *>(data) : 0u});
    }
}
int __cdecl StandNumLocks()
{
    difftest::NotCounted nc;
    Note("CRYPTO_num_locks", 0);
    return gWorld.numLocks;
}
int __cdecl StandAddLock(int *count, int amount, int type, const char *file, int line)
{
    difftest::NotCounted nc;
    Note("CRYPTO_add_lock", {NormWord(count), uint32_t(amount), uint32_t(type), TextHash(file), uint32_t(line)});
    return *count += amount;
}
template <int I> void __cdecl StandSetCallback(void *cb)
{
    difftest::NotCounted nc;
    gWorld.callbacks[I] = cb;
    Note("CRYPTO_set_callback", {uint32_t(I), CallbackCode(cb)});
}
template <uint32_t Token> void *__cdecl StandMethod()
{
    difftest::NotCounted nc;
    Note("method", Token);
    return reinterpret_cast<void *>(uintptr_t(Token));
}
void *__cdecl StandCtxNew(void *method)
{
    difftest::NotCounted nc;
    Note("SSL_CTX_new", P(method));
    return gWorld.ctxNewOk ? Context(0) : nullptr;
}
void __cdecl StandCtxFree(void *ctx)
{
    difftest::NotCounted nc;
    Note("SSL_CTX_free", NormWord(ctx));
}
int __cdecl StandLoadVerify(void *ctx, const char *file, const char *path)
{
    difftest::NotCounted nc;
    Note("load_verify_locations", {NormWord(ctx), TextHash(file), P(path)});
    return gWorld.verifyResult;
}
int __cdecl StandDefaultPaths(void *ctx)
{
    difftest::NotCounted nc;
    Note("set_default_verify_paths", NormWord(ctx));
    return gWorld.defaultResult;
}
void __cdecl StandSetVerify(void *ctx, int mode, void *cb)
{
    difftest::NotCounted nc;
    Note("set_verify", {NormWord(ctx), uint32_t(mode), cb == GameAddress<void>(0xc64f6f) ? 1u : P(cb)});
}
void __cdecl StandSetDh(void *ctx, void *cb)
{
    difftest::NotCounted nc;
    Note("set_tmp_dh_callback", {NormWord(ctx), cb == GameAddress<void>(0xc64ee9) ? 1u : P(cb)});
}
void __cdecl StandVerifyDepth(void *ctx, int depth)
{
    difftest::NotCounted nc;
    Note("set_verify_depth", {NormWord(ctx), uint32_t(depth)});
}
long __cdecl StandCtrl(void *ctx, int cmd, long larg, void *parg)
{
    difftest::NotCounted nc;
    Note("SSL_CTX_ctrl", {NormWord(ctx), uint32_t(cmd), uint32_t(larg), P(parg)});
    return 0x77;
}
int __cdecl StandCipherList(void *ctx, const char *list)
{
    difftest::NotCounted nc;
    Note("set_cipher_list", {NormWord(ctx), TextHash(list)});
    return 1;
}
void __cdecl StandSessionFree(void *session)
{
    difftest::NotCounted nc;
    Note("SSL_SESSION_free", NormWord(session));
    if (session) --*reinterpret_cast<int *>(static_cast<uint8_t *>(session) + 0x9c);
}
__time64_t __cdecl StandTime64(__time64_t *out)
{
    const __time64_t t = 0x123456789abcLL;
    if (out) *out = t;
    return t;
}
bool __cdecl StandPathExists(const cIGZString &path)
{
    difftest::NotCounted nc;
    Note("PathExists", TextHash(reinterpret_cast<const cRZString *>(&path)->ToChar()));
    return gWorld.exists;
}

// The SSL socket: a block with a vtable whose slot 1 (AddRef) and slot 13 (the manager) log.
uint32_t __fastcall SocketAddRef(void *self, void *)
{
    difftest::NotCounted nc;
    Note("socket.AddRef", 0);
    return 1;
}
void __fastcall SocketSetManager(void *self, void *, void *manager)
{
    difftest::NotCounted nc;
    Note("socket.SetManager", manager == gWorld.object ? 1u : NormWord(manager));
}
void *gSocketVTable[14] = {nullptr, reinterpret_cast<void *>(&SocketAddRef), nullptr, nullptr, nullptr, nullptr, nullptr,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&SocketSetManager)};
void *__fastcall StandSocketCtor(void *self, void *)
{
    difftest::NotCounted nc;
    Note("socket.ctor", 0);
    *static_cast<void ***>(self) = gSocketVTable;
    return self;
}

// Every stand-in for one test.
struct StandIns {
    shape::CodePatch libInit{0xfa570, reinterpret_cast<void *>(&StandLibraryInit)};
    shape::CodePatch randSeed{0xf2ee0, reinterpret_cast<void *>(&StandRandSeed)};
    shape::CodePatch numLocks{0xf2010, reinterpret_cast<void *>(&StandNumLocks)};
    shape::CodePatch addLock{0xf22a0, reinterpret_cast<void *>(&StandAddLock)};
    shape::CodePatch setId{0xf21c0, reinterpret_cast<void *>(&StandSetCallback<0>)};
    shape::CodePatch setLocking{0xf21b0, reinterpret_cast<void *>(&StandSetCallback<1>)};
    shape::CodePatch setCreate{0xf2180, reinterpret_cast<void *>(&StandSetCallback<2>)};
    shape::CodePatch setDynLock{0xf2190, reinterpret_cast<void *>(&StandSetCallback<3>)};
    shape::CodePatch setDestroy{0xf21a0, reinterpret_cast<void *>(&StandSetCallback<4>)};
    shape::CodePatch client{0xfa4f0, reinterpret_cast<void *>(&StandMethod<0x3e700001>)};
    shape::CodePatch server{0xf9ca0, reinterpret_cast<void *>(&StandMethod<0x3e700002>)};
    shape::CodePatch any{0xf9480, reinterpret_cast<void *>(&StandMethod<0x3e700003>)};
    shape::CodePatch ctxNew{0xf8500, reinterpret_cast<void *>(&StandCtxNew)};
    shape::CodePatch ctxFree{0xf7840, reinterpret_cast<void *>(&StandCtxFree)};
    shape::CodePatch loadVerify{0xf7fb0, reinterpret_cast<void *>(&StandLoadVerify)};
    shape::CodePatch defaultPaths{0xf7fa0, reinterpret_cast<void *>(&StandDefaultPaths)};
    shape::CodePatch setVerify{0xf7930, reinterpret_cast<void *>(&StandSetVerify)};
    shape::CodePatch setDh{0xf7ff0, reinterpret_cast<void *>(&StandSetDh)};
    shape::CodePatch depth{0xf7950, reinterpret_cast<void *>(&StandVerifyDepth)};
    shape::CodePatch ctrl{0xf73e0, reinterpret_cast<void *>(&StandCtrl)};
    shape::CodePatch cipher{0xf7650, reinterpret_cast<void *>(&StandCipherList)};
    shape::CodePatch sessionFree{0xf8940, reinterpret_cast<void *>(&StandSessionFree)};
    shape::CodePatch socket{0xc6524c, reinterpret_cast<void *>(&StandSocketCtor)};
    shape::StandIn time{0x82fbf6, &::_time64, &StandTime64};
    shape::StandIn exists{0x1c1242, &cRZFile::PathExists, &StandPathExists};
};

// The imports: the mutexes, the thread id, the process id.
void FakeImports(impfx::Imports &imp)
{
    imp.Threads();
    imp.Sync();
    IMPFX_ADD(imp, GetCurrentProcessId).Returns(0x1234);
}

// Each side starts from the same world: fresh sessions (one reference each), no callbacks, no mutex array.
void BeginSide(impfx::Imports &imp, const World &settings)
{
    imp.BeginSide();
    difftest::NotCounted nc;
    gWorld.numLocks = settings.numLocks;
    std::memset(gWorld.sessions, 0, sizeof(gWorld.sessions));
    std::memset(gWorld.contexts, 0, sizeof(gWorld.contexts));
    for (uint32_t i = 0; i < kSessions; ++i) SessionRefs(i) = 1;
    for (void *&cb : gWorld.callbacks) cb = nullptr;
    *GameAddress<void *>(kMutexArray) = nullptr;
    netssl::Locks::sMutexes = nullptr;
    drawfx::AddRange(gWorld.sessions, sizeof(gWorld.sessions), kSessionTag);
    drawfx::AddRange(gWorld.contexts, sizeof(gWorld.contexts), kContextTag);
    drawfx::gLog.reserve(1 << 14);
}

// ---- calls ---------------------------------------------------------------------------------------------------------------

uint32_t Call(void *m, uint32_t face, uint32_t slot, unsigned words, const uint32_t *args)
{
    void *self = static_cast<uint8_t *>(m) + face;
    return svc::CallWords(svc::VTableAt(m, face)[slot], self, words, args);
}

const char *const kNames[] = {"Alpha", "alpha", "beta", "BETA", "", "server.example.com:443/a-long-session-name"};
constexpr uint32_t kNameCount = sizeof(kNames) / sizeof(kNames[0]);
const char *const kPaths[] = {"certs/ca.pem", "", "C:\\Some\\Long\\Path\\To\\The\\Certificates\\ca-bundle.crt"};

enum Op : uint8_t {
    kInit, kShutdown, kGetContext, kSetContext, kCreateSocket, kGetMode, kSetMode, kSeed, kCertificate, kGetSession,
    kAddSession, kRemoveSession, kFreeSession, kClear, kQI, kRef, kService, kCallbacks, kOps
};
const char *const kOpNames[] = {"Init", "Shutdown", "GetContext", "SetContext", "CreateSocket", "GetMode", "SetMode",
                                "Seed", "Certificate", "GetSession", "AddSession", "RemoveSession", "FreeSession", "Clear",
                                "QI", "Ref", "Service", "Callbacks"};

struct Step {
    Op op;
    uint32_t a, b, c;
    int libInit, verifyResult, defaultResult;
    bool ctxNewOk, exists;
};

Step RandomStep(std::mt19937 &rng)
{
    static const Op weighted[] = {kInit, kInit, kInit, kShutdown, kGetContext, kSetContext, kCreateSocket, kGetMode, kSetMode,
                                  kSetMode, kSeed, kCertificate, kCertificate, kGetSession, kGetSession, kAddSession,
                                  kAddSession, kAddSession, kRemoveSession, kFreeSession, kClear, kQI, kRef, kService,
                                  kService, kCallbacks, kCallbacks};
    Step st{};
    st.op = weighted[rng() % (sizeof(weighted) / sizeof(weighted[0]))];
    st.a = rng();
    st.b = rng();
    st.c = rng();
    st.libInit = rng() % 6 == 0 ? 0 : 1;
    st.verifyResult = rng() % 5 == 0 ? 0 : 1;
    st.defaultResult = rng() % 6 == 0 ? 0 : 1;
    st.ctxNewOk = rng() % 6 != 0;
    st.exists = rng() % 3 != 0;
    return st;
}

const uint32_t kIids[] = {1, cIGZNetSSLManager::kIid, cIGZNetSSLManager::kIidAlternate, cIGZSystemService::kIID, 0x12345678, 0};

// The +4 face's service slots: {slot, stack words, result}.
struct ServiceSlot {
    uint32_t slot, words;
    svc::Ret ret;
};
const ServiceSlot kServiceSlots[] = {{3, 0, svc::kWord}, {4, 1, svc::kVoid}, {5, 0, svc::kWord}, {6, 0, svc::kByte},
                                     {7, 1, svc::kVoid}, {10, 1, svc::kByte}, {11, 1, svc::kByte}, {12, 0, svc::kWord}};

void **MutexArray(bool original)
{
    return original ? *GameAddress<void **>(kMutexArray) : netssl::Locks::sMutexes;
}

// The object's state as notes.
void Snap(void *m, bool original)
{
    difftest::NotCounted nc;
    const auto *b = static_cast<const uint8_t *>(m);
    auto word = [b](uint32_t off) { return *reinterpret_cast<const uint32_t *>(b + off); };
    Note("fields", {word(0xc), word(0x10), word(0x14), word(0x18), b[0x1c], word(0x20), b[0x24], word(0x28),
                    drawfx::Norm(word(0x2c))});
    // The layouts are the same (std types are the image's), so the original object reads through our class.
    const Manager *x = static_cast<const Manager *>(m);
    Note("certificate", {uint32_t(x->mCertificateFile.mString.size()), TextHash(x->mCertificateFile.mString.c_str())});
    Note("lock", uint32_t(reinterpret_cast<const cRZCriticalSection *>(&x->mLock)->mBlock->lockCount));
    Note("entries", uint32_t(x->mSessions.size()));
    for (const auto &e : x->mSessions) Note("entry", {TextHash(e.first.mString.c_str()), NormWord(e.second)});
    for (uint32_t i = 0; i < kSessions; ++i) Note("refs", uint32_t(SessionRefs(i)));
    void **mutexes = MutexArray(original);
    Note("mutexes", mutexes ? 1u : 0u);
    if (mutexes)
        for (int i = 0; i < gWorld.numLocks; ++i) Note("mutex", P(mutexes[i]));
}

void Apply(void *m, const Step &st)
{
    gWorld.libInit = st.libInit;
    gWorld.verifyResult = st.verifyResult;
    gWorld.defaultResult = st.defaultResult;
    gWorld.ctxNewOk = st.ctxNewOk;
    gWorld.exists = st.exists;
    const char *name = kNames[st.a % kNameCount];
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
    case kGetContext: Note("GetContext", drawfx::Norm(Call(m, 0, 5, 0, nullptr))); break;
    case kSetContext: {
        const uint32_t a[1] = {st.b % 3 == 0 ? 0u : P(Context(st.b % 3 - 1))};
        Note("SetContext", Call(m, 0, 6, 1, a) & 0xff);
        break;
    }
    case kCreateSocket: {
        void *socket = reinterpret_cast<void *>(uintptr_t(Call(m, 0, 7, 0, nullptr)));
        Note("CreateSocket", socket ? 1u : 0u);
        ::operator delete(socket);
        break;
    }
    case kGetMode: Note("GetMode", Call(m, 0, 8, 0, nullptr)); break;
    case kSetMode: {
        const uint32_t a[1] = {st.b % 8 == 7 ? 0xfffffffeu : st.b % 8};
        Call(m, 0, 9, 1, a);
        break;
    }
    case kSeed: {
        static const uint8_t data[8] = {9, 8, 7, 6, 5, 4, 3, 2};
        const uint32_t a[2] = {st.b % 2 ? P(data) : 0u, (st.c % 3) * 4};
        Call(m, 0, 10, 2, a);
        break;
    }
    case kCertificate: {
        const uint32_t a[1] = {P(kPaths[st.b % 3])};
        Note("SetCertificateFile", Call(m, 0, 11, 1, a) & 0xff);
        break;
    }
    case kGetSession: {
        const uint32_t a[2] = {P(name), st.b % 2};
        Note("GetSession", NormWord(reinterpret_cast<void *>(uintptr_t(Call(m, 0, 12, 2, a)))));
        break;
    }
    case kAddSession: {
        const uint32_t a[2] = {P(Session(st.b % kSessions)), P(name)};
        Note("AddSession", Call(m, 0, 13, 2, a) & 0xff);
        break;
    }
    case kRemoveSession: {
        const uint32_t a[1] = {P(name)};
        Note("RemoveSession", Call(m, 0, 14, 1, a) & 0xff);
        break;
    }
    case kFreeSession: {
        const uint32_t a[1] = {st.b % 5 == 4 ? 0u : P(Session(st.b % 5))};
        Note("FreeSession", Call(m, 0, 15, 1, a) & 0xff);
        break;
    }
    case kClear: Call(m, 0, 16, 0, nullptr); break;
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
        const uint32_t refs = *reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(m) + 0xc);
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
    case kCallbacks: {
        // The lock callbacks the side handed OpenSSL, as OpenSSL would call them (CRYPTO_LOCK 1, CRYPTO_UNLOCK 2, with
        // CRYPTO_READ 4 or CRYPTO_WRITE 8).
        void *const *cb = gWorld.callbacks;
        if (!cb[0] || !cb[1] || !cb[2] || !cb[3] || !cb[4] || gWorld.numLocks == 0) break;
        const uint32_t kinds[] = {0, 4, 8};
        const int extra = int(kinds[st.c % 3]);
        Note("ThreadId", reinterpret_cast<unsigned long(__cdecl *)()>(cb[0])());
        const int index = int(st.b % uint32_t(gWorld.numLocks));
        reinterpret_cast<void(__cdecl *)(int, int, const char *, int)>(cb[1])(1 | extra, index, "file.c", 10);
        reinterpret_cast<void(__cdecl *)(int, int, const char *, int)>(cb[1])(2 | extra, index, "file.c", 11);
        void *dyn = reinterpret_cast<void *(__cdecl *)(const char *, int)>(cb[2])("file.c", 12);
        Note("dynamic", dyn ? 1u : 0u);
        if (dyn) {
            Note("dynamic.mutex", *static_cast<uint32_t *>(dyn));
            reinterpret_cast<void(__cdecl *)(int, void *, const char *, int)>(cb[3])(1 | extra, dyn, "file.c", 13);
            reinterpret_cast<void(__cdecl *)(int, void *, const char *, int)>(cb[3])(2 | extra, dyn, "file.c", 14);
            reinterpret_cast<void(__cdecl *)(void *, const char *, int)>(cb[4])(dyn, "file.c", 15);
        }
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
    return cRZNetSSLManagerAdapters::Construct(memory, nullptr);
}

// Each side's vtable words as codes (the three faces 0..2), the cRZString vtable at +0x30 (the same word on both sides),
// and what a destructor leaves (the service part's bases, 0x20..).
struct Maps {
    svc::CodeMap a, b;
    Maps()
    {
        void *ours = NewOurs(0);
        a.AddRva(Manager::kVTable, 0);
        a.AddRva(Manager::kSystemVTable, 1);
        a.AddRva(Manager::kUnknownVTable, 2);
        for (uint32_t f = 0; f < 3; ++f) b.Add(svc::VTableAt(ours, kFaces[f]), f);
        static_cast<Manager *>(ours)->~Manager();
        ::operator delete(ours);
        svc::AddServiceBaseCodes(a, b, 0x20);
    }
    // From +4 on (after destruction our +0 may hold an interface's vtable the original never stores there); the heap
    // pointers (the lock's block at +0x54, the map head at +0x58) are masked: the caller compares what they point at.
    // `before` (after a destruction): each side's certificate text word (+0x34) before it. The text's tidy-up writes only
    // its first byte (the NUL), so the other three keep the old heap pointer's (or the short text's) bytes: those must be
    // the side's own, the NUL byte compares as usual.
    std::string Compare(const void *x, const void *y, uint32_t from, const uint32_t *before = nullptr) const
    {
        std::vector<uint32_t> wa = svc::Words(x, Manager::kSize, a), wb = svc::Words(y, Manager::kSize, b);
        for (uint32_t i = 0; i < from / 4; ++i) wa[i] = wb[i] = 0;
        wa[0x54 / 4] = wb[0x54 / 4] = 0;
        wa[0x58 / 4] = wb[0x58 / 4] = 0;
        std::string residue;
        if (before) {
            const uint32_t ra = static_cast<const uint32_t *>(x)[0x34 / 4], rb = static_cast<const uint32_t *>(y)[0x34 / 4];
            if ((ra >> 8) != (before[0] >> 8) || (rb >> 8) != (before[1] >> 8)) residue = "+0x34 residue ";
            wa[0x34 / 4] = ra & 0xff;
            wb[0x34 / 4] = rb & 0xff;
        }
        return residue + svc::Diff(wa, wb);
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

struct RawHead {
    RawHead *left, *parent, *right;
    uint8_t color, isnil;
};

} // namespace

// ---- tests ---------------------------------------------------------------------------------------------------------------

// The constructor (the construct-only test of src/app/AppInit.*, moved here and strengthened): fixed and random fills;
// the result is the block, every word compares (the vtables as codes, the padding included; the lock's block and the map
// head checked by content: a lock block of the same heap size with a zero count, an empty self-linked head of the same
// size), the 16 bytes past the object are untouched, and our faces carry our vtables.
DIFF_TEST(cRZNetSSLManager_Construct, kCtor)
{
    constexpr size_t kTail = 16;
    Maps maps;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        uint8_t *ma = static_cast<uint8_t *>(::operator new(Manager::kSize + kTail));
        uint8_t *mb = static_cast<uint8_t *>(::operator new(Manager::kSize + kTail));
        std::mt19937 rng(seed);
        for (size_t i = 0; i < Manager::kSize + kTail; ++i)
            ma[i] = mb[i] = seed <= 4 ? static_cast<uint8_t>(0x55 * (seed - 1)) : static_cast<uint8_t>(rng());
        void *a = ctx.Original<void *(__thiscall *)(void *)>(kCtor)(ma);
        Manager *b = cRZNetSSLManagerAdapters::Construct(mb, nullptr);
        ++ctx.cases;
        if (a != ma || reinterpret_cast<uint8_t *>(b) != mb) ctx.Fail("seed %u: wrong return", seed);
        const std::string diff = maps.Compare(a, b, 0);
        if (!diff.empty()) ctx.Fail("seed %u: object differs at %s", seed, diff.c_str());
        if (std::memcmp(ma + Manager::kSize, mb + Manager::kSize, kTail) != 0) ctx.Fail("seed %u: bytes past the object differ", seed);
        CheckGameVTables(ctx, a);
        for (uint32_t f = 0; f < 3; ++f)
            if (svc::VTableAt(b, kFaces[f]) == GameAddress<void *>(f == 0 ? Manager::kVTable : f == 1 ? Manager::kSystemVTable : Manager::kUnknownVTable))
                ctx.Fail("our face %u has the game's vtable", f);
        const Manager *x = static_cast<const Manager *>(a);
        const auto *la = reinterpret_cast<const cRZCriticalSection *>(&x->mLock), *lb = reinterpret_cast<const cRZCriticalSection *>(&b->mLock);
        if (la->mBlock->lockCount != 0 || lb->mBlock->lockCount != 0) ctx.Fail("seed %u: lock counts", seed);
        if (HeapSize(GetProcessHeap(), 0, la->mBlock) != HeapSize(GetProcessHeap(), 0, lb->mBlock)) ctx.Fail("seed %u: lock block sizes differ", seed);
        const RawHead *ha = *reinterpret_cast<RawHead *const *>(ma + 0x58), *hb = *reinterpret_cast<RawHead *const *>(mb + 0x58);
        if (ha->left != ha || ha->parent != ha || ha->right != ha || ha->color != 1 || ha->isnil != 1) ctx.Fail("original head unexpected");
        if (hb->left != hb || hb->parent != hb || hb->right != hb || hb->color != 1 || hb->isnil != 1) ctx.Fail("our head differs");
        if (HeapSize(GetProcessHeap(), 0, ha) != HeapSize(GetProcessHeap(), 0, hb)) ctx.Fail("seed %u: head sizes differ", seed);
        ctx.Original<void(__thiscall *)(void *)>(kDtor)(a);
        b->~Manager();
        ::operator delete(ma);
        ::operator delete(mb);
    }
}

// The sequences (see the top of the file); each case ends through one destruction route. Per side, the heap blocks and
// bytes allocated minus freed over the whole case (the test's own bookkeeping left out) compare too.
DIFF_TEST(cRZNetSSLManager_Sequences, 0xc64690)
{
    impfx::Imports imp(ctx);
    FakeImports(imp);
    StandIns stands;
    Maps maps;
    for (uint32_t seed = 1; seed <= 160; ++seed) {
        World settings;
        settings.numLocks = int(seed % 4); // 0 locks too
        std::mt19937 rng(seed * 7919);
        std::vector<Step> steps(10 + rng() % 50);
        for (Step &st : steps) st = RandomStep(rng);
        const uint32_t route = rng() % 7, flags = rng() % 2;
        long long blocks[2] = {}, bytes[2] = {};
        bool freed[2] = {};
        uint32_t certWord[2] = {};
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
                Snap(m, original);
            }
            // The end: 0..2 Release to zero on face route, 3 the +8 face's deleting destructor (flags), 4 Destroy,
            // 5..6 the destructor (then the block freed by the test).
            certWord[side] = static_cast<const uint32_t *>(m)[0x34 / 4];
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
            } else if (route == 4) {
                Call(m, 0, 17, 0, nullptr);
            } else if (original) {
                ctx.Original<void(__thiscall *)(void *)>(kDtor)(m);
            } else {
                static_cast<Manager *>(m)->~Manager();
            }
            freed[side] = difftest::FreedSinceBegin(m);
            for (uint32_t i = 0; i < kSessions; ++i) Note("refs.end", uint32_t(SessionRefs(i)));
            Note("mutexes.end", MutexArray(original) ? 1u : 0u);
            counter.Stop();
            blocks[side] = counter.Blocks();
            bytes[side] = counter.Bytes();
        };
        auto a = drawfx::Side([&] { BeginSide(imp, settings); }, [&] { run(0); });
        auto b = drawfx::Side([&] { BeginSide(imp, settings); }, [&] { run(1); });
        if (!drawfx::Same(ctx, a.first, b.first, "sequence", seed, a.second.empty() ? b.second : a.second)) return;
        ++ctx.cases;
        if (freed[0] != freed[1]) ctx.Fail("seed %u route %u: freed %d/%d", seed, route, freed[0], freed[1]);
        if (blocks[0] != blocks[1] || bytes[0] != bytes[1])
            ctx.Fail("seed %u route %u: heap balance %lld blocks %lld bytes / %lld blocks %lld bytes", seed, route, blocks[0], bytes[0], blocks[1], bytes[1]);
        if (!freed[0] && !freed[1]) {
            const std::string diff = maps.Compare(objects[0], objects[1], 4, certWord);
            if (!diff.empty()) ctx.Fail("seed %u route %u: words after destruction differ at %s", seed, route, diff.c_str());
        }
        for (int j = 0; j < 2; ++j)
            if (!freed[j]) ::operator delete(objects[j]);
        *GameAddress<void *>(kMutexArray) = nullptr;
        netssl::Locks::sMutexes = nullptr;
    }
}
