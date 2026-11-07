#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

class cIGZString;

// The game's application interfaces (lane EC, docs/subsystems/startup.md). Most of the names and argument meanings
// below are tentative (marked so in the Ghidra facts): the slots were sized from their `ret N` and their callers.

// cITSAppFW (id 0x9af89af3; 5 slots): the face at +0x40 of cTSAppFW (vtable 0xe984f8).
class cITSAppFW : public cIGZUnknown {
public:
    // slot 3, 0xc296b4 (ret 4): cCTGFramework::MainLoop 0xc2d9c5 calls it with each MSG (true = consumed) and with
    // null once per frame.
    virtual bool PreTranslateMessage(void *msg) = 0;
    virtual uint32_t GetWord70() = 0; // slot 4, folded 0x1457d0 (face + 0x30 = object +0x70)
};

// cITSAppCommon (id 0xebabd35c; 18 slots): the option/settings store, implemented by cTSAppCommonImpl (0x18 bytes,
// vtable 0xeb5468; at +0x44 of cTSAppFW, vtable 0xe98510). It holds two ordered maps of string-keyed option lists. Slot
// 10 is the interface's virtual destructor: it is declared in place, so the compiler puts it at slot 10 as the original.
class cITSAppCommon : public cIGZUnknown {
public:
    // slot 3, 0xca7230 (ret 0x14): adds an option to one of the two maps.
    virtual bool AddOption(uint32_t group, uint32_t id, const char *text, uint32_t flag, int32_t length) = 0;
    virtual bool GetOptionText(uint32_t id, cIGZString *out, uint32_t a, uint32_t b, int32_t c) = 0; // slot 4, 0xca5e40
    virtual bool GetOptionList(uint32_t id, void *list, uint32_t a, uint32_t b, int32_t c) = 0;      // slot 5, 0xca5e80
    virtual bool HasOption(uint32_t key) = 0;                     // slot 6, 0xca6bd0 (ret 4)
    virtual bool IsNameUnused(cIGZString *name) = 0;              // slot 7, 0xca69f0 (ret 4)
    virtual bool IsOptionNameUnused(uint32_t id) = 0;             // slot 8, 0xca6920 (ret 4)
    virtual void SetAppName(const char *text) = 0;                // slot 9, cTSAppFW 0xc299da (ret 4)
    virtual ~cITSAppCommon() = default;                           // slot 10 (cTSAppFW: thunk 0xc26788)
    virtual bool ReadOptionText(uint32_t id, cIGZString *out, uint32_t a, uint32_t b, int32_t c) = 0; // slot 11, 0xca5ed0
    virtual bool ReadOptionList(uint32_t id, void *list, uint32_t a, uint32_t b, int32_t c) = 0;      // slot 12, 0xca6200
    virtual bool InitOptions() = 0;                               // slot 13, cTSAppFW 0xc2a7bb (tail call of slot 14: its al)
    virtual bool FindProductDirectories() = 0;                    // slot 14, 0xca75c0 (was "ParseOptions"; true)
    virtual bool SetupUserDataDirectories() = 0;                  // slot 15, 0xca8710, cTSAppFW 0xc2a7de (false when slot 13 fails)
    virtual bool ReadResourceConfiguration() = 0;                 // slot 16, 0xca6c10 (was "ClearOptions"; true)
    virtual void RegisterRegistryHandlers() = 0;                  // slot 17, 0xca5d20
};
