// Differential tests for the realtime debug display and its entries (lane LA): cGZRealtimeDebugDisplay, gz::cGZRTDEntry
// and gz::cGZRTDValueEntry (src/gz/cGZRealtimeDebugDisplay.h), in the standalone shape.
//
// RealtimeDebugDisplay_Run: a display built by the original constructor (0x9704bb, on 0xc5 memory) against one built by
// the constructor hook's adapter (every fourth seed: by the original COM class factory 0x92f5ed against Create, the
// result the +0x1c face), each driven through its own vtables by the same random sequence of operations, then
// destroyed by the last Release (the +4 face's slot 3: the thunk 0x970577 to the deleting destructor 0x9705bd; the
// destructor 0x97054b, which calls Shutdown). Operations: Init (0x9708f7), Shutdown (0x970c63), QueryInterface (0x9709a3;
// thunks 0x9709cd (+4), 0x9709d5 (+0x1c)) / AddRef / Release on every face, the base service's accessors, Show (0x970c12),
// Hide (0x9708a7), IsVisible (folded 0x1458b0), AddEntry0 (0x9706fc), RemoveEntry0 (0x970aae), AddTextEntry1 (0x970669),
// AddEntry1 (0x9705dc), RemoveEntry1 (0x9709fc), AddTextEntry0 (0x970818), AddEntry2 (0x97078b), RemoveEntry2 (0x970b60).
// Through them the entries' constructors (0x970399, 0x9703bd, 0x9703fc), AddRef / Release (0x97077e, 0x9709dd), the
// destructors (0x9704f9, 0x97050e) and deleting destructors (0x97057f, 0x97059e). The test also holds references on
// chosen entries and on every text copy, so what the display's Release / the entries' destructors do to them is seen
// after they leave the list (and an entry held by the test outlives the display; the test's last Release deletes it).
//
// RTDEntry_Run: one entry built by each original constructor against ours, its count driven over the byte's wrap (AddRef
// up to 256 times, Release until the deleting Release), the text copies held by the test to see the destructor release
// them (only the first mTextCount of them).
//
// Fakes (tests/ts_services_fixture.h): the service object (GetServiceObject's cell 0x10c9378) answers GetSystemService
// (slot 5) with the message server fake, whose MessageSend (slot 3) and MessagePost (slot 4) log the message's contents;
// the texts are fakes whose ToChar (slot 5) gives a chosen text. Compared after every operation: the result (the low byte
// where the original returns a byte in al), every byte of the display except the list's head pointer (a heap address: the
// list is compared by walking it), every entry (kind, count, keys, value, texts pointer, the copies' contents and counts),
// the held entries and copies, the event log, every fake's count, and the stack balance of every slot call.
//
// Left out, with the reason:
// - an entry's bytes +6 / +7: no code writes them (the constructor 0x970399 stores the word at +4 and the dword at +8:
//   `mov word ptr [esi + 4], 0`, `mov dword ptr [esi + 8], eax`), so they hold whatever the heap block held;
// - on a display from the factory, the three bytes after the running byte (+0x18) and after the flag (+0x20): padding
//   no code writes (the 0xc5 fill shows it on the constructor path), holding whatever the heap block held;
// - the strings' buffer bytes past the text and the list's / vectors' heap addresses (compared as sizes and contents).
// Not driven: a missing framework or message server, and a failed allocation in the add slots or the copies (the
// original then calls through a null pointer: ORIGINAL-BUG), and entry kinds other than 0-2 (no code makes one).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZRealtimeDebugDisplay.h"
#include "rz/cRZString.h"

#include <cstring>
#include <list>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace rtd_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cGZRealtimeDebugDisplay;
using Entry = gz::cGZRTDEntry;
using ValueEntry = gz::cGZRTDValueEntry;

