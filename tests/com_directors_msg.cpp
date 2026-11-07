// Differential test for the audio director's message handler (lane FB, src/rz/COMDirectors.cpp):
// cTSAudioCOMDirector::DoMessage 0x20e7c7 (slot 3 of the +0x64 face, cIGZMessage2 target).
//
// Method (the fake world of tests/com_directors_start.cpp, cut to what this function touches). Every object the code
// calls through a vtable is a fake whose slots log the call (slot, the object's identity, the arguments with known
// objects mapped to identities and stack addresses to one marker):
//  - the message (id 1): slot 0 QueryInterface (answer and out object from the scenario), slot 4 the type;
//  - the director block (id 0x10; its +0x64 face id 0x74, &mField90 id 0x90): slot 12 gives the framework COM object;
//    the +0x64 vtable is all traps (the handler must not dispatch to its own face);
//  - the framework COM object (id 2): slot 3 creates (an object or none, per call);
//  - created and held objects (ids 0x400+): slot 0 QueryInterface (always an object, the answer per call), slots 3 / 7 /
//    15 answer per call (words whose low byte decides, e.g. 0x100 is "no"), slot 6 gives an object;
//  - the sound service (id 3), what 0x9c5b7e returns or null: slot 13 makes an object;
//  - game code still called by both sides, patched to logged fakes: 0x20f60e, 0x20f028, 0x20f805 (the director's own
//    helpers), 0xb39640 (cheat registration: the command's vtable RVA, two words and allocation size are logged, then it
//    is freed), 0x9c5b7e; the log object's slot 2 (vtable 0xda3e6c) logs the cRZStd as built and the line's arguments.
//    The image's operator new / delete (0x824632 / 0x824624) go to malloc / free, as ours do.
// Routes: the original on the block, and ours (the hooked member, called on the same block). The hook is the member
// itself (the audio director is hook-shaped: no adapter, no vtable of ours), so the third route of ET's tests (an
// adapter with its own slot trapped) is the second route with the +0x64 vtable trapped, which every route has.
// Compared: the call log, the result, the escaping exception, the ten references and the words +0x90..+0x9c after the
// call. Scenarios are random over the message type (0xca, 0xcb, 0xeded3807, others, null message), every answer and the
// initial references; for the first 40 the k-th logged call (any but AddRef / Release, which the original makes in
// noexcept regions) throws, for every k: the holders must be released the same way.
// Constraint: the original uses three objects untested (reference 1's created object, what it answers for 0x89e51472,
// the sound service's made object): the scenarios always give those (a null there is an access violation in both).
#include "difftest.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "rz/COMDirectors.h"
#include "rz/MiscHelpersA.h"

#include <intrin.h>
#include <malloc.h>
#include <windows.h>

#include <array>
#include <cstring>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace {

using namespace comdir;

