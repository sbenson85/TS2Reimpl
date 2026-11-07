// Differential tests for nGZCommandParser::cArguments (src/gz/cArguments.h, standalone shape).
//
// A script, generated from a seed, runs on three routes and their transcripts are compared line by line:
// - original: objects built by the image's constructor 0xb0be60 / copy constructor 0xb0bdd0, assigned by 0xb0c000,
//   destroyed by 0xb0bf40 (or slot 16, 0xb0c320, flags 0), and every slot called at its RVA:
//   0 0xb0c720 Parse, 1 0xb0cc70 TakeSwitch, 2 0xb0cbb0 TakeSwitchArguments, 3 0xb0cb90 (exactly), 4 0xb0c2a0 Argument,
//   5 0xb0c410 GetAll, 6 0xb0c3f0 ArgumentLength, 7 0xb0c6d0 Count, 8 0x149cf0 PositionalCount (folded), 9 0xb0c6e0
//   SwitchCount, 10 0xb0c650 Positional, 11 0xb0c630 (exactly), 12 0xb0c550 HasSwitch, 13 0xb0c6f0 UnusedSwitchCount,
//   14 0xb0cce0 GetUnusedSwitches, 15 0xb0c430 CheckSwitchesUsed, 16 0xb0c320 the deleting destructor;
// - vtable: our objects (the constructor hooks' adapters), every slot called through the object's own vtable;
// - adapter: objects built by the image's constructor (the game's vtable), every slot called through its RVA hook's
//   adapter, with a trap in the object's (copied) vtable at that slot, so an adapter that dispatched virtually fails.
// The copy constructor, assignment and destructor adapters run on the vtable and adapter routes.
//
// The RZ allocator (RZMemory::Malloc / Free, 0x1be3b0 / 0x1be3a1) is stood in on both sides by the process heap with a
// log of each allocation's size and each free by allocation number; new blocks are filled with 0xcd. Objects live in
// buffers that start with the same random bytes on every route (the copy constructor leaves +0x15 as it was, and the
// constructors leave +0x16 / +0x17). Each transcript line: the call, its result (pointers as offsets into the object's
// buffer or argument vector), what it threw (the cError's message and type list), and the object's whole state (vtable
// word as a code, every field, the buffer's bytes up to its size, the vectors' sizes and capacities).
#define NOMINMAX
#include "code_patch.h"
#include "command_parser.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "gz/cArguments.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using nGZCommandParser::cArguments;
using A = nGZCommandParser::cArgumentsAdapters;

constexpr uint32_t kVTable = 0xe7f84c;
constexpr unsigned kSlots = 17;
constexpr uint32_t kCtor = 0xb0be60, kCopy = 0xb0bdd0, kAssign = 0xb0c000, kDtor = 0xb0bf40;
constexpr uint32_t kSlotRva[kSlots] = {0xb0c720, 0xb0cc70, 0xb0cbb0, 0xb0cb90, 0xb0c2a0, 0xb0c410, 0xb0c3f0, 0xb0c6d0, 0x149cf0,
                                       0xb0c6e0, 0xb0c650, 0xb0c630, 0xb0c550, 0xb0c6f0, 0xb0cce0, 0xb0c430, 0xb0c320};
constexpr unsigned kWords[kSlots] = {3, 1, 4, 2, 1, 2, 1, 0, 0, 0, 3, 1, 1, 0, 1, 0, 1};

