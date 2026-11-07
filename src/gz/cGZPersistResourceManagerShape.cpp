#include "gz/cGZPersistResourceManagerShape.h"
#include "gz/cGZPersistCacheStrategyBasic.h"
#include "rz/cRZCriticalSection.h"
#include "std/StdSeq.h"

#include <new>
#include <unordered_map>

namespace gz {

namespace {

cRZCriticalSection *Lock(void *(&lock)[2])
{
    return reinterpret_cast<cRZCriticalSection *>(lock);
}

// The two factory maps are std::unordered_map<uint32_t, uint32_t> of the game's STL, whose destructor 0x15d6a0 is the
// library's (db/library-matches.jsonl): ours has the same layout and code.
using U32Map = std::unordered_map<uint32_t, uint32_t>;
static_assert(sizeof(U32Map) == sizeof(cGZPersistHashMap), "unordered_map layout");

// The cache list's clear (the game's 0x99df40: the last node's link cut, then each strategy released and its node freed from
// the first on; the sentinel stays).
void ClearValueList(cGZPersistValueNode *sentinel)
{
    sentinel->prev->next = nullptr;
    cGZPersistValueNode *node = sentinel->next;
    while (node) {
        cGZPersistValueNode *next = node->next;
        if (node->value) reinterpret_cast<::cIGZUnknown *>(node->value)->Release();
        ::operator delete(node, sizeof(cGZPersistValueNode));
        node = next;
    }
}

// The segment deque's tidy (the game's 0x3cfe90, the library's deque tidy for 4-element blocks with an iterator proxy):
// elements are plain words, so only the blocks (0x10 bytes each, last to first) and the block table are freed.
void TidyDeque(cGZPersistDeque &deque)
{
    // Elements pop off the back; the start offset resets only when the last one goes.
    while (deque.size) {
        if (--deque.size == 0) deque.offset = 0;
    }
    if (deque.map) {
        for (uint32_t i = deque.mapSize; i > 0;) {
            --i;
            if (deque.map[i]) ::operator delete(deque.map[i], 0x10);
        }
        std::allocator<uint32_t *>().deallocate(deque.map, deque.mapSize);
        deque.map = nullptr;
        deque.mapSize = 0;
    }
}

} // namespace

void DestroyPersistU32Map(cGZPersistHashMap &map)
{
    reinterpret_cast<U32Map *>(&map)->~U32Map();
}

void ConstructPersistHashMap(cGZPersistHashMap &map, uint32_t nodeSize)
{
    map.maxLoadFactor = 0.0f;
    map.head = nullptr;
    map.size = 0;
    auto *head = static_cast<cGZPersistListNode *>(::operator new(nodeSize));
    head->next = head;
    head->prev = head;
    map.head = head;
    map.buckets = nullptr;
    map.bucketsEnd = nullptr;
    map.bucketsCap = nullptr;
    map.mask = 7;
    map.maxIndex = 8;
    map.maxLoadFactor = 1.0f;
    reinterpret_cast<StdSeqVectorU32 *>(&map.buckets)->Assign(16, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(head)));
}

// 0x9b3f30: the system service (id 0x56b906e, priority 1000000), the four locks, the two factory maps (node 0x10), the
// segment deque (with its 8-byte container proxy), the cache list (node 0xc), serial 0xffff; then a basic cache strategy
// (0x34 bytes, the game's constructor 0x9b35f0; ours since lane QQ) held by a local reference is appended to the cache list (0x8c1350, the
// list's push_back: AddRef, count, link at the back).
cGZPersistResourceManager::cGZPersistResourceManager() : rz::cRZSystemService(kServiceId, kPriority)
{
    Lock(mLockA)->Construct();
    ConstructPersistHashMap(mFactories, 0x10);
    ConstructPersistHashMap(mTypes, 0x10);
    Lock(mLockB)->Construct();
    mDeque.proxy = nullptr;
    mDeque.map = nullptr;
    mDeque.mapSize = 0;
    mDeque.offset = 0;
    mDeque.size = 0;
    auto *proxy = static_cast<void **>(::operator new(8));
    proxy[1] = nullptr;
    mDeque.proxy = proxy;
    proxy[0] = &mDeque;
    mField84 = 0;
    Lock(mLockC)->Construct();
    mValueHead = nullptr;
    mValueSize = 0;
    auto *head = static_cast<cGZPersistValueNode *>(::operator new(sizeof(cGZPersistValueNode)));
    head->next = head;
    head->prev = head;
    mValueHead = head;
    Lock(mLockD)->Construct();
    mSerial = 0xffff;
    mField = 0;

    void *memory = ::operator new(cGZPersistCacheStrategyBasic::kObjectSize);
    ::cIGZUnknown *cache = memory ? cGZPersistCacheStrategyBasic::Construct(memory) : nullptr;
    if (cache) cache->AddRef();
    auto *node = static_cast<cGZPersistValueNode *>(::operator new(sizeof(cGZPersistValueNode)));
    node->value = cache;
    if (cache) cache->AddRef();
    ++mValueSize;
    cGZPersistValueNode *last = mValueHead->prev;
    node->next = mValueHead;
    node->prev = last;
    mValueHead->prev = node;
    last->next = node;
    if (cache) cache->Release();
}

