#define NOMINMAX
// Differential tests for nGZCommandParser::cFileParser (lane IM; src/gz/cFileParser.{h,cpp}), in the standalone shape.
//
// The original side is a file parser made by the game's factory 0xb0dc30 (its inline constructor, the game's vtables), every
// slot called through those vtables (the image's functions). Our side is our factory (cFileParser::Create, the hook's entry),
// every slot called through our own vtables, in the slot where the game's vtable holds the original. Vtable words are compared
// as codes; pointers to the test's fakes by name. Left out of the comparison: the padding after the two flags (+0x11..0x13,
// +0x25..0x27: the factory 0xb0dc30 stores bytes there, the rest is the heap's), the stopwatch's dword at +0x34 (its
// constructor 0x1c9030 never writes it) and each entry's padding (+0x41..0x43: AddFile 0xb0d5c0 copies the entry from its
// frame, where only the byte at ebp-0x60 is written).
//
// Fakes on both sides: the parser (a cIBaseParser face with a vbtable leading to its counted cIGZUnknown; RunFile may throw),
// its source (slot 7 names the file), the director's framework object (0x1ae18d and ours: GetClassObject hands out watchers
// or nothing), the watchers, the message server (the cache cell 0x10f519c, which the image's getter 0x9c5a89 and ours return),
// RZMemory's Malloc / Free (0x1be3b0 / 0x1be3a1 and ours) for the message, and the clocks (gzlog_support.h). Paths are made
// absolute for real (RealCwd: the CRT call sites of MakeAbsolutePath reach the host's CRT).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gzlog_support.h"
#include "shape.h"

#include "gz/CommandParser.h"
#include "gz/cFileParser.h"
#include "rz/RZMemory.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFileName.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "game/vtable.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <direct.h>
#include <windows.h>

namespace cp = nGZCommandParser;

namespace {

constexpr uint32_t kGameVTable = cp::cFileParser::kVTable;
constexpr uint32_t kGameTargetVTable = cp::cFileParser::kTargetVTable;
constexpr uint32_t kGameUnknownVTable = cp::cFileParser::kUnknownVTable;
constexpr uint32_t kGameRZUnknownVTable = 0xda2fd0; // what cRZUnknown's destructor 0x1b9096 leaves at +8

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// The game's operator new / delete as ours (both sides free through the same heap; FreeRecorder sees both).
struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

// As tests/gz_file_source.cpp (and tests/ini_file_paths.cpp): MakeAbsolutePath's CRT call sites (_getdrive 0x1c16c8,
// _getdcwd 0x1c16f2, library code) reach the host's CRT; relative paths resolve against the real working directory.
struct RealCwd {
    uint8_t *sites[2];
    uint8_t saved[2][5];
    static void Patch(uint8_t *site, void *target)
    {
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE8;
        std::memcpy(site + 1, &rel, 4);
    }
    explicit RealCwd(difftest::Context &ctx)
    {
        sites[0] = ctx.image.At<uint8_t>(0x1c16c8);
        sites[1] = ctx.image.At<uint8_t>(0x1c16f2);
        for (int i = 0; i < 2; ++i) std::memcpy(saved[i], sites[i], 5);
        Patch(sites[0], reinterpret_cast<void *>(&::_getdrive));
        Patch(sites[1], reinterpret_cast<void *>(&::_getdcwd));
    }
    ~RealCwd()
    {
        for (int i = 0; i < 2; ++i) std::memcpy(sites[i], saved[i], 5);
    }
};

// "X:rel" for a drive other than the current one: the CRT's _getdcwd for another drive crashes in this process
// (tests/ini_file_paths.cpp), so such paths are left out.
bool NeedsOtherDriveDirectory(const std::string &s)
{
    if (s.size() < 2 || s[1] != ':' || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return false;
    if (s.size() > 2 && (s[2] == '\\' || s[2] == '/')) return false;
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    return (s[0] & ~0x20) != (cwd[0] & ~0x20);
}

std::string RandomPath(std::mt19937 &g)
{
    static const char *const kHeads[] = {"", "C:", "C:\\", "c:/", "D:\\", "\\\\", "//", "\\", "/", "x", "ab:", "1:\\", "Z:/",
                                         ".", "..\\", "res:/", "ab1:/", "q:/", "Res:", "gz:/x/", "\xe9t\xe9:/"};
    static const char *const kParts[] = {"dir", "Res", "a b", "x.txt", "..", ".", "file.package", "Config", "%s", "deep"};
    static const char *const kSeps[] = {"\\", "/", "\\\\", "//"};
    std::string s;
    do {
        s = kHeads[g() % std::size(kHeads)];
        for (unsigned i = 0, n = g() % 5; i < n; ++i) {
            if (i) s += kSeps[g() % std::size(kSeps)];
            s += kParts[g() % std::size(kParts)];
        }
        if (g() % 4 == 0) s += kSeps[g() % std::size(kSeps)];
        if (g() % 24 == 0) s = std::string(g() % 300, 'w');
    } while (NeedsOtherDriveDirectory(s));
    return s;
}

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t W(const void *o, uint32_t at)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(o) + at, 4);
    return w;
}
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}
uint32_t Hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

