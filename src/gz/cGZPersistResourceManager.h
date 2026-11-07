#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>

// The game's resource manager (cIGZPersistResourceManager, with cRZSystemService and cRZUnknownMT
// subobjects). It maps resource keys to loaded resources and factories. RVAs are 1.18.0.187.
//
// Field map (offsets from the start of the full object):
//   +0x00  primary vtable (54 slots, RVA 0xe60274)
//   +0x04  cRZSystemService vtable (RVA 0xe60350, 13 slots)
//   +0x08  cRZUnknownMT vtable (RVA 0xe60388, 6 slots)
//   +0x20  lock A, an object of its own (vtable at +0x20). Slot 1 = Enter, slot 2 = Leave, called
//          with the lock's address as `this`. Used by vf25 and vf26.
//   +0x28  hash map factory id -> factory object (cGZPersistHashMap; its list head is at +0x2c and its
//          size at +0x30, read by vf25 and vf26). Used under lock A.
//   +0x48  hash map resource type -> factory id (cGZPersistHashMap). vf20 registers into both maps;
//          vf22 looks a type up here, then the factory id in +0x28.
//   +0x68  lock B, same interface as lock A. Used by vf35, vf36, vf46, vf47, vf48 and the deque
//          readers vf15, vf18, vf31, vf32 and vf33 under lock B.
//   +0x70  std::deque<uint32_t> (MSVC layout, see cGZPersistDeque). Its element count is at +0x80,
//          which vf35 returns under lock B.
//   +0x84  object pointer, set by vf46 under lock B (AddRef on the new one, Release on the old one);
//          vf47 returns it under lock B, and vf48 calls its slot 10 under lock B (when non-null).
//   +0x88  lock C, same interface as lock A (vtable at +0x88, second dword unknown). Used by vf11,
//          vf41, vf43, vf44, vf45, vf50 and OnIdle.
//   +0x90  value list head: pointer to the sentinel of an MSVC std::list whose nodes are
//          cGZPersistValueNode (12 bytes, value at +8). Used under lock C.
//   +0x94  value list size (_Mysize of that list). vf41 refuses to grow it past 0x15555555.
//   +0x98  lock D, same interface as lock A (vtable at +0x98). Used by vf6 only.
//   +0xa0  serial counter, bumped under lock D by vf6.
//   +0xa4  field read under lock D by vf6 (the value is not used there).
// Not mapped yet: +0x8c.
//
// OnIdle is slot 11 of the cRZSystemService vtable at +0x04, so its `this` is that subobject, not
// the full object: the code reads the lock at subobject +0x84 and the list head at subobject +0x8c,
// which are the full-object offsets +0x88 and +0x90 above. The member function takes the
// subobject as `this` and steps back 4 bytes to reach the full object.
//
// Calls through other objects' vtables (this's slots 4, 8, 22, 23, 34; lock slots; the value
// objects' slots; the object at +0x84 and the argument of vf9) are made as the original makes
// them: through the vtable, with the arguments pushed in the same order.

// The deque at +0x70 (MSVC std::deque<uint32_t>). Element i is
// map[((offset + i) >> 2) & (mapSize - 1)][(offset + i) & 3]; mapSize is a power of two.
struct cGZPersistDeque {
    void *proxy;      // +0x00 (+0x70): iterator proxy whose first dword points back at the deque
    uint32_t **map;   // +0x04 (+0x74): block pointers, four uint32_t per block
    uint32_t mapSize; // +0x08 (+0x78)
    uint32_t offset;  // +0x0c (+0x7c): position of element 0 in the blocks
    uint32_t size;    // +0x10 (+0x80): element count
};
static_assert(sizeof(cGZPersistDeque) == 0x14, "cGZPersistDeque layout");

// Node of the hash maps at +0x28 and +0x48 (MSVC std::list layout under a hash map; key at +8,
// mapped value at +0xc).
struct cGZPersistListNode {
    cGZPersistListNode *next;
    cGZPersistListNode *prev;
    uint32_t key;
    uint32_t value;
};
static_assert(sizeof(cGZPersistListNode) == 0x10, "cGZPersistListNode layout");

