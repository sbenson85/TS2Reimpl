// Differential tests for cTSAudioMusicManager (lane KI, src/audio/cTSAudioMusicManager.h): the class in the standalone shape.
//
// AudioMusicManager_Run: a manager built by the original constructor (0x214020, game vtables 0xda8978 / 0xda89d4 / 0xda89e8 /
// 0xda89fc / 0xda8a10 / 0xda8a48) or factory (0x214c96) against one built by ours (the Construct adapter / Create), each
// driven through its own vtables by the same random operations, then destroyed by the last Release (0x215437 reaching the
// +0x14 face's deleting destructor thunk 0x214505, the deleting destructor 0x2145ae and the destructor 0x21436e) or by that
// thunk with flags 0. Operations: Init (0x214f1f, also through the service face's thunk 0x2151eb: the "nomusic" switch,
// no renderer, no service 0x1af85bfb, twice), Shutdown (0x215576, thunk 0x215640), the proxies on and off the renderer's
// thread (slots 5-11: 0x215856, 0x215492, 0x215648, 0x21578a, 0x2158f8, 0x21536b, 0x215294; no renderer, no player),
// the station slots (12-20: 0x214d6e, 0x214d75 with 0..4 stations, short and long names, 0x214e5e, 0x214ded, 0x214cd1
// with 0..3 listed objects, 0x214c3b, 0x214ebd, 0x2151f3, 0x215477), Update (0x215998 with and without the pending flag
// and the player), DoMessage (0x214b85: every handled type and others, the message without cIGZMessage2Standard),
// HandleEvent (0x2145f8: every handled id and others, the event without cITSAudioEvent, the 0x0bb6649b event with each word
// zero, found or not, waiting or not, restarted or not, the 0x2bc250e7 event with and without an object and its face, the
// stream events with names that do and do not start with "stereo" in any case, with and without the word),
// QueryInterface (0x21520e) / AddRef (folded 0x18fd70) / Release (0x215437) on every face (the thunks 0x21526c / 0x215274
// / 0x21528c / 0x21527c / 0x215284, 0x2145d0 / 0x2145d8 / 0x2145f0 / 0x2145e0 / 0x2145e8, 0x21544f / 0x215457 / 0x21546f /
// 0x21545f / 0x215467), the service accessors, the folded slot 21 (0x142870), and the members changing between operations.
//
// Every object the manager reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's
// cell 0x10c9378 and the cache 0x10f5194) with its command line, the renderer, the service 0x1af85bfb, the sound probe
// (0x9c5b7e), the sound manager (GetCachedServiceObjectC's cell 0x10cc648), the events' carrier and message, the listed
// objects, the station state, the player and what it plays. The events are real cTSAudioEvents on both sides (operator new
// 0x210087 and the constructor 0x20ff24 patched to ours, over a real page pool), logged by content. Stood in on both sides
// (the original's code the manager still calls): every worker, the state / playlist constructors (heap fakes), the setup
// 0x216d55 / 0x216a18 (which fills the members), the clears 0x21753e / 0x2174a7 / 0x215a59, 0x21738b, the message
// handlers, the state, object and player members, the message-id walks, the thread id, the log line (cRZStd's slot 2).
// Run for real: the strings (cRZString), the critical section, the containers. Compared after every operation: the result,
// every field (the never-written bytes too, 0xc5), the containers by content, the vtable words, the event log and every
// fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "audio/cTSAudioEvent.h"
#include "audio/cTSAudioMusicManager.h"
#include "game/game.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersA.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZFixedAllocator.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace audiomusic_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{7, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cTSAudioMusicManager;

constexpr uint32_t kTag = 0x7d000000;     // the manager under test
constexpr uint32_t kPageTag = 0x7c000000; // the event pool's pages
constexpr uint32_t kThread = 0x7777;

enum : unsigned {
    kService = 0, kCmdLine, kRenderer, kService38, kProbe, kSoundMgr, kCarrier, kMsg, kObj7C, kIface7C, kState, kListA,
    kListB, kPlayer, kPlayer5C, kPlaying, kSource, kDoMsg, kStdMsg,
};

std::mt19937 gRng;

// ---- the world for a seed ----
struct World {
    bool noMusic, renderer, service38, onThread;
    uint32_t eventId;      // the carrier's slot 4
    bool qiOk;             // the carrier / message answers the interface asked
    uint32_t data[3];      // the carrier's message words
    bool objOk, ifaceOk;   // the 0x2bc250e7 message's object and its face
    uint32_t msgType;      // DoMessage's type
};
World gWorld;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// ---- the event pool (real pages; tests/ts_audio_ambience_manager.cpp) ----
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPages = 256;
constexpr uint32_t kBlocks = 78;
uint8_t *gPages;
uint32_t gNextPage, gHighPage;
RZFixedPage *Page(uint32_t i) { return reinterpret_cast<RZFixedPage *>(gPages + kPageSize * i); }
RZFixedPage *ChainPage(uint32_t p)
{
    RZFixedPage *page = Page(p);
    uint8_t *first = nullptr;
    for (uint32_t k = kBlocks; k-- > 0;) {
        uint8_t *block = gPages + kPageSize * p + 16 + k * 0x34;
        *reinterpret_cast<uint8_t **>(block) = first;
        first = block;
    }
    page->freeList = first;
    page->used = 0;
    page->next = page->prev = page;
    return page;
}
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    if (gNextPage >= kPages) std::abort();
    if (gNextPage > gHighPage) gHighPage = gNextPage;
    return ChainPage(gNextPage++);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *) {}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};
