// cTSContentRegistry (lane KT): the class in the standalone shape (src/ts/cTSContentRegistry.h).
#include "rz/PathFileOps.h"
#include "ts/cTSContentRegistry.h"

#include "app/AppInit.h" // CommonImplSlot4, HighestOptionBit, HasOptionBits
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZString.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h"   // Dispatch8
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

constexpr uint32_t kRegistryDirPath = 0x2bcfce83; // the app path of the user data directory
constexpr uint32_t kDataDirPath = 0x4bcfce5e;     // the app path of a game data directory (one option bit)
constexpr uint32_t kRegistryFilePath = 0xedb70568; // the registry file (Load, Clear)
constexpr uint32_t kRebuildFilePath = 0xedb70c55;  // the registry file a rebuild writes (slot 17)
constexpr uint32_t kPersistClass = 0x056b906e;
constexpr uint32_t kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kPrimeClass = 0x2a4915ea; // a service Init asks for once (0xb57380) and drops
constexpr uint32_t kPrimeIid = 0x4a490e00;
constexpr uint32_t kSegmentClass = 0x086d4a89; // a packed-file segment (the director's class)
constexpr uint32_t kSegmentIid = 0x656b8f0c;
constexpr uint32_t kSerialRecordIid = 0xc56b8f08;
constexpr uint32_t kUserDataDir = 0x107b620; // the image's pointer to "%UserDataDir%"
constexpr uint32_t kGameDataDirFormat = 0x107b6e4; // the image's pointer to "%%GameDataDir%03d%%"
constexpr uint32_t kUserDataDirLength = 0x10fc484; // strlen of the above (13; the game's static initialiser 0x13fff0)
constexpr uint32_t kFileName = 0x10fc488;   // the cRZFileName Normalize and Rebuild share (static initialiser 0x13ffd0)
constexpr uint32_t kDataDirCount = 32;

// The registry's records (type 0xcdb467b8, group 1): the header, the keys and (in any package) the properties.
constexpr cGZPersistResourceKey kHeaderKey{0xcdb467b8, 1, 1, 0};     // 0x107b6b4
constexpr cGZPersistResourceKey kKeysKey{0xcdb467b8, 2, 1, 0};       // 0x107b6c4
constexpr cGZPersistResourceKey kPropertiesKey{0xcdb467b8, 3, 1, 0}; // 0x107b6d4

// The game data entries' versions (0x107b6e8, bits 0..4); later bits get {id, 1, bit + 1}.
constexpr ContentVersion kDataVersions[5] = {
    {0x7fa7dc7c, 0x0099, 1}, {0x7fa7dc7c, 0x00b6, 1}, {0x7fa7dc7c, 0x00de, 1}, {0x7fa7dc7c, 0x0110, 1}, {0x7fa7dc7c, 0x0001, 5},
};

// The CRC table (the image's 0x10fb480 and its flag 0x10fb478, which only 0xb58ce0, slot 7 and slot 13 use): row b is the
// byte b shifted through the 128-bit polynomial 0xe860b8 eight times.
uint8_t gCrcTable[256][16];
bool gCrcTableBuilt;
constexpr uint8_t kCrcPolynomial[16] = {0xa0, 0x00, 0x00, 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};

// Original callees nobody has replaced (cRZFile group): the file's attributes set (0x1c3505 with 2, 1) and the file
// deleted (0x1c2e25).
void SetFileAttributes(cRZString *path) { OriginalFunction<uint8_t(__cdecl *)(cRZString *, uint32_t, uint32_t), 0x1c3505>()(path, 2, 1); }
void RemoveFile(cRZString *path) { PathDelete(path->AsInterface()); }

cRZFileName *SharedFileName() { return GameAddress<cRZFileName>(kFileName); }
const char *UserDataDirToken() { return *GameAddress<const char *>(kUserDataDir); }
const char *GameDataDirFormat() { return *GameAddress<const char *>(kGameDataDirFormat); }

// The app's path `id` (option `mask`) into `out`: CommonImplSlot4(id, out, 1, 0, mask); al is the answer.
bool AppPath(uint32_t id, ContentString &out, uint32_t mask)
{
    return static_cast<uint8_t>(appinit::CommonImplSlot4(id, Word(&out.s), 1, 0, mask)) != 0;
}

// _Traits_compare(text, min(size, cap), prefix, prefixLength) == 0: `text` starts with the prefix (cap at least as long).
bool StartsWith(const std::string &text, uint32_t cap, const char *prefix, uint32_t prefixLength)
{
    const uint32_t length = static_cast<uint32_t>(text.size()) < cap ? static_cast<uint32_t>(text.size()) : cap;
    return length == prefixLength && std::memcmp(text.data(), prefix, length) == 0;
}

