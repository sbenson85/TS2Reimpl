// Differential tests for cTSPersistSystem helpers (lane IP): MakePathKey 0x1e6e80, NormalisePath 0x1e8b80, FillInSegments 0x1e95c0,
// SaveChangeRecords 0x1dedf0, PathStamp 0x1e8ef0, ResetPathEntry 0x1e94e0, IndexSegmentGroups 0x1e9b50,
// SegmentGroups 0x1e97b0, ParkOpenSegments 0x1ea760, RelistParkedSegments 0x1e9eb0, LoadResourceNames 0x1e8fd0,
// SaveResourceNames 0x1ea4f0 (and ITS slot 56, 0x1e3b10, which forwards to it), SaveGroupMap 0x1ea190,
// BackgroundOpenThread 0x1defe0.
//
// MakePathKey's world: the cTSAppCommonImpl object (reached through the cache global 0x10cc040 by the forwarder 0x1d0c00,
// ours and the image's) is a fake whose slot 4 answers the user folder (0x2bcfce83) and the game folder of an option bit
// (0x4bcfce5e) from a seeded table, logging each call with the text it was handed (the game folder string is reused
// across the loop, so a stale text shows up); the option mask global 0x10faf94 is seeded non-zero, so HighestOptionBit
// (ours and the image's) only reads it. cRZString::Sprintf (0x1ad6f7 and ours) is a stand-in on both sides that logs its
// format and argument and writes the text itself. Neither function reads its object (the original passes `this` on to
// NormalisePath, which ignores it), so `this` is a zeroed buffer. Compared: the call log, the key (text and capacity) and
// the path afterwards. The texts are ASCII: the CRT's tolower (0x837529 in the image, ours in our build) is library code
// whose answer above 0x7f depends on the locale each CRT was left in.
#include "code_patch.h"
#include "difftest.h"
#include "app_debugger_fakes.h"
#include "app/AppInit.h"
#include "rz/cRZFileName.h"
#include "rz/PathFileOps.h"
#include "rz/cRZString.h"
#include "rz/StaticGetters.h"
#include "rz/nGZLog.h"
#include "simantics/ServiceRefVector.h"
#include "ts/ChangeRecordSet.h"
#include "ts/cTSPersistSystem.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <cstddef>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string gLog;

struct Folder {
    bool answers;  // al
    bool sets;     // the text written
    std::string text;
};
Folder gUser;
Folder gGame[32];

void SetText(cRZString *s, const std::string &text)
{
    s->mString.assign(text.data(), text.size());
}

std::string Describe(const cRZString *s)
{
    return std::string(s->mVTable ? "v:" : "nov:") + s->mString + (s->mString.capacity() == 15 ? "|sso" : "|big");
}

uint32_t __fastcall Slot4(void *, void *, uint32_t id, cRZString *text, uint32_t c, uint32_t d, uint32_t e)
{
    char line[96];
    std::snprintf(line, sizeof line, "slot4 %08x %u %u %08x [", id, c, d, e);
    gLog += line;
    gLog += Describe(text) + "]\n";
    const Folder *folder = nullptr;
    if (id == 0x2bcfce83) folder = &gUser;
    else if (id == 0x4bcfce5e) {
        for (int bit = 0; bit < 32; ++bit)
            if (e == (1u << bit)) folder = &gGame[bit];
    }
    if (!folder) return 0x5500;
    if (folder->sets) SetText(text, folder->text);
    return folder->answers ? 0xabcdef01u : 0xabcdef00u;
}

cRZString *__cdecl SprintfStand(cRZString *self, const char *fmt, uint32_t value)
{
    gLog += "sprintf [" + std::string(fmt) + "] " + std::to_string(value) + " on [" + Describe(self) + "]\n";
    char text[48];
    std::snprintf(text, sizeof text, "<G%u>", value);
    SetText(self, text);
    return self;
}

struct FakeObject {
    void **vtable;
    uint32_t pad[3];
};
void *gCommonVtable[16];
FakeObject gCommon;

// A folder text in seeded case and separators, maybe with a trailing separator.
std::string Variant(std::mt19937 &rng, const std::string &base)
{
    std::string out = base;
    for (char &c : out) {
        if ((c == '\\' || c == '/') && rng() % 3 == 0) c = c == '\\' ? '/' : '\\';
        else if (c >= 'a' && c <= 'z' && rng() % 4 == 0) c = static_cast<char>(c - 32);
        else if (c >= 'A' && c <= 'Z' && rng() % 4 == 0) c = static_cast<char>(c + 32);
    }
    if (rng() % 3 == 0) out += rng() % 2 ? "\\" : "/";
    return out;
}

const char *const kBases[] = {"C:\\Users\\Me\\Documents\\EA Games\\The Sims 2", "C:\\Program Files\\EA GAMES\\The Sims 2\\TSData",
                              "D:\\Games\\Sims2\\EP2", "C:\\Program Files\\EA GAMES\\The Sims 2", "E:\\a", "x", ""};
constexpr uint32_t kBaseCount = sizeof(kBases) / sizeof(kBases[0]);
const char *const kTails[] = {"", "\\Neighborhoods\\N001", "/Downloads/thing.package", "\\Res\\Catalog\\", "Extra\\more",
                              "\\a very long tail that goes on past the small string buffer.package"};
constexpr uint32_t kTailCount = sizeof(kTails) / sizeof(kTails[0]);

} // namespace

DIFF_TEST(TSPersistSystemIP_MakePathKey, 0x1e6e80)
{
    std::memset(gCommonVtable, 0, sizeof(gCommonVtable));
    gCommonVtable[4] = reinterpret_cast<void *>(&Slot4);
    gCommon.vtable = gCommonVtable;
    adt::GlobalOverride<void *> common(ctx, appinit::kCommonImplCacheRva, &gCommon);
    adt::GlobalOverride<uint32_t> mask(ctx, appinit::kOptionMaskRva, 0);
    shape::StandIn format(0x1ad6f7, &cRZString::Sprintf, &SprintfStand);
    auto original = ctx.Original<void(__thiscall *)(void *, const cRZString *, cRZString *)>(0x1e6e80);
    alignas(16) static uint8_t object[0x300];
    auto *ours = reinterpret_cast<cTSPersistSystem *>(object);

    uint32_t userHits = 0, gameHits = 0, numbered = 0, aliased = 0, misses = 0;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed * 2654435761u + 7);
        // The mask: a few low bits and usually 0x1000 (the built mask always has it).
        uint32_t maskValue = (rng() % 5 ? 0x1000u : 0) | (rng() & 0x3f);
        if (rng() % 8 == 0) maskValue = 1u << (rng() % 20);
        if (!maskValue) maskValue = 1;
        mask.Set(maskValue);

        gUser = {rng() % 6 != 0, rng() % 10 != 0, Variant(rng, kBases[rng() % 3 == 0 ? rng() % kBaseCount : 0])};
        if (seed % 50 == 7) gUser = {true, true, ""};
        for (auto &game : gGame) game = {rng() % 3 != 0, rng() % 4 != 0, Variant(rng, kBases[1 + rng() % (kBaseCount - 1)])};

        // The path: a folder the fakes know (or not), a tail.
        std::string text;
        const uint32_t pick = rng() % 4;
        if (pick == 0) text = Variant(rng, gUser.text);
        else if (pick == 1 || pick == 2) text = Variant(rng, gGame[rng() % 14].text);
        else text = Variant(rng, kBases[rng() % kBaseCount]);
        text += kTails[rng() % kTailCount];
        const bool alias = rng() % 6 == 0;
        const std::string keyBefore = rng() % 2 ? "old key text that is long enough for the heap" : "old";

        std::string logs[2], keys[2], paths[2];
        size_t capacities[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            cRZString path, key;
            path.Construct(text.c_str());
            key.Construct(keyBefore.c_str());
            cRZString *target = alias ? &path : &key;
            if (side == 0) original(object, &path, target);
            else ours->MakePathKey(&path, target);
            logs[side] = gLog;
            keys[side] = Describe(target);
            capacities[side] = target->mString.capacity();
            paths[side] = Describe(&path);
            key.Destruct();
            path.Destruct();
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) ctx.Fail("seed %u: calls differ\n--- original\n%s--- ours\n%s", seed, logs[0].c_str(), logs[1].c_str());
        if (keys[0] != keys[1] || capacities[0] != capacities[1])
            ctx.Fail("seed %u: key '%s' (%zu) vs '%s' (%zu)", seed, keys[0].c_str(), capacities[0], keys[1].c_str(), capacities[1]);
        if (paths[0] != paths[1]) ctx.Fail("seed %u: path '%s' vs '%s'", seed, paths[0].c_str(), paths[1].c_str());
        if (keys[0].find("%UserDataDir%") != std::string::npos) ++userHits;
        else if (logs[0].find("sprintf") != std::string::npos) ++numbered;
        else if (keys[0].find("%GameDataDir%") != std::string::npos) ++gameHits;
        else ++misses;
        aliased += alias;
    }
    std::printf("  MakePathKey coverage: user %u, game %u, numbered %u, none %u, aliased %u\n", userHits, gameHits, numbered, misses, aliased);
    if (userHits < 200 || gameHits < 200 || numbered < 200 || misses < 100 || aliased < 200)
        ctx.Fail("coverage: user %u, game %u, numbered %u, none %u, aliased %u", userHits, gameHits, numbered, misses, aliased);
}

DIFF_TEST(TSPersistSystemIP_NormalisePath, 0x1e8b80)
{
    auto original = ctx.Original<void(__thiscall *)(void *, cRZString *)>(0x1e8b80);
    alignas(16) static uint8_t object[0x300];
    auto *ours = reinterpret_cast<cTSPersistSystem *>(object);
    const char kChars[] = "aZ09\\/._ -%Q";
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 rng(seed + 11);
        std::string text;
        const uint32_t length = seed % 7 == 0 ? rng() % 3 : rng() % 40;
        for (uint32_t i = 0; i < length; ++i) text += kChars[rng() % (sizeof(kChars) - 1)];
        if (rng() % 3 == 0) text += rng() % 2 ? "\\" : "//";
        std::string out[2];
        for (int side = 0; side < 2; ++side) {
            cRZString path;
            path.Construct(text.c_str());
            if (side == 0) original(object, &path);
            else ours->NormalisePath(&path);
            out[side] = Describe(&path) + "|" + std::to_string(path.mString.capacity()) + "|" + std::to_string(path.mRefCount);
            path.Destruct();
        }
        ++ctx.cases;
        if (out[0] != out[1]) ctx.Fail("seed %u '%s': '%s' vs '%s'", seed, text.c_str(), out[0].c_str(), out[1].c_str());
    }
}


