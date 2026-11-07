// Differential tests for ChangeRecordSet (src/ts/ChangeRecordSet.*): one group's cached resources in the persist system's
// change records. Constructor 0x1f5850, Erase 0x1f6410, Take 0x1f6780, Put 0x1f6de0, Read 0x1f6bd0, Write 0x1f6e90.
//
// Each side builds the same seeded set (0..24 entries; keys from a small pool whose type + group + instance sums
// collide, so buckets hold several nodes; a few entries without an object) and runs the original or ours on it. The cached
// objects are fakes logging AddRef / Release (never freed during a side); the streams are fakes logging every read and
// write, answering from a seeded script that can fail at any step. Put copies a real cRZVariant source (the image's
// copy constructor 0x9c7a40 on the original side, our cRZVariant on ours; the copy's bytes are compared, its vtable word
// checked against the side's own). The game's operator new / sized delete go to ours. Compared: the call log, the result,
// the table in list order (key, change id, object), its size and bucket count, the changed flag, every object's count.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZVariant.h"
#include "ts/ChangeRecordSet.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

std::string gLog;
uint32_t gSeed;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

// A cached object (0x14 bytes, the size Put allocates for its copy).
struct Obj {
    void **vtable;
    uint32_t id;
    int32_t refs;
    uint32_t pad[2];
};
static_assert(sizeof(Obj) == 0x14, "fake object size");
uint32_t __fastcall ObjAddRef(Obj *self, void *)
{
    gLog += "+" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall ObjRelease(Obj *self, void *)
{
    gLog += "-" + std::to_string(self->id) + " ";
    return static_cast<uint32_t>(--self->refs);
}
void *gObjVTable[4] = {nullptr, reinterpret_cast<void *>(&ObjAddRef), reinterpret_cast<void *>(&ObjRelease)};

std::vector<Obj *> gMade; // objects made by a side (variant copies, read objects): freed after the side
uint32_t gNextId;
Obj *Make(void *at = nullptr)
{
    Obj *obj = at ? static_cast<Obj *>(at) : static_cast<Obj *>(::operator new(sizeof(Obj)));
    *obj = Obj{gObjVTable, gNextId++, 0, {0, 0}};
    gMade.push_back(obj);
    return obj;
}

// The stream: a script of answers; step `failAt` fails.
struct Stream {
    void **vtable;
    std::vector<uint32_t> words; // GetUint32 answers, in order
    std::vector<ChangeRecordKey> keys;
    uint32_t wordAt = 0, keyAt = 0, steps = 0, failAt = ~0u;
    bool Step() { return steps++ != failAt; }
};
std::string KeyText(const ChangeRecordKey &k)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%x.%x.%x.%x", k.words[0], k.words[1], k.words[2], k.words[3]);
    return buf;
}
uint32_t Answer(bool ok)
{
    return 0xabcd1200u | (ok ? 1u : 0u); // garbage above al
}
uint32_t __fastcall GetU32(Stream *self, void *, uint32_t *out)
{
    gLog += "get32(" + std::to_string(*out) + ") ";
    if (!self->Step()) return Answer(false);
    *out = self->wordAt < self->words.size() ? self->words[self->wordAt++] : 0x77;
    return Answer(true);
}
uint32_t __fastcall GetKey(Stream *self, void *, ChangeRecordKey *out)
{
    gLog += "getkey(" + KeyText(*out) + ") ";
    if (!self->Step()) return Answer(false);
    *out = self->keyAt < self->keys.size() ? self->keys[self->keyAt++] : ChangeRecordKey{{9, 9, 9, 9}};
    return Answer(true);
}
uint32_t __fastcall GetObj(Stream *self, void *, void **out)
{
    gLog += "getobj(" + std::string(*out ? "set" : "null") + ") ";
    if (!self->Step()) return Answer(false);
    if (Mix(gSeed, self->steps) % 5 == 0) {
        *out = nullptr;
    } else {
        Obj *obj = Make();
        obj->refs = 1;
        *out = obj;
    }
    return Answer(true);
}
uint32_t __fastcall SetU32(Stream *self, void *, uint32_t value)
{
    gLog += "set32(" + std::to_string(value) + ") ";
    return Answer(self->Step());
}
uint32_t __fastcall SetKey(Stream *self, void *, const ChangeRecordKey *key)
{
    gLog += "setkey(" + KeyText(*key) + ") ";
    return Answer(self->Step());
}
uint32_t __fastcall SetObj(Stream *self, void *, const Obj *object)
{
    gLog += "setobj(" + std::string(object ? std::to_string(object->id) : "null") + ") ";
    return Answer(self->Step());
}
void *gStreamVTable[64];

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

