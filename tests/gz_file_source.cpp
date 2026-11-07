#define NOMINMAX
// Differential tests for nGZCommandParser::cFileSource (lane IM; src/gz/cFileSource.{h,cpp}), in the standalone shape.
//
// The original side is a source as the game's constructors build it inline (cBaseParser 0xafd800, SetSource 0xb06140: the
// game's vtables 0xe7dc30 / 0xe7dc54 and a count), every slot called through those vtables (the image's functions). Our side
// is `new cFileSource` (our constructor), every slot called through its own vtable, in the slot where the game's vtable holds
// the original. Vtable words are compared as codes (the game's and ours of the same face alike; cRZRefCount's after the
// destructor); nothing else is normalised. The adapters test calls the hooks' entries on game-built objects whose vtables are
// traps. Callees run on both sides: PathExists (0x1c1242 and ours) and cRZFile's Open / Length / Read / Close (the image's
// and ours) are stand-ins that log and answer the same; the file's constructor and destructor run for real (no file I/O).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz_file_source.h"
#include "shape.h"

#include "app/IniFile.h"
#include "gz/cFileSource.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"
#include "game/vtable.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <direct.h>
#include <windows.h>

namespace cp = nGZCommandParser;

namespace {

constexpr uint32_t kGameVTable = filesource::kGameVTable;
constexpr uint32_t kGameRefCountVTable = filesource::kGameRefCountVTable;
constexpr uint32_t kGameRZRefCountVTable = 0xda2fa8; // what the original destructor leaves at +4

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// The game's operator new / delete as ours (both sides free through the same heap; FreeRecorder sees both).
struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

// The allocator service the file data vector uses (cache 0x10c95d4, guard 0x10c95d8 as after its first use): slot 3
// allocates, slot 4 frees; the live count is checked after each case.
int gServiceLive = 0;
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n)
{
    ++gServiceLive;
    return std::malloc(n ? n : 1);
}
void __fastcall ServiceFree(void *, void *, void *p)
{
    if (p) --gServiceLive;
    std::free(p);
}
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceAlloc), reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct ServiceFake {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    explicit ServiceFake(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache), savedGuard(*guard)
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
    }
    ~ServiceFake()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---- the two sides' objects ----

// A source as the game's constructors build it: the game's vtables and a count.
void *NewOriginal(uint32_t count)
{
    auto *w = static_cast<uint32_t *>(::operator new(12));
    w[0] = P(GameAddress<void>(kGameVTable));
    w[1] = P(GameAddress<void>(kGameRefCountVTable));
    w[2] = count;
    return w;
}
void *NewOurs(uint32_t count)
{
    auto *s = new cp::cFileSource;
    s->mRefCount = count;
    return static_cast<cp::cIParserSource *>(s);
}
void *NewSide(int side, uint32_t count) { return side == 0 ? NewOriginal(count) : NewOurs(count); }
void FreeSide(void *object) { ::operator delete(object); } // the fields need no teardown

// Our rz::cRZRefCount vtable (what our destructor leaves at +4).
uint32_t OurRefCountBaseVTable()
{
    static uint32_t word = 0;
    if (!word) {
        alignas(8) static uint8_t buffer[sizeof(cp::cFileSource)];
        auto *s = ::new (buffer) cp::cFileSource;
        s->cp::cFileSource::~cFileSource();
        std::memcpy(&word, buffer + 4, 4);
    }
    return word;
}

// A word of an object as text: vtables as codes (face 0, face 1, cRZRefCount's), the rest in hex.
std::string Code(uint32_t word)
{
    const uint32_t *ours = filesource::OurVTables();
    if (word == ours[0] || word == P(GameAddress<void>(kGameVTable))) return "F0";
    if (word == ours[1] || word == P(GameAddress<void>(kGameRefCountVTable))) return "F1";
    if (word == OurRefCountBaseVTable() || word == P(GameAddress<void>(kGameRZRefCountVTable))) return "RC";
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", word);
    return b;
}
std::string Words(const void *object)
{
    const auto *w = static_cast<const uint32_t *>(object);
    return Code(w[0]) + "," + Code(w[1]) + "," + Code(w[2]);
}

