// Differential tests for the log-settings cheat files and the allocator helpers behind them
// (src/app/AppDebugger.*):
//   0x1e7190 AllocateListNodes, 0x152b80 FreeStringBuffer, 0x1d5ea0 DestroyStringListNodes,
//   0xcb0640 / 0xcb0650 / 0xcb0660 the cheat global's setters, 0xcb0760 LoadCheatServices,
//   0xcb0690 PrepareCheatGlobal, 0xcaf560 OpenCheatFile, 0xcaf060 LoadCheatFile, 0xcaf090 LoadLogSettingsCheats.
//
// Each side (original, then ours) runs against the same fakes (tests/app_debugger_fakes.h): the framework
// (game global 0x10c9378), the cTSAppCommonImpl object (cache 0x10cc040), the allocator service (cache
// 0x10c95d4), and the cheat global's stream. The calls they receive are logged and compared, and so is
// every object the code leaves behind (the global's impl, list nodes).
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "difftest.h"

#define NOMINMAX
#include <windows.h>

#include <climits>
#include <cstring>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace appdebugger;
using adt::kAllocator;
using adt::kCommonImpl;
using adt::kConsoleService;
using adt::kFramework;
using adt::kLogService;
using adt::kStreamBase;

constexpr uint32_t kFrameworkGlobal = 0x10c9378, kCommonCache = 0x10cc040;

std::string Utf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool WriteFileText(const std::wstring &path, const std::string &text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(h);
    return ok;
}

// ---- a std::list<std::string> node, as raw memory --------------------------------------------------------------

struct Node {
    Node *next;
    Node *prev;
    std::string text;
};
static_assert(sizeof(Node) == 0x20, "node layout");

struct BuiltList {
    Node *sentinel;
    std::vector<Node *> nodes;
};

// Builds a circular list (sentinel + one node per text) in blocks filled with `seed`'s random bytes.
BuiltList BuildList(const std::vector<std::string> &texts, uint32_t seed, adt::AllocatorFake *track)
{
    std::mt19937 rng(seed);
    BuiltList list;
    auto make = [&]() {
        auto *n = static_cast<Node *>(::operator new(sizeof(Node)));
        for (size_t i = 0; i < sizeof(Node); ++i) reinterpret_cast<uint8_t *>(n)[i] = static_cast<uint8_t>(rng());
        if (track) track->Track(n);
        return n;
    };
    list.sentinel = make();
    list.sentinel->next = list.sentinel->prev = list.sentinel;
    for (const std::string &t : texts) {
        Node *n = make();
        new (&n->text) std::string(t);
        n->next = list.sentinel;
        n->prev = list.sentinel->prev;
        list.sentinel->prev->next = n;
        list.sentinel->prev = n;
        list.nodes.push_back(n);
    }
    return list;
}

// ---- the cheat global ------------------------------------------------------------------------------------------

struct CheatSide {
    LogCheatImpl *impl;
    explicit CheatSide(uint32_t seed, const std::string &name)
    {
        impl = new LogCheatImpl{};
        std::mt19937 rng(seed);
        impl->mField0 = rng();
        impl->mServiceA = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        impl->mServiceB = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        impl->mName = name;
    }
    ~CheatSide() { delete impl; }
};

std::string DescribeImpl(const LogCheatImpl *impl)
{
    char buf[120];
    std::snprintf(buf, sizeof(buf), "f0=%x a=%p b=%p cap=%zu size=%zu text=", impl->mField0, impl->mServiceA, impl->mServiceB, impl->mName.capacity(), impl->mName.size());
    return std::string(buf) + impl->mName.c_str();
}

