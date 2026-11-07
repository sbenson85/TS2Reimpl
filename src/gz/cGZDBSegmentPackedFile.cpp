#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZDBSegmentPackedFileInternal.h"
#include "app/AppServices2.h"
#include "app/ExceptionReporter.h"
#include "app/IniFile.h"
#include "app/ReportBlockers.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZDBPFIndexCompressed.h"
#include "gz/cGZDBReadRecord.h"
#include "gz/cGZDBSerialRecordAsIGZFile.h"
#include "gz/cGZDBPFIndexModifiable.h"
#include "gz/cGZDBPFIndexStatic.h"
#include "gz/cGZDBWriteRecordPackedFile.h"
#include "gz/cGZPersistDBSegment.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cGZDBRecordPackedFile.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZDBSegmentPackedFile.h"
#include "ifaces/cIGZFile.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "rz/GeneralAllocator.h"
#include "rz/PathFileOps.h"
#include "rz/RZMemory.h"
#include "rz/StringKeyParser.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/cRZRefCount.h"
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using namespace gz_dbsegment_detail;

bool cGZDBSegmentPackedFile::QueryInterface(uint32_t riid, void **ppvObj)
{
    cIGZUnknown *target;
    switch (riid) {
    case kIdSelf:
        *ppvObj = this;
        target = As<cIGZUnknown>(this);
        break;
    case kIdSub:
    case kIdSubUnknown:
        *ppvObj = &mSegment;
        target = As<cIGZUnknown>(&mSegment);
        break;
    default:
        return false;
    }
    target->AddRef(); // through the vtable of the returned object
    return true;
}

uint32_t cGZDBSegmentPackedFile::Release()
{
    // The count is shared with the subobject (+0x08); the original runs on the subobject.
    int32_t count = RZAtomicAdd(&mSegment.mRefCount, -1);
    if (count != 0) return static_cast<uint32_t>(count);
    mSegment.mRefCount = 1;
    As<ifaces::cIGZPersistDBSegment>(&mSegment)->Destroy();
    return 0;
}

bool cGZDBSegmentPackedFile::SetBaseOffset(uint32_t value)
{
    if ((mOpenMode & 3) != 0) return false;
    mBaseOffset = value;
    return true;
}

void cGZDBSegmentPackedFile::GetPath(cIGZString *out)
{
    // std::string's data() is inline up to 15 characters, the same test the original makes on capacity.
    out->FromChar(mPath.mString.data(), static_cast<uint32_t>(mPath.mString.size()));
}

bool cGZDBSegmentPackedFile::SetPath(void *path)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    bool accepted;
    if ((mOpenMode & 3) == 0) {
        mPath.AsInterface()->operator=(*static_cast<const cIGZString *>(path));
        accepted = true;
    } else {
        accepted = false;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return accepted;
}

void cGZDBSegmentPackedFile::GetFileUserVersion(uint32_t *a, uint32_t *b)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    *a = mHeader.userVersionMajor;
    *b = mHeader.userVersionMinor;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
}

void cGZDBSegmentPackedFile::GetUserVersion(uint32_t *a, uint32_t *b)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    *a = mUserVersionMajor;
    *b = mUserVersionMinor;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
}

void cGZDBSegmentPackedFile::SetUserVersion(uint32_t a, uint32_t b)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    mUserVersionMajor = a;
    mUserVersionMinor = b;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
}

bool cGZDBSegmentPackedFile::SetBuffering(uint32_t a, uint32_t b)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    bool accepted = As<ifaces::cIGZFile>(&mFile)->SetBuffering(a, b);
    if (accepted) {
        mReadBufferSize = a;
        mWriteBufferSize = b;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return accepted;
}

bool cGZDBSegmentPackedFile::SetOptions(uint32_t value)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    mOptions = value;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return true;
}

void cGZDBSegmentPackedFile::SetCompactRatio(float value)
{
    mCompactRatio = value;
}

void cGZDBSegmentPackedFile::SetFlag2A(uint8_t value)
{
    mFlag2A = value;
}

bool cGZDBSegmentPackedFile::IsDirty()
{
    return mIndexDirty != 0 || mHeaderDirty != 0 || mFreeRangesDirty != 0;
}

