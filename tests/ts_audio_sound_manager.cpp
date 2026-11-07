// Differential tests for cTSAudioSoundManager (lane IY, src/audio/cTSAudioSoundManager.h): the whole class in the
// standalone shape.
//
// AudioSoundManager_Run: a manager built by the original constructor (0x2188d6, game vtables 0xda92e0 / 0xda9434 /
// 0xda9448 / 0xda945c / 0xda9470 / 0xda9484) or factory (0x21a182) against one built by ours (the Construct adapter /
// Create), each driven through its own vtables by the same random operations, then destroyed by the last Release (0x21b544
// -> 0x21b4fb, the deleting destructor 0x218f2f, the destructor 0x218cf9). Operations: Init (0x21a649, also through the
// service face's thunk 0x21aa33), Shutdown (0x21c8df, thunk 0x21cae7), PostEvent (0x21b0a9), the thread proxies 18..37 and
// 42..54, 59 (0x21bc54, 0x21b8e7, 0x219ef7, 0x219124, 0x219247, 0x21c54f, 0x21c3b4, 0x21cbcf, 0x21cce7, 0x21cf79, 0x21d047,
// 0x21ceac, 0x21afb7, 0x21cddb, 0x21aeea, 0x21a0c9, 0x21c61f, 0x21c484, 0x21c6ea, 0x21d1c0, 0x21c7eb, 0x21b716, 0x21bf88,
// 0x21bd43, 0x21bb79, 0x21b9c2, 0x21c21e, 0x21c143, 0x21c063, 0x21b81a, 0x21b65d, 0x21c2f9, 0x21ba9d, 0x21b41b) on and off
// the renderer's thread, the queries 39 / 40 / 41 / 55 (0x21a252, 0x21a36d, 0x21caef, 0x21aad8), NewInstanceId (0x21a1bd,
// the counter at the 0x20000000 wrap), the speaker modes (0x21aa95, 0x21beb3 with modes 0..5, 0x21be44), GetSoundName
// (0x21ac60: every path), GetCategoryLogChannel (0x21abb8, categories 0..10), the getters 62..82 (0x15e970 folded, 0x21aed6,
// 0x219ff8, 0x21b571, 0x21aec8, 0x21aecf, 0x21b578, 0x21b12a, 0x21b5aa, 0x21934b, 0x21a4b1, 0x21a497, 0x21b5a3, 0x21a1d5
// with categories 0..9 and the enabled flags, 0x21a206, 0x21a642, 0x21a231, 0x21a244, 0x21a24b, 0x21b59c, 0x218f71),
// HandleEvent (0x219430: the renderer's types 1 / 2 / 0x6ebaf256 and every event id), Update (0x21d15c with the ramps of
// 0x21d114 at rates around the constant at 0xd9e82c), OnSoundComplete (0x219385), DoMessage (0x219b33: every message
// type), OnTick (0x21aedd), FlushPendingEvents (0x21d5e9), the behaviour slots (OnEvent 0x21a4f8 with the base 0x21a4cb,
// 0x21b5c4, 0x219fff, 0x2190ba, 0x219365, 0x21aebf, 0x218f78, 0x219082, GetEventManager 0x2190da), QueryInterface
// (0x21b144) / AddRef (the folded body 0x1a9a25) / Release on every face (the thunks 0x21b1db, 0x21b1fb, 0x21b1e3, 0x21b1eb, 0x21b1f3, 0x218f69,
// 0x218f51, 0x218f59, 0x218f61, 0x21b549, 0x21b569, 0x21b551, 0x21b559, 0x21b561) and the service accessors.
//
// Every object the manager reaches is a recording fake (tests/ts_services_fixture.h). The events are ours on both sides
// (the original's operator new 0x210087 and constructor 0x20ff24 patched to ours, from a pool of real pages) and logged by
// content wherever the renderer gets one. The original callees are stood in for on the image (both sides call them): the
// workers of the requests and of HandleEvent, the registry 0x21b370, the id iterators 0x1a9942 / 0x1a98e0 (and ours of 0x1a98e0), the curves
// 0xc0a6a1 / 0xc0a86a, the memory buffer's constructor / destructor 0x23b4a8 / 0x23b4ce and load 0x21b203, the namers
// 0x21efa5 / 0x21fd55; and on both sides: the thread id 0x1b9cad, the thread signal 0x9ccaa9 / 0x9ccb13, the message-id
// walks 0x1c0458 / 0x1c081e, cRZString::Sprintf 0x1ad6f7 and the log line. The startup workers (LoadSoundDefaults,
// ApplySettings, UpdateSpeakerConfig, UpdateSounds) are ours on both sides here (the persist resource manager never loads
// the defaults; no settings service; no config object; no sounds): their own tests are at the end of the file.
// Compared after every operation: the result, the event log, every field (the never-written bytes too: both blocks start
// as 0xc5), the containers by content (the pending events by content) and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "audio/cTSAudioEvent.h"
#include "audio/cTSAudioSoundManager.h"
#include "game/game.h"
#include "gz/cAttributeIDIterator.h"
#include "gz/cGZAny.h"
#include "gz/cGZEventReflector.h"
#include "gz/cGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersA.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZFixedAllocator.h"
#include "rz/cRZString.h"
#include "rz/cRZThreadSignal.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace audiosnd_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{11, "-d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Mgr = cTSAudioSoundManager;

constexpr uint32_t kTag = 0x7e000000;     // the manager under test
constexpr uint32_t kPageTag = 0x7d000000; // the event pool's pages
constexpr uint32_t kBufferTag = 0x7c000000; // the memory buffer's data

enum : unsigned {
    kService = 0, kComFw, kRenderer, kRObj, kDevice, kSvc9C, kVoice, kReverb, kView, kEvMgrSvc, kEvMgr, kIter, kIterEnd,
    kListener, kDemon, kApp, kState, kTesting, kMsg, kStd, kSndEvent, kSndMsg, kSound, kName,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Roll() { return 0x100u | (gRng() & 1); }

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float Float(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// ---- the world for a seed ----
struct World {
    bool renderer, svc9C, voice, view, testing, demon, demonOk, reverb, device, onThread;
    uint32_t bufferBytes; // the memory buffer's data
    uint32_t currentMode; // the voice service's
    const char *modeName; // the name the speaker-mode string matches
};
World gWorld;
constexpr uint32_t kThread = 0x7777;

// ---- the event pool (real pages) ----
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPages = 512;
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

// ---- dumping values ----
void DumpText(Blob &b, const void *text)
{
    if (!text) {
        b.push_back(0xdead0000);
        return;
    }
    const char *chars = Slot<const char *>(const_cast<void *>(text), 5);
    b.push_back(uint32_t(std::strlen(chars)));
    for (const char *c = chars; *c; ++c) b.push_back(uint8_t(*c));
}
void DumpAny(Blob &b, void *any)
{
    if (!any) {
        b.push_back(0xdead0001);
        return;
    }
    const uint32_t type = Slot<uint32_t>(any, 4);
    b.push_back(type);
    if (type == gzany::kTypeString) DumpText(b, reinterpret_cast<void *>(uintptr_t(Word(any, 4))));
    else if (type == nGZNumerics::AnyVector3::kType)
        for (uint32_t i = 0; i < 3; ++i) b.push_back(Word(any, 4 + 4 * i));
    else if (type == gzany::kTypeUint32 || type == gzany::kTypeSint32 || type == gzany::kTypeFloat32)
        b.push_back(Word(any, 4)); // numbers: data
    else b.push_back(svcfx::Norm(Word(any, 4))); // a string set, a wrapped clone: a pointer
}
// An event by content: its id, every entry (key and value), the message face's object.
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
        DumpAny(b, value);
    }
    b.push_back(svcfx::Norm(P(e->AsMessage()->GetIGZUnknown())));
}
void LogEvent(uint32_t pointer) { DumpEvent(svcfx::gEv, reinterpret_cast<cITSAudioEvent *>(uintptr_t(pointer))); }

// ---- stand-ins on both sides ----
void __cdecl StandThread(uint32_t *out) { *out = kThread; }

void *gSignalVT[6];
void __fastcall SignalSignal(void *self, void *)
{
    Ev(0x51600001);
    Ev(svcfx::Norm(P(self)));
}
uint32_t __fastcall SignalWait(void *self, void *, uint32_t ms)
{
    Ev(0x51600003);
    Ev(svcfx::Norm(P(self)));
    Ev(ms);
    return Roll();
}
cRZThreadSignal *__fastcall StandSignal(cRZThreadSignal *self, void *, uint32_t manualReset)
{
    Ev(0x9ccaa9);
    Ev(svcfx::Norm(P(self)));
    Ev(manualReset);
    auto *w = reinterpret_cast<uint32_t *>(self);
    w[0] = P(gSignalVT);
    w[1] = 0x5150;
    return self;
}
void __fastcall StandSignalEnd(cRZThreadSignal *self, void *)
{
    Ev(0x9ccb13);
    Ev(svcfx::Norm(P(self)));
}

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

void LogFormatted(const char *format, va_list args)
{
    for (const char *f = format; *f; ++f) {
        if (*f != '%') continue;
        ++f;
        unsigned longs = 0;
        for (; *f && std::strchr("-+ #0123456789.lhI", *f); ++f) longs += *f == 'l';
        if (!*f) break;
        if (*f == '%') continue;
        if (*f == 's') {
            const char *text = va_arg(args, const char *);
            Ev(uint32_t(std::strlen(text)));
        } else if (longs >= 2) {
            const uint64_t v = va_arg(args, uint64_t);
            Ev(uint32_t(v));
            Ev(uint32_t(v >> 32));
        } else if (std::strchr("eEfFgGaA", *f)) { // a double: its bits
            const double v = va_arg(args, double);
            uint64_t bits;
            std::memcpy(&bits, &v, 8);
            Ev(uint32_t(bits));
            Ev(uint32_t(bits >> 32));
        } else if (*f == 'p') { // the one pointer conversion
            Ev(svcfx::Norm(va_arg(args, uint32_t)));
        } else { // the integer conversions: data
            Ev(va_arg(args, uint32_t));
        }
    }
}
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
    LogFormatted(format, args);
    va_end(args);
}
cRZString *__cdecl StandSprintf(cRZString *self, const char *format, ...)
{
    Ev(0x1ad6f7);
    Ev(P(format) - P(GameAddress<void>(0)));
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    LogFormatted(format, copy);
    va_end(copy);
    char text[128];
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    self->mString = text;
    return self;
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

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- stand-ins on the image (the original callees both sides call) ----

// The workers' and handlers' argument words by RVA (svcfx::detail::DumpKinds letters, one per argument).
constexpr const char *ImplKinds(uint32_t rva)
{
    // the event handlers: the event (inside the test's block); the request workers: the request's words (data) and
    // the test's own image addresses
    switch (rva) {
    case 0x21d654: case 0x21db90: case 0x21e9e5: case 0x21ff2c: case 0x22011e: case 0x220828: case 0x220c98: return "d";
    case 0x21f000: case 0x22001a: case 0x220062: case 0x2200ae: case 0x2201f9: case 0x220269: case 0x2206ca:
    case 0x2207ac: case 0x2207ea: case 0x2209f1: case 0x221a18: case 0x221b59: case 0x222000: return "dd";
    case 0x21d882: case 0x21dde7: case 0x21f0cc: case 0x22029e: case 0x220741: case 0x220b67: case 0x221c1e: return "ddd";
    case 0x21ff8d: case 0x220bf5: case 0x220eec: return "dddd";
    case 0x21d3ff: case 0x220d0a: return "dddddd";
    case 0x21d4ff: return "dddp";
    case 0x220835: case 0x2208dc: case 0x22094a: case 0x220af9: return "dp";
    case 0x21de95: case 0x21def7: case 0x21dfae: case 0x21dff1: case 0x21e02d: case 0x21e04a: case 0x21e087:
    case 0x21e0ca: case 0x21e1ac: case 0x21e1e2: case 0x21e228: case 0x21e27d: case 0x21e2b0: case 0x21e2e3:
    case 0x21e316: case 0x21e34c: case 0x21e38f: case 0x21e3c2: case 0x21e3ff: case 0x21e432: case 0x21e465:
    case 0x21e502: case 0x21e59f: case 0x21e5e2: case 0x21e623: case 0x21e675: case 0x21e6b2: case 0x21e751:
    case 0x21e7c1: case 0x21e80e: case 0x21e870: case 0x21e8f5: case 0x21e92b: return "p";
    default: return "";
    }
}
// A worker or handler: its RVA, `this` and the argument words by ImplKinds; a rolled answer.
template <uint32_t R, class Seq> struct ImplGen;
template <uint32_t R, size_t... I> struct ImplGen<R, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(void *self, void *, svcfx::detail::Word<I>... a)
    {
        const uint32_t words[] = {0u, uint32_t(a)...};
        Ev(R);
        Ev(svcfx::Norm(P(self)));
        svcfx::detail::DumpKinds(svcfx::gEv, sizeof...(I), ImplKinds(R), [&](size_t i) { return words[i + 1]; },
                                 [](size_t i) {
                                     char text[80];
                                     std::snprintf(text, sizeof(text), "Impl %#x argument %u", R, unsigned(i));
                                     return std::string(text);
                                 });
        return Roll();
    }
};
template <uint32_t R, unsigned N> void *Impl() { return reinterpret_cast<void *>(&ImplGen<R, std::make_index_sequence<N>>::Fn); }