// A resource key (type, group, instance, resource), as resources return it from slot 3 (by value). 16 bytes: the
// game's slot 3 implementations (e.g. 0x49e6c0) copy 16 bytes into the caller's key; a 12-byte key here let them
// write past our stack temporary (crash in neighbourhood save, runs46).
struct cGZPersistResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
};
static_assert(sizeof(cGZPersistResourceKey) == 0x10, "resource key layout");

// A bucket of those maps: its first and last node in the list ({head, head} when empty).
struct cGZPersistBucket {
    cGZPersistListNode *first;
    cGZPersistListNode *last;
};

// The hash maps at +0x28 (factory id -> factory) and +0x48 (resource type -> factory id): the MSVC unordered_map
// layout of the game's STL, uint32_t keys hashed with FNV-1a over their four bytes. The game's find
// walks a bucket forwards from its first node (MSVC 14.40's walks backwards), so lookups are written
// out here.
struct cGZPersistHashMap {
    float maxLoadFactor;          // +0x00
    cGZPersistListNode *head;     // +0x04: list sentinel (end())
    uint32_t size;                // +0x08
    cGZPersistBucket *buckets;    // +0x0c: bucket vector begin
    cGZPersistBucket *bucketsEnd; // +0x10
    cGZPersistBucket *bucketsCap; // +0x14
    uint32_t mask;                // +0x18: bucket count - 1
    uint32_t maxIndex;            // +0x1c: bucket count

    struct Iterator {
        cGZPersistListNode *node;
    };

    // find(key): the node holding `key`, or head when there is none. The game inlines it for the
    // type map and calls it out of line (0x9b3eb0) for the resource map.
    __forceinline Iterator FindInline(const uint32_t &key) const
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&key);
        uint32_t hash = 0x811c9dc5u;
        for (int i = 0; i < 4; ++i) hash = (hash ^ bytes[i]) * 0x1000193u;
        const cGZPersistBucket &bucket = buckets[hash & mask];
        cGZPersistListNode *node = bucket.first;
        if (node == head) return {head};
        cGZPersistListNode *last = bucket.last;
        for (; node->key != key; node = node->next)
            if (node == last) return {head};
        return {node};
    }
    // 0x9b3eb0 (thiscall, the iterator returned through a hidden pointer, ret 8).
    __declspec(noinline) Iterator Find(const uint32_t &key) const;

    // erase(node): fixes the node's bucket (emptied, or its first/last moved past the node), unlinks
    // the node from the list and frees it (sized delete, 16 bytes). Inlined by the game.
    __forceinline void Erase(cGZPersistListNode *node)
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&node->key);
        uint32_t hash = 0x811c9dc5u;
        for (int i = 0; i < 4; ++i) hash = (hash ^ bytes[i]) * 0x1000193u;
        cGZPersistBucket &bucket = buckets[hash & mask];
        if (bucket.last == node) {
            if (bucket.first == node) {
                bucket.first = head;
                bucket.last = head;
            } else {
                bucket.last = node->prev;
            }
        } else if (bucket.first == node) {
            bucket.first = node->next;
        }
        node->prev->next = node->next;
        node->next->prev = node->prev;
        --size;
        ::operator delete(node, sizeof(cGZPersistListNode));
    }
};
static_assert(sizeof(cGZPersistHashMap) == 0x20, "cGZPersistHashMap layout");
static_assert(sizeof(std::unordered_map<uint32_t, uint32_t>) == sizeof(cGZPersistHashMap), "the factory map's library type");

// List node of the list at +0x90 (MSVC std::list layout; 12 bytes, value at +8).
struct cGZPersistValueNode {
    cGZPersistValueNode *next;
    cGZPersistValueNode *prev;
    void *value;
};
static_assert(sizeof(cGZPersistValueNode) == 0xc, "cGZPersistValueNode layout");

class cGZPersistResourceManager {
public:
    static constexpr uint32_t kVTable = 0xe60274;

