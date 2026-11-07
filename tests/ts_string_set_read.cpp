// Differential tests for the cTSStringSet readers and shell (lane DG): Construct, Create, GetStringCount, GetString,
// GetStringInto, GetStringExact, GetComment, GetName, SetName, IsWritable, CopyFrom, AppendString(From), SetKeyFromFile,
// ResetSortOrder, GetSortedIndex and the string-set factory's CreateInstance / CreateInstanceByType / Write.
// Same fakes as ts_string_set_load.cpp (ts_string_set_load.h).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "heap_poison.h"
#include "ifaces/cITSStringSet.h"
#include "ts_string_set_load.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace tsload;

namespace {

struct Temp : cRZString {
    explicit Temp(const char *text) { Construct(text); }
};

using SetFn = cTSStringSet *(__thiscall *)(cTSStringSet *);
using Destroy = void(__thiscall *)(cTSStringSet *);
constexpr uint32_t kKeyType = 0x53545223;

struct Key {
    uint32_t words[4] = {kKeyType, 0x1234, 0x20, 0};
};

// A set built by the original constructor in `memory`.
cTSStringSet *Make(difftest::Context &ctx, uint8_t *memory, const Key &key)
{
    std::memset(memory, 0xcd, 0x7c);
    auto *set = reinterpret_cast<cTSStringSet *>(memory);
    ctx.Original<SetFn>(0x1eace0)(set);
    std::memcpy(set->mKey, key.words, sizeof set->mKey);
    return set;
}

template <typename Body>
std::string RunSide(difftest::Context &ctx, Fixture &fixture, const Config &config, const Key &key, Body &&body)
{
    fixture.Reset(config);
    alignas(4) uint8_t memory[0x7c];
    cTSStringSet *set = Make(ctx, memory, key);
    std::string out = body(set);
    out += "|" + DumpSet(set) + "|key" + std::to_string(set->mKey[0]) + "," + std::to_string(set->mKey[1]) + "," + std::to_string(set->mKey[2]) +
           "," + std::to_string(set->mKey[3]) + "|sort";
    for (uint32_t v : set->mSortOrder) out += "," + std::to_string(v);
    out += "/" + std::to_string(set->mSortOrder.capacity()) + "|" + DumpEvents() + "|out" + std::to_string(Hash(gOutBytes.data(), gOutBytes.size()));
    ctx.Original<Destroy>(0x1eae50)(set);
    return out;
}

template <typename Body>
void Compare(difftest::Context &ctx, Fixture &fixture, const char *what, int scenario, const Config &config, const Key &key, Body &&body)
{
    const std::string original = RunSide(ctx, fixture, config, key, [&](cTSStringSet *set) { return body(set, false); });
    const std::string ours = RunSide(ctx, fixture, config, key, [&](cTSStringSet *set) { return body(set, true); });
    if (original != ours) {
        size_t at = 0;
        while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
        ctx.Fail("%s scenario %d differs at %zu:\n  original: %s\n  ours:     %s", what, scenario, at, original.substr(at > 40 ? at - 40 : 0, 220).c_str(),
            ours.substr(at > 40 ? at - 40 : 0, 220).c_str());
    }
    ++ctx.cases;
}

void Populate(cTSStringSet *set, int variant)
{
    auto add = [](cTSStringSetMap &map, int language, std::initializer_list<const char *> texts) {
        auto &list = map[static_cast<signed char>(language)];
        for (const char *t : texts) {
            Temp s(t);
            list.PushBackAny(s);
        }
    };
    switch (variant) {
    case 1:
        add(set->mStrings, 0, {"zero one", "zero two", "zero three"});
        add(set->mStrings, 3, {"three one"});
        add(set->mStrings, 17, {"seventeen a", "seventeen b", "seventeen c", "seventeen d"});
        add(set->mComments, 0, {"c0 one", "c0 two"});
        break;
    case 2: // empty entries in the other languages: the default language's text stands in for them
        add(set->mStrings, 0, {"zero one", "", "zero three", "zero four"});
        add(set->mStrings, 3, {"", "three two", ""});
        add(set->mStrings, 17, {"seventeen a", "", "", ""});
        add(set->mStrings, 1, {});
        add(set->mComments, 0, {"c0 one", "", "c0 three"});
        break;
    default:
        break;
    }
}

std::string Where(cTSStringSet *set, const cRZString *text)
{
    for (auto *map : {&set->mStrings, &set->mComments})
        for (const auto &entry : *map)
            if (text >= entry.second.mFirst && text < entry.second.mLast)
                return std::string(map == &set->mStrings ? "S" : "C") + std::to_string(int(entry.first)) + "[" + std::to_string(text - entry.second.mFirst) + "]";
    return "static'" + text->mString + "'";
}

const int kIndexes[] = {0, 1, 2, 3, 4, 5, 100, -1};
const int kLanguages[] = {0, 1, 3, 17, 18, 5, -1};

Config RecordConfig()
{
    Config config;
    Writer w;
    w.u16(0xfffd);
    struct E { uint8_t l; const char *t, *c; };
    const E entries[] = {{1, "en one", "ce1"}, {1, "", "ce2"}, {1, "en three", ""}, {4, "four one", "c41"}, {4, "", "c42"}, {4, "four three", "c43"},
        {18, "eighteen", "c18"}, {2, "two one", "c21"}};
    w.u16(8);
    for (const auto &e : entries) w.u8(e.l).cstr(e.t).cstr(e.c);
    config.record = MakeRecord("record name", w.bytes);
    return config;
}

// The variants every reader is run against: loaded lazily from the record, set up by hand, nothing to load (group 0).
struct Variant {
    int populate;
    uint32_t group;
    bool showSource;
};
const Variant kVariants[] = {{0, 0x1234, false}, {1, 0x1234, false}, {2, 0x1234, false}, {0, 0, false}, {2, 0x1234, true}, {1, 0, true}};

template <typename Body>
void RunVariants(difftest::Context &ctx, const char *what, Body &&body)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (const Variant &variant : kVariants)
        for (int service : {1, 4}) {
            Config config = RecordConfig();
            config.languageId = service;
            Key key;
            key.words[1] = variant.group;
            Compare(ctx, fixture, what, scenario++, config, key, [&](cTSStringSet *set, bool ours) {
                Populate(set, variant.populate);
                set->mShowSource = variant.showSource;
                set->mSourceName.mString = "source.package";
                return body(set, ours);
            });
        }
}

} // namespace

