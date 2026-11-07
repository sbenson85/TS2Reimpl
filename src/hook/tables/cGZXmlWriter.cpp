// The Xml writers (standalone shape): the XmlUtils director's creators and the two constructors build ours; every slot
// is then ours through our vtables (no slot adapters).
#include "hook/hooks.h"
#include "gz/cGZXmlWriter.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZXmlBufferWriter_Create", 0x9c5261, 32, 0xde253375, reinterpret_cast<void *>(&cGZXmlWriterAdapters::CreateBufferWriter)},
    {"cGZXmlFileWriter_Create", 0x9c528e, 32, 0x9e2781c6, reinterpret_cast<void *>(&cGZXmlWriterAdapters::CreateFileWriter)},
    {"cGZXmlBufferWriter_Construct", 0x9c515d, 32, 0xf20be3fb,
     reinterpret_cast<void *>(&cGZXmlWriterAdapters::ConstructBufferWriter)},
    {"cGZXmlFileWriter_Construct", 0x9c518f, 32, 0xcd98ae68,
     reinterpret_cast<void *>(&cGZXmlWriterAdapters::ConstructFileWriter)},
};
REGISTER_HOOKS(kHooks);

} // namespace