// ---- names and the call log ----

std::map<uint32_t, std::string> gNames; // pointers the log and the form show by name
std::string gLog;
void Log(const std::string &line) { gLog += line + "; "; }
std::string Name(const void *p)
{
    if (!p) return "null";
    auto it = gNames.find(P(p));
    return it != gNames.end() ? it->second : "?" + Hex(P(p));
}

// ---- the fakes ----

struct FakeUnknown : cIGZUnknown {
    std::string name;
    uint32_t count = 1;
    bool QueryInterface(uint32_t riid, void **) override
    {
        Log(name + ".qi " + Hex(riid));
        return false;
    }
    uint32_t AddRef() override
    {
        Log(name + ".addref " + std::to_string(++count));
        return count;
    }
    uint32_t Release() override
    {
        Log(name + ".release " + std::to_string(--count));
        return count;
    }
};

// The parser: its cIBaseParser face (+0, 51 slots), the face's vbptr (+4: {-4, 4}: its cIGZUnknown at +8).
const int32_t kFakeVbTable[2] = {-4, 4};
struct FakeParser {
    void **vtable;
    const int32_t *vbtable;
    FakeUnknown unknown;
};
static_assert(offsetof(FakeParser, unknown) == 8, "FakeParser layout");

bool gAllowEscape = false; // RunFile may throw something that is not a cIGZExceptionBase
int gParserTraps = 0;
uint32_t ParserTrap(void *, unsigned) { return uint32_t(++gParserTraps); }

struct OtherError : cIGZExceptionBase {};

uint32_t __fastcall ParserInitialise(void *face, void *, uint32_t flag)
{
    Log(Name(face) + ".init " + Hex(flag));
    return 1;
}
uint32_t __fastcall ParserShutdown(void *face, void *)
{
    Log(Name(face) + ".shutdown");
    return 1;
}
uint32_t __fastcall ParserRestart(void *face, void *)
{
    Log(Name(face) + ".restart");
    return 0;
}
uint32_t __fastcall ParserFinished(void *face, void *)
{
    Log(Name(face) + ".finished");
    return 0;
}
uint32_t __fastcall ParserRunFile(void *face, void *, const char *path, uint32_t flags)
{
    Log(Name(face) + ".run '" + path + "' " + Hex(flags));
    switch (Hash(path) % 7) {
    case 0: {
        cp::cError error;
        error.mVTable = GameAddress<void>(cp::cError::kVTable);
        error.mMessage = "failed";
        throw error;
    }
    case 1: throw OtherError{};
    case 2:
        if (gAllowEscape) throw 7;
        break;
    default: break;
    }
    return 1;
}
struct FakeSource {
    void **vtable;
};
FakeSource gSource;
void *__fastcall ParserGetSource(void *face, void *)
{
    Log(Name(face) + ".source");
    return &gSource;
}

void *gParserVTable[51];
void InitParserVTable()
{
    for (unsigned s = 0; s < 51; ++s) gParserVTable[s] = shape::TrapFor<&ParserTrap>(0);
    gParserVTable[0] = reinterpret_cast<void *>(&ParserInitialise);
    gParserVTable[1] = reinterpret_cast<void *>(&ParserShutdown);
    gParserVTable[8] = reinterpret_cast<void *>(&ParserRestart);
    gParserVTable[11] = reinterpret_cast<void *>(&ParserFinished);
    gParserVTable[12] = reinterpret_cast<void *>(&ParserRunFile);
    gParserVTable[50] = reinterpret_cast<void *>(&ParserGetSource);
}

// The source's slot 7: the name it gives a path (by the path's hash): nothing (false), a path under one of three watched
// directories, or the path itself.
uint32_t __fastcall SourceCanonicalPath(void *, void *, const char *path, cIGZString *out)
{
    Log(std::string("source.canonical '") + path + "'");
    const uint32_t h = Hash(path);
    switch (h % 6) {
    case 0: return 0;
    case 1: out->FromChar(("C:\\games\\dir" + std::to_string(h / 6 % 3) + "\\file.txt").c_str()); return 1;
    case 2: out->FromChar(("c:/Games/Dir" + std::to_string(h / 6 % 3) + "/sub/x.cfg").c_str()); return 1;
    default: out->FromChar(path); return 1;
    }
}
void *gSourceVTable[8];

