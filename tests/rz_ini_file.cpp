// Differential tests for cRZIniFile slots 2 (SetPath), 4 (SetFile), 7 (GetLong), 11 (SetLong) and
// 16 (SetEnabled). Two objects with identical state: one carries the original vtable, the other a
// copy whose reimplemented slots point at our functions. The slots the reimplementations call
// through the vtable (5 Clear, 6 GetString, 10 SetString, 17 load) stay the game's on both sides.
//
// The object's file is a real cRZFile on a sandboxed file, so loading and saving the ini run the
// original's file code against the sandbox. Each side has its own copy of the same ini text; after
// every operation the return values (bool in al), the path, flags, the table's size, the padding
// bytes, which file the object holds and the files' state must match. Once the sequence ends the
// two files must be byte-identical.
#include "difftest.h"
#include "game/vtable.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kSequences = 600;
constexpr int kSteps = 12;
constexpr int kSlots = 20;
// The object's full size is not known; its padding is zeroed, which is a valid empty state.
constexpr size_t kObjectBytes = 0x100;

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// The original vtable with the slots we reimplement pointing at our functions.
struct OurVTable {
    void *slots[kSlots];
    explicit OurVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZIniFile::kVTable), sizeof(slots));
        slots[2] = Addr(&cRZIniFile::SetPath);
        slots[4] = Addr(&cRZIniFile::SetFile);
        slots[7] = Addr(&cRZIniFile::GetLong);
        slots[11] = Addr(&cRZIniFile::SetLong);
        slots[16] = Addr(&cRZIniFile::SetEnabled);
    }
};

using UintThiscall0 = uint32_t(__thiscall *)(void *);
using PathFn = void(__thiscall *)(void *, const cIGZString *);
using SetFileFn = bool(__thiscall *)(void *, void *);
using GetLongFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, int32_t *);
using SetLongFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, int32_t);
using EnabledFn = bool(__thiscall *)(void *, uint32_t);

// The table header is an empty MSVC tree head. The original's own code adds its nodes, so the
// element type is never needed here.
using TableHeader = std::map<uint32_t, uint32_t>;
static_assert(sizeof(TableHeader) == sizeof(cRZIniFile::mEntries), "table header size");

std::string ToUtf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool WriteFileBytes(const std::wstring &path, const std::string &text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(h);
    return ok;
}

std::string ReadFileBytes(const std::wstring &path)
{
    std::string text;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "<missing>";
    char buf[512];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got) text.append(buf, got);
    CloseHandle(h);
    return text;
}

cRZIniFile *MakeIni(const void *vtable, const std::string &path, bool enabled, bool loaded, void *file)
{
    auto *p = static_cast<cRZIniFile *>(::operator new(kObjectBytes));
    std::memset(p, 0, kObjectBytes);
    p->mVTable = vtable;
    p->mPath.Construct();
    p->mPath.mString = path;
    p->mEnabled = enabled;
    p->mLoaded = loaded;
    new (static_cast<void *>(p->mEntries)) TableHeader();
    new (static_cast<void *>(p->mSecondTable)) TableHeader();
    p->mFile = file;
    return p;
}

// Frees the object. The original's Clear (slot 5) destroys whatever the original put in the table
// and uses the file, so the file must still be alive; the sentinel head is then ours to free.
void FreeIni(cRZIniFile *p)
{
    using ClearFn = bool(__thiscall *)(void *);
    VirtualSlot<ClearFn>(p, 5)(p);
    ::operator delete(*reinterpret_cast<void **>(p->mEntries));
    ::operator delete(*reinterpret_cast<void **>(p->mSecondTable));
    p->mPath.Destruct();
    ::operator delete(p);
}

uint32_t TreeSize(const uint8_t *header)
{
    uint32_t size;
    std::memcpy(&size, header + 4, sizeof(size));
    return size;
}

// Two sandbox paths of the two sides match when they differ only in the leading letter of the name
// ("A_P1.ini" and "B_P1.ini"); the last five characters are compared.
bool SameName(const std::string &pa, const std::string &pb)
{
    return pa.size() == pb.size() && (pa.size() < 5 || pa.compare(pa.size() - 5, 5, pb, pb.size() - 5, 5) == 0);
}

std::string Describe(const cRZIniFile &p)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "path '%s' enabled %d loaded %d tables %u/%u file %p", p.mPath.mString.c_str(), p.mEnabled,
                  p.mLoaded, TreeSize(p.mEntries), TreeSize(p.mSecondTable), p.mFile);
    return buf;
}

