// Differential tests for the destructors of nGZLog's helper objects (src/rz/nGZLog.cpp,
// src/rz/cRZThreadSignal.cpp): the record, the text formatter, the group level filter, the alias table
// and the thread signal, with their deleting destructors and the thunks that reach them through the
// cRZUnknown base. Objects come from the original constructors on both sides (same random bytes under
// the constructed fields), filled with the same text; every heap block either object owns is recorded
// before the destructor runs and must be freed afterwards on both sides, and the object bytes that are
// not freed pointers must match. Blocks are tracked in fixed arrays, so nothing allocates while
// measuring.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/cRZThreadSignal.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <map>
#include <optional>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kRecordCtor = 0xc9c1ed, kFormatterCtor = 0xc9c164, kFilterCtor = 0xc9c113;
constexpr uint32_t kRecordDtor = 0xc9c7f1, kRecordDeleting = 0xc9ca5a, kRecordThunk = 0xc9c9df;
constexpr uint32_t kFormatterDtor = 0xc9c75e, kFormatterDeleting = 0xc9ca38, kFormatterThunk = 0xc9c9d7;
constexpr uint32_t kFilterDtor = 0xc9c724, kFilterDeleting = 0xc9c9ef, kFilterThunk = 0xc9c9cf;
constexpr uint32_t kAliasDelete = 0xc9ca0e;
constexpr uint32_t kSignalStateDtor = 0x9ccaf8, kSignalDtor = 0x9ccb13, kSignalDeleting = 0x9ccb38;

// Flags for the deleting destructors: only bit 0 of the low byte means "delete".
constexpr uint32_t kFlagSet[8] = {0, 1, 2, 3, 0x100, 0x101, 0xfffffffe, 0xffffffff};

// The frees of the call under test: BeginFrees() right before it starts a FreeRecorder (replacing the previous
// one), Blocks::Snap() right after stops it; Snap() and IsLive() then read it (exact, unlike HeapValidate on a
// block that may have been freed and reused).
std::optional<difftest::FreeRecorder> gFrees;
void BeginFrees()
{
    gFrees.reset();
    gFrees.emplace();
}

// Heap blocks to be checked for release, kept in a fixed array.
struct Blocks {
    void *p[2048];
    size_t n = 0;
    void Add(const void *x)
    {
        if (x && n < 2048) p[n++] = const_cast<void *>(x);
    }
    void AddString(const std::string &s)
    {
        if (s.capacity() > 15) Add(s.data()); // a heap buffer (capacity beyond the small buffer)
    }
    size_t snap = 0; // blocks the call under test did not free
    void Snap()
    {
        gFrees->Stop();
        snap = Live();
    }
    size_t Live() const
    {
        size_t live = 0;
        for (size_t i = 0; i < n; ++i) live += !gFrees->Freed(p[i]);
        return live;
    }
};

// A block of `size` bytes from operator new, filled from the seed (so two copies match byte for byte).
uint8_t *NewFilled(size_t size, uint32_t seed)
{
    uint8_t *m = static_cast<uint8_t *>(::operator new(size));
    std::mt19937 rng(seed);
    for (size_t i = 0; i < size; ++i) m[i] = static_cast<uint8_t>(rng());
    return m;
}

bool IsLive(const void *p)
{
    return !gFrees->Freed(p); // after Blocks::Snap() stopped the recorder
}

std::string RandomText(std::mt19937 &rng)
{
    static const size_t lengths[] = {0, 1, 5, 15, 16, 17, 40, 200};
    std::string s(lengths[rng() % 8], 'a');
    for (char &c : s) c = static_cast<char>('a' + rng() % 26);
    return s;
}

// The first 16 bytes of a std::string beyond its size field are a buffer or a pointer (freed on
// destruction): masked in byte comparisons except its first byte.
void MaskString(std::vector<std::pair<size_t, size_t>> &skip, size_t stringOffset)
{
    skip.push_back({stringOffset + 1, 3}); // the pointer's upper bytes survive on the large-string path
}

void MaskCStrings(std::vector<std::pair<size_t, size_t>> &skip, std::initializer_list<size_t> cRZStringOffsets)
{
    for (size_t off : cRZStringOffsets) MaskString(skip, off + 4);
}

void FillCString(cRZString &s, std::mt19937 &rng, std::string text)
{
    s.mString = text;
    (void)rng;
}

} // namespace

// ---- record ---------------------------------------------------------------------------------------

