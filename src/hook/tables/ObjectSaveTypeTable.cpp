#include "hook/hooks.h"
#include "ts/ObjectSaveTypeTable.h"

namespace {

// Lane MB (F4e): the save table's destructor body. Its only references are the unwind funclets of the folder's
// ReadSaveTable / WriteSaveTable (ours, so they never run); it is the only code storing the game's vtable 0xdcd0c0, so
// this hook is what the slots' "in effect through callers" entries rest on (db/status/ObjectSaveTypeTable.jsonl).
const HookSpec kHooks[] = {
    {"ObjectSaveTypeTable_Destructor", 0x4cf490, 32, 0x2fe7d084, reinterpret_cast<void *>(&ObjectSaveTypeTableAdapters::Destroy)},
};
REGISTER_HOOKS(kHooks);

} // namespace