void cGZDBSegmentPackedFile::SetGroupInfo(uint32_t value)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    if (mHeader.groupInfo != value) {
        mHeader.groupInfo = value;
        mHeaderDirty = 1;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
}

bool cGZDBSegmentPackedFile::SetCompressAll(uint8_t value)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    mCompressAll = value;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return true;
}

bool cGZDBSegmentPackedFile::IsRecordCompressed(const uint32_t *record)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    KeyCopy keyCopy(record);
    const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
    bool result = false;
    if (hit != nullptr) result = (hit[2] & 0x7fffffffu) > hit[1];
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

void cGZDBSegmentPackedFile::SetRecordFullSize(const uint32_t *record, uint8_t useArg, uint32_t value)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    KeyCopy keyCopy(record);
    uint32_t *hit = mIndex->FindRecordForWrite(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
    if (hit != nullptr) {
        // The original keeps the top bit of hit[2] and takes the other 31 bits from the argument, or from
        // hit[1] when the byte argument is zero.
        uint32_t low = (useArg == 0) ? hit[1] : value;
        hit[2] = (hit[2] & 0x80000000u) | (low & 0x7fffffffu);
        mIndexDirty = 1;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
}

uint32_t cGZDBSegmentPackedFile::GetCompressedStoredSize(const uint32_t *record)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    uint32_t result = 0xffffffffu;
    KeyCopy keyCopy(record);
    const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
    if (hit != nullptr && (hit[2] & 0x7fffffffu) > hit[1]) result = hit[1];
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

bool cGZDBSegmentPackedFile::GetRecordFlag(const uint32_t *record)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    KeyCopy keyCopy(record);
    const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
    bool result = false;
    if (hit != nullptr) result = (hit[2] >> 31) != 0;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

void cGZDBSegmentPackedFile::SetMatchFlagA(uint8_t value)
{
    mMatchFlagA = value;
}

void cGZDBSegmentPackedFile::SetMatchFlagB(uint8_t value)
{
    mMatchFlagB = value;
}

bool cGZDBSegmentPackedFile::CreateRecordFile(void *owner, void **out)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    // The original's operator new throws on failure (the null branch would crash), so the block is taken
    // as it is. The object's words: [0] primary vtable, [1] secondary vtable, [2] refcount, [3] holder.
    void **object = static_cast<void **>(::operator new(0x10));
    object[0] = const_cast<void *>(cGZDBSerialRecordAsIGZFile::VTable());
    object[1] = const_cast<void *>(cGZDBSerialRecordAsIGZFile::FileVTable());
    object[2] = nullptr;
    // holder = owner - 4 when owner is not 0 (the original's `and` with the mask), 0 when it is 0 or 4.
    void *holder = owner != nullptr ? static_cast<uint8_t *>(owner) - 4 : nullptr;
    object[3] = holder;
    if (holder != nullptr) As<cIGZUnknown>(holder)->AddRef();
    void *result = reinterpret_cast<uint8_t *>(object) + 4;
    *out = result;
    As<cIGZUnknown>(result)->AddRef();
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return true;
}

// ---- The cGZPersistDBSegment subobject (this = object + 4) ----

bool SegmentView::IsOpen() const
{
    return (Primary(this)->mOpenMode & 3) != 0;
}

bool SegmentView::IsWritable() const
{
    uint32_t word; // the original reads the state byte with its three padding bytes
    std::memcpy(&word, &Primary(this)->mOpenMode, sizeof(word));
    return ((word >> 1) & 1) != 0;
}

bool SegmentView::AutoOpen(uint8_t state, uint8_t flag)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    file->mAutoOpenRead = state;
    file->mAutoOpenWrite = flag;
    return true;
}

bool SegmentView::SetSegmentID(const uint32_t *id)
{
    Primary(this)->mSegmentID = *id;
    return true;
}

bool SegmentView::DoGetRecordChangeID(void *arg, uint32_t *out)
{
    auto *self = As<ifaces::cIGZPersistDBSegment>(this);
    if (!self->DoTestForRecord(static_cast<const cGZPersistResourceKey *>(arg))) return false;
    *out = self->DoGetSegmentChangeID();
    return true;
}

