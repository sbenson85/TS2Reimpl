#include "hook/hooks.h"
#include "rz/cRZSerializableStream.h"

namespace {

using SS = cRZSerializableStream;
using IS = cRZSerializableStreamIS;
using SG = cRZSerializableStreamSS;

// Release (0x9cbed8) is a jmp thunk to the shared stream release and is not hooked; its subobject
// thunks (0x9cbedd, 0x9cbee5) are, and reach our Release.
const HookSpec kHooks[] = {
    {"cRZSerializableStream_Construct", 0x9cbcf3, 32, 0x0e1beae9, MemberAddress(&SS::Construct)},
    {"cRZSerializableStream_Destruct", 0x9cbd43, 32, 0x67b3a878, MemberAddress(&SS::Destruct)},
    {"cRZSerializableStream_vf21", 0x9cbd79, 31, 0xa83a5869, MemberAddress(&SS::DeletingDestructor)},
    {"cRZSerializableStream_QueryInterface", 0x9cbe22, 32, 0xff7c51ea, MemberAddress(&SS::QueryInterface)},
    {"cRZSerializableStream_SetVoid", 0x9cbeed, 32, 0x8e1436c7, MemberAddress(&SS::SetVoid)},
    {"cRZSerializableStream_vf22", 0x9cbdee, 32, 0x2e4bd243, MemberAddress(&SS::SetByteOrder)},
    {"cRZSerializableStream_vf23", 0x9cbf2f, 32, 0xa57530ae, MemberAddress(&SS::Reset)},
    {"cRZSerializableStream_Remaining", 0x9cbff6, 7, 0xda0f0617, MemberAddress(&SS::Remaining)},
    {"cRZSerializableStream_Reserve", 0x9cbffd, 32, 0x611a710c, MemberAddress(&SS::Reserve)},
    {"cRZSerializableStream_Grow", 0x9cc050, 32, 0xa987edb6, MemberAddress(&SS::Grow)},
    {"cRZSerializableStream_Skip", 0x9cbf71, 32, 0xc95cba53, MemberAddress(&IS::Skip)},
    {"cRZSerializableStream_GetVoid", 0x9cbda0, 32, 0x5105136e, MemberAddress(&IS::GetVoid)},
    {"cRZSerializableStream_Write", 0x9cbfab, 32, 0xdc8af557, MemberAddress(&SG::Write)},
    {"cRZSerializableStream_Read", 0x9cbe85, 32, 0x027a9423, MemberAddress(&SG::Read)},
    {"cRZSerializableStream_vf16_0", 0x9cbe75, 8, 0x72b7d684, MemberAddress(&IS::QueryInterface)},
    {"cRZSerializableStream_vf32_0", 0x9cbe7d, 8, 0x43520167, MemberAddress(&SG::QueryInterface)},
    {"cRZSerializableStream_vf16_2", 0x9cbedd, 8, 0x9cd186fa, MemberAddress(&IS::Release)},
    {"cRZSerializableStream_vf32_2", 0x9cbee5, 8, 0x0d06fe27, MemberAddress(&SG::Release)},
    {"cRZSerializableStream_vf16_21", 0x9cbd71, 8, 0x08bc19a1, MemberAddress(&IS::vf16_21)},
    {"cRZSerializableStream_vf16_22", 0x9cbe12, 8, 0xa134117d, MemberAddress(&IS::vf16_22)},
    {"cRZSerializableStream_vf32_8", 0x9cbe1a, 8, 0x30e369a0, MemberAddress(&SG::vf32_8)},
    {"cRZSerializableStream_vf16_23", 0x9cbf61, 8, 0x5b248e69, MemberAddress(&IS::vf16_23)},
    {"cRZSerializableStream_vf32_9", 0x9cbf69, 8, 0xf1d501b7, MemberAddress(&SG::vf32_9)},
};
REGISTER_HOOKS(kHooks);

} // namespace