double __fastcall StandCompletionTime(void *self, void *, uint32_t instance)
{
    Ev(0x21dd75);
    Ev(svcfx::Norm(P(self)));
    Ev(instance);
    return 0.25 + Draw(9);
}
// The value worker 0x22012b (a, key, value): the value by content.
uint32_t __fastcall StandSetValue(void *self, void *, uint32_t a, uint32_t key, void *value)
{
    Ev(0x22012b);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    Ev(key);
    DumpAny(svcfx::gEv, value); // by content: from an event it is the event's own copy on the heap
    return Roll();
}
// The text worker 0x21fba9 (text, a, b): the text by content.
uint32_t __fastcall StandText(void *self, void *, void *text, uint32_t a, uint32_t b)
{
    Ev(0x21fba9);
    Ev(svcfx::Norm(P(self)));
    DumpText(svcfx::gEv, text);
    Ev(a);
    Ev(b);
    return Roll();
}
// The registry 0x21b370 (cdecl 0, info): the test's map.
using Registry = std::map<uint32_t, std::array<uint32_t, 6>>;
Registry gRegistry;
Registry *__cdecl StandRegistry(uint32_t a, uint32_t *info)
{
    Ev(0x21b370);
    Ev(a);
    for (int i = 0; i < 6; ++i) Ev(i == 1 ? info[i] - P(GameAddress<void>(0)) : info[i]);
    return &gRegistry;
}
// The id iterator 0x1a9942 (vector, cursor): the vector's ids and the cursor's index; holds the vector.
void *__fastcall StandIterator(uint32_t *self, void *, void *vec, uint32_t *cursor)
{
    Ev(0x1a9942);
    const auto &ids = *reinterpret_cast<const std::vector<uint32_t> *>(static_cast<uint8_t *>(vec) + 4);
    Ev(uint32_t(ids.size()));
    for (uint32_t id : ids) Ev(id);
    Ev(uint32_t((*cursor - P(ids.data())) / 4));
    Ev(Word(vec, 0x10));
    self[0] = 0x17e7a700;
    self[1] = 0;
    self[2] = P(vec);
    self[3] = *cursor;
    Slot<uint32_t>(vec, 1);
    return self;
}
// The range 0x1a98e0 (out, first, last): the iterators' cursors; lets go of them.
void *__fastcall StandRange(uint32_t *out, void *, uint32_t *first, uint32_t *last)
{
    Ev(0x1a98e0);
    Ev(svcfx::Norm(P(out)));
    void *vec = reinterpret_cast<void *>(uintptr_t(first[2]));
    const auto &ids = *reinterpret_cast<const std::vector<uint32_t> *>(static_cast<uint8_t *>(vec) + 4);
    Ev(first[2] == last[2]);
    Ev(uint32_t((first[3] - P(ids.data())) / 4));
    Ev(uint32_t((last[3] - P(ids.data())) / 4));
    Ev(Word(vec, 0x10));
    Slot<uint32_t>(vec, 2);
    Slot<uint32_t>(vec, 2);
    ::operator delete(first);
    ::operator delete(last);
    out[0] = out[1] = out[2] = 0x0dd0dd00;
    return out;
}
double __fastcall StandCurve(void *self, void *, float x)
{
    Ev(0xc0a6a1);
    Ev(svcfx::Norm(P(self)));
    Ev(Bits(x));
    return double(x) * 1.1 + Draw(3);
}
void __fastcall StandCurveLoad(void *self, void *, void *text)
{
    Ev(0xc0a86a);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(P(text)));
}
void *__fastcall StandBuffer(uint8_t *self, void *)
{
    Ev(0x23b4a8);
    Ev(svcfx::Norm(P(self)));
    std::memset(self, 0x3c, 0x18);
    auto *data = ::new (self + 8) std::vector<char>();
    for (uint32_t i = 0; i < gWorld.bufferBytes; ++i) data->push_back(char(0x41 + i));
    if (!data->empty()) svcfx::AddRange(data->data(), data->size(), kBufferTag); // its data reaches the reverb
    return self;
}
void __fastcall StandBufferEnd(uint8_t *self, void *)
{
    Ev(0x23b4ce);
    Ev(svcfx::Norm(P(self)));
    auto *data = reinterpret_cast<std::vector<char> *>(self + 8);
    std::erase_if(svcfx::gRanges, [&](const svcfx::Range &r) { return r.tag == kBufferTag; });
    data->~vector();
}
uint32_t __fastcall StandBufferLoad(uint8_t *self, void *, const uint32_t *key)
{
    Ev(0x21b203);
    Ev(svcfx::Norm(P(self)));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    const auto *data = reinterpret_cast<const std::vector<char> *>(self + 8);
    return data->empty() ? 0x100u : Roll(); // the original's at(0) throws on an empty buffer: not reached
}
// The namers 0x21efa5 / 0x21fd55 (a, b, out string): a name written on success.
template <uint32_t R> uint32_t __fastcall StandNamer(void *self, void *, uint32_t a, uint32_t b, cRZString *out)
{
    Ev(R);
    Ev(svcfx::Norm(P(self)));
    Ev(a);
    Ev(b);
    const uint32_t r = Roll();
    if (r & 1) {
        char text[64];
        std::snprintf(text, sizeof(text), "%s-%08x-%u", R == 0x21efa5 ? "cheat" : "named", a, b);
        out->mString = text;
    }
    return r;
}

// ---- fake handlers ----
uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    struct {
        uint32_t id, iid;
        bool World::*present;
        unsigned fake;
    } static const kServices[] = {
        {0x23cecba0, 0x106077d2, &World::renderer, kRenderer}, {0x1af85bfb, 0x3af85bb7, &World::svc9C, kSvc9C},
        {0xe99b3207, 0x899b182d, &World::voice, kVoice},       {0xfb6372ee, 0x5b637209, &World::view, kView},
        {0x0ac1887e, 0x6bb22f38, &World::testing, kTesting},
    };
    for (const auto &s : kServices)
        if (a[0] == s.id && a[1] == s.iid) return gWorld.*s.present ? HandOut(a[2], F(s.fake)) : 0x500u;
    if (a[0] == 0x5b1e896f && a[1] == 0x1b1e895a) return HandOut(a[2], F(kEvMgrSvc));
    return 0x5100u;
}
uint32_t CreateInstance(Obj *, uint32_t *a)
{
    if (a[0] == 0x8d5812a4 && a[1] == 0x4d7fd97e && gWorld.demon) return HandOut(a[2], F(kDemon));
    return 0x300u;
}
uint32_t RollHandler(Obj *, uint32_t *) { return Roll(); }
uint32_t RendererThread(Obj *, uint32_t *) { return gWorld.onThread ? kThread : kThread + 1; }
uint32_t RendererDevice(Obj *, uint32_t *) { return gWorld.device ? P(&F(kDevice)) : 0; }
uint32_t RendererPost(Obj *, uint32_t *a)
{
    LogEvent(a[0]);
    return 0x1200u;
}
uint32_t RendererSend(Obj *, uint32_t *a)
{
    auto *e = reinterpret_cast<cITSAudioEvent *>(uintptr_t(a[0]));
    LogEvent(a[0]);
    const uint32_t r = Roll();
    if (!(r & 1) && Draw(4)) return r; // a failed send may still have written its answer
    const uint32_t id = e->GetEventId();
    const uint32_t out = e->GetUint32(0xbd63091);
    if (id == 0x8bdf1f98 && out && Draw(3)) *reinterpret_cast<float *>(uintptr_t(out)) = 0.5f + float(Draw(7));
    if ((id == 0xec150cf5 || id == 0x4e710074) && out && Draw(3)) *reinterpret_cast<uint8_t *>(uintptr_t(out)) = uint8_t(Draw(3));
    return r;
}
uint32_t RendererFloat(Obj *, uint32_t *a)
{
    *reinterpret_cast<float *>(uintptr_t(a[0])) = 0.125f * float(Draw(9));
    return 0;
}
uint32_t VoiceQuery(Obj *, uint32_t *a)
{
    if (a[0] == 0xab79e31c && gWorld.reverb) return HandOut(a[1], F(kReverb));
    return 0x900u;
}
uint32_t VoiceMode(Obj *, uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gWorld.currentMode;
    return Roll();
}
uint32_t VoiceCount(Obj *, uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = 16 + Draw(16);
    return Roll();
}
uint32_t DemonInit(Obj *, uint32_t *) { return gWorld.demonOk ? 0x101u : 0x100u; }
// The event manager's slot 10 (range out, object, kind): the range of listeners, counted.
uint32_t ListenerRangeOf(Obj *, uint32_t *a)
{
    auto *range = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    range[0] = P(&F(kIter));
    range[1] = P(&F(kIterEnd));
    range[2] = P(&F(kIterEnd));
    F(kIter).refs++;
    F(kIterEnd).refs += 2;
    F(kIter).value[0] = Draw(4); // listeners
    return 0;
}
uint32_t IterAtEnd(Obj *self, uint32_t *) { return self->value[0] == 0 ? 0x101u : 0x100u; }
uint32_t IterCurrent(Obj *self, uint32_t *a)
{
    auto *pair = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    pair[0] = P(&F(kListener));
    pair[1] = 0x4000 + self->value[0] * 16 + (self->value[1]++ & 15);
    return 0;
}
uint32_t IterNext(Obj *self, uint32_t *)
{
    if (self->value[0]) --self->value[0];
    return 0;
}
uint32_t MessageQuery(Obj *self, uint32_t *a)
{
    if (a[0] != 0x65297976) return 0x3000u;
    if (self->ret[4] != 0xcbccdece && Draw(4) == 0) return 0x3300u;
    return HandOut(a[1], F(kStd));
}
uint8_t gStdBlock[8];
cITSAudioEvent *gTestEvent;
// The renderer's event hands out its two objects in turn (the message, then the sound).
unsigned gSndEventTurn;
uint32_t SndEventNext(Obj *, uint32_t *) { return P(&F(gSndEventTurn++ % 2 ? kSound : kSndMsg)); }
uint32_t SndEventQuery(Obj *, uint32_t *a)
{
    if (a[0] != cITSAudioEvent::kIid || !gTestEvent || Draw(6) == 0) return 0x3400u;
    gTestEvent->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = gTestEvent;
    return 1;
}
uint32_t NameEquals(Obj *, uint32_t *a)
{
    const char *text = reinterpret_cast<const char *>(uintptr_t(a[0]));
    return gWorld.modeName && std::strcmp(text, gWorld.modeName) == 0 ? 0x101u : 0x100u;
}

