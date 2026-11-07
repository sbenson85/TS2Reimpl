// Differential tests for the path normalisation chain under 0x1c33df and the string concatenation
// 0x2f5500 (src/app/IniFile.*): 0x1c0f23, 0x1c2215, 0x1c21fe, 0x1c148f, 0x1c1655, 0x1c1760, 0x1c17bd,
// 0x1c33df, 0x2f5500.
//
// Paths are random strings over an alphabet of letters, dots, both separators and colons, so the "."
// and ".." collapsing, drive prefixes, UNC prefixes and the trailing-separator rules all get hit.
// Output buffers are filled with the same canary on both sides so over-writes show up. The cRZString
// arguments carry the original vtable.
#include "app/IniFile.h"
#include "difftest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <direct.h>
#include <windows.h>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kIsLetter = 0x1c0f23, kIsCanonical = 0x1c2215, kIsCanonicalString = 0x1c21fe, kCombine = 0x1c148f, kMakeAbsolute = 0x1c1655;
constexpr uint32_t kAbsoluteString = 0x1c1760, kCombineString = 0x1c17bd, kResolve = 0x1c33df, kConcat = 0x2f5500;

using IsLetterFn = uint32_t(__cdecl *)(char);
using IsCanonicalFn = uint32_t(__cdecl *)(const char *);
using IsCanonicalStringFn = uint32_t(__cdecl *)(const cRZString *);
using CombineFn = char *(__cdecl *)(char *, const char *, int32_t, const char *);
using MakeAbsoluteFn = char *(__cdecl *)(char *, const char *, int32_t);
using AbsoluteStringFn = uint32_t(__cdecl *)(cRZString *, const cRZString *);
using CombineStringFn = uint32_t(__cdecl *)(cRZString *, const cRZString *, const cRZString *);
using ConcatFn = std::string *(__cdecl *)(std::string *, const std::string *, std::string *);

const char kAlphabet[] = "abZ.\\/\\/..:C:cdD z";

std::string RandomPath(std::mt19937 &rng, int maxLength)
{
    int style = static_cast<int>(rng() % 6);
    std::string s;
    switch (style) {
    case 0: s = "C:\\"; break;
    case 1: s = "\\\\"; break;
    case 2: s = "d:/"; break;
    case 3: s = "\\"; break;
    default: break;
    }
    int n = static_cast<int>(rng() % static_cast<uint32_t>(maxLength + 1));
    for (int i = 0; i < n; ++i) s += kAlphabet[rng() % (sizeof(kAlphabet) - 1)];
    return s;
}

// Component-structured paths ("a\b\..\.\c\") reach the collapsing code far more often than uniform noise.
std::string StructuredPath(std::mt19937 &rng)
{
    static const char *const parts[] = {"a", "bb", ".", "..", "..", "x.y", "...", ".a", "a."};
    static const char seps[] = {'\\', '/'};
    std::string s;
    int style = static_cast<int>(rng() % 5);
    if (style == 0) s = "C:\\";
    else if (style == 1) s = "\\\\srv\\";
    else if (style == 2) s = "e:";
    else if (style == 3) s = "\\";
    int n = static_cast<int>(rng() % 9);
    for (int i = 0; i < n; ++i) {
        s += parts[rng() % (sizeof(parts) / sizeof(parts[0]))];
        if (i + 1 < n || rng() % 2) s += seps[rng() % 2];
    }
    return s;
}

std::string RandomOrStructured(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return RandomPath(rng, 12);
    case 1: return RandomPath(rng, 40);
    default: return StructuredPath(rng);
    }
}

std::string Bytes(const unsigned char *p, size_t n)
{
    return std::string(reinterpret_cast<const char *>(p), n);
}

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const char *text)
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text);
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

