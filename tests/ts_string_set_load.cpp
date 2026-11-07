// Differential tests for the cTSStringSet loaders and writer (lane DG): IsValidUtf8, OpenResourceRecord, GetStrings,
// GetComments, GetOrAddStrings, LoadAllLanguages, LoadAll, WriteStrings, Commit, and Clear with populated maps.
// The record, file reference, stream, persist system and language service are fakes (ts_string_set_load.h) reached through the
// same game globals by the original and by our code; each run compares the result, the set's loaded state, the record's bytes
// (the old format edits them in place) and the log of every call into a fake.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts_string_set_load.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace tsload;

namespace {

struct Temp : cRZString {
    explicit Temp(const char *text) { Construct(text); }
};

constexpr uint32_t kKeyType = 0x53545223;

struct Key {
    uint32_t words[4] = {kKeyType, 0x1234, 0x20, 0};
};

using SetFn = cTSStringSet *(__thiscall *)(cTSStringSet *);

// One run of `body` on a freshly constructed set (the original constructor on both sides), returning everything observable.
template <typename Body>
std::string RunSide(difftest::Context &ctx, Fixture &fixture, const Config &config, const Key &key, Body &&body,
    const char *initialName = nullptr)
{
    fixture.Reset(config);
    alignas(4) uint8_t memory[0x7c];
    std::memset(memory, 0xcd, sizeof memory);
    auto *set = reinterpret_cast<cTSStringSet *>(memory);
    ctx.Original<SetFn>(0x1eace0)(set);
    std::memcpy(set->mKey, key.words, sizeof set->mKey);
    if (initialName) set->mName.mString = initialName;
    std::string out = body(set);
    out += "|" + DumpSet(set) + "|" + DumpEvents() + "|rec" + std::to_string(Hash(gRecord.data.data(), gRecord.data.size())) + "|new" +
           std::to_string(Hash(gNewRecord.data.data(), gNewRecord.data.size())) + "|out" +
           std::to_string(Hash(gOutBytes.data(), gOutBytes.size())) + "/" + std::to_string(gOutBytes.size());
    ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1eae50)(set);
    return out;
}

// Runs the same body once as the original (`false`) and once as ours (`true`) and compares everything.
template <typename Body>
void Compare(difftest::Context &ctx, Fixture &fixture, const char *what, int scenario, const Config &config, const Key &key, Body &&body,
    const char *initialName = nullptr)
{
    const std::string original = RunSide(ctx, fixture, config, key, [&](cTSStringSet *set) { return body(set, false); }, initialName);
    const std::string ours = RunSide(ctx, fixture, config, key, [&](cTSStringSet *set) { return body(set, true); }, initialName);
    if (original != ours) {
        size_t at = 0;
        while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
        ctx.Fail("%s scenario %d differs at %zu:\n  original: %s\n  ours:     %s", what, scenario, at, original.substr(at > 40 ? at - 40 : 0, 200).c_str(),
            ours.substr(at > 40 ? at - 40 : 0, 200).c_str());
    }
    ++ctx.cases;
}

// ---- record payloads ----
struct Entry {
    uint8_t language;
    std::string text, comment;
};

