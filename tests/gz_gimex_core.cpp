// Differential tests for src/gz/Gimex.size.cpp: Gimex's format table helpers and dispatchers.
//
// The current format is the original's global 0x10e693c on the original side and ours (gimex::CurrentFormat) on ours:
// each side's is compared after the call, and both are put back afterwards. The formats' descriptions come from the
// codecs' own `about` functions (original code, run on both sides; their blocks freed with the codecs' free 0x87da72).
// The dispatchers reach the codecs through the tables (the image's 0x105b990 and ours, both holding the codecs' entry
// points): every entry point of rows 1..6 but `about` is patched to a recording fake for the test, so both sides log
// the same calls and get the same scripted answers. The null rows answer 0 on both sides (the image's shared
// `xor eax, eax` bodies and our stubs).
#include "code_patch.h"
#include "shape.h"
#include "difftest.h"
#include "game/game.h"
#include "impfx.h"
#include "gz/Gimex.h"
#include "gz_gimex_fakes.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFormatCount = 0x87d7f6;
constexpr uint32_t kSetFormat = 0x87d96b;
constexpr uint32_t kGetFormat = 0x87d919;
constexpr uint32_t kCurrentAbout = 0x87d7d0;
constexpr uint32_t kIs = 0x87d92c;
constexpr uint32_t kOpen = 0x87d939;
constexpr uint32_t kGetInfo = 0x87d91f;
constexpr uint32_t kRead = 0x87d946;
constexpr uint32_t kClose = 0x87d7dd;
constexpr uint32_t kWOpen = 0x87d9a0;
constexpr uint32_t kWrite = 0x87d9ad;
constexpr uint32_t kWClose = 0x87d987;
constexpr uint32_t kCompareNoCase = 0x87d78d;
constexpr uint32_t kFindFormat = 0x87d80f;
constexpr uint32_t kFreeMemory = 0x87da72;
constexpr uint32_t kCurrentCell = 0x10e693c;

int32_t &OriginalCurrent()
{
    return *GameAddress<int32_t>(kCurrentCell);
}

// Puts both current formats back when the test ends.
struct KeepCurrent {
    int32_t original = OriginalCurrent();
    int32_t ours = gimex::CurrentFormat();
    KeepCurrent() = default;
    KeepCurrent(const KeepCurrent &) = delete;
    KeepCurrent &operator=(const KeepCurrent &) = delete;
    ~KeepCurrent()
    {
        OriginalCurrent() = original;
        gimex::SetFormat(ours);
    }
};

using SetFormatFn = void(__stdcall *)(int32_t);
using GetFormatFn = int32_t(__cdecl *)();

DIFF_TEST(Gimex_FormatCount, 0x87d7f6)
{
    const int32_t a = ctx.Original<int32_t(__cdecl *)()>(kFormatCount)();
    const int32_t b = gimex::FormatCount();
    ++ctx.cases;
    if (a != b) ctx.Fail("FormatCount: original %d, ours %d", a, b);
}

// SetFormat and GetFormat (0x87d919), on ids in and out of the table.
DIFF_TEST(Gimex_SetFormat, 0x87d96b)
{
    KeepCurrent keep;
    std::vector<int32_t> ids = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 48, 49, 50, 51, 1000, INT32_MIN, INT32_MAX, -50};
    std::mt19937 rng(7);
    for (int i = 0; i < 40; ++i) ids.push_back(static_cast<int32_t>(rng() % 120) - 30);
    for (const int32_t id : ids) {
        OriginalCurrent() = 17;
        gimex::SetFormat(17);
        ctx.Original<SetFormatFn>(kSetFormat)(id);
        const int32_t a = OriginalCurrent();
        const int32_t ag = ctx.Original<GetFormatFn>(kGetFormat)();
        gimex::SetFormat(id);
        const int32_t b = gimex::CurrentFormat();
        const int32_t bg = gimex::GetFormat();
        ++ctx.cases;
        if (a != b || ag != bg) ctx.Fail("SetFormat(%d): original %d / %d, ours %d / %d", id, a, ag, b, bg);
    }
}