int32_t __fastcall LockLock(void *, void *) { return 1; }
int32_t __fastcall LockUnlock(void *, void *) { return 0; }
void *gLockVT[6] = {nullptr, reinterpret_cast<void *>(&LockLock), reinterpret_cast<void *>(&LockUnlock), nullptr, nullptr, nullptr};
void BuildPool()
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPageSize * kPages, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(gPages, 0x3b, kPageSize * (gHighPage + 1));
    gNextPage = 0;
    gHighPage = 0;
    uint32_t *pool = GameAddress<uint32_t>(cTSAudioEvent::kPool);
    pool[0] = P(gPoolVT);
    pool[1] = P(PoolNewPage(nullptr, nullptr));
    pool[2] = 0;
    *GameAddress<uint32_t>(cTSAudioEvent::kPoolLock) = P(gLockVT);
}

void Text(Blob &b, const std::string &s)
{
    b.push_back(uint32_t(s.size()));
    for (char c : s) b.push_back(uint8_t(c));
}
void Text(Blob &b, const cRZString *s)
{
    if (!s) return b.push_back(0xdead0003);
    b.push_back(s->mVTable == GameAddress(cRZString::kVTable) ? 0x5770 : 0xbad);
    b.push_back(s->mRefCount);
    Text(b, s->mString);
}
void Text(Blob &b, const char *s)
{
    if (!s) return b.push_back(0xdead0004);
    Text(b, std::string(s));
}

void DumpEvent(Blob &b, cITSAudioEvent *e)
{
    if (!e) {
        b.push_back(0xdead0002);
        return;
    }
    b.push_back(0xe7e70000);
    b.push_back(e->GetEventId());
    uint32_t key = 0;
    void *value = nullptr;
    for (bool ok = e->GetFirst(&key, &value); ok; ok = e->GetNext(&key, &value)) {
        b.push_back(key);
        const uint32_t type = (*static_cast<uint32_t(__thiscall ***)(void *)>(value))[4](value);
        b.push_back(type);
        cRZString text;
        text.Construct();
        if (e->GetText(key, text.AsInterface())) Text(b, text.mString);
        else b.push_back(static_cast<const uint32_t *>(value)[1]); // a word (the words past it are not the value's)
        text.Destruct();
    }
}

// ---- heap fakes: the music states (0x239c97, 0x1c bytes) and the playlist manager (0x23a84c, 0x18 bytes) ----
struct HeapFake {
    void **vt;
    int32_t refs;
    uint32_t id;
    uint32_t kind;
};
uint32_t __fastcall HeapQuery(HeapFake *self, void *, uint32_t riid, void **)
{
    Ev(0x4ea70000 | self->kind);
    Ev(self->id);
    Ev(riid);
    return 0;
}
uint32_t __fastcall HeapAddRef(HeapFake *self, void *) { return uint32_t(++self->refs); }
uint32_t __fastcall HeapRelease(HeapFake *self, void *)
{
    if (--self->refs > 0) return uint32_t(self->refs);
    Ev(0x4ea7dead);
    Ev(self->kind);
    Ev(self->id);
    ::operator delete(self);
    return 0;
}
void *gHeapVT[3] = {reinterpret_cast<void *>(&HeapQuery), reinterpret_cast<void *>(&HeapAddRef), reinterpret_cast<void *>(&HeapRelease)};
uint32_t gPlaylistCount;
uint32_t HeapId(const void *p)
{
    if (!p) return 0;
    const auto *h = static_cast<const HeapFake *>(p);
    if (h->vt == gHeapVT) return h->kind << 28 | h->id << 8 | uint32_t(h->refs & 0xff);
    return svcfx::Norm(P(p)) ^ 0x0b000000;
}
HeapFake *__fastcall StandNewState(void *memory, void *, uint32_t id)
{
    Ev(0x239c97);
    Ev(id);
    return ::new (memory) HeapFake{gHeapVT, 0, id & 0xfffff, 1};
}
HeapFake *__fastcall StandNewPlaylist(void *memory, void *)
{
    Ev(0x23a84c);
    return ::new (memory) HeapFake{gHeapVT, 0, ++gPlaylistCount, 2};
}

// ---- stand-ins for the original's code the manager still calls ----
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl StandThread(uint32_t *out) { *out = kThread; }
void *__cdecl StandProbe() { return &F(kProbe); }

uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}

template <class T> T &At(void *self, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(self) + off); }
uint32_t Bit() { return gRng() & 1; }

const char *const kNames[] = {"Rock", "Classical Radio Station Long Name", "", "rock", "Stereo Pop", "x"};

