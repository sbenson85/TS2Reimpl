// Differential tests for the constructors and small pieces of the log setup (src/app/AppDebugger.*):
//   0x1bf8fb LogServiceHolder::Acquire, 0x1b4f60 / 0x1b4fab the stream bases, 0xc9c2e2 cRZStdAdapter,
//   0x1b9cad StoreCurrentThreadId and
//   0xca9bf0 cTSWinCheatConsole.
//
// Method: two blocks filled with the same random bytes (constructors leave padding alone), the original
// constructor on one and ours on the other. The blocks must then be byte-identical except for the heap
// pointers (a std::string's buffer when it outgrew the small buffer, the critical section's block, the id
// set's head), which are blanked in copies and checked separately.
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "difftest.h"

#define NOMINMAX
#include <windows.h>

#include <climits>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace appdebugger;

constexpr size_t kTail = 16;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194, kFrameworkGlobal = 0x10c9378;

struct Block {
    uint8_t *a;
    uint8_t *b;
    size_t size;
};

Block MakeBlock(size_t size, uint32_t seed)
{
    Block blk{static_cast<uint8_t *>(::operator new(size + kTail)), static_cast<uint8_t *>(::operator new(size + kTail)), size};
    std::mt19937 rng(seed);
    for (size_t i = 0; i < size + kTail; ++i) blk.a[i] = blk.b[i] = static_cast<uint8_t>(rng());
    return blk;
}

void FreeBlock(Block &blk)
{
    ::operator delete(blk.a);
    ::operator delete(blk.b);
}

// A cRZString inside a block (at `off`): vtable, std::string (16-byte buffer, size, capacity), refcount.
struct StringView {
    const uint8_t *base;
    const uint32_t *words() const { return reinterpret_cast<const uint32_t *>(base); }
    uint32_t size() const { return words()[5]; }
    uint32_t capacity() const { return words()[6]; }
    bool heap() const { return capacity() > 15; }
    const char *text() const { return heap() ? reinterpret_cast<const char *>(words()[1]) : reinterpret_cast<const char *>(base + 4); }
};

// Compares two blocks: `strings` lists the offsets of cRZStrings (compared by content, capacity and, when
// the buffer is inline, its bytes; when it is on the heap, the pointer word is blanked and the heap size
// compared), `pointers` the offsets of other heap pointers (only null-ness is compared).
void CompareBlocks(difftest::Context &ctx, const char *what, const Block &blk, std::initializer_list<size_t> strings, std::initializer_list<size_t> pointers)
{
    ++ctx.cases;
    std::vector<uint8_t> ca(blk.a, blk.a + blk.size + kTail), cb(blk.b, blk.b + blk.size + kTail);
    for (size_t off : strings) {
        StringView sa{blk.a + off}, sb{blk.b + off};
        if (sa.size() != sb.size() || sa.capacity() != sb.capacity() || std::memcmp(sa.text(), sb.text(), sa.size() + 1) != 0) {
            ctx.Fail("%s: the string at +0x%zx differs (size %u/%u, capacity %u/%u)", what, off, sa.size(), sb.size(), sa.capacity(), sb.capacity());
            return;
        }
        if (sa.heap()) {
            SIZE_T ha = HeapSize(GetProcessHeap(), 0, const_cast<char *>(sa.text())), hb = HeapSize(GetProcessHeap(), 0, const_cast<char *>(sb.text()));
            if (ha != hb) ctx.Fail("%s: the heap buffer at +0x%zx has size %zu/%zu", what, off, static_cast<size_t>(ha), static_cast<size_t>(hb));
            std::memset(ca.data() + off + 4, 0, 4);
            std::memset(cb.data() + off + 4, 0, 4);
        }
    }
    for (size_t off : pointers) {
        void *pa = *reinterpret_cast<void **>(blk.a + off);
        void *pb = *reinterpret_cast<void **>(blk.b + off);
        if ((pa == nullptr) != (pb == nullptr)) ctx.Fail("%s: pointer at +0x%zx null on one side only", what, off);
        std::memset(ca.data() + off, 0, 4);
        std::memset(cb.data() + off, 0, 4);
    }
    if (ca != cb) {
        size_t first = 0;
        while (ca[first] == cb[first]) ++first;
        ctx.Fail("%s: blocks differ at +0x%zx (original %02x, ours %02x)", what, first, ca[first], cb[first]);
    }
}