// ---- building the fakes ----
void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 23);
    svcfx::ResetAll();
    gWorld.renderer = rng() % 6 != 0;
    gWorld.svc9C = rng() % 8 != 0;
    gWorld.voice = rng() % 8 != 0;
    gWorld.view = rng() % 3 != 0;
    gWorld.testing = rng() % 3 != 0;
    gWorld.demon = rng() % 8 != 0;
    gWorld.demonOk = rng() % 8 != 0;
    gWorld.reverb = rng() % 2 != 0;
    gWorld.device = rng() % 5 != 0;
    gWorld.onThread = rng() % 2 != 0;
    gWorld.bufferBytes = rng() % 3;
    gWorld.currentMode = rng() % 5;
    static const char *const kNames[] = {"mono", "stereo", "4.0", "5.1", "7.1", nullptr};
    gWorld.modeName = kNames[rng() % 6];

    F(kService).Set(5, "ddp", 0, &GetSystemService);
    for (unsigned s : {3u, 4u, 9u, 10u}) F(kService).Set(s, "p", 0x700 + s);
    F(kComFw).Set(3, "ddp", 0, &CreateInstance);
    Obj &r = F(kRenderer);
    r.Set(4, 0, &F(kRObj));
    r.Set(8, "p", 0, &RendererSend);
    r.Set(11, "d----", 0x1100);
    r.Set(12, "p", 0, &RendererPost);
    r.Set(15, "p", 0x1500);
    r.Set(16, "p", 0x1600);
    r.Set(30, "pd", 0x3000);
    r.Set(31, "p", 0x3100);
    r.Set(41, 0, 0, &RendererDevice);
    r.Set(48, 0, 0x4800);
    r.Set(51, 0, 0, &RendererThread);
    F(kRObj).Set(9, "p", 0, &RendererFloat);
    Obj &v = F(kVoice);
    v.Set(0, "dp", 0, &VoiceQuery);
    v.Set(3, 0, 0x303);
    v.Set(4, 0, 0x304);
    v.Set(11, "d", 0, &RollHandler);
    v.Set(12, "d", 0x312);
    v.Set(13, "p", 0, &VoiceMode);
    v.Set(19, "-p", 0, &VoiceCount);
    F(kReverb).Set(4, "d", 0x404);
    F(kReverb).Set(5, "-p", 0x405);
    F(kReverb).Set(7, "-d", 0x407);
    F(kView).Set(8, "dpd", 0x808);
    F(kView).Set(9, "dpd", 0x809);
    F(kEvMgrSvc).Set(3, 0, &F(kEvMgr));
    Obj &m = F(kEvMgr);
    m.Set(3, "pddd", 0x1003);
    m.Set(4, "pddd", 0x1004);
    m.Set(5, "p", 0x1005);
    m.Set(6, "p", 0, &RollHandler);
    m.Set(10, "ppd", 0, &ListenerRangeOf);
    F(kIter).Set(4, "p", 0, &IterAtEnd);
    F(kIter).Set(5, "p", 0, &IterCurrent);
    F(kIter).Set(6, 0, 0, &IterNext);
    F(kListener).Set(3, "dddp", 0x1303);
    F(kListener).Set(4, "ddd", 0, &RollHandler);
    F(kDemon).Set(3, 0, 0, &DemonInit);
    F(kDemon).Set(4, 0, 0x1404);
    F(kApp).Set(31, 0, 0, &RollHandler);
    F(kApp).Set(23, 0, rng() % 4 ? &F(kState) : nullptr);
    F(kState).Set(5, 0, 0u);
    F(kTesting).Set(4, "pdp-", 0x1704);
    F(kMsg).Set(4, 0, 0u);
    F(kMsg).Set(0, "dp", 0, &MessageQuery);
    F(kStd).Set(9, 0, 0x1909);
    F(kStd).Set(15, 0, gStdBlock);
    F(kStd).Set(19, 0, 0x1919);
    F(kSndEvent).Set(4, 0, 0u);
    gSndEventTurn = 0;
    F(kSndEvent).Set(6, 0, 0, &SndEventNext);
    F(kSndEvent).Set(0, "dp", 0, &SndEventQuery);
    F(kSndMsg).Set(9, 0, 0x2109);
    F(kSndMsg).Set(14, 0, 0x2114);
    F(kSound).Set(9, 0, 0x2209);
    F(kSound).Set(14, 0, 0x2214);
    F(kSound).Set(12, 0, 0x2212);
    F(kSound).Set(30, 0, 0x2230);
    F(kSound).Set(31, 0, 0x2231);
    F(kName).Set(10, "p-", 0, &NameEquals);
    F(kIterEnd).Set(5, "pdp--", 0x100); // the persist resource manager (0x10faf6c): the tuning values never load here
    for (int i = 0; i < 8; ++i) gStdBlock[i] = uint8_t(rng());

    gRegistry.clear();
    for (unsigned i = rng() % 7; i-- > 0;) {
        std::array<uint32_t, 6> words;
        for (auto &w : words) w = rng();
        gRegistry[rng() % 40] = words;
    }

    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? &F(kService) : nullptr;
    *GameAddress<void *>(0x10cc288) = &F(kApp);
    *GameAddress<void *>(0x10faf6c) = &F(kIterEnd);
    *GameAddress<void *>(0x10cc2a8) = rng() % 3 ? nullptr : &F(kState);
    *GameAddress<void *>(0x10cc528) = rng() % 2 ? &F(kEvMgr) : nullptr;
    *GameAddress<uint32_t>(0xff4af8) = 0x10000 + rng() % 5;
    svcfx::gFrameworkIndex = kComFw;
}

// ---- the manager ----
bool gFactory;

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) return ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x21a182)();
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) cTSAudioSoundManagerAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x2188d6)(memory);
    return memory;
}

void DumpString(Blob &b, const uint8_t *at)
{
    const auto *s = reinterpret_cast<const std::string *>(at + 4);
    b.push_back(uint32_t(s->size()));
    for (char c : *s) b.push_back(uint8_t(c));
}
// An unordered map: its words, each node's words from +8 (`refAt` normalised, `stringAt` as a string), the buckets.
void DumpHash(Blob &b, const uint8_t *at, unsigned words, int refAt, int stringAt = -1)
{
    b.push_back(Word(at, 0));
    b.push_back(Word(at, 8));
    b.push_back(Word(at, 0x18));
    b.push_back(Word(at, 0x1c));
    std::vector<uint32_t> order;
    const uint32_t head = Word(at, 4);
    for (uint32_t n = Word(reinterpret_cast<void *>(uintptr_t(head)), 0); n != head; n = Word(reinterpret_cast<void *>(uintptr_t(n)), 0)) {
        order.push_back(n);
        const auto *w = reinterpret_cast<const uint8_t *>(uintptr_t(n));
        for (unsigned i = 2; i < 2 + words; ++i) b.push_back(int(i) == refAt ? svcfx::Norm(Word(w, 4 * i)) : Word(w, 4 * i));
        if (stringAt >= 0) DumpString(b, w + stringAt);
    }
    const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0xc)));
    const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0x10)));
    b.push_back(uint32_t(last - first));
    for (const uint32_t *p = first; p != last; ++p) {
        uint32_t index = 0xffffffff;
        for (uint32_t i = 0; i < order.size(); ++i)
            if (order[i] == *p) index = i;
        if (*p != head && index == 0xffffffff) index = 0xeeeeeeee;
        b.push_back(index);
    }
}
void DumpRefList(Blob &b, const std::list<AudioRef> &l, bool events)
{
    b.push_back(uint32_t(l.size()));
    for (const AudioRef &r : l) {
        b.push_back(svcfx::Norm(P(r.Get())));
        if (events) DumpEvent(b, reinterpret_cast<cITSAudioEvent *>(r.Get()));
    }
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    auto &mgr = *static_cast<Mgr *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[6][2] = {{0, 0xda92e0}, {8, 0xda9434}, {0xc, 0xda9448},
                                          {0x10, 0xda945c}, {0x14, 0xda9470}, {0x18, 0xda9484}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    b.push_back(Word(m, 4));
    // Plain words (the never-written bytes are compared too; from a factory the allocator's bytes are not).
    auto words = [&](uint32_t from, uint32_t to) {
        for (uint32_t off = from; off < to; off += 4) b.push_back(Word(m, off));
    };
    b.push_back(Word(m, 0x1c));
    b.push_back(gFactory ? m[0x20] : Word(m, 0x20));
    words(0x24, 0x40);
    DumpHash(b, m + 0x40, 2, -1);
    DumpRefList(b, mgr.mList60, false);
    words(0x68, 0x98);
    for (uint32_t off = 0x98; off < 0xa8; off += 4) b.push_back(svcfx::Norm(Word(m, off)));
    b.push_back(uint32_t(mgr.mMessageIds.size()));
    for (uint32_t id : mgr.mMessageIds) b.push_back(id);
    b.push_back(svcfx::Norm(Word(m, 0xb0)));
    words(0xb4, gFactory ? 0xf0 : 0xf4);
    if (gFactory) b.push_back(m[0xf0]);
    words(0xf4, 0x160);
    for (uint32_t off = 0x160; off < 0x1c0; off += 0x10) b.push_back(Word(m, off) != 0), b.push_back(Word(m, off + 0xc));
    DumpString(b, m + 0x1c0);
    DumpString(b, m + 0x1e0);
    DumpRefList(b, mgr.mPending, true);
    if (gFactory) {
        words(0x208, 0x218);
        words(0x284, 0x298);
        words(0x2b8, 0x2c0);
        b.push_back(m[0x2c0]), b.push_back(m[0x2c1]);
        b.push_back(Word(m, 0x2c4));
    } else {
        words(0x208, 0x2c8);
    }
    b.push_back(Word(m, 0x2c8) == P(gSignalVT));
    b.push_back(Word(m, 0x2cc));
    words(0x2d0, 0x2f8);
    b.push_back(m[0x2f8]), b.push_back(m[0x2f9]);
    if (!gFactory) b.push_back(m[0x2fa]), b.push_back(m[0x2fb]);
    b.push_back(Word(m, 0x2fc));
    words(0x300, 0x308);
    const auto &data = *reinterpret_cast<const std::vector<char> *>(m + 0x308);
    b.push_back(uint32_t(data.size()));
    words(0x314, 0x318);
    DumpHash(b, m + 0x318, 2, 3);
    words(0x338, gFactory ? 0x344 : 0x348);
    DumpHash(b, m + 0x348, 2, -1, 0x10);
    DumpHash(b, m + 0x368, 2, -1, 0x10);
    b.push_back(gFactory ? m[0x388] : Word(m, 0x388));
    DumpHash(b, m + 0x38c, 2, -1, 0x10);
}

// ---- operations ----
struct Op {
    unsigned kind;
    uint32_t a, b;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    if (rng() % 6) ops.push_back({0, rng(), rng()});
    const unsigned n = 4 + rng() % 30;
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 19, rng(), rng()});
    return ops;
}