ChangeRecordKey PoolKey(uint32_t r)
{
    // Sums collide: (1, 2, 3) and (3, 2, 1) and (0, 6, 0) share a hash; the fourth word only tells keys apart.
    static const uint32_t kTriples[][3] = {{1, 2, 3}, {3, 2, 1}, {0, 6, 0}, {0x10, 0, 0}, {7, 7, 2}, {0xad1a6720, 0, 5}, {16, 0, 0}};
    const auto &t = kTriples[r % 7];
    return ChangeRecordKey{{t[0], t[1], t[2], (r / 7) % 3}};
}

std::vector<std::unique_ptr<Obj>> gOwned; // the pre-existing objects of a side

// The seeded set, built through our class on raw memory.
ChangeRecordSet *Build(void *memory, std::mt19937 &rng)
{
    auto *set = new (memory) ChangeRecordSet();
    const uint32_t count = rng() % 25;
    for (uint32_t i = 0; i < count; ++i) {
        ChangeRecordEntry &entry = set->mRecords[PoolKey(rng())];
        entry.changeId = rng() % 4;
        if (!entry.record.object && rng() % 6) {
            gOwned.emplace_back(new Obj{gObjVTable, 100 + i, 1, {0, 0}});
            entry.record.object = gOwned.back().get();
        }
    }
    set->mChanged = static_cast<uint8_t>(rng() % 2);
    return set;
}

std::string Describe(ChangeRecordSet *set)
{
    std::string out = "size " + std::to_string(set->mRecords.size()) + " buckets " + std::to_string(set->mRecords.bucket_count()) +
                      " changed " + std::to_string(set->mChanged) + " |";
    for (const auto &entry : set->mRecords) {
        const auto *obj = static_cast<const Obj *>(entry.second.record.object);
        out += " " + KeyText(entry.first) + "=" + std::to_string(entry.second.changeId) + "/" + (obj ? std::to_string(obj->id) : "null");
    }
    out += " | refs";
    for (auto &obj : gOwned) out += " " + std::to_string(obj->refs);
    for (Obj *obj : gMade) out += " m" + std::to_string(obj->refs);
    return out;
}

// Runs `body` on both sides over the seeded world; body(side, set, rng) returns the result's text.
template <typename Body> void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Body body)
{
    alignas(16) static uint8_t memory[sizeof(ChangeRecordSet)];
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gLog.clear();
            gNextId = 1000;
            std::mt19937 rng(seed * 2654435761u + 17);
            ChangeRecordSet *set = Build(memory, rng);
            const std::string result = body(side, set, rng);
            results[side] = gLog + "| " + result + " | " + Describe(set);
            set->~ChangeRecordSet();
            for (Obj *obj : gMade) ::operator delete(obj);
            gMade.clear();
            gOwned.clear();
        }
        ++ctx.cases;
        if (results[0] != results[1]) {
            size_t at = 0;
            while (at < results[0].size() && at < results[1].size() && results[0][at] == results[1][at]) ++at;
            const size_t from = at > 80 ? at - 80 : 0;
            ctx.Fail("%s seed %u: differ at %zu: orig ...%s / ours ...%s", name, seed, at, results[0].substr(from, 180).c_str(),
                results[1].substr(from, 180).c_str());
        }
    }
}

