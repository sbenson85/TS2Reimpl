// Differential test for the IME server (lane KR, src/gz/cGZWinIMEServerW32.h): cGZWinIMEServerW32, its base
// cGZWinIMEServer and the cGZWinIMEKeyMessage it posts, all in the standalone shape.
//
// IMEServer_Run: a server built by the original constructor (W32 0x971ca3 over the base 0x96cb1d and the sub-object
// constructors 0x96c965 / 0x96c913; every fifth seed only the base, 0x96cb1d, game vtables 0xe5a32c...) on 0xc5 memory, or
// by the original factory 0x970d15, against one built by ours (placement new / Create), each driven through its own
// vtables by the same random operations, then destroyed by the last Release (0x96dcf1 / W32 stub 0x971f1e -> Destroy
// 0x96d040: Shutdown, then the +8 face's deleting destructor: W32 thunk 0x971d36 -> 0x971d5d -> 0x971ce8 -> the base
// destructor 0x96cc96; base 0x96cd0e -> 0x96cd79) or by that +8 slot directly.
// Operations: Init 0x96d9c9 / Shutdown 0x96ec45 (+0 stubs 0x971dea / 0x97249e, service face thunks 0x971def / 0x9724a3,
// base 0x96da1b / 0x96ec73); SetEnabled (W32 0x9720d5: the window service, its filter face's slots 4 / 6; base 0x96e179)
// with raw bytes; IsOpen 0x971dae, SetOpen 0x97204f, SetComposing 0x971fff (base 0x96dec3), SetCompositionString
// 0x97208e (and the base's 0x96e0ec), SetCandidateListOpen 0x971f3e (base 0x96ddf8), SelectCandidate 0x971fa6 (base
// 0x96de64), IsCandidateListOpen 0x6ef784 (stub 0x971d9f) and the folded getters through their stubs (0x971da9,
// 0x971de5, 0x971da4); QueryInterface 0x971ed6 (base 0x96dc53) / AddRef (+0 stub 0x971d7f) / Release on every face (thunks 0x971f03,
// 0x971f0b, 0x971f13, 0x971d84, 0x971d8c, 0x971d94, 0x971f23, 0x971f2b, 0x971f33, base 0x96dc7e, 0x96dc86, 0x96dd06,
// 0x96dd0e); the window procedure filter 0x9724ab (+0x9c face) with WM_INPUTLANGCHANGE, the IME messages and others,
// reaching ReadComposition 0x97231a, ReadCandidates 0x9721a6, PostCharacter 0x971df7 (string compare 0x971aa2), the
// views 0x96ddd4 / 0x96dd2c and the clears 0x96ce2d / 0x96ce0f; the never-written +0x98 byte set by the test (both
// sides) so SetCandidateListOpen's open path is reached.
// The key messages (cGZWinIMEKeyMessage: constructor 0x971c81, QueryInterface 0x971e95, destructor 0x971ce1, deleting
// destructor 0x971d3e) are driven by the message server fake through their own vtables, and some kept and released later.
//
// Every object reached is a recording fake (tests/ts_services_fixture.h): the framework (cell 0x10c9378; slot 5 the
// message server 0x64063387, the language service 0x441e5070 (slots 7 / 12), the window service 0xa3460deb (its
// QueryInterface 0xe46397db: slot 4 the window, 6 the filter)), the cached message server (cell 0x10f519c, read by the
// original getter 0x9c5a89 on both sides). Every Win32 call is a logging fake in its import slot: ImmGetContext 0xd9a0cc,
// ImmReleaseContext 0xd9a0e4, ImmGetCompositionStringW 0xd9a0e0, ImmSetCompositionStringW 0xd9a0dc,
// ImmGetCandidateListW 0xd9a0d8, ImmGetOpenStatus 0xd9a0d0, ImmSetOpenStatus 0xd9a0e8, ImmNotifyIME 0xd9a0d4,
// DefWindowProcA 0xd9a524, GetAsyncKeyState 0xd9a548 (read by 0x155860, cCanvasW32::KeyModifiers: ours on our side since lane OQ). The fakes
// write what imm32 writes: up to the bytes asked, the composition data, the attribute bytes, a CANDIDATELIST. The
// game's operator new / delete (0x824632 / 0x824624) and the CRT malloc / free (0x8252b1 / 0x6c4b50, the candidate
// buffer) are ours. Situations where the original dereferences null (the IME messages before Init found both message
// servers) are kept out: those messages are sent only while both are held.
// Left out of the comparison: the composition's hasResult byte right after construction (0x96c965 writes +0x39 but not
// +0x38: the original leaves the allocator's byte); the test writes 0 there on both sides after building, and op 10
// sets it to 0 or 1 on both sides later so the clears' reset of it is seen.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "gzlog_support.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZWinIMEServerW32.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace imeserver_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;
using svcfx::P;
using Server = cGZWinIMEServerW32;
using Base = cGZWinIMEServer;

