#include "hook/hooks.h"
#include "ts/DirectoryListing.h"

namespace {

// The persist system's folder listing (lane IP): constructed, walked and destroyed by cTSPersistSystem (ours) and the
// game's other callers.
const HookSpec kHooks[] = {
    {"DirectoryListing_Construct", 0x1f58f0, 32, 0x9ae7d577, reinterpret_cast<void *>(&DirectoryListingAdapters::Construct)},
    {"DirectoryListing_Destruct", 0x1f6090, 32, 0xb619c2bf, reinterpret_cast<void *>(&DirectoryListingAdapters::Destruct)},
    {"DirectoryListing_First", 0x1f6dd0, 13, 0xf1003b95, MemberAddress<&DirectoryListing::First>()},
    {"DirectoryListing_Next", 0x1f61e0, 14, 0xbe99128b, MemberAddress<&DirectoryListing::Next>()},
    {"DirectoryListing_EntryPath", 0x1f64f0, 32, 0x20e75fb2, MemberAddress<&DirectoryListing::EntryPath>()},
    {"DirectoryListing_EntryKind", 0x1f65c0, 12, 0x8773a98a, MemberAddress<&DirectoryListing::EntryKind>()},
    {"DirectoryListing_FolderStamp", 0x1f6870, 32, 0xc3c9e978, reinterpret_cast<void *>(&DirectoryListing::FolderStamp)},
};
REGISTER_HOOKS(kHooks);

} // namespace
