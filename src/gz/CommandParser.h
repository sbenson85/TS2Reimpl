#pragma once
#include "gz/CommandParserTables.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The base of the game's exceptions: only the vtable (0xda5060 for cError). Slot 0 gives the message.
// Named as in the game, so that code catching it by this type (the match is by decorated name) is
// reached by exceptions thrown from our code.
class cIGZExceptionBase {
public:
    const void *mVTable; // +0
};

// nGZCommandParser: the game's command/script parser. This header holds the command classes
// (cICommand -> cCommand -> cBlockCommand / cMetaCommand, and the commands derived from them); RVAs
// are 1.18.0.187.
//
// A command is created by the registration function at 0xb02880 (all constructors are inlined
// there), then driven through its vtable by the parser: SetContext gives it the parser and the
// parser's state, Execute runs it with the parsed arguments. Every Execute does the same first
// thing: it calls slot 15 of the argument list, then asks slot 11 of the argument list for the
// arguments it needs (a pointer to an array of that many strings).
//
// Calls through other objects (the argument list, the parser, the parser state and its output
// object) go through their vtables with VirtualSlot, as the original does; the parser state is
// described only by the offsets the commands use.

namespace nGZCommandParser {

// ---- cError: what the parser throws ---------------------------------------------------------------
//
// 0x1c bytes: the game's vtable (0xda5060, one slot: the message as a C string, 0x1acd4f), then the
// message as a std::string at +4. The class is not polymorphic here: the vtable pointer is a plain
// field, so that the object keeps the layout the game's code reads, and the compiler's own type
// information for `throw cError` has the same names and sizes as the game's (".?AVcError@
// nGZCommandParser@@" of size 0x1c, with base ".?AVcIGZExceptionBase@@" of size 4).
class cError : public cIGZExceptionBase {
public:
    static constexpr uint32_t kVTable = 0xda5060;

    std::string mMessage; // +4
};

static_assert(sizeof(cError) == 0x1c && sizeof(cIGZExceptionBase) == 4, "cError layout");

// Builds a cError from a format string and its arguments (32-bit values: integers or pointers) and
// throws it. The object is made by the game's constructor (0x1d099e, RZFormattedErrorCtor in
// rz/RZFrameworkHelpers.h, verified there) in raw memory, and then moved into a cError, which is the
// type the compiler throws.
template <class... Args> [[noreturn]] void ThrowFormatted(const char *fmt, Args... args)
{
    alignas(cError) unsigned char raw[sizeof(cError)];
    RZFormattedErrorCtor(raw, fmt, args...);
    cError error(std::move(*reinterpret_cast<cError *>(raw)));
    reinterpret_cast<cError *>(raw)->mMessage.~basic_string();
    // The constructor stores the image's table; the error's table is the owned static (the same table in the hooked
    // game, ours in an image-free program, whose slot 0 is what a handler reads the message through).
    error.mVTable = &ts2_rva_da5060;
    throw error;
}

// 0xace7d0 (__cdecl, varargs): does nothing when `ok` is true; otherwise formats the message into
// the game's buffer 0x10f6bc8 and throws a cError built from it. ORIGINAL-BUG: the formatted text is
// used as the format string of the cError constructor, with no arguments, so a '%' in an argument
// (a file name, say) is read as a conversion again and takes its value from the stack: the four
// words the original left there (the buffer address, 0x800, the format string and the argument
// pointer), then the half-built error object. The same words are supplied here. As in the
// constructor, a message of 2048 characters or more is not terminated and the length runs on past
// the buffer.
void __cdecl ThrowUnless(bool ok, const char *fmt, ...);

// 0xafc340 (__cdecl, varargs): ThrowUnless for a pointer: does nothing when `p` is not null (the whole word is
// tested); otherwise the same as ThrowUnless with its own buffer, 0x10fa248 (the same ORIGINAL-BUG: the formatted
// text is the error's format string, reading the same four words and the half-built error object).
void __cdecl ThrowIfNull(const void *p, const char *fmt, ...);

// ---- number formatting used by the set-float and set-vector commands ------------------------------
//
// All three write into a caller's buffer and report in al whether the text fitted.

// 0x1b111e (__cdecl): the digits of `value` as plain decimal text (no exponent), with at most
// `precision` digits after the decimal point and trailing zeros dropped, into buf[0..size). Fails
// (false) when size <= 0 or the text does not fit: then a NUL is written at the last position
// that was used (or at 0). Uses _fcvt for 64 digits after the point.
bool __cdecl FormatDecimal(double value, char *buf, int size, int precision);

// 0x1b128f (__cdecl): like printf's %g: plain decimal text when the decimal exponent is from -4 up
// to precision - 1, otherwise a mantissa from FormatDecimal and "e", a sign and the exponent.
// Quirk (not reachable with the buffers the commands use, so not an ORIGINAL-BUG): the room left for the exponent
// counts the digits of a negative exponent as one, so with a tiny `size` the exponent text can run
// past the buffer.
bool __cdecl FormatGeneral(double value, char *buf, int size, int precision);

// 0xb02740 (__cdecl, varargs of doubles): copies `fmt` into out[0..size), replacing each '%' and
// the character after it by the next double formatted with FormatGeneral(.., 6). False when the
// output does not fit (then no terminator is written); true with the terminator otherwise.
// Quirk (a valid caller does not do this): a '%' as the last character of fmt makes the format run on past its terminator.
bool __cdecl PrintDoubles(char *out, int size, const char *fmt, ...);

// ---- name tables ---------------------------------------------------------------------------------
//
// The parser keeps its enumerations (the log levels, the render states of the shader commands, ...)
// as a sorted vector of (name, value) entries, 0x1c bytes each: a std::string and an int. Names are
// ordered by length first, then by their bytes compared from the last one backwards (as signed
// chars). Looking a name up is an equal_range over that order. 86 callers.

struct EnumEntry {
    std::string name; // +0
    int32_t value;    // +0x18
};
static_assert(sizeof(EnumEntry) == 0x1c, "EnumEntry layout");

// A name as the end and the start of its characters (end first, as the game's comparison objects
// keep it).
struct KeyRange {
    const char *end;
    const char *begin;

