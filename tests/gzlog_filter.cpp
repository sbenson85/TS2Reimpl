// Differential tests for nGZLog::cGZLogFilterGroupLevels's slots (src/rz/nGZLogFilter.cpp, lane LY): Accept
// (slot 4, 0xc9dbfb), Write (slot 5, 0xc9e96e), Clone (slot 8, 0xc9cf67), SetGroupLevel (slot 9, 0xc9cbf2) and
// GroupLevel (slot 10, 0xc9d322).
//
// Side A is a filter built by the image's constructor (0xc9c113) and driven by the image's slots, side B one built
// by ours and driven by ours; a script of SetGroupLevel calls (reserved names in any case, repeated and new groups)
// fills both. The two group-name pointers (0x1085d9c, 0x1085d98) and the level text pointers are pointed at test
// texts, so the reserved names vary. The record and the output stream are fakes whose slots log their calls;
// Accept's call of slot 10 lands on a stand-in (the image's 0xc9d322 and ours) that logs and answers from the
// script. Write's eax is leftover (the last write's answer, or the table's list head when the table is empty), so
// it is not compared; a clone's list order is its rehash's, not the source's. Compared: results (al where the
// original defines only al), the logs, the two levels, the table's
// contents in list order and its bucket words.
#include "code_patch.h"
#include "difftest.h"
#include "game/vtable.h"
#include "rz/nGZLog.h"

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace nGZLog;

namespace {

constexpr uint32_t kAccept = 0xc9dbfb, kWrite = 0xc9e96e, kClone = 0xc9cf67, kSetGroupLevel = 0xc9cbf2, kGroupLevel = 0xc9d322;
constexpr uint32_t kFilterCtor = 0xc9c113, kFilterDtor = 0xc9c724;
constexpr uint32_t kAnyGroupPointer = 0x1085d9c, kNoGroupPointer = 0x1085d98;
constexpr uint32_t kLevelPointers[7] = {0x1085d94, 0x1085d6c, 0x1085d74, 0x1085d7c, 0x1085d84, 0x1085d8c, 0x1085d64}; // levels 0, 2..7
const char *const kLevelTexts[7] = {"L0", "L2", "L3", "L4", "L5", "L6", "L7"};

std::string gLog;

void Log(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += ';';
}

// Saves and restores pointer globals of the mapped image.
class Globals {
public:
    explicit Globals(difftest::Context &c) : ctx(c) {}
    ~Globals()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<uint32_t>(it->first) = it->second;
    }
    void Set(uint32_t rva, const void *pointer)
    {
        uint32_t *slot = ctx.image.At<uint32_t>(rva);
        saved.emplace_back(rva, *slot);
        *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, uint32_t>> saved;
};

// The reserved names: the game's own spelling and two others.
struct Reserved {
    const char *any;
    const char *none;
};
const Reserved kReserved[] = {{"AnyGroup", "NoGroup"}, {"everything", "Nothing"}, {"*", "-"}};

void PointGlobals(Globals &globals, const Reserved &reserved)
{
    globals.Set(kAnyGroupPointer, reserved.any);
    globals.Set(kNoGroupPointer, reserved.none);
    for (int i = 0; i < 7; ++i) globals.Set(kLevelPointers[i], kLevelTexts[i]);
}

// ---- the fakes ----

uint32_t gWriteCount = 0;

struct FakeStream {
    void *const *vtable;
};
uint32_t __fastcall StreamText(FakeStream *, void *, const char *text)
{
    Log("t'%s'", text);
    return 0x5100 + gWriteCount++;
}
uint32_t __fastcall StreamString(FakeStream *, void *, const cRZString *text)
{
    Log("s'%s'", text->mString.c_str());
    return 0x5200 + gWriteCount++;
}
void *gStreamVTable[16];

struct FakeRecord {
    void *const *vtable;
    std::vector<int32_t> levels; // successive answers of slot 3 (the last repeats)
    size_t next;
    const char *group;
};
int32_t __fastcall RecordLevel(FakeRecord *self, void *)
{
    const int32_t v = self->levels[self->next < self->levels.size() ? self->next : self->levels.size() - 1];
    ++self->next;
    Log("level=%d", v);
    return v;
}
const char *__fastcall RecordGroup(FakeRecord *self, void *)
{
    Log("group");
    return self->group;
}
void *gRecordVTable[8];

struct Setup {
    Setup()
    {
        gStreamVTable[14] = reinterpret_cast<void *>(&StreamText);
        gStreamVTable[15] = reinterpret_cast<void *>(&StreamString);
        gRecordVTable[3] = reinterpret_cast<void *>(&RecordLevel);
        gRecordVTable[5] = reinterpret_cast<void *>(&RecordGroup);
    }
} gSetup;

// The group level stand-in Accept reaches through slot 10.
int32_t gGroupAnswer = 0;
const void *gFilterSelf = nullptr;
int32_t __fastcall StandGroupLevel(cLogFilterGroupLevels *self, void *, const char *group)
{
    Log("groupLevel(%s, '%s')=%d", self == gFilterSelf ? "this" : "other", group ? group : "(null)", gGroupAnswer);
    return gGroupAnswer;
}

int32_t RandomLevel(std::mt19937 &rng)
{
    static const int32_t kLevels[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x7fffffff, INT32_MIN, 100};
    return kLevels[rng() % (sizeof(kLevels) / sizeof(kLevels[0]))];
}

const char *const kGroups[] = {"AnyGroup", "anygroup", "NOGROUP", "NoGroup", "everything", "EVERYTHING", "nothing", "*", "-",
                               "Net", "net", "Render", "a", "b", "c", "", "Sound.Mixer", "x y", "Script", "Lot",
                               "g01", "g02", "g03", "g04", "g05", "g06", "g07", "g08", "g09", "g10", "g11", "g12"};
constexpr size_t kGroupCount = sizeof(kGroups) / sizeof(kGroups[0]);

// A pair of filters, A built and driven by the image, B by ours.
struct Filters {
    difftest::Context &ctx;
    cLogFilterGroupLevels *a, *b;
    explicit Filters(difftest::Context &c, const std::string &name) : ctx(c)
    {
        a = static_cast<cLogFilterGroupLevels *>(::operator new(cLogFilterGroupLevels::kSize));
        b = static_cast<cLogFilterGroupLevels *>(::operator new(cLogFilterGroupLevels::kSize));
        ctx.Original<cLogFilterGroupLevels *(__thiscall *)(void *)>(kFilterCtor)(a);
        b->Construct();
        a->mName.mString = name;
        b->mName.mString = name;
    }
    ~Filters()
    {
        ctx.Original<void(__thiscall *)(void *)>(kFilterDtor)(a);
        ::operator delete(a);
        b->Destruct();
        ::operator delete(b);
    }
    Filters(const Filters &) = delete;
    Filters &operator=(const Filters &) = delete;