Bytes PayloadFFFD(const std::vector<Entry> &entries, int count = -1)
{
    Writer w;
    w.u16(0xfffd).u16(uint16_t(count < 0 ? entries.size() : size_t(count)));
    for (const auto &e : entries) w.u8(e.language).cstr(e.text).cstr(e.comment);
    return w.bytes;
}
Bytes PayloadFFFE(const std::vector<Entry> &entries, bool comments, int count = -1)
{
    Writer w;
    w.u16(comments ? 0xfffe : 0xffff).u16(uint16_t(count < 0 ? entries.size() : size_t(count)));
    for (const auto &e : entries) {
        w.cstr(e.text);
        if (comments) w.cstr(e.comment);
    }
    return w.bytes;
}
Bytes PayloadOld(const std::vector<std::string> &texts)
{
    Writer w;
    w.u8(uint8_t(texts.size() >> 8)).u8(uint8_t(texts.size()));
    for (const auto &t : texts) w.u8(uint8_t(t.size())).raw(t);
    return w.bytes;
}
struct StreamLanguage {
    int8_t language;
    std::vector<Entry> entries; // the language byte of an entry is unused here; it is the two ignored bytes
};
Bytes StreamFFF6(const std::vector<StreamLanguage> &languages)
{
    Writer w;
    w.u16(0xfff6).u16(uint16_t(languages.size()));
    for (const auto &l : languages) {
        w.u8(uint8_t(l.language)).u16(uint16_t(l.entries.size()));
        for (const auto &e : l.entries) w.u8(e.language).u8(0x5a).gzstr(e.text).gzstr(e.comment);
    }
    return w.bytes;
}

const std::vector<std::string> kTexts = {
    "Hello", "Line1\r\nLine2", "caf\xE9 au lait", "caf\xC3\xA9", "", "\xE2\x80\xA2 one \xE2\x80\xA2 two", "x\r\n\r\ny\r\n",
    "\xE2\x80\xA2\xE2\x80\xA2", "tail\r", "\xA1\xD5 mixed \x80\xFF", "a\xC3", "\xF0\x9F\x98\x80 emoji", "\xE0\x80\x80"};

std::vector<Entry> SomeEntries()
{
    std::vector<Entry> e;
    uint8_t language = 1;
    for (size_t i = 0; i < kTexts.size(); ++i) {
        e.push_back({language, kTexts[i], "c" + std::to_string(i) + (i % 3 == 0 ? "\r\n" : "")});
        if (i % 4 == 3) language = uint8_t(language + 1);
    }
    e.push_back({0, "zero", "zero-comment"});
    e.push_back({19, "\xE2\x80\xA2 bullet \r\n \xE2\x80\xA2\xE2\x80\xA2", "bc"});
    e.push_back({19, "plain 19", "pc"});
    e.push_back({2, "second 2", "sc"});
    return e;
}

struct RecordCase {
    Config config;
    const char *what;
};

std::vector<RecordCase> RecordCases()
{
    std::vector<RecordCase> cases;
    auto add = [&](const char *what, const Bytes &payload, const Bytes &stream = {}) {
        Config config;
        config.record = MakeRecord("Name of the record", payload);
        config.stream = stream;
        cases.push_back({config, what});
    };
    add("fffd", PayloadFFFD(SomeEntries()));
    add("fffd-empty", PayloadFFFD({}));
    add("fffd-negative-count", PayloadFFFD(SomeEntries(), 0x8000));
    add("fffd-overcount", PayloadFFFD({{1, "only", "one"}, {2, "two", ""}}, 5));
    add("fffe", PayloadFFFE(SomeEntries(), true));
    add("fffe-empty", PayloadFFFE({}, true));
    add("ffff", PayloadFFFE(SomeEntries(), false));
    add("ffff-overcount", PayloadFFFE({{1, "a", ""}}, false, 4));
    {
        std::vector<std::string> old;
        for (const auto &t : kTexts)
            if (t.size() < 0x80) old.push_back(t);
        old.push_back("\xA1\xD5\xA1\xD5");
        add("old", PayloadOld(old));
        add("old-empty", PayloadOld({}));
    }
    {
        // 0xFFF6: format in the record, then the stream (format word, language count, ...).
        std::vector<StreamLanguage> languages;
        languages.push_back({0, {{1, "zero a", "ca"}, {2, "zero b\r\n", "cb"}}});
        languages.push_back({2, {{3, "two a", "c2a"}, {4, "caf\xE9", "c2b"}, {5, "", ""}}});
        languages.push_back({18, {{6, "\xE2\x80\xA2 bullets \xE2\x80\xA2\r\n", "c18"}}});
        languages.push_back({5, {}});
        languages.push_back({7, {{7, "seven", "c7"}}});
        add("fff6", Writer().u16(0xfff6).bytes, StreamFFF6(languages));
        add("fff6-empty", Writer().u16(0xfff6).bytes, StreamFFF6({}));
    }
    return cases;
}