DIFF_TEST(StringSet_GetStringCount, 0x1eb6e0)
{
    RunVariants(ctx, "GetStringCount", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int language : kLanguages)
            out += std::to_string(ours ? set->GetStringCount(language)
                                       : ctx.Original<uint32_t(__thiscall *)(cTSStringSet *, int)>(0x1eb6e0)(set, language)) + ",";
        return out;
    });
}

DIFF_TEST(StringSet_GetString, 0x1ebfb0)
{
    RunVariants(ctx, "GetString", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int language : kLanguages)
            for (int index : kIndexes) {
                const cRZString *text = ours ? set->GetString(uint32_t(index), language)
                                             : ctx.Original<const cRZString *(__thiscall *)(cTSStringSet *, uint32_t, int)>(0x1ebfb0)(set, uint32_t(index), language);
                out += Where(set, text) + ";";
            }
        return out;
    });
}

DIFF_TEST(StringSet_GetStringInto, 0x1ec080)
{
    RunVariants(ctx, "GetStringInto", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int language : kLanguages)
            for (int index : kIndexes) {
                Temp text("preset");
                const bool found = ours ? set->GetStringInto(text.AsInterface(), uint32_t(index), language)
                                        : ctx.Original<bool(__thiscall *)(cTSStringSet *, cIGZString *, uint32_t, int)>(0x1ec080)(set, text.AsInterface(), uint32_t(index), language);
                out += std::string(found ? "1'" : "0'") + text.mString + "';";
            }
        return out;
    });
}

