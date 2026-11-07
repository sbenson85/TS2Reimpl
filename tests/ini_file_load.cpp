// Differential tests for cRZIniFile's file access, indexing, enumerations and binary values (src/rz/
// cRZIniFileLoad.size.cpp): Clear 0x9c5ecb (slot 5), GetBinary 0x9c66b2 (8), GetBinaryAlloc 0x9c6722 (9), SetBinary
// 0x9c6b28 (12), ForEachKey 0x9c5ef2 (13), ForEachSection 0x9c615f (14), Load 0x9c63da (17), ReadLine 0x9c623d
// (18), Open 0x9c65f1 (19), and the hex helpers 0x1afb5e / 0x1afbd7; and the writer (src/rz/cRZIniFileWrite.size.cpp),
// SetString 0x9c6bbc (10), in IniFile_SetString.
//
// Two objects with the same state: side A carries the original vtable and is driven by the original code; side B
// carries a copy whose reimplemented slots (all but 1, 3 and 15) are ours, so the slots our code calls through
// the vtable are ours too (SetBinary and SetString reach each side's own SetString and loader).
// Each object reads a real cRZFile on its own sandboxed copy of the same ini text (the original's file code runs
// against the sandbox). After every operation: the results, the out arguments (strings, buffers, callback logs),
// the flags, both tables entry by entry, the open mode byte and the file's state; at the end the two files.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "ifaces/cIGZFile.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZIniFileTables.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kClear = 0x9c5ecb, kGetString = 0x9c679f, kGetBinary = 0x9c66b2, kGetBinaryAlloc = 0x9c6722;
constexpr uint32_t kSetBinary = 0x9c6b28, kForEachKey = 0x9c5ef2, kForEachSection = 0x9c615f, kLoad = 0x9c63da;
constexpr uint32_t kReadLine = 0x9c623d, kOpen = 0x9c65f1, kSetEnabled = 0x9c6aec, kHexDecode = 0x1afb5e;
constexpr uint32_t kHexEncode = 0x1afbd7;
constexpr uint32_t kSetString = 0x9c6bbc;
constexpr uint32_t kSetPath = 0x9c6b06;
constexpr int kSlots = 20;
constexpr size_t kObjectBytes = 0x40;

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// The original vtable with every reimplemented slot pointing at ours.
struct OurVTable {
    void *slots[kSlots];
    explicit OurVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZIniFile::kVTable), sizeof(slots));
        slots[0] = Addr(&cRZIniFile::DeletingDestructor);
        slots[2] = Addr(&cRZIniFile::SetPath);
        slots[4] = Addr(&cRZIniFile::SetFile);
        slots[5] = Addr(&cRZIniFile::Clear);
        slots[6] = Addr(&cRZIniFile::GetString);
        slots[7] = Addr(&cRZIniFile::GetLong);
        slots[8] = Addr(&cRZIniFile::GetBinary);
        slots[9] = Addr(&cRZIniFile::GetBinaryAlloc);
        slots[10] = Addr(&cRZIniFile::SetString);
        slots[11] = Addr(&cRZIniFile::SetLong);
        slots[12] = Addr(&cRZIniFile::SetBinary);
        slots[13] = Addr(&cRZIniFile::ForEachKey);
        slots[14] = Addr(&cRZIniFile::ForEachSection);
        slots[16] = Addr(&cRZIniFile::SetEnabled);
        slots[17] = Addr(&cRZIniFile::Load);
        slots[18] = Addr(&cRZIniFile::ReadLine);
        slots[19] = Addr(&cRZIniFile::Open);
    }
};

using UintThiscall0 = uint32_t(__thiscall *)(void *);
using ClearFn = bool(__thiscall *)(void *);
using GetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, cIGZString *);
using GetBinaryFn = uint32_t(__thiscall *)(void *, const cIGZString *, const cIGZString *, void *, uint32_t);
using GetBinaryAllocFn = uint32_t(__thiscall *)(void *, const cIGZString *, const cIGZString *, void **);
using SetBinaryFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, const void *, uint32_t);
using SetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, const cIGZString *);
using SetPathFn = void(__thiscall *)(void *, const cIGZString *);
using ForEachKeyFn = bool(__thiscall *)(void *, const cIGZString *, cRZIniFile::KeyCallback, void *);
using ForEachSectionFn = bool(__thiscall *)(void *, cRZIniFile::SectionCallback, void *);
using LoadFn = bool(__thiscall *)(void *, uint32_t);
using ReadLineFn = bool(__thiscall *)(void *, cIGZString *);
using OpenFn = bool(__thiscall *)(void *, uint32_t);
using EnabledFn = bool(__thiscall *)(void *, uint32_t);
using HexDecodeFn = bool(__cdecl *)(const char *, uint32_t, uint8_t *);
using HexEncodeFn = void(__cdecl *)(const uint8_t *, uint32_t, char *);

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