// 0x9b4120: lock D, the cache list (ClearValueList, ours: each strategy released, every node freed but the sentinel; then
// the sentinel), lock C, the default segment released, the deque (TidyDeque, ours) and its proxy, lock B, the two maps,
// lock A, then the system service.
cGZPersistResourceManager::~cGZPersistResourceManager()
{
    Lock(mLockD)->Destruct();
    ClearValueList(mValueHead);
    ::operator delete(mValueHead, sizeof(cGZPersistValueNode));
    Lock(mLockC)->Destruct();
    if (mField84) reinterpret_cast<::cIGZUnknown *>(static_cast<uintptr_t>(mField84))->Release();
    TidyDeque(mDeque);
    void *proxy = mDeque.proxy;
    mDeque.proxy = nullptr;
    ::operator delete(proxy, 8);
    Lock(mLockB)->Destruct();
    DestroyPersistU32Map(mTypes);
    DestroyPersistU32Map(mFactories);
    Lock(mLockA)->Destruct();
}

bool cGZPersistResourceManager::QueryInterface(uint32_t riid, void **out) { return Hooked()->QueryInterface(riid, out) != 0; }
uint32_t cGZPersistResourceManager::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZPersistResourceManager::Release() { return rz::cRZSystemService::Release(); }

uint32_t cGZPersistResourceManager::vf3(uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5) { return Hooked()->vf3(key, iid, out, a4, a5); }
uint8_t cGZPersistResourceManager::vf4(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) { return Hooked()->vf4(key, a2, a3, a4, a5, a6); }
uint8_t cGZPersistResourceManager::vf5(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { return Hooked()->vf5(key, a2, a3, a4, a5); }
uint8_t cGZPersistResourceManager::vf6(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { return Hooked()->vf6(a1, a2, a3, a4, a5); }
uint8_t cGZPersistResourceManager::vf7(const void *key, uint32_t iid, void **out, uint32_t a4, uint32_t a5) { return Hooked()->vf7(key, iid, out, a4, a5); }
uint8_t cGZPersistResourceManager::vf8(void *resource) { return Hooked()->vf8(resource); }
uint32_t cGZPersistResourceManager::vf9(uint32_t a1, void *resource) { return Hooked()->vf9(a1, resource); }
uint8_t cGZPersistResourceManager::vf10(uint32_t key) { return Hooked()->vf10(key); }
uint8_t cGZPersistResourceManager::vf11(uint32_t key) { return Hooked()->vf11(key); }
uint8_t cGZPersistResourceManager::vf12(void *const *range, uint32_t a2) { return Hooked()->vf12(range, a2); }
uint8_t cGZPersistResourceManager::vf13(const void *key, uint32_t a2) { return Hooked()->vf13(key, a2); }
uint8_t cGZPersistResourceManager::vf14(void *resource, const void *key, void *segment) { return Hooked()->vf14(resource, key, segment); }
uint8_t cGZPersistResourceManager::vf15(uint32_t key) { return Hooked()->vf15(key); }
uint32_t cGZPersistResourceManager::vf16(uint32_t a1, uint32_t a2) { return Hooked()->vf16(a1, a2); }
uint32_t cGZPersistResourceManager::vf17(uint32_t list, uint32_t type) { return Hooked()->vf17(list, type); }
uint32_t cGZPersistResourceManager::vf18(uint32_t list, uint32_t filter) { return Hooked()->vf18(list, filter); }
uint32_t cGZPersistResourceManager::vf19(uint32_t list, uint32_t type) { return Hooked()->vf19(list, type); }
uint8_t cGZPersistResourceManager::vf20(uint32_t factoryId, uint32_t type, void *factory) { return Hooked()->vf20(factoryId, type, factory); }
uint8_t cGZPersistResourceManager::vf21(uint32_t factoryId) { return Hooked()->vf21(factoryId); }
uint8_t cGZPersistResourceManager::vf22(uint32_t type, void **out) { return Hooked()->vf22(type, out); }
uint32_t cGZPersistResourceManager::vf23(const uint32_t *key, uint32_t b) { return Hooked()->vf23(key, b); }
uint32_t cGZPersistResourceManager::vf24(void *resource, uint32_t a2) { return Hooked()->vf24(resource, a2); }
uint32_t cGZPersistResourceManager::vf25() { return Hooked()->vf25(); }
uint32_t cGZPersistResourceManager::vf26(uint32_t index) { return Hooked()->vf26(index); }
uint32_t cGZPersistResourceManager::vf27(uint32_t segment) { return Hooked()->vf27(segment); }
uint8_t cGZPersistResourceManager::vf28(void *segment) { return Hooked()->vf28(segment); }
uint8_t cGZPersistResourceManager::vf29(void *segment) { return Hooked()->vf29(segment); }
uint8_t cGZPersistResourceManager::vf30(void *segment) { return Hooked()->vf30(segment); }
uint8_t cGZPersistResourceManager::vf31(uint32_t segment) { return Hooked()->vf31(segment); }
uint8_t cGZPersistResourceManager::vf32() { return Hooked()->vf32(); }
uint8_t cGZPersistResourceManager::vf33(uint32_t key, void **out) { return Hooked()->vf33(key, out); }
uint8_t cGZPersistResourceManager::vf34(uint32_t key, void **out) { return Hooked()->vf34(key, out); }
uint32_t cGZPersistResourceManager::vf35() { return Hooked()->vf35(); }
uint32_t cGZPersistResourceManager::vf36(uint32_t index) { return Hooked()->vf36(index); }
uint8_t cGZPersistResourceManager::vf37(uint32_t predicate, uint32_t a2) { return Hooked()->vf37(predicate, a2); }
uint32_t cGZPersistResourceManager::vf38(void **out, uint32_t *count) { return Hooked()->vf38(out, count); }
uint8_t cGZPersistResourceManager::vf39(uint32_t key, uint32_t record, uint32_t mode) { return Hooked()->vf39(key, record, mode); }
uint8_t cGZPersistResourceManager::vf40(uint32_t a1, void *const *holder) { return Hooked()->vf40(a1, holder); }
uint8_t cGZPersistResourceManager::vf41(void *cache) { return Hooked()->vf41(cache); }
uint8_t cGZPersistResourceManager::vf42(void *cache) { return Hooked()->vf42(cache); }
uint8_t cGZPersistResourceManager::vf43() { return Hooked()->vf43(); }
void cGZPersistResourceManager::vf44(uint32_t a1) { Hooked()->vf44(a1); }
uint8_t cGZPersistResourceManager::vf45() { return Hooked()->vf45(); }
void cGZPersistResourceManager::vf46(void *segment) { Hooked()->vf46(segment); }
uint32_t cGZPersistResourceManager::vf47() { return Hooked()->vf47(); }
uint32_t cGZPersistResourceManager::vf48() { return Hooked()->vf48(); }
uint8_t cGZPersistResourceManager::vf49(uint32_t a1, uint32_t a2, uint32_t a3) { return Hooked()->vf49(a1, a2, a3); }
uint8_t cGZPersistResourceManager::vf50(uint32_t a1, uint32_t a2) { return Hooked()->vf50(a1, a2); }
uint8_t cGZPersistResourceManager::vf51(const uint32_t *key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { return Hooked()->vf51(key, a2, a3, a4, a5); }
uint8_t cGZPersistResourceManager::vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) { return Hooked()->vf52(a1, a2, a3, a4, a5, a6); }
uint8_t cGZPersistResourceManager::vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) { return Hooked()->vf53(a1, a2, a3, a4, a5, a6, a7); }

bool cGZPersistResourceManager::Shutdown()
{
    // The struct's Shutdown takes the +4 face as `this`, as the slot does.
    return reinterpret_cast<::cGZPersistResourceManager *>(static_cast<cIGZSystemService *>(this))->Shutdown() != 0;
}

bool cGZPersistResourceManager::OnIdle(uint32_t counter)
{
    // The struct's OnIdle takes the +4 face as `this`, as the slot does.
    return reinterpret_cast<::cGZPersistResourceManager *>(static_cast<cIGZSystemService *>(this))->OnIdle(counter) != 0;
}

cGZPersistResourceManager *__fastcall cGZPersistResourceManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZPersistResourceManager;
}

void __fastcall cGZPersistResourceManagerAdapters::Destruct(cGZPersistResourceManager *self, void *)
{
    self->cGZPersistResourceManager::~cGZPersistResourceManager();
}

} // namespace gz
