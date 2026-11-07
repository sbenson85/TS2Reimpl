// Differential tests for the cTSPersistSystem functions of lane FD.
//
// ITS slot 50 CommitSegmentsWithBackup (0x1dcef0). The world: a primary block (0x300 random bytes from the seed) whose
// own vtable has slot 54 (ClearSpeculativeLoader) and whose cITSPersistSystem face (+0xa8) has slot 51 (CommitSegments),
// both recording fakes; a segment vector of 0..5 entries drawn from four segment fakes (QueryInterface for the rollback
// face, Release, Open, Close, GetPath with per-segment texts). Each segment's rollback face is a fake too (QueryInterface
// for the packed-file face, Release, GetSegment handing out an inner fake whose GetPath has its own text,
// IsRollbackEnabled, IsUnchanged, EndRollback); the packed-file faces only log Release. Every answer is drawn from the
// seed and the call number, with garbage above the byte the callers test.
// No file is touched: cRZFile's constructor, destructor, Open, Length, Read, Write and Close are recording stand-ins on
// both sides (the original's at 0x1c0c07 / 0x1c0e6d / 0x1c285d / 0x1c230b / 0x1c2c6e / 0x1c3bec / 0x1c0faa in the image,
// ours in this executable: the replacement calls our cRZFile), and so is the free-space probe 0x1c1a65 (the same original
// callee on both sides). Read fills the buffer with seeded bytes and Write logs a hash of what it is given, so the backup
// that is written back is compared by content. The game's operator new / new[] / sized delete / delete[] are
// redirected to ours.
// Compared: the result byte, the call log (every fake and stand-in call with its arguments, in order) and the primary
// block afterwards. Not compared: whether the backup buffers are freed (ours uses delete[], the original 0x6c4b50; the
// buffers are not reachable from any state the test can see).
#include "difftest.h"
#include "game/vtable.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"
#include "std/StdHash.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"
#include "shape.h"
#include "ts/ChangeRecordSet.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

// Overwrites a function's first five bytes with a jump to a stand-in, for the scope of the test.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch()
    {
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
};

template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

uint32_t gSeed = 0;
uint32_t gCalls = 0;
std::vector<std::string> gLog;
alignas(16) uint8_t gPrimary[0x300];

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

// The next seeded answer: a dword with garbage above a low byte that is 1 with the given chance (percent).
uint32_t Answer(uint32_t percent)
{
    ++gCalls;
    const uint32_t r = Mix(gSeed, gCalls);
    return (r & 0xffffff00u) | (Mix(gSeed, 1000 + gCalls) % 100 < percent ? 1u : 0u);
}

std::string Hex(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

void Put(uint32_t off, uint32_t v)
{
    std::memcpy(gPrimary + off, &v, 4);
}

std::string Text(const void *cRZStringObject)
{
    const auto *s = static_cast<const cRZString *>(cRZStringObject);
    return s->mString + (s->mString.capacity() == 15 ? "|sso" : "|big");
}

// ---- the fakes: an object is a vtable pointer, a kind letter and an index ----

struct Obj {
    void *const *vt;
    char kind; // 's' segment, 'r' rollback face, 'p' packed-file face, 'g' the rollback face's segment
    int index;
};

std::string Name(const Obj *o)
{
    return std::string(1, o->kind) + std::to_string(o->index);
}

Obj gSegments[4], gRollbacks[4], gPacked[4], gInner[4];
bool gHasRollback[4], gHasPacked[4];

const char *const kSegmentPaths[] = {"C:\\Saves\\N001\\N001_Neighborhood.package", "c:/saves/n001/Lots/N001_Lot12.package",
                                     "", "D:/A folder with a name long enough for the heap/Family.package"};
const char *const kInnerPaths[] = {"C:\\Saves\\N001\\Characters\\N001_User00001.package", "short.package",
                                   "D:/Inner path that is also long enough to need a heap block.package", "x"};

uint32_t __fastcall SegQuery(Obj *self, void *, uint32_t iid, void **out)
{
    const uint32_t r = Answer(85);
    gLog.push_back(Name(self) + ".Query(" + Hex(iid) + ") -> " + Hex(r & 0xff));
    if ((r & 0xff) && gHasRollback[self->index]) *out = &gRollbacks[self->index];
    return (r & 0xff) && gHasRollback[self->index] ? r : r & 0xffffff00u;
}
uint32_t __fastcall SegRelease(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".Release");
    return Mix(gSeed, 2);
}
uint32_t __fastcall SegOpen(Obj *self, void *, uint32_t read, uint32_t write)
{
    gLog.push_back(Name(self) + ".Open(" + Hex(read & 0xff) + ", " + Hex(write & 0xff) + ")");
    return Answer(50);
}
uint32_t __fastcall SegClose(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".Close");
    return Answer(50);
}
uint32_t __fastcall SegGetPath(Obj *self, void *, void *path)
{
    const char *const *texts = self->kind == 'g' ? kInnerPaths : kSegmentPaths;
    static_cast<cRZString *>(path)->mString.assign(texts[self->index]);
    gLog.push_back(Name(self) + ".GetPath");
    return Mix(gSeed, 11);
}