std::string Describe(const cRZFile &f)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "'%s' open %d refs %u access %u error 0x%x pos %d fpos %d rbuf %u/%u wbuf %u/%u", f.mPath.mString.c_str(),
                  f.mOpen, f.mRefCount, f.mAccess, f.mError, f.mPosition, f.mFilePosition, f.mReadBufferSize, f.mReadBufferLength,
                  f.mWriteBufferSize, f.mWriteBufferLength);
    return buf;
}

bool SameIni(const cRZIniFile &a, const cRZIniFile &b)
{
    return SameName(a.mPath.mString, b.mPath.mString) && a.mEnabled == b.mEnabled && a.mLoaded == b.mLoaded
        && TreeSize(a.mEntries) == TreeSize(b.mEntries) && TreeSize(a.mSecondTable) == TreeSize(b.mSecondTable);
}

// The state a cRZFile exposes: reference count, open state, path, positions and buffers.
bool SameFile(const cRZFile &a, const cRZFile &b)
{
    return a.mRefCount == b.mRefCount && a.mOpen == b.mOpen && SameName(a.mPath.mString, b.mPath.mString)
        && a.mAccess == b.mAccess && a.mCreation == b.mCreation && a.mSharing == b.mSharing && a.mError == b.mError
        && a.mCachedLength == b.mCachedLength && a.mLengthCached == b.mLengthCached && a.mPosition == b.mPosition
        && a.mFilePosition == b.mFilePosition && a.mReadBufferSize == b.mReadBufferSize
        && a.mReadBufferStart == b.mReadBufferStart && a.mReadBufferLength == b.mReadBufferLength
        && a.mWriteBufferSize == b.mWriteBufferSize && a.mWriteBufferStart == b.mWriteBufferStart
        && a.mWriteBufferLength == b.mWriteBufferLength && a.mReadBuffer == b.mReadBuffer
        && a.mWriteBuffer == b.mWriteBuffer && (a.mHandle == INVALID_HANDLE_VALUE) == (b.mHandle == INVALID_HANDLE_VALUE);
}

// Includes empty and blank values: an empty string makes sscanf return EOF (-1), not 0.
const char *const kIniText[] = {"[Sims]\r\n", "Width=12\r\n", "Height=-3\r\n", "[Options]\r\n", "Volume=2147483647\r\n",
                                "k=0\r\n", "Volume=abc\r\n", "[x]\r\n", "k=  7\r\n", "junk line\r\n", "Width=\r\n",
                                "Height=   \r\n", "Volume=\r\n", "k=\r\n"};

std::string RandomIniText(std::mt19937 &rng)
{
    std::string text;
    int n = static_cast<int>(rng() % 12);
    for (int i = 0; i < n; ++i) text += kIniText[rng() % (sizeof(kIniText) / sizeof(kIniText[0]))];
    return text;
}

// cRZString arguments (cIGZString layout) with a given text.
struct Arg {
    cRZString text;
    explicit Arg(const std::string &s)
    {
        text.Construct();
        text.mString = s;
    }
    ~Arg() { text.Destruct(); }
    const cIGZString *get() const { return text.AsInterface(); }
};

const std::vector<std::string> kSections = {"", "Sims", "Options", "x"};
const std::vector<std::string> kKeys = {"", "Width", "Volume", "k"};

// The paths the ini objects are given, one list per side. They are inside the sandbox, because the
// original opens the file at its path when it loads (outside the sandbox, the import trap would
// fire), and each side needs its own (the original's load moves its file to the object's path).
std::vector<std::string> gPathsA, gPathsB;

const std::string &Pick(std::mt19937 &rng, const std::vector<std::string> &pool)
{
    return pool[rng() % pool.size()];
}

// Two files per side, each a real cRZFile. The test keeps one reference on each, and the object
// holds another on whichever it is using.
struct Side {
    cRZIniFile *ini;
    cRZFile *file[2];
};

cRZFile *OpenFile(const std::wstring &name)
{
    cRZFile *f = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
    f->Construct(ToUtf8(difftest::SandboxPath(name)).c_str());
    f->AddRef();
    return f;
}