void *Adapter(unsigned slot)
{
    switch (slot) {
    case 0: return reinterpret_cast<void *>(&A::Parse);
    case 1: return reinterpret_cast<void *>(&A::TakeSwitch);
    case 2: return reinterpret_cast<void *>(&A::TakeSwitchArguments);
    case 3: return reinterpret_cast<void *>(&A::TakeSwitchArgumentsExactly);
    case 4: return reinterpret_cast<void *>(&A::Argument);
    case 5: return reinterpret_cast<void *>(&A::GetAll);
    case 6: return reinterpret_cast<void *>(&A::ArgumentLength);
    case 7: return reinterpret_cast<void *>(&A::Count);
    case 9: return reinterpret_cast<void *>(&A::SwitchCount);
    case 10: return reinterpret_cast<void *>(&A::Positional);
    case 11: return reinterpret_cast<void *>(&A::PositionalExactly);
    case 12: return reinterpret_cast<void *>(&A::HasSwitch);
    case 13: return reinterpret_cast<void *>(&A::UnusedSwitchCount);
    case 14: return reinterpret_cast<void *>(&A::GetUnusedSwitches);
    case 15: return reinterpret_cast<void *>(&A::CheckSwitchesUsed);
    case 16: return reinterpret_cast<void *>(&A::DeletingDestructor);
    default: return nullptr; // 8 (folded, not ours to hook): the image's
    }
}

enum class Route { Original, VTable, Adapter };

// The game's operator new (0x824632) and sized delete (0x824624) to ours: the std::vector slot 14 fills and the strings
// slot 15 builds are freed by the other side's code.
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- the RZ allocator stand-in ----

std::string *gTranscript = nullptr;
std::unordered_map<uintptr_t, int> gSerial;
int gNextSerial = 0;
int gTrapHits = 0;

void Note(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (gTranscript) *gTranscript += buf;
}

void *__cdecl TestMalloc(uint32_t bytes)
{
    void *p = HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
    std::memset(p, 0xcd, bytes);
    int serial = ++gNextSerial;
    gSerial[reinterpret_cast<uintptr_t>(p)] = serial;
    Note("[M%u#%d]", bytes, serial);
    return p;
}

void __cdecl TestFree(void *p)
{
    if (!p) {
        Note("[F0]");
        return;
    }
    auto it = gSerial.find(reinterpret_cast<uintptr_t>(p));
    if (it == gSerial.end()) {
        Note("[F?]"); // not one of ours: left alone
        return;
    }
    Note("[F#%d]", it->second);
    gSerial.erase(it);
    HeapFree(GetProcessHeap(), 0, p);
}

// ---- describing state ----

void **gOurVTable = nullptr;

void **OurVTable()
{
    if (!gOurVTable) {
        alignas(8) static uint8_t scratch[sizeof(cArguments)];
        cArguments *object = A::Construct(scratch, nullptr);
        gOurVTable = *reinterpret_cast<void ***>(scratch);
        object->cArguments::~cArguments();
    }
    return gOurVTable;
}

void *gTrapCopy[4][kSlots]; // per object: the copy the adapter route installs while a slot runs

std::string VtCode(uint32_t word)
{
    if (word == shape::P(GameAddress<void>(kVTable)) || word == shape::P(OurVTable())) return "VT";
    for (auto &copy : gTrapCopy)
        if (word == shape::P(copy)) return "VT";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "raw%08x", word);
    return buf;
}

// A pointer into [first, first + span] as an offset, or raw.
std::string Off(uint32_t p, uint32_t first, uint32_t span, const char *base)
{
    char buf[32];
    if (p == 0) return "null";
    if (first != 0 && p >= first && p <= first + span) std::snprintf(buf, sizeof(buf), "%s+%u", base, p - first);
    else std::snprintf(buf, sizeof(buf), "?%08x", p);
    return buf;
}

