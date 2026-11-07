#include "gz/PixelFormatsManager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "gz/PixelFormatVTables.h"

namespace nGZGraphic4 {

namespace {

using LessSlot = uint32_t(__thiscall *)(const void *, const void *, uint32_t);
using QuerySlot = uint32_t(__thiscall *)(const void *, uint32_t, void **);
using ReleaseSlot = uint32_t(__thiscall *)(const void *);

// The ordering the registry keeps: a->Less(b, strict = true) through the vtable (slot 10).
template <uint32_t Strict>
struct FormatLess {
    bool operator()(const cPixelFormat *a, const cPixelFormat *b) const
    {
        return static_cast<uint8_t>(VirtualCall<LessSlot, 10>(a, b, Strict)) != 0;
    }
};

// Builds the temporary key, finds the matching format and returns it with the class's interface
// (null when absent). The key is destroyed the way the original does it: BaseT vtable and the name freed.
template <typename Format, uint32_t Iid, FormatClass Class, typename... Args>
cPixelFormat *Lookup(const cPixelFormatsManagerDX *mgr, Args... args)
{
    alignas(8) uint8_t storage[sizeof(Format)];
    Format *key = reinterpret_cast<Format *>(storage);
    key->Construct(0, args...);
    cPixelFormat *keyBase = key;
    cPixelFormatsManagerDX::Range range;
    cPixelFormatsManagerDX::EqualRange(&range, mgr->mSorted.data(), mgr->mSorted.data() + mgr->mSorted.size(), &keyBase, 0);
    cPixelFormat *found = nullptr;
    if (range.first != range.last) {
        cPixelFormat *candidate = *range.first;
        void *typed = nullptr;
        VirtualCall<QuerySlot, 0>(candidate, Iid, &typed);
        found = static_cast<cPixelFormat *>(typed);
        if (found) VirtualCall<ReleaseSlot, 2>(found);
    }
    key->template DeletingDestructorT<Class, cPixelFormat::kSize>(0);
    return found;
}

} // namespace

cPixelFormatsManagerDX::Range *cPixelFormatsManagerDX::EqualRange(Range *result, cPixelFormat *const *first, cPixelFormat *const *last,
                                                                  cPixelFormat *const *key, uint32_t)
{
    auto range = std::equal_range(first, last, *key, FormatLess<1>());
    result->first = range.first;
    result->last = range.second;
    return result;
}

cPixelFormatsManagerDX *cPixelFormatsManagerDX::Instance()
{
    // The manager is built and torn down by Construct and Destruct (it has no constructor of its own).
    struct Holder {
        alignas(cPixelFormatsManagerDX) unsigned char bytes[sizeof(cPixelFormatsManagerDX)];
        cPixelFormatsManagerDX *Get() { return reinterpret_cast<cPixelFormatsManagerDX *>(bytes); }
        Holder() { Get()->Construct(); }
        ~Holder() { Get()->Destruct(); }
        Holder(const Holder &) = delete;
        Holder &operator=(const Holder &) = delete;
    };
    static Holder holder;
    return holder.Get();
}

cPixelFormatsManagerDX *cPixelFormatsManagerDX::Construct()
{
    mVTable = ManagerVTable();
    new (&mById) std::unordered_map<uint32_t, cPixelFormat *>();
    new (&mSorted) std::vector<cPixelFormat *>(kFormatCount);
    cPixelFormat *const *table = CurrentStore().table;
    for (uint32_t i = 0; i < kFormatCount; ++i) {
        cPixelFormat *format = table[i];
        mSorted[i] = format;
        uint32_t id = VirtualCall<ReleaseSlot, 3>(format); // GetFormatId
        mById[id] = format;
    }
    std::sort(mSorted.data(), mSorted.data() + mSorted.size(), FormatLess<0>()); // the library's _Sort_unchecked, 0x15b030
    return this;
}

void cPixelFormatsManagerDX::Destruct()
{
    mVTable = ManagerVTable();
    mSorted.~vector();
    mById.~unordered_map();
}

bool cPixelFormatsManagerDX::QueryInterface(uint32_t riid, void **ppv)
{
    if (!ppv) return false;
    if (riid != 1 && riid != kIidPixelFormatsManager) return false;
    *ppv = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

cPixelFormat *cPixelFormatsManagerDX::GetFormat(uint32_t formatId)
{
    auto it = mById.find(formatId);
    if (it == mById.end()) return CurrentStore().unknown;
    return it->second;
}

cPixelFormat *cPixelFormatsManagerDX::GetUnknownFormat() { return CurrentStore().unknown; }

void *cPixelFormatsManagerDX::CreateIterator(void *out, uint32_t flags)
{
    auto create = CurrentStore().createIterator;
    if (!create) {
        std::fputs("cPixelFormatsManagerDX::CreateIterator is not reimplemented and this store has no image to lend it\n", stderr);
        std::abort();
    }
    return create(this, out, flags);
}

void *cPixelFormatsManagerDX::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

cPixelFormat *cPixelFormatsManagerDX::GetARGB(uint32_t totalBits, uint32_t alphaBits, uint32_t redBits, uint32_t greenBits, uint32_t blueBits)
{
    return Lookup<cPixelFormatARGB, kIidARGB, FormatClass::ARGB>(this, totalBits, alphaBits, redBits, greenBits, blueBits);
}

cPixelFormat *cPixelFormatsManagerDX::GetAlphaLuminance(uint32_t totalBits, uint32_t alphaBits, uint32_t luminanceBits)
{
    return Lookup<cPixelFormatAlphaLuminance, kIidAlphaLuminance, FormatClass::AlphaLuminance>(this, totalBits, alphaBits, luminanceBits);
}

cPixelFormat *cPixelFormatsManagerDX::GetABGRFloat(uint32_t alphaBits, uint32_t blueBits, uint32_t greenBits, uint32_t redBits)
{
    return Lookup<cPixelFormatABGRFloat, kIidABGRFloat, FormatClass::ABGRFloat>(this, alphaBits, blueBits, greenBits, redBits);
}

cPixelFormat *cPixelFormatsManagerDX::GetYUV(const uint8_t *fourcc)
{
    return Lookup<cPixelFormatYUV, kIidYUV, FormatClass::YUV>(this, uint32_t(fourcc[0]), uint32_t(fourcc[1]), uint32_t(fourcc[2]), uint32_t(fourcc[3]));
}

cPixelFormat *cPixelFormatsManagerDX::GetDXT(uint32_t version)
{
    return Lookup<cPixelFormatDXT, kIidDXT, FormatClass::DXT>(this, version);
}

cPixelFormat *cPixelFormatsManagerDX::GetDepthStencil(uint32_t totalBits, uint32_t depthBits, uint32_t stencilBits, uint32_t floatDepth)
{
    return Lookup<cPixelFormatDepthStencil, kIidDepthStencil, FormatClass::DepthStencil>(this, totalBits, depthBits, stencilBits, uint8_t(floatDepth));
}

} // namespace nGZGraphic4