const float kFloats[] = {0.0f, 1.0f, -2.5f, 0.75f, 1e30f, -0.0f, std::nanf("")};
const uint32_t kFlags[] = {0, 1, 0x100, 0x1ff, 0xffffffff, 2};
float FloatArg() { return kFloats[Draw(7)]; }
uint32_t FlagArg() { return kFlags[Draw(6)]; }
uint32_t WordArg() { return Draw(4) ? 0x100 + Draw(64) : gRng(); }
float gPosition[3] = {1.5f, -2.0f, 3.25f};
cRZString *gText;
gz::AnyUint32 gAny(0x1234);

bool HasRenderer(void *object) { return Word(object, 0x98) != 0; }
bool Pending(void *object) { return !static_cast<Mgr *>(object)->mPending.empty(); }

// The event the renderer's raw event hands out (HandleEvent), with the keys its id's branch reads.
uint8_t gAnswer;
void MakeTestEvent(uint32_t id)
{
    gTestEvent = new cTSAudioEvent(id);
    gTestEvent->AddRef();
    gTestEvent->SetUint32(0x8bd63096, FlagArg());
    gTestEvent->SetUint32(0x6d03f57a, WordArg());
    gTestEvent->SetUint32(0x8bd63087, WordArg());
    gTestEvent->SetUint32(0xacbf1a9f, WordArg());
    gTestEvent->SetUint32(0x6bd63081, WordArg());
    gTestEvent->SetUint32(0xac5b5b4f, WordArg());
    gTestEvent->SetUint32(0xedd18e72, WordArg());
    gTestEvent->SetUint32(0xbd63077, WordArg());
    gTestEvent->SetUint32(0xbd63091, Draw(4) ? P(&gAnswer) : 0);
    gTestEvent->SetSint32(0xd1e3ae8, int32_t(gRng()));
    gTestEvent->SetSint32(0xcd1e509f, int32_t(gRng()));
    if (Draw(3)) gTestEvent->SetText(0x2d41d97a, reinterpret_cast<cIGZString *>(gText));
    uint32_t key = Draw(3) ? 0x5e000000 + Draw(3) : 0;
    gTestEvent->SetUint32(0xedc89abd, key);
    if (Draw(2)) gTestEvent->SetProperty(&key, &gAny);
}

const uint32_t kEventIds[] = {
    0xd96318d, 0xc77381f, 0xb8d81d5, 2, 0x6a451908, 0xd963193, 0x2a5b69c0, 0x2c5b9799, 0x2c5b979e, 0x4a451915, 0x4bb54186,
    0x4e66585c, 0x6bb5418a, 0x6bb54195, 0x6c0fd27e, 0x6c2b8ce3, 0x8bc77093, 0x8bdf1f98, 0x8c2b8cfa, 0x8dfe7c03, 0xac0fd2a2,
    0xac5a4651, 0xac5a4657, 0xad9f9fee, 0xca45190f, 0xec150cf5, 0xcbb78f7a, 0xcc488a33, 0xcc488a38, 0xea451903, 0xec7153c5,
    0xec773209, 0x2dcae8ef, 0x6a95cba2, 0x4a8c52c8, 0xabb5419e, 0xcaa44ca7, 0xd7c4339, 0x2cbf1a22, 0x2cbf1a2b, 0x2d41d87e,
    0x4e710074, 0x8d238daa, 0xccdde023, 0xed1e3aca, 0xedc86cf8, 0xeedbcdc7, 0, 1, 3, 0x12345678, 0xffffffff};
const uint32_t kMessageTypes[] = {0x6d21507b, 0xb6, 0xb3, 0xb2, 0x4d213401, 0xcbccdece, 0xcdafda21, 0xece20be8,
                                  0xb4, 0xb5, 0xcbea26d7, 0xb7, 0x12345678};
const uint32_t kIids[] = {1, 0x4ba8123a, 0xbb523bb2, 0x5b34f0af, 0x1b3637c6, 0x452294aa, 0x287fb697, 0x3b253dc7, 0xfb859324,
                          0x12345678};

void Proxy(Blob &b, void *o)
{
    const unsigned slot = 18 + Draw(42);
    switch (slot) {
    case 18: Slot<void>(o, 18, FloatArg(), FloatArg()); break;
    case 19: case 44: case 46: case 47: case 48: case 49: Slot<void>(o, slot, WordArg(), FloatArg()); break;
    case 20: case 36: case 45: Slot<void>(o, slot, WordArg(), FloatArg(), FloatArg()); break;
    case 21: b.push_back(Slot<uint32_t>(o, 21, WordArg(), WordArg(), WordArg(), WordArg(), WordArg(), WordArg()) & 0xff); break;
    case 22:
        b.push_back(Slot<uint32_t>(o, 22, WordArg(), WordArg(), WordArg(), Draw(3) ? static_cast<void *>(&F(kSound)) : nullptr) & 0xff);
        break;
    case 23: case 24: Slot<void>(o, slot, WordArg(), Draw(3) ? static_cast<void *>(&F(kSound)) : nullptr); break;
    case 25: Slot<void>(o, 25, WordArg(), WordArg(), WordArg(), WordArg(), WordArg(), FlagArg()); break;
    case 26: case 42: Slot<void>(o, slot, WordArg(), WordArg(), WordArg(), FlagArg()); break;
    case 27: case 28: case 29: case 51: Slot<void>(o, slot, WordArg(), FlagArg()); break;
    case 30: Slot<void>(o, 30, WordArg(), FlagArg(), FloatArg()); break;
    case 31: case 32: Slot<void>(o, slot, FlagArg(), WordArg()); break;
    case 33: case 52: Slot<void>(o, slot, WordArg()); break;
    case 34: case 35: Slot<void>(o, slot, WordArg(), static_cast<const float *>(gPosition)); break;
    case 37: Slot<void>(o, 37); break;
    case 43: Slot<void>(o, 43, WordArg(), WordArg(), FloatArg(), FlagArg()); break;
    case 50: Slot<void>(o, 50, WordArg(), WordArg(), int32_t(WordArg())); break;
    case 53: Slot<void>(o, 53, FlagArg()); break;
    case 54: Slot<void>(o, 54, WordArg(), WordArg(), static_cast<void *>(&gAny)); break;
    case 59: Slot<void>(o, 59, static_cast<void *>(gText), WordArg(), WordArg()); break;
    case 39: b.push_back(Bits(Slot<float>(o, 39, WordArg()))); break;
    case 40: b.push_back(Slot<uint32_t>(o, 40, WordArg(), FlagArg(), WordArg()) & 0xff); break;
    case 41: case 55: b.push_back(Slot<uint32_t>(o, slot, WordArg()) & 0xff); break;
    default: break; // 38, 56..58: their own operations
    }
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const handler = Face(object, 8);
    void *const update = Face(object, 0xc);
    void *const completion = Face(object, 0x10);
    void *const target = Face(object, 0x14);
    void *const service = Face(object, 0x18);
    auto &mgr = *static_cast<Mgr *>(object);
    const uint32_t a = op.a;
    const bool renderer = HasRenderer(object);
    switch (op.kind) {
    case 0: b.push_back((a % 3 ? Slot<uint32_t>(primary, 15) : Slot<uint32_t>(service, 8)) & 0xff); break;
    case 1: // Shutdown (its flush needs the renderer when events are pending)
        if (!renderer && Pending(object)) break;
        b.push_back((a % 3 ? Slot<uint32_t>(primary, 16) : Slot<uint32_t>(service, 9)) & 0xff);
        break;
    case 2: { // PostEvent
        if (!renderer) break;
        cITSAudioEvent *e = new cTSAudioEvent(kEventIds[a % 52]);
        e->AddRef();
        e->SetUint32(0x6bd63081, a);
        Slot<void>(primary, 17, e);
        e->Release();
        break;
    }
    case 3: case 4: Proxy(b, primary); break;
    case 5: { // NewInstanceId at the wrap
        static const uint32_t kCounters[] = {0x10000, 0x1fffffff, 0x20000000, 0x20000001, 0xffffffff, 0};
        if (a % 2) *GameAddress<uint32_t>(0xff4af8) = kCounters[(a >> 1) % 6];
        b.push_back(Slot<uint32_t>(primary, 38));
        b.push_back(*GameAddress<uint32_t>(0xff4af8));
        break;
    }
    case 6: // the speaker modes
        if (a % 3 == 0) {
            b.push_back(Slot<uint32_t>(primary, 56, WordArg()) & 0xff);
        } else if (renderer) {
            gWorld.currentMode = Draw(5);
            if (a % 3 == 1) b.push_back(Slot<uint32_t>(primary, 57, Draw(6)) & 0xff);
            else b.push_back(Slot<uint32_t>(primary, 58, static_cast<void *>(&F(kName))) & 0xff);
        }
        break;
    case 7: { // GetSoundName, with the maps and objects filled
        if (a % 4 == 0) {
            mgr.mUseNameMaps = uint8_t(Draw(3));
            for (unsigned i = Draw(4); i-- > 0;) {
                const uint64_t id = uint64_t(Draw(3) ? Draw(4) : 0) << 32 | (Draw(4) ? 0x40 + Draw(8) : 0);
                AudioNameMap &names = Draw(2) ? mgr.mNames348 : mgr.mNames368;
                char text[32];
                std::snprintf(text, sizeof(text), "map-%08x-%u", uint32_t(id), uint32_t(id >> 32));
                names[id].s.mString = text;
            }
            for (unsigned i = Draw(3); i-- > 0;) mgr.mObjects[0x40 + Draw(8)] = P(&F(kSound));
        }
        const uint32_t instance = Draw(5) ? 0x40 + Draw(10) : 0; // 0 meets the maps' ids without an instance
        const char *name = Slot<const char *>(primary, 60, instance, Draw(5));
        b.push_back(uint32_t(std::strlen(name)));
        for (const char *c = name; *c; ++c) b.push_back(uint8_t(*c));
        break;
    }
    case 8: // the getters
        b.push_back(Slot<uint32_t>(primary, 61, a % 11));
        for (unsigned s : {62u, 63u, 64u, 65u, 77u, 78u, 82u}) b.push_back(Slot<uint32_t>(primary, s) & (s >= 63 && s <= 77 && s != 78 ? 0xff : 0xffffffff));
        if (gFactory) b.back() = 0; // +0x344 is never written: from a factory it is the allocator's bytes
        for (unsigned s : {66u, 67u, 74u, 79u, 80u, 81u}) b.push_back(Bits(Slot<float>(primary, s)));
        for (unsigned s = 68; s <= 73; ++s) b.push_back(Bits(Slot<float>(primary, s, FloatArg())));
        if (a & 0x100) {
            for (uint32_t i = 0; i < 9; ++i) {
                mgr.mCategoryEnabled[i] = uint8_t(Draw(3));
                mgr.mCategoryVolumes[i] = kFloats[Draw(5)] * 0.3f;
                mgr.mCategoryFades[i].value = kFloats[Draw(5)] * 0.7f;
            }
            mgr.mMasterFade.value = 0.9f;
        }
        for (uint32_t i = 0; i < 11; ++i) b.push_back(Bits(Slot<float>(primary, 75, i)));
        if (renderer) b.push_back(Bits(Slot<float>(primary, 76)));
        break;
    case 9: { // HandleEvent
        const uint32_t type = a % 5 == 0 ? 1 : a % 5 == 1 ? 2 : a % 5 == 2 ? 0x6ebaf256 : 0x4000;
        F(kSndEvent).ret[4] = type;
        MakeTestEvent(kEventIds[(a >> 4) % 52]);
        gAnswer = 0x77;
        b.push_back(Slot<uint32_t>(handler, 3, static_cast<void *>(&F(kSndEvent))) & 0xff);
        b.push_back(gAnswer);
        gTestEvent->Release();
        gTestEvent = nullptr;
        break;
    }
    case 10: { // Update, the ramps around the threshold
        const float threshold = *GameAddress<float>(0xd9e82c);
        const float rates[] = {threshold, threshold + 0.001f, threshold - 0.001f, 0.01f, -0.01f, 0.0f, std::nanf("")};
        auto ramp = [&](AudioRamp &r) {
            r.value = kFloats[Draw(4)];
            r.target = Draw(3) ? kFloats[Draw(4)] : r.value;
            r.rate = rates[Draw(7)];
        };
        if (a % 2) {
            ramp(mgr.mMasterFade);
            for (AudioRamp &r : mgr.mCategoryFades) ramp(r);
        }
        static const uint32_t kElapsed[] = {0, 1, 33, 1000, 0x80000000, 0xffffffff};
        Slot<void>(update, 3, kElapsed[(a >> 1) % 6]);
        break;
    }
    case 11: // OnSoundComplete
        if (renderer) Slot<void>(completion, 3, static_cast<void *>(&F(kSound)));
        break;
    case 12: { // DoMessage
        uint32_t type = kMessageTypes[a % 13];
        if (!renderer && (type == 0xb3 || type == 0xcdafda21 || type == 0xece20be8)) type = 0xb4;
        F(kMsg).ret[4] = type;
        static const uint32_t kModes[] = {0xcc18133f, 0xcc181346, 0xcc18134a, 0xbec20c2, 0xc18069d, 0x12345};
        F(kState).ret[5] = kModes[(a >> 4) % 6];
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMsg))) & 0xff);
        break;
    }
    case 13: // OnTick (flushes)
        if (!renderer && Pending(object)) break;
        b.push_back(Slot<uint32_t>(service, 10, a) & 0xff);
        break;
    case 14: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, handler, update, completion, target, service};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const bool nullOut = (a >> 20) % 7 == 0;
        const uint32_t iid = kIids[(a >> 4) % 10];
        const uint32_t ok = Slot<uint32_t>(faces[a % 6], 0, iid, nullOut && iid != 1 && iid > 0x30000000 && iid != 0x4ba8123a &&
                                                                         iid != 0xbb523bb2 && iid != 0x5b34f0af && iid != 0x452294aa
                                                                     ? static_cast<void **>(nullptr)
                                                                     : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 6], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 6], 2));
        if (ok && q != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e)) && iid != 0xfb859324) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 15: { // the behaviour type
        static const uint32_t kEvents[] = {0x5b51e806, 0x1b637474, 0x1b637474, 0x12345};
        uint32_t data[2] = {0x77, (a >> 4) % 11};
        switch ((a >> 8) % 9) {
        case 0: Slot<void>(primary, 3, kEvents[a % 4], 0x33u, static_cast<void *>(data), 0x44u); break;
        case 1: b.push_back(Slot<uint32_t>(primary, 4, a % 5, 0x10u, 0x20u) & 0xff); break;
        case 2: Slot<void>(primary, 5, a % 5, 0x10u, 0x20u); break;
        case 3: b.push_back(Slot<uint32_t>(primary, 6, a % 5, 0x10u, 0x20u) & 0xff); break;
        case 4: Slot<void>(primary, 7 + a % 4, 1u, 2u, 3u); break;
        case 5: b.push_back(Slot<uint32_t>(primary, 11)); b.push_back(Slot<uint32_t>(primary, 12)); break;
        case 6: {
            uint32_t out[3] = {1, 2, 3};
            b.push_back(Slot<uint32_t *>(primary, 13, static_cast<void *>(out)) == out);
            for (uint32_t w : out) b.push_back(w);
            break;
        }
        case 7: {
            uint32_t out[6];
            const uint32_t id = gRegistry.empty() || Draw(3) == 0 ? 41 : std::next(gRegistry.begin(), Draw(uint32_t(gRegistry.size())))->first;
            b.push_back(Slot<uint32_t *>(primary, 14, static_cast<void *>(out), id) == out);
            for (uint32_t w : out) b.push_back(w);
            break;
        }
        default: Slot<void>(primary, 3, 0x5b51e806u, 0u, static_cast<void *>(data), 0u); break;
        }
        break;
    }
    case 16: // the service accessors
        b.push_back(Slot<uint32_t>(service, 3));
        Slot<void>(service, 4, a);
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        Slot<void>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, a) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        break;
    default: // the world changes
        gWorld.onThread = a % 2 != 0;
        gWorld.device = (a >> 1) % 4 != 0;
        gWorld.bufferBytes = (a >> 3) % 3;
        mgr.mFlag2F8 = uint8_t((a >> 5) % 3);
        if ((a >> 7) % 3 == 0) mgr.mRefB0.Assign((a >> 9) % 2 ? reinterpret_cast<cIGZUnknown *>(&F(kSound)) : nullptr);
        break;
    }
}

