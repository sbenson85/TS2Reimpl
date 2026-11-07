// The two text helpers cGZCOM's RealInit and the library loader use: ConvertBytesToString (0x1afc9d)
// and GetModulePath (0x1b139f). See gz_com_support.h.
#include "gz_com_support.h"

namespace {

constexpr uint32_t kSystemCodePageCache = 0xff23a4; // RZText::SystemCodePage's cache, -3 until first use

std::string Dump(const std::string &text)
{
    std::string s;
    char buf[8];
    for (unsigned char c : text) {
        std::snprintf(buf, sizeof(buf), "%02x", c);
        s += buf;
    }
    return s;
}

// A string whose FromChar (slot 3) is recorded and then forwarded to the real one.
void *gFromCharVt[40];
void (__fastcall *gRealFromChar)(void *, void *, const char *, uint32_t) = nullptr;
void __fastcall RecordFromChar(void *self, void *edx, const char *source, uint32_t length)
{
    Log("FromChar len=%u [%s]", length, Dump(std::string(source, length)).c_str());
    gRealFromChar(self, edx, source, length);
}

} // namespace

DIFF_TEST(GZCOMText_ConvertBytesToString, 0x1afc9d)
{
    std::memcpy(gFromCharVt, ctx.image.At<void *>(cRZString::kVTable), sizeof(gFromCharVt));
    gRealFromChar = reinterpret_cast<decltype(gRealFromChar)>(gFromCharVt[3]);
    gFromCharVt[3] = reinterpret_cast<void *>(&RecordFromChar);
    std::mt19937 rng(21);

    struct Case {
        std::vector<unsigned char> bytes;
        uint32_t length;
        int32_t encoding;
    };
    std::vector<Case> cases;
    auto bytesOf = [](const std::string &s) { return std::vector<unsigned char>(s.begin(), s.end()); };
    auto wide = [](const std::u16string &s) {
        std::vector<unsigned char> v;
        for (char16_t c : s) {
            v.push_back(c & 0xff);
            v.push_back(c >> 8);
        }
        return v;
    };
    for (int32_t enc : {-2, -1, 0, 8, 16, 1252, 437, 932}) {
        cases.push_back({bytesOf("plain ascii"), 11, enc});
        cases.push_back({bytesOf(""), 0, enc});
        cases.push_back({bytesOf("caf\xe9 \x80 \xff"), 8, enc});
        cases.push_back({bytesOf("caf\xc3\xa9 \xe2\x82\xac"), 10, enc});
        cases.push_back({wide(u"wide \u00e9\u20ac\u3042 text"), 14, enc});
        cases.push_back({wide(u"abc"), 3, enc});
        for (uint32_t length : {84u, 85u, 86u, 87u, 200u}) {
            std::vector<unsigned char> bytes(length * 2);
            for (auto &b : bytes) b = static_cast<unsigned char>(0x20 + rng() % 90);
            cases.push_back({bytes, length, enc});
        }
    }
    for (const Case &c : cases) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
            cRZString dst;
            dst.Construct("previous text");
            dst.mVTable = gFromCharVt;
            gLog.clear();
            uint32_t r = side == 0 ? ctx.Original<uint32_t(__cdecl *)(const void *, uint32_t, int32_t, void *)>(kConvertBytes)(c.bytes.data(), c.length, c.encoding, &dst)
                                   : ConvertBytesToString(c.bytes.data(), c.length, c.encoding, dst.AsInterface());
            results[side] = std::to_string(r) + " [" + Dump(dst.mString) + "] " + Joined(gLog);
            dst.mVTable = GameAddress(cRZString::kVTable);
            dst.Destruct();
        }
        char what[64];
        std::snprintf(what, sizeof(what), "ConvertBytesToString len=%u enc=%d", c.length, c.encoding);
        ExpectSame(ctx, what, results[0], results[1]);
    }
}

namespace {

HMODULE gHandleResult = nullptr;
const wchar_t *gWideName = L"";
const char *gNarrowName = "";
int gWideCalls = 0, gNarrowCalls = 0, gHandleCalls = 0;
const wchar_t *gSeenWideName = nullptr;

HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    ++gHandleCalls;
    Log("GetModuleHandleA %s", name);
    return gHandleResult;
}
DWORD WINAPI FakeNarrow(void *module, char *buffer, DWORD size)
{
    ++gNarrowCalls;
    Log("GetModuleFileNameA module=%p size=%u", module, size);
    std::strcpy(buffer, gNarrowName);
    return static_cast<DWORD>(std::strlen(gNarrowName));
}
DWORD WINAPI FakeWide(void *module, wchar_t *buffer, DWORD size)
{
    ++gWideCalls;
    Log("GetModuleFileNameW module=%p size=%u", module, size);
    std::wcscpy(buffer, gWideName);
    return static_cast<DWORD>(std::wcslen(gWideName));
}

} // namespace

