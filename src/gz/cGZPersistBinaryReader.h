#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "game/owned.h"

// cGZPersistBinaryReader: reads typed values from a persisted binary stream, used when loading
// resources. Primary vtable 0xe60f34 (cIGZPersistReader and cIGZUnknown at +0). RVAs are 1.18.0.187.
//
// Layout, from the constructor (RVA 0x9bf9a0), the deleting destructor (0x9bfc20, size 0x28) and the
// methods below:
//   +0x00 vtable
//   +0x04 reference count (AddRef at slot 1 increments it)
//   +0x08 ring sub-object: owns an 8-byte sentinel node (the constructor allocates it), and holds a
//         circular buffer of entry pointers. Its destructor (0x9bfa70) frees the entries and the buffer.
//   +0x0c   buffer: RingEntry * [capacity]         (sub-object +4)
//   +0x10   capacity, a power of two               (sub-object +8)
//   +0x14   head index                              (sub-object +0xc)
//   +0x18   count of live entries                   (sub-object +0x10)
//   +0x1c cIGZIStream *: the stream every read goes through (set by the creator; zero after construction)
//   +0x20 cIGZPersistReader *: optional delegate for the array readers (zero after construction)
//   +0x24 the group ReadRoot looks dependent resources up in first (zeroed by the constructor)
// +0x08..+0x1b is MSVC 14.40's std::deque of 32-byte entries (EntryRing below; the deque keeps a proxy in every
// build), so ReadRoot and the destructor use a real std::deque through Ring().
//
// Typed reads call the stream's slots as the original does: 4/5 Sint8, 5 Uint8 (also used as the bool
// read), 6/7 Sint16, 7 Uint16, 8/9 Sint32, 9 Uint32, 12 Float32, 13 Float64, 17 GetVoid (raw read).
// The stream's own getters are not part of this class; the stream is a cIGZIStream subobject.
//
// The array readers (slots 16, 18, 20) forward to the delegate, when there is one, through the slot
// numbers the original uses (16 -> 10, 18 -> 14, 20 -> 20): the delegate's layout is not known here.
class cGZPersistBinaryReader {
public:
    static constexpr uint32_t kVTable = 0xe60f34;     // the game's; VTable() is its owned symbol
    static const void *VTable();                      // the word our code stores at +0
    static const void *OwnVTable();                   // rzface::cGZPersistBinaryReader's: the image-free VTable()

    // The constructor, 0x9bf9a0: the vtable, a zero count, an empty ring (its proxy node allocated) and no stream,
    // delegate or group. The caller owns the memory (0x28 bytes); the count starts at 0 (the creator AddRefs).
    cGZPersistBinaryReader *Construct();
    uint32_t Release();                                 // slot 2, shared_vf_178b40: our own body, never hooked

    // Interface ids QueryInterface accepts (compared directly, no subtraction).
    static constexpr uint32_t kIdFirst = 1;
    static constexpr uint32_t kIdSecond = 0xfb0c4344;

    // Stream slots (cIGZIStream numbering).
    static constexpr int kStreamSint8 = 4;
    static constexpr int kStreamUint8 = 5;
    static constexpr int kStreamSint16 = 6;
    static constexpr int kStreamSint32 = 8;
    static constexpr int kStreamUint32 = 9;
    static constexpr int kStreamFloat32 = 12;
    static constexpr int kStreamFloat64 = 13;
    static constexpr int kStreamGetVoid = 17;

    // Delegate slots the array readers jump to when mDelegate is set.
    static constexpr int kDelegateSint16Array = 10;
    static constexpr int kDelegateSint32Array = 14;
    static constexpr int kDelegateFloat32Array = 20;

    // One entry of the ring. Only the two pointer arrays at +0 and +0xc are read here (+0x18 and +0x1c
    // hold reference counted objects that the destructor releases; not used by these functions).
    struct RingEntry {
        void **mFirst;   // +0
        uint32_t mPad4;  // +4
        uint32_t mPad8;  // +8
        void **mSecond;  // +0xc
    };

