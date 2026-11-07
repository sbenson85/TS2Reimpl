#pragma once
// nGZCommandParser::cBaseParser, the command parser's base class (the cheat console, the log configuration and the
// other text parsers derive from it): its constructor 0xafd800 and what it builds. RVAs are 1.18.0.187.
//
// The class is hook-shaped for now: its 59 virtual functions are the game's, so the constructor installs the game's
// vtables, as the original's does (docs/standalone-shape.md is the later step; it needs those bodies first).
//
// RTTI: cBaseParser derives from cParserIsState (with its cState at +4: a type id) and, virtually, from cIBaseParser
// (which itself derives virtually from cIGZUnknown). MSVC lays it out as: the primary vtable at +0 (5 slots), the
// cState id at +4, the virtual base table pointer at +8, the fields, then the virtual bases, each preceded by a
// vtordisp word: cIGZUnknown at +0x1b0 (3 slots) and cIBaseParser at +0x1b8 (51 slots; its own vbptr at +0x1bc).
// In a derived parser the virtual bases sit further out: the derived constructor passes mostDerived = 0 and this
// constructor finds them through the vbtable the derived one stored at +8.
#include "game/owned.h"
#include "gz/CommandParser.h"
#include "gz/cArguments.h"
#include "gz/EvalFunctionTable.h"
#include "gz/cFileSource.h"
#include "misc/UtilitiesC.h"
#include "rz/cRZString.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector
#include "game/game.h"
#include "game/vtable.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace nGZCommandParser {

// A node of the namespace's variable table: the list links, the name (in the namespace's NamePool) and the value.
struct VariableNode {
    VariableNode *next;   // +0
    VariableNode *prev;   // +4
    const char *key;      // +8
    uint32_t keyLength;   // +0xc
    std::string value;    // +0x10
};
static_assert(sizeof(VariableNode) == 0x28, "VariableNode layout");

// The variable table: the same hash table layout as CommandTable, with VariableNode nodes.
struct VariableTable {
    float maxLoad;                       // +0 (1.0)
    VariableNode *head;                  // +4 sentinel of the circular node list
    uint32_t size;                       // +8
    std::vector<VariableNode *> buckets; // +0xc two list pointers per bucket
    uint32_t mask;                       // +0x18 bucketCount - 1
    uint32_t bucketCount;                // +0x1c

    // 0xafcb90 (__cdecl; the first argument, the list's allocator, unused): frees every node after the sentinel
    //  (each value string destroyed), the circle broken first at the last node. The sentinel stays, unlinked.
    static void __cdecl ClearNodes(void *unused, VariableNode *head);
    // 0xafdda0, the table's destructor, is library code (a std::unordered_map instantiation): ParserNamespace::Destruct
    // destroys the table as the VariableMap below, the same layout.

    struct InsertResult {
        VariableNode *node;
        uint8_t inserted; // false when the name was already there
    };
    // 0xafd050 (__thiscall, ret 8, returns out; only SetVariable calls it): finds the name or adds a node for it with
    // an empty value: CommandTable::Insert's algorithm on this layout (CommandTable's Find and Rehash), the limit
    // 0x6666666 nodes ("unordered_map/set too long").
    InsertResult *Insert(InsertResult *out, const KeyView *key);
};
static_assert(sizeof(VariableTable) == 0x20, "VariableTable layout");

// The table as the toolchain's container: the node is {next, prev, pair<key (pointer, length), string>}, the same as
// VariableNode, and the other fields are the container's (max load, list head and size, bucket vector, mask, bucket
// count). It is used where the table is destroyed: the original's 0xafdda0 is this container's destructor.
struct VariableKey {
    const char *ptr;
    uint32_t length;
};
struct VariableKeyHash {
    size_t operator()(const VariableKey &) const;
};
struct VariableKeyEqual {
    bool operator()(const VariableKey &, const VariableKey &) const;
};
using VariableMap = std::unordered_map<VariableKey, std::string, VariableKeyHash, VariableKeyEqual>;
static_assert(sizeof(VariableMap) == sizeof(VariableTable), "the table is a std::unordered_map");

struct ParserNamespace;
// Where a namespace path leads (0xb036d0): the namespace whose child table holds the last name, and that node; the
// owner null (and the node the table's sentinel) when a name on the way is missing.
struct NamespaceLookup {
    ParserNamespace *owner;
    CommandNode *node;
};