DIFF_TEST(GZCOMText_GetModulePath, 0x1b139f)
{
    IatFakes iat(ctx);
    iat.Set(kIatGetModuleHandleA, &FakeGetModuleHandleA);
    iat.Set(kIatGetModuleFileNameA, &FakeNarrow);
    uint8_t *resolved = ctx.image.At<uint8_t>(0x10caeb5);
    void **cached = ctx.image.At<void *>(0x10caeb8);
    uint8_t savedResolved = *resolved;
    void *savedCached = *cached;
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    void *realWide = reinterpret_cast<void *>(GetProcAddress(kernel, "GetModuleFileNameW"));

    struct Case {
        const char *name;
        uint8_t resolved;
        void *cached;
        HMODULE handle;
        const wchar_t *wide;
        const char *narrow;
        const char *preset;
        bool realProc = false; // the real GetProcAddress instead of the fake
    };
    const Case table[] = {
        {"first use, lookup answered by the fake (null)", 0, nullptr, kernel, L"", "C:\ansi\game.exe", ""},
        {"first use, real GetProcAddress", 0, nullptr, kernel, L"", "", "", true},
        {"first use, no module handle, cache empty", 0, nullptr, nullptr, L"", "C:\\ansi\\game.exe", ""},
        {"first use, no module handle, cache set", 0, realWide, nullptr, L"", "C:\\ansi\\game.exe", ""},
        {"resolved, wide result", 1, reinterpret_cast<void *>(&FakeWide), kernel, L"C:\\Games\\The Sims 2\\caf\u00e9 \u20ac.exe", "ignored", ""},
        {"resolved, wide result empty -> narrow", 1, reinterpret_cast<void *>(&FakeWide), kernel, L"", "C:\\ansi\\game \xe9.exe", ""},
        {"resolved, wide empty and narrow empty", 1, reinterpret_cast<void *>(&FakeWide), kernel, L"", "", ""},
        {"resolved, no function -> narrow", 1, nullptr, kernel, L"never", "C:\\ansi\\game.exe", ""},
        {"resolved, preset text kept when wide is empty", 1, reinterpret_cast<void *>(&FakeWide), kernel, L"", "C:\\ansi\\game.exe", "preset"},
        {"resolved, wide replaces the preset", 1, reinterpret_cast<void *>(&FakeWide), kernel, L"D:\\x.exe", "C:\\ansi\\game.exe", "preset"},
        {"resolved, real function", 1, realWide, kernel, L"", "", ""},
        {"flag is any non-zero byte", 0x80, reinterpret_cast<void *>(&FakeWide), kernel, L"E:\\y.exe", "", ""},
    };
    for (const Case &c : table) {
        std::string results[2];
        void *fakeProc = *ctx.image.At<void *>(kIatGetProcAddress);
        if (c.realProc) *ctx.image.At<void *>(kIatGetProcAddress) = reinterpret_cast<void *>(GetProcAddress);
        for (int side = 0; side < 2; ++side) {
            *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
            *resolved = c.resolved;
            *cached = c.cached;
            gHandleResult = c.handle;
            gWideName = c.wide;
            gNarrowName = c.narrow;
            cRZString dst;
            dst.Construct(c.preset);
            gLog.clear();
            uint32_t r = side == 0 ? ctx.Original<uint32_t(__cdecl *)(void *)>(kModulePath)(&dst) : GetModulePath(dst.AsInterface());
            char state[64];
            std::snprintf(state, sizeof(state), " flag=%d cache=%s", *resolved, *cached == c.cached ? "same" : *cached == realWide ? "real" : "other");
            // With the real kernel32 the text is the test program's own path; both sides ask the
            // same API in the same process.
            results[side] = std::to_string(r) + " [" + dst.mString + "]" + state + " " + Joined(gLog);
            dst.Destruct();
        }
        *ctx.image.At<void *>(kIatGetProcAddress) = fakeProc;
        ExpectSame(ctx, c.name, results[0], results[1]);
    }
    *resolved = savedResolved;
    *cached = savedCached;
}