constexpr uint32_t kTag = 0x7e000000;      // the display under test
constexpr uint32_t kTextsTag = 0x7e100000; // the test's text pointer array
constexpr uint32_t kServerId = 0x64063387, kServerIid = 0xc1085722;
constexpr uint32_t kEntryVTable = 0xe5b0d4, kValueEntryVTable = 0xe5b0e4;

enum : unsigned {
    kSvcObj = 0,
    kServer,
    kText0,
    kTextFakes = 5,
};

std::mt19937 gRng;
uint32_t Bool() { return 0x100u | (gRng() % 2); } // a bool in al with a busy upper byte
bool gOurs;
bool gFromFactory; // the display came from a factory (a heap block, not the 0xc5 fill)

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
void Call(Obj *self, unsigned slot)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(slot);
}

// ---- calls with the stack balance checked ----

// Every slot is called with its arguments pushed by hand and the stack pointer read back: a body that pops a different
// number of bytes than the caller pushed (a wrong declaration) sets gStackSkew, which each step logs.
int32_t gStackSkew;
uint32_t CallChecked(void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class... A> uint32_t Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A)));
}

// ---- fakes ----

// The message's contents (the server gets a stack address: Norm makes it the stack marker on both sides).
void LogMessage(uint32_t at)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(at));
    for (int i = 0; i < 4; ++i) Ev(words[i]);
}
uint32_t __fastcall ServerSend(Obj *self, void *, uint32_t message)
{
    Call(self, 3);
    Ev(svcfx::Norm(message));
    LogMessage(message);
    return Bool();
}
uint32_t __fastcall ServerPost(Obj *self, void *, uint32_t message, uint32_t urgent)
{
    Call(self, 4);
    Ev(svcfx::Norm(message));
    LogMessage(message);
    Ev(urgent);
    return Bool();
}
uint32_t __fastcall FrameworkGetService(Obj *self, void *, uint32_t id, uint32_t iid, uint32_t out)
{
    Call(self, 5);
    Ev(id);
    Ev(iid);
    Ev(svcfx::Norm(out));
    // Always answered: the original uses the result unchecked (ORIGINAL-BUG, not driven).
    ++F(kServer).refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &F(kServer);
    return Bool();
}
template <class Fn> void Shim(Obj &obj, unsigned slot, unsigned args, Fn fn)
{
    obj.Set(slot, args);
    obj.table[slot] = reinterpret_cast<void *>(fn);
}

const char *const kTexts[kTextFakes] = {"", "a", "fifteen chars!!", "sixteen chars!!!", "a text longer than sixteen characters"};
cIGZString *gTexts[8];

void BuildFakes()
{
    svcfx::ResetAll();
    Shim(F(kSvcObj), 5, 3, &FrameworkGetService);
    Shim(F(kServer), 3, 1, &ServerSend);
    Shim(F(kServer), 4, 2, &ServerPost);
    for (unsigned i = 0; i < kTextFakes; ++i) F(kText0 + i).Set(5, 0, kTexts[i]);
    for (unsigned i = 0; i < 8; ++i) gTexts[i] = reinterpret_cast<cIGZString *>(&F(kText0 + (i * 3) % kTextFakes));
    svcfx::AddRange(gTexts, sizeof(gTexts), kTextsTag);
    *GameAddress<void *>(0x10c9378) = &F(kSvcObj);
}

// ---- dumps ----

uint32_t gOurEntryVt, gOurValueVt;
void FindOurVTables()
{
    alignas(8) static uint8_t buffer[sizeof(ValueEntry)];
    auto *entry = ::new (buffer) Entry(0);
    gOurEntryVt = Word(buffer, 0);
    entry->~Entry();
    auto *value = ::new (buffer) ValueEntry(0, 0);
    gOurValueVt = Word(buffer, 0);
    value->~ValueEntry();
}