const int kLanguages[] = {0, 1, 2, 3, 4, 5, 7, 18, -1, 43, 17};

Key KeyWith(uint32_t group, uint32_t instance)
{
    Key key;
    key.words[1] = group;
    key.words[2] = instance;
    return key;
}

using GetFn = cRZStringVector *(__thiscall *)(cTSStringSet *, int);

// GetStrings / GetComments on both sides: the same call twice (the second finds the entry the first made).
template <bool Comments>
void RunLoader(difftest::Context &ctx)
{
    Fixture fixture(ctx);
    const uint32_t rva = Comments ? 0x1ec7e0 : 0x1ecf60;
    const char *what = Comments ? "GetComments" : "GetStrings";
    int scenario = 0;
    auto run = [&](const Config &config, const Key &key, int language) {
        Compare(ctx, fixture, what, scenario++, config, key, [&](cTSStringSet *set, bool ours) {
            std::string out;
            for (int round = 0; round < 2; ++round) {
                const cRZStringVector *list;
                if (ours)
                    list = Comments ? set->GetComments(static_cast<signed char>(language)) : set->GetStrings(static_cast<signed char>(language));
                else
                    list = ctx.Original<GetFn>(rva)(set, language);
                out += WhichList(set, list) + ";";
            }
            return out;
        });
    };
    for (const auto &c : RecordCases())
        for (int language : kLanguages) {
            Config config = c.config;
            config.languageId = language == 5 ? 6 : (language == 2 ? 0 : (language == 1 ? -1 : 1));
            run(config, Key(), language);
        }
    // The language service's answers for the current language (-1): 0, -1 (both mean 1), 1, 2, 19.
    for (int id : {0, -1, 1, 2, 19, 100})
        for (const auto &c : RecordCases()) {
            Config config = c.config;
            config.languageId = id;
            run(config, Key(), -1);
        }
    // Where the record comes from, and the ways it can be missing.
    const Bytes record = MakeRecord("Nm", PayloadFFFD(SomeEntries()));
    auto env = [&](Config config, const Key &key, int language) {
        config.record = record;
        for (int l : {language, 0}) run(config, key, l);
    };
    Config base;
    env(base, KeyWith(0, 0x20), 1);                // group 0: nothing is looked up, nothing inserted
    { Config c = base; c.createFile = false; env(c, Key(), 1); }
    { Config c = base; c.openResult = 0xffffff9e; env(c, Key(), 1); }
    { Config c = base; c.fileHasRecord = false; env(c, Key(), 1); } // no record, no high half: nothing more is tried
    for (bool manager : {true, false})
        for (bool its : {true, false})
            for (bool serial : {true, false})
                for (uint32_t position : {0u, 0x10u, 0x40u}) {
                    Config c = base;
                    c.fileHasRecord = false;
                    c.managerOpens = manager;
                    c.dbAnswersIts = its;
                    c.dbAnswersSerial = serial;
                    c.position = position;
                    c.bytesLeft = 0x100;
                    c.serialPayload = record;
                    env(c, KeyWith(0x1234, 0x20 | 0x10000), 1);
                }
    { Config c = base; c.createFile = false; c.dbAnswersIts = false; c.serialPayload = record; env(c, KeyWith(0x1234, 0x80020), 0); }
    // Random records of the byte formats.
    std::mt19937 rng(Comments ? 77 : 99);
    for (int i = 0; i < 160; ++i) {
        auto text = [&]() {
            std::string s;
            const int n = rng() % 9;
            for (int k = 0; k < n; ++k) {
                switch (rng() % 8) {
                case 0: s += "\r\n"; break;
                case 1: s += "\xE2\x80\xA2"; break;
                case 2: s.push_back(char(0x80 + rng() % 0x80)); break;
                case 3: s += "\xC3\xA9"; break;
                default: s.push_back(char('a' + rng() % 26)); break;
                }
            }
            return s;
        };
        std::vector<Entry> entries;
        const int n = rng() % 6;
        for (int k = 0; k < n; ++k) entries.push_back({uint8_t(rng() % 4 == 0 ? 19 : rng() % 4), text(), text()});
        Bytes payload;
        switch (rng() % 4) {
        case 0: payload = PayloadFFFD(entries, rng() % 5 == 0 ? int(rng() % 8) : -1); break;
        case 1: payload = PayloadFFFE(entries, true); break;
        case 2: payload = PayloadFFFE(entries, false); break;
        default: {
            std::vector<std::string> old;
            for (auto &e : entries) old.push_back(e.text);
            payload = PayloadOld(old);
        }
        }
        Config config;
        config.record = MakeRecord("random", payload);
        config.languageId = 1 + int(rng() % 3);
        const int languages[] = {0, 0, 1, 2, 18, -1};
        run(config, Key(), languages[rng() % 6]);
    }
}

} // namespace

