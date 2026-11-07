#include "cas/cTSSimOutfitData.h"

#include "game/original.h"
#include "game/vtable.h"
#include "misc/UtilitiesE.h"
#include "rz/ServiceGetters.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>

namespace nTSCASCore {

namespace {

// Typed field access by byte offset. The fields are not all aligned, so they are copied rather than dereferenced.
template <typename T> T Load(const void *object, intptr_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof(value));
    return value;
}

template <typename T> void Store(void *object, intptr_t offset, T value)
{
    std::memcpy(static_cast<uint8_t *>(object) + offset, &value, sizeof(value));
}

// Copies 16 bytes like the original's movups load-then-store: the source is read completely before the
// destination is written, so a source overlapping the destination is copied the same way.
void CopyBlock16(void *object, intptr_t offset, const void *src)
{
    uint8_t block[16];
    std::memcpy(block, src, sizeof(block));
    std::memcpy(static_cast<uint8_t *>(object) + offset, block, sizeof(block));
}

constexpr intptr_t kTable154 = 0x154; // 14 dwords, indexed by bit number
constexpr uint32_t kAllBits = 0x3fff; // the mask a zero argument selects (bits 0..13)
constexpr uint32_t kDefaultValue = 0x80; // the value stored when the argument is -1

} // namespace

// Body: mov eax,[esp+4]; movups xmm0,[eax]; movups [ecx+0x6c],xmm0; ret 4.
void cTSSimOutfitData::StoreBlock6C(const void *src)
{
    CopyBlock16(this, 0x6c, src);
}

// Body: mov eax,[esp+4]; movups xmm0,[eax]; movups [ecx+0x7c],xmm0; ret 4.
void cTSSimOutfitData::StoreBlock7C(const void *src)
{
    CopyBlock16(this, 0x7c, src);
}

// Body: mov eax,[esp+4]; movups xmm0,[eax]; movups [ecx+0x8c],xmm0; ret 4.
void cTSSimOutfitData::StoreBlock8C(const void *src)
{
    CopyBlock16(this, 0x8c, src);
}

// Body: mov al,[esp+4]; mov [ecx+0x14c],al; ret 4.
void cTSSimOutfitData::StoreByte14C(uint8_t value)
{
    Store<uint8_t>(this, 0x14c, value);
}

// Body: mov eax,[esp+4]; mov [ecx+0x134],eax; ret 4.
void cTSSimOutfitData::StoreDword134(uint32_t value)
{
    Store<uint32_t>(this, 0x134, value);
}

// Body: movss xmm0,[esp+4]; movss [ecx+0x58],xmm0; ret 4. The argument is a float, but movss only copies its four
// bytes, so it is taken here as its bits: a signalling NaN keeps its bit pattern, which passing it as a C++ float
// through the x87 stack would not.
void cTSSimOutfitData::StoreFloatBits58(uint32_t bits)
{
    Store<uint32_t>(this, 0x58, bits);
}

// Body: lea eax,[ecx+0x138]; ret.
void *cTSSimOutfitData::Address138()
{
    return reinterpret_cast<uint8_t *>(this) + 0x138;
}

// Body: lea eax,[ecx+0xe4]; ret.
void *cTSSimOutfitData::Address0E4()
{
    return reinterpret_cast<uint8_t *>(this) + 0xe4;
}

// Body: eax = [ecx+0x50]; if eax == 0 then eax = 1; ret. The full eax is the result.
uint32_t cTSSimOutfitData::Dword50OrOne()
{
    const uint32_t value = Load<uint32_t>(this, 0x50);
    return value != 0 ? value : 1;
}

// Body: mov edx,[esp+4]; mov eax,0x20; or edx,edx; je +; bsf eax,edx; cmp eax,0xe; jae zero; eax = [ecx+eax*4+0x154];
// ret 4; zero: xor eax,eax; ret 4. So the result is the table entry for the lowest set bit of the mask (32 for an
// empty mask, which is out of range), or 0 when that bit is 14 or above.
uint32_t cTSSimOutfitData::SlotForLowestBit(uint32_t mask)
{
    uint32_t bit = 0x20;
    if (mask != 0) {
        for (bit = 0; ((mask >> bit) & 1) == 0; ++bit) {
        }
    }
    if (bit >= 0xe) return 0;
    return Load<uint32_t>(this, kTable154 + bit * 4);
}

