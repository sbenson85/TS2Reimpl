#include "gz/cGZPersistResourceManager.h"
#include "gz/cGZPersistResourceKeyFilterByType.h"
#include "game/vtable.h"
#include "rz/QueryInterfaces.h"
#include "game/game.h"
#include "game/original.h"
#include "rz/StaticGetters.h"
#include "ifaces/PersistDBRecord.h"
#include "ifaces/PersistDBSegment.h"
#include "ifaces/PersistResource.h"
#include "ifaces/PersistResourceFactory.h"
#include "ifaces/PersistResourceManager.h"

#include <deque>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace {

// Element `i` of the deque at +0x70 (the same blocks the original indexes with shr 2 and and 3).
uint32_t DequeAt(const cGZPersistDeque &deque, uint32_t i)
{
    uint32_t pos = deque.offset + i;
    return deque.map[(pos >> 2) & (deque.mapSize - 1)][pos & 3];
}

using NoArgU32 = uint32_t(__thiscall *)(void *);

// Lock objects (cRZCriticalSection-style): slot 1 Enter, slot 2 Leave, both with `this` in ecx.
using LockCall = uint32_t(__thiscall *)(void *);

void Enter(void *lock)
{
    VirtualCall<LockCall, 1>(lock);
}

uint32_t Leave(void *lock)
{
    return VirtualCall<LockCall, 2>(lock);
}

// Reference counting on the value objects (cIGZUnknown slots 1 and 2).
void AddRefObj(void *obj)
{
    VirtualCall<LockCall, 1>(obj);
}

void ReleaseObj(void *obj)
{
    VirtualCall<LockCall, 2>(obj);
}

void *ToPtr(uint32_t value)
{
    return reinterpret_cast<void *>(static_cast<uintptr_t>(value));
}

// An address passed as a stack argument (the original pushes pointers to its locals).
uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The interface id passed to the GetInterface-style slots (52 and 3) by vf4, vf5, vf52 and vf53.
constexpr uint32_t kInterfaceId = 0x456b8f1d;

// The interface id vf20 asks the framework for when it creates a factory (resource factory).
constexpr uint32_t kFactoryInterfaceId = 0xa56b8f17;

} // namespace

uint32_t cGZPersistResourceManager::vf3(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    // The original pushes the five arguments and a zero, so the zero is the last parameter.
    using Slot4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<Slot4, 4>(this, a1, a2, a3, a4, a5, 0);
}

uint32_t cGZPersistResourceManager::vf9(uint32_t a1, void *obj)
{
    using ObjSlot4 = void(__thiscall *)(void *, uint32_t);
    VirtualCall<ObjSlot4, 4>(obj, a1);
    using SelfSlot8 = uint32_t(__thiscall *)(void *, void *);
    return VirtualCall<SelfSlot8, 8>(this, obj);
}

uint32_t cGZPersistResourceManager::vf23(const uint32_t *key, uint32_t b)
{
    using SelfSlot22 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    return VirtualCall<SelfSlot22, 22>(this, *key, b);
}

uint32_t cGZPersistResourceManager::vf25()
{
    Enter(mLockA);
    uint32_t size = mFactories.size;
    Leave(mLockA);
    return size;
}

uint32_t cGZPersistResourceManager::vf26(uint32_t index)
{
    Enter(mLockA);
    uint32_t value = 0;
    if (index < mFactories.size) {
        cGZPersistListNode *node = mFactories.head->next;
        for (uint32_t i = 0; i < index; ++i) node = node->next;
        value = node->value;
    }
    Leave(mLockA);
    return value;
}

uint32_t cGZPersistResourceManager::vf35()
{
    Enter(mLockB);
    uint32_t value = mDeque.size;
    Leave(mLockB);
    return value;
}

uint32_t cGZPersistResourceManager::vf47()
{
    Enter(mLockB);
    uint32_t value = mField84;
    Leave(mLockB);
    return value;
}

uint32_t cGZPersistResourceManager::vf48()
{
    Enter(mLockB);
    void *obj = reinterpret_cast<void *>(static_cast<uintptr_t>(mField84));
    if (obj) {
        using ObjSlot10 = void(__thiscall *)(void *);
        VirtualCall<ObjSlot10, 10>(obj);
    }
    return Leave(mLockB);
}