DIFF_TEST(StringSet_GetStrings, 0x1ecf60)
{
    RunLoader<false>(ctx);
}

DIFF_TEST(StringSet_GetComments, 0x1ec7e0)
{
    RunLoader<true>(ctx);
}

// A set with some strings and comments loaded already (built through our containers on both sides).
namespace {
void Populate(cTSStringSet *set, bool strings, bool comments, bool commentsForEveryLanguage)
{
    auto add = [](cTSStringSetMap &map, int language, std::initializer_list<const char *> texts) {
        auto &list = map[static_cast<signed char>(language)];
        for (const char *t : texts) {
            Temp s(t);
            list.PushBackAny(s);
        }
    };
    if (strings) {
        add(set->mStrings, 0, {"zero one", "zero two", "zero three"});
        add(set->mStrings, 3, {"three one"});
        add(set->mStrings, 17, {"seventeen a", "seventeen b", "seventeen c", "seventeen d"});
    }
    if (comments) {
        add(set->mComments, 0, {"c0 one", "c0 two"});
        if (commentsForEveryLanguage) {
            add(set->mComments, 3, {"c3 one", "c3 two"});
            add(set->mComments, 17, {"c17 one"});
        }
    }
}
} // namespace

DIFF_TEST(StringSet_GetOrAddStrings, 0x1eb860)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (int id : {0, 1, 4})
        for (int language : {0, 3, 17, 5, -1, 43}) {
            Config config;
            config.languageId = id;
            Compare(ctx, fixture, "GetOrAddStrings", scenario++, config, Key(), [&](cTSStringSet *set, bool ours) {
                Populate(set, true, false, false);
                std::string out;
                for (int round = 0; round < 2; ++round) {
                    const cRZStringVector *list = ours ? set->GetOrAddStrings(static_cast<signed char>(language))
                                                      : ctx.Original<GetFn>(0x1eb860)(set, language);
                    out += WhichList(set, list) + ";";
                }
                return out;
            });
        }
}

DIFF_TEST(StringSet_LoadAllLanguages, 0x1ec7b0)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (const auto &c : RecordCases()) {
        Compare(ctx, fixture, "LoadAllLanguages", scenario++, c.config, Key(), [&](cTSStringSet *set, bool ours) {
            if (ours) set->LoadAllLanguages();
            else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1ec7b0)(set);
            return std::string("done");
        });
    }
    { Config c = RecordCases()[0].config; c.createFile = false; Compare(ctx, fixture, "LoadAllLanguages", scenario++, c, Key(), [&](cTSStringSet *set, bool ours) {
        if (ours) set->LoadAllLanguages(); else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1ec7b0)(set);
        return std::string("nofile"); }); }
}