// 1: the entry's vtable, 2: the value entry's (the game's on the original side, ours on ours), else the word.
uint32_t EntryKind(const void *entry)
{
    const uint32_t vt = Word(entry, 0);
    if (vt == (gOurs ? gOurEntryVt : P(GameAddress<void>(kEntryVTable)))) return 1;
    if (vt == (gOurs ? gOurValueVt : P(GameAddress<void>(kValueEntryVTable)))) return 2;
    return vt;
}

void DumpString(Blob &b, const cRZString *s)
{
    b.push_back(P(s->mVTable) == P(GameAddress<void>(cRZString::kVTable)));
    b.push_back(uint32_t(s->mString.size()));
    b.push_back(uint32_t(s->mString.capacity()));
    for (char c : s->mString) b.push_back(uint8_t(c));
    b.push_back(s->mRefCount);
}

void DumpEntry(Blob &b, const void *entry)
{
    const auto *m = static_cast<const uint8_t *>(entry);
    const uint32_t kind = EntryKind(entry);
    b.push_back(kind);
    b.push_back(m[4]);
    b.push_back(m[5]); // +6 / +7 never written (file comment)
    b.push_back(Word(m, 8));
    DumpString(b, reinterpret_cast<const cRZString *>(m + 0xc));
    if (kind != 2) return;
    for (uint32_t off = 0x2c; off < 0x48; off += 4) b.push_back(off == 0x34 ? svcfx::Norm(Word(m, off)) : Word(m, off));
    const auto *v = static_cast<const ValueEntry *>(static_cast<const Entry *>(entry));
    b.push_back(uint32_t(v->mCopies.size()));
    b.push_back(uint32_t(v->mCopies.capacity()));
    for (const cRZString *copy : v->mCopies) DumpString(b, copy);
}

// Entries and text copies the test holds a reference on.
std::vector<void *> gHeldEntries;
std::vector<cRZString *> gHeldCopies;

void DumpHeld(Blob &b)
{
    b.push_back(uint32_t(gHeldEntries.size()));
    for (void *entry : gHeldEntries) DumpEntry(b, entry);
    b.push_back(uint32_t(gHeldCopies.size()));
    for (cRZString *copy : gHeldCopies) b.push_back(copy->mRefCount);
}

std::list<Entry *> &EntriesOf(void *object) { return *reinterpret_cast<std::list<Entry *> *>(Face(object, 0x28)); }

void DumpDisplay(Blob &b, void *object, const uint32_t (&ours)[3])
{
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kDisplayVTable}};
    for (unsigned f = 0; f < 3; ++f) {
        const uint32_t vt = Word(object, kFaces[f][0]);
        b.push_back(vt == (gOurs ? ours[f] : P(GameAddress<void>(kFaces[f][1]))) ? 0xd0000000u | f : vt);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(object, off));
    // The base's running byte and the three bytes after it (as at +0x20).
    b.push_back(gFromFactory ? Word(object, 0x18) & 0xff : Word(object, 0x18));
    // The flag and the three bytes after it (the 0xc5 fill: never written; from a factory, whatever the heap block held).
    b.push_back(gFromFactory ? Word(object, 0x20) & 0xff : Word(object, 0x20));
    b.push_back(svcfx::Norm(Word(object, 0x24)));
    b.push_back(Word(object, 0x2c)); // the list's size (+0x28, its head, is a heap address)
    b.push_back(Word(object, 0x30));
    for (Entry *entry : EntriesOf(object)) DumpEntry(b, entry);
    DumpHeld(b);
}

// ---- operations ----

const uint32_t kKeys[] = {0, 1, 2, 0x80000000, 0xffffffff};
const uint32_t kValues[] = {0, 1, 0x7fffffff, 0xffffffff};
const uint32_t kCounts[] = {0, 1, 2, 3, 5};
const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697, 0x0dc7057a, 0x12345678};

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