uint32_t __fastcall RbQuery(Obj *self, void *, uint32_t iid, void **out)
{
    const uint32_t r = Answer(80);
    gLog.push_back(Name(self) + ".Query(" + Hex(iid) + ") -> " + Hex(r & 0xff));
    const bool ok = (r & 0xff) && gHasPacked[self->index];
    if (ok) *out = &gPacked[self->index];
    return ok ? r : r & 0xffffff00u;
}
uint32_t __fastcall RbGetSegment(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".GetSegment");
    return Word(&gInner[self->index]);
}
uint32_t __fastcall RbEnabled(Obj *self, void *)
{
    const uint32_t r = Answer(80);
    gLog.push_back(Name(self) + ".IsRollbackEnabled -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall RbUnchanged(Obj *self, void *)
{
    const uint32_t r = Answer(35);
    gLog.push_back(Name(self) + ".IsUnchanged -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall RbEnd(Obj *self, void *)
{
    const uint32_t r = Answer(88);
    gLog.push_back(Name(self) + ".EndRollback -> " + Hex(r & 0xff));
    return r;
}

void *gSegVT[16], *gRbVT[16], *gPkVT[16], *gInnerVT[16];
void *gPrimaryVT[64], *gItsVT[80];

uint32_t __fastcall PrimaryClearLoader(void *self, void *)
{
    gLog.push_back(std::string("primary.54") + (self == gPrimary ? "" : "?"));
    return Mix(gSeed, 54);
}
const void *gVector = nullptr;
uint32_t __fastcall ItsCommit(void *self, void *, void *segments)
{
    gLog.push_back(std::string("its.51(") + (segments == gVector ? "vector" : "?") + ")" + (self == gPrimary + 0xa8 ? "" : "?"));
    return Answer(50);
}

// ---- cRZFile stand-ins (thiscall as __fastcall): the file is known by its object address, named by its path ----

std::map<const void *, std::string> gFiles;

std::string FileName(const void *file)
{
    auto it = gFiles.find(file);
    return it == gFiles.end() ? std::string("<unknown file>") : it->second;
}
void *__fastcall FileConstruct(void *self, void *, const cIGZString *path)
{
    gFiles[self] = Text(path);
    gLog.push_back("file(" + Text(path) + ")");
    return self;
}
void __fastcall FileDestruct(void *self, void *)
{
    gLog.push_back("~file(" + FileName(self) + ")");
    gFiles.erase(self);
}
uint32_t __fastcall FileOpen(void *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    const uint32_t r = Answer(80);
    gLog.push_back("file.Open(" + FileName(self) + ", " + Hex(access) + ", " + Hex(creation) + ", " + Hex(sharing) + ") -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall FileLength(void *self, void *)
{
    ++gCalls;
    const uint32_t r = Mix(gSeed, gCalls) % 5 == 0 ? 0 : Mix(gSeed, 2000 + gCalls) % 600;
    gLog.push_back("file.Length(" + FileName(self) + ") -> " + Hex(r));
    return r;
}
uint32_t __fastcall FileRead(void *self, void *, void *dst, uint32_t length)
{
    const uint32_t r = Answer(80);
    for (uint32_t i = 0; i < length; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(Mix(gSeed + gCalls, i));
    gLog.push_back("file.Read(" + FileName(self) + ", " + Hex(length) + ") -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall FileWrite(void *self, void *, const void *src, uint32_t length)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < length; ++i) h = (h ^ static_cast<const uint8_t *>(src)[i]) * 16777619u;
    const uint32_t r = Answer(80);
    gLog.push_back("file.Write(" + FileName(self) + ", " + Hex(length) + ", bytes " + Hex(h) + ") -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall FileClose(void *self, void *)
{
    const uint32_t r = Answer(80);
    gLog.push_back("file.Close(" + FileName(self) + ") -> " + Hex(r & 0xff));
    return r;
}

// 0x1c1a65 (__cdecl): the free bytes on the path's drive.
uint64_t __cdecl FreeSpaceStand(const cRZString *path)
{
    ++gCalls;
    static const uint64_t kChoices[] = {0, 0x63fffff, 0x6400000, 0x1000000ull, 0x100000000ull, 0x5000ull, 0x100001000ull, 0x63ffff0ull};
    const uint64_t r = kChoices[Mix(gSeed, gCalls) % 8];
    gLog.push_back("freespace(" + Text(path) + ") -> " + Hex(static_cast<uint32_t>(r >> 32)) + ":" + Hex(static_cast<uint32_t>(r)));
    return r;
}

// The game's operator new (0x824632), sized delete (0x824624) and delete[] (0x6c4b50), redirected to ours.
void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}
void __cdecl GameDelete(void *p, size_t)
{
    ::operator delete(p);
}
void __cdecl GameDeleteArray(void *p)
{
    ::operator delete[](p);
}

void BuildFakes()
{
    gSegVT[0] = reinterpret_cast<void *>(&SegQuery);
    gSegVT[2] = reinterpret_cast<void *>(&SegRelease);
    gSegVT[5] = reinterpret_cast<void *>(&SegOpen);
    gSegVT[9] = reinterpret_cast<void *>(&SegClose);
    gSegVT[11] = reinterpret_cast<void *>(&SegGetPath);
    gInnerVT[11] = reinterpret_cast<void *>(&SegGetPath);
    gRbVT[0] = reinterpret_cast<void *>(&RbQuery);
    gRbVT[2] = reinterpret_cast<void *>(&SegRelease);
    gRbVT[3] = reinterpret_cast<void *>(&RbGetSegment);
    gRbVT[5] = reinterpret_cast<void *>(&RbEnabled);
    gRbVT[6] = reinterpret_cast<void *>(&RbUnchanged);
    gRbVT[7] = reinterpret_cast<void *>(&RbEnd);
    gPkVT[2] = reinterpret_cast<void *>(&SegRelease);
    gPrimaryVT[54] = reinterpret_cast<void *>(&PrimaryClearLoader);
    gItsVT[51] = reinterpret_cast<void *>(&ItsCommit);
    for (int i = 0; i < 4; ++i) {
        gSegments[i] = Obj{gSegVT, 's', i};
        gRollbacks[i] = Obj{gRbVT, 'r', i};
        gPacked[i] = Obj{gPkVT, 'p', i};
        gInner[i] = Obj{gInnerVT, 'g', i};
    }
}

struct SideResult {
    uint32_t ret = 0;
    std::vector<std::string> log;
    std::vector<uint8_t> primary;
};

using CtorString = cRZFile *(cRZFile::*)(const cIGZString &);

} // namespace

PS_TEST(TSPersistSystemFD_CommitSegmentsWithBackup, 0x1dcef0)
{
    using F = uint32_t(__thiscall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<F>(0x1dcef0);
    BuildFakes();

    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch gameDeleteArray(ctx.image.At<uint8_t>(0x6c4b50), reinterpret_cast<void *>(&GameDeleteArray));
    CodePatch freeSpace(ctx.image.At<uint8_t>(0x1c1a65), reinterpret_cast<void *>(&FreeSpaceStand));
    // cRZFile: the original's methods in the image, and ours.
    struct FileStand {
        uint32_t rva;
        void *ours;
        void *stand;
    };
    const FileStand stands[] = {
        {0x1c0c07, MemberCode(static_cast<CtorString>(&cRZFile::Construct)), reinterpret_cast<void *>(&FileConstruct)},
        {0x1c0e6d, MemberCode(&cRZFile::Destruct), reinterpret_cast<void *>(&FileDestruct)},
        {0x1c285d, MemberCode(&cRZFile::Open), reinterpret_cast<void *>(&FileOpen)},
        {0x1c230b, MemberCode(&cRZFile::Length), reinterpret_cast<void *>(&FileLength)},
        {0x1c2c6e, MemberCode(&cRZFile::Read), reinterpret_cast<void *>(&FileRead)},
        {0x1c3bec, MemberCode(&cRZFile::Write), reinterpret_cast<void *>(&FileWrite)},
        {0x1c0faa, MemberCode(&cRZFile::Close), reinterpret_cast<void *>(&FileClose)},
    };
    std::vector<std::unique_ptr<CodePatch>> patches;
    for (const FileStand &s : stands) {
        patches.emplace_back(new CodePatch(ctx.image.At<uint8_t>(s.rva), s.stand));
        patches.emplace_back(new CodePatch(s.ours, s.stand));
    }

    static void *items[8];
    struct Vector {
        void **first, **last, **end;
    } vec;
    gVector = &vec;

    uint32_t backups = 0, restored = 0, failed = 0, openFailed = 0, succeeded = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 rng(seed * 2654435761u + 13);
        const uint32_t count = rng() % 6;
        for (uint32_t i = 0; i < count; ++i) items[i] = &gSegments[rng() % 4];
        for (int i = 0; i < 4; ++i) {
            gHasRollback[i] = rng() % 5 != 0;
            gHasPacked[i] = rng() % 4 != 0;
        }
        const uint32_t guard = (rng() & 0xffffff00u) | (rng() % 10 < 7 ? 1u : 0u);

        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gLog.clear();
            gFiles.clear();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            Put(0, Word(gPrimaryVT));
            Put(0xa8, Word(gItsVT));
            vec.first = items;
            vec.last = items + count;
            vec.end = items + 8;
            void *its = gPrimary + 0xa8;
            const uint32_t r = side == 0 ? orig(its, &vec, guard)
                                         : psshape::Call(PSX(CommitSegmentsWithBackup), its, reinterpret_cast<void *const *>(&vec), guard);
            SideResult &s = sides[side];
            s.ret = r & 0xff;
            s.log = gLog;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
        }
        ++ctx.cases;
        bool backedUp = false, restoredAny = false, its = false, openFail = false;
        for (const std::string &line : sides[0].log) {
            if (line.rfind("file.Read(", 0) == 0) backedUp = true;
            if (line.rfind("file.Write(", 0) == 0) restoredAny = true;
            if (line.rfind("its.51", 0) == 0) its = true;
            if (line.rfind("file.Open(", 0) == 0 && line.find(", 0x1, 0x2, 0x3) -> 0x0") != std::string::npos) openFail = true;
        }
        backups += backedUp;
        restored += restoredAny;
        failed += its;
        openFailed += openFail;
        succeeded += sides[0].ret != 0 && count > 0;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) {
            size_t i = 0;
            while (i < sides[0].log.size() && i < sides[1].log.size() && sides[0].log[i] == sides[1].log[i]) ++i;
            ctx.Fail("seed %u: call %zu differs: original '%s', ours '%s'", seed, i, i < sides[0].log.size() ? sides[0].log[i].c_str() : "(end)",
                     i < sides[1].log.size() ? sides[1].log[i].c_str() : "(end)");
        }
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    if (backups < 150 || restored < 30 || failed < 150 || openFailed < 30 || succeeded < 150)
        ctx.Fail("coverage: %u backed up, %u restored, %u failed, %u backup opens failed, %u succeeded", backups, restored, failed, openFailed, succeeded);
}

// ---- AddPackageFile (ITS slot 46, 0x1e3180) ----
//
// The world: the primary block (random bytes) with its own vtable (slot 27 AddSegment, slot 47 the segment cache value),
// lock B (+0x68: slot 1 Lock, slot 2 Unlock) and the segment vector (+0xd4/+0xd8) over 0..4 of the segment fakes, whose
// GetPath hands out canonical absolute paths (so cRZFileName's construction needs no current directory); the path
// argument is a cRZString (the game's vtable: its Strlen / Data slots run as they are). The COM director static
// (0x10c9390, guard 0x10c93f4) answers slot 12 with a framework fake whose GetClassObject hands out the created object
// (Release, slot 3 its segment: Init (slot 3), Open (slot 6, only the low byte of the writable flag logged: the original
// pushes a dword with stack garbage above it), SetSegmentCache (slot 17)). File system: GetFileAttributesW (wide global
// 0x10cb5f4) is a fake answering by path and seed. Original callees as recording stand-ins on both sides: 0x1c8365 (file
// name compare: logs both names' parts, answers 0 or a seeded nonzero), 0x1e9b50, 0x1c2182, 0x1e7230 (logs both strings
// and its other arguments), 0x1dab60; the game's operator new / sized delete go to ours. cRZFileName's construction,
// GetExtension and GetName run for real on each side (the original's in the image, ours here).
// Compared: result byte, call log, primary block.

namespace {

constexpr uint32_t kDirectorStatic = 0x10c9390, kDirectorGuard = 0x10c93f4, kGetFileAttributesWGlobal = 0x10cb5f4;

const char *const kAddPaths[] = {"C:\\Sims\\Downloads\\Gizmo.package",
                                 "C:\\Sims\\Downloads\\GIZMO.PACKAGE",
                                 "D:\\Games\\Mods\\A folder name long enough for the heap\\Hair Set.package",
                                 "C:\\Sims\\Downloads\\notes.txt",
                                 "C:\\Sims\\Downloads\\Folder.files",
                                 "E:\\x.Package",
                                 "C:\\Sims\\Downloads\\Gizmo.packages",
                                 "C:\\Sims\\Neighborhoods\\N001\\N001_Neighborhood.package"};
constexpr uint32_t kAddPathCount = sizeof(kAddPaths) / sizeof(kAddPaths[0]);

Obj gAddSegs[4], gCom, gCreated, gNewSeg;
uint32_t gAddSegPath[4];
void *gAddSegVT[16], *gComVT[8], *gCreatedVT[8], *gNewSegVT[24], *gDirectorVT[16], *gAddPrimaryVT[64], *gLockVT[8];
bool gCreateWorks = false;

std::string Narrow(const wchar_t *w)
{
    std::string s;
    for (; *w; ++w) s += static_cast<char>(*w);
    return s;
}

uint32_t TextHash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

DWORD WINAPI FakeGetFileAttributesW(const wchar_t *path)
{
    const std::string p = Narrow(path);
    const bool exists = Mix(gSeed ^ 0x5151, TextHash(p)) % 4 != 0;
    gLog.push_back("GetFileAttributesW(" + p + ") -> " + (exists ? "file" : "none"));
    return exists ? FILE_ATTRIBUTE_NORMAL : INVALID_FILE_ATTRIBUTES;
}

uint32_t __fastcall AddSegGetPath(Obj *self, void *, void *path)
{
    static_cast<cRZString *>(path)->mString.assign(kAddPaths[gAddSegPath[self->index]]);
    gLog.push_back(Name(self) + ".GetPath");
    return Mix(gSeed, 11);
}
uint32_t __fastcall DirectorFramework(Obj *, void *)
{
    gLog.push_back("director.12");
    return Word(&gCom);
}
uint32_t __fastcall ComGetClassObject(Obj *, void *, uint32_t cls, uint32_t iid, void **out)
{
    const uint32_t r = Answer(85);
    const bool ok = (r & 0xff) && gCreateWorks;
    gLog.push_back("com.GetClassObject(" + Hex(cls) + ", " + Hex(iid) + ") -> " + (ok ? "1" : "0"));
    if (ok) *out = &gCreated;
    return ok ? r : r & 0xffffff00u;
}
uint32_t __fastcall CreatedSegment(Obj *, void *)
{
    gLog.push_back("created.3");
    return Word(&gNewSeg);
}
uint32_t __fastcall NewSegInit(Obj *, void *)
{
    const uint32_t r = Answer(75);
    gLog.push_back("newseg.3 -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall NewSegOpen(Obj *, void *, uint32_t read, uint32_t write)
{
    gLog.push_back("newseg.Open(" + Hex(read) + ", " + Hex(write & 0xff) + ")");
    return Mix(gSeed, 6);
}
uint32_t __fastcall NewSegSetCache(Obj *, void *, uint32_t value)
{
    gLog.push_back("newseg.17(" + Hex(value) + ")");
    return Mix(gSeed, 17);
}
uint32_t __fastcall AddPrimary27(void *self, void *, void *segment)
{
    gLog.push_back(std::string("primary.27(") + (segment == &gNewSeg ? "newseg" : "?") + ")" + (self == gPrimary ? "" : "?"));
    return Mix(gSeed, 27);
}
uint32_t __fastcall AddPrimary47(void *self, void *)
{
    gLog.push_back(std::string("primary.47") + (self == gPrimary ? "" : "?"));
    return Mix(gSeed, 47);
}
uint32_t __fastcall LockEnter(void *self, void *)
{
    gLog.push_back(std::string("lock") + (self == gPrimary + 0x68 ? "" : "?"));
    return Mix(gSeed, 81);
}
uint32_t __fastcall LockLeave(void *self, void *)
{
    gLog.push_back(std::string("unlock") + (self == gPrimary + 0x68 ? "" : "?"));
    return Mix(gSeed, 82);
}

std::string Parts(const cRZFileName *f)
{
    return f->mDrive.mString + "|" + f->mDir.mString + "|" + f->mName.mString + "|" + f->mExt.mString;
}
int32_t __fastcall CompareStand(cRZFileName *self, void *, cRZFileName *other, uint32_t flag)
{
    ++gCalls;
    const int32_t r = Mix(gSeed, gCalls) % 100 < 55 ? 0 : static_cast<int32_t>(Mix(gSeed, 3000 + gCalls) | 1);
    gLog.push_back("0x1c8365(" + Parts(self) + ", " + Parts(other) + ", " + Hex(flag) + ") -> " + Hex(static_cast<uint32_t>(r)));
    return r;
}
void __fastcall GroupsStand(void *self, void *, void *segment)
{
    gLog.push_back("0x1e9b50(" + std::string(self == gPrimary ? "primary" : "?") + ", " +
                   (segment >= static_cast<void *>(gAddSegs) && segment < static_cast<void *>(gAddSegs + 4) ? Name(static_cast<Obj *>(segment)) : "?") + ")");
}
uint32_t __cdecl WritableStand(const cRZString *path)
{
    const uint32_t r = Answer(50);
    gLog.push_back("0x1c2182(" + Text(path) + ") -> " + Hex(r & 0xff));
    return r;
}
void __fastcall SetUpStand(void *self, void *, cRZString *path, cRZString *name, uint32_t a3, uint32_t type, void *segment)
{
    gLog.push_back("0x1e7230(" + std::string(self == gPrimary ? "primary" : "?") + ", " + Text(path) + ", " + Text(name) + ", " + Hex(a3) + ", " +
                   Hex(type) + ", " + (segment == &gNewSeg ? "newseg" : "?") + ")");
}
void __cdecl FiltersStand(void *created)
{
    gLog.push_back(std::string("0x1dab60(") + (created == &gCreated ? "created" : "?") + ")");
}

void BuildAddFakes()
{
    gAddSegVT[11] = reinterpret_cast<void *>(&AddSegGetPath);
    gDirectorVT[12] = reinterpret_cast<void *>(&DirectorFramework);
    gComVT[3] = reinterpret_cast<void *>(&ComGetClassObject);
    gCreatedVT[2] = reinterpret_cast<void *>(&SegRelease);
    gCreatedVT[3] = reinterpret_cast<void *>(&CreatedSegment);
    gNewSegVT[3] = reinterpret_cast<void *>(&NewSegInit);
    gNewSegVT[6] = reinterpret_cast<void *>(&NewSegOpen);
    gNewSegVT[17] = reinterpret_cast<void *>(&NewSegSetCache);
    gAddPrimaryVT[27] = reinterpret_cast<void *>(&AddPrimary27);
    gAddPrimaryVT[47] = reinterpret_cast<void *>(&AddPrimary47);
    gLockVT[1] = reinterpret_cast<void *>(&LockEnter);
    gLockVT[2] = reinterpret_cast<void *>(&LockLeave);
    for (int i = 0; i < 4; ++i) gAddSegs[i] = Obj{gAddSegVT, 's', i};
    gCom = Obj{gComVT, 'f', 0};
    gCreated = Obj{gCreatedVT, 'c', 0};
    gNewSeg = Obj{gNewSegVT, 'n', 0};
}

} // namespace

PS_TEST(TSPersistSystemFD_AddPackageFile, 0x1e3180)
{
    using F = uint32_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e3180);
    BuildFakes();
    BuildAddFakes();

    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch compare(ctx.image.At<uint8_t>(0x1c8365), reinterpret_cast<void *>(&CompareStand));
    CodePatch groups(ctx.image.At<uint8_t>(0x1e9b50), reinterpret_cast<void *>(&GroupsStand));
    CodePatch groupsOurs(shape::MemberCode(&cTSPersistSystem::IndexSegmentGroups), reinterpret_cast<void *>(&GroupsStand));
    CodePatch writable(ctx.image.At<uint8_t>(0x1c2182), reinterpret_cast<void *>(&WritableStand));
    CodePatch setUp(ctx.image.At<uint8_t>(0x1e7230), reinterpret_cast<void *>(&SetUpStand));
    // Since lane FL our AddPackageFile calls our SetUpSegment, not the image: the same stand-in goes over it.
    CodePatch setUpOurs(shape::MemberCode(&cTSPersistSystem::SetUpSegment), reinterpret_cast<void *>(&SetUpStand));
    CodePatch filters(ctx.image.At<uint8_t>(0x1dab60), reinterpret_cast<void *>(&FiltersStand));
    void **directorCell = ctx.image.At<void *>(kDirectorStatic);
    int32_t *guardCell = ctx.image.At<int32_t>(kDirectorGuard);
    void **attributesCell = ctx.image.At<void *>(kGetFileAttributesWGlobal);
    void *const savedDirector = *directorCell;
    const int32_t savedGuard = *guardCell;
    void *const savedAttributes = *attributesCell;
    *directorCell = gDirectorVT;
    *guardCell = INT32_MIN;
    *attributesCell = reinterpret_cast<void *>(&FakeGetFileAttributesW);

    static void *items[8];
    uint32_t found = 0, created = 0, added = 0, missing = 0, notPackage = 0;
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 rng(seed * 2246822519u + 5);
        const uint32_t count = rng() % 5;
        for (uint32_t i = 0; i < count; ++i) items[i] = &gAddSegs[rng() % 4];
        for (uint32_t &p : gAddSegPath) p = rng() % kAddPathCount;
        const uint32_t argPath = rng() % kAddPathCount;
        gCreateWorks = rng() % 5 != 0;

        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gLog.clear();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            Put(0, Word(gAddPrimaryVT));
            Put(0x68, Word(gLockVT));
            Put(0xd4, Word(items));
            Put(0xd8, Word(items + count));
            Put(0xdc, Word(items + 8));
            cRZString path;
            path.Construct(kAddPaths[argPath]);
            void *its = gPrimary + 0xa8;
            const uint32_t r = side == 0 ? orig(its, &path) : psshape::Call(PSX(AddPackageFile), its, path.AsInterface());
            path.Destruct();
            SideResult &s = sides[side];
            s.ret = r & 0xff;
            s.log = gLog;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            found += line.rfind("0x1e9b50(", 0) == 0;
            created += line.rfind("0x1e7230(", 0) == 0;
            added += line.rfind("primary.27(", 0) == 0;
            missing += line.find("-> none") != std::string::npos;
        }
        notPackage += sides[0].log.size() > 0 && sides[0].log.back() == "unlock" && sides[0].ret == 0 &&
                      std::string(kAddPaths[argPath]).find(".package") == std::string::npos;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) {
            size_t i = 0;
            while (i < sides[0].log.size() && i < sides[1].log.size() && sides[0].log[i] == sides[1].log[i]) ++i;
            ctx.Fail("seed %u: call %zu differs: original '%s', ours '%s'", seed, i, i < sides[0].log.size() ? sides[0].log[i].c_str() : "(end)",
                     i < sides[1].log.size() ? sides[1].log[i].c_str() : "(end)");
        }
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    *directorCell = savedDirector;
    *guardCell = savedGuard;
    *attributesCell = savedAttributes;
    if (found < 150 || created < 100 || added < 60 || missing < 60 || notPackage < 60)
        ctx.Fail("coverage: %u found, %u set up, %u added, %u missing files, %u not packages", found, created, added, missing, notPackage);
}

// ---- the resource names: SetResourceName (ITS slot 54, 0x1e4de0) and GetResourceName (ITS slot 55, 0x1e21b0) ----
//
// The world: the primary block (random bytes) whose +0x1b8 holds a real std::map<segment id, std::map<type,
// std::map<(group, instance), cRZString *>>> built from the seed (small id / type / key sets so lookups hit and miss);
// four segment fakes (QueryInterface answering the directory-files id 0x856c8cf7 for some of them, Release,
// GetSegmentID); a directory-files fake (Release, slot 17 filling a canonical absolute path or failing). 0x1e8fd0 (the
// per-segment fill, original) is a recording stand-in on both sides that adds names for the segment in some seeds. The
// names are game-layout cRZStrings (the image's vtable: AddRef / Release run as they are, a release to zero frees through
// the game's delete, redirected to ours with operator new). cRZFileName runs for real on each side.
// Compared: result byte, call log, primary block (with the map's head and size words), the whole map afterwards (keys,
// texts, reference counts, which strings are the ones made before the call) and, for slot 55, *out (text and count).

namespace {

using NamesByKey = std::map<std::pair<uint32_t, uint32_t>, cRZString *>;
using NamesByType = std::map<uint32_t, NamesByKey>;
using Names = std::map<uint32_t, NamesByType>;

Obj gNameSegs[4], gDirectory;
void *gNameSegVT[24], *gDirectoryVT[24];
uint32_t gSegIds[4];
bool gHasDirectory[4];
std::vector<cRZString *> gPremade;
const char *const kNameTexts[] = {"Gizmo", "A name long enough to need a heap block", "", "x", "Bed (double)"};
const char *const kDirPaths[] = {"C:\\Sims\\Objects\\Gizmo.package", "D:\\X\\A long file name for the heap test.simpe", "C:\\N\\noext"};

cRZString *MakeName(const char *text)
{
    auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    s->Construct(text);
    s->mRefCount = 2; // the map's reference plus one held by the test, so a release stays observable
    return s;
}

Names &PrimaryNames()
{
    return *reinterpret_cast<Names *>(gPrimary + 0x1b8);
}

void FillNames(Names &names, uint32_t id, std::mt19937 &rng)
{
    NamesByType &types = names[id];
    for (uint32_t t = rng() % 3; t > 0; --t) {
        NamesByKey &keys = types[1 + rng() % 3];
        for (uint32_t k = rng() % 4; k > 0; --k) {
            cRZString *&slot = keys[std::make_pair(10 + rng() % 2, 100 + rng() % 3)];
            if (!slot) {
                slot = MakeName(kNameTexts[rng() % 5]);
                gPremade.push_back(slot);
            }
        }
    }
}

std::string Premade(const cRZString *s)
{
    for (size_t i = 0; i < gPremade.size(); ++i)
        if (gPremade[i] == s) return "#" + std::to_string(i);
    return "new";
}

std::vector<std::string> DumpNames(const Names &names)
{
    std::vector<std::string> out;
    for (const auto &segment : names) {
        out.push_back("seg " + Hex(segment.first));
        for (const auto &type : segment.second) {
            out.push_back("  type " + Hex(type.first));
            for (const auto &entry : type.second) {
                const cRZString *s = entry.second;
                out.push_back("    (" + Hex(entry.first.first) + ", " + Hex(entry.first.second) + ") " +
                              (s ? Text(s) + " refs " + std::to_string(s->mRefCount) + " " + Premade(s) : std::string("null")));
            }
        }
    }
    return out;
}

void FreeNames(Names &names)
{
    for (auto &segment : names)
        for (auto &type : segment.second)
            for (auto &entry : type.second)
                if (entry.second && Premade(entry.second) == "new") {
                    entry.second->Destruct();
                    ::operator delete(entry.second);
                }
    names.clear();
    for (cRZString *s : gPremade) {
        s->Destruct();
        ::operator delete(s);
    }
    gPremade.clear();
}

uint32_t __fastcall NameSegQuery(Obj *self, void *, uint32_t iid, void **out)
{
    const uint32_t r = Answer(90);
    const bool ok = (r & 0xff) && iid == 0x856c8cf7 && gHasDirectory[self->index];
    gLog.push_back(Name(self) + ".Query(" + Hex(iid) + ") -> " + (ok ? "1" : "0"));
    if (ok) *out = &gDirectory;
    return ok ? r : r & 0xffffff00u;
}
uint32_t __fastcall NameSegId(Obj *self, void *)
{
    gLog.push_back(Name(self) + ".GetSegmentID");
    return gSegIds[self->index];
}
uint32_t __fastcall DirectoryPath(Obj *, void *, const uint32_t *key, void *path)
{
    const uint32_t r = Answer(70);
    gLog.push_back("dir.17(" + Hex(key[0]) + ", " + Hex(key[1]) + ", " + Hex(key[2]) + ", " + Hex(key[3]) + ") -> " + Hex(r & 0xff));
    if (r & 0xff) static_cast<cRZString *>(path)->mString.assign(kDirPaths[Mix(gSeed, 77) % 3]);
    return r;
}
uint32_t gFillSeed = 0;
void __fastcall FillStand(void *self, void *, Obj *segment)
{
    gLog.push_back("0x1e8fd0(" + std::string(self == gPrimary ? "primary" : "?") + ", " + Name(segment) + ")");
    if (Mix(gSeed, 4242) % 3 != 0) {
        std::mt19937 rng(gFillSeed);
        FillNames(PrimaryNames(), gSegIds[segment->index], rng);
    }
}
void __cdecl GameDeletePlain(void *p)
{
    ::operator delete(p);
}

void BuildNameFakes()
{
    gNameSegVT[0] = reinterpret_cast<void *>(&NameSegQuery);
    gNameSegVT[2] = reinterpret_cast<void *>(&SegRelease);
    gNameSegVT[15] = reinterpret_cast<void *>(&NameSegId);
    gDirectoryVT[2] = reinterpret_cast<void *>(&SegRelease);
    gDirectoryVT[17] = reinterpret_cast<void *>(&DirectoryPath);
    for (int i = 0; i < 4; ++i) gNameSegs[i] = Obj{gNameSegVT, 's', i};
    gDirectory = Obj{gDirectoryVT, 'd', 0};
}

struct NameResult {
    uint32_t ret = 0;
    std::vector<std::string> log, names;
    std::vector<uint8_t> primary;
    std::string out;
};

// Runs one function (54 when `set`, else 55) over 1000 seeded worlds on both sides.
template <bool set, typename Orig> void RunNames(difftest::Context &ctx, Orig orig)
{
    BuildFakes();
    BuildNameFakes();
    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch gameDeletePlain(ctx.image.At<uint8_t>(0x82540f), reinterpret_cast<void *>(&GameDeletePlain));
    CodePatch fill(ctx.image.At<uint8_t>(0x1e8fd0), reinterpret_cast<void *>(&FillStand));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::LoadResourceNames), reinterpret_cast<void *>(&FillStand));

    uint32_t hits = 0, misses = 0, fills = 0, directory = 0, replaced = 0;
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        std::mt19937 rng(seed * 747796405u + (set ? 1u : 2u));
        for (int i = 0; i < 4; ++i) {
            gSegIds[i] = 0x100 + rng() % 3;
            gHasDirectory[i] = !set && rng() % 4 == 0;
        }
        Obj *segment = &gNameSegs[rng() % 4];
        const uint32_t key[4] = {1 + rng() % 3, 10 + rng() % 2, 100 + rng() % 3, static_cast<uint32_t>(rng())};
        const char *text = kNameTexts[rng() % 5];
        const uint32_t worldSeed = static_cast<uint32_t>(rng());
        gFillSeed = static_cast<uint32_t>(rng());

        NameResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gLog.clear();
            gPremade.clear();
            std::mt19937 fillBytes(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fillBytes());
            new (gPrimary + 0x1b8) Names();
            {
                std::mt19937 world(worldSeed);
                for (uint32_t n = world() % 3; n > 0; --n) FillNames(PrimaryNames(), 0x100 + world() % 3, world);
            }
            cRZString name;
            name.Construct(text);
            cIGZString *out = nullptr;
            void *its = gPrimary + 0xa8;
            uint32_t r;
            if constexpr (set) {
                r = side == 0 ? orig(its, segment, key, name.AsInterface())
                              : psshape::Call(PSX(SetResourceName), its, segment, key, name.AsInterface());
            } else {
                r = side == 0 ? orig(its, segment, key, &out) : psshape::Call(PSX(GetResourceName), its, segment, key, &out);
            }
            name.Destruct();
            NameResult &s = sides[side];
            s.ret = r & 0xff;
            s.log = gLog;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            if (out) {
                auto *o = reinterpret_cast<cRZString *>(out);
                s.out = Text(o) + " refs " + std::to_string(o->mRefCount) + " " + Premade(o);
                VirtualCall<uint32_t(__thiscall *)(void *), 2>(o); // the caller's reference
            }
            s.names = DumpNames(PrimaryNames());
            for (size_t i = 0; i < gPremade.size(); ++i)
                s.names.push_back("premade #" + std::to_string(i) + " refs " + std::to_string(gPremade[i]->mRefCount));
            FreeNames(PrimaryNames());
            PrimaryNames().~Names();
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            fills += line.rfind("0x1e8fd0(", 0) == 0;
            directory += line.rfind("dir.17(", 0) == 0;
        }
        hits += sides[0].ret != 0;
        misses += sides[0].ret == 0;
        if (set && sides[0].ret) {
            bool replacedOne = false;
            for (const std::string &line : sides[0].names) replacedOne = replacedOne || line.find(" new") != std::string::npos;
            replaced += replacedOne;
        }
        // The primary block holds the map's head pointer: compare it without those eight bytes (the head nodes differ
        // by side), the map itself is compared by content.
        for (NameResult &s : sides) std::memset(s.primary.data() + 0x1b8, 0, 8);
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) {
            size_t i = 0;
            while (i < sides[0].log.size() && i < sides[1].log.size() && sides[0].log[i] == sides[1].log[i]) ++i;
            ctx.Fail("seed %u: call %zu differs: original '%s', ours '%s'", seed, i, i < sides[0].log.size() ? sides[0].log[i].c_str() : "(end)",
                     i < sides[1].log.size() ? sides[1].log[i].c_str() : "(end)");
        }
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
        if (sides[0].names != sides[1].names) {
            size_t i = 0;
            while (i < sides[0].names.size() && i < sides[1].names.size() && sides[0].names[i] == sides[1].names[i]) ++i;
            ctx.Fail("seed %u: names differ at line %zu: original '%s', ours '%s'", seed, i, i < sides[0].names.size() ? sides[0].names[i].c_str() : "(end)",
                     i < sides[1].names.size() ? sides[1].names[i].c_str() : "(end)");
        }
        if (sides[0].out != sides[1].out) ctx.Fail("seed %u: out differs: original '%s', ours '%s'", seed, sides[0].out.c_str(), sides[1].out.c_str());
    }
    if (hits < 200 || misses < 100 || fills < 150 || (!set && directory < 80) || (set && replaced < 100))
        ctx.Fail("coverage: %u hits, %u misses, %u fills, %u directory paths, %u with a new string", hits, misses, fills, directory, replaced);
}

} // namespace

PS_TEST(TSPersistSystemFD_SetResourceName, 0x1e4de0)
{
    using F = uint32_t(__thiscall *)(void *, void *, const uint32_t *, void *);
    RunNames<true>(ctx, ctx.Original<F>(0x1e4de0));
}

PS_TEST(TSPersistSystemFD_GetResourceName, 0x1e21b0)
{
    using F = uint32_t(__thiscall *)(void *, void *, const uint32_t *, cIGZString **);
    RunNames<false>(ctx, ctx.Original<F>(0x1e21b0));
}

// ---- GetCachedResource (ITS slot 58, 0x1dfaa0) ----
// The world: lock D (+0x98) logged; the store at +0x84 (or none); mChangeRecords at +0x20c built by hand as the
// game's hash table (static head, eight buckets, mask 7) over 0..3 group nodes, each with change records holding
// 0..3 heap entries (0x20 bytes, objects that log their release) and a bucket vector of seeded junk. Our Find32 and
// the image's 0x15ac20 both read it. 0x1d7970 (insert), 0x1f6bd0 (read), 0x1f6780 (cached lookup) and 0x1f6410
// (erase), and ours (ChangeRecordSet Read, Take, Erase), are recording stand-ins
// on both sides; 0x1d7970 hands out a fourth, seeded group node. Compared: result,
// *out, call log, every group node's records by content (size, flag, entries, buckets as "head" or junk), and the
// primary block.
namespace {

struct CacheEntry : StdHashNode {
    uint32_t key[4];
    Obj *object;
    uint32_t changeId;
};
static_assert(sizeof(CacheEntry) == 0x20, "cache entry");
struct CacheRecords {
    uint32_t load;
    CacheEntry *head;
    uint32_t size;
    CacheEntry **bucketsBegin, **bucketsEnd, **bucketsCapacity;
    uint32_t mask, count;
    uint8_t changed, pad[3];
};
struct CacheGroupNode : StdHashNode {
    uint32_t group;
    CacheRecords records;
};
static_assert(sizeof(CacheGroupNode) == 0x30, "group node");

CacheGroupNode gGroupNodes[4], gOuterHead; // [3]: the node the 0x1d7970 stand-in gives
CacheEntry gRecordHeads[4];
CacheEntry *gRecordBuckets[4][6];
StdHashBucket gOuterBuckets[8];
Obj gCacheObjects[17], gStore, gRecord, gStream;
void *gCacheObjVT[8], *gStoreVT[24], *gRecordVT[8], *gCachePrimaryVT[64], *gLockDVT[8];
const uint32_t kCacheGroups[] = {0x10, 0x11, 0x12, 0x5a5a5a5a, 0xad1a6720};

uint32_t CacheFnv(uint32_t v)
{
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (8 * i)) & 0xff)) * 0x1000193u;
    return h;
}