// A stream-like object: a virtual base pointer at +8 whose dword at +8 is the distance from the object + 8 to
// the base (a fake of kind kStreamBase) whose slot 12 the code calls.
struct StreamFake {
    adt::Fake base{kStreamBase};
    alignas(8) uint8_t memory[0x30] = {};
    int32_t vbtable[4] = {0x11, 0x22, 0, 0x33};
    StreamFake()
    {
        vbtable[2] = static_cast<int32_t>(reinterpret_cast<uintptr_t>(base.get()) - (reinterpret_cast<uintptr_t>(memory) + 8));
        *reinterpret_cast<void **>(memory + 8) = vbtable;
    }
    void *object() { return memory; }
};

void DefineStream()
{
    adt::Def(kStreamBase, 12, 2, "si");
}

// The fake cTSAppCommonImpl: slot 4 writes the option text, slot 5 fills the list with the directories.
std::string gOptionText;
std::vector<std::string> gDirectories;
adt::AllocatorFake *gAllocator = nullptr;

void DefineCommonImpl()
{
    adt::Def(kCommonImpl, 4, 5, "iSiii", [](void *, const uint32_t *args) {
        if (args[0] == 0x4d4896ba) reinterpret_cast<cRZString *>(args[1])->mString = gOptionText;
        return 1u;
    });
    adt::Def(kCommonImpl, 5, 5, "ioiii", [](void *, const uint32_t *args) {
        auto *header = reinterpret_cast<ListHeader *>(args[1]);
        auto *sentinel = static_cast<Node *>(header->mHead);
        for (const std::string &dir : gDirectories) {
            auto *n = static_cast<Node *>(::operator new(sizeof(Node)));
            gAllocator->Track(n);
            new (&n->text) std::string(dir);
            n->next = sentinel;
            n->prev = sentinel->prev;
            sentinel->prev->next = n;
            sentinel->prev = n;
        }
        return 1u;
    });
}

// The fake framework: slot 5 answers the service queries by id.
void *gAnswerOther = nullptr, *gAnswerLog = nullptr, *gAnswerAllocator = nullptr;

void DefineFramework()
{
    adt::Def(kFramework, 5, 3, "iio", [](void *, const uint32_t *args) {
        void *answer = nullptr;
        if (args[0] == 0x0b6b619a && args[1] == 0xeb6b6192) answer = gAnswerOther;
        else if (args[0] == 0x09b69c4e && args[1] == 0x09b69c46) answer = gAnswerLog;
        else if (args[0] == 0x3ae4bea3 && args[1] == 0x3ae4beab) answer = gAnswerAllocator;
        *reinterpret_cast<void **>(args[2]) = answer;
        return static_cast<uint32_t>(answer != nullptr);
    });
}

int gReleaseModeOther = 0, gReleaseModeLog = 0; // 0 returns, 1 throws a C++ exception, 2 raises an SEH exception

uint32_t DoRelease(int mode)
{
    if (mode == 1) throw std::runtime_error("release");
    if (mode == 2) RaiseException(0xE0001234, 0, 0, nullptr);
    return 1;
}

void DefineReleasable()
{
    adt::Def(kConsoleService, 2, 0, "", [](void *, const uint32_t *) { return DoRelease(gReleaseModeOther); });
    adt::Def(kLogService, 2, 0, "", [](void *, const uint32_t *) { return DoRelease(gReleaseModeLog); });
}

// Runs `body` once as the original and once as ours (each with its own log); fails on any difference.
template <typename Original, typename Ours, typename Describe> void RunBoth(difftest::Context &ctx, const char *what, Original original, Ours ours, Describe describe)
{
    std::vector<std::string> logs[2];
    std::string state[2];
    for (int side = 0; side < 2; ++side) {
        adt::ResetSide();
        if (side == 0) original();
        else ours();
        logs[side] = adt::gLog;
        state[side] = describe();
    }
    ++ctx.cases;
    if (logs[0] != logs[1]) ctx.Fail("%s: calls differ: %s", what, adt::FirstDifference(logs[0], logs[1]).c_str());
    if (state[0] != state[1]) ctx.Fail("%s: state differs:\n  original %s\n  ours     %s", what, state[0].c_str(), state[1].c_str());
}