// FillInSegments 0x1e95c0 (with its callback 0x1e9710 and the stable sort it instantiates). The world: a zeroed primary
// block whose vtable's slot 37 walks a seeded deque of fake segments with the callback (stopping at a zero byte), whose
// ITS face (+0xa8) slot 30 answers per (segment, group) with garbage above al, and whose mSegmentsByGroup (+0x114) is a
// real table of the game's layout (identity hash) with 0..12 groups already listed (lists in allocator-service blocks,
// maybe full). Fake segments log AddRef / Release / QueryInterface (0x286d4a6a answered per segment, with garbage above
// al; some hand themselves out, counted). The allocator service is a fake (InsertGrown grows through it); the game's
// operator new / delete go to ours. Compared: the call log (segments' and the allocator's), the returned list (as its
// node's group), the table in list order (group, members by id, capacity), the bucket count, and every segment's count.
// Deques stay under 0x400 segments: above that the sort's buffer comes from the CRT heap (operator new(nothrow) and
// free in the image), which the harness traps.
namespace fill {

struct IdentityHash {
    size_t operator()(uint32_t key) const { return key; }
};
struct List {
    void **first = nullptr;
    void **last = nullptr;
    void **end = nullptr;
};
using Table = std::unordered_map<uint32_t, List, IdentityHash>;

struct Seg {
    void **vtable;
    uint32_t id;
    int32_t refs;
    uint8_t packed;
    bool handsOut;
};
std::string gCalls;
uint32_t gSeed;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

uint32_t __fastcall SegQuery(Seg *self, void *, uint32_t iid, void **out)
{
    gCalls += "q" + std::to_string(self->id) + (iid == 0x286d4a6a ? "" : "?") + " ";
    if (self->handsOut) {
        *out = self;
        ++self->refs;
    }
    return 0xabcd1200u | self->packed;
}
uint32_t __fastcall SegAddRef(Seg *self, void *)
{
    gCalls += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall SegRelease(Seg *self, void *)
{
    gCalls += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}
void *gSegVTable[8] = {reinterpret_cast<void *>(&SegQuery), reinterpret_cast<void *>(&SegAddRef),
                       reinterpret_cast<void *>(&SegRelease)};

std::vector<Seg *> gDeque;
using Pred = uint32_t(__cdecl *)(void *, void *);
uint32_t __fastcall Walk(void *, void *, Pred pred, void *context)
{
    gCalls += "walk ";
    uint32_t r = 0x55;
    for (Seg *s : gDeque) {
        r = pred(s, context);
        if ((r & 0xff) == 0) break;
    }
    gCalls += "walked(" + std::to_string(r & 0xff) + ") ";
    return r;
}
uint32_t __fastcall InGroup(void *, void *, Seg *segment, uint32_t group)
{
    const uint32_t answer = Mix(gSeed, segment->id * 31 + group) % 3 != 0;
    gCalls += "in" + std::to_string(segment->id) + "/" + std::to_string(group & 0xff) + "=" + std::to_string(answer) + " ";
    return 0xdead0000u | answer;
}
void *gPrimaryVTable[64];
void *gItsVTable[64];

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ServiceRefVector::InsertGrown (0x1d57e0 and ours, verified with its own tests): a stand-in on both sides that logs the
// list's size and capacity and the value, and grows the list itself (a block the allocator fake frees later).
adt::AllocatorFake *gAlloc;
void **__fastcall GrowStand(List *self, void *, void **where, void *const *value)
{
    const size_t size = self->last - self->first, capacity = self->end - self->first, at = where - self->first;
    gCalls += "grow(" + std::to_string(size) + "/" + std::to_string(capacity) + "@" + std::to_string(at) + "," +
              std::to_string(static_cast<Seg *>(*value)->id) + ") ";
    const size_t grown = capacity + capacity / 2 > capacity ? capacity + capacity / 2 : capacity + 1;
    auto **block = static_cast<void **>(::operator new(grown * 4));
    gAlloc->Track(block);
    for (size_t i = 0; i < at; ++i) block[i] = self->first[i];
    block[at] = *value;
    SegAddRef(static_cast<Seg *>(*value), nullptr);
    for (size_t i = at; i < size; ++i) block[i + 1] = self->first[i];
    self->first = block;
    self->last = block + size + 1;
    self->end = block + grown;
    return block + at;
}

} // namespace fill

DIFF_TEST(TSPersistSystemIP_FillInSegments, 0x1e95c0)
{
    using namespace fill;
    static_assert(offsetof(cTSPersistSystem, mSegmentsByGroup) == 0x114, "mSegmentsByGroup");
    static_assert(sizeof(Table) == sizeof(cGZPersistHashMap), "table layout");
    gPrimaryVTable[37] = reinterpret_cast<void *>(&Walk);
    gItsVTable[30] = reinterpret_cast<void *>(&InGroup);
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    adt::AllocatorFake alloc(ctx);
    gAlloc = &alloc;
    shape::StandIn grow(0x1d57e0, &ServiceRefVector::InsertGrown, &GrowStand);
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1e95c0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);

    uint32_t merged = 0, insertion = 0, existing = 0, grown = 0;
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 rng(seed * 7919u + 3);
        const uint32_t segmentCount = seed % 5 == 0 ? 40 + rng() % 120 : rng() % 30;
        const uint32_t groups = rng() % 13;
        const uint32_t group = rng() % 4 == 0 && groups ? 0x100 + rng() % groups : 0x100 + rng() % 20;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::vector<std::unique_ptr<Seg>> segments;
            for (uint32_t i = 0; i < segmentCount; ++i) {
                const uint32_t r = Mix(seed, 1000 + i);
                segments.emplace_back(new Seg{gSegVTable, i, 1, static_cast<uint8_t>(r % 3 == 0 ? 0 : (r % 3 == 1 ? 1 : 0x80)),
                                              (r >> 8) % 2 == 0});
            }
            gDeque.clear();
            for (auto &s : segments) gDeque.push_back(s.get());
            std::memset(block, 0, sizeof(block));
            *reinterpret_cast<void **>(block) = gPrimaryVTable;
            *reinterpret_cast<void **>(block + 0xa8) = gItsVTable;
            auto *table = new (block + 0x114) Table();
            // Groups already listed: 0..5 members each (from the same segments, counted), capacity 0..2 more.
            std::mt19937 worldRng(seed + 99);
            for (uint32_t g = 0; g < groups; ++g) {
                const uint32_t members = segmentCount ? worldRng() % 6 : 0;
                const uint32_t room = worldRng() % 3;
                List list;
                if (members + room) {
                    list.first = static_cast<void **>(::operator new((members + room) * 4));
                    alloc.Track(list.first);
                    list.last = list.first;
                    list.end = list.first + members + room;
                    for (uint32_t m = 0; m < members; ++m) {
                        Seg *s = segments[worldRng() % segmentCount].get();
                        ++s->refs;
                        *list.last++ = s;
                    }
                }
                (*table)[0x100 + g] = list;
            }
            const bool had = table->count(group) != 0;
            void *ret = side == 0 ? original(system, group) : system->FillInSegments(group);
            // The result: the table in list order, the returned list's group, the segments' counts.
            std::string out = gCalls + "| alloc:";
            for (const auto &line : adt::gLog) out += line + ";";
            out += " | ret ";
            bool found = false;
            for (auto &entry : *table) {
                if (ret == &entry.second) {
                    out += std::to_string(entry.first);
                    found = true;
                }
            }
            if (!found) out += "?";
            out += " | buckets " + std::to_string(table->bucket_count()) + " |";
            for (auto &entry : *table) {
                out += " " + std::to_string(entry.first) + ":[";
                for (void **p = entry.second.first; p != entry.second.last; ++p) out += std::to_string(static_cast<Seg *>(*p)->id) + ",";
                out += "]cap" + std::to_string(entry.second.end - entry.second.first);
            }
            out += " | refs";
            for (auto &s : segments) out += " " + std::to_string(s->refs);
            results[side] = out;
            if (side == 0) {
                const auto &list = (*table)[group];
                const size_t n = list.last - list.first;
                if (n > 32) ++merged;
                else if (n > 1) ++insertion;
                if (had) ++existing;
                if (gCalls.find("grow(") != std::string::npos) ++grown;
            }
            table->~Table();
            alloc.Release();
        }
        ++ctx.cases;
        if (results[0] != results[1]) {
            size_t at = 0;
            while (at < results[0].size() && at < results[1].size() && results[0][at] == results[1][at]) ++at;
            const size_t from = at > 60 ? at - 60 : 0;
            ctx.Fail("seed %u: differ at %zu: orig ...%s / ours ...%s", seed, at, results[0].substr(from, 160).c_str(),
                results[1].substr(from, 160).c_str());
        }
    }
    std::printf("  FillInSegments coverage: merge sorts %u, insertion sorts %u, existing lists %u, grown %u\n", merged, insertion,
        existing, grown);
    if (merged < 100 || insertion < 300 || existing < 100 || grown < 300)
        ctx.Fail("coverage: merge %u, insertion %u, existing %u, grown %u", merged, insertion, existing, grown);
}