// The watchers the framework hands out (each with one reference).
int gWatcherSerial = 0;
struct FakeWatcher : cIGZPersistDirectoryWatcher {
    std::string name;
    uint32_t count = 1;
    explicit FakeWatcher(int serial) : name("W" + std::to_string(serial)) { gNames[P(this)] = name; }
    bool QueryInterface(uint32_t riid, void **) override
    {
        Log(name + ".qi " + Hex(riid));
        return false;
    }
    uint32_t AddRef() override
    {
        Log(name + ".addref " + std::to_string(++count));
        return count;
    }
    uint32_t Release() override
    {
        Log(name + ".release " + std::to_string(--count));
        if (count == 0) {
            gNames.erase(P(this));
            delete this;
            return 0;
        }
        return count;
    }
    void Slot3() override { Log(name + ".slot3"); }
    bool Init() override
    {
        Log(name + ".init");
        return true;
    }
    bool Shutdown() override
    {
        Log(name + ".shutdown");
        return true;
    }
    bool Start() override
    {
        Log(name + ".start");
        return true;
    }
    void Slot7() override { Log(name + ".slot7"); }
    void SetTarget(cIGZMessageTarget2 *target) override { Log(name + ".target " + Name(target)); }
    void Slot9(uint32_t) override { Log(name + ".slot9"); }
    void SetDirectory(cIGZString *directory, uint32_t filter, bool subtree) override
    {
        Log(name + ".directory '" + reinterpret_cast<cRZString *>(directory)->mString + "' " + Hex(filter) + " " +
            std::to_string(static_cast<uint8_t>(subtree)));
    }
};

// The director's framework object: slot 3 (GetClassObject) gives a new watcher three times in four.
int gComCalls = 0;
uint32_t __fastcall ComGetClassObject(void *, void *, uint32_t classId, uint32_t iid, void **out)
{
    Log("com.get " + Hex(classId) + " " + Hex(iid) + " out " + Name(*out));
    if (gComCalls++ % 4 == 3) return 0;
    *out = static_cast<cIGZUnknown *>(new FakeWatcher(++gWatcherSerial));
    return 1;
}
void *gComVTable[4];
void *gComObject = gComVTable;
void *__cdecl FakeFramework() { return &gComObject; }

// The message server (when on): slot 3 posts; logged with the message's fields.
bool gServerOn = true;
uint32_t __fastcall ServerPost(void *, void *, void *message)
{
    const auto *m = static_cast<const uint8_t *>(message);
    Log("server.post id " + Hex(W(m, 0xc)) + " flags " + Hex(m[8]) + " ptr24 " + Name(reinterpret_cast<void *>(W(m, 0x24))) +
        " count " + std::to_string(W(m, 4)));
    return 1;
}
void *gServerVTable[4];
void *gServerObject = gServerVTable;

void *__cdecl FakeMalloc(uint32_t n) { return std::malloc(n ? n : 1); }
void __cdecl FakeFree(void *p) { std::free(p); }

// A message for DoMessage: slot 4 is its id.
struct FakeMessage {
    void **vtable;
    uint32_t id;
};
uint32_t __fastcall MessageGetId(FakeMessage *m, void *) { return m->id; }
void *gMessageVTable[5];

int gTrapHits = 0;
uint32_t TrapHit(void *, unsigned) { return uint32_t(++gTrapHits); }

void InitFakes()
{
    static bool done = false;
    if (done) return;
    done = true;
    InitParserVTable();
    for (void *&s : gSourceVTable) s = shape::TrapFor<&ParserTrap>(0);
    gSourceVTable[7] = reinterpret_cast<void *>(&SourceCanonicalPath);
    gSource.vtable = gSourceVTable;
    for (void *&s : gComVTable) s = shape::TrapFor<&ParserTrap>(0);
    gComVTable[3] = reinterpret_cast<void *>(&ComGetClassObject);
    for (void *&s : gServerVTable) s = shape::TrapFor<&ParserTrap>(0);
    gServerVTable[3] = reinterpret_cast<void *>(&ServerPost);
    for (void *&s : gMessageVTable) s = shape::TrapFor<&ParserTrap>(0);
    gMessageVTable[4] = reinterpret_cast<void *>(&MessageGetId);
}

