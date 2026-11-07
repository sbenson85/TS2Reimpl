#pragma once
// The fragment and bin filters Create-a-Sim's system hands the bin catalog when it gathers fragments (lane JL): the
// interfaces, and the two filters only cTSCASSystem builds (its gathers, src/cas/cTSCASSystem.cpp), as our classes.
//
// The catalog's slot 30 lists the fragments of the bins a bin filter accepts that a fragment filter accepts; its slot 32
// the fragments of one bin a fragment filter accepts. The gathers then turn each fragment into a CASItem.
#include "cas/cTSSkinCatalog.h"            // SkinKey, SkinKeyLess
#include "gz/cIGZUnknown.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstdint>
#include <set>

namespace nTSCASCore {

struct CASItem;

// nTSCASCore::cITSSimFragmentFilter (iid 0xcec03a70; the catalog asks it of each fragment).
class cITSSimFragmentFilter : public cIGZUnknown {
public:
    virtual bool Accept(cIGZUnknown *fragment) = 0; // slot 3
    virtual ~cITSSimFragmentFilter() = default;      // slot 4 (deleting)
};

// nTSCASCore::cITSFragEntryFilter: the face the gathers take (each derived filter's second vtable).
class cITSFragEntryFilter {
public:
    virtual cITSSimFragmentFilter *GetFilter() = 0;      // slot 0: the filter itself (its primary face)
    virtual bool AcceptItem(const CASItem *item) = 0;     // slot 1: a gathered item (the type caches' path)
};

// nTSCASCore::cITSBinFilter (the catalog asks it of each bin).
class cITSBinFilter : public cIGZUnknown {
public:
    virtual bool Accept(cIGZUnknown *bin) = 0; // slot 3
};

// nTSCASCore::cTSSimFragmentFilterBase (vtable 0xe88444; constructor 0xb64680, destructor 0xb64720; 0x14 bytes): the count,
// and the keys of the fragments seen so far when it drops duplicates (the set's node 0x20: the key at +0x10).
class cTSSimFragmentFilterBase : public cITSSimFragmentFilter {
public:
    explicit cTSSimFragmentFilterBase(bool dropDuplicates) : mDropDuplicates(dropDuplicates) {}
    cTSSimFragmentFilterBase(const cTSSimFragmentFilterBase &) = delete;
    cTSSimFragmentFilterBase &operator=(const cTSSimFragmentFilterBase &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0xb64c40: iid 1 or 0xcec03a70
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x723ae0: deletes at a count of 1 or below

protected:
    // 0xb64790: when dropping duplicates, whether the key was seen before (it is remembered).
    bool SeenBefore(const SkinKey *key);

    uint32_t mRefs = 0;                    // +0x04
    bool mDropDuplicates;                  // +0x08
    std::set<SkinKey, SkinKeyLess> mSeen;  // +0x0c
};
static_assert(sizeof(cTSSimFragmentFilterBase) == 0x14, "cTSSimFragmentFilterBase");

// nTSCASCore::cTSDuplicateObjectFragmentFilter (vtable 0xdce700; built by the gathers 0x50a1d0 / 0x50bc00 only): every
// fragment once (by its key, slot 21).
class cTSDuplicateObjectFragmentFilter : public cTSSimFragmentFilterBase {
public:
    cTSDuplicateObjectFragmentFilter() : cTSSimFragmentFilterBase(true) {}
    bool Accept(cIGZUnknown *fragment) override; // 0x505660
};

// nTSCASCore::cTSBinFilterBase (vtable 0xe8845c; constructor 0xb64490, inlined by our derived filters; 8 bytes): the count.
class cTSBinFilterBase : public cITSBinFilter {
public:
    cTSBinFilterBase() = default;
    cTSBinFilterBase(const cTSBinFilterBase &) = delete;
    cTSBinFilterBase &operator=(const cTSBinFilterBase &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0xb64c40
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // 0xb64c70: frees the 8-byte base block at a count of 1

    uint32_t mRefs = 0; // +0x04
};
static_assert(sizeof(cTSBinFilterBase) == 8, "cTSBinFilterBase");

// nTSCASCore::cTSCASBinFilter (vtable 0xdce72c; constructor 0x4ff7c0, also inlined in 0x50bc00; 0x10 bytes; bases
// cTSBinTypeFilter 0xb644a0 (+8 the type) and cTSBinFilterBase 0xb64490 (+4 the count)): the bins of a type (their slot 12
// has a bit of it), but of those only the catalog's own bin for the type (its slot 19) or bins without bits 0..1 of their
// slot 27.
class cTSCASBinFilter : public cTSBinFilterBase {
public:
    explicit cTSCASBinFilter(uint32_t type); // 0x4ff7c0
    bool Accept(cIGZUnknown *bin) override;  // 0x504a70 (over cTSBinTypeFilter's 0xb64930)

