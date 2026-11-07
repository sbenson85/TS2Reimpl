// Differential tests for cRZTextFile: construction, the two destructors (plain and deleting), and
// Open with its byte-order-mark check. Each case gives the original and the reimplementation
// identical objects; the reimplementation's objects carry a vtable copy whose Open, deleting
// destructor, Length, Read and SeekToPosition slots point at our functions, so Open's calls through
// the vtable run our code on that side. Strings are long enough to own heap blocks, and frees are
// observed per block right after each side's call.
#include "difftest.h"
#include "free_scope.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kSlots = 34;

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

using SetPathChar = void (cRZFile::*)(const char *);
using SetPathString = void (cRZFile::*)(const cIGZString &);
using GetPathString = void (cRZFile::*)(cIGZString &) const;
using CopyFile = bool (cRZFile::*)(const void *, uint8_t);
using CopyPath = bool (cRZFile::*)(const cIGZString &, uint8_t);

// The original cRZTextFile vtable with every slot we reimplement pointing at our function: the
// text file's Open and deleting destructor, and the cRZFile slots (Open reads through them, and
// handles opened in ANSI mode come from the real CreateFileA, not the sandbox, so original code
// must not touch them).
struct OurTextVTable {
    void *slots[kSlots];
    explicit OurTextVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZTextFile::kVTable), sizeof(slots));
        slots[0] = Addr(&cRZFile::QueryInterface);
        slots[1] = Addr(&cRZFile::AddRef);
        slots[2] = Addr(&cRZFile::Release);
        slots[3] = Addr(&cRZTextFile::Open);
        slots[5] = Addr(&cRZFile::Close);
        slots[6] = Addr(&cRZFile::Position);
        slots[7] = Addr(&cRZFile::Length);
        slots[8] = Addr(&cRZFile::SetLength);
        slots[9] = Addr(&cRZFile::SeekToBegin);
        slots[10] = Addr(&cRZFile::SeekToEnd);
        slots[11] = Addr(&cRZFile::SeekToRelativePosition);
        slots[12] = Addr(&cRZFile::SeekToPosition);
        slots[13] = Addr(&cRZFile::Seek);
        slots[14] = Addr(&cRZFile::Read);
        slots[15] = Addr(&cRZFile::ReadWithCount);
        slots[16] = Addr(&cRZFile::Write);
        slots[17] = Addr(&cRZFile::WriteWithCount);
        slots[18] = Addr(&cRZFile::Flush);
        slots[19] = Addr(&cRZFile::SysFlush);
        slots[20] = Addr(&cRZFile::Remove);
        slots[21] = Addr(&cRZFile::Rename);
        slots[22] = Addr(&cRZFile::Exists);
        slots[23] = Addr(static_cast<CopyFile>(&cRZFile::Copy));
        slots[24] = Addr(static_cast<CopyPath>(&cRZFile::Copy));
        slots[26] = Addr(static_cast<GetPathString>(&cRZFile::GetPath));
        slots[28] = Addr(static_cast<SetPathChar>(&cRZFile::SetPath));
        slots[29] = Addr(static_cast<SetPathString>(&cRZFile::SetPath));
        slots[30] = Addr(&cRZFile::GetOpenModes);
        slots[31] = Addr(&cRZFile::GetBuffering);
        slots[32] = Addr(&cRZFile::SetBuffering);
        slots[33] = Addr(&cRZTextFile::DeletingDestructor);
    }
};

template <typename Fn> Fn Slot(const cRZFile *f, int slot)
{
    return reinterpret_cast<Fn>(static_cast<void *const *>(f->mVTable)[slot]);
}

// The file system the cRZFile tests use for wide paths (see tests/rz_file.cpp).
const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

void SetWideMode(difftest::Context &ctx, bool wide)
{
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = wide ? difftest::SandboxFunction(kWideNames[i]) : nullptr;
}

std::string Utf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}

void WriteFileBytes(const std::wstring &path, const std::vector<char> &bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    if (!bytes.empty()) WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
}

std::vector<char> FileBytes(const std::wstring &path)
{
    std::vector<char> bytes;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return bytes;
    DWORD size = GetFileSize(h, nullptr), read = 0;
    bytes.resize(size);
    if (size) ReadFile(h, bytes.data(), size, &read, nullptr);
    CloseHandle(h);
    bytes.resize(read);
    return bytes;
}