constexpr uint32_t kTag = 0x7d000000; // the server under test
constexpr uint32_t kInstanceCell = 0x10f4e90;
constexpr uint32_t kCachedServerCell = 0x10f519c;

enum : unsigned { kFramework = 0, kMsgServer, kMsgServer2, kLanguage, kWindowSvc, kWinFilter, kOtherMsg, kFakeCount };
static_assert(kFakeCount <= svcfx::kFakes);

enum : unsigned { svMsg, svLang, svWin, svCount };
constexpr uint32_t kSvcIds[svCount][3] = {
    {Base::kMessageServerId, Base::kMessageServerIid, kMsgServer},
    {0x441e5070, 0x54b7d5, kLanguage},
    {Server::kWindowServiceId, Server::kWindowServiceIid, kWindowSvc},
};

struct World {
    bool framework;
    bool service[svCount];
    bool cachedServer;
    bool filterAnswers, filterWritesOnFail;
    uint32_t himc;
    std::u16string comp, result;
    std::vector<uint8_t> attrs;
    uint32_t cursor;
    std::vector<uint8_t> cand[32];
    uint32_t openStatus, setOpenResult, notifyResult, setCompResult;
    uint32_t reply2, reply3; // what the message server writes into a message's data2 / data3
    int16_t keys[8];
    unsigned queryPick;
    bool hold;
};
World gWorld;
std::mt19937 gHandlerRng;
std::vector<void *> gHeld; // key messages the server fake kept

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }
void PutOut(uint32_t outArg, Obj &o)
{
    ++o.refs;
    *reinterpret_cast<void **>(uintptr_t(outArg)) = &o;
}

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// ---- fakes ----

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    for (unsigned s = 0; s < svCount; ++s) {
        if (a[0] != kSvcIds[s][0] || a[1] != kSvcIds[s][1] || !gWorld.service[s]) continue;
        PutOut(a[2], F(kSvcIds[s][2]));
        return 0x101;
    }
    return 0x100;
}

// The window service's QueryInterface: the filter face (iid 0xe46397db) by the world; a failing query may still write.
uint32_t WindowQuery(Obj *, uint32_t *a)
{
    if (a[0] == Server::kWindowFilterIid && gWorld.filterAnswers) {
        PutOut(a[1], F(kWinFilter));
        return 0x7701;
    }
    if (gWorld.filterWritesOnFail) PutOut(a[1], F(kWinFilter));
    return 0x7700;
}

uint32_t LanguageFromLayout(Obj *, uint32_t *a) { return a[0] ^ 0xf0005a5a; }

// The message server's slot 3: the message's words logged (data2 of the start message points at a 1), then data2 /
// data3 overwritten as a handler may.
uint32_t SendMessage(Obj *, uint32_t *a)
{
    auto *msg = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    Ev(0x7300);
    Ev(msg[0]);
    Ev(Norm(msg[1]));
    if (msg[0] == Server::kMsgStartComposition) Ev(Norm(msg[2]) == 0x5ac00000), Ev(*reinterpret_cast<uint32_t *>(uintptr_t(msg[2])));
    else Ev(msg[2]);
    Ev(msg[3]);
    msg[2] = gWorld.reply2;
    msg[3] = gWorld.reply3;
    return 0x301;
}