// A string from a cIGZString's characters (slot 7 the length first, then slot 6 the text).
void CopyCharacters(ContentString &out, const cIGZString *text)
{
    const uint32_t length = text->Strlen();
    const char *const chars = text->Data();
    out.s.mString.assign(chars, length);
}

// The serial record face of an open record (QueryInterface 0xc56b8f08, unchecked as in the original).
struct SerialRecord {
    void *mRecord = nullptr;
    void *mSerial = nullptr;
    SerialRecord() = default;
    SerialRecord(const SerialRecord &) = delete;
    SerialRecord &operator=(const SerialRecord &) = delete;
    ~SerialRecord()
    {
        if (mRecord) As<ifaces::cIGZPersistDBRecord>(mRecord)->Release();
    }
    // OpenRecord(key, mode) on the segment; the serial face asked for when it opened.
    bool Open(void *segment, const cGZPersistResourceKey &key, uint32_t mode)
    {
        return As<ifaces::cIGZPersistDBSegment>(segment)->OpenRecord(&key, &mRecord, mode) != 0;
    }
    ifaces::cIGZPersistDBSerialRecord *Serial()
    {
        As<ifaces::cIGZPersistDBRecord>(mRecord)->QueryInterface(kSerialRecordIid, &mSerial);
        return As<ifaces::cIGZPersistDBSerialRecord>(mSerial);
    }
    // The record closed (slot 6), then the serial face released.
    void Close()
    {
        As<ifaces::cIGZPersistDBRecord>(mRecord)->Close();
        if (mSerial) As<ifaces::cIGZPersistDBSerialRecord>(mSerial)->Release();
    }
};

// 0xb591e0: the header record's format word (read, dropped), then the entry's version.
void ReadHeader(ContentEntry &entry, void *segment)
{
    SerialRecord record;
    if (!record.Open(segment, kHeaderKey, 1)) return;
    ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
    uint32_t format;
    serial->GetFieldUint32(&format);
    serial->GetFieldUint32(&entry.mVersion.mId);
    serial->GetFieldSint16(&entry.mVersion.mMinor); // slot 11 (the signed reader: the same two bytes)
    serial->GetFieldSint16(&entry.mVersion.mMajor);
    record.Close();
}

// 0xb59d90: the header record rewritten (format 1, the entry's version).
void WriteHeader(const ContentEntry &entry, void *segment)
{
    SerialRecord record;
    if (!record.Open(segment, kHeaderKey, 3)) return;
    ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
    serial->SetFieldUint32(1);
    serial->SetFieldUint32(entry.mVersion.mId);
    serial->SetFieldSint16(int16_t(entry.mVersion.mMinor)); // slot 35 (the signed writer)
    serial->SetFieldSint16(int16_t(entry.mVersion.mMajor));
    record.Close();
}

// 0xb59fc0: the keys record rewritten: format 1, the count, then each key and path in the map's list order.
void WriteKeys(const ContentKeyMap &keys, void *segment)
{
    SerialRecord record;
    if (!record.Open(segment, kKeysKey, 3)) return;
    ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
    serial->SetFieldUint32(1);
    serial->SetFieldUint32(static_cast<uint32_t>(keys.size()));
    for (const auto &item : keys) {
        for (uint32_t word : item.first.mWords) serial->SetFieldUint32(word);
        serial->SetFieldString(const_cast<cRZString *>(&item.second.s));
    }
    record.Close();
}

// 0xb57380: the service 0x2a4915ea asked for (made if need be) and dropped.
void PrimeService()
{
    ScopedRef service{nullptr};
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kPrimeClass, kPrimeIid, &service.mObject);
    service.ReleaseObject();
}

// A packed-file segment from the director's framework object (class 0x086d4a89), counted.
bool NewSegment(ContentRef &segment)
{
    // The answer is al (the slot is declared with a dword result).
    return static_cast<uint8_t>(As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(kSegmentClass, kSegmentIid, segment.AsPPVoid())) != 0;
}

// The resource manager's segments (slot 35 the count, slot 38 copies that many pointers into the array and sets the
// count; nothing is counted), in an operator new[] array of `count` words (all ones when that overflows).
uint32_t *SegmentList(void *manager, uint32_t &count)
{
    count = As<ifaces::cIGZPersistResourceManager>(manager)->GetSegmentCount();
    const uint64_t bytes = static_cast<uint64_t>(count) * 4;
    auto *const list = static_cast<uint32_t *>(::operator new[](bytes >> 32 ? 0xffffffffu : static_cast<size_t>(bytes)));
    RawSlot<38>(manager, list, &count);
    return list;
}

} // namespace

// ---- the free helpers ----

