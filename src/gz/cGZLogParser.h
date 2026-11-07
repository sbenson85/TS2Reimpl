#pragma once
// cGZLogParser: the parser of the log configuration (the "log" cheat file: filters, formatters, presets, reporter
// and service levels), one static at RVA 0x1107510 built by XC slot 773 (0x1409f0) and destroyed by its atexit thunk
// 0xd98bc0. appdebugger's LogCheatGlobal / PrepareCheatGlobal / LoadCheatFile use it. RVAs are 1.18.0.187.
#include "gz/cBaseParser.h"

#include <cstdint>
#include <string>

namespace nGZCommandParser {

// The state the log commands work on (0x24 bytes): a type id, two service pointers the cheat code fills in later
// (the constructor leaves them as they are), and a name.
struct cGZLogParserState {
    static constexpr uint32_t kTypeId = 0x2d4a3f03;

    cGZLogParserState() : mTypeId(kTypeId) {}

    uint32_t mTypeId;    // +0
    void *mService4;     // +4 not written by the constructor
    void *mService8;     // +8 not written by the constructor
    std::string mName;   // +0xc
};
static_assert(sizeof(cGZLogParserState) == 0x24 && offsetof(cGZLogParserState, mName) == 0xc, "state layout");

class cGZLogParser {
public:
    // The log commands' vtables (each a cCommandT<cGZLogParserState>, 16 bytes), in the order they are added.
    static constexpr uint32_t kCommandVTables[] = {
        0xeb6fcc, // cLogFileCheat
        0xeb6be4, // cLogFilterCheat
        0xeb6c88, // cLogFilterLevelCheat
        0xeb6c34, // cLogFormatterCheat
        0xeb6e8c, // cLogFormatterPresetCheat
        0xeb6e1c, // cLogPresetCheat
        0xeb6da4, // cLogServiceLevelCheat
        0xeb6d10, // cLogReporterLevelCheat
        0xeb6f04, // cLogReporterPropCheat
        0xeb7104, // cLogSettingsCheat
        0xeb709c, // cLogWindowCheat
    };

    // 0xcac2b0 (__thiscall, returns this): a new state and a new cBaseParser (held with a reference), the
    // parser's built-in commands (cIBaseParser slot 0 with 0), the state handed to the parser (slot 3), then the
    // log commands (RegisterCommands).
    cGZLogParser();
    // 0xcac4c0 (__thiscall): the parser's slot 1 (on the cIBaseParser face; the parser must exist), the parser
    // released, the state deleted.
    ~cGZLogParser();

    // 0xcaf620 (__thiscall, ret 4): on `parser` (a cIBaseParser face): slot 3 with this's state, each log command
    // made (a zeroed 16-byte cCommand with its vtable) and added (slot 4), then slot 3 with null.
    void RegisterCommands(void *parser);

    cBaseParser *mParser;       // +0 a counted reference (AddRef / Release on its cIGZUnknown face)
    cGZLogParserState *mState;  // +4 owned

private:
    void SetParser(cBaseParser *parser);
};
static_assert(sizeof(cGZLogParser) == 8, "cGZLogParser layout");

// The hooks on the constructor and destructor (this in ecx: __fastcall with an unused edx).
struct cGZLogParserAdapters {
    static cGZLogParser *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(cGZLogParser *self, void *);
};

} // namespace nGZCommandParser
