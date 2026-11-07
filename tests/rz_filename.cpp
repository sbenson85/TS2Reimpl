// Differential tests for cRZFileName (a path split into drive / directory / name / extension / full
// strings) and the helpers under it: the path splitter, the in-place normalisers and the separator
// normaliser. Each case builds two identical worlds, runs the original on one and ours on the other
// and compares return values and every string afterwards (text, size and capacity).
#include "difftest.h"
#include "free_recorder.h"
#include "app/IniFile.h"
#include "rz/cRZFileName.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kVTable = 0xd9b470;
constexpr uint32_t kCtor = 0x1c7ef4, kDtor = 0x1c0eb2, kInit = 0x1c8bb8, kGetDrive = 0x1c87ba, kGetDir = 0x1c87ac,
                   kGetName = 0x1c8800, kGetExt = 0x1c87ed, kAssemble = 0x1c898e, kGetFull = 0x1c882a, kGetDrivePath = 0x1c87c7,
                   kIsFile = 0x1c8936, kIsDir = 0x1c88cd, kNormDrive = 0x1c86fe, kNormDir = 0x1c86a1, kNormExt = 0x1c875f,
                   kSplitPath = 0x1c3a97, kSplitInto = 0x1c39bc, kNormSeparators = 0x1c0f3b;

using ThisGetter = cRZString *(__thiscall *)(void *);
using BoolGetter = uint32_t(__thiscall *)(void *);

std::string Show(const std::string &s)
{
    std::string out;
    for (char c : s) out += c ? c : '~';
    return out;
}

bool SameString(const cRZString &a, const cRZString &b)
{
    return a.mString == b.mString && a.mString.capacity() == b.mString.capacity() && a.mRefCount == b.mRefCount &&
           a.mVTable == b.mVTable;
}

// A file name object in raw memory with its five strings set to given texts (the vtable is the game's).
struct World {
    alignas(cRZFileName) unsigned char raw[sizeof(cRZFileName)];
    explicit World(const std::string (&parts)[5])
    {
        std::memset(raw, 0xcd, sizeof(raw));
        cRZString *p = &F()->mDrive;
        for (int i = 0; i < 5; ++i) {
            p[i].Construct();
            p[i].mString.assign(parts[i]);
        }
    }
    ~World() { Destroy(); }
    void Destroy()
    {
        if (!alive) return;
        alive = false;
        F()->Destruct();
    }
    cRZFileName *F() { return reinterpret_cast<cRZFileName *>(raw); }
    bool alive = true;
};

bool SameWorld(World &a, World &b)
{
    cRZString *pa = &a.F()->mDrive, *pb = &b.F()->mDrive;
    for (int i = 0; i < 5; ++i) {
        if (!SameString(pa[i], pb[i])) return false;
    }
    return true;
}

std::string Describe(World &w)
{
    cRZString *p = &w.F()->mDrive;
    std::string out;
    for (int i = 0; i < 5; ++i) out += "[" + Show(p[i].mString) + "]";
    return out;
}

// A cRZString out parameter in raw memory.
struct Out {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    Out() { std::memset(raw, 0xcd, sizeof(raw)); }
    cRZString *S() { return reinterpret_cast<cRZString *>(raw); }
    void Free() { S()->Destruct(); }
};

const char *const kDrives[] = {"", "", "C", "c:", "Q:", "1", "C:x", "D:", "ab", ":", "\xC9:", "z"};
const char *const kDirs[] = {"", "", "a", "\\a", "\\", "\\\\srv\\sh", "\\.\\x", "/a/", "a\\..\\", "/", "\\a\\b\\", "x/y"};
const char *const kNames[] = {"", "", "f", ".", "name", "a b", "..", "longer name for the heap path"};
const char *const kExts[] = {"", "", ".x", "x", ".", "..", ".txt", "txt.bak"};
template <size_t N> const char *Pick(std::mt19937 &rng, const char *const (&list)[N]) { return list[rng() % N]; }

