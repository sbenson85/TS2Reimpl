#include "gz/cGZDBSegmentDirectoryFiles.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/DirectoryScan.h"
#include "rz/PathFileOps.h"
#include "rz/cRZRandom.h"
#include "rz/cRZFile.h"
#include "rz/cRZCallbackTimer.h"
#include "gz/cGZPersistDBSegment.h"
#include "gz/cIGZString.h"
#include "rz/cRZRefCount.h"
#include "rz/cRZString.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StringKeyParser.h"
#include "app/AppServices2.h"
#include "gz/cGZRESIDtoExtensionMap.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include "app/ExceptionReporter.h" // appbase::SplitPath

namespace {

// A cRZString on the stack (the game's vtable), destroyed at the end of its scope.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// Original callees (cdecl path helpers, not replaced yet).
// 0x1c2732: out = drive + directory + name + extension (each a cRZString or null; _makepath-style).
using JoinPathFn = void(__cdecl *)(cRZString *out, const cRZString *drive, const cRZString *directory,
                                    const cRZString *name, const cRZString *extension);
// 0x1c1ea9: the file name part of a path (with its extension when asked) into out's FromChar.
using FileNameOfFn = bool(__cdecl *)(const cIGZString *path, cIGZString *out, uint32_t withExtension);
// 0x1c1e0e: the extension of a path (with its '.') into out's FromChar.
using ExtensionOfFn = bool(__cdecl *)(const cIGZString *path, cIGZString *out);

using KeyFromPathFn = bool(__thiscall *)(void *, const cRZString *, DirResourceKey *, void *);  // +0xac slot 4
using IsResourceFn = bool(__thiscall *)(void *, const cIGZString *, void *);                     // +0xac slot 3
using KeyInPlaceFn = void(__thiscall *)(void *, DirResourceKey *, DirResourceKey *);              // namespace slot 4
using TypeForExtensionFn = bool(__thiscall *)(void *, const cIGZString *, uint32_t *);           // extension map slot 4
using KeyFromNameFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, DirResourceKey *); // slot 19
using GetServiceFn = void(__thiscall *)(void *, uint32_t, uint32_t, void **);                    // framework slot 5
using RegisterFn = bool(__thiscall *)(void *, const char *, void *, void *);                     // listener slot 7

constexpr uint32_t kOpenFailedMessage = 0xe5ef20;    // "...::Open: Unable to open for one of various reasons."
constexpr uint32_t kNoWriteDirMessage = 0xe5eec8;    // "...::Open: Directory open for write access doesn't exist."
constexpr uint32_t kListenerName = 0xe5ea00;         // "CN_DBSDF"
constexpr uint32_t kListenerCallback = 0x99fb10;
constexpr uint32_t kListenerService = 0x25076b9e;
constexpr uint32_t kListenerInterface = 0xa237613c;

// Eight hex digits at text, as the original reads them (a copy of the eight characters through strtoul).
uint32_t Hex8(const char *text)
{
    std::string digits(text, 8);
    return static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, 16));
}

bool IsHexDigit(char c)
{
    return std::isxdigit(static_cast<signed char>(c)) != 0; // the original passes the char sign-extended
}

using AddRefFn = uint32_t(__thiscall *)(void *);
using VoidFn = void(__thiscall *)(void *);
using FromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
using BoolFn = bool(__thiscall *)(void *);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using U32Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using U32Fn3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using OpenFn = bool(__thiscall *)(void *, uint8_t, uint8_t);
// Slot 6 (Open) declared with its real bool result: MSVC then loads the slot into a register for the call, as the original does.
struct OpenSlot : vtable_detail::Pad<6> {
    virtual bool __thiscall call(uint8_t, uint8_t) = 0;
};
using TimerCallbackFn = void(__thiscall *)(void *, void *, void *);
using TimerStartFn = void(__thiscall *)(void *, uint32_t, void *);
using KeyStringFn = bool(__thiscall *)(void *, const DirResourceKey *, void *);
using AppendFn = void(__thiscall *)(void *, const char *, uint32_t);
using KeyTranslateFn = void(__thiscall *)(void *, const DirResourceKey *, DirResourceKey *);
using KeyNameFn = bool(__thiscall *)(void *, const DirResourceKey *, cIGZString *, uint32_t, void *);
using ExtensionFn = bool(__thiscall *)(void *, uint32_t *, uint32_t);
using LogFn = void(__thiscall *)(void *, const char *);
using SprintfFn = void(__cdecl *)(cIGZString *, const char *, ...);
using OpenRecordFn = uint8_t(__thiscall *)(void *, const DirResourceKey *, void **, uint32_t);
using KeyOfFn = const DirResourceKey *(__thiscall *)(void *);
using FilterFn = bool(__thiscall *)(void *, const DirResourceKey *);

constexpr uint32_t kRefreshCallback = 0x9a2b90;   // the timer callback StartRefreshTimer installs
constexpr size_t kScanSize = 0x5c; // the object at +0x6c; 0x1c49eb is its destructor
constexpr uint32_t kRZStringVTable = 0xd9b470;
constexpr uint32_t kUnknownTypeMessage = 0xe5f138; // GetFileNameFromKey's "unrecognized resource type" text
constexpr uint32_t kInstanceFormat = 0xe5f120;      // format: the instance as 8 hex digits
constexpr uint32_t kInstanceGroupFormat = 0xe5f128; // format: instance then group, 8 hex digits each

// The subobject of a cGZDBSegmentDirectoryFiles, at +4.
void *SubObject(void *object)
{
    return static_cast<uint8_t *>(object) + 4;
}

const uint8_t *Bytes(const void *view)
{
    return static_cast<const uint8_t *>(view);
}

uint8_t *Bytes(void *view)
{
    return static_cast<uint8_t *>(view);
}

} // namespace

bool cGZDBSegmentDirectoryFiles::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kIdSelf) {
        // The original tail-jumps to the base's QueryInterface with this + 4 and the same arguments.
        return reinterpret_cast<cGZPersistDBSegment *>(SubObject(this))->QueryInterface(riid, ppvObj);
    }
    *ppvObj = this;
    VirtualCall<AddRefFn, 1>(this); // AddRef through the vtable of this
    return true;
}

uint32_t cGZDBSegmentDirectoryFiles::Release()
{
    int32_t count = RZAtomicAdd(&mRefCount, -1);
    if (count != 0) return static_cast<uint32_t>(count);
    void *sub = SubObject(this);
    VirtualCall<VoidFn, kDeleteSlot>(sub);
    return 0;
}

void cGZDBSegmentDirectoryFiles::SetFlag3C()
{
    if (mFlag3C == 0) mFlag3C = 1;
}