    // 0xaf6880 (__thiscall, ret 8; the second argument is not used): true when the entry sorts
    // before the key.
    bool EntryBeforeKey(const EnumEntry *entry, uint32_t unused) const;
    // 0xaf68f0 (__thiscall, ret 8; the FIRST argument is not used, the entry is the second): true when the key sorts before
    // the entry.
    bool KeyBeforeEntry(uint32_t unused, const EnumEntry *entry) const;
};

struct EntryRange {
    const EnumEntry *first;
    const EnumEntry *last;
};

// 0xaf6300 (__cdecl, 6 stack words): std::equal_range of the key over [first, last); the range
// goes to *out. `keyWord` points at a word that the original passes along to the comparisons (the
// start of the name); it is not used. The key arrives by value (its two words are arguments 5 and 6).
void __cdecl FindEnumRange(EntryRange *out, const EnumEntry *first, const EnumEntry *last, const void *keyWord, KeyRange key);

struct EnumTable {
    std::vector<EnumEntry> entries;

    // 0xaf6df0 (__thiscall, ret 4): the value of `name`; throws a cError "Unknown enum '%s'" when
    // the table has no such entry.
    int32_t Lookup(const char *name) const;
};

// ---- the parser's command lookup -----------------------------------------------------------------

// 0xaf6b80 (__cdecl): the first token of `text`: skips the leading separators and measures the token,
// both against the separator string the game keeps at RVA 0x107479c (a std::string). The length goes
// to *length; the result is the start of the token, or null when there is none.
const char *__cdecl NextToken(uint32_t *length, const char *text);

// The parser's state, seen only through the offsets the lookup uses: the stack of open blocks (a
// vector of block commands: begin +0x104, end +0x108), the flag at +0x2c (clear: names are case
// folded) and the table of commands at +0x30.
class cParserState {
public:
    // 0xaff340 (__thiscall, ret 8): finds a command by name: first in the open blocks, innermost
    // first (slot 11 of each), then in the table. Returns null when nobody has it; the command is
    // AddRef'd and released again, as in the block's own lookup. A folding table does not find
    // names of 0x80 bytes or more.
    void *FindCommand(const char *name, uint32_t length);
};

class cIArguments; // the parsed arguments of a command line (reached through its vtable only)
class cIBaseParser; // the parser a command runs in (reached through its vtable only)

// ---- cCommand: the base of the commands that act on a parser --------------------------------------
//
// Layout (12 bytes): vtable, the parser (set by SetContext), the reference count. Vtable 0xe7d72c
// has 10 slots:
//   0 QueryInterface   1 AddRef   2 Release   3 SetContext   4 Execute (pure)   5 GetName (pure)
//   6 GetHelp          7 -> false  8 -> 0      9 deleting destructor
class cCommand {
public:
    static constexpr uint32_t kVTable = 0xe7d72c;