DIFF_TEST(Gimex_CompareNoCase, 0x87d78d)
{
    using Fn = int32_t(__cdecl *)(const char *, const char *);
    std::mt19937 rng(11);
    static const char kChars[] = "aAbBzZ@[`{.-_09\x7f\x80\xc1\xe1\xff";
    auto text = [&]() {
        std::string s;
        const uint32_t n = rng() % 7;
        for (uint32_t i = 0; i < n; ++i) s += kChars[rng() % (sizeof(kChars) - 1)];
        return s;
    };
    std::vector<std::pair<std::string, std::string>> pairs = {
        {"", ""}, {".BMP", ".bmp"}, {".bmp", ".bmpx"}, {"[", "{"}, {"@", "`"}, {"Z", "z"}, {"\xc1", "\xe1"}, {"a", ""}};
    for (int i = 0; i < 400; ++i) {
        std::string a = text();
        std::string b = rng() % 3 ? a : text();
        for (char &c : b)
            if (rng() % 2 && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        pairs.emplace_back(a, b);
    }
    for (const auto &p : pairs) {
        const int32_t a = ctx.Original<Fn>(kCompareNoCase)(p.first.c_str(), p.second.c_str());
        const int32_t b = gimex::CompareNoCase(p.first.c_str(), p.second.c_str());
        ++ctx.cases;
        if (a != b) ctx.Fail("CompareNoCase(\"%s\", \"%s\"): original %d, ours %d", p.first.c_str(), p.second.c_str(), a, b);
    }
}

// The codecs' descriptions through the current row (the codecs' own about functions on both sides).
DIFF_TEST(Gimex_CurrentAbout, 0x87d7d0)
{
    KeepCurrent keep;
    using Fn = gimex::About *(__stdcall *)();
    using FreeFn = int32_t(__stdcall *)(void *);
    impfx::Imports imp(ctx);
    imp.Libraries(); // the IJL row's about asks for ijl15.dll; no module is found on either side
    for (int32_t format = 0; format < gimex::kRows; ++format) {
        OriginalCurrent() = format;
        gimex::About *a = ctx.Original<Fn>(kCurrentAbout)();
        gimex::SetFormat(format);
        gimex::About *b = gimex::CurrentAbout();
        ++ctx.cases;
        if ((a == nullptr) != (b == nullptr)) {
            ctx.Fail("format %d: original %s, ours %s", format, a ? "a description" : "null", b ? "a description" : "null");
        } else if (a && std::memcmp(a, b, sizeof(gimex::About)) != 0) {
            ctx.Fail("format %d: the descriptions differ", format);
        }
        if (a) ctx.Original<FreeFn>(kFreeMemory)(a);
        if (b) gimex::FreeMemory(b);
    }
}

DIFF_TEST(Gimex_FindFormatByExtension, 0x87d80f)
{
    KeepCurrent keep;
    impfx::Imports imp(ctx);
    imp.Libraries(); // the real about entries run (the IJL row's asks for ijl15.dll once)
    using Fn = int32_t(__stdcall *)(const char *, int32_t *);
    std::vector<std::string> names = {"bmp", ".BMP", "-jpg", "+png", "tga", "TGA", "fsh", ".fsh", "jpeg", "JPG", "jpe",
                                      "png", "xyz", "", " bmp", "bmp?x", "bm p", "bmp\t", ".", "-", "+", "..bmp",
                                      "bmpx", "bm", "?bmp", "dib", "rle", "jfif", "qfs", "targa", "vda", "icb", "vst"};
    names.push_back(std::string(0x3fd, 'b'));
    names.push_back(std::string(0x3fe, 'x'));
    std::mt19937 rng(23);
    static const char kChars[] = "bmpjegntaf.-+?x ";
    for (int i = 0; i < 120; ++i) {
        std::string s;
        const uint32_t n = rng() % 6;
        for (uint32_t k = 0; k < n; ++k) s += kChars[rng() % (sizeof(kChars) - 1)];
        names.push_back(s);
    }
    for (const std::string &name : names) {
        for (int withSlot = 0; withSlot < 2; ++withSlot) {
            const int32_t start = static_cast<int32_t>(rng() % 7);
            int32_t slotA = 0x5a5a, slotB = 0x5a5a;
            OriginalCurrent() = start;
            const int32_t a = ctx.Original<Fn>(kFindFormat)(name.c_str(), withSlot ? &slotA : nullptr);
            const int32_t currentA = OriginalCurrent();
            gimex::SetFormat(start);
            const int32_t b = gimex::FindFormatByExtension(name.c_str(), withSlot ? &slotB : nullptr);
            const int32_t currentB = gimex::CurrentFormat();
            ++ctx.cases;
            if (a != b || slotA != slotB || currentA != currentB)
                ctx.Fail("\"%.40s\" (slot %d): original %d slot %d current %d, ours %d slot %d current %d", name.c_str(),
                         withSlot, a, slotA, currentA, b, slotB, currentB);
        }
    }
}

// FindFormatByExtension over scripted descriptions: the six formats' about entries are patched to fakes handing out
// descriptions from a script (overlapping extensions, "." entries, empty slots, flag bits 0..3, sometimes none), the
// null codec's (0x87d9d2, every other row) to one with no extensions, and
// the codecs' free 0x87da72 to a fake that logs what it frees. The result, the slot, the current format and the
// sequence of about calls and frees are compared.
namespace scripted {

gimex::About gPool[64];
uint32_t gNext;
gimex::About gScript[7]; // rows 1..6; 0 the null codec's (every other row)
bool gNone[7];           // the row's about answers null
std::vector<uint32_t> gLog; // the about's row, or 0x100 + the freed block's pool index

template <int Row> gimex::About *__stdcall FakeAbout()
{
    gLog.push_back(Row);
    if (gNone[Row]) return nullptr;
    gimex::About &a = gPool[gNext++ % 64];
    a = gScript[Row];
    return &a;
}

int32_t __stdcall FakeFree(void *memory)
{
    gLog.push_back(0x100 + static_cast<uint32_t>(static_cast<gimex::About *>(memory) - gPool));
    return 1;
}

} // namespace scripted

DIFF_TEST(Gimex_FindFormatScripted, 0x87d80f)
{
    using namespace scripted;
    KeepCurrent keep;
    using Fn = int32_t(__stdcall *)(const char *, int32_t *);
    shape::CodePatch abouts[] = {
        {0x87d9d2, gimexfx::Fn(&FakeAbout<0>)}, {0x87dbad, gimexfx::Fn(&FakeAbout<1>)},
        {0x87ec26, gimexfx::Fn(&FakeAbout<2>)}, {0x87f864, gimexfx::Fn(&FakeAbout<3>)},
        {0x8800be, gimexfx::Fn(&FakeAbout<4>)},
        {0x881267, gimexfx::Fn(&FakeAbout<5>)}, {0x882862, gimexfx::Fn(&FakeAbout<6>)},
    };
    // The about of a row that is ours (lane MN): the table of ours reaches it, not the image's code.
    shape::CodePatch oursBmp{gimexfx::Addr(&gimex::bmp::NewAbout), gimexfx::Fn(&FakeAbout<5>)};
    shape::CodePatch oursTga{gimexfx::Addr(&gimex::tga::NewAbout), gimexfx::Fn(&FakeAbout<6>)};
    shape::CodePatch oursIjl{gimexfx::Addr(&gimex::ijl::NewAbout), gimexfx::Fn(&FakeAbout<3>)};
    shape::CodePatch oursJpeg{gimexfx::Addr(&gimex::jpeg::NewAbout), gimexfx::Fn(&FakeAbout<2>)};
    shape::CodePatch oursPng{gimexfx::Addr(&gimex::png::NewAbout), gimexfx::Fn(&FakeAbout<4>)};
    shape::CodePatch oursFsh{gimexfx::Addr(&gimex::fsh::NewAbout), gimexfx::Fn(&FakeAbout<1>)};
    shape::CodePatch freeFake{kFreeMemory, gimexfx::Fn(&FakeFree)};
    shape::CodePatch oursFree{gimexfx::Addr(&gimex::FreeMemory), gimexfx::Fn(&FakeFree)}; // ours (lane MP)
    static const char *const kExtensions[] = {".bmp", ".BMP", ".jpg", ".x", ".", ".bm", ".png", ".JPG"};
    static const char *const kNames[] = {"bmp", "BMP", "jpg", "x", " x", "", ".bmp", "-jpg", "+x", "bm", "png",
                                         "jpg?", "x y", ".", "zz", "\x7f"};
    std::mt19937 rng(41);
    for (int round = 0; round < 1500; ++round) {
        for (int row = 1; row <= 6; ++row) {
            gimex::About &a = gScript[row];
            std::memset(&a, 0, sizeof(a));
            a.fourcc = static_cast<uint32_t>(row);
            a.size = sizeof(gimex::About);
            a.flags = rng() % 4;
            const uint32_t count = rng() % 9;
            for (uint32_t i = 0; i < count; ++i) {
                if (rng() % 5 == 0) continue; // an empty slot between others
                std::strcpy(a.extensions[i], kExtensions[rng() % (sizeof(kExtensions) / sizeof(kExtensions[0]))]);
            }
            gNone[row] = rng() % 12 == 0;
        }
        const char *const name = kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))];
        const bool withSlot = rng() % 2 != 0;
        const int32_t start = static_cast<int32_t>(rng() % 7);
        int32_t slot[2] = {0x5a5a, 0x5a5a}, result[2], current[2];
        std::vector<uint32_t> log[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gNext = 0;
            if (side) {
                gimex::SetFormat(start);
                result[1] = gimex::FindFormatByExtension(name, withSlot ? &slot[1] : nullptr);
                current[1] = gimex::CurrentFormat();
            } else {
                OriginalCurrent() = start;
                result[0] = ctx.Original<Fn>(kFindFormat)(name, withSlot ? &slot[0] : nullptr);
                current[0] = OriginalCurrent();
            }
            log[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1] || slot[0] != slot[1] || current[0] != current[1] || log[0] != log[1])
            ctx.Fail("round %d \"%s\" (slot %d): original %d slot %d current %d (%zu calls), ours %d slot %d current %d "
                     "(%zu calls)", round, name, withSlot, result[0], slot[0], current[0], log[0].size(), result[1],
                     slot[1], current[1], log[1].size());
    }
}