uint8_t cGZPersistResourceManager::vf11(uint32_t a1)
{
    Enter(mLockC);
    using Slot10 = uint8_t(__thiscall *)(void *, uint32_t);
    uint8_t result = 0;
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead && result == 0; node = node->next) {
        void *value = node->value;
        if (VirtualCall<Slot10, 10>(value, a1)) result = 1;
    }
    Leave(mLockC);
    return result;
}

uint8_t cGZPersistResourceManager::vf39(uint32_t a1, uint32_t a2, uint32_t a3)
{
    // Slot 34 of this, with (a1, &obj). The original clears obj first; the callee fills it in.
    using FindSlot = uint8_t(__thiscall *)(void *, uint32_t, uint32_t *);
    uint32_t obj = 0;
    if (!VirtualCall<FindSlot, 34>(this, a1, &obj)) return 0;
    void *found = ToPtr(obj);
    // The flag is 3 when a3's low byte is non-zero, otherwise 1.
    uint32_t flag = static_cast<uint8_t>(a3) != 0 ? 3 : 1;
    using Slot22 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    uint8_t result = VirtualCall<Slot22, 22>(found, a1, a2, flag);
    ReleaseObj(found);
    return result;
}

uint8_t cGZPersistResourceManager::vf40(uint32_t a1, void *const *holder)
{
    // Neither `this` nor the first argument is used.
    (void)a1;
    using Slot6 = uint8_t(__thiscall *)(void *);
    void *obj = *holder;
    uint8_t result = VirtualCall<Slot6, 6>(obj);
    // The original reads the holder again for the Release.
    ReleaseObj(*holder);
    return result;
}

uint8_t cGZPersistResourceManager::vf41(void *value)
{
    Enter(mLockC);
    cGZPersistValueNode *sentinel = mValueHead;
    for (cGZPersistValueNode *node = sentinel->next; node != sentinel; node = node->next) {
        if (node->value == value) {
            Leave(mLockC);
            return 0;
        }
    }
    if (value) AddRefObj(value);
    if (mValueSize == 0x15555555u) std::_Xlength_error("list too long"); // ORIGINAL-BUG: the lock stays held on this throw
    cGZPersistValueNode *before = mValueHead->next;
    auto *node = static_cast<cGZPersistValueNode *>(::operator new(sizeof(cGZPersistValueNode)));
    node->value = value;
    if (value) AddRefObj(value);
    ++mValueSize;
    cGZPersistValueNode *prev = before->prev;
    node->next = before;
    node->prev = prev;
    before->prev = node;
    prev->next = node;
    if (value) ReleaseObj(value);
    Leave(mLockC);
    return 1;
}

uint8_t cGZPersistResourceManager::vf43()
{
    Enter(mLockC);
    using Slot15 = uint8_t(__thiscall *)(void *);
    uint8_t result = 0;
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead; node = node->next) {
        void *value = node->value;
        result |= VirtualCall<Slot15, 15>(value);
    }
    Leave(mLockC);
    return result;
}

void cGZPersistResourceManager::vf44(uint32_t a1)
{
    Enter(mLockC);
    using Slot16 = void(__thiscall *)(void *, uint32_t);
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead; node = node->next) {
        void *value = node->value;
        VirtualCall<Slot16, 16>(value, a1);
    }
    Leave(mLockC);
}

uint8_t cGZPersistResourceManager::vf45()
{
    Enter(mLockC);
    using Slot17 = uint8_t(__thiscall *)(void *);
    uint8_t result = 0;
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead; node = node->next) {
        void *value = node->value;
        result |= VirtualCall<Slot17, 17>(value);
    }
    Leave(mLockC);
    return result;
}

void cGZPersistResourceManager::vf46(void *obj)
{
    Enter(mLockB);
    void *old = ToPtr(mField84);
    if (obj != old) {
        if (obj) AddRefObj(obj);
        mField84 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj));
        if (old) ReleaseObj(old);
    }
    Leave(mLockB);
}

uint8_t cGZPersistResourceManager::vf50(uint32_t a1, uint32_t a2)
{
    Enter(mLockC);
    using Slot11 = uint8_t(__thiscall *)(void *, uint32_t);
    using Slot8 = void(__thiscall *)(void *, uint32_t, uint32_t);
    uint8_t result = 0;
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead && result == 0; node = node->next) {
        void *value = node->value;
        if (VirtualCall<Slot11, 11>(value, a1)) {
            VirtualCall<Slot8, 8>(value, a1, a2);
            result = 1;
        }
    }
    Leave(mLockC);
    return result;
}