// cRZString arguments with a given text.
struct Arg {
    cRZString text;
    explicit Arg(const std::string &s)
    {
        text.Construct();
        text.mString = s;
    }
    ~Arg() { text.Destruct(); }
    Arg(const Arg &) = delete;
    const cIGZString *get() const { return text.AsInterface(); }
    cIGZString *out() { return text.AsInterface(); }
};

cRZIniFile *MakeIni(const void *vtable, const std::string &path, bool enabled, void *file, uint8_t filler)
{
    auto *p = static_cast<cRZIniFile *>(::operator new(kObjectBytes));
    std::memset(p, filler, kObjectBytes);
    p->mVTable = vtable;
    p->mPath.Construct();
    p->mPath.mString = path;
    p->mEnabled = enabled;
    p->mLoaded = false;
    new (static_cast<void *>(p->mEntries)) rzini::SectionTable();
    new (static_cast<void *>(p->mSecondTable)) rzini::SecondTable();
    p->mFile = file;
    return p;
}

void FreeIni(cRZIniFile *p)
{
    using rzini::SecondTable;
    using rzini::SectionTable;
    reinterpret_cast<SecondTable *>(p->mSecondTable)->~SecondTable();
    reinterpret_cast<SectionTable *>(p->mEntries)->~SectionTable();
    p->mPath.Destruct();
    ::operator delete(p);
}

// Both tables, entry by entry, in order.
std::string Tables(cRZIniFile *p)
{
    std::string s = "sections:";
    for (const auto &e : *reinterpret_cast<rzini::SectionTable *>(p->mEntries))
        s += " [" + e.first.key.mString + "]@" + std::to_string(e.second) + (e.first.key.mRefCount ? "!" : "");
    s += " names:";
    for (const auto &e : *reinterpret_cast<rzini::SecondTable *>(p->mSecondTable))
        s += " [" + e.first.key.mString + "]=[" + e.second.key.mString + "]";
    return s;
}

std::string Describe(const cRZFile &f)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "open %d refs %u access %u/%u/%u error 0x%x pos %d fpos %d len %u/%d rbuf %u/%u/%u wbuf %u/%u/%u", f.mOpen,
                  f.mRefCount, f.mAccess, f.mCreation, f.mSharing, f.mError, f.mPosition, f.mFilePosition, f.mCachedLength, f.mLengthCached,
                  f.mReadBufferSize, f.mReadBufferStart, f.mReadBufferLength, f.mWriteBufferSize, f.mWriteBufferStart, f.mWriteBufferLength);
    return buf;
}

// Everything observable about one side, the path's sandbox letter aside.
std::string State(cRZIniFile *p)
{
    const auto *file = static_cast<const cRZFile *>(p->mFile);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "enabled %d loaded %d write %d pad %02x%02x%02x%02x%02x path %zu | ", p->mEnabled, p->mLoaded,
                  p->mOpenForWrite, p->mUnknown26[0], p->mUnknown26[1], p->mUnknown3d[0], p->mUnknown3d[1], p->mUnknown3d[2],
                  p->mPath.mString.size());
    return buf + Tables(p) + " | " + Describe(*file);
}

// The ini lines the texts are made of: sections in several cases and spellings, values for the binary slots
// (valid hex, odd, invalid, longer than a read chunk), comments, blank lines, the three line ends, NULs. No value
// has an odd length of 17 or 33 (see GetBinaryAlloc's ORIGINAL-BUG: the original writes one byte past the block).
const std::string kLines[] = {"[Sims]\r\n", "Width=12\r\n", "  Height = -3  \r\n", "[Options]\n", "Volume=21\r", "; comment\r\n",
                              "\r\n", "\n\n", "   \r\n", "[x]\r\n", "k =  7 \r\n", "junk line\r\n", "Width=\r\n",
                              "NAME=Mixed Case Value\r\n", "name2\t=\ttabbed\r\n", "[ Padded ]\r\n", "k=a=b=c\r\n", "[SIMS]\r\n",
                              "WIDTH=99\r\n", "=nokey\r\n", "[sims] trailing\r\n", "[]\r\n", "[ab\r\n", "[[x]]\r\n", "Data=0A1b2C\r\n",
                              "Data=ABC\r\n", "Data=0G12\r\n", "Data=00112233445566778899AABBCCDDEEFF00112233\r\n",
                              std::string("Nul=a\0b\r\n", 9), "[Bin]\r\n", "Blob=DEADBEEF\r\n",
                              "Exact=0123456789ABCDEF0123456789ABCDEF01\r\n", "Over=0123456789abcdef0123456789abcdef0123\r\n",
                              "\r\r\n\n", "  [Indented]\r\n", "Blob=\r\n", std::string(70, 'L') + "=x\r\n",
                              "[Bin]\r\nBlob=DEADBEEF\r\nData=0A1b2C\r\n", "[bin]\nData=0123456789abcdef\nBlob=1\n"};