std::string RandomText(std::mt19937 &rng, int maxLen)
{
    static const char alphabet[] = {'a', 'b', 'Z', '1', '.', '\\', '/', ':', ' ', '\xC9'};
    int len = std::uniform_int_distribution<int>(0, maxLen)(rng);
    if (rng() % 5 == 0) len = std::uniform_int_distribution<int>(14, 17)(rng);
    std::string s;
    for (int i = 0; i < len; ++i) s += alphabet[rng() % sizeof(alphabet)];
    return s;
}

void RandomParts(std::mt19937 &rng, std::string (&parts)[5])
{
    parts[0] = rng() % 4 ? Pick(rng, kDrives) : RandomText(rng, 4).c_str();
    parts[1] = rng() % 4 ? Pick(rng, kDirs) : RandomText(rng, 12).c_str();
    parts[2] = rng() % 4 ? Pick(rng, kNames) : RandomText(rng, 12).c_str();
    parts[3] = rng() % 4 ? Pick(rng, kExts) : RandomText(rng, 6).c_str();
    parts[4] = RandomText(rng, 20);
}

} // namespace

DIFF_TEST(cRZFileName_SplitInto, kSplitInto)
{
    using SplitIntoFn = void(__cdecl *)(const void *, void *, void *, void *, void *);
    auto original = ctx.Original<SplitIntoFn>(kSplitInto);
    std::mt19937 rng(0x1c39bc);
    static const char alphabet[] = {'a', 'b', '.', '\\', '/', ':', 'Q'};
    for (int i = 0; i < 20000; ++i) {
        std::string path;
        int len = std::uniform_int_distribution<int>(0, 24)(rng);
        if (i % 40 == 0) len = std::uniform_int_distribution<int>(256, 600)(rng);
        for (int k = 0; k < len; ++k) path += alphabet[rng() % sizeof(alphabet)];
        if (i % 3 == 0 && !path.empty()) path[1 % path.size()] = ':';
        std::string parts[5];
        for (std::string &p : parts) p = RandomText(rng, 24);
        parts[4] = path;
        World a(parts), b(parts);
        bool use[4];
        for (bool &u : use) u = i % 4 == 0 ? rng() % 2 : true;
        cRZString *pa = &a.F()->mDrive, *pb = &b.F()->mDrive;
        original(&pa[4], use[0] ? &pa[0] : nullptr, use[1] ? &pa[1] : nullptr, use[2] ? &pa[2] : nullptr, use[3] ? &pa[3] : nullptr);
        cRZFileName::SplitInto(&pb[4], use[0] ? &pb[0] : nullptr, use[1] ? &pb[1] : nullptr, use[2] ? &pb[2] : nullptr, use[3] ? &pb[3] : nullptr);
        ++ctx.cases;
        if (!SameWorld(a, b)) ctx.Fail("SplitInto(\"%s\"): original %s ours %s", Show(path).c_str(), Describe(a).c_str(), Describe(b).c_str());
    }
}

// The in-place normalisers on single strings, called directly.
DIFF_TEST(cRZFileName_Normalisers, kNormDrive)
{
    using NormFn = void(__stdcall *)(void *);
    auto drive = ctx.Original<NormFn>(kNormDrive);
    auto directory = ctx.Original<NormFn>(kNormDir);
    auto extension = ctx.Original<NormFn>(kNormExt);
    std::mt19937 rng(0x1c86fe);
    for (int i = 0; i < 30000; ++i) {
        std::string text = rng() % 3 == 0 ? RandomText(rng, 5) : (rng() % 2 ? Pick(rng, kDrives) : rng() % 2 ? Pick(rng, kDirs) : Pick(rng, kExts));
        if (rng() % 8 == 0) text += RandomText(rng, 20); // long enough for the heap
        if (rng() % 16 == 0) text = std::string(1, static_cast<char>(rng())) + (rng() % 2 ? ":" : "x"); // any first byte
        for (int which = 0; which < 3; ++which) {
            Out a, b;
            a.S()->Construct();
            b.S()->Construct();
            a.S()->mString = text;
            b.S()->mString = text;
            if (which == 0) {
                drive(a.S());
                cRZFileName::NormaliseDrive(b.S());
            } else if (which == 1) {
                directory(a.S());
                cRZFileName::NormaliseDirectory(b.S());
            } else {
                extension(a.S());
                cRZFileName::NormaliseExtension(b.S());
            }
            ++ctx.cases;
            if (!SameString(*a.S(), *b.S()))
                ctx.Fail("Normalise%s(\"%s\"): original \"%s\" cap %zu, ours \"%s\" cap %zu", which == 0 ? "Drive" : which == 1 ? "Directory" : "Extension",
                         Show(text).c_str(), Show(a.S()->mString).c_str(), a.S()->mString.capacity(), Show(b.S()->mString).c_str(),
                         b.S()->mString.capacity());
            a.Free();
            b.Free();
        }
    }
}

