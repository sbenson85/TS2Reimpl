// Differential tests for the temporary-file name makers of src/rz/PathFileOps.* (lane QN): GetTempDirectory (0x1c1f9c),
// TrimToByteLength (0x1c236b), MakeTempFileIn (0x1c23bf) and MakeTempFile (0x1c255e).
//
// What the harness cannot run is stood in on both sides alike (shape::StandIn): cRZString::Sprintf (formatting reaches
// uninitialised CRT state), the cRZFile members the loop uses (a fake file system: a set of taken names), the two string
// conversions GetTempDirectory ends with (logging fakes). The CRT's time is patched on the image and on the host to one value, so a
// test can make the first names "taken" and see which one wins. The wide and ANSI system calls are fakes in the game's cells.
#include "difftest.h"
#include "code_patch.h"
#include "game/game.h"
#include "gz/GZCOM.h"
#include "rz/PathFileOps.h"
#include "rz/RZTextConvert.h"
#include "rz/WinApiTables.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "app/AppServices2.h"

#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr int64_t kNow = 1700000000;

std::vector<std::string> gLog;
std::set<std::string> gTaken;
std::map<const void *, std::string> gPaths;

void Log(const char *fmt, ...)
{
    char text[600];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    gLog.push_back(text);
}

// ---- Stands ----
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    self->mString = text;
    return self;
}
void *__cdecl HostNew(size_t n) { return ::operator new(n); }
void __cdecl HostDelete(void *p, size_t) { ::operator delete(p); }
int64_t __cdecl StandTime(int64_t *out)
{
    if (out) *out = kNow;
    return kNow;
}

cRZFile *__fastcall StandFileCtor(cRZFile *self, void *, const cIGZString &path)
{
    gPaths[self] = path.ToChar();
    return self;
}
bool __fastcall StandFileOpen(cRZFile *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    const std::string &p = gPaths[self];
    const bool free = gTaken.insert(p).second;
    Log("open %s %u %u %u -> %d", p.c_str(), access, creation, sharing, free);
    return free;
}
bool __fastcall StandFileClose(cRZFile *self, void *)
{
    Log("close %s", gPaths[self].c_str());
    return true;
}
void __fastcall StandFileDestruct(cRZFile *self, void *)
{
    Log("destruct %s", gPaths[self].c_str());
    gPaths.erase(self);
}
uint32_t __cdecl StandConvertBytes(const void *src, uint32_t length, int32_t encoding, cIGZString *dst)
{
    std::string narrow;
    const wchar_t *w = static_cast<const wchar_t *>(src);
    for (uint32_t i = 0; i < length; ++i) narrow.push_back(static_cast<char>(w[i]));
    Log("bytes %u enc %d '%s'", length, encoding, narrow.c_str());
    dst->FromChar(narrow.c_str(), static_cast<uint32_t>(narrow.size()));
    return length;
}
bool __cdecl StandConvertFrom(const cIGZString *src, int32_t encoding, cIGZString *dst)
{
    Log("from enc %d '%s'", encoding, src->ToChar());
    dst->FromChar(src->ToChar(), src->Strlen());
    return true;
}

struct Stands {
    shape::CodePatch imageNew{0x824632, reinterpret_cast<const void *>(&HostNew)};
    shape::CodePatch imageDelete{0x824624, reinterpret_cast<const void *>(&HostDelete)};
    shape::CodePatch imageTime{0x82fbf6, reinterpret_cast<const void *>(&StandTime)};
    shape::CodePatch hostTime{reinterpret_cast<void *>(&_time64), reinterpret_cast<const void *>(&StandTime)};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    shape::StandIn fileCtor{0x1c0c07, static_cast<cRZFile *(cRZFile::*)(const cIGZString &)>(&cRZFile::Construct), &StandFileCtor};
    shape::StandIn fileOpen{0x1c285d, &cRZFile::Open, &StandFileOpen};
    shape::StandIn fileClose{0x1c0faa, &cRZFile::Close, &StandFileClose};
    shape::StandIn fileDestruct{0x1c0e6d, &cRZFile::Destruct, &StandFileDestruct};
};

