// Differential test for the HIT debug information service (lane LH): cHitDebugInfo and its modules
// (src/audio/cHitDebugInfo.h), in the standalone shape.
//
// HitDebugInfo_Run: a service built by the original factory 0xb2a30c (every 4th seed) or constructor 0xb31526 (on 0xc5
// memory) against one built by ours (Create, or placement new), each driven through its own vtables by the same random
// sequence of operations, then destroyed by the last Release (0xb31c4e: the deleting destructor 0xb3163f behind the +8
// thunk 0xb31637; the destructor 0xb315a6 and the modules' destructor 0xb3160d). Operations: QueryInterface (0xb31bed;
// thunks 0xb31c3e (+4), 0xb31c46 (+8)) / AddRef / Release (thunks 0xb31c68 (+4), 0xb31c70 (+8)) on every face, a Release
// from count 0, the base service's accessors with GetServicePriority 0xb31865 and Shutdown on both faces (+4 thunk
// 0xb31c78), and every cIHitDebugInfo slot: AddModule 0xb31681 (the module constructor 0xb31577), FindLineAddress 0xb3176a
// (0xb316f2, 0xb31bc4), FindLine 0xb317d5 (0xb3186b, 0xb3188e), HitManAddress 0xb31711, HitManSet 0xb31817, LoadLineInfo
// 0xb318de, ReadSymbolFile 0xb31b28, AddLine 0xb316ea (0xb3165e).
//
// Every string argument is a recording fake cIGZString (Data / Strlen, and Copy for FindLine's output, logged), so the
// strings the code copies and drops are seen. The C runtime calls (fopen 0x83aa0f, fgets 0xc4b542, sscanf 0x242ec5, fclose
// 0x838ff4) are redirected on the original side and swapped through cHitDebugInfo's seams on ours, to the same fakes: a
// file per open (made from the seed and whether the object has a current module), every call logged, sscanf run as the
// CRT's vsscanf. The framework (game cell 0x10c9378) hands out the hit manager fake (counted); its slot 14 writes an object
// and an offset by the key, the objects answer slot 3 (a base) and take slot 6. Compared after every operation: the
// result (the low byte where the original returns a bool in al), the outputs, the event log, every field (the base
// service's, the count, every module with its name, base, size and lines in list order, which one is current, the string
// at +0x30, +0x50 / +0x54), the fakes' counts and the stack balance of every slot call; after the last Release, how many
// of the modules' blocks it did not free (difftest::FreeRecorder).
//
// Not driven (each the original's own undefined behaviour, see the bodies in src/audio/cHitDebugInfo.size.cpp):
// - FindLineAddress for line -1: 0xb31bc4's loop starts its "last line" at -1, so for a module without lines it returns
//   the list head node's uninitialised value (`cmp edx, [esp + 4]` after the loop, then `mov eax, [eax + 0xc]` with eax the
//   head);
// - LoadLineInfo lines that fail to scan before the first that scans in the same call: the scanned name (esp + 0x550) and
//   the pair (esp + 0x10 / 0x14) are uninitialised stack, written only by sscanf;
// - AddLine (and LoadLineInfo pairs) with no current module: 0xb316ea reads +0x2c and 0xb3165e uses it untested;
// - a failed new of a module (AddModule appends null and later slots use it), no hit manager (used untested);
// - relative paths for LoadLineInfo: cRZFileName resolves them with the OS (an import the harness traps).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "audio/cHitDebugInfo.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace hdi_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cHitDebugInfo;

constexpr uint32_t kTag = 0x7e000000; // the service under test
constexpr uint32_t kServiceCell = 0x10c9378;

enum : unsigned {
    kFw = 0,
    kHitMan,
    kObj0, // three objects the hit manager hands out
    kStr0 = 5, // the string fakes
};
constexpr unsigned kStrCount = 12;
constexpr unsigned kOutStr = kStr0 + kStrCount; // FindLine's output string
static_assert(kOutStr < svcfx::kFakes, "fakes");

enum : uint32_t { evOpen = 0x40, evGets, evScan, evClose, evCopy };

std::mt19937 gRng;
bool gOurs;
bool gFactory;
void *gObject; // the side's object (for the file generator)

uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
void LogText(const char *text, size_t n)
{
    Ev(0x7e7e0000u | uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
}
void LogText(const std::string &text) { LogText(text.data(), text.size()); }

// ---- the strings ----

// Texts of the string arguments: paths (absolute, canonical: see the file comment) and module / file names.
const char *const kTexts[kStrCount] = {
    "C:\\hit\\audio.dat", "C:\\hit\\sub\\x.y.z", "D:\\a\\", "C:\\hit\\audio", "C:\\a very long directory name\\file.hit",
    "C:\\hit\\audio.HDB", "a.hit", "", "C:\\hit\\b.hit", "verylongsourcefilename_more_than_15.hit", "A.HIT", "x",
};
std::string gOutText; // what FindLine copied into its output

uint32_t StrData(Obj *self, uint32_t *) { return P(kTexts[self->id - 200 - kStr0]); }
uint32_t StrLen(Obj *self, uint32_t *) { return uint32_t(std::strlen(kTexts[self->id - 200 - kStr0])); }
// FindLine's output: slot 17 (Copy) given a module's name (a cRZString, a heap address: logged by its text).
uint32_t __fastcall OutCopy(Obj *self, void *, uint32_t name)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(17);
    gOutText = reinterpret_cast<const cRZString *>(uintptr_t(name))->mString;
    LogText(gOutText);
    return 0;
}
void *Str(uint32_t i) { return &F(kStr0 + i % kStrCount); }

// ---- the hit manager ----

const int32_t kOffsets[] = {0, 1, 4, -4, 0x7fffffff, INT_MIN, 0x100};
const uint32_t kBases[] = {0, 0x1000, 0xfffffff0, 0x80000000, 1};
struct World {
    uint32_t objectBase[3];
};
World gWorld;

uint32_t GetService(Obj *, uint32_t *args)
{
    auto **out = reinterpret_cast<void **>(uintptr_t(args[2]));
    *out = &F(kHitMan);
    ++F(kHitMan).refs;
    return 0x101;
}
// Slot 14 (key, &object, &offset): which object (or null), whether each output is written, and the result, from the key.
uint32_t HitManLookup(Obj *, uint32_t *args)
{
    const uint32_t key = args[0];
    auto **object = reinterpret_cast<void **>(uintptr_t(args[1]));
    auto *offset = reinterpret_cast<int32_t *>(uintptr_t(args[2]));
    if ((key >> 2) % 5) *object = key % 4 == 3 ? nullptr : &F(kObj0 + key % 4);
    if ((key >> 8) % 4) *offset = kOffsets[(key >> 5) % 7];
    return (key >> 10) % 3 ? 0x5501 : 0x5500;
}

// ---- the C runtime ----

struct FakeFile {
    std::string text;
    size_t at = 0;
    uint32_t id = 0;
};
std::vector<FakeFile *> gFiles;
uint32_t gOpens;
uint32_t gOpenSeed;

const char *const kSourceNames[] = {"a.hit", "dir/b.hit", "verylongsourcefilename_more_than_15.hit", "c"};
const int32_t kNumbers[] = {0, 1, 2, 7, -1, -7, 100, INT_MAX, INT_MIN, 0x1000, 16, 4};

std::string Number(std::mt19937 &rng) { return std::to_string(kNumbers[rng() % 12]); }

// A file's text: lines of every kind the readers tell apart. Lines that would scan nothing before the first that scans,
// and pairs before any module, are left out (see the file comment).
std::string MakeFile(std::mt19937 &rng, bool current)
{
    std::string out;
    bool haveSource = false, havePair = false;
    const uint32_t lines = rng() % 14;
    for (uint32_t i = 0; i < lines; ++i) {
        std::string line;
        switch (rng() % 12) {
        case 0:
        case 1:
            line = "SOURCEFILE " + std::string(kSourceNames[rng() % 4]) + "  {line,offset}";
            haveSource = current = true;
            break;
        case 2:
            if (!haveSource) continue;
            line = rng() % 3 == 0 ? "Sxyz" : rng() % 2 ? "SOURCEFILE" : "SOURCE x";
            break;
        case 3:
        case 4:
        case 5:
        case 6:
            if (!current) continue;
            line = "{" + Number(rng) + "," + Number(rng) + "}";
            havePair = true;
            break;
        case 7:
            if (!current || !havePair) continue;
            line = rng() % 3 == 0 ? "{" + Number(rng) + "}" : rng() % 2 ? "{x,y}" : "{";
            break;
        case 8: line = rng() % 2 ? "" : " SOURCEFILE q.hit"; break;
        case 9: line = rng() % 2 ? "// comment" : "sym 1234 ("; break;
        case 10: line = std::string(1100 + rng() % 3, 'x'); break; // over fgets' 0x400: read in pieces
        default: line = "name " + Number(rng) + " (x)"; break;
        }
        out += line;
        if (i + 1 < lines || rng() % 3) out += "\n";
    }
    return out;
}