// One random sequence of operations on a pair of objects.
void Sequence(difftest::Context &ctx, std::mt19937 &rng, const OurVTable &ours, int &hits)
{
    auto origPath = ctx.Original<PathFn>(0x9c6b06);
    auto origSetFile = ctx.Original<SetFileFn>(0x9c6a73);
    auto origGetLong = ctx.Original<GetLongFn>(0x9c69ab);
    auto origSetLong = ctx.Original<SetLongFn>(0x9c75f5);
    auto origEnabled = ctx.Original<EnabledFn>(0x9c6aec);

    // Each side gets its own copy of the same ini text (side A: "A1"/"A2", side B: "B1"/"B2").
    std::string text1 = RandomIniText(rng), text2 = RandomIniText(rng);
    if (!WriteFileBytes(difftest::SandboxPath(L"A1.ini"), text1) || !WriteFileBytes(difftest::SandboxPath(L"A2.ini"), text2)
        || !WriteFileBytes(difftest::SandboxPath(L"B1.ini"), text1) || !WriteFileBytes(difftest::SandboxPath(L"B2.ini"), text2))
        ctx.Fail("cannot write the sandbox files (one is still open?)");

    Side a{nullptr, {OpenFile(L"A1.ini"), OpenFile(L"A2.ini")}};
    Side b{nullptr, {OpenFile(L"B1.ini"), OpenFile(L"B2.ini")}};

    bool enabled = rng() & 1, loaded = rng() & 1;
    size_t pathIndex = rng() % gPathsA.size();
    // The object holds file 0 (AddRef'd for the object) on both sides.
    VirtualSlot<UintThiscall0>(a.file[0], 1)(a.file[0]);
    VirtualSlot<UintThiscall0>(b.file[0], 1)(b.file[0]);
    a.ini = MakeIni(ctx.image.At(cRZIniFile::kVTable), gPathsA[pathIndex], enabled, loaded, a.file[0]);
    b.ini = MakeIni(ours.slots, gPathsB[pathIndex], enabled, loaded, b.file[0]);

    for (int step = 0; step < kSteps; ++step) {
        int op = static_cast<int>(rng() % 5);
        ++ctx.cases;
        switch (op) {
        case 0: { // SetPath
            pathIndex = rng() % gPathsA.size();
            Arg argA(gPathsA[pathIndex]), argB(gPathsB[pathIndex]);
            origPath(a.ini, argA.get());
            b.ini->SetPath(*argB.get());
            break;
        }
        case 1: { // SetFile: file 0 or file 1 (the object's reference moves between them)
            int choice = static_cast<int>(rng() % 2);
            bool ra = origSetFile(a.ini, a.file[choice]);
            bool rb = b.ini->SetFile(b.file[choice]);
            if (ra != rb) ctx.Fail("SetFile choice %d: returns %d vs %d", choice, ra, rb);
            break;
        }
        case 2: { // SetEnabled
            bool flag = rng() & 1;
            bool ra = origEnabled(a.ini, flag ? 1u : 0u);
            bool rb = b.ini->SetEnabled(flag);
            if (ra != rb) ctx.Fail("SetEnabled(%d): returns %d vs %d", flag, ra, rb);
            break;
        }
        case 3: { // SetLong
            Arg section(Pick(rng, kSections)), key(Pick(rng, kKeys));
            int32_t value = static_cast<int32_t>(rng() % 7) - 3;
            if (rng() % 8 == 0) value = static_cast<int32_t>(rng());
            bool ra = origSetLong(a.ini, section.get(), key.get(), value);
            bool rb = b.ini->SetLong(*section.get(), *key.get(), value);
            if (ra != rb) ctx.Fail("SetLong(%d): returns %d vs %d", value, ra, rb);
            break;
        }
        case 4: { // GetLong
            Arg section(Pick(rng, kSections)), key(Pick(rng, kKeys));
            int32_t va = 0x5a5a5a5a, vb = 0x5a5a5a5a;
            bool ra = origGetLong(a.ini, section.get(), key.get(), &va);
            bool rb = b.ini->GetLong(*section.get(), *key.get(), &vb);
            if (ra != rb) ctx.Fail("GetLong: returns %d vs %d", ra, rb);
            if (va != vb) ctx.Fail("GetLong: value %d vs %d", va, vb);
            if (ra) ++hits;
            break;
        }
        }
        if (!SameIni(*a.ini, *b.ini)) ctx.Fail("op %d: object state differs: %s | %s", op, Describe(*a.ini).c_str(), Describe(*b.ini).c_str());
        for (int k = 0; k < 2; ++k)
            if (!SameFile(*a.file[k], *b.file[k])) ctx.Fail("op %d: file %d state differs: %s | %s", op, k, Describe(*a.file[k]).c_str(), Describe(*b.file[k]).c_str());
        int ka = a.ini->mFile == a.file[0] ? 0 : a.ini->mFile == a.file[1] ? 1 : 2;
        int kb = b.ini->mFile == b.file[0] ? 0 : b.ini->mFile == b.file[1] ? 1 : 2;
        if (ka != kb) ctx.Fail("op %d: holds file %d vs %d", op, ka, kb);
    }

    // The object's reference goes first (its Clear uses the file), then ours on each file.
    void *heldA = a.ini->mFile, *heldB = b.ini->mFile;
    FreeIni(a.ini);
    FreeIni(b.ini);
    VirtualSlot<UintThiscall0>(heldA, 2)(heldA);
    VirtualSlot<UintThiscall0>(heldB, 2)(heldB);
    for (int k = 0; k < 2; ++k) {
        VirtualSlot<UintThiscall0>(a.file[k], 2)(a.file[k]);
        VirtualSlot<UintThiscall0>(b.file[k], 2)(b.file[k]);
    }
    if (ReadFileBytes(difftest::SandboxPath(L"A1.ini")) != ReadFileBytes(difftest::SandboxPath(L"B1.ini")) || ReadFileBytes(difftest::SandboxPath(L"A2.ini")) != ReadFileBytes(difftest::SandboxPath(L"B2.ini")))
        ctx.Fail("file contents differ after the sequence");
}