std::string RandomIniText(std::mt19937 &rng)
{
    std::string text;
    int n = static_cast<int>(rng() % 16);
    for (int i = 0; i < n; ++i) text += kLines[rng() % (sizeof(kLines) / sizeof(kLines[0]))];
    if (rng() % 4 == 0) text += "last=no newline";
    return text;
}

const std::vector<std::string> kSections = {"Sims", "SIMS", "Options", "x", " Padded ", "Bin", "missing", "", "[x]", "ab", "Indented", "sims"};
const std::vector<std::string> kKeys = {"Width", "WIDTH", "Height", "Volume", "k", "name", "NAME2", "Data", "Blob", "Exact", "Over", "Nul", "last", "missing", ""};

// SetString's extra names (new sections and keys, case and padding variants) and values ('=', spaces, tabs, '%',
// longer than a read chunk, empty).
const std::vector<std::string> kNewSections = {"New", "NEW", "new section", "Fresh", "sims", "Bin", "x"};
const std::vector<std::string> kNewKeys = {"Added", "added", " Width", "Width ", "k", "Blob", "data", "fresh key", "; x"};
const std::vector<std::string> kValues = {"", "1", "v a l", "a=b", "%d%s", "  padded  ", "x\ty", std::string(60, 'V'), "DEADBEEF"};

const std::string &Pick(std::mt19937 &rng, const std::vector<std::string> &pool)
{
    return pool[rng() % pool.size()];
}

void __cdecl LogKey(const cIGZString *key, const cIGZString *value, void *context)
{
    auto *log = static_cast<std::string *>(context);
    const auto *k = reinterpret_cast<const cRZString *>(key);
    const auto *v = reinterpret_cast<const cRZString *>(value);
    *log += "{" + k->mString + "=" + v->mString + "}";
}

void __cdecl LogSection(const cIGZString *name, const cIGZString *unused, void *context)
{
    auto *log = static_cast<std::string *>(context);
    *log += "{" + reinterpret_cast<const cRZString *>(name)->mString + "|" + reinterpret_cast<const cRZString *>(unused)->mString + "}";
}

std::string Hex(const uint8_t *p, size_t n)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", p[i]);
        s += b;
    }
    return s;
}

struct Counts {
    int loads = 0, lines = 0, keys = 0, sections = 0, binaries = 0, allocs = 0, sets = 0, opens = 0;
    // SetString's outcomes: refused, a new file, a new section, a rewrite in place (enabled objects only), the rest
    // (the line appended at the end, or any success on a disabled object); and the file comparisons after it.
    int refusals = 0, newFiles = 0, newSections = 0, rewrites = 0, appends = 0, fileChecks = 0;
    // Writes after the test changed the files under a loaded object (op 12) or moved its path (op 13): a new file
    // while the index was loaded, a section whose indexed offset is the file's end, a new file while the old one
    // was open, a key line that is a comment's text.
    int staleNewFiles = 0, sectionsAtEnd = 0, movedWhileOpen = 0, commentKeys = 0;
};

// The two paths each side's object switches between (op 13): the sandbox file name for side A or B.
std::wstring SidePath(bool second, char side)
{
    return std::wstring(L"L") + static_cast<wchar_t>(side) + (second ? L"2.ini" : L".ini");
}

// The offset the loaded object indexed for `section` (lowercased like SetString's lookup), or -1.
int64_t IndexedOffset(cRZIniFile *p, const std::string &section)
{
    std::string lower = section;
    for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto &e : *reinterpret_cast<rzini::SectionTable *>(p->mEntries))
        if (e.first.key.mString == lower) return e.second;
    return -1;
}

cRZFile *MakeFile(const std::wstring &name)
{
    cRZFile *f = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
    f->Construct(ToUtf8(difftest::SandboxPath(name)).c_str());
    f->AddRef();
    return f;
}

size_t SectionCount(cRZIniFile *p)
{
    return reinterpret_cast<rzini::SectionTable *>(p->mEntries)->size();
}

