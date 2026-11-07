// The packed-file segment's owned vtables (docs/standalone-shape.md, "Owned statics"): the faces rzface::X, one per
// table the game's constructor and destructor store: the primary (0xe5fc58, 59 slots), the cGZPersistDBSegment subobject
// at +4 (0xe5fd48, 50 slots) and the abstract interface table the constructor stores first (0xe5f45c, 53 pure slots).
// Each slot runs the hook-shaped member of the same name (cGZDBSegmentPackedFile.cpp, cGZPersistDBSegment.cpp); the
// sub-object face is entered with `this` = object + 4, as the game does. In the hooked game the objects keep the game's
// tables (VTable() is the image's symbol); the faces are what an image-free program stores.
#include "gz/cGZDBSegmentPackedFile.h"

#include "gz/cGZPersistDBSegment.h"
#include "gz/cIGZString.h"

namespace rzface {

namespace {
::cGZDBSegmentPackedFile *Self(void *face) { return reinterpret_cast<::cGZDBSegmentPackedFile *>(face); }
SegmentView *Sub(void *face) { return reinterpret_cast<SegmentView *>(face); }
::cGZPersistDBSegment *Base(void *face) { return reinterpret_cast<::cGZPersistDBSegment *>(face); }
} // namespace

// The primary table.
class cGZDBSegmentPackedFile final {
public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self(this)->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Self(this)->AddRef(); }                                                      // 1
    virtual uint32_t Release() { return Self(this)->Release(); }                                                    // 2
    virtual void *AsPersistDBSegment() { return Self(this)->AsPersistDBSegment(); }                                 // 3
    virtual bool Init() { return Self(this)->Init(); }                                                              // 4
    virtual bool Shutdown() { return Self(this)->Shutdown(); }                                                      // 5
    virtual uint32_t GetBaseOffset() { return Self(this)->GetBaseOffset(); }                                        // 6
    virtual bool SetBaseOffset(uint32_t v) { return Self(this)->SetBaseOffset(v); }                                 // 7
    virtual void GetPath(cIGZString *out) { Self(this)->GetPath(out); }                                             // 8
    virtual bool SetPath(void *path) { return Self(this)->SetPath(path); }                                          // 9
    virtual void GetFileUserVersion(uint32_t *a, uint32_t *b) { Self(this)->GetFileUserVersion(a, b); }            // 10
    virtual void GetUserVersion(uint32_t *a, uint32_t *b) { Self(this)->GetUserVersion(a, b); }                     // 11
    virtual void SetUserVersion(uint32_t a, uint32_t b) { Self(this)->SetUserVersion(a, b); }                       // 12
    virtual bool GetDates(uint32_t *a, uint32_t *b) { return Self(this)->GetDates(a, b); }                          // 13
    virtual bool SetBuffering(uint32_t a, uint32_t b) { return Self(this)->SetBuffering(a, b); }                    // 14
    virtual bool SetOptions(uint32_t v) { return Self(this)->SetOptions(v); }                                       // 15
    virtual void SetCompactRatio(float v) { Self(this)->SetCompactRatio(v); }                                       // 16
    virtual void SetFlag2A(uint8_t v) { Self(this)->SetFlag2A(v); }                                                 // 17
    virtual void SetCreateIfMissing(uint8_t v) { Self(this)->SetCreateIfMissing(v); }                               // 18
    virtual bool IsRecordOpen(const uint32_t *key) { return Self(this)->IsRecordOpen(key); }                        // 19
    virtual bool CreateRecordFile(void *owner, void **out) { return Self(this)->CreateRecordFile(owner, out); }     // 20
    virtual uint8_t ImportFile(const uint32_t *key, cIGZString *path) { return Self(this)->ImportFile(key, path); } // 21
    virtual void *GetFile() { return Self(this)->GetFile(); }                                                       // 22
    virtual uint8_t ReopenIfDirty() { return Self(this)->ReopenIfDirty(); }                                         // 23
    virtual bool IsDirty() { return Self(this)->IsDirty(); }                                                        // 24
    virtual uint32_t GetGroupInfo() { return Self(this)->GetGroupInfo(); }                                          // 25
    virtual void SetGroupInfo(uint32_t v) { Self(this)->SetGroupInfo(v); }                                          // 26
    virtual uint32_t GetRecordStoredSize(const uint32_t *r) { return Self(this)->GetRecordStoredSize(r); }          // 27
    virtual uint32_t ReadRecordData(const uint32_t *key, void *dst, uint32_t *size)                                 // 28
    {
        return Self(this)->ReadRecordData(key, dst, size);
    }
    virtual bool WriteRecordData(const uint32_t *key, const void *data, uint32_t size) // 29
    {
        return Self(this)->WriteRecordData(key, data, size);
    }
    virtual uint32_t GetRecordOffset(const uint32_t *r) { return Self(this)->GetRecordOffset(r); } // 30
    virtual uint32_t CountTypes() { return Self(this)->CountTypes(); }                             // 31
    virtual uint32_t CountGroups() { return Self(this)->CountGroups(); }                           // 32
    virtual uint32_t SetRefTracking(uint8_t v) { return Self(this)->SetRefTracking(v); }           // 33
    virtual uint32_t CopyToFile(void *path, uint32_t a2, uint32_t a3, uint32_t a4)                 // 34
    {
        return Self(this)->CopyToFile(path, a2, a3, a4);
    }
    virtual uint32_t CopyRecordsTo(void *other, uint32_t filter, uint32_t a, uint32_t b) // 35
    {
        return Self(this)->CopyRecordsTo(other, filter, a, b);
    }
    virtual uint8_t Compact() { return Self(this)->Compact(); }                                                // 36
    virtual bool VisitAllRecords() { return Self(this)->VisitAllRecords(); }                                   // 37
    virtual bool SetCompressAll(uint8_t v) { return Self(this)->SetCompressAll(v); }                           // 38
    virtual bool EditCompressedTypes(uint32_t v, bool add) { return Self(this)->EditCompressedTypes(v, add); } // 39
    virtual bool EditCompressedGroups(uint32_t v, bool add) { return Self(this)->EditCompressedGroups(v, add); } // 40
    virtual bool IsRecordCompressed(const uint32_t *r) { return Self(this)->IsRecordCompressed(r); }           // 41
    virtual void SetRecordFullSize(const uint32_t *r, uint8_t useArg, uint32_t v)                              // 42
    {
        Self(this)->SetRecordFullSize(r, useArg, v);
    }
    virtual uint32_t GetCompressedStoredSize(const uint32_t *r) { return Self(this)->GetCompressedStoredSize(r); } // 43
    virtual bool AllocateSpace(uint32_t *out, uint32_t length) { return Self(this)->AllocateSpace(out, length); }  // 44
    virtual bool AddFreeRange(uint32_t offset, uint32_t length) { return Self(this)->AddFreeRange(offset, length); } // 45
    virtual bool Reserved46(uint32_t a, uint32_t b, uint32_t c) { return Self(this)->Reserved46(a, b, c); }        // 46
    virtual bool GetFreeRangeTotals(uint32_t *count, uint32_t *total) // 47
    {
        return Self(this)->GetFreeRangeTotals(count, total);
    }
    virtual uint32_t GetIndexMinorVersion() { return Self(this)->mHeader.indexMinorVersion; } // 48
    virtual void SetMatchFlagA(uint8_t v) { Self(this)->SetMatchFlagA(v); }                   // 49
    virtual void SetMatchFlagB(uint8_t v) { Self(this)->SetMatchFlagB(v); }                   // 50
    virtual void GetInfo(PackedFileInfo *out) { Self(this)->GetInfo(out); }                   // 51
    virtual bool GetRecordFlag(const uint32_t *r) { return Self(this)->GetRecordFlag(r); }    // 52
    virtual bool OpenRecordForWrite(const uint32_t *key, void **out) // 53
    {
        return Self(this)->OpenRecordForWrite(key, out);
    }
    virtual uint8_t ExportRecord(const uint32_t *key, cIGZString *path) { return Self(this)->ExportRecord(key, path); } // 54
    virtual bool SeekTo(uint32_t offset) { return Self(this)->SeekTo(offset); }                                         // 55
    virtual bool ReadAt(void *dst, uint32_t offset, uint32_t length) { return Self(this)->ReadAt(dst, offset, length); } // 56
    virtual bool ReadCountedAt(void *dst, uint32_t offset, uint32_t *length) // 57
    {
        return Self(this)->ReadCountedAt(dst, offset, length);
    }
    virtual bool WriteAt(const void *src, uint32_t offset, uint32_t length) // 58
    {
        return Self(this)->WriteAt(src, offset, length);
    }
};