namespace content_registry {

// 0xb58ce0 (388 B).
void BuildCrcTable()
{
    if (gCrcTableBuilt) return;
    for (uint32_t b = 0; b < 256; ++b) {
        uint8_t *const row = gCrcTable[b];
        row[0] = static_cast<uint8_t>(b);
        std::memset(row + 1, 0, 15);
        for (int round = 0; round < 8; ++round) {
            const bool carry = (row[0] & 0x80) != 0;
            for (int j = 0; j < 15; ++j) {
                const uint8_t shifted = static_cast<uint8_t>((row[j] << 1) | (row[j + 1] >> 7));
                row[j] = carry ? static_cast<uint8_t>(shifted ^ kCrcPolynomial[j]) : shifted;
            }
            row[15] = static_cast<uint8_t>((row[15] << 1) ^ (carry ? kCrcPolynomial[15] : 0));
        }
    }
    gCrcTableBuilt = true;
}

// 0xb57ac0 (203 B): the path through the shared cRZFileName (its drive and directory), separators made '/'.
void Normalize(cRZString *path)
{
    cRZFileName *const name = SharedFileName();
    name->Init(path);
    ContentString drivePath;
    drivePath.s.Destruct(); // GetDrivePath builds into raw memory
    name->GetDrivePath(&drivePath.s);
    path->mString.assign(drivePath.s.mString);
    cRZFileName::NormaliseSeparators(path, '/');
}

// 0xb57b90 (121 B): each character lower-cased (tolower on the signed char), '\\' made '/', one trailing '/' dropped.
void LowerCase(cRZString *path)
{
    std::string &text = path->mString;
    const size_t length = text.size();
    for (size_t i = 0; i < length; ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<signed char>(text[i])));
        text[i] = c == '\\' ? '/' : c;
    }
    if (!text.empty() && text.back() == '/') text.pop_back();
}

// 0xb58170 (668 B): the lower-cased path with the user data directory replaced by "%UserDataDir%", else with the first
// game data directory (bits 0..31 the app has) it starts with replaced by "%GameDataDir<bit>%".
void MakePortable(const cRZString *path, cRZString *out)
{
    if (out != path) out->mString.assign(path->mString);
    LowerCase(out);
    ContentString userDir;
    AppPath(kRegistryDirPath, userDir, 0xffffffffu); // the answer is not checked
    LowerCase(&userDir.s);
    const std::string &user = userDir.s.mString;
    if (StartsWith(out->mString, static_cast<uint32_t>(user.size()), user.data(), static_cast<uint32_t>(user.size()))) {
        const char *const token = UserDataDirToken();
        out->mString.replace(0, user.size(), token, std::strlen(token));
        return;
    }
    ContentString dataDir; // reused: a bit the app lacks leaves the last one's text
    for (uint32_t bit = 0; bit < kDataDirCount; ++bit) {
        if (!AppPath(kDataDirPath, dataDir, 1u << bit)) continue;
        LowerCase(&dataDir.s);
        const std::string &dir = dataDir.s.mString;
        if (!StartsWith(out->mString, static_cast<uint32_t>(dir.size()), dir.data(), static_cast<uint32_t>(dir.size()))) continue;
        ContentString token;
        token.s.Sprintf(GameDataDirFormat(), bit);
        const char *const chars = token.s.mString.c_str();
        out->mString.replace(0, dir.size(), chars, std::strlen(chars));
        return;
    }
}

// 0xb57e50 (797 B): the reverse: "%UserDataDir%" (its first [0x10fc484] characters) replaced by the lower-cased user
// data directory; else the first "%GameDataDir<bit>%" the path starts with by that bit's directory when the app has it.
void ExpandPortable(const cRZString *path, cRZString *out)
{
    if (out != path) out->mString.assign(path->mString);
    const char *const token = UserDataDirToken();
    const uint32_t tokenCap = *GameAddress<uint32_t>(kUserDataDirLength);
    if (StartsWith(out->mString, tokenCap, token, static_cast<uint32_t>(std::strlen(token)))) {
        ContentString userDir;
        AppPath(kRegistryDirPath, userDir, 0xffffffffu);
        LowerCase(&userDir.s);
        const char *const chars = userDir.s.mString.c_str();
        out->mString.replace(0, tokenCap, chars, std::strlen(chars));
        return;
    }
    for (uint32_t bit = 0; bit < kDataDirCount; ++bit) {
        ContentString prefix;
        prefix.s.Sprintf(GameDataDirFormat(), bit);
        const std::string &text = prefix.s.mString;
        if (!StartsWith(out->mString, static_cast<uint32_t>(text.size()), text.data(), static_cast<uint32_t>(text.size()))) continue;
        ContentString dataDir;
        if (AppPath(kDataDirPath, dataDir, 1u << bit)) {
            LowerCase(&dataDir.s);
            const char *const chars = dataDir.s.mString.c_str();
            out->mString.replace(0, text.size(), chars, std::strlen(chars));
        }
        return;
    }
}

} // namespace content_registry