void cGZDBSegmentDirectoryFiles::GetPath(cIGZString *out)
{
    // std::string's data() is inline up to 15 characters, the same test the original makes on capacity.
    VirtualCall<FromCharFn, 3>(out, mPath.data(), static_cast<uint32_t>(mPath.size()));
}

bool DirectorySegmentView::IsOpen() const
{
    return (Bytes(this)[0x40] & 3) != 0;
}

bool DirectorySegmentView::IsWritable() const
{
    uint32_t state = *reinterpret_cast<const uint32_t *>(Bytes(this) + 0x40);
    return ((state >> 1) & 1) != 0;
}

bool DirectorySegmentView::AutoOpen(uint8_t state, uint8_t flag)
{
    Bytes(this)[0x44] = state;
    Bytes(this)[0x45] = flag;
    return true;
}

bool DirectorySegmentView::SetSegmentID(const uint32_t *id)
{
    uint8_t *sub = Bytes(this);
    uint32_t value = *id;
    bool first = *reinterpret_cast<uint32_t *>(sub + 0x6c) == 0;
    *reinterpret_cast<uint32_t *>(sub + 0x70) = value;
    if (first) *reinterpret_cast<uint32_t *>(sub + 0x6c) = value;
    return true;
}

bool cGZDBSegmentDirectoryFiles::Vf9(void *arg)
{
    if ((mState & 3) != 0) return false;
    if (VirtualCall<BoolFn, 8>(arg)) return false;
    // The embedded object at +0x4c: its vtable pointer is mEmbedded4C, and slot 16 takes arg.
    void *embedded = reinterpret_cast<uint8_t *>(this) + 0x4c;
    VirtualCall<VoidPtrFn, 16>(embedded, arg);
    // ORIGINAL-BUG: with an empty path the original reads the byte before the string's buffer (index -1):
    // inside this object for inline storage, before the heap block otherwise. The byte decides whether a
    // separator is appended, so the garbage can change the result. An empty path is a valid state; reproduced.
    const char *data = mPath.data();
    char last = data[static_cast<int32_t>(mPath.size()) - 1];
    if (last != '\\' && last != '/') mPath.push_back('\\');
    return true;
}

void cGZDBSegmentDirectoryFiles::SetBytes78(uint8_t a, uint8_t b)
{
    mByte78 = a;
    mByte79 = b;
}

void cGZDBSegmentDirectoryFiles::SetByte7A(uint8_t a)
{
    mByte7A = a;
}

bool cGZDBSegmentDirectoryFiles::SetObjectAC(void *obj)
{
    void *old = mObjectAC;
    if (obj != old) {
        if (obj) VirtualCall<VoidFn, 1>(obj); // AddRef on the new object first
        mObjectAC = obj;
        if (old) VirtualCall<VoidFn, 2>(old); // then Release the old one (its result is not used)
    }
    return true;
}

void cGZDBSegmentDirectoryFiles::SetByteB0(uint8_t a)
{
    mByteB0 = a;
}

uint32_t cGZDBSegmentDirectoryFiles::Vf31(uint32_t a, uint32_t b)
{
    // The original pushes 2 as a third argument and returns slot 32's eax unchanged.
    return VirtualCall<U32Fn3, 32>(this, a, b, 2);
}

uint32_t cGZDBSegmentDirectoryFiles::Vf36(uint32_t a, uint32_t b, uint32_t c)
{
    // The original jumps to slot 35 with the same stack arguments; slot 35 pops them.
    return VirtualCall<U32Fn3, 35>(this, a, b, c);
}

void DirectorySegmentView::Init()
{
    reinterpret_cast<cGZDBSegmentDirectoryFiles *>(Bytes(this) - 4)->SetFlag3C();
}

void DirectorySegmentView::GetPath(cIGZString *out)
{
    reinterpret_cast<cGZDBSegmentDirectoryFiles *>(Bytes(this) - 4)->GetPath(out);
}

// ---- tables and helpers ----

DirOpenRecord::~DirOpenRecord()
{
    if (record) VirtualCall<VoidFn, 2>(record); // Release
}

void cGZDBSegmentDirectoryFiles::EnsureOpen()
{
    if ((mState & 3) != 0) return;
    if (mAutoOpen48 == 0 && mAutoOpen49 == 0) return;
    if (static_cast<OpenSlot *>(static_cast<void *>(this))->call(mAutoOpen48, mAutoOpen49)) return;
    // Opening for writing failed: try again read-only, when that was asked for at all.
    if (mAutoOpen49 != 0 && mAutoOpen48 != 0) static_cast<OpenSlot *>(static_cast<void *>(this))->call(mAutoOpen48, 0);
}

DirFileMap::iterator cGZDBSegmentDirectoryFiles::FindEntry(const DirResourceKey &key)
{
    auto it = mFiles.find(key);
    if (it != mFiles.end()) return it;
    bool byInstance = mByteB0 != 0 && key.resource == 0;
    if (mByteB1 != 0 && key.resource != 0) {
        // A key with a resource part may stand for the file stored without one.
        DirResourceKey plain{key.type, key.group, key.instance, 0};
        return mFiles.find(plain);
    }
    if (!byInstance) return mFiles.end();
    auto probe = mProbes.find(key.instance);
    if (probe == mProbes.end()) {
        // First time for this instance: the first file in table order with the same type, group and instance,
        // whatever its resource part, and the answer is cached either way.
        for (auto node = mFiles.begin(); node != mFiles.end(); ++node) {
            const DirResourceKey &k = node->first;
            if (k.type == key.type && k.group == key.group && k.instance == key.instance) {
                DirInstanceProbe &cached = mProbes[key.instance];
                cached.resource = k.resource;
                cached.found = true;
                return node;
            }
        }
        DirInstanceProbe &cached = mProbes[key.instance];
        cached.found = false;
        cached.resource = 0;
        return mFiles.end();
    }
    if (!probe->second.found) return mFiles.end();
    DirResourceKey cachedKey{key.type, key.group, key.instance, probe->second.resource};
    return mFiles.find(cachedKey);
}

void cGZDBSegmentDirectoryFiles::EraseProbe(const DirResourceKey *key)
{
    mProbes.erase(key->instance);
}

void __fastcall cGZDBSegmentDirectoryFiles::ClearFiles(DirFileMap *files)
{
    files->clear();
}

void DirFileEntry::RemoveOpenRecord(const void *face)
{
    for (auto it = open.begin(); it != open.end(); ++it) {
        if (it->Face() == face) {
            open.erase(it); // unlinks, then releases the record
            return;
        }
    }
}