// The copies an entry just added holds, each counted by the test.
void HoldCopies(void *object)
{
    if (EntriesOf(object).empty()) return;
    Entry *last = EntriesOf(object).back();
    if (EntryKind(last) != 2) return;
    for (cRZString *copy : static_cast<ValueEntry *>(last)->mCopies) {
        Slot(copy, 1);
        gHeldCopies.push_back(copy);
    }
}

void DisplayOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const face = Face(object, 0x1c);
    const uint32_t a = op.arg, c = op.arg2;
    const uint32_t key = kKeys[a % 5], value = kValues[(a >> 4) % 4];
    switch (op.kind) {
    case 0: b.push_back(Slot(primary, 8) & 0xff); break; // Init
    case 1: b.push_back(Slot(primary, 9) & 0xff); break; // Shutdown
    case 2: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, Face(object, 4), face};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot(faces[a % 3], 0, kIids[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot(faces[(a >> 8) % 3], 1));
        b.push_back(Slot(faces[(a >> 10) % 3], 2));
        if (ok) b.push_back(Slot(q, 2));
        break;
    }
    case 3: // the base service's accessors, IsVisible
        b.push_back(Slot(primary, 3));
        b.push_back(Slot(primary, 5));
        b.push_back(Slot(primary, 6) & 0xff);
        b.push_back(Slot(primary, 12));
        Slot(primary, 7, a % 2);
        Slot(primary, 4, kValues[c % 4]);
        b.push_back(Slot(primary, 3));
        b.push_back(Slot(primary, 10, c) & 0xff);
        b.push_back(Slot(primary, 11, c) & 0xff);
        b.push_back(Slot(Face(object, 4), 5));
        b.push_back(Slot(face, 5) & 0xff);
        break;
    case 4: b.push_back(Slot(face, 3) & 0xff); break; // Show
    case 5: b.push_back(Slot(face, 4) & 0xff); break; // Hide
    case 6: b.push_back(Slot(face, 6, key) & 0xff); break;    // AddEntry0
    case 7: b.push_back(Slot(face, 7, key) & 0xff); break;    // RemoveEntry0
    case 8: case 11: { // AddTextEntry1 / AddTextEntry0
        const uint32_t first = c % 3, count = kCounts[(c >> 4) % 5];
        b.push_back(Slot(face, op.kind, key, value, &gTexts[first], count) & 0xff);
        HoldCopies(object);
        break;
    }
    case 9: b.push_back(Slot(face, 9, key, value) & 0xff); break;  // AddEntry1
    case 10: b.push_back(Slot(face, 10, key) & 0xff); break;       // RemoveEntry1
    case 12: b.push_back(Slot(face, 12, key, value) & 0xff); break; // AddEntry2
    case 13: b.push_back(Slot(face, 13, key) & 0xff); break;       // RemoveEntry2
    case 14: { // hold a listed entry (AddRef through its vtable)
        std::list<Entry *> &entries = EntriesOf(object);
        if (entries.empty() || gHeldEntries.size() >= 4) break;
        auto it = entries.begin();
        std::advance(it, c % entries.size());
        b.push_back(Slot(*it, 0));
        gHeldEntries.push_back(*it);
        break;
    }
    default: { // a held entry's count up and down again (staying at or below 255)
        if (gHeldEntries.empty()) break;
        void *entry = gHeldEntries[c % gHeldEntries.size()];
        const uint32_t room = 255u - static_cast<uint8_t *>(entry)[5];
        const uint32_t n = (a >> 8) % 2 ? room : room % 3;
        for (uint32_t i = 0; i < n; ++i) b.push_back(Slot(entry, 0));
        for (uint32_t i = 0; i < n; ++i) b.push_back(Slot(entry, 1));
        break;
    }
    }
}

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 3);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 28;
    if (rng() % 4) ops.push_back({0, uint32_t(rng()), uint32_t(rng())}); // usually Init first
    for (unsigned i = 0; i < n; ++i) {
        // the add and remove slots more often than the rest
        const unsigned r = rng() % 24;
        const unsigned kind = r < 16 ? r : 6 + r % 8;
        ops.push_back({kind, uint32_t(rng()), uint32_t(rng())});
    }
    return ops;
}

