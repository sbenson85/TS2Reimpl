// Differential test for nGZCommandParser::cDumpParserStateCommand::Execute (0xb008a0; src/gz/CommandParser.cpp), on the
// command fixture of tests/command_parser_env.h (recorded calls, compared bytes).
//
// With the option "variables" (the argument list's slot 1 answers), every variable of the state's scope tree is written
// as 'path:name' = 'value', the scopes visited depth first from the root at state+0x78 (each a pair of hash tables of the
// CommandTable kind: the variables' node list at +4, the child scopes' at +0x24; a node holds the name's characters and
// length at +8 / +0xc and a std::string value or a child scope at +0x10); with "definitions", every definition's name and
// parameters (its parameter list at +0x20: slot 7 the count, slot 4 each text); with "scope", the state's scope path (a
// std::string at +0x134). Then slot 15 of the argument list.
//
// The output object's slot 14 logs its text into the data the fakes saw (the texts are often the command's own stack or
// heap strings, whose addresses differ between the runs), with no address in the call log. The scope trees are built from
// static nodes; the scope path string is built before each run and destroyed after it.
#include "command_parser_env.h"

#include <string>

namespace {

void PushText(const char *s)
{
    gExtra.insert(gExtra.end(), s, s + std::strlen(s) + 1);
}

uint32_t __fastcall OutText(void *self, void *, uint32_t text)
{
    Record(self, 14, 0);
    PushText(reinterpret_cast<const char *>(text));
    return 0;
}

// Slot 1 of the argument list: is the option given? Answers from gOptions (bit 0 variables, 1 definitions, 2 scope);
// bits 8 and up give the raw byte above the answer's low bit.
uint32_t gOptions = 0;
uint32_t __fastcall ArgsHas(void *self, void *, uint32_t text)
{
    Record(self, 1, text);
    const char *s = reinterpret_cast<const char *>(text);
    int bit = std::strcmp(s, "variables") == 0 ? 0 : std::strcmp(s, "definitions") == 0 ? 1 : std::strcmp(s, "scope") == 0 ? 2 : -1;
    if (bit < 0 || !(gOptions >> bit & 1)) return gOptions & 0xffffff00u;
    return (gOptions & 0xffffff00u) | (bit == 1 ? 0x80u : 1u);
}

struct Node {
    Node *next;
    Node *prev;
    const char *name;
    uint32_t length;
};
struct VarNode : Node {
    std::string value; // +0x10
};
struct Scope;
struct ChildNode : Node {
    Scope *child; // +0x10
};
static_assert(offsetof(VarNode, value) == 0x10 && offsetof(ChildNode, child) == 0x10, "node layout");

struct Scope {
    uint32_t w0;
    Node *vars;      // +4
    uint8_t pad[0x1c];
    Node *children;  // +0x24
};
static_assert(offsetof(Scope, children) == 0x24, "scope layout");

constexpr int kMaxScopes = 6, kMaxVars = 24;
Scope gScopes[kMaxScopes];
Node gHeads[2 * (kMaxScopes + 1)];
VarNode gVars[kMaxVars];
ChildNode gChildren[kMaxScopes];
int gHeadUsed = 0, gVarUsed = 0, gChildUsed = 0;

const char *const kVarNames[] = {"a", "count", "", "long_variable_name_beyond_sso", "x:y"};
const char *const kValues[] = {"1", "", "hello world", "a value long enough for the heap: 0123456789", "%s"};

Node *NewHead()
{
    Node *h = &gHeads[gHeadUsed++];
    h->next = h->prev = h;
    return h;
}

void Link(Node *head, Node *n)
{
    n->prev = head->prev;
    n->next = head;
    head->prev->next = n;
    head->prev = n;
}

// Fills a scope (whose table words live at `vars` / `children`) with up to three variables and, below `depth`, children.
void FillScope(Node **vars, Node **children, int depth, std::mt19937 &gen)
{
    *vars = NewHead();
    *children = NewHead();
    int nv = static_cast<int>(gen() % 4);
    for (int i = 0; i < nv && gVarUsed < kMaxVars; ++i) {
        VarNode &v = gVars[gVarUsed++];
        const char *name = kVarNames[gen() % 5];
        v.name = name;
        v.length = static_cast<uint32_t>(std::strlen(name));
        if (v.length && gen() % 4 == 0) --v.length; // the length decides, not a terminator
        v.value = kValues[gen() % 5];
        Link(*vars, &v);
    }
    if (depth <= 0) return;
    int nc = static_cast<int>(gen() % 3);
    for (int i = 0; i < nc && gChildUsed < kMaxScopes; ++i) {
        ChildNode &c = gChildren[gChildUsed];
        Scope &s = gScopes[gChildUsed++];
        const char *name = kVarNames[gen() % 5];
        c.name = name;
        c.length = static_cast<uint32_t>(std::strlen(name));
        c.child = &s;
        Link(*children, &c);
        FillScope(&s.vars, &s.children, depth - 1, gen);
    }
}

struct Definition {
    void *vtable;
    uint32_t word4;
    std::string name;  // +8
    void *params;      // +0x20 (the parameter list's vtable)
};
Definition gDefs[4];
void *gDefArray[4];
const char *const kDefNames[] = {"x", "macro", "", "a_definition_name_on_the_heap"};
const char *const kParams[] = {"p0", "second", "", "a parameter text"};

uint32_t __fastcall ParamText(void *self, void *, uint32_t index)
{
    Record(self, 4, index);
    return reinterpret_cast<uint32_t>(kParams[index % 4]);
}

std::string gScopePath;

} // namespace