const std::vector<std::string> kNames = {"", "x", "LogSettings", "exactly15chars!", "exactly 16 chars", "a considerably longer name that cannot live in the inline buffer of a string"};

} // namespace

// ---- 0x1e7190 / 0x152b80 ----------------------------------------------------------------------------------------

DIFF_TEST(AppDebugger_AllocateListNodes, 0x1e7190)
{
    auto orig = ctx.Original<void *(__stdcall *)(uint32_t, uint32_t)>(0x1e7190);
    adt::GlobalOverride<void *> framework(ctx, kFrameworkGlobal);
    adt::AllocatorFake alloc(ctx);
    DefineFramework();
    adt::Fake fw(kFramework);
    gAnswerAllocator = alloc.get();
    for (int firstUse = 0; firstUse < 2; ++firstUse) {
        for (uint32_t count : {1u, 2u, 3u, 7u, 100u}) {
            for (uint32_t unused : {0u, 0xdeadbeefu}) {
                std::vector<std::string> logs[2];
                void *got[2];
                for (int side = 0; side < 2; ++side) {
                    framework.Set(fw.get());
                    if (firstUse) alloc.Disarm();
                    else alloc.Arm();
                    adt::ResetSide();
                    got[side] = side == 0 ? orig(count, unused) : AllocateListNodes(count, unused);
                    logs[side] = adt::gLog;
                    if (!got[side] || std::memcmp(got[side], "\xcd\xcd\xcd\xcd", 4) != 0) ctx.Fail("side %d: no block or not the allocator's", side);
                    if (firstUse && (*ctx.image.At<void *>(adt::AllocatorFake::kCacheRva) != alloc.get() || *ctx.image.At<int>(adt::AllocatorFake::kGuardRva) == 0 || *ctx.image.At<int>(adt::AllocatorFake::kGuardRva) == -1))
                        ctx.Fail("side %d: cache or guard not set after the first use", side);
                    alloc.Release();
                }
                ++ctx.cases;
                if (logs[0] != logs[1]) ctx.Fail("count %u first use %d: calls differ: %s", count, firstUse, adt::FirstDifference(logs[0], logs[1]).c_str());
                char expect[64];
                std::snprintf(expect, sizeof(expect), "alloc.3(0x%x)", count * 32);
                if (logs[0].empty() || logs[0].back() != expect) ctx.Fail("count %u: expected %s, original logged %s", count, expect, adt::Joined(logs[0]).c_str());
            }
        }
    }
    alloc.Arm();
}

DIFF_TEST(AppDebugger_FreeStringBuffer, 0x152b80)
{
    auto orig = ctx.Original<void(__cdecl *)(void *, void *, uint32_t)>(0x152b80);
    adt::GlobalOverride<void *> framework(ctx, kFrameworkGlobal);
    adt::AllocatorFake alloc(ctx);
    DefineFramework();
    adt::Fake fw(kFramework);
    gAnswerAllocator = alloc.get();
    std::mt19937 rng(0x152b80);
    for (int firstUse = 0; firstUse < 2; ++firstUse) {
        for (int i = 0; i < 12; ++i) {
            uint32_t a = rng(), c = rng();
            std::vector<std::string> logs[2];
            for (int side = 0; side < 2; ++side) {
                framework.Set(fw.get());
                if (firstUse) alloc.Disarm();
                else alloc.Arm();
                void *mem = ::operator new(16);
                adt::ResetSide();
                if (side == 0) orig(reinterpret_cast<void *>(a), mem, c);
                else FreeStringBuffer(reinterpret_cast<void *>(a), mem, c);
                logs[side] = adt::gLog;
                if (alloc.mFreed.size() != 1 || alloc.mFreed[0] != mem) ctx.Fail("side %d: freed %zu blocks, not the second argument", side, alloc.mFreed.size());
                alloc.Release();
            }
            ++ctx.cases;
            if (logs[0] != logs[1]) ctx.Fail("first use %d: calls differ: %s", firstUse, adt::FirstDifference(logs[0], logs[1]).c_str());
        }
    }
    alloc.Arm();
}