    // `count` SetGroupLevel calls, the same on both sides.
    void Fill(std::mt19937 &rng, int count)
    {
        auto set = ctx.Original<void(__thiscall *)(void *, const char *, int32_t)>(kSetGroupLevel);
        for (int k = 0; k < count; ++k) {
            const char *group = kGroups[rng() % kGroupCount];
            const int32_t level = RandomLevel(rng);
            set(a, group, level);
            b->SetGroupLevel(group, level);
        }
    }
};

// The table in list order, the two levels and the bucket words (mask and maximum index at +0x18 / +0x1c).
std::string Contents(const cLogFilterGroupLevels *f)
{
    std::string s = "any=" + std::to_string(f->mAnyGroupLevel) + " none=" + std::to_string(f->mNoGroupLevel) + " [";
    for (const auto &entry : f->mLevels) s += entry.first.mString + "=" + std::to_string(static_cast<int32_t>(entry.second)) + ",";
    const uint32_t *words = reinterpret_cast<const uint32_t *>(&f->mLevels);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "] size %u mask %u max %u", words[2], words[6], words[7]);
    return s + buf;
}

} // namespace

DIFF_TEST(GZLogFilter_SetGroupLevel, kSetGroupLevel)
{
    Globals globals(ctx);
    std::mt19937 rng(kSetGroupLevel);
    for (int i = 0; i < 300; ++i) {
        const Reserved &reserved = kReserved[i % 3];
        PointGlobals(globals, reserved);
        Filters f(ctx, "F");
        f.Fill(rng, static_cast<int>(rng() % 40));
        ++ctx.cases;
        const std::string ca = Contents(f.a), cb = Contents(f.b);
        if (ca != cb) ctx.Fail("case %d: %.200s / %.200s", i, ca.c_str(), cb.c_str());
    }
}

