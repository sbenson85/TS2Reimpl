#include "gz/cGZPropertySetSerializer.h"
#include "hook/hooks.h"

namespace {

using BinaryAdapters = cGZPropertySetSerializerBinaryStringAdapters;
using DispatcherAdapters = cGZPropertySetSerializerStringAdapters;
using U32DispatcherAdapters = cGZPropertySetSerializerUint32Adapters;
using BinaryU32Adapters = cGZPropertySetSerializerBinaryUint32Adapters;

// The string-keyed property-set serializer and its binary delegate (src/gz/cGZPropertySetSerializer.cpp). Not hooked:
// QueryInterface 0x555400 (shared_vf_: the dispatcher's and the binary delegate's slot 0, one body).
const HookSpec kHooks[] = {
    // The dispatcher in the standalone shape (lane EL): only ever the resource's base (+0x40), built by the resource's
    // constructor; these entries are adapters for direct calls and for resources the game built with its own vtables. Read,
    // Write and the deleting destructor (0x555c90, 0x5568c0, 0x552190; lane GT) are in effect through their callers: only the
    // game's vtable reaches them, and nothing carries it while the string resource's constructor hook is in (the host traps
    // them under it). Likewise the uint32 dispatcher's and uint32 binary delegate's deleting destructors (0x241fc9, 0x237015).
    {"cGZPropertySetSerializerString_ReadWith", 0x555b10, 32, 0xe11c4e9b, reinterpret_cast<void *>(&DispatcherAdapters::ReadWith)},
    // The binary delegate in the standalone shape (docs/standalone-shape.md, lane EF): built only by the dispatcher's
    // ReadWith / Write (ours: `new`), so every one carries our vtable; these entries are adapters for direct calls. Its
    // switch is the dispatcher's two hooks (_ReadWith, _Write): with them off the game builds delegates with its own vtable.
    {"cGZPropertySetSerializerBinaryString_DeletingDestructor", 0x552160, 32, 0xa0b55674, reinterpret_cast<void *>(&BinaryAdapters::DeletingDestructor)},
    {"cGZPropertySetSerializerBinaryString_Read", 0x555510, 32, 0x8f2e359b, reinterpret_cast<void *>(&BinaryAdapters::Read)},
    {"cGZPropertySetSerializerBinaryString_Write", 0x556570, 32, 0xc37d91d3, reinterpret_cast<void *>(&BinaryAdapters::Write)},
    // The uint32-keyed dispatcher in the standalone shape (lane EQ; src/gz/cGZPropertySetSerializerUint32.size.cpp). The game
    // builds it by its constructor (the uint32 resource's, and on their own: cTSAudioResourceSoundMetaData, cHitPropertySet*
    // resources), so the constructor hook gives every one our vtable (the class's switch, bisect: _Construct); the rest are adapters.
    {"cGZPropertySetSerializerUint32_Construct", 0x241e79, 32, 0x0a401a9b, reinterpret_cast<void *>(&U32DispatcherAdapters::Construct)},
    {"cGZPropertySetSerializerUint32_Destruct", 0x241f2c, 32, 0xaee7c081, reinterpret_cast<void *>(&U32DispatcherAdapters::Destruct)},
    {"cGZPropertySetSerializerUint32_Read", 0x2425ad, 32, 0xa119ea32, reinterpret_cast<void *>(&U32DispatcherAdapters::Read)},
    {"cGZPropertySetSerializerUint32_ReadWith", 0x242547, 32, 0xcdf2fb24, reinterpret_cast<void *>(&U32DispatcherAdapters::ReadWith)},
    {"cGZPropertySetSerializerUint32_Write", 0x242899, 32, 0x4e558dda, reinterpret_cast<void *>(&U32DispatcherAdapters::Write)},
    // The uint32 binary delegate in the standalone shape (lane EU; src/gz/cGZPropertySetSerializerUint32Delegates.size.cpp, the
    // template shared with the string one). Built by the uint32 dispatcher's ReadWith / Write (ours: `new`) and on the stack by
    // the game's 0x238159 / 0x2386c5 through the constructor 0x236be6: its hook gives those our vtable (the class's switch,
    // bisect: _Construct with the dispatcher's _ReadWith / _Write). The rest are adapters for direct calls (0x238159 / 0x2386c5
    // call Read / Write directly). Not hooked: QI 0x237d6f (shared_vf_), AddRef / Release (folded), WriteText 0x238987 (not a
    // function start: inside Write's chunk).
    {"cGZPropertySetSerializerBinaryUint32_Construct", 0x236be6, 19, 0x97f46950, reinterpret_cast<void *>(&BinaryU32Adapters::Construct)},
    {"cGZPropertySetSerializerBinaryUint32_Destruct", 0x236edd, 7, 0x8833a46f, reinterpret_cast<void *>(&BinaryU32Adapters::Destruct)},
    {"cGZPropertySetSerializerBinaryUint32_Read", 0x237dc2, 32, 0x2833f842, reinterpret_cast<void *>(&BinaryU32Adapters::Read)},
    {"cGZPropertySetSerializerBinaryUint32_Write", 0x23879c, 32, 0xce04ebe2, reinterpret_cast<void *>(&BinaryU32Adapters::Write)},
    {"cGZPropertySetSerializerBinaryUint32_ReadKey", 0x237c98, 32, 0xb75c8bb2, reinterpret_cast<void *>(&BinaryU32Adapters::ReadKey)},
    {"cGZPropertySetSerializerBinaryUint32_ReadText", 0x23822c, 32, 0x8f539ba2, reinterpret_cast<void *>(&BinaryU32Adapters::ReadText)},
    {"cGZPropertySetSerializerBinaryUint32_WriteKey", 0x23876b, 18, 0x5dc3eb36, reinterpret_cast<void *>(&BinaryU32Adapters::WriteKey)},
};
REGISTER_HOOKS(kHooks);

} // namespace