DIFF_TEST(StringSet_GetStringExact, 0x1ebb70)
{
    RunVariants(ctx, "GetStringExact", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int language : kLanguages)
            for (int index : kIndexes) {
                Temp text("preset");
                const bool found = ours ? set->GetStringExact(text.AsInterface(), uint32_t(index), language)
                                        : ctx.Original<bool(__thiscall *)(cTSStringSet *, cIGZString *, uint32_t, int)>(0x1ebb70)(set, text.AsInterface(), uint32_t(index), language);
                out += std::string(found ? "1'" : "0'") + text.mString + "';";
            }
        return out;
    });
}

DIFF_TEST(StringSet_GetComment, 0x1ebb20)
{
    RunVariants(ctx, "GetComment", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int index : kIndexes) {
            Temp text("preset");
            const bool found = ours ? set->GetComment(text.AsInterface(), uint32_t(index), 7)
                                    : ctx.Original<bool(__thiscall *)(cTSStringSet *, cIGZString *, uint32_t, int)>(0x1ebb20)(set, text.AsInterface(), uint32_t(index), 7);
            out += std::string(found ? "1'" : "0'") + text.mString + "';";
        }
        return out;
    });
}

DIFF_TEST(StringSet_GetName, 0x1ebbe0)
{
    RunVariants(ctx, "GetName", [&](cTSStringSet *set, bool ours) {
        Temp text("preset");
        const bool result = ours ? set->GetName(text.AsInterface())
                                 : ctx.Original<bool(__thiscall *)(cTSStringSet *, cIGZString *)>(0x1ebbe0)(set, text.AsInterface());
        return std::string(result ? "1'" : "0'") + text.mString + "'";
    });
}

DIFF_TEST(StringSet_SetName, 0x1eec60)
{
    RunVariants(ctx, "SetName", [&](cTSStringSet *set, bool ours) {
        Temp text("the new name of the set, longer than a small string");
        if (ours) set->SetName(text.AsInterface());
        else ctx.Original<void(__thiscall *)(cTSStringSet *, cIGZString *)>(0x1eec60)(set, text.AsInterface());
        return std::string("done");
    });
}

DIFF_TEST(StringSet_IsWritable, 0x1eb580)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (bool found : {true, false})
        for (bool writable : {true, false}) {
            Config config;
            config.segmentFound = found;
            config.writable = writable;
            Compare(ctx, fixture, "IsWritable", scenario++, config, Key(), [&](cTSStringSet *set, bool ours) {
                const bool result = ours ? set->IsWritable() : ctx.Original<bool(__thiscall *)(cTSStringSet *)>(0x1eb580)(set);
                return std::string(result ? "true" : "false");
            });
        }
}

DIFF_TEST(StringSet_AppendString, 0x1eb550)
{
    RunVariants(ctx, "AppendString", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int language : {0, 3, -1}) {
            Temp text("appended text");
            const uint32_t index = ours ? set->AppendString(text.AsInterface(), language)
                                        : ctx.Original<uint32_t(__thiscall *)(cTSStringSet *, cIGZString *, int)>(0x1eb550)(set, text.AsInterface(), language);
            out += std::to_string(index) + ",";
        }
        return out;
    });
}

DIFF_TEST(StringSet_AppendStringFrom, 0x1eb510)
{
    RunVariants(ctx, "AppendStringFrom", [&](cTSStringSet *set, bool ours) {
        alignas(4) uint8_t memory[0x7c];
        cTSStringSet *source = Make(ctx, memory, Key());
        Populate(source, 2);
        std::string out;
        for (int sourceIndex : {0, 1, 2, 4, -3, 9}) {
            const uint32_t index = ours ? set->AppendStringFrom(source, uint32_t(sourceIndex))
                                        : ctx.Original<uint32_t(__thiscall *)(cTSStringSet *, void *, uint32_t)>(0x1eb510)(set, source, uint32_t(sourceIndex));
            out += std::to_string(index) + ",";
        }
        out += DumpSet(source);
        ctx.Original<Destroy>(0x1eae50)(source);
        return out;
    });
}