    cCommand *Construct();            // 0xaf7820: vtable, parser and count zeroed
    void Destruct();                  // 0xaf7990: sets the cCommand vtable
    cCommand *DeletingDestructor(uint32_t flags); // slot 9 of the cCommand vtable, 0xaf7a00 (size 12)

    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0xaf7c30
    uint32_t AddRef();                // slot 1, 0x145260 (shared by 680 classes)
    uint32_t Release();               // slot 2, 0x1d0be0 (shared); deletes through slot 9
    void SetContext(void *parser, void *state); // slot 3 of cCommand, 0x1d0b40 (shared): stores the parser
    uint32_t GetHelpDefault(uint32_t index);    // slot 6 of cCommand, 0x12e40 (shared): returns 0
    bool ReturnsFalse();              // slot 7, 0x147480 (shared)
    uint32_t ReturnsZero();           // slot 8, 0x6c6b0 (shared)

    const void *mVTable;              // +0
    void *mParser;                    // +4  cIBaseParser *
    int32_t mRefCount;                // +8
};

// A cCommand that also keeps the parser state: 16 bytes. This is nGZCommandParser::cCommandT<State>
// (one instantiation per state type); the commands below derive from it.
class cCommandT : public cCommand {
public:
    // Slot 3, 0x24c810: stores the parser, then the state with 4 subtracted (null stays null):
    // the state arrives as a pointer to its interface subobject, 4 bytes into the state object.
    void SetContextAdjusted(void *parser, void *stateInterface);
    // Slot 3 of cAliasStubCommand, 0x369510: stores the parser, then the state as given.
    void SetContextExact(void *parser, void *state);
    // Slot 9, 0x247a60: runs the cCommand destructor, deletes 16 bytes when flags & 1.
    cCommandT *DeletingDestructor(uint32_t flags);

