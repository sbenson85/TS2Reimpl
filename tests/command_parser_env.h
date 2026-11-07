#pragma once
// The fixture shared by the CommandParser command tests: an Env (command object, fake argument list,
// parser, parser state and output object), ExecuteTest, and the condition-stack fixture. See
// tests/command_parser_exec.cpp for how they are used.
#include "command_parser.h"
#include "gz/CommandParser.h"

#include <functional>
#include <map>
#include <memory>
#include <random>
#include <type_traits>

namespace cp = nGZCommandParser;
using namespace cptest;

namespace {

constexpr size_t kStateSize = 0x400;
constexpr size_t kCmdSize = 0x50;

const char *const kStrings[] = {"alpha", "", "x", "set up", "100%", "a\\b", "ThE end"};

struct Env {
    FakeVTable argsVt{1}, parserVt{2}, stateVt{3}, outVt{4}, itemVt{5}, scopeVt{6};
    alignas(8) uint8_t args[16] = {};
    alignas(8) uint8_t parser[16] = {};
    alignas(8) uint8_t output[16] = {};
    alignas(8) uint8_t state[kStateSize] = {};
    alignas(8) uint8_t cmd[kCmdSize] = {};
    alignas(8) uint8_t items[4][16] = {}; // fake commands or blocks (vtable itemVt)
    alignas(8) uint8_t scope[16] = {};    // the state's virtual base (vtable scopeVt)
    uint32_t itemList[4] = {};            // pointers to the items
    int32_t vbtable[4] = {};              // the state's vbtable
    const char *argv[8] = {};
    std::mt19937 &gen;

    explicit Env(std::mt19937 &g) : gen(g)
    {
        *reinterpret_cast<void **>(args) = argsVt.slots;
        *reinterpret_cast<void **>(parser) = parserVt.slots;
        *reinterpret_cast<void **>(output) = outVt.slots;
        for (int i = 0; i < 4; ++i) {
            *reinterpret_cast<void **>(items[i]) = itemVt.slots;
            itemList[i] = reinterpret_cast<uint32_t>(items[i]);
        }
        *reinterpret_cast<void **>(scope) = scopeVt.slots;
        for (auto &w : reinterpret_cast<uint32_t(&)[kStateSize / 4]>(state)) w = gen();
        for (auto &w : reinterpret_cast<uint32_t(&)[kCmdSize / 4]>(cmd)) w = gen();
        *reinterpret_cast<void **>(state) = stateVt.slots;
        *reinterpret_cast<void **>(state + 0x14) = output;
        *reinterpret_cast<void **>(cmd + 4) = parser;
        *reinterpret_cast<void **>(cmd + 0xc) = state;
        for (auto &s : argv) s = kStrings[gen() % (sizeof(kStrings) / sizeof(kStrings[0]))];
        // The argument list answers slot 11 with the array of argument strings; slot 15 and the
        // others record.
        argsVt.Set<15>(0);
        argsVt.Set<11>(1);
        argsVt.ret[11] = reinterpret_cast<uint32_t>(argv);
    }
    std::vector<Region> Regions() { return {{cmd, kCmdSize}, {state, kStateSize}, {output, 16}}; }
    // Makes the state's virtual base (at state + 8 + offset) the fake `scope` object: the word at
    // state+8 points to a vbtable whose third entry is the offset. The scope must not overlap the
    // state's own words that the commands read, so it lives outside the state block: offset is
    // (scope - (state + 8)).
    void SetUpVirtualBase()
    {
        int32_t offset = static_cast<int32_t>(scope - (state + 8));
        vbtable[0] = 0;
        vbtable[1] = 0;
        vbtable[2] = offset;
        vbtable[3] = 0;
        *reinterpret_cast<void **>(state + 8) = vbtable;
    }
    // A random 32-bit result for a fake slot, with the low byte sometimes zero.
    uint32_t RandomResult() { return gen() % 3 == 0 ? (gen() & 0xffffff00u) : gen(); }
};

// What a setup can return to rebuild and normalise memory around each run (see Differential).
struct Hooks {
    std::function<void()> before, after;
};

template <class C, class Fn, class Setup>
void ExecuteTest(difftest::Context &ctx, uint32_t rva, const char *name, Fn ours, Setup setup, int cases = 60)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(rva);
    std::mt19937 gen(rva);
    for (int i = 0; i < cases; ++i) {
        Env e(gen);
        Hooks hooks;
        if constexpr (std::is_void_v<decltype(setup(e))>) setup(e);
        else hooks = setup(e);
        char what[96];
        std::snprintf(what, sizeof(what), "%s case %d", name, i);
        Differential(
            ctx, what, e.Regions(),
            [&] { return RunGuarded([&] { orig(e.cmd, e.args); }); },
            [&] { return RunGuarded([&] { (reinterpret_cast<C *>(e.cmd)->*ours)(reinterpret_cast<cp::cIArguments *>(e.args)); }); },
            [&] {
                if (hooks.after) hooks.after();
            },
            [&] {
                if (hooks.before) hooks.before();
            });
    }
}