// The original's static CRT is not initialised in the harness, and its _getdcwd for a drive other than
// the current one reaches environment state that isn't there. _getdrive and _getdcwd are library code
// (the same functions our code calls), so for the life of the guard the two call sites inside 0x1c1655
// (0x1c16c8 and 0x1c16f2) are redirected to the host's CRT; the test then compares the logic around the
// calls. Also sets the working directory to each of a few real directories in turn.
struct RealCwd {
    static constexpr uint32_t kGetDriveSite = 0x1c16c8, kGetDcwdSite = 0x1c16f2;
    uint8_t *sites[2];
    uint8_t saved[2][5];
    wchar_t previous[MAX_PATH];
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
        GetCurrentDirectoryW(MAX_PATH, previous);
    }
    ~RealCwd()
    {
        SetCurrentDirectoryW(previous);
        for (int i = 0; i < 2; ++i) std::memcpy(sites[i], saved[i], 5);
    }
    // Windows directory ("C:\Windows"), its drive root ("C:\"), and the system directory.
    static std::vector<std::wstring> Directories()
    {
        wchar_t windows[MAX_PATH], system[MAX_PATH];
        GetWindowsDirectoryW(windows, MAX_PATH);
        GetSystemDirectoryW(system, MAX_PATH);
        std::wstring root(windows, 3);
        return {windows, root, system};
    }
};

// "X:rel" resolves against the current directory of drive X. The CRT's _getdcwd for a drive other than
// the current one crashes in this process (the host's own CRT included, with a fast-fail), so those
// inputs are left out; the explicit-drive paths that take a separator after the colon, and the current
// drive, are covered.
bool NeedsOtherDriveDirectory(const std::string &s)
{
    if (s.size() < 2 || s[1] != ':' || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return false;
    if (s.size() > 2 && (s[2] == '\\' || s[2] == '/')) return false;
    return (s[0] & ~0x20) != 'C';
}

} // namespace

DIFF_TEST(IniFile_IsAsciiLetter, kIsLetter)
{
    auto original = ctx.Original<IsLetterFn>(kIsLetter);
    for (int c = -128; c < 128; ++c) {
        ++ctx.cases;
        bool a = (original(static_cast<char>(c)) & 0xff) != 0;
        bool b = inifile::IsAsciiLetter(static_cast<char>(c));
        if (a != b) ctx.Fail("IsAsciiLetter(%d): %d/%d", c, a, b);
    }
}

DIFF_TEST(IniFile_IsCanonicalAbsolutePath, kIsCanonical)
{
    auto original = ctx.Original<IsCanonicalFn>(kIsCanonical);
    auto originalString = ctx.Original<IsCanonicalStringFn>(kIsCanonicalString);
    std::mt19937 rng(0x1c2215);
    const char *fixed[] = {"", "C", "C:", "C:\\", "C:/", "C:\\a", "C:\\a\\.", "C:\\a/.b", "c:\\x", "1:\\x", "\\\\", "\\\\srv\\a", "\\\\a\\..\\b", "\\a", "//a", "\\/a", "C:\\a.b\\c.d",
                           "C:\\.hidden", "C:\\a\\b/.", "z:\\\\"};
    std::vector<std::string> inputs(std::begin(fixed), std::end(fixed));
    for (int i = 0; i < 3000; ++i) inputs.push_back(RandomOrStructured(rng));
    for (const std::string &s : inputs) {
        ++ctx.cases;
        bool a = (original(s.c_str()) & 0xff) != 0;
        bool b = inifile::IsCanonicalAbsolutePath(s.c_str());
        Str str(s.c_str());
        bool c = (originalString(&str.S()) & 0xff) != 0;
        bool d = inifile::IsCanonicalAbsolutePathString(&str.S());
        if (a != b || c != d || a != c) ctx.Fail("IsCanonicalAbsolutePath(\"%s\"): %d/%d string %d/%d", s.c_str(), a, b, c, d);
    }
}