// The members the original's setup and workers fill (written at the original's offsets, the same on both sides).
void FillMembers(void *self, uint32_t r)
{
    std::mt19937 rng(r);
    auto &stations = At<MusicStationList>(self, 0x98);
    for (unsigned n = rng() % 5; n-- > 0;) {
        stations.emplace_back();
        MusicStation &s = stations.back();
        s.idLow = rng() % 4;
        s.idHigh = rng() % 2 ? 0 : 0x10;
        s.name.s.mString = kNames[rng() % 6];
        s.name2.s.mString = kNames[rng() % 6];
    }
    auto &list44 = At<MusicObjectList>(self, 0x44);
    for (unsigned n = rng() % 4; n-- > 0;) {
        const unsigned k = rng() % 3;
        list44.emplace_back(reinterpret_cast<cIGZUnknown *>(k == 0 ? &F(kListA) : k == 1 ? &F(kListB) : nullptr));
    }
    if (rng() % 4) At<AudioRef>(self, 0x60).Assign(reinterpret_cast<cIGZUnknown *>(&F(kPlayer)));
    if (rng() % 2) At<AudioRef>(self, 0x5c).Assign(reinterpret_cast<cIGZUnknown *>(&F(kPlayer5C)));
    for (uint32_t off : {0x80u, 0x88u, 0x90u})
        for (unsigned n = rng() % 3; n-- > 0;) At<MusicIdList>(self, off).push_back(rng() % 100);
    for (uint32_t off : {0xd0u, 0xd8u})
        for (unsigned n = rng() % 3; n-- > 0;) {
            const uint32_t key = rng() % 10;
            At<MusicRefMap>(self, off)[key].Assign(reinterpret_cast<cIGZUnknown *>(rng() % 2 ? &F(kListA) : &F(kListB)));
        }
    At<MusicString>(self, 0xac).s.mString = rng() % 2 ? "" : (rng() % 2 ? "w" : "a waiting name of some length");
    At<uint32_t>(self, 0xa4) = rng() % 2 ? 0 : rng();
    At<uint8_t>(self, 0xa0) = uint8_t(rng());
    At<uint32_t>(self, 0x64) = rng();
    At<uint32_t>(self, 0x68) = rng();
    At<uint8_t>(self, 0xcc) = uint8_t(rng());
    At<uint8_t>(self, 0xcd) = rng() % 3 == 0;
}

void __fastcall StandSetupA(void *self, void *) { Ev(0x216d55); Ev(svcfx::Norm(P(self))); }
void __fastcall StandSetupB(void *self, void *)
{
    Ev(0x216a18);
    Ev(svcfx::Norm(P(self)));
    FillMembers(self, gRng());
}
void __fastcall StandAddState(void *self, void *, cIGZUnknown *state)
{
    Ev(0x2159c2);
    Ev(HeapId(state));
    if (state) At<MusicObjectList>(self, 0x54).emplace_back(state);
}
void __fastcall StandClearA(void *self, void *)
{
    Ev(0x21753e);
    // Sometimes the players are dropped here (so Shutdown's own drops are seen when they are not).
    if (gRng() % 2) At<AudioRef>(self, 0x5c).Assign(nullptr);
    if (gRng() % 2) At<AudioRef>(self, 0x60).Assign(nullptr);
    auto &states = At<MusicObjectList>(self, 0x54);
    while (!states.empty()) {
        Ev(HeapId(states.front().Get()));
        states.pop_front();
    }
}
void __fastcall StandClearB(void *self, void *)
{
    Ev(0x2174a7);
    At<MusicObjectList>(self, 0x44).clear();
}
void __fastcall StandClearC(void *self, void *)
{
    Ev(0x215a59);
    for (uint32_t off : {0x80u, 0x88u, 0x90u}) At<MusicIdList>(self, off).clear();
    At<MusicRefMap>(self, 0xd8).clear();
}
void __fastcall StandPending(void *self, void *)
{
    Ev(0x21738b);
    At<uint32_t>(self, 0xa4) ^= 1;
}
template <uint32_t Rva> uint32_t __fastcall StandMessage(void *self, void *, void *message)
{
    Ev(Rva);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(P(message)));
    if (gRng() % 3 == 0) At<uint8_t>(self, 0xcd) = 1;
    return gRng() & 0x101;
}
void __fastcall Stand2175cb(void *self, void *, uint32_t a, uint32_t flag)
{
    Ev(0x2175cb);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    Ev(flag);
}
uint32_t __fastcall Stand217845(void *self, void *, uint32_t a, uint32_t b, int32_t c, const cRZString *d, const cRZString *e)
{
    Ev(0x217845);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    Ev(b);
    Ev(uint32_t(c));
    Text(svcfx::gEv, d);
    Text(svcfx::gEv, e);
    return gRng() & 0x101;
}
uint32_t __fastcall Stand217be2(void *self, void *, uint32_t a)
{
    Ev(0x217be2);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    return gRng() & 0x101;
}
void __fastcall Stand217c55(void *self, void *) { Ev(0x217c55); Ev(svcfx::Norm(P(self))); }
void __fastcall Stand217490(void *self, void *, uint32_t a, uint32_t flag)
{
    Ev(0x217490);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    Ev(flag);
}
void __fastcall Stand21745e(void *self, void *, uint32_t a, const char *name)
{
    Ev(0x21745e);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    Text(svcfx::gEv, name);
}
uint32_t __fastcall StandNameToWord(void *self, void *, const char *name)
{
    Ev(0x21737b);
    Ev(svcfx::Norm(P(self)));
    Text(svcfx::gEv, name);
    uint32_t h = 0x811c9dc5;
    for (const char *c = name; c && *c; ++c) h = (h ^ uint8_t(*c)) * 0x01000193;
    return h;
}
void __fastcall StandWordToName(void *self, void *, uint32_t word, cRZString *out)
{
    Ev(0x2168f9);
    Ev(svcfx::Norm(P(self)));
    Ev(word);
    Text(svcfx::gEv, out);
    char text[16];
    std::snprintf(text, sizeof text, "n%x", word % 0x1000);
    out->mString = word % 5 ? text : "";
}
uint32_t __fastcall StandFindState(void *self, void *, uint32_t lo, uint32_t hi, void **out)
{
    Ev(0x215c55);
    Ev(svcfx::Norm(P(self)));
    Ev(lo);
    Ev(hi);
    Ev(svcfx::Norm(P(*out)));
    if (lo % 3 == 0) return gRng() & 0x100;
    ++F(kState).refs;
    *out = &F(kState);
    return 1 | (gRng() & 0x100);
}
template <uint32_t Rva> void __fastcall StandStream(void *self, void *, cITSAudioEvent *e, const cRZString *name, uint32_t word)
{
    Ev(Rva);
    Ev(svcfx::Norm(P(self)));
    DumpEvent(svcfx::gEv, e);
    Text(svcfx::gEv, name);
    Ev(word);
}
// the state's, the listed objects' and the player's members
void *__fastcall StandStateList(Obj *state, void *)
{
    Ev(0x237d60);
    Ev(state->id);
    return &F(kState).value[2];
}
uint64_t __fastcall StandState237d27(Obj *state, void *, uint32_t a)
{
    Ev(0x237d27);
    Ev(state->id);
    Ev(a);
    return uint64_t(a) << 32 | (gRng() & 0xffff);
}
void __fastcall StandState237bf4(Obj *state, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Ev(0x237bf4);
    Ev(state->id);
    Ev(a);
    Ev(b);
    Ev(c);
}
uint32_t __fastcall StandState237cfa(Obj *state, void *, uint32_t a, uint32_t b)
{
    Ev(0x237cfa);
    Ev(state->id);
    Ev(a);
    Ev(b);
    return (a ^ b) & 0x101;
}
uint32_t __fastcall StandObject237cb9(Obj *object, void *, uint32_t b, uint32_t c, uint32_t a)
{
    Ev(0x237cb9);
    Ev(object ? object->id : 0);
    Ev(b);
    Ev(c);
    Ev(a);
    return ((a + b + c + (object ? object->id : 0)) % 3 == 0) | (gRng() & 0x100);
}
uint32_t __cdecl Stand233d0a(uint32_t a, uint32_t b, uint32_t c)
{
    Ev(0x233d0a);
    Ev(a);
    Ev(b);
    Ev(c);
    return (a % 4 == 0) | (gRng() & 0x100);
}
void __fastcall StandPlayer23a2f4(Obj *player, void *) { Ev(0x23a2f4); Ev(player ? player->id : 0); }
template <uint32_t Rva> void __fastcall StandPlayerFloat(Obj *player, void *, float value)
{
    Ev(Rva);
    Ev(player ? player->id : 0);
    Ev(Bits(value));
}
void __fastcall StandPlayerUpdate(Obj *player, void *, uint32_t now)
{
    Ev(0x23a6a7);
    Ev(player ? player->id : 0);
    Ev(now);
}
uint32_t __fastcall StandPlayerFind(Obj *player, void *, uint32_t a, void **out)
{
    Ev(0x23a0db);
    Ev(player ? player->id : 0);
    Ev(a);
    Ev(svcfx::Norm(P(*out)));
    if (a % 4 == 0) return gRng() & 0x100;
    ++F(kPlaying).refs;
    *out = &F(kPlaying);
    return 1;
}
uint32_t __fastcall StandPlaying238eef(Obj *playing, void *)
{
    Ev(0x238eef);
    Ev(playing->id);
    return gRng() % 3 == 0;
}
void __fastcall StandRestart239555(Obj *playing, void *) { Ev(0x239555); Ev(playing->id); }