DIFF_TEST(cRZFileName_NormaliseSeparators, kNormSeparators)
{
    using Fn = void(__cdecl *)(void *, char);
    auto original = ctx.Original<Fn>(kNormSeparators);
    std::mt19937 rng(0x1c0f3b);
    static const char separators[] = {'\\', '/', ':', 'a', '\0', '.'};
    for (int i = 0; i < 30000; ++i) {
        std::string text = RandomText(rng, 20);
        if (rng() % 8 == 0) text += "\\//\\";
        if (rng() % 8 == 0) text.clear();
        char separator = separators[rng() % sizeof(separators)];
        Out a, b;
        a.S()->Construct();
        b.S()->Construct();
        a.S()->mString = text;
        b.S()->mString = text;
        original(a.S(), separator);
        cRZFileName::NormaliseSeparators(b.S(), separator);
        ++ctx.cases;
        if (!SameString(*a.S(), *b.S()))
            ctx.Fail("NormaliseSeparators(\"%s\", 0x%02x): original \"%s\" cap %zu, ours \"%s\" cap %zu", Show(text).c_str(), separator,
                     Show(a.S()->mString).c_str(), a.S()->mString.capacity(), Show(b.S()->mString).c_str(), b.S()->mString.capacity());
        a.Free();
        b.Free();
    }
}

// The five part accessors, Assemble with every mask, and the two "copy of the assembled path" getters.
DIFF_TEST(cRZFileName_Accessors, kAssemble)
{
    auto getDrive = ctx.Original<ThisGetter>(kGetDrive);
    auto getDir = ctx.Original<ThisGetter>(kGetDir);
    auto getName = ctx.Original<ThisGetter>(kGetName);
    auto getExt = ctx.Original<ThisGetter>(kGetExt);
    auto assemble = ctx.Original<void(__thiscall *)(void *, uint32_t)>(kAssemble);
    auto getFull = ctx.Original<cRZString *(__thiscall *)(void *, void *)>(kGetFull);
    auto getDrivePath = ctx.Original<cRZString *(__thiscall *)(void *, void *)>(kGetDrivePath);
    std::mt19937 rng(0x1c898e);
    for (int i = 0; i < 20000; ++i) {
        std::string parts[5];
        RandomParts(rng, parts);
        // Getters.
        {
            World a(parts), b(parts);
            cRZString *ra, *rb;
            const char *which;
            switch (i % 4) {
            case 0: which = "GetDrive"; ra = getDrive(a.F()); rb = b.F()->GetDrive(); break;
            case 1: which = "GetDirectory"; ra = getDir(a.F()); rb = b.F()->GetDirectory(); break;
            case 2: which = "GetName"; ra = getName(a.F()); rb = b.F()->GetName(); break;
            default: which = "GetExtension"; ra = getExt(a.F()); rb = b.F()->GetExtension(); break;
            }
            ++ctx.cases;
            if (reinterpret_cast<unsigned char *>(ra) - a.raw != reinterpret_cast<unsigned char *>(rb) - b.raw || !SameWorld(a, b))
                ctx.Fail("%s: original %s ours %s", which, Describe(a).c_str(), Describe(b).c_str());
        }
        // Assemble, with random (also high) mask bits: only the low byte counts.
        {
            World a(parts), b(parts);
            uint32_t mask = (rng() % 3 == 0) ? 0xf : static_cast<uint32_t>(rng());
            assemble(a.F(), mask);
            b.F()->Assemble(mask);
            ++ctx.cases;
            if (!SameWorld(a, b)) ctx.Fail("Assemble(0x%x): original %s ours %s", mask, Describe(a).c_str(), Describe(b).c_str());
        }
        // The two copy-out getters.
        for (int which = 0; which < 2; ++which) {
            World a(parts), b(parts);
            Out outA, outB;
            cRZString *ra = (which ? getDrivePath : getFull)(a.F(), outA.S());
            cRZString *rb = which ? b.F()->GetDrivePath(outB.S()) : b.F()->GetFullPath(outB.S());
            ++ctx.cases;
            if (ra != outA.S() || rb != outB.S() || !SameWorld(a, b) || !SameString(*outA.S(), *outB.S()))
                ctx.Fail("%s: original %s \"%s\" ours %s \"%s\"", which ? "GetDrivePath" : "GetFullPath", Describe(a).c_str(),
                         Show(outA.S()->mString).c_str(), Describe(b).c_str(), Show(outB.S()->mString).c_str());
            outA.Free();
            outB.Free();
        }
    }
}