DIFF_TEST(StringSet_SetKeyFromFile, 0x1eebf0)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (bool withFile : {true, false})
        for (uint32_t instance : {0u, 0x20u, 0x8000u, 0xffffu, 0x12345678u})
            for (uint32_t type : {0u, 0x43545353u, kKeyType})
                for (int variant : {0, 1}) {
                    Compare(ctx, fixture, "SetKeyFromFile", scenario++, Config(), Key(), [&](cTSStringSet *set, bool ours) {
                        Populate(set, variant);
                        void *file = withFile ? static_cast<void *>(&gFile) : nullptr;
                        if (ours) set->SetKeyFromFile(file, instance, type);
                        else ctx.Original<void(__thiscall *)(cTSStringSet *, void *, uint32_t, uint32_t)>(0x1eebf0)(set, file, instance, type);
                        return std::string("done");
                    });
                }
}

DIFF_TEST(StringSet_ResetSortOrder, 0x1ef250)
{
    RunVariants(ctx, "ResetSortOrder", [&](cTSStringSet *set, bool ours) {
        for (int round = 0; round < 3; ++round) {
            if (round == 1) set->mSortOrder.assign({9, 8, 7});
            if (ours) set->ResetSortOrder();
            else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1ef250)(set);
        }
        return std::string("done");
    });
}

DIFF_TEST(StringSet_GetSortedIndex, 0x1ebf20)
{
    RunVariants(ctx, "GetSortedIndex", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int round = 0; round < 2; ++round)
            for (uint32_t position : {0u, 1u, 2u, 3u, 7u, 100u}) {
                if (round == 1 && position == 2) set->mSortOrder.assign({5, 4, 3, 2, 1, 0});
                out += std::to_string(ours ? set->GetSortedIndex(position)
                                           : ctx.Original<uint32_t(__thiscall *)(cTSStringSet *, uint32_t)>(0x1ebf20)(set, position)) + ",";
            }
        return out;
    });
}

DIFF_TEST(StringSet_CopyFrom, 0x1eb620)
{
    RunVariants(ctx, "CopyFrom", [&](cTSStringSet *set, bool ours) {
        std::string out;
        for (int sourceVariant : {-1, 0, 1, 2}) {
            alignas(4) uint8_t memory[0x7c];
            cTSStringSet *source = Make(ctx, memory, Key());
            source->mRefCount = 1;
            if (sourceVariant > 0) Populate(source, sourceVariant);
            else if (sourceVariant == 0) source->mKey[1] = 0x1234; // lazy: loaded from the record
            void *other = sourceVariant < 0 ? nullptr : static_cast<void *>(source);
            if (ours) set->CopyFrom(other);
            else ctx.Original<void(__thiscall *)(cTSStringSet *, void *)>(0x1eb620)(set, other);
            out += "[" + DumpSet(source) + " rc" + std::to_string(source->mRefCount) + "]";
            ctx.Original<Destroy>(0x1eae50)(source);
        }
        return out;
    });
}

// Construct: the original constructor and ours on the same seeded bytes leave the same object; the two map heads are
// self-linked sentinels (their addresses differ).
namespace {
struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isNil;
};
std::string HeadCheck(const cTSStringSetMap &map)
{
    auto *head = *reinterpret_cast<const RawNode *const *>(&map);
    if (!head) return "null";
    return std::string(head->left == head && head->parent == head && head->right == head ? "self" : "linked") + std::to_string(head->color) +
           std::to_string(head->isNil) + "/" + std::to_string(reinterpret_cast<const uint32_t *>(&map)[1]);
}
} // namespace