    // Slot 3, 0x9b4d70: returns this->slot4(a1, a2, a3, a4, a5, 0).
    uint32_t vf3(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    // Slot 9, 0x9b5430: obj->slot4(a1), then returns this->slot8(obj).
    uint32_t vf9(uint32_t a1, void *obj);
    // Slot 11, 0x9b4f80: under lock C, true (1) if slot 10 of some value in the list returns
    // non-zero for a1. Stops at the first such value.
    uint8_t vf11(uint32_t a1);
    // Slot 6, 0x9b4c20. `a1` is not used (the original overwrites its copy). Under lock C, the
    // serial counter is taken under lock D and bumped; then this->slot51(&key, iid, &obj, a4, a5)
    // with key = 0 and obj = the old serial. If that returns non-zero: obj->slot4(&key), then
    // obj->slot0(a2, a3) (the result is 1 when that is non-zero, after this->slot50(&key, obj)).
    // obj is released in that case.
    uint8_t vf6(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    // Slot 8, 0x9b5460: obj->slot3(&out) (out is written by it), then under lock C this->slot11(&out)
    // with the out pointer as a value; when that is zero, this->slot50(&out, obj). Null obj: 0.
    uint8_t vf8(void *obj);
    // Slot 23, 0x9b4790: returns this->slot22(*key, b).
    uint32_t vf23(const uint32_t *key, uint32_t b);
    // Slot 25, 0x9b4b50: under lock A, returns the list size.
    uint32_t vf25();
    // Slot 26, 0x9b4b10: under lock A, returns the value of node `index` (0 when index >= size).
    uint32_t vf26(uint32_t index);
    // cIGZUnknown-style QueryInterface (slot 0), 0x9b5100. riid 0x656b8efc stores this in *ppv and
    // AddRefs it (through this's slot 1) and returns 1; anything else is the cRZSystemService
    // QueryInterface of the subobject at +4, tail-called in the original.
    uint8_t QueryInterface(uint32_t riid, void **ppv);
    // Slot 10, 0x9b5d30: under lock C, for the values in order (while the result is 0): if slot 10
    // of the value returns non-zero for a1, the result is that value's slot 9 for a1.
    uint8_t vf10(uint32_t a1);
    // Slot 12, 0x9b5560: `range` points at {begin, end} of 16-byte items; returns 1 when this's
    // slot 13 returns non-zero for every item (with a2), else 0 (1 for an empty range).
    uint8_t vf12(void *const *range, uint32_t a2);
    // Slot 15, 0x9b59c0: under lock B, 1 when slot 20 of some deque element returns non-zero for a1
    // (stops at the first).
    uint8_t vf15(uint32_t a1);
    // Slot 16, 0x9b4d90: under lock C, sum of slot 12 (a1, a2) over every value in the list.
    uint32_t vf16(uint32_t a1, uint32_t a2);
    // Slot 18, 0x9b4970: under lock B, sum of slot 19 (a1, a2) over every deque element.
    uint32_t vf18(uint32_t a1, uint32_t a2);
    // Slot 27, 0x1e36e0: a 7-byte tail jump to this's slot 28 (one dword argument, ret 4), so the
    // result is slot 28's.
    uint32_t vf27(uint32_t a1);
    // Slot 24, 0x9b47b0: returns this's slot 22 (obj->slot5(), a2).
    uint32_t vf24(void *obj, uint32_t a2);
    // Slot 31, 0x9b5940: under lock B, 1 when some deque element equals a1 (stops at the first).
    uint8_t vf31(uint32_t a1);
    // Slot 32, 0x9b4890: under lock B, AND of slot 10 over every deque element (all are called).
    uint8_t vf32();
    // Slot 33, 0x9b46f0: under lock B, *out = 0; the first element whose slot 15 returns key is
    // stored in *out and AddRefed, and the result is 1. `out` must not be null.
    uint8_t vf33(uint32_t key, void **out);
    // Slot 35, 0x9b4f50: under lock B, returns the deque's element count (+0x80).
    uint32_t vf35();
    // Slot 36, 0x9b4ed0: under lock B, returns deque element `index`, or 0 when index >= the count.
    uint32_t vf36(uint32_t index);
    // Slot 39, 0x9b50a0: obj = this->slot34(a1) into a local (slot 34 writes it through its second
    // argument); if that returns non-zero, returns obj->slot22(a1, a2, a3 ? 3 : 1) and releases obj.
    uint8_t vf39(uint32_t a1, uint32_t a2, uint32_t a3);
    // Slot 40, 0x9b43e0: `this` is unused; returns (*holder)->slot6(), then releases *holder.
    uint8_t vf40(uint32_t a1, void *const *holder);
    // Slot 41, 0x9b4290: under lock C, adds `value` at the front of the list (AddRef on it) unless
    // it is already there. Returns 1 when added, 0 when already present. Throws length_error when
    // the list size is 0x15555555 (after the AddRef, under the lock, without leaving it).
    uint8_t vf41(void *value);
    // Slot 42, 0x9b54e0: under lock C, removes the first list node whose value is `value`; the node
    // is unlinked, the size drops, the value is released and the node is freed (sized delete).
    // Returns 1 when one was removed, 0 otherwise.
    uint8_t vf42(void *value);
    // Slot 43, 0x9b4ff0: under lock C, OR of slot 15 of every value in the list.
    uint8_t vf43();
    // Slot 44, 0x9b5740: under lock C, calls slot 16 of every value in the list with a1.
    void vf44(uint32_t a1);
    // Slot 45, 0x9b4920: under lock C, OR of slot 17 of every value in the list.
    uint8_t vf45();
    // Slot 46, 0x9b5790: under lock B, replaces the object at +0x84 with `obj` (AddRef the new one
    // unless it is the same, Release the old one).
    void vf46(void *obj);
    // Slot 47, 0x9b4f20: under lock B, returns the dword at +0x84.
    uint32_t vf47();
    // Slot 48, 0x9b4400: under lock B, calls slot 10 of the object at +0x84 when non-null, then
    // returns what lock B's Leave returns (the original tail-calls it).
    uint32_t vf48();
    // Slot 50, 0x9b4360: under lock C, finds the first value whose slot 11 returns non-zero for a1,
    // calls its slot 8 with (a1, a2) and returns 1; returns 0 when none matched.
    uint8_t vf50(uint32_t a1, uint32_t a2);
    // Slot 51, 0x9b44c0: if this->slot23(key, &obj) returns non-zero, returns obj->slot4(*key, a2,
    // a3, a4, a5) and releases obj; returns 0 otherwise.
    uint8_t vf51(const uint32_t *key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    // Slot 4, 0x9b4e40: if this->slot49(a1, a2, a3) returns non-zero, returns 1. Otherwise this->slot52
    // (a1, 0x456b8f1d, &out, a4, a5, a6) fills `out` (in the stack slot of a1); when that returns
    // non-zero, this->slot50(a1, out) is called and the result is out->slot0(a2, a3). `out` is
    // released when non-null, on both paths.
    uint8_t vf4(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
    // Slot 5, 0x9b4d20: if this->slot52(a1, 0x456b8f1d, &out, a4, a5, 0) fills `out` (in the stack
    // slot of a5) and returns non-zero, the result is out->slot0(a2, a3), and out is released.
    uint8_t vf5(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
    // Slot 34, 0x9b4650: under lock B, *out = 0; the first deque element whose slot 20 returns
    // non-zero for a1 is stored in *out and AddRefed (result 1). Elements are objects.
    uint8_t vf34(uint32_t a1, void **out);
    // Slot 37, 0x9b45f0: `pred` is a __cdecl function (element, a2) -> byte. Under no lock: over the
    // deque elements in order, calls pred(element, a2) and stops at the first zero; the result is
    // the last call's byte. A null pred or an empty deque returns the caller's eax untouched in the
    // original (undefined), so 0 is returned here.
    uint8_t vf37(uint32_t pred, uint32_t a2);
    // Slot 38, 0x9b4520: copies the first min(*count, deque size) deque elements into arr[0..],
    // AddRefing each, and sets *count to the number copied. Returns the deque size. Nothing is done
    // when arr is null or *count is 0, but the size is still returned (with *count untouched).
    uint32_t vf38(void **arr, uint32_t *count);
    // Slot 49, 0x9b4a40: under lock C, walks the value list: each value's slot 7 (a1, &out) is
    // tried in turn (AddRefing the value for the call); on the first non-zero, returns out->slot0
    // (a2, a3). `out` is released at the end (the previous one before each try).
    uint8_t vf49(uint32_t a1, uint32_t a2, uint32_t a3);
    // Slot 52, 0x9b4430: if this->slot23(a1, &obj1) finds an object, the result is this->slot53(a1,
    // obj1, obj, a2, a3, a4, a5) != 0, where obj is a6 when a6 is non-zero, and otherwise the
    // object this->slot34(a1, &obj) finds (released afterwards). obj1 is released.
    uint8_t vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
    // Slot 53, 0x9b5140: does not use `this`. If a3->slot22(a1, &key, 1) returns non-zero, out is
    // a2->slot3(key, 0x456b8f1d, &out, a6, a7); when that succeeds, the result is out->slot0(a4, a5)
    // (out->slot4(a1) is called first), and a3->slot24(key) is called; otherwise a3->slot25(key) is
    // called. `key` is released when non-null, and `out` when found.
    uint8_t vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);
    // cRZSystemService slot 11 (OnIdle), 0x9b5040, called with the subobject at +4 as `this`.
    // Under lock C, ORs slot 14 of every value in the list with a1 (the byte of a1 is the initial
    // value); returns the result as a byte.
    uint8_t OnIdle(uint32_t a1);
    // cRZSystemService slot 9 (Shutdown), 0x9b57e0, `this` the subobject at +4 as for OnIdle. Under lock C, each cache
    // strategy's slot 4, then the list emptied (each released, its node freed); under lock A, every factory released
    // and the factory map cleared (the type map is left as it is); under lock B, each segment's slots 9, 4, 2 (null
    // elements skipped), the deque cleared, then the default segment at +0x84 gets slots 10, 9, 4, is nulled and
    // released. Returns 1.
    uint8_t Shutdown();
    // Slot 7, 0x9b4b70: under lock C, creates the resource for `key` through slot 51 (interface
    // 0x456b8f1d, a4, a5), sets its key (slot 4) and asks it for `iid` into *out; when that succeeds,
    // registers it (slot 50) and returns true. The created resource is released either way.
    bool vf7(const void *key, uint32_t iid, void **out, uint32_t a4, uint32_t a5);
    // Slot 13, 0x9b55b0: gets the resource for `key` through slot 3 (interface 0x456b8f1d, 0, 0) and
    // returns slot 14 (resource, key, a2), releasing the resource; false when slot 3 fails.
    bool vf13(const void *key, uint32_t a2);
    // Slot 14, 0x9b5610: writes `resource` into `segment`. A record for the resource's key is
    // reopened for writing (mode 3) and emptied (SetSize 0) when the segment has one, otherwise
    // created for `key`; the factory for the resource (slot 24) writes it, and the record is closed.
    // True only when all of that succeeds; a record left open on failure is aborted.
    bool vf14(void *resource, const void *key, void *segment);
    // Slot 17, 0x9b4df0: returns slot 16 (list, a new type key filter for `type`), AddRef/Release
    // around the call.
    uint32_t vf17(uint32_t list, uint32_t type);
    // Slot 19, 0x9b49f0: the same as vf17 through slot 18.
    uint32_t vf19(uint32_t list, uint32_t type);
    // Slot 20, 0x9b5380: registers a resource factory. A null `factory` is created through the
    // framework (CreateInstance(factoryId, 0xa56b8f17)), a given one is AddRefed; with a factory,
    // under lock A, inserts {factoryId, factory} into the factory map and {type, factoryId} into the
    // type map (the game's inserts 0x9b3cf0 / 0x314ec0) and returns true. False when none was made.
    bool vf20(uint32_t factoryId, uint32_t type, void *factory);
    // Slot 21, 0x9b5b20: unregisters a factory. Under lock A, every factory-map entry for
    // `factoryId` is released and erased, then every type-map entry that maps to it is erased.
    // True when a factory was removed.
    bool vf21(uint32_t factoryId);
    // Slot 22, 0x9b47d0: under lock A, looks `type` up in the type map (+0x48) and the factory id
    // found in the factory map (+0x28, through its find 0x9b3eb0); when both are found, *out = the
    // factory, AddRefed, and the result is true.
    bool vf22(uint32_t type, void **out);
    // Slot 28, 0x9b52c0: under lock B, AddRefs `segment`, removes it from the deque (slot 30) and
    // pushes it to the front. Returns true.
    bool vf28(void *segment);
    // Slot 29, 0x9b5200: the same as vf28, pushing to the back.
    bool vf29(void *segment);
    // Slot 30, 0x9b5a60: under lock B, releases and erases the first deque element equal to
    // `segment` (the deque's erase, 0x2e9d10). True when one was removed.
    bool vf30(void *segment);
    // The deque at +0x70 as the library type it is.
    std::deque<void *> &Segments() { return *reinterpret_cast<std::deque<void *> *>(&mDeque); }
    // The factory map at +0x28 as the library type it is (MSVC unordered_map, FNV-1a std::hash of the uint32 key, node
    // 0x10: next, prev, key, value), for whole-table operations; lookups stay written out (see cGZPersistHashMap).
    std::unordered_map<uint32_t, uint32_t> &FactoryMap()
    {
        return *reinterpret_cast<std::unordered_map<uint32_t, uint32_t> *>(&mFactories);
    }

    const void *mVTable;                  // +0x00
    uint8_t mSubobjects[0x20 - 4];        // +0x04: subobject vtables, not modelled
    void *mLockA[2];                      // +0x20
    cGZPersistHashMap mFactories;         // +0x28: factory id -> factory object (list head +0x2c, size +0x30)
    cGZPersistHashMap mTypes;             // +0x48: type -> id
    void *mLockB[2];                      // +0x68
    cGZPersistDeque mDeque;               // +0x70 (element count at +0x80)
    uint32_t mField84;                    // +0x84 (object pointer, see vf46)
    void *mLockC[2];                      // +0x88
    cGZPersistValueNode *mValueHead;      // +0x90
    uint32_t mValueSize;                  // +0x94
    void *mLockD[2];                      // +0x98
    uint32_t mSerial;                     // +0xa0
    uint32_t mField;                      // +0xa4
};

static_assert(offsetof(cGZPersistResourceManager, mLockA) == 0x20, "lock A offset");
static_assert(offsetof(cGZPersistResourceManager, mFactories) + offsetof(cGZPersistHashMap, head) == 0x2c, "list head offset");
static_assert(offsetof(cGZPersistResourceManager, mFactories) + offsetof(cGZPersistHashMap, size) == 0x30, "list size offset");
static_assert(offsetof(cGZPersistResourceManager, mTypes) == 0x48, "type map offset");
static_assert(offsetof(cGZPersistResourceManager, mLockB) == 0x68, "lock B offset");
static_assert(offsetof(cGZPersistResourceManager, mDeque) == 0x70, "deque offset");
static_assert(offsetof(cGZPersistResourceManager, mDeque) + offsetof(cGZPersistDeque, size) == 0x80, "deque size offset");
static_assert(offsetof(cGZPersistResourceManager, mField84) == 0x84, "field +0x84 offset");
static_assert(offsetof(cGZPersistResourceManager, mLockC) == 0x88, "lock C offset");
static_assert(offsetof(cGZPersistResourceManager, mValueHead) == 0x90, "value list head offset");
static_assert(offsetof(cGZPersistResourceManager, mValueSize) == 0x94, "value list size offset");
static_assert(offsetof(cGZPersistResourceManager, mLockD) == 0x98, "lock D offset");
static_assert(offsetof(cGZPersistResourceManager, mSerial) == 0xa0, "serial offset");
static_assert(offsetof(cGZPersistResourceManager, mField) == 0xa4, "field offset");
