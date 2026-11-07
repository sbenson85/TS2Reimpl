#define _CRT_SECURE_NO_WARNINGS
// Differential test for the exception reporter's exception-code text (0x1bbb10, DescribeExceptionCode):
// 22 codes map to fixed names; every other code goes through GetModuleHandleA and FormatMessageA
// (import slots pointed at recording fakes for the life of a guard).
#define NOMINMAX
#include "app/ReportText.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kDescribe = 0x1bbb10;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatFormatMessageA = 0xd9a134;

std::vector<std::string> gLog;
std::string gScripted; // what the fake FormatMessageA writes
bool gFormatFails = false;

HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    gLog.push_back(std::string("module ") + (name ? name : "(null)"));
    return reinterpret_cast<HMODULE>(0x5150);
}

DWORD WINAPI FakeFormatMessageA(DWORD flags, const void *source, DWORD id, DWORD language, char *buffer, DWORD size, void *args)
{
    char line[200];
    std::snprintf(line, sizeof(line), "format %x %p %x %x size=%u args=%p first=%02x", flags, source, id, language, size, args, static_cast<uint8_t>(buffer[0]));
    gLog.push_back(line);
    if (gFormatFails) return 0;
    std::memcpy(buffer, gScripted.c_str(), gScripted.size()); // the text, no terminator: the buffer was zeroed
    return static_cast<DWORD>(gScripted.size());
}

class Imports {
public:
    explicit Imports(difftest::Context &c) : ctx(c) {}
    ~Imports()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t iatRva, void *fake)
    {
        void **slot = ctx.image.At<void *>(iatRva);
        saved.emplace_back(iatRva, *slot);
        *slot = fake;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

// The state of a cRZString that must match: vtable, text, size, capacity, reference count.
std::string StringState(cRZString &s)
{
    char head[120];
    std::snprintf(head, sizeof(head), "%p size=%zu cap=%zu ref=%u ", s.mVTable, s.mString.size(), s.mString.capacity(), s.mRefCount);
    return std::string(head) + s.mString.c_str();
}

} // namespace

DIFF_TEST(ReportText_DescribeExceptionCode, kDescribe)
{
    auto orig = ctx.Original<void(__stdcall *)(uint32_t, cRZString *)>(kDescribe);
    Imports imports(ctx);
    imports.Set(kIatGetModuleHandleA, reinterpret_cast<void *>(&FakeGetModuleHandleA));
    imports.Set(kIatFormatMessageA, reinterpret_cast<void *>(&FakeFormatMessageA));

    std::mt19937 rng(0x1bbb10);
    std::vector<uint32_t> codes = {0xc0000005, 0x80000002, 0x80000003, 0x80000004, 0xc000008c, 0xc000008d, 0xc000008e, 0xc000008f, 0xc0000090,
                                   0xc0000091, 0xc0000092, 0xc0000093, 0xc0000094, 0xc0000095, 0xc0000096, 0xc0000006, 0xc000001d, 0xc0000025,
                                   0xc00000fd, 0xc0000026, 0x80000001, 0xc0000008};
    // neighbours of every boundary and of every named code, then random codes
    for (size_t i = 0, n = codes.size(); i < n; ++i)
        for (int d = -2; d <= 2; ++d)
            if (d) codes.push_back(codes[i] + d);
    for (uint32_t c : {0u, 1u, 0x7fffffffu, 0x80000000u, 0x80000005u, 0xc0000000u, 0xc0000007u, 0xc0000097u, 0xc00000fcu, 0xc00000feu, 0xffffffffu, 0x40000015u, 0xc0000374u})
        codes.push_back(c);
    for (int i = 0; i < 400; ++i) codes.push_back(rng());

    static const char *const kInitial[] = {"", "x", "a short text", "sixteen chars...", "a string that is definitely longer than the small buffer of sixteen"};
    for (uint32_t code : codes) {
        for (int variant = 0; variant < 4; ++variant) {
            // The scripted message: empty, short, a typical line, the longest, and one with a newline.
            switch (variant) {
            case 0: gScripted = ""; break;
            case 1: gScripted = "The instruction at 0x%p referenced memory.\r\n"; break;
            case 2: gScripted = std::string(0x200, 'm'); break; // fills the whole buffer: only the last byte write ends it
            default: gScripted = "msg" + std::to_string(code); break;
            }
            gFormatFails = (variant == 0) && (code & 1);
            const char *initial = kInitial[rng() % 5];
            alignas(4) uint8_t rawA[sizeof(cRZString)], rawB[sizeof(cRZString)];
            for (size_t i = 0; i < sizeof(rawA); ++i) rawA[i] = rawB[i] = static_cast<uint8_t>(rng());
            auto *a = reinterpret_cast<cRZString *>(rawA);
            auto *b = reinterpret_cast<cRZString *>(rawB);
            a->Construct(initial);
            b->Construct(initial);

            gLog.clear();
            orig(code, a);
            std::string logA = Joined();
            gLog.clear();
            appbase::DescribeExceptionCode(code, b);
            std::string logB = Joined();

            ++ctx.cases;
            std::string sa = StringState(*a), sb = StringState(*b);
            if (logA != logB) ctx.Fail("code %08x variant %d: imports differ:\n  original: %s\n  ours:     %s", code, variant, logA.c_str(), logB.c_str());
            if (sa != sb) ctx.Fail("code %08x variant %d: string differs:\n  original: %.150s\n  ours:     %.150s", code, variant, sa.c_str(), sb.c_str());
            a->Destruct();
            b->Destruct();
        }
    }
}