// SaveChangeRecords 0x1dedf0. The world: a zeroed primary block with mHasChangeRecords seeded, the store (+0x84) a fake
// whose OpenRecord (slot 22) answers per group (handing out a record or not), the record a fake (QueryInterface
// 0xc56b8f08 hands out the output stream; slots 6 Close, 7 Abort, 11 SetSize, 12 GetPosition logged), the stream a fake
// (slots 37, 49, 50, seeded to fail at a step), and mChangeRecords (+0x20c) a real table of 0..8 groups, each a
// ChangeRecordSet (built through ours) of 0..4 entries holding counted fakes, changed or not. ChangeRecordSet::Write
// (0x1f6e90) runs on each side (the image's from the original, ours from ours: verified with its own tests). Compared:
// the call log, each set's changed flag, the system flag, every count.
namespace save {

std::string gCalls;
uint32_t gSeed;
uint32_t gStep, gFailAt;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x85ebca6bu ^ (b + 0x165667b1u + (a << 5) + (a >> 3));
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

struct Obj {
    void **vtable;
    uint32_t id;
    int32_t refs;
};
uint32_t __fastcall ObjAddRef(Obj *self, void *)
{
    gCalls += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall ObjRelease(Obj *self, void *)
{
    gCalls += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}

Obj gStream, gRecord;
uint32_t __fastcall RecordQuery(Obj *self, void *, uint32_t iid, void **out)
{
    gCalls += "record.qi(" + std::to_string(iid == 0xc56b8f08) + ") ";
    *out = &gStream;
    ++gStream.refs;
    return 0xab00u | 1;
}
uint32_t __fastcall RecordSlot(Obj *, void *, int slot)
{
    gCalls += "record." + std::to_string(slot) + " ";
    return 0xab00u | (Mix(gSeed, slot + 50) & 1);
}
uint32_t __fastcall RecordClose(Obj *self, void *e) { return RecordSlot(self, e, 6); }
uint32_t __fastcall RecordAbort(Obj *self, void *e) { return RecordSlot(self, e, 7); }
uint32_t __fastcall RecordSetSize(Obj *, void *, uint32_t size)
{
    gCalls += "record.setsize(" + std::to_string(size) + ") ";
    return 0xab01u;
}
uint32_t __fastcall RecordPosition(Obj *, void *)
{
    gCalls += "record.position ";
    return 0x40 + Mix(gSeed, 77) % 100;
}
void *gRecordVTable[16] = {reinterpret_cast<void *>(&RecordQuery), reinterpret_cast<void *>(&ObjAddRef),
                           reinterpret_cast<void *>(&ObjRelease)};

uint32_t Step()
{
    return 0xcd00u | (gStep++ != gFailAt ? 1u : 0u);
}
uint32_t __fastcall SetU32(Obj *, void *, uint32_t value)
{
    gCalls += "s32(" + std::to_string(value) + ") ";
    return Step();
}
uint32_t __fastcall SetKey(Obj *, void *, const uint32_t *key)
{
    gCalls += "skey(" + std::to_string(key[0]) + "." + std::to_string(key[3]) + ") ";
    return Step();
}
uint32_t __fastcall SetObject(Obj *, void *, const Obj *object)
{
    gCalls += "sobj(" + (object ? std::to_string(object->id) : std::string("null")) + ") ";
    return Step();
}
void *gStreamVTable[64];
void *gObjVTable[4] = {nullptr, reinterpret_cast<void *>(&ObjAddRef), reinterpret_cast<void *>(&ObjRelease)};

uint32_t __fastcall OpenRecord(Obj *, void *, const uint32_t *key, void **record, uint32_t mode)
{
    gCalls += "open(" + std::to_string(key[0] == 0xad1a6720u) + "," + std::to_string(key[1]) + "," + std::to_string(key[2]) + "," +
              std::to_string(key[3]) + ";" + std::to_string(mode) + ") ";
    const uint32_t r = Mix(gSeed, key[2]);
    if (r % 7 == 0) return 0xef00u; // fails, nothing handed out
    if (r % 7 == 1) {               // fails, a record handed out anyway
        *record = &gRecord;
        ++gRecord.refs;
        return 0xef00u;
    }
    *record = &gRecord;
    ++gRecord.refs;
    return 0xef01u;
}
void *gStoreVTable[32];
Obj gStore;

using Table = std::unordered_map<uint32_t, ChangeRecordSet>;

} // namespace save

DIFF_TEST(TSPersistSystemIP_SaveChangeRecords, 0x1dedf0)
{
    using namespace save;
    static_assert(offsetof(cTSPersistSystem, mChangeRecords) == 0x20c && offsetof(cTSPersistSystem, mHasChangeRecords) == 0x22c,
        "change records");
    static_assert(sizeof(Table) == sizeof(cGZPersistHashMap), "table layout");
    gRecordVTable[6] = reinterpret_cast<void *>(&RecordClose);
    gRecordVTable[7] = reinterpret_cast<void *>(&RecordAbort);
    gRecordVTable[11] = reinterpret_cast<void *>(&RecordSetSize);
    gRecordVTable[12] = reinterpret_cast<void *>(&RecordPosition);
    gStreamVTable[1] = reinterpret_cast<void *>(&ObjAddRef);
    gStreamVTable[2] = reinterpret_cast<void *>(&ObjRelease);
    gStreamVTable[37] = reinterpret_cast<void *>(&SetU32);
    gStreamVTable[49] = reinterpret_cast<void *>(&SetKey);
    gStreamVTable[50] = reinterpret_cast<void *>(&SetObject);
    gStoreVTable[22] = reinterpret_cast<void *>(&OpenRecord);
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&fill::GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&fill::GameDelete));
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x1dedf0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);

    uint32_t written = 0, aborted = 0, skipped = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            gStep = 0;
            std::mt19937 rng(seed * 40503u + 1);
            gFailAt = rng() % 3 == 0 ? rng() % 30 : ~0u;
            gStream = Obj{gStreamVTable, 900, 1};
            gRecord = Obj{gRecordVTable, 800, 1};
            gStore = Obj{gStoreVTable, 700, 1};
            std::memset(block, 0, sizeof(block));
            system->mField84 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gStore));
            system->mHasChangeRecords = static_cast<uint8_t>(rng() % 6 ? 1 : 0);
            auto *table = new (&system->mChangeRecords) Table();
            std::vector<std::unique_ptr<Obj>> objects;
            const uint32_t groups = rng() % 9;
            for (uint32_t g = 0; g < groups; ++g) {
                ChangeRecordSet &records = table->try_emplace(0x7f000000u + rng() % 40).first->second;
                const uint32_t entries = rng() % 5;
                for (uint32_t e = 0; e < entries; ++e) {
                    ChangeRecordEntry &entry = records.mRecords[ChangeRecordKey{{rng() % 3, 0, rng() % 4, e}}];
                    entry.changeId = rng() % 9;
                    if (!entry.record.object && rng() % 5) {
                        objects.emplace_back(new Obj{gObjVTable, static_cast<uint32_t>(objects.size()), 1});
                        entry.record.object = objects.back().get();
                    }
                }
                records.mChanged = static_cast<uint8_t>(rng() % 3 ? 1 : 0);
            }
            if (side == 0) original(system);
            else system->SaveChangeRecords();
            std::string out = gCalls + "| flag " + std::to_string(system->mHasChangeRecords) + " |";
            for (auto &entry : *table) out += " " + std::to_string(entry.first) + ":" + std::to_string(entry.second.mChanged);
            out += " | refs " + std::to_string(gStream.refs) + " " + std::to_string(gRecord.refs);
            for (auto &obj : objects) out += " " + std::to_string(obj->refs);
            results[side] = out;
            if (side == 0) {
                for (size_t at = 0; (at = gCalls.find("record.6", at)) != std::string::npos; ++at) ++written;
                for (size_t at = 0; (at = gCalls.find("record.7", at)) != std::string::npos; ++at) ++aborted;
                if (!system->mHasChangeRecords && gCalls.empty()) ++skipped;
            }
            table->~Table();
        }
        ++ctx.cases;
        if (results[0] != results[1]) {
            size_t at = 0;
            while (at < results[0].size() && at < results[1].size() && results[0][at] == results[1][at]) ++at;
            const size_t from = at > 80 ? at - 80 : 0;
            ctx.Fail("seed %u: differ at %zu: orig ...%s / ours ...%s", seed, at, results[0].substr(from, 180).c_str(),
                results[1].substr(from, 180).c_str());
        }
    }
    std::printf("  SaveChangeRecords coverage: written %u, aborted %u, skipped %u\n", written, aborted, skipped);
    if (written < 500 || aborted < 200 || skipped < 100) ctx.Fail("coverage: written %u, aborted %u, skipped %u", written, aborted, skipped);
}


// PathStamp 0x1e8ef0, ResetPathEntry 0x1e94e0, IndexSegmentGroups 0x1e9b50. The world: a zeroed primary block whose
// mSegmentsByGroup (+0x114, identity hash), mSegmentIdRanges (+0x134, std::hash) and mMap154 (+0x154, HashChars over the
// key text) are real tables of the game's layout, seeded per case: path entries (some for the paths the segments name,
// with garbage words and 0..6 values), ids mapped to those entries' vectors or to vectors of the test's own, group lists
// of counted fake segments (blocks from the allocator fake, which 0x1e6a40, original on both sides, frees). Fake segments
// answer QueryInterface 0x3b2271bb (a face whose slot 4 gives a value, maybe 0; handed out or not), slot 11 (their path,
// one of a few, so entries are shared), slot 15 (an id from a small range) and slot 19 (0..8 keys, groups from a small
// range and 0x1c0532fa, in a block; the count maybe 0 with a block anyway). Stand-ins on both sides: MakePathKey (0x1e6e80
// and ours; verified with its own test) keys a path as "k:" + its text lowercased, 0x1c2068 (the file's time, original
// code reaching imports the harness traps) answers a function of the text (0 for some), ServiceBlockVector::FreeBlock
// (0x195720 and ours) logs and frees the key block. NormalisePath runs for real (verified). The game's operator new /
// delete go to ours. Compared: the call log, every table in its own order (keys, words, values and capacity, the vector
// each id maps to, the group lists), bucket counts, every count.
namespace groups {

std::string gCalls;
uint32_t gSeed;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x27d4eb2fu ^ (b + 0x165667b1u + (a << 7) + (a >> 3));
    h ^= h >> 15;
    h *= 0x85ebca77u;
    h ^= h >> 13;
    return h;
}

