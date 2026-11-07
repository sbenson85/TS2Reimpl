// Differential tests for the parser's command lookup (cParserState::FindCommand 0xaff340, NextToken
// 0xaf6b80) and for the if command's slot 10 (0xb03a20), which uses them.
//
// The parser state is a block of random bytes with the open-block stack (+0x104/+0x108), the case
// flag (+0x2c) and a command table (+0x30) built by the original's own constructor and insert. The
// blocks, the commands and the line are fakes that record their calls.
#include "command_parser_env.h"

#include <windows.h>

#include <memory>
#include <new>

namespace {

using cp::CommandNode;
using cp::CommandTable;
using cp::InsertResult;
using cp::KeyView;

// A fake block: slot 11 (name, length) gives the command configured for the name.
struct FakeBlock {
    void *vtable;
    const char *knownName;
    void *command;
};

uint32_t __fastcall BlockFind(void *self, void *, uint32_t name, uint32_t length)
{
    Record(self, 11, name, length);
    auto *b = static_cast<FakeBlock *>(self);
    if (b->knownName && std::strlen(b->knownName) == length && std::strncmp(b->knownName, reinterpret_cast<const char *>(name), length) == 0)
        return reinterpret_cast<uint32_t>(b->command);
    return 0;
}

// A fake command: slot 0 QueryInterface(id, out): answers yes for the meta interface when asked to,
// writing the fake meta object; slots 1 and 2 are AddRef and Release.
struct FakeCommand {
    void *vtable;
    bool meta;
    void *metaObject;
};

uint32_t __fastcall CommandQuery(void *self, void *, uint32_t iid, void **out)
{
    Record(self, 0, iid, 0);
    auto *c = static_cast<FakeCommand *>(self);
    if (iid == 0xcae872f1 && c->meta) {
        *out = c->metaObject;
        return 0x100 | 1; // al = 1 (the upper bits are not used)
    }
    return 0x200; // al = 0
}

// The condition stack entries as the state keeps them (level, active, else seen).
struct ConditionEntryForTest {
    int32_t level;
    uint8_t active, elseSeen, pad[2];
};

// A line of text: slot 5 gives the text.
struct Line {
    void *vtable;
    const char *text;
};

uint32_t __fastcall LineText(void *self, void *)
{
    Record(self, 5);
    return reinterpret_cast<uint32_t>(static_cast<Line *>(self)->text);
}

struct LookupWorld {
    FakeVTable blockVt{20}, cmdVt{21}, metaVt{22}, lineVt{23};
    FakeBlock blocks[3];
    FakeCommand cmds[4];
    FakeCommand metaObjects[1];
    uint32_t metaRaw[4];
    const char *names[4] = {"alpha", "BETA", "gamma", "Delta"};
    uint32_t blockList[3];
    std::string tableKeys[4];

    LookupWorld()
    {
        blockVt.slots[11] = reinterpret_cast<void *>(&BlockFind);
        cmdVt.slots[0] = reinterpret_cast<void *>(&CommandQuery);
        cmdVt.Set<1>(0);
        cmdVt.Set<2>(0);
        metaVt.Set<2>(0);
        metaVt.Set<9>(1);
        metaRaw[0] = reinterpret_cast<uint32_t>(metaVt.slots);
        for (int i = 0; i < 4; ++i) {
            cmds[i].vtable = cmdVt.slots;
            cmds[i].meta = i % 2 == 0;
            cmds[i].metaObject = metaRaw;
        }
        for (int i = 0; i < 3; ++i) {
            blocks[i].vtable = blockVt.slots;
            blocks[i].knownName = nullptr;
            blocks[i].command = nullptr;
            blockList[i] = reinterpret_cast<uint32_t>(&blocks[i]);
        }
    }
};

// A command table in the state at +0x30 made by the original constructor (on a scratch block) and
// filled by the original insert with `keys` (already folded as the table needs them).
void BuildStateTable(difftest::Context &ctx, uint8_t *state, const std::vector<std::string *> &keys, const std::vector<void *> &values)
{
    auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xaf7750);
    auto insert = ctx.Original<InsertResult *(__thiscall *)(CommandTable *, InsertResult *, const KeyView *)>(0xaf7560);
    alignas(8) uint8_t scratch[0x50];
    ctor(scratch);
    auto *block = reinterpret_cast<cp::cBlockCommandBase *>(scratch);
    block->mNames.Destruct(); // the pool is not needed: keys belong to the test
    std::memcpy(state + 0x30, &block->mCommands, sizeof(CommandTable));
    auto *table = reinterpret_cast<CommandTable *>(state + 0x30);
    for (size_t i = 0; i < keys.size(); ++i) {
        KeyView key{keys[i]->data(), static_cast<uint32_t>(keys[i]->size())};
        InsertResult r;
        insert(table, &r, &key);
        r.node->value = values[i];
    }
}