// ---- 0x1d5ea0 ---------------------------------------------------------------------------------------------------

DIFF_TEST(AppDebugger_DestroyStringListNodes, 0x1d5ea0)
{
    auto orig = ctx.Original<void(__cdecl *)(ListHeader *, void *)>(0x1d5ea0);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(0x1d5ea0);
    std::string big(70, 'q'), edge(15, 'e'), edge2(16, 'f');
    const std::vector<std::vector<std::string>> lists = {{}, {"a"}, {big}, {edge, edge2}, {"x", big, "yz", edge2, ""}, {big, big, big}, {"p", "q", "r", "s", "t", "u"}};
    for (const auto &texts : lists) {
        std::vector<std::string> logs[2];
        std::vector<std::vector<uint8_t>> bytes[2];
        std::vector<int> link[2];
        for (int side = 0; side < 2; ++side) {
            BuiltList list = BuildList(texts, 7 + static_cast<uint32_t>(texts.size()), &alloc);
            ListHeader header{list.sentinel, 0};
            adt::ResetSide();
            if (side == 0) orig(&header, list.sentinel);
            else DestroyStringListNodes(&header, list.sentinel);
            logs[side] = adt::gLog;
            // The links: the sentinel's next is the first node (null for an empty list), the last node's next is null.
            auto index = [&](Node *p) {
                if (!p) return -1;
                for (size_t i = 0; i < list.nodes.size(); ++i)
                    if (list.nodes[i] == p) return static_cast<int>(i);
                return p == list.sentinel ? -2 : -3;
            };
            link[side].push_back(index(list.sentinel->next));
            link[side].push_back(index(list.sentinel->prev));
            for (Node *n : list.nodes) {
                link[side].push_back(index(n->next));
                link[side].push_back(index(n->prev));
            }
            // The nodes' bytes (the string part): a heap string keeps only its pointer's upper bytes, which differ
            // between the sides, so its first word is reduced to the low byte (the original clears that one).
            for (size_t i = 0; i < list.nodes.size(); ++i) {
                std::vector<uint8_t> raw(reinterpret_cast<uint8_t *>(list.nodes[i]) + 8, reinterpret_cast<uint8_t *>(list.nodes[i]) + 0x20);
                if (texts[i].size() > 15) std::memset(raw.data() + 1, 0, 3);
                bytes[side].push_back(raw);
            }
            if (alloc.mFreed.size() != texts.size() + [&] { size_t heap = 0; for (auto &t : texts) heap += t.size() > 15; return heap; }())
                ctx.Fail("side %d: %zu frees for %zu nodes", side, alloc.mFreed.size(), texts.size());
            alloc.Release();
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) ctx.Fail("%zu nodes: calls differ: %s", texts.size(), adt::FirstDifference(logs[0], logs[1]).c_str());
        if (link[0] != link[1]) ctx.Fail("%zu nodes: links differ", texts.size());
        if (bytes[0] != bytes[1]) ctx.Fail("%zu nodes: node bytes differ", texts.size());
    }
    // A string with capacity 16 (the std::string never makes one: 15 is inline, the next heap size is 31) is
    // already heap-allocated for the code, which tests "above 0xf": the buffer must be freed.
    {
        std::vector<std::string> logs[2];
        for (int side = 0; side < 2; ++side) {
            BuiltList list = BuildList({"a"}, 3, &alloc);
            auto *raw = reinterpret_cast<uint32_t *>(&list.nodes[0]->text);
            char *buffer = static_cast<char *>(::operator new(17));
            std::memset(buffer, 'z', 17);
            raw[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
            raw[4] = 1;
            raw[5] = 16;
            ListHeader header{list.sentinel, 0};
            adt::ResetSide();
            if (side == 0) orig(&header, list.sentinel);
            else DestroyStringListNodes(&header, list.sentinel);
            logs[side] = adt::gLog;
            alloc.Track(buffer);
            alloc.Release();
        }
        ++ctx.cases;
        if (logs[0] != logs[1] || logs[0].size() != 2) ctx.Fail("capacity 16: calls differ or the buffer was not freed: %s", adt::FirstDifference(logs[0], logs[1]).c_str());
    }
    (void)rng;
}

// ---- 0xcb0640 / 0xcb0650 / 0xcb0660 ---------------------------------------------------------------------------------

DIFF_TEST(AppDebugger_CheatSetters, 0xcb0660)
{
    auto origA = ctx.Original<void(__thiscall *)(void *, void *)>(0xcb0640);
    auto origB = ctx.Original<void(__thiscall *)(void *, void *)>(0xcb0650);
    auto origName = ctx.Original<void(__thiscall *)(void *, const char *)>(0xcb0660);
    uint32_t seed = 1;
    for (const std::string &start : kNames) {
        for (const std::string &text : kNames) {
            for (int which = 0; which < 3; ++which) {
                CheatSide sides[2] = {CheatSide(seed, start), CheatSide(seed, start)};
                ++seed;
                LogCheatGlobal globals[2] = {{nullptr, sides[0].impl}, {nullptr, sides[1].impl}};
                void *service = reinterpret_cast<void *>(static_cast<uintptr_t>(0x5000 + seed));
                if (which == 0) {
                    origA(&globals[0], service);
                    SetCheatServiceA(&globals[1], nullptr, service);
                } else if (which == 1) {
                    origB(&globals[0], service);
                    SetCheatServiceB(&globals[1], nullptr, service);
                } else {
                    origName(&globals[0], text.c_str());
                    SetCheatName(&globals[1], nullptr, text.c_str());
                }
                ++ctx.cases;
                std::string a = DescribeImpl(sides[0].impl), b = DescribeImpl(sides[1].impl);
                if (a != b) ctx.Fail("setter %d, start \"%s\", text \"%s\":\n  original %s\n  ours     %s", which, start.c_str(), text.c_str(), a.c_str(), b.c_str());
                if (which == 2 && sides[0].impl->mName != text) ctx.Fail("name not set");
            }
        }
    }
}

// ---- 0xcb0760 ----------------------------------------------------------------------------------------------------

DIFF_TEST(AppDebugger_LoadCheatServices, 0xcb0760)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcb0760);
    adt::GlobalOverride<void *> framework(ctx, kFrameworkGlobal);
    DefineFramework();
    DefineReleasable();
    adt::Fake fw(kFramework), other(kConsoleService), log(kLogService);
    for (int present = 0; present < 2; ++present) {
        for (int answers = 0; answers < 4; ++answers) {
            for (int modeOther = 0; modeOther < 2; ++modeOther) {
                for (int modeLog = 0; modeLog < 3; ++modeLog) {
                    framework.Set(present ? fw.get() : nullptr);
                    gAnswerOther = (answers & 1) ? other.get() : nullptr;
                    gAnswerLog = (answers & 2) ? log.get() : nullptr;
                    gReleaseModeOther = modeOther;
                    gReleaseModeLog = modeLog;
                    CheatSide sides[2] = {CheatSide(5, "before"), CheatSide(5, "before")};
                    LogCheatGlobal globals[2] = {{nullptr, sides[0].impl}, {nullptr, sides[1].impl}};
                    int side = 0;
                    char what[96];
                    std::snprintf(what, sizeof(what), "framework %d answers %d modes %d/%d", present, answers, modeOther, modeLog);
                    RunBoth(
                        ctx, what, [&] { side = 0; orig(&globals[0]); }, [&] { side = 1; LoadCheatServices(&globals[1], nullptr); },
                        [&] { return DescribeImpl(sides[side].impl); });
                }
            }
        }
    }
}