FILE *__cdecl FakeOpen(const char *path, const char *mode)
{
    Ev(evOpen);
    LogText(path, std::strlen(path));
    LogText(mode, std::strlen(mode));
    std::mt19937 rng(gOpenSeed * 7919 + gOpens++);
    if (rng() % 6 == 0) return nullptr;
    auto *file = new FakeFile;
    file->id = uint32_t(gFiles.size());
    file->text = MakeFile(rng, Word(gObject, 0x2c) != 0);
    gFiles.push_back(file);
    return reinterpret_cast<FILE *>(file);
}
char *__cdecl FakeGets(char *buffer, int size, FILE *stream)
{
    auto *file = reinterpret_cast<FakeFile *>(stream);
    Ev(evGets);
    Ev(uint32_t(size));
    Ev(file->id);
    if (file->at >= file->text.size() || size < 2) return nullptr;
    int n = 0;
    while (n < size - 1 && file->at < file->text.size()) {
        const char c = file->text[file->at++];
        buffer[n++] = c;
        if (c == '\n') break;
    }
    buffer[n] = 0;
    return buffer;
}
int __cdecl FakeScan(const char *text, const char *format, ...)
{
    Ev(evScan);
    LogText(format, std::strlen(format));
    LogText(text, std::strlen(text));
    va_list args;
    va_start(args, format);
    const int r = vsscanf(text, format, args);
    va_end(args);
    Ev(uint32_t(r));
    return r;
}
int __cdecl FakeClose(FILE *stream)
{
    Ev(evClose);
    Ev(reinterpret_cast<FakeFile *>(stream)->id);
    return 0;
}
void DropFiles()
{
    for (FakeFile *file : gFiles) delete file;
    gFiles.clear();
}

// The game's new / free over ours, so both sides' blocks meet.
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

// ---- driving ----

// Every slot is called with its arguments pushed by hand and the stack pointer read back: a body that pops a different
// number of bytes than the caller pushed sets gStackSkew, which each step logs.
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
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    static_assert(sizeof(R) == 4, "dword result");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return R(CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A))));
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    svcfx::ResetAll();
    DropFiles();
    gOpens = 0;
    gOpenSeed = seed;
    gOutText.clear();
    for (uint32_t &base : gWorld.objectBase) base = kBases[rng() % 5];
    F(kFw).Set(5, "ddp", 0, &GetService);
    F(kHitMan).Set(14, "dpp", 0, &HitManLookup);
    for (unsigned i = 0; i < 3; ++i) {
        F(kObj0 + i).Set(3, 0, gWorld.objectBase[i]);
        F(kObj0 + i).Set(6, "d", 0x77);
    }
    for (unsigned i = 0; i < kStrCount; ++i) {
        F(kStr0 + i).Set(5, 0, 0, &StrData);
        F(kStr0 + i).Set(6, 0, 0, &StrData);
        F(kStr0 + i).Set(7, 0, 0, &StrLen);
    }
    F(kOutStr).Set(17, 1);
    F(kOutStr).table[17] = reinterpret_cast<void *>(&OutCopy);
}