// The game's vtable slot that holds `rva` (face 0, 8 slots).
unsigned Slot(uint32_t rva)
{
    const unsigned s = shape::SlotOf(kGameVTable, 8, rva);
    if (s == ~0u) std::abort();
    return s;
}

template <class R, class... A> R CallSlot(void *face, unsigned slot, A... a)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(face, slot)(face, a...);
}

// A cRZString built and destroyed by the game's bodies (ours: Construct / Destruct), the slot's out argument.
struct Text {
    union {
        cRZString value;
    };
    explicit Text(const char *s = "") { value.Construct(s); }
    ~Text() { value.Destruct(); }
    cIGZString *Face() { return value.AsInterface(); }
};

// As tests/ini_file_paths.cpp: the original's static CRT is not initialised in the harness, so the two CRT call sites in
// MakeAbsolutePath 0x1c1655 (_getdrive 0x1c16c8, _getdcwd 0x1c16f2: library code, the functions our code calls) are
// redirected to the host's CRT for the guard's life; relative paths then resolve against the real working directory on
// both sides.
struct RealCwd {
    static constexpr uint32_t kGetDriveSite = 0x1c16c8, kGetDcwdSite = 0x1c16f2;
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
        sites[0] = ctx.image.At<uint8_t>(kGetDriveSite);
        sites[1] = ctx.image.At<uint8_t>(kGetDcwdSite);
        for (int i = 0; i < 2; ++i) std::memcpy(saved[i], sites[i], 5);
        Patch(sites[0], reinterpret_cast<void *>(&::_getdrive));
        Patch(sites[1], reinterpret_cast<void *>(&::_getdcwd));
    }
    ~RealCwd()
    {
        for (int i = 0; i < 2; ++i) std::memcpy(sites[i], saved[i], 5);
    }
};

// "X:rel" for a drive other than the current one: the CRT's _getdcwd for another drive crashes in this process (the host's
// own CRT too; tests/ini_file_paths.cpp), so such paths are left out of the resolving tests.
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
                                         ".", "..\\", "C:\\.", "C:/./"};
    static const char *const kParts[] = {"dir", "Res", "a b", "x.txt", "..", ".", "file.package", "Config", "%s", "deep"};
    static const char *const kSeps[] = {"\\", "/", "\\\\", "//", "\\.", "/."};
    std::string s = kHeads[g() % std::size(kHeads)];
    for (unsigned i = 0, n = g() % 5; i < n; ++i) {
        if (i) s += kSeps[g() % std::size(kSeps)];
        s += kParts[g() % std::size(kParts)];
    }
    if (g() % 4 == 0) s += kSeps[g() % std::size(kSeps)];
    if (g() % 16 == 0) s = std::string(g() % 300, 'w');
    return s;
}

} // namespace

// ---- the reference count, QueryInterface, the destructor, CanonicalPath ----