struct Str {
    alignas(8) unsigned char raw[sizeof(cRZString)];
    cRZString *s;
    explicit Str(const char *text)
    {
        s = reinterpret_cast<cRZString *>(raw);
        s->Construct(text);
    }
    ~Str() { s->Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;
};

using TrimFn = void(__cdecl *)(cRZString *, uint32_t);
using MakeInFn = uint32_t(__cdecl *)(const cIGZString *, cIGZString *, const cIGZString *);
using MakeFn = uint32_t(__cdecl *)(cIGZString *, const cIGZString *);
using TempDirFn = uint32_t(__cdecl *)(cIGZString *);

// The names the fake file system holds when the first `count` names for a prefix and suffix are taken.
void TakeNames(const std::string &dir, const std::string &prefix, const std::string &suffix, int count)
{
    gTaken.clear();
    for (int i = 0; i < count; ++i) gTaken.insert(dir + prefix + std::to_string(kNow - i) + suffix);
}

// ---- GetTempDirectory cells ----
std::wstring gWideTemp, gWideLong;
std::string gAnsiTemp, gAnsiLong;

DWORD WINAPI FakeTempPathW(DWORD size, LPWSTR out)
{
    Log("tempW %u", size);
    if (gWideTemp.empty()) return 0;
    std::wmemcpy(out, gWideTemp.c_str(), gWideTemp.size() + 1);
    return static_cast<DWORD>(gWideTemp.size());
}
DWORD WINAPI FakeLongW(LPCWSTR in, LPWSTR out, DWORD size)
{
    Log("longW %u same %d", size, in == out);
    std::wmemcpy(out, gWideLong.c_str(), gWideLong.size() + 1);
    return static_cast<DWORD>(gWideLong.size());
}
DWORD WINAPI FakeTempPathA(DWORD size, LPSTR out)
{
    Log("tempA %u", size);
    if (gAnsiTemp.empty()) return 0;
    std::memcpy(out, gAnsiTemp.c_str(), gAnsiTemp.size() + 1);
    return static_cast<DWORD>(gAnsiTemp.size());
}
DWORD WINAPI FakeLongA(LPCSTR in, LPSTR out, DWORD size)
{
    Log("longA %u same %d", size, in == out);
    std::memcpy(out, gAnsiLong.c_str(), gAnsiLong.size() + 1);
    return static_cast<DWORD>(gAnsiLong.size());
}

} // namespace

DIFF_TEST(PathTempNames_Trim, 0x1c236b)
{
    auto orig = ctx.Original<TrimFn>(0x1c236b);
    Stands stands;
    const char *texts[] = {
        "",
        "a",
        "abcdef",
        "abcdefghijklmnopqrstuvwxyz",
        "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",          // two-byte characters
        "a\xe2\x82\xac" "b\xe2\x82\xac",              // three-byte
        "\xf0\x9f\x98\x80" "xy",                      // four-byte
        "\xc3",                                        // a lone lead byte
        "ab\xc3",                                      // a lead byte at the end
        "\xe2\x82",                                    // a truncated three-byte character
        "\x80\x80\x41",                                // stray continuation bytes
        "\xfc\x80\x80\x80\x80\x80z",                   // six-byte form
    };
    int trimmed = 0;
    for (const char *t : texts) {
        const size_t len = std::strlen(t);
        for (uint32_t max = 0; max <= len + 2; ++max) {
            Str a(t), b(t);
            orig(a.s, max);
            TrimToByteLength(b.s, max);
            if (a.s->mString != b.s->mString) ctx.Fail("'%s' max %u: original '%s', ours '%s'", t, max, a.s->mString.c_str(), b.s->mString.c_str());
            if (a.s->mString.size() < len) ++trimmed;
        }
    }
    {
        // Past the 244-byte cut the makers use, and a long text that fits.
        std::string longText(300, 'q');
        for (uint32_t max : {0xf4u, 300u, 299u, 301u}) {
            Str a(longText.c_str()), b(longText.c_str());
            orig(a.s, max);
            TrimToByteLength(b.s, max);
            if (a.s->mString != b.s->mString) ctx.Fail("long text max %u differs", max);
        }
    }
    if (trimmed < 20) ctx.Fail("setup reaches too little (%d trims)", trimmed);
}