// The log line: cRZStd's slot 2 (cdecl: the object, channel, level, format, arguments: words).
void __cdecl LogLine(cRZStd *self, uint32_t channel, uint32_t level, const char *format, ...)
{
    Ev(0x10600);
    Ev(self->mFirst - P(GameAddress<void>(0)));
    Ev(self->mSecond);
    Ev(channel);
    Ev(level);
    Ev(P(format) - P(GameAddress<void>(0)));
    va_list args;
    va_start(args, format);
    Ev(va_arg(args, uint32_t));
    Text(svcfx::gEv, va_arg(args, const char *));
    va_end(args);
}
struct VtablePatch {
    void **slot;
    void *saved;
    VtablePatch(uint32_t vtable, unsigned index, void *stand) : slot(GameAddress<void *>(vtable + index * 4))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = stand;
    }
    ~VtablePatch() { *slot = saved; }
    VtablePatch(const VtablePatch &) = delete;
    VtablePatch &operator=(const VtablePatch &) = delete;
};

// ---- fake handlers ----
uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x23cecba0 && a[1] == 0x106077d2) return gWorld.renderer ? HandOut(a[2], F(kRenderer)) : 0x500u;
    if (a[0] == 0x1af85bfb && a[1] == 0x3af85bb7) return gWorld.service38 ? HandOut(a[2], F(kService38)) : 0x500u;
    return 0x5100u;
}
uint32_t CommandLineOf(Obj *, uint32_t *) { return P(&F(kCmdLine)); }
uint32_t SwitchPresent(Obj *, uint32_t *a)
{
    Text(svcfx::gEv, reinterpret_cast<const cRZString *>(uintptr_t(a[0])));
    return gWorld.noMusic ? 0x201u : 0x200u;
}
uint32_t RendererThread(Obj *, uint32_t *) { return gWorld.onThread ? kThread : kThread + 1; }
uint32_t TakeEvent(Obj *, uint32_t *a)
{
    DumpEvent(svcfx::gEv, reinterpret_cast<cITSAudioEvent *>(uintptr_t(a[0])));
    return 0x1200u;
}
cITSAudioEvent *gEvent; // the event the carrier hands out
uint32_t CarrierQuery(Obj *, uint32_t *a)
{
    if (a[0] != cITSAudioEvent::kIid || !gWorld.qiOk || !gEvent) return 0x100u;
    gEvent->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = gEvent;
    return 1;
}
uint32_t CarrierId(Obj *, uint32_t *) { return gWorld.eventId; }
uint32_t MsgData(Obj *self, uint32_t *) { return gWorld.data[self->value[0]++ % 3]; }
uint32_t MsgObject(Obj *, uint32_t *) { return gWorld.objOk ? P(&F(kObj7C)) : 0; }
uint32_t Obj7CQuery(Obj *, uint32_t *a) { return gWorld.ifaceOk && a[0] == 0x206077d2 ? HandOut(a[1], F(kIface7C)) : 0x100u; }
uint32_t DoMsgQuery(Obj *, uint32_t *a) { return gWorld.qiOk && a[0] == 0x65297976 ? HandOut(a[1], F(kStdMsg)) : 0x100u; }
uint32_t DoMsgType(Obj *, uint32_t *) { return gWorld.msgType; }