// Every slot of both faces through each side's own vtable (QueryInterface is folded 0xb059b0, AddRef 0x1dab20 and Release
// 0x5b7980 folded, the count face's thunks 0xaff047 / 0x683fab / 0x1dab28 and the shared 0x1b9538 / 0x147a30; the deleting
// destructor 0xaff0a0, CanonicalPath 0xb02720): QueryInterface for ids 1, 0x4d8241bd and others (the out word untouched or
// the object; the count after), AddRef / Release / RemoveRef / RefCount on either face over counts 0..4, Release to zero (the
// object freed: FreeRecorder), the deleting destructor with flags 0 (the words left, as codes) and 1 (freed), CanonicalPath
// (the out string's text, al).
DIFF_TEST(FileSource_Slots, 0xb059b0)
{
    Allocators allocators;
    const uint32_t ids[] = {1, 0x4d8241bd, 0, 2, 0x4d8241bc, 0x4d8241be, 0x1cf94583, 0xffffffff};
    for (uint32_t id : ids)
        for (uint32_t count = 0; count < 3; ++count) {
            std::string got[2];
            for (int side = 0; side < 2; ++side) {
                void *o = NewSide(side, count);
                void *out = reinterpret_cast<void *>(0x5eed5eed);
                const uint32_t r = CallSlot<uint32_t>(o, 0, id, &out) & 0xff;
                got[side] = std::to_string(r) + " out " + (out == o ? std::string("self") : Code(P(out))) + " " + Words(o);
                FreeSide(o);
            }
            ++ctx.cases;
            if (got[0] != got[1]) ctx.Fail("QueryInterface %08x count %u: %s / %s", id, count, got[0].c_str(), got[1].c_str());
        }

    // The count slots: {face, slot} with the count before.
    struct CountSlot {
        uint32_t face;
        unsigned slot;
        const char *name;
    };
    const CountSlot slots[] = {{0, 1, "AddRef"}, {0, 2, "Release"}, {4, 1, "count Release"}, {4, 2, "count AddRef"},
                               {4, 3, "RemoveRef"}, {4, 4, "RefCount"}};
    for (const CountSlot &s : slots)
        for (uint32_t count = 0; count < 5; ++count) {
            std::string got[2];
            for (int side = 0; side < 2; ++side) {
                void *o = NewSide(side, count);
                difftest::FreeRecorder freed;
                const uint32_t r = CallSlot<uint32_t>(static_cast<uint8_t *>(o) + s.face, s.slot);
                freed.Stop();
                got[side] = std::to_string(r) + (freed.Freed(o) ? " freed" : " " + Words(o));
                if (!freed.Freed(o)) FreeSide(o);
            }
            ++ctx.cases;
            if (got[0] != got[1]) ctx.Fail("%s count %u: %s / %s", s.name, count, got[0].c_str(), got[1].c_str());
        }

    // The deleting destructor through the count face's slot 0.
    for (uint32_t flags : {0u, 1u, 2u, 3u}) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            void *o = NewSide(side, 7);
            difftest::FreeRecorder freed;
            void *r = CallSlot<void *>(static_cast<uint8_t *>(o) + 4, 0, flags);
            freed.Stop();
            got[side] = std::string(r == o ? "this" : "other") + (freed.Freed(o) ? " freed" : " " + Words(o));
            if (!freed.Freed(o)) FreeSide(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("deleting destructor flags %u: %s / %s", flags, got[0].c_str(), got[1].c_str());
    }

    // CanonicalPath (slot 7): the out string becomes the path.
    std::mt19937 g(5);
    for (int i = 0; i < 200; ++i) {
        const std::string path = RandomPath(g);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            void *o = NewSide(side, 1);
            Text out("previous text");
            const uint32_t r = CallSlot<uint32_t>(o, Slot(0xb02720), path.c_str(), out.Face()) & 0xff;
            got[side] = std::to_string(r) + " '" + out.value.mString + "' " + Words(o);
            FreeSide(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("CanonicalPath '%s': %s / %s", path.c_str(), got[0].c_str(), got[1].c_str());
    }
}

// ---- AcceptsPath (slot 3) ----

// Paths with every head (drives, UNC, rooted, relative, letters before the colon), '/' and '\\' mixed, "\." and "/." parts,
// empty and long ones: al compared.
DIFF_TEST(FileSource_AcceptsPath, 0xb05490)
{
    Allocators allocators;
    std::mt19937 g(17);
    void *o[2] = {NewOriginal(1), NewOurs(1)};
    for (int i = 0; i < 3000; ++i) {
        const std::string path = RandomPath(g);
        uint32_t r[2];
        for (int side = 0; side < 2; ++side) r[side] = CallSlot<uint32_t>(o[side], Slot(0xb05490), path.c_str()) & 0xff;
        ++ctx.cases;
        if (r[0] != r[1]) ctx.Fail("AcceptsPath '%s': %u / %u", path.c_str(), r[0], r[1]);
    }
    FreeSide(o[0]);
    FreeSide(o[1]);
}

// ---- Exists (slot 4) ----

std::vector<std::string> gLog;
uint8_t gExists = 0;
bool __cdecl StandPathExists(const cIGZString &path)
{
    gLog.push_back("PathExists '" + reinterpret_cast<const cRZString *>(&path)->mString + "'");
    return gExists != 0;
}

// The path handed to PathExists (stand-in over the image's and ours) and its answer passed back: al compared.
DIFF_TEST(FileSource_Exists, 0xb02370)
{
    Allocators allocators;
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    std::mt19937 g(23);
    void *o[2] = {NewOriginal(1), NewOurs(1)};
    for (int i = 0; i < 400; ++i) {
        const std::string path = RandomPath(g);
        gExists = static_cast<uint8_t>(g() % 2);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            const uint32_t r = CallSlot<uint32_t>(o[side], Slot(0xb02370), path.c_str()) & 0xff;
            got[side] = std::to_string(r);
            for (const std::string &l : gLog) got[side] += "; " + l;
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("Exists '%s': %s / %s", path.c_str(), got[0].c_str(), got[1].c_str());
    }
    FreeSide(o[0]);
    FreeSide(o[1]);
}

// ---- ReadFile (slot 5) ----

using cp::FileData;

uint8_t gOpens = 0, gReads = 0;
uint32_t gLength = 0;
uint32_t __fastcall StandOpen(cRZFile *file, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    gLog.push_back("Open '" + file->mPath.mString + "' " + std::to_string(access) + "," + std::to_string(creation) + "," +
                   std::to_string(sharing));
    return gOpens;
}
uint32_t __fastcall StandLength(cRZFile *file, void *)
{
    gLog.push_back("Length '" + file->mPath.mString + "'");
    return gLength;
}
uint32_t __fastcall StandRead(cRZFile *file, void *, void *dst, uint32_t length)
{
    gLog.push_back("Read '" + file->mPath.mString + "' " + std::to_string(length));
    if (length < 0x10000)
        for (uint32_t i = 0; i < length; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(0x41 + (i * 7) % 26);
    return gReads;
}
uint32_t __fastcall StandClose(cRZFile *file, void *)
{
    gLog.push_back("Close '" + file->mPath.mString + "'");
    return 1;
}

// The file opened for reading (the modes logged), the data vector resized to the length plus one over every starting state
// (empty; shorter, equal, longer; capacity enough or not), the read over all but the last byte, the close; failures to open
// and to read; a length of 0xffffffff (the vector emptied, the read's length -1). Compared: al, the calls in order (with the
// file's path), the vector's size, capacity and bytes; every allocator-service block is freed after each case.
DIFF_TEST(FileSource_ReadFile, 0xb035e0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    shape::StandIn open(0x1c285d, &cRZFile::Open, &StandOpen);
    shape::StandIn length(0x1c230b, &cRZFile::Length, &StandLength);
    shape::StandIn read(0x1c2c6e, &cRZFile::Read, &StandRead);
    shape::StandIn close(0x1c0faa, &cRZFile::Close, &StandClose);
    std::mt19937 g(29);
    void *o[2] = {NewOriginal(1), NewOurs(1)};
    for (int i = 0; i < 600; ++i) {
        const std::string path = RandomPath(g);
        gOpens = static_cast<uint8_t>(g() % 5 != 0);
        gReads = static_cast<uint8_t>(g() % 4 != 0);
        const uint32_t lengths[] = {0, 1, 7, 15, 16, 31, 100, 0xffffffff};
        gLength = g() % 3 ? lengths[g() % std::size(lengths)] : g() % 200;
        const size_t startSize = g() % 3 == 0 ? 0 : g() % 120, startCapacity = startSize + (g() % 2 ? g() % 100 : 0);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            {
                FileData data;
                data.reserve(startCapacity);
                for (size_t k = 0; k < startSize; ++k) data.push_back(static_cast<char>('a' + k % 26));
                const uint32_t r = CallSlot<uint32_t>(o[side], Slot(0xb035e0), path.c_str(), &data) & 0xff;
                got[side] = std::to_string(r) + " size " + std::to_string(data.size()) + " cap " + std::to_string(data.capacity()) + " '" +
                            std::string(data.data() ? data.data() : "", data.size()) + "'";
                for (const std::string &l : gLog) got[side] += "; " + l;
            }
            if (gServiceLive != 0) ctx.Fail("ReadFile side %d: %d allocator-service blocks left", side, gServiceLive);
            gServiceLive = 0;
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("ReadFile '%s':\n    A: %s\n    B: %s", path.c_str(), got[0].c_str(), got[1].c_str());
    }
    FreeSide(o[0]);
    FreeSide(o[1]);
}

// ---- ResolvePath (slot 6) and the file name helpers ----

// `path` after the drive and directory of `relativeTo` (absolute ones, relative ones resolved against the current
// directory, UNC, empty): the out string's text compared (it held text before).
DIFF_TEST(FileSource_ResolvePath, 0xb05ec0)
{
    Allocators allocators;
    RealCwd cwd(ctx);
    std::mt19937 g(31);
    void *o[2] = {NewOriginal(1), NewOurs(1)};
    for (int i = 0; i < 1500; ++i) {
        const std::string path = RandomPath(g), base = RandomPath(g);
        if (NeedsOtherDriveDirectory(base)) continue;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            Text out("before");
            CallSlot<void>(o[side], Slot(0xb05ec0), path.c_str(), base.c_str(), out.Face());
            got[side] = out.value.mString;
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("ResolvePath '%s' against '%s': '%s' / '%s'", path.c_str(), base.c_str(), got[0].c_str(), got[1].c_str());
    }
    FreeSide(o[0]);
    FreeSide(o[1]);
}

namespace {

struct Name {
    union {
        cRZFileName value;
    };
    Name() { value.ConstructEmpty(); }
    ~Name() { value.Destruct(); }
    std::string Form() const
    {
        return value.mDrive.mString + "|" + value.mDir.mString + "|" + value.mName.mString + "|" + value.mExt.mString + "|" +
               value.mFull.mString;
    }
};

} // namespace

// 0x1c8789 (the full path resolved from a path, then split) and 0x1c8beb (the split alone, nothing when the full path is
// empty), at their RVAs against ours, on file names holding junk parts: the five strings compared.
DIFF_TEST(FileSource_SetFileNamePath, 0x1c8789)
{
    Allocators allocators;
    RealCwd cwd(ctx);
    std::mt19937 g(37);
    for (int i = 0; i < 1500; ++i) {
        const std::string path = RandomPath(g), junk = RandomPath(g);
        const bool splitOnly = g() % 3 == 0;
        if (!splitOnly && NeedsOtherDriveDirectory(path)) continue;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            Name n;
            n.value.mDrive.mString = junk;
            n.value.mName.mString = "old";
            n.value.mExt.mString = ".x";
            if (splitOnly) {
                n.value.mFull.mString = path;
                if (side == 0)
                    ctx.Original<void(__thiscall *)(cRZFileName *)>(0x1c8beb)(&n.value);
                else
                    cp::cFileSourceAdapters::SplitFileNamePath(&n.value, nullptr);
            } else {
                Text text(path.c_str());
                if (side == 0)
                    ctx.Original<void(__thiscall *)(cRZFileName *, const cRZString *)>(0x1c8789)(&n.value, &text.value);
                else
                    cp::cFileSourceAdapters::SetFileNamePath(&n.value, nullptr, &text.value);
            }
            got[side] = n.Form();
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("%s '%s': '%s' / '%s'", splitOnly ? "Split" : "SetPath", path.c_str(), got[0].c_str(), got[1].c_str());
    }
}

// ---- the adapters ----

namespace {

int gTrapHits = 0;
uint32_t TrapHit(void *, unsigned) { return uint32_t(++gTrapHits); }

// A game-built source whose two vtables are copies with every slot a trap of its arity (an adapter that dispatched
// virtually lands there).
struct Trapped {
    void *face0[8];
    void *face1[5];
    Trapped()
    {
        const unsigned words0[] = {2, 0, 0, 1, 1, 2, 3, 2};
        const unsigned words1[] = {1, 0, 0, 0, 0};
        for (unsigned s = 0; s < 8; ++s) face0[s] = shape::TrapFor<&TrapHit>(words0[s]);
        for (unsigned s = 0; s < 5; ++s) face1[s] = shape::TrapFor<&TrapHit>(words1[s]);
    }
    void *Make(uint32_t count)
    {
        auto *w = static_cast<uint32_t *>(NewOriginal(count));
        w[0] = P(face0);
        w[1] = P(face1);
        return w;
    }
};

} // namespace

// Each hook entry (the adapters of 0xaff0a0, 0xb05490, 0xb02370, 0xb035e0, 0xb05ec0, 0xb02720) on a game-built source
// with trap vtables against the original at its RVA on a game-built source: results, outputs and the words left compared,
// no trap hit. PathExists and the file are stand-ins as above.
DIFF_TEST(FileSource_Adapters, 0xaff0a0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn open(0x1c285d, &cRZFile::Open, &StandOpen);
    shape::StandIn length(0x1c230b, &cRZFile::Length, &StandLength);
    shape::StandIn read(0x1c2c6e, &cRZFile::Read, &StandRead);
    shape::StandIn close(0x1c0faa, &cRZFile::Close, &StandClose);
    using A = cp::cFileSourceAdapters;
    RealCwd cwd(ctx);
    Trapped traps;
    std::mt19937 g(41);
    for (int i = 0; i < 300; ++i) {
        const std::string path = RandomPath(g), base = RandomPath(g);
        if (NeedsOtherDriveDirectory(base)) continue;
        gExists = static_cast<uint8_t>(g() % 2);
        gOpens = static_cast<uint8_t>(g() % 4 != 0);
        gReads = static_cast<uint8_t>(g() % 2);
        gLength = g() % 50;
        const uint32_t which = i % 6, flags = g() % 2;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gTrapHits = 0;
            void *o = side == 0 ? NewOriginal(3) : traps.Make(3);
            auto *self = static_cast<cp::cFileSource *>(static_cast<cp::cIParserSource *>(o));
            Text out("before");
            FileData data;
            bool freedObject = false;
            uint32_t r = 0;
            switch (which) {
            case 0: {
                difftest::FreeRecorder freed;
                void *back = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xaff0a0)(o, flags) : A::DeletingDestructor(self, nullptr, flags);
                freed.Stop();
                freedObject = freed.Freed(o);
                r = back == o;
                break;
            }
            case 1: r = (side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const char *)>(0xb05490)(o, path.c_str()) : A::AcceptsPath(self, nullptr, path.c_str())) & 0xff; break;
            case 2: r = (side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const char *)>(0xb02370)(o, path.c_str()) : A::Exists(self, nullptr, path.c_str())) & 0xff; break;
            case 3: r = (side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const char *, FileData *)>(0xb035e0)(o, path.c_str(), &data) : A::ReadFile(self, nullptr, path.c_str(), &data)) & 0xff; break;
            case 4:
                if (side == 0) ctx.Original<void(__thiscall *)(void *, const char *, const char *, cIGZString *)>(0xb05ec0)(o, path.c_str(), base.c_str(), out.Face());
                else A::ResolvePath(self, nullptr, path.c_str(), base.c_str(), out.Face());
                break;
            default: r = (side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const char *, cIGZString *)>(0xb02720)(o, path.c_str(), out.Face()) : A::CanonicalPath(self, nullptr, path.c_str(), out.Face())) & 0xff; break;
            }
            // The words left: the trap copies stand for the game's vtables on our side.
            std::string words = "freed";
            if (!freedObject) {
                uint32_t w[3];
                std::memcpy(w, o, 12);
                if (w[0] == P(traps.face0)) w[0] = P(GameAddress<void>(kGameVTable));
                if (w[1] == P(traps.face1)) w[1] = P(GameAddress<void>(kGameRefCountVTable));
                words = Code(w[0]) + "," + Code(w[1]) + "," + Code(w[2]);
                FreeSide(o);
            }
            got[side] = std::to_string(r) + " " + words + " out '" + out.value.mString + "' data " + std::to_string(data.size()) + "/" +
                        std::string(data.data() ? data.data() : "", data.size()) + " traps " + std::to_string(gTrapHits);
            for (const std::string &l : gLog) got[side] += "; " + l;
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("adapter %u '%s' '%s':\n    A: %s\n    B: %s", which, path.c_str(), base.c_str(), got[0].c_str(), got[1].c_str());
    }
}