DIFF_TEST(PathTempNames_Directory, 0x1c1f9c)
{
    auto orig = ctx.Original<TempDirFn>(0x1c1f9c);
    Stands stands;
    shape::StandIn convertBytes(0x1afc9d, &ConvertBytesToString, &StandConvertBytes);
    shape::StandIn convertFrom(0x1b0356, &as2::ConvertStringFrom, &StandConvertFrom);
    auto &table = ts2_rva_10cb5e0;
    void *saved[4] = {table.getTempPath, table.getLongPathName, table.getLongPathNameA, ts2_rva_d9a21c};
    int succeeded = 0;
    // Which cells are filled: wide pair; wide temp alone; ANSI long name present or not.
    for (int variant = 0; variant < 6; ++variant) {
        const bool wideTemp = variant < 3, wideLong = variant < 2;
        const bool ansiLong = variant != 3 && variant != 5;
        for (int empty = 0; empty < 2; ++empty) {
            table.getTempPath = wideTemp ? reinterpret_cast<void *>(&FakeTempPathW) : nullptr;
            table.getLongPathName = wideLong ? reinterpret_cast<void *>(&FakeLongW) : nullptr;
            table.getLongPathNameA = ansiLong ? reinterpret_cast<void *>(&FakeLongA) : nullptr;
            ts2_rva_d9a21c = reinterpret_cast<void *>(&FakeTempPathA);
            gWideTemp = empty ? L"" : L"C:\\DOCUME~1\\u\\LOCALS~1\\Temp\\";
            gWideLong = L"C:\\Documents and Settings\\u\\Local Settings\\Temp\\";
            gAnsiTemp = empty ? "" : "C:\\SHORT~1\\Temp\\";
            gAnsiLong = "C:\\Short Name\\Temp\\";
            std::vector<std::string> logs;
            std::vector<std::string> texts;
            std::vector<uint32_t> results;
            for (int side = 0; side < 2; ++side) {
                Str out("untouched");
                gLog.clear();
                const bool r = side == 0 ? (orig(out.s->AsInterface()) & 0xff) != 0 : GetTempDirectory(out.s->AsInterface());
                results.push_back(r);
                texts.push_back(out.s->mString);
                std::string joined;
                for (auto &l : gLog) joined += l + "|";
                logs.push_back(joined);
            }
            if (results[0] != results[1]) ctx.Fail("variant %d empty %d: results %u / %u", variant, empty, results[0], results[1]);
            if (texts[0] != texts[1]) ctx.Fail("variant %d empty %d: text '%s' / '%s'", variant, empty, texts[0].c_str(), texts[1].c_str());
            if (logs[0] != logs[1]) ctx.Fail("variant %d empty %d: calls '%s' / '%s'", variant, empty, logs[0].c_str(), logs[1].c_str());
            if (results[0] && texts[0] == "untouched") ctx.Fail("variant %d: success left the text alone", variant);
            if (!results[0] && texts[0] != "untouched") ctx.Fail("variant %d: failure touched the text", variant);
            succeeded += results[0];
        }
    }
    table.getTempPath = saved[0];
    table.getLongPathName = saved[1];
    table.getLongPathNameA = saved[2];
    ts2_rva_d9a21c = saved[3];
    if (succeeded != 6) ctx.Fail("setup reaches too little (%d successes)", succeeded);
}

DIFF_TEST(PathTempNames_MakeIn, 0x1c23bf)
{
    auto orig = ctx.Original<MakeInFn>(0x1c23bf);
    Stands stands;
    struct Case {
        const char *dir, *name, *suffix;
        int taken;
    };
    const std::string longName(300, 'n');
    const std::string edgeName = std::string(243, 'e') + "\xc3\xa9"; // the two-byte character straddles the 244-byte cut
    const Case cases[] = {
        {"C:\\T\\", "report", nullptr, 0},
        {"C:\\T\\", "report", ".dat", 0},
        {"C:\\T\\", "report", "", 0},
        {"C:\\T\\", "", nullptr, 0},
        {"C:\\T\\", "", ".x", 3},
        {"", "rel", nullptr, 1},
        {"C:\\T\\", "report", nullptr, 1},
        {"C:\\T\\", "report", nullptr, 7},
        {"C:\\T\\", "report", ".dat", 9999},
        {"C:\\T\\", "report", nullptr, 10000},
        {"C:\\T\\", "report", nullptr, 10001},
        {"D:\\", longName.c_str(), nullptr, 2},
        {"D:\\", edgeName.c_str(), ".b", 0},
    };
    int made = 0, failed = 0;
    for (const Case &c : cases) {
        std::string results[2], logs[2], names[2];
        uint32_t rv[2];
        for (int side = 0; side < 2; ++side) {
            Str dir(c.dir), name(c.name);
            Str suffix(c.suffix ? c.suffix : "");
            const cIGZString *suffixArg = c.suffix ? suffix.s->AsInterface() : nullptr;
            // The cut prefix the caller's text becomes (the same on both sides).
            std::string prefix = c.name;
            if (prefix.size() > 0xf4) prefix.resize(0xf4);
            if (c.name == edgeName) prefix = std::string(243, 'e');
            if (prefix.empty()) prefix = "RZ_";
            TakeNames(c.dir, prefix, c.suffix ? c.suffix : ".tmp", c.taken);
            gLog.clear();
            gPaths.clear();
            const bool r = side == 0 ? (orig(dir.s->AsInterface(), name.s->AsInterface(), suffixArg) & 0xff) != 0
                                     : MakeTempFileIn(dir.s->AsInterface(), name.s->AsInterface(), suffixArg);
            rv[side] = r;
            names[side] = name.s->mString;
            std::string joined;
            for (auto &l : gLog) joined += l + "|";
            logs[side] = joined;
            if (!gPaths.empty()) ctx.Fail("'%s': a file object leaked", c.name);
        }
        if (rv[0] != rv[1]) ctx.Fail("dir '%s' name '%.20s' taken %d: results %u / %u", c.dir, c.name, c.taken, rv[0], rv[1]);
        if (names[0] != names[1]) ctx.Fail("dir '%s' name '%.20s' taken %d: name '%.60s' / '%.60s'", c.dir, c.name, c.taken, names[0].c_str(), names[1].c_str());
        if (logs[0] != logs[1]) ctx.Fail("dir '%s' name '%.20s' taken %d: file calls differ", c.dir, c.name, c.taken);
        if (rv[0]) {
            ++made;
            // The winning name is the first free one: time minus the number taken.
            const std::string tail = std::to_string(kNow - c.taken);
            if (names[0].find(tail) == std::string::npos) ctx.Fail("taken %d: winning name '%.60s' lacks %s", c.taken, names[0].c_str(), tail.c_str());
        } else {
            ++failed;
            if (names[0] != c.name) ctx.Fail("taken %d: a failed call changed the name", c.taken);
        }
    }
    if (made < 8 || failed < 2) ctx.Fail("setup reaches too little (%d made, %d failed)", made, failed);
}