    uint32_t mType;                           // +0x08
    cRZAutoRefCount<cIGZUnknown> mTypeBin;    // +0x0c
};
static_assert(sizeof(cTSCASBinFilter) == 0x10, "cTSCASBinFilter");

// nTSCASCore::cTSBinNameFilter (vtable 0xdce718; built on the stack by cTSCASSystem's slot 19 0x501dd0 only; 8 bytes): the
// bins that can have a name: not flagged 2 (bit 1 of their slot 27's low byte) and with a key (their slot 16) other than
// all zero.
class cTSBinNameFilter : public cTSBinFilterBase {
public:
    bool Accept(cIGZUnknown *bin) override; // 0x504a30
};
static_assert(sizeof(cTSBinNameFilter) == 8, "cTSBinNameFilter");

// ---- the filters cTSCASSystem's slots 26 to 31 build on the stack (lane JM) ----
//
// Each is a fragment filter (primary face) and a cITSFragEntryFilter (+0x14, or +0xe8 for the cosmetics filter) that the
// gathers take: the catalog asks the first of each fragment (slot 3), the type-cache path the second of each cached item.

// nTSCASCore::cTSAGSFragFilter (no vtable of its own; 0x2c bytes): the base of the age / gender / "style" filters: a fragment
// of the class's key type, not custom content unless allowed, each key once, whose property set (the resource service's
// slot 37, iid 0x8c1571d6) has a bit of each mask (slot 5).
class cTSAGSFragFilter : public cTSSimFragmentFilterBase, public cITSFragEntryFilter {
public:
    cTSAGSFragFilter(uint32_t ages, uint32_t genders, uint32_t styles, uint32_t flags, uint8_t allowCustom)
        : cTSSimFragmentFilterBase(true), mAges(ages), mGenders(genders), mStyles(styles), mFlags(flags),
          mAllowCustom(allowCustom)
    {
    }

    // slot 5, 0x505750 (shared by the four filters below): the property set has a bit of the ages (its slot 19), the
    // genders (20) and the styles (21), and, when flags are given and it has some (25), one of them.
    virtual bool AcceptProperties(cIGZUnknown *properties);
    cITSSimFragmentFilter *GetFilter() override { return this; } // 0x500cb0 (the +0x14 face's slot 0)