DIFF_TEST(IniFile_CombinePath, kCombine)
{
    auto original = ctx.Original<CombineFn>(kCombine);
    std::mt19937 rng(0x1c148f);
    std::vector<std::pair<std::string, std::string>> inputs;
    const char *fixedBase[] = {"", "C:\\", "C:\\a\\b", "C:\\a\\b\\", "C:/x/y", "\\\\", "\\\\srv\\share", "C:", "C:\\a"};
    const char *fixedPath[] = {"", "a", "a\\", ".", "..", "..\\..", "\\..", "a\\..", "a\\..\\..", "a\\b\\..\\..\\..", "./a", "a/./b", "a\\.\\", "a\\..\\", "...", "a...", "..\\a", "x\\..",
                               "C:\\", "\\", "\\a", "a\\b\\c\\..", "a\\.."};
    for (const char *b : fixedBase)
        for (const char *p : fixedPath) inputs.emplace_back(b, p);
    for (int i = 0; i < 6000; ++i) inputs.emplace_back(RandomOrStructured(rng), RandomOrStructured(rng));
    // Lengths around the 0x208 / 0x209 limits.
    for (int baseLen : {0, 1, 100, 0x207, 0x208, 0x209, 0x20a})
        for (int pathLen : {0, 1, 5, 0x100, 0x207, 0x208, 0x209}) {
            inputs.emplace_back(std::string(static_cast<size_t>(baseLen), 'b'), std::string(static_cast<size_t>(pathLen), 'p'));
            inputs.emplace_back("C:\\" + std::string(static_cast<size_t>(baseLen), 'b'), "\\" + std::string(static_cast<size_t>(pathLen), 'p') + "\\");
        }
    const int32_t maxlens[] = {0x208, 4, 8, 0x40, 0x209};
    for (size_t index = 0; index < inputs.size(); ++index) {
        const auto &in = inputs[index];
        for (int mode = 0; mode < 3; ++mode) {
            int32_t maxlen = maxlens[(index + mode) % 5];
            unsigned char bufA[0x240], bufB[0x240];
            std::memset(bufA, 0x5a, sizeof(bufA));
            std::memset(bufB, 0x5a, sizeof(bufB));
            bool allocate = mode == 2;
            char *ra = original(allocate ? nullptr : reinterpret_cast<char *>(bufA), in.second.c_str(), maxlen, in.first.c_str());
            char *rb = inifile::CombinePath(allocate ? nullptr : reinterpret_cast<char *>(bufB), in.second.c_str(), maxlen, in.first.c_str());
            ++ctx.cases;
            if ((ra == nullptr) != (rb == nullptr)) {
                ctx.Fail("CombinePath(base \"%.60s\"(%zu), path \"%.60s\"(%zu), %d, alloc %d): null %d/%d", in.first.c_str(), in.first.size(), in.second.c_str(), in.second.size(), maxlen, allocate,
                         ra == nullptr, rb == nullptr);
            } else if (ra) {
                bool okPtr = allocate ? true : (ra == reinterpret_cast<char *>(bufA) && rb == reinterpret_cast<char *>(bufB));
                if (!okPtr || std::strcmp(ra, rb) != 0) ctx.Fail("CombinePath(base \"%.60s\", path \"%.60s\", %d): \"%s\" / \"%s\"", in.first.c_str(), in.second.c_str(), maxlen, ra, rb);
            }
            if (!allocate && Bytes(bufA, sizeof(bufA)) != Bytes(bufB, sizeof(bufB)))
                ctx.Fail("CombinePath buffer differs (base \"%.60s\", path \"%.60s\", %d)", in.first.c_str(), in.second.c_str(), maxlen);
            if (allocate) {
                std::free(ra);
                std::free(rb);
            }
        }
    }
    // A null path.
    if (original(nullptr, nullptr, 0x208, "C:\\") != nullptr || inifile::CombinePath(nullptr, nullptr, 0x208, "C:\\") != nullptr) ctx.Fail("null path");
}