// File contents for an Open test: empty, one byte, two-byte values around the byte-order marks, and
// marks not at the start, with random tails.
std::vector<char> MakeContent(std::mt19937 &rng)
{
    std::vector<char> v;
    auto tail = [&](uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) v.push_back(static_cast<char>(rng()));
    };
    switch (rng() % 10) {
    case 0: break;
    case 1: v.push_back(static_cast<char>(rng())); break;
    case 2: v = {'\xfe', '\xff'}; tail(rng() % 300); break;
    case 3: v = {'\xff', '\xfe'}; tail(rng() % 300); break;
    case 4: v = {static_cast<char>(rng() % 2 ? 0xff : 0xfe), static_cast<char>(rng() % 2 ? 0xff : 0xfe)}; tail(rng() % 40); break;
    case 5: v = {'x', '\xfe', '\xff'}; tail(rng() % 40); break;
    case 6: v = {'\xff', '\xfe', '\xfe', '\xff'}; tail(rng() % 40); break;
    case 7: tail(rng() % 3); break;
    case 8: tail(rng() % 40); break;
    default: tail(rng() % 9000); break;
    }
    return v;
}

// Everything observable about an object apart from its vtable, handle value and path.
bool SameOpenState(const cRZTextFile &a, const cRZTextFile &b)
{
    return a.mCharSize == b.mCharSize && a.mByteOrderMark == b.mByteOrderMark
        && a.mOpen == b.mOpen && a.mLengthCached == b.mLengthCached
        && (a.mHandle == INVALID_HANDLE_VALUE) == (b.mHandle == INVALID_HANDLE_VALUE)
        && a.mAccess == b.mAccess && a.mCreation == b.mCreation && a.mSharing == b.mSharing
        && a.mRefCount == b.mRefCount && a.mError == b.mError && a.mCachedLength == b.mCachedLength
        && a.mPosition == b.mPosition && a.mFilePosition == b.mFilePosition
        && a.mReadBufferSize == b.mReadBufferSize && a.mReadBuffer == b.mReadBuffer
        && a.mReadBuffer.capacity() == b.mReadBuffer.capacity()
        && a.mReadBufferStart == b.mReadBufferStart && a.mReadBufferLength == b.mReadBufferLength
        && a.mWriteBufferSize == b.mWriteBufferSize && a.mWriteBuffer == b.mWriteBuffer
        && a.mWriteBuffer.capacity() == b.mWriteBuffer.capacity()
        && a.mWriteBufferStart == b.mWriteBufferStart && a.mWriteBufferLength == b.mWriteBufferLength
        && a.mLine.size() == b.mLine.size() && a.mWide.size() == b.mWide.size();
}

std::string Describe(const cRZTextFile &f)
{
    char buf[200];
    std::snprintf(buf, sizeof(buf), "open %d char %d bom %d access %u creation %u sharing %u error 0x%x pos %d fpos %d",
                  f.mOpen, f.mCharSize, f.mByteOrderMark, f.mAccess, f.mCreation, f.mSharing, f.mError, f.mPosition, f.mFilePosition);
    return buf;
}

// The state a destructor leaves in the strings and the text cRZString: size, capacity and the first
// byte of each buffer (the rest of a freed heap pointer is not comparable between the two sides).
bool SameDestroyedState(const cRZTextFile &a, const cRZTextFile &b)
{
    return a.mVTable == b.mVTable && a.mCharSize == b.mCharSize && a.mByteOrderMark == b.mByteOrderMark
        && a.mText.mVTable == b.mText.mVTable && a.mText.mRefCount == b.mText.mRefCount
        && a.mText.mString.size() == b.mText.mString.size() && a.mText.mString.capacity() == b.mText.mString.capacity()
        && a.mLine.size() == b.mLine.size() && a.mLine.capacity() == b.mLine.capacity()
        && a.mWide.size() == b.mWide.size() && a.mWide.capacity() == b.mWide.capacity()
        && reinterpret_cast<const unsigned char *>(&a.mLine)[0] == reinterpret_cast<const unsigned char *>(&b.mLine)[0]
        && reinterpret_cast<const unsigned char *>(&a.mWide)[0] == reinterpret_cast<const unsigned char *>(&b.mWide)[0];
}

bool Live(const void *p)
{
    return !difftest::FreedSinceBegin(p); // the call under test ran after difftest::BeginFrees()
}

std::string LongText(std::mt19937 &rng, uint32_t n)
{
    std::string s(n, 'a');
    for (char &c : s) c = static_cast<char>('a' + rng() % 26);
    return s;
}

} // namespace