// A namespace of the parser (0x50 bytes): the global one is a member of the parser (+0x78), and the parser's
// "namespace" command (cIBaseParser slot 20, 0xb05770) makes one per block it opens. Variables, commands, the pool
// their names are copied into, and a word the constructor clears.
struct ParserNamespace {
    VariableTable mVariables; // +0
    CommandTable mCommands;   // +0x20
    NamePool mNames;          // +0x40
    uint32_t mUseCount;       // +0x4c (0): how many times it is open on the parser's namespace stack
    // mCommands holds the child namespaces: the nodes' values are ParserNamespace * (owned).

    // 0xafdc40 (__thiscall, returns this): two empty tables (a self-linked sentinel, 8 buckets, load 1.0), an
    // empty pool and the use count cleared.
    ParserNamespace *Construct();
    // 0xb05d70 (__thiscall, ret 4): empties the namespace: each child namespace destroyed and its node removed, or,
    // when `all` is false and the child is open on the stack (use count not 0), only emptied the same way; then the
    // variables freed and the name pool emptied.
    void Clear(bool all);
    // 0xafe800 (__thiscall): the destructor: Clear(false), the pool, the child table (the shared 0x472280: nodes
    // and buckets freed, the values not), the variable table.
    void Destruct();
    // 0xb036d0 (__thiscall, ret 8, returns out): follows a path of names separated by ':' through the child tables
    // ("a:b:c"; a ':' at the end ends the path).
    NamespaceLookup *FindPath(NamespaceLookup *out, const std::string &path);
};
static_assert(sizeof(ParserNamespace) == 0x50, "ParserNamespace layout");

// An empty hash table as the original's constructors build one: maxLoad, head and size zeroed, the sentinel
// allocated and linked to itself, an empty bucket vector, mask 7 and 8 buckets, maxLoad 1.0, then the bucket vector
// assigned 16 words that all point at the sentinel (the library's _Hash_vec::_Assign_grow 0x15fdc0).
void ConstructEmptyTable(CommandTable &table);
void ConstructEmptyTable(VariableTable &table);

// 0xaf6960 (__thiscall on the pool): frees every chunk, last first, and sets `used` to 0x1000 (the next name takes
// a new block). NamePool belongs to the command classes' file, so this one is a free function here.
void ResetNamePool(NamePool *pool);

// 0xafd700 (__thiscall on a namespace's child table, ret 8, returns out): the node removed from its bucket and the
// list and freed (its value is not touched); *out = the node after it.
CommandNode **EraseChildNode(CommandTable *table, CommandNode **out, CommandNode *node);

// A macro definition the parser holds: slot 2 counts it, slot 1 releases it.
struct CallDefinition {
    void *definition = nullptr;
    CallDefinition() = default;
    CallDefinition(const CallDefinition &other) : definition(other.definition)
    {
        if (definition) VirtualCall<uint32_t(__thiscall *)(void *), 2>(definition);
    }
    CallDefinition &operator=(const CallDefinition &) = delete;
    ~CallDefinition()
    {
        if (definition) VirtualCall<uint32_t(__thiscall *)(void *), 1>(definition);
    }
};

// An element of the parser's pending calls (+0x11c; 0x34 bytes; the create command's frames): a macro definition
// (held) and the arguments of the call. A copy holds the definition, then copies the arguments (cArguments' copy
// constructor); destruction destroys the arguments, then releases the definition. The vector is a
// std::vector<PendingCall> (no move: its growth, 0xafc6c0, copies the elements, then destroys the old ones).
struct PendingCall {
    CallDefinition held;  // +0
    cArguments arguments; // +4
};
static_assert(sizeof(PendingCall) == 0x34 && offsetof(PendingCall, arguments) == 4, "PendingCall layout");
using PendingCallVector = std::vector<PendingCall>;
static_assert(sizeof(PendingCallVector) == 0xc, "a std::vector");

// 0xafc470 (__cdecl (first, last, allocator)): destroys the pending calls: each one's arguments (cArguments'
// destructor), then its definition released.
void __cdecl DestroyPendingCalls(PendingCall *first, PendingCall *last, void *allocator);