// The cached message server's slot 3 (in its table directly: the generic log would print the message's heap address):
// a key message, read and exercised through its own vtable.
uint32_t __fastcall PostMessage2(Obj *, void *, void *msg)
{
    Ev(0x7400);
    for (unsigned s : {4u, 9u, 10u, 11u, 12u}) Ev(Slot<uint32_t>(msg, s));
    static const uint32_t kAsk[5] = {1, cIGZKeyMessage::kIid, cIGZMessage2::kIid, 0x65297976, 0x12345678};
    void *q = reinterpret_cast<void *>(uintptr_t(0xf5e5e5e5));
    const uint32_t ok = Slot<uint32_t>(msg, 0, kAsk[gHandlerRng() % 5], &q) & 0xff;
    Ev(ok), Ev(q == msg ? 1 : P(q));
    if (ok) Ev(Slot<uint32_t>(q, 2));
    Ev(Slot<uint32_t>(msg, 6, &F(kOtherMsg)) & 0xff);
    Ev(Slot<uint32_t>(msg, 7, &F(kOtherMsg)) & 0xff);
    void *created = nullptr;
    Ev(Slot<uint32_t>(msg, 3, 0x1234u, &created) & 0xff), Ev(P(created));
    if (gHandlerRng() % 3 == 0) Ev(Slot<uint32_t>(msg, 5, 0xf0000000 | gHandlerRng() % 0x1000)), Ev(Slot<uint32_t>(msg, 4));
    if (gWorld.hold && gHandlerRng() % 2) {
        Ev(Slot<uint32_t>(msg, 1));
        gHeld.push_back(msg);
    }
    return 0x401;
}

// ---- Win32 import fakes (__stdcall) ----

// A window: a handle the window filter fake made up (0xf0d00000 | n), data on both sides.
uint32_t NormWindow(void *w) { return P(w); }

