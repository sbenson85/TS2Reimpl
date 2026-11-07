// cTSCustomContentSupport (lane JB): the class in the standalone shape (src/ts/cTSCustomContentSupport.h).
#include "rz/RZMessageServer.h"
#include "rz/PathFileOps.h"
#include "ts/cTSCustomContentSupport.h"
#include "app/SpecialFolders.h"

#include "app/AppInit.h" // CommonImplSlot4
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZApp.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cTSObject.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "simantics/ServiceRefVector.h"

#include <new>
#include <set>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// An original callee nobody has replaced: the message server getter.
void *MessageServer() { return GetCachedMessageServer(); }
// "Make the directory unless it exists" (0x1c5031, ours: lane LY).
void EnsureDirectory(cRZString *path) { folders::EnsureDirectory(path); }
// 0x1c2e25 (__cdecl): deletes the file (cRZFile group, not replaced yet).
void RemoveFile(cRZString *path) { PathDelete(path->AsInterface()); }

// The services the slots look up (the image's cached getters 0xb38fb0 / 0xb390c0 / 0xb39070, same shape).
void *ContentRegistry() { return CachedServiceGetter<0x10faf80, 0x0dc7057a, 0xcdb467ae>(); }
void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }
void *ContentTracker() { return CachedServiceGetter<0x10faf7c, 0xac6965bc, 0x6c69658c>(); }

constexpr uint32_t kRegistryClass = 0x0dc7057a;
constexpr uint32_t kRegistryIid = 0xcdb467ae;
constexpr uint32_t kCommonImplIid = 0xebabd35c; // the app (the framework's slot 37) as cTSAppCommonImpl: it has the paths
constexpr uint32_t kDownloadsPath = 0xece41771; // the app's path id (CommonImplSlot4)
constexpr uint32_t kImageClass = 0x0bd09b0b;
constexpr uint32_t kImageIid = 0xebd09b07;
constexpr uint32_t kPictureIid = 0x20732180;
constexpr uint32_t kSnapshotSize = 300;
constexpr uint32_t kInstallerClass = 0xad50ac22; // the framework's class (iid the same)
constexpr uint32_t kTextClass = 0x8af76dc8;
constexpr uint32_t kTextIid = 0x4af76d8c;
constexpr uint32_t kCommitClass = 0x8bfc2f67;
constexpr uint32_t kCommitIid = 0x0bfc2ed0;
constexpr uint32_t kKeyFormat = 0xdbc7c0; // the image's "%08x" x 4

// A cRZFileName local (the hook-shaped one, built and destroyed by hand).
struct ContentFileName {
    union {
        cRZFileName f;
    };
    explicit ContentFileName(const cRZString *path) { f.Construct(path); }
    ContentFileName(const ContentFileName &) = delete;
    ContentFileName &operator=(const ContentFileName &) = delete;
    ~ContentFileName() { f.Destruct(); }
};
// 0x1c8151 (__thiscall, original: cRZFileName group): 0 when `name` is not in `directory` (with 0: the directory part).
bool SameDirectory(cRZFileName *name, cRZFileName *directory)
{
    return OriginalFunction<uint32_t(__thiscall *)(cRZFileName *, cRZFileName *, uint32_t), 0x1c8151>()(name, directory, 0) != 0;
}

// The object set slot 23 fills (the lot's slot 76): ordered pointers, nodes from the allocator service (0x204140; the tree
// erased by 0x2c9bd0, the head freed through 0x197100).
using ContentObjectSet = std::set<void *, std::less<void *>, GZServiceAllocator<void *>>;

// A new counted cRZString (operator new 0x20, the text copied, count 0), as the original builds them inline.
cRZString *NewString(const std::string &text)
{
    auto *const copy = static_cast<cRZString *>(::operator new(0x20));
    ::new (&copy->mString) std::string(text);
    copy->mVTable = GameAddress<void>(cRZString::kVTable);
    copy->mRefCount = 0;
    return copy;
}

void LowerCase(ContentString &text) { reinterpret_cast<cRZStringHelpers *>(&text.s)->LowerCase(); }

// Slots 20 / 21: the name's characters (slot 7 the length first, then slot 6 the text) lower-cased, looked up.
uint32_t FindName(const ContentNameMap &names, const cIGZString *name)
{
    const uint32_t length = name->Strlen();
    const char *const chars = name->Data();
    ContentString key;
    key.s.mString.assign(chars, length);
    LowerCase(key);
    const auto found = names.find(key);
    return found != names.end() ? found->second : 0;
}

} // namespace