class cBaseParser {
public:
    static constexpr uint32_t kVTable = 0xe7db24;           // primary (5 slots)
    static constexpr uint32_t kUnknownVTable = 0xe7db3c;    // the cIGZUnknown face (3 slots)
    static constexpr uint32_t kParserVTable = 0xe7db4c;     // the cIBaseParser face (51 slots)
    static constexpr uint32_t kVbTable = 0xe7dc18;          // {-8, 0x1a8, 0x1b0}
    static constexpr uint32_t kParserFaceVbTable = 0xe7dc24; // cIBaseParser's own: {-4, -12}
    // What the virtual bases' own constructors store when this is the most derived class.
    static constexpr uint32_t kIGZUnknownVTable = 0xd9b428;    // cIGZUnknown's (all _purecall)
    static constexpr uint32_t kIBaseParserVTable = 0xdb574c;   // cIBaseParser's (all _purecall)
    static constexpr uint32_t kIBaseParserUnknownVTable = 0xdb581c; // cIBaseParser's view of its cIGZUnknown
    static constexpr uint32_t kStateId = 0x8ba83acb;        // cState's type id at +4
    // The two virtual bases' offsets from the vbptr at +8 when this is the most derived class.
    static constexpr int32_t kUnknownOffset = 0x1a8, kParserOffset = 0x1b0;

    // Our own tables (cBaseParserTables.cpp, lane RQ), the slot order the image's: what an image-free program stores
    // (standalone/owned_statics.cpp aliases the owned statics below to them). The hooked game stores the image's, whose
    // entries are hooked to the same forwarders (gz_base_parser_tables checks slot for slot).
    static const void *const kOwnVTable[5];          // kVTable
    static const void *const kOwnUnknownVTable[3];   // kUnknownVTable (vtordisp forwarders)
    static const void *const kOwnParserVTable[51];   // kParserVTable (vtordisp forwarders)
    static const int32_t kOwnVbTable[3];             // kVbTable
    static const int32_t kOwnParserFaceVbTable[2];   // kParserFaceVbTable
    static const void *const kOwnPureUnknownVTable[3];  // kIGZUnknownVTable and kIBaseParserUnknownVTable: _purecall
    static const void *const kOwnPureParserVTable[51];  // kIBaseParserVTable: _purecall

    // The parser's condition stack entry (the if / elseif / else / endif commands): `level` is how many entries
    // the matching endif removes, `active` whether the current branch's text runs.
    struct Condition {
        int32_t level;
        uint8_t active;
        uint8_t elseSeen;
        uint8_t pad[2]; // left as stack bytes by the original's constructor; zero here
    };
    static_assert(sizeof(Condition) == 8, "Condition layout");

    // An element of the two lists at +0x19c and +0x1a4 (0x50 bytes; nodes of 0x58): a line being run (slot 15
    // takes one from the free list +0x1a4 to the front of the in-use list +0x19c, and gives it back after) and its
    // parsed arguments. The text is a cRZString (hook-shaped: built by hand, refcount 0); destruction destroys the
    // arguments, then the text (0xafe660).
    struct ListItem {
        cRZString text;       // +0
        cArguments arguments; // +0x20
        ListItem() { text.Construct(); }
        // The copy the free list's push_back makes: the text copied (refcount 0), then the arguments.
        ListItem(const ListItem &other) : text(), arguments((CopyText(other.text), other.arguments)) {}
        ListItem &operator=(const ListItem &) = delete;

    private:
        void CopyText(const cRZString &from)
        {
            text.mString.assign(from.mString.c_str(), from.mString.size());
            text.mVTable = cRZString::VTable();
            text.mRefCount = 0;
        }
    };

    // 0xafd800 (__thiscall, ret 4, returns this). mostDerived != 0: this is the whole object, so the vbptrs and
    // the virtual bases' own vtables are stored first. Then the vtables (primary and both faces), the vtordisps
    // (0 for the whole object, else the extra displacement the derived class's vbtable gives) and the members.
    cBaseParser *Construct(uint32_t mostDerived);

    // A new text source as the constructor and SetSource make one (operator new, then its constructor inline: our
    // cFileSource, src/gz/cFileSource.h). No reference taken.
    static cFileSource *NewFileSource();

    // The faces, found through the vbtable at +8 as the original's code finds them.
    void *UnknownFace() { return reinterpret_cast<char *>(this) + 8 + mVbTable[1]; }
    void *ParserFace() { return reinterpret_cast<char *>(this) + 8 + mVbTable[2]; }

    // ---- Virtual functions (hook shape: members with `this` = the whole parser; the game's vtables reach them
    // through the adapters below, which take each face's `this`). cParserSlots.cpp. ----

    // The cIGZUnknown face (+0x1b0).
    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0xb05940: 1 -> the cIGZUnknown face, 0x95035f4 -> cIBaseParser
    uint32_t AddRef();                              // slot 1, 0xaff100: the count at +0xc
    uint32_t Release();                             // slot 2, 0xb05c50: deletes through primary slot 0 at zero