// One random sequence of operations on a pair of objects. `writes`: a third of the steps are SetString, files are
// missing more often, and one object in 25 has no path.
void Sequence(difftest::Context &ctx, std::mt19937 &rng, const OurVTable &ours, Counts &counts, bool writes = false)
{
    const std::string text = RandomIniText(rng);
    const bool exists = rng() % (writes ? 4 : 12) != 0;
    const bool pathless = writes && exists && rng() % 25 == 0;
    if (exists) {
        if (!WriteFileBytes(difftest::SandboxPath(L"LA.ini"), text) || !WriteFileBytes(difftest::SandboxPath(L"LB.ini"), text))
            ctx.Fail("cannot write the sandbox files (one is still open?)");
    } else {
        DeleteFileW(difftest::SandboxPath(L"LA.ini").c_str());
        DeleteFileW(difftest::SandboxPath(L"LB.ini").c_str());
    }
    DeleteFileW(difftest::SandboxPath(L"LA2.ini").c_str());
    DeleteFileW(difftest::SandboxPath(L"LB2.ini").c_str());
    bool second = false;    // the objects' paths are the L?2.ini files (op 13)
    std::string cutSection; // the section the files were last cut at (op 12): half the time the next step's (then dropped), else a third of the later SetStrings'
    cRZFile *fileA = MakeFile(L"LA.ini");
    cRZFile *fileB = MakeFile(L"LB.ini");
    const bool enabled = rng() % 3 != 0;
    const uint8_t filler = static_cast<uint8_t>(rng());
    cRZIniFile *a = MakeIni(ctx.image.At(cRZIniFile::kVTable), pathless ? "" : ToUtf8(difftest::SandboxPath(L"LA.ini")), enabled, fileA, filler);
    cRZIniFile *b = MakeIni(ours.slots, pathless ? "" : ToUtf8(difftest::SandboxPath(L"LB.ini")), enabled, fileB, filler);

    // An object without a path never sets its file's path in SetString, so the file keeps the sandbox path it was
    // made with; it is opened first (Load would hand the empty path to the file, which the sandbox refuses), and
    // the one step is a SetString.
    if (pathless) {
        using FileOpenFn = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
        const bool forWrite = rng() % 2 == 0;
        VirtualSlot<FileOpenFn>(fileA, 3)(fileA, forWrite ? 3 : 1, forWrite ? 3 : 2, forWrite ? 0 : 1);
        VirtualSlot<FileOpenFn>(fileB, 3)(fileB, forWrite ? 3 : 1, forWrite ? 3 : 2, forWrite ? 0 : 1);
    }
    const int steps = pathless ? 1 : 1 + static_cast<int>(rng() % 14);
    for (int step = 0; step < steps; ++step) {
        // Writes only: op 12 changes the files under the objects, op 13 moves their paths.
        const bool afterCut = writes && !cutSection.empty() && rng() % 2;
        const int op = pathless || afterCut || (writes && rng() % 3 == 0) ? 11 : writes && rng() % 10 == 0 ? 12 + static_cast<int>(rng() % 2) : static_cast<int>(rng() % 11);
        ++ctx.cases;
        std::string what;
        uint64_t ra = 0, rb = 0;
        std::string outA, outB;
        switch (op) {
        case 0: { // Load
            const bool write = rng() % 4 == 0;
            what = write ? "Load(write)" : "Load";
            ra = ctx.Original<LoadFn>(kLoad)(a, write);
            rb = b->Load(write);
            if (ra) ++counts.loads;
            break;
        }
        case 1: // Clear
            what = "Clear";
            ra = ctx.Original<ClearFn>(kClear)(a);
            rb = b->Clear();
            break;
        case 2: { // ReadLine
            what = "ReadLine";
            const std::string previous = rng() % 2 ? "" : "a previous line long enough for the heap";
            if (rng() % 2) { // back to the start of both files (the test's own call, the same on both sides)
                VirtualSlot<UintThiscall0>(a->mFile, 9)(a->mFile);
                VirtualSlot<UintThiscall0>(b->mFile, 9)(b->mFile);
            }
            Arg lineA(previous), lineB(previous);
            ra = ctx.Original<ReadLineFn>(kReadLine)(a, lineA.out());
            rb = b->ReadLine(lineB.out());
            outA = lineA.text.mString + "/" + std::to_string(lineA.text.mString.capacity());
            outB = lineB.text.mString + "/" + std::to_string(lineB.text.mString.capacity());
            if (ra) ++counts.lines;
            break;
        }
        case 3: { // Open
            const bool write = rng() % 3 == 0;
            what = write ? "Open(write)" : "Open";
            ra = ctx.Original<OpenFn>(kOpen)(a, write);
            rb = b->Open(write);
            if (ra) ++counts.opens;
            break;
        }
        case 4: { // GetString (ours, through our slots)
            Arg s(Pick(rng, kSections)), k(Pick(rng, kKeys)), oa("old"), ob("old");
            what = "GetString(" + s.text.mString + ", " + k.text.mString + ")";
            ra = ctx.Original<GetStringFn>(kGetString)(a, s.get(), k.get(), oa.out());
            rb = b->GetString(*s.get(), *k.get(), ob.out());
            outA = oa.text.mString;
            outB = ob.text.mString;
            break;
        }
        case 5: { // GetBinary
            Arg s(rng() % 3 ? std::string(rng() % 2 ? "Bin" : "Sims") : Pick(rng, kSections)),
                k(rng() % 4 ? std::string(rng() % 2 ? "Data" : "Blob") : Pick(rng, kKeys));
            const uint32_t size = static_cast<uint32_t>(rng() % 24);
            uint8_t bufA[32], bufB[32];
            std::memset(bufA, 0xcc, sizeof(bufA));
            std::memset(bufB, 0xcc, sizeof(bufB));
            what = "GetBinary(" + s.text.mString + ", " + k.text.mString + ", " + std::to_string(size) + ")";
            ra = ctx.Original<GetBinaryFn>(kGetBinary)(a, s.get(), k.get(), bufA, size);
            rb = b->GetBinary(*s.get(), *k.get(), bufB, size);
            outA = Hex(bufA, sizeof(bufA));
            outB = Hex(bufB, sizeof(bufB));
            if (ra) ++counts.binaries;
            break;
        }
        case 6: { // GetBinaryAlloc
            Arg s(rng() % 3 ? std::string(rng() % 2 ? "Bin" : "Sims") : Pick(rng, kSections)),
                k(rng() % 4 ? std::string(rng() % 2 ? "Data" : "Blob") : Pick(rng, kKeys));
            void *pa = reinterpret_cast<void *>(0x1234), *pb = reinterpret_cast<void *>(0x1234);
            what = "GetBinaryAlloc(" + s.text.mString + ", " + k.text.mString + ")";
            difftest::FreeRecorder freesA;
            ra = ctx.Original<GetBinaryAllocFn>(kGetBinaryAlloc)(a, s.get(), k.get(), &pa);
            freesA.Stop();
            const size_t freedA = freesA.Count();
            difftest::FreeRecorder freesB;
            rb = b->GetBinaryAlloc(*s.get(), *k.get(), &pb);
            freesB.Stop();
            const size_t freedB = freesB.Count();
            auto show = [](void *p, uint64_t n, size_t freed) {
                if (p == reinterpret_cast<void *>(0x1234)) return std::string("untouched") + " frees " + std::to_string(freed);
                if (!p) return std::string("null") + " frees " + std::to_string(freed);
                return Hex(static_cast<uint8_t *>(p), static_cast<size_t>(n)) + " frees " + std::to_string(freed);
            };
            outA = show(pa, ra, freedA);
            outB = show(pb, rb, freedB);
            if (pa && pa != reinterpret_cast<void *>(0x1234)) ::operator delete[](pa);
            if (pb && pb != reinterpret_cast<void *>(0x1234)) ::operator delete[](pb);
            if (ra) ++counts.allocs;
            break;
        }
        case 7: { // SetBinary
            Arg s(rng() % 5 == 0 ? std::string("") : Pick(rng, kSections)), k(rng() % 5 == 0 ? std::string("") : Pick(rng, kKeys));
            const uint32_t size = static_cast<uint32_t>(rng() % 20);
            uint8_t data[20];
            for (auto &d : data) d = static_cast<uint8_t>(rng());
            what = "SetBinary(" + s.text.mString + ", " + k.text.mString + ", " + Hex(data, size) + ")";
            ra = ctx.Original<SetBinaryFn>(kSetBinary)(a, s.get(), k.get(), data, size);
            rb = b->SetBinary(*s.get(), *k.get(), data, size);
            if (ra) ++counts.sets;
            break;
        }
        case 8: { // ForEachKey
            Arg s(Pick(rng, kSections));
            what = "ForEachKey(" + s.text.mString + ")";
            ra = ctx.Original<ForEachKeyFn>(kForEachKey)(a, s.get(), &LogKey, &outA);
            rb = b->ForEachKey(*s.get(), &LogKey, &outB);
            if (!outA.empty()) ++counts.keys;
            break;
        }
        case 9: // ForEachSection
            what = "ForEachSection";
            ra = ctx.Original<ForEachSectionFn>(kForEachSection)(a, &LogSection, &outA);
            rb = b->ForEachSection(&LogSection, &outB);
            if (!outA.empty()) ++counts.sections;
            break;
        case 11: { // SetString
            const bool atCut = afterCut || (!cutSection.empty() && rng() % 3 == 0);
            const std::string section = atCut ? cutSection : "";
            if (afterCut) cutSection.clear(); // one forced write per cut
            Arg s(atCut ? section : rng() % 2 ? Pick(rng, kSections) : Pick(rng, kNewSections)), k(rng() % 2 ? Pick(rng, kKeys) : Pick(rng, kNewKeys)),
                v(Pick(rng, kValues));
            what = "SetString(" + s.text.mString + ", " + k.text.mString + ", " + v.text.mString + ")";
            const std::wstring pathA = difftest::SandboxPath(SidePath(second, 'A'));
            const bool existed = GetFileAttributesW(pathA.c_str()) != INVALID_FILE_ATTRIBUTES;
            const size_t sectionsBefore = SectionCount(a);
            const bool loadedBefore = a->mLoaded != 0, openBefore = As<ifaces::cIGZFile>(fileA)->IsOpen() != 0;
            const int64_t indexed = loadedBefore ? IndexedOffset(a, s.text.mString) : -1;
            if (!pathless && loadedBefore && !existed) ++counts.staleNewFiles;
            if (!pathless && !existed && openBefore) ++counts.movedWhileOpen;
            if (!pathless && existed && !openBefore && indexed >= 0 && static_cast<size_t>(indexed) == ReadFileBytes(pathA).size())
                ++counts.sectionsAtEnd;
            ra = ctx.Original<SetStringFn>(kSetString)(a, s.get(), k.get(), v.get());
            rb = b->SetString(*s.get(), *k.get(), *v.get());
            if (ra && existed && k.text.mString == "; x" && SectionCount(a) == sectionsBefore) ++counts.commentKeys;
            if (!ra)
                ++counts.refusals;
            else if (!existed)
                ++counts.newFiles;
            else if (SectionCount(a) > sectionsBefore)
                ++counts.newSections;
            else if (a->mEnabled && !a->mLoaded)
                ++counts.rewrites;
            else
                ++counts.appends;
            if (rng() % 2) { // closed (written out) and compared now
                ctx.Original<ClearFn>(kClear)(a);
                b->Clear();
                outA = ReadFileBytes(difftest::SandboxPath(SidePath(second, 'A')));
                outB = ReadFileBytes(difftest::SandboxPath(SidePath(second, 'B')));
                ++counts.fileChecks;
            }
            break;
        }
        case 12: { // the files closed and compared, then deleted or cut (at an indexed section's offset, or anywhere)
            As<ifaces::cIGZFile>(fileA)->Close();
            As<ifaces::cIGZFile>(fileB)->Close();
            const std::wstring pathA = difftest::SandboxPath(SidePath(second, 'A')), pathB = difftest::SandboxPath(SidePath(second, 'B'));
            outA = ReadFileBytes(pathA);
            outB = ReadFileBytes(pathB);
            if (rng() % 3 == 0 || outA == "<missing>") {
                what = "files deleted";
                DeleteFileW(pathA.c_str());
                DeleteFileW(pathB.c_str());
            } else {
                size_t cut = outA.empty() ? 0 : rng() % (outA.size() + 1);
                const auto &sections = *reinterpret_cast<rzini::SectionTable *>(a->mEntries);
                if (!sections.empty() && rng() % 5 != 0) {
                    auto it = sections.begin();
                    std::advance(it, rng() % sections.size());
                    cut = it->second;
                    cutSection = it->first.key.mString;
                }
                what = "files cut at " + std::to_string(cut);
                if (cut <= outA.size()) WriteFileBytes(pathA, outA.substr(0, cut));
                if (cut <= outB.size()) WriteFileBytes(pathB, outB.substr(0, cut));
            }
            break;
        }
        case 13: { // SetPath (slot 2, verified elsewhere) to the other file of the side, sometimes deleted first
            second = !second;
            const std::wstring nameA = SidePath(second, 'A'), nameB = SidePath(second, 'B');
            if (rng() % 2) {
                DeleteFileW(difftest::SandboxPath(nameA).c_str());
                DeleteFileW(difftest::SandboxPath(nameB).c_str());
            }
            what = "SetPath(" + ToUtf8(nameA) + ")";
            Arg pa(ToUtf8(difftest::SandboxPath(nameA))), pb(ToUtf8(difftest::SandboxPath(nameB)));
            ctx.Original<SetPathFn>(kSetPath)(a, pa.get());
            b->SetPath(*pb.get());
            break;
        }
        default: { // SetEnabled (verified elsewhere; changes what the others do)
            const bool flag = rng() % 2 == 0;
            what = flag ? "SetEnabled(1)" : "SetEnabled(0)";
            ra = ctx.Original<EnabledFn>(kSetEnabled)(a, flag);
            rb = b->SetEnabled(flag);
            break;
        }
        }
        const std::string sa = State(a), sb = State(b);
        if (ra != rb || outA != outB || sa != sb)
            ctx.Fail("step %d %s: %llu / %llu\n  out %s\n  out %s\n  %s\n  %s\n  text [%s]", step, what.c_str(), ra, rb, outA.c_str(), outB.c_str(),
                     sa.c_str(), sb.c_str(), text.c_str());
    }

    // Clear closes the file; then the tables, the path and the object go, and the files.
    ctx.Original<ClearFn>(kClear)(a);
    b->Clear();
    FreeIni(a);
    FreeIni(b);
    VirtualSlot<UintThiscall0>(fileA, 2)(fileA);
    VirtualSlot<UintThiscall0>(fileB, 2)(fileB);
    for (bool other : {false, true}) {
        const std::string endA = ReadFileBytes(difftest::SandboxPath(SidePath(other, 'A')));
        const std::string endB = ReadFileBytes(difftest::SandboxPath(SidePath(other, 'B')));
        if (endA != endB) ctx.Fail("file contents differ after the sequence: [%s] / [%s]", endA.c_str(), endB.c_str());
    }
}