// The playing object's embedded source: slot 5 is its name.
const char *__fastcall SourceName(void *self, void *)
{
    Ev(0x50a0005);
    Ev(svcfx::Norm(P(self)));
    return "Some Track Name";
}
void *gSourceVT[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&SourceName)};

void SetWorld(uint32_t r)
{
    gWorld.noMusic = r % 7 == 0;
    gWorld.renderer = (r >> 3) % 6 != 0;
    gWorld.service38 = (r >> 6) % 6 != 0;
    gWorld.onThread = (r >> 9) % 2 != 0;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 3u);
    svcfx::ResetAll();
    SetWorld(rng());
    gPlaylistCount = 0;

    F(kService).Set(5, "ddp", 0, &GetSystemService);
    F(kService).Set(3, "p", 0x703);
    F(kService).Set(4, "p", 0x704);
    F(kService).Set(21, 0, 0, &CommandLineOf);
    F(kCmdLine).Set(9, "p", 0, &SwitchPresent);
    F(kRenderer).Set(51, 0, 0, &RendererThread);
    F(kRenderer).Set(8, "p", 0, &TakeEvent);
    F(kRenderer).Set(15, "p", 0x1515);
    F(kRenderer).Set(16, "p", 0x1616);
    F(kRenderer).Set(30, "pd", 0x3030);
    F(kRenderer).Set(31, "p", 0x3131);
    F(kProbe).Set(8, "p", 0, &TakeEvent);
    F(kProbe).Set(12, "p", 0, &TakeEvent);
    F(kSoundMgr).Set(17, "p", 0, &TakeEvent);
    F(kCarrier).Set(0, "dp", 0, &CarrierQuery);
    F(kCarrier).Set(4, 0, 0, &CarrierId);
    F(kCarrier).Set(6, 0, P(&F(kMsg)));
    F(kMsg).Set(9, 0, 0, &MsgData);
    F(kMsg).Set(14, 0, 0, &MsgData);
    F(kMsg).Set(19, 0, 0, &MsgData);
    F(kMsg).Set(32, 0, 0, &MsgObject);
    F(kObj7C).Set(0, "dp", 0, &Obj7CQuery);
    F(kDoMsg).Set(0, "dp", 0, &DoMsgQuery);
    F(kDoMsg).Set(4, 0, 0, &DoMsgType);
    F(kPlaying).ret[11] = P(gSourceVT); // the object embedded at +0x58 of what is playing (0x238ec0 returns its address)

    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? &F(kService) : nullptr;
    *GameAddress<void *>(0x10cc648) = &F(kSoundMgr);
}

// ---- driving a manager ----
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

bool gFactory; // this seed builds through the factories (the never-written bytes filled afterwards)
void *gOurVT[6];
constexpr uint32_t kFaces[6] = {0, 4, 8, 0xc, 0x10, 0x14};
constexpr uint32_t kGameVT[6] = {0xda8978, 0xda89d4, 0xda89e8, 0xda89fc, 0xda8a10, 0xda8a48};
// The bytes the constructors never write: [from, to).
constexpr uint32_t kUnwritten[][2] = {{0x29, 0x2c}, {0x31, 0x34}, {0x6c, 0x7c}, {0xa1, 0xa4}, {0xce, 0xd0}};

void *Build(difftest::Context &ctx, bool ours)
{
    void *object;
    if (gFactory) {
        object = ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x214c96)();
        for (const auto &r : kUnwritten) std::memset(Face(object, r[0]), 0xc5, r[1] - r[0]);
    } else {
        object = ::operator new(Mgr::kSize);
        std::memset(object, 0xc5, Mgr::kSize);
        if (ours) cTSAudioMusicManagerAdapters::Construct(object, nullptr);
        else ctx.Original<void *(__thiscall *)(void *)>(0x214020)(object);
    }
    return object;
}