    // The primary vtable (+0).
    void DropDefinitionsOfLevel(int32_t level);     // slot 4, 0xb05c80: removes the definitions whose +0x68 is `level`

    // The cIBaseParser face (+0x1b8).
    bool Shutdown();                                       // 1, 0xb06760
    void SetVersionRange(int32_t lowest, int32_t highest); // 2, 0xb06110
    void SetContextState(void *state);                     // 3, 0xb06230
    void RegisterCommand(void *command);                   // 4, 0xb059e0
    bool UnregisterCommand(const char *name);              // 5, 0xb072f0
    bool UnregisterCommandObject(void *command);           // 6, 0xb072b0: slot 5 with the command's name
    void RegisterMetaCommand(void *command);               // 7, 0xb05b50
    void RestartViaReset();                                // 8, 0xb05ea0: slot 9 of the face (a tail call)
    void Reset();                                          // 9, 0xaff120
    void ResetBlocks();                                    // 10, 0xb03e50
    void PushOpenBlock(void *block);                       // 16, 0xb068b0
    void PushMetaBlock(void *block);                       // 17, 0xb068f0
    void PopMetaBlock();                                   // 18, 0xaff880
    void RunExpandedText(uint32_t context, const char *text); // 19, 0xb06350
    void PushNamespace(const char *name);                  // 20, 0xb05770
    void PopNamespace();                                   // 21, 0xb055b0
    void PurgeNamespace(const char *name);                 // 22, 0xb05640
    EvalFunctionTable *GetFunctions();                     // 23, 0xb02440
    void SetOutput(void *output);                          // 24, 0xb06250
    void SetHandler(void *handler);                        // 25, 0xb06310
    void SetSource(void *source);                          // 26, 0xb06140
    const char *FindVariable(const char *name);            // 27, 0xb07520: the value's text, or null
    const char *GetNamespacePath();                        // 28, 0xaff4d0
    void ExpandVariables(void *text, bool parenthesize);   // 29, 0xb06fb0: $name and ${name} in a cIGZString
    bool EvalBool(const char *text);                       // 30, 0xb03f30
    float EvalFloat(const char *text);                     // 31, 0xb050c0
    int32_t EvalInt(const char *text);                     // 32 and 33, 0xb05120
    // Slots 34..39 return `out` (the lighting script's commands copy the values from the returned pointer).
    float *EvalVector2(float *out, const char *text, uint32_t minimum); // 34, 0xb052e0
    float *EvalVector3(float *out, const char *text, uint32_t minimum); // 35, 0xb05370
    float *EvalVector4(float *out, const char *text, uint32_t minimum); // 36, 0xb05410
    float *EvalColorRGBA(float *out, const char *text);      // 37, 0xb05190
    float *EvalColorARGB(float *out, const char *text);      // 38, 0xb05200: slot 37, alpha moved first
    float *EvalColorRGB(float *out, const char *text);       // 39, 0xb05260
    uint32_t CommandCount();                               // 40, 0xb03e20
    void *CommandAt(uint32_t index);                       // 41, 0xaff2e0
    uint8_t IsInitialised();                               // 42, 0xb035b0
    uint8_t IsCaseSensitive();                             // 43, 0xb035a0
    const char *GetText();                                 // 44, 0xb035c0
    const char *GetFileName();                             // 46, 0xb04680
    uint32_t GetLine();                                    // 47, 0xb04670
    bool HasDefinition(const char *name);                  // 48, 0xaff4f0
    bool HasCommand(const char *name);                     // 49, 0xaff300
    void *GetSource();                                     // 50, 0xb068a0

    // ---- The run path (cParserRun.cpp, lane IH). ----