void FreeStateTable(uint8_t *state)
{
    auto *table = reinterpret_cast<CommandTable *>(state + 0x30);
    CommandNode *n = table->head->next;
    while (n != table->head) {
        CommandNode *next = n->next;
        ::operator delete(n);
        n = next;
    }
    ::operator delete(table->head);
    table->buckets.~vector();
}

std::string Fold(std::string s)
{
    for (auto &c : s) c = static_cast<char>(c & 0xdf);
    return s;
}

// The globals the code reads, put in the image for the test and put back afterwards.
struct Globals {
    difftest::Context &ctx;
    uint8_t savedString[sizeof(std::string)];
    uint32_t savedLengths[4];
    static constexpr uint32_t lengthRvas[4] = {0x10748f4, 0x1074994, 0x1074998, 0x107499c};
    explicit Globals(difftest::Context &c, const char *separators) : ctx(c)
    {
        std::memcpy(savedString, ctx.image.At(0x107479c), sizeof(savedString));
        new (ctx.image.At(0x107479c)) std::string(separators);
        const uint32_t lengths[4] = {2, 6, 4, 5}; // "if", "elseif", "else", "endif"
        for (int i = 0; i < 4; ++i) {
            savedLengths[i] = *ctx.image.At<uint32_t>(lengthRvas[i]);
            *ctx.image.At<uint32_t>(lengthRvas[i]) = lengths[i];
        }
    }
    ~Globals()
    {
        ctx.image.At<std::string>(0x107479c)->~basic_string();
        std::memcpy(ctx.image.At(0x107479c), savedString, sizeof(savedString));
        for (int i = 0; i < 4; ++i) *ctx.image.At<uint32_t>(lengthRvas[i]) = savedLengths[i];
    }
};
constexpr uint32_t Globals::lengthRvas[4];

} // namespace

DIFF_TEST(CommandParser_NextToken, 0xaf6b80)
{
    auto orig = ctx.Original<const char *(__cdecl *)(uint32_t *, const char *)>(0xaf6b80);
    Globals globals(ctx, " \t,;");
    std::mt19937 gen(0xaf6b80);
    static const char alphabet[] = {'a', 'b', ' ', '\t', ',', ';', 'z', '\xe9', '1'};
    for (int i = 0; i < 2000; ++i) {
        std::string text;
        for (size_t k = 0, n = gen() % 14; k < n; ++k) text += alphabet[gen() % sizeof(alphabet)];
        uint32_t la = 0xaaaaaaaa, lb = 0xbbbbbbbb;
        const char *ra = orig(&la, text.c_str()), *rb = cp::NextToken(&lb, text.c_str());
        ++ctx.cases;
        if (ra != rb || la != lb) ctx.Fail("NextToken('%s'): %td/%u vs %td/%u", Printable(text).c_str(), ra ? ra - text.c_str() : -1, la, rb ? rb - text.c_str() : -1, lb);
    }
}