// ---- Construction, the auto-open helper, the file operations and the small slots ------------------------

namespace {

// A tree head node: left, parent and right point at itself, and the colour/nil word (bytes +0xc and +0xd) is
// 0x101: black and nil. The remaining bytes of the block are left as they are, as the original does.
void *NewHeadNode(uint32_t bytes)
{
    uint32_t *node = static_cast<uint32_t *>(RZMemory::Malloc(bytes));
    uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    node[0] = self;
    node[1] = self;
    node[2] = self;
    uint16_t colorNil = 0x101;
    std::memcpy(reinterpret_cast<uint8_t *>(node) + 0xc, &colorNil, sizeof(colorNil));
    return node;
}

// The body of the cRZString's std::string as the constructor writes it: the 16 buffer bytes zeroed (not just the
// first one, which std::string's own constructor would clear), size 0, capacity 15.
struct StdStringBody {
    char buffer[16];
    uint32_t size;
    uint32_t capacity;
};
static_assert(sizeof(StdStringBody) == sizeof(std::string), "StdStringBody: the MSVC std::string layout");

} // namespace

cGZDBSegmentPackedFile *cGZDBSegmentPackedFile::Construct()
{
    mVTable = InterfaceVTable(); // the interface's table, replaced below
    mSegment.Construct();
    mVTable = VTable();
    mSegment.mVTable = SubVTable();
    mInitialized = 0;
    mSegmentID = 0;
    mOpenMode = 0;
    std::memset(mOpenModePad, 0, sizeof(mOpenModePad));
    mAutoOpenRead = 0;
    mAutoOpenWrite = 0;
    mFlag2A = 0;
    mCreateIfMissing = 1;
    mField2C = 0;
    mIndex = nullptr;
    mIndexDirty = 0;
    StdStringBody &pathBody = reinterpret_cast<StdStringBody &>(mPath.mString);
    std::memset(pathBody.buffer, 0, sizeof(pathBody.buffer));
    pathBody.size = 0;
    pathBody.capacity = 0xf;
    mPath.mVTable = cRZString::VTable();
    mPath.mRefCount = 0;
    mFile.Construct();
    mBaseOffset = 0;
    mReadBufferSize = 0xffffffffu;
    mWriteBufferSize = 0xffffffffu;
    mFileStamp = 0;
    mUserVersionMajor = 0;
    mUserVersionMinor = 0;
    mSmallRecordLimit = 0x200;
    mOpenRecords.head = nullptr;
    mOpenRecords.size = 0;
    mOpenRecords.head = static_cast<KeyTreeNode *>(NewHeadNode(0x24));
    mHeader.magic = 0;
    mHeaderDirty = 0;
    mOptions = 0x13;
    mCompactRatio = 0.15f; // 0x3e19999a
    mFreeRanges.head = nullptr;
    mFreeRanges.size = 0;
    mFreeRanges.head = NewHeadNode(0x18);
    mPendingFreeRanges.head = nullptr;
    mPendingFreeRanges.size = 0;
    mPendingFreeRanges.head = NewHeadNode(0x18);
    mSpareFreeRanges.head = nullptr;
    mSpareFreeRanges.size = 0;
    mSpareFreeRanges.head = NewHeadNode(0x18);
    mFreeRangesDirty = 0;
    mCompacting = 0;
    mTrackRefs = 0;
    mTypeRefs = nullptr;
    mGroupRefs = nullptr;
    mMatchFlagA = 0;
    mMatchFlagB = 0;
    mCompressAll = 1;
    mCompressedTypes = nullptr;
    mCompressedGroups = nullptr;
    return this;
}

uint8_t cGZDBSegmentPackedFile::EnsureLoaded()
{
    if ((mOpenMode & 3) != 0) return 1;
    uint8_t first = mAutoOpenRead;
    if (first == 0 && mAutoOpenWrite == 0) return 1;
    auto *segment = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    // Open's arguments are the two auto-open bytes read before the call; it reads only their low bytes.
    uint8_t opened = segment->Open(first, mAutoOpenWrite);
    if (opened != 0) return 0;
    // Everything below re-reads the bytes: the call may have changed them.
    if (mAutoOpenWrite == 0) return 0;
    uint8_t again = mAutoOpenRead;
    if (again == 0) return 0;
    return segment->Open(again, 0);
}