void *__stdcall FakeImmGetContext(void *window)
{
    Ev(0x7101), Ev(NormWindow(window));
    return reinterpret_cast<void *>(uintptr_t(gWorld.himc));
}
int __stdcall FakeImmReleaseContext(void *window, void *himc)
{
    Ev(0x7102), Ev(NormWindow(window)), Ev(P(himc));
    return 0x7f01;
}
int32_t __stdcall FakeImmGetCompositionString(void *himc, uint32_t index, void *buffer, uint32_t bytes)
{
    Ev(0x7103), Ev(P(himc)), Ev(index), Ev(buffer != nullptr), Ev(bytes);
    const void *source = nullptr;
    uint32_t size = 0;
    switch (index) {
    case 0x10: source = gWorld.attrs.data(), size = uint32_t(gWorld.attrs.size()); break;
    case 8: source = gWorld.comp.data(), size = uint32_t(gWorld.comp.size() * 2); break;
    case 0x800: source = gWorld.result.data(), size = uint32_t(gWorld.result.size() * 2); break;
    case 0x80: return int32_t(gWorld.cursor);
    default: return 0;
    }
    if (!buffer) return int32_t(size);
    const uint32_t n = bytes < size ? bytes : size;
    if (n) std::memcpy(buffer, source, n);
    return int32_t(n);
}
int __stdcall FakeImmSetCompositionString(void *himc, uint32_t index, const void *text, uint32_t bytes, const void *reading,
                                          uint32_t readingBytes)
{
    Ev(0x7104), Ev(P(himc)), Ev(index), Ev(text != nullptr), Ev(bytes), Ev(P(reading)), Ev(readingBytes);
    if (text)
        for (uint32_t i = 0; i < bytes / 2 && i < 16; ++i) Ev(static_cast<const uint16_t *>(text)[i]);
    return int(gWorld.setCompResult);
}
uint32_t __stdcall FakeImmGetCandidateList(void *himc, uint32_t index, void *list, uint32_t bytes)
{
    Ev(0x7105), Ev(P(himc)), Ev(index), Ev(list != nullptr), Ev(bytes);
    const std::vector<uint8_t> &blob = gWorld.cand[index & 31];
    if (!list) return uint32_t(blob.size());
    const uint32_t n = bytes < blob.size() ? bytes : uint32_t(blob.size());
    if (n) std::memcpy(list, blob.data(), n);
    return uint32_t(blob.size());
}
int __stdcall FakeImmGetOpenStatus(void *himc)
{
    Ev(0x7106), Ev(P(himc));
    return int(gWorld.openStatus);
}
int __stdcall FakeImmSetOpenStatus(void *himc, uint32_t open)
{
    Ev(0x7107), Ev(P(himc)), Ev(open);
    return int(gWorld.setOpenResult);
}
int __stdcall FakeImmNotifyIME(void *himc, uint32_t action, uint32_t index, uint32_t value)
{
    Ev(0x7108), Ev(P(himc)), Ev(action), Ev(index), Ev(value);
    return int(gWorld.notifyResult);
}
intptr_t __stdcall FakeDefWindowProc(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam)
{
    Ev(0x7109), Ev(NormWindow(window)), Ev(message), Ev(uint32_t(wParam)), Ev(uint32_t(lParam));
    return 0x5eed;
}
int16_t __stdcall FakeGetAsyncKeyState(int vk)
{
    Ev(0x710a), Ev(uint32_t(vk));
    return gWorld.keys[vk & 7];
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl GameMalloc(size_t n) { return std::malloc(n); }
void __cdecl GameFree(void *p) { std::free(p); }

// ---- the world ----

std::u16string RandomText(std::mt19937 &rng, unsigned maxLength)
{
    static const char16_t kChars[] = {u'a', u'Z', u'0', 0x7f, 0x80, 0xe9, 0x3042, 0x30ab, 0x4e2d, 0xffe5};
    std::u16string s(rng() % (maxLength + 1), 0);
    for (char16_t &c : s) c = kChars[rng() % 10];
    return s;
}

// A CANDIDATELIST: style 1 (read) or 2 (code); a code list with one entry holds the code in its first offset.
std::vector<uint8_t> RandomCandidates(std::mt19937 &rng)
{
    const uint32_t style = 1 + rng() % 2, count = rng() % 4;
    std::vector<std::u16string> items;
    for (uint32_t i = 0; i < count; ++i) items.push_back(RandomText(rng, 3));
    uint32_t size = 0x18 + 4 * (count ? count : 1);
    std::vector<uint32_t> offsets;
    for (const auto &s : items) offsets.push_back(size), size += uint32_t(s.size() + 1) * 2;
    std::vector<uint8_t> blob(size, 0);
    const uint32_t header[6] = {size, style, count, rng() % 5, rng() % 3, 4};
    std::memcpy(blob.data(), header, sizeof(header));
    if (style == 2 && count == 1) offsets[0] = 0x8000 | rng() % 0x100;
    for (uint32_t i = 0; i < count; ++i) std::memcpy(blob.data() + 0x18 + 4 * i, &offsets[i], 4);
    if (!(style == 2 && count == 1))
        for (uint32_t i = 0; i < count; ++i) std::memcpy(blob.data() + offsets[i], items[i].data(), items[i].size() * 2);
    return blob;
}

void RandomWorld(std::mt19937 &rng)
{
    gWorld.framework = rng() % 8 != 0;
    for (bool &s : gWorld.service) s = rng() % 5 != 0;
    gWorld.cachedServer = rng() % 6 != 0;
    gWorld.filterAnswers = rng() % 4 != 0;
    gWorld.filterWritesOnFail = rng() % 2 != 0;
    gWorld.himc = rng() % 5 ? 0xf1c00000 | rng() % 16 : 0;
    gWorld.comp = RandomText(rng, 9);
    gWorld.result = RandomText(rng, 4);
    gWorld.attrs.assign(rng() % 10, 0);
    for (uint8_t &a : gWorld.attrs) a = uint8_t(rng() % 6);
    gWorld.cursor = rng() % 12;
    for (auto &c : gWorld.cand) c.clear();
    for (unsigned i : {0u, 1u, 2u, 5u, 31u})
        if (rng() % 3) gWorld.cand[i] = RandomCandidates(rng);
    gWorld.openStatus = rng() % 3;
    gWorld.setOpenResult = rng() % 3;
    gWorld.notifyResult = rng() % 3;
    gWorld.setCompResult = rng() % 3;
    gWorld.reply2 = rng() % 2 ? 0 : 0xf2000000 | rng() % 64;
    gWorld.reply3 = rng() % 2 ? 0 : 0xf3000000 | rng() % 64;
    for (int16_t &k : gWorld.keys) k = int16_t(rng() % 2 ? 0x8001 : rng() % 2);
    gWorld.hold = rng() % 2 != 0;
}

void ApplyWorld()
{
    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kFramework) : nullptr;
    *GameAddress<void *>(kCachedServerCell) = gWorld.cachedServer ? &F(kMsgServer2) : nullptr;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 13);
    svcfx::ResetAll();
    RandomWorld(rng);
    gHandlerRng.seed(seed);
    gHeld.clear();
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kMsgServer).Set(3, "p", 0, &SendMessage);
    F(kMsgServer2).Set(3, 1, 0x401u);
    F(kMsgServer2).table[3] = reinterpret_cast<void *>(&PostMessage2);
    F(kLanguage).Set(7, 0, 0xf0000000 | rng() % 100);
    F(kLanguage).Set(12, "d", 0, &LanguageFromLayout);
    F(kWindowSvc).Set(0, "dp", 0, &WindowQuery);
    F(kWinFilter).Set(4, 0, 0xf0d00000 | rng() % 100);
    F(kWinFilter).Set(6, "pd", 0x600);
    F(kOtherMsg).Set(4, 0, rng() % 2 ? cGZWinIMEKeyMessage::kType : rng() % 2 ? 0x7a10474f : 0x7a104751);
    ApplyWorld();
}