// ---- the dispatchers, over recording codec entry points ----

using namespace gimexfx;

enum Kind { kKindIs, kKindOpen, kKindInfo, kKindRead, kKindClose, kKindWOpen, kKindWrite, kKindWClose, kKinds };

// One dispatcher call on one side: the result (low byte only for the bool dispatchers) and the events.
uint32_t Dispatch(difftest::Context &ctx, bool ours, Kind kind, const uint32_t (&w)[4])
{
    auto *const instance = reinterpret_cast<gimex::Instance *>(static_cast<uintptr_t>(w[0]));
    auto *const stream = reinterpret_cast<ifaces::cIGZFile *>(static_cast<uintptr_t>(w[1]));
    auto *const out = reinterpret_cast<gimex::Instance **>(static_cast<uintptr_t>(w[2]));
    auto *const info = reinterpret_cast<gimex::Info *>(static_cast<uintptr_t>(w[1]));
    auto *const pixels = reinterpret_cast<void *>(static_cast<uintptr_t>(w[2]));
    const char *path = reinterpret_cast<const char *>(static_cast<uintptr_t>(w[3]));
    const int32_t n = static_cast<int32_t>(w[3]);
    switch (kind) {
    case kKindIs:
        return static_cast<uint32_t>(ours ? gimex::Is(stream) : ctx.Original<gimex::IsFn>(kIs)(stream));
    case kKindOpen:
        return static_cast<uint32_t>(ours ? gimex::Open(out, stream, path, n)
                                          : ctx.Original<gimex::OpenFn>(kOpen)(out, stream, path, n));
    case kKindInfo:
        return shape::P(ours ? gimex::GetInfo(instance, n) : ctx.Original<gimex::InfoFn>(kGetInfo)(instance, n));
    case kKindRead:
        return ours ? gimex::Read(instance, info, pixels, n)
                    : static_cast<uint8_t>(ctx.Original<gimex::ReadFn>(kRead)(instance, info, pixels, n));
    case kKindClose:
        return ours ? gimex::Close(instance) : static_cast<uint8_t>(ctx.Original<gimex::CloseFn>(kClose)(instance));
    case kKindWOpen:
        return static_cast<uint32_t>(ours ? gimex::WOpen(out, stream, path, n)
                                          : ctx.Original<gimex::WOpenFn>(kWOpen)(out, stream, path, n));
    case kKindWrite:
        return ours ? gimex::Write(instance, info, pixels, n)
                    : static_cast<uint8_t>(ctx.Original<gimex::WriteFn>(kWrite)(instance, info, pixels, n));
    default:
        return ours ? gimex::WClose(instance) : static_cast<uint8_t>(ctx.Original<gimex::WCloseFn>(kWClose)(instance));
    }
}