void cGZDBSegmentDirectoryFiles::StartRefreshTimer()
{
    void *timer = &mTimer;
    if (VirtualCall<BoolFn, 8>(timer)) return; // already running
    int32_t seconds = reinterpret_cast<cRZRandom *>(mRandom)->RandomSint32RangeUniform(
        static_cast<int32_t>(mRefreshSeconds), static_cast<int32_t>(mRefreshSeconds * 2));
    VirtualCall<TimerCallbackFn, 9>(timer, GameAddress(kRefreshCallback), this);
    VirtualCall<TimerStartFn, 6>(timer, static_cast<uint32_t>(seconds * 1000), this);
}

// ---- slots ----

bool cGZDBSegmentDirectoryFiles::Shutdown()
{
    if (mFlag3C == 0) return true;
    mFlag3C = 0;
    void *timer = &mTimer;
    VirtualCall<VoidFn, 7>(timer); // stop
    if ((mState & 3) != 0) {
        void *sub = SubObject(this);
        VirtualCall<BoolFn, 9>(sub); // Close
    }
    mNextInstance = 1;
    mPath.clear();
    void *scan = mScan;
    mSegmentIDOnce = 0;
    mSegmentID = 0;
    mByte78 = 1;
    mByte79 = 0;
    if (scan) {
        DirectoryScanAdapters::Destruct(static_cast<DirectoryScan *>(scan), nullptr); // ours (lane LY), as 0x1c49eb
        ::operator delete(scan, kScanSize);
        mScan = nullptr;
    }
    void *extensions = &mExtensionMap;
    VirtualCall<VoidFn, 6>(extensions); // clear
    ClearFiles(&mFiles);
    if (void *obj = mObjectAC) {
        mObjectAC = nullptr;
        VirtualCall<VoidFn, 2>(obj);
    }
    return reinterpret_cast<cGZPersistDBSegment *>(SubObject(this))->Shutdown();
}

uint32_t cGZDBSegmentDirectoryFiles::Vf14(uint32_t a, uint32_t b)
{
    void *extensions = &mExtensionMap;
    return VirtualCall<U32Fn2, 3>(extensions, a, b);
}

bool cGZDBSegmentDirectoryFiles::GetFileName(const DirResourceKey *key, cIGZString *out)
{
    EnsureOpen();
    auto it = FindEntry(*key);
    if (it != mFiles.end()) {
        VirtualCall<VoidPtrFn, 17>(out, &it->second.name); // Copy
        return true;
    }
    return false;
}

bool cGZDBSegmentDirectoryFiles::GetFilePath(const DirResourceKey *key, cIGZString *out)
{
    DirFileName name{GameAddress(kRZStringVTable), std::string(), 0};
    if (!VirtualCall<KeyStringFn, 17>(this, key, &name)) return false;
    VirtualCall<VoidPtrFn, 17>(out, &mEmbedded4C);                    // Copy the path
    VirtualCall<AppendFn, 20>(out, name.text.data(), static_cast<uint32_t>(name.text.size()));
    return true;
}

bool cGZDBSegmentDirectoryFiles::NewKey(uint32_t type, DirResourceKey *out)
{
    EnsureOpen();
    if (mFlag3C == 0) return false;
    out->group = mSegmentIDOnce;
    out->type = type;
    while (mNextInstance != 0xffffffffu) {
        out->instance = mNextInstance;
        ++mNextInstance;
        if (mFiles.find(*out) == mFiles.end()) return true;
    }
    return false;
}

uint32_t cGZDBSegmentDirectoryFiles::GetFileNames(cIGZString **names, uint32_t max)
{
    void *lock = &mLock;
    VirtualCall<VoidFn, 1>(lock);
    EnsureOpen();
    uint32_t n;
    if (names == nullptr) {
        n = static_cast<uint32_t>(mFiles.size());
    } else {
        n = 0;
        for (auto it = mFiles.begin(); it != mFiles.end() && n < max && names[n] != nullptr; ++it, ++n) {
            const std::string &text = it->second.name.text;
            VirtualCall<FromCharFn, 3>(names[n], text.data(), static_cast<uint32_t>(text.size()));
        }
    }
    VirtualCall<VoidFn, 2>(lock);
    return n;
}

bool cGZDBSegmentDirectoryFiles::SetAutoRefresh(uint8_t enable, uint32_t seconds)
{
    if (mAutoRefresh != enable) {
        if (enable == 0) {
            void *timer = &mTimer;
            VirtualCall<VoidFn, 7>(timer);
        } else if (mFlag3C != 0) {
            StartRefreshTimer();
        }
        mAutoRefresh = enable;
    }
    mRefreshSeconds = seconds < 15 ? 15 : seconds;
    return true;
}

bool cGZDBSegmentDirectoryFiles::GetFileNameFromKey(const DirResourceKey *key, cIGZString *out, uint32_t extension)
{
    DirResourceKey local = *key;
    if (void *ns = mNamespace) VirtualCall<KeyTranslateFn, 3>(ns, key, &local);
    if (void *obj = mObjectAC) return VirtualCall<KeyNameFn, 5>(obj, &local, out, extension, this);
    uint32_t type = local.type;
    void *extensions = &mExtensionMap;
    if (!VirtualCall<ExtensionFn, 5>(extensions, &type, extension)) {
        VirtualCall<LogFn, 30>(this, GameAddress<const char>(kUnknownTypeMessage));
        return false;
    }
    auto sprintf = VirtualSlot<SprintfFn>(out, 31);
    if (local.group == mSegmentIDOnce) {
        sprintf(out, GameAddress<const char>(kInstanceFormat), local.instance);
    } else {
        sprintf(out, GameAddress<const char>(kInstanceGroupFormat), local.instance, local.group);
    }
    return true;
}

cGZDBSegmentDirectoryFiles *DirectorySegmentView::Primary()
{
    return reinterpret_cast<cGZDBSegmentDirectoryFiles *>(Bytes(this) - 4);
}

bool DirectorySegmentView::QueryInterfaceThunk(uint32_t riid, void **ppvObj)
{
    return Primary()->QueryInterface(riid, ppvObj);
}

uint32_t DirectorySegmentView::ReleaseThunk()
{
    return Primary()->Release();
}

bool DirectorySegmentView::ShutdownThunk()
{
    return Primary()->Shutdown();
}

bool DirectorySegmentView::SetPathThunk(void *path)
{
    return Primary()->Vf9(path);
}

void DirectorySegmentView::DeleteSelf()
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    VirtualCall<BoolFn, 5>(self); // Shutdown, through the primary's vtable
    reinterpret_cast<cGZPersistDBSegment *>(this)->vf34();
}

bool DirectorySegmentView::Close()
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    if ((self->mState & 3) == 0) return true;
    for (auto &file : self->mFiles) {
        if (file.second.open.empty()) continue;
        for (auto &rec : file.second.open) VirtualCall<VoidFn, 15>(rec.record); // not checked for null
        file.second.open.clear(); // releases each record
    }
    self->mState = 0;
    void *extensions = &self->mExtensionMap;
    VirtualCall<VoidFn, 6>(extensions);
    cGZDBSegmentDirectoryFiles::ClearFiles(&self->mFiles);
    if (void *scan = self->mScan) {
        DirectoryScanAdapters::Destruct(static_cast<DirectoryScan *>(scan), nullptr); // ours (lane LY), as 0x1c49eb
        ::operator delete(scan, kScanSize);
        self->mScan = nullptr;
    }
    return true;
}