// ---- building and dumping ----

bool gFactory; // this seed builds through the factory (no 0xc5 fill)
bool gBase;    // this seed builds the base class only

void *Build(difftest::Context &ctx, bool ours)
{
    void *object;
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Server::Create()) : ctx.Original<void *(__cdecl *)()>(0x970d15)();
        object = static_cast<uint8_t *>(face) - 4; // the +4 face (Dump checks every face's vtable from there)
    } else {
        object = ::operator new(Server::kSize);
        std::memset(object, 0xc5, Server::kSize);
        if (gBase) {
            if (ours) ::new (object) Base;
            else ctx.Original<void *(__thiscall *)(void *)>(0x96cb1d)(object);
        } else {
            if (ours) ::new (object) Server;
            else ctx.Original<void *(__thiscall *)(void *)>(0x971ca3)(object);
        }
    }
    static_cast<uint8_t *>(object)[0x6c] = 0; // the composition's hasResult (see the file comment)
    return object;
}

// The containers by size and contents; capacities are the two STLs' growth policies (the game's vector<char> resize
// grows to the size asked, ours by half again), not behaviour.
uint32_t Relation(const void *p, const void *data) { return !p ? 0 : p == data ? 1 : 2; }

void DumpString(Blob &b, const std::u16string &s)
{
    b.push_back(uint32_t(s.size()));
    for (char16_t c : s) b.push_back(c);
}

uint32_t gInstance(bool ours) { return Norm(ours ? P(Server::sInstance) : *GameAddress<uint32_t>(kInstanceCell)); }