using namespace content_registry;

// ---- names ----

bool ContentNameLess::operator()(const ContentString &a, const ContentString &b) const
{
    return _stricmp(a.s.mString.c_str(), b.s.mString.c_str()) < 0;
}

// ---- construction ----

// 0xb56dc0: the service base (id 0x0dc7057a, priority -1,000,000), the members empty. +0x21..+0x23 are not written.
cTSContentRegistry::cTSContentRegistry() : rz::cRZSystemService(kServiceId, kPriority), mInitialized(false) {}

// 0xb57290 (deleting): the members in reverse order (the manager released, the data directories, the registry directory,
// the name map, the entries), then the base.
cTSContentRegistry::~cTSContentRegistry() noexcept {}

cITSContentRegistry *__cdecl cTSContentRegistry::Create() { return new cTSContentRegistry; }

// ---- cIGZUnknown ----

// 0xb59820: null `out` refused; cITSContentRegistry -> +0x1c (counted through the primary face's AddRef), else the
// service's.
bool cTSContentRegistry::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSContentRegistry *>(this);
    AddRef();
    return true;
}

uint32_t cTSContentRegistry::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSContentRegistry::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0xb58b40 (385 B): once: the CRC table, the persist system's resource manager kept, the registry directory and the data
// directories (bits 0..HighestOptionBit()) read and normalised, the registry loaded, then the service 0x2a4915ea primed.
bool cTSContentRegistry::Init()
{
    if (mInitialized) return true;
    BuildCrcTable();
    ScopedRef manager{nullptr};
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kPersistClass, kResourceManagerIid, &manager.mObject);
    mManager.Assign(static_cast<cIGZUnknown *>(manager.mObject));
    appinit::CommonImplSlot4(kRegistryDirPath, Word(&mRegistryDir.s), 1, 0, 0xffffffffu);
    Normalize(&mRegistryDir.s);
    const uint32_t count = appinit::HighestOptionBit() + 1;
    mDataDirs.resize(count);
    uint32_t mask = 1;
    for (uint32_t i = 0; i < count; ++i) {
        appinit::CommonImplSlot4(kDataDirPath, Word(&mDataDirs[i].s), 0, 0, mask);
        if (mDataDirs[i].s.mString.size()) Normalize(&mDataDirs[i].s);
        mask = (mask << 1) | (mask >> 31);
    }
    Load();
    mInitialized = true;
    PrimeService();
    manager.ReleaseObject();
    return true;
}

// 0xb5a300: when initialised: everything saved (slot 5), the manager dropped (the field nulled first).
bool cTSContentRegistry::Shutdown()
{
    if (mInitialized) {
        SaveAll();
        mManager.AsPPVoid(); // nulled, then released
        mInitialized = false;
    }
    return true;
}

// ---- the primary face's slot 13 ----

// 0xb57c10 (174 B).
bool cTSContentRegistry::UpdateCrc(uint8_t *state, const uint8_t *data, uint32_t size, bool continuing)
{
    if (!continuing) std::memset(state, 0xff, 16);
    for (; size; --size, ++data) {
        const uint8_t *const row = gCrcTable[state[0] ^ *data];
        for (int j = 0; j < 15; ++j) state[j] = static_cast<uint8_t>(row[j] ^ state[j + 1]);
        state[15] = row[15];
    }
    return true;
}

// ---- private helpers ----

// 0xb58f80 (618 B): the entries rebuilt (one per option bit up to the highest, plus the registry's own), the registry
// file's path read; when it exists, its header and keys read through a new segment. Then the game data entries' versions.
void cTSContentRegistry::Load()
{
    mEntries.clear();
    const uint32_t highest = appinit::HighestOptionBit();
    mEntries.resize(highest + 2);
    ContentEntry &registry = mEntries[0];
    appinit::CommonImplSlot4(kRegistryFilePath, Word(&registry.mPath.s), 1, 0, 0xffffffffu);
    if (registry.mPath.s.mString.size() && cRZFile::PathExists(*registry.mPath.Interface())) {
        ContentRef segment;
        if (NewSegment(segment)) {
            auto *const packed = As<ifaces::cIGZPersistDBSegment>(segment.Get());
            packed->SetPath(&registry.mPath.s);
            packed->Init();
            if (packed->Open(1, 0)) {
                ReadHeader(registry, segment.Get());
                // 0xb592c0: the keys record: format (dropped), count, then each key and its path, into the entry's
                // map and the name map (the helper's record reference goes before the segment is closed).
                {
                    SerialRecord record;
                    if (record.Open(segment.Get(), kKeysKey, 1)) {
                        ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
                        uint32_t format;
                        uint32_t count;
                        serial->GetFieldUint32(&format);
                        serial->GetFieldUint32(&count);
                        for (uint32_t i = 0; i < count; ++i) {
                            ContentKey key;
                            for (uint32_t &word : key.mWords) serial->GetFieldUint32(&word);
                            ContentString path;
                            serial->GetFieldString(&path.s);
                            registry.mKeys[key] = path;
                            mNames.try_emplace(path, key);
                        }
                        record.Close();
                    }
                }
                packed->Close();
            }
        }
    }
    for (uint32_t bit = 0; bit < highest + 1; ++bit) {
        if (!static_cast<uint8_t>(appinit::HasOptionBits(1u << (bit & 31)))) continue;
        ContentVersion &version = mEntries[bit + 1].mVersion;
        if (bit < 5) {
            version = kDataVersions[bit];
        } else {
            version = ContentVersion{0x7fa7dc7c, 1, static_cast<uint16_t>(bit + 1)};
        }
    }
}