std::string Dump(const uint8_t *o)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(o);
    std::string out = " {" + VtCode(w[0]);
    char buf[64];
    uint32_t bufFirst = w[1], bufLast = w[2], bufEnd = w[3];
    uint32_t bufCap = bufFirst ? bufEnd - bufFirst : 0;
    if (bufFirst == 0) {
        std::snprintf(buf, sizeof(buf), " buf null %x %x", bufLast, bufEnd);
        out += buf;
    } else {
        std::snprintf(buf, sizeof(buf), " buf %u/%u '", bufLast - bufFirst, bufCap);
        out += buf;
        for (uint32_t p = bufFirst; p < bufLast; ++p) {
            unsigned char c = *reinterpret_cast<const unsigned char *>(p);
            if (c >= 0x20 && c < 0x7f && c != '\\') out += static_cast<char>(c);
            else {
                std::snprintf(buf, sizeof(buf), "\\%02x", c);
                out += buf;
            }
        }
        out += "'";
    }
    std::snprintf(buf, sizeof(buf), " pos %u flags %02x %02x %02x %02x", w[4], o[0x14], o[0x15], o[0x16], o[0x17]);
    out += buf;
    // The argument vector.
    if (w[6] == 0) {
        std::snprintf(buf, sizeof(buf), " args null %x %x", w[7], w[8]);
        out += buf;
    } else {
        std::snprintf(buf, sizeof(buf), " args %d/%d [", static_cast<int>(w[7] - w[6]) / 4, static_cast<int>(w[8] - w[6]) / 4);
        out += buf;
        for (uint32_t p = w[6]; p < w[7]; p += 4) out += Off(*reinterpret_cast<const uint32_t *>(p), bufFirst, bufCap, "b") + " ";
        out += "]";
    }
    // The switches.
    if (w[9] == 0) {
        std::snprintf(buf, sizeof(buf), " sw null %x %x", w[10], w[11]);
        out += buf;
    } else {
        std::snprintf(buf, sizeof(buf), " sw %d/%d [", static_cast<int>(w[10] - w[9]) / 16, static_cast<int>(w[11] - w[9]) / 16);
        out += buf;
        for (uint32_t p = w[9]; p < w[10]; p += 16) {
            const uint8_t *s = reinterpret_cast<const uint8_t *>(p);
            const uint32_t *sw = reinterpret_cast<const uint32_t *>(p);
            out += Off(sw[0], bufFirst, bufCap, "b");
            std::snprintf(buf, sizeof(buf), ",%u,%u,%02x%02x%02x%02x ", sw[1], sw[2], s[12], s[13], s[14], s[15]);
            out += buf;
        }
        out += "]";
    }
    return out + "}";
}

// ---- the script ----

const char *const kNames[] = {"v", "V", "on", "On", "x", "name", "NAME", "zz"};
const char *const kWordsPool[] = {"set", "abc", "Foo", "x1", "a-b", "\xe9t\xe9", "q", "-1", "-9z"};

std::string MakeText(std::mt19937 &g, bool errors)
{
    std::string t;
    auto blanks = [&](unsigned most) {
        static const char kBlank[] = {' ', '\t', '\r', '\n'};
        unsigned n = g() % (most + 1);
        for (unsigned i = 0; i < n; ++i) t += kBlank[g() % 4];
    };
    auto word = [&] { return std::string(kWordsPool[g() % (sizeof(kWordsPool) / sizeof(kWordsPool[0]))]); };
    blanks(2);
    // One text in eight is long and mostly switches (33 to 250 of them, many equal but for case): past 32 the sort
    // of the switches is a merge sort (stable), not an insertion sort, so an unstable sort would show. Under 256
    // switches the merge buffer is on the stack (the original's would allocate past that).
    bool longText = g() % 8 == 0;
    unsigned tokens = longText ? 40 + g() % 260 : g() % 9;
    for (unsigned i = 0; i < tokens; ++i) {
        unsigned r = g() % 100;
        if (longText && r < 80) r = 22 + r % 25;
        if (r < 22) t += word();
        else if (r < 47) t += std::string("-") + kNames[g() % 7];
        else if (r < 55) t += "\"" + (g() % 3 ? word() + " " + word() : std::string()) + "\"";
        else if (r < 62) t += "(" + word() + " (" + word() + ") \")\")";
        else if (r < 66) t += "(" + (g() % 2 ? word() : std::string()) + ")";
        else if (r < 70) t += word() + "\"q r\"";
        else if (r < 73) t += word() + "(p q)";
        else if (r < 76) t += "\"-v\"";
        else if (r < 79) t += "-v(";
        else if (errors && r < 82) t += "-";
        else if (errors && r < 85) t += "\"open";
        else if (errors && r < 88) t += "(open";
        else if (errors && r < 91) t += "close)";
        else t += word();
        if (i + 1 < tokens) {
            t += ' ';
            blanks(2);
        }
    }
    blanks(2);
    return t;
}

