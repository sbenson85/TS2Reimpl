#include "hook/hooks.h"
#include "gz/cGZPersistBinaryReader.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZPersistBinaryReader_QueryInterface", 0x9bfcb0, 32, 0x1cc9923b, MemberAddress(&cGZPersistBinaryReader::QueryInterface)},
    {"cGZPersistBinaryReader_vf4", 0x9c0510, 24, 0x60aba517, MemberAddress(&cGZPersistBinaryReader::GetBool)},
    {"cGZPersistBinaryReader_vf5", 0x9c0800, 20, 0x315280f1, MemberAddress(&cGZPersistBinaryReader::GetSint8)},
    {"cGZPersistBinaryReader_vf6", 0x9c0900, 20, 0xc51da4e2, MemberAddress(&cGZPersistBinaryReader::GetUint8)},
    {"cGZPersistBinaryReader_vf7", 0x9c0740, 19, 0x210a46cc, MemberAddress(&cGZPersistBinaryReader::GetSint16)},
    {"cGZPersistBinaryReader_vf9", 0x9c07a0, 18, 0x8e891213, MemberAddress(&cGZPersistBinaryReader::GetSint32)},
    {"cGZPersistBinaryReader_vf10", 0x9c08a0, 18, 0x15185005, MemberAddress(&cGZPersistBinaryReader::GetUint32)},
    {"cGZPersistBinaryReader_vf11", 0x9c0570, 18, 0xcf8c8c23, MemberAddress(&cGZPersistBinaryReader::GetFloat32)},
    {"cGZPersistBinaryReader_vf12", 0x9c05d0, 22, 0x7713c1e8, MemberAddress(&cGZPersistBinaryReader::GetFloat64)},
    {"cGZPersistBinaryReader_vf13", 0x9c0530, 32, 0xb0c48971, MemberAddress(&cGZPersistBinaryReader::GetBoolArray)},
    {"cGZPersistBinaryReader_vf14", 0x9c0820, 8, 0x1aaa888a, MemberAddress(&cGZPersistBinaryReader::GetVoid)},
    {"cGZPersistBinaryReader_vf16", 0x9c0760, 32, 0x46dbf14f, MemberAddress(&cGZPersistBinaryReader::GetSint16Array)},
    {"cGZPersistBinaryReader_vf18", 0x9c07c0, 32, 0x518a6832, MemberAddress(&cGZPersistBinaryReader::GetSint32Array)},
    {"cGZPersistBinaryReader_vf20", 0x9c0590, 32, 0xc47f70a2, MemberAddress(&cGZPersistBinaryReader::GetFloat32Array)},
    {"cGZPersistBinaryReader_vf25", 0x9c06b0, 32, 0x39af9a59, MemberAddress(&cGZPersistBinaryReader::GetObjectRef)},
    {"cGZPersistBinaryReader_vf8", 0x9c0840, 19, 0xd54562df, MemberAddress(&cGZPersistBinaryReader::GetUint16)},
    {"cGZPersistBinaryReader_vf17", 0x9c0860, 32, 0xa0cbbed1, MemberAddress(&cGZPersistBinaryReader::GetUint16Array)},
    {"cGZPersistBinaryReader_vf19", 0x9c08c0, 32, 0xb79a27ac, MemberAddress(&cGZPersistBinaryReader::GetUint32Array)},
    {"cGZPersistBinaryReader_vf21", 0x9c05f0, 32, 0x226f3f3c, MemberAddress(&cGZPersistBinaryReader::GetFloat64Array)},
    {"cGZPersistBinaryReader_vf22", 0x9c0830, 8, 0x44747184, MemberAddress(&cGZPersistBinaryReader::GetGZStr)},
    {"cGZPersistBinaryReader_vf23", 0x9c0920, 8, 0x62ad4128, MemberAddress(&cGZPersistBinaryReader::Skip)},
    {"cGZPersistBinaryReader_vf24", 0x9c0630, 32, 0xff4996af, MemberAddress(&cGZPersistBinaryReader::ReadObject)},
    {"cGZPersistBinaryReader_vf26", 0x9c0490, 32, 0x5869bf2c, MemberAddress(&cGZPersistBinaryReader::ReadStringRecord)},
    {"cGZPersistBinaryReader_vf29", 0x9bfc20, 32, 0x34152eb8, MemberAddress(&cGZPersistBinaryReader::DeletingDestructor)},
    {"cGZPersistBinaryReader_vf3", 0x9bfd10, 32, 0xed6f63f4, MemberAddress(&cGZPersistBinaryReader::ReadRoot)},
    {"cGZPersistBinaryReader_Constructor", 0x9bf9a0, 32, 0xca128022, MemberAddress(&cGZPersistBinaryReader::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