bool DirectorySegmentView::TestForRecord(const DirResourceKey *key)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    self->EnsureOpen();
    return self->FindEntry(*key) != self->mFiles.end();
}

bool DirectorySegmentView::OpenRecord(const DirResourceKey *key, void **out, uint32_t mode)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    self->EnsureOpen();
    uint32_t access;
    if ((mode & 2) != 0) {
        access = 2;
    } else if ((mode & 1) != 0) {
        access = 1;
    } else {
        return false;
    }
    return VirtualCall<OpenRecordFn, 32>(self, key, out, access) != 0;
}

bool DirectorySegmentView::CreateNewRecord(const DirResourceKey *key, void **out)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    bool ok = VirtualCall<OpenRecordFn, 32>(self, key, out, 3) != 0;
    if (ok) self->EraseProbe(key); // a new file may answer an instance-only lookup that failed before
    return ok;
}

bool DirectorySegmentView::FinishRecord(void *face, int slot)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    const DirResourceKey *key = VirtualCall<KeyOfFn, 3>(face);
    auto it = self->FindEntry(*key);
    if (it == self->mFiles.end()) return false;
    for (auto &rec : it->second.open) {
        if (rec.Face() != face) continue;
        if (rec.record == nullptr) return false;
        VirtualSlot<VoidFn>(rec.record, slot)(rec.record);
        it->second.RemoveOpenRecord(face);
        return true;
    }
    return false;
}

bool DirectorySegmentView::CloseRecord(void *face)
{
    return FinishRecord(face, 15);
}

bool DirectorySegmentView::AbortRecord(void *face)
{
    return FinishRecord(face, 16);
}

uint32_t DirectorySegmentView::GetResourceKeyList(DirKeyVector *out, void *filter)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    self->EnsureOpen();
    uint32_t count = 0;
    for (auto &file : self->mFiles) {
        if (filter != nullptr && !VirtualCall<FilterFn, 3>(filter, &file.first)) continue;
        if (out != nullptr) out->push_back(file.first);
        ++count;
    }
    return count;
}

uint32_t DirectorySegmentView::Vf44(uint32_t arg)
{
    // The original pushes arg, then 0, so slot 45 sees (0, arg); its eax is returned unchanged.
    return VirtualCall<U32Fn2, 45>(this, 0, arg);
}

bool DirectorySegmentView::OpenThunk(uint8_t read, uint8_t write)
{
    return Primary()->Open(read, write);
}

// ---- opening, file names (lane NP) ----

bool cGZDBSegmentDirectoryFiles::Open(uint8_t read, uint8_t write)
{
    if ((mState & 3) != 0) return true;
    if (mFlag3C == 0 || mPath.size() == 0 || (read == 0 && write == 0)) {
        VirtualCall<LogFn, 30>(this, GameAddress<const char>(kOpenFailedMessage));
        mState = 0;
        return false;
    }
    mState = read != 0 ? 1u : 0u;
    if (write != 0) {
        mState |= 2;
        // +0x3d: writing needs the directory to exist already.
        if (mWriteNeedsDirectory != 0 &&!as2::DirectoryExists(reinterpret_cast<cRZString *>(&mEmbedded4C))) {
            VirtualCall<LogFn, 30>(this, GameAddress<const char>(kNoWriteDirMessage));
            mState = 0;
            return false;
        }
    }
    if (mScan == nullptr) {
        mScan = new DirectoryScan(reinterpret_cast<const cRZString *>(&mEmbedded4C), false, false, false);
    }
    void *listener = nullptr;
    if (void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378()))) {
        VirtualCall<GetServiceFn, 5>(framework, kListenerService, kListenerInterface, &listener);
    }
    if (listener != nullptr &&
        !VirtualCall<RegisterFn, 7>(listener, GameAddress<const char>(kListenerName), GameAddress(kListenerCallback),
                                    this)) {
        VirtualCall<VoidFn, 2>(listener);
        return false;
    }
    bool built = VirtualCall<BoolFn, 33>(this); // BuildKeyList
    if (built && mAutoRefresh != 0) StartRefreshTimer();
    if (listener != nullptr) VirtualCall<VoidFn, 2>(listener);
    return built;
}

bool cGZDBSegmentDirectoryFiles::CopyExtensionMap(void **out)
{
    auto *map = new cGZRESIDtoExtensionMap();
    map->AddRef();
    map->CopyFrom(*reinterpret_cast<const cIGZPersistRESIDtoExtensionMap *>(&mExtensionMap));
    *out = map;
    return true;
}

bool cGZDBSegmentDirectoryFiles::KeyFromFileName(const cIGZString *name, const cIGZString *extension,
                                                 DirResourceKey *out)
{
    if (void *resolver = mObjectAC) {
        LocalString path;
        LocalString base;
        uint32_t length = name->Strlen();
        base.s.mString.assign(name->Data(), length);
        LocalString ext;
        length = extension->Strlen();
        ext.s.mString.assign(extension->Data(), length);
        OriginalFunction<JoinPathFn, 0x1c2732>()(&path.s, nullptr, nullptr, &base.s, &ext.s);
        if (!VirtualCall<KeyFromPathFn, 4>(resolver, &path.s, out, this)) return false;
        if (void *ns = mNamespace) VirtualCall<KeyInPlaceFn, 4>(ns, out, out);
        return true;
    }

    uint32_t group = mSegmentIDOnce;
    uint32_t instance = 0;
    bool parsed = false;
    const uint32_t length = name->Strlen();
    if (length >= 8) {
        if (mByte78 != 0) {
            // Leading hex digits: eight are the instance, sixteen the instance then the group.
            const char *text = name->ToChar();
            const int32_t limit = static_cast<int32_t>(length < 16 ? length : 16);
            int32_t digits = 0;
            while (digits < limit && IsHexDigit(text[digits])) ++digits;
            if (digits >= 8) {
                instance = Hex8(text);
                if (length >= 16 && digits >= 16) group = Hex8(text + 8);
                parsed = true;
            }
        }
        if (!parsed && mByte79 != 0) {
            // Trailing hex digits (among the last sixteen): the same, counted from the end.
            const int32_t first = length >= 16 ? static_cast<int32_t>(length - 16) : 0;
            const char *text = name->ToChar();
            int32_t at = static_cast<int32_t>(length) - 1;
            while (at >= first && IsHexDigit(text[at])) --at;
            const uint32_t digits = length - static_cast<uint32_t>(at) - 1;
            if (digits >= 8) {
                if (digits < 16) {
                    instance = Hex8(text + length - 8);
                } else {
                    group = Hex8(text + length - 8);
                    instance = Hex8(text + length - 16);
                }
                parsed = true;
            }
        }
    }
    if (!parsed) {
        if (mByte7A == 0) return false;
        instance = StringKeyParser::Crc24(name->ToChar(), 0) | 0xff000000u;
    }
    uint32_t type;
    if (!VirtualCall<TypeForExtensionFn, 4>(&mExtensionMap, extension, &type)) return false;
    out->type = type;
    out->group = group;
    out->instance = instance;
    if (void *ns = mNamespace) VirtualCall<KeyInPlaceFn, 4>(ns, out, out);
    return true;
}

