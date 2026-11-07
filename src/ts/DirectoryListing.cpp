#include "rz/PathFileOps.h"
#include "ts/DirectoryListing.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/DirectoryScan.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZFileName.h"

#include <cstring>
#include <new>
#include <string>

#include <windows.h>

namespace {

constexpr uint32_t kIatGetVolumeInformationA = 0xd9a220;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

// The cache record's key: type, group, instance, resource.
struct CacheKey {
    uint32_t type, group, instance, resource;
};

// The default segment's slot 22 (OpenRecord) and the record's slots (2 Release, 6 Close, 7 Abort, 8 Read, 9 Write, 10
// GetSize).
uint8_t OpenRecord(void *store, const CacheKey *key, void **record, uint32_t mode)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, const CacheKey *, void **, uint32_t), 22>(store, key, record, mode);
}
uint8_t ReadRecord(void *record, void *out, uint32_t size)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, void *, uint32_t), 8>(record, out, size);
}
uint8_t WriteRecord(void *record, const void *data, uint32_t size)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, const void *, uint32_t), 9>(record, data, size);
}
uint32_t RecordSize(void *record)
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 10>(record);
}
void CloseRecord(void *record)
{
    VirtualCall<uint8_t(__thiscall *)(void *), 6>(record);
}
void AbortRecord(void *record)
{
    VirtualCall<uint8_t(__thiscall *)(void *), 7>(record);
}
void ReleaseRecord(void *record)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(record);
}

void Append(std::vector<char> &block, const char *text, size_t size)
{
    block.insert(block.end(), text, text + size);
}

} // namespace

DirectoryListing::DirectoryListing(const cRZString *path, void *system) : mCursor(nullptr)
{
    mPath.Construct();
    // The persist system's default segment (primary slot 47) holds the cache.
    void *store = system ? VirtualCall<void *(__thiscall *)(void *), 47>(system) : nullptr;
    mPath.mString = path->mString;
    if (!mPath.mString.empty()) {
        const char last = mPath.mString.back();
        if (last != '\\' && last != '/') mPath.mString.push_back('\\');
    }

    CacheKey key{kCacheType, 0, StringKeyParser::Crc32(mPath.mString.c_str(), 1), 0};
    uint32_t stamp = FolderStamp(&mPath);
    bool cached = false;
    if (store && stamp) {
        void *record = nullptr;
        if (OpenRecord(store, &key, &record, 1)) {
            uint32_t stored = 0;
            if (ReadRecord(record, &stored, 4) && stamp == stored) {
                const uint32_t size = RecordSize(record) - 4;
                // Two bytes more than the record holds: zeros that end the last string and the block.
                mBuffer.resize(size + 2);
                if (mBuffer.empty() || ReadRecord(record, mBuffer.data(), size))
                    cached = VirtualCall<uint8_t(__thiscall *)(void *, const char *, uint32_t, uint8_t), 9>(&mPath,
                                 mBuffer.data(), 0xffffffffu, 0) != 0;
            }
            CloseRecord(record);
        }
        if (record) ReleaseRecord(record);
    }

    if (!cached) {
        // Our directory scan (lane LV), built and destroyed through its adapters: the addressable entry points that
        // tests stand in for, as for the image's 0x1c48da / 0x1c49eb.
        alignas(DirectoryScan) uint8_t scanMemory[sizeof(DirectoryScan)];
        DirectoryScan *scan = DirectoryScanAdapters::Construct(scanMemory, nullptr, &mPath, false, false, false);
        scan->Scan();
        mBuffer.clear();
        mEntries.clear();
        Append(mBuffer, mPath.mString.data(), mPath.mString.size());
        mBuffer.push_back(0);
        const uint32_t count = scan->mCount;
        for (uint32_t i = 0; i < count; ++i) {
            const dscan::Entry *entry = scan->EntryAt(i);
            mBuffer.push_back(static_cast<char>(static_cast<uint8_t>(entry->mKind) | 0x80));
            const std::string &name = entry->mName.mString;
            Append(mBuffer, name.data(), name.size());
            mBuffer.push_back(0);
        }
        mBuffer.push_back(0);
        DirectoryScanAdapters::Destruct(scan, nullptr);
    }

    // The entries follow the path string, up to the empty string at the end.
    const char *text = mBuffer.data();
    text += std::strlen(text) + 1;
    while (*text) {
        mEntries.push_back(text);
        text += std::strlen(text) + 1;
    }

    if (store && !cached && stamp) {
        void *record = nullptr;
        if (OpenRecord(store, &key, &record, 2)) {
            if (WriteRecord(record, &stamp, 4) && WriteRecord(record, mBuffer.data(), static_cast<uint32_t>(mBuffer.size())))
                CloseRecord(record);
            else
                AbortRecord(record);
        }
        if (record) ReleaseRecord(record);
    }
}

DirectoryListing::~DirectoryListing()
{
    mPath.Destruct(); // the vectors follow as members
}

bool DirectoryListing::First()
{
    mCursor = mEntries.data();
    return mCursor != mEntries.data() + mEntries.size();
}

bool DirectoryListing::Next()
{
    ++mCursor;
    return mCursor != mEntries.data() + mEntries.size();
}

cRZString *DirectoryListing::EntryPath(cRZString *out) const
{
    new (&out->mString) std::string(mPath.mString.data(), mPath.mString.size());
    out->mVTable = GameAddress<void>(cRZString::kVTable);
    out->mRefCount = 0;
    const char *name = *mCursor + 1;
    out->mString.append(name, std::strlen(name));
    return out;
}

uint32_t DirectoryListing::EntryKind() const
{
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(**mCursor))) & 0x7f;
}

uint32_t __cdecl DirectoryListing::FolderStamp(const cRZString *folder)
{
    cRZString path;
    path.Construct();
    path.mString.assign(folder->mString.data(), folder->mString.size());
    if (!path.mString.empty()) {
        const char last = path.mString.back();
        if (last == '\\' || last == '/') path.mString.pop_back();
    }

    cRZFileName name;
    name.Construct(&path);
    const cRZString *drive = name.GetDrive();
    BOOL answered;
    char fileSystem[MAX_PATH];
    DWORD maxComponent, flags;
    {
        std::string root;
        const char *rootName = nullptr;
        if (!drive->mString.empty()) {
            root = drive->mString + '\\';
            rootName = root.c_str();
        }
        answered = Import<decltype(&GetVolumeInformationA)>(kIatGetVolumeInformationA)(rootName, nullptr, 0, nullptr,
            &maxComponent, &flags, fileSystem, MAX_PATH);
    }
    uint32_t stamp;
    if (answered && std::strncmp(fileSystem, "NTFS", 4) != 0)
        stamp = 0;
    else
        stamp = PathFileTime(path.AsInterface(), 2);
    name.Destruct();
    path.Destruct();
    return stamp;
}

DirectoryListing *__fastcall DirectoryListingAdapters::Construct(DirectoryListing *self, void *, const cRZString *path, void *system)
{
    return new (self) DirectoryListing(path, system);
}

void __fastcall DirectoryListingAdapters::Destruct(DirectoryListing *self, void *)
{
    self->~DirectoryListing();
}