uint32_t TextHash(const std::string &text)
{
    uint32_t h = 2166136261u;
    for (char c : text) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

// The path-entry table's types, as the game lays them out (key: a cRZString copied with its text; count 0).
struct Key {
    cRZString text;
    Key() { text.Construct(); }
    Key(const Key &other)
    {
        text.Construct();
        text.mString = other.text.mString;
    }
    Key &operator=(const Key &) = delete;
    ~Key() { text.Destruct(); }
};
struct KeyHash {
    size_t operator()(const Key &key) const { return nGZLog::HashChars(key.text.ToChar()); }
};
struct KeyEqual {
    bool operator()(const Key &a, const Key &b) const { return a.text.mString == b.text.mString; }
};
struct Entry {
    uint32_t words[2];
    std::vector<uint32_t> values;
};
using PathTable = std::unordered_map<Key, Entry, KeyHash, KeyEqual>;
using IdTable = std::unordered_map<uint32_t, std::vector<uint32_t> *>;
struct IdentityHash {
    size_t operator()(uint32_t key) const { return key; }
};
struct List {
    void **first = nullptr;
    void **last = nullptr;
    void **end = nullptr;
};
using GroupTable = std::unordered_map<uint32_t, List, IdentityHash>;
static_assert(sizeof(PathTable) == 0x20 && sizeof(IdTable) == 0x20 && sizeof(GroupTable) == 0x20, "table layout");
static_assert(sizeof(std::pair<const Key, Entry>) + 8 == 0x3c, "path node");

struct Obj {
    void **vtable;
    uint32_t id;
    int32_t refs;
    uint32_t value;  // the face's value
    bool answers;    // QueryInterface
    bool handsOut;
    uint32_t segId;  // slot 15
    std::string path;
    uint32_t keysAnswer;
    uint8_t open;    // slot 7
    std::vector<uint32_t> keys; // type, group pairs
    Obj *face;
};

uint32_t __fastcall ObjAddRef(Obj *self, void *)
{
    gCalls += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall ObjRelease(Obj *self, void *)
{
    gCalls += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall SegQuery(Obj *self, void *, uint32_t iid, void **out)
{
    gCalls += "q" + std::to_string(self->id) + (iid == 0x3b2271bb ? "" : "?") + " ";
    if (self->handsOut) {
        *out = self->face;
        ++self->face->refs;
    }
    return 0xabcd1200u | (self->answers ? 1u : 0u);
}
uint32_t __fastcall SegPath(Obj *self, void *, cRZString *path)
{
    gCalls += "path" + std::to_string(self->id) + "[" + path->mString + "] ";
    path->mString = self->path;
    return 0x7701;
}
uint32_t __fastcall SegId(Obj *self, void *)
{
    gCalls += "id" + std::to_string(self->id) + " ";
    return self->segId;
}
uint32_t __fastcall SegKeys(Obj *self, void *, ServiceBlockVector *keys, void *filter)
{
    gCalls += "keys" + std::to_string(self->id) + (filter ? "f" : "") + "[" + std::to_string(keys->mFirst != nullptr) + "] ";
    const size_t n = self->keys.size() / 2;
    if (n) {
        auto *block = static_cast<uint32_t *>(::operator new(n * 16));
        for (size_t i = 0; i < n; ++i) {
            block[i * 4] = self->keys[i * 2];
            block[i * 4 + 1] = self->keys[i * 2 + 1];
            block[i * 4 + 2] = 0x11110000u + static_cast<uint32_t>(i);
            block[i * 4 + 3] = 0x22220000u + static_cast<uint32_t>(i);
        }
        keys->mFirst = block;
        keys->mLast = keys->mEnd = block + n * 4;
    }
    return self->keysAnswer;
}
uint32_t __fastcall SegOpen(Obj *self, void *)
{
    gCalls += "open" + std::to_string(self->id) + " ";
    return 0xab00u | self->open;
}
uint32_t __fastcall FaceValue(Obj *self, void *)
{
    gCalls += "value" + std::to_string(self->id) + " ";
    return self->value;
}
void *gSegVTable[24];
void *gFaceVTable[8];

void __fastcall KeyStand(void *, void *, const cRZString *path, cRZString *key)
{
    gCalls += "makekey[" + path->mString + "] ";
    std::string text = "k:";
    for (char c : path->mString) text += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    key->mString = text;
}
uint32_t __cdecl StatStand(const cRZString *path, uint32_t which)
{
    gCalls += "stat[" + path->mString + "]" + std::to_string(which) + " ";
    const uint32_t h = Mix(gSeed, TextHash(path->mString));
    return h % 4 == 0 ? 0 : 0x1000 + h % 3;
}
void __fastcall FreeStand(ServiceBlockVector *self, void *)
{
    gCalls += "free" + std::to_string(self->mFirst ? (static_cast<uint8_t *>(self->mLast) - static_cast<uint8_t *>(self->mFirst)) / 16 : 99) + " ";
    ::operator delete(self->mFirst);
    self->mFirst = self->mLast = self->mEnd = nullptr;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

const char *const kPaths[] = {"C:\\Sims\\Downloads\\One.package", "c:/sims/downloads/one.package", "D:\\Game\\TSData\\Res\\Objects",
                              "D:/Game/TSData/Res/Objects/", "E:\\a very long folder name that is not small\\Two.package", "x"};
constexpr uint32_t kPathCount = sizeof(kPaths) / sizeof(kPaths[0]);

struct World {
    std::vector<std::unique_ptr<Obj>> segments;
    std::vector<std::unique_ptr<std::vector<uint32_t>>> own; // the test's own id vectors
};

// Fills the tables at +0x114, +0x134, +0x154 and the segments from `rng` (the same on both sides).
void Build(uint8_t *block, World &world, std::mt19937 &rng, adt::AllocatorFake &alloc)
{
    auto *paths = new (block + 0x154) PathTable();
    auto *ids = new (block + 0x134) IdTable();
    auto *byGroup = new (block + 0x114) GroupTable();
    const uint32_t segmentCount = 1 + rng() % 4;
    for (uint32_t i = 0; i < segmentCount; ++i) {
        auto seg = std::make_unique<Obj>();
        auto face = std::make_unique<Obj>();
        seg->vtable = gSegVTable;
        seg->id = i;
        seg->refs = 1;
        seg->answers = rng() % 3 != 0;
        seg->handsOut = seg->answers || rng() % 2 != 0;
        seg->segId = 0x500 + rng() % 6;
        seg->path = kPaths[rng() % kPathCount];
        seg->keysAnswer = rng() % 5 == 0 ? 0 : 0x300 + rng() % 3;
        const uint32_t keyCount = rng() % 9;
        for (uint32_t k = 0; k < keyCount; ++k) {
            seg->keys.push_back(0x7000 + rng() % 5);
            seg->keys.push_back(rng() % 4 == 0 ? 0x1c0532fau : 0x100 + rng() % 12);
        }
        face->vtable = gFaceVTable;
        face->id = 50 + i;
        face->refs = 1;
        face->value = rng() % 3 == 0 ? 0 : (rng() % 4 ? 0x100 + rng() % 3 : 0x7000 + rng() % 5);
        seg->open = static_cast<uint8_t>(rng() % 2);
        seg->face = face.get();
        world.segments.push_back(std::move(seg));
        world.segments.push_back(std::move(face));
    }
    // Path entries: keyed as the stand-in keys them, for some of the paths, and a few others.
    const uint32_t entries = rng() % 5;
    for (uint32_t e = 0; e < entries; ++e) {
        Key key;
        if (rng() % 3) {
            key.text.mString = "k:";
            for (const char *c = kPaths[rng() % kPathCount]; *c; ++c) key.text.mString += static_cast<char>(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        } else {
            key.text.mString = "other" + std::to_string(rng() % 4);
        }
        Entry &entry = (*paths)[key];
        // The words a segment's list is current for: the path's stamp (0 takes any), the face's value.
        entry.words[0] = rng() % 3 == 0 ? 0 : (rng() % 3 ? 0x1000 + rng() % 3 : rng());
        entry.words[1] = rng() % 3 == 0 ? 0 : (rng() % 4 ? 0x100 + rng() % 3 : rng());
        const uint32_t values = rng() % 7;
        for (uint32_t v = 0; v < values; ++v) entry.values.push_back(0x100 + rng() % 12);
        if (rng() % 2) entry.values.reserve(entry.values.size() + rng() % 4);
    }
    // Ids: to an entry's vector, or to a vector of the test's own.
    const uint32_t mapped = rng() % 4;
    for (uint32_t m = 0; m < mapped; ++m) {
        const uint32_t id = 0x500 + rng() % 6;
        if (!paths->empty() && rng() % 2) {
            auto it = paths->begin();
            std::advance(it, rng() % paths->size());
            (*ids)[id] = &it->second.values;
        } else {
            world.own.emplace_back(new std::vector<uint32_t>());
            const uint32_t values = rng() % 5;
            for (uint32_t v = 0; v < values; ++v) world.own.back()->push_back(0x100 + rng() % 12);
            (*ids)[id] = world.own.back().get();
        }
    }
    // Group lists of counted segments.
    const uint32_t groupCount = rng() % 10;
    for (uint32_t g = 0; g < groupCount; ++g) {
        const uint32_t group = rng() % 5 == 0 ? 0x7000 + rng() % 5 : (rng() % 6 == 0 ? 0x1c0532fau : 0x100 + rng() % 12);
        if (byGroup->count(group)) continue;
        const uint32_t members = rng() % 4, room = rng() % 2;
        List list;
        if (members + room) {
            list.first = static_cast<void **>(::operator new((members + room) * 4));
            alloc.Track(list.first);
            list.last = list.first;
            list.end = list.first + members + room;
            for (uint32_t i = 0; i < members; ++i) {
                Obj *s = world.segments[2 * (rng() % segmentCount)].get();
                ++s->refs;
                *list.last++ = s;
            }
        }
        (*byGroup)[group] = list;
    }
}

std::string VectorName(uint8_t *block, const World &world, const std::vector<uint32_t> *v)
{
    for (auto &entry : *reinterpret_cast<PathTable *>(block + 0x154))
        if (&entry.second.values == v) return entry.first.text.mString;
    for (size_t i = 0; i < world.own.size(); ++i)
        if (world.own[i].get() == v) return "own" + std::to_string(i);
    return "?";
}

std::string Values(const std::vector<uint32_t> &v)
{
    std::string out = "[";
    for (uint32_t x : v) out += std::to_string(x) + ",";
    return out + "]cap" + std::to_string(v.capacity());
}

std::string Dump(uint8_t *block, const World &world)
{
    std::string out = "| alloc:";
    for (const auto &line : adt::gLog) out += line + ";";
    auto &paths = *reinterpret_cast<PathTable *>(block + 0x154);
    out += " | paths " + std::to_string(paths.bucket_count()) + ":";
    for (auto &entry : paths)
        out += " " + entry.first.text.mString + "/" + std::to_string(entry.first.text.mRefCount) + "=" + std::to_string(entry.second.words[0]) +
               "," + std::to_string(entry.second.words[1]) + Values(entry.second.values);
    auto &ids = *reinterpret_cast<IdTable *>(block + 0x134);
    out += " | ids " + std::to_string(ids.bucket_count()) + ":";
    for (auto &entry : ids) out += " " + std::to_string(entry.first) + "->" + VectorName(block, world, entry.second);
    for (size_t i = 0; i < world.own.size(); ++i) out += " own" + std::to_string(i) + Values(*world.own[i]);
    auto &byGroup = *reinterpret_cast<GroupTable *>(block + 0x114);
    out += " | groups " + std::to_string(byGroup.bucket_count()) + ":";
    for (auto &entry : byGroup) {
        out += " " + std::to_string(entry.first) + ":[";
        for (void **p = entry.second.first; p != entry.second.last; ++p) out += std::to_string(static_cast<Obj *>(*p)->id) + ",";
        out += "]";
    }
    out += " | refs";
    for (auto &s : world.segments) out += " " + std::to_string(s->refs);
    return out;
}

void Teardown(uint8_t *block)
{
    reinterpret_cast<PathTable *>(block + 0x154)->~PathTable();
    reinterpret_cast<IdTable *>(block + 0x134)->~IdTable();
    reinterpret_cast<GroupTable *>(block + 0x114)->~GroupTable();
}

void Report(difftest::Context &ctx, uint32_t seed, const std::string (&results)[2])
{
    if (results[0] == results[1]) return;
    size_t at = 0;
    while (at < results[0].size() && at < results[1].size() && results[0][at] == results[1][at]) ++at;
    const size_t from = at > 80 ? at - 80 : 0;
    ctx.Fail("seed %u: differ at %zu: orig ...%s / ours ...%s", seed, at, results[0].substr(from, 200).c_str(),
        results[1].substr(from, 200).c_str());
}

// Installs the vtables and the stand-ins shared by the three tests.
struct Setup {
    shape::CodePatch newPatch{0x824632u, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch deletePatch{0x824624u, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn stat{0x1c2068u, &PathFileTime, &StatStand};
    shape::StandIn key{0x1e6e80, &cTSPersistSystem::MakePathKey, &KeyStand};
    shape::StandIn freeBlock{0x195720, &ServiceBlockVector::FreeBlock, &FreeStand};
    Setup()
    {
        gSegVTable[0] = reinterpret_cast<void *>(&SegQuery);
        gSegVTable[1] = gFaceVTable[1] = reinterpret_cast<void *>(&ObjAddRef);
        gSegVTable[2] = gFaceVTable[2] = reinterpret_cast<void *>(&ObjRelease);
        gSegVTable[11] = reinterpret_cast<void *>(&SegPath);
        gSegVTable[15] = reinterpret_cast<void *>(&SegId);
        gSegVTable[19] = reinterpret_cast<void *>(&SegKeys);
        gSegVTable[7] = reinterpret_cast<void *>(&SegOpen);
        gFaceVTable[4] = reinterpret_cast<void *>(&FaceValue);
    }
};

} // namespace groups

DIFF_TEST(TSPersistSystemIP_PathStamp, 0x1e8ef0)
{
    using namespace groups;
    Setup setup;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, const cRZString *)>(0x1e8ef0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t zero = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed * 2654435761u + 7);
        cRZString path;
        path.Construct();
        path.mString = Variant(rng, kBases[rng() % kBaseCount]) + kTails[rng() % kTailCount];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            const uint32_t r = side == 0 ? original(system, &path) : system->PathStamp(&path);
            results[side] = gCalls + "| " + std::to_string(r) + " | " + Describe(&path);
            if (side == 0 && r == 1) ++zero;
        }
        path.Destruct();
        ++ctx.cases;
        Report(ctx, seed, results);
    }
    std::printf("  PathStamp coverage: zero times %u\n", zero);
    if (zero < 60) ctx.Fail("coverage: zero %u", zero);
}

DIFF_TEST(TSPersistSystemIP_ResetPathEntry, 0x1e94e0)
{
    using namespace groups;
    Setup setup;
    adt::AllocatorFake alloc(ctx);
    auto original = ctx.Original<void *(__thiscall *)(void *, const cRZString *, uint32_t)>(0x1e94e0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t existing = 0, made = 0, rehashed = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::mt19937 rng(seed * 40503u + 11);
            std::memset(block, 0, sizeof(block));
            World world;
            Build(block, world, rng, alloc);
            cRZString path;
            path.Construct();
            path.mString = kPaths[rng() % kPathCount];
            const uint32_t value = rng() % 3 ? rng() : 0;
            auto &paths = *reinterpret_cast<PathTable *>(block + 0x154);
            // A quarter of the tables filled up to the load limit (8 entries in 8 buckets), so a new entry rehashes.
            for (uint32_t i = 0; seed % 4 == 0 && paths.size() < 8; ++i) {
                Key filler;
                filler.text.mString = "filler" + std::to_string(i);
                paths[filler].words[0] = i;
            }
            const size_t before = paths.size(), buckets = paths.bucket_count();
            void *ret = side == 0 ? original(system, &path, value) : system->ResetPathEntry(&path, value);
            results[side] = gCalls + "| ret " + VectorName(block, world, static_cast<std::vector<uint32_t> *>(ret)) + " " + Dump(block, world) +
                            " | path " + Describe(&path);
            if (side == 0) {
                if (paths.size() == before) ++existing;
                else ++made;
                if (paths.bucket_count() != buckets) ++rehashed;
            }
            path.Destruct();
            Teardown(block);
            alloc.Release();
        }
        ++ctx.cases;
        Report(ctx, seed, results);
    }
    std::printf("  ResetPathEntry coverage: existing %u, made %u, rehashed %u\n", existing, made, rehashed);
    if (existing < 200 || made < 300 || rehashed < 20) ctx.Fail("coverage: existing %u, made %u, rehashed %u", existing, made, rehashed);
}

DIFF_TEST(TSPersistSystemIP_IndexSegmentGroups, 0x1e9b50)
{
    using namespace groups;
    static_assert(offsetof(cTSPersistSystem, mOpeningContent) == 0x174, "+0x174");
    Setup setup;
    adt::AllocatorFake alloc(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x1e9b50);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t skipped = 0, newId = 0, knownId = 0, erased = 0, special = 0;
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::mt19937 rng(seed * 69069u + 5);
            std::memset(block, 0, sizeof(block));
            World world;
            Build(block, world, rng, alloc);
            system->mOpeningContent = static_cast<uint8_t>(rng() % 8 == 0 ? 1 + rng() % 255 : 0);
            // One to three calls, on seeded segments.
            const uint32_t calls = 1 + rng() % 3;
            const size_t segmentCount = world.segments.size() / 2;
            auto &ids = *reinterpret_cast<IdTable *>(block + 0x134);
            auto &byGroup = *reinterpret_cast<GroupTable *>(block + 0x114);
            for (uint32_t c = 0; c < calls; ++c) {
                Obj *segment = world.segments[2 * (rng() % segmentCount)].get();
                const bool known = ids.count(segment->segId) != 0;
                const size_t groupsBefore = byGroup.size();
                gCalls += "call" + std::to_string(segment->id) + " ";
                if (side == 0) original(system, segment);
                else system->IndexSegmentGroups(segment);
                if (side == 0) {
                    if (system->mOpeningContent) ++skipped;
                    else if (known) ++knownId;
                    else ++newId;
                    if (byGroup.size() < groupsBefore) ++erased;
                    for (size_t k = 1; k < segment->keys.size(); k += 2)
                        if (segment->keys[k] == 0x1c0532fau && !system->mOpeningContent) ++special;
                }
            }
            results[side] = gCalls + Dump(block, world);
            Teardown(block);
            alloc.Release();
        }
        ++ctx.cases;
        Report(ctx, seed, results);
    }
    std::printf("  IndexSegmentGroups coverage: skipped %u, new ids %u, known ids %u, erasing calls %u, 0x1c0532fa keys %u\n", skipped,
        newId, knownId, erased, special);
    if (skipped < 150 || newId < 1000 || knownId < 400 || erased < 600 || special < 500)
        ctx.Fail("coverage: skipped %u, new %u, known %u, erased %u, special %u", skipped, newId, knownId, erased, special);
}