// ---- 0xcb0690 ----------------------------------------------------------------------------------------------------

DIFF_TEST(AppDebugger_PrepareCheatGlobal, 0xcb0690)
{
    auto orig = ctx.Original<void(__cdecl *)()>(0xcb0690);
    adt::GlobalOverride<void *> framework(ctx, kFrameworkGlobal);
    adt::GlobalOverride<void *> commonCache(ctx, kCommonCache);
    adt::GlobalOverride<LogCheatImpl *> impl(ctx, LogCheatGlobal::kRva + 4);
    DefineFramework();
    DefineReleasable();
    DefineCommonImpl();
    adt::Fake fw(kFramework), other(kConsoleService), log(kLogService), common(kCommonImpl);
    framework.Set(fw.get());
    commonCache.Set(common.get());
    gAnswerOther = other.get();
    gAnswerLog = log.get();
    for (const std::string &option : kNames) {
        for (int answers = 0; answers < 4; ++answers) {
            gOptionText = option;
            gAnswerOther = (answers & 1) ? other.get() : nullptr;
            gAnswerLog = (answers & 2) ? log.get() : nullptr;
            CheatSide sides[2] = {CheatSide(9, "previous name that is rather long, longer than fifteen"), CheatSide(9, "previous name that is rather long, longer than fifteen")};
            int side = 0;
            RunBoth(
                ctx, "PrepareCheatGlobal", [&] { side = 0; impl.Set(sides[0].impl); orig(); }, [&] { side = 1; impl.Set(sides[1].impl); PrepareCheatGlobal(); },
                [&] { return DescribeImpl(sides[side].impl); });
        }
    }
}