// 0xb59c40: the entries and names emptied, then the registry's own entry made again (version 0, the registry file).
void cTSContentRegistry::Clear()
{
    mEntries.clear();
    mNames.clear();
    ContentString path;
    AppPath(kRegistryFilePath, path, 0xffffffffu);
    const ContentVersion none{0, 0, 0};
    AddEntry(&none, &path.s);
}

// 0xb573b0: a new dirty entry with `version` and `path`, whose file is made writable and deleted; its index.
uint16_t cTSContentRegistry::AddEntry(const ContentVersion *version, cRZString *path)
{
    const size_t index = mEntries.size();
    mEntries.push_back(ContentEntry());
    ContentEntry &entry = mEntries.back();
    entry.mDirty = 1;
    entry.mVersion = *version;
    entry.mPath.s.Copy(*reinterpret_cast<const cIGZString *>(path));
    SetFileAttributes(path);
    RemoveFile(path);
    return static_cast<uint16_t>(index);
}

// 0xb59680 (ret 0xc): for entry 0 only: the segment's path made portable and registered under `key` (the entry dirty).
void cTSContentRegistry::AddSegmentKey(const ContentKey *key, void *segment, uint16_t index)
{
    if (!segment || index != 0) return;
    ContentEntry &entry = mEntries[0];
    ContentString path;
    ContentString portable;
    As<ifaces::cIGZPersistDBSegment>(segment)->GetPath(&path.s);
    MakePortable(&path.s, &portable.s);
    entry.mKeys[*key] = portable;
    entry.mDirty = 1;
    mNames.try_emplace(portable, *key);
}

// Inline in slots 21 and 23: the key's path dropped from the name map, the key from the entry; the entry dirty.
void cTSContentRegistry::EraseKey(ContentEntry &entry, const ContentKey &key)
{
    const auto found = entry.mKeys.find(key);
    if (found != entry.mKeys.end()) mNames.erase(found->second);
    entry.mKeys.erase(key);
    entry.mDirty = 1;
}

// ---- cITSContentRegistry ----

// 0xb59d60: slot 6 for entry 0 when there are entries.
bool cTSContentRegistry::SaveAll() { return mEntries.empty() ? true : SaveEntry(0); }

// 0xb59e70 (ret 4): only entry 0 is saved: when dirty, its file (made writable and deleted when it exists) rewritten
// through a new segment. The entry stays dirty.
bool cTSContentRegistry::SaveEntry(uint16_t index)
{
    if (index >= mEntries.size() || index != 0) return false;
    ContentEntry &entry = mEntries[0];
    if (!entry.mDirty) return true;
    if (cRZFile::PathExists(*entry.mPath.Interface())) {
        SetFileAttributes(&entry.mPath.s);
        RemoveFile(&entry.mPath.s);
    }
    ContentRef segment;
    if (NewSegment(segment)) {
        auto *const packed = As<ifaces::cIGZPersistDBSegment>(segment.Get());
        packed->SetPath(&entry.mPath.s);
        packed->Init();
        if (packed->Open(1, 1)) {
            WriteHeader(entry, segment.Get());
            WriteKeys(entry.mKeys, segment.Get());
            packed->Close();
        }
    }
    return true;
}