uint8_t cGZPersistResourceManager::vf51(const uint32_t *key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    // Slot 23 of this, with (key, &obj): the factory for the key's type (slot 22). The original clears obj first.
    using FindSlot = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t *);
    uint32_t obj = 0;
    if (!VirtualCall<FindSlot, 23>(this, key, &obj)) return 0;
    void *found = ToPtr(obj); // receiver: cIGZPersistResourceFactory
    using Slot4 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    uint8_t result = VirtualCall<Slot4, 4>(found, *key, a2, a3, a4, a5); // the factory's slot 4 (0x9b61b0, ret 0x14)
    ReleaseObj(found);
    return result;
}

uint8_t cGZPersistResourceManager::OnIdle(uint32_t a1)
{
    // `this` is the cRZSystemService subobject at +4 of the full object.
    auto *self = reinterpret_cast<cGZPersistResourceManager *>(reinterpret_cast<uint8_t *>(this) - 4);
    Enter(self->mLockC);
    using Slot14 = uint8_t(__thiscall *)(void *, uint32_t);
    // The original starts from the low byte of its argument and ORs in every result.
    uint8_t result = static_cast<uint8_t>(a1);
    for (cGZPersistValueNode *node = self->mValueHead->next; node != self->mValueHead; node = node->next) {
        void *value = node->value;
        result |= VirtualCall<Slot14, 14>(value, a1);
    }
    Leave(self->mLockC);
    return result;
}

uint8_t cGZPersistResourceManager::Shutdown()
{
    // `this` is the cRZSystemService subobject at +4 of the full object.
    auto *self = reinterpret_cast<cGZPersistResourceManager *>(reinterpret_cast<uint8_t *>(this) - 4);
    using VoidCall = void(__thiscall *)(void *);

    // The cache strategies: each one's slot 4, then the list emptied, each strategy released with its node.
    Enter(self->mLockC);
    for (cGZPersistValueNode *node = self->mValueHead->next; node != self->mValueHead; node = node->next)
        VirtualCall<VoidCall, 4>(node->value);
    cGZPersistValueNode *head = self->mValueHead;
    head->prev->next = nullptr;
    for (cGZPersistValueNode *node = head->next; node != nullptr;) {
        cGZPersistValueNode *next = node->next;
        if (node->value) ReleaseObj(node->value);
        ::operator delete(node, sizeof(cGZPersistValueNode));
        node = next;
    }
    head->next = head;
    head->prev = head;
    self->mValueSize = 0;
    Leave(self->mLockC);

    // The factories: each released, then the factory map emptied (the type map keeps its entries).
    Enter(self->mLockA);
    for (cGZPersistListNode *node = self->mFactories.head->next; node != self->mFactories.head; node = node->next)
        if (void *factory = ToPtr(node->value)) ReleaseObj(factory);
    self->FactoryMap().clear();
    Leave(self->mLockA);

    // The segments: each one's slots 9 and 4, then released; the deque emptied; then the default segment.
    Enter(self->mLockB);
    const uint32_t count = self->mDeque.size;
    for (uint32_t i = 0; i < count; ++i) {
        void *segment = ToPtr(DequeAt(self->mDeque, i));
        if (segment == nullptr) continue;
        VirtualCall<VoidCall, 9>(segment);
        VirtualCall<VoidCall, 4>(segment);
        ReleaseObj(segment);
    }
    self->Segments().clear();
    if (self->mField84 != 0) {
        // The original reads +0x84 again for every call.
        VirtualCall<VoidCall, 10>(ToPtr(self->mField84));
        VirtualCall<VoidCall, 9>(ToPtr(self->mField84));
        VirtualCall<VoidCall, 4>(ToPtr(self->mField84));
        if (void *segment = ToPtr(self->mField84)) {
            self->mField84 = 0;
            ReleaseObj(segment);
        }
    }
    Leave(self->mLockB);
    return 1;
}