    // 0x504fa0: custom content only when allowed (the fragment's slot 27 is zero for it), each key once, and the
    // fragment's property set passes slot 5. Hooked: cTSSpecialOutfitFragFilter's Accept 0x505700 (game code) calls it.
    bool AcceptFragment(cIGZUnknown *fragment);

protected:
    // The AcceptItem bodies' shape: the item has a fragment and the resource at `ref` (custom content only when allowed,
    // if `customTest`), its key was not seen, and the resource's property set (its slot 3) passes slot 5.
    bool AcceptItemAt(const CASItem *item, unsigned ref, bool customTest);
    // The Accept bodies' shape: a fragment whose key (slot 21) is of `type` and that AcceptFragment takes.
    bool AcceptOfType(cIGZUnknown *fragment, uint32_t type);

public:
    // +0x14: the cITSFragEntryFilter face's vtable
    uint32_t mAges;       // +0x18
    uint32_t mGenders;    // +0x1c
    uint32_t mStyles;     // +0x20
    uint32_t mFlags;      // +0x24 (0: any)
    uint8_t mAllowCustom; // +0x28 (AllowCustomContent's raw byte, or 1)
};
static_assert(sizeof(cTSAGSFragFilter) == 0x2c, "cTSAGSFragFilter");

// nTSCASCore::cTSArchetypesFragFilter (vtables 0xdce740 / 0xdce75c; slot 27 0x509f60): key type 0x8c93e35c, the item's
// +0x0c resource.
class cTSArchetypesFragFilter : public cTSAGSFragFilter {
public:
    using cTSAGSFragFilter::cTSAGSFragFilter;
    bool Accept(cIGZUnknown *fragment) override;      // 0x505080
    bool AcceptItem(const CASItem *item) override;    // 0x504ce0
};
// nTSCASCore::cTSModifierFragFilter (vtables 0xdce768 / 0xdce784; slot 28 0x50a4f0): key type 0x0c93e3de, the item's +0x10.
class cTSModifierFragFilter : public cTSAGSFragFilter {
public:
    using cTSAGSFragFilter::cTSAGSFragFilter;
    bool Accept(cIGZUnknown *fragment) override;      // 0x505680
    bool AcceptItem(const CASItem *item) override;    // 0x504e30
};
// nTSCASCore::cTSSkinTonesFragFilter (vtables 0xdce790 / 0xdce7ac; slot 29 0x50a790): key type 0x4c158081, the item's +0x18.
class cTSSkinTonesFragFilter : public cTSAGSFragFilter {
public:
    using cTSAGSFragFilter::cTSAGSFragFilter;
    bool Accept(cIGZUnknown *fragment) override;      // 0x5056c0
    bool AcceptItem(const CASItem *item) override;    // 0x504e80
};
// nTSCASCore::cTSBodyOptionsFragFilter (vtables 0xdce800 / 0xdce81c; slot 30 0x50a000): key type 0xd1954460, the item's +0x20.
class cTSBodyOptionsFragFilter : public cTSAGSFragFilter {
public:
    using cTSAGSFragFilter::cTSAGSFragFilter;
    bool Accept(cIGZUnknown *fragment) override;      // 0x5052f0
    bool AcceptItem(const CASItem *item) override;    // 0x504d80
};

// nTSCASCore::cTSBinSkinFragFilter (vtables 0xdce7b8 / 0xdce7d0; slot 31 0x50a840; 0x40 bytes): the skin fragments (key type
// 0xebcf3e27) whose part resource (iid 0xebcf3e20) matches: its property set (the part's slot 3) has a bit of the ages and
// genders, the part's keys (slots 5, 6) are zero or the ones given, and so on (MatchesPart).
class cTSBinSkinFragFilter : public cTSSimFragmentFilterBase, public cITSFragEntryFilter {
public:
    cTSBinSkinFragFilter(uint32_t ages, uint32_t genders, const SkinKey *keyA, const SkinKey *keyB, uint32_t styles,
                         uint32_t categories, uint32_t parts, uint32_t allowedFlags, const SkinKey *keyC, uint8_t allowCustom)
        : cTSSimFragmentFilterBase(true), mAges(ages), mGenders(genders), mStyles(styles), mCategories(categories),
          mParts(parts), mAllowedFlags(allowedFlags), mKeyA(keyA), mKeyB(keyB), mKeyC(keyC), mAllowCustom(allowCustom)
    {
    }