    // 15, 0xb03fa0: runs one line. A line item moves from the free list (+0x1a4; one is made when it is empty) to the
    // front of the in-use list (+0x19c) for the run and back after it (also when the run throws). The text, without
    // its comments (AppendUncommented; as it is while a pending call is open), is skipped when empty or when the
    // innermost meta block (its slot 10, given the text and whether the current branch runs) declines it; otherwise
    // its variables are expanded (face slot 29), it is parsed (skipping the name, the parser's case flag) and the
    // command named by the first word runs: found by FindCommand ("Unknown command" when missing); +0xe4 = the
    // condition depth plus the command's slot 8; its slot 4 with the arguments; when its slot 7 says so, the
    // arguments' slot 15 (unused switches). Errors are caught: the blocks reset (face slot 10) and a cError thrown
    // naming the line (the cError's text, or "unknown exception type" for other cIGZExceptionBase errors).
    void RunLine(const char *text);
    // 0xb02450 (__thiscall, ret 8): appends `text` without its comments to *out; true when *out is not empty after.
    // Outside a block comment (+0xf4 is the "#<" ... "#>" depth) the text is trimmed of blanks first. A '#' not
    // followed by '<' or '>' ends the text; "#<" opens a block comment (the text before it appended when outside one),
    // "#>" closes one ("'#>' without matching '#<'" when none is open), the text going on after it at depth 0.
    bool AppendUncommented(const char *text, std::string *out);
    // 14, 0xb046b0 (text, size (unused), file name): runs a text line by line ("\r", "\n", "\r\n" and "\n\r" end a
    // line; the text is written to: each break becomes a 0) through face slot 15, with +0xe0 the line number and
    // +0xc8 the file name (restored after). An error from a line gets "file(line): error: " put before the parser's
    // text (+0x178; once: +0x198 set when it is rethrown) and is rethrown as a cError of that text, unless errors are
    // ignored (+0xe8): then the next line runs. After the text, the open block and meta block counts must be what
    // they were (cErrors otherwise; the file name is then not restored).
    void RunText(char *text, uint32_t size, const char *fileName);
    // 12, 0xb04490 (path, flags): runs a file of the parser's source (+0x1c): with flags bit 0 the source resolves
    // the path against the current file first (its slot 6); its slot 3 must accept the path ("Path type not
    // supported"), its slot 5 reads the file into a vector ("File not found" unless flags bit 1), whose text goes to
    // face slot 14.
    void RunFile(const char *path, uint32_t flags);
    // 13, 0xb04940 (pattern, flags): runs every file a wildcard path names: with flags bit 0 the source resolves the
    // pattern first (as slot 12). A directory scan of the pattern's drive and directory lists the files matching its
    // name and extension: the names (each run as directory + name), or with flags bit 2 the full paths of the
    // matches in that directory and the ones below it. Each runs through face slot 12 (flags without bit 0); an
    // error is collected ("Errors parsing files!\n" then, per failing file, "\n" path "\n\t" and the error's text or
    // "\n\tUnknown exception") and the next file runs. Collected errors are thrown as one cError afterwards.
    void RunFiles(const char *pattern, uint32_t flags);
    // 0, 0xb02880: once (+0x10): face slot 8 (restart), the case flag (+0x2c), no output (face slot 24), then the
    // 36 built-in commands registered through face slot 4 and the 6 evaluator functions through face slot 7. True
    // the first time.
    bool Initialise(uint8_t caseSensitive);
    // Primary slot 1, 0xb06450 (name, value): sets a variable. "a:b:name" goes through (and makes) the child
    // namespaces a and a:b of the innermost open namespace; the name is copied into the namespace's pool when it is
    // new. With +0xea set the assignment is written to the output first ('path' = 'value').
    void SetVariable(const char *name, const char *value);

    // 0xafe1b0 (__thiscall, `this` the cBaseParser part; derived parsers' destructors call it): the class's own
    // vtables and vtordisps again, Shutdown when initialised (on +0x1b8 as the original), then the members in
    // reverse order.
    void Destruct();
    // 0xaff070, primary slot 0: the destructor, then the object freed (0x1c0 bytes) when bit 0 of `flags` is set.
    cBaseParser *DeletingDestructor(uint32_t flags);

    // The parser from a face pointer the game's vtables hand over (the bodies are compiled for the whole-object offsets).
    static cBaseParser *FromParserFace(void *face) { return reinterpret_cast<cBaseParser *>(static_cast<char *>(face) - 0x1b8); }
    static cBaseParser *FromUnknownFace(void *face) { return reinterpret_cast<cBaseParser *>(static_cast<char *>(face) - 0x1b0); }

    // A std::vector the parser handles by hand (its elements' types are not modelled).
    struct RawVector {
        void *first, *last, *end;
    };

