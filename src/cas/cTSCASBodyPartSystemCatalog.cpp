// nTSCASCore::cTSCASBodyPartSystem (lane KA): slot 40, which asks the resource catalog whether a GUID's item is among
// the enabled parts of the modifier stack. The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "misc/HexParse.h"
#include "rz/ServiceGetters.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>
#include <map>
#include <set>
#include <string>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

// The interfaces a catalog match is asked for by its type (slot 12), and the slot giving its value there.
constexpr uint32_t kIidType4 = 0x2c1fd89d; // type 4: value slot 7
constexpr uint32_t kIidType5 = 0x0c1fe23e; // type 5: value slot 6
constexpr float kEnabledWeight = 0.5f;     // a kind-3 record above it counts as worn

void *Catalog() { return CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>(); }

void SetFilterBlockC8(void *filter, const void *block)
{
    OriginalFunction<void(__thiscall *)(void *, const void *), 0xb65610>()(filter, block);
}

// The catalog's matches for the filter (slot 21 with the filter's face at +0x54); when there are none and the skin
// base's GUID differs from +0xc8, asked again with it in place of +0xc8 (put back after).
void Query(void *catalog, void *filter, ServiceBlockVector *out, const uint8_t *skinBase, const uint8_t *blockC8)
{
    using QueryFn = void(__thiscall *)(void *, ServiceBlockVector *, void *, uint32_t, uint32_t);
    void *const face = filter == nullptr ? nullptr : static_cast<uint8_t *>(filter) + 0x54;
    VirtualCall<QueryFn, 21>(catalog, out, face, 1, 1);
    if (out->mFirst != out->mLast || std::memcmp(skinBase, blockC8, 16) == 0) return;
    SetFilterBlockC8(filter, skinBase);
    VirtualCall<QueryFn, 21>(catalog, out, face, 1, 1);
    SetFilterBlockC8(filter, blockC8);
}

// The first match's value through the interface its type picks; false for other types, a missing interface, or (with
// `guid`) an interface whose GUID (slot 5) differs.
bool FirstMatchValue(const ServiceBlockVector &matches, const void *guid, uint32_t *value)
{
    if (matches.mFirst == matches.mLast) return false;
    void *const match = *static_cast<void *const *>(matches.mFirst);
    const uint32_t type = VirtualCall<uint32_t(__thiscall *)(void *), 12>(match);
    if (type != 4 && type != 5) return false;
    void *face = nullptr;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(match, type == 4 ? kIidType4 : kIidType5, &face);
    if (face == nullptr) return false;
    const bool same = guid == nullptr || std::memcmp(VirtualCall<const void *(__thiscall *)(void *), 5>(face), guid, 16) == 0;
    if (same) {
        *value = type == 4 ? VirtualCall<uint32_t(__thiscall *)(void *), 7>(face) : VirtualCall<uint32_t(__thiscall *)(void *), 6>(face);
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(face);
    return same;
}

// The game's case-insensitive order of names (_stricmp).
struct NoCaseLess {
    bool operator()(const std::string &a, const std::string &b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
};

} // namespace

uint32_t BodyPartSystemIface::IsGuidApplied(uint32_t a, uint32_t b, const char *guidText)
{
    void *const catalog = Catalog();
    uint32_t guid[4];
    hexparse::ParseGuidText(guid, guidText);
    uint8_t skinBase[16];
    std::memcpy(skinBase, mBlockC8, sizeof(skinBase));
    if (void *const base = OriginalFunction<void *(__thiscall *)(void *), 0xceefb0>()(SystemOf(this))) {
        std::memcpy(skinBase, VirtualCall<const void *(__thiscall *)(void *), 5>(base), sizeof(skinBase));
    }

    VirtualCall<void(__thiscall *)(void *), 7>(mOverlayFilter);
    using SetWord = void(__thiscall *)(void *, uint32_t);
    using SetBlock = void(__thiscall *)(void *, const void *);
    using MapWord = uint32_t(__cdecl *)(uint32_t);
    OriginalFunction<SetWord, 0xb654f0>()(mOverlayFilter, OriginalFunction<MapWord, 0xb5fc80>()(a));
    OriginalFunction<SetWord, 0xb655f0>()(mOverlayFilter, OriginalFunction<MapWord, 0xb5fd20>()(b));
    OriginalFunction<SetWord, 0xb65750>()(mOverlayFilter, OriginalFunction<MapWord, 0x267350>()(mAge));
    OriginalFunction<SetBlock, 0xb656f0>()(mOverlayFilter, mBlockB8);
    SetFilterBlockC8(mOverlayFilter, mBlockC8);
    OriginalFunction<SetBlock, 0xb65550>()(mOverlayFilter, guid);

    ServiceBlockVector matches{};
    Query(catalog, mOverlayFilter, &matches, skinBase, mBlockC8);
    uint32_t value = 0;
    if (!FirstMatchValue(matches, nullptr, &value)) {
        matches.FreeBlock();
        return 0;
    }

    // Which part names are worn: the last kind-3 record of each name (compared without case) decides.
    std::map<std::string, bool, NoCaseLess> worn;
    for (const BodyPartEntry *e = mStack->mFirst; e != mStack->mLast; ++e) {
        if (e->mKind == 3) worn[e->Name()] = e->mWeight > kEnabledWeight;
    }
    // The values of the worn parts whose first match has this GUID.
    std::set<uint32_t> values;
    for (const BodyPartEntry *e = mStack->mFirst; e != mStack->mLast; ++e) {
        if (e->mKind != 3 || !worn.find(e->Name())->second) continue;
        // ORIGINAL-BUG (not reproduced): the original parses the name inline with no limit of four words, so a name
        // with more than 32 characters other than '-', '{' and '}' writes past its 16-byte GUID into its locals.
        uint32_t partGuid[4];
        hexparse::ParseGuidText(partGuid, e->Name());
        OriginalFunction<SetBlock, 0xb65550>()(mOverlayFilter, partGuid);
        ServiceBlockVector parts{};
        Query(catalog, mOverlayFilter, &parts, skinBase, mBlockC8);
        uint32_t partValue;
        if (FirstMatchValue(parts, guid, &partValue)) values.insert(partValue);
        parts.FreeBlock();
    }
    const uint32_t applied = values.count(value) != 0 ? 1u : 0u;
    matches.FreeBlock();
    return applied;
}

} // namespace nTSCASCore