DIFF_TEST(IniFile_MakeAbsolutePath, kMakeAbsolute)
{
    auto original = ctx.Original<MakeAbsoluteFn>(kMakeAbsolute);
    std::mt19937 rng(0x1c1655);
    std::vector<std::string> inputs = {"", "a", "a\\b", "\\", "\\a", "/a\\b", "\\\\srv\\share\\x", "//srv/x", "C:", "C:a", "C:\\", "C:\\a\\..", "c:\\a\\.\\b", "d:rel\\x", "D:/x", "Z:\\x", "1:\\x", ":\\x", "..", "..\\a",
                                       ".\\a", "a\\..\\..", "\\..", "C:..\\x", "x:", "\\\\", "\\\\x", ":", "a:"};
    for (int i = 0; i < 3000; ++i) inputs.push_back(RandomOrStructured(rng));
    const int32_t maxlens[] = {0x208, 4, 0x20, 0x100, 0x209};
    RealCwd guard(ctx);
    for (size_t index = 0; index < inputs.size(); ++index) {
        const std::string &s = inputs[index];
        if (NeedsOtherDriveDirectory(s)) continue;
        static const std::vector<std::wstring> dirs = RealCwd::Directories();
        SetCurrentDirectoryW(dirs[index % dirs.size()].c_str());
        int32_t maxlen = maxlens[index % 5];
        unsigned char bufA[0x240], bufB[0x240];
        std::memset(bufA, 0x5a, sizeof(bufA));
        std::memset(bufB, 0x5a, sizeof(bufB));
        char *ra = original(reinterpret_cast<char *>(bufA), s.c_str(), maxlen);
        char *rb = inifile::MakeAbsolutePath(reinterpret_cast<char *>(bufB), s.c_str(), maxlen);
        ++ctx.cases;
        if ((ra == nullptr) != (rb == nullptr)) ctx.Fail("MakeAbsolutePath(\"%s\", %d): null %d/%d", s.c_str(), maxlen, ra == nullptr, rb == nullptr);
        else if (ra && std::strcmp(ra, rb) != 0) ctx.Fail("MakeAbsolutePath(\"%s\", %d): \"%s\" / \"%s\"", s.c_str(), maxlen, ra, rb);
        if (Bytes(bufA, sizeof(bufA)) != Bytes(bufB, sizeof(bufB))) ctx.Fail("MakeAbsolutePath buffer differs (\"%s\", %d)", s.c_str(), maxlen);
    }
}

// The three string wrappers: success flag, the destination string (text, size, capacity) and the
// source/base strings staying untouched.
DIFF_TEST(IniFile_PathStrings, kResolve)
{
    auto absoluteOriginal = ctx.Original<AbsoluteStringFn>(kAbsoluteString);
    auto combineOriginal = ctx.Original<CombineStringFn>(kCombineString);
    auto resolveOriginal = ctx.Original<CombineStringFn>(kResolve);
    std::mt19937 rng(0x1c33df);
    const char *previous[] = {"", "previous", "a previous destination long enough to live on the heap for sure"};
    RealCwd guard(ctx);
    const std::vector<std::wstring> dirs = RealCwd::Directories();
    for (int i = 0; i < 4000; ++i) {
        SetCurrentDirectoryW(dirs[static_cast<size_t>(i) % dirs.size()].c_str());
        std::string in = RandomOrStructured(rng), base = RandomOrStructured(rng);
        if (i % 7 == 0) in = "C:\\" + in; // canonical absolute roots show up often enough
        const char *prev = previous[rng() % 3];
        for (int function = 0; function < 3; ++function) {
            for (int useBase = 0; useBase < 2; ++useBase) {
                if (function == 0 && useBase) continue;
                if ((function == 0 || (function == 2 && !useBase)) && NeedsOtherDriveDirectory(in)) continue;
                if (function == 1 && !useBase) {
                    // 0x1c17bd accepts a null base
                }
                Str outA(prev), outB(prev), inA(in.c_str()), inB(in.c_str()), baseA(base.c_str()), baseB(base.c_str());
                const cRZString *bA = useBase ? &baseA.S() : nullptr;
                const cRZString *bB = useBase ? &baseB.S() : nullptr;
                bool ra, rb;
                const char *name;
                switch (function) {
                case 0:
                    name = "AbsolutePathString";
                    ra = (absoluteOriginal(&outA.S(), &inA.S()) & 0xff) != 0;
                    rb = inifile::AbsolutePathString(&outB.S(), &inB.S());
                    break;
                case 1:
                    name = "CombinePathString";
                    ra = (combineOriginal(&outA.S(), &inA.S(), bA) & 0xff) != 0;
                    rb = inifile::CombinePathString(&outB.S(), &inB.S(), bB);
                    break;
                default:
                    name = "ResolvePathString";
                    ra = (resolveOriginal(&outA.S(), &inA.S(), bA) & 0xff) != 0;
                    rb = inifile::ResolvePathString(&outB.S(), &inB.S(), bB);
                    break;
                }
                ++ctx.cases;
                const std::string &sa = outA.S().mString;
                const std::string &sb = outB.S().mString;
                if (ra != rb || sa != sb || sa.capacity() != sb.capacity() || inA.S().mString != inB.S().mString || baseA.S().mString != baseB.S().mString)
                    ctx.Fail("%s(in \"%s\", base \"%s\" %d): ret %d/%d out \"%s\"/\"%s\" cap %zu/%zu", name, in.c_str(), base.c_str(), useBase, ra, rb, sa.c_str(), sb.c_str(), sa.capacity(),
                             sb.capacity());
            }
        }
    }
}

