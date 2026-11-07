#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rz/cRZString.h"

// DirectoryListing: the cached listing of one folder that the persist system walks when it opens content folders and
// checks segments (cTSPersistSystem's IsSegmentWritable 0x1e1d70, OpenContentFolder 0x1e3c70, SetUpSegment 0x1e7230 build
// it on their stacks; nothing else does). No RTTI: the name is ours. 0x3c bytes; RVAs are 1.18.0.187.
//
// The listing is one block of text: the folder path, then for every entry its kind byte with bit 7 set (4 a folder, 8 a
// file) followed by its name, each NUL-terminated, and an empty string at the end. mEntries points at each entry's kind
// byte. The block is cached in the persist system's default segment (primary slot 47) as the record
// {0x8cf2e796, 0, CRC32 of the lowercased path, 0}: a 4-byte folder stamp (FolderStamp) followed by the block. A cached
// block is used when its stamp equals the folder's current one and its first string equals the path (case-insensitive);
// otherwise the folder is scanned and the cache rewritten. A stamp of 0 (a volume that isn't NTFS) turns the cache off.
class DirectoryListing {
public:
    static constexpr uint32_t kCacheType = 0x8cf2e796;
    static constexpr uint8_t kFolder = 4;
    static constexpr uint8_t kFile = 8;

    // 0x1f58f0 (thiscall, ret 8; returns this): the listing of `path` (a '\' is appended unless it ends in '\' or '/').
    // `system` is the persist system's primary face, or null for no cache.
    DirectoryListing(const cRZString *path, void *system);
    // 0x1f6090: the path string, then the entry pointers, then the block.
    ~DirectoryListing();
    DirectoryListing(const DirectoryListing &) = delete;
    DirectoryListing &operator=(const DirectoryListing &) = delete;

    // 0x1f6dd0: the cursor to the first entry; false when there are none.
    bool First();
    // 0x1f61e0: the cursor to the next entry; false past the last. Only valid after First answered true.
    bool Next();
    // 0x1f64f0 (thiscall, ret 4; the string returned by value): out is built (it is raw memory) as the folder path
    // followed by the current entry's name. Returns out.
    cRZString *EntryPath(cRZString *out) const;
    // 0x1f65c0: the current entry's kind byte without bit 7 (sign-extended first, so the result is 0..0x7f).
    uint32_t EntryKind() const;

    // 0x1f6870 (__cdecl): the folder's cache stamp. The path is copied without one trailing separator; the volume of its
    // drive (GetVolumeInformationA on "<drive>\", or on null when the path names no drive) decides: a volume that answers
    // with a file system other than NTFS gives 0; otherwise (NTFS, or the call failed) the folder's modification time
    // (FileTimeOfPath 0x1c2068 with 2).
    static uint32_t __cdecl FolderStamp(const cRZString *folder);

    std::vector<char> mBuffer;         // +0x00: the block
    std::vector<const char *> mEntries; // +0x0c: each entry's kind byte in mBuffer
    const char *const *mCursor;        // +0x18: the current element of mEntries
    cRZString mPath;                   // +0x1c: the folder path (the game's cRZString vtable, refcount 0)
};

static_assert(offsetof(DirectoryListing, mEntries) == 0x0c, "DirectoryListing: entries at +0xc");
static_assert(offsetof(DirectoryListing, mCursor) == 0x18, "DirectoryListing: cursor at +0x18");
static_assert(offsetof(DirectoryListing, mPath) == 0x1c, "DirectoryListing: path at +0x1c");
static_assert(sizeof(DirectoryListing) == 0x3c, "DirectoryListing: 0x3c bytes");

// The hook entries' targets for the constructor and destructor (thiscall as __fastcall).
struct DirectoryListingAdapters {
    static DirectoryListing *__fastcall Construct(DirectoryListing *self, void *, const cRZString *path, void *system);
    static void __fastcall Destruct(DirectoryListing *self, void *);
};

// A listing on the caller's stack, built and destroyed through the adapters: real calls the persist system's tests can
// stand in for, as they stand in for the image's constructor and destructor.
class ScopedDirectoryListing {
public:
    ScopedDirectoryListing(const cRZString *path, void *system) { DirectoryListingAdapters::Construct(Get(), nullptr, path, system); }
    ~ScopedDirectoryListing() { DirectoryListingAdapters::Destruct(Get(), nullptr); }
    ScopedDirectoryListing(const ScopedDirectoryListing &) = delete;
    ScopedDirectoryListing &operator=(const ScopedDirectoryListing &) = delete;
    DirectoryListing *Get() { return reinterpret_cast<DirectoryListing *>(mBytes); }
    DirectoryListing *operator->() { return Get(); }

private:
    alignas(4) uint8_t mBytes[sizeof(DirectoryListing)];
};