DIFF_TEST(TSPersistSystemIP_SegmentGroups, 0x1e97b0)
{
    using namespace groups;
    Setup setup;
    adt::AllocatorFake alloc(ctx);
    auto original = ctx.Original<void *(__thiscall *)(void *, void *)>(0x1e97b0);
    auto originalIndex = ctx.Original<void(__thiscall *)(void *, void *)>(0x1e9b50);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t current = 0, listed = 0, unlisted = 0, none = 0;
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::mt19937 rng(seed * 22695477u + 9);
            std::memset(block, 0, sizeof(block));
            World world;
            Build(block, world, rng, alloc);
            // One to three calls on seeded segments, some after IndexSegmentGroups on the same segment (as AddSegment
            // and OpenContentFolder call them).
            const uint32_t calls = 1 + rng() % 3;
            const size_t segmentCount = world.segments.size() / 2;
            for (uint32_t c = 0; c < calls; ++c) {
                Obj *segment = world.segments[2 * (rng() % segmentCount)].get();
                if (rng() % 3 == 0) {
                    gCalls += "index" + std::to_string(segment->id) + " ";
                    if (side == 0) originalIndex(system, segment);
                    else system->IndexSegmentGroups(segment);
                }
                gCalls += "call" + std::to_string(segment->id) + " ";
                const size_t at = gCalls.size();
                auto *ret = static_cast<std::vector<uint32_t> *>(side == 0 ? original(system, segment) : system->SegmentGroups(segment));
                const std::string tail = gCalls.substr(at);
                gCalls += "ret " + (ret ? VectorName(block, world, ret) + Values(*ret) : std::string("null")) + " ";
                if (side == 0) {
                    if (!ret) ++none;
                    else if (tail.find("keys") == std::string::npos) ++current;
                    else if (segment->keysAnswer) ++listed;
                    else ++unlisted;
                }
            }
            results[side] = gCalls + Dump(block, world);
            Teardown(block);
            alloc.Release();
        }
        ++ctx.cases;
        Report(ctx, seed, results);
    }
    std::printf("  SegmentGroups coverage: current %u, listed %u, unlisted %u, none %u\n", current, listed, unlisted, none);
    if (current < 300 || listed < 1500 || unlisted < 150 || none < 150)
        ctx.Fail("coverage: current %u, listed %u, unlisted %u, none %u", current, listed, unlisted, none);
}


// ParkOpenSegments 0x1ea760 and RelistParkedSegments 0x1e9eb0: the world above, plus the segment vector (+0xd4: 0..5
// counted segments, maybe with room; growth through ServiceRefVector::InsertGrown, 0x1d57e0 and ours, a stand-in on
// both sides that logs and grows the vector itself), mNamedMap (+0xf4: 0..2 entries put in through the image's emplace
// 0x1d84b0, which both sides call; the test's type only walks and destroys it), mMap178 (+0x178: 0..4 entries keyed as
// the MakePathKey stand-in keys the paths, or not, holding counted segments that may or may not be in the vector) and
// mMap198 (+0x198: 0..2 entries), and the ITS face (+0xa8) whose slot 31 logs. cRZFileName::NormaliseSeparators and
// cRZString LowerCase run for real (verified). Compared as above, with the vector, mNamedMap in list order and both kept
// tables.
namespace park {

using groups::Obj;
using groups::Key;
using KeptTable = std::unordered_map<Key, Obj *, groups::KeyHash, groups::KeyEqual>;
using NamedTable = std::unordered_multimap<Key, Obj *, groups::KeyHash, groups::KeyEqual>;
static_assert(sizeof(KeptTable) == 0x20 && sizeof(NamedTable) == 0x20, "table layout");
static_assert(sizeof(std::pair<const Key, Obj *>) + 8 == 0x2c, "kept node");

struct Vec {
    Obj **first, **last, **end;
};
struct NamedValue {
    cRZString path;
    Obj *segment;
};

adt::AllocatorFake *gAlloc;
Obj **__fastcall VecGrowStand(Vec *self, void *, Obj **where, Obj *const *value)
{
    const size_t size = self->last - self->first, capacity = self->end - self->first, at = where - self->first;
    groups::gCalls += "grow(" + std::to_string(size) + "/" + std::to_string(capacity) + "@" + std::to_string(at) + "," +
                      std::to_string((*value)->id) + ") ";
    const size_t grown = capacity + capacity / 2 > capacity ? capacity + capacity / 2 : capacity + 1;
    auto **block = static_cast<Obj **>(::operator new(grown * 4));
    gAlloc->Track(block);
    for (size_t i = 0; i < at; ++i) block[i] = self->first[i];
    block[at] = *value;
    groups::ObjAddRef(*value, nullptr);
    for (size_t i = at; i < size; ++i) block[i + 1] = self->first[i];
    self->first = block;
    self->last = block + size + 1;
    self->end = block + grown;
    return block + at;
}

uint32_t __fastcall ItsPath(void *, void *, Obj *segment, cRZString *path)
{
    groups::gCalls += "its31(" + std::to_string(segment->id) + ",[" + path->mString + "]) ";
    return 0x4401;
}
void *gItsVTable[40];

using Emplace = void *(__thiscall *)(void *, void **, NamedValue *);

std::string Lower(const std::string &text)
{
    std::string out;
    for (char c : text) out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return out;
}

void Build(uint8_t *block, groups::World &world, std::mt19937 &rng, adt::AllocatorFake &alloc, Emplace emplace)
{
    *reinterpret_cast<void **>(block + 0xa8) = gItsVTable;
    const size_t segmentCount = world.segments.size() / 2;
    auto segment = [&]() { return world.segments[2 * (rng() % segmentCount)].get(); };
    auto *vec = reinterpret_cast<Vec *>(block + 0xd4);
    const uint32_t listed = rng() % 6, room = rng() % 2;
    if (listed + room) {
        vec->first = static_cast<Obj **>(::operator new((listed + room) * 4));
        alloc.Track(vec->first);
        vec->last = vec->first;
        vec->end = vec->first + listed + room;
        for (uint32_t i = 0; i < listed; ++i) {
            Obj *s = segment();
            ++s->refs;
            *vec->last++ = s;
        }
    }
    new (block + 0xf4) NamedTable();
    const uint32_t named = rng() % 3;
    for (uint32_t i = 0; i < named; ++i) {
        NamedValue value;
        value.path.Construct();
        value.path.mString = "named" + std::to_string(rng() % 3);
        value.segment = segment();
        void *inserted = nullptr;
        emplace(block + 0xf4, &inserted, &value);
        value.path.Destruct();
    }
    auto *kept = new (block + 0x178) KeptTable();
    const uint32_t keptCount = rng() % 5;
    for (uint32_t i = 0; i < keptCount; ++i) {
        Key key;
        key.text.mString = rng() % 4 ? "k:" + Lower(groups::kPaths[rng() % groups::kPathCount]) : "kept" + std::to_string(rng() % 3);
        Obj *&slot = (*kept)[key];
        if (slot) --slot->refs;
        slot = segment();
        ++slot->refs;
    }
    auto *other = new (block + 0x198) KeptTable();
    const uint32_t otherCount = rng() % 3;
    for (uint32_t i = 0; i < otherCount; ++i) {
        Key key;
        key.text.mString = "m198_" + std::to_string(i);
        Obj *s = segment();
        ++s->refs;
        (*other)[key] = s;
    }
}

std::string Dump(uint8_t *block, const groups::World &world)
{
    std::string out = groups::Dump(block, world);
    auto *vec = reinterpret_cast<Vec *>(block + 0xd4);
    out += " | vector:";
    for (Obj **p = vec->first; p != vec->last; ++p) out += " " + std::to_string((*p)->id);
    out += " cap" + std::to_string(vec->end - vec->first);
    auto &named = *reinterpret_cast<NamedTable *>(block + 0xf4);
    out += " | named " + std::to_string(named.size()) + ":";
    for (auto &entry : named) out += " " + entry.first.text.mString + "/" + std::to_string(entry.first.text.mRefCount) + "=" + std::to_string(entry.second->id);
    for (uint32_t offset : {0x178u, 0x198u}) {
        auto &kept = *reinterpret_cast<KeptTable *>(block + offset);
        out += " | kept " + std::to_string(kept.size()) + "/" + std::to_string(kept.bucket_count()) + ":";
        for (auto &entry : kept)
            out += " " + entry.first.text.mString + "/" + std::to_string(entry.first.text.mRefCount) + "=" +
                   (entry.second ? std::to_string(entry.second->id) : std::string("null"));
    }
    return out;
}

void Teardown(uint8_t *block)
{
    reinterpret_cast<NamedTable *>(block + 0xf4)->~NamedTable();
    reinterpret_cast<KeptTable *>(block + 0x178)->~KeptTable();
    reinterpret_cast<KeptTable *>(block + 0x198)->~KeptTable();
    groups::Teardown(block);
}

} // namespace park