// Everything both sides' code reaches besides the object, for a test's life.
struct World {
    Allocators allocators;
    RealCwd cwd;
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &FakeFramework};
    shape::StandIn mallocStand{0x1be3b0, &RZMemory::Malloc, &FakeMalloc};
    shape::StandIn freeStand{0x1be3a1, &RZMemory::Free, &FakeFree};
    gzlog_test::Clocks clocks;
    gzlog_test::ClockScope clockScope;
    FakeParser parsers[2];
    explicit World(difftest::Context &ctx) : cwd(ctx), clockScope(ctx, clocks)
    {
        InitFakes();
        clocks.counter = {1000, 1300, 1700, 2600, 2900, 4100};
        for (int i = 0; i < 2; ++i) {
            parsers[i].vtable = gParserVTable;
            parsers[i].vbtable = kFakeVbTable;
        }
    }
    ~World() { *GameAddress<void *>(0x10f519c) = nullptr; }
    // A side starts: the fakes' state and names as new. The message server is the cache cell's content, which the
    // image's getter (0x9c5a89) and ours (GetCachedMessageServer) both return when it is set; with the server off the
    // cell stays empty and both ask the framework global (0x10c9378, null here) and find nothing.
    void Reset()
    {
        *GameAddress<void *>(0x10f519c) = gServerOn ? &gServerObject : nullptr;
        gLog.clear();
        gNames.clear();
        gWatcherSerial = 0;
        gComCalls = 0;
        gParserTraps = 0;
        gTrapHits = 0;
        clocks.Rewind();
        const char *names[2] = {"PA", "PB"};
        const char *unknowns[2] = {"UA", "UB"};
        for (int i = 0; i < 2; ++i) {
            parsers[i].unknown.name = unknowns[i];
            parsers[i].unknown.count = 1;
            gNames[P(&parsers[i])] = names[i];
            gNames[P(&parsers[i].unknown)] = unknowns[i];
        }
        gNames[P(&gSource)] = "source";
    }
};

// ---- the two sides' objects ----

// Our vtables: [0..2] the three faces, [3] rz::cRZUnknown's (what our destructor leaves at +8).
const uint32_t *OurVTables()
{
    static uint32_t words[4] = {};
    if (!words[0]) {
        rz::cRZUnknown *face = cp::cFileParser::Create();
        auto *o = reinterpret_cast<uint8_t *>(static_cast<cp::cFileParser *>(face));
        for (int i = 0; i < 3; ++i) words[i] = W(o, 4 * i);
        face->Release();
        rz::cRZUnknown probe;
        std::memcpy(&words[3], &probe, 4);
    }
    return words;
}

std::string Code(uint32_t word)
{
    const uint32_t *ours = OurVTables();
    if (word == ours[0] || word == P(GameAddress<void>(kGameVTable))) return "F0";
    if (word == ours[1] || word == P(GameAddress<void>(kGameTargetVTable))) return "F1";
    if (word == ours[2] || word == P(GameAddress<void>(kGameUnknownVTable))) return "F2";
    if (word == ours[3] || word == P(GameAddress<void>(kGameRZUnknownVTable))) return "RU";
    return Hex(word);
}

// A new file parser's object (the +0 face): the game's factory or ours.
uint8_t *NewSide(difftest::Context &ctx, int side)
{
    void *face = side == 0 ? ctx.Original<void *(__cdecl *)()>(0xb0dc30)() : static_cast<void *>(cp::cFileParser::Create());
    auto *o = static_cast<uint8_t *>(face) - 8;
    gNames[P(o)] = "self0";
    gNames[P(o + 4)] = "self4";
    gNames[P(o + 8)] = "self8";
    return o;
}

std::string StringForm(const cp::FileString &s)
{
    const uint32_t vt = P(s.value.mVTable);
    return std::string(vt == P(GameAddress<void>(cRZString::kVTable)) ? "S" : Hex(vt)) + "'" + s.value.mString + "'c" +
           std::to_string(s.value.mRefCount);
}

// The object's state (see the file comment for what is left out).
std::string Form(const uint8_t *o, const uint32_t *vtableWords = nullptr)
{
    uint32_t vt[3] = {W(o, 0), W(o, 4), W(o, 8)};
    if (vtableWords)
        for (int i = 0; i < 3; ++i) vt[i] = vtableWords[i];
    std::string s = Code(vt[0]) + "," + Code(vt[1]) + "," + Code(vt[2]) + " n" + std::to_string(W(o, 0xc)) + " init" +
                    std::to_string(o[0x10]) + " parser " + Name(reinterpret_cast<void *>(W(o, 0x14)));
    const auto &files = *reinterpret_cast<const std::vector<cp::FileEntry> *>(o + 0x18);
    s += " files " + std::to_string(files.size()) + "/" + std::to_string(files.capacity());
    for (const cp::FileEntry &e : files)
        s += " [" + StringForm(e.path) + " " + StringForm(e.resolved) + " r" +
             std::to_string(*reinterpret_cast<const uint8_t *>(&e.required)) + "]";
    s += " dirty" + std::to_string(o[0x24]);
    const auto &watchers = *reinterpret_cast<const std::map<std::string, cp::WatcherRef> *>(o + 0x28);
    s += " watch " + std::to_string(watchers.size());
    for (const auto &w : watchers) s += " '" + w.first + "'=" + Name(w.second.p);
    uint64_t start, accumulated;
    std::memcpy(&start, o + 0x38, 8);
    std::memcpy(&accumulated, o + 0x40, 8);
    s += " clock " + std::to_string(W(o, 0x30)) + "/" + std::to_string(start) + "/" + std::to_string(accumulated);
    return s;
}