void DestroyString(uint8_t *block, size_t off)
{
    reinterpret_cast<cRZString *>(block + off)->Destruct();
}

// The text lengths around the inline-buffer limit and beyond.
const char *const kTexts[] = {"", "a", "StdOut", "AppTitleBar", "exactly15chars!", "exactly 16 chars", "a considerably longer text that does not fit the inline buffer of a string at all"};

uint32_t gThreadId = 0;
uint32_t WINAPI FakeGetCurrentThreadId()
{
    return gThreadId;
}

class Iat {
public:
    Iat(difftest::Context &c, uint32_t rva, void *fake) : ctx(c), at(rva), saved(*c.image.At<void *>(rva)) { *c.image.At<void *>(rva) = fake; }
    ~Iat() { *ctx.image.At<void *>(at) = saved; }

private:
    difftest::Context &ctx;
    uint32_t at;
    void *saved;
};

} // namespace

// 0x1bf8fb: the holder is cleared; with a framework the service comes from its slot 5 with the two ids.
DIFF_TEST(AppDebugger_LogServiceHolder_Acquire, 0x1bf8fb)
{
    auto original = ctx.Original<LogServiceHolder *(__thiscall *)(LogServiceHolder *)>(0x1bf8fb);
    void **frameworkGlobal = ctx.image.At<void *>(kFrameworkGlobal);
    void *saved = *frameworkGlobal;
    static void *answer = nullptr;
    static bool answerFlag = false;
    adt::Def(adt::kFramework, 5, 3, "iio", [](void *, const uint32_t *args) {
        *reinterpret_cast<void **>(args[2]) = answer;
        return static_cast<uint32_t>(answerFlag);
    });
    adt::Fake framework(adt::kFramework);
    int object;
    for (int withFramework = 0; withFramework < 2; ++withFramework) {
        for (int variant = 0; variant < 4; ++variant) {
            *frameworkGlobal = withFramework ? framework.get() : nullptr;
            answer = (variant & 1) ? &object : nullptr;
            answerFlag = (variant & 2) != 0;
            void *results[2];
            LogServiceHolder *returned[2];
            std::vector<std::string> logs[2];
            for (int side = 0; side < 2; ++side) {
                LogServiceHolder holder;
                holder.mService = reinterpret_cast<void *>(0xdeadbeef); // must be cleared whatever happens
                adt::ResetSide();
                returned[side] = side == 0 ? original(&holder) : holder.Acquire();
                results[side] = holder.mService;
                logs[side] = adt::gLog;
                if (returned[side] != &holder) ctx.Fail("side %d returned another pointer", side);
            }
            ++ctx.cases;
            if (results[0] != results[1]) ctx.Fail("framework %d variant %d: holder %p/%p", withFramework, variant, results[0], results[1]);
            if (logs[0] != logs[1]) ctx.Fail("framework %d variant %d: calls differ: %s", withFramework, variant, adt::FirstDifference(logs[0], logs[1]).c_str());
            if (withFramework && (logs[0].size() != 1 || logs[0][0] != "fw.5(0x9b69c4e, 0x9b69c46, out)")) ctx.Fail("unexpected call log for the original: %s", adt::Joined(logs[0]).c_str());
            if (!withFramework && !logs[0].empty()) ctx.Fail("calls without a framework");
        }
    }
    *frameworkGlobal = saved;
}

DIFF_TEST(AppDebugger_StreamBases, 0x1b4f60)
{
    auto origOStream = ctx.Original<OStreamBase *(__thiscall *)(void *)>(0x1b4f60);
    auto origText = ctx.Original<OStreamBase *(__thiscall *)(void *)>(0x1b4fab);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        for (int text = 0; text < 2; ++text) {
            Block blk = MakeBlock(OStreamBase::kSize, seed);
            auto *ra = text ? origText(blk.a) : origOStream(blk.a);
            auto *rb = text ? reinterpret_cast<OStreamBase *>(blk.b)->ConstructTextOStream() : reinterpret_cast<OStreamBase *>(blk.b)->ConstructOStream();
            ++ctx.cases;
            if (reinterpret_cast<uint8_t *>(ra) != blk.a || reinterpret_cast<uint8_t *>(rb) != blk.b || std::memcmp(blk.a, blk.b, blk.size + kTail) != 0)
                ctx.Fail("seed %u text %d: constructor differs", seed, text);
            FreeBlock(blk);
        }
    }
}