uint32_t U(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::vector<uint32_t> gLog;
struct Known {
    const void *p;
    uint32_t id;
};
std::vector<Known> gKnown;
void Know(const void *p, uint32_t id) { gKnown.push_back({p, id}); }

uint32_t IdOf(uint32_t v)
{
    if (!v) return 0;
    for (const Known &k : gKnown)
        if (U(k.p) == v) return k.id;
    auto *tib = reinterpret_cast<NT_TIB *>(NtCurrentTeb());
    if (v >= U(tib->StackLimit) && v < U(tib->StackBase)) return 0x5555;
    return v;
}

struct FakeObject {
    void **vtable;
    uint32_t pad[3];
};

enum Kind { kMessage, kCom, kSound, kObject, kDirector, kOther };

void *gMsgVt[64], *gComVt[64], *gSoundVt[64], *gObjectVt[64], *gDirVt[64], *gTrapVt[64];
FakeObject gMsg{}, gCom{}, gSound{};
FakeObject gPool[64];
int gPoolUsed = 0;
uint8_t *gImage = nullptr;

// The scenario.
struct Scenario {
    uint32_t type = 0;
    bool msgQiRet = false, msgQiOut = false;
    uint64_t createOut = 0;
    uint64_t qiRet = 0;
    uint32_t answers[64] = {};
    bool soundGiven = false;
};
Scenario gS;
int gCreates = 0, gQis = 0, gAnswers = 0, gCalls = 0, gThrowAt = -1;
bool gTrapHit = false;

Kind KindOf(void *self)
{
    void **vt = *static_cast<void ***>(self);
    if (vt == gMsgVt) return kMessage;
    if (vt == gComVt) return kCom;
    if (vt == gSoundVt) return kSound;
    if (vt == gObjectVt) return kObject;
    if (vt == gDirVt) return kDirector;
    return kOther;
}

void *NewPoolObject()
{
    FakeObject *o = &gPool[gPoolUsed++ & 63];
    o->vtable = gObjectVt;
    return o;
}

void Throwable()
{
    if (gCalls++ == gThrowAt) throw 9;
}

uint32_t Behave(void *self, int slot, int arity, const uint32_t *args)
{
    Kind kind = KindOf(self);
    gLog.push_back(0x100 + slot);
    gLog.push_back(IdOf(U(self)));
    for (int i = 0; i < arity; ++i) gLog.push_back(IdOf(args[i]));
    if (slot != 1 && slot != 2) Throwable();
    switch (kind) {
    case kMessage:
        if (slot == 0) {
            if (gS.msgQiOut) *reinterpret_cast<void **>(args[1]) = NewPoolObject();
            return gS.msgQiRet ? 0x101 : 0x100;
        }
        if (slot == 4) return gS.type;
        break;
    case kCom:
        if (slot == 3) {
            int k = gCreates++;
            if ((gS.createOut >> k) & 1) *reinterpret_cast<void **>(args[2]) = NewPoolObject();
            return k & 1;
        }
        break;
    case kObject:
        if (slot == 0) {
            *reinterpret_cast<void **>(args[1]) = NewPoolObject();
            return ((gS.qiRet >> (gQis++ & 63)) & 1) ? 1 : 0x200;
        }
        if (slot == 3 || slot == 7 || slot == 15) return gS.answers[gAnswers++ & 63];
        if (slot == 6) return U(NewPoolObject());
        break;
    case kSound:
        if (slot == 13) *reinterpret_cast<void **>(args[2]) = NewPoolObject();
        break;
    case kDirector:
        if (slot == 12) return U(&gCom);
        break;
    default: break;
    }
    return 0x7e7e;
}

template <int S> uint32_t __fastcall Rec0(void *self, void *) { return Behave(self, S, 0, nullptr); }
template <int S> uint32_t __fastcall Rec1(void *self, void *, uint32_t a)
{
    uint32_t v[] = {a};
    return Behave(self, S, 1, v);
}
template <int S> uint32_t __fastcall Rec2(void *self, void *, uint32_t a, uint32_t b)
{
    uint32_t v[] = {a, b};
    return Behave(self, S, 2, v);
}
template <int S> uint32_t __fastcall Rec3(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t v[] = {a, b, c};
    return Behave(self, S, 3, v);
}
template <int S> uint32_t __fastcall Rec7(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    uint32_t v[] = {a, b, c, d, e, f, g};
    return Behave(self, S, 7, v);
}

template <size_t... S> std::array<std::array<void *, 64>, 8> MakeRecs(std::index_sequence<S...>)
{
    return {{{reinterpret_cast<void *>(&Rec0<S>)...},
             {reinterpret_cast<void *>(&Rec1<S>)...},
             {reinterpret_cast<void *>(&Rec2<S>)...},
             {reinterpret_cast<void *>(&Rec3<S>)...},
             {},
             {},
             {},
             {reinterpret_cast<void *>(&Rec7<S>)...}}};
}
const std::array<std::array<void *, 64>, 8> kRecs = MakeRecs(std::make_index_sequence<64>{});

void BuildVt(void **vt, std::initializer_list<std::pair<int, int>> arities)
{
    for (int s = 0; s < 64; ++s) vt[s] = kRecs[0][s];
    for (auto [slot, arity] : arities) vt[slot] = kRecs[arity][slot];
}

uint32_t __fastcall Trap(void *, void *)
{
    gTrapHit = true;
    return 0;
}

void BuildWorld()
{
    BuildVt(gMsgVt, {{0, 2}});
    BuildVt(gComVt, {{3, 3}});
    BuildVt(gSoundVt, {{8, 1}, {13, 7}});
    BuildVt(gObjectVt, {{0, 2}, {33, 1}});
    BuildVt(gDirVt, {});
    for (auto &s : gTrapVt) s = reinterpret_cast<void *>(&Trap);
    gMsg.vtable = gMsgVt;
    gCom.vtable = gComVt;
    gSound.vtable = gSoundVt;
    for (FakeObject &o : gPool) o.vtable = gObjectVt;
}

// ---------------------------------------------------------------------------------------------
// Patches of the image

struct Jump {
    uint8_t *site;
    uint8_t saved[5];
    Jump(void *at, const void *to) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(U(to) - (U(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    Jump(const Jump &) = delete;
    Jump &operator=(const Jump &) = delete;
    ~Jump() { std::memcpy(site, saved, 5); }
};

void *__cdecl ImageNew(size_t size) { return malloc(size); }
void __cdecl ImageSizedDelete(void *p, size_t) { free(p); }

void __fastcall FakeReset(void *self, void *)
{
    gLog.push_back(0x20f6);
    gLog.push_back(IdOf(U(self)));
    Throwable();
}
void __fastcall FakeReload(void *self, void *)
{
    gLog.push_back(0x20f0);
    gLog.push_back(IdOf(U(self)));
    Throwable();
}
void __fastcall FakeRebind(void *self, void *, void *field)
{
    gLog.push_back(0x20f8);
    gLog.push_back(IdOf(U(self)));
    gLog.push_back(IdOf(U(field)));
    Throwable();
}
void __cdecl FakeRegisterCheat(void *command)
{
    gLog.push_back(0xb396);
    if (!command) {
        gLog.push_back(0);
    } else {
        auto *w = static_cast<uint32_t *>(command);
        gLog.push_back(w[0] - U(gImage));
        gLog.push_back(w[1]);
        gLog.push_back(w[2]);
        gLog.push_back(static_cast<uint32_t>(_msize(command)));
        free(command);
    }
    Throwable();
}
void *__cdecl FakeSoundService()
{
    gLog.push_back(0x9c5b);
    Throwable();
    return gS.soundGiven ? &gSound : nullptr;
}
void __cdecl FakeLogLine(cRZStd *self, uint32_t channel, uint32_t level, const char *text)
{
    gLog.push_back(0x77);
    gLog.push_back(U(self->mVTable) - U(gImage));
    gLog.push_back(self->mFirst - U(gImage));
    gLog.push_back(self->mSecond);
    gLog.push_back(self->mFlags);
    gLog.push_back(channel);
    gLog.push_back(level);
    gLog.push_back(U(text) - U(gImage));
    Throwable();
}

struct LogVtablePatch {
    void **slot;
    void *saved;
    explicit LogVtablePatch(difftest::Context &ctx) : slot(ctx.image.At<void *>(cRZStd::kVTable + 8))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&FakeLogLine);
    }
    LogVtablePatch(const LogVtablePatch &) = delete;
    LogVtablePatch &operator=(const LogVtablePatch &) = delete;
    ~LogVtablePatch() { *slot = saved; }
};

// ---------------------------------------------------------------------------------------------
// One route

struct Outcome {
    std::vector<uint32_t> log;
    unsigned long escaped = 0;
    bool result = false;
    int calls = 0;
    uint32_t refs[10] = {};
    uint32_t words[4] = {};
};

struct Invocation {
    void *fn;
    void *self;
    void *message;
    bool result;
};
void Invoke(Invocation *c) { c->result = reinterpret_cast<bool(__thiscall *)(void *, void *)>(c->fn)(c->self, c->message); }
unsigned long InvokeGuarded(Invocation *c)
{
    __try {
        Invoke(c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    return 0;
}

int gInitialRefs[10]; // pool index or -1
bool gNullMessage = false;

Outcome RunRoute(difftest::Context &ctx, int route)
{
    auto *d = static_cast<uint8_t *>(malloc(0xa8));
    std::memset(d, 0x5a, 0xa8);
    *reinterpret_cast<void ***>(d) = gDirVt;
    *reinterpret_cast<void ***>(d + 0x64) = gTrapVt;
    auto *refs = reinterpret_cast<void **>(d + 0x68);
    for (int i = 0; i < 10; ++i) refs[i] = gInitialRefs[i] < 0 ? nullptr : &gPool[gInitialRefs[i]];
    for (uint32_t i = 0; i < 4; ++i) *reinterpret_cast<uint32_t *>(d + 0x90 + 4 * i) = 0x90900000 + i;
    gKnown.clear();
    Know(&gMsg, 1);
    Know(&gCom, 2);
    Know(&gSound, 3);
    Know(d, 0x10);
    Know(d + 0x64, 0x74);
    Know(d + 0x90, 0x90);
    for (int k = 0; k < 64; ++k) Know(&gPool[k], 0x400 + k);
    gPoolUsed = 10;
    gCreates = gQis = gAnswers = gCalls = 0;
    gLog.clear();
    gTrapHit = false;

    void *fn = route == 0 ? ctx.image.At(0x20e7c7) : MemberAddress(&AudioMessageTarget::DoMessage);
    Invocation call{fn, d + 0x64, gNullMessage ? nullptr : &gMsg, false};
    Outcome out;
    out.escaped = InvokeGuarded(&call);
    out.result = call.result;
    out.calls = gCalls;
    out.log = gLog;
    for (int i = 0; i < 10; ++i) out.refs[i] = IdOf(U(refs[i]));
    for (int i = 0; i < 4; ++i) out.words[i] = *reinterpret_cast<uint32_t *>(d + 0x90 + 4 * i);
    if (gTrapHit) ctx.Fail("DoMessage route %d dispatched through the director's own +0x64 vtable", route);
    free(d);
    return out;
}

bool Same(const Outcome &a, const Outcome &b)
{
    return a.escaped == b.escaped && a.log == b.log && (a.escaped != 0 || a.result == b.result) && std::memcmp(a.refs, b.refs, sizeof(a.refs)) == 0 &&
           std::memcmp(a.words, b.words, sizeof(a.words)) == 0;
}

} // namespace

DIFF_TEST(COMDirectorsMsg_AudioDoMessage, 0x20e7c7)
{
    gImage = ctx.image.At<uint8_t>(0);
    BuildWorld();
    Jump imageNew(ctx.image.At(0x824632), reinterpret_cast<void *>(&ImageNew));
    Jump imageDelete(ctx.image.At(0x824624), reinterpret_cast<void *>(&ImageSizedDelete));
    Jump reset(ctx.image.At(0x20f60e), reinterpret_cast<void *>(&FakeReset));
    Jump reload(ctx.image.At(0x20f028), reinterpret_cast<void *>(&FakeReload));
    Jump rebind(ctx.image.At(0x20f805), reinterpret_cast<void *>(&FakeRebind));
    Jump cheat(ctx.image.At(0xb39640), reinterpret_cast<void *>(&FakeRegisterCheat));
    Jump sound(ctx.image.At(0x9c5b7e), reinterpret_cast<void *>(&FakeSoundService));
    LogVtablePatch logPatch(ctx);
    uint32_t *channel = ctx.image.At<uint32_t>(0xff5f10);
    uint32_t savedChannel = *channel;
    *channel = 0x5a5a0001;

    static const uint32_t kTypes[] = {0xca, 0xcb, 0xeded3807, 0xc9, 0x1ca};
    static const uint32_t kAnswers[] = {0, 1, 0x100, 0x101, 0xffffff00u, 0xff, 0x80, 0x200};
    std::mt19937 rng(0xFB20e7c7);
    unsigned withThrows = 0;
    // What the original did across the scenarios (each must have happened at least once).
    unsigned lines = 0, rebinds = 0, handed = 0, resets = 0, stops = 0, starts = 0;
    for (int scenario = 0; scenario < 400; ++scenario) {
        gS = Scenario{};
        gS.type = kTypes[scenario < 15 ? scenario % 3 : rng() % 5];
        gNullMessage = scenario >= 15 && rng() % 16 == 0;
        gS.msgQiRet = scenario < 15 || rng() % 8 != 0;
        gS.msgQiOut = rng() % 4 != 0;
        gS.createOut = (static_cast<uint64_t>(rng()) << 32) | rng();
        gS.qiRet = (static_cast<uint64_t>(rng()) << 32) | rng();
        for (uint32_t &a : gS.answers) a = kAnswers[rng() % 8];
        if (scenario < 3) {
            // Everything present and answering.
            gS.createOut = ~0ull;
            gS.qiRet = ~0ull;
            for (uint32_t &a : gS.answers) a = 1;
        }
        if (gS.type == 0xca) gS.createOut |= 1ull << 7; // reference 1's object is used untested
        gS.soundGiven = scenario < 3 || rng() % 4 != 0;
        for (int &r : gInitialRefs) r = (scenario < 3 || rng() % 3 != 0) ? static_cast<int>(rng() % 10) : -1;
        if (scenario < 3) gInitialRefs[9] = 9;

        gThrowAt = -1;
        Outcome first = RunRoute(ctx, 0);
        for (size_t i = 0; i + 1 < first.log.size(); ++i) {
            uint32_t w = first.log[i];
            lines += w == 0x77;
            rebinds += w == 0x20f8;
            resets += w == 0x20f6;
            handed += w == 0x100 + 33;
        }
        stops += gS.type == 0xcb && !gNullMessage && gS.msgQiRet;
        starts += first.result;
        int throwModes = scenario < 40 ? first.calls : 0;
        for (int mode = -1; mode < throwModes; ++mode) {
            gThrowAt = mode;
            Outcome a = mode == -1 ? first : RunRoute(ctx, 0);
            Outcome b = RunRoute(ctx, 1);
            ++ctx.cases;
            if (mode >= 0) ++withThrows;
            if (!Same(a, b)) {
                size_t at = 0;
                while (at < a.log.size() && at < b.log.size() && a.log[at] == b.log[at]) ++at;
                ctx.Fail("DoMessage (scenario %d, type %x, throw at %d): escaped %lx/%lx, result %d/%d, logs %zu/%zu words, first difference at word %zu (%x/%x), refs %s",
                         scenario, gS.type, mode, a.escaped, b.escaped, a.result, b.result, a.log.size(), b.log.size(), at, at < a.log.size() ? a.log[at] : 0,
                         at < b.log.size() ? b.log[at] : 0, std::memcmp(a.refs, b.refs, sizeof(a.refs)) == 0 ? "same" : "differ");
                break;
            }
        }
    }
    gThrowAt = -1;
    *channel = savedChannel;
    if (withThrows == 0) ctx.Fail("DoMessage: no exception scenario ran");
    if (!lines || !rebinds || !handed || !resets || !stops || !starts)
        ctx.Fail("DoMessage coverage: %u log lines, %u 0x20f805 calls, %u hand-overs, %u 0x20f60e calls, %u shutdowns, %u inits", lines, rebinds, handed, resets, stops, starts);
}