// The game's vtable slot (face +0, 11 slots) that holds `rva`.
unsigned Slot(uint32_t rva)
{
    const unsigned s = shape::SlotOf(kGameVTable, 11, rva);
    if (s == ~0u) std::abort();
    return s;
}

template <class R, class... A> R CallSlot(void *face, unsigned slot, A... a)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(face, slot)(face, a...);
}

// ---- the operations ----

enum Op { kInit, kShutdown, kSetParser, kAddFile, kRunFiles, kSetDirty, kGetParser, kIsDirty, kDoMessage, kQuery, kCount, kOps };

struct Step {
    Op op;
    uint32_t a = 0, b = 0;
    std::string path;
};

std::vector<Step> RandomSteps(std::mt19937 &g)
{
    std::vector<Step> steps;
    bool hasParser = false, initialised = false; // Shutdown, once initialised, lets go of the parser
    const int n = 4 + g() % 14;
    for (int i = 0; i < n; ++i) {
        Step s;
        s.op = static_cast<Op>(g() % kOps);
        // Files are added more often, and only with a parser (the original asks it for its source unconditionally).
        if (g() % 3 == 0) s.op = kAddFile;
        if (s.op == kAddFile && !hasParser) s.op = kSetParser;
        switch (s.op) {
        case kInit: initialised = true; break;
        case kShutdown:
            if (initialised) hasParser = false;
            initialised = false;
            break;
        case kSetParser:
            s.a = g() % 5; // 0, 1: parser A / B; 2: null; 3, 4: A
            s.b = g() % 3 == 0 ? g() : g() % 2;
            hasParser = s.a != 2;
            break;
        case kAddFile:
            s.path = RandomPath(g);
            s.a = g() % 4;
            break;
        case kSetDirty: s.a = g() % 2; break;
        case kDoMessage: s.a = g() % 2 ? cp::cFileParser::kChangedMessage : (g() % 2 ? g() : cp::cFileParser::kChangedMessage + 1); break;
        case kQuery: {
            const uint32_t ids[] = {1, cp::cFileParser::kInterfaceId, 0, 0x1cf94584, 0x4b3bc675};
            s.a = ids[g() % std::size(ids)];
            s.b = 4 * (g() % 3);
            break;
        }
        case kCount: s.a = g() % 6; break; // AddRef on faces 0 / 4 / 8, RemoveRef, RefCount, Release on face 0 when above 1
        default: break;
        }
        steps.push_back(s);
    }
    return steps;
}

// Runs one step on a side's object through its own vtables; returns what it gave back.
std::string RunStep(World &world, uint8_t *o, const Step &s)
{
    switch (s.op) {
    case kInit: return std::to_string(CallSlot<uint32_t>(o, Slot(0xb0dd30)) & 0xff);
    case kShutdown: return std::to_string(CallSlot<uint32_t>(o, Slot(0xb0df90)) & 0xff);
    case kSetParser: {
        FakeParser *p = s.a == 2 ? nullptr : &world.parsers[s.a == 1];
        CallSlot<void>(o, Slot(0xb0df30), p, s.b);
        return "-";
    }
    case kAddFile: CallSlot<void>(o, Slot(0xb0d5c0), s.path.c_str(), s.a); return "-";
    case kRunFiles:
        try {
            return std::to_string(CallSlot<uint32_t>(o, Slot(0xb0dd40)) & 0xff);
        } catch (int e) {
            return "threw " + std::to_string(e);
        }
    case kSetDirty: CallSlot<void>(o, Slot(0x46e0c0), static_cast<uint32_t>(s.a)); return "-";
    case kGetParser: return Name(CallSlot<void *>(o, Slot(0x149d00)));
    case kIsDirty: return std::to_string(CallSlot<uint32_t>(o, Slot(0x1bcdd0)) & 0xff);
    case kDoMessage: {
        FakeMessage m{gMessageVTable, s.a};
        return std::to_string(CallSlot<uint32_t>(o + 4, 3, &m) & 0xff);
    }
    case kQuery: {
        void *out = reinterpret_cast<void *>(0x5eed5eed);
        const uint32_t r = CallSlot<uint32_t>(o + s.b, 0, s.a, &out) & 0xff;
        return std::to_string(r) + " " + (out == reinterpret_cast<void *>(0x5eed5eed) ? std::string("untouched") : Name(out));
    }
    case kCount:
        switch (s.a) {
        case 0: return std::to_string(CallSlot<uint32_t>(o, 1));
        case 1: return std::to_string(CallSlot<uint32_t>(o + 4, 1));
        case 2: return std::to_string(CallSlot<uint32_t>(o + 8, 1));
        case 3: return std::to_string(CallSlot<uint32_t>(o + 8, 4));
        case 4: return std::to_string(CallSlot<uint32_t>(o + 8, 5));
        default: return W(o, 0xc) > 1 ? std::to_string(CallSlot<uint32_t>(o + 4, 2)) : "skip";
        }
    default: return "?";
    }
}

} // namespace