// ---- the startup workers' tests (LoadSoundDefaults, ApplySettings, UpdateSpeakerConfig, UpdateSounds) ----

enum : unsigned { kDefRes = 0, kDefSet, kDefOld, kDefFw, kDefSettings, kDefMgr, kDefCfg, kDefOther };

template <class T = void> T *Ptr(uint32_t a) { return reinterpret_cast<T *>(uintptr_t(a)); }
void DumpChars(Blob &b, const char *s)
{
    const uint32_t n = uint32_t(std::strlen(s));
    b.push_back(n);
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        for (uint32_t k = 0; k < 4 && i + k < n; ++k) w |= uint32_t(uint8_t(s[i + k])) << (8 * k);
        b.push_back(w);
    }
}

// The texts the fakes hand out: real cRZStrings (the game's vtable), the test holding one reference to each (never freed).
std::vector<cRZString *> gTexts;
cRZString *Text(const char *s)
{
    auto *t = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    t->Construct(s);
    Slot<uint32_t>(t, 1);
    gTexts.push_back(t);
    return t;
}
uint32_t Count(cRZString *t)
{
    Slot<uint32_t>(t, 1);
    return Slot<uint32_t>(t, 2);
}

// A property: absent, a word (float bits, uint32, or the boolean's byte), a text, or a null string.
struct DefProp {
    enum : uint8_t { kAbsent, kWord, kText, kNull } kind = kAbsent;
    uint32_t word = 0;
    cRZString *text = nullptr;
};
std::map<std::string, DefProp> gProps;
bool gDefLoads;
bool gWithSettings;

// A string-keyed set's slot 9 (both the defaults resource's set and the settings service): the name logged, the value
// written by the any's type (slot 4): a string shared (the old one released, as AnyString's Assign does), a boolean's
// byte only, otherwise the dword. al is the answer; the upper bytes vary.
uint32_t SetGetValue(Obj *, uint32_t *a)
{
    const auto *name = Ptr<const GZServiceString>(a[0]);
    DumpChars(svcfx::gEv, name->c_str());
    const auto it = gProps.find(name->c_str());
    if (it == gProps.end() || it->second.kind == DefProp::kAbsent) return Draw(2) ? 0x100u : 0u;
    void *const any = Ptr(a[1]);
    const uint32_t type = Slot<uint32_t>(any, 4);
    Ev(type);
    auto *word = static_cast<uint32_t *>(any) + 1;
    if (type == gzany::kTypeString) {
        cRZString *const text = it->second.kind == DefProp::kText ? it->second.text : nullptr;
        if (text) Slot<uint32_t>(text, 1);
        if (*word) Slot<uint32_t>(Ptr(*word), 2);
        *word = P(text);
    } else if (type == gzany::kTypeBoolean) {
        *reinterpret_cast<uint8_t *>(word) = uint8_t(it->second.word);
    } else {
        *word = it->second.word;
    }
    return 0x101u | (Draw(2) << 9);
}
// The persist resource manager's slot 5 (key, iid, out, 0, 0): the key logged; the set handed out when it loads.
uint32_t ResLoad(Obj *, uint32_t *a)
{
    for (uint32_t i = 0; i < 4; ++i) Ev(Ptr<uint32_t>(a[0])[i]);
    if (!gDefLoads) return 0x100u | (Draw(2) << 10);
    return HandOut(a[2], F(kDefSet));
}
// The framework's slot 5: the settings service (0x0ac1887e, 0x4bb0d269) when the world has it.
uint32_t FwService(Obj *, uint32_t *a)
{
    if (a[0] == 0x0ac1887e && a[1] == 0x4bb0d269 && gWithSettings) return HandOut(a[2], F(kDefSettings));
    return 0x100u;
}
// The curve load 0xc0a86a with the text's content.
void __fastcall StandCurveLoadText(void *self, void *, void *text)
{
    Ev(0xc0a86a);
    Ev(svcfx::Norm(P(self)));
    DumpText(svcfx::gEv, text);
}
// ApplySettings' own slots 51 (the boolean's dword: only its low byte is the value, see the test) and 58 (the name).
uint32_t __fastcall MgrSlot51(Obj *self, void *, uint32_t group, uint32_t on)
{
    Ev(0x51);
    Ev(self->id);
    Ev(group);
    Ev(on & 0xff);
    return 0;
}
uint32_t __fastcall MgrSlot58(Obj *self, void *, void *name)
{
    Ev(0x58);
    Ev(self->id);
    DumpText(svcfx::gEv, name);
    return 0x5800u | Draw(2);
}

// The names LoadSoundDefaults reads (the cells of 0xff4518..0xff4580) by kind.
const uint32_t kFloatNames[] = {0xff4518, 0xff454c, 0xff4550, 0xff4554, 0xff4560, 0xff4558, 0xff4574, 0xff4578, 0xff4580};
const uint32_t kUintNames[] = {0xff4564, 0xff4568, 0xff456c, 0xff4570};
const uint32_t kCurveNames[] = {0xff451c, 0xff4520, 0xff4524, 0xff4528, 0xff452c, 0xff4530, 0xff455c};
const uint32_t kFloatListNames[] = {0xff4538, 0xff4544, 0xff4548};
const uint32_t kUintListNames[] = {0xff453c, 0xff4540};
const char *CellName(uint32_t cell) { return *GameAddress<const char *>(cell); }

std::string Numbers(uint32_t n, uint32_t zeroEvery)
{
    std::string s;
    for (uint32_t i = 0; i < n; ++i) {
        if (i) s += ',';
        s += std::to_string(zeroEvery && i % zeroEvery == 0 ? 0 : i + 1);
    }
    return s;
}