    void *mState;                     // +0xc  the parser state
};

// ---- the plain commands (cCommandT-based, 16 bytes each) -----------------------------------------
//
// Each has: Execute (slot 4), GetName (slot 5: the command's name in the game's string data) and
// GetHelp (slot 6: its help text, whatever the index). The string getters ignore `this`.

#define NGZ_COMMAND_CLASS(cls)                                                                          \
    class cls : public cCommandT {                                                                      \
    public:                                                                                             \
        void Execute(cIArguments *args);                                                                \
        const char *GetName() const;                                                                    \
        const char *GetHelp(uint32_t index) const;                                                      \
    }

NGZ_COMMAND_CLASS(cArrayCreateCommand);
NGZ_COMMAND_CLASS(cAssertCommand);
NGZ_COMMAND_CLASS(cCreateCommand);
NGZ_COMMAND_CLASS(cDebugParserCommand);
NGZ_COMMAND_CLASS(cDebuggerBreakCommand);
NGZ_COMMAND_CLASS(cDumpParserStateCommand);
NGZ_COMMAND_CLASS(cElseIfCommand);
NGZ_COMMAND_CLASS(cEndDefCommand);
NGZ_COMMAND_CLASS(cEvalCommand);
NGZ_COMMAND_CLASS(cIgnoreErrorsCommand);
NGZ_COMMAND_CLASS(cIncludeCommand);
NGZ_COMMAND_CLASS(cListCommandsCommand);
NGZ_COMMAND_CLASS(cLogCommand);
NGZ_COMMAND_CLASS(cPurgeCommand);
NGZ_COMMAND_CLASS(cSetBCommand);
NGZ_COMMAND_CLASS(cSetCCommand);
NGZ_COMMAND_CLASS(cSetCommand);
NGZ_COMMAND_CLASS(cSetECommand);
NGZ_COMMAND_CLASS(cSetFCommand);
NGZ_COMMAND_CLASS(cSetV2Command);
NGZ_COMMAND_CLASS(cSetV3Command);
NGZ_COMMAND_CLASS(cSetV4Command);
NGZ_COMMAND_CLASS(cSetICommand);
NGZ_COMMAND_CLASS(cShowArgumentsCommand);
NGZ_COMMAND_CLASS(cSincludeCommand);
NGZ_COMMAND_CLASS(cTraceCommand);
NGZ_COMMAND_CLASS(cUndefineCommand);
NGZ_COMMAND_CLASS(cVersionCommand);
NGZ_COMMAND_CLASS(cWildIncludeCommand);

// The alias command creates stubs: a stub is a command that runs another command line with its own
// arguments appended. It keeps four strings (0x70 bytes in all). The state is stored as given.
class cAliasStubCommand : public cCommandT {
public:
    static constexpr uint32_t kVTable = 0xe7ed4c;

    void Execute(cIArguments *args);             // slot 4, 0xaffb80
    const char *GetName() const;                 // slot 5, 0xb03b80 (shared): the alias name as a C string
    const char *GetHelp(uint32_t index) const;   // slot 6, 0xaff5c0: 0 the short help, 1 the long help, else null
    void Destruct();                             // 0xafe080: the four strings, then cCommand::Destruct
    cAliasStubCommand *DeletingDestructor(uint32_t flags); // slot 9, 0xaff050 (size 0x70)

    std::string mName;      // +0x10
    std::string mTarget;    // +0x28 the command line to run
    std::string mShortHelp; // +0x40
    std::string mHelp;      // +0x58
};

class cAliasCommand : public cCommandT {
public:
    void Execute(cIArguments *args);             // slot 4, 0xaff8b0
    const char *GetName() const;                 // slot 5, 0xb03b70
    const char *GetHelp(uint32_t index) const;   // slot 6, 0xaff590: 0 and 1 give two help texts, else null
};

// cElseCommand and cEndIfCommand also answer slot 8 (cElse: -1; cEndIf: the negated word before the
// state's block stack end); cEndCommand and cAliasCommand are below.
class cElseCommand : public cCommandT {
public:
    void Execute(cIArguments *args);
    const char *GetName() const;
    const char *GetHelp(uint32_t index) const;
    int32_t ReturnsMinusOne(); // slot 8, 0x1b86e0 (shared with cElseIfCommand)
};

class cEndIfCommand : public cCommandT {
public:
    void Execute(cIArguments *args);
    const char *GetName() const;
    const char *GetHelp(uint32_t index) const;
    int32_t NegatedBlockWord(); // slot 8, 0xb02860
};

class cEndCommand : public cCommandT {
public:
    void Execute(cIArguments *args);
    const char *GetName() const;
    const char *GetHelp(uint32_t index) const;
};


// ---- cMetaCommand: commands that run while a conditional or definition is being collected ----------
//
// Layout (8 bytes): vtable (0xe7d790, 12 slots), the reference count at +4. The slots are the
// cCommand ones with the count at +4, and no parser: SetContext does nothing. Slot 11 is the
// deleting destructor (size 8).
class cMetaCommand {
public:
    static constexpr uint32_t kVTable = 0xe7d790;

    cMetaCommand *Construct();        // 0xaf7860: the vtable and a zero count
    void Destruct();                  // 0xaf79b0: sets the cMetaCommand vtable
    cMetaCommand *DeletingDestructor(uint32_t flags); // slot 11 of the cMetaCommand vtable, 0xaf7a60 (size 8)

    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0xaf7ca0
    uint32_t AddRef();                // slot 1, 0x148fb0 (shared)
    uint32_t Release();               // slot 2, 0x153f70 (shared); deletes through slot 11
    void SetContext(void *parser, void *state); // slot 3, 0x1b9a50 (shared): does nothing

