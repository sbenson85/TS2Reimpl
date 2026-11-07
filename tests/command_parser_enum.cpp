// Differential tests for the parser's name tables (EnumTable::Lookup 0xaf6df0, the equal_range
// 0xaf6300 and its two comparisons 0xaf6880, 0xaf68f0) and for the log command that uses one.
#include "command_parser_env.h"

#include <algorithm>

namespace {

using cp::EntryRange;
using cp::EnumEntry;
using cp::KeyRange;

// The table's order, written independently of the source: by length, then from the last byte
// backwards as signed chars.
bool Before(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return a.size() < b.size();
    for (size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i]) return static_cast<signed char>(a[i]) < static_cast<signed char>(b[i]);
    return false;
}

// A name for a failure message: bytes outside ASCII as xNN.
std::string Show(const std::string &s)
{
    std::string out;
    char buf[8];
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
        else {
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

std::string RandomName(std::mt19937 &gen)
{
    static const char alphabet[] = {'a', 'b', 'Z', '_', '\xe9', '\x80', '0'};
    size_t length = gen() % 7 == 0 ? 14 + gen() % 8 : gen() % 6;
    std::string s;
    for (size_t i = 0; i < length; ++i) s += alphabet[gen() % sizeof(alphabet)];
    return s;
}

struct Table {
    std::vector<EnumEntry> entries;
    void Fill(std::mt19937 &gen)
    {
        size_t n = gen() % 14;
        std::vector<std::string> names;
        for (size_t i = 0; i < n; ++i) names.push_back(gen() % 5 == 0 && !names.empty() ? names[gen() % names.size()] : RandomName(gen));
        std::stable_sort(names.begin(), names.end(), Before);
        entries.clear();
        for (auto &s : names) entries.push_back({s, static_cast<int32_t>(gen())});
    }
    std::string Pick(std::mt19937 &gen) const
    {
        if (!entries.empty() && gen() % 3 != 0) {
            std::string s = entries[gen() % entries.size()].name;
            if (gen() % 4 == 0 && !s.empty()) s.back() ^= 1; // a near miss
            return s;
        }
        return RandomName(gen);
    }
};

} // namespace

DIFF_TEST(CommandParser_KeyRange_EntryBeforeKey, 0xaf6880)
{
    auto orig = ctx.Original<bool(__thiscall *)(const KeyRange *, const EnumEntry *, uint32_t)>(0xaf6880);
    std::mt19937 gen(0xaf6880);
    for (int i = 0; i < 3000; ++i) {
        std::string k = RandomName(gen);
        if (gen() % 3 == 0) k = RandomName(gen);
        EnumEntry e{gen() % 3 == 0 ? k : RandomName(gen), 0};
        if (gen() % 4 == 0 && !e.name.empty()) e.name[gen() % e.name.size()] ^= static_cast<char>(1 + gen() % 3);
        KeyRange key{k.c_str() + k.size(), k.c_str()};
        uint32_t junk = gen();
        bool a = orig(&key, &e, junk) & 1, b = key.EntryBeforeKey(&e, junk);
        ++ctx.cases;
        if (a != b) ctx.Fail("EntryBeforeKey('%s' vs '%s'): %d vs %d", Show(e.name).c_str(), Show(k).c_str(), a, b);
    }
}

DIFF_TEST(CommandParser_KeyRange_KeyBeforeEntry, 0xaf68f0)
{
    auto orig = ctx.Original<bool(__thiscall *)(const KeyRange *, uint32_t, const EnumEntry *)>(0xaf68f0);
    std::mt19937 gen(0xaf68f0);
    for (int i = 0; i < 3000; ++i) {
        std::string k = RandomName(gen);
        EnumEntry e{gen() % 3 == 0 ? k : RandomName(gen), 0};
        if (gen() % 4 == 0 && !e.name.empty()) e.name[gen() % e.name.size()] ^= static_cast<char>(1 + gen() % 3);
        KeyRange key{k.c_str() + k.size(), k.c_str()};
        uint32_t junk = gen();
        bool a = orig(&key, junk, &e) & 1, b = key.KeyBeforeEntry(junk, &e);
        ++ctx.cases;
        if (a != b) ctx.Fail("KeyBeforeEntry('%s' vs '%s'): %d vs %d", Show(k).c_str(), Show(e.name).c_str(), a, b);
    }
}

DIFF_TEST(CommandParser_FindEnumRange, 0xaf6300)
{
    auto orig = ctx.Original<void(__cdecl *)(EntryRange *, const EnumEntry *, const EnumEntry *, const void *, KeyRange)>(0xaf6300);
    std::mt19937 gen(0xaf6300);
    for (int i = 0; i < 1500; ++i) {
        Table t;
        t.Fill(gen);
        std::string k = t.Pick(gen);
        KeyRange key{k.c_str() + k.size(), k.c_str()};
        const EnumEntry *first = t.entries.data(), *last = first + t.entries.size();
        const char *word = k.c_str();
        EntryRange a{nullptr, nullptr}, b{nullptr, nullptr};
        orig(&a, first, last, &word, key);
        cp::FindEnumRange(&b, first, last, &word, key);
        ++ctx.cases;
        if (a.first != b.first || a.last != b.last)
            ctx.Fail("FindEnumRange('%s', %zu entries): [%td, %td) vs [%td, %td)", Show(k).c_str(), t.entries.size(), a.first - first,
                     a.last - first, b.first - first, b.last - first);
    }
}

DIFF_TEST(CommandParser_EnumTable_Lookup, 0xaf6df0)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(const cp::EnumTable *, const char *)>(0xaf6df0);
    std::mt19937 gen(0xaf6df0);
    for (int i = 0; i < 1500; ++i) {
        cp::EnumTable table;
        Table t;
        t.Fill(gen);
        table.entries = t.entries;
        std::string k = t.Pick(gen);
        Differential(
            ctx, "EnumTable::Lookup", {},
            [&] { return RunGuarded([&] { return orig(&table, k.c_str()); }); },
            [&] { return RunGuarded([&] { return table.Lookup(k.c_str()); }); });
    }
}