    // A counted reference (slot 1 AddRef, slot 2 Release, null allowed), the element type of the ring entries' vectors
    // and of their stream / record fields. Copying stores, then AddRefs; there is no move, so the vectors' growth copies
    // and releases the old elements, as the game's (0x9bef30 / 0x9bf850) do.
    class Ref {
    public:
        Ref() = default;
        explicit Ref(void *object) : mObject(object) { Hold(mObject); }
        Ref(const Ref &other) : mObject(other.mObject) { Hold(mObject); }
        Ref &operator=(const Ref &) = delete;
        ~Ref() noexcept(false) { Drop(mObject); }
        void *Get() const { return mObject; }
        void **Out() { return &mObject; }
        // Clears the field, then releases the old object (the order the game's inlined reset uses).
        void Reset()
        {
            void *old = mObject;
            if (old == nullptr) return;
            mObject = nullptr;
            Drop(old);
        }

    private:
        static void Hold(void *object);
        static void Drop(void *object);
        void *mObject = nullptr;
    };

    // One element of the ring (32 bytes): what ReadRoot pushes for the stream it reads from. GetObjectRef indexes the
    // two vectors (its first flag picks the resources).
    struct Entry {
        std::vector<Ref> mResources; // +0: the dependent resources, in the order the stream lists them (null if not found)
        std::vector<Ref> mObjects;   // +0xc: the root, then the objects the stream lists (created by class id)
        Ref mStream;                 // +0x18: the stream (cIGZIStream)
        Ref mRecord;                 // +0x1c: the stream's serial record face (QueryInterface 0xc56b8f08), or null
        explicit Entry(const Ref &stream) : mStream(stream) {}
    };
    using EntryRing = std::deque<Entry>; // at +8: the game's std::deque<Blob4<32>> (proxy, map, map size, offset, size)

    // The 16-byte resource key (cIGZPersistResource slot 3 fills one).
    struct Key {
        uint32_t mType;
        uint32_t mGroup;
        uint32_t mInstance;
        uint32_t mInstanceHi;
    };

    static constexpr uint32_t kGroupContextIid = 0xad095a01;  // the context's group face (slots 4 remap, 5 get, 6 set)
    static constexpr uint32_t kSerialRecordIid = 0xc56b8f08;  // the stream's serial record face (the delegate)
    static constexpr uint32_t kResourceIid = 0x9b11bf70;      // asked of the resource manager for each dependent resource
    static constexpr uint32_t kSerializableIid = 0xbb11a53e;  // the listed objects (CreateInstance)
    static constexpr uint32_t kReadObjectIid = 0x5b0aaa74;    // ReadObject's CreateInstance
    static constexpr uint32_t kFallbackGroup = 0x1c0532fa;    // the last group a dependent resource is looked up in

    // Slot 3, 0x9bfd10 (ret 0xc): reads `root` (its persist resource face from root slot 8) and the objects it refers to
    // from `stream`; `context` may give a group face. Details in the .cpp.
    void ReadRoot(void *root, void *stream, void *context);

    bool QueryInterface(uint32_t id, void **out);       // slot 0, 0x9bfcb0 (AddRef through slot 1)
    bool GetBool();                                     // slot 4, 0x9c0510: stream Uint8 slot, normalised
    int8_t GetSint8();                                  // slot 5, 0x9c0800
    uint8_t GetUint8();                                 // slot 6, 0x9c0900
    int16_t GetSint16();                                // slot 7, 0x9c0740
    int32_t GetSint32();                                // slot 9, 0x9c07a0
    uint32_t GetUint32();                               // slot 10, 0x9c08a0
    float GetFloat32();                                 // slot 11, 0x9c0570
    double GetFloat64();                                // slot 12, 0x9c05d0
    void GetBoolArray(bool *dst, uint32_t count);       // slot 13, 0x9c0530: no delegate
    bool GetVoid(void *dst, uint32_t n);                // slots 14, 15, 0x9c0820: forwards to stream slot 17
    void GetSint16Array(int16_t *dst, uint32_t count);  // slot 16, 0x9c0760: delegate slot 10
    void GetSint32Array(int32_t *dst, uint32_t count);  // slot 18, 0x9c07c0: delegate slot 14
    void GetFloat32Array(float *dst, uint32_t count);   // slot 20, 0x9c0590: delegate slot 20
    bool GetObjectRef(uint32_t id, void **out);         // slot 25, 0x9c06b0: ring lookup, then QueryInterface