uint8_t cGZPersistResourceManager::QueryInterface(uint32_t riid, void **ppv)
{
    // Like the other QueryInterface functions here, ppv is not checked: riid 0x656b8efc stores
    // through it, so a null ppv faults in the original too. Callers must pass one.
    if (riid == 0x656b8efc) {
        *ppv = this;
        VirtualCall<NoArgU32, 1>(this); // AddRef through this's own vtable
        return 1;
    }
    // Everything else is the cRZSystemService QueryInterface of the subobject at +4 (a tail jump in
    // the original; the verified replacement for it is called here).
    auto *service = reinterpret_cast<uint8_t *>(this) + 4;
    return qi::SystemServiceQueryInterface(service, nullptr, riid, ppv) ? 1 : 0;
}

uint8_t cGZPersistResourceManager::vf10(uint32_t a1)
{
    Enter(mLockC);
    using Slot10 = uint8_t(__thiscall *)(void *, uint32_t);
    using Slot9 = uint8_t(__thiscall *)(void *, uint32_t);
    uint8_t result = 0;
    // The loop only goes on while the result is 0, so a value whose slot 9 returns 0 leaves it to
    // the next value.
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead && result == 0; node = node->next) {
        void *value = node->value;
        if (VirtualCall<Slot10, 10>(value, a1)) result = VirtualCall<Slot9, 9>(value, a1);
    }
    Leave(mLockC);
    return result;
}

uint8_t cGZPersistResourceManager::vf12(void *const *range, uint32_t a2)
{
    // `range` is {begin, end} of 16-byte items. The original takes (end - begin) >> 4 with an
    // arithmetic shift, so end must not be below begin.
    uint32_t begin = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(range[0]));
    uint32_t end = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(range[1]));
    using Slot13 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    // The original counts (end - begin) >> 4 items, and calls slot 13 for every one of them.
    uint32_t count = (end - begin) >> 4;
    uint8_t all = 1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t item = begin + 16 * i;
        uint8_t r = VirtualCall<Slot13, 13>(this, item, a2);
        if (r == 0) all = 0;
    }
    return all;
}

uint8_t cGZPersistResourceManager::vf15(uint32_t a1)
{
    Enter(mLockB);
    // The original reaches the deque through the proxy at +0x70 (its first dword is the deque);
    // this reads the deque's fields directly, which is the same memory.
    using Slot20 = uint8_t(__thiscall *)(void *, uint32_t);
    uint8_t result = 0;
    for (uint32_t i = 0; i < mDeque.size && result == 0; ++i) {
        void *element = ToPtr(DequeAt(mDeque, i));
        if (VirtualCall<Slot20, 20>(element, a1)) result = 1;
    }
    Leave(mLockB);
    return result;
}

uint32_t cGZPersistResourceManager::vf16(uint32_t a1, uint32_t a2)
{
    Enter(mLockC);
    using Slot12 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    uint32_t sum = 0;
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead; node = node->next) {
        void *value = node->value;
        sum += VirtualCall<Slot12, 12>(value, a1, a2);
    }
    Leave(mLockC);
    return sum;
}

uint32_t cGZPersistResourceManager::vf18(uint32_t a1, uint32_t a2)
{
    Enter(mLockB);
    using Slot19 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < mDeque.size; ++i) {
        void *element = ToPtr(DequeAt(mDeque, i));
        sum += VirtualCall<Slot19, 19>(element, a1, a2);
    }
    Leave(mLockB);
    return sum;
}

uint32_t cGZPersistResourceManager::vf24(void *obj, uint32_t a2)
{
    // obj->slot5(), then this's slot 22 with that result and a2.
    using Slot5 = uint32_t(__thiscall *)(void *);
    uint32_t r = VirtualCall<Slot5, 5>(obj);
    using Slot22 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    return VirtualCall<Slot22, 22>(this, r, a2);
}

uint8_t cGZPersistResourceManager::vf31(uint32_t a1)
{
    Enter(mLockB);
    uint8_t found = 0;
    for (uint32_t i = 0; i < mDeque.size && found == 0; ++i) {
        if (DequeAt(mDeque, i) == a1) found = 1;
    }
    Leave(mLockB);
    return found;
}

uint8_t cGZPersistResourceManager::vf32()
{
    Enter(mLockB);
    using Slot10 = uint8_t(__thiscall *)(void *);
    // Every element is called (no early stop); the flag only accumulates.
    uint8_t all = 1;
    for (uint32_t i = 0; i < mDeque.size; ++i) {
        void *element = ToPtr(DequeAt(mDeque, i));
        if (VirtualCall<Slot10, 10>(element) == 0) all = 0;
    }
    Leave(mLockB);
    return all;
}