// 0xb57770 (ret 8): the package's "crc" property as four hex words, else the CRC of its resources (sorted by type, group
// and instance; read into a buffer of at least 0x800 bytes), each word byte-swapped.
bool cTSContentRegistry::ComputeKey(void *segment, ContentKey *out)
{
    if (!segment) return false;
    if (!gCrcTableBuilt) BuildCrcTable();
    ContentString crc;
    if (GetProperty(segment, "crc", crc.Interface())) {
        const std::string &text = crc.s.mString;
        for (uint32_t i = 0; i < 4; ++i) {
            const size_t pos = i * 8;
            ContentString part; // 0x1ad18d: empty past the end
            if (pos < text.size()) part.s.mString.assign(text, pos, 8);
            char *end;
            out->mWords[i] = std::strtoul(part.s.mString.c_str(), &end, 16);
        }
        return true;
    }
    using KeyList = std::vector<cGZPersistResourceKey, GZServiceAllocator<cGZPersistResourceKey>>;
    KeyList keys;
    auto *const packed = As<ifaces::cIGZPersistDBSegment>(segment);
    packed->GetResourceKeyList(&keys, nullptr);
    std::sort(keys.begin(), keys.end(), [](const cGZPersistResourceKey &a, const cGZPersistResourceKey &b) {
        if (a.type != b.type) return a.type < b.type;
        if (a.group != b.group) return a.group < b.group;
        return a.instance < b.instance;
    });
    // ORIGINAL-BUG: with no resources the original byte-swaps a state nothing wrote (its stack); ours starts from the
    // CRC's initial state.
    uint8_t state[16];
    std::memset(state, 0xff, sizeof(state));
    uint32_t capacity = 0x800;
    auto *buffer = static_cast<uint8_t *>(::operator new[](capacity));
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        uint32_t size = packed->GetRecordSize(&*it);
        if (size > capacity) {
            ::operator delete[](buffer);
            buffer = static_cast<uint8_t *>(::operator new[](size));
            capacity = size;
        }
        packed->ReadRecord(&*it, buffer, &size);
        UpdateCrc(state, buffer, size, it != keys.begin());
    }
    ::operator delete[](buffer);
    for (uint32_t i = 0; i < 4; ++i) {
        out->mWords[i] = uint32_t(state[i * 4]) << 24 | uint32_t(state[i * 4 + 1]) << 16 | uint32_t(state[i * 4 + 2]) << 8 | state[i * 4 + 3];
    }
    return true;
}

// 0xb5aad0 (ret 8): the "version" property, "0" without one.
bool cTSContentRegistry::GetVersion(void *segment, cIGZString *out)
{
    if (!segment) return false;
    if (!GetProperty(segment, "version", out)) out->FromChar("0");
    return true;
}

// 0xb5a0d0 (ret 0xc): `out` emptied, then the properties record's (name, value) pairs searched for `name` (_stricmp).
bool cTSContentRegistry::GetProperty(void *segment, const char *name, cIGZString *out)
{
    if (!segment) return false;
    out->FromChar("");
    SerialRecord record;
    if (!record.Open(segment, kPropertiesKey, 1)) return false;
    ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
    uint32_t count;
    serial->GetFieldUint32(&count);
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i) {
        ContentString key;
        ContentString value;
        serial->GetFieldString(&key.s);
        serial->GetFieldString(&value.s);
        if (_stricmp(name, key.s.mString.c_str()) == 0) {
            found = true;
            out->FromChar(value.s.mString.c_str());
        }
    }
    record.Close();
    return found;
}

// 0xb59800.
uint32_t cTSContentRegistry::EntryCount() { return static_cast<uint32_t>(mEntries.size()); }

// 0xb58410 (ret 8): an entry's version when it has one (a non-zero id).
bool cTSContentRegistry::GetEntryVersion(uint16_t index, ContentVersion *out)
{
    if (index >= mEntries.size() || !mEntries[index].mVersion.mId) return false;
    *out = mEntries[index].mVersion;
    return true;
}

// 0xb58e70 (ret 4).
bool cTSContentRegistry::HasKey(const ContentKey *key)
{
    uint16_t index;
    return FindKey(key, &index);
}

// 0xb58460 (ret 8): the entry of the package the resource manager finds the resource in.
bool cTSContentRegistry::FindEntryForResource(const cGZPersistResourceKey *key, int16_t *out)
{
    if (!mManager.Get()) return false;
    ContentRef segment;
    bool result = false;
    if (As<ifaces::cIGZPersistResourceManager>(mManager.Get())->FindSegmentWithKey(key, segment.AsPPVoid())) {
        ContentString path;
        As<ifaces::cIGZPersistDBSegment>(segment.Get())->GetPath(&path.s);
        result = FindEntryForPath(path.Interface(), out);
    }
    return result;
}