// The object's words as Dump logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] = "----dddd dddpp";
void Dump(Blob &b, void *object, bool ours)
{

    const auto *m = static_cast<const uint8_t *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kW32Faces[4][2] = {{0, 0xe5b3c4}, {4, 0xe5b410}, {8, 0xe5b448}, {0x9c, 0xe5b464}};
    static const uint32_t kBaseFaces[3][2] = {{0, 0xe5a32c}, {4, 0xe5a378}, {8, 0xe5a3b0}};
    if (gBase) {
        for (const auto &f : kBaseFaces) b.push_back(ours ^ (Word(m, f[0]) == P(GameAddress<void>(f[1]))));
    } else {
        for (const auto &f : kW32Faces) b.push_back(ours ^ (Word(m, f[0]) == P(GameAddress<void>(f[1]))));
    }
    // The service part and the counts / flags / holders (+0xc..+0x34); the padding only on 0xc5 memory.
    for (uint32_t off = 0xc; off < 0x34; off += 4) {
        const bool byteField = off == 0x1c || off == 0x28;
        if (byteField && gFactory) b.push_back(off == 0x28 ? Word(m, off) & 0xffff : m[off]);
        else svcfx::Field(b, Word(m, off), kObjectKinds, off);
    }
    const auto *server = static_cast<const Base *>(object);
    const cGZIMEComposition &c = server->mComposition;
    b.push_back(Relation(c.text, c.string.data()));
    b.push_back(Relation(c.attributes, c.attributeBuffer.data()));
    b.push_back(c.length), b.push_back(c.cursor), b.push_back(c.language);
    DumpString(b, c.string);
    b.push_back(uint32_t(c.attributeBuffer.size()));
    for (uint8_t a : c.attributeBuffer) b.push_back(a);
    b.push_back(gFactory ? Word(m, 0x6c) & 0xffff : Word(m, 0x6c)); // hasResult, composing (+ padding)
    const cGZIMECandidates &k = server->mCandidates;
    b.push_back(Relation(k.items, k.pointers.data()));
    b.push_back(k.count), b.push_back(k.selection), b.push_back(k.language);
    b.push_back(uint32_t(k.strings.size()));
    for (const auto &s : k.strings) DumpString(b, s);
    b.push_back(uint32_t(k.pointers.size()));
    for (const char16_t *p : k.pointers) {
        uint32_t which = p ? 0xfffe : 0xffff;
        for (size_t j = 0; j < k.strings.size(); ++j)
            if (p == k.strings[j].data()) which = uint32_t(j);
        b.push_back(which);
    }
    b.push_back(gFactory ? m[0x98] : Word(m, 0x98)); // open (+ padding)
    if (!gBase) b.push_back(Word(m, 0xa0)), b.push_back(Word(m, 0xa4)); // +0xa4: a window handle (data)
    b.push_back(gInstance(ours));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    const unsigned n = 10 + rng() % 40;
    // Most seeds start with Init so the filter's messages find the message servers.
    if (rng() % 4) ops.push_back({0, rng(), rng()});
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 19, rng(), rng()});
    return ops;
}

const char16_t kText[12] = {u'k', 0x3042, u'Q', 0, 0x30ab, u'7', 0xe9, u'x', 0x4e2d, u'y', u'z', 0x80};

uint32_t RawBool(uint32_t c)
{
    const uint32_t v = c % 4;
    return (v == 0 ? 0 : v == 1 ? 1 : v == 2 ? 0x80 : 2) | (c & 0xff00);
}