DIFF_TEST(StringSet_Construct, 0x1eace0)
{
    for (uint32_t seed = 0; seed < 8; ++seed) {
        std::mt19937 rng(seed + 11);
        uint8_t pattern[0x7c];
        for (auto &b : pattern) b = uint8_t(rng());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint8_t memory[0x7c];
            std::memcpy(memory, pattern, sizeof memory);
            auto *set = reinterpret_cast<cTSStringSet *>(memory);
            if (side) {
                if (set->Construct() != set) ctx.Fail("Construct returned another pointer");
            } else if (ctx.Original<SetFn>(0x1eace0)(set) != set) {
                ctx.Fail("original Construct returned another pointer");
            }
            std::string heads = HeadCheck(set->mStrings) + HeadCheck(set->mComments);
            uint8_t copy[0x7c];
            std::memcpy(copy, memory, sizeof copy);
            std::memset(copy + 0x3c, 0, 4); // the head pointers (heap addresses)
            std::memset(copy + 0x44, 0, 4);
            results[side] = heads + "|" + std::to_string(Hash(copy, sizeof copy));
            ctx.Original<Destroy>(0x1eae50)(set);
        }
        if (results[0] != results[1]) ctx.Fail("Construct seed %u differs: %s vs %s", seed, results[0].c_str(), results[1].c_str());
        ++ctx.cases;
    }
}

DIFF_TEST(StringSet_Create, 0x1eba70)
{
    for (uint32_t i = 0; i < 8; ++i) {
        std::string results[2];
        // Every heap block is poisoned on both sides (heap_poison.h, DT): the padding after the flag byte (0x59-0x5b, never
        // stored) holds the pattern on both sides and is compared.
        heappoison::Scope poison(ctx, i % 2 ? 0x5a : 0xcd);
        for (int side = 0; side < 2; ++side) {
            auto *set = side ? cTSStringSet::Create() : ctx.Original<cTSStringSet *(__cdecl *)()>(0x1eba70)();
            if (!set) ctx.Fail("Create returned null");
            uint8_t copy[0x7c];
            std::memcpy(copy, set, sizeof copy);
            std::string heads = HeadCheck(set->mStrings) + HeadCheck(set->mComments);
            std::memset(copy + 0x3c, 0, 12);
            results[side] = heads + "|" + std::to_string(Hash(copy, sizeof copy));
            ctx.Original<void *(__thiscall *)(cTSStringSet *, uint32_t)>(0x1eb230)(set, 1);
        }
        if (results[0] != results[1]) ctx.Fail("Create differs: %s vs %s", results[0].c_str(), results[1].c_str());
        ++ctx.cases;
    }
}

// ---- the factory ----
namespace {
struct FactoryObject {
    const void *vt;
    uint32_t count;
};
std::string DescribeCreated(void *out, uint32_t riid)
{
    if (out == nullptr) return "null";
    auto *set = reinterpret_cast<cTSStringSet *>(static_cast<uint8_t *>(out) - (riid == 0x456b8f1d ? 4 : 0));
    std::string text = "rc" + std::to_string(set->mRefCount) + " key" + std::to_string(set->mKey[0]) + "," + std::to_string(set->mKey[1]) + "," +
                       std::to_string(set->mKey[2]) + "," + std::to_string(set->mKey[3]) + " vt" + (set->mVTable == GameAddress(cTSStringSet::kVTable) ? "ok" : "bad") +
                       (set->mPersist.mVTable == GameAddress(cTSStringSet::kPersistVTable) ? "ok" : "bad") + " " + DumpSet(set) + " " + HeadCheck(set->mStrings);
    return text;
}
}