    const void *mVTable;                 // +0x000
    uint32_t mStateId;                   // +0x004 cState: kStateId
    const int32_t *mVbTable;             // +0x008
    uint32_t mRefCount;                  // +0x00c 0
    uint8_t mInitialised;                // +0x010 0: set once the commands are registered (slot 0), cleared by slot 1
    uint8_t mPad11[3];
    void *mOutput;                       // +0x014 0: where the parser's messages go (counted; slot 14 writes text)
    void *mHandler;                      // +0x018 0 (counted; slot 25)
    cIParserSource *mSource;             // +0x01c a new cFileSource, with one reference
    EvalFunctionTable mFunctions;        // +0x020 the evaluator's built-in names
    uint8_t mCaseSensitive;              // +0x02c not written by the constructor; 0: command names are folded
    uint8_t mPad2d[3];
    CommandTable mCommands;              // +0x030 the global commands
    NamePool mNames;                     // +0x050
    uint8_t mVersionRangeSet;            // +0x05c 0
    uint8_t mPad5d[3];
    int32_t mLowestVersion;              // +0x060 -1
    int32_t mHighestVersion;             // +0x064 -1
    void *mContextState;                 // +0x068 0: the state handed to commands (0: the parser's own cState at +4)
    RawVector mDefinitions;              // +0x06c the macro definitions (counted through their slot 1), sorted by name
    ParserNamespace mGlobalNamespace;    // +0x078
    std::string mFileName;               // +0x0c8
    uint32_t mLine;                      // +0x0e0 0
    uint32_t mFieldE4;                   // +0x0e4 not written by the constructor
    uint8_t mFlagE8;                     // +0x0e8 0 (the constructor clears the dword +0xe8..+0xeb)
    uint8_t mTraceNamespaces;            // +0x0e9 0: report entering / leaving namespaces
    uint8_t mFieldEA;                    // +0x0ea 0
    uint8_t mTraceVariables;             // +0x0eb 0: report variable lookups (slot 27)
    uint8_t mTrace;                      // +0x0ec 0: trace the commands run
    uint8_t mFieldED;                    // +0x0ed 0
    uint8_t mFieldEE;                    // +0x0ee 0
    uint8_t mFieldEF;                    // +0x0ef not written by the constructor
    int32_t mVersion;                    // +0x0f0 -1
    uint32_t mFieldF4;                   // +0x0f4 0
    std::vector<void *> mMetaBlocks;     // +0x0f8 the meta command parse blocks (slots 17 and 18)
    std::vector<void *> mOpenBlocks;     // +0x104 the block commands open, innermost last
    std::vector<Condition> mConditions;  // +0x110 one entry {0, active} after the constructor
    RawVector mPendingCalls;             // +0x11c elements of 0x34 bytes: a counted pointer, then an argument list
    std::vector<uint32_t> mNamespaceLengths; // +0x128 the length of mNamespacePath before each namespace opened
    std::string mNamespacePath;          // +0x134
    std::vector<ParserNamespace *> mNamespaces; // +0x14c the open namespaces, innermost last
    uint32_t mField158;                  // +0x158 0 (the value of primary slots 2 and 3)
    void *mCurrentDefinition;            // +0x15c 0 (counted through its slot 1)
    RefPtrVector mCommandList;           // +0x160 the registered commands, in order
    RefPtrVector mMetaCommands;          // +0x16c
    cRZString mText;                     // +0x178
    uint8_t mField198;                   // +0x198 0
    uint8_t mPad199[3];
    std::list<ListItem> mList19C;        // +0x19c
    std::list<ListItem> mList1A4;        // +0x1a4
    // The virtual bases of a cBaseParser that is the whole object:
    int32_t mUnknownVtorDisp;            // +0x1ac
    const void *mUnknownVTable;          // +0x1b0 cIGZUnknown
    int32_t mParserVtorDisp;             // +0x1b4
    const void *mParserFaceVTable;       // +0x1b8 cIBaseParser
    const int32_t *mParserFaceVbTable;   // +0x1bc
};

static_assert(offsetof(cBaseParser, mSource) == 0x1c && offsetof(cBaseParser, mFunctions) == 0x20 &&
                  offsetof(cBaseParser, mCommands) == 0x30 && offsetof(cBaseParser, mNames) == 0x50 &&
                  offsetof(cBaseParser, mContextState) == 0x68 && offsetof(cBaseParser, mDefinitions) == 0x6c &&
                  offsetof(cBaseParser, mGlobalNamespace) == 0x78 && offsetof(cBaseParser, mFileName) == 0xc8 &&
                  offsetof(cBaseParser, mLine) == 0xe0 && offsetof(cBaseParser, mTraceNamespaces) == 0xe9 &&
                  offsetof(cBaseParser, mTrace) == 0xec && offsetof(cBaseParser, mVersion) == 0xf0 &&
                  offsetof(cBaseParser, mMetaBlocks) == 0xf8 && offsetof(cBaseParser, mOpenBlocks) == 0x104 &&
                  offsetof(cBaseParser, mConditions) == 0x110 && offsetof(cBaseParser, mPendingCalls) == 0x11c &&
                  offsetof(cBaseParser, mNamespaceLengths) == 0x128 && offsetof(cBaseParser, mNamespacePath) == 0x134 &&
                  offsetof(cBaseParser, mNamespaces) == 0x14c && offsetof(cBaseParser, mField158) == 0x158 &&
                  offsetof(cBaseParser, mCurrentDefinition) == 0x15c && offsetof(cBaseParser, mCommandList) == 0x160 &&
                  offsetof(cBaseParser, mMetaCommands) == 0x16c && offsetof(cBaseParser, mText) == 0x178 &&
                  offsetof(cBaseParser, mList19C) == 0x19c && offsetof(cBaseParser, mList1A4) == 0x1a4 &&
                  offsetof(cBaseParser, mUnknownVTable) == 0x1b0 && offsetof(cBaseParser, mParserFaceVTable) == 0x1b8,
              "cBaseParser layout");