// The game fills these globals with its wide file functions at start-up (as on NT); the sandbox provides the
// same functions, so the original's file code runs against the sandbox.
const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

void InstallWideFunctions(difftest::Context &ctx)
{
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = difftest::SandboxFunction(kWideNames[i]);
}

// ---- Open's wait: a fake clock and a fake sleep on both sides ----------------------------------------------

constexpr uint32_t kIatGetSystemTimeAsFileTime = 0xd9a2a4, kIatSleepEx = 0xd9a188;
int64_t gNow = 0;
std::string gSleepLog;
int gSleeps = 0;

void WINAPI FakeGetSystemTimeAsFileTime(FILETIME *out)
{
    const uint64_t ticks = static_cast<uint64_t>(gNow) * 10000000ull + 116444736000000000ull;
    out->dwLowDateTime = static_cast<DWORD>(ticks);
    out->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}

DWORD WINAPI FakeSleepEx(DWORD ms, BOOL alertable)
{
    gSleepLog += "sleep " + std::to_string(ms) + " " + std::to_string(alertable) + " at " + std::to_string(gNow) + "; ";
    if (++gSleeps % 3 == 0) ++gNow;
    return 0;
}

struct ClockFixture {
    difftest::Context &ctx;
    void *savedTime, *savedSleep, *savedOwnTime;
    explicit ClockFixture(difftest::Context &c) : ctx(c)
    {
        savedTime = *ctx.image.At<void *>(kIatGetSystemTimeAsFileTime);
        savedSleep = *ctx.image.At<void *>(kIatSleepEx);
        *ctx.image.At<void *>(kIatGetSystemTimeAsFileTime) = reinterpret_cast<void *>(&FakeGetSystemTimeAsFileTime);
        *ctx.image.At<void *>(kIatSleepEx) = reinterpret_cast<void *>(&FakeSleepEx);
        // Ours reads the clock through our CRT's _time64, which calls our own import.
        savedOwnTime = difftest::SwapOwnImport("GetSystemTimeAsFileTime", reinterpret_cast<void *>(&FakeGetSystemTimeAsFileTime));
    }
    ~ClockFixture()
    {
        *ctx.image.At<void *>(kIatGetSystemTimeAsFileTime) = savedTime;
        *ctx.image.At<void *>(kIatSleepEx) = savedSleep;
        difftest::SwapOwnImport("GetSystemTimeAsFileTime", savedOwnTime);
    }
};

} // namespace