bool cGZDBSegmentDirectoryFiles::IsResourceFileName(const cIGZString *fileName)
{
    if (void *resolver = mObjectAC) return VirtualCall<IsResourceFn, 3>(resolver, fileName, this);
    LocalString base;
    LocalString ext;
    DirResourceKey key{};
    OriginalFunction<FileNameOfFn, 0x1c1ea9>()(fileName, base.s.AsInterface(), 0);
    OriginalFunction<ExtensionOfFn, 0x1c1e0e>()(fileName, ext.s.AsInterface());
    std::string &extension = ext.s.mString;
    if (!extension.empty() && extension[0] == '.') extension.erase(0, 1);
    return VirtualCall<KeyFromNameFn, 19>(this, base.s.AsInterface(), ext.s.AsInterface(), &key);
}

// ---- the directory listing (lane NP) ----

namespace {

using FileChangedFn = bool(__thiscall *)(void *, const cRZString *, uint8_t);             // slot 34
using NamesFn = uint32_t(__thiscall *)(void *, cRZString **, uint32_t);                  // slot 22 (GetFileNames)
using IsFileFn = bool(__thiscall *)(void *, const cRZString *);                          // slot 20
using DeleteFn = void(__thiscall *)(void *, uint32_t);                                   // cRZString slot 33
using ToCharFn = const char *(__thiscall *)(void *);                                     // cIGZString slot 5
using SprintfRetFn = cIGZString *(__cdecl *)(void *, const char *, ...);                 // cRZString slot 31
using OpenCacheRecordFn = bool(__thiscall *)(void *, const DirResourceKey *, void **, uint32_t); // cache slot 22
using StreamFn = bool(__thiscall *)(void *, void *, uint32_t);                           // record slots 8 / 9
using SeekFn = void(__thiscall *)(void *, uint32_t);                                     // record slot 11
// 0x9a11c0 (__cdecl, the path): the stamp the cached key list is checked against (0 on a volume that isn't NTFS).
using VolumeStampFn = uint32_t(__cdecl *)(const cRZString *path);

constexpr uint32_t kMissingDirectoryMessage = 0xe5f3f0; // "...BuildKeyList: directory \"%s\" ..." format
constexpr uint32_t kDuplicateKeyMessage = 0xe5f378;     // "...Duplicate resource key encountered: \"%s%s\" ..." format
constexpr uint32_t kKeyListType = 0xa079ce1b;           // the cache record's type (the segment's class id)
constexpr uint32_t kCacheRead = 1;
constexpr uint32_t kCacheWrite = 2;

void Lower(cRZString *s)
{
    reinterpret_cast<cRZStringHelpers *>(s)->LowerCase(); // 0x1ad167, ours
}

// A copy of a file name as the table stores it (the game's cRZString vtable, reference count 0).
void AddFile(DirFileMap &files, const DirResourceKey &key, const std::string &name)
{
    DirFileEntry &entry = files.try_emplace(key).first->second;
    entry.name.vtable = GameAddress(kRZStringVTable);
    entry.name.text = name;
    entry.name.refCount = 0;
}

} // namespace

bool cGZDBSegmentDirectoryFiles::FileChanged(const cRZString *fileName, uint8_t added)
{
    EnsureOpen();
    if (added == 0) {
        for (auto it = mFiles.begin(); it != mFiles.end(); ++it) {
            if (!it->second.open.empty()) continue;
            LocalString lower;
            lower.s.mString = it->second.name.text;
            Lower(&lower.s);
            if (lower.s.mString == fileName->mString) {
                mFiles.erase(it);
                return true;
            }
        }
        return false;
    }
    LocalString base;
    LocalString ext;
    DirResourceKey key{};
    OriginalFunction<FileNameOfFn, 0x1c1ea9>()(fileName->AsInterface(), base.s.AsInterface(), 0);
    OriginalFunction<ExtensionOfFn, 0x1c1e0e>()(fileName->AsInterface(), ext.s.AsInterface());
    if (!ext.s.mString.empty()) ext.s.mString.erase(0, 1); // the first character, whatever it is
    if (VirtualCall<KeyFromNameFn, 19>(this, base.s.AsInterface(), ext.s.AsInterface(), &key) &&
        mFiles.find(key) == mFiles.end()) {
        AddFile(mFiles, key, fileName->mString);
    }
    return false;
}

bool cGZDBSegmentDirectoryFiles::Refresh()
{
    if (mFlag3C == 0) return false;
    bool changed = false;
    {
        DirectoryScan scan(reinterpret_cast<const cRZString *>(&mEmbedded4C), false, false, false);
        dscan::PathList files;
        if (scan.ListFiles(&files)) {
            const uint32_t count = VirtualCall<NamesFn, 22>(this, nullptr, 0);
            uint32_t kept = 0;
            for (auto it = files.begin(); it != files.end();) {
                if (!VirtualCall<IsFileFn, 20>(this, &it->s)) {
                    it = files.erase(it); // the path's slot 33 (flags 0), then the node
                } else {
                    Lower(&it->s);
                    ++kept;
                    ++it;
                }
            }
            if (kept != 0 && count != 0) {
                const uint64_t bytes = static_cast<uint64_t>(count) * 4;
                auto **names = static_cast<cRZString **>(
                    ::operator new[](bytes >> 32 ? 0xffffffffu : static_cast<size_t>(bytes)));
                for (uint32_t i = 0; i < count; ++i) {
                    names[i] = static_cast<cRZString *>(::operator new(0x20));
                    names[i]->Construct();
                }
                VirtualCall<NamesFn, 22>(this, names, count);
                for (uint32_t i = 0; i < count; ++i) Lower(names[i]);

                // Listed files the table already knows are dropped from both sides.
                uint32_t matched = 0;
                for (auto it = files.begin(); it != files.end();) {
                    bool found = false;
                    for (uint32_t j = 0; j < count; ++j) {
                        if (names[j] == nullptr || names[j]->mString != it->s.mString) continue;
                        // ORIGINAL-BUG: the name is freed without its destructor (a long name's text leaks).
                        ::operator delete(names[j], 4);
                        names[j] = nullptr;
                        it = files.erase(it);
                        ++matched;
                        found = true;
                        break;
                    }
                    if (!found) ++it;
                }
                if (matched < count) {
                    changed = true;
                    for (uint32_t i = 0; i < count; ++i) {
                        if (names[i] != nullptr) VirtualCall<FileChangedFn, 34>(this, names[i], 0);
                    }
                }
                if (!files.empty()) {
                    changed = true;
                    for (dscan::Path &path : files) {
                        LocalString copy;
                        copy.s.mString = path.s.mString;
                        VirtualCall<FileChangedFn, 34>(this, &copy.s, 1);
                    }
                }
                for (uint32_t i = 0; i < count; ++i) {
                    if (names[i] != nullptr) VirtualCall<DeleteFn, 33>(names[i], 1);
                }
                ::operator delete(names, 4);
            }
        }
        if (mAutoRefresh != 0) {
            const int32_t seconds = reinterpret_cast<cRZRandom *>(mRandom)->RandomSint32RangeUniform(
                static_cast<int32_t>(mRefreshSeconds), static_cast<int32_t>(mRefreshSeconds * 2));
            void *timer = &mTimer;
            VirtualCall<TimerStartFn, 6>(timer, static_cast<uint32_t>(seconds * 1000), this);
        }
    }
    return changed;
}