uint8_t cGZPersistResourceManager::vf33(uint32_t key, void **out)
{
    Enter(mLockB);
    using Slot15 = uint32_t(__thiscall *)(void *);
    *out = nullptr;
    uint8_t found = 0;
    for (uint32_t i = 0; i < mDeque.size; ++i) {
        void *element = ToPtr(DequeAt(mDeque, i));
        if (VirtualCall<Slot15, 15>(element) == key) {
            *out = element;
            AddRefObj(element);
            found = 1;
            break;
        }
    }
    Leave(mLockB);
    return found;
}

uint32_t cGZPersistResourceManager::vf36(uint32_t index)
{
    Enter(mLockB);
    uint32_t value = 0;
    if (index < mDeque.size) value = DequeAt(mDeque, index);
    Leave(mLockB);
    return value;
}

uint8_t cGZPersistResourceManager::vf4(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    using Slot49 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    if (VirtualCall<Slot49, 49>(this, a1, a2, a3)) return 1;

    using Slot52 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t *, uint32_t, uint32_t, uint32_t);
    uint32_t obj = 0;
    uint8_t result = 0;
    if (VirtualCall<Slot52, 52>(this, a1, kInterfaceId, &obj, a4, a5, a6)) {
        using Slot50 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
        VirtualCall<Slot50, 50>(this, a1, obj);
        using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
        void *found = ToPtr(obj);
        result = VirtualCall<QueryFn, 0>(found, a2, a3);
    }
    // The original releases the object on both paths, but only when it is non-null.
    if (obj) ReleaseObj(ToPtr(obj));
    return result;
}

uint8_t cGZPersistResourceManager::vf5(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    using Slot52 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t *, uint32_t, uint32_t, uint32_t);
    uint32_t obj = 0;
    if (!VirtualCall<Slot52, 52>(this, a1, kInterfaceId, &obj, a4, a5, 0)) return 0;
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    void *found = ToPtr(obj);
    uint8_t result = VirtualCall<QueryFn, 0>(found, a2, a3);
    ReleaseObj(found);
    return result;
}

uint8_t cGZPersistResourceManager::vf34(uint32_t a1, void **out)
{
    Enter(mLockB);
    *out = nullptr;
    using Slot20 = uint8_t(__thiscall *)(void *, uint32_t);
    uint8_t found = 0;
    for (uint32_t i = 0; i < mDeque.size; ++i) {
        void *element = ToPtr(DequeAt(mDeque, i));
        if (VirtualCall<Slot20, 20>(element, a1)) {
            *out = element;
            AddRefObj(element);
            found = 1;
            break;
        }
    }
    Leave(mLockB);
    return found;
}

uint32_t cGZPersistResourceManager::vf38(void **arr, uint32_t *count)
{
    // No lock here. The original returns the deque size in every case.
    uint32_t size = mDeque.size;
    if (!arr) return size;
    uint32_t wanted = *count;
    if (wanted == 0) return size;
    *count = 0;
    // Unsigned minimum of the deque size and the wanted count.
    uint32_t limit = size < wanted ? size : wanted;
    uint32_t copied = 0;
    while (copied < limit) {
        void *element = ToPtr(DequeAt(mDeque, copied));
        arr[copied] = element;
        AddRefObj(element);
        ++copied;
        *count = copied;
    }
    return size;
}

uint8_t cGZPersistResourceManager::vf49(uint32_t a1, uint32_t a2, uint32_t a3)
{
    Enter(mLockC);
    using Slot7 = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    void *out = nullptr;
    uint8_t result = 0;
    for (cGZPersistValueNode *node = mValueHead->next; node != mValueHead;) {
        void *value = node->value;
        if (value) AddRefObj(value);
        if (out) {
            void *previous = out;
            out = nullptr;
            ReleaseObj(previous);
        }
        if (VirtualCall<Slot7, 7>(value, a1, &out)) {
            result = VirtualCall<QueryFn, 0>(out, a2, a3);
            if (value) ReleaseObj(value);
            break;
        }
        // The original reads the next node before releasing the value.
        cGZPersistValueNode *next = node->next;
        if (value) ReleaseObj(value);
        node = next;
    }
    Leave(mLockC);
    if (out) ReleaseObj(out);
    return result;
}