void DumpObjects(Blob &b, const MusicObjectList &list)
{
    b.push_back(uint32_t(list.size()));
    for (const AudioRef &r : list) b.push_back(HeapId(r.Get()));
}
void DumpIds(Blob &b, const MusicIdList &list)
{
    b.push_back(uint32_t(list.size()));
    for (uint32_t v : list) b.push_back(v);
}
void DumpMap(Blob &b, const MusicRefMap &map)
{
    b.push_back(uint32_t(map.size()));
    for (const auto &kv : map) b.push_back(kv.first), b.push_back(HeapId(kv.second.Get()));
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    const auto &mgr = *static_cast<const Mgr *>(object);
    for (int f = 0; f < 6; ++f)
        b.push_back(Word(object, kFaces[f]) == (ours ? P(gOurVT[f]) : P(GameAddress<void>(kGameVT[f]))) ? 0x7a000000u + f : 0xbad);
    for (uint32_t off = 0x18; off < 0x34; off += 4) b.push_back(Word(m, off)); // the service base, the count, the flag
    b.push_back(HeapId(mgr.mRenderer.Get()));
    b.push_back(HeapId(mgr.mService38.Get()));
    b.push_back(uint32_t(mgr.mMessageIds.size()));
    for (uint32_t id : mgr.mMessageIds) b.push_back(id);
    DumpObjects(b, mgr.mList44);
    b.push_back(Word(m, 0x4c) == P(GameAddress<void>(cRZCriticalSection::kVTable)) ? 0x10c4u : 0xbad);
    b.push_back(mgr.mLock.mBlock->lockCount);
    DumpObjects(b, mgr.mStates);
    for (uint32_t off = 0x5c; off < 0x80; off += 4) b.push_back(off == 0x5c || off == 0x60 || off == 0x7c ? HeapId(reinterpret_cast<const AudioRef *>(m + off)->Get()) : Word(m, off));
    DumpIds(b, mgr.mList80);
    DumpIds(b, mgr.mList88);
    DumpIds(b, mgr.mList90);
    b.push_back(uint32_t(mgr.mStations.size()));
    for (const MusicStation &s : mgr.mStations) {
        b.push_back(s.idLow);
        b.push_back(s.idHigh);
        Text(b, &s.name.s);
        Text(b, &s.name2.s);
    }
    for (uint32_t off = 0xa0; off < 0xac; off += 4) b.push_back(Word(m, off));
    Text(b, &mgr.mStringAC.s);
    b.push_back(Word(m, 0xcc));
    DumpMap(b, mgr.mMapD0);
    DumpMap(b, mgr.mMapD8);
    b.push_back(HeapId(mgr.mPlaylistManager.Get()));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};
std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 24;
    if (rng() % 6) ops.push_back({0, rng(), rng()}); // mostly initialised first
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 12, rng(), rng()});
    return ops;
}

const uint32_t kIids[] = {1, 0xabb90bdb, 0xbb523bb2, 0x452294aa, 0x5b34f0af, Mgr::kServiceId, 0x12345678};
const uint32_t kEventIds[] = {0x6bbf8a5a, 0x4c322bef, 0x2bc250e7, 0x0bb6649b, 0x6c322bf4, 0x6ea4b702, 0x6ea4b706,
                              0xac33830d, 0x2bc78968, 0xcbc78960, 0xcc313a42, 0x0bb6649a, 0xcbc78961, 0};
const uint32_t kMsgTypes[] = {0xb3, 0xb4, 0xb5, 0xb6, 0x2bea2063, 0x4c3220e7, 0x4c8e7905, 0xb2, 0xb7, 0x4c8e7904, 0};
const char *const kStreamNames[] = {"Stereo Track", "stereo", "STEREO", "mono", "stere", "", "StErEoX", " stereo"};
const float kFloats[] = {0.0f, 0.5f, -1.25f, 100.0f, std::numeric_limits<float>::infinity()};