// ---- stand-ins: the game's new / delete over ours, so both sides' blocks meet ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void Finish(Blob &b)
{
    b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
    gStackSkew = 0;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
}

struct Scope {
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    Scope()
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        globals.SetWord(0x10c9378, 0);
        FindOurVTables();
    }
    ~Scope() { svcfx::gRanges.clear(); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

bool Compare(difftest::Context &ctx, const char *name, uint32_t seed, std::vector<Blob> (&results)[2])
{
    for (size_t i = 0; i < results[0].size() || i < results[1].size(); ++i) {
        const Blob empty;
        const Blob &x = i < results[0].size() ? results[0][i] : empty;
        const Blob &y = i < results[1].size() ? results[1][i] : empty;
        if (!svcfx::Same(ctx, x, y, name, seed)) {
            if (svcfx::gTrace)
                for (size_t w = 0; w < x.size() || w < y.size(); ++w)
                    fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < x.size() ? x[w] : 0, w < y.size() ? y[w] : 0);
            ctx.Fail("  at step %u", unsigned(i));
            return false;
        }
    }
    return true;
}

// The test's references dropped: the held entries (the last reference deletes them), then the copies.
void ReleaseHeld(Blob &b)
{
    for (void *entry : gHeldEntries) b.push_back(Slot(entry, 1));
    gHeldEntries.clear();
    b.push_back(uint32_t(gHeldCopies.size()));
    for (cRZString *copy : gHeldCopies) b.push_back(copy->mRefCount);
    for (cRZString *copy : gHeldCopies) Slot(copy, 2);
    gHeldCopies.clear();
}

} // namespace rtd_test

using namespace rtd_test;

DIFF_TEST(RealtimeDebugDisplay_Run, 0x9704bb)
{
    // Also reached (see the file comment): 0x92f5ed (the factory), 0x97054b (destructor), 0x9705bd (deleting destructor), 0x970577 (its +4 thunk),
    // 0x9709a3 (QueryInterface), 0x9709cd / 0x9709d5 (its thunks), 0x9708f7 (Init), 0x970c63 (Shutdown), 0x970c12, 0x9708a7,
    // 0x9706fc, 0x970aae, 0x970669, 0x9705dc, 0x9709fc, 0x970818, 0x97078b, 0x970b60; the entries' 0x970399, 0x9703bd,
    // 0x9703fc, 0x97077e, 0x9709dd, 0x9704f9, 0x97050e, 0x97057f, 0x97059e.
    Scope scope;
    uint32_t ourVt[3] = {};
    {
        alignas(8) static uint8_t buffer[Sys::kSize];
        Sys *probe = cGZRealtimeDebugDisplayAdapters::Construct(buffer, nullptr);
        ourVt[0] = Word(buffer, 0);
        ourVt[1] = Word(buffer, 4);
        ourVt[2] = Word(buffer, 0x1c);
        probe->~Sys();
    }
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes();
            Blob first;
            void *memory;
            gFromFactory = seed % 4 == 0;
            if (gFromFactory) {
                // The COM class factory: a new display as its +0x1c face (the original's block from the game's new,
                // patched to ours).
                void *face = gOurs ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x92f5ed)();
                first.push_back(face != nullptr);
                if (!face) break;
                memory = static_cast<uint8_t *>(face) - 0x1c;
                first.push_back(Word(memory, 0x1c) == (gOurs ? ourVt[2] : P(GameAddress<void>(Sys::kDisplayVTable))));
            } else {
                memory = ::operator new(Sys::kSize);
                std::memset(memory, 0xc5, Sys::kSize);
                void *r = gOurs ? static_cast<void *>(cGZRealtimeDebugDisplayAdapters::Construct(memory, nullptr))
                                : ctx.Original<void *(__thiscall *)(void *)>(0x9704bb)(memory);
                first.push_back(r == memory);
            }
            svcfx::AddRange(memory, Sys::kSize, kTag);
            DumpDisplay(first, memory, ourVt);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot(memory, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    DisplayOp(b, memory, ops[i]);
                    DumpDisplay(b, memory, ourVt);
                } else {
                    b.push_back(Slot(memory, 2)); // the last release destroys the display
                    DumpHeld(b);
                    ReleaseHeld(b);
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        if (!Compare(ctx, "RealtimeDebugDisplay_Run", seed, results)) break;
    }
}