// ---- construction ----

// 0x38e0a0 (2463 B): the service base (id 0xac59cfaf, priority -1,000,000), the members empty, then the category names
// (slot 18 for 0..12, lower-cased; an existing name keeps its first index) and "assets" -> 2, the type names (slot 19 for
// 0..29) and "pet" -> 0x1c, "sim" -> 4, "part" -> 4. +0xc9..+0xcb are not written.
cTSCustomContentSupport::cTSCustomContentSupport()
    : rz::cRZSystemService(kServiceId, kPriority), mState(0), mCurrent(nullptr), mFlagC8(0)
{
    for (uint32_t i = 0; i < kCategoryCount; ++i) {
        ContentString name;
        cTSCustomContentSupport::GetCategoryName(i, name.Interface());
        LowerCase(name);
        mCategories.try_emplace(name, i);
    }
    mCategories.try_emplace(ContentString("assets"), 2u);
    for (uint32_t i = 0; i < kTypeCount; ++i) {
        ContentString name;
        cTSCustomContentSupport::GetTypeName(i, name.Interface());
        LowerCase(name);
        mTypes.try_emplace(name, i);
    }
    mTypes.try_emplace(ContentString("pet"), 0x1cu);
    mTypes.try_emplace(ContentString("sim"), 4u);
    mTypes.try_emplace(ContentString("part"), 4u);
}

// 0x38f420 (459 B): the members in reverse order (the type and category maps, the records, the snapshot path, the package
// files, +0x58, the written files, the persist system and registry released, the downloads path), then the base.
cTSCustomContentSupport::~cTSCustomContentSupport() noexcept {}

cITSCustomContentSupport *__cdecl cTSCustomContentSupport::Create() { return new cTSCustomContentSupport; }

cTSCustomContentSupport *__fastcall cTSCustomContentSupportAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSCustomContentSupport;
}

uint32_t __fastcall cTSCustomContentSupportAdapters::StateCode(const cTSCustomContentSupport *self, void *)
{
    return self->StateCode();
}

// ---- cIGZUnknown ----

// 0x39f030: cITSCustomContentSupport -> +0x20 (counted through the primary face's AddRef), else the service's.
bool cTSCustomContentSupport::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSCustomContentSupport *>(this);
    AddRef();
    return true;
}

uint32_t cTSCustomContentSupport::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSCustomContentSupport::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0x39cbb0 (337 B): whether the app (the framework's slot 37 object) is a cTSAppCommonImpl, the registry and the persist
// system kept, the downloads path filled and made when it is, the import message registered, then the base's.
bool cTSCustomContentSupport::Init()
{
    bool hasPaths = false;
    if (void *const framework = GetServiceObject()) {
        if (void *const app = As<ifaces::cIGZFrameWork>(framework)->Application()) {
            ContentRef common;
            hasPaths = As<ifaces::cIGZApp>(app)->QueryInterface(kCommonImplIid, common.AsPPVoid());
        }
    }
    ScopedRef registry{nullptr};
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kRegistryClass, kRegistryIid, &registry.mObject);
    mRegistry.Assign(static_cast<cIGZUnknown *>(registry.mObject));
    mPersist.Assign(static_cast<cIGZUnknown *>(PersistSystem()));
    if (hasPaths) {
        appinit::CommonImplSlot4(kDownloadsPath, Word(&mDownloadsPath.s), 1, 0, 0xffffffffu);
        EnsureDirectory(&mDownloadsPath.s);
    }
    RawSlot<5, void>(MessageServer(), MessageTarget(), kImportMessage);
    const bool result = rz::cRZSystemService::Init(); // the base's (shared 0x144910): true
    registry.ReleaseObject();
    return result;
}

// 0x39fac0: the import message unregistered, then the base's.
bool cTSCustomContentSupport::Shutdown()
{
    RawSlot<6, void>(MessageServer(), MessageTarget(), kImportMessage);
    return rz::cRZSystemService::Shutdown();
}