void Install()
{
    gStreamVTable[13] = reinterpret_cast<void *>(&GetU32);
    gStreamVTable[25] = reinterpret_cast<void *>(&GetKey);
    gStreamVTable[26] = reinterpret_cast<void *>(&GetObj);
    gStreamVTable[37] = reinterpret_cast<void *>(&SetU32);
    gStreamVTable[49] = reinterpret_cast<void *>(&SetKey);
    gStreamVTable[50] = reinterpret_cast<void *>(&SetObj);
}

} // namespace

DIFF_TEST(ChangeRecordSet_Construct, 0x1f5850)
{
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x1f5850);
    alignas(16) static uint8_t memory[2][sizeof(ChangeRecordSet)];
    for (uint32_t seed = 0; seed < 50; ++seed) {
        std::string out[2];
        for (int side = 0; side < 2; ++side) {
            std::memset(memory[side], 0xcc, sizeof(memory[side]));
            void *ret = side == 0 ? original(memory[side]) : ChangeRecordSetAdapters::Construct(reinterpret_cast<ChangeRecordSet *>(memory[side]), nullptr);
            auto *set = reinterpret_cast<ChangeRecordSet *>(memory[side]);
            const auto *words = reinterpret_cast<const uint32_t *>(memory[side]);
            const auto *head = reinterpret_cast<void *const *>(words[1]);
            const auto *buckets = reinterpret_cast<void *const *>(words[3]);
            const size_t bucketWords = (words[4] - words[3]) / 4, capacity = (words[5] - words[3]) / 4;
            out[side] = std::string(ret == memory[side] ? "this" : "other") + " lf " + std::to_string(words[0]) + " size " +
                        std::to_string(words[2]) + " head self " + std::to_string(head[0] == head && head[1] == head) + " buckets " +
                        std::to_string(bucketWords) + "/" + std::to_string(capacity) + " mask " + std::to_string(words[6]) + " max " +
                        std::to_string(words[7]) + " changed " + std::to_string(set->mChanged) + " pad " + std::to_string(memory[side][0x21]);
            bool allHead = true;
            for (size_t i = 0; i < bucketWords; ++i) allHead &= buckets[i] == head;
            out[side] += allHead ? " all-head" : " stray";
            // Use it: a few entries, so a table built by either side behaves alike.
            for (uint32_t i = 0; i < 20; ++i) set->mRecords[PoolKey(Mix(seed, i))].changeId = i;
            out[side] += " then " + std::to_string(set->mRecords.size()) + "/" + std::to_string(set->mRecords.bucket_count());
            set->~ChangeRecordSet();
        }
        ++ctx.cases;
        if (out[0] != out[1]) ctx.Fail("seed %u: orig %s / ours %s", seed, out[0].c_str(), out[1].c_str());
    }
}

DIFF_TEST(ChangeRecordSet_Erase, 0x1f6410)
{
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    auto original = ctx.Original<void(__thiscall *)(void *, const ChangeRecordKey *)>(0x1f6410);
    Compare(ctx, "Erase", 2000, [&](int side, ChangeRecordSet *set, std::mt19937 &rng) {
        const ChangeRecordKey key = PoolKey(rng());
        if (side == 0) original(set, &key);
        else set->Erase(&key);
        return std::string("erase ") + KeyText(key);
    });
}

DIFF_TEST(ChangeRecordSet_Take, 0x1f6780)
{
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, const ChangeRecordKey *, uint32_t, void **)>(0x1f6780);
    uint32_t hits = 0;
    Compare(ctx, "Take", 2000, [&](int side, ChangeRecordSet *set, std::mt19937 &rng) {
        const ChangeRecordKey key = PoolKey(rng());
        const uint32_t changeId = rng() % 4;
        // Take hands out the object without a null check: keep entries with a matching id holding one.
        auto it = set->mRecords.find(key);
        if (it != set->mRecords.end() && it->second.changeId == changeId && !it->second.record.object) it->second.changeId ^= 8;
        void *out = reinterpret_cast<void *>(0x1234);
        const uint32_t r = side == 0 ? original(set, &key, changeId, &out) & 0xff : set->Take(&key, changeId, &out);
        if (side == 0 && r) ++hits;
        const Obj *obj = out == reinterpret_cast<void *>(0x1234) ? nullptr : static_cast<const Obj *>(out);
        return "take " + KeyText(key) + "@" + std::to_string(changeId) + " -> " + std::to_string(r) + " out " +
               (obj ? std::to_string(obj->id) : "untouched");
    });
    if (hits < 200) ctx.Fail("coverage: %u hits", hits);
}