void DumpService(Blob &b, void *object)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kServiceVTable}, {8, Sys::kUnknownVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(gOurs ? !game : game);
    }
    // +0x1d..+0x1f (after the running flag) are never written: the 0xc5 fill, or heap leftovers from the factory.
    for (uint32_t off = 0xc; off < 0x1c; off += 4) b.push_back(Word(m, off));
    b.push_back(m[0x1c]);
    b.push_back(Word(m, 0x20));
    const auto *modules = reinterpret_cast<const HitDebugModules *>(m + 0x24);
    const auto *current = reinterpret_cast<const HitDebugModule *>(uintptr_t(Word(m, 0x2c)));
    b.push_back(uint32_t(modules->size()));
    uint32_t index = 0, currentAt = current ? 0xffff : 0xfffe;
    for (const HitDebugModule *module : *modules) {
        if (module == current) currentAt = index;
        ++index;
        b.push_back(Word(&module->mName, 0) == P(GameAddress<void>(cRZString::kVTable)));
        b.push_back(module->mName.s.mRefCount);
        LogText(module->mName.s.mString);
        b.push_back(module->mBase);
        b.push_back(module->mSize);
        b.push_back(uint32_t(module->mLines.size()));
        for (const HitDebugLine &pair : module->mLines) {
            b.push_back(uint32_t(pair.line));
            b.push_back(uint32_t(pair.offset));
        }
    }
    b.push_back(currentAt);
    const auto *text = reinterpret_cast<const ContentString *>(m + 0x30);
    b.push_back(Word(text, 0) == P(GameAddress<void>(cRZString::kVTable)));
    b.push_back(text->s.mRefCount);
    b.push_back(uint32_t(text->s.mString.size()));
    b.push_back(Word(m, 0x50));
    b.push_back(Word(m, 0x54));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 3);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 28;
    for (unsigned i = 0; i < n; ++i) ops.push_back({unsigned(rng() % 14), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

// Lines and addresses to look up: the numbers the files use and the modules' bases plus small offsets.
int32_t LineArg(uint32_t a)
{
    const int32_t line = kNumbers[a % 12];
    return line == -1 ? 3 : line; // not -1 (see the file comment)
}
uint32_t AddressArg(void *object, uint32_t a)
{
    const auto *modules = reinterpret_cast<const HitDebugModules *>(Face(object, 0x24));
    if (modules->empty() || a % 5 == 0) return kBases[(a >> 3) % 5] + (a >> 8) % 3;
    uint32_t k = (a >> 3) % modules->size();
    auto it = modules->begin();
    while (k--) ++it;
    static const int32_t kDeltas[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, -1, 0x100, 0x7fffffff};
    return (*it)->mBase + uint32_t(kDeltas[(a >> 8) % 14]) + ((a >> 12) % 4 == 0 ? (*it)->mSize : 0);
}
const uint32_t kSizes[] = {0, 1, 0x10, 0x1000, 0x7fffffff, 0xffffffff, 0x20};

void ServiceOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: { // QueryInterface on a face (null output sometimes), AddRef / Release through others
        static const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697, Sys::kServiceId, 0x12345678};
        void *faces[] = {primary, Face(object, 4), Face(object, 8)};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const bool null = (a >> 12) % 7 == 0;
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kIids[(a >> 4) % 5], null ? nullptr : &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 10) % 3], 2));
        if (ok && !null) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 1: { // the base service's accessors, both Shutdowns, cRZUnknownMT's RemoveRef / RefCount
        void *service = Face(object, 4);
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<uint32_t>(service, 7, a % 2);
        Slot<uint32_t>(service, 4, c);
        b.push_back(Slot<uint32_t>(service, 8) & 0xff);
        b.push_back(Slot<uint32_t>(service, 9) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 3) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 8), 4));
        b.push_back(Slot<uint32_t>(Face(object, 8), 5));
        break;
    }
    case 2:
    case 3: // AddModule
        Slot<uint32_t>(primary, 4, Str(a), kBases[(c >> 4) % 5] + c % 3, kSizes[(c >> 8) % 7]);
        break;
    case 4: { // FindLineAddress
        uint32_t out = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(primary, 5, Str(a), LineArg(c), &out) & 0xff);
        b.push_back(out);
        break;
    }
    case 5:
    case 6: { // FindLine
        int32_t line = 0x5e5e5e5e;
        gOutText.assign("old");
        b.push_back(Slot<uint32_t>(primary, 6, AddressArg(object, c), &F(kOutStr), &line) & 0xff);
        b.push_back(uint32_t(line));
        LogText(gOutText);
        break;
    }
    case 7: { // HitManAddress
        uint32_t out = 0x5e5e5e5e;
        b.push_back(Slot<uint32_t>(primary, 7, a, &out) & 0xff);
        b.push_back(out);
        break;
    }
    case 8: b.push_back(Slot<uint32_t>(primary, 8, a, c) & 0xff); break; // HitManSet
    case 9:
    case 10: // LoadLineInfo
        b.push_back(Slot<uint32_t>(primary, 9, Str(a % 6), kBases[(c >> 4) % 5] + c % 3, kSizes[(c >> 8) % 7]) & 0xff);
        break;
    case 11: b.push_back(Slot<uint32_t>(primary, 10, Str(a)) & 0xff); break; // ReadSymbolFile
    case 12: // AddLine, when there is a current module
        if (!Word(object, 0x2c)) {
            b.push_back(0xdead);
            break;
        }
        Slot<uint32_t>(primary, 11, kNumbers[a % 12], kNumbers[c % 12]);
        break;
    default: // AddRef / Release
        b.push_back(Slot<uint32_t>(Face(object, 4 * (a % 3)), 1));
        b.push_back(Slot<uint32_t>(Face(object, 4 * (c % 3)), 2));
        break;
    }
}