// 0x39a1d0 (286 B).
void cTSCustomContentSupport::SaveSnapshot(void *source)
{
    if (!source || !mSnapshotPath.s.Strlen()) return;
    ScopedRef service{nullptr};
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kImageClass, kImageIid, &service.mObject);
    {
        ContentRef picture;
        if (service.mObject && RawSlot<4, void *>(service.mObject)) {
            void *const images = RawSlot<4, void *>(service.mObject);
            void **const out = picture.AsPPVoid();
            const uint32_t made = RawSlot<41>(source, kSnapshotSize, kSnapshotSize, kPictureIid, out);
            if (RawSlot<6, bool>(images, made) && uint8_t(Slot28(Word(picture.Get()), Word(&mSnapshotPath.s))))
                mWrittenFiles.push_back(mSnapshotPath);
        }
    }
    service.ReleaseObject();
}

// ---- cIGZMessageTarget2 ----

// 0x398210: 0xc5 runs the import (slot 12); the message is not checked for null.
bool cTSCustomContentSupport::DoMessage(void *message)
{
    if (As<ifaces::cIGZMessage2Standard>(message)->GetType() != kImportMessage) return false;
    ImportDownloads();
    return true;
}

// ---- cITSCustomContentSupport ----

// 0x39eaa0 (212 B): the files of `file` (slot 13 into a counted vector over the allocator service and a word), installed
// into `target` (slot 5) and the state reset (slot 6) when that worked; slot 15 gets the files, which are then released.
bool cTSCustomContentSupport::InstallFile(void *file, void *target)
{
    ServiceRefVector files{};
    uint32_t word;
    bool ok = uint8_t(Slot13(Word(file), Word(&files), Word(&word))) != 0;
    if (ok) {
        ok = Install(target);
        if (ok) Reset();
    }
    ClosePackages(&files);
    files.ReleaseAndFree();
    return ok;
}

// Folded 0x1ae2f0 (`mov eax, [ecx]; jmp [eax + 0x28]`): slot 10 through the object's own vtable.
void cTSCustomContentSupport::Reset() { Cleanup(); }

// 0x39fd30 (773 B): the state and the current object dropped, the written-files list emptied (the files stay), +0x58
// deleted when there and emptied, every package file (the downloads path + its name) deleted when there, the list
// emptied, the snapshot deleted when there and emptied, each record's file (its resource's slot 11) deleted unchecked,
// the records dropped.
void cTSCustomContentSupport::Cleanup()
{
    mState = 0;
    mCurrent = nullptr;
    mWrittenFiles.clear();
    if (cRZFile::PathExists(*mText58.Interface())) RemoveFile(&mText58.s);
    mText58.s.mString.clear();
    const int32_t count = int32_t(mPackageFiles.size());
    for (int32_t i = 0; i < count; ++i) {
        ContentString path;
        path.s.mString.assign(mDownloadsPath.s.mString);
        path.s.mString.append(mPackageFiles[i].s.mString);
        if (cRZFile::PathExists(*path.Interface())) RemoveFile(&path.s);
    }
    mPackageFiles.clear();
    if (cRZFile::PathExists(*mSnapshotPath.Interface())) RemoveFile(&mSnapshotPath.s);
    mSnapshotPath.s.mString.clear();
    for (ContentRecord &record : mRecords) {
        if (!record.mResource) continue;
        ContentString path;
        RawSlot<11, void>(record.mResource.Get(), path.Interface());
        RemoveFile(&path.s);
    }
    mRecords.clear();
}

// 0x39e820 (632 B).
bool cTSCustomContentSupport::Install(void *target)
{
    ContentRef installer;
    void *const framework = GetDirectorFrameworkObject();
    if (!RawSlot<3, bool>(framework, kInstallerClass, kInstallerClass, installer.AsPPVoid())) return false;
    if (!RawSlot<3, bool>(installer.Get(), target)) return false;
    bool named;
    {
        ContentString name(GameAddress<cRZString>(kCategoryNames)[3]);
        named = RawSlot<5, bool>(installer.Get(), &name.s);
    }
    if (!named) return false;
    ScopedRef texts{nullptr};
    if (void *const service = GetServiceObject()) As<ifaces::cIGZFrameWork>(service)->GetSystemService(kTextClass, kTextIid, &texts.mObject);
    bool result;
    {
        ContentString text;
        RawSlot<19, void>(texts.mObject, text.Interface());
        result = RawSlot<11, bool>(installer.Get(), &text.s);
        if (result) {
            ContentString extra;
            cRZString *const typeNames = GameAddress<cRZString>(kTypeNames);
            for (ContentRecord &record : mRecords) {
                ContentString path;
                if (!record.mResource) continue;
                RawSlot<11, void>(record.mResource.Get(), path.Interface());
                RawSlot<7, void>(installer.Get(), &typeNames[record.mType], &path.s, record.mKey, &record.mName.s, &extra.s);
            }
            result = RawSlot<12, bool>(installer.Get(), 1u);
        }
    }
    texts.ReleaseObject();
    return result;
}