uint32_t cGZDBSegmentPackedFile::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mSegment.mRefCount, 1));
}

void *cGZDBSegmentPackedFile::AsPersistDBSegment()
{
    return &mSegment;
}

bool cGZDBSegmentPackedFile::Init()
{
    if (mInitialized == 0) mInitialized = 1;
    return true;
}

uint32_t cGZDBSegmentPackedFile::GetBaseOffset()
{
    return mBaseOffset;
}

bool cGZDBSegmentPackedFile::GetDates(uint32_t *a, uint32_t *b)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = false;
    if ((mOpenMode & 3) != 0) {
        result = true;
        *a = mHeader.created;
        *b = mHeader.modified;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

void cGZDBSegmentPackedFile::SetCreateIfMissing(uint8_t value)
{
    mCreateIfMissing = value;
}

bool cGZDBSegmentPackedFile::IsRecordOpen(const uint32_t *key)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    KeyTreeNode *end = mOpenRecords.head;
    KeyTreeNode *found;
    mOpenRecords.Find(&found, key);
    bool result = found != end;
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

void *cGZDBSegmentPackedFile::GetFile()
{
    return &mFile;
}

uint32_t cGZDBSegmentPackedFile::GetGroupInfo()
{
    return mHeader.groupInfo;
}

uint32_t cGZDBSegmentPackedFile::GetRecordStoredSize(const uint32_t *record)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    uint32_t result = 0xffffffffu;
    if ((mOpenMode & 3) != 0) {
        KeyCopy keyCopy(record);
        const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
        if (hit != nullptr) result = hit[1];
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

uint32_t cGZDBSegmentPackedFile::GetRecordOffset(const uint32_t *record)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    uint32_t result = 0xffffffffu;
    if ((mOpenMode & 3) != 0) {
        KeyCopy keyCopy(record);
        const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
        if (hit != nullptr) result = hit[0];
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

bool cGZDBSegmentPackedFile::Reserved46(uint32_t, uint32_t, uint32_t)
{
    return false;
}

bool cGZDBSegmentPackedFile::SeekTo(uint32_t offset)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = false;
    if ((mOpenMode & 3) != 0) {
        uint32_t landed = As<ifaces::cIGZFile>(&mFile)->SeekToPosition(mBaseOffset + offset);
        result = landed == mBaseOffset + offset; // +0xf4 is read again after the call
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

bool cGZDBSegmentPackedFile::ReadAt(void *dst, uint32_t offset, uint32_t length)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = false;
    if ((mOpenMode & 3) != 0) {
        auto *file = As<ifaces::cIGZFile>(&mFile);
        uint32_t landed = file->SeekToPosition(mBaseOffset + offset);
        if (landed == mBaseOffset + offset) {
            if (file->Read(dst, length) != 0)
                result = true;
        }
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

bool cGZDBSegmentPackedFile::ReadCountedAt(void *dst, uint32_t offset, uint32_t *length)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = false;
    if ((mOpenMode & 3) != 0) {
        auto *file = As<ifaces::cIGZFile>(&mFile);
        uint32_t landed = file->SeekToPosition(mBaseOffset + offset);
        if (landed == mBaseOffset + offset) {
            if (file->ReadWithCount(dst, length) != 0)
                result = true;
        }
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

bool cGZDBSegmentPackedFile::WriteAt(const void *src, uint32_t offset, uint32_t length)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = false;
    // The original tests "open" and "writable" separately; bit 1 implies the first.
    if ((mOpenMode & 3) != 0 && (mOpenMode & 2) != 0) {
        auto *file = As<ifaces::cIGZFile>(&mFile);
        uint32_t landed = file->SeekToPosition(mBaseOffset + offset);
        if (landed == mBaseOffset + offset) {
            if (file->Write(const_cast<void *>(src), length) != 0)
                result = true;
        }
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

KeyTreeNode **KeyTree::Find(KeyTreeNode **out, const uint32_t *key)
{
    KeyTreeNode *best = head;
    KeyTreeNode *node = best->parent;
    while (node->isNil == 0) {
        if (KeyLess(node->key, key)) {
            node = node->right;
        } else {
            best = node;
            node = node->left;
        }
    }
    if (best->isNil == 0 && !KeyLess(key, best->key)) {
        *out = best;
    } else {
        *out = head;
    }
    return out;
}

// ---- The subobject's thunks and small slots ---------------------------------------------------------------

bool SegmentView::QueryInterface(uint32_t riid, void **ppvObj)
{
    return Primary(this)->QueryInterface(riid, ppvObj);
}

uint32_t SegmentView::AddRef()
{
    return reinterpret_cast<cGZPersistDBSegment *>(this)->AddRef();
}

uint32_t SegmentView::Release()
{
    return Primary(this)->Release();
}

bool SegmentView::Init()
{
    return Primary(this)->Init();
}

void SegmentView::GetPath(cIGZString *out)
{
    Primary(this)->GetPath(out);
}

bool SegmentView::SetPath(void *path)
{
    return Primary(this)->SetPath(path);
}

uint32_t SegmentView::GetSegmentID() const
{
    return Primary(this)->mSegmentID;
}

void SegmentView::Delete()
{
    if ((Primary(this)->mOpenMode & 3) != 0) As<ifaces::cIGZPersistDBSegment>(this)->Close();
    if (Primary(this)->mInitialized != 0) As<ifaces::cIGZDBSegmentPackedFile>(Primary(this))->Shutdown();
    reinterpret_cast<cGZPersistDBSegment *>(this)->vf34();
}

// ---- Shutdown, destruction and the containers ---------------------------------------------------------------

HashMap32 *HashMap32::Construct()
{
    // The default constructor of the library's unordered_map<unsigned, unsigned>: 0x20 bytes, an empty list
    // with its head node, eight buckets (sixteen list iterators) and a load factor of 1.0.
    new (this) std::unordered_map<uint32_t, uint32_t>();
    return this;
}

void KeyTree::EraseSubtree(const void *allocator, KeyTreeNode *root)
{
    for (KeyTreeNode *node = root; node->isNil == 0;) {
        EraseSubtree(allocator, node->right);
        KeyTreeNode *done = node;
        node = node->left;
        RZMemory::Free(done);
    }
}

bool cGZDBSegmentPackedFile::Shutdown()
{
    if (mInitialized == 0) return true;
    mInitialized = 0;
    auto *segment = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    if ((mOpenMode & 3) != 0) segment->Close();
    if (mCompressedTypes != nullptr) delete mCompressedTypes;
    mCompressedTypes = nullptr;
    if (mCompressedGroups != nullptr) delete mCompressedGroups;
    mCompressedGroups = nullptr;
    return mSegment.Shutdown();
}

namespace {

// The four trees (the free-range trees and the open-record tree) share the node header of KeyTree (left, parent,
// right, colour, nil), so the one subtree erase serves them all: the nodes under the head's root, then the head
// (read again after the erase).
template <typename Tree> void DestroyTree(Tree &tree)
{
    KeyTree *keyTree = reinterpret_cast<KeyTree *>(&tree);
    keyTree->EraseSubtree(keyTree, keyTree->head->parent);
    if (keyTree->head != nullptr) RZMemory::Free(keyTree->head);
}

} // namespace

cGZDBSegmentPackedFile *cGZDBSegmentPackedFile::DeletingDestructor(uint32_t flags)
{
    mVTable = VTable();
    mSegment.mVTable = SubVTable();
    Shutdown();
    DestroyTree(mSpareFreeRanges);
    DestroyTree(mPendingFreeRanges);
    DestroyTree(mFreeRanges);
    DestroyTree(mOpenRecords);
    mFile.Destruct();
    mPath.Destruct();
    mSegment.Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool SegmentView::Shutdown()
{
    return Primary(this)->Shutdown();
}

cGZDBSegmentPackedFile *SegmentView::DeletingDestructor(uint32_t flags)
{
    return Primary(this)->DeletingDestructor(flags);
}