// ---- 0xcaf560 ----------------------------------------------------------------------------------------------------

DIFF_TEST(AppDebugger_OpenCheatFile, 0xcaf560)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *)>(0xcaf560);
    DefineStream();
    StreamFake stream;
    LogCheatGlobal globals[2] = {{stream.object(), nullptr}, {stream.object(), nullptr}};
    for (const std::string &name : kNames) {
        int side = 0;
        RunBoth(
            ctx, "OpenCheatFile", [&] { side = 0; orig(&globals[0], name.c_str()); }, [&] { side = 1; OpenCheatFile(&globals[1], nullptr, name.c_str()); }, [&] { return std::string(); });
        (void)side;
    }
    // A different distance to the base (the table's dword at +8) must be followed too.
    adt::Fake other(kStreamBase);
    stream.vbtable[2] = static_cast<int32_t>(reinterpret_cast<uintptr_t>(other.get()) - (reinterpret_cast<uintptr_t>(stream.memory) + 8));
    int side = 0;
    RunBoth(
        ctx, "OpenCheatFile (another base)", [&] { side = 0; orig(&globals[0], "again"); }, [&] { side = 1; OpenCheatFile(&globals[1], nullptr, "again"); }, [&] { return std::string(); });
    (void)side;
}

// ---- 0xcaf060 / 0xcaf090 ------------------------------------------------------------------------------------------

namespace {

struct CheatWorld {
    adt::GlobalOverride<void *> framework;
    adt::GlobalOverride<void *> commonCache;
    adt::GlobalOverride<void *> object;
    adt::GlobalOverride<LogCheatImpl *> impl;
    adt::Fake fw{kFramework}, other{kConsoleService}, log{kLogService}, common{kCommonImpl};
    StreamFake stream;
    explicit CheatWorld(difftest::Context &ctx)
        : framework(ctx, kFrameworkGlobal), commonCache(ctx, kCommonCache), object(ctx, LogCheatGlobal::kRva), impl(ctx, LogCheatGlobal::kRva + 4)
    {
    }
    void Setup()
    {
        framework.Set(fw.get());
        commonCache.Set(common.get());
        object.Set(stream.object());
        gAnswerOther = other.get();
        gAnswerLog = log.get();
    }
};

void DefineAll()
{
    DefineFramework();
    DefineReleasable();
    DefineCommonImpl();
    DefineStream();
}

} // namespace