DIFF_TEST(RTDEntry_Run, 0x9703fc)
{
    // Also reached: 0x970399, 0x9703bd (the other constructors), 0x97077e (AddRef), 0x9709dd (Release), 0x9704f9 /
    // 0x97050e (destructors), 0x97057f / 0x97059e (deleting destructors).
    Scope scope;
    static const uint32_t kAddRefs[] = {0, 1, 2, 253, 254, 255, 256};
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::mt19937 rng(seed * 97 + 1);
        const unsigned ctor = rng() % 3;
        const uint32_t key = kKeys[rng() % 5], value = kValues[rng() % 4];
        const uint32_t first = rng() % 3, count = kCounts[rng() % 5];
        std::vector<uint32_t> steps;
        for (unsigned i = 0; i < 1 + rng() % 6; ++i) steps.push_back(uint32_t(rng()));
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes();
            Blob b;
            const uint32_t size = ctor == 0 ? Entry::kSize : ValueEntry::kSize;
            void *memory = ::operator new(size);
            std::memset(memory, 0xc5, size);
            void *r;
            if (gOurs) {
                if (ctor == 0) r = ::new (memory) Entry(key);
                else if (ctor == 1) r = static_cast<Entry *>(::new (memory) ValueEntry(key, value));
                else r = static_cast<Entry *>(::new (memory) ValueEntry(key, value, &gTexts[first], count));
            } else if (ctor == 0) {
                r = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x970399)(memory, key);
            } else if (ctor == 1) {
                r = ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t)>(0x9703bd)(memory, key, value);
            } else {
                r = ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t, cIGZString *const *, uint32_t)>(0x9703fc)(
                    memory, key, value, &gTexts[first], count);
            }
            b.push_back(r == memory);
            DumpEntry(b, memory);
            if (ctor == 2)
                for (cRZString *copy : static_cast<ValueEntry *>(static_cast<Entry *>(memory))->mCopies) {
                    Slot(copy, 1);
                    gHeldCopies.push_back(copy);
                }
            bool alive = true;
            for (uint32_t step : steps) {
                if (!alive) break;
                const uint32_t n = kAddRefs[step % 7];
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t got = Slot(memory, 0);
                    if (i + 3 >= n) b.push_back(got);
                }
                const uint32_t releases = 1 + (step >> 8) % 3;
                for (uint32_t i = 0; i < releases && alive; ++i) {
                    alive = static_cast<uint8_t *>(memory)[5] > 1; // this Release deletes otherwise
                    b.push_back(Slot(memory, 1));
                }
                if (alive) DumpEntry(b, memory);
            }
            if (alive) {
                // down to the deleting Release
                while (static_cast<uint8_t *>(memory)[5] > 1) b.push_back(Slot(memory, 1));
                b.push_back(Slot(memory, 1));
            }
            b.push_back(uint32_t(gHeldCopies.size()));
            for (cRZString *copy : gHeldCopies) b.push_back(copy->mRefCount);
            for (cRZString *copy : gHeldCopies) Slot(copy, 2);
            gHeldCopies.clear();
            Finish(b);
            results[side].push_back(std::move(b));
            svcfx::gRanges.clear();
        }
        if (!Compare(ctx, "RTDEntry_Run", seed, results)) break;
    }
}