// SetFile(null) makes the object a cRZFile on its own path. Both sides must make equal files.
void CreatedFileTest(difftest::Context &ctx, std::mt19937 &rng, const OurVTable &ours)
{
    auto origSetFile = ctx.Original<SetFileFn>(0x9c6a73);
    for (int i = 0; i < 50; ++i) {
        size_t pathIndex = rng() % gPathsA.size();
        WriteFileBytes(difftest::SandboxPath(L"A1.ini"), "");
        WriteFileBytes(difftest::SandboxPath(L"B1.ini"), "");
        cRZFile *fa = OpenFile(L"A1.ini"), *fb = OpenFile(L"B1.ini");
        // The object takes its own reference, so the original's Release leaves the test's alive.
        VirtualSlot<UintThiscall0>(fa, 1)(fa);
        VirtualSlot<UintThiscall0>(fb, 1)(fb);
        cRZIniFile *a = MakeIni(ctx.image.At(cRZIniFile::kVTable), gPathsA[pathIndex], false, false, fa);
        cRZIniFile *b = MakeIni(ours.slots, gPathsB[pathIndex], false, false, fb);
        bool ra = origSetFile(a, nullptr);
        bool rb = b->SetFile(nullptr);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("SetFile(null): returns %d vs %d", ra, rb);
        if (!SameIni(*a, *b)) ctx.Fail("SetFile(null): object state differs");
        if (a->mFile == fa || b->mFile == fb || a->mFile == nullptr || b->mFile == nullptr) ctx.Fail("SetFile(null): did not make a new file");
        if (!SameFile(*static_cast<cRZFile *>(a->mFile), *static_cast<cRZFile *>(b->mFile))) ctx.Fail("SetFile(null): created cRZFile differs");
        void *createdA = a->mFile, *createdB = b->mFile;
        FreeIni(a);
        FreeIni(b);
        VirtualSlot<UintThiscall0>(createdA, 2)(createdA);
        VirtualSlot<UintThiscall0>(createdB, 2)(createdB);
        VirtualSlot<UintThiscall0>(fa, 2)(fa);
        VirtualSlot<UintThiscall0>(fb, 2)(fb);
    }
}

} // namespace

// The game fills these globals with its wide file functions at start-up (as on NT); the sandbox
// provides the same functions, so the original's file code runs against the sandbox.
const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

DIFF_TEST(RZIniFile_All, 0x9c6b06)
{
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = difftest::SandboxFunction(kWideNames[i]);
    // The object's path is one of its two files, so loads read the ini text the sequence wrote.
    gPathsA = {ToUtf8(difftest::SandboxPath(L"A1.ini")), ToUtf8(difftest::SandboxPath(L"A2.ini"))};
    gPathsB = {ToUtf8(difftest::SandboxPath(L"B1.ini")), ToUtf8(difftest::SandboxPath(L"B2.ini"))};
    std::mt19937 rng(9);
    OurVTable ours(ctx);
    int hits = 0;
    for (int i = 0; i < kSequences; ++i) Sequence(ctx, rng, ours, hits);
    CreatedFileTest(ctx, rng, ours);
    // Successful lookups reach the sscanf branch; there were about 330 of them in a run.
    // Successful lookups reach the sscanf branch, and empty values its EOF case; several dozen per run.
    if (hits < 50) ctx.Fail("GetLong: only %d successful lookups; the conversion is barely tested", hits);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
