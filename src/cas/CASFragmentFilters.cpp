// The filters cTSCASSystem's gathers hand the bin catalog (lane JL; src/cas/CASFragmentFilters.h).
#include "cas/CASFragmentFilters.h"

#include "cas/cTSCASSystem.h" // CASItem
#include "game/original.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"

#include <cstring>
#include <new>

namespace nTSCASCore {

namespace {
constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kFilterIid = 0xcec03a70;

} // namespace

// ---- cTSSimFragmentFilterBase ----

bool cTSSimFragmentFilterBase::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kFilterIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSSimFragmentFilterBase::AddRef() { return ++mRefs; }

uint32_t cTSSimFragmentFilterBase::Release()
{
    if (mRefs > 1) return --mRefs;
    delete this;
    return 0;
}

bool cTSSimFragmentFilterBase::SeenBefore(const SkinKey *key)
{
    if (!mDropDuplicates) return false;
    return !mSeen.insert(*key).second;
}

// ---- cTSDuplicateObjectFragmentFilter ----

// 0x505660: a fragment whose key (its slot 21) was not seen before.
bool cTSDuplicateObjectFragmentFilter::Accept(cIGZUnknown *fragment) { return !SeenBefore(RawSlot<21, const SkinKey *>(fragment)); }

// ---- cTSBinFilterBase ----

bool cTSBinFilterBase::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kUnknownIid && riid != kFilterIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSBinFilterBase::AddRef() { return ++mRefs; }

// 0xb64c70: at a count of 1 or below the 8-byte base block is freed (nothing else is destroyed).
uint32_t cTSBinFilterBase::Release()
{
    if (mRefs > 1) return --mRefs;
    ::operator delete(static_cast<void *>(this), 8);
    return 0;
}

// ---- cTSBinNameFilter ----

// 0x504a30: a bin without bit 1 in the low byte of its slot 27 whose key (slot 16) is not all zero.
bool cTSBinNameFilter::Accept(cIGZUnknown *bin)
{
    if (!bin || (RawSlot<27, uint8_t>(bin) & 2)) return false;
    const SkinKey *const key = RawSlot<16, const SkinKey *>(bin);
    return key->type || key->group || key->instance || key->resource;
}

// ---- cTSCASBinFilter ----

// 0x4ff7c0: the type, and the catalog's bin for it (its slot 19; counted here).
cTSCASBinFilter::cTSCASBinFilter(uint32_t type) : mType(type)
{
    void *const catalog = CachedServiceGetter<0x10faf78, 0x0c4f252f, 0x2c4f2954>();
    mTypeBin.Assign(RawSlot<19, cIGZUnknown *>(catalog, type));
}

// 0x504a70: a bin of the type (cTSBinTypeFilter's 0xb64930: its slot 12 has a bit of it) that is the type's own bin or
// has neither bit 0 nor bit 1 in the low byte of its slot 27.
bool cTSCASBinFilter::Accept(cIGZUnknown *bin)
{
    if (!bin || !(RawSlot<12, uint32_t>(bin) & mType)) return false;
    if (bin == mTypeBin.Get()) return true;
    return (RawSlot<27, uint8_t>(bin) & 3) == 0;
}

// ---- lane JM: the filters of slots 26 to 31 ----

namespace {
constexpr uint32_t kPropertiesIid = 0x8c1571d6; // a fragment's property set (the resource service's slot 37)
constexpr uint32_t kPartIid = 0xebcf3e20;       // a skin fragment's part resource
constexpr uint32_t kTextureIid = 0x2c1fd89d;    // an overlay's texture face (and resource)
constexpr uint32_t kMeshIid = 0x0c1fe23e;       // an overlay's mesh face (and resource)

void *ResourceService() { return CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>(); }
const SkinKey *ZeroKey() { return OriginalFunction<const SkinKey *(__cdecl *)(), 0x4f8e50>()(); }
bool SameKey(const SkinKey *a, const SkinKey *b) { return std::memcmp(a, b, sizeof(SkinKey)) == 0; } // Blob4<16>::operator==

// The resource a key names, as the filters ask it: the out reference emptied first, then the service's slot 37.
bool LoadFor(const SkinKey *key, uint32_t iid, cRZAutoRefCount<cIGZUnknown> &out)
{
    void *const service = ResourceService();
    return RawSlot<37, bool>(service, key, iid, out.AsPPVoid());
}
} // namespace

// 0x504cb0 (thiscall on the key): the key is the zero key (0x4f8e50's).
bool IsZeroKey(const SkinKey *key) { return SameKey(key, ZeroKey()); }
bool __fastcall CASFilterAdapters::IsZeroKey(const SkinKey *key, void *) { return nTSCASCore::IsZeroKey(key); }

// ---- cTSAGSFragFilter ----

bool cTSAGSFragFilter::AcceptProperties(cIGZUnknown *properties)
{
    using namespace fragprops;
    if (!properties) return false;
    if (!(mAges & RawSlot<kAges, uint32_t>(properties)) || !(mGenders & RawSlot<kGenders, uint32_t>(properties)) ||
        !(mStyles & RawSlot<kStyles, uint32_t>(properties)))
        return false;
    if (mFlags && RawSlot<kFlags, uint32_t>(properties) && !(mFlags & RawSlot<kFlags, uint32_t>(properties))) return false;
    return true;
}

bool cTSAGSFragFilter::AcceptFragment(cIGZUnknown *fragment)
{
    if (!mAllowCustom && RawSlot<27, int16_t>(fragment) == 0) return false;
    const SkinKey *const key = RawSlot<21, const SkinKey *>(fragment);
    if (SeenBefore(key)) return false;
    cRZAutoRefCount<cIGZUnknown> properties;
    return LoadFor(key, kPropertiesIid, properties) && AcceptProperties(properties.Get());
}

bool cTSAGSFragFilter::AcceptItemAt(const CASItem *item, unsigned ref, bool customTest)
{
    cIGZUnknown *const fragment = item->refs[0].Get();
    cIGZUnknown *const resource = item->refs[ref].Get();
    if (!fragment || !resource) return false;
    if (customTest && !mAllowCustom && item->flag) return false;
    if (SeenBefore(RawSlot<21, const SkinKey *>(fragment))) return false;
    return AcceptProperties(RawSlot<3, cIGZUnknown *>(resource));
}

bool cTSAGSFragFilter::AcceptOfType(cIGZUnknown *fragment, uint32_t type)
{
    return fragment && RawSlot<21, const SkinKey *>(fragment)->type == type && AcceptFragment(fragment);
}

bool __fastcall CASFilterAdapters::AcceptFragment(cTSAGSFragFilter *self, void *, cIGZUnknown *fragment)
{
    return self->AcceptFragment(fragment);
}

bool cTSArchetypesFragFilter::Accept(cIGZUnknown *fragment) { return AcceptOfType(fragment, 0x8c93e35c); }
bool cTSArchetypesFragFilter::AcceptItem(const CASItem *item) { return AcceptItemAt(item, 3, false); }
bool cTSModifierFragFilter::Accept(cIGZUnknown *fragment) { return AcceptOfType(fragment, 0x0c93e3de); }
bool cTSModifierFragFilter::AcceptItem(const CASItem *item) { return AcceptItemAt(item, 4, false); }
bool cTSSkinTonesFragFilter::Accept(cIGZUnknown *fragment) { return AcceptOfType(fragment, 0x4c158081); }
bool cTSSkinTonesFragFilter::AcceptItem(const CASItem *item) { return AcceptItemAt(item, 6, true); }
bool cTSBodyOptionsFragFilter::Accept(cIGZUnknown *fragment) { return AcceptOfType(fragment, 0xd1954460); }
bool cTSBodyOptionsFragFilter::AcceptItem(const CASItem *item) { return AcceptItemAt(item, 8, true); }

// ---- cTSBinSkinFragFilter ----

bool cTSBinSkinFragFilter::Accept(cIGZUnknown *fragment)
{
    if (!fragment) return false;
    if (!mAllowCustom && RawSlot<27, int16_t>(fragment) == 0) return false;
    const SkinKey *const key = RawSlot<21, const SkinKey *>(fragment);
    if (key->type != 0xebcf3e27 || SeenBefore(key)) return false;
    cRZAutoRefCount<cIGZUnknown> part;
    return LoadFor(key, kPartIid, part) && part && MatchesPart(part.Get());
}

bool cTSBinSkinFragFilter::AcceptItem(const CASItem *item)
{
    cIGZUnknown *const fragment = item->refs[0].Get();
    cIGZUnknown *const part = item->refs[1].Get();
    if (!fragment || !part) return false;
    if (!mAllowCustom && item->flag) return false;
    if (SeenBefore(RawSlot<21, const SkinKey *>(fragment))) return false;
    return MatchesPart(part);
}

bool cTSBinSkinFragFilter::MatchesPart(cIGZUnknown *part)
{
    using namespace fragprops;
    if (!part) return false;
    const auto properties = [&] { return RawSlot<3, cIGZUnknown *>(part); };
    const uint32_t flags = RawSlot<kFlags, uint32_t>(properties());
    if (!(mAges & RawSlot<kAges, uint32_t>(properties())) || !(mGenders & RawSlot<kGenders, uint32_t>(properties()))) return false;
    // A key given must be the part's, unless the part has the zero key there.
    if (mKeyA && !IsZeroKey(RawSlot<5, const SkinKey *>(part)) && !SameKey(mKeyA, RawSlot<5, const SkinKey *>(part))) return false;
    if (mKeyB && !IsZeroKey(RawSlot<6, const SkinKey *>(part)) && !SameKey(mKeyB, RawSlot<6, const SkinKey *>(part))) return false;
    if (RawSlot<kStyles, uint32_t>(properties()) && !(mStyles & RawSlot<kStyles, uint32_t>(properties()))) return false;
    if (RawSlot<kCategories, uint32_t>(properties()) && !(mCategories & RawSlot<kCategories, uint32_t>(properties()))) return false;
    if (RawSlot<7, uint16_t>(part) && !(mParts & RawSlot<7, uint16_t>(part))) return false;
    if ((mAllowedFlags & flags) != flags) return false;
    if (mKeyC && !SameKey(mKeyC, RawSlot<kKey, const SkinKey *>(properties()))) return false;
    return true;
}

// ---- the overlay filters ----

void cTSOutfitDataFragmentFilterBase::SetDropDuplicates(bool drop)
{
    mDropDuplicates = drop;
    if (!drop) mSeen.clear();
}

bool cTSOutfitDataFragmentFilterBase::Matches(uint32_t ages, uint32_t genders, uint32_t styles, uint32_t parts,
                                              uint32_t flags, const SkinKey *keyA, const SkinKey *keyB)
{
    if ((mTests & kTestAges) && (mAges & ages) != mAges) return false;
    if ((mTests & kTestGenders) && (mGenders & genders) != mGenders) return false;
    if ((mTests & kTestStyles) && (mStyles & styles) != mStyles) return false;
    if ((mTests & kTestParts) && (mParts & parts) != mParts) return false;
    if ((mTests & kTestFlags) && mFlags && (mFlags & flags) != flags) return false;
    if ((mTests & kTestKeyA) && !IsZeroKey(keyA) && !SameKey(keyA, &mKeyA)) return false;
    if ((mTests & kTestKeyB) && !IsZeroKey(keyB) && !SameKey(keyB, &mKeyB)) return false;
    return true;
}

namespace {
// The mesh and texture filters' shape: a fragment of the key type, not seen, whose resource of `iid` loads; then the
// resource's property set (its slot 3, asked four times) and its own values, handed to Matches.
template <unsigned KeyBSlot, unsigned KeyASlot, unsigned PartsSlot>
bool AcceptOverlay(cTSOutfitDataFragmentFilterBase *self, cIGZUnknown *fragment, uint32_t type, uint32_t iid,
                   int)
{
    const SkinKey *const key = RawSlot<21, const SkinKey *>(fragment);
    if (key->type != type || self->Seen(key)) return false;
    void *const service = ResourceService();
    cRZAutoRefCount<cIGZUnknown> resource;
    if (!RawSlot<37, bool>(service, key, iid, resource.AsPPVoid())) return false;
    // The original reads the resource without a null test (0xb649d9 / 0xb64b89).
    cIGZUnknown *const r = resource.Get();
    cIGZUnknown *const forFlags = RawSlot<3, cIGZUnknown *>(r);
    cIGZUnknown *const forStyles = RawSlot<3, cIGZUnknown *>(r);
    cIGZUnknown *const forGenders = RawSlot<3, cIGZUnknown *>(r);
    cIGZUnknown *const forAges = RawSlot<3, cIGZUnknown *>(r);
    const SkinKey *const keyB = RawSlot<KeyBSlot, const SkinKey *>(r);
    const SkinKey *const keyA = RawSlot<KeyASlot, const SkinKey *>(r);
    const uint32_t flags = RawSlot<fragprops::kCategories, uint32_t>(forFlags);
    const uint32_t parts = RawSlot<PartsSlot, uint16_t>(r);
    const uint32_t styles = RawSlot<fragprops::kStyles, uint32_t>(forStyles);
    const uint32_t genders = RawSlot<fragprops::kGenders, uint32_t>(forGenders);
    const uint32_t ages = RawSlot<fragprops::kAges, uint32_t>(forAges);
    return self->Matches(ages, genders, styles, parts, flags, keyA, keyB);
}
} // namespace

bool cTSMeshOverlayFragmentFilter::Accept(cIGZUnknown *fragment)
{
    return AcceptOverlay<7, 8, 10>(this, fragment, 0x0c1fe246, 0x0c1fe23e, 0);
}

bool cTSTextureOverlayFragmentFilter::Accept(cIGZUnknown *fragment)
{
    return AcceptOverlay<8, 9, 13>(this, fragment, 0x2c1fd8a1, 0x2c1fd89d, 0);
}

bool __fastcall CASFilterAdapters::MeshAccept(cTSMeshOverlayFragmentFilter *self, void *, cIGZUnknown *fragment)
{
    return self->cTSMeshOverlayFragmentFilter::Accept(fragment);
}

cTSOverlayFragmentFilter::cTSOverlayFragmentFilter()
{
    mTexture.SetDropDuplicates(false);
    mMesh.SetDropDuplicates(false);
}

void cTSOverlayFragmentFilter::Prepare()
{
    for (cTSOutfitDataFragmentFilterBase *part : {static_cast<cTSOutfitDataFragmentFilterBase *>(&mTexture),
                                                  static_cast<cTSOutfitDataFragmentFilterBase *>(&mMesh)}) {
        part->mAges = mAges;
        part->mGenders = mGenders;
        part->mParts = mParts;
        part->mKeyA = mKeyA;
        part->mKeyB = mKeyB;
        part->mTests = mTests;
    }
}

bool cTSOverlayFragmentFilter::Accept(cIGZUnknown *fragment)
{
    if (!mPrepared) {
        Prepare();
        mPrepared = true;
    }
    const SkinKey *const key = RawSlot<21, const SkinKey *>(fragment);
    if (SeenBefore(key)) return false;
    if (key->type == 0x0c1fe246) return mMesh.Accept(fragment);
    if (key->type == 0x2c1fd8a1) return mTexture.Accept(fragment);
    return false;
}

// ---- cTSCosmeticsFragFilter ----

cTSCosmeticsFragFilter::cTSCosmeticsFragFilter(uint32_t ages, uint32_t genders, uint32_t styles, const SkinKey *keyB,
                                               const SkinKey *keyA, const SkinKey *keyC, uint32_t parts, uint32_t flags)
    : mCosmeticStyles(styles), mAllowedFlags(flags), mKeyC(keyC)
{
    mAges = ages;
    mGenders = genders;
    mParts = parts;
    if (keyA) mKeyA = *keyA;
    if (keyB) mKeyB = *keyB;
    mTests = 0;
}

bool cTSCosmeticsFragFilter::Accept(cIGZUnknown *fragment)
{
    if (!fragment) return false;
    if (!mAllowCustom && RawSlot<27, int16_t>(fragment) == 0) return false;
    if (!cTSOverlayFragmentFilter::Accept(fragment)) return false;
    void *const service = ResourceService();
    cRZAutoRefCount<cIGZUnknown> properties;
    void **const out = properties.AsPPVoid();
    const SkinKey *const key = RawSlot<21, const SkinKey *>(fragment);
    return RawSlot<37, bool>(service, key, kPropertiesIid, out) && properties && MatchesProperties(properties.Get());
}

bool cTSCosmeticsFragFilter::AcceptItem(const CASItem *item)
{
    cIGZUnknown *const fragment = item->refs[0].Get();
    cIGZUnknown *const properties = item->refs[2].Get();
    if (!fragment || !properties) return false;
    if (!mAllowCustom && item->flag) return false;
    if (SeenBefore(RawSlot<21, const SkinKey *>(fragment))) return false;
    return MatchesProperties(properties);
}

bool cTSCosmeticsFragFilter::MatchesProperties(cIGZUnknown *properties)
{
    using namespace fragprops;
    if (!properties) return false;
    if (!(mAges & RawSlot<kAges, uint32_t>(properties)) || !(mGenders & RawSlot<kGenders, uint32_t>(properties))) return false;
    if (mCosmeticStyles && RawSlot<kStyles, uint32_t>(properties) && !(mCosmeticStyles & RawSlot<kStyles, uint32_t>(properties)))
        return false;
    if (mAllowedFlags && RawSlot<kFlags, uint32_t>(properties) && (~mAllowedFlags & RawSlot<kFlags, uint32_t>(properties))) return false;
    // The texture face, else the mesh face (released in that order's reverse).
    cRZAutoRefCount<cIGZUnknown> texture, mesh;
    if (!RawSlot<0, bool>(properties, kTextureIid, texture.AsPPVoid()) && !RawSlot<0, bool>(properties, kMeshIid, mesh.AsPPVoid()))
        return false;
    const auto keyB = [&] { return mesh ? RawSlot<7, const SkinKey *>(mesh.Get()) : RawSlot<8, const SkinKey *>(texture.Get()); };
    const auto keyA = [&] { return mesh ? RawSlot<8, const SkinKey *>(mesh.Get()) : RawSlot<9, const SkinKey *>(texture.Get()); };
    if (!IsZeroKey(&mKeyB)) {
        const SkinKey *const k = keyB();
        if (!IsZeroKey(k) && !SameKey(&mKeyB, k)) return false;
    }
    if (!IsZeroKey(&mKeyA)) {
        const SkinKey *const k = keyA();
        if (!IsZeroKey(k) && !SameKey(&mKeyA, k)) return false;
    }
    if (mKeyC && !SameKey(mKeyC, RawSlot<5, const SkinKey *>(mesh ? mesh.Get() : texture.Get()))) return false;
    if (mParts) {
        const uint16_t parts = mesh ? RawSlot<10, uint16_t>(mesh.Get()) : RawSlot<13, uint16_t>(texture.Get());
        if (parts && !(mParts & parts)) return false;
    }
    return true;
}

} // namespace nTSCASCore