// The property table for a LoadSoundDefaults case.
void DefaultsWorld(std::mt19937 &rng)
{
    static const float kFloats[] = {0.0f, -0.0f, 1.0f, 0.5f, -1.0f, 65535.0f, 2000.0f, 1e30f, 3.3f, 400.0f, 7.0f,
                                    12345.678f, 3.0f, 1e-3f, 0.1f, 1e-40f, 257.0f, 333.0f,
                                    std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    static const uint32_t kUints[] = {0, 1, 44100, 0xffffffffu, 0x80000000u};
    static const char *const kCurves[] = {"1,2", "", "0,100,0.5,8000,1,65535"};
    static const char *const kFloatLists[] = {"", "1", "2.5,3.5", "1,2,3", "1,2,3,4", "0.5,1,1.5,2,2.5,3", " 7 , 8 ",
                                              "-1.5,3e9,1e20,-2147483649", ",,4,,5,", "abc,1x,.5e1",
                                              "4000000000.9,-0.9,2147483648", "1e-50,-1e40,9.99"};
    static std::vector<std::string> uintLists;
    if (uintLists.empty())
        uintLists = {"", "5", "1,2,3,4", "0,0x10,010", "-1,4294967295,4294967296", "9,0,7", Numbers(26, 4),
                     Numbers(27, 5), Numbers(28, 3), Numbers(40, 0), "0,0,0", " 12, 0x7fffffff ,z"};
    gProps.clear();
    auto present = [&] { return rng() % 2 == 0; };
    for (uint32_t cell : kFloatNames)
        if (present()) {
            const float f = kFloats[rng() % std::size(kFloats)];
            gProps[CellName(cell)] = {DefProp::kWord, Bits(f), nullptr};
        }
    for (uint32_t cell : kUintNames)
        if (present()) gProps[CellName(cell)] = {DefProp::kWord, kUints[rng() % std::size(kUints)], nullptr};
    for (uint32_t cell : kCurveNames)
        if (present()) gProps[CellName(cell)] = {DefProp::kText, 0, Text(kCurves[rng() % std::size(kCurves)])};
    for (uint32_t cell : kFloatListNames)
        if (present()) gProps[CellName(cell)] = {DefProp::kText, 0, Text(kFloatLists[rng() % std::size(kFloatLists)])};
    for (uint32_t cell : kUintListNames)
        if (present()) gProps[CellName(cell)] = {DefProp::kText, 0, Text(uintLists[rng() % uintLists.size()].c_str())};
}

// The settings table for an ApplySettings case.
void SettingsWorld(std::mt19937 &rng)
{
    static const uint32_t kLevels[] = {0, 1, 50, 99, 100, 101, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffffffu,
                                       16777217u, 33554431u};
    static const uint32_t kBytes[] = {0, 1, 2, 0xff};
    static const char *const kModes[] = {"mono", "stereo", "5.1", ""};
    gProps.clear();
    auto present = [&] { return rng() % 3 != 0; };
    for (const char *name : {"FXVolume", "AmbienceVolume", "VOXVolume", "MusicVolume", "AudioPerformance"})
        if (present()) gProps[name] = {DefProp::kWord, kLevels[rng() % std::size(kLevels)], nullptr};
    for (const char *name : {"SoundMasterGroupVoxEnabled", "SoundMasterGroupMusicEnabled",
                             "SoundMasterGroupAmbienceEnabled", "SoundMasterGroupFXEnabled"})
        if (present()) gProps[name] = {DefProp::kWord, kBytes[rng() % std::size(kBytes)], nullptr};
    if (present()) {
        const uint32_t m = rng() % (std::size(kModes) + 1);
        gProps["SoundOutputMode"] = m < std::size(kModes) ? DefProp{DefProp::kText, 0, Text(kModes[m])}
                                                         : DefProp{DefProp::kNull, 0, nullptr};
    }
}

// ---- UpdateSpeakerConfig's config object ----
std::map<uint32_t, uint32_t> gCfgHas;   // id -> slot 10's answer
std::map<uint32_t, uint32_t> gCfgValue; // id -> what slot 9 writes (absent: nothing)
uint32_t CfgHas(Obj *, uint32_t *a)
{
    const uint32_t id = *Ptr<uint32_t>(a[0]);
    Ev(id);
    const auto it = gCfgHas.find(id);
    return it == gCfgHas.end() ? 0x200u : it->second;
}
uint32_t CfgGet(Obj *, uint32_t *a)
{
    const uint32_t id = *Ptr<uint32_t>(a[0]);
    Ev(id);
    const auto it = gCfgValue.find(id);
    if (it != gCfgValue.end()) Ptr<uint32_t>(a[1])[1] = it->second;
    return Roll();
}

// ---- UpdateSounds' sounds (slot 83 is past the generic fakes' 80 slots) ----
struct SoundFake {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t done, started, paused, flag9, category;
};
uint32_t __fastcall SoundAddRef(SoundFake *s, void *) { return uint32_t(++s->refs); }
uint32_t __fastcall SoundRelease(SoundFake *s, void *) { return uint32_t(--s->refs); }
uint32_t __fastcall SoundUpdate(SoundFake *s, void *, uint32_t elapsed)
{
    Ev(83);
    Ev(s->id);
    Ev(elapsed);
    return 0x8300u;
}
uint32_t __fastcall SoundDone(SoundFake *s, void *)
{
    Ev(71);
    Ev(s->id);
    return s->done;
}
uint32_t __fastcall SoundStarted(SoundFake *s, void *)
{
    Ev(62);
    Ev(s->id);
    return s->started;
}
uint32_t __fastcall SoundPaused(SoundFake *s, void *, uint32_t a)
{
    Ev(65);
    Ev(s->id);
    Ev(a);
    return s->paused;
}
uint32_t __fastcall SoundFlag(SoundFake *s, void *, uint32_t a)
{
    Ev(57);
    Ev(s->id);
    Ev(a);
    return s->flag9;
}
uint32_t __fastcall SoundCategory(SoundFake *s, void *)
{
    Ev(36);
    Ev(s->id);
    return s->category;
}
void *gSoundVT[90];
void BuildSoundVT()
{
    gSoundVT[1] = reinterpret_cast<void *>(&SoundAddRef);
    gSoundVT[2] = reinterpret_cast<void *>(&SoundRelease);
    gSoundVT[83] = reinterpret_cast<void *>(&SoundUpdate);
    gSoundVT[71] = reinterpret_cast<void *>(&SoundDone);
    gSoundVT[62] = reinterpret_cast<void *>(&SoundStarted);
    gSoundVT[65] = reinterpret_cast<void *>(&SoundPaused);
    gSoundVT[57] = reinterpret_cast<void *>(&SoundFlag);
    gSoundVT[36] = reinterpret_cast<void *>(&SoundCategory);
}

// A raw manager block for the workers (0xc5, the words the test sets): only the members the worker reads are built.
uint8_t *RawBlock()
{
    auto *mem = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memset(mem, 0xc5, Mgr::kSize);
    svcfx::AddRange(mem, Mgr::kSize, kTag);
    return mem;
}
// The block's words as DumpBlock logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kBlockKinds[] =
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd ddd";
// The block's words outside [skipLo, skipHi), by their kinds.
void DumpBlock(Blob &b, const uint8_t *mem, uint32_t skipLo, uint32_t skipHi)
{
    for (uint32_t off = 0; off < Mgr::kSize; off += 4)
        if (off < skipLo || off >= skipHi) svcfx::Field(b, Word(mem, off), kBlockKinds, off);
}

} // namespace audiosnd_test

using namespace audiosnd_test;

DIFF_TEST(AudioSoundManager_Run, 0x2188d6)
{
    // Also reached (see the file comment): 0x21a182 (factory), 0x218cf9, 0x218f2f, 0x21b544, 0x21b4fb, 0x21b144, 0x21a649,
    // 0x21aa33, 0x21c8df, 0x21cae7, 0x21b0a9, 0x21bc54, 0x21b8e7, 0x219ef7, 0x219124, 0x219247, 0x21c54f, 0x21c3b4,
    // 0x21cbcf, 0x21cce7, 0x21cf79, 0x21d047, 0x21ceac, 0x21afb7, 0x21cddb, 0x21aeea, 0x21a0c9, 0x21c61f, 0x21c484,
    // 0x21c6ea, 0x21d1c0, 0x21a1bd, 0x21a252, 0x21a36d, 0x21caef, 0x21c7eb, 0x21b716, 0x21bf88, 0x21bd43, 0x21bb79,
    // 0x21b9c2, 0x21c21e, 0x21c143, 0x21c063, 0x21b81a, 0x21b65d, 0x21c2f9, 0x21ba9d, 0x21aad8, 0x21aa95, 0x21beb3,
    // 0x21be44, 0x21b41b, 0x21ac60, 0x21abb8, 0x21aed6, 0x219ff8, 0x21b571, 0x21aec8, 0x21aecf, 0x21b578, 0x21b12a,
    // 0x21b5aa, 0x21934b, 0x21a4b1, 0x21a497, 0x21b5a3, 0x21a1d5, 0x21a206, 0x21a642, 0x21a231, 0x21a244, 0x21a24b,
    // 0x21b59c, 0x218f71, 0x219430, 0x21d15c, 0x21d114, 0x219385, 0x219b33, 0x21aedd, 0x21d5e9, 0x21a4f8, 0x21a4cb,
    // 0x21b5c4, 0x219fff, 0x2190ba, 0x219365, 0x21aebf, 0x218f78, 0x219082, 0x2190da, 0x21ac34, 0x21a225, 0x21a22b, the
    // thunks 0x21b1db, 0x21b1fb, 0x21b1e3, 0x21b1eb, 0x21b1f3, 0x218f69, 0x218f51, 0x218f59, 0x218f61, 0x21b549, 0x21b569,
    // 0x21b551, 0x21b559, 0x21b561.
    svcfx::gTrace = std::getenv("TS2_TEST_TRACE") != nullptr;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10faf6cu, 0x10f5194u, 0x10cc288u, 0x10cc2a8u, 0x10cc528u, 0xff4af8u, cTSAudioEvent::kPool,
                          cTSAudioEvent::kPool + 4, cTSAudioEvent::kPool + 8, cTSAudioEvent::kPoolLock})
        globals.SetWord(cell, 0);
    globals.SetWord(0xff5f10, 0x10f5);
    // GetSoundName's static string (0x10cc4f8) built, its guard (0x10cc518) past initialisation.
    for (uint32_t off = 0; off < 0x24; off += 4) globals.SetWord(0x10cc4f8 + off, 0);
    reinterpret_cast<cRZString *>(GameAddress<void>(0x10cc4f8))->Construct();
    globals.SetWord(0x10cc518, 0x80000000u);

    gSignalVT[1] = reinterpret_cast<void *>(&SignalSignal);
    gSignalVT[3] = reinterpret_cast<void *>(&SignalWait);
    std::vector<std::unique_ptr<shape::CodePatch>> patches;
    auto patch = [&](uint32_t rva, void *code) { patches.push_back(std::make_unique<shape::CodePatch>(rva, code)); };
    patch(0x824632, shape::CodeOf(&GameNew));
    patch(0x824624, shape::CodeOf(&GameDelete));
    patch(0x210087, reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&cTSAudioEvent::operator new)));
    patch(0x20ff24, shape::CodeOf(&cTSAudioEventAdapters::Construct));