DIFF_TEST(cRZFileName_IsAbsolute, kIsFile)
{
    auto isFile = ctx.Original<BoolGetter>(kIsFile);
    auto isDir = ctx.Original<BoolGetter>(kIsDir);
    std::mt19937 rng(0x1c8936);
    int trueFile = 0, trueDir = 0;
    for (int i = 0; i < 30000; ++i) {
        std::string parts[5];
        RandomParts(rng, parts);
        if (i % 2 == 0) { // bias towards a canonical absolute path: a drive and a rooted directory
            parts[0] = rng() % 2 ? "C:" : "d";
            parts[1] = rng() % 3 ? "\\dir\\sub\\" : (rng() % 2 ? "x" : "");
            if (rng() % 2) parts[2].clear();
            if (rng() % 2) parts[3].clear();
        }
        for (int which = 0; which < 2; ++which) {
            World a(parts), b(parts);
            bool ra = ((which ? isDir : isFile)(a.F()) & 0xff) != 0;
            bool rb = which ? b.F()->IsAbsoluteDirectory() : b.F()->IsAbsoluteFile();
            (which ? trueDir : trueFile) += ra;
            ++ctx.cases;
            if (ra != rb || !SameWorld(a, b))
                ctx.Fail("%s: %d/%d original %s ours %s", which ? "IsAbsoluteDirectory" : "IsAbsoluteFile", ra, rb, Describe(a).c_str(),
                         Describe(b).c_str());
        }
    }
    if (trueFile < 500 || trueDir < 500) ctx.Fail("test too weak: only %d file / %d directory cases were absolute", trueFile, trueDir);
}

