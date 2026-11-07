// The Gimex reader and writer (standalone shape): the director's creators and the two constructors build ours; every
// slot is then ours through our vtables (no slot adapters).
#include "hook/hooks.h"
#include "gz/cGZGimexReader.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZGimexReader_Create", 0x87b428, 22, 0xe481ac9f, reinterpret_cast<void *>(&cGZGimexReaderAdapters::CreateReader)},
    {"cGZGimexWriter_Create", 0x87b43e, 22, 0x9a015a0d, reinterpret_cast<void *>(&cGZGimexReaderAdapters::CreateWriter)},
    {"cGZGimexReader_Construct", 0x87b4b4, 29, 0x35b45672,
     reinterpret_cast<void *>(&cGZGimexReaderAdapters::ConstructReader)},
    {"cGZGimexWriter_Construct", 0x87b4d1, 29, 0xa2925e3c,
     reinterpret_cast<void *>(&cGZGimexReaderAdapters::ConstructWriter)},
};
REGISTER_HOOKS(kHooks);

} // namespace