    const void *mVTable;              // +0
    int32_t mRefCount;                // +4
};

// A cMetaCommand that keeps the parser state: 12 bytes (cMetaCommandT<State>).
class cMetaCommandT : public cMetaCommand {
public:
    // Slot 3, 0x24c860: the state arrives as a pointer 4 bytes into the state object; stores it
    // minus 4 (null stays null).
    void SetContextAdjusted(void *parser, void *stateInterface);
    // Slot 11, 0xacfdc0: runs the cMetaCommand destructor, deletes 12 bytes when flags & 1.
    cMetaCommandT *DeletingDestructor(uint32_t flags);
    // Slot 9, 0xb02350 (ret 4, shared by cDefineCommand and cIfCommand; the argument is not used):
    // calls slot 17 of the state's virtual base with this command.
    uint32_t RegisterInScope(void *unused);

    void *mState;                     // +8
};

// ---- cExpressionFunction: the base of the evaluator's functions ------------------------------------
//
// Layout (12 bytes): cFunction's vtable (0xe7d7c4), the cIExpressionFunction face (vtable 0xe7d7d0; the face is what
// the evaluator holds), the reference count. The parser's functions (cFunctionT<cBaseParser>, 0x10 bytes: the
// state at +0xc, set when the function is registered) derive from it.
class cExpressionFunction {
public:
    static constexpr uint32_t kVTable = 0xe7d7c4;
    static constexpr uint32_t kFaceVTable = 0xe7d7d0;

    cExpressionFunction *Construct(); // 0xaf7840: both vtables and a zero count (the state word is not written)
    void Destruct();                  // 0xaf79a0: both vtables (the deleting destructors' body)

    const void *mVTable;              // +0
    const void *mFaceVTable;          // +4
    int32_t mRefCount;                // +8
};

class cDefineCommand : public cMetaCommandT {
public:
    void Execute(cIArguments *args);  // slot 4, 0xb005e0 (gz/MacroDefinition.cpp)
    bool HandleLine(void *line, uint8_t running); // slot 10, 0xb038f0 (gz/MacroDefinition.cpp; `line` a cIGZString)
    const char *GetName() const;      // slot 5, 0xb03bf0
    const char *GetHelp(uint32_t index) const; // slot 6, 0xaff650
};

class cIfCommand : public cMetaCommandT {
public:
    // Slot 10, 0xb03a20 (ret 8; the second argument is not used): called with each line collected
    // while the branch is not being run. True when the branch is active (the line is run), or the
    // line starts with if, elseif, else or endif (the conditional commands run); otherwise a command
    // found by the line's first word that answers the meta interface (id 0xcae872f1) is told, with
    // the rest of the line (its slot 9), and the result is false. `line` gives the text by its slot 5.
    bool HandleLine(void *line, uint32_t unused);
    void Execute(cIArguments *args);  // slot 4, 0xb016a0
    const char *GetName() const;      // slot 5, 0xb03c80
    const char *GetHelp(uint32_t index) const; // slot 6, 0xaff6e0
};


// ---- cBlockCommand: commands that collect a block of commands --------------------------------------
//
// Layout (0x40 bytes): vtable (0xe7d758, 13 slots), the parser, the count at +8, a value at +0xc,
// two tables (+0x10 and +0x30), and a flag byte at +0x3c. Named cBlockCommandBase here because
// rz/TopCallersA.h already has a (partial) cBlockCommand in the same namespace; its slot 3 (0xaf7bd0:
// stores the value and the object and queries the object's slot 43) lives there.
//
// The block keeps the commands defined inside it in a hash table by name (+0x10), with the names
// copied into a pool of 4 KB chunks (+0x30). When the flag at +0x3c is clear, names are case-folded
// (the flag is the answer to the parser's slot 43 query made by slot 3).

// A command in the table: a node of a circular doubly linked list (0x14 bytes). `key` points at the
// name's characters (in the pool, or in the command itself when the block is case sensitive).
struct CommandNode {
    CommandNode *next;   // +0
    CommandNode *prev;   // +4
    const char *key;     // +8
    uint32_t keyLength;  // +0xc
    void *value;         // +0x10 the command (cICommand *): AddRef'd while it is in the table
};
static_assert(sizeof(CommandNode) == 0x14, "CommandNode layout");

struct KeyView {
    const char *ptr;
    uint32_t length;
};

struct FindResult {
    CommandNode *position; // where an insert goes (before this node), or the node after a match
    CommandNode *found;    // the matching node, or null
};

struct InsertResult {
    CommandNode *node;
    uint8_t inserted; // false when the key was already there
};

// The hash of a name: the sum of its bytes (as signed chars) in 32 bits, adding the carry back in.
uint32_t HashName(const char *name, uint32_t length);

// A hash table of commands, the layout of the standard library's hash table of the time (0x20
// bytes): the list of nodes (a sentinel `head`), the bucket vector (two list pointers per bucket:
// the first and the last node of the bucket, both `head` when it is empty), and the sizes.
struct CommandTable {
    float maxLoad;                      // +0 (1.0)
    CommandNode *head;                  // +4 sentinel of the circular node list
    uint32_t size;                      // +8 number of nodes
    std::vector<CommandNode *> buckets; // +0xc
    uint32_t mask;                      // +0x18 bucketCount - 1
    uint32_t bucketCount;               // +0x1c a power of two