DIFF_TEST(cDumpParserStateCommand_Execute, 0xb008a0)
{
    static FakeVTable paramsVt{7};
    paramsVt.Set<7>(0);
    paramsVt.slots[4] = reinterpret_cast<void *>(&ParamText);
    ExecuteTest<cp::cDumpParserStateCommand>(
        ctx, 0xb008a0, "cDumpParserStateCommand::Execute", &cp::cDumpParserStateCommand::Execute,
        [](Env &e) {
            gOptions = e.gen() % 3 == 0 ? e.gen() : (e.gen() & ~7u) | 7u;
            e.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsHas);
            e.outVt.slots[14] = reinterpret_cast<void *>(&OutText);
            gHeadUsed = gVarUsed = gChildUsed = 0;
            FillScope(reinterpret_cast<Node **>(e.state + 0x78 + 4), reinterpret_cast<Node **>(e.state + 0x78 + 0x24), 3, e.gen);
            int defs = static_cast<int>(e.gen() % 5);
            for (int i = 0; i < 4; ++i) {
                gDefs[i].vtable = e.itemVt.slots;
                gDefs[i].name = kDefNames[e.gen() % 4];
                gDefs[i].params = paramsVt.slots;
                gDefArray[i] = &gDefs[i];
            }
            paramsVt.ret[7] = e.gen() % 4 == 0 ? static_cast<uint32_t>(-1 - static_cast<int>(e.gen() % 2)) : e.gen() % 4;
            *reinterpret_cast<void **>(e.state + 0x6c) = gDefArray;
            *reinterpret_cast<void **>(e.state + 0x70) = gDefArray + defs;
            static const char *const kPaths[] = {"", "root:", "a:b:c:", "a scope path long enough to be on the heap:"};
            gScopePath = kPaths[e.gen() % 4];
            uint8_t *state = e.state;
            Hooks h;
            h.before = [state] { ::new (state + 0x134) std::string(gScopePath); };
            h.after = [state] {
                reinterpret_cast<std::string *>(state + 0x134)->~basic_string();
                std::memset(state + 0x134, 0, sizeof(std::string));
            };
            return h;
        },
        1500);
}