// ---- the log command ------------------------------------------------------------------------------

namespace {

int32_t gCount10 = 0;
const char *const *gArgv10 = nullptr;
uint32_t __fastcall ArgsSlot10(void *self, void *, int32_t *countOut, uint32_t lo, uint32_t hi)
{
    Record(self, 10, 0, lo, hi);
    *countOut = gCount10;
    return reinterpret_cast<uint32_t>(gArgv10);
}

} // namespace

DIFF_TEST(cLogCommand_Execute, 0xb018d0)
{
    // The command looks its level up in the game's table at RVA 0x10f9a30: a table of ours is put
    // there for the test (and the 12 bytes of the empty one put back).
    auto *global = ctx.image.At<cp::EnumTable>(0x10f9a30);
    uint8_t saved[sizeof(cp::EnumTable)];
    std::memcpy(saved, global, sizeof(saved));
    new (global) cp::EnumTable();
    global->entries = {{"b", 1}, {"aa", 2}, {"ab", 3}, {"warning", 4}};
    std::stable_sort(global->entries.begin(), global->entries.end(), [](const EnumEntry &x, const EnumEntry &y) { return Before(x.name, y.name); });
    ExecuteTest<cp::cLogCommand>(
        ctx, 0xb018d0, "cLogCommand::Execute", &cp::cLogCommand::Execute,
        [](Env &e) {
            static const char *const kNames[] = {"b", "aa", "ab", "warning", "unknown", "", "AA", "wARning"};
            static const char *argv[3];
            for (auto &w : argv) w = kNames[e.gen() % 8];
            e.argsVt.slots[10] = reinterpret_cast<void *>(&ArgsSlot10);
            gArgv10 = argv;
            gCount10 = e.gen() % 5;
        },
        150);
    global->~EnumTable();
    std::memcpy(global, saved, sizeof(saved));
}