std::string RecordsName(const void *p)
{
    for (int i = 0; i < 4; ++i)
        if (p == &gGroupNodes[i].records) return "rec" + std::to_string(i);
    return "?";
}
std::string ObjName(const Obj *o)
{
    return o ? Name(o) : std::string("null");
}
std::string KeyText(const uint32_t *k)
{
    return Hex(k[0]) + "," + Hex(k[1]) + "," + Hex(k[2]) + "," + Hex(k[3]);
}

void FillRecords(int i, std::mt19937 &rng)
{
    CacheRecords &r = gGroupNodes[i].records;
    CacheEntry *head = &gRecordHeads[i];
    r.load = rng();
    r.head = head;
    head->next = head->prev = head;
    r.size = 0;
    for (uint32_t k = rng() % 4; k > 0; --k) {
        auto *e = static_cast<CacheEntry *>(::operator new(sizeof(CacheEntry)));
        for (uint32_t &w : e->key) w = rng() % 5;
        e->object = rng() % 4 ? &gCacheObjects[i * 4 + (r.size & 3)] : nullptr;
        e->changeId = rng();
        e->prev = head->prev;
        e->next = head;
        head->prev->next = e;
        head->prev = e;
        ++r.size;
    }
    const uint32_t buckets = 2 + rng() % 5;
    for (uint32_t b = 0; b < 6; ++b) gRecordBuckets[i][b] = reinterpret_cast<CacheEntry *>(static_cast<uintptr_t>(0x7000 + i * 16 + b));
    r.bucketsBegin = gRecordBuckets[i];
    r.bucketsEnd = gRecordBuckets[i] + buckets;
    r.bucketsCapacity = gRecordBuckets[i] + 6;
    r.mask = rng();
    r.count = rng();
    r.changed = static_cast<uint8_t>(rng() % 2);
}