// ---- sequences through the vtables ----

// Random sequences of every slot (the faces' QueryInterface / AddRef / Release / RemoveRef / RefCount, Init, Shutdown,
// SetParser, AddFile, RunFiles, the folded SetDirty / GetParser / IsDirty, DoMessage) on each side's own object from its
// factory, then its end: Release through a face to zero, or the deleting destructor (cRZUnknown face slot 3) with flags 1
// (freed) or 0 (the vtable words and count left; the block then freed by hand). After every step: the result, the calls the
// fakes saw (in order, with their arguments), the object's form; at the end whether it was freed and the calls its teardown
// made (watchers shut down and released, the parser released, the message posted after a run).
// Also covers the compiler-made adjustor thunks that exist only in our vtables (db/status/cFileParser.jsonl): the +8 face's
// slot 3 (0xb0d58e), the +4 face's QueryInterface (0xb0df17) and the +8 face's QueryInterface (0xb0df1f).
DIFF_TEST(FileParser_Sequences, 0xb0d5c0)
{
    World world(ctx);
    std::mt19937 g(31);
    for (int i = 0; i < 600; ++i) {
        const std::vector<Step> steps = RandomSteps(g);
        gAllowEscape = g() % 4 == 0;
        const bool serverOn = g() % 4 != 0;
        const uint32_t end = g() % 4;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            world.Reset();
            gServerOn = serverOn;
            *GameAddress<void *>(0x10f519c) = gServerOn ? &gServerObject : nullptr;
            uint8_t *o = NewSide(ctx, side);
            got[side] = "new " + Form(o) + "\n";
            for (const Step &s : steps) {
                gLog.clear();
                const std::string r = RunStep(world, o, s);
                got[side] += std::to_string(s.op) + " -> " + r + " | " + gLog + "| " + Form(o) + "\n";
            }
            gLog.clear();
            difftest::FreeRecorder freed;
            std::string ending;
            if (end == 0) {
                ending = "deleting 0 ";
                CallSlot<void *>(o + 8, 3, 0u);
                freed.Stop();
                ending += Code(W(o, 0)) + "," + Code(W(o, 4)) + "," + Code(W(o, 8)) + " n" + std::to_string(W(o, 0xc));
            } else if (end == 1) {
                ending = "deleting 1 " + std::string(CallSlot<void *>(o + 8, 3, 1u) == o + 8 ? "self8" : "other");
                freed.Stop();
            } else {
                ending = "release";
                for (int k = 0; k < 12; ++k) {
                    const uint32_t left = CallSlot<uint32_t>(o + 4 * (k % 3), 2);
                    ending += " " + std::to_string(left);
                    if (left == 0) break;
                }
                freed.Stop();
            }
            const bool gone = freed.Freed(o);
            got[side] += ending + (gone ? " freed" : " kept") + " | " + gLog + " traps " + std::to_string(gParserTraps);
            if (!gone) ::operator delete(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("sequence %d:\n--- A:\n%s\n--- B:\n%s", i, got[0].c_str(), got[1].c_str());
    }
}

// ---- the adapters ----

namespace {

// A game-built file parser whose vtables are copies with a trap in every slot an adapter forwards (an adapter that
// dispatched virtually lands there); AddRef and Release stay the game's (QueryInterface counts through them).
struct Trapped {
    void *face0[11], *face1[4], *face2[6];
    Trapped()
    {
        std::memcpy(face0, GameAddress<void>(kGameVTable), sizeof(face0));
        std::memcpy(face1, GameAddress<void>(kGameTargetVTable), sizeof(face1));
        std::memcpy(face2, GameAddress<void>(kGameUnknownVTable), sizeof(face2));
        const unsigned words0[] = {2, 0, 0, 0, 0, 2, 2, 0, 1, 0, 0};
        for (unsigned s : {0u, 3u, 4u, 5u, 6u, 7u}) face0[s] = shape::TrapFor<&TrapHit>(words0[s]);
        face1[0] = shape::TrapFor<&TrapHit>(2);
        face1[3] = shape::TrapFor<&TrapHit>(1);
        face2[0] = shape::TrapFor<&TrapHit>(2);
        face2[3] = shape::TrapFor<&TrapHit>(1);
    }
    void Apply(uint8_t *o)
    {
        const uint32_t w[3] = {P(face0), P(face1), P(face2)};
        std::memcpy(o, w, 12);
    }
    // The words as the game's (the copies stand for them).
    void Normalise(uint32_t *w)
    {
        if (w[0] == P(face0)) w[0] = P(GameAddress<void>(kGameVTable));
        if (w[1] == P(face1)) w[1] = P(GameAddress<void>(kGameTargetVTable));
        if (w[2] == P(face2)) w[2] = P(GameAddress<void>(kGameUnknownVTable));
    }
};

} // namespace

// Each hook entry (the adapters of 0xb0d390, 0xb0d5a0, 0xb0def0, 0xb0dd30, 0xb0df90, 0xb0df30, 0xb0d5c0, 0xb0dd40,
// 0xb0dc00) on a game-built object with trap vtables against the original at its RVA on a game-built object, after the same
// random prefix (run through the originals at their RVAs on both sides): result, calls, form and words compared, no trap hit.
DIFF_TEST(FileParser_Adapters, 0xb0d390)
{
    World world(ctx);
    using A = cp::cFileParserAdapters;
    Trapped traps;
    std::mt19937 g(43);
    for (int i = 0; i < 400; ++i) {
        const std::vector<Step> prefix = RandomSteps(g);
        gAllowEscape = false;
        const uint32_t which = i % 10;
        Step last;
        last.path = RandomPath(g);
        last.a = g() % 4;
        last.b = g() % 2 ? cp::cFileParser::kChangedMessage : g();
        const uint32_t ids[] = {1, cp::cFileParser::kInterfaceId, 0, 0x1cf94584};
        const uint32_t id = ids[g() % std::size(ids)];
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            world.Reset();
            uint8_t *o = NewSide(ctx, 0);
            // The prefix through the originals at their RVAs (the object's own vtables are the game's on both sides here).
            for (const Step &s : prefix) {
                switch (s.op) {
                case kInit: ctx.Original<uint32_t(__thiscall *)(void *)>(0xb0dd30)(o); break;
                case kShutdown: ctx.Original<uint32_t(__thiscall *)(void *)>(0xb0df90)(o); break;
                case kSetParser:
                    ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0xb0df30)(o, s.a == 2 ? nullptr : &world.parsers[s.a == 1], s.b);
                    break;
                case kAddFile: ctx.Original<void(__thiscall *)(void *, const char *, uint32_t)>(0xb0d5c0)(o, s.path.c_str(), s.a); break;
                case kRunFiles: ctx.Original<uint32_t(__thiscall *)(void *)>(0xb0dd40)(o); break;
                default: break;
                }
            }
            const bool hasParser = W(o, 0x14) != 0;
            if (side == 1) traps.Apply(o);
            gLog.clear();
            gTrapHits = 0;
            auto *self = reinterpret_cast<cp::cFileParser *>(o);
            std::string r;
            bool destroyed = false;
            difftest::FreeRecorder freed;
            switch (which) {
            case 0:
                if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0xb0d390)(o);
                else A::Destructor(self, nullptr);
                destroyed = true;
                break;
            case 1: {
                void *back = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xb0d5a0)(o, last.a & 1) : A::DeletingDestructor(self, nullptr, last.a & 1);
                r = back == o ? "self" : "other";
                destroyed = true;
                break;
            }
            case 2: {
                void *out = reinterpret_cast<void *>(0x5eed5eed);
                r = std::to_string((side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0xb0def0)(o, id, &out) : A::QueryInterface(self, nullptr, id, &out)) & 0xff);
                r += " " + (out == reinterpret_cast<void *>(0x5eed5eed) ? std::string("untouched") : Name(out));
                break;
            }
            case 3: r = std::to_string((side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xb0dd30)(o) : A::Init(self, nullptr)) & 0xff); break;
            case 4: r = std::to_string((side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xb0df90)(o) : A::Shutdown(self, nullptr)) & 0xff); break;
            case 5: {
                FakeParser *p = last.a == 2 ? nullptr : &world.parsers[last.a & 1];
                if (side == 0) ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0xb0df30)(o, p, last.b);
                else A::SetParser(self, nullptr, reinterpret_cast<cp::cIBaseParser *>(p), last.b);
                break;
            }
            case 6:
                if (!hasParser) break;
                if (side == 0) ctx.Original<void(__thiscall *)(void *, const char *, uint32_t)>(0xb0d5c0)(o, last.path.c_str(), last.a);
                else A::AddFile(self, nullptr, last.path.c_str(), last.a);
                break;
            case 7: r = std::to_string((side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xb0dd40)(o) : A::RunFiles(self, nullptr)) & 0xff); break;
            default: {
                FakeMessage m{gMessageVTable, last.b};
                r = std::to_string((side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0xb0dc00)(o + 4, &m) : A::DoMessage(reinterpret_cast<cIGZMessageTarget2 *>(o + 4), nullptr, &m)) & 0xff);
                break;
            }
            }
            freed.Stop();
            const bool gone = freed.Freed(o);
            uint32_t w[3] = {W(o, 0), W(o, 4), W(o, 8)};
            std::string form;
            if (gone) form = "freed";
            else {
                traps.Normalise(w);
                form = destroyed ? Code(w[0]) + "," + Code(w[1]) + "," + Code(w[2]) + " n" + std::to_string(W(o, 0xc)) : Form(o, w);
            }
            got[side] = r + " | " + gLog + "| " + form + " traps " + std::to_string(gTrapHits) + "/" + std::to_string(gParserTraps);
            // Teardown (not compared): the original destructor and the block.
            if (!gone) {
                gLog.clear();
                if (!destroyed) ctx.Original<void(__thiscall *)(void *)>(0xb0d390)(o);
                ::operator delete(o);
            }
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("adapter %u (case %d):\n    A: %s\n    B: %s", which, i, got[0].c_str(), got[1].c_str());
    }
}