// Body: mov ecx,[ecx+0x150]; and ecx,[esp+4]; cmp ecx,[esp+4]; sete al; ret 4. Only al is defined.
bool cTSSimOutfitData::FlagsContain(uint32_t mask)
{
    const uint32_t flags = Load<uint32_t>(this, 0x150);
    return (flags & mask) == mask;
}

// Body: mask = bits, or 0x3fff when bits is 0; eax = [ecx+0x150]; if (set's low byte is nonzero) [ecx+0x150] = eax | mask,
// else [ecx+0x150] = eax & ~mask; ret 8. Only the low byte of `set` is tested (cmp byte ptr [esp+8],0).
void cTSSimOutfitData::SetMaskBits150(uint32_t bits, uint32_t set)
{
    const uint32_t mask = bits != 0 ? bits : kAllBits;
    const uint32_t flags = Load<uint32_t>(this, 0x150);
    const bool on = (set & 0xff) != 0;
    Store<uint32_t>(this, 0x150, on ? (flags | mask) : (flags & ~mask));
}

// Body: esi = mask, or 0x3fff when it is 0; edi = value, or 0x80 when it is -1; eax = 1; for 14 bits: when bit i of esi
// is set, [ecx+0x154+4i] = edi; ret 8.
void cTSSimOutfitData::FillSlots154(uint32_t mask, uint32_t value)
{
    const uint32_t bits = mask != 0 ? mask : kAllBits;
    const uint32_t stored = value != 0xffffffffu ? value : kDefaultValue;
    for (int i = 0; i < 14; ++i) {
        if ((bits >> i) & 1) Store<uint32_t>(this, kTable154 + i * 4, stored);
    }
}

// Body: if (arg == [ecx+0x50]) return; [ecx+0x50] = arg; call [[ecx]+0xd4] (slot 53) with ecx = this; ret 4.
void cTSSimOutfitData::SetDword50Notify(uint32_t value)
{
    if (value == Load<uint32_t>(this, 0x50)) return;
    Store<uint32_t>(this, 0x50, value);
    VirtualCall<void(__thiscall *)(void *), 53>(this);
}

// Body: call [[ecx]+0x40] (slot 16) with ecx = this; test ax,ax; sete al; ret. Only the low word of the slot's
// result is tested.
bool cTSSimOutfitData::LowWordIsZero()
{
    const uint32_t result = VirtualCall<uint32_t(__thiscall *)(void *), 16>(this);
    return static_cast<uint16_t>(result) == 0;
}

namespace {

constexpr uint32_t kPartResourceType = 0xebcf3e20; // the resources listed in the part map
constexpr uint32_t kPreloadPriority = 60000;
constexpr intptr_t kPartMap = 0x114;

// The node holding `key` in the part map, or null: a lower bound walk, then an exact-key check.
OutfitPartNode *FindPart(void *object, uint32_t key)
{
    OutfitPartNode *head = Load<OutfitPartNode *>(object, kPartMap);
    OutfitPartNode *bound = head;
    for (OutfitPartNode *node = head->mParent; !node->mIsNil;) {
        if (node->mKey < key) {
            node = node->mRight;
        } else {
            bound = node;
            node = node->mLeft;
        }
    }
    if (bound->mIsNil || key < bound->mKey || bound == head) return nullptr;
    return bound;
}

// The resource manager's interface as these calls use it.
bool GetResource(void *manager, const void *key, uint32_t type, void **out)
{
    using Fn = bool(__thiscall *)(void *, const void *, uint32_t, void **, uint32_t, uint32_t);
    return VirtualCall<Fn, 5>(manager, key, type, out, 0, 0);
}

void Preload(void *manager, const void *key)
{
    using Fn = void(__thiscall *)(void *, const void *, uint32_t, uint32_t, uint32_t, uint32_t);
    VirtualCall<Fn, 6>(manager, key, 0, kPreloadPriority, 0, 1);
}

void ReleaseObject(void *object)
{
    if (object) VirtualCall<void(__thiscall *)(void *), 2>(object);
}

// Resource and item slots.
void GetItems(void *resource, ServiceBlockVector *items)
{
    VirtualCall<void(__thiscall *)(void *, ServiceBlockVector *), 12>(resource, items);
}

const char *ItemName(void *item)
{
    return VirtualCall<const char *(__thiscall *)(void *), 4>(item);
}

const void *ItemKey(void *item, void *out)
{
    return VirtualCall<const void *(__thiscall *)(void *, void *), 5>(item, out);
}

// Preloads the item of the listed resource whose name matches `item` (the only item when there is one).
void PreloadMatchingItem(void *manager, OutfitPartNode *listed, void *item)
{
    void *resource = nullptr;
    if (GetResource(manager, listed->mFirst, kPartResourceType, &resource)) {
        ServiceBlockVector candidates{};
        GetItems(resource, &candidates);
        void **first = static_cast<void **>(candidates.mFirst);
        void **last = static_cast<void **>(candidates.mLast);
        uint8_t key[16];
        if (((reinterpret_cast<uintptr_t>(last) - reinterpret_cast<uintptr_t>(first)) & ~3u) == 4) {
            Preload(manager, ItemKey(first[0], key));
        } else {
            for (void **it = first; it != last; ++it) {
                const char *wanted = ItemName(item);
                if (_stricmp(ItemName(*it), wanted) == 0) {
                    Preload(manager, ItemKey(*it, key));
                    break;
                }
            }
        }
        candidates.FreeBlock();
    }
    ReleaseObject(resource);
}

} // namespace