static_assert(sizeof(cBaseParser) == 0x1c0, "cBaseParser size");
static_assert(sizeof(cBaseParser::ListItem) == 0x50 && offsetof(cBaseParser::ListItem, arguments) == 0x20, "ListItem layout");

// The entry points the game's vtables (and direct calls) reach: `this` in ecx is the slot's face, as the original's
// bodies receive it (__fastcall with the dummy edx = __thiscall). Each forwards to the member with the whole object.
struct cBaseParserAdapters {
    static bool __fastcall QueryInterface(void *unknownFace, void *, uint32_t iid, void **out);
    static uint32_t __fastcall AddRef(void *unknownFace, void *);
    static uint32_t __fastcall Release(void *unknownFace, void *);
    static void __fastcall DropDefinitionsOfLevel(cBaseParser *self, void *, int32_t level);
    static bool __fastcall Shutdown(void *face, void *);
    static void __fastcall SetVersionRange(void *face, void *, int32_t lowest, int32_t highest);
    static void __fastcall SetContextState(void *face, void *, void *state);
    static void __fastcall RegisterCommand(void *face, void *, void *command);
    static bool __fastcall UnregisterCommand(void *face, void *, const char *name);
    static bool __fastcall UnregisterCommandObject(void *face, void *, void *command);
    static void __fastcall RegisterMetaCommand(void *face, void *, void *command);
    static void __fastcall RestartViaReset(void *face, void *);
    static void __fastcall Reset(void *face, void *);
    static void __fastcall ResetBlocks(void *face, void *);
    static void __fastcall PushOpenBlock(void *face, void *, void *block);
    static void __fastcall PushMetaBlock(void *face, void *, void *block);
    static void __fastcall PopMetaBlock(void *face, void *);
    static void __fastcall RunExpandedText(void *face, void *, uint32_t context, const char *text);
    static void __fastcall PushNamespace(void *face, void *, const char *name);
    static void __fastcall PopNamespace(void *face, void *);
    static void __fastcall PurgeNamespace(void *face, void *, const char *name);
    static EvalFunctionTable *__fastcall GetFunctions(void *face, void *);
    static void __fastcall SetOutput(void *face, void *, void *output);
    static void __fastcall SetHandler(void *face, void *, void *handler);
    static void __fastcall SetSource(void *face, void *, void *source);
    static const char *__fastcall FindVariable(void *face, void *, const char *name);
    static const char *__fastcall GetNamespacePath(void *face, void *);
    static void __fastcall ExpandVariables(void *face, void *, void *text, bool parenthesize);
    static bool __fastcall EvalBool(void *face, void *, const char *text);
    static float __fastcall EvalFloat(void *face, void *, const char *text);
    static int32_t __fastcall EvalInt(void *face, void *, const char *text);
    static float *__fastcall EvalVector2(void *face, void *, float *out, const char *text, uint32_t minimum);
    static float *__fastcall EvalVector3(void *face, void *, float *out, const char *text, uint32_t minimum);
    static float *__fastcall EvalVector4(void *face, void *, float *out, const char *text, uint32_t minimum);
    static float *__fastcall EvalColorRGBA(void *face, void *, float *out, const char *text);
    static float *__fastcall EvalColorARGB(void *face, void *, float *out, const char *text);
    static float *__fastcall EvalColorRGB(void *face, void *, float *out, const char *text);
    static uint32_t __fastcall CommandCount(void *face, void *);
    static void *__fastcall CommandAt(void *face, void *, uint32_t index);
    static uint8_t __fastcall IsInitialised(void *face, void *);
    static uint8_t __fastcall IsCaseSensitive(void *face, void *);
    static const char *__fastcall GetText(void *face, void *);
    static const char *__fastcall GetFileName(void *face, void *);
    static uint32_t __fastcall GetLine(void *face, void *);
    static bool __fastcall HasDefinition(void *face, void *, const char *name);
    static bool __fastcall HasCommand(void *face, void *, const char *name);
    static void *__fastcall GetSource(void *face, void *);
    static void __fastcall RunLine(void *face, void *, const char *text);
    static bool __fastcall AppendUncommented(cBaseParser *self, void *, const char *text, std::string *out);
    static void __fastcall RunText(void *face, void *, char *text, uint32_t size, const char *fileName);
    static void __fastcall RunFile(void *face, void *, const char *path, uint32_t flags);
    static void __fastcall RunFiles(void *face, void *, const char *pattern, uint32_t flags);
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive);
    static void __fastcall SetVariable(cBaseParser *self, void *, const char *name, const char *value);
    static VariableTable::InsertResult *__fastcall InsertVariable(VariableTable *table, void *, VariableTable::InsertResult *out,
                                                                 const KeyView *key);
    static void __fastcall InsertEvalFunction(EvalFunctionTable *functions, void *, const char *name, cEvalFunction *function);
    // Helpers the game calls __thiscall on objects that are not ours to give members to.
    static void __fastcall ResetNamePool(NamePool *pool, void *);
    static CommandNode **__fastcall EraseChildNode(CommandTable *table, void *, CommandNode **out, CommandNode *node);
    // 0xafce70: the child table's find-or-add, the same code as CommandTable::Insert (0xaf7560) but for its own
    // rehash 0xb07790, which is the same code as CommandTable::Rehash (0xaf7da0; only the registers differ).
    static InsertResult *__fastcall InsertChild(CommandTable *table, void *, InsertResult *out, const KeyView *key);
    static void __fastcall RehashChildren(CommandTable *table, void *, uint32_t n);
    // 0xafe000: the pending calls' vector destroyed (elements, then the storage; pointers null).
    static void __fastcall DestroyPendingCallVector(cBaseParser::RawVector *calls, void *);
    // Our own tables' entries for the slots with no adapter above (cBaseParserTables.cpp).
    static cBaseParser *__fastcall DeletingDestructor(cBaseParser *self, void *, uint32_t flags); // primary 0
    static void __fastcall SetField158(cBaseParser *self, void *, uint32_t value); // primary 2 (the folded 0x960960)
    static uint32_t __fastcall GetField158(cBaseParser *self, void *);            // primary 3 (the folded 0x5b1920)
    static void __fastcall NoOp(void *face, void *);         // face slot 11's body: the folded bare `ret` 0x140fe0
    static int32_t __fastcall GetVersion(void *face, void *); // face slot 45's body: the folded 0x9d6630 (+0xf0)
};