#define IMPL(rva, n) patch(rva, Impl<rva, n>())
    // the request workers
    IMPL(0x220269, 2); IMPL(0x220062, 2); IMPL(0x21d882, 3); IMPL(0x21d3ff, 6); IMPL(0x21d4ff, 4); IMPL(0x22094a, 2);
    IMPL(0x220835, 2); IMPL(0x220d0a, 6); IMPL(0x220eec, 4); IMPL(0x221c1e, 3); IMPL(0x222000, 2); IMPL(0x221b59, 2);
    IMPL(0x21f0cc, 3); IMPL(0x221a18, 2); IMPL(0x21f000, 2); IMPL(0x21db90, 1); IMPL(0x220af9, 2); IMPL(0x2208dc, 2);
    IMPL(0x220b67, 3); IMPL(0x21dde7, 3); IMPL(0x220c98, 1); IMPL(0x220bf5, 4); IMPL(0x21ff8d, 4);
    IMPL(0x2206ca, 2); IMPL(0x22029e, 3); IMPL(0x2201f9, 2); IMPL(0x2200ae, 2); IMPL(0x2207ea, 2); IMPL(0x2207ac, 2);
    IMPL(0x220741, 3); IMPL(0x22001a, 2); IMPL(0x21ff2c, 1); IMPL(0x220828, 1); IMPL(0x21e9e5, 1); IMPL(0x22011e, 1);
    IMPL(0x2209f1, 2); IMPL(0x21fc9e, 0); IMPL(0x21ea18, 0);
    IMPL(0x21d654, 1);
    // the startup workers: ours on both sides (their own tests below compare them with the originals)
    patch(0x21f164, shape::CodeOf(&Mgr::LoadSoundDefaults));
    patch(0x220320, shape::CodeOf(&Mgr::ApplySettings));
    patch(0x2221e8, shape::CodeOf(&Mgr::UpdateSpeakerConfig));
    patch(0x222147, shape::CodeOf(&Mgr::UpdateSounds));
    // the event handlers
    IMPL(0x21e502, 1); IMPL(0x21e3c2, 1); IMPL(0x21e0ca, 1); IMPL(0x21e80e, 1); IMPL(0x21e465, 1); IMPL(0x21e316, 1);
    IMPL(0x21e432, 1); IMPL(0x21e3ff, 1); IMPL(0x21e5e2, 1); IMPL(0x21de95, 1); IMPL(0x21e087, 1); IMPL(0x21e751, 1);
    IMPL(0x21e02d, 1); IMPL(0x21e1e2, 1); IMPL(0x21e623, 1); IMPL(0x21e27d, 1); IMPL(0x21e04a, 1); IMPL(0x21e228, 1);
    IMPL(0x21e870, 1); IMPL(0x21e1ac, 1); IMPL(0x21e2e3, 1); IMPL(0x21e2b0, 1); IMPL(0x21dfae, 1); IMPL(0x21e59f, 1);
    IMPL(0x21e675, 1); IMPL(0x21e8f5, 1); IMPL(0x21e38f, 1); IMPL(0x21e34c, 1); IMPL(0x21e6b2, 1); IMPL(0x21def7, 1);
    IMPL(0x21e92b, 1); IMPL(0x21e7c1, 1); IMPL(0x21dff1, 1);
#undef IMPL
    patch(0x21dd75, shape::CodeOf(&StandCompletionTime));
    patch(0x22012b, shape::CodeOf(&StandSetValue));
    patch(0x21fba9, shape::CodeOf(&StandText));
    patch(0x21b370, shape::CodeOf(&StandRegistry));
    // Ours calls our 0x1a9942 (lane VX): the same stand-in over both.
    shape::StandIn iterator(0x1a9942, &nGZEvent::cAttributeIDIterator::Construct, &StandIterator);
    patch(0x1a98e0, shape::CodeOf(&StandRange));
    // Ours calls our 0x1a98e0 (lane LW): the same stand-in over it.
    patches.push_back(std::make_unique<shape::CodePatch>(
        shape::CodeOf(&nGZEvent::cGZEventReflectorAdapters::MakeIteratorRange), shape::CodeOf(&StandRange)));
    patch(0xc0a6a1, shape::CodeOf(&StandCurve));
    patch(0xc0a86a, shape::CodeOf(&StandCurveLoad));
    patch(0x23b4a8, shape::CodeOf(&StandBuffer));
    patch(0x23b4ce, shape::CodeOf(&StandBufferEnd));
    patch(0x21b203, shape::CodeOf(&StandBufferLoad));
    patch(0x21efa5, shape::CodeOf(&StandNamer<0x21efa5>));
    patch(0x21fd55, shape::CodeOf(&StandNamer<0x21fd55>));
    shape::StandIn thread(0x1b9cad, &nGZLog::StoreCurrentThreadId, &StandThread);
    shape::StandIn signal(0x9ccaa9, &cRZThreadSignal::Construct, &StandSignal);
    shape::StandIn signalEnd(0x9ccb13, &cRZThreadSignal::Destruct, &StandSignalEnd);
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::StandIn sprintf(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    VtablePatch logLine(cRZStd::kVTable, 2, reinterpret_cast<void *>(&LogLine));

    gText = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    gText->Construct("a request's text");
    Slot<uint32_t>(gText, 1);

    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        // Even seeds at 24-bit x87 precision, as the game's D3D9 thread may run (GetCategoryVolume's x87 products;
        // lane PJ, docs/lane/test.md "x87 precision"), odd seeds at the harness's 53-bit.
        difftest::X87Precision precision(seed % 2 ? difftest::X87Pc::Bits53 : difftest::X87Pc::Bits24);
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            BuildPool();
            svcfx::AddRange(gPages, kPageSize * kPages, kPageTag);
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
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.push_back(*GameAddress<uint32_t>(0x10cc528) == P(&F(kEvMgr)));
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioSoundManager_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                Slot<uint32_t>(gText, 2);
                reinterpret_cast<cRZString *>(GameAddress<void>(0x10cc4f8))->Destruct();
                return;
            }
        }
    }
    Slot<uint32_t>(gText, 2);
    reinterpret_cast<cRZString *>(GameAddress<void>(0x10cc4f8))->Destruct();
}

// AudioRamp_Step: the volume ramp's step 0x21d114 (hooked: cTSAudioSoundInstance still calls it) on its own, the
// original against ours on the same ramp: values, targets and rates from a pool with equal, signed zero, infinite and
// NaN entries, rates at and next to the threshold at 0xd9e82c (min below it, max above), elapsed times 0..1000, negative
// and NaN. The result byte and the ramp's three floats compared.
DIFF_TEST(AudioRamp_Step, 0x21d114)
{
    const float threshold = *GameAddress<float>(0xd9e82c);
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float values[] = {0.0f, -0.0f, 1.0f, 0.5f, 0.25f, -1.0f, 3.0f, inf, -inf, nan, threshold};
    const float rates[] = {0.0f, -0.0f, 0.001f, -0.001f, 0.5f, -0.5f, 2.0f, -2.0f, inf, -inf, nan, threshold,
                           std::nextafter(threshold, inf), std::nextafter(threshold, -inf)};
    const float elapsed[] = {0.0f, 1.0f, 16.0f, 33.0f, 1000.0f, -1.0f, nan};
    std::mt19937 rng(77);
    for (uint32_t n = 0; n < 20000; ++n) {
        const float value = values[rng() % std::size(values)];
        const float target = rng() % 4 ? values[rng() % std::size(values)] : value;
        const float rate = rates[rng() % std::size(rates)];
        const float dt = elapsed[rng() % std::size(elapsed)];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            AudioRamp ramp{value, target, rate};
            const bool moved = side ? ramp.Step(dt)
                                    : ctx.Original<bool(__thiscall *)(AudioRamp *, float)>(0x21d114)(&ramp, dt);
            results[side] = {uint32_t(moved), Bits(ramp.value), Bits(ramp.target), Bits(ramp.rate)};
        }
        if (!svcfx::Same(ctx, results[0], results[1], "AudioRamp_Step", n)) return;
    }
}

// SoundDefaults_Load: LoadSoundDefaults (0x21f164, hooked) with its helpers, in effect through it: the getters 0x217fbb
// (float) and 0x218012 (uint32), the list readers 0x21817d (floats) and 0x2180f2 (uint32s) with their token readers
// 0x23b6af / 0x23b687, and the truncating copy 0x218208. On a raw manager block (0xc5; the two curve strings +0x1c0 /
// +0x1e0 built; +0xb0 null or a fake's reference, released first): the persist resource manager (cell 0x10faf6c) loads
// the defaults set or not (al 0 with the upper bytes set), the set answers each name present (al; floats including NaN,
// infinities and 1e30, uint32s at 0 / 2^31 / 2^32-1; curve texts; lists empty, of 1..6 floats around the caps 3 and 5,
// with blanks, signs, exponents, junk and values past int32 / int64 for the truncation, uint32 lists of 26 / 27 / 28 / 40
// numbers around the 27 of the per-group table, zeros (-1 there), hex / octal, overflow). Compared: the result, the block
// (the strings by text), the log (the set's names, the loads' keys), the fakes' counts and the counts of the texts handed
// out. Stood in on both sides: the curve load 0xc0a86a (logging the text) and cRZString::Sprintf 0x1ad6f7; the number
// reader 0x1b1ab5 and the CRT's strtoul run for real.
DIFF_TEST(SoundDefaults_Load, 0x21f164)
{
    // Also reached: 0x217fbb, 0x218012, 0x2180f2, 0x21817d, 0x23b687, 0x23b6af, 0x218208.
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(svcfx::kPersistCell, &F(kDefRes));
    globals.SetWord(0x10cc4c8, 0);
    globals.SetWord(0x10cc4cc, 0);
    globals.SetWord(0xff5f10, 0x10f5);
    std::vector<std::unique_ptr<shape::CodePatch>> patches;
    auto patch = [&](uint32_t rva, void *code) { patches.push_back(std::make_unique<shape::CodePatch>(rva, code)); };
    patch(0x824632, shape::CodeOf(&GameNew));
    patch(0x824624, shape::CodeOf(&GameDelete));
    patch(0xc0a86a, shape::CodeOf(&StandCurveLoadText));
    shape::StandIn sprintf(0x1ad6f7, &cRZString::Sprintf, &StandSprintf);
    VtablePatch logLine(cRZStd::kVTable, 2, reinterpret_cast<void *>(&LogLine));

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng(seed);
        DefaultsWorld(rng);
        gDefLoads = rng() % 6 != 0;
        const bool old = rng() % 2 != 0;
        const uint32_t instance[2] = {rng(), rng()};
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kDefRes).Set(5, "pdp--", 0, &ResLoad);
            F(kDefSet).Set(9, "pp", 0, &SetGetValue);
            *GameAddress<uint32_t>(0x10cc4c8) = instance[0];
            *GameAddress<uint32_t>(0x10cc4cc) = instance[1];
            gRng.seed(seed);
            uint8_t *const mem = RawBlock();
            auto *s1 = reinterpret_cast<cRZString *>(mem + 0x1c0);
            auto *s2 = reinterpret_cast<cRZString *>(mem + 0x1e0);
            s1->Construct();
            s2->Construct();
            *reinterpret_cast<uint32_t *>(mem + 0xb0) = old ? P(&F(kDefOld)) : 0;
            std::vector<uint32_t> before;
            for (const auto &[name, prop] : gProps)
                if (prop.text) before.push_back(Count(prop.text));
            const bool r = side ? reinterpret_cast<Mgr *>(mem)->LoadSoundDefaults()
                                : ctx.Original<bool(__thiscall *)(void *)>(0x21f164)(mem);
            Blob &b = results[side];
            b.push_back(r);
            DumpBlock(b, mem, 0x1c0, 0x200);
            DumpText(b, s1);
            DumpText(b, s2);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::CaptureFakes(b);
            size_t i = 0;
            for (const auto &[name, prop] : gProps)
                if (prop.text) b.push_back(Count(prop.text) - before[i++]);
            s1->Destruct();
            s2->Destruct();
            ::operator delete(mem);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "SoundDefaults_Load", seed)) {
            if (svcfx::gTrace)
                for (size_t w = 0; w < results[0].size() || w < results[1].size(); ++w)
                    fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0].size() ? results[0][w] : 0,
                            w < results[1].size() ? results[1][w] : 0);
            return;
        }
    }
}