namespace {

struct RecordPair {
    cLogRecord *a, *b;
    Blocks blocksA, blocksB;
    RecordPair(difftest::Context &ctx, std::mt19937 &rng)
    {
        uint32_t seed = rng();
        a = reinterpret_cast<cLogRecord *>(NewFilled(cLogRecord::kSize, seed));
        b = reinterpret_cast<cLogRecord *>(NewFilled(cLogRecord::kSize, seed));
        auto ctor = ctx.Original<cLogRecord *(__thiscall *)(void *)>(kRecordCtor);
        ctor(a);
        ctor(b);
        std::string t1 = RandomText(rng), t2 = RandomText(rng);
        a->mText.mString = t1;
        b->mText.mString = t1;
        a->mText2.mString = t2;
        b->mText2.mString = t2;
        blocksA.AddString(a->mText.mString);
        blocksA.AddString(a->mText2.mString);
        blocksB.AddString(b->mText.mString);
        blocksB.AddString(b->mText2.mString);
    }
};

std::vector<std::pair<size_t, size_t>> RecordMasks()
{
    std::vector<std::pair<size_t, size_t>> skip;
    MaskCStrings(skip, {0x10, 0x34});
    return skip;
}

} // namespace

DIFF_TEST(GZLog_RecordDtor, kRecordDtor)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kRecordDtor);
    std::mt19937 rng(kRecordDtor);
    for (int i = 0; i < 120; ++i) {
        RecordPair p(ctx, rng);
        BeginFrees();
        original(p.a);
        p.blocksA.Snap();
        BeginFrees();
        p.b->Destruct();
        p.blocksB.Snap();
        ++ctx.cases;
        int at = FirstDifference(reinterpret_cast<uint8_t *>(p.a), reinterpret_cast<uint8_t *>(p.b), cLogRecord::kSize, RecordMasks());
        if (at >= 0) ctx.Fail("object bytes differ at 0x%x", at);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("string buffers still allocated: %zu/%zu of %zu", p.blocksA.snap, p.blocksB.snap, p.blocksA.n);
        if (p.b->mUnknown.mVTable != ctx.image.At(0xda2fd0)) ctx.Fail("base vtable");
        ::operator delete(p.a);
        ::operator delete(p.b);
    }
}

DIFF_TEST(GZLog_RecordDeletingDtor, kRecordDeleting)
{
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kRecordDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kRecordThunk);
    std::mt19937 rng(kRecordDeleting);
    for (int i = 0; i < 160; ++i) {
        uint32_t flags = kFlagSet[i % 8];
        bool viaThunk = (i / 8) % 2 == 1;
        RecordPair p(ctx, rng);
        BeginFrees();
        void *ra = viaThunk ? thunk(reinterpret_cast<uint8_t *>(p.a) + 4, flags) : original(p.a, flags);
        p.blocksA.Snap();
        bool liveA = IsLive(p.a);
        BeginFrees();
        void *rb = viaThunk ? thunks::RecordDeletingFromUnknown(reinterpret_cast<uint8_t *>(p.b) + 4, nullptr, flags) : p.b->DeletingDestructor(flags);
        p.blocksB.Snap();
        bool liveB = IsLive(p.b);
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("flags %x: returns %p/%p, want the object", flags, ra, rb);
        if (liveA == ((flags & 1) != 0) || liveB == ((flags & 1) != 0)) ctx.Fail("flags %x: object freed %d/%d", flags, !liveA, !liveB);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("flags %x: string buffers still allocated", flags);
        if (!(flags & 1)) {
            int at = FirstDifference(reinterpret_cast<uint8_t *>(p.a), reinterpret_cast<uint8_t *>(p.b), cLogRecord::kSize, RecordMasks());
            if (at >= 0) ctx.Fail("flags %x: object bytes differ at 0x%x", flags, at);
            ::operator delete(p.a);
            ::operator delete(p.b);
        }
    }
}

// ---- formatter ------------------------------------------------------------------------------------