DIFF_TEST(StringSet_LoadAll, 0x1eb8f0)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (const auto &c : RecordCases()) {
        for (const char *name : {"", "preset name", "a name that is longer than the sixteen characters of a small string"}) {
            Compare(ctx, fixture, "LoadAll", scenario++, c.config, Key(), [&](cTSStringSet *set, bool ours) {
                if (ours) set->LoadAll();
                else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1eb8f0)(set);
                return std::string("done");
            }, name);
        }
    }
    { Config c = RecordCases()[0].config; Compare(ctx, fixture, "LoadAll", scenario++, c, KeyWith(0, 0x20), [&](cTSStringSet *set, bool ours) {
        if (ours) set->LoadAll(); else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1eb8f0)(set);
        return std::string("group0"); }, "kept"); }
}

DIFF_TEST(StringSet_WriteStrings, 0x1ef4e0)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (bool strings : {true, false})
        for (bool comments : {true, false})
            for (bool every : {true, false}) {
                Compare(ctx, fixture, "WriteStrings", scenario++, Config(), Key(), [&](cTSStringSet *set, bool ours) {
                    Populate(set, strings, comments, every);
                    // Texts of one language beyond the comments of another, and a language with no texts but comments.
                    if (strings && comments && every) {
                        Temp extra("only a text");
                        set->mStrings[5].PushBackAny(extra);
                        set->mStrings[5].PushBackAny(extra);
                        set->mComments[9].PushBackAny(extra);
                    }
                    const bool result = ours ? set->WriteStrings(&gOut) : ctx.Original<bool(__thiscall *)(cTSStringSet *, void *)>(0x1ef4e0)(set, &gOut);
                    return std::string(result ? "true" : "false");
                });
            }
}

DIFF_TEST(StringSet_Commit, 0x1ee510)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (const auto &c : RecordCases())
        for (bool segment : {true, false})
            for (bool record : {true, false}) {
                Config config = c.config;
                config.segmentFound = segment;
                config.createRecord = record;
                Compare(ctx, fixture, "Commit", scenario++, config, Key(), [&](cTSStringSet *set, bool ours) {
                    const uint32_t result = ours ? set->Commit() : ctx.Original<uint32_t(__thiscall *)(cTSStringSet *)>(0x1ee510)(set);
                    return std::to_string(result);
                }, "nm");
            }
    { Config c = RecordCases()[0].config; Key key = KeyWith(0x77, 0xfffe8000); key.words[0] = 0x43545353;
      Compare(ctx, fixture, "Commit", scenario++, c, key, [&](cTSStringSet *set, bool ours) {
        return std::to_string(ours ? set->Commit() : ctx.Original<uint32_t(__thiscall *)(cTSStringSet *)>(0x1ee510)(set)); }, "ctss"); }
}

DIFF_TEST(StringSet_OpenResourceRecord, 0x1ec1c0)
{
    Fixture fixture(ctx);
    using Fn = bool(__cdecl *)(const uint32_t *, void **);
    int scenario = 0;
    const Bytes record = MakeRecord("resource", PayloadFFFD({{1, "x", "y"}}));
    for (bool manager : {true, false})
        for (bool its : {true, false})
            for (bool serial : {true, false})
                for (uint32_t position : {0u, 0x10u, 0x3fu, 0x40u, 0x1000u})
                    for (uint32_t left : {0u, 5u, 0x30u, 0x100u})
                        for (size_t payloadSize : {size_t(0), size_t(7), size_t(0x90)}) {
                            Config config;
                            config.managerOpens = manager;
                            config.dbAnswersIts = its;
                            config.dbAnswersSerial = serial;
                            config.position = position;
                            config.bytesLeft = left;
                            config.record = record;
                            config.serialPayload.assign(payloadSize, 0x7c);
                            Compare(ctx, fixture, "OpenResourceRecord", scenario++, config, Key(), [&](cTSStringSet *, bool ours) {
                                void *out = reinterpret_cast<void *>(uintptr_t(0x5555aaaa));
                                const uint32_t key[4] = {1, 2, 3, 4};
                                const bool result = ours ? cTSStringSet::OpenResourceRecord(key, &out) : ctx.Original<Fn>(0x1ec1c0)(key, &out);
                                const char *which = out == &gRecord ? "record" : out == &gNewRecord ? "new" : uintptr_t(out) == 0x5555aaaa ? "unchanged" : "other";
                                return std::string(result ? "true " : "false ") + which;
                            });
                        }
}