// The subobject's table (this = the cGZPersistDBSegment at +4). Slots 13, 14, 17..32, 48 and 49 are the base class's.
class cGZDBSegmentPackedFileSub final {
public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Sub(this)->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return Sub(this)->AddRef(); }                                                     // 1
    virtual uint32_t Release() { return Sub(this)->Release(); }                                                   // 2
    virtual bool Init() { return Sub(this)->Init(); }                                                             // 3
    virtual bool Shutdown() { return Sub(this)->Shutdown(); }                                                     // 4
    virtual uint8_t Open(uint32_t read, uint32_t write) { return Sub(this)->Open(read, write); }                  // 5
    virtual bool AutoOpen(uint8_t state, uint8_t flag) { return Sub(this)->AutoOpen(state, flag); }               // 6
    virtual bool IsOpen() { return Sub(this)->IsOpen(); }                                                         // 7
    virtual bool IsWritable() { return Sub(this)->IsWritable(); }                                                 // 8
    virtual uint8_t Close() { return Sub(this)->Close(); }                                                        // 9
    virtual uint8_t Flush() { return Sub(this)->Flush(); }                                                        // 10
    virtual void GetPath(cIGZString *out) { Sub(this)->GetPath(out); }                                            // 11
    virtual bool SetPath(void *path) { return Sub(this)->SetPath(path); }                                         // 12
    virtual bool Lock() { return Base(this)->Lock(); }                                                            // 13
    virtual bool Unlock() { return Base(this)->Unlock(); }                                                        // 14
    virtual uint32_t GetSegmentID() { return Sub(this)->GetSegmentID(); }                                         // 15
    virtual bool SetSegmentID(const uint32_t *id) { return Sub(this)->SetSegmentID(id); }                         // 16
    virtual void SetSegmentCache(void *cache) { Base(this)->SetSegmentCache(cache); }                             // 17
    virtual uint32_t GetRecordCount(uint32_t a) { return Base(this)->GetRecordCount(a); }                         // 18
    virtual uint32_t GetResourceKeyList(uint32_t a, uint32_t b) { return Base(this)->GetResourceKeyList(a, b); }  // 19
    virtual bool TestForRecord(uint32_t a) { return Base(this)->TestForRecord(a); }                               // 20
    virtual uint32_t GetRecordSize(uint32_t a) { return Base(this)->GetRecordSize(a); }                           // 21
    virtual bool OpenRecord(uint32_t a, uint32_t b, uint32_t c) { return Base(this)->OpenRecord(a, b, c); }       // 22
    virtual bool CreateNewRecord(uint32_t a, uint32_t b) { return Base(this)->CreateNewRecord(a, b); }            // 23
    virtual bool CloseRecord(uint32_t a) { return Base(this)->CloseRecord(a); }                                   // 24
    virtual bool AbortRecord(uint32_t a) { return Base(this)->AbortRecord(a); }                                   // 25
    virtual bool DeleteRecord(uint32_t a) { return Base(this)->DeleteRecord(a); }                                 // 26
    virtual uint32_t ReadRecord(uint32_t a, uint32_t b, uint32_t c) { return Base(this)->ReadRecord(a, b, c); }   // 27
    virtual bool WriteRecord(uint32_t a, uint32_t b, uint32_t c) { return Base(this)->WriteRecord(a, b, c); }     // 28
    virtual uint32_t GetSegmentChangeID() { return Base(this)->GetSegmentChangeID(); }                            // 29
    virtual bool GetRecordChangeID(uint32_t key, uint32_t *id) { return Base(this)->GetRecordChangeID(key, id); } // 30
    virtual void SetNamespace(void *ns) { Base(this)->SetNamespace(ns); }                                         // 31
    virtual bool GetNamespace(void **out) { return Base(this)->GetNamespace(out); }                               // 32
    virtual ::cGZDBSegmentPackedFile *DeletingDestructor(uint32_t flags) { return Sub(this)->DeletingDestructor(flags); } // 33
    virtual void Delete() { Sub(this)->Delete(); }                                                                // 34
    virtual bool DoTestForRecord(const uint32_t *key) { return Sub(this)->DoTestForRecord(key); }                 // 35
    virtual uint32_t DoGetRecordSize(const uint32_t *key) { return Sub(this)->DoGetRecordSize(key); }             // 36
    virtual bool DoOpenRecord(const uint32_t *key, void **out, uint32_t mode) // 37
    {
        return Sub(this)->DoOpenRecord(key, out, mode);
    }
    virtual bool DoCreateNewRecord(const uint32_t *key, void **out) { return Sub(this)->DoCreateNewRecord(key, out); } // 38
    virtual bool DoCloseRecord(void *handle) { return Sub(this)->DoCloseRecord(handle); }                              // 39
    virtual bool DoAbortRecord(void *handle) { return Sub(this)->DoAbortRecord(handle); }                              // 40
    virtual bool DoDeleteRecord(const uint32_t *key) { return Sub(this)->DoDeleteRecord(key); }                        // 41
    virtual uint32_t DoReadRecord(const uint32_t *key, void *dst, uint32_t *size) // 42
    {
        return Sub(this)->DoReadRecord(key, dst, size);
    }
    virtual bool DoWriteRecord(const uint32_t *key, const void *data, uint32_t size) // 43
    {
        return Sub(this)->DoWriteRecord(key, data, size);
    }
    virtual uint32_t DoGetRecordCount(uint32_t filter) { return Sub(this)->DoGetRecordCount(filter); }              // 44
    virtual uint32_t DoGetResourceKeyList(uint32_t a, uint32_t b) { return Sub(this)->DoGetResourceKeyList(a, b); } // 45
    virtual uint32_t DoGetSegmentChangeID() { return Sub(this)->DoGetSegmentChangeID(); }                           // 46
    virtual bool DoGetRecordChangeID(void *arg, uint32_t *out) { return Sub(this)->DoGetRecordChangeID(arg, out); } // 47
    virtual void SetNamespaceInner(void *ns) { Base(this)->SetNamespaceInner(ns); }                                 // 48
    virtual bool GetNamespaceInner(void **out) { return Base(this)->vf49(out); }                                    // 49
};