    uint16_t GetUint16();                               // slot 8, 0x9c0840: stream slot 7 into the this slot
    void GetUint16Array(uint16_t *dst, uint32_t count); // slot 17, 0x9c0860: delegate slot 8, else stream slot 7
    void GetUint32Array(uint32_t *dst, uint32_t count); // slot 19, 0x9c08c0: delegate slot 12, else stream slot 9
    void GetFloat64Array(double *dst, uint32_t count);  // slot 21, 0x9c05f0: delegate slot 22, else stream slot 13
    bool GetGZStr(void *str);                           // slot 22, 0x9c0830: stream slot 15 (cIGZString *)
    bool Skip(uint32_t n);                              // slot 23, 0x9c0920: stream slot 3
    // Slot 24, 0x9c0630 (ret 8): reads a class id; 0 gives *out = null, else the framework creates the object (iid
    // 0x5b0aaa74), it reads itself (its slot 3 with this), answers QueryInterface(iid, out) and is released. The result
    // is what the original leaves in eax: the Release's count, or `out` for class id 0.
    uint32_t ReadObject(uint32_t iid, void **out);
    // Slot 26, 0x9c0490 (ret 4): fills a {counted cIGZString, uint32, uint32} record: a new empty cRZString (stored
    // through RZUnknownSlot::Assign after clearing the field without a release), the stream's slot 15 into it, then two
    // stream slot 9 reads into the words. Returns `record`.
    void *ReadStringRecord(void *record);
    // Slot 29, 0x9bfc20 (ret 4): destroys the ring (its entries and blocks, the map and the proxy); frees this (0x28
    // bytes) when flags & 1. Returns this.
    cGZPersistBinaryReader *DeletingDestructor(uint32_t flags);

    EntryRing &Ring() { return *reinterpret_cast<EntryRing *>(&mSentinel); }

    const void *mVTable;        // +0
    uint32_t mRefCount;         // +4
    void *mSentinel;            // +8: owned 8-byte node of the ring sub-object
    RingEntry **mBuffer;        // +0xc
    uint32_t mCapacity;         // +0x10
    uint32_t mHead;             // +0x14
    uint32_t mCount;            // +0x18
    void *mStream;              // +0x1c: cIGZIStream
    void *mDelegate;            // +0x20: cIGZPersistReader or null
    uint32_t mGroup;            // +0x24: the group ReadRoot looks dependent resources up in first (0 = none)
};
static_assert(sizeof(cGZPersistBinaryReader::Entry) == 0x20, "cGZPersistBinaryReader: ring entry 32 bytes");
static_assert(sizeof(cGZPersistBinaryReader::EntryRing) == 0x14, "cGZPersistBinaryReader: ring = proxy, map, size, offset, count");

static_assert(offsetof(cGZPersistBinaryReader, mVTable) == 0, "cGZPersistBinaryReader: vtable at +0");
static_assert(offsetof(cGZPersistBinaryReader, mRefCount) == 4, "cGZPersistBinaryReader: refcount at +4");
static_assert(offsetof(cGZPersistBinaryReader, mSentinel) == 8, "cGZPersistBinaryReader: ring node at +8");
static_assert(offsetof(cGZPersistBinaryReader, mBuffer) == 0xc, "cGZPersistBinaryReader: ring buffer at +0xc");
static_assert(offsetof(cGZPersistBinaryReader, mCapacity) == 0x10, "cGZPersistBinaryReader: capacity at +0x10");
static_assert(offsetof(cGZPersistBinaryReader, mHead) == 0x14, "cGZPersistBinaryReader: head at +0x14");
static_assert(offsetof(cGZPersistBinaryReader, mCount) == 0x18, "cGZPersistBinaryReader: count at +0x18");
static_assert(offsetof(cGZPersistBinaryReader, mStream) == 0x1c, "cGZPersistBinaryReader: stream at +0x1c");
static_assert(offsetof(cGZPersistBinaryReader, mDelegate) == 0x20, "cGZPersistBinaryReader: delegate at +0x20");
static_assert(sizeof(cGZPersistBinaryReader) == 0x28, "cGZPersistBinaryReader: size 0x28");