DIFF_TEST(TSPersistSystemIP_ParkOpenSegments, 0x1ea760)
{
    using namespace groups;
    static_assert(offsetof(cTSPersistSystem, mSegmentsBegin) == 0xd4 && offsetof(cTSPersistSystem, mNamedMap) == 0xf4 &&
                      offsetof(cTSPersistSystem, mMap178) == 0x178 && offsetof(cTSPersistSystem, mMap198) == 0x198,
        "fields");
    Setup setup;
    adt::AllocatorFake alloc(ctx);
    park::gAlloc = &alloc;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x1ea760);
    auto emplace = ctx.Original<park::Emplace>(0x1d84b0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t parked = 0, replaced = 0, named = 0, cleared = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::mt19937 rng(seed * 1103515245u + 17);
            std::memset(block, 0, sizeof(block));
            World world;
            groups::Build(block, world, rng, alloc);
            park::Build(block, world, rng, alloc, emplace);
            const size_t listed = system->mSegmentsEnd - system->mSegmentsBegin;
            const size_t keptBefore = reinterpret_cast<park::KeptTable *>(block + 0x178)->size();
            const size_t namedBefore = reinterpret_cast<park::NamedTable *>(block + 0xf4)->size();
            const size_t groupsBefore = reinterpret_cast<GroupTable *>(block + 0x114)->size();
            if (side == 0) original(system);
            else system->ParkOpenSegments();
            results[side] = gCalls + park::Dump(block, world);
            if (side == 0) {
                parked += static_cast<uint32_t>(listed);
                if (gCalls.find("-") != std::string::npos && keptBefore) ++replaced;
                if (namedBefore) ++named;
                if (groupsBefore) ++cleared;
            }
            park::Teardown(block);
            alloc.Release();
        }
        ++ctx.cases;
        Report(ctx, seed, results);
    }
    std::printf("  ParkOpenSegments coverage: parked %u, released %u, named %u, groups cleared %u\n", parked, replaced, named, cleared);
    if (parked < 2000 || replaced < 300 || named < 600 || cleared < 800)
        ctx.Fail("coverage: parked %u, released %u, named %u, cleared %u", parked, replaced, named, cleared);
}

DIFF_TEST(TSPersistSystemIP_RelistParkedSegments, 0x1e9eb0)
{
    using namespace groups;
    Setup setup;
    adt::AllocatorFake alloc(ctx);
    park::gAlloc = &alloc;
    shape::StandIn grow(0x1d57e0, &ServiceRefVector::InsertGrown, &park::VecGrowStand);
    park::gItsVTable[31] = reinterpret_cast<void *>(&park::ItsPath);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x1e9eb0);
    auto emplace = ctx.Original<park::Emplace>(0x1d84b0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t relisted = 0, skipped = 0, grown = 0, empty = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::mt19937 rng(seed * 134775813u + 1);
            std::memset(block, 0, sizeof(block));
            World world;
            groups::Build(block, world, rng, alloc);
            park::Build(block, world, rng, alloc, emplace);
            const size_t listed = system->mSegmentsEnd - system->mSegmentsBegin;
            const size_t kept = reinterpret_cast<park::KeptTable *>(block + 0x178)->size();
            if (side == 0) original(system);
            else system->RelistParkedSegments();
            results[side] = gCalls + park::Dump(block, world);
            if (side == 0) {
                const size_t added = (system->mSegmentsEnd - system->mSegmentsBegin) - listed;
                relisted += static_cast<uint32_t>(added);
                skipped += static_cast<uint32_t>(kept - added);
                if (gCalls.find("grow(") != std::string::npos) ++grown;
                if (!kept) ++empty;
            }
            park::Teardown(block);
            alloc.Release();
        }
        ++ctx.cases;
        Report(ctx, seed, results);
    }
    std::printf("  RelistParkedSegments coverage: relisted %u, skipped %u, grown %u, nothing kept %u\n", relisted, skipped, grown, empty);
    if (relisted < 500 || skipped < 800 || grown < 300 || empty < 150)
        ctx.Fail("coverage: relisted %u, skipped %u, grown %u, empty %u", relisted, skipped, grown, empty);
}