void FilterOp(Blob &b, void *object, const Op &op)
{
    static const uint32_t kMessages[] = {0x51, 0x10d, 0x10e, 0x10f, 0x10f, 0x10f, 0x281, 0x282, 0x282, 0x282,
                                         0x283, 0x284, 0x286, 0x50, 0x10c, 0x300};
    const uint32_t a = op.arg, c = op.arg2;
    const uint32_t message = kMessages[a % 16];
    const uint8_t *m = static_cast<uint8_t *>(object);
    const bool sends = message == 0x10d || message == 0x10e || message == 0x10f || message == 0x282;
    if (sends && (!Word(m, 0x2c) || !Word(m, 0x30))) return; // (the original sends without a test)
    uint32_t wParam = c % 7, lParam = c;
    if (message == 0x282) wParam = (a >> 8) % 16, lParam = c & (1u | 2u | 4u | 0x20u | 0x80000000u);
    if (message == 0x10f) {
        lParam = 0;
        for (uint32_t flag : {0x10u, 8u, 0x800u, 0x80u})
            if (c >> (flag & 0xf) & 1 || (c & 0x10000)) lParam |= flag;
        lParam |= c & 0x40000000;
    }
    void *window = reinterpret_cast<void *>(uintptr_t(0xf0e00000 | (a >> 12) % 4));
    bool handled = (c & 1) != 0; // (cleared first)
    const intptr_t r = Slot<intptr_t>(Face(object, 0x9c), 3, window, message, uintptr_t(wParam), intptr_t(lParam), &handled);
    b.push_back(uint32_t(r)), b.push_back(handled);
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const ime = object;
    void *const service = Face(object, 4);
    void *const mt = Face(object, 8);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: // Init on the IME face or the service face's thunk
        b.push_back(Slot<uint32_t>(a % 2 ? ime : service, a % 2 ? 3 : 8) & 0xff);
        break;
    case 1: // Shutdown
        b.push_back(Slot<uint32_t>(a % 2 ? ime : service, a % 2 ? 4 : 9) & 0xff);
        break;
    case 2: // SetEnabled, IsEnabled
        Slot<void>(ime, 7, RawBool(c));
        b.push_back(Slot<uint32_t>(ime, 6) & 0xff);
        break;
    case 3: // IsOpen, SetOpen
        b.push_back(Slot<uint32_t>(ime, 8) & 0xff);
        b.push_back(Slot<uint32_t>(ime, 9, RawBool(c)) & 0xff);
        break;
    case 4: // IsComposing, SetComposing
        b.push_back(Slot<uint32_t>(ime, 10) & 0xff);
        b.push_back(Slot<uint32_t>(ime, 11, RawBool(c)) & 0xff);
        break;
    case 5: { // SetCompositionString
        const char16_t *text = a % 5 ? kText + a % 3 : nullptr;
        b.push_back(Slot<uint32_t>(ime, 13, text, c % 10) & 0xff);
        break;
    }
    case 6: { // the candidate slots and the getters
        const uint32_t count = uint32_t(static_cast<Base *>(object)->mCandidates.strings.size());
        const uint32_t picks[5] = {0, count - 1, count, count + 1, 0xffffffff};
        b.push_back(Slot<uint32_t>(ime, 14) & 0xff);
        b.push_back(Slot<uint32_t>(ime, 15, RawBool(c)) & 0xff);
        b.push_back(Slot<uint32_t>(ime, 17, picks[a % 5]) & 0xff);
        b.push_back(Slot<uint32_t>(ime, 5) & 0xff);
        b.push_back(Norm(Slot<uint32_t>(ime, 12)));
        b.push_back(Norm(Slot<uint32_t>(ime, 16)));
        break;
    }
    case 7: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cIGZWinIMEServer::kIid, cIGZWinProcFilterW32::kIid, 0x287fb697, 0x12345678};
        void *faces[] = {ime, service, mt, gBase ? mt : Face(object, 0x9c)};
        void *q = reinterpret_cast<void *>(uintptr_t(0xf5e5e5e5));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kAsk[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q), 0xf5e5e5e5));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 8: // the cIGZSystemService accessors and the MT part's count
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        b.push_back(Slot<uint32_t>(mt, 5));
        break;
    case 9: { // the world changes
        std::mt19937 rng(a);
        RandomWorld(rng);
        ApplyWorld();
        break;
    }
    case 10: // the never-written +0x98 byte (the candidate list's open flag)
        static_cast<uint8_t *>(object)[0x98] = a % 3 == 0;
        // and the composition's hasResult (+0x6c, left unset by the ctor 0x96c965), so the clears' reset shows
        if (a >> 8 & 1) static_cast<uint8_t *>(object)[0x6c] = a >> 9 & 1;
        break;
    default:
        if (gBase) RunOp(b, object, {2 + a % 5, a, c});
        else FilterOp(b, object, op);
        break;
    }
}

} // namespace imeserver_test

using namespace imeserver_test;