void FreeRecords(int i)
{
    CacheEntry *head = &gRecordHeads[i];
    for (StdHashNode *n = head->next; n && n != head;) {
        StdHashNode *next = n->next;
        ::operator delete(n);
        n = next;
    }
    head->next = head->prev = head;
}

std::vector<std::string> DumpRecords()
{
    std::vector<std::string> out;
    for (int i = 0; i < 4; ++i) {
        const CacheRecords &r = gGroupNodes[i].records;
        const CacheEntry *head = &gRecordHeads[i];
        out.push_back("rec" + std::to_string(i) + " group " + Hex(gGroupNodes[i].group) + " size " + std::to_string(r.size) + " changed " +
                      std::to_string(r.changed) + " load " + Hex(r.load) + " mask " + Hex(r.mask) + " count " + Hex(r.count) +
                      (r.head == head ? "" : " head?") + (head->prev && head->prev->next == head ? "" : " ring?"));
        int guard = 0;
        for (const StdHashNode *n = head->next; n && n != head && guard < 8; n = n->next, ++guard) {
            const auto *e = static_cast<const CacheEntry *>(n);
            out.push_back("  " + KeyText(e->key) + " " + ObjName(e->object) + " " + Hex(e->changeId));
        }
        std::string buckets = "  buckets";
        for (CacheEntry *const *b = r.bucketsBegin; b < r.bucketsCapacity; ++b)
            buckets += *b == head ? " head" : " " + Hex(Word(*b));
        out.push_back(buckets + " end " + std::to_string(r.bucketsEnd - r.bucketsBegin));
    }
    return out;
}