namespace {

struct FormatterPair {
    cLogFormatterText *a, *b;
    Blocks blocksA, blocksB;
    FormatterPair(difftest::Context &ctx, std::mt19937 &rng)
    {
        uint32_t seed = rng();
        a = reinterpret_cast<cLogFormatterText *>(NewFilled(cLogFormatterText::kSize, seed));
        b = reinterpret_cast<cLogFormatterText *>(NewFilled(cLogFormatterText::kSize, seed));
        auto ctor = ctx.Original<cLogFormatterText *(__thiscall *)(void *)>(kFormatterCtor);
        ctor(a);
        ctor(b);
        for (cLogFormatterText *f : {a, b}) {
            std::mt19937 text(seed);
            f->mName.mString = RandomText(text);
            for (cRZString &s : f->mFormats) s.mString = RandomText(text);
            for (cRZString &s : f->mPresets) s.mString = RandomText(text);
            f->mOutput.mString = RandomText(text);
            f->mScratch.mString = RandomText(text);
            Blocks &blocks = f == a ? blocksA : blocksB;
            blocks.AddString(f->mName.mString);
            for (cRZString &s : f->mFormats) blocks.AddString(s.mString);
            for (cRZString &s : f->mPresets) blocks.AddString(s.mString);
            blocks.AddString(f->mOutput.mString);
            blocks.AddString(f->mScratch.mString);
            blocks.Add(f->mLock.mBlock);
        }
    }
};

std::vector<std::pair<size_t, size_t>> FormatterMasks()
{
    std::vector<std::pair<size_t, size_t>> skip;
    std::vector<size_t> offsets = {0xc};
    for (size_t k = 0; k < 5; ++k) {
        offsets.push_back(0x2c + 0x20 * k);
        offsets.push_back(0xcc + 0x20 * k);
    }
    offsets.push_back(0x16c);
    offsets.push_back(0x18c);
    for (size_t off : offsets) MaskString(skip, off + 4);
    skip.push_back({0x1b0 + 4, 4}); // the lock's block pointer
    return skip;
}

} // namespace

DIFF_TEST(GZLog_FormatterDtor, kFormatterDtor)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kFormatterDtor);
    std::mt19937 rng(kFormatterDtor);
    for (int i = 0; i < 100; ++i) {
        FormatterPair p(ctx, rng);
        BeginFrees();
        original(p.a);
        p.blocksA.Snap();
        BeginFrees();
        p.b->Destruct();
        p.blocksB.Snap();
        ++ctx.cases;
        int at = FirstDifference(reinterpret_cast<uint8_t *>(p.a), reinterpret_cast<uint8_t *>(p.b), cLogFormatterText::kSize, FormatterMasks());
        if (at >= 0) ctx.Fail("object bytes differ at 0x%x", at);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("blocks still allocated: %zu/%zu of %zu", p.blocksA.snap, p.blocksB.snap, p.blocksA.n);
        ::operator delete(p.a);
        ::operator delete(p.b);
    }
}

DIFF_TEST(GZLog_FormatterDeletingDtor, kFormatterDeleting)
{
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kFormatterDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kFormatterThunk);
    std::mt19937 rng(kFormatterDeleting);
    for (int i = 0; i < 80; ++i) {
        uint32_t flags = kFlagSet[i % 8];
        bool viaThunk = (i / 8) % 2 == 1;
        FormatterPair p(ctx, rng);
        BeginFrees();
        void *ra = viaThunk ? thunk(reinterpret_cast<uint8_t *>(p.a) + 4, flags) : original(p.a, flags);
        p.blocksA.Snap();
        bool liveA = IsLive(p.a);
        BeginFrees();
        void *rb = viaThunk ? thunks::FormatterDeletingFromUnknown(reinterpret_cast<uint8_t *>(p.b) + 4, nullptr, flags) : p.b->DeletingDestructor(flags);
        p.blocksB.Snap();
        bool liveB = IsLive(p.b);
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("flags %x: returns %p/%p, want the object", flags, ra, rb);
        if (liveA == ((flags & 1) != 0) || liveB == ((flags & 1) != 0)) ctx.Fail("flags %x: object freed %d/%d", flags, !liveA, !liveB);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("flags %x: blocks still allocated", flags);
        if (!(flags & 1)) {
            int at = FirstDifference(reinterpret_cast<uint8_t *>(p.a), reinterpret_cast<uint8_t *>(p.b), cLogFormatterText::kSize, FormatterMasks());
            if (at >= 0) ctx.Fail("flags %x: object bytes differ at 0x%x", flags, at);
            ::operator delete(p.a);
            ::operator delete(p.b);
        }
    }
}

// ---- filter ---------------------------------------------------------------------------------------

namespace {

// Everything a hash container of cRZString keys owns: the list head, every node, the bucket vector and
// the key (and value) strings' heap buffers.
template <typename Map> void CollectHash(Blocks &blocks, const uint8_t *container, const Map &map, bool valuesAreStrings)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(container);
    blocks.Add(reinterpret_cast<void *>(static_cast<uintptr_t>(w[1]))); // list head
    blocks.Add(reinterpret_cast<void *>(static_cast<uintptr_t>(w[3]))); // bucket vector
    for (const auto &e : map) {
        blocks.Add(reinterpret_cast<const uint8_t *>(&e) - 8); // the node starts with its two links
        blocks.AddString(e.first.mString);
        if (valuesAreStrings) blocks.AddString(reinterpret_cast<const LogString *>(&e.second)->mString);
    }
}