DIFF_TEST(ChangeRecordSet_Put, 0x1f6de0)
{
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    auto original = ctx.Original<void(__thiscall *)(void *, const ChangeRecordKey *, uint32_t, const void *)>(0x1f6de0);
    // The copy is a real cRZVariant on both sides (since lane LQ ours builds our class; the original runs the image's
    // constructor 0x9c7a40, which copies through the source's slot 66). The source is a real variant of a seeded kind.
    static const void *const ourVariantVTable = [] {
        cRZVariant probe;
        return *reinterpret_cast<const void *const *>(&probe);
    }();
    Compare(ctx, "Put", 2000, [&](int side, ChangeRecordSet *set, std::mt19937 &rng) {
        const ChangeRecordKey key = PoolKey(rng());
        const uint32_t changeId = rng();
        const uint32_t pick = rng() % 4, value = rng();
        cRZVariant source = pick == 0 ? cRZVariant(value) : pick == 1 ? cRZVariant(static_cast<float>(value % 1000))
                          : pick == 2 ? cRZVariant(static_cast<uint16_t>(value)) : cRZVariant();
        if (side == 0) original(set, &key, changeId, static_cast<cIGZVariant *>(&source));
        else set->Put(&key, changeId, static_cast<cIGZVariant *>(&source));
        const auto *copy = static_cast<const uint8_t *>(set->mRecords[key].record.object);
        std::string copyText = "null";
        if (copy) {
            // The vtable word differs by design (the image's on the original side, ours on ours); every other byte is compared.
            const void *vtable = *reinterpret_cast<const void *const *>(copy);
            const bool own = vtable == (side == 0 ? GameAddress<void>(cRZVariant::kVTable) : ourVariantVTable);
            char buf[96];
            uint32_t words[4];
            std::memcpy(words, copy + 4, sizeof(words));
            std::snprintf(buf, sizeof buf, "%s %08x %08x %08x %08x %s", own ? "variant" : "other-vtable", words[0], words[1], words[2],
                          words[3], copy == reinterpret_cast<const uint8_t *>(&source) ? "same" : "new");
            copyText = buf;
        }
        return "put " + KeyText(key) + "@" + std::to_string(changeId) + " copy " + copyText;
    });
}

DIFF_TEST(ChangeRecordSet_Read, 0x1f6bd0)
{
    Install();
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1f6bd0);
    uint32_t complete = 0;
    Compare(ctx, "Read", 3000, [&](int side, ChangeRecordSet *set, std::mt19937 &rng) {
        Stream stream;
        stream.vtable = gStreamVTable;
        const uint32_t count = rng() % 8;
        stream.words.push_back(rng() % 6 ? 1 : rng() % 3);
        stream.words.push_back(count);
        for (uint32_t i = 0; i < count; ++i) {
            stream.keys.push_back(PoolKey(rng()));
            stream.words.push_back(rng() % 5);
        }
        stream.failAt = rng() % 3 ? ~0u : rng() % (4 + 3 * count);
        const uint32_t r = side == 0 ? original(set, &stream) & 0xff : set->Read(&stream);
        if (side == 0 && r) ++complete;
        return "read -> " + std::to_string(r);
    });
    if (complete < 500) ctx.Fail("coverage: %u complete reads", complete);
}

DIFF_TEST(ChangeRecordSet_Write, 0x1f6e90)
{
    Install();
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1f6e90);
    Compare(ctx, "Write", 2000, [&](int side, ChangeRecordSet *set, std::mt19937 &rng) {
        Stream stream;
        stream.vtable = gStreamVTable;
        stream.failAt = rng() % 3 ? ~0u : rng() % 40;
        const uint32_t r = side == 0 ? original(set, &stream) & 0xff : set->Write(&stream);
        return "write -> " + std::to_string(r);
    });
}