    bool Accept(cIGZUnknown *fragment) override;                 // 0x5050c0
    cITSSimFragmentFilter *GetFilter() override { return this; } // 0x500cb0
    bool AcceptItem(const CASItem *item) override;               // 0x504d30
    // 0x505a60: the part's property set (slot 3) has a bit of the ages (19) and genders (20); each key given matches the
    // part's (slots 5, 6) unless that is the zero key; the styles (21) and categories (22) when it has some; the parts
    // (its slot 7, a short) when it has some; all its flags (25) allowed; and keyC, when given, is its set's slot 16.
    bool MatchesPart(cIGZUnknown *part);

    // +0x14: the cITSFragEntryFilter face's vtable
    uint32_t mAges;          // +0x18
    uint32_t mGenders;       // +0x1c
    uint32_t mStyles;        // +0x20
    uint32_t mCategories;    // +0x24
    uint32_t mParts;         // +0x28 (16 bits used)
    uint32_t mAllowedFlags;  // +0x2c
    const SkinKey *mKeyA;    // +0x30
    const SkinKey *mKeyB;    // +0x34
    const SkinKey *mKeyC;    // +0x38
    uint8_t mAllowCustom;    // +0x3c
};
static_assert(sizeof(cTSBinSkinFragFilter) == 0x40, "cTSBinSkinFragFilter");

// ---- the cosmetics filter (slot 26) and the overlay filters it is built on ----

// nTSCASCore::cTSOutfitDataFragmentFilterBase (vtable 0xe88470; 0x4c bytes): what an outfit asks of a fragment, and which of
// those tests are on (+0x48, all at first). Its constructor 0xb644e0 takes an outfit (its slots 24, 22, 23, 41, 29, 28);
// ours is the form slot 26 uses, without one (every field zero). The outfit form, built by game code (0x6f9cd5's collar
// filter), stays the image's.
class cTSOutfitDataFragmentFilterBase : public cTSSimFragmentFilterBase {
public:
    cTSOutfitDataFragmentFilterBase() : cTSSimFragmentFilterBase(true) {}

    // 0xb64790 (the base's), for the overlay filters' shared body.
    bool Seen(const SkinKey *key) { return SeenBefore(key); }

    // 0xb648d0: whether keys seen before are dropped (cleared: the keys seen are forgotten).
    void SetDropDuplicates(bool drop);
    // 0xb647d0: the fragment's values pass each test that is on: all the ages, genders, styles and parts asked for; its
    // flags among those asked for (when any are); the keys, unless the fragment's is the zero key.
    bool Matches(uint32_t ages, uint32_t genders, uint32_t styles, uint32_t parts, uint32_t flags, const SkinKey *keyA,
                 const SkinKey *keyB);

    enum : uint32_t { kTestAges = 1, kTestGenders = 2, kTestParts = 4, kTestKeyA = 8, kTestKeyB = 0x10, kTestStyles = 0x20,
                      kTestFlags = 0x40 };
    uint32_t mStyles = 0;          // +0x14 (the outfit's slot 24)
    uint32_t mAges = 0;            // +0x18 (22)
    uint32_t mGenders = 0;         // +0x1c (23)
    uint32_t mParts = 0;           // +0x20 (41)
    uint32_t mFlags = 0;           // +0x24
    SkinKey mKeyA = {};            // +0x28 (*29)
    SkinKey mKeyB = {};            // +0x38 (*28)
    uint32_t mTests = 0xffffffff;  // +0x48
};
static_assert(sizeof(cTSOutfitDataFragmentFilterBase) == 0x4c, "cTSOutfitDataFragmentFilterBase");

// nTSCASCore::cTSTextureOverlayFragmentFilter (vtable 0xe88488; constructor 0xb646e0): texture overlays (key type 0x2c1fd8a1)
// whose texture resource (iid 0x2c1fd89d) matches.
class cTSTextureOverlayFragmentFilter : public cTSOutfitDataFragmentFilterBase {
public:
    bool Accept(cIGZUnknown *fragment) override; // 0xb64b10
};
// nTSCASCore::cTSMeshOverlayFragmentFilter (vtable 0xe884a0; constructor 0xb644c0): mesh overlays (key type 0x0c1fe246)
// whose mesh resource (iid 0x0c1fe23e) matches. Its Accept is hooked: the collar filter's Accept 0x6fa298 (game code) calls
// it directly.
class cTSMeshOverlayFragmentFilter : public cTSOutfitDataFragmentFilterBase {
public:
    bool Accept(cIGZUnknown *fragment) override; // 0xb64960
};

// nTSCASCore::cTSOverlayFragmentFilter (vtable 0xe884b8; constructor 0xb645f0 without an outfit; 0xe8 bytes): mesh and texture
// overlays, each key once, through the two filters it holds (which keep every key), handed its own tests on first use.
class cTSOverlayFragmentFilter : public cTSOutfitDataFragmentFilterBase {
public:
    cTSOverlayFragmentFilter(); // 0xb645f0
    bool Accept(cIGZUnknown *fragment) override; // 0xb64a90
    // 0xb64c90: the ages, genders, parts, keys and tests handed to the two filters (not the styles or flags).
    void Prepare();