DIFF_TEST(AppDebugger_LoadCheatFile, 0xcaf060)
{
    auto orig = ctx.Original<void(__cdecl *)(const cRZString *)>(0xcaf060);
    adt::InstallWideFunctions(ctx);
    DefineAll();
    CheatWorld world(ctx);
    world.Setup();
    gOptionText = "option text taken from the common implementation";
    std::wstring present = difftest::SandboxPath(L"cheat_present.cheat");
    if (!WriteFileText(present, "x")) ctx.Fail("cannot create the sandbox file");
    std::wstring missing = difftest::SandboxPath(L"cheat_missing.cheat");
    DeleteFileW(missing.c_str());
    for (const std::wstring &path : {present, missing}) {
        std::string text = Utf8(path);
        cRZString str;
        str.Construct(text.c_str());
        CheatSide sides[2] = {CheatSide(3, "old"), CheatSide(3, "old")};
        int side = 0;
        RunBoth(
            ctx, path == present ? "present" : "missing", [&] { side = 0; world.impl.Set(sides[0].impl); orig(&str); }, [&] { side = 1; world.impl.Set(sides[1].impl); LoadCheatFile(&str); },
            [&] { return DescribeImpl(sides[side].impl); });
        str.Destruct();
    }
    DeleteFileW(present.c_str());
}

DIFF_TEST(AppDebugger_LoadLogSettingsCheats, 0xcaf090)
{
    auto orig = ctx.Original<void(__cdecl *)()>(0xcaf090);
    adt::InstallWideFunctions(ctx);
    DefineAll();
    adt::AllocatorFake alloc(ctx);
    gAllocator = &alloc;
    CheatWorld world(ctx);
    world.Setup();
    gOptionText = "x";
    std::mt19937 rng(0xcaf090);
    // Directories d0..d3 in the sandbox, each maybe with either file.
    std::wstring dirs[4];
    std::string dirText[4];
    for (int i = 0; i < 4; ++i) {
        dirs[i] = difftest::SandboxPath(L"dbg_dir" + std::to_wstring(i));
        CreateDirectoryW(dirs[i].c_str(), nullptr);
        dirs[i] += L"\\";
        dirText[i] = Utf8(dirs[i]);
    }
    int loaded = 0;
    for (int round = 0; round < 160; ++round) {
        size_t count = rng() % 5;
        gDirectories.clear();
        for (size_t i = 0; i < count; ++i) gDirectories.push_back(dirText[rng() % 4]);
        if (rng() % 8 == 0) gDirectories.push_back(Utf8(difftest::SandboxPath(L"no_such_dir\\")));
        for (int i = 0; i < 4; ++i) {
            for (const wchar_t *file : {L"LogSettings.cheat", L"UserLogSettings.cheat"}) {
                std::wstring path = dirs[i] + file;
                if (rng() % 3 == 0) WriteFileText(path, "x");
                else DeleteFileW(path.c_str());
            }
        }
        CheatSide sides[2] = {CheatSide(round, "old"), CheatSide(round, "old")};
        int side = 0;
        RunBoth(
            ctx, "LoadLogSettingsCheats", [&] { side = 0; world.impl.Set(sides[0].impl); orig(); }, [&] { side = 1; world.impl.Set(sides[1].impl); LoadLogSettingsCheats(); },
            [&] {
                std::string freed = std::to_string(alloc.mFreed.size());
                alloc.Release();
                return DescribeImpl(sides[side].impl) + " frees " + freed;
            });
        if (sides[0].impl->mName != "old") ++loaded;
    }
    if (loaded < 40) ctx.Fail("only %d rounds loaded a file", loaded);
    for (int i = 0; i < 4; ++i) {
        for (const wchar_t *file : {L"LogSettings.cheat", L"UserLogSettings.cheat"}) DeleteFileW((dirs[i] + file).c_str());
        RemoveDirectoryW(dirs[i].substr(0, dirs[i].size() - 1).c_str());
    }
    gAllocator = nullptr;
}