void *BuildService(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *face = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xb2a30c)();
        b.push_back(face ? 1 : 0);
        return Face(face, uint32_t(-4));
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0xb31526)(memory);
    b.push_back(r == memory);
    return memory;
}

// The modules' blocks (the list's order) before the last Release, and afterwards how many of them the CRT heap (both
// sides' new: the original's goes through GameNew) still holds: the destructor deletes every module. The free recorder
// sees both sides' frees (a heap walk's answer for a block just freed depends on the heap's layout: lane LU).
std::vector<void *> ModuleBlocks(void *object)
{
    std::vector<void *> out;
    for (HitDebugModule *module : *reinterpret_cast<const HitDebugModules *>(Face(object, 0x24))) out.push_back(module);
    return out;
}
uint32_t StillBusy(const std::vector<void *> &blocks, const difftest::FreeRecorder &freed)
{
    uint32_t busy = 0;
    for (void *block : blocks) busy += !freed.Freed(block);
    return busy;
}

void Finish(Blob &b)
{
    b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
    gStackSkew = 0;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
}

struct Seams {
    decltype(Sys::sOpen) open = Sys::sOpen;
    decltype(Sys::sGets) gets = Sys::sGets;
    decltype(Sys::sScan) scan = Sys::sScan;
    decltype(Sys::sClose) close = Sys::sClose;
    Seams()
    {
        Sys::sOpen = &FakeOpen;
        Sys::sGets = &FakeGets;
        Sys::sScan = &FakeScan;
        Sys::sClose = &FakeClose;
    }
    ~Seams()
    {
        Sys::sOpen = open;
        Sys::sGets = gets;
        Sys::sScan = scan;
        Sys::sClose = close;
    }
    Seams(const Seams &) = delete;
    Seams &operator=(const Seams &) = delete;
};

struct Scope {
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    shape::CodePatch open{0x83aa0f, shape::CodeOf(&FakeOpen)};
    shape::CodePatch gets{0xc4b542, shape::CodeOf(&FakeGets)};
    shape::CodePatch scan{0x242ec5, shape::CodeOf(&FakeScan)};
    shape::CodePatch close{0x838ff4, shape::CodeOf(&FakeClose)};
    Seams seams;
    Scope()
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        globals.Set(kServiceCell, &F(kFw));
    }
    ~Scope()
    {
        DropFiles();
        svcfx::gRanges.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

} // namespace hdi_test

using namespace hdi_test;

DIFF_TEST(HitDebugInfo_Run, 0xb31526)
{
    // Also reached (see the file comment): 0xb2a30c (factory), 0xb315a6 (destructor), 0xb3163f (deleting destructor),
    // 0xb31637 (its +8 thunk), 0xb3160d (the module destructor), 0xb31bed (QueryInterface), 0xb31c3e / 0xb31c46 (its
    // thunks), 0xb31c4e (Release), 0xb31c68 / 0xb31c70 (its thunks), 0xb31865, 0xb31c78, 0xb31681, 0xb31577, 0xb3176a,
    // 0xb316f2, 0xb31bc4, 0xb317d5, 0xb3186b, 0xb3188e, 0xb31711, 0xb31817, 0xb318de, 0xb31b28, 0xb316ea, 0xb3165e.
    Scope scope;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            Blob first;
            void *object = BuildService(ctx, first, gOurs);
            gObject = object;
            svcfx::AddRange(object, Sys::kSize, kTag);
            DumpService(first, object);
            if (seed % 7 == 0) { // a Release from count 0, then back
                first.push_back(Slot<uint32_t>(object, 2));
                first.push_back(Slot<uint32_t>(Face(object, 4), 1));
            }
            Finish(first);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    ServiceOp(b, object, ops[i]);
                    DumpService(b, object);
                } else {
                    const std::vector<void *> blocks = ModuleBlocks(object);
                    difftest::FreeRecorder freed;
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys the service
                    freed.Stop();
                    b.push_back(uint32_t(blocks.size()));
                    b.push_back(StillBusy(blocks, freed));
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        bool stop = false;
        for (size_t i = 0; i < results[0].size() || i < results[1].size(); ++i) {
            const Blob empty;
            const Blob &x = i < results[0].size() ? results[0][i] : empty;
            const Blob &y = i < results[1].size() ? results[1][i] : empty;
            if (!svcfx::Same(ctx, x, y, "HitDebugInfo_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < x.size() || w < y.size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < x.size() ? x[w] : 0, w < y.size() ? y[w] : 0);
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2), i && op < ops.size() ? ops[op].kind : 99u);
                stop = true;
                break;
            }
        }
        if (stop) break;
    }
}