// 0xb09550 (__thiscall on the evaluator's function table, ret 8): puts (name, function) into the table at its sorted
// place: before the first entry whose name is not below it by strcmp (a full vector grows through 0x14a800). Only
// RegisterMetaCommand calls it.
void InsertEvalFunction(EvalFunctionTable *functions, const char *name, cEvalFunction *function);

// 0xafcc10 (__cdecl): copies the counted pointers [first, last) over the ones at `dest` and returns the end of what
// was written. The pointers are macro definitions, which count with slot 2 and release with slot 1: each assignment
// counts the new one and releases the old one, skipping equal ones and an element assigned to itself. The undefine
// command (cUndefineCommand) and primary slot 4 use it to close the gap left by removed definitions.
void **__cdecl MoveCountedPointers(void **first, void **last, void **dest);

} // namespace nGZCommandParser

// cBaseParser's tables, owned statics (game/owned.h): the image's in the hooked game, ours (the kOwn* above) in an
// image-free program.
TS2_OWNED_STATIC(const OwnedVTable, e7db24);  // kVTable
TS2_OWNED_STATIC(const OwnedVTable, e7db3c);  // kUnknownVTable
TS2_OWNED_STATIC(const OwnedVTable, e7db4c);  // kParserVTable
TS2_OWNED_STATIC(const int32_t, e7dc18)[3];   // kVbTable
TS2_OWNED_STATIC(const int32_t, e7dc24)[2];   // kParserFaceVbTable
TS2_OWNED_STATIC(const OwnedVTable, d9b428);  // kIGZUnknownVTable
TS2_OWNED_STATIC(const OwnedVTable, db574c);  // kIBaseParserVTable
TS2_OWNED_STATIC(const OwnedVTable, db581c);  // kIBaseParserUnknownVTable