bool cGZDBSegmentDirectoryFiles::BuildKeyList()
{
    LocalString name;
    LocalString ext;
    DirResourceKey key{};
    dscan::PathList files;
    cRZString *path = reinterpret_cast<cRZString *>(&mEmbedded4C);
    if (!as2::DirectoryExists(path)) {
        LocalString message;
        cRZString *text = message.s.Sprintf(GameAddress<const char>(kMissingDirectoryMessage), mPath.c_str());
        VirtualCall<LogFn, 30>(this, VirtualCall<ToCharFn, 5>(text));
        return false;
    }
    uint32_t stamp = OriginalFunction<VolumeStampFn, 0x9a11c0>()(path);
    void *record = nullptr;
    const auto read = [&record](void *data, uint32_t size) { return VirtualCall<StreamFn, 8>(record, data, size); };
    const auto write = [&record](const void *data, uint32_t size) {
        return VirtualCall<StreamFn, 9>(record, const_cast<void *>(data), size);
    };

    bool scanned = true;
    if (mSegmentCache != nullptr && stamp != 0) {
        DirResourceKey cacheKey{kKeyListType, 0, StringKeyParser::Crc32(mPath.c_str(), 1), 0};
        if (VirtualCall<OpenCacheRecordFn, 22>(mSegmentCache, &cacheKey, &record, kCacheRead)) {
            bool ok = false;
            uint32_t stored = 0;
            uint32_t count = 0;
            if (read(&stored, 4) && stored == stamp && read(&count, 4)) {
                ok = true;
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t length = 0;
                    if (!read(&cacheKey, 0xc) || !read(&length, 4)) {
                        ok = false;
                        break;
                    }
                    DirResourceKey fileKey{cacheKey.type, cacheKey.group, cacheKey.instance, 0};
                    auto [at, inserted] = mFiles.try_emplace(fileKey); // operator[]: a new entry has an empty name
                    DirFileEntry &entry = at->second;
                    if (inserted) entry.name.vtable = GameAddress(kRZStringVTable);
                    entry.name.text.resize(length, '\0');
                    if (!read(entry.name.text.data(), length)) {
                        ok = false;
                        break;
                    }
                }
            }
            VirtualCall<VoidFn, 6>(record); // close the record (also after a failed read)
            scanned = !ok;
        }
    }

    if (scanned) {
        static_cast<DirectoryScan *>(mScan)->ListFiles(&files);
        for (dscan::Path &file : files) {
            char drive[256], directory[256], fileName[256], extension[256];
            appbase::SplitPath(file.s.mString.c_str(), drive, directory, fileName, extension);
            name.s.mString.assign(fileName, std::strlen(fileName));
            if (extension[0] != '\0') {
                ext.s.FromChar(extension + 1);
            } else {
                ext.s.mString.clear();
            }
            if (!VirtualCall<KeyFromNameFn, 19>(this, name.s.AsInterface(), ext.s.AsInterface(), &key)) continue;
            auto existing = mFiles.find(key);
            if (existing == mFiles.end()) {
                AddFile(mFiles, key, file.s.mString);
            } else {
                LocalString message;
                auto sprintf = VirtualSlot<SprintfRetFn>(&message.s, 31);
                cIGZString *text = sprintf(&message.s, GameAddress<const char>(kDuplicateKeyMessage), mPath.c_str(),
                                           file.s.mString.c_str(), existing->second.name.text.c_str());
                VirtualCall<LogFn, 30>(this, VirtualCall<ToCharFn, 5>(text));
            }
        }

        if (void *cache = mSegmentCache; cache != nullptr && stamp != 0) {
            if (void *old = record) {
                record = nullptr;
                VirtualCall<VoidFn, 2>(old);
            }
            DirResourceKey cacheKey{kKeyListType, 0, StringKeyParser::Crc32(mPath.c_str(), 1), 0};
            if (VirtualCall<OpenCacheRecordFn, 22>(cache, &cacheKey, &record, kCacheWrite)) {
                VirtualCall<SeekFn, 11>(record, 0);
                uint32_t count = static_cast<uint32_t>(mFiles.size());
                bool ok = write(&stamp, 4) && write(&count, 4);
                for (auto it = mFiles.begin(); ok && it != mFiles.end(); ++it) {
                    const DirResourceKey &fileKey = it->first;
                    uint32_t head[3] = {fileKey.type, fileKey.group, fileKey.instance};
                    uint32_t length = static_cast<uint32_t>(it->second.name.text.size());
                    ok = write(head, 0xc) && write(&length, 4) && write(it->second.name.text.data(), length);
                }
                if (ok) {
                    VirtualCall<VoidFn, 6>(record); // close
                } else {
                    VirtualCall<VoidFn, 7>(record); // abort after a failed write
                }
            }
        }
    }
    if (record != nullptr) VirtualCall<VoidFn, 2>(record);
    return true;
}

// ---- the record functions on the file system (lane NP) ----