// A real event for the carrier, its values set as the case reads them (some left out).
cITSAudioEvent *MakeEvent(uint32_t id, uint32_t r)
{
    cITSAudioEvent *e = new cTSAudioEvent(id);
    e->AddRef();
    const uint32_t keys[] = {0xcc313a33, 0x8d696ffd, 0x8c322c22, 0x8d80f069, 0x8c322c25, 0xabd5d0a2};
    for (unsigned i = 0; i < 6; ++i)
        if ((r >> i) % 4) e->SetUint32(keys[i], (r >> (i * 3)) % 3 == 0 ? 0 : r * (i + 1));
    if ((r >> 7) % 3) e->SetSint32(0xad80f06f, int32_t(r) >> 8);
    cRZString text;
    text.Construct(kNames[(r >> 10) % 6]);
    if ((r >> 13) % 3) e->SetText(0x8c322c25, text.AsInterface());
    text.mString = kNames[(r >> 15) % 6];
    if ((r >> 18) % 3) e->SetText(0x8c322c27, text.AsInterface());
    text.mString = kStreamNames[(r >> 20) % 8];
    if ((r >> 23) % 5) e->SetText(0x2bcb73fc, text.AsInterface());
    text.Destruct();
    return e;
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const handler = Face(object, 4);
    void *const target = Face(object, 8);
    void *const messages = Face(object, 0xc);
    void *const service = Face(object, 0x10);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: b.push_back((a % 3 ? Slot<uint32_t>(primary, 3) : Slot<uint32_t>(service, 8)) & 0xff); break;
    case 1: // Shutdown (not after an Init that found no renderer: the original reads the renderer's vtable unchecked, 0x2155ba /
            // 0x2155c3 for slot 31 at 0x2155c8, again at 0x2155cb / 0x2155d4 for slot 16, and faults on a null one)
        if (Face(object, 0x30)[0] && !Word(object, 0x34)) break;
        b.push_back((a % 3 ? Slot<uint32_t>(primary, 4) : Slot<uint32_t>(service, 9)) & 0xff);
        break;
    case 2: { // the proxies
        gWorld.onThread = (c >> 4) % 2 != 0;
        cRZString d, e;
        d.Construct(kNames[(c >> 8) % 6]);
        e.Construct(kNames[(c >> 11) % 6]);
        switch (a % 7) {
        case 0: Slot<void>(primary, 5); break;
        case 1: Slot<void>(primary, 6, c, uint32_t(a >> 8), uint32_t((a >> 4) % 3 ? 0x100u : 0x101u)); break;
        case 2: Slot<void>(primary, 7, c, a >> 3, int32_t(a) >> 12, static_cast<const cIGZString *>(d.AsInterface()),
                           static_cast<const cIGZString *>(e.AsInterface())); break;
        case 3: Slot<void>(primary, 8, c); break;
        case 4: Slot<void>(primary, 9); break;
        case 5: Slot<void>(primary, 10, c, uint32_t(a >> 8)); break;
        default: Slot<void>(primary, 11, c, kNames[(a >> 5) % 6]); break;
        }
        e.Destruct();
        d.Destruct();
        break;
    }
    case 3: { // the station slots
        const uint32_t lo = c % 4, hi = (c >> 2) % 2 ? 0 : 0x10;
        switch (a % 9) {
        case 0: b.push_back(Slot<uint32_t>(primary, 12) - P(object)); break;
        case 1: {
            cRZString name;
            name.Construct(kNames[(c >> 4) % 6]);
            const uint64_t id = Slot<uint64_t>(primary, 13, static_cast<const cIGZString *>(name.AsInterface()));
            b.push_back(uint32_t(id));
            b.push_back(uint32_t(id >> 32));
            name.Destruct();
            break;
        }
        case 2: b.push_back(svcfx::Norm(Slot<uint32_t>(primary, 14, lo, hi))); break;
        case 3: {
            const uint64_t v = Slot<uint64_t>(primary, 15, lo, hi, c >> 8);
            b.push_back(uint32_t(v));
            b.push_back(uint32_t(v >> 32));
            break;
        }
        case 4: b.push_back(Slot<uint32_t>(primary, 16, a >> 4, c, c >> 16) & 0xff); break;
        case 5: Slot<void>(primary, 17, lo, hi, a, a >> 8, c >> 8); break;
        case 6: b.push_back(Slot<uint32_t>(primary, 18, lo, hi, a >> 4, c >> 4) & 0xff); break;
        case 7: Slot<void>(primary, a % 2 ? 19 : 20, kFloats[(c >> 4) % 5]); break;
        default: b.push_back(HeapId(Slot<void *>(primary, 21))); break;
        }
        break;
    }
    case 4: // Update
        Slot<void>(target, 3, c);
        break;
    case 5: // DoMessage
        gWorld.qiOk = a % 5 != 0;
        gWorld.msgType = kMsgTypes[c % 11];
        b.push_back(Slot<uint32_t>(messages, 3, &F(kDoMsg)) & 0xff);
        break;
    case 6:
    case 7: { // HandleEvent
        gWorld.eventId = kEventIds[c % 14];
        gWorld.qiOk = a % 7 != 0;
        for (unsigned i = 0; i < 3; ++i) gWorld.data[i] = (a >> (4 + i)) % 4 == 0 ? 0 : a * (i + 3);
        gWorld.objOk = (a >> 8) % 4 != 0;
        gWorld.ifaceOk = (a >> 10) % 3 != 0;
        F(kMsg).value[0] = 0;
        gEvent = (a >> 12) % 9 ? MakeEvent(gWorld.eventId, c * 2654435761u + a) : nullptr;
        if (svcfx::gTrace) fprintf(stderr, "event %08x qi %d made %d\n", gWorld.eventId, gWorld.qiOk, gEvent != nullptr);
        b.push_back(Slot<uint32_t>(handler, 3, &F(kCarrier)) & 0xff);
        if (gEvent) {
            DumpEvent(b, gEvent);
            gEvent->Release();
            gEvent = nullptr;
        }
        break;
    }
    case 8: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, handler, target, messages, service, Face(object, 0x14)};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 6], 0, kIids[(a >> 4) % 7], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 6], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 6], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 9: // the service face's accessors, the base's count
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        b.push_back(Slot<uint32_t>(Face(object, 0x14), 5));
        if (a % 3 == 0) Slot<void>(service, 4, c);
        Slot<void>(service, 7, uint32_t(a % 2 ? 0x101u : 0x200u));
        break;
    case 10: // the original's workers change the members
        FillMembers(object, a);
        break;
    default: // the world changes
        SetWorld(a);
        break;
    }
}

} // namespace audiomusic_test

using namespace audiomusic_test;