void BuildCacheWorld(uint32_t seed, uint32_t *groupOut)
{
    std::mt19937 rng(seed * 2654435761u + 58);
    std::mt19937 fillBytes(seed);
    for (auto &b : gPrimary) b = static_cast<uint8_t>(fillBytes());
    Put(0, Word(gCachePrimaryVT));
    Put(0x98, Word(gLockDVT));
    Put(0x84, rng() % 10 ? Word(&gStore) : 0);

    // 0..3 distinct groups from the candidates, linked in bucket order.
    std::vector<uint32_t> chosen;
    for (uint32_t n = rng() % 4; n > 0; --n) {
        const uint32_t g = kCacheGroups[rng() % 5];
        if (std::find(chosen.begin(), chosen.end(), g) == chosen.end()) chosen.push_back(g);
    }
    std::stable_sort(chosen.begin(), chosen.end(), [](uint32_t a, uint32_t b) { return (CacheFnv(a) & 7) < (CacheFnv(b) & 7); });
    StdHashNode *head = &gOuterHead;
    head->next = head->prev = head;
    for (auto &b : gOuterBuckets) b = StdHashBucket{head, head};
    for (size_t i = 0; i < chosen.size(); ++i) {
        CacheGroupNode *node = &gGroupNodes[i];
        node->group = chosen[i];
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
        StdHashBucket &b = gOuterBuckets[CacheFnv(chosen[i]) & 7];
        if (b.first == head) b.first = node;
        b.last = node;
    }
    for (size_t i = chosen.size(); i < 4; ++i) gGroupNodes[i].group = 0xdead0000u + static_cast<uint32_t>(i);
    for (int i = 0; i < 4; ++i) FillRecords(i, rng);
    Put(0x20c, rng());
    Put(0x210, Word(head));
    Put(0x214, static_cast<uint32_t>(chosen.size()));
    Put(0x218, Word(gOuterBuckets));
    Put(0x21c, Word(gOuterBuckets + 8));
    Put(0x220, Word(gOuterBuckets + 8));
    Put(0x224, 7);
    Put(0x228, 8);
    gPrimary[0x22c] = static_cast<uint8_t>(rng() % 2);
    *groupOut = kCacheGroups[rng() % 5];
}