// Body: see the header. Unwinding on exceptions (frees the item vectors, releases the resources) is not mirrored.
void cTSSimOutfitData::PreloadParts(uint32_t age, uint32_t part)
{
    void *manager = CachedServiceGetter<0x10fc6bc, 0x056b906e, 0x9af5e924>();
    void *partInfo = CachedServiceGetter<0x10fc6c0, 0x4bcf1bb0, 0x0bcf1b58>();
    if (age == 0xffffffffu) age = 1;

    auto recurse = [&](uint32_t bit) {
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 104>(this, age, bit);
    };
    auto optionalBits = [&]() { return VirtualCall<uint32_t(__thiscall *)(void *), 25>(this); };

    if (part == 0 || part == 0xffffffffu) {
        if ((Load<uint8_t>(this, 0x50) & 0xe) != 0) {
            recurse(2);
            recurse(8);
            recurse(0x20);
            for (uint32_t bit : {0x100u, 0x80u, 0x40u, 0x200u, 0x400u, 0x800u, 0x1000u, 0x2000u}) {
                if (optionalBits() & bit) recurse(bit);
            }
        } else {
            for (uint32_t bit : {1u, 2u, 4u, 0x10u, 8u, 0x20u}) recurse(bit);
        }
        return;
    }

    const uint32_t flags = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 5>(partInfo, part);
    if ((flags & 1) && age == 0x80) age = 8;

    const bool singleBit = part != 0 && (part & (part - 1)) == 0 && part <= 0x2000;
    if (!singleBit) {
        for (uint32_t bit : {1u, 2u, 4u, 0x10u, 8u, 0x20u, 0x40u, 0x80u, 0x100u, 0x200u, 0x400u, 0x800u, 0x1000u, 0x2000u}) {
            if (part & bit) recurse(bit);
        }
        return;
    }

    const uint32_t partLow = part & 0xffff;
    OutfitPartNode *entry = FindPart(this, age << 16 | partLow);
    if (!entry) return;
    GetCachedQueriedObject();

    for (uint8_t(*key)[16] = entry->mFirst; key != entry->mLast; ++key) {
        void *resource = nullptr;
        if (!GetResource(manager, key, kPartResourceType, &resource)) {
            ReleaseObject(resource);
            continue;
        }
        using AgeMaskFn = uint16_t(__thiscall *)(void *);
        if (VirtualCall<AgeMaskFn, 7>(resource) != 0 &&
            (age & VirtualCall<AgeMaskFn, 7>(resource)) == 0) {
            ReleaseObject(resource);
            continue;
        }
        uint8_t resourceKey[16];
        VirtualCall<void(__thiscall *)(void *, void *), 10>(resource, resourceKey);
        Preload(manager, resourceKey);

        ServiceBlockVector items{};
        GetItems(resource, &items);
        GetCachedQueriedObject();
        for (void **item = static_cast<void **>(items.mFirst); item != items.mLast; ++item) {
            OutfitPartNode *listed = FindPart(this, partLow | 0x800000);
            if (!listed && (part == 4 || part == 0x10 || part == 8)) {
                listed = FindPart(this, 0x800004);
                if (!listed) listed = FindPart(this, 0x800010);
                if (!listed) listed = FindPart(this, 0x800008);
            }
            if (listed) PreloadMatchingItem(manager, listed, *item);
            uint8_t itemKey[16];
            ItemKey(*item, itemKey);
            OriginalFunction<void(__cdecl *)(const void *), 0xb9bec0>()(itemKey);
        }
        items.FreeBlock();
        ReleaseObject(resource);
    }
}

} // namespace nTSCASCore