// 0xb58540 (ret 8): 0 for a path in the registry directory, 1 + i for one in data directory i, else -1 (false).
bool cTSContentRegistry::FindEntryForPath(const cIGZString *path, int16_t *out)
{
    {
        ContentString copy;
        CopyCharacters(copy, path);
        ContentString portable;
        MakePortable(&copy.s, &portable.s); // only its calls matter: the result is dropped
    }
    *out = -1;
    ContentString normal;
    CopyCharacters(normal, path);
    Normalize(&normal.s);
    const char *const chars = normal.s.mString.c_str();
    if (mRegistryDir.s.IsEqual(chars, static_cast<uint32_t>(mRegistryDir.s.mString.size()), false)) {
        *out = 0;
    } else {
        const int32_t count = static_cast<int32_t>(mDataDirs.size());
        for (int32_t i = 0; i < count; ++i) {
            const cRZString &dir = mDataDirs[i].s;
            const size_t length = dir.mString.size();
            if (!length) continue;
            if (_strnicmp(chars, dir.mString.c_str(), length) != 0) continue;
            if (!dir.IsEqual(chars, static_cast<uint32_t>(length), false)) continue;
            *out = static_cast<int16_t>(i + 1);
            break;
        }
    }
    return *out != -1;
}

// 0xb588d0 (ret 8): whether entry 0 has the key (*out = 0).
bool cTSContentRegistry::FindKey(const ContentKey *key, uint16_t *out)
{
    const ContentKeyMap &keys = mEntries[0].mKeys;
    if (keys.find(*key) == keys.end()) return false;
    *out = 0;
    return true;
}

// 0xb58e90 (ret 4): a path of entry 0, or (no entry at all) the normalised path is the registry directory's.
bool cTSContentRegistry::IsPathRegistered(const cIGZString *path)
{
    int16_t index;
    if (FindEntryForPath(path, &index)) return index == 0;
    ContentString normal;
    CopyCharacters(normal, path);
    Normalize(&normal.s);
    return mRegistryDir.s.IsEqual(normal.s.mString.c_str(), static_cast<uint32_t>(mRegistryDir.s.mString.size()), false);
}

// 0xb57480 (ret 4): the registry rebuilt from every segment of the resource manager outside the registry directory, under
// a new entry (version `version`, the file 0xedb70c55), which is then saved.
bool cTSContentRegistry::Rebuild(const ContentVersion *version)
{
    Clear();
    ContentString file;
    AppPath(kRebuildFilePath, file, 0xffffffffu);
    const uint16_t index = AddEntry(version, &file.s);
    uint32_t count;
    uint32_t *const list = SegmentList(mManager.Get(), count);
    for (uint32_t i = 0; i < count; ++i) {
        void *const segment = reinterpret_cast<void *>(static_cast<uintptr_t>(list[i]));
        if (!segment) continue;
        ContentString path;
        As<ifaces::cIGZPersistDBSegment>(segment)->GetPath(&path.s);
        cRZFileName *const name = SharedFileName();
        name->Init(&path.s);
        ContentString drivePath;
        drivePath.s.Destruct(); // GetDrivePath builds into raw memory
        name->GetDrivePath(&drivePath.s);
        cRZFileName::NormaliseSeparators(&drivePath.s, '/');
        if (mRegistryDir.s.IsEqual(drivePath.s.mString.c_str(), static_cast<uint32_t>(mRegistryDir.s.mString.size()), false)) continue;
        ContentKey key{};
        if (ComputeKey(segment, &key)) AddSegmentKey(&key, segment, index);
    }
    ::operator delete[](list);
    if (index < mEntries.size()) SaveEntry(index);
    return true;
}

// 0xb5a800 (ret 8): entry 0's keys rebuilt (the name map kept) from the segments outside the registry directory whose
// keys it lacks; the version replaced; then saved.
bool cTSContentRegistry::RebuildEntry(uint16_t index, const ContentVersion *version)
{
    if (mEntries.size() <= index || index != 0) return false;
    ContentEntry &entry = mEntries[0];
    entry.mDirty = 1;
    entry.mVersion = *version;
    entry.mKeys.clear();
    uint32_t count;
    uint32_t *const list = SegmentList(mManager.Get(), count);
    for (uint32_t i = 0; i < count; ++i) {
        void *const segment = reinterpret_cast<void *>(static_cast<uintptr_t>(list[i]));
        if (!segment) continue;
        ContentString path;
        As<ifaces::cIGZPersistDBSegment>(segment)->GetPath(&path.s);
        ContentString normal(path);
        Normalize(&normal.s);
        if (mRegistryDir.s.IsEqual(normal.s.mString.c_str(), static_cast<uint32_t>(mRegistryDir.s.mString.size()), false)) continue;
        ContentKey key{};
        if (ComputeKey(segment, &key) && !HasKey(&key)) AddSegmentKey(&key, segment, 0);
    }
    SaveEntry(0);
    ::operator delete[](list);
    return true;
}