DIFF_TEST(cRZTextFile_Construct, 0x1c3ec4)
{
    auto origCtor = ctx.Original<cRZTextFile *(__thiscall *)(void *, const char *)>(0x1c3ec4);
    const char *paths[] = {"", "x", "C:\\some\\path\\file.package", "a much longer path that does not fit in the small string buffer.bin"};
    for (const char *path : paths) {
        auto *a = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        auto *b = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        std::memset(a, 0xCD, sizeof(cRZTextFile));
        std::memset(b, 0xCD, sizeof(cRZTextFile));
        cRZTextFile *ra = origCtor(a, path);
        cRZTextFile *rb = b->Construct(path);
        ++ctx.cases;
        // The path's heap block (long paths) is allocated separately on each side, so its pointer
        // differs; everything else, including the padding after the bool at 0xa0, must match.
        bool same = ra == a && rb == b && a->mVTable == b->mVTable && a->mPath.mVTable == b->mPath.mVTable
            && a->mPath.mRefCount == b->mPath.mRefCount && a->mPath.mString == b->mPath.mString
            && std::memcmp(reinterpret_cast<const char *>(a) + 0x24, reinterpret_cast<const char *>(b) + 0x24,
                           sizeof(cRZTextFile) - 0x24) == 0;
        if (!same) ctx.Fail("constructor('%s') differs (return %d)", path, ra == a && rb == b);
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(cRZTextFile_ConstructString, 0x1c3e4a)
{
    auto origCtor = ctx.Original<cRZTextFile *(__thiscall *)(void *, const cIGZString &)>(0x1c3e4a);
    const char *paths[] = {"", "x", "C:\\some\\path\\file.package", "a much longer path that does not fit in the small string buffer.bin"};
    for (const char *path : paths) {
        cRZString name;
        name.Construct();
        name.FromChar(path);
        auto *a = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        auto *b = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        std::memset(a, 0xCD, sizeof(cRZTextFile));
        std::memset(b, 0xCD, sizeof(cRZTextFile));
        cRZTextFile *ra = origCtor(a, *name.AsInterface());
        cRZTextFile *rb = b->Construct(*name.AsInterface());
        ++ctx.cases;
        bool same = ra == a && rb == b && a->mVTable == b->mVTable && a->mPath.mVTable == b->mPath.mVTable
            && a->mPath.mRefCount == b->mPath.mRefCount && a->mPath.mString == b->mPath.mString
            && std::memcmp(reinterpret_cast<const char *>(a) + 0x24, reinterpret_cast<const char *>(b) + 0x24,
                           sizeof(cRZTextFile) - 0x24) == 0;
        if (!same) ctx.Fail("constructor(string '%s') differs (return %d)", path, ra == a && rb == b);
        ::operator delete(a);
        ::operator delete(b);
        name.Destruct();
    }
}

DIFF_TEST(cRZTextFile_Destruct, 0x1bab55)
{
    auto origDeleting = ctx.Original<cRZTextFile *(__thiscall *)(void *, uint32_t)>(0x1c3ff8);
    auto origDtor = ctx.Original<void(__thiscall *)(void *)>(0x1bab55);
    auto ctorOrig = ctx.Original<cRZTextFile *(__thiscall *)(void *, const char *)>(0x1c3ec4);
    std::mt19937 rng(0xDE57);
    for (uint32_t run = 0; run < 120; ++run) {
        auto *a = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        auto *b = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        std::memset(a, 0xCD, sizeof(cRZTextFile));
        std::memset(b, 0xCD, sizeof(cRZTextFile));
        std::string path = "t" + std::to_string(run) + ".bin";
        ctorOrig(a, path.c_str());
        b->Construct(path.c_str());

        // Long strings on both sides (heap blocks), and short ones.
        uint32_t lineLength = run % 3 == 0 ? 0 : run % 2 ? 1 + rng() % 15 : 16 + rng() % 300;
        std::string line = LongText(rng, lineLength);
        std::u16string wide(run % 4 == 0 ? 0 : 8 + rng() % 200, u'w');
        a->mLine.assign(line);
        b->mLine.assign(line);
        a->mWide.assign(wide);
        b->mWide.assign(wide);
        // The text cRZString's std::string also owns a heap block when long.
        std::string text(40 + rng() % 100, 't');
        a->mText.mString.assign(text);
        b->mText.mString.assign(text);
        const void *textBlock = a->mText.mString.capacity() > 15 ? a->mText.mString.data() : nullptr;
        const void *textBlockB = b->mText.mString.capacity() > 15 ? b->mText.mString.data() : nullptr;
        a->mCharSize = b->mCharSize = static_cast<int32_t>(run % 3) * 2;
        a->mByteOrderMark = b->mByteOrderMark = run % 2 == 0;

        const void *lineBlock = a->mLine.capacity() > 15 ? a->mLine.data() : nullptr;
        const void *wideBlock = a->mWide.capacity() > 7 ? a->mWide.data() : nullptr;
        const void *lineBlockB = b->mLine.capacity() > 15 ? b->mLine.data() : nullptr;
        const void *wideBlockB = b->mWide.capacity() > 7 ? b->mWide.data() : nullptr;

        // Plain destructor, then the same with flags 0 through the deleting destructor.
        bool deleting = run % 2 == 1;
        difftest::BeginFrees();
        if (deleting) {
            origDeleting(a, 0);
        } else {
            origDtor(a);
        }
        bool aLineFreed = lineBlock && !Live(lineBlock), aWideFreed = wideBlock && !Live(wideBlock);
        bool aTextFreed = textBlock && !Live(textBlock);
        difftest::BeginFrees();
        if (deleting) b->DeletingDestructor(0);
        else b->Destruct();
        bool bLineFreed = lineBlockB && !Live(lineBlockB), bWideFreed = wideBlockB && !Live(wideBlockB);
        bool bTextFreed = textBlockB && !Live(textBlockB);
        ++ctx.cases;
        if (aLineFreed != bLineFreed || aWideFreed != bWideFreed || aTextFreed != bTextFreed) {
            ctx.Fail("run %u: heap blocks freed differ: line %d/%d wide %d/%d text %d/%d", run, aLineFreed, bLineFreed, aWideFreed,
                     bWideFreed, aTextFreed, bTextFreed);
        }
        if (textBlock && !aTextFreed) ctx.Fail("run %u: original left its text buffer allocated", run);
        if (textBlockB && !bTextFreed) ctx.Fail("run %u: ours left its text buffer allocated", run);
        if (lineBlock && !aLineFreed) ctx.Fail("run %u: original left its line buffer allocated", run);
        if (lineBlockB && !bLineFreed) ctx.Fail("run %u: ours left its line buffer allocated", run);
        if (wideBlock && !aWideFreed) ctx.Fail("run %u: original left its wide buffer allocated", run);
        if (wideBlockB && !bWideFreed) ctx.Fail("run %u: ours left its wide buffer allocated", run);
        if (!SameDestroyedState(*a, *b)) {
            ctx.Fail("run %u: state after destruction differs: %s / %s", run, Describe(*a).c_str(), Describe(*b).c_str());
        }
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(cRZTextFile_DeletingDestructor, 0x1c3ff8)
{
    auto origDeleting = ctx.Original<cRZTextFile *(__thiscall *)(void *, uint32_t)>(0x1c3ff8);
    auto ctorOrig = ctx.Original<cRZTextFile *(__thiscall *)(void *, const char *)>(0x1c3ec4);
    std::mt19937 rng(0xDE1E7E);
    for (uint32_t run = 0; run < 80; ++run) {
        uint32_t flags = run % 2; // flags 1 frees the object: check the block right after each side's call
        auto *a = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        auto *b = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        std::memset(a, 0xCD, sizeof(cRZTextFile));
        std::memset(b, 0xCD, sizeof(cRZTextFile));
        ctorOrig(a, "f.bin");
        b->Construct("f.bin");
        std::string line = LongText(rng, run % 3 == 0 ? 3 : 20 + rng() % 200);
        a->mLine.assign(line);
        b->mLine.assign(line);
        const void *lineBlock = a->mLine.capacity() > 15 ? a->mLine.data() : nullptr;
        const void *lineBlockB = b->mLine.capacity() > 15 ? b->mLine.data() : nullptr;
        difftest::BeginFrees();
        cRZTextFile *ra = origDeleting(a, flags);
        bool aObjectFreed = flags && !Live(a);
        bool aLineFreed = lineBlock && !Live(lineBlock);
        difftest::BeginFrees();
        cRZTextFile *rb = b->DeletingDestructor(flags);
        bool bObjectFreed = flags && !Live(b);
        bool bLineFreed = lineBlockB && !Live(lineBlockB);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("run %u: DeletingDestructor return differs", run);
        if (aObjectFreed != bObjectFreed || aLineFreed != bLineFreed) {
            ctx.Fail("run %u (flags %u): frees differ: object %d/%d line %d/%d", run, flags, aObjectFreed, bObjectFreed, aLineFreed, bLineFreed);
        }
        if (flags == 1 && !aObjectFreed) ctx.Fail("run %u: original did not free the object", run);
        if (!flags) {
            if (!SameDestroyedState(*a, *b)) ctx.Fail("run %u: state after deleting destructor differs", run);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

DIFF_TEST(cRZTextFile_Open, 0x1c401a)
{
    OurTextVTable ours(ctx);
    auto origCtor = ctx.Original<cRZTextFile *(__thiscall *)(void *, const char *)>(0x1c3ec4);
    auto origDtor = ctx.Original<void(__thiscall *)(void *)>(0x1bab55);
    const uint32_t accesses[] = {1, 1, 1, 2, 3, 0, 7};
    const uint32_t creations[] = {2, 2, 2, 2, 2, 3, 0, 1, 4, 5};
    std::mt19937 rng(0xB0A7E);
    for (uint32_t run = 0; run < 400; ++run) {
        SetWideMode(ctx, run % 2 == 0);
        std::vector<char> content = MakeContent(rng);
        bool missing = rng() % 12 == 0;
        std::wstring pathA = difftest::SandboxPath(L"t" + std::to_wstring(run) + L"a.bin");
        std::wstring pathB = difftest::SandboxPath(L"t" + std::to_wstring(run) + L"b.bin");
        if (!missing) {
            WriteFileBytes(pathA, content);
            WriteFileBytes(pathB, content);
        }
        auto *a = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        auto *b = static_cast<cRZTextFile *>(::operator new(sizeof(cRZTextFile)));
        std::memset(a, 0xCD, sizeof(cRZTextFile));
        std::memset(b, 0xCD, sizeof(cRZTextFile));
        std::string narrowA = Utf8(pathA), narrowB = Utf8(pathB);
        origCtor(a, narrowA.c_str());
        b->Construct(narrowB.c_str());
        b->mVTable = ours.slots;
        // Open must leave these alone unless it finds a mark: start from random values.
        const int32_t sizes[] = {0, 2, 5, 0, -1};
        int32_t charSize = sizes[rng() % 5];
        bool bom = rng() % 2 == 0;
        a->mCharSize = b->mCharSize = charSize;
        a->mByteOrderMark = b->mByteOrderMark = bom;

        uint32_t access = accesses[rng() % std::size(accesses)];
        uint32_t creation = creations[rng() % std::size(creations)];
        uint32_t sharing = rng() % 4;
        bool resA = Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(a, 3)(a, access, creation, sharing);
        bool resB = Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(b, 3)(b, access, creation, sharing);
        ++ctx.cases;
        if (resA != resB || !SameOpenState(*a, *b)) {
            ctx.Fail("run %u (access %u creation %u sharing %u, %zu bytes%s): open %d/%d; original: %s; ours: %s", run, access, creation,
                     sharing, content.size(), missing ? ", missing" : "", resA, resB, Describe(*a).c_str(), Describe(*b).c_str());
        }
        // A second Open while the file is already open (cRZFile::Open returns true early).
        if (rng() % 5 == 0) {
            bool again = Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(a, 3)(a, access, creation, sharing);
            bool againB = Slot<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(b, 3)(b, access, creation, sharing);
            ++ctx.cases;
            if (again != againB || !SameOpenState(*a, *b)) {
                ctx.Fail("run %u: second open differs: %d/%d; original: %s; ours: %s", run, again, againB, Describe(*a).c_str(), Describe(*b).c_str());
            }
        }
        // Close (through the vtable, slot 5), then compare the files: both sides must leave them
        // identical, and without truncation (CREATE_ALWAYS, TRUNCATE_EXISTING) unchanged.
        if (a->mOpen) Slot<bool(__thiscall *)(void *)>(a, 5)(a);
        if (b->mOpen) Slot<bool(__thiscall *)(void *)>(b, 5)(b);
        bool truncates = creation == 1 || creation == 4;
        if (FileBytes(pathA) != FileBytes(pathB) || (!missing && !truncates && FileBytes(pathA) != content)) {
            ctx.Fail("run %u (creation %u): file contents differ or changed", run, creation);
        }
        origDtor(a);
        b->Destruct();
        ::operator delete(a);
        ::operator delete(b);
    }
    SetWideMode(ctx, false);
    difftest::EmptySandbox();
}
