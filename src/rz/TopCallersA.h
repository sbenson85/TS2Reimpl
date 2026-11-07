#pragma once
#include <cstdint>

// Reimplementations for the TopCallersA group (RVAs are 1.18.0.187). Each class carries only the
// layout this group touches; the gaps are bytes the original code never reads for these slots.

namespace nTSWorld {

// Reference-counted world message data: vtable at +0, count at +4.
class cWorldDBMessageData {
public:
    static constexpr uint32_t kVTable = 0xdd36c4;
    uint32_t Release(); // slot 2, 0x5449e0: returns the new count, or 0 when it deleted the object
    const void *mVTable;
    uint32_t mRefCount;
};

} // namespace nTSWorld

// Finds the window's child for `id` (slot 39); if there is one, sets its flag 2 to `enabled` when
// that flag differs (compared as a byte). Returns 1 when a child was found, else 0 (al).
// TS2-Extender hooks this address (db/community-hooks.jsonl), so it is not installed as a hook.
uint8_t __cdecl SetWindowEnabled(void *window, uint32_t id, uint32_t enabled); // 0x93526f

// Iterator over unsigned 16-bit elements (cRZMemPtrIterator<cGZElementByValue<unsigned short>>).
class cRZMemPtrIteratorU16 {
public:
    static constexpr uint32_t kVTable = 0xe73550;
    cRZMemPtrIteratorU16 *Copy() const; // slot 3, 0xa70130: new 16-byte copy, count word zeroed
    const void *mVTable;
    uint32_t mField4;   // zeroed in the copy
    uint32_t mField8;
    uint32_t mFieldC;
};

namespace nGZCommandParser {

// Block command: vtable at +0, object at +4, value at +0xc, byte result at +0x3c.
class cBlockCommand {
public:
    void StoreAndQuery(void *object, uint32_t value); // slot 3, 0xaf7bd0 (ret 8)
    const void *mVTable;
    void *mObject;
    uint32_t _gap8;
    uint32_t mValue;
    uint8_t _gap10[0x2c];
    uint8_t mFlag;
};

} // namespace nGZCommandParser

// cGZPersistResource::Release (0x9b6260) moved to the class's own group in the standalone shape (lane EL):
// src/gz/cGZPersistResource.h.

static_assert(sizeof(nTSWorld::cWorldDBMessageData) == 8, "layout");
static_assert(sizeof(cRZMemPtrIteratorU16) == 16, "layout");
static_assert(sizeof(nGZCommandParser::cBlockCommand) == 0x40, "layout");