uint8_t cGZPersistResourceManager::vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    using Slot23 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t *);
    uint32_t obj1 = 0;
    if (!VirtualCall<Slot23, 23>(this, a1, &obj1)) return 0;

    uint32_t obj = a6;
    bool owned = false;
    bool ok = true;
    if (a6 == 0) {
        using Slot34 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t *);
        obj = 0;
        ok = VirtualCall<Slot34, 34>(this, a1, &obj) != 0;
        owned = ok;
    }
    uint8_t result = 0;
    if (ok) {
        using Slot53 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        result = VirtualCall<Slot53, 53>(this, a1, obj1, obj, a2, a3, a4, a5) != 0;
        if (owned) ReleaseObj(ToPtr(obj));
    }
    ReleaseObj(ToPtr(obj1));
    return result;
}

uint8_t cGZPersistResourceManager::vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    // `this` is not used: the original works on its arguments only.
    using Slot22 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t *, uint32_t);
    using Slot25 = uint8_t(__thiscall *)(void *, uint32_t);
    uint32_t key = 0;
    if (!VirtualCall<Slot22, 22>(a3, a1, &key, 1)) {
        if (key) {
            VirtualCall<Slot25, 25>(a3, key);
            ReleaseObj(ToPtr(key));
        }
        return 0;
    }

    using Slot3 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t *, uint32_t, uint32_t);
    uint32_t out = 0;
    if (!VirtualCall<Slot3, 3>(a2, key, kInterfaceId, &out, a6, a7)) {
        VirtualCall<Slot25, 25>(a3, key);
        if (key) ReleaseObj(ToPtr(key));
        return 0;
    }

    using Slot4 = void(__thiscall *)(void *, uint32_t);
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    using Slot24 = uint8_t(__thiscall *)(void *, uint32_t);
    void *found = ToPtr(out);
    VirtualCall<Slot4, 4>(found, a1);
    uint8_t result = VirtualCall<QueryFn, 0>(found, a4, a5);
    VirtualCall<Slot24, 24>(a3, key);
    ReleaseObj(found);
    if (key) ReleaseObj(ToPtr(key));
    return result;
}

uint8_t cGZPersistResourceManager::vf6(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    Enter(mLockC);
    // Lock D is entered twice: the serial is taken and bumped under the first hold, the field is
    // read under the second (the original keeps it in a local that this path never passes on).
    Enter(mLockD);
    uint32_t serial = mSerial;
    mSerial = serial + 1;
    Leave(mLockD);
    Enter(mLockD);
    uint32_t field = mField;
    Leave(mLockD);

    // A 16-byte resource key {a1, field, old serial, 0}; its address goes to slot 51 (which fills obj),
    // to the found object's slot 4 (which writes the key back) and to slot 50.
    uint32_t key[4] = {a1, field, serial, 0};
    uint32_t obj = 0;
    uint8_t result = 0;
    using Slot51 = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
    if (VirtualCall<Slot51, 51>(this, key, kInterfaceId, Word(&obj), a4, a5)) {
        void *found = ToPtr(obj);
        using Slot4 = void(__thiscall *)(void *, uint32_t);
        VirtualCall<Slot4, 4>(found, Word(key));
        using Slot0 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
        if (VirtualCall<Slot0, 0>(found, a2, a3)) {
            using Slot50 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
            VirtualCall<Slot50, 50>(this, Word(key), obj);
            result = 1;
        }
        ReleaseObj(found);
    }
    Leave(mLockC);
    return result;
}

uint8_t cGZPersistResourceManager::vf8(void *obj)
{
    if (!obj) return 0;
    Enter(mLockC);
    // A 16-byte resource key local, filled by obj's slot 3 (the key getter); its address goes to
    // slots 11 and 50.
    uint32_t key[4] = {};
    using Slot3 = void(__thiscall *)(void *, uint32_t);
    VirtualCall<Slot3, 3>(obj, Word(key));
    using Slot11 = uint8_t(__thiscall *)(void *, uint32_t);
    uint8_t result = VirtualCall<Slot11, 11>(this, Word(key));
    if (!result) {
        using Slot50 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
        result = VirtualCall<Slot50, 50>(this, Word(key), Word(obj));
    }
    Leave(mLockC);
    return result;
}