DIFF_TEST(PathTempNames_Make, 0x1c255e)
{
    auto orig = ctx.Original<MakeFn>(0x1c255e);
    Stands stands;
    shape::StandIn convertBytes(0x1afc9d, &ConvertBytesToString, &StandConvertBytes);
    shape::StandIn convertFrom(0x1b0356, &as2::ConvertStringFrom, &StandConvertFrom);
    auto &table = ts2_rva_10cb5e0;
    void *saved[4] = {table.getTempPath, table.getLongPathName, table.getLongPathNameA, ts2_rva_d9a21c};
    table.getTempPath = reinterpret_cast<void *>(&FakeTempPathW);
    table.getLongPathName = reinterpret_cast<void *>(&FakeLongW);
    gWideLong = L"C:\\Users\\u\\Temp\\";
    int made = 0;
    for (int emptyDir = 0; emptyDir < 2; ++emptyDir) {
        gWideTemp = emptyDir ? L"" : L"C:\\U~1\\Temp\\";
        const std::string dir = emptyDir ? "" : "C:\\Users\\u\\Temp\\";
        for (int suffixCase = 0; suffixCase < 3; ++suffixCase) {
            for (int taken : {0, 4}) {
                const char *suffixText = suffixCase == 0 ? nullptr : (suffixCase == 1 ? ".sim" : "");
                std::string names[2], logs[2];
                uint32_t rv[2];
                for (int side = 0; side < 2; ++side) {
                    Str name("save"), suffix(suffixText ? suffixText : "");
                    TakeNames(dir, "save", suffixText ? suffixText : ".tmp", taken);
                    gLog.clear();
                    gPaths.clear();
                    rv[side] = side == 0 ? (orig(name.s->AsInterface(), suffixText ? suffix.s->AsInterface() : nullptr) & 0xff)
                                         : MakeTempFile(name.s->AsInterface(), suffixText ? suffix.s->AsInterface() : nullptr);
                    names[side] = name.s->mString;
                    std::string joined;
                    for (auto &l : gLog) joined += l + "|";
                    logs[side] = joined;
                }
                if (rv[0] != rv[1] || names[0] != names[1] || logs[0] != logs[1])
                    ctx.Fail("emptyDir %d suffix %d taken %d: '%s' (%u) / '%s' (%u)", emptyDir, suffixCase, taken, names[0].c_str(), rv[0], names[1].c_str(), rv[1]);
                if (rv[0]) {
                    ++made;
                    if (names[0].compare(0, dir.size(), dir) != 0) ctx.Fail("name '%s' not in the directory", names[0].c_str());
                    if (names[0].find(std::to_string(kNow - taken)) == std::string::npos) ctx.Fail("taken %d: wrong winner '%s'", taken, names[0].c_str());
                }
            }
        }
    }
    table.getTempPath = saved[0];
    table.getLongPathName = saved[1];
    table.getLongPathNameA = saved[2];
    ts2_rva_d9a21c = saved[3];
    if (made < 10) ctx.Fail("setup reaches too little (%d made)", made);
}