// 0x39cd10 (599 B).
bool cTSCustomContentSupport::Commit()
{
    ServiceRefVector names{};
    for (ContentRecord &record : mRecords) {
        ContentString text;
        if (!record.mResource) continue;
        text.s.Sprintf(GameAddress<const char>(kKeyFormat), record.mKey[0], record.mKey[1], record.mKey[2], record.mKey[3]);
        cRZString *const copy = NewString(text.s.mString);
        RawSlot<1>(copy);
        names.PushBack(copy);
        RawSlot<2>(copy);
    }
    ScopedRef committer{nullptr};
    if (void *const service = GetServiceObject()) As<ifaces::cIGZFrameWork>(service)->GetSystemService(kCommitClass, kCommitIid, &committer.mObject);
    const bool ok = RawSlot<11, bool>(committer.mObject, &names, StateCode(), 1u);
    committer.ReleaseObject();
    if (!ok) Cleanup();
    names.ReleaseAndFree();
    return ok;
}

// 0x39faf0 (572 B): nothing (0) without written files or +0x58. The written files that are there, as new counted strings,
// with the state code, +0x58 and `value` to the service 0x8bfc2f67's slot 9: its result. `unused` is not read.
uint32_t cTSCustomContentSupport::SubmitWrittenFiles(uint32_t, uint32_t value)
{
    if (mWrittenFiles.empty() || !mText58.s.Strlen()) return 0;
    ServiceRefVector files{};
    for (size_t i = 0; i < mWrittenFiles.size(); ++i) {
        if (!cRZFile::PathExists(*mWrittenFiles[i].Interface())) continue;
        cRZString *const copy = NewString(mWrittenFiles[i].s.mString);
        RawSlot<1>(copy);
        files.PushBack(copy);
        RawSlot<2>(copy);
    }
    ScopedRef committer{nullptr};
    if (void *const service = GetServiceObject()) As<ifaces::cIGZFrameWork>(service)->GetSystemService(kCommitClass, kCommitIid, &committer.mObject);
    const uint32_t result = RawSlot<9>(committer.mObject, StateCode(), &files, &mText58.s, value);
    committer.ReleaseObject();
    files.ReleaseAndFree();
    return result;
}

uint32_t cTSCustomContentSupport::StateCode() const
{
    if (mCurrent) return RawSlot<24>(mCurrent) != 1 ? 4u : 0u;
    return mState == 0 ? 3u : 1u;
}

// 0x391040 (713 B).
void cTSCustomContentSupport::ClosePackages(void *list)
{
    auto *const files = static_cast<ServiceRefVector *>(list);
    ContentFileName downloads(&mDownloadsPath.s);
    void *const manager = As<ifaces::cITSPersistSystem>(PersistSystem())->GetResourceManager();
    const uint32_t count = As<ifaces::cIGZPersistResourceManager>(manager)->GetSegmentCount();
    for (uint32_t i = 0; i < files->Size(); ++i) {
        void *const file = files->mFirst[i];
        if (!file) continue;
        const uint32_t length = As<cIGZString>(file)->Strlen();
        const char *const chars = As<cIGZString>(file)->Data();
        ContentString name;
        name.s.mString.assign(chars, length);
        ContentFileName fileName(&name.s);
        if (!SameDirectory(&fileName.f, &downloads.f)) {
            RemoveFile(&name.s);
            continue;
        }
        for (uint32_t j = 0; j < count; ++j) {
            void *const package = As<ifaces::cIGZPersistResourceManager>(manager)->GetSegmentAt(j);
            if (!package) continue;
            ContentString path;
            As<ifaces::cIGZPersistDBSegment>(package)->GetPath(path.Interface());
            if (path.s.CompareTo(*name.Interface(), false) != 0) continue;
            As<ifaces::cIGZPersistDBSegment>(package)->Close();
            if (!As<ifaces::cIGZPersistDBSegment>(package)->Open(1u, 1u)) As<ifaces::cIGZPersistDBSegment>(package)->Open(1u, 0u);
            break;
        }
    }
    for (void **p = files->mFirst; p != files->mLast; ++p)
        if (*p) RawSlot<2>(*p);
    files->mLast = files->mFirst;
}