void RunDispatchers(difftest::Context &ctx, const Kind (&kinds)[2])
{
    KeepCurrent keep;
    CodecFakes fakes;
    const int32_t formats[] = {0, 1, 2, 3, 4, 5, 6, 7, 25, 49, 50, -1};
    std::mt19937 rng(31);
    for (const Kind kind : kinds) {
        for (const int32_t format : formats) {
            for (int round = 0; round < 6; ++round) {
                const uint32_t seed = rng();
                uint32_t w[4] = {0x10000 + (rng() & 0xffff0), 0x20000 + (rng() & 0xffff0), 0x30000 + (rng() & 0xffff0),
                                 rng() % 4 ? rng() % 9 : rng()};
                static uint32_t outSlot;
                if (kind == kKindOpen || kind == kKindWOpen) w[2] = round % 3 ? shape::P(&outSlot) : 0;
                uint32_t result[2], written[2];
                std::vector<Event> events[2];
                for (int side = 0; side < 2; ++side) {
                    gEvents.clear();
                    gAnswers.seed(seed);
                    outSlot = 0x5a5a5a5a;
                    if (side) {
                        gimex::SetFormat(format);
                    } else {
                        ctx.Original<SetFormatFn>(kSetFormat)(format);
                    }
                    result[side] = Dispatch(ctx, side != 0, kind, w);
                    events[side] = gEvents;
                    written[side] = outSlot;
                }
                ++ctx.cases;
                if (result[0] != result[1] || !(events[0] == events[1]) || written[0] != written[1])
                    ctx.Fail("kind %d format %d: original %08x (%zu calls, out %08x), ours %08x (%zu calls, out %08x)", kind,
                             format, result[0], events[0].size(), written[0], result[1], events[1].size(), written[1]);
            }
        }
    }
}

// Each dispatcher test names its pair: is / open (0x87d939), info (0x87d91f) / read (0x87d946), close (0x87d7dd) / wopen
// (0x87d9a0), write (0x87d9ad) / wclose (0x87d987).
DIFF_TEST(Gimex_Is, 0x87d92c)
{
    RunDispatchers(ctx, {kKindIs, kKindOpen});
}
DIFF_TEST(Gimex_GetInfo, 0x87d91f)
{
    RunDispatchers(ctx, {kKindInfo, kKindRead});
}
DIFF_TEST(Gimex_Close, 0x87d7dd)
{
    RunDispatchers(ctx, {kKindClose, kKindWOpen});
}
DIFF_TEST(Gimex_Write, 0x87d9ad)
{
    RunDispatchers(ctx, {kKindWrite, kKindWClose});
}

} // namespace