    cTSTextureOverlayFragmentFilter mTexture; // +0x4c
    cTSMeshOverlayFragmentFilter mMesh;       // +0x98
    bool mPrepared = false;                   // +0xe4
};
static_assert(sizeof(cTSOverlayFragmentFilter) == 0xe8, "cTSOverlayFragmentFilter");

// nTSCASCore::cTSCosmeticsFragFilter (vtables 0xdce7dc / 0xdce7f4; slot 26 0x50a0c0; 0x100 bytes): overlays whose property
// set (iid 0x8c1571d6) has a bit of the ages and genders, of the styles and only allowed flags when it has some, and whose
// texture (QueryInterface 0x2c1fd89d) or mesh (0x0c1fe23e) face matches the keys and parts.
class cTSCosmeticsFragFilter : public cTSOverlayFragmentFilter, public cITSFragEntryFilter {
public:
    cTSCosmeticsFragFilter(uint32_t ages, uint32_t genders, uint32_t styles, const SkinKey *keyB, const SkinKey *keyA,
                           const SkinKey *keyC, uint32_t parts, uint32_t flags);

    bool Accept(cIGZUnknown *fragment) override;                 // 0x505330
    cITSSimFragmentFilter *GetFilter() override { return this; } // 0x500cc0
    bool AcceptItem(const CASItem *item) override;               // 0x504de0
    // 0x505bc0: the property-set tests above.
    bool MatchesProperties(cIGZUnknown *properties);

    // +0xe8: the cITSFragEntryFilter face's vtable
    uint32_t mCosmeticStyles; // +0xec
    uint32_t mUnusedF0 = 0;   // +0xf0 never written (the original leaves it uninitialised)
    uint32_t mAllowedFlags;   // +0xf4 (0: any)
    const SkinKey *mKeyC;     // +0xf8
    uint8_t mAllowCustom = 1; // +0xfc
};
static_assert(sizeof(cTSCosmeticsFragFilter) == 0x100, "cTSCosmeticsFragFilter");

// 0x504cb0 (thiscall on the key; 19 callers, CAS and its UI): the key is the zero key (0x4f8e50's). Hooked.
bool IsZeroKey(const SkinKey *key);

// The hooks' entry points for bodies game code calls directly.
struct CASFilterAdapters {
    static bool __fastcall IsZeroKey(const SkinKey *key, void *edx);                                         // 0x504cb0
    static bool __fastcall AcceptFragment(cTSAGSFragFilter *self, void *edx, cIGZUnknown *fragment);        // 0x504fa0
    static bool __fastcall MeshAccept(cTSMeshOverlayFragmentFilter *self, void *edx, cIGZUnknown *fragment); // 0xb64960
};

// The property-set face the filters read (iid 0x8c1571d6; slots by the original's calls).
namespace fragprops {
constexpr unsigned kAges = 19, kGenders = 20, kStyles = 21, kCategories = 22, kFlags = 25, kKey = 16;
}

} // namespace nTSCASCore