TS2_OWNED_STATIC(const OwnedVTable, e60f34);
inline const void *cGZPersistBinaryReader::VTable() { return &ts2_rva_e60f34; }

// ---- Our vtable ---------------------------------------------------------------------------------------------------
//
// One face in namespace rzface (the RTTI name is the game's). Slots forward to the members above; the stubs of the game's
// table (1 AddRef 0x148fb0, 27 the stream 0x147d00, 28 the group 0x15e970) are written out. Slot 2 is the face's own
// body (the game's is folded across four classes).
namespace rzface {

class cGZPersistBinaryReader final {
    using R = ::cGZPersistBinaryReader;
    R *Self() { return reinterpret_cast<R *>(this); }

public:
    virtual bool QueryInterface(uint32_t id, void **out) { return Self()->QueryInterface(id, out); }                // 0
    virtual uint32_t AddRef() { return ++Self()->mRefCount; }                                                       // 1
    virtual uint32_t Release() { return Self()->Release(); }                                                        // 2
    virtual void ReadRoot(void *root, void *stream, void *context) { Self()->ReadRoot(root, stream, context); }     // 3
    virtual bool GetBool() { return Self()->GetBool(); }                                                            // 4
    virtual int8_t GetSint8() { return Self()->GetSint8(); }                                                        // 5
    virtual uint8_t GetUint8() { return Self()->GetUint8(); }                                                       // 6
    virtual int16_t GetSint16() { return Self()->GetSint16(); }                                                     // 7
    virtual uint16_t GetUint16() { return Self()->GetUint16(); }                                                    // 8
    virtual int32_t GetSint32() { return Self()->GetSint32(); }                                                     // 9
    virtual uint32_t GetUint32() { return Self()->GetUint32(); }                                                    // 10
    virtual float GetFloat32() { return Self()->GetFloat32(); }                                                     // 11
    virtual double GetFloat64() { return Self()->GetFloat64(); }                                                    // 12
    virtual void GetBoolArray(bool *dst, uint32_t n) { Self()->GetBoolArray(dst, n); }                              // 13
    virtual bool GetVoidA(void *dst, uint32_t n) { return Self()->GetVoid(dst, n); }                                // 14
    virtual bool GetVoidB(void *dst, uint32_t n) { return Self()->GetVoid(dst, n); }                                // 15
    virtual void GetSint16Array(int16_t *dst, uint32_t n) { Self()->GetSint16Array(dst, n); }                       // 16
    virtual void GetUint16Array(uint16_t *dst, uint32_t n) { Self()->GetUint16Array(dst, n); }                      // 17
    virtual void GetSint32Array(int32_t *dst, uint32_t n) { Self()->GetSint32Array(dst, n); }                       // 18
    virtual void GetUint32Array(uint32_t *dst, uint32_t n) { Self()->GetUint32Array(dst, n); }                      // 19
    virtual void GetFloat32Array(float *dst, uint32_t n) { Self()->GetFloat32Array(dst, n); }                       // 20
    virtual void GetFloat64Array(double *dst, uint32_t n) { Self()->GetFloat64Array(dst, n); }                      // 21
    virtual bool GetGZStr(void *str) { return Self()->GetGZStr(str); }                                              // 22
    virtual bool Skip(uint32_t n) { return Self()->Skip(n); }                                                       // 23
    virtual uint32_t ReadObject(uint32_t iid, void **out) { return Self()->ReadObject(iid, out); }                  // 24
    virtual bool GetObjectRef(uint32_t id, void **out) { return Self()->GetObjectRef(id, out); }                    // 25
    virtual void *ReadStringRecord(void *record) { return Self()->ReadStringRecord(record); }                       // 26
    virtual void *GetStream() { return Self()->mStream; }                                                           // 27
    virtual uint32_t GetGroup() { return Self()->mGroup; }                                                          // 28
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }                 // 29
};

} // namespace rzface

inline const void *cGZPersistBinaryReader::OwnVTable()
{
    rzface::cGZPersistBinaryReader face;
    return *reinterpret_cast<const void *const *>(&face);
}