// Construct (five default strings, then Init: resolve and split). Only canonical absolute paths: the
// others resolve against the current directory, which the harness can't provide.
DIFF_TEST(cRZFileName_Construct, kCtor)
{
    using CtorFn = void *(__thiscall *)(void *, const void *);
    auto construct = ctx.Original<CtorFn>(kCtor);
    auto init = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(kInit);
    std::mt19937 rng(0x1c7ef4);
    static const char names[][8] = {"a", "bb", "cc.d", ".e", "f.", "..", "g.h.i", "x y"};
    static const char *const roots[] = {"C:\\", "d:\\", "Z:/", "\\\\srv\\share\\", "\\\\h/"};
    int done = 0;
    for (int i = 0; i < 40000 && done < 15000; ++i) {
        std::string path = roots[rng() % 5];
        int depth = rng() % 4;
        for (int k = 0; k < depth; ++k) path += std::string(names[rng() % 8]) + (rng() % 2 ? "\\" : "/");
        if (rng() % 4) path += names[rng() % 8];
        if (rng() % 10 == 0) path += std::string(rng() % 300, 'q'); // parts beyond 255 characters
        if (rng() % 10 == 0) path += ".ext" + std::string(rng() % 300, 'w');
        Out inRaw;
        cRZString &in = *inRaw.S();
        in.Construct(path.c_str());
        if (!inifile::IsCanonicalAbsolutePathString(&in)) {
            in.Destruct();
            continue;
        }
        ++done;
        alignas(cRZFileName) unsigned char rawA[sizeof(cRZFileName)], rawB[sizeof(cRZFileName)];
        std::memset(rawA, 0x5a, sizeof(rawA));
        std::memset(rawB, 0x5a, sizeof(rawB));
        auto *fa = reinterpret_cast<cRZFileName *>(rawA);
        auto *fb = reinterpret_cast<cRZFileName *>(rawB);
        void *ra = construct(fa, &in);
        cRZFileName *rb = fb->Construct(&in);
        ++ctx.cases;
        bool same = ra == fa && rb == fb && in.mString == path;
        const cRZString *pa = &fa->mDrive, *pb = &fb->mDrive;
        for (int k = 0; k < 5; ++k) same = same && SameString(pa[k], pb[k]);
        if (!same) {
            std::string sa, sb;
            for (int k = 0; k < 5; ++k) {
                sa += "[" + Show(pa[k].mString) + "]";
                sb += "[" + Show(pb[k].mString) + "]";
            }
            ctx.Fail("Construct(\"%s\"): original %s ours %s", Show(path).c_str(), sa.c_str(), sb.c_str());
        }
        // Init alone, on a second pair of objects holding old text: returns true, replaces full and parts.
        {
            std::string parts[5];
            for (std::string &p : parts) p = RandomText(rng, 20);
            World a(parts), b(parts);
            bool ia = (init(a.F(), &in) & 0xff) != 0;
            bool ib = b.F()->Init(&in);
            ++ctx.cases;
            if (ia != ib || !ia || !SameWorld(a, b)) ctx.Fail("Init(\"%s\"): %d/%d original %s ours %s", Show(path).c_str(), ia, ib, Describe(a).c_str(), Describe(b).c_str());
        }
        fa->Destruct();
        fb->Destruct();
        in.Destruct();
    }
    if (done < 5000) ctx.Fail("too few canonical paths generated (%d)", done);
}

// Destruct: the five strings go away last to first, each with its vtable set to the game's. Heap
// blocks are judged by a FreeRecorder across each side's own call.
DIFF_TEST(cRZFileName_Destruct, kDtor)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    std::mt19937 rng(0x1c0eb2);
    for (int i = 0; i < 3000; ++i) {
        std::string parts[5];
        for (std::string &p : parts) p = RandomText(rng, 30) + (rng() % 2 ? std::string(20, 'h') : std::string());
        for (int side = 0; side < 2; ++side) {
            alignas(cRZFileName) unsigned char raw[sizeof(cRZFileName)];
            auto *f = reinterpret_cast<cRZFileName *>(raw);
            std::memset(raw, 0xcd, sizeof(raw));
            cRZString *p = &f->mDrive;
            const char *blocks[5];
            for (int k = 0; k < 5; ++k) {
                p[k].Construct();
                p[k].mString.assign(parts[k]);
                p[k].mVTable = reinterpret_cast<const void *>(0x12345678); // must be overwritten
                blocks[k] = p[k].mString.capacity() > 15 ? p[k].mString.data() : nullptr;
            }
            difftest::FreeRecorder rec;
            if (side == 0) original(f);
            else f->Destruct();
            rec.Stop();
            ++ctx.cases;
            for (int k = 0; k < 5; ++k) {
                bool freed = blocks[k] == nullptr || rec.Freed(blocks[k]);
                if (!freed) ctx.Fail("%s Destruct left part %d allocated (\"%s\")", side ? "ours" : "original", k, Show(parts[k]).c_str());
                if (p[k].mVTable != static_cast<const void *>(ctx.image.At(kVTable)))
                    ctx.Fail("%s Destruct left part %d with vtable %p", side ? "ours" : "original", k, p[k].mVTable);
            }
        }
    }
}