// LoadResourceNames 0x1e8fd0 and SaveResourceNames 0x1ea4f0. The world: the resource names (+0x1b8: segment id -> type ->
// (group, instance) -> counted cRZString, three std::maps of the game's layout) with 0..3 segments of 0..3 types of
// 0..4 names (real cRZStrings, AddRef'd by the table), a fake segment (slot 15 its id; slot 19 lists 0..4 name-map keys
// through the filter it is handed, logging the filter's vtable and fields, answering 0 for some with a block anyway;
// slots 22 / 23 open a record for reading / writing, failing for some, handing one out anyway for some), a fake record
// (QueryInterface 0xc56b8f08 failing for some; slot 6 logged) and a fake stream (GetUint32, slot 13, answers small
// seeded values so counts are 0..3 and (group, instance) pairs repeat; GetGZStr, slot 24, writes a seeded text;
// SetUint32 / SetGZStr, slots 37 / 48, log), and the ITS face (+0xa8) whose slot 45 logs. Strings and filters freed
// through the game's delete are logged. The game's cRZString code
// (vtable 0xd9b470) and the filter's (0xda5d08) run on both sides; FreeBlock is a stand-in on both. Compared: the call
// log, the result, the names in order with each string's text and count.
namespace names {

using groups::gCalls;
using groups::gSeed;
using groups::Mix;
using ByKey = std::map<std::pair<uint32_t, uint32_t>, cRZString *>;
using ByType = std::map<uint32_t, ByKey>;
using Names = std::map<uint32_t, ByType>;
static_assert(sizeof(Names) == 8, "names layout");

struct Obj {
    void **vtable;
    uint32_t id;
    int32_t refs;
};
uint32_t gStep;
uint32_t gSegId;
uint32_t gFilterVTable; // the image's 0xda5d08
uint32_t __fastcall AddRef(Obj *self, void *)
{
    gCalls += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall Release(Obj *self, void *)
{
    gCalls += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}
Obj gSegment, gRecord, gStream;
uint32_t Roll(uint32_t salt) { return Mix(gSeed, salt * 1000 + gStep++); }

uint32_t __fastcall SegId(Obj *, void *)
{
    gCalls += "id ";
    return gSegId;
}
uint32_t __fastcall SegKeys(Obj *, void *, ServiceBlockVector *keys, const uint32_t *filter)
{
    gCalls += "keys(" + std::to_string(filter[0] == gFilterVTable) +
              "," + std::to_string(filter[1]) + "," + std::to_string(filter[2]) + "," + std::to_string(filter[3]) + ") ";
    const uint32_t n = Roll(1) % 5;
    if (n) {
        auto *block = static_cast<uint32_t *>(::operator new(n * 16));
        for (uint32_t i = 0; i < n; ++i) {
            block[i * 4] = filter[2];
            block[i * 4 + 1] = filter[3];
            block[i * 4 + 2] = 0x100 + Roll(2) % 4;
            block[i * 4 + 3] = 0;
        }
        keys->mFirst = block;
        keys->mLast = keys->mEnd = block + n * 4;
    }
    return Roll(3) % 6 == 0 ? 0 : 0x3300 + n;
}
uint32_t OpenRecord(const uint32_t *key, Obj **record, const char *what)
{
    gCalls += std::string(what) + "(" + std::to_string(key[0]) + "," + std::to_string(key[1]) + "," + std::to_string(key[2]) + "," +
              std::to_string(key[3]) + ") ";
    const uint32_t r = Roll(4) % 9;
    if (r == 0) return 0xef00u;
    *record = &gRecord;
    ++gRecord.refs;
    return r == 1 ? 0xef00u : 0xef01u;
}
uint32_t __fastcall SegRead(Obj *, void *, const uint32_t *key, Obj **record, uint32_t mode)
{
    return OpenRecord(key, record, mode == 1 ? "read" : "read?");
}
uint32_t __fastcall SegWrite(Obj *, void *, const uint32_t *key, Obj **record) { return OpenRecord(key, record, "write"); }
uint32_t __fastcall RecordQuery(Obj *, void *, uint32_t iid, void **out)
{
    gCalls += "qi" + std::string(iid == 0xc56b8f08 ? "" : "?") + " ";
    if (Roll(5) % 10 == 0) return 0xab00u;
    *out = &gStream;
    ++gStream.refs;
    return 0xab01u;
}
uint32_t __fastcall RecordClose(Obj *, void *)
{
    gCalls += "close ";
    return 0xab00u | (Roll(6) & 1);
}
uint32_t __fastcall GetU32(Obj *, void *, uint32_t *value)
{
    *value = Roll(7) % 4;
    gCalls += "get" + std::to_string(*value) + " ";
    return 0xcd01u;
}
uint32_t __fastcall GetText(Obj *, void *, cRZString *text)
{
    gCalls += "gettext[" + text->mString + "] ";
    text->mString = "name" + std::to_string(Roll(8) % 50);
    return 0xcd01u;
}
uint32_t __fastcall SetU32(Obj *, void *, uint32_t value)
{
    gCalls += "set" + std::to_string(value) + " ";
    return 0xcd01u;
}
uint32_t __fastcall SetText(Obj *, void *, const cRZString *text)
{
    gCalls += "settext[" + text->mString + "] ";
    return 0xcd01u;
}
// The game's sized delete (0x824624), logging the strings (0x20) and key filters (0x10) it frees: the game's
// cRZString and filter Release (both sides) end there; map nodes (0x1c) are not logged, as ours are freed by our
// allocator directly.
void __cdecl LoggingDelete(void *p, size_t n)
{
    if (n == 0x20 || n == 0x10) gCalls += "del" + std::to_string(n) + " ";
    ::operator delete(p);
}
uint32_t __fastcall ItsRegister(void *, void *, Obj *segment)
{
    gCalls += "its45(" + std::to_string(segment->id) + ") ";
    return 0x7701;
}
void *gSegVTable[32], *gRecordVTable[16], *gStreamVTable[64], *gItsVTable[64];

void Install()
{
    gSegVTable[1] = gRecordVTable[1] = gStreamVTable[1] = reinterpret_cast<void *>(&AddRef);
    gSegVTable[2] = gRecordVTable[2] = gStreamVTable[2] = reinterpret_cast<void *>(&Release);
    gSegVTable[15] = reinterpret_cast<void *>(&SegId);
    gSegVTable[19] = reinterpret_cast<void *>(&SegKeys);
    gSegVTable[22] = reinterpret_cast<void *>(&SegRead);
    gSegVTable[23] = reinterpret_cast<void *>(&SegWrite);
    gRecordVTable[0] = reinterpret_cast<void *>(&RecordQuery);
    gRecordVTable[6] = reinterpret_cast<void *>(&RecordClose);
    gStreamVTable[13] = reinterpret_cast<void *>(&GetU32);
    gStreamVTable[24] = reinterpret_cast<void *>(&GetText);
    gStreamVTable[37] = reinterpret_cast<void *>(&SetU32);
    gStreamVTable[48] = reinterpret_cast<void *>(&SetText);
    gItsVTable[45] = reinterpret_cast<void *>(&ItsRegister);
}

cRZString *NewText(const std::string &text)
{
    auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    s->Construct();
    s->mString = text;
    s->mRefCount = 1;
    return s;
}

// The names table and the fakes for one side.
Names *Build(uint8_t *block, std::mt19937 &rng)
{
    *reinterpret_cast<void **>(block + 0xa8) = gItsVTable;
    gSegment = Obj{gSegVTable, 1, 1};
    gRecord = Obj{gRecordVTable, 2, 1};
    gStream = Obj{gStreamVTable, 3, 1};
    gSegId = 0x500 + rng() % 4;
    auto *names = new (block + 0x1b8) Names();
    const uint32_t segments = rng() % 4;
    for (uint32_t s = 0; s < segments; ++s) {
        ByType &types = (*names)[0x500 + rng() % 4];
        const uint32_t typeCount = rng() % 4;
        for (uint32_t t = 0; t < typeCount; ++t) {
            ByKey &byKey = types[0x100 + rng() % 4];
            const uint32_t count = rng() % 5;
            for (uint32_t n = 0; n < count; ++n) {
                cRZString *&slot = byKey[std::make_pair(rng() % 4, rng() % 4)];
                if (!slot) slot = NewText("old" + std::to_string(rng() % 50));
            }
        }
    }
    return names;
}

std::string Dump(const Names &names)
{
    std::string out = " | names:";
    for (auto &segment : names) {
        out += " [" + std::to_string(segment.first);
        for (auto &type : segment.second) {
            out += " " + std::to_string(type.first) + ":";
            for (auto &name : type.second)
                out += "(" + std::to_string(name.first.first) + "," + std::to_string(name.first.second) + ")=" + name.second->mString + "/" +
                       std::to_string(name.second->mRefCount);
        }
        out += "]";
    }
    out += " | refs " + std::to_string(gSegment.refs) + " " + std::to_string(gRecord.refs) + " " + std::to_string(gStream.refs);
    return out;
}

} // namespace names

DIFF_TEST(TSPersistSystemIP_LoadResourceNames, 0x1e8fd0)
{
    using namespace names;
    static_assert(offsetof(cTSPersistSystem, mTreeHead) == 0x1b8, "+0x1b8");
    groups::Setup setup;
    Install();
    shape::CodePatch loggingDelete(0x824624u, reinterpret_cast<const void *>(&LoggingDelete));
    gFilterVTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(0xda5d08)));
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x1e8fd0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t loaded = 0, failed = 0, unlisted = 0, replaced = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            gStep = 0;
            std::mt19937 rng(seed * 2891336453u + 3);
            std::memset(block, 0, sizeof(block));
            Names *table = Build(block, rng);
            const uint8_t r = side == 0 ? original(system, &gSegment) : system->LoadResourceNames(&gSegment);
            results[side] = gCalls + "| " + std::to_string(r) + Dump(*table);
            if (side == 0) {
                if (gCalls.find("keys(") != std::string::npos && gCalls.find("read(") == std::string::npos && !r) ++unlisted;
                else if (r) ++loaded;
                else ++failed;
                // Three names read or more: (group, instance) pairs repeat, so some replace an earlier one.
                size_t texts = 0;
                for (size_t at = 0; (at = gCalls.find("gettext", at)) != std::string::npos; ++at) ++texts;
                if (texts >= 3) ++replaced;
            }
            table->~Names();
        }
        ++ctx.cases;
        groups::Report(ctx, seed, results);
    }
    std::printf("  LoadResourceNames coverage: loaded %u, failed %u, unlisted %u, busy %u\n", loaded, failed, unlisted, replaced);
    if (loaded < 600 || failed < 300 || unlisted < 150 || replaced < 300)
        ctx.Fail("coverage: loaded %u, failed %u, unlisted %u, busy %u", loaded, failed, unlisted, replaced);
}

DIFF_TEST(TSPersistSystemIP_SaveResourceNames, 0x1ea4f0)
{
    using namespace names;
    groups::Setup setup;
    Install();
    shape::CodePatch loggingDelete(0x824624u, reinterpret_cast<const void *>(&LoggingDelete));
    gFilterVTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(0xda5d08)));
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x1ea4f0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t saved = 0, failed = 0, absent = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            gStep = 0;
            std::mt19937 rng(seed * 747796405u + 1);
            std::memset(block, 0, sizeof(block));
            Names *table = Build(block, rng);
            // Two thirds of the cases on a segment the table has names for.
            if (seed % 3 && !table->empty()) {
                auto it = table->begin();
                std::advance(it, rng() % table->size());
                gSegId = it->first;
            }
            const bool has = table->count(gSegId) != 0;
            // Through the ITS slot 56 thunk on our side some of the time (vf168_56 forwards to it).
            uint8_t r;
            if (side == 0) r = original(system, &gSegment);
            else if (seed % 2) r = system->SaveResourceNames(&gSegment);
            else r = system->cTSPersistSystem::vf168_56(&gSegment);
            results[side] = gCalls + "| " + std::to_string(r) + Dump(*table);
            if (side == 0) {
                if (!has) ++absent;
                else if (r) ++saved;
                else ++failed;
            }
            table->~Names();
        }
        ++ctx.cases;
        groups::Report(ctx, seed, results);
    }
    std::printf("  SaveResourceNames coverage: saved %u, failed %u, absent %u\n", saved, failed, absent);
    if (saved < 500 || failed < 150 || absent < 300) ctx.Fail("coverage: saved %u, failed %u, absent %u", saved, failed, absent);
}