// ---- the helpers: the file name's lazy parts, the stopwatch reset ----

namespace {

// A file name from a path (0x1c7ef4), then some parts emptied or replaced, as the case says.
struct Name5 {
    union {
        cRZFileName value;
    };
    struct PathText {
        union {
            cRZString value;
        };
        explicit PathText(const char *s) { value.Construct(s); }
        ~PathText() { value.Destruct(); }
    };
    Name5(const std::string &path, uint32_t edits)
    {
        {
            PathText text(path.c_str());
            value.Construct(&text.value);
        }
        cRZString *parts[5] = {&value.mDrive, &value.mDir, &value.mName, &value.mExt, &value.mFull};
        for (int k = 0; k < 5; ++k) {
            const uint32_t e = (edits >> (2 * k)) & 3;
            if (e == 1) parts[k]->mString.clear();
            if (e == 2) parts[k]->mString = k == 0 ? "q" : "\\x/y";
        }
    }
    ~Name5() { value.Destruct(); }
    std::string Text() const
    {
        return "'" + value.mDrive.mString + "' '" + value.mDir.mString + "' '" + value.mName.mString + "' '" + value.mExt.mString +
               "' '" + value.mFull.mString + "'";
    }
};

} // namespace

// 0x1c8554 (directory) and 0x1c85c2 (drive) at their RVAs against ours on file names made from random paths with parts
// emptied or replaced (the split runs only when the part is empty and the full path is not): the five strings and the
// returned part compared. 0x1c95dd on stopwatches of random words: the 0x18 bytes compared.
DIFF_TEST(FileParser_Helpers, 0x1c8554)
{
    Allocators allocators;
    RealCwd cwd(ctx);
    std::mt19937 g(53);
    for (int i = 0; i < 1500; ++i) {
        const std::string path = RandomPath(g);
        const uint32_t edits = g() & 0x3ff;
        const bool drive = i % 2 != 0;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            Name5 n(path, edits);
            cRZString *r;
            if (side == 0) r = ctx.Original<cRZString *(__thiscall *)(cRZFileName *)>(drive ? 0x1c85c2 : 0x1c8554)(&n.value);
            else r = drive ? cp::cFileParserAdapters::SplitDrive(&n.value, nullptr) : cp::cFileParserAdapters::SplitDirectory(&n.value, nullptr);
            got[side] = std::to_string(reinterpret_cast<uint8_t *>(r) - reinterpret_cast<uint8_t *>(&n.value)) + " " + n.Text();
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("%s '%s' edits %x: %s / %s", drive ? "drive" : "directory", path.c_str(), edits, got[0].c_str(), got[1].c_str());
    }
    for (int i = 0; i < 200; ++i) {
        uint32_t words[2][6];
        for (uint32_t &w : words[0]) w = g();
        std::memcpy(words[1], words[0], sizeof(words[0]));
        ctx.Original<void(__thiscall *)(void *)>(0x1c95dd)(words[0]);
        cp::cFileParserAdapters::ResetStopwatch(reinterpret_cast<cRZStopwatch *>(words[1]), nullptr);
        ++ctx.cases;
        if (std::memcmp(words[0], words[1], sizeof(words[0])) != 0) ctx.Fail("stopwatch reset %d differs", i);
    }
}