DIFF_TEST(GZLogFilter_GroupLevel, kGroupLevel)
{
    Globals globals(ctx);
    auto original = ctx.Original<int32_t(__thiscall *)(void *, const char *)>(kGroupLevel);
    std::mt19937 rng(kGroupLevel);
    for (int i = 0; i < 200; ++i) {
        PointGlobals(globals, kReserved[i % 3]);
        Filters f(ctx, "F");
        f.Fill(rng, static_cast<int>(rng() % 30));
        for (size_t g = 0; g < kGroupCount; ++g) {
            const int32_t ra = original(f.a, kGroups[g]);
            const int32_t rb = f.b->GroupLevel(kGroups[g]);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("case %d '%s': %d / %d", i, kGroups[g], ra, rb);
        }
    }
}

DIFF_TEST(GZLogFilter_Write, kWrite)
{
    Globals globals(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kWrite);
    std::mt19937 rng(kWrite);
    FakeStream stream{gStreamVTable};
    static const char *const kNames[] = {"Default", "", "my filter"};
    for (int i = 0; i < 200; ++i) {
        PointGlobals(globals, kReserved[i % 3]);
        Filters f(ctx, kNames[rng() % 3]);
        f.Fill(rng, static_cast<int>(rng() % 30));
        gLog.clear();
        gWriteCount = 0;
        original(f.a, &stream);
        const std::string logA = gLog;
        gLog.clear();
        gWriteCount = 0;
        f.b->Write(&stream);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: %.240s / %.240s", i, logA.c_str(), gLog.c_str());
    }
}

DIFF_TEST(GZLogFilter_Clone, kClone)
{
    Globals globals(ctx);
    auto original = ctx.Original<cLogFilterGroupLevels *(__thiscall *)(void *)>(kClone);
    std::mt19937 rng(kClone);
    for (int i = 0; i < 200; ++i) {
        PointGlobals(globals, kReserved[i % 3]);
        Filters f(ctx, "Source");
        f.Fill(rng, static_cast<int>(rng() % 30));
        cLogFilterGroupLevels *ca = original(f.a);
        cLogFilterGroupLevels *cb = f.b->Clone();
        ++ctx.cases;
        const std::string sa = Contents(ca), sb = Contents(cb);
        if (sa != sb) ctx.Fail("case %d: %.200s / %.200s", i, sa.c_str(), sb.c_str());
        if (ca->mName.mString != cb->mName.mString || !cb->mName.mString.empty()) ctx.Fail("case %d: names '%s' / '%s'", i, ca->mName.mString.c_str(), cb->mName.mString.c_str());
        if (ca->mVTable != cb->mVTable || ca->mUnknown.mVTable != cb->mUnknown.mVTable || ca->mUnknown.mRefCount != cb->mUnknown.mRefCount) ctx.Fail("case %d: vtables or count", i);
        ctx.Original<void(__thiscall *)(void *)>(kFilterDtor)(ca);
        ::operator delete(ca);
        cb->Destruct();
        ::operator delete(cb);
    }
}

DIFF_TEST(GZLogFilter_Accept, kAccept)
{
    Globals globals(ctx);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(kAccept);
    shape::StandIn groupLevel(kGroupLevel, &cLogFilterGroupLevels::GroupLevel, &StandGroupLevel);
    std::mt19937 rng(kAccept);
    static const char *const kRecordGroups[] = {"Net", "", "AnyGroup", nullptr};
    for (int i = 0; i < 4000; ++i) {
        PointGlobals(globals, kReserved[i % 3]);
        Filters f(ctx, "F");
        const int32_t any = RandomLevel(rng);
        f.a->mAnyGroupLevel = f.b->mAnyGroupLevel = any;
        f.a->mNoGroupLevel = f.b->mNoGroupLevel = RandomLevel(rng);
        gGroupAnswer = RandomLevel(rng);
        FakeRecord record{gRecordVTable, {RandomLevel(rng), RandomLevel(rng)}, 0, kRecordGroups[rng() % 4]};
        if (rng() % 2) record.levels[1] = record.levels[0];
        gLog.clear();
        gFilterSelf = f.a;
        record.next = 0;
        const uint8_t ra = static_cast<uint8_t>(original(f.a, &record));
        const std::string logA = gLog;
        gLog.clear();
        gFilterSelf = f.b;
        record.next = 0;
        const uint8_t rb = f.b->Accept(&record);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("case %d: any %d group %d levels %d,%d: %u / %u", i, any, gGroupAnswer, record.levels[0], record.levels[1], ra, rb);
        if (logA != gLog) ctx.Fail("case %d: %.200s / %.200s", i, logA.c_str(), gLog.c_str());
    }
}