DIFF_TEST(IMEServer_Run, 0x971ca3)
{
    // Also reached (see the file comment): 0x970d15 (factory), 0x96cb1d, 0x96c965, 0x96c913, 0x971ce8, 0x96cc96,
    // 0x971d5d, 0x971d36, 0x96cd79, 0x96cd0e, 0x96dc53, 0x96dcf1, 0x96d040, 0x96d9c9, 0x96ec45, 0x96da1b, 0x96ec73,
    // 0x96e179, 0x96dec3, 0x96e0ec, 0x6ef784, 0x96ddf8, 0x96de64, 0x96dc7e, 0x96dc86, 0x96dd06, 0x96dd0e, 0x971ed6,
    // 0x971f03, 0x971f0b, 0x971f13, 0x971d84, 0x971d8c, 0x971d94, 0x971f23, 0x971f2b, 0x971f33, 0x971def, 0x9724a3,
    // 0x9720d5, 0x971dae, 0x97204f, 0x971fff, 0x97208e, 0x971f3e, 0x971fa6, 0x9724ab, 0x97231a, 0x9721a6, 0x971df7,
    // 0x971aa2, 0x97283b, 0x96ddd4, 0x96dd2c, 0x96ce2d, 0x96ce0f, 0x971c81, 0x971e95, 0x971ce1, 0x971d3e.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    globals.SetWord(kCachedServerCell, 0);
    globals.SetWord(kInstanceCell, 0);
    gzlog_test::Slots iat(ctx);
    iat.Set(0xd9a0cc, reinterpret_cast<void *>(&FakeImmGetContext));
    iat.Set(0xd9a0e4, reinterpret_cast<void *>(&FakeImmReleaseContext));
    iat.Set(0xd9a0e0, reinterpret_cast<void *>(&FakeImmGetCompositionString));
    iat.Set(0xd9a0dc, reinterpret_cast<void *>(&FakeImmSetCompositionString));
    iat.Set(0xd9a0d8, reinterpret_cast<void *>(&FakeImmGetCandidateList));
    iat.Set(0xd9a0d0, reinterpret_cast<void *>(&FakeImmGetOpenStatus));
    iat.Set(0xd9a0e8, reinterpret_cast<void *>(&FakeImmSetOpenStatus));
    iat.Set(0xd9a0d4, reinterpret_cast<void *>(&FakeImmNotifyIME));
    iat.Set(0xd9a524, reinterpret_cast<void *>(&FakeDefWindowProc));
    iat.Set(0xd9a548, reinterpret_cast<void *>(&FakeGetAsyncKeyState));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch gameMalloc(0x8252b1, shape::CodeOf(&GameMalloc));
    shape::CodePatch gameFree(0x6c4b50, shape::CodeOf(&GameFree));

    bool failed = false;
    for (uint32_t seed = 1; seed <= 3000 && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gBase = seed % 5 == 0;
        gFactory = !gBase && seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            *GameAddress<uint32_t>(kInstanceCell) = 0;
            Server::sInstance = nullptr;
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Server::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    for (void *held : gHeld) b.push_back(Slot<uint32_t>(held, 2)); // the kept key messages let go
                    gHeld.clear();
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it (Shutdown first); every fifth seed the +8 face's deleting destructor.
                    if (seed % 5 != 1) {
                        const uint32_t faces[4] = {0, 4, 8, 0x9c};
                        b.push_back(Slot<uint32_t>(Face(object, faces[(seed >> 3) % (gBase ? 3 : 4)]), 2));
                    } else {
                        b.push_back(Norm(Slot<uint32_t>(Face(object, 8), 3, 1u)));
                    }
                    b.push_back(gInstance(side == 1));
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "IMEServer_Run", seed)) {
                ctx.Fail("  at op %u of %u (kind %u, base %d, factory %d)", unsigned(i), unsigned(ops.size()),
                         i < ops.size() ? ops[i].kind : 99u, int(gBase), int(gFactory));
                for (int side = 0; side < 2; ++side) {
                    std::string line;
                    char word[12];
                    for (uint32_t w : results[side][i]) snprintf(word, sizeof(word), " %x", w), line += word;
                    ctx.Fail("  %s:%s", side ? "ours" : "orig", line.c_str());
                }
                failed = true;
                break;
            }
        }
    }
}
