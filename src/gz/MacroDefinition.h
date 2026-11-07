#pragma once
// The command parser's macro definitions (lane NV): the definition object `define` records (cMacroDefinition), the
// parser's sorted vector of held definitions and its searches, and the parameter substitution `create` runs on each
// line. RVAs are 1.18.0.187.
//
// A script defines a macro with
//     define name "param1 param2 ..."
//         lines, where &param1 or &{param1} stands for the call's argument
//     enddef
// and runs it with `create name "arg1 arg2 ..."` (gz/CommandParser.cpp). While a definition records, cDefineCommand is
// the innermost meta block (face slot 17): every line goes to its HandleLine, which appends the line to the body until
// the line starting with "enddef" runs the enddef command (which files the definition, gz/CommandParser.cpp).
//
// Definitions store the owned static ts2_rva_e7db0c (the deleting destructor, then cRZRefCount's Release, AddRef,
// RemoveRef and count): the image's table in the hooked game (slot 0 is hooked here, slot 1 is cRZRefCount's, slots 2-4
// are folded bodies shared with other classes), ours (ts2_own_vt_e7db0c, MacroDefinition.cpp; lane RU) in image-free
// programs, where slots 2-4 are the members below.
#include "gz/CommandParser.h"
#include "gz/cArguments.h"
#include "game/owned.h"
#include "rz/cRZRefCount.h"

#include <cstdint>
#include <string>
#include <vector>

class cIGZString;

namespace nGZCommandParser {

class cBaseParser;

// A recorded line of a definition: the line number it came from (the parser's +0xe0 then) and the condition depth
// create last ran it at (-1000 until it has run).
struct MacroLine {
    static constexpr int32_t kNotRun = -1000;
    int32_t number; // +0
    int32_t depth;  // +4
};
static_assert(sizeof(MacroLine) == 8, "MacroLine layout");

// nGZCommandParser::cBaseParser::cMacroDefinition (0x88 bytes, a cRZRefCount).
class cMacroDefinition : public cRZRefCount {
public:
    static constexpr uint32_t kVTable = 0xe7db0c;
    static constexpr uint32_t kSize = 0x88;

    // What define's inline constructor builds: the game's vtable, count 0, empty strings, vectors and parameter list.
    // The level and the first line are not set (define sets them).
    cMacroDefinition();
    cMacroDefinition(const cMacroDefinition &) = delete;
    cMacroDefinition &operator=(const cMacroDefinition &) = delete;

    // 0xafe6c0: the members in reverse order, then cRZRefCount's destructor (its vtable stored). Hook shape: the
    // object is only ever destroyed through here (slot 0), never by a C++ delete.
    void Destruct();
    cMacroDefinition *DeletingDestructor(uint32_t flags); // slot 0, 0xaff0d0 (size 0x88)
    // Slots 2-4 of our table: what the image's folded bodies do (each shared with other classes, never hooked).
    uint32_t AddRef();          // slot 2, 0x148fb0: the count plus one, returned
    uint32_t RemoveRef();       // slot 3, 0x1b9538: the count minus one unless it is 0, returned; never deletes
    uint32_t RefCount() const;  // slot 4, 0x147a30: the count

    std::string mName;             // +0x08
    cArguments mParameters;        // +0x20 the parameter names (define parses them case-sensitively)
    std::vector<char> mBody;       // +0x50 the lines, each with its terminating zero
    std::vector<MacroLine> mLines; // +0x5c one per line of the body
    int32_t mLevel;                // +0x68 the parser's +0x158 when it was defined (face slot 4 drops a level's)
    std::string mFile;             // +0x6c the file it was defined in
    int32_t mFirstLine;            // +0x84 the line of the define command (0: not from a file)
};
static_assert(offsetof(cMacroDefinition, mName) == 0x08 && offsetof(cMacroDefinition, mParameters) == 0x20 &&
                  offsetof(cMacroDefinition, mBody) == 0x50 && offsetof(cMacroDefinition, mLines) == 0x5c &&
                  offsetof(cMacroDefinition, mLevel) == 0x68 && offsetof(cMacroDefinition, mFile) == 0x6c &&
                  offsetof(cMacroDefinition, mFirstLine) == 0x84 && sizeof(cMacroDefinition) == 0x88,
              "cMacroDefinition layout");

// A held definition (the original's cRZAutoRefCount<cMacroDefinition>): a copy takes a reference (slot 2), destruction
// and assignment release one (slot 1). Assignment does nothing for the same object or the same pointer.
class DefinitionRef {
public:
    DefinitionRef() = default;
    explicit DefinitionRef(void *definition);
    DefinitionRef(const DefinitionRef &other);
    DefinitionRef &operator=(const DefinitionRef &other);
    ~DefinitionRef();