namespace {

constexpr uint32_t kSizeUnknownMessage = 0xe5f030;   // GetRecordSize: no such record
constexpr uint32_t kDeleteUnknownMessage = 0xe5efd8; // DeleteRecord: no such record
constexpr uint32_t kDeleteOpenMessage = 0xe5ef70;    // DeleteRecord: the record is open
constexpr uint32_t kReadOpenMessage = 0xe5f078;      // ReadRecord: the record is open for writing
constexpr uint32_t kReadUnknownMessage = 0xe5f0e0;   // ReadRecord: no such record

using RecordSizeFn = uint32_t(__thiscall *)(void *); // a record's slot 3

// The access an open record was opened with (cGZDBSDFRecord +0x184): 2 and 3 write.
bool OpenForWriting(const DirOpenRecord &rec)
{
    const uint32_t access = *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(rec.record) + 0x184);
    return access == 2 || access == 3;
}

// The file's only open record is open for writing (a file with several open records never counts).
bool OnlyRecordWrites(const DirFileEntry &entry)
{
    return entry.open.size() == 1 && OpenForWriting(entry.open.front());
}

// out = the segment's path followed by the file's name.
void FilePath(const cGZDBSegmentDirectoryFiles *self, const DirFileEntry &entry, cRZString *out)
{
    out->mString = self->mPath + entry.name.text;
}

// A cRZFile on the stack, destroyed at the end of its scope.
struct LocalFile {
    union {
        cRZFile f;
    };
    explicit LocalFile(const cRZString &path) { f.Construct(*path.AsInterface()); }
    ~LocalFile() { f.Destruct(); }
    LocalFile(const LocalFile &) = delete;
    LocalFile &operator=(const LocalFile &) = delete;
};

} // namespace

uint32_t DirectorySegmentView::GetRecordSize(const DirResourceKey *key)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    self->EnsureOpen();
    if (self->mFlag3C == 0) return 0xffffffffu;
    auto it = self->FindEntry(*key);
    if (it == self->mFiles.end()) {
        VirtualCall<LogFn, 30>(self, GameAddress<const char>(kSizeUnknownMessage));
        return 0xffffffffu;
    }
    uint32_t size = 0xffffffffu;
    for (auto &rec : it->second.open) {
        if (OpenForWriting(rec)) size = VirtualCall<RecordSizeFn, 3>(rec.record);
    }
    if (size != 0xffffffffu) return size;
    LocalString path;
    FilePath(self, it->second, &path.s);
    return cRZFile::LengthOfPath(*path.s.AsInterface());
}

bool DirectorySegmentView::DeleteRecord(const DirResourceKey *key)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    self->EnsureOpen();
    if (self->mFlag3C == 0) return false;
    auto it = self->FindEntry(*key);
    if (it == self->mFiles.end()) {
        VirtualCall<LogFn, 30>(self, GameAddress<const char>(kDeleteUnknownMessage));
        return false;
    }
    if (!it->second.open.empty()) {
        VirtualCall<LogFn, 30>(self, GameAddress<const char>(kDeleteOpenMessage));
        return false;
    }
    LocalString path;
    FilePath(self, it->second, &path.s);
    self->mFiles.erase(it);
    PathDelete(path.s.AsInterface());
    self->EraseProbe(key);
    return true;
}

int32_t DirectorySegmentView::ReadRecord(const DirResourceKey *key, void *buffer, uint32_t *size)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    self->EnsureOpen();
    if (self->mFlag3C == 0) {
        *size = 0;
        return -1;
    }
    auto it = self->FindEntry(*key);
    if (it == self->mFiles.end()) {
        VirtualCall<LogFn, 30>(self, GameAddress<const char>(kReadUnknownMessage));
        *size = 0;
        return -1;
    }
    if (OnlyRecordWrites(it->second)) {
        VirtualCall<LogFn, 30>(self, GameAddress<const char>(kReadOpenMessage));
        *size = 0;
        return -1;
    }
    LocalString path;
    FilePath(self, it->second, &path.s);
    if (buffer == nullptr) {
        *size = 0;
        return static_cast<int32_t>(cRZFile::LengthOfPath(*path.s.AsInterface()));
    }
    LocalFile file(path.s);
    if (!file.f.Open(1, 2, 1)) {
        *size = 0;
        return -1;
    }
    const uint32_t length = file.f.Length();
    if (length < *size) *size = length;
    file.f.ReadWithCount(buffer, *size);
    const uint32_t read = *size;
    file.f.Close();
    return static_cast<int32_t>(read);
}

bool DirectorySegmentView::GetRecordStamp(const DirResourceKey *key, uint32_t *out)
{
    cGZDBSegmentDirectoryFiles *self = Primary();
    if (self->mFlag3C == 0) return false;
    auto it = self->FindEntry(*key);
    if (it == self->mFiles.end()) return false;
    const bool writing = OnlyRecordWrites(it->second);
    *out = 0;
    LocalString path;
    FilePath(self, it->second, &path.s);
    if (!writing) {
        const uint32_t length = cRZFile::LengthOfPath(*path.s.AsInterface());
        if (length != 0xffffffffu) {
            const uint32_t time = PathFileTime(path.s.AsInterface(), 2);
            if (time != 0) *out = time ^ length;
        }
    }
    return true;
}

// ---- opening a record (lane NP) ----

namespace {

constexpr uint32_t kRecordNotReadyMessage = 0xe5f310;   // not initialised, or no access asked
constexpr uint32_t kRecordUnknownMessage = 0xe5f1a0;    // reading a record that isn't there
constexpr uint32_t kRecordBusyMessage = 0xe5f210;       // writing or creating a record that is open
constexpr uint32_t kRecordWritingMessage = 0xe5f288;    // reading a record whose first open record isn't reading
constexpr uint32_t kRecordFaceIID = 0xe56b8f03;         // cIGZPersistDBRecord
constexpr size_t kRecordSize = 0x188;
// cGZDBSDFRecord's vtables, by offset: its own, the serial record's, the record face's, the output and input streams'.
constexpr struct {
    size_t at;
    uint32_t rva;
} kRecordVTables[] = {{0, 0xe5ead4}, {4, 0xe5eb1c}, {8, 0xe5ec18}, {0x24, 0xe5ec70}, {0x34, 0xe5ecd4}};

// 0x99b840 (__thiscall on the record + 4: segment, flag byte): cGZPersistDBSerialRecord's constructor (original code).
using SerialRecordCtorFn = void *(__thiscall *)(void *, void *segment, uint8_t);
using NameAndExtensionFn = bool(__thiscall *)(void *, const DirResourceKey *, cIGZString *, cIGZString *); // slot 36
using FileNameOfKeyFn = bool(__thiscall *)(void *, const DirResourceKey *, cIGZString *);               // slot 17
using RecordOpenFn = bool(__thiscall *)(void *, const DirResourceKey *, const cRZString *, uint32_t);   // record slot 14
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);                                        // slot 0