uint32_t __fastcall LockDEnter(void *self, void *)
{
    gLog.push_back(std::string("lockD") + (self == gPrimary + 0x98 ? "" : "?"));
    return Mix(gSeed, 5);
}
uint32_t __fastcall LockDLeave(void *self, void *)
{
    gLog.push_back(std::string("unlockD") + (self == gPrimary + 0x98 ? "" : "?"));
    return Mix(gSeed, 6);
}
uint32_t __fastcall CacheChangeId(void *self, void *, const uint32_t *key, uint32_t *changeId)
{
    const uint32_t r = Answer(80);
    const uint32_t id = Mix(gSeed, 55) % 4 == 0 ? 0 : Mix(gSeed, 56);
    gLog.push_back(std::string("p.55(") + KeyText(key) + ") -> " + Hex(r & 0xff) + " id " + Hex(id) + (self == gPrimary ? "" : "?"));
    *changeId = id;
    return r;
}
uint32_t __fastcall StoreOpen(Obj *, void *, const uint32_t *key, Obj **record, uint32_t flag)
{
    const uint32_t r = Answer(75);
    gLog.push_back("store.22(" + KeyText(key) + ", " + Hex(flag) + ") -> " + Hex(r & 0xff));
    if ((r & 0xff) || Mix(gSeed, 22) % 3 == 0) *record = &gRecord;
    return r;
}
uint32_t __fastcall RecordQuery(Obj *, void *, uint32_t iid, void **out)
{
    const uint32_t r = Answer(80);
    gLog.push_back("record.Query(" + Hex(iid) + ") -> " + Hex(r & 0xff));
    if (r & 0xff) *out = &gStream;
    return r;
}
uint32_t __fastcall RecordSlot6(Obj *, void *)
{
    gLog.push_back("record.6");
    return Mix(gSeed, 7);
}
uint32_t *__fastcall CacheEntryStand(void *self, void *, uint32_t *result, const uint32_t *group)
{
    gLog.push_back("0x1d7970(" + std::string(self == gPrimary + 0x20c ? "table" : "?") + ", " + Hex(*group) + ")");
    gGroupNodes[3].group = *group;
    result[0] = Word(&gGroupNodes[3]);
    result[1] = 1;
    return result;
}
uint32_t __fastcall CacheReadStand(void *self, void *, Obj *stream)
{
    const uint32_t r = Answer(55);
    gLog.push_back("0x1f6bd0(" + RecordsName(self) + ", " + ObjName(stream) + ") -> " + Hex(r & 0xff));
    return r;
}
uint32_t __fastcall CacheLookupStand(void *self, void *, const uint32_t *key, uint32_t changeId, Obj **out)
{
    const uint32_t r = Answer(50);
    gLog.push_back("0x1f6780(" + RecordsName(self) + ", " + KeyText(key) + ", " + Hex(changeId) + ") -> " + Hex(r & 0xff));
    if (r & 0xff) *out = &gCacheObjects[16];
    return r;
}
void __fastcall CacheEraseStand(void *self, void *, const uint32_t *key)
{
    gLog.push_back("0x1f6410(" + RecordsName(self) + ", " + KeyText(key) + ")");
}