// The abstract interface table (cIGZDBSegmentPackedFile): every slot pure, 53 of them.
class cIGZDBSegmentPackedFile final {
public:
    virtual void Pure0(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure1() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure2() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure3() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure4() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure5() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure6() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure7(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure8(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure9(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure10(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure11(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure12(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure13(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure14(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure15(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure16(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure17(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure18(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure19(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure20(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure21(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure22() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure23() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure24() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure25() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure26(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure27(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure28(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure29(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure30(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure31() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure32() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure33(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure34(uint32_t, uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure35(uint32_t, uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure36() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure37() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure38(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure39(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure40(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure41(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure42(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure43(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure44(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure45(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure46(uint32_t, uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure47(uint32_t, uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure48() { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure49(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure50(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure51(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
    virtual void Pure52(uint32_t) { PureSlot(); } // eax-ignored: pure slot, aborts
};

} // namespace rzface

// The game's tables (the image's objects in the hooked game) and ours.
const void *cGZDBSegmentPackedFile::VTable() { return &ts2_rva_e5fc58; }
const void *cGZDBSegmentPackedFile::SubVTable() { return &ts2_rva_e5fd48; }
const void *cGZDBSegmentPackedFile::InterfaceVTable() { return &ts2_rva_e5f45c; }

const void *cGZDBSegmentPackedFile::OwnVTable()
{
    rzface::cGZDBSegmentPackedFile face;
    return *reinterpret_cast<const void *const *>(&face);
}

const void *cGZDBSegmentPackedFile::OwnSubVTable()
{
    rzface::cGZDBSegmentPackedFileSub face;
    return *reinterpret_cast<const void *const *>(&face);
}

const void *cGZDBSegmentPackedFile::OwnInterfaceVTable()
{
    rzface::cIGZDBSegmentPackedFile face;
    return *reinterpret_cast<const void *const *>(&face);
}