struct Step {
    unsigned object; // 0..2
    unsigned op;     // 0..16 slots; 20 copy-construct object 2 from `arg0`; 21 assign object from `arg0`
    std::string text;
    uint32_t arg[4];
};

std::vector<Step> MakeScript(uint32_t seed)
{
    std::mt19937 g(seed);
    std::vector<Step> script;
    auto parse = [&](unsigned object) {
        Step s{object, 0, MakeText(g, g() % 4 == 0), {}};
        static const uint32_t kFlags[] = {0, 1, 0, 1, 2, 0x101};
        s.arg[0] = kFlags[g() % 6];
        s.arg[1] = kFlags[g() % 6];
        script.push_back(s);
    };
    auto queries = [&](unsigned object, unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            static const unsigned kOps[] = {1, 1, 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 12, 13, 14, 15};
            Step s{object, kOps[g() % (sizeof(kOps) / sizeof(kOps[0]))], {}, {}};
            s.text = kNames[g() % 8];
            for (uint32_t &a : s.arg) a = g() % 4;
            if (s.op == 4 || s.op == 6) s.arg[0] = g() % 12;
            if (s.op == 2 || s.op == 10) s.arg[3] = g() % 2; // null count pointer?
            script.push_back(s);
        }
    };
    parse(0);
    queries(0, 8 + g() % 8);
    if (g() % 2) {
        parse(0);
        queries(0, 6);
    }
    parse(1);
    queries(1, 3);
    script.push_back(Step{1, 21, {}, {g() % 4 == 0 ? 1u : 0u}}); // object 1 = object 0 (or itself)
    queries(1, 6);
    script.push_back(Step{2, 20, {}, {0}}); // object 2 copied from object 0
    queries(2, 6);
    if (g() % 2) {
        parse(2);
        queries(2, 4);
    }
    return script;
}

alignas(8) uint8_t gObjects[3][0x30];

template <class... Args> uint32_t CallThis(void *fn, void *self, Args... args)
{
    return reinterpret_cast<uint32_t(__thiscall *)(void *, Args...)>(fn)(self, args...);
}

// Installs, for the adapter route, a copy of the object's own vtable with a trap at `slot` while the call runs.
struct TrapScope {
    void **object;
    void **saved;
    void **copy;
    TrapScope(Route route, unsigned index, unsigned slot) : object(nullptr), saved(nullptr), copy(gTrapCopy[index])
    {
        if (route != Route::Adapter) return;
        object = reinterpret_cast<void **>(gObjects[index]);
        saved = static_cast<void **>(*object);
        std::memcpy(copy, saved, sizeof(gTrapCopy[0]));
        copy[slot] = shape::TrapFor<shape::Count<gTrapHits>>(kWords[slot]);
        *object = copy;
    }
    ~TrapScope()
    {
        if (object && *object == copy) *object = saved;
    }
};

std::string Describe(const cptest::Outcome &o, unsigned slot, const uint8_t *object)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(object);
    char buf[64];
    std::string out;
    if (o.threw) return " threw " + o.detail;
    switch (slot) {
    case 1:
    case 12: std::snprintf(buf, sizeof(buf), " -> %u", o.ret & 0xff); break;
    case 2:
    case 3:
    case 10:
    case 11: out = " -> " + Off(o.ret, w[6], w[8] - w[6], "a"); return out;
    case 4: out = " -> " + Off(o.ret, w[1], w[3] - w[1], "b"); return out;
    case 6:
    case 7:
    case 8:
    case 9:
    case 13: std::snprintf(buf, sizeof(buf), " -> %d", static_cast<int>(o.ret)); break;
    case 16: return o.ret == shape::P(object) ? " -> self" : " -> other";
    default: return "";
    }
    return buf;
}

