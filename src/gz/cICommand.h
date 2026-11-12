#pragma once
// nGZCommandParser::cICommand as a C++ interface: the slots every command a parser runs has (cIGZUnknown's three, then
// slots 3-9). The hook-shaped commands in gz/CommandParser.h reach the same slots through the game's vtables; this is for
// commands whose objects our own constructors build, with our vtable. RVAs are 1.18.0.187.
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace nGZCommandParser {

class cIArguments;

class cICommand : public cIGZUnknown {
public:
    virtual void SetContext(void *parser, void *state) = 0; // 3 (ret 8): the parser the command runs in
    virtual void Execute(cIArguments *args) = 0;            // 4 (ret 4)
    virtual const char *GetName() = 0;                      // 5: the command's name
    virtual const char *GetHelp(uint32_t index) = 0;        // 6 (ret 4)
    virtual bool Slot7() = 0;                               // 7: the base commands' answer is false (0x147480)
    virtual uint32_t Slot8() = 0;                           // 8: the base commands' answer is 0 (0x6c6b0)
    virtual ~cICommand() = default;                         // 9: the deleting destructor (ret 4)
};

} // namespace nGZCommandParser