DIFF_TEST(IniFile_Load, kLoad)
{
    // Load 0x9c63da, Clear 0x9c5ecb, ReadLine 0x9c623d, Open 0x9c65f1, GetBinary 0x9c66b2, GetBinaryAlloc 0x9c6722,
    // SetBinary 0x9c6b28, ForEachKey 0x9c5ef2, ForEachSection 0x9c615f.
    InstallWideFunctions(ctx);
    OurVTable ours(ctx);
    std::mt19937 rng(kLoad);
    Counts counts;
    for (int i = 0; i < 1500; ++i) Sequence(ctx, rng, ours, counts);
    if (counts.loads < 300 || counts.lines < 300 || counts.keys < 100 || counts.sections < 100 || counts.binaries < 30 || counts.allocs < 30 ||
        counts.sets < 100 || counts.opens < 200)
        ctx.Fail("coverage: %d loads, %d lines, %d key walks, %d section walks, %d binaries, %d allocs, %d sets, %d opens", counts.loads,
                 counts.lines, counts.keys, counts.sections, counts.binaries, counts.allocs, counts.sets, counts.opens);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(IniFile_SetString, kSetString)
{
    // SetString 0x9c6bbc among the other operations: new files, new sections, keys replaced, lines put before the
    // next section or at the end, refusals (empty names, no path, a section offset at or past the end); the files are
    // compared after half of the writes and at the end of each sequence. Between the writes the test also deletes or
    // cuts the files under a loaded object, and moves the objects' paths (SetPath 0x9c6b06, the original on side A).
    InstallWideFunctions(ctx);
    OurVTable ours(ctx);
    std::mt19937 rng(kSetString);
    Counts counts;
    for (int i = 0; i < 2000; ++i) Sequence(ctx, rng, ours, counts, true);
    if (counts.refusals < 50 || counts.newFiles < 50 || counts.newSections < 100 || counts.rewrites < 200 || counts.appends < 100 ||
        counts.fileChecks < 500)
        ctx.Fail("coverage: %d refused, %d new files, %d new sections, %d rewrites, %d appends, %d file checks", counts.refusals,
                 counts.newFiles, counts.newSections, counts.rewrites, counts.appends, counts.fileChecks);
    if (counts.staleNewFiles < 10 || counts.sectionsAtEnd < 10 || counts.movedWhileOpen < 10 || counts.commentKeys < 20)
        ctx.Fail("coverage: %d new files while loaded, %d sections at the end, %d new files while open, %d comment keys",
                 counts.staleNewFiles, counts.sectionsAtEnd, counts.movedWhileOpen, counts.commentKeys);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(IniFile_OpenWait, kOpen)
{
    // Open 0x9c65f1 on a file another handle holds without sharing: it retries every quarter second (the fake
    // sleep advances the fake clock) and gives up after more than two seconds; and a file opened in the other
    // mode is reopened.
    InstallWideFunctions(ctx);
    ClockFixture clock(ctx);
    OurVTable ours(ctx);
    for (int i = 0; i < 8; ++i) {
        const bool write = (i & 1) != 0;
        const bool locked = (i & 2) != 0;
        const bool openFirst = (i & 4) != 0;
        WriteFileBytes(difftest::SandboxPath(L"WA.ini"), "[a]\r\nk=v\r\n");
        WriteFileBytes(difftest::SandboxPath(L"WB.ini"), "[a]\r\nk=v\r\n");
        std::string logs[2];
        uint32_t results[2];
        std::string states[2];
        for (int side = 0; side < 2; ++side) {
            const wchar_t *name = side == 0 ? L"WA.ini" : L"WB.ini";
            cRZFile *file = MakeFile(name);
            cRZIniFile *p = MakeIni(side == 0 ? ctx.image.At(cRZIniFile::kVTable) : static_cast<const void *>(ours.slots),
                                    ToUtf8(difftest::SandboxPath(name)), true, file, 0);
            if (openFirst) ctx.Original<OpenFn>(kOpen)(p, !write);
            HANDLE holder = INVALID_HANDLE_VALUE;
            if (locked) holder = CreateFileW(difftest::SandboxPath(name).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            gNow = 1700000000 + i * 100;
            gSleepLog.clear();
            gSleeps = 0;
            results[side] = side == 0 ? ctx.Original<OpenFn>(kOpen)(p, write) : p->Open(write);
            logs[side] = gSleepLog;
            states[side] = State(p);
            if (holder != INVALID_HANDLE_VALUE) CloseHandle(holder);
            ctx.Original<ClearFn>(kClear)(p);
            FreeIni(p);
            VirtualSlot<UintThiscall0>(file, 2)(file);
        }
        ++ctx.cases;
        if (results[0] != results[1] || logs[0] != logs[1] || states[0] != states[1])
            ctx.Fail("case %d (write %d, locked %d, open first %d): %u / %u\n  %s\n  %s\n  %s\n  %s", i, write, locked, openFirst, results[0],
                     results[1], logs[0].c_str(), logs[1].c_str(), states[0].c_str(), states[1].c_str());
        if (locked && !openFirst && logs[0].empty()) ctx.Fail("case %d: the locked file was not waited for", i);
    }
}

DIFF_TEST(IniFile_Hex, kHexDecode)
{
    // HexDecode 0x1afb5e and HexEncode 0x1afbd7, on random text (every byte value, odd lengths reading one past) and data.
    auto decode = ctx.Original<HexDecodeFn>(kHexDecode);
    auto encode = ctx.Original<HexEncodeFn>(kHexEncode);
    std::mt19937 rng(kHexDecode);
    static const char kHexish[] = "0123456789abcdefABCDEF/:@G`g";
    for (int i = 0; i < 20000; ++i) {
        const uint32_t length = static_cast<uint32_t>(rng() % 24);
        char text[32];
        for (auto &c : text) c = rng() % 6 == 0 ? static_cast<char>(rng()) : kHexish[rng() % (sizeof(kHexish) - 1)];
        uint8_t outA[16], outB[16];
        std::memset(outA, 0xcc, sizeof(outA));
        std::memset(outB, 0xcc, sizeof(outB));
        const bool ra = decode(text, length, outA);
        const bool rb = IniHexDecode(text, length, outB);
        ++ctx.cases;
        if (ra != rb || std::memcmp(outA, outB, sizeof(outA)) != 0)
            ctx.Fail("decode [%.*s] %u: %d / %d, %s / %s", static_cast<int>(length), text, length, ra, rb, Hex(outA, 16).c_str(), Hex(outB, 16).c_str());

        uint8_t data[12];
        for (auto &d : data) d = static_cast<uint8_t>(rng());
        const uint32_t count = static_cast<uint32_t>(rng() % 13);
        char hexA[28], hexB[28];
        std::memset(hexA, 0x5a, sizeof(hexA));
        std::memset(hexB, 0x5a, sizeof(hexB));
        encode(data, count, hexA);
        IniHexEncode(data, count, hexB);
        ++ctx.cases;
        if (std::memcmp(hexA, hexB, sizeof(hexA)) != 0)
            ctx.Fail("encode %s: [%.28s] / [%.28s]", Hex(data, count).c_str(), hexA, hexB);
    }
}