// 0x39cb10 (97 B): the sizes of the written files plus that of +0x58 when it is set and there.
uint32_t cTSCustomContentSupport::WrittenFilesSize()
{
    uint32_t total = 0;
    const size_t count = mWrittenFiles.size();
    for (size_t i = 0; i < count; ++i) total += cRZFile::LengthOfPath(*mWrittenFiles[i].Interface());
    if (mText58.s.Strlen() && cRZFile::PathExists(*mText58.Interface())) total += cRZFile::LengthOfPath(*mText58.Interface());
    return total;
}

// 0x39bed0 / 0x39bf00: the static name (the first one for an index past the table) assigned to `out` (its slot 16).
void cTSCustomContentSupport::GetCategoryName(uint32_t index, cIGZString *out)
{
    cRZString *const names = GameAddress<cRZString>(kCategoryNames);
    RawSlot<16, void *>(out, &names[index < kCategoryCount ? index : 0]);
}
void cTSCustomContentSupport::GetTypeName(uint32_t index, cIGZString *out)
{
    cRZString *const names = GameAddress<cRZString>(kTypeNames);
    RawSlot<16, void *>(out, &names[index < kTypeCount ? index : 0]);
}

// 0x39b0f0 / 0x39c470 (264 B each).
void cTSCustomContentSupport::FindCategory(const cIGZString *name, uint32_t *out) { *out = FindName(mCategories, name); }
void cTSCustomContentSupport::FindType(const cIGZString *name, uint32_t *out) { *out = FindName(mTypes, name); }

// 0x390be0 (407 B): false when the lot source has no lot (its slot 1) or one of the lot's objects (its slot 76 into a set,
// handed to the object manager's slot 55) is blocked: an object whose slot 4 object does not answer 0x2a (slot 3) is looked
// up as its definition (the manager's slot 21, else itself), and the blocked list (the app's slot 26) is asked about that
// one's slot 28 (slot 42).
bool cTSCustomContentSupport::LotObjectsAllowed(void *lotSource)
{
    void *const lot = RawSlot<1, void *>(lotSource);
    if (!lot) return false;
    void *const blocked = RawSlot<26, void *>(GetCachedSingleton());
    void *const objects = RawSlot<24, void *>(GetCachedSingleton());
    ContentObjectSet found;
    RawSlot<55, void>(objects, RawSlot<76>(lot, &found));
    bool allowed = true;
    for (void *const object : found) {
        if (!object) continue;
        void *const kind = RawSlot<4, void *>(object);
        if (kind && RawSlot<3>(kind, 0x2au)) continue;
        void *const definition = RawSlot<21, void *>(objects, object);
        if (RawSlot<42>(blocked, RawSlot<28>(definition ? definition : object))) {
            allowed = false;
            break;
        }
    }
    return allowed;
}

// 0x39e6e0 (319 B): the resource's key (its slot 3's slot 3 into a 16-byte buffer, the entry out) known to the resource
// manager (the persist system's slot 3, its slot 34): the registry's answer (slot 16) for the file the entry names (slot 11).
bool cTSCustomContentSupport::IsRegistered(void *resource)
{
    if (!resource) return false;
    void *const registry = ContentRegistry();
    void *const persist = PersistSystem();
    if (!registry) return false;
    ContentRef entry;
    void *const key = RawSlot<3, void *>(resource);
    void *const manager = As<ifaces::cITSPersistSystem>(persist)->GetResourceManager();
    uint32_t buffer[4];
    void **const out = entry.AsPPVoid();
    if (!RawSlot<34, bool>(manager, RawSlot<3>(key, buffer, out))) return false;
    ContentString path;
    As<ifaces::cIGZPersistDBSegment>(entry.Get())->GetPath(path.Interface());
    return RawSlot<16, bool>(registry, &path.s);
}

// 0x3975d0 (338 B): as IsRegistered, then the registry's slot 22 for the entry; both true: the tracker (0xac6965bc) gets the
// resource (slot 4). Needs all three services.
bool cTSCustomContentSupport::Register(void *resource)
{
    if (!resource) return false;
    void *const registry = ContentRegistry();
    void *const persist = PersistSystem();
    void *const tracker = ContentTracker();
    if (!persist || !registry || !tracker) return false;
    ContentRef entry;
    void *const key = RawSlot<3, void *>(resource);
    void *const manager = As<ifaces::cITSPersistSystem>(persist)->GetResourceManager();
    uint32_t buffer[4];
    void **const out = entry.AsPPVoid();
    if (!RawSlot<34, bool>(manager, RawSlot<3>(key, buffer, out))) return false;
    ContentString path;
    As<ifaces::cIGZPersistDBSegment>(entry.Get())->GetPath(path.Interface());
    if (!RawSlot<16, bool>(registry, &path.s) || !RawSlot<22, bool>(registry, entry.Get())) return false;
    RawSlot<4, void>(tracker, resource);
    return true;
}