DIFF_TEST(StringSetFactory_CreateInstance, 0x1eb710)
{
    Fixture fixture(ctx);
    FactoryObject factory{GameAddress(cTSResourceFactoryStringSet::kVTable), 1};
    using Fn = bool(__thiscall *)(void *, void *, uint32_t, void **, uint32_t, uint32_t);
    for (uint32_t riid : {1u, 0xfb4cb0deu, 0xfb4cbaabu, 0x456b8f1du, 7u, 0u}) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            fixture.Reset(Config());
            void *out = reinterpret_cast<void *>(uintptr_t(0x5555aaaa));
            difftest::FreeRecorder frees;
            const bool answered = side ? reinterpret_cast<cTSResourceFactoryStringSet *>(&factory)->CreateInstance(&gDb, riid, &out, 0, 0)
                                       : ctx.Original<Fn>(0x1eb710)(&factory, &gDb, riid, &out, 0, 0);
            frees.Stop();
            results[side] = std::string(answered ? "1 " : "0 ") + (uintptr_t(out) == 0x5555aaaa ? "unchanged" : DescribeCreated(out, riid)) +
                            " frees" + std::to_string(frees.Count()) + DumpEvents();
            if (answered) As<ifaces::cITSStringSet>(reinterpret_cast<cTSStringSet *>(static_cast<uint8_t *>(out) - (riid == 0x456b8f1d ? 4 : 0)))->Release();
        }
        if (results[0] != results[1]) ctx.Fail("CreateInstance(%x) differs:\n  original: %s\n  ours:     %s", riid, results[0].c_str(), results[1].c_str());
        ++ctx.cases;
    }
}

DIFF_TEST(StringSetFactory_CreateInstanceByType, 0x1eb7c0)
{
    Fixture fixture(ctx);
    FactoryObject factory{GameAddress(cTSResourceFactoryStringSet::kVTable), 1};
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t);
    for (uint32_t type : {kKeyType, 0x43545353u, 0u})
        for (uint32_t riid : {0xfb4cb0deu, 0x456b8f1du, 7u}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                fixture.Reset(Config());
                void *out = reinterpret_cast<void *>(uintptr_t(0x5555aaaa));
                difftest::FreeRecorder frees;
                const bool answered = side ? reinterpret_cast<cTSResourceFactoryStringSet *>(&factory)->CreateInstanceByType(type, riid, &out, 0, 0)
                                           : ctx.Original<Fn>(0x1eb7c0)(&factory, type, riid, &out, 0, 0);
                frees.Stop();
                results[side] = std::string(answered ? "1 " : "0 ") + (uintptr_t(out) == 0x5555aaaa ? "unchanged" : DescribeCreated(out, riid)) +
                                " frees" + std::to_string(frees.Count());
                if (answered) As<ifaces::cITSStringSet>(reinterpret_cast<cTSStringSet *>(static_cast<uint8_t *>(out) - (riid == 0x456b8f1d ? 4 : 0)))->Release();
            }
            if (results[0] != results[1]) ctx.Fail("CreateInstanceByType(%x,%x) differs:\n  original: %s\n  ours:     %s", type, riid, results[0].c_str(), results[1].c_str());
            ++ctx.cases;
        }
}

DIFF_TEST(StringSetFactory_Write, 0x1ef2c0)
{
    Fixture fixture(ctx);
    FactoryObject factory{GameAddress(cTSResourceFactoryStringSet::kVTable), 1};
    using Fn = bool(__thiscall *)(void *, void *, void *);
    int scenario = 0;
    for (size_t nameLength : {size_t(0), size_t(5), size_t(0x3f), size_t(0x40), size_t(0x45)})
        for (bool resourceAnswers : {true, false})
            for (bool streamAnswers : {true, false})
                for (int variant : {0, 1, 2}) {
                    Config config;
                    config.dbAnswersStream = streamAnswers;
                    Compare(ctx, fixture, "FactoryWrite", scenario++, config, Key(), [&](cTSStringSet *set, bool ours) {
                        Populate(set, variant);
                        set->mRefCount = 1;
                        set->mName.mString.assign(nameLength, 'n');
                        void *resource = resourceAnswers ? static_cast<void *>(set) : static_cast<void *>(&gDb);
                        const bool result = ours ? reinterpret_cast<cTSResourceFactoryStringSet *>(&factory)->Write(resource, &gDb)
                                                 : ctx.Original<Fn>(0x1ef2c0)(&factory, resource, &gDb);
                        return std::string(result ? "true" : "false") + " rc" + std::to_string(set->mRefCount);
                    });
                }
}