// The condition stack of the parser state (state+0x110: begin, end, capacity of 8-byte entries:
// int level, byte active, byte elseSeen, two padding bytes). It is a std::vector in the game, and
// the commands may reallocate it: the original frees the old block, so every run starts with a
// fresh block built from the same entries. After a run the block is read (size, capacity, the
// entries) into the data the fakes saw, freed, and the three pointers cleared.
struct CondEntry {
    int32_t level;
    uint8_t active, elseSeen, pad0, pad1;
};

struct CondFixture {
    uint8_t *vec;                  // state + 0x110
    std::vector<CondEntry> initial;
    size_t capacity;

    void Before()
    {
        auto *block = static_cast<CondEntry *>(::operator new(capacity * sizeof(CondEntry)));
        for (size_t i = 0; i < initial.size(); ++i) block[i] = initial[i];
        reinterpret_cast<CondEntry **>(vec)[0] = block;
        reinterpret_cast<CondEntry **>(vec)[1] = block + initial.size();
        reinterpret_cast<CondEntry **>(vec)[2] = block + capacity;
    }

    void After()
    {
        auto *begin = reinterpret_cast<CondEntry **>(vec)[0];
        auto *end = reinterpret_cast<CondEntry **>(vec)[1];
        auto *cap = reinterpret_cast<CondEntry **>(vec)[2];
        uint32_t size = static_cast<uint32_t>(end - begin), capacity_ = static_cast<uint32_t>(cap - begin);
        gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&size), reinterpret_cast<uint8_t *>(&size) + 4);
        gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&capacity_), reinterpret_cast<uint8_t *>(&capacity_) + 4);
        for (uint32_t i = 0; i < size; ++i) {
            CondEntry e = begin[i];
            // The two padding bytes of an entry the command pushed hold stack leftovers in the
            // original (uninitialised there): they are not compared.
            if (i >= initial.size()) e.pad0 = e.pad1 = 0;
            gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&e), reinterpret_cast<uint8_t *>(&e) + 8);
        }
        ::operator delete(begin);
        reinterpret_cast<CondEntry **>(vec)[0] = reinterpret_cast<CondEntry **>(vec)[1] = reinterpret_cast<CondEntry **>(vec)[2] = nullptr;
    }
};

Hooks MakeCondHooks(Env &e, std::mt19937 &gen, size_t minSize, size_t maxSize, bool validLevels)
{
    auto fx = std::make_shared<CondFixture>();
    fx->vec = e.state + 0x110;
    size_t n = minSize + gen() % (maxSize - minSize + 1);
    for (size_t i = 0; i < n; ++i) {
        CondEntry c;
        c.level = static_cast<int32_t>(gen() % 3);
        c.active = gen() % 2;
        c.elseSeen = gen() % 3 == 0;
        c.pad0 = static_cast<uint8_t>(gen());
        c.pad1 = static_cast<uint8_t>(gen());
        fx->initial.push_back(c);
    }
    if (validLevels && n) fx->initial.back().level = static_cast<int32_t>(gen() % n); // the end command removes this many
    fx->capacity = n + (gen() % 2 ? 0 : 1 + gen() % 3);
    if (fx->capacity == 0) fx->capacity = 1;
    e.state[0xec] = gen() % 2;
    Hooks h;
    h.before = [fx] { fx->Before(); };
    h.after = [fx] { fx->After(); };
    return h;
}

} // namespace