DIFF_TEST(IniFile_ConcatMove, kConcat)
{
    auto original = ctx.Original<ConcatFn>(kConcat);
    std::mt19937 rng(0x2f5500);
    const size_t sizes[] = {0, 1, 5, 14, 15, 16, 17, 31, 32, 100, 400};
    for (size_t leftSize : sizes) {
        for (size_t rightSize : sizes) {
            for (int reserve = 0; reserve < 3; ++reserve) {
                std::string leftText, rightText;
                for (size_t i = 0; i < leftSize; ++i) leftText += static_cast<char>('a' + rng() % 26);
                for (size_t i = 0; i < rightSize; ++i) rightText += static_cast<char>('A' + rng() % 26);
                alignas(std::string) unsigned char outA[sizeof(std::string)], outB[sizeof(std::string)];
                std::memset(outA, 0x77, sizeof(outA));
                std::memset(outB, 0x77, sizeof(outB));
                std::string leftA = leftText, leftB = leftText, rightA = rightText, rightB = rightText;
                if (reserve) {
                    rightA.reserve(rightSize + leftSize + (reserve == 2 ? 50 : 0));
                    rightB.reserve(rightSize + leftSize + (reserve == 2 ? 50 : 0));
                }
                std::string *ra = original(reinterpret_cast<std::string *>(outA), &leftA, &rightA);
                std::string *rb = inifile::ConcatMove(reinterpret_cast<std::string *>(outB), &leftB, &rightB);
                ++ctx.cases;
                std::string &oa = *reinterpret_cast<std::string *>(outA);
                std::string &ob = *reinterpret_cast<std::string *>(outB);
                bool same = ra == reinterpret_cast<std::string *>(outA) && rb == reinterpret_cast<std::string *>(outB) && oa == ob && oa == leftText + rightText && oa.capacity() == ob.capacity() &&
                            rightA.empty() && rightB.empty() && rightA.capacity() == rightB.capacity() && leftA == leftB;
                if (!same) ctx.Fail("ConcatMove(%zu, %zu, reserve %d): out \"%.40s\"/\"%.40s\" cap %zu/%zu", leftSize, rightSize, reserve, oa.c_str(), ob.c_str(), oa.capacity(), ob.capacity());
            }
        }
    }
    // Inserting a string into itself.
    {
        std::string a = "selfcat", b = "selfcat";
        alignas(std::string) unsigned char outA[sizeof(std::string)], outB[sizeof(std::string)];
        original(reinterpret_cast<std::string *>(outA), &a, &a);
        inifile::ConcatMove(reinterpret_cast<std::string *>(outB), &b, &b);
        ++ctx.cases;
        if (*reinterpret_cast<std::string *>(outA) != *reinterpret_cast<std::string *>(outB)) ctx.Fail("self concat differs");
    }
}