// ---- slots still game code (handoff D2): the original on our object, `this` its face ----

uint32_t cTSCustomContentSupport::Slot4(uint32_t a, uint32_t b, uint32_t c)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0x39eb80>()(Content(), a, b, c);
}
uint32_t cTSCustomContentSupport::Slot8(uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x39f680>()(Content(), a);
}
uint32_t cTSCustomContentSupport::Slot11(uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 0x39cf70>()(Content(), a, b);
}
uint32_t cTSCustomContentSupport::ImportDownloads()
{
    return OriginalFunction<uint32_t(__thiscall *)(void *), 0x39d400>()(Content());
}
uint32_t cTSCustomContentSupport::Slot13(uint32_t a, uint32_t b, uint32_t c)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 0x391c20>()(Content(), a, b, c);
}
uint32_t cTSCustomContentSupport::Slot14(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0x3923f0>()(
        Content(), a, b, c, d, e);
}
uint32_t cTSCustomContentSupport::Slot16() { return OriginalFunction<uint32_t(__thiscall *)(void *), 0x39a2f0>()(Content()); }
uint32_t cTSCustomContentSupport::Slot22(uint32_t a)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t), 0x390400>()(Content(), a);
}
uint32_t cTSCustomContentSupport::Slot24(uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 0x397730>()(Content(), a, b);
}
// 0x390d80 (695 B): both answers false first. With an owner and a resource that is registered (slot 25): the resource's
// entry (as IsRegistered gets it, the manager from the kept persist system, used unchecked) known to the manager: the
// persist system's entries for the owner (its slot 48 with the owner's slot 76) searched for that entry or one naming the
// same file (slot 11) -> *inOwner. Then, registered or not past the first checks, the app's slot 26 object's list (slot 31):
// an object that answers slot 188, not slot 25 (0x10000), and slot 295 for the resource -> *byObject.
void cTSCustomContentSupport::ResourceInUse(void *owner, void *resource, bool *byObject, bool *inOwner)
{
    *byObject = false;
    *inOwner = false;
    if (!resource || !owner || !IsRegistered(resource)) return;
    ContentRef entry;
    void *const key = RawSlot<3, void *>(resource);
    void *const manager = As<ifaces::cITSPersistSystem>(mPersist.Get())->GetResourceManager();
    uint32_t buffer[4];
    void **const out = entry.AsPPVoid();
    if (RawSlot<34, bool>(manager, RawSlot<3>(key, buffer, out))) {
        ContentString name;
        As<ifaces::cIGZPersistDBSegment>(entry.Get())->GetPath(name.Interface());
        ServiceRefVector entries{};
        As<ifaces::cITSPersistSystem>(mPersist.Get())->GetSegmentsForGroup(RawSlot<76>(owner), &entries);
        const int32_t count = int32_t(entries.Size());
        for (int32_t i = 0; i < count; ++i) {
            void *const other = entries.mFirst[i];
            ContentString otherName;
            As<ifaces::cIGZPersistDBSegment>(other)->GetPath(otherName.Interface());
            if (other == entry.Get() || otherName.s.mString == name.s.mString) {
                *inOwner = true;
                break;
            }
        }
        entries.ReleaseAndFree();
    }
    void *const objects = RawSlot<26, void *>(GetCachedSingleton());
    if (!objects) return;
    struct Node {
        Node *next;
        Node *prev;
        void *object;
    };
    Node *const *const list = RawSlot<31, Node *const *>(objects);
    for (Node *node = (*list)->next; node != *list; node = node->next) {
        void *const object = node->object;
        if (object && As<ifaces::cTSObject>(object)->IsAvailable() && !As<ifaces::cTSObject>(object)->TestFlags(0x10000u) && RawSlot<295, bool>(object, resource)) {
            *byObject = true;
            return;
        }
    }
}
uint32_t cTSCustomContentSupport::Slot28(uint32_t image, uint32_t path)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 0x396070>()(Content(), image, path);
}
uint32_t cTSCustomContentSupport::Slot29(uint32_t a, uint32_t b)
{
    return OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 0x398240>()(Content(), a, b);
}