uint32_t cGZPersistResourceManager::vf27(uint32_t a1)
{
    // A tail jump in the original: slot 28 gets the same argument and its result is ours.
    using Slot28 = uint32_t(__thiscall *)(void *, uint32_t);
    return VirtualCall<Slot28, 28>(this, a1);
}

uint8_t cGZPersistResourceManager::vf37(uint32_t pred, uint32_t a2)
{
    // A null predicate or an empty deque leaves eax as the caller had it (undefined): 0 here.
    if (pred == 0 || mDeque.size == 0) return 0;
    using Pred = uint8_t(__cdecl *)(uint32_t, uint32_t);
    auto fn = reinterpret_cast<Pred>(static_cast<uintptr_t>(pred));
    uint8_t r = 0;
    for (uint32_t i = 0; i < mDeque.size; ++i) {
        r = fn(DequeAt(mDeque, i), a2);
        if (r == 0) break;
    }
    return r;
}

uint8_t cGZPersistResourceManager::vf42(void *value)
{
    Enter(mLockC);
    uint8_t result = 0;
    cGZPersistValueNode *sentinel = mValueHead;
    for (cGZPersistValueNode *node = sentinel->next; node != sentinel; node = node->next) {
        if (node->value == value) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
            --mValueSize;
            if (node->value) ReleaseObj(node->value);
            ::operator delete(node, sizeof(cGZPersistValueNode));
            result = 1;
            break;
        }
    }
    Leave(mLockC);
    return result;
}

// The key filter vf17 and vf19 hand to slots 16 and 18: gz/cGZPersistResourceKeyFilterByType.h.

uint32_t cGZPersistResourceManager::vf17(uint32_t list, uint32_t type)
{
    auto *filter = new cGZPersistResourceKeyFilterByType(type);
    AddRefObj(filter);
    uint32_t count = As<ifaces::PersistResourceManager>(this)->GetResourceKeyList(list, filter);
    ReleaseObj(filter);
    return count;
}

uint32_t cGZPersistResourceManager::vf19(uint32_t list, uint32_t type)
{
    auto *filter = new cGZPersistResourceKeyFilterByType(type);
    AddRefObj(filter);
    uint32_t count = As<ifaces::PersistResourceManager>(this)->GetResourceKeyList2(list, filter);
    ReleaseObj(filter);
    return count;
}

bool cGZPersistResourceManager::vf13(const void *key, uint32_t a2)
{
    void *resource = nullptr;
    bool result = false;
    auto *self = As<ifaces::PersistResourceManager>(this);
    if (self->GetResource(key, kInterfaceId, &resource, 0, 0)) {
        result = self->SaveResourceTo(resource, Word(key), ToPtr(a2));
        ReleaseObj(resource);
    }
    return result;
}

bool cGZPersistResourceManager::vf7(const void *key, uint32_t iid, void **out, uint32_t a4, uint32_t a5)
{
    Enter(mLockC);
    void *created = nullptr;
    bool result = false;
    auto *self = As<ifaces::PersistResourceManager>(this);
    if (self->CreateResource(key, kInterfaceId, &created, a4, a5)) {
        As<ifaces::PersistResource>(created)->SetKey(key);
        if (As<ifaces::PersistResource>(created)->QueryInterface(iid, out)) {
            result = true;
            self->RegisterResource(key, created);
        }
        As<ifaces::PersistResource>(created)->Release();
        created = nullptr;
    }
    Leave(mLockC);
    return result;
}

cGZPersistHashMap::Iterator cGZPersistHashMap::Find(const uint32_t &key) const
{
    return FindInline(key);
}

bool cGZPersistResourceManager::vf22(uint32_t type, void **out)
{
    Enter(mLockA);
    bool found = false;
    cGZPersistHashMap::Iterator typeEntry = mTypes.FindInline(type);
    if (typeEntry.node != mTypes.head) {
        uint32_t id = typeEntry.node->value;
        cGZPersistHashMap::Iterator factory = mFactories.Find(id);
        if (factory.node != mFactories.head) {
            *out = ToPtr(factory.node->value);
            AddRefObj(*out);
            found = true;
        }
    }
    Leave(mLockA);
    return found;
}

// The deque at +0x70 is the toolchain's std::deque<uint32_t> (the same layout: proxy, map, map size,
// offset, size); the members that push to it use the library's push_front/push_back.
static_assert(sizeof(std::deque<void *>) == sizeof(cGZPersistDeque), "deque layout");