DIFF_TEST(StringSet_IsValidUtf8, 0x1b285f)
{
    using Fn = bool(__cdecl *)(const uint8_t *, int);
    std::mt19937 rng(0xdead);
    auto check = [&](const std::vector<uint8_t> &bytes) {
        std::vector<uint8_t> exact(bytes);
        const bool original = ctx.Original<Fn>(0x1b285f)(exact.data(), int(exact.size()));
        const bool ours = cTSStringSet::IsValidUtf8(exact.data(), int(exact.size()));
        if (original != ours) {
            std::string hex;
            for (uint8_t b : bytes) hex += std::to_string(b) + " ";
            ctx.Fail("IsValidUtf8(%s) original %d ours %d", hex.c_str(), original, ours);
        }
        ++ctx.cases;
    };
    check({});
    // Every lead byte with every second byte, followed by continuations and by a plain end.
    for (int lead = 0; lead < 256; ++lead)
        for (int second = 0; second < 256; second += (second < 0x70 && second > 0x20 && second % 16 ? 8 : 1)) {
            for (int tail = 0; tail < 6; ++tail) {
                std::vector<uint8_t> bytes = {uint8_t(lead)};
                if (tail > 0) bytes.push_back(uint8_t(second));
                for (int k = 1; k < tail; ++k) bytes.push_back(uint8_t(0x80 + k));
                check(bytes);
                bytes.push_back('x');
                check(bytes);
            }
        }
    for (int i = 0; i < 6000; ++i) {
        std::vector<uint8_t> bytes(rng() % 12);
        for (auto &b : bytes) {
            switch (rng() % 6) {
            case 0: b = uint8_t(0x80 + rng() % 0x40); break;
            case 1: b = uint8_t(0xc0 + rng() % 0x40); break;
            case 2: b = uint8_t(rng()); break;
            default: b = uint8_t(rng() % 0x80); break;
            }
        }
        check(bytes);
    }
}

// Clear with populated maps (the empty-map case of lane DC's byte comparison cannot show a missed erase): the same lists
// are freed, in both maps, and both maps end empty.
DIFF_TEST(StringSet_Clear, 0x1ee360)
{
    Fixture fixture(ctx);
    int scenario = 0;
    for (bool strings : {true, false})
        for (bool comments : {true, false})
            for (bool every : {true, false}) {
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    fixture.Reset(Config());
                    alignas(4) uint8_t memory[0x7c];
                    std::memset(memory, 0xcd, sizeof memory);
                    auto *set = reinterpret_cast<cTSStringSet *>(memory);
                    ctx.Original<SetFn>(0x1eace0)(set);
                    Populate(set, strings, comments, every);
                    difftest::FreeRecorder frees;
                    if (side) set->Clear();
                    else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1ee360)(set);
                    frees.Stop();
                    size_t sizes[64];
                    const size_t count = frees.SortedSizes(sizes, 64);
                    results[side] = DumpSet(set) + " frees " + std::to_string(frees.Count());
                    for (size_t i = 0; i < count; ++i) results[side] += "," + std::to_string(sizes[i]);
                    ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1eae50)(set);
                }
                if (results[0] != results[1]) ctx.Fail("Clear scenario %d differs:\n  original: %s\n  ours:     %s", scenario, results[0].c_str(), results[1].c_str());
                ++scenario;
                ++ctx.cases;
            }
}