std::string RunScript(difftest::Context &ctx, Route route, uint32_t seed, const std::vector<Step> &script)
{
    std::string transcript;
    gTranscript = &transcript;
    gSerial.clear();
    gNextSerial = 0;
    std::mt19937 fill(seed ^ 0x5bd1e995u);
    for (auto &object : gObjects)
        for (uint8_t &b : object) b = static_cast<uint8_t>(fill());
    for (unsigned i = 0; i < 2; ++i) {
        if (route == Route::VTable) A::Construct(gObjects[i], nullptr);
        else ctx.Original<void(__thiscall *)(void *)>(kCtor)(gObjects[i]);
        transcript += "ctor" + Dump(gObjects[i]) + "\n";
    }
    bool live[3] = {true, true, false};
    char buf[96];
    for (const Step &s : script) {
        uint8_t *object = gObjects[s.object];
        std::snprintf(buf, sizeof(buf), "route %d seed %u step %u op %u", static_cast<int>(route), seed,
                      static_cast<unsigned>(&s - script.data()), s.op);
        ctx.Mark(buf);
        if (s.op == 20) {
            const uint8_t *from = gObjects[s.arg[0]];
            cptest::Outcome o = cptest::RunGuarded([&] {
                if (route == Route::Original) ctx.Original<void(__thiscall *)(void *, const void *)>(kCopy)(object, from);
                else A::ConstructCopy(object, nullptr, reinterpret_cast<const cArguments *>(from));
            });
            live[s.object] = true;
            transcript += "copy" + Describe(o, 0, object) + Dump(object) + "\n";
            continue;
        }
        if (s.op == 21) {
            const uint8_t *from = s.arg[0] ? object : gObjects[0];
            cptest::Outcome o = cptest::RunGuarded([&] {
                if (route == Route::Original)
                    return ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(kAssign)(object, from);
                return shape::P(A::Assign(reinterpret_cast<cArguments *>(object), nullptr, reinterpret_cast<const cArguments *>(from)));
            });
            transcript += std::string("assign") + (o.ret == shape::P(object) ? " -> self" : " -> other") + Dump(object) + "\n";
            continue;
        }
        const uint32_t *w = reinterpret_cast<const uint32_t *>(object);
        int32_t arguments = w[6] ? static_cast<int32_t>(w[7] - w[6]) / 4 : 0;
        // Indices the original reads without checking stay inside the vector.
        if (s.op == 4 && static_cast<int32_t>(s.arg[0]) >= arguments) continue;
        if (s.op == 6 && static_cast<int32_t>(s.arg[0]) + 1 >= arguments) continue;
        void *fn = route == Route::Original ? GameAddress<void>(kSlotRva[s.op])
                   : route == Route::VTable  ? (*reinterpret_cast<void ***>(object))[s.op]
                   : Adapter(s.op)           ? Adapter(s.op)
                                             : GameAddress<void>(kSlotRva[s.op]);
        uint32_t count = 0xdeadbeefu;
        int32_t all = 0x5a5a5a5a;
        uint32_t allVector = 0x5a5a5a5a;
        nGZCommandParser::NameVector unused;
        if (s.arg[0] % 2) unused.assign(3, nullptr);
        cptest::Outcome o;
        {
            TrapScope trap(route, s.object, s.op);
            o = cptest::RunGuarded([&]() -> uint32_t {
                switch (s.op) {
                case 0: return CallThis(fn, object, s.text.c_str(), s.arg[0], s.arg[1]);
                case 1:
                case 12: return CallThis(fn, object, s.text.c_str());
                case 2: return CallThis(fn, object, s.text.c_str(), s.arg[3] ? &count : nullptr, s.arg[0], s.arg[1]);
                case 3: return CallThis(fn, object, s.text.c_str(), s.arg[0]);
                case 4:
                case 6:
                case 11: return CallThis(fn, object, s.arg[0]);
                case 5: return CallThis(fn, object, &all, &allVector);
                case 10: return CallThis(fn, object, s.arg[3] ? &count : nullptr, s.arg[0], s.arg[1]);
                case 14: return CallThis(fn, object, &unused);
                default: return CallThis(fn, object);
                }
            });
        }
        std::snprintf(buf, sizeof(buf), "slot %u(%u,%u,%u) '", s.op, s.arg[0], s.arg[1], s.arg[2]);
        transcript += buf;
        transcript += s.op == 0 ? s.text : s.text.substr(0, 8);
        transcript += "'" + Describe(o, s.op, object);
        if (count != 0xdeadbeefu) {
            std::snprintf(buf, sizeof(buf), " count %u", count);
            transcript += buf;
        }
        if (s.op == 5 && !o.threw) {
            std::snprintf(buf, sizeof(buf), " all %d ", all);
            transcript += buf + Off(allVector, w[6], w[8] - w[6], "a");
        }
        if (s.op == 14 && !o.threw) {
            std::snprintf(buf, sizeof(buf), " unused %u/%u", static_cast<unsigned>(unused.size()), static_cast<unsigned>(unused.capacity()));
            transcript += buf;
            for (const char *name : unused) transcript += " " + Off(shape::P(name), w[1], w[3] - w[1], "b");
        }
        transcript += Dump(object) + "\n";
    }
    // Destruction: object 1 through slot 16 with flags 0, the others through the destructor.
    for (unsigned i = 0; i < 3; ++i) {
        if (!live[i]) continue;
        uint8_t *object = gObjects[i];
        uint32_t ret = 0;
        if (i == 1) {
            TrapScope trap(route, i, 16);
            void *fn = route == Route::Original ? GameAddress<void>(kSlotRva[16])
                       : route == Route::VTable  ? (*reinterpret_cast<void ***>(object))[16]
                                                 : Adapter(16);
            ret = CallThis(fn, object, 0u);
        } else if (route == Route::Original) {
            ctx.Original<void(__thiscall *)(void *)>(kDtor)(object);
        } else {
            A::Destruct(reinterpret_cast<cArguments *>(object), nullptr);
        }
        transcript += i == 1 ? (ret == shape::P(object) ? "delete0 -> self" : "delete0 -> other") : "dtor";
        transcript += Dump(object) + "\n";
    }
    std::snprintf(buf, sizeof(buf), "blocks left %u\n", static_cast<unsigned>(gSerial.size()));
    transcript += buf;
    for (auto &entry : gSerial) HeapFree(GetProcessHeap(), 0, reinterpret_cast<void *>(entry.first));
    gSerial.clear();
    gTranscript = nullptr;
    return transcript;
}