DIFF_TEST(AppDebugger_StdAdapter_Construct, 0xc9c2e2)
{
    auto orig = ctx.Original<cRZStdAdapter *(__thiscall *)(void *, int32_t, const char *)>(0xc9c2e2);
    uint32_t seed = 1;
    for (const char *text : kTexts) {
        for (int32_t target : {0, 1, -1, 0x12345678}) {
            Block blk = MakeBlock(cRZStdAdapter::kSize, seed++);
            auto *ra = orig(blk.a, target, text);
            auto *rb = reinterpret_cast<cRZStdAdapter *>(blk.b)->Construct(target, text);
            if (reinterpret_cast<uint8_t *>(ra) != blk.a || reinterpret_cast<uint8_t *>(rb) != blk.b) ctx.Fail("seed %u: wrong return", seed);
            CompareBlocks(ctx, "cRZStdAdapter", blk, {0x14}, {});
            DestroyString(blk.a, 0x14);
            DestroyString(blk.b, 0x14);
            FreeBlock(blk);
        }
    }
}

DIFF_TEST(AppDebugger_StoreCurrentThreadId, 0x1b9cad)
{
    Iat iat(ctx, kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeGetCurrentThreadId));
    auto orig = ctx.Original<void(__cdecl *)(uint32_t *)>(0x1b9cad);
    // Ours calls kernel32's GetCurrentThreadId itself (the IAT patch only reaches the original), so the
    // original's fake answers with the real id; two pre-filled targets show both write exactly that dword.
    gThreadId = GetCurrentThreadId();
    for (uint32_t fill : {0u, 0x11111111u, 0xffffffffu}) {
        uint32_t cells[2][3] = {{fill, fill, fill}, {fill, fill, fill}};
        orig(&cells[0][1]);
        StoreCurrentThreadId(&cells[1][1]);
        ++ctx.cases;
        if (cells[0][1] != gThreadId || cells[1][1] != gThreadId || cells[0][0] != fill || cells[0][2] != fill || cells[1][0] != fill || cells[1][2] != fill)
            ctx.Fail("fill %x: id stored as %u/%u (expected %u) or neighbours changed", fill, cells[0][1], cells[1][1], gThreadId);
    }
}

DIFF_TEST(AppDebugger_CheatConsole_Construct, 0xca9bf0)
{
    Iat iat(ctx, kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeGetCurrentThreadId));
    auto orig = ctx.Original<cTSWinCheatConsole *(__thiscall *)(void *)>(0xca9bf0);
    gThreadId = GetCurrentThreadId(); // see AppDebugger_StoreCurrentThreadId
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        Block blk = MakeBlock(cTSWinCheatConsole::kSize, seed);
        auto *ra = orig(blk.a);
        auto *rb = reinterpret_cast<cTSWinCheatConsole *>(blk.b)->Construct();
        if (reinterpret_cast<uint8_t *>(ra) != blk.a || reinterpret_cast<uint8_t *>(rb) != blk.b) ctx.Fail("seed %u: wrong return", seed);
        auto *ca = reinterpret_cast<cTSWinCheatConsole *>(blk.a);
        auto *cb = reinterpret_cast<cTSWinCheatConsole *>(blk.b);
        // The id set is compared through its contents (its head pointer is blanked below).
        if (ca->mIds.size() != 1 || cb->mIds.size() != 1 || *ca->mIds.begin() != cTSWinCheatConsole::kInitialId || *cb->mIds.begin() != cTSWinCheatConsole::kInitialId)
            ctx.Fail("seed %u: id set differs", seed);
        if (ca->mOwnerThread != gThreadId || cb->mOwnerThread != gThreadId) ctx.Fail("seed %u: owner thread %u/%u", seed, ca->mOwnerThread, cb->mOwnerThread);
        // +0xb0 is the critical section's block pointer.
        CompareBlocks(ctx, "cTSWinCheatConsole", blk, {0x8c, 0xc0}, {0x38, 0xb0});
        auto *ba = *reinterpret_cast<cRZCriticalSection::Block **>(blk.a + 0xb0);
        auto *bb = *reinterpret_cast<cRZCriticalSection::Block **>(blk.b + 0xb0);
        if (!ba || !bb || ba->lockCount != bb->lockCount) ctx.Fail("seed %u: critical section block missing or lock count differs", seed);
        ca->mIds.~set();
        cb->mIds.~set();
        FreeBlock(blk);
    }
}