void BuildCacheFakes()
{
    gCacheObjVT[2] = reinterpret_cast<void *>(&SegRelease);
    gStoreVT[22] = reinterpret_cast<void *>(&StoreOpen);
    gRecordVT[0] = reinterpret_cast<void *>(&RecordQuery);
    gRecordVT[2] = reinterpret_cast<void *>(&SegRelease);
    gRecordVT[6] = reinterpret_cast<void *>(&RecordSlot6);
    gCachePrimaryVT[55] = reinterpret_cast<void *>(&CacheChangeId);
    gLockDVT[1] = reinterpret_cast<void *>(&LockDEnter);
    gLockDVT[2] = reinterpret_cast<void *>(&LockDLeave);
    for (int i = 0; i < 17; ++i) gCacheObjects[i] = Obj{gCacheObjVT, 'o', i};
    gStore = Obj{gStoreVT, 't', 0};
    gRecord = Obj{gRecordVT, 'c', 0};
    gStream = Obj{gCacheObjVT, 'm', 0};
}

struct CacheResult {
    uint32_t ret = 0;
    std::string out;
    std::vector<std::string> log, records;
    std::vector<uint8_t> primary;
};

} // namespace

PS_TEST(TSPersistSystemFD_GetCachedResource, 0x1dfaa0)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t, const uint32_t *, void **);
    auto orig = ctx.Original<F>(0x1dfaa0);
    BuildCacheFakes();
    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch entry(ctx.image.At<uint8_t>(0x1d7970), reinterpret_cast<void *>(&CacheEntryStand));
    CodePatch read(ctx.image.At<uint8_t>(0x1f6bd0), reinterpret_cast<void *>(&CacheReadStand));
    CodePatch lookup(ctx.image.At<uint8_t>(0x1f6780), reinterpret_cast<void *>(&CacheLookupStand));
    CodePatch erase(ctx.image.At<uint8_t>(0x1f6410), reinterpret_cast<void *>(&CacheEraseStand));
    CodePatch readOurs(shape::MemberCode(&ChangeRecordSet::Read), reinterpret_cast<void *>(&CacheReadStand));
    CodePatch lookupOurs(shape::MemberCode(&ChangeRecordSet::Take), reinterpret_cast<void *>(&CacheLookupStand));
    CodePatch eraseOurs(shape::MemberCode(&ChangeRecordSet::Erase), reinterpret_cast<void *>(&CacheEraseStand));

    uint32_t found = 0, loaded = 0, cleared = 0, hits = 0, erased = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        CacheResult sides[2];
        uint32_t key[4];
        {
            std::mt19937 k(seed + 9);
            for (uint32_t &w : key) w = k() % 5;
        }
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gLog.clear();
            uint32_t group;
            BuildCacheWorld(seed, &group);
            void *out = nullptr;
            void *its = gPrimary + 0xa8;
            const uint32_t r = side == 0 ? orig(its, group, key, &out)
                                         : psshape::Call(PSX(GetCachedResource), its, group, key, &out);
            CacheResult &s = sides[side];
            s.ret = r & 0xff;
            s.out = ObjName(static_cast<Obj *>(out));
            s.log = gLog;
            s.records = DumpRecords();
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            for (int i = 0; i < 4; ++i) FreeRecords(i);
        }
        ++ctx.cases;
        bool sawEntry = false, sawRead = false;
        for (const std::string &line : sides[0].log) {
            sawEntry = sawEntry || line.rfind("0x1d7970(", 0) == 0;
            sawRead = sawRead || (line.rfind("0x1f6bd0(", 0) == 0 && line.find("-> 0x0") != std::string::npos);
            found += line.rfind("0x1f6780(rec0", 0) == 0 || line.rfind("0x1f6780(rec1", 0) == 0 || line.rfind("0x1f6780(rec2", 0) == 0;
            erased += line.rfind("0x1f6410(", 0) == 0;
        }
        loaded += sawEntry;
        cleared += sawRead; // a failed read: the handed-out node's records (0..3 entries) are emptied
        hits += sides[0].ret != 0;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].out != sides[1].out) ctx.Fail("seed %u: out: original %s, ours %s", seed, sides[0].out.c_str(), sides[1].out.c_str());
        if (sides[0].log != sides[1].log) {
            size_t i = 0;
            while (i < sides[0].log.size() && i < sides[1].log.size() && sides[0].log[i] == sides[1].log[i]) ++i;
            ctx.Fail("seed %u: call %zu differs: original '%s', ours '%s'", seed, i, i < sides[0].log.size() ? sides[0].log[i].c_str() : "(end)",
                     i < sides[1].log.size() ? sides[1].log[i].c_str() : "(end)");
        }
        if (sides[0].records != sides[1].records) {
            size_t i = 0;
            while (i < sides[0].records.size() && i < sides[1].records.size() && sides[0].records[i] == sides[1].records[i]) ++i;
            ctx.Fail("seed %u: records differ at line %zu: original '%s', ours '%s'", seed, i,
                     i < sides[0].records.size() ? sides[0].records[i].c_str() : "(end)", i < sides[1].records.size() ? sides[1].records[i].c_str() : "(end)");
        }
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    if (found < 100 || loaded < 200 || cleared < 50 || hits < 100 || erased < 100)
        ctx.Fail("coverage: %u found, %u loaded, %u cleared, %u hits, %u erased", found, loaded, cleared, hits, erased);
}