    // 0xaf7420 (__thiscall, ret 0xc): looks for `key` (whose hash is `hash`) in its bucket. Returns `out` (game
    // callers read eax, e.g. 0xaf76d5).
    FindResult *Find(FindResult *out, const KeyView *key, uint32_t hash) const;
    // 0xaf7560 (__thiscall, ret 8): finds the key or adds a node for it (the value null), growing
    // the table first when the load would pass maxLoad. Returns `out`.
    InsertResult *Insert(InsertResult *out, const KeyView *key);
    // 0xaf7da0 (__thiscall, ret 4): rebuilds the buckets for at least n of them (a power of two),
    // regrouping nodes with equal keys.
    void Rehash(uint32_t n);
    // 0xaf7870: frees the bucket vector, releases every command, and frees the sentinel.
    void Destruct();
    // 0xaf74e0 (__cdecl, two arguments, the first not used): releases the command of every node after
    // the sentinel `head`, and frees the nodes.
    static void __cdecl ClearNodes(void *unused, CommandNode *head);
};
static_assert(sizeof(CommandTable) == 0x20, "CommandTable layout");

// The pool the names are copied into: a circular list of chunk records (0xc bytes: next, prev, the
// chunk's 4 KB block), the number of chunks, and how much of the last block is used. A new pool
// starts with `used` at 0x1000, so that the first copy takes a block.
struct NamePool {
    struct Chunk {
        Chunk *next;
        Chunk *prev;
        uint8_t *block;
    };
    Chunk *head;    // +0 sentinel
    uint32_t count; // +4
    uint32_t used;  // +8

    NamePool *Construct();                              // 0xaf66b0
    void Destruct();                                    // 0xaf6770
    // 0xaf69b0 (__thiscall, ret 8): copies key into the pool and gives the copy in *out. A key
    // longer than 0x1000 bytes overruns the block, and an empty key (as the first one) gives a pointer
    // made from an uninitialised word: names are short and not empty.
    KeyView *Store(KeyView *out, const KeyView *key);
};
static_assert(sizeof(NamePool) == 0xc, "NamePool layout");

// 0xaf6be0 (__cdecl): clears bit 5 of every character of the name (folds a-z to A-Z).
void __cdecl FoldName(KeyView *key);

class cBlockCommandBase {
public:
    static constexpr uint32_t kVTable = 0xe7d758;