void CompareTranscripts(difftest::Context &ctx, uint32_t seed, const char *route, const std::string &a, const std::string &b)
{
    if (a == b) return;
    size_t start = 0;
    for (;;) {
        size_t ea = a.find('\n', start), eb = b.find('\n', start);
        std::string la = a.substr(start, ea == std::string::npos ? std::string::npos : ea - start);
        std::string lb = b.substr(start, eb == std::string::npos ? std::string::npos : eb - start);
        if (la != lb || ea == std::string::npos || eb == std::string::npos) {
            ctx.Fail("seed %u (%s): original: %.200s", seed, route, la.c_str());
            ctx.Fail("seed %u (%s): ours:     %.200s", seed, route, lb.c_str());
            return;
        }
        start = ea + 1;
    }
}

void RunRoutes(difftest::Context &ctx, Route ours, const char *name, uint32_t cases)
{
    svcfx::AllocatorScope services;
    shape::StandIn malloc_(0x1be3b0, &RZMemory::Malloc, &TestMalloc);
    shape::StandIn free_(0x1be3a1, &RZMemory::Free, &TestFree);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    OurVTable();
    for (uint32_t seed = 1; seed <= cases && ctx.failures.size() < 4; ++seed) {
        std::vector<Step> script = MakeScript(seed);
        std::string original = RunScript(ctx, Route::Original, seed, script);
        gTrapHits = 0;
        std::string replacement = RunScript(ctx, ours, seed, script);
        if (gTrapHits != 0) ctx.Fail("seed %u (%s): %d trap hits (an adapter dispatched virtually)", seed, name, gTrapHits);
        CompareTranscripts(ctx, seed, name, original, replacement);
        ++ctx.cases;
    }
}

} // namespace

