#include "hook/hooks.h"
#include "gz/cHitTrackPlayer.h"

namespace {

const HookSpec kHooks[] = {
    {"cHitTrackPlayer_QueryInterface", 0xb366e2, 32, 0x69bfa49e, MemberAddress(&cHitTrackPlayer::QueryInterface)},
    {"cHitTrackPlayer_AddRef", 0xb337e7, 8, 0x5fda37f9, MemberAddress(&cHitTrackPlayer::AddRef)},
    {"cHitTrackPlayer_Release", 0xb367c0, 23, 0x9423963b, MemberAddress(&cHitTrackPlayer::Release)},
    {"cHitTrackPlayer_GetMultipleSourcesInterface", 0xb2f12b, 10, 0xf4dc140e, MemberAddress(&cHitTrackPlayer::GetMultipleSourcesInterface)},
    {"cHitTrackPlayer_InvokeChainedSlot5", 0xb36857, 16, 0xfc77b840, MemberAddress(&cHitTrackPlayer::InvokeChainedSlot5)},
    {"cHitTrackPlayer_SetFlagAtD4", 0xb368f4, 8, 0x65538ced, MemberAddress(&cHitTrackPlayer::SetFlagAtD4)},
    {"cHitTrackPlayer_CallSndSlot7", 0xb35aec, 13, 0x5c850f21, MemberAddress(&cHitTrackPlayer::CallSndSlot7)},
    {"cHitTrackPlayer_SndFlagAt40", 0xb2f914, 8, 0xa412eccd, MemberAddress(&cHitTrackPlayer::SndFlagAt40)},
    {"cHitTrackPlayer_SndFlagAt5C", 0xb2f91c, 8, 0xd0062e2d, MemberAddress(&cHitTrackPlayer::SndFlagAt5C)},
    {"cHitTrackPlayer_SndQuery13", 0xb2f44d, 11, 0x9bcbb8fe, MemberAddress(&cHitTrackPlayer::SndQuery13)},
    {"cHitTrackPlayer_SndQuery15", 0xb2f458, 11, 0xf81b8dc4, MemberAddress(&cHitTrackPlayer::SndQuery15)},
    {"cHitTrackPlayer_SndQuery16", 0xb2f42f, 11, 0xc9f39759, MemberAddress(&cHitTrackPlayer::SndQuery16)},
    {"cHitTrackPlayer_SndQuery17", 0xb2f419, 11, 0x6f849ced, MemberAddress(&cHitTrackPlayer::SndQuery17)},
    {"cHitTrackPlayer_SndQuery49", 0xb2f424, 11, 0x1c6e0e07, MemberAddress(&cHitTrackPlayer::SndQuery49)},
    {"cHitTrackPlayer_SndTestId2A", 0xb2f904, 16, 0x35177612, MemberAddress(&cHitTrackPlayer::SndTestId2A)},
    {"cHitTrackPlayer_SndForwardSlot5", 0xb3648b, 17, 0x02d1aaaa, MemberAddress(&cHitTrackPlayer::SndForwardSlot5)},
    {"cHitTrackPlayer_SndNotifyEmbedded", 0xb3090e, 17, 0xcf6c2fa1, MemberAddress(&cHitTrackPlayer::SndNotifyEmbedded)},
};
REGISTER_HOOKS(kHooks);

} // namespace