// SaveGroupMap 0x1ea190. The world: mMap154 (+0x154) as above (0..5 path entries with words and 0..6 values, some
// tables filled further), the cTSAppCommonImpl fake of the MakePathKey test answering the user folder (or not, or
// without writing it), the framework global (0x10c9378) a fake whose slot 21 logs, the COM director's framework object
// (0x1ae18d and ours: GetDirectorFrameworkObject, a stand-in on both sides) a fake whose slot 3 makes a packed-file fake
// or fails, the packed file (slots 3, 5 Open, 9 Close, 12 SetPath, 22 OpenRecord logged; Open and OpenRecord failing
// for some, OpenRecord handing a record out anyway for some), its record (QueryInterface 0xc56b8f08 always answering:
// the original faults without a stream) and stream (slots 37 and 48 logged). The cRZFileName setters 0x1c8a65 and
// 0x1c8b5f (original on both sides) and GetFullPath (0x1c882a and ours) are logging stand-ins (the setters keep the texts,
// GetFullPath joins them). Compared: the call log (with the cTSAppCommonImpl fake's) and every count.
namespace savegroups {

using groups::gCalls;
using groups::gSeed;
using groups::Mix;

struct Obj {
    void **vtable;
    uint32_t id;
    int32_t refs;
};
uint32_t __fastcall AddRef(Obj *self, void *)
{
    gCalls += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall Release(Obj *self, void *)
{
    gCalls += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}
Obj gFramework, gDirector, gPacked, gRecord, gStream;
std::string gFolder, gName;

uint32_t __fastcall FrameworkCmdLine(Obj *, void *)
{
    gCalls += "cmdline ";
    return 0x1234;
}
void *__cdecl DirectorStand()
{
    gCalls += "director ";
    return &gDirector;
}
uint32_t __fastcall Create(Obj *, void *, uint32_t clsid, uint32_t iid, Obj **out)
{
    gCalls += "create(" + std::to_string(clsid) + "," + std::to_string(iid) + ") ";
    if (Mix(gSeed, 1) % 6 == 0) return 0xab00u;
    *out = &gPacked;
    ++gPacked.refs;
    return 0xab01u;
}
uint32_t __fastcall PackedSlot3(Obj *, void *)
{
    gCalls += "p3 ";
    return 0xab01u;
}
uint32_t __fastcall PackedOpen(Obj *, void *, uint32_t a, uint32_t b)
{
    gCalls += "open(" + std::to_string(a) + "," + std::to_string(b) + ") ";
    return 0xab00u | (Mix(gSeed, 2) % 6 != 0);
}
uint32_t __fastcall PackedClose(Obj *, void *)
{
    gCalls += "close ";
    return 0xab01u;
}
uint32_t __fastcall PackedPath(Obj *, void *, const cRZString *path)
{
    gCalls += "path[" + path->mString + "] ";
    return 0xab01u;
}
uint32_t __fastcall PackedRecord(Obj *, void *, const uint32_t *key, Obj **record, uint32_t mode)
{
    gCalls += "record(" + std::to_string(key[0]) + "," + std::to_string(key[1]) + "," + std::to_string(key[2]) + "," +
              std::to_string(key[3]) + ";" + std::to_string(mode) + ") ";
    const uint32_t r = Mix(gSeed, 3) % 7;
    if (r == 0) return 0xab00u;
    *record = &gRecord;
    ++gRecord.refs;
    return r == 1 ? 0xab00u : 0xab01u;
}
uint32_t __fastcall RecordQuery(Obj *, void *, uint32_t iid, void **out)
{
    gCalls += "qi" + std::string(iid == 0xc56b8f08 ? "" : "?") + " ";
    *out = &gStream;
    ++gStream.refs;
    return 0xab01u;
}
uint32_t __fastcall RecordClose(Obj *, void *)
{
    gCalls += "rclose ";
    return 0xab01u;
}
uint32_t __fastcall SetU32(Obj *, void *, uint32_t value)
{
    gCalls += "s" + std::to_string(value) + " ";
    return 0xcd01u;
}
uint32_t __fastcall SetText(Obj *, void *, const cRZString *text)
{
    gCalls += "t[" + text->mString + "/" + std::to_string(text->mRefCount) + "] ";
    return 0xcd01u;
}
void *__fastcall SetFolderStand(void *self, void *, const cRZString *folder, uint32_t flag)
{
    gCalls += "folder[" + folder->mString + "]" + std::to_string(flag) + " ";
    gFolder = folder->mString;
    return self;
}
void *__fastcall SetNameStand(void *self, void *, const cRZString *name, uint32_t flag)
{
    gCalls += "name[" + name->mString + "]" + std::to_string(flag) + " ";
    gName = name->mString;
    return self;
}
cRZString *__fastcall FullPathStand(void *, void *, cRZString *out)
{
    gCalls += "full ";
    out->Construct();
    out->mString = gFolder + "|" + gName;
    return out;
}
void *gFrameworkVTable[32], *gDirectorVTable[8], *gPackedVTable[32], *gRecordVTable[16], *gStreamVTable[64];

} // namespace savegroups

DIFF_TEST(TSPersistSystemIP_SaveGroupMap, 0x1ea190)
{
    using namespace savegroups;
    std::memset(gCommonVtable, 0, sizeof(gCommonVtable));
    gCommonVtable[4] = reinterpret_cast<void *>(&Slot4);
    gCommon.vtable = gCommonVtable;
    adt::GlobalOverride<void *> common(ctx, appinit::kCommonImplCacheRva, &gCommon);
    adt::GlobalOverride<void *> framework(ctx, 0x10c9378, &gFramework);
    groups::Setup setup;
    shape::StandIn director(0x1ae18d, &GetDirectorFrameworkObject, &DirectorStand);
    shape::CodePatch setFolder(0x1c8a65u, reinterpret_cast<const void *>(&SetFolderStand));
    shape::CodePatch setName(0x1c8b5fu, reinterpret_cast<const void *>(&SetNameStand));
    shape::StandIn fullPath(0x1c882a, &cRZFileName::GetFullPath, &FullPathStand);
    gFrameworkVTable[21] = reinterpret_cast<void *>(&FrameworkCmdLine);
    gDirectorVTable[3] = reinterpret_cast<void *>(&Create);
    for (void **vt : {gPackedVTable, gRecordVTable, gStreamVTable}) {
        vt[1] = reinterpret_cast<void *>(&AddRef);
        vt[2] = reinterpret_cast<void *>(&Release);
    }
    gPackedVTable[3] = reinterpret_cast<void *>(&PackedSlot3);
    gPackedVTable[5] = reinterpret_cast<void *>(&PackedOpen);
    gPackedVTable[9] = reinterpret_cast<void *>(&PackedClose);
    gPackedVTable[12] = reinterpret_cast<void *>(&PackedPath);
    gPackedVTable[22] = reinterpret_cast<void *>(&PackedRecord);
    gRecordVTable[0] = reinterpret_cast<void *>(&RecordQuery);
    gRecordVTable[6] = reinterpret_cast<void *>(&RecordClose);
    gStreamVTable[37] = reinterpret_cast<void *>(&savegroups::SetU32);
    gStreamVTable[48] = reinterpret_cast<void *>(&savegroups::SetText);
    adt::AllocatorFake alloc(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x1ea190);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    auto *system = reinterpret_cast<cTSPersistSystem *>(block);
    uint32_t written = 0, entries = 0, unopened = 0, uncreated = 0;
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls.clear();
            adt::ResetSide();
            std::mt19937 rng(seed * 2246822519u + 13);
            gUser = {rng() % 6 != 0, rng() % 8 != 0, Variant(rng, kBases[rng() % kBaseCount])};
            gFolder.clear();
            gName.clear();
            gLog.clear(); // the cTSAppCommonImpl fake's log
            gFramework = Obj{gFrameworkVTable, 1, 1};
            gDirector = Obj{gDirectorVTable, 2, 1};
            gPacked = Obj{gPackedVTable, 3, 1};
            gRecord = Obj{gRecordVTable, 4, 1};
            gStream = Obj{gStreamVTable, 5, 1};
            std::memset(block, 0, sizeof(block));
            groups::World world;
            groups::Build(block, world, rng, alloc);
            if (side == 0) original(system);
            else system->SaveGroupMap();
            results[side] = gLog + gCalls + "| refs " + std::to_string(gPacked.refs) + " " + std::to_string(gRecord.refs) + " " +
                            std::to_string(gStream.refs) + groups::Dump(block, world);
            if (side == 0) {
                if (gCalls.find("rclose") != std::string::npos) {
                    ++written;
                    entries += static_cast<uint32_t>(reinterpret_cast<groups::PathTable *>(block + 0x154)->size());
                } else if (gCalls.find("create") != std::string::npos && gCalls.find("open(") == std::string::npos) {
                    ++uncreated;
                } else {
                    ++unopened;
                }
            }
            groups::Teardown(block);
            alloc.Release();
        }
        ++ctx.cases;
        groups::Report(ctx, seed, results);
    }
    std::printf("  SaveGroupMap coverage: written %u (%u entries), not created %u, not opened %u\n", written, entries, uncreated, unopened);
    if (written < 600 || entries < 1000 || uncreated < 100 || unopened < 200)
        ctx.Fail("coverage: written %u, entries %u, not created %u, not opened %u", written, entries, uncreated, unopened);
}


// BackgroundOpenThread 0x1defe0. The world: a zeroed primary block whose ITS face (+0xa8) logs slots 43 and 44 (the
// text pointer compared with the image's), whose lock B (+0x68) logs Lock / Unlock, and whose base segment deque (+0x70)
// is a real std::deque (the game's layout) of 0..40 counted fake segments (repeats allowed, the front popped a few times
// so the offset moves) logging AddRef, Release and slot 18. The image's list code (0x1d4bb0) and operator new / delete
// (to ours) run on the original side. Compared: the result, the call log, every count.
namespace bgopen {

using groups::gCalls;
struct Obj {
    void **vtable;
    uint32_t id;
    int32_t refs;
};
uint32_t __fastcall AddRef(Obj *self, void *)
{
    gCalls += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall Release(Obj *self, void *)
{
    gCalls += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}
uint32_t __fastcall Slot18(Obj *self, void *, uint32_t arg)
{
    gCalls += "s18(" + std::to_string(self->id) + "," + std::to_string(arg) + ") ";
    return 0xab01u;
}
uintptr_t gTexts[3];
uint32_t __fastcall Its44(void *, void *, uint32_t mask, const char *text)
{
    int which = -1;
    for (int i = 0; i < 3; ++i)
        if (reinterpret_cast<uintptr_t>(text) == gTexts[i]) which = i;
    gCalls += "its44(" + std::to_string(mask) + ",#" + std::to_string(which) + ") ";
    return 0x4401;
}
uint32_t __fastcall Its43(void *, void *, uint32_t mask)
{
    gCalls += "its43(" + std::to_string(mask) + ") ";
    return 0x4301;
}
uint32_t __fastcall Lock(void *, void *)
{
    gCalls += "lock ";
    return 1;
}
uint32_t __fastcall Unlock(void *, void *)
{
    gCalls += "unlock ";
    return 1;
}
void *gSegVTable[24], *gItsVTable[64], *gLockVTable[8];

} // namespace bgopen

DIFF_TEST(TSPersistSystemIP_BackgroundOpenThread, 0x1defe0)
{
    using namespace bgopen;
    gSegVTable[1] = reinterpret_cast<void *>(&bgopen::AddRef);
    gSegVTable[2] = reinterpret_cast<void *>(&bgopen::Release);
    gSegVTable[18] = reinterpret_cast<void *>(&Slot18);
    gItsVTable[43] = reinterpret_cast<void *>(&Its43);
    gItsVTable[44] = reinterpret_cast<void *>(&Its44);
    gLockVTable[1] = reinterpret_cast<void *>(&Lock);
    gLockVTable[2] = reinterpret_cast<void *>(&Unlock);
    gTexts[0] = reinterpret_cast<uintptr_t>(ctx.image.At(0xda66b4));
    gTexts[1] = reinterpret_cast<uintptr_t>(ctx.image.At(0xda66c0));
    gTexts[2] = reinterpret_cast<uintptr_t>(ctx.image.At(0xda66cc));
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&groups::GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&groups::GameDelete));
    auto original = ctx.Original<uint32_t(__stdcall *)(void *)>(0x1defe0);
    alignas(16) static uint8_t block[sizeof(cTSPersistSystem)];
    using Deque = std::deque<void *>;
    static_assert(sizeof(Deque) == 0x14, "deque layout");
    uint32_t walked = 0, empty = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gCalls.clear();
            std::mt19937 rng(seed * 1664525u + 1013904223u);
            std::memset(block, 0, sizeof(block));
            *reinterpret_cast<void **>(block + 0xa8) = gItsVTable;
            *reinterpret_cast<void **>(block + 0x68) = gLockVTable;
            auto *deque = new (block + 0x70) Deque();
            std::vector<std::unique_ptr<Obj>> segments;
            const uint32_t count = seed % 7 == 0 ? 0 : 1 + rng() % 12;
            for (uint32_t i = 0; i < count; ++i) segments.emplace_back(new Obj{gSegVTable, i, 1});
            const uint32_t pushes = count ? rng() % 41 : 0;
            for (uint32_t i = 0; i < pushes; ++i) deque->push_back(segments[rng() % count].get());
            const uint32_t pops = pushes ? rng() % (pushes / 2 + 1) : 0;
            for (uint32_t i = 0; i < pops; ++i) deque->pop_front();
            const uint32_t r = side == 0 ? original(block) : cTSPersistSystem::BackgroundOpenThread(block);
            results[side] = gCalls + "| " + std::to_string(r) + " | refs";
            for (auto &s : segments) results[side] += " " + std::to_string(s->refs);
            if (side == 0) {
                walked += static_cast<uint32_t>(deque->size());
                if (deque->empty()) ++empty;
            }
            deque->~Deque();
        }
        ++ctx.cases;
        groups::Report(ctx, seed, results);
    }
    std::printf("  BackgroundOpenThread coverage: segments walked %u, empty deques %u\n", walked, empty);
    if (walked < 3000 || empty < 80) ctx.Fail("coverage: walked %u, empty %u", walked, empty);
}