// A new cGZDBSDFRecord of the segment (reference count 0), as the original builds it in place.
void *NewDirectoryRecord(void *segment)
{
    auto *record = static_cast<uint8_t *>(::operator new(kRecordSize));
    OriginalFunction<SerialRecordCtorFn, 0x99b840>()(record + 4, segment, 1);
    for (const auto &vt : kRecordVTables) *reinterpret_cast<const void **>(record + vt.at) = GameAddress(vt.rva);
    record[0x48] = 0;
    reinterpret_cast<cRZFile *>(record + 0x4c)->Construct();
    reinterpret_cast<cRZFile *>(record + 0xe8)->Construct();
    *reinterpret_cast<uint32_t *>(record + 0x184) = 0;
    return record;
}

// The files table's entry at key, inserted (unnamed: the game's cRZString vtable, reference count 0) when missing:
// the table's operator[] (0x99e9c0).
DirFileEntry &EntryAt(DirFileMap &files, const DirResourceKey &key)
{
    auto [it, inserted] = files.try_emplace(key);
    if (inserted) {
        it->second.name.vtable = GameAddress(kRZStringVTable);
        it->second.name.refCount = 0;
    }
    return it->second;
}

} // namespace

bool cGZDBSegmentDirectoryFiles::OpenRecordFile(const DirResourceKey *key, void **out, uint32_t access)
{
    EnsureOpen();
    if (mFlag3C == 0 || access == 0) {
        VirtualCall<LogFn, 30>(this, GameAddress<const char>(kRecordNotReadyMessage));
        return false;
    }
    auto it = FindEntry(*key);
    const bool found = it != mFiles.end();
    if (!found) {
        if (access == 1) {
            VirtualCall<LogFn, 30>(this, GameAddress<const char>(kRecordUnknownMessage));
            return false;
        }
    } else if (!it->second.open.empty()) {
        if (access == 3 || access == 2) {
            VirtualCall<LogFn, 30>(this, GameAddress<const char>(kRecordBusyMessage));
            return false;
        }
        const void *first = it->second.open.front().record;
        if (*reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(first) + 0x184) != 1) {
            VirtualCall<LogFn, 30>(this, GameAddress<const char>(kRecordWritingMessage));
            return false;
        }
    }

    LocalString name;
    LocalString path;
    if (access == 3) {
        LocalString extension;
        VirtualCall<NameAndExtensionFn, 36>(this, key, name.s.AsInterface(), extension.s.AsInterface());
        name.s.mString += '.';
        name.s.mString += extension.s.mString;
    } else {
        VirtualCall<FileNameOfKeyFn, 17>(this, key, name.s.AsInterface());
    }
    path.s.mString = mPath + name.s.mString;

    void *record = NewDirectoryRecord(SubObject(this));
    VirtualCall<AddRefFn, 1>(record);
    bool ok = false;
    if (VirtualCall<RecordOpenFn, 14>(record, key, &path.s, access)) {
        DirFileEntry &entry = found ? it->second : EntryAt(mFiles, *key);
        VirtualCall<AddRefFn, 1>(record); // the open list's reference
        entry.open.emplace_front(record); // at the front (0x9a3080 links the node before the first)
        if (!found) entry.name.text = name.s.mString;
        if (VirtualCall<QueryFn, 0>(record, kRecordFaceIID, out)) {
            ok = true;
        } else {
            // ORIGINAL-BUG: the record is looked for at the exact key, not at the entry it joined: when FindEntry matched
            // another key (the +0xb0 / +0xb1 instance matching) the record stays in that entry's open list, and an empty
            // entry is inserted at this key.
            EntryAt(mFiles, *key).RemoveOpenRecord(static_cast<uint8_t *>(record) + 8);
        }
    }
    VirtualCall<VoidFn, 2>(record); // Release
    return ok;
}

// ---- construction and destruction (lane NP) ----

namespace {

constexpr uint32_t kExtensionMapVTable = 0xe5ed38; // cGZRESIDtoExtensionMap's (its slots are ours, hooked)
constexpr uint32_t kDefaultRefreshSeconds = 120;
constexpr size_t kObjectSize = 0xf8;

cGZRESIDtoExtensionMap *Extensions(const void **at)
{
    return reinterpret_cast<cGZRESIDtoExtensionMap *>(at);
}

} // namespace

cGZDBSegmentDirectoryFiles *cGZDBSegmentDirectoryFiles::Construct()
{
    reinterpret_cast<cGZPersistDBSegment *>(&mSubVTable)->Construct();
    mVTable = GameAddress(kVTable);
    mSubVTable = GameAddress(kSubVTable);
    new (&mProbes) DirProbeMap();
    mFlag3C = 0;
    mWriteNeedsDirectory = 0;
    mByte3E = 1;
    mNextInstance = 1;
    mState = 0;
    mAutoOpen48 = 0;
    mAutoOpen49 = 0;
    mEmbedded4C = GameAddress(kRZStringVTable);
    new (&mPath) std::string();
    mPathRefCount = 0;
    mScan = nullptr;
    mSegmentIDOnce = 0;
    mSegmentID = 0;
    mByte78 = 1;
    mByte79 = 0;
    mByte7A = 0;
    // The embedded extension map keeps the game's vtable, as the original builds it (its slots are hooked to ours).
    mExtensionMap = GameAddress(kExtensionMapVTable);
    Extensions(&mExtensionMap)->mRefCount = 0;
    new (&Extensions(&mExtensionMap)->mMap) residext::Map();
    new (&mFiles) DirFileMap();
    mObjectAC = nullptr;
    mByteB0 = 0;
    mByteB1 = 0;
    mAutoRefresh = 0;
    mRefreshSeconds = kDefaultRefreshSeconds;
    reinterpret_cast<cRZCallbackTimer *>(&mTimer)->Construct();
    reinterpret_cast<cRZRandom *>(mRandom)->Construct(0xffffffffu);
    return this;
}

cGZDBSegmentDirectoryFiles *cGZDBSegmentDirectoryFiles::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    mSubVTable = GameAddress(kSubVTable);
    Shutdown();
    reinterpret_cast<cRZCallbackTimer *>(&mTimer)->Destruct();
    if (mObjectAC != nullptr) VirtualCall<VoidFn, 2>(mObjectAC); // Release (Shutdown already nulled it when it ran)
    std::destroy_at(&mFiles);
    mExtensionMap = GameAddress(kExtensionMapVTable);
    std::destroy_at(&Extensions(&mExtensionMap)->mMap);
    mEmbedded4C = GameAddress(kRZStringVTable);
    std::destroy_at(&mPath);
    std::destroy_at(&mProbes);
    reinterpret_cast<cGZPersistDBSegment *>(&mSubVTable)->Destruct();
    if ((flags & 1) != 0) ::operator delete(this, kObjectSize);
    return this;
}

cGZDBSegmentDirectoryFiles *DirectorySegmentView::DeletingDestructorThunk(uint32_t flags)
{
    return Primary()->DeletingDestructor(flags);
}