bool cGZPersistResourceManager::vf28(void *segment)
{
    void *lock = mLockB;
    Enter(lock);
    AddRefObj(segment);
    As<ifaces::PersistResourceManager>(this)->RemoveSegment(segment);
    Segments().push_front(segment);
    Leave(lock);
    return true;
}

bool cGZPersistResourceManager::vf29(void *segment)
{
    Enter(mLockB);
    AddRefObj(segment);
    As<ifaces::PersistResourceManager>(this)->RemoveSegment(segment);
    Segments().push_back(segment);
    Leave(mLockB);
    return true;
}

bool cGZPersistResourceManager::vf30(void *segment)
{
    Enter(mLockB);
    bool removed = false;
    std::deque<void *> &segments = Segments();
    for (auto it = segments.begin(); it != segments.end(); ++it) {
        void *element = *it;
        if (element == segment) {
            ReleaseObj(element);
            segments.erase(it, it + 1);
            removed = true;
            break;
        }
    }
    Leave(mLockB);
    return removed;
}

bool cGZPersistResourceManager::vf14(void *resource, const void *key, void *segment)
{
    void *record = nullptr;
    void *factory = nullptr;
    auto *res = As<ifaces::PersistResource>(resource);
    auto *seg = As<ifaces::PersistDBSegment>(segment);
    bool ok;
    if (seg->TestForRecord(res->GetKey())) {
        // An existing record is reopened for writing and emptied.
        if (record) ReleaseObj(record);
        ok = seg->OpenRecord(res->GetKey(), &record, 3);
        if (ok) As<ifaces::PersistDBRecord>(record)->SetSize(0);
    } else {
        if (record) ReleaseObj(record);
        ok = seg->CreateNewRecord(key, &record);
    }
    if (ok) {
        ok = false;
        if (As<ifaces::PersistResourceManager>(this)->FindFactory(resource, &factory)) {
            if (As<ifaces::PersistResourceFactory>(factory)->Write(resource, record) && seg->CloseRecord(record)) {
                if (record) {
                    void *closed = record;
                    record = nullptr;
                    ReleaseObj(closed);
                }
                ok = true;
            }
            ReleaseObj(factory);
        }
    }
    // A record still held here was not written: abort it.
    if (record) {
        seg->AbortRecord(record);
        if (record) ReleaseObj(record);
    }
    return ok;
}

bool cGZPersistResourceManager::vf20(uint32_t factoryId, uint32_t type, void *factory)
{
    bool registered = false;
    if (factory) {
        AddRefObj(factory);
    } else if (void *framework = GetDirectorFrameworkObject()) {
        using CreateInstanceFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<CreateInstanceFn, 3>(framework, factoryId, kFactoryInterfaceId, &factory);
    }
    if (factory) {
        void *lock = mLockA;
        Enter(lock);
        // Both inserts (the game's instantiations 0x9b3cf0 and 0x314ec0) are unordered_multimap<uint32_t, uint32_t>::insert:
        // the library's code on the same layout.
        using Multimap = std::unordered_multimap<uint32_t, uint32_t>;
        static_assert(sizeof(Multimap) == sizeof(cGZPersistHashMap), "multimap layout");
        reinterpret_cast<Multimap *>(&mFactories)->insert({factoryId, Word(factory)});
        reinterpret_cast<Multimap *>(&mTypes)->insert({type, factoryId});
        registered = true;
        Leave(lock);
    }
    return registered;
}

bool cGZPersistResourceManager::vf21(uint32_t factoryId)
{
    void *lock = mLockA;
    Enter(lock);
    bool removed = false;
    cGZPersistHashMap::Iterator it = mFactories.Find(factoryId);
    while (it.node != mFactories.head) {
        removed = true;
        ReleaseObj(ToPtr(it.node->value));
        mFactories.Erase(it.node);
        it = mFactories.FindInline(factoryId);
    }
    // Every type that maps to this factory goes too.
    cGZPersistListNode *end = mTypes.head;
    for (cGZPersistListNode *node = end->next; node != end;) {
        cGZPersistListNode *next = node->next;
        if (node->value == factoryId) mTypes.Erase(node);
        node = next;
    }
    Leave(lock);
    return removed;
}