    cBlockCommandBase *Construct();   // 0xaf7750: vtable, zeros, an empty table (maxLoad 1.0, 8 buckets) and pool
    bool QueryInterface(uint32_t iid, void **out); // slot 0, 0xaf7c00
    uint32_t AddRef();                // slot 1, 0x145260 (the same function as cCommand::AddRef)
    uint32_t Release();               // slot 2, 0x313300 (shared); deletes through slot 12
    void EndBlock();                  // slot 9, 0x140fe0 (shared): does nothing
    // Slot 10, 0xaf7cd0: puts a command in the table under its name (slot 5 of the command), folding
    // the name when the block is case insensitive, takes a reference on it (releasing the one it
    // replaces), and gives it this block's parser and value (its slot 3).
    void AddCommand(void *command);
    // Slot 11, 0xaf7a90 (ret 8): the command called `name`, or null. Names of 0x80 bytes or more are
    // not found in a case insensitive block. The command is AddRef'd and released again.
    void *FindCommand(const char *name, uint32_t length);
    void Destruct();                  // 0xaf7970: the vtable, then the pool and the table
    cBlockCommandBase *DeletingDestructor(uint32_t flags); // slot 12, 0xaf79c0 (size 0x40)

    const void *mVTable;              // +0
    void *mParser;                    // +4
    int32_t mRefCount;                // +8
    uint32_t mValue;                  // +0xc
    CommandTable mCommands;           // +0x10
    NamePool mNames;                  // +0x30
    uint8_t mFlag;                    // +0x3c
    uint8_t mPad[3];
};

// A cBlockCommand that keeps the parser state: 0x44 bytes (cBlockCommandT<State>).
class cBlockCommandT : public cBlockCommandBase {
public:
    // Slot 3, 0x2c3c40: runs cBlockCommand's slot 3 (0xaf7bd0), then stores the state with 4
    // subtracted (null stays null).
    void SetContextAdjusted(void *object, void *stateInterface);
    // Slot 12 of the derived classes, 0x2c2160 (shared_vf): the block's destructor body (0xaf7970),
    // then deletes 0x44 bytes when flags & 1.
    cBlockCommandT *DeletingDestructor(uint32_t flags);

    void *mState;                     // +0x40
};

class cNamespaceCommand : public cBlockCommandT {
public:
    uint32_t EndBlock();              // slot 9, 0xb03e30: calls slot 21 of the state's virtual base
    void Execute(cIArguments *args);  // slot 4, 0xb01910
    const char *GetName() const;      // slot 5, 0xb03d00
    const char *GetHelp(uint32_t index) const; // slot 6, 0xaff760
};

static_assert(sizeof(cBlockCommandBase) == 0x40 && sizeof(cBlockCommandT) == 0x44, "cBlockCommand layout");

static_assert(sizeof(cCommand) == 12, "cCommand layout");
static_assert(sizeof(cCommandT) == 16, "cCommandT layout");
static_assert(sizeof(cEvalCommand) == 16, "command layout");
static_assert(sizeof(cAliasStubCommand) == 0x70 && offsetof(cAliasStubCommand, mHelp) == 0x58, "alias stub layout");

} // namespace nGZCommandParser

// The commands' data (lane RS; defined in standalone/owned_statics.cpp for image-free programs): the throw buffers, the
// token separators, the block keywords' lengths and the log levels.
TS2_OWNED_STATIC(char, 10f6bc8)[0x800];   // the throw helpers' message buffers (2 KB each)
TS2_OWNED_STATIC(char, 10fa248)[0x800];
TS2_OWNED_STATIC(char, 10f9a48)[0x800];   // ThrowUnlessRecording's
TS2_OWNED_STATIC(std::string, 107479c);   // space, tab, newline, return: the token separators
TS2_OWNED_STATIC(uint32_t, 10748f4);      // 2, the length of "if"
TS2_OWNED_STATIC(uint32_t, 1074994);      // 6, "elseif"
TS2_OWNED_STATIC(uint32_t, 1074998);      // 4, "else"
TS2_OWNED_STATIC(uint32_t, 107499c);      // 5, "endif"
TS2_OWNED_STATIC(nGZCommandParser::EnumTable, 10f9a30); // the log levels: always 4, error 2, warning 3, info 5, debug 6