// SoundDefaults_Getters: the property getters 0x217fbb (float) and 0x218012 (uint32) on their own, as LoadSoundDefaults
// never passes them a null set nor a fallback other than its fixed ones: a null set, a set without the name and one with
// it (al answers with the upper bytes set or not), fallbacks and values from pools with signed zeros, NaN, infinities,
// 0 / 1 / 2^31 / 2^32-1. Compared: the results (float bits), the log, the fakes' counts.
DIFF_TEST(SoundDefaults_Getters, 0x217fbb)
{
    // Also reached: 0x218012.
    svcfx::AllocatorScope allocator;
    const float floats[] = {0.0f, -0.0f, 1.0f, 0.6f, -3.5f, 1e30f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()};
    const uint32_t words[] = {0, 1, 7, 0x80000000u, 0xffffffffu};
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t kind = rng() % 3; // 0: no set, 1: the name absent, 2: present
        const bool asFloat = rng() % 2 != 0;
        const float fallback = floats[rng() % std::size(floats)];
        const uint32_t fallbackWord = words[rng() % std::size(words)];
        const uint32_t stored = asFloat ? Bits(floats[rng() % std::size(floats)]) : words[rng() % std::size(words)];
        gProps.clear();
        if (kind == 2) gProps["Name"] = {DefProp::kWord, stored, nullptr};
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kDefSet).Set(9, "pp", 0, &SetGetValue);
            gRng.seed(seed);
            void *const set = kind ? &F(kDefSet) : nullptr;
            const GZServiceString name("Name");
            uint32_t r;
            if (asFloat) {
                using Fn = float(__cdecl *)(void *, const GZServiceString *, float);
                r = Bits(side ? SoundDefaults::GetFloat(set, &name, fallback) : ctx.Original<Fn>(0x217fbb)(set, &name, fallback));
            } else {
                using Fn = uint32_t(__cdecl *)(void *, const GZServiceString *, uint32_t);
                r = side ? SoundDefaults::GetUint(set, &name, fallbackWord) : ctx.Original<Fn>(0x218012)(set, &name, fallbackWord);
            }
            Blob &b = results[side];
            b.push_back(r);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "SoundDefaults_Getters", seed)) return;
    }
}

// SoundSettings_Apply: ApplySettings (0x220320, hooked) with `this` a fake whose slots 19, 51, 52 and 58 record: the
// settings service (framework slot 5, 0x0ac1887e / 0x4bb0d269) there or not, each setting present or not: the four
// volumes as percentages (0, 1, 99..101, 2^24+1, 2^25-1 and 2^31-1 .. 2^32-1, the unsigned conversion), the performance
// level, the four group switches (bytes 0 / 1 / 2 / 0xff) and the output mode (a text, an empty one, or a null string).
// Compared: the log (the setting names, slot 19's float bits, slot 58's text), the fakes' counts and the texts' counts.
// Slot 51's second argument is compared by its low byte only: the original pushes the AnyBoolean's whole dword
// (0x22062c `push dword ptr [ebp - 0x34]`) of which only the byte was ever written (0x22036d `mov byte ptr [ebp - 0x34],
// 1`, the set's Assign writes the byte), so its upper bytes are the original's stack.
DIFF_TEST(SoundSettings_Apply, 0x220320)
{
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    globals.Set(0x10c9378, &F(kDefFw));
    std::vector<std::unique_ptr<shape::CodePatch>> patches;
    auto patch = [&](uint32_t rva, void *code) { patches.push_back(std::make_unique<shape::CodePatch>(rva, code)); };
    patch(0x824632, shape::CodeOf(&GameNew));
    patch(0x824624, shape::CodeOf(&GameDelete));

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng(seed);
        SettingsWorld(rng);
        gWithSettings = rng() % 4 != 0;
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kDefFw).Set(5, "ddp", 0, &FwService);
            F(kDefSettings).Set(9, "pp", 0, &SetGetValue);
            Obj &m = F(kDefMgr);
            m.Set(19, "dd", 0x1900);
            m.Set(52, "d", 0x5200);
            m.table[51] = reinterpret_cast<void *>(&MgrSlot51);
            m.table[58] = reinterpret_cast<void *>(&MgrSlot58);
            gRng.seed(seed);
            std::vector<uint32_t> before;
            for (const auto &[name, prop] : gProps)
                if (prop.text) before.push_back(Count(prop.text));
            if (side) reinterpret_cast<Mgr *>(&m)->ApplySettings();
            else ctx.Original<void(__thiscall *)(void *)>(0x220320)(&m);
            Blob &b = results[side];
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::CaptureFakes(b);
            size_t i = 0;
            for (const auto &[name, prop] : gProps)
                if (prop.text) b.push_back(Count(prop.text) - before[i++]);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "SoundSettings_Apply", seed)) return;
    }
}

// SoundSpeaker_Config: UpdateSpeakerConfig (0x2221e8, hooked) and the config lookups 0x21e9ab (has) / 0x21dc23 (get) (hooked:
// the request worker 0x220eec still calls them) on a raw block whose map +0x318 holds the config object (key 0xedc89ac8)
// or not, and other keys: the object's slot 10 answers 0xcd201058 / 0xac19638c / 0xdc99afb (al, upper bytes set or not)
// and slot 9 writes or not, values 0, 1, 2..4 (the modes with multipliers; 1 and 5 next to them), 2^31+2, 2^32-1; the
// pitch / volume tables +0x284 / +0x298 and +0x338..+0x344 random. Then the lookups directly with the config key, another
// key in the map and an absent one. Compared: the block (the map's own words aside), the lookups' results by their low
// byte (their callers 0x2221e8 and 0x220eec test al only: 0x22120d `test al, al`; the original's absent path returns the
// map's end iterator with its low byte cleared), the log and the fakes' counts.
DIFF_TEST(SoundSpeaker_Config, 0x2221e8)
{
    // Also reached: 0x21e9ab, 0x21dc23.
    const uint32_t ids[3] = {0xcd201058, 0xac19638c, 0xdc99afb};
    const uint32_t values[] = {0, 1, 2, 3, 4, 5, 0x100, 0x80000002u, 0xffffffffu};
    const float floats[] = {0.0f, 1.0f, 0.5f, -2.0f, 1.2f, std::numeric_limits<float>::quiet_NaN()};
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t configs = rng() % 4; // 0: no config object
        const uint32_t others = rng() % 3;
        gCfgHas.clear();
        gCfgValue.clear();
        for (uint32_t id : ids) {
            if (rng() % 4) gCfgHas[id] = rng() % 3 ? (0x100u | (rng() % 2)) : 0x7f000001u;
            if (rng() % 4) gCfgValue[id] = values[rng() % std::size(values)];
        }
        uint32_t fields[0x30 / 4 + 4];
        for (uint32_t &w : fields) w = rng();
        float tables[10];
        for (float &f : tables) f = floats[rng() % std::size(floats)];
        const uint32_t probeKey[3] = {0xedc89ac8u, 0x5000u + others, 0x1234u};
        const uint32_t probeId = rng() % 2 ? ids[rng() % 3] : rng();
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kDefCfg).Set(10, "p", 0, &CfgHas);
            F(kDefCfg).Set(9, "pp", 0, &CfgGet);
            F(kDefOther).Set(10, "p", 0, &CfgHas);
            F(kDefOther).Set(9, "pp", 0, &CfgGet);
            gRng.seed(seed);
            uint8_t *const mem = RawBlock();
            auto *mgr = reinterpret_cast<Mgr *>(mem);
            auto *map = ::new (&mgr->mRefs318) std::unordered_map<uint32_t, AudioRef>();
            for (uint32_t k = 0; k <= others; ++k)
                (*map)[0x5000u + k].Assign(reinterpret_cast<cIGZUnknown *>(&F(kDefOther)));
            if (configs) (*map)[0xedc89ac8u].Assign(reinterpret_cast<cIGZUnknown *>(&F(kDefCfg)));
            std::memcpy(mem + 0x284, tables, sizeof(tables));
            std::memcpy(mem + 0x338, fields, 0x10);
            Blob &b = results[side];
            if (side) mgr->UpdateSpeakerConfig();
            else ctx.Original<void(__thiscall *)(void *)>(0x2221e8)(mem);
            for (uint32_t key : probeKey) {
                gz::AnyUint32 out(0x77);
                const uint32_t has = side ? uint32_t(mgr->HasConfigProperty(key, probeId))
                                          : ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x21e9ab)(
                                                mem, key, probeId);
                const uint32_t got =
                    side ? uint32_t(mgr->GetConfigProperty(key, probeId, &out))
                         : ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *)>(0x21dc23)(mem, key,
                                                                                                             probeId, &out);
                b.push_back(has & 0xff);
                b.push_back(got & 0xff);
                b.push_back(out.mValue);
            }
            DumpBlock(b, mem, 0x318, 0x338);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            map->~unordered_map();
            svcfx::CaptureFakes(b);
            ::operator delete(mem);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "SoundSpeaker_Config", seed)) return;
    }
}

// SoundUpdate_Sounds: UpdateSounds (0x222147, hooked: in effect for HandleEvent, Update and Request37 through ours too) on
// a raw block with 0..7 sounds in the list +0x60 (fakes with their own 90-slot table): each sound's slot 71 (0, 1, 0x100,
// 2^31: compared whole), 62 / 65 / 57 (al, upper bytes set or not) and its category (slot 36, 0..8: the counter table's
// ends) drawn, the elapsed time and the counters +0x28 and +0x68..+0x98 random. Compared: the block (the list's own words
// aside), the log and the sounds' counts.
DIFF_TEST(SoundUpdate_Sounds, 0x222147)
{
    BuildSoundVT();
    const uint32_t dones[] = {0, 0, 0, 1, 0x100, 0x80000000u};
    const uint32_t bools[] = {0, 1, 0x100, 0x101, 0xff00, 0x7f};
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        std::mt19937 rng(seed);
        SoundFake sounds[7];
        const uint32_t count = rng() % 8;
        for (uint32_t i = 0; i < count; ++i) {
            SoundFake &s = sounds[i];
            s.vt = gSoundVT;
            s.id = 0x500 + i;
            s.refs = 100;
            s.done = dones[rng() % std::size(dones)];
            s.started = bools[rng() % std::size(bools)];
            s.paused = bools[rng() % std::size(bools)];
            s.flag9 = bools[rng() % std::size(bools)];
            s.category = rng() % 9;
        }
        uint32_t fields[13];
        for (uint32_t &w : fields) w = rng() % 4 ? rng() % 16 : rng();
        const uint32_t elapsed = rng() % 3 ? rng() % 100 : rng();
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            for (uint32_t i = 0; i < count; ++i) sounds[i].refs = 100;
            svcfx::ResetAll();
            uint8_t *const mem = RawBlock();
            auto *mgr = reinterpret_cast<Mgr *>(mem);
            auto *list = ::new (&mgr->mList60) std::list<AudioRef>();
            for (uint32_t i = 0; i < count; ++i) list->emplace_back(reinterpret_cast<cIGZUnknown *>(&sounds[i]));
            std::memcpy(mem + 0x28, fields, 4);
            std::memcpy(mem + 0x68, fields + 1, 0x30);
            if (side) mgr->UpdateSounds(elapsed);
            else ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x222147)(mem, elapsed);
            Blob &b = results[side];
            DumpBlock(b, mem, 0x60, 0x68);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            list->~list();
            for (uint32_t i = 0; i < count; ++i) b.push_back(uint32_t(sounds[i].refs));
            ::operator delete(mem);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "SoundUpdate_Sounds", seed)) return;
    }
}