// 0xb5a3b0 (ret 8): the path made portable and registered under `key` in entry 0 (dirty) and the name map.
bool cTSContentRegistry::RegisterPath(const cIGZString *path, const ContentKey *key)
{
    ContentString copy;
    CopyCharacters(copy, path);
    ContentString portable;
    MakePortable(&copy.s, &portable.s);
    ContentString value(portable);
    ContentEntry &entry = mEntries[0];
    entry.mKeys[*key] = value;
    entry.mDirty = 1;
    mNames.try_emplace(value, *key);
    return true;
}

// 0xb5a6f0 (ret 4): the package's key computed and registered.
bool cTSContentRegistry::RegisterSegment(void *segment)
{
    ContentKey key{};
    const bool result = ComputeKey(segment, &key);
    if (result) AddSegmentKey(&key, segment, 0);
    return result;
}

// 0xb5a750 (ret 4): the package's key dropped from its entry and the name map.
bool cTSContentRegistry::UnregisterSegment(void *segment)
{
    if (!segment) return false;
    ContentKey key{};
    uint16_t index = 0;
    if (ComputeKey(segment, &key) && FindKey(&key, &index)) EraseKey(mEntries[index], key);
    return true;
}

// 0xb57cc0 (ret 4): the package unregistered, closed, shut down, taken from the resource manager and its file deleted.
bool cTSContentRegistry::RemoveSegment(void *segment)
{
    if (!segment) return false;
    UnregisterSegment(segment);
    auto *const packed = As<ifaces::cIGZPersistDBSegment>(segment);
    packed->Close();
    packed->Shutdown();
    As<ifaces::cIGZPersistResourceManager>(mManager.Get())->RemoveSegment(static_cast<ifaces::cIGZPersistDBSegment *>(segment));
    ContentString path;
    packed->GetPath(&path.s);
    RemoveFile(&path.s);
    return true;
}

// 0xb57d80 (ret 4): the key dropped from entry 0 and the name map.
void cTSContentRegistry::UnregisterKey(const ContentKey *key)
{
    uint16_t index;
    if (FindKey(key, &index) && index == 0) EraseKey(mEntries[0], *key);
}

// 0xb58950 (ret 0xc): the first entry's path for the key: as stored when it has no '%' (*literal set), else expanded.
bool cTSContentRegistry::GetPathForKey(const ContentKey *key, cIGZString *out, bool *literal)
{
    *literal = false;
    for (ContentEntry &entry : mEntries) {
        const auto found = entry.mKeys.find(*key);
        if (found == entry.mKeys.end()) continue;
        ContentString path(found->second);
        if (path.s.Find("%", 0, true) == -1) {
            *literal = true;
            out->Copy(*path.Interface());
        } else {
            ContentString expanded;
            ExpandPortable(&path.s, &expanded.s);
            out->Copy(*expanded.Interface());
        }
        return true;
    }
    return false;
}

// 0xb5ab10 (ret 8): the properties record rewritten: its size 0, the count of pairs, then each pair whose name and value
// texts (0xaeb800) are both non-empty.
bool cTSContentRegistry::WriteProperties(void *segment, const ContentPropertyIds *ids)
{
    if (!segment) return false;
    SerialRecord record;
    if (!record.Open(segment, kPropertiesKey, 2)) return false;
    As<ifaces::cIGZPersistDBRecord>(record.mRecord)->SetSize(0);
    ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
    serial->SetFieldUint32(static_cast<uint32_t>(ids->size()));
    for (const auto &pair : *ids) {
        auto *const first = const_cast<uint32_t *>(&pair.first);
        auto *const second = const_cast<uint32_t *>(&pair.second);
        if (!*reinterpret_cast<const char *>(Dispatch8(first))) continue;
        if (!*reinterpret_cast<const char *>(Dispatch8(second))) continue;
        ContentString name(reinterpret_cast<const char *>(Dispatch8(first)));
        serial->SetFieldString(&name.s);
        ContentString value(reinterpret_cast<const char *>(Dispatch8(second)));
        serial->SetFieldString(&value.s);
    }
    record.Close();
    return true;
}

// 0xb59870 (ret 8): the properties record's (name, value) pairs into `out` (resized to the count).
bool cTSContentRegistry::ReadProperties(void *segment, ContentProperties *out)
{
    SerialRecord record;
    if (!record.Open(segment, kPropertiesKey, 1)) return false;
    ifaces::cIGZPersistDBSerialRecord *const serial = record.Serial();
    uint32_t count;
    serial->GetFieldUint32(&count);
    out->resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        ContentString name;
        ContentString value;
        serial->GetFieldString(&name.s);
        serial->GetFieldString(&value.s);
        (*out)[i].first.s.mString.assign(name.s.mString);
        (*out)[i].second.s.mString.assign(value.s.mString);
    }
    record.Close();
    return true;
}