DIFF_TEST(AudioMusicManager_Run, 0x214020)
{
    // Also reached (see the file comment): 0x214c96 (factory), 0x21436e, 0x2145ae, 0x214505, 0x21520e, 0x18fd70,
    // 0x215437, 0x214f1f, 0x2151eb, 0x215576, 0x215640, 0x215856, 0x215492, 0x215648, 0x21578a, 0x2158f8, 0x21536b,
    // 0x215294, 0x214d6e, 0x214d75, 0x214e5e, 0x214ded, 0x214cd1, 0x214c3b, 0x214ebd, 0x2151f3, 0x215477, 0x142870,
    // 0x2145f8, 0x215998, 0x214b85, the adjustor thunks 0x21526c, 0x215274, 0x21528c, 0x21527c, 0x215284, 0x2145d0,
    // 0x2145d8, 0x2145f0, 0x2145e0, 0x2145e8, 0x21544f, 0x215457, 0x21546f, 0x21545f, 0x215467.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10f5194u, 0x10cc648u, cTSAudioEvent::kPool, cTSAudioEvent::kPool + 4,
                          cTSAudioEvent::kPool + 8, cTSAudioEvent::kPoolLock})
        globals.SetWord(cell, 0);
    globals.SetWord(0xff5f18, 0x10f6);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch eventNew(0x210087, reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&cTSAudioEvent::operator new)));
    shape::CodePatch eventCtor(0x20ff24, shape::CodeOf(&cTSAudioEventAdapters::Construct));
    shape::StandIn thread(0x1b9cad, &nGZLog::StoreCurrentThreadId, &StandThread);
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::CodePatch probe(0x9c5b7e, shape::CodeOf(&StandProbe));
    shape::CodePatch newState(0x239c97, shape::CodeOf(&StandNewState));
    shape::CodePatch newPlaylist(0x23a84c, shape::CodeOf(&StandNewPlaylist));
    shape::CodePatch setupA(0x216d55, shape::CodeOf(&StandSetupA));
    shape::CodePatch setupB(0x216a18, shape::CodeOf(&StandSetupB));
    shape::CodePatch addState(0x2159c2, shape::CodeOf(&StandAddState));
    shape::CodePatch clearA(0x21753e, shape::CodeOf(&StandClearA));
    shape::CodePatch clearB(0x2174a7, shape::CodeOf(&StandClearB));
    shape::CodePatch clearC(0x215a59, shape::CodeOf(&StandClearC));
    shape::CodePatch pending(0x21738b, shape::CodeOf(&StandPending));
    shape::CodePatch msgA(0x216270, shape::CodeOf(&StandMessage<0x216270>));
    shape::CodePatch msgB(0x2162fd, shape::CodeOf(&StandMessage<0x2162fd>));
    shape::CodePatch msgC(0x21670f, shape::CodeOf(&StandMessage<0x21670f>));
    shape::CodePatch msgD(0x215ff8, shape::CodeOf(&StandMessage<0x215ff8>));
    shape::CodePatch w2175cb(0x2175cb, shape::CodeOf(&Stand2175cb));
    shape::CodePatch w217845(0x217845, shape::CodeOf(&Stand217845));
    shape::CodePatch w217be2(0x217be2, shape::CodeOf(&Stand217be2));
    shape::CodePatch w217c55(0x217c55, shape::CodeOf(&Stand217c55));
    shape::CodePatch w217490(0x217490, shape::CodeOf(&Stand217490));
    shape::CodePatch w21745e(0x21745e, shape::CodeOf(&Stand21745e));
    shape::CodePatch nameToWord(0x21737b, shape::CodeOf(&StandNameToWord));
    shape::CodePatch wordToName(0x2168f9, shape::CodeOf(&StandWordToName));
    shape::CodePatch findState(0x215c55, shape::CodeOf(&StandFindState));
    shape::CodePatch streamA(0x2163d2, shape::CodeOf(&StandStream<0x2163d2>));
    shape::CodePatch streamB(0x2166ba, shape::CodeOf(&StandStream<0x2166ba>));
    shape::CodePatch stateList(0x237d60, shape::CodeOf(&StandStateList));
    shape::CodePatch state237d27(0x237d27, shape::CodeOf(&StandState237d27));
    shape::CodePatch state237bf4(0x237bf4, shape::CodeOf(&StandState237bf4));
    shape::CodePatch state237cfa(0x237cfa, shape::CodeOf(&StandState237cfa));
    shape::CodePatch object237cb9(0x237cb9, shape::CodeOf(&StandObject237cb9));
    shape::CodePatch static233d0a(0x233d0a, shape::CodeOf(&Stand233d0a));
    shape::CodePatch player23a2f4(0x23a2f4, shape::CodeOf(&StandPlayer23a2f4));
    shape::CodePatch player23a25e(0x23a25e, shape::CodeOf(&StandPlayerFloat<0x23a25e>));
    shape::CodePatch player23a457(0x23a457, shape::CodeOf(&StandPlayerFloat<0x23a457>));
    shape::CodePatch playerUpdate(0x23a6a7, shape::CodeOf(&StandPlayerUpdate));
    shape::CodePatch playerFind(0x23a0db, shape::CodeOf(&StandPlayerFind));
    shape::CodePatch playing238eef(0x238eef, shape::CodeOf(&StandPlaying238eef));
    shape::CodePatch restart239555(0x239555, shape::CodeOf(&StandRestart239555));
    VtablePatch logLine(cRZStd::kVTable, 2, reinterpret_cast<void *>(&LogLine));

    { // our vtables, from an object our constructor built
        alignas(8) uint8_t scratch[Mgr::kSize];
        Mgr *made = cTSAudioMusicManagerAdapters::Construct(scratch, nullptr);
        for (int f = 0; f < 6; ++f) gOurVT[f] = reinterpret_cast<void *>(uintptr_t(Word(scratch, kFaces[f])));
        made->~Mgr();
    }

    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            BuildPool();
            svcfx::AddRange(gPages, kPageSize * kPages, kPageTag);
            gRng.seed(seed);
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            for (uint32_t &w : svcfx::gEv) // the factory's construction ran before the object's range was known
                if (w >= P(object) && w < P(object) + Mgr::kSize) w = kTag | (w - P(object));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else if (seed % 5 == 0) {
                    // The +0x14 face's deleting destructor (thunk 0x214505) with flags 0: destroyed, not freed.
                    b.push_back(svcfx::Norm(Slot<uint32_t>(Face(object, 0x14), 3, 0u)));
                    ::operator delete(object);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioMusicManager_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                return;
            }
        }
    }
}