struct FilterPair {
    cLogFilterGroupLevels *a, *b;
    Blocks blocksA, blocksB;
    FilterPair(difftest::Context &ctx, std::mt19937 &rng)
    {
        uint32_t seed = rng();
        a = reinterpret_cast<cLogFilterGroupLevels *>(NewFilled(cLogFilterGroupLevels::kSize, seed));
        b = reinterpret_cast<cLogFilterGroupLevels *>(NewFilled(cLogFilterGroupLevels::kSize, seed));
        auto ctor = ctx.Original<cLogFilterGroupLevels *(__thiscall *)(void *)>(kFilterCtor);
        ctor(a);
        ctor(b);
        int entries = rng() % 5 == 0 ? 0 : rng() % 40; // enough to grow the table past its eight buckets
        std::string name = RandomText(rng);
        a->mName.mString = name;
        b->mName.mString = name;
        for (int k = 0; k < entries; ++k) {
            std::string key = RandomText(rng) + std::to_string(k);
            uint32_t level = rng();
            a->mLevels[LogString(key.c_str())] = level;
            b->mLevels[LogString(key.c_str())] = level;
        }
        blocksA.AddString(a->mName.mString);
        blocksB.AddString(b->mName.mString);
        CollectHash(blocksA, reinterpret_cast<const uint8_t *>(&a->mLevels), a->mLevels, false);
        CollectHash(blocksB, reinterpret_cast<const uint8_t *>(&b->mLevels), b->mLevels, false);
    }
};

std::vector<std::pair<size_t, size_t>> FilterMasks()
{
    std::vector<std::pair<size_t, size_t>> skip;
    MaskString(skip, 0xc + 4);
    skip.push_back({0x34 + 4, 4}); // the list head pointer (freed, not cleared)
    return skip;
}

} // namespace

DIFF_TEST(GZLog_FilterDtor, kFilterDtor)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kFilterDtor);
    std::mt19937 rng(kFilterDtor);
    for (int i = 0; i < 100; ++i) {
        FilterPair p(ctx, rng);
        BeginFrees();
        original(p.a);
        p.blocksA.Snap();
        BeginFrees();
        p.b->Destruct();
        p.blocksB.Snap();
        ++ctx.cases;
        int at = FirstDifference(reinterpret_cast<uint8_t *>(p.a), reinterpret_cast<uint8_t *>(p.b), cLogFilterGroupLevels::kSize, FilterMasks());
        if (at >= 0) ctx.Fail("object bytes differ at 0x%x", at);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("blocks still allocated: %zu/%zu of %zu", p.blocksA.snap, p.blocksB.snap, p.blocksA.n);
        ::operator delete(p.a);
        ::operator delete(p.b);
    }
}

DIFF_TEST(GZLog_FilterDeletingDtor, kFilterDeleting)
{
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kFilterDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kFilterThunk);
    std::mt19937 rng(kFilterDeleting);
    for (int i = 0; i < 80; ++i) {
        uint32_t flags = kFlagSet[i % 8];
        bool viaThunk = (i / 8) % 2 == 1;
        FilterPair p(ctx, rng);
        BeginFrees();
        void *ra = viaThunk ? thunk(reinterpret_cast<uint8_t *>(p.a) + 4, flags) : original(p.a, flags);
        p.blocksA.Snap();
        bool liveA = IsLive(p.a);
        BeginFrees();
        void *rb = viaThunk ? thunks::FilterDeletingFromUnknown(reinterpret_cast<uint8_t *>(p.b) + 4, nullptr, flags) : p.b->DeletingDestructor(flags);
        p.blocksB.Snap();
        bool liveB = IsLive(p.b);
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("flags %x: returns %p/%p, want the object", flags, ra, rb);
        if (liveA == ((flags & 1) != 0) || liveB == ((flags & 1) != 0)) ctx.Fail("flags %x: object freed %d/%d", flags, !liveA, !liveB);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("flags %x: blocks still allocated", flags);
        if (!(flags & 1)) {
            int at = FirstDifference(reinterpret_cast<uint8_t *>(p.a), reinterpret_cast<uint8_t *>(p.b), cLogFilterGroupLevels::kSize, FilterMasks());
            if (at >= 0) ctx.Fail("flags %x: object bytes differ at 0x%x", flags, at);
            ::operator delete(p.a);
            ::operator delete(p.b);
        }
    }
}