DIFF_TEST(cArguments_VTable, 0xb0c720)
{
    RunRoutes(ctx, Route::VTable, "vtable", 600);
}

DIFF_TEST(cArguments_Adapters, 0xb0c720)
{
    RunRoutes(ctx, Route::Adapter, "adapter", 300);
}

// Slot 16 (0xb0c320) with flags 1 on a heap object: the object is freed (FreeRecorder), and what it held goes back to the
// RZ allocator first.
DIFF_TEST(cArguments_DeletingDestructorFrees, 0xb0c320)
{
    svcfx::AllocatorScope services;
    shape::StandIn malloc_(0x1be3b0, &RZMemory::Malloc, &TestMalloc);
    shape::StandIn free_(0x1be3a1, &RZMemory::Free, &TestFree);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    std::string logs[2];
    gSerial.clear();
    gNextSerial = 0;
    for (int side = 0; side < 2; ++side) {
        std::string &transcript = logs[side];
        gTranscript = &transcript;
        void *memory = ::operator new(sizeof(cArguments));
        void *fn;
        if (side == 0) {
            ctx.Original<void(__thiscall *)(void *)>(kCtor)(memory);
            ctx.Original<void(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(kSlotRva[0])(memory, "a -b c", 1, 0);
            fn = GameAddress<void>(kSlotRva[16]);
        } else {
            A::Construct(memory, nullptr);
            CallThis(OurVTable()[0], memory, "a -b c", 1u, 0u);
            fn = (*static_cast<void ***>(memory))[16];
        }
        difftest::FreeRecorder rec;
        uint32_t ret = CallThis(fn, memory, 1u);
        rec.Stop();
        if (!rec.Freed(memory)) ctx.Fail("side %d: the object was not freed", side);
        if (ret != shape::P(memory)) ctx.Fail("side %d: returned %08x, not the object", side, ret);
        gTranscript = nullptr;
        for (auto &entry : gSerial) HeapFree(GetProcessHeap(), 0, reinterpret_cast<void *>(entry.first));
        gSerial.clear();
        gNextSerial = 0;
        ++ctx.cases;
    }
    // The vectors' blocks (Parse's two reservations and the buffer) are freed in the destructor's order.
    if (logs[0] != logs[1] || logs[0].find("[F#") == std::string::npos)
        ctx.Fail("allocator logs: original %s, ours %s", logs[0].c_str(), logs[1].c_str());
}

// Slot 7 (0xb0c6d0) on raw words (moved from TopCallersA_All): the argument vector's end minus its begin, a signed shift,
// minus one, so misaligned and reversed ranges give what the original gives.
DIFF_TEST(cArguments_CountRawWords, 0xb0c6d0)
{
    using Fn = int32_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0xb0c6d0);
    std::mt19937 rng(15);
    for (int i = 0; i < 20000; ++i) {
        alignas(4) uint32_t raw[12];
        for (auto &word : raw) word = rng();
        uint32_t begin = raw[6];
        int kind = i % 4;
        if (kind == 0) raw[7] = begin + 4u * (rng() % 13);
        else if (kind == 1) raw[7] = begin + (rng() % 40) - 20;
        int32_t ra = orig(raw);
        int32_t rb = A::Count(reinterpret_cast<cArguments *>(raw), nullptr);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Count begin %08x end %08x: original %d, ours %d", raw[6], raw[7], ra, rb);
    }
}