DIFF_TEST(cParserState_FindCommand, 0xaff340)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, const char *, uint32_t)>(0xaff340);
    std::mt19937 gen(0xaff340);
    for (int round = 0; round < 60; ++round) {
        LookupWorld w;
        alignas(8) uint8_t state[0x400];
        for (auto &b : state) b = static_cast<uint8_t>(gen());
        bool fold = round % 3 != 0;
        state[0x2c] = fold ? 0 : static_cast<uint8_t>(1 + gen() % 255);
        // The table holds some of the names (folded when the table folds); the blocks know others.
        std::vector<std::string *> keys;
        std::vector<void *> values;
        for (int i = 0; i < 4; ++i) {
            if (gen() % 3 == 0) continue;
            w.tableKeys[i] = fold ? Fold(w.names[i]) : std::string(w.names[i]);
            keys.push_back(&w.tableKeys[i]);
            values.push_back(&w.cmds[i]);
        }
        BuildStateTable(ctx, state, keys, values);
        size_t depth = gen() % 4;
        *reinterpret_cast<uint32_t *>(state + 0x104) = reinterpret_cast<uint32_t>(w.blockList);
        *reinterpret_cast<uint32_t *>(state + 0x108) = reinterpret_cast<uint32_t>(w.blockList + depth);
        for (size_t d = 0; d < depth; ++d) {
            if (gen() % 2) {
                int pick = gen() % 4;
                w.blocks[d].knownName = w.names[pick];
                w.blocks[d].command = &w.cmds[pick];
            }
        }
        for (int q = 0; q < 12; ++q) {
            static const char *const probes[] = {"alpha", "ALPHA", "beta", "BETA", "gamma", "Delta", "delta", "nothing", ""};
            std::string name = gen() % 7 == 0 ? std::string(0x70 + gen() % 0x20, 'a') : std::string(probes[gen() % 9]);
            Differential(
                ctx, "cParserState::FindCommand", {{state, sizeof(state)}},
                [&]() { return RunGuarded([&]() -> uint32_t { return reinterpret_cast<uint32_t>(orig(state, name.c_str(), static_cast<uint32_t>(name.size()))); }); },
                [&]() {
                    return RunGuarded([&]() -> uint32_t {
                        return reinterpret_cast<uint32_t>(reinterpret_cast<cp::cParserState *>(state)->FindCommand(name.c_str(), static_cast<uint32_t>(name.size())));
                    });
                });
        }
        FreeStateTable(state);
    }
}

DIFF_TEST(cIfCommand_HandleLine, 0xb03a20)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *, uint32_t)>(0xb03a20);
    Globals globals(ctx, " \t");
    std::mt19937 gen(0xb03a20);
    for (int round = 0; round < 80; ++round) {
        LookupWorld w;
        alignas(8) uint8_t state[0x400];
        for (auto &b : state) b = static_cast<uint8_t>(gen());
        state[0x2c] = round % 3 == 0 ? 1 : 0;
        std::vector<std::string *> keys;
        std::vector<void *> values;
        for (int i = 0; i < 4; ++i) {
            if (gen() % 3 == 0) continue;
            w.tableKeys[i] = state[0x2c] ? std::string(w.names[i]) : Fold(w.names[i]);
            keys.push_back(&w.tableKeys[i]);
            values.push_back(&w.cmds[i]);
        }
        BuildStateTable(ctx, state, keys, values);
        // Up to two open blocks (they are asked for the name, and may know it).
        size_t depth = gen() % 3;
        *reinterpret_cast<uint32_t *>(state + 0x104) = reinterpret_cast<uint32_t>(w.blockList);
        *reinterpret_cast<uint32_t *>(state + 0x108) = reinterpret_cast<uint32_t>(w.blockList + depth);
        for (size_t d = 0; d < depth; ++d) {
            if (gen() % 2) {
                int pick = gen() % 4;
                w.blocks[d].knownName = w.names[pick];
                w.blocks[d].command = &w.cmds[pick];
            }
        }
        // The condition stack: the last entry says whether the branch is active.
        alignas(8) ConditionEntryForTest entries[2] = {};
        entries[1].active = round % 2;
        *reinterpret_cast<uint32_t *>(state + 0x114) = reinterpret_cast<uint32_t>(entries + 2);
        alignas(8) uint8_t command[16];
        for (auto &b : command) b = static_cast<uint8_t>(gen());
        *reinterpret_cast<uint32_t *>(command + 8) = reinterpret_cast<uint32_t>(state);
        FakeVTable lineVt(30);
        lineVt.slots[5] = reinterpret_cast<void *>(&LineText);
        Line line{lineVt.slots, ""};
        for (int q = 0; q < 14; ++q) {
            static const char *const lines[] = {"if x", "elseif y", "else", "endif", "  \t endif z", "ifx", "iff", "", "   ", "alpha rest of it", "BETA  1 2",
                                                "gamma", "delta x", "unknown thing", "else{", "endi", "elseifz", "\tif"};
            line.text = lines[gen() % 18];
            uint32_t junk = gen();
            Differential(
                ctx, "cIfCommand::HandleLine", {{state, sizeof(state)}, {command, sizeof(command)}},
                [&]() { return RunGuarded([&]() -> uint32_t { return orig(command, &line, junk) & 0xff; }); },
                [&]() { return RunGuarded([&]() -> uint32_t { return reinterpret_cast<cp::cIfCommand *>(command)->HandleLine(&line, junk) ? 1 : 0; }); });
        }
        FreeStateTable(state);
    }
}
