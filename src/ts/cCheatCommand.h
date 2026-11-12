#pragma once
// cITSCheatSystem::cCheatCommand (RTTI `.?AUcCheatCommand@cITSCheatSystem@@`, vtable 0xda502c, 12 slots): the base of the
// game's cheat commands, a nGZCommandParser::cICommand that keeps the parser it runs in (+4, slot 3 stores it, slot 11
// returns it) and its own reference count (+8). Here in the global namespace (cITSCheatSystem itself is not defined). Its
// slots 1-3 and 7-11 are folded with other classes' bodies (verified with nGZCommandParser::cCommand, gz/CommandParser.h);
// its QueryInterface, 0x1d0b4a, is the cheat family's own (CheatQueryInterface below). RVAs are 1.18.0.187.
#include "gz/cICommand.h"

#include <cstdint>

// 0x1d0b4a (ret 8): no out pointer, false; ids 1 (cIGZUnknown) and 0xaada39c7 (the command interface) store the object
// and count it (slot 1), true; any other id, false with *out left as it was.
bool CheatQueryInterface(nGZCommandParser::cICommand *self, uint32_t iid, void **out);

class cCheatCommand : public nGZCommandParser::cICommand {
public:
    static constexpr uint32_t kIid = 0xaada39c7;

    cCheatCommand() = default;
    cCheatCommand(const cCheatCommand &) = delete;
    cCheatCommand &operator=(const cCheatCommand &) = delete;

    bool QueryInterface(uint32_t iid, void **out) override { return CheatQueryInterface(this, iid, out); } // 0: 0x1d0b4a
    uint32_t AddRef() override { return ++mRefCount; }                                  // 1: 0x145260 (shared)
    // 2: 0x1d0be0 (shared): the last reference deletes the command through slot 9.
    uint32_t Release() override
    {
        if (--mRefCount == 0) {
            delete this;
            return 0;
        }
        return mRefCount;
    }
    void SetContext(void *parser, void *) override { mParser = parser; }     // 3: 0x1d0b40 (shared)
    const char *GetHelp(uint32_t) override { return GetDescription(); }      // 6: 0x1d0a53, slot 10
    bool Slot7() override { return false; }                                  // 7: 0x147480 (shared)
    uint32_t Slot8() override { return 0; }                                  // 8: 0x6c6b0 (shared)
    ~cCheatCommand() override = default;                                     // 9: 0x1d0a33 (the base's deleting destructor)
    virtual const char *GetDescription() { return nullptr; }                 // 10: 0x6c6b0 (shared)
    virtual void *GetParser() { return mParser; }                            // 11: 0x147a30 (shared)

    void *mParser = nullptr;  // +4 the parser (nGZCommandParser::cIBaseParser)
    uint32_t mRefCount = 0;   // +8
};

static_assert(sizeof(cCheatCommand) == 0xc, "cCheatCommand layout");