    void *Get() const { return mDefinition; }
    const std::string &Name() const { return static_cast<const cMacroDefinition *>(mDefinition)->mName; }

private:
    void *mDefinition = nullptr; // cMacroDefinition (or another cRZRefCount object with the same layout)
};
static_assert(sizeof(DefinitionRef) == 4, "a held pointer");

// The parser's definitions (+0x6c): a std::vector<DefinitionRef>, sorted by name (NameBefore's order).
struct DefinitionVector {
    std::vector<DefinitionRef> mVector;

    // 0xafd3c0 (__thiscall, ret 0xc): std::vector::emplace(where, const T &); the new element's position to *out.
    DefinitionRef **Insert(DefinitionRef **out, DefinitionRef *where, const DefinitionRef *value);
    // 0xafc950 (__thiscall, ret 8): the reallocating emplace (called when the vector is full); returns the new element.
    DefinitionRef *EmplaceReallocate(DefinitionRef *where, const DefinitionRef *value);
};
static_assert(sizeof(DefinitionVector) == 0xc, "a std::vector");

// 0x29a2e0 (__cdecl): the vector's uninitialized copy of held pointers (each copy takes a reference, slot 2); when a
// reference throws, the copies made so far are released. Returns the end of the copies. The fourth argument (the
// vector, as its allocator) is not used.
DefinitionRef *__cdecl CopyDefinitions(const DefinitionRef *first, const DefinitionRef *last, DefinitionRef *dest, void *allocator);

// The name-table order (length first, then the bytes from the last one, as signed chars): true when a sorts before b.
bool NameBefore(const char *aBegin, const char *aEnd, const char *bBegin, const char *bEnd);

// A definition name as the searches take it: its end, then its start (KeyRange's layout).
struct DefinitionKey {
    const char *end;
    const char *begin;

    // 0xafea90 (__thiscall, ret 8; the second argument is not used): true when the definition sorts before the key.
    bool DefinitionBefore(const DefinitionRef *entry, uint32_t unused) const;
    // 0xafeb10 (__thiscall, ret 8; the FIRST argument is not used): true when the key sorts before the definition.
    bool BeforeDefinition(uint32_t unused, const DefinitionRef *entry) const;
};
static_assert(sizeof(DefinitionKey) == 8, "KeyRange layout");

struct DefinitionRange {
    DefinitionRef *first;
    DefinitionRef *last;
};

// 0xafd790 (__cdecl): std::lower_bound of the key over [first, last). The original reads the word `keyWord` points to
// and hands it to the comparison, which ignores it.
DefinitionRef *__cdecl LowerBoundDefinition(DefinitionRef *first, DefinitionRef *last, const void *keyWord, const DefinitionKey *key);
// 0xafd520 (__cdecl, the key by value): std::equal_range of the key over [first, last) into *out; `keyWord` as above.
DefinitionRange *__cdecl EqualRangeDefinitions(DefinitionRange *out, DefinitionRef *first, DefinitionRef *last, const void *keyWord,
                                               DefinitionKey key);

// The parser as the macro code sees it (a view of cBaseParser; never constructed).
class cMacroParser {
public:
    cMacroParser() = delete;
    static cMacroParser *Of(void *parser) { return static_cast<cMacroParser *>(parser); }

    // 0xb06920 (__thiscall, ret 4): the parameters of the innermost pending call (create's frame, +0x11c) substituted
    // in `text`: "&name" becomes "(value)", "&{name}" the value itself, "&&" a single '&'. A name starts with a letter
    // or '_' and goes on with letters, digits and '_'. Errors: "missing parameter name" ('&' or "&{" at the end),
    // "bad parameter name", "missing '}'", "Unknown parameter: name". Nothing changes without a pending call or a '&'.
    void ExpandParameters(std::string *text);
};

} // namespace nGZCommandParser

// The owned statics of the definitions (game/owned.h; lane RU): the vtable (above) and the length of "enddef", 6, which
// the image's static initialisers set and HandleLine compares the first word's length with.
TS2_OWNED_STATIC(const OwnedVTable, e7db0c);
TS2_OWNED_STATIC(uint32_t, 10749a0);
// Our table for image-free programs (MacroDefinition.cpp): the image's 5 slots in order.
extern "C" const void *ts2_own_vt_e7db0c[5];
