// nTSCASCore::cTSCASBodyPartSystem: slots of the cITSCASBodyPartSystem face that copy state in from another object or
// add part records (lane JA). The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>
#include <string>

namespace nTSCASCore {

namespace {

using ServiceString = std::basic_string<char, std::char_traits<char>, CasAlloc<char>>;

const uint32_t *ZeroGuid()
{
    return OriginalFunction<const uint32_t *(__cdecl *)(), 0x4f8e50>()();
}

bool SameGuid(const uint32_t *a, const uint32_t *b)
{
    for (int i = 0; i < 4; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// Getters of the source object CopyFrom reads (its own interface; slot numbers there).
using GetWord = uint32_t(__thiscall *)(void *);
using GetPointer = void *(__thiscall *)(void *);
using GetGuid = const uint32_t *(__thiscall *)(void *);

} // namespace

void BodyPartSystemIface::CopyFrom(void *source, uint8_t resetBlocks, uint8_t copyWord, uint8_t copyKeys, uint8_t copyNamed,
                                   uint8_t copyItems, uint8_t copyNames5)
{
    if (VirtualCall<int32_t(__thiscall *)(void *), 24>(source) == 1) return;
    if (resetBlocks) {
        VirtualCall<void(__thiscall *)(void *), 16>(this);
        VirtualCall<void(__thiscall *)(void *, const void *), 54>(this, ZeroGuid());
        VirtualCall<void(__thiscall *)(void *, const void *), 74>(this, ZeroGuid());
        VirtualCall<void(__thiscall *)(void *, const void *), 56>(this, ZeroGuid());
    }
    if (copyWord) VirtualCall<void(__thiscall *)(void *, uint32_t), 60>(this, VirtualCall<GetWord, 33>(source));
    if (copyKeys) {
        for (uint32_t index = 0; index < 2; ++index) {
            const void *const key = VirtualCall<const void *(__thiscall *)(void *, uint32_t), 32>(source, index);
            VirtualCall<void(__thiscall *)(void *, uint32_t, const void *), 58>(this, index, key);
        }
    }
    if (copyNamed) {
        VirtualCall<void(__thiscall *)(void *, void *), 35>(this, VirtualCall<GetPointer, 38>(source));
        VirtualCall<void(__thiscall *)(void *, void *), 41>(this, VirtualCall<GetPointer, 35>(source));
        VirtualCall<void(__thiscall *)(void *, void *), 43>(this, VirtualCall<GetPointer, 36>(source));
    }
    if (copyNames5) VirtualCall<void(__thiscall *)(void *, void *), 45>(this, VirtualCall<GetPointer, 37>(source));
    if (!copyItems) return;
    VirtualCall<void(__thiscall *)(void *, void *), 33>(this, VirtualCall<GetPointer, 34>(source));
    // The source's first GUID as text (0x4fdd30), applied through slot 38.
    ServiceString text;
    OriginalFunction<void(__thiscall *)(const uint32_t *, ServiceString *), 0x4fdd30>()(VirtualCall<GetGuid, 30>(source), &text);
    VirtualCall<void(__thiscall *)(void *, const char *, uint32_t, uint32_t), 38>(this, text.c_str(), 1, 0);
    // Block D8 gets the second GUID unless it is zero or the same as the first.
    const uint32_t *block;
    if (SameGuid(VirtualCall<GetGuid, 31>(source), ZeroGuid())) {
        block = ZeroGuid();
    } else {
        const uint32_t *const second = VirtualCall<GetGuid, 31>(source);
        const uint32_t *const first = VirtualCall<GetGuid, 30>(source);
        block = SameGuid(first, second) ? ZeroGuid() : VirtualCall<GetGuid, 31>(source);
    }
    VirtualCall<void(__thiscall *)(void *, const void *), 56>(this, block);
}

namespace {

constexpr uint32_t kRvaCRZStringVTable = 0xd9b470;

void AppendEntry(BodyPartEntryVector *stack, const BodyPartEntry &entry)
{
    BodyPartEntry *const last = stack->mLast;
    if (last == stack->mEnd) {
        OriginalFunction<void(__thiscall *)(BodyPartEntryVector *, BodyPartEntry *, const BodyPartEntry *), 0xcda310>()(stack, last, &entry);
    } else {
        OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xcddbc0>()(last, &entry);
        stack->mLast = stack->mLast + 1;
    }
}

} // namespace

void BodyPartSystemIface::ApplyPartGuid(const uint32_t *guid, uint8_t on, uint8_t asNew)
{
    // The skin base filter (system +0x15c) narrowed to this age and GUID, then the skin catalog's matches for it.
    void *const filter = mSkinBaseFilter;
    VirtualCall<void(__thiscall *)(void *), 7>(filter);
    const uint32_t mask = OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0x267350>()(mAge);
    OriginalFunction<void(__thiscall *)(void *, uint32_t), 0xb65750>()(mSkinBaseFilter, mask);
    OriginalFunction<void(__thiscall *)(void *, const uint32_t *), 0xb65550>()(mSkinBaseFilter, guid);
    void *const catalog = CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>();
    ServiceBlockVector matches{};
    uint8_t *const filterFace = mSkinBaseFilter == nullptr ? nullptr : static_cast<uint8_t *>(mSkinBaseFilter) + 0x54;
    VirtualCall<void(__thiscall *)(void *, ServiceBlockVector *, void *, uint32_t, uint32_t), 21>(catalog, &matches, filterFace, 1, 1);
    if (matches.mFirst != matches.mLast) {
        uint32_t category = 0, value = 5, layer = 0;
        if (OriginalFunction<bool(__cdecl *)(uint32_t, uint32_t *, uint32_t *, uint32_t *), 0xcf1c20>()(
                *static_cast<const uint32_t *>(matches.mFirst), &category, &value, &layer)) {
            BodyPartEntry e;
            std::memset(&e, 0, sizeof(e));
            e.mKind = 3;
            e.mShortA = static_cast<int16_t>(category);
            e.mShortB = static_cast<int16_t>(layer);
            e.mValue = static_cast<int32_t>(value);
            e.mWeight = on ? 1.0f : 0.0f;
            auto *const name = reinterpret_cast<cRZString *>(&e.mNameVTable);
            name->Construct();
            name->Sprintf(GameAddress<const char>(0xdce3cc), guid[0], guid[1] >> 16, guid[1] & 0xffff, guid[2] >> 16,
                          guid[2] & 0xffff, guid[3]);
            BodyPartEntry *match = nullptr;
            for (BodyPartEntry *at = mStack->mFirst; at != mStack->mLast; ++at) {
                if (at->mKind == 3 && (mAge == 0 || at->mValue == e.mValue) && !asNew && at->mShortA == e.mShortA &&
                    at->mShortB <= e.mShortB) {
                    match = at;
                    break;
                }
            }
            if (match != nullptr) {
                OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xce01a0>()(match, &e); // assignment
            } else {
                AppendEntry(mStack, e);
            }
            OriginalFunction<void(__thiscall *)(BodyPartEntry *), 0xcdf820>()(&e);
        }
    }
    matches.FreeBlock();
}

} // namespace nTSCASCore