// ---- alias table ------------------------------------------------------------------------------------

DIFF_TEST(GZLog_AliasMapDelete, kAliasDelete)
{
    auto original = ctx.Original<AliasMap *(__thiscall *)(AliasMap *, uint32_t)>(kAliasDelete);
    std::mt19937 rng(kAliasDelete);
    for (int i = 0; i < 120; ++i) {
        uint32_t flags = kFlagSet[i % 8];
        AliasMap *a = new (::operator new(sizeof(AliasMap))) AliasMap();
        AliasMap *b = new (::operator new(sizeof(AliasMap))) AliasMap();
        int entries = rng() % 6 == 0 ? 0 : rng() % 30;
        for (int k = 0; k < entries; ++k) {
            std::string key = RandomText(rng) + std::to_string(k), value = RandomText(rng);
            AddAliasText(a, nullptr, key.c_str(), value.c_str());
            AddAliasText(b, nullptr, key.c_str(), value.c_str());
        }
        Blocks blocksA, blocksB;
        CollectHash(blocksA, reinterpret_cast<const uint8_t *>(a), *a, true);
        CollectHash(blocksB, reinterpret_cast<const uint8_t *>(b), *b, true);
        BeginFrees();
        AliasMap *ra = original(a, flags);
        blocksA.Snap();
        bool liveA = IsLive(a);
        BeginFrees();
        AliasMap *rb = DeleteAliasMap(b, nullptr, flags);
        blocksB.Snap();
        bool liveB = IsLive(b);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("flags %x: returns the table: %d/%d", flags, ra == a, rb == b);
        if (liveA == ((flags & 1) != 0) || liveB == ((flags & 1) != 0)) ctx.Fail("flags %x: table block freed %d/%d", flags, !liveA, !liveB);
        if (blocksA.snap != 0 || blocksB.snap != 0) ctx.Fail("flags %x: %zu/%zu of %zu blocks still allocated", flags, blocksA.snap, blocksB.snap, blocksA.n);
        if (!(flags & 1)) {
            // The destroyed table's bucket vector is cleared; the list head pointer is left dangling.
            const uint32_t *wa = reinterpret_cast<const uint32_t *>(a), *wb = reinterpret_cast<const uint32_t *>(b);
            if (wa[0] != wb[0] || wa[2] != wb[2] || wa[3] != wb[3] || wa[4] != wb[4] || wa[5] != wb[5] || wa[6] != wb[6] || wa[7] != wb[7]) ctx.Fail("flags %x: destroyed table words differ", flags);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

// ---- thread signal ------------------------------------------------------------------------------------

namespace {

std::vector<std::string> gSignalLog;
uintptr_t gHandleCounter2 = 0;

HANDLE WINAPI FakeCreateEvent2(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR)
{
    return reinterpret_cast<HANDLE>(0x6000 + 4 * gHandleCounter2++);
}

BOOL WINAPI FakeCloseHandle(HANDLE h)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "CloseHandle(%p)", static_cast<void *>(h));
    gSignalLog.push_back(buf);
    return TRUE;
}

struct SignalPair {
    cRZThreadSignal *a, *b;
    Blocks blocksA, blocksB;
    SignalPair(difftest::Context &ctx, std::mt19937 &rng)
    {
        uint32_t seed = rng();
        a = reinterpret_cast<cRZThreadSignal *>(NewFilled(sizeof(cRZThreadSignal), seed));
        b = reinterpret_cast<cRZThreadSignal *>(NewFilled(sizeof(cRZThreadSignal), seed));
        gHandleCounter2 = seed % 64;
        a->Construct(seed);
        gHandleCounter2 = seed % 64;
        b->Construct(seed);
        std::string text = RandomText(rng);
        a->mState->mName.mString = text;
        b->mState->mName.mString = text;
        for (cRZThreadSignal *s : {a, b}) {
            Blocks &blocks = s == a ? blocksA : blocksB;
            blocks.Add(s->mState);
            blocks.Add(s->mState->mLock.mBlock);
            blocks.AddString(s->mState->mName.mString);
        }
        (void)ctx;
    }
};

} // namespace

DIFF_TEST(GZLog_SignalDtor, kSignalDtor)
{
    Slots slots(ctx);
    slots.Set(cRZThreadSignal::kIatCreateEventA, reinterpret_cast<void *>(&FakeCreateEvent2));
    slots.Set(cRZThreadSignal::kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
    auto original = ctx.Original<void(__thiscall *)(void *)>(kSignalDtor);
    std::mt19937 rng(kSignalDtor);
    for (int i = 0; i < 100; ++i) {
        SignalPair p(ctx, rng);
        gSignalLog.clear();
        BeginFrees();
        original(p.a);
        p.blocksA.Snap();
        std::vector<std::string> logA = gSignalLog;
        gSignalLog.clear();
        BeginFrees();
        p.b->Destruct();
        p.blocksB.Snap();
        ++ctx.cases;
        if (logA != gSignalLog || logA.size() != 1) ctx.Fail("CloseHandle calls differ: %zu/%zu", logA.size(), gSignalLog.size());
        if (p.a->mVTable != p.b->mVTable || p.b->mVTable != ctx.image.At(cRZThreadSignal::kVTable)) ctx.Fail("vtable");
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("state blocks still allocated: %zu/%zu of %zu", p.blocksA.snap, p.blocksB.snap, p.blocksA.n);
        ::operator delete(p.a);
        ::operator delete(p.b);
    }
}

DIFF_TEST(GZLog_SignalDeletingDtor, kSignalDeleting)
{
    Slots slots(ctx);
    slots.Set(cRZThreadSignal::kIatCreateEventA, reinterpret_cast<void *>(&FakeCreateEvent2));
    slots.Set(cRZThreadSignal::kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kSignalDeleting);
    std::mt19937 rng(kSignalDeleting);
    for (int i = 0; i < 80; ++i) {
        uint32_t flags = kFlagSet[i % 8];
        SignalPair p(ctx, rng);
        gSignalLog.clear();
        BeginFrees();
        void *ra = original(p.a, flags);
        p.blocksA.Snap();
        std::vector<std::string> logA = gSignalLog;
        bool liveA = IsLive(p.a);
        gSignalLog.clear();
        BeginFrees();
        void *rb = p.b->DeletingDestructor(flags);
        p.blocksB.Snap();
        bool liveB = IsLive(p.b);
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("flags %x: returns the object: %d/%d", flags, ra == p.a, rb == p.b);
        if (logA != gSignalLog || logA.size() != 1) ctx.Fail("flags %x: CloseHandle calls differ", flags);
        if (liveA == ((flags & 1) != 0) || liveB == ((flags & 1) != 0)) ctx.Fail("flags %x: object freed %d/%d", flags, !liveA, !liveB);
        if (p.blocksA.snap != 0 || p.blocksB.snap != 0) ctx.Fail("flags %x: state blocks still allocated", flags);
        if (!(flags & 1)) {
            ::operator delete(p.a);
            ::operator delete(p.b);
        }
    }
}

DIFF_TEST(GZLog_SignalStateDtor, kSignalStateDtor)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kSignalStateDtor);
    std::mt19937 rng(kSignalStateDtor);
    for (int i = 0; i < 100; ++i) {
        uint32_t seed = rng();
        auto *a = reinterpret_cast<cRZThreadSignalState *>(NewFilled(cRZThreadSignalState::kSize, seed));
        auto *b = reinterpret_cast<cRZThreadSignalState *>(NewFilled(cRZThreadSignalState::kSize, seed));
        a->Construct();
        b->Construct();
        std::string text = RandomText(rng);
        a->mName.mString = text;
        b->mName.mString = text;
        Blocks blocksA, blocksB;
        blocksA.Add(a->mLock.mBlock);
        blocksA.AddString(a->mName.mString);
        blocksB.Add(b->mLock.mBlock);
        blocksB.AddString(b->mName.mString);
        BeginFrees();
        original(a);
        blocksA.Snap();
        BeginFrees();
        b->Destruct();
        blocksB.Snap();
        ++ctx.cases;
        std::vector<std::pair<size_t, size_t>> skip;
        MaskString(skip, 0xc);
        skip.push_back({0x28 + 4, 4});
        int at = FirstDifference(reinterpret_cast<uint8_t *>(a), reinterpret_cast<uint8_t *>(b), cRZThreadSignalState::kSize, skip);
        if (at >= 0) ctx.Fail("state bytes differ at 0x%x", at);
        if (blocksA.snap != 0 || blocksB.snap != 0) ctx.Fail("blocks still allocated: %zu/%zu", blocksA.snap, blocksB.snap);
        ::operator delete(a);
        ::operator delete(b);
    }
}
