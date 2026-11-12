// nTSSG::cLotImposterManager: life cycle, COM slots, message face and definitions (lane 261108j; sg/cLotImposterManager.h).
#include "sg/cLotImposterManager.h"

#include "app/AppInit.h"
#include "game/original.h"
#include "game/owned.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "gz/cIGZUnknown.h"
#include "misc/UtilitiesC.h" // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/StaticGetters.h"
#include "rz/ServiceGetters.h" // CachedServiceGetter (the persist system)
#include "rz/Singleton_20ddfb.h" // GetCachedSingleton
#include "rz/StaticGetters2.h" // GetCachedSlot23
#include "rz/RZWildcard.h"
#include "rz/cRZFile.h"
#include "ts/cTSCatalog.h" // GZStringList

#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <xmmintrin.h>

TS2_OWNED_STATIC(const OwnedVTable, dbb3c4); // the manager
TS2_OWNED_STATIC(const OwnedVTable, dbb3f0); // its cIGZMessageTarget2 face
TS2_OWNED_STATIC(const OwnedVTable, dbb480); // cObjectImposterCommand (row LT2b2)

namespace nTSSG {
namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// A virtual base of an object built with virtual inheritance: the virtual-base table pointer at +vbptr, entry `entry`.
void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

using ThisFn = void *(__thiscall *)(void *);

} // namespace

cVirtualBaseRef::~cVirtualBaseRef()
{
    if (mObject) Call<2>(VirtualBase(mObject, 4, 1));
}

void cVirtualBaseRef::Reset()
{
    void *const old = mObject;
    if (!old) return;
    mObject = nullptr;
    Call<2>(VirtualBase(old, 4, 1));
}

cSliceImposterCreatorMember::cSliceImposterCreatorMember() { OriginalFunction<ThisFn, 0xb51100>()(this); }
cSliceImposterCreatorMember::~cSliceImposterCreatorMember() { OriginalFunction<ThisFn, 0xb51300>()(this); }
void cSliceImposterCreatorMember::Release() { OriginalFunction<ThisFn, 0xb518a0>()(this); }

void cImposterDefinitions::Clear() { mDefinitions.clear(); }

// ---- lane 261111u: the definitions command's helpers (row LT2a2a) ----

cImposterDefinition::cImposterDefinition(const char *model) { name.s.mString.assign(model ? model : ""); }

cImposterDefinition *__fastcall cImposterDefinition::Construct(cImposterDefinition *self, void *, const char *model)
{
    return new (self) cImposterDefinition(model);
}

void __fastcall cImposterDefinition::Destruct(cImposterDefinition *self, void *) { self->~cImposterDefinition(); }

cImposterDefinition &cImposterDefinition::operator=(const cImposterDefinition &other)
{
    name = other.name;
    if (&entries != &other.entries) entries = other.entries;
    return *this;
}

void cImposterDefinition::AddEntry(const char *a, const char *b, const char *c, const char *d)
{
    if (!a || !b || !c || !d) return;
    const cImposterStringPair key(a, b);
    const cImposterStringPair value(c, d);
    cImposterStringPair &entry = entries[key];
    entry.first = value.first;
    entry.second = value.second;
}

bool cImposterDefinition::Find(const cImposterStringPair *key, cImposterStringPair *out)
{
    const auto it = entries.find(*key);
    if (it == entries.end()) return false;
    out->first = it->second.first;
    out->second = it->second.second;
    return true;
}

void cImposterDefinitions::Define(const char *name, const char *model)
{
    const cImposterString key(name);
    if (mDefinitions.find(key) != mDefinitions.end()) return;
    const cImposterDefinition definition(model);
    mDefinitions.try_emplace(key).first->second = definition;
}

void cImposterDefinitions::Define(const char *name, const char *a, const char *b, const char *model, const char *c,
                                  const char *d)
{
    const cImposterString key(name);
    const auto it = mDefinitions.find(key);
    if (it != mDefinitions.end()) {
        it->second.AddEntry(a, b, c, d);
        return;
    }
    cImposterDefinition definition(model);
    definition.AddEntry(a, b, c, d);
    mDefinitions.try_emplace(key).first->second = definition;
}

bool cImposterDefinitions::Map(cDesignModes *loaded, cDesignModes *out)
{
    out->Clear();
    for (auto &[pattern, definition] : mDefinitions) {
        const char *const wildcard = pattern.s.mString.c_str();
        if (!RZWildcard::MatchCase(loaded->mTag.ToChar(), wildcard, 0)) continue;
        out->SetName(definition.name.s.mString.c_str(), false);
        for (const auto &mode : loaded->mModes) {
            cImposterStringPair from;
            from.first.s.mString = mode.first.mString;
            from.second.s.mString = mode.second.mString;
            cImposterStringPair to;
            if (definition.Find(&from, &to)) out->AddDesignMode(to.first.s.mString.c_str(), to.second.s.mString.c_str());
        }
        return true;
    }
    return false;
}

cLotImposterManager::cLotImposterManager() : mVTable(&ts2_rva_dbb3c4), mFace{&ts2_rva_dbb3f0}
{
    mMessageIds.insert(kMessageGenerate);
}

cLotImposterManager::~cLotImposterManager()
{
    mVTable = &ts2_rva_dbb3c4;
    mFace.mVTable = &ts2_rva_dbb3f0;
}

cLotImposterManager *__cdecl cLotImposterManager::Create()
{
    void *memory = ::operator new(kSize);
    if (!memory) return nullptr;
    return new (memory) cLotImposterManager();
}

bool cLotImposterManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == 1 || iid == kIid) {
        *out = this;
    } else if (iid == kIidMessageTarget2) {
        *out = &mFace;
    } else {
        return false;
    }
    Call<1>(this);
    return true;
}

uint32_t cLotImposterManager::Release()
{
    if (--mRefCount) return mRefCount;
    Call<8>(this, 1u);
    return 0;
}

bool cLotImposterManager::Init()
{
    if (mInitialised) return false;
    mInitialised = true;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(&mFace), nullptr);
    mPlacement.Assign(static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()()));
    LoadDefinitions();
    return true;
}

bool cLotImposterManager::Shutdown()
{
    if (!mInitialised) return false;
    mInitialised = false;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(&mFace), nullptr);
    mDefinitions.Clear();
    ReleaseGenerationState();
    ReleaseGenerationServices();
    mPlacement.Reset();
    return true;
}

void cLotImposterManager::ReleaseGenerationState()
{
    for (int i = 4; i < 8; ++i) m03c[i].Reset();
    m05c.clear();
    m068.clear();
    m074.clear();
    mSliceCreator.Release();
}

void cLotImposterManager::ReleaseGenerationServices()
{
    m038.Reset();
    for (int i = 0; i < 4; ++i) m03c[i].Reset();
}

int32_t cLotImposterManager::SliceCount() const
{
    return (static_cast<int32_t>(m0a4) - 2 * static_cast<int32_t>(m150)) / static_cast<int32_t>(m14c);
}

float cLotImposterManager::SliceCountF() const { return static_cast<float>(SliceCount()); }

bool cLotImposterManager::HasSliceBorder() const { return m198[2] != 0 && static_cast<int32_t>(m150) > 0; }

cRZString *__cdecl cLotImposterManager::SnapshotName(cRZString *out, int32_t n, int32_t direction)
{
    out->Construct();
    const char *format;
    switch (direction) {
    case 0: format = "LotSnapshot_W_%d"; break;
    case 1: format = "LotSnapshot_S_%d"; break;
    case 2: format = "LotSnapshot_E_%d"; break;
    case 3: format = "LotSnapshot_N_%d"; break;
    default: format = "LotSnapshot_%d"; break;
    }
    out->Sprintf(format, n);
    return out;
}

// ---- row LT2b2a (lane 261111r) ----

namespace {

// The world database's slots (nTSWorld::cIWorldDB).
enum : int {
    kGetElevation = 62, kGetGroundElevation = 64, kGetMinX = 81, kGetMinY = 82, kGetMaxX = 83, kGetMaxY = 84,
    kGetTileSize = 87, kGetMinLevel = 88, kGetMaxLevel = 89, kGetWallHeight = 95,
};

// The nearest integer by the 2^23 + 2^22 trick on f - 0.5, moved up when n + 1 converts to exactly f.
int32_t FloorIndex(float f)
{
    const float shifted = (f - 0.5f) + 12582912.0f;
    uint32_t bits;
    std::memcpy(&bits, &shifted, 4);
    int32_t n = static_cast<int32_t>(bits + 0xb4c00000u);
    if (static_cast<float>(n + 1) == f) n = n + 1;
    return n;
}

} // namespace

uint32_t __cdecl cLotImposterManager::ClassId() { return 0x0bc4a38d; }
uint32_t __cdecl LotImposterClassId() { return 0x4d892b0b; }
const char *cObjectImposterCommandName::GetName() { return "objectImposter"; }

void cLotImposterManager::ClearAtlases() { mAtlases.clear(); }

float cLotImposterManager::GroundHeightAt(const float *xy)
{
    void *const world = mPlacement.Get();
    const float tile = Call<kGetTileSize, float>(world);
    const int32_t x = FloorIndex(static_cast<float>(static_cast<double>(xy[0] / tile) + 0.5));
    const int32_t y = FloorIndex(static_cast<float>(static_cast<double>(xy[1] / tile) + 0.5));
    return Call<kGetGroundElevation, float>(mPlacement.Get(), x, y);
}

bool cLotImposterManager::IsFlat()
{
    const int32_t width = mLotMaxX - mLotMinX + 1;
    const int32_t height = mLotMaxY - mLotMinY + 1;
    const float ref = Call<kGetGroundElevation, float>(mPlacement.Get(), 0, 0);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const double h = Call<kGetGroundElevation, float>(mPlacement.Get(), x, y);
            const float d = static_cast<float>(h - static_cast<double>(ref));
            if (static_cast<double>(std::fabs(d)) > 0.1) return false;
        }
    }
    return true;
}

void cLotImposterManager::KeepHighest(const cHeightKey *key, float height)
{
    auto [it, inserted] = mHeights.try_emplace(*key, height);
    if (!inserted && height > it->second) it->second = height;
}

void cLotImposterManager::AddHeight(const float *p, bool given)
{
    const cHeightKey key{p[0], p[1]};
    const float height = given ? p[2] : GroundHeightAt(&key.x);
    KeepHighest(&key, height);
}

void cLotImposterManager::ComputeLotBounds()
{
    mLotMinX = Call<kGetMinX, int32_t>(mPlacement.Get());
    mLotMaxX = Call<kGetMaxX, int32_t>(mPlacement.Get()) + 1;
    mLotMinY = Call<kGetMinY, int32_t>(mPlacement.Get());
    int32_t maxY = Call<kGetMaxY, int32_t>(mPlacement.Get()) + 1;
    mLotMinX += m178[0];
    mLotMaxX -= m178[2];
    maxY -= m178[1];
    mLotMinY += m178[3];
    mLotMaxY = maxY;

    void *const service = GetCachedSlot23();
    if (Call<5>(service) == 0x0bea28c2 || Call<5>(service) == 0x2c1805b0 || Call<5>(service) == 0xcc181346) {
        if (void *const lot = reinterpret_cast<void *>(Call<9>(service))) {
            void *const view = reinterpret_cast<void *>(Call<4>(lot));
            if (Call<14, bool>(view, 0)) mLotMinX += 10;
            if (Call<14, bool>(view, 2)) mLotMaxX -= 10;
            if (Call<14, bool>(view, 3)) mLotMinY += 10;
            if (Call<14, bool>(view, 1)) mLotMaxY -= 10;
        }
    }

    Call<kGetMinLevel, int32_t>(mPlacement.Get());
    const int32_t level = Call<kGetMaxLevel, int32_t>(mPlacement.Get());
    const float wallHeight = Call<kGetWallHeight, float>(mPlacement.Get());
    for (int32_t y = mLotMinY; y <= mLotMaxY; ++y) {
        for (int32_t x = mLotMinX; x <= mLotMaxX; ++x) {
            const cImposterTile tile{x, y, level};
            Call<kGetElevation, float>(mPlacement.Get(), &tile);
        }
    }
    mMinHeight = 0.0f;
    mMaxHeight = 0.0f;
    for (int32_t y = mLotMinY; y <= mLotMaxY; ++y) {
        for (int32_t x = mLotMinX; x <= mLotMaxX; ++x) {
            const float e = Call<kGetGroundElevation, float>(mPlacement.Get(), x, y);
            mMinHeight = mMinHeight < e ? mMinHeight : e;
            mMaxHeight = mMaxHeight > e ? mMaxHeight : e;
        }
    }
    mMaxHeight = static_cast<float>(level) * wallHeight + mMaxHeight;
}

bool __stdcall cLotImposterManager::CurrentLotName(void *out)
{
    void *const lot = reinterpret_cast<void *>(Call<9>(GetCachedSlot23()));
    if (!lot) return false;
    Call<17>(out, Call<19>(lot));
    return true;
}

namespace {
// The image data's slots (nGZGraphic4::cIImageData, sg/cImageData.h).
enum : int {
    kSetPixelOpaque = 19, kSetPixel = 20, kLevelWidth = 26, kLevelHeight = 27, kGetPixelRGB = 38, kGetPixel = 39,
};
// The nearest integer to f by the 2^23 + 2^22 trick (exact for |f| below 2^22), less `minus`.
int32_t RoundTrick(float f, uint32_t minus)
{
    const float shifted = f + 12582912.0f;
    uint32_t bits;
    std::memcpy(&bits, &shifted, 4);
    return static_cast<int32_t>(bits + 0xb4c00000u - minus);
}
} // namespace

void __cdecl cLotImposterManager::ScaleImage(void *image, int32_t which)
{
    void *const system = OriginalFunction<void *(__cdecl *)(), 0x20defb>()();
    void *const source = reinterpret_cast<void *>(Call<48>(system));
    const float value = Call<23, float>(source, which);
    const float scale = static_cast<float>(1.0 / static_cast<double>(value));
    const int32_t width = Call<kLevelWidth, int32_t>(image, 0);
    const int32_t height = Call<kLevelHeight, int32_t>(image, 0);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            float pixel[4];
            const float *const p = Call<kGetPixelRGB, float *>(image, pixel, x, y, 0);
            const float r = p[0], g = p[1], b = p[2];
            // The channel first (a NaN channel's payload wins over a NaN scale's), so not left to the compiler.
            const __m128 s = _mm_set_ss(scale);
            float scaled[3];
            scaled[0] = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(r), s));
            scaled[1] = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(g), s));
            scaled[2] = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(b), s));
            Call<kSetPixelOpaque>(image, x, y, static_cast<const float *>(scaled), 0);
        }
    }
}

void cLotImposterManager::CopySliceBorders(int32_t index)
{
    if (m198[1] == 0 || static_cast<int32_t>(m150) <= 0) return; // +0x199 (HasSliceBorder reads +0x19a)
    void *const image = m05c[index].Get();
    const int32_t width = Call<kLevelWidth, int32_t>(image, 0);
    const int32_t height = Call<kLevelHeight, int32_t>(image, 0);
    for (size_t i = 0; i < m140.size(); ++i) {
        const auto &slice = *reinterpret_cast<const cImposterSliceRecord *>(m140[i].data());
        if (slice.mTexture != index) continue;
        const int32_t x0 = RoundTrick(static_cast<float>(width) * slice.mU0, 0);
        const int32_t x1 = RoundTrick(static_cast<float>(width) * slice.mU1, 1);
        const int32_t y0 = RoundTrick(static_cast<float>(height) * slice.mV0, 0);
        const int32_t y1 = RoundTrick(static_cast<float>(height) * slice.mV1, 1);
        if (x0 >= x1 || y0 >= y1) continue;
        for (int32_t ring = 1; ring <= static_cast<int32_t>(m150); ++ring) {
            const int32_t left = x0 - ring < 0 ? 0 : x0 - ring;
            const int32_t right = x1 + ring > width - 1 ? width - 1 : x1 + ring;
            const int32_t top = y0 - ring < 0 ? 0 : y0 - ring;
            const int32_t bottom = y1 + ring > height - 1 ? height - 1 : y1 + ring;
            for (int32_t x = left + 1; x < right; ++x) {
                float pixel[4];
                Call<kSetPixel>(image, x, bottom, static_cast<const float *>(Call<kGetPixel, float *>(image, pixel, x, y1, 0)), 0);
                float pixel2[4];
                Call<kSetPixel>(image, x, top, static_cast<const float *>(Call<kGetPixel, float *>(image, pixel2, x, y0, 0)), 0);
            }
            for (int32_t y = top; y <= bottom; ++y) {
                float pixel[4];
                Call<kSetPixel>(image, left, y, static_cast<const float *>(Call<kGetPixel, float *>(image, pixel, x0, y, 0)), 0);
                float pixel2[4];
                Call<kSetPixel>(image, right, y, static_cast<const float *>(Call<kGetPixel, float *>(image, pixel2, x1, y, 0)), 0);
            }
        }
    }
}

void cLotImposterManager::PlaceSlice(void *record)
{
    auto *const slice = static_cast<cImposterSliceRecord *>(record);
    const int32_t border = static_cast<int32_t>(m150) * 2;
    const int32_t width = slice->mColumns * static_cast<int32_t>(m14c) + border;
    const int32_t rows = slice->RowCount();
    const int32_t height = rows * static_cast<int32_t>(m14c) + static_cast<int32_t>(m150) * 2;
    slice->mTexture = -1;
    size_t atlas = 0;
    int32_t node = -1;
    for (; atlas < mAtlases.size(); ++atlas) {
        node = mAtlases[atlas].Allocate(width, height);
        if (node != -1) break;
    }
    if (node == -1) {
        atlas = mAtlases.size();
        mAtlases.resize(atlas + 1);
        mAtlases.back().Init(static_cast<int32_t>(m0a4), static_cast<int32_t>(m0a4));
        node = mAtlases.back().Allocate(width, height);
        if (node == -1) {
            mAtlases.pop_back();
            return;
        }
    }
    int32_t rect[4];
    std::memcpy(rect, &mAtlases[atlas].mNodes[node].rect, sizeof(rect));
    const int32_t inset = static_cast<int32_t>(m150);
    slice->mTexture = static_cast<int32_t>(atlas);
    const float size = static_cast<float>(static_cast<int32_t>(m0a4));
    slice->mU0 = static_cast<float>(rect[0] + inset) / size;
    slice->mV0 = static_cast<float>(rect[1] + inset) / size;
    slice->mU1 = static_cast<float>(rect[2] - inset) / size;
    slice->mV1 = static_cast<float>(rect[3] - inset) / size;
}

namespace {
// A cRZString on the stack built from text and a length (the game's inline form: vtable, the std::string, refcount 0),
// destroyed by cRZString's destructor (0x141193).
struct LocalString {
    union {
        cRZString s;
    };
    LocalString(const char *text, uint32_t length)
    {
        s.mVTable = cRZString::VTable();
        std::construct_at(&s.mString, text, length);
        s.mRefCount = 0;
    }
    explicit LocalString(const char *text) : LocalString(text, static_cast<uint32_t>(std::strlen(text))) {}
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { s.Destruct(); }
};

void *PersistSystem() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); }
} // namespace

bool __stdcall cLotImposterManager::OpenLotResource(void *name, void *, uint32_t *group, void **out)
{
    void *const persist = PersistSystem();
    void *const app = reinterpret_cast<void *>(Call<6>(GetCachedSingleton()));
    void *const lot = app ? reinterpret_cast<void *>(Call<56>(app, name))
                          : reinterpret_cast<void *>(Call<9>(GetCachedSlot23()));
    if (!lot) return false;
    void *const lotName = reinterpret_cast<void *>(Call<8>(lot));
    const uint32_t length = Call<7>(lotName);
    const auto *const text = reinterpret_cast<const char *>(Call<6>(lotName));
    LocalString nameString(text, length);
    const uint32_t id = Call<11>(persist, &nameString.s);
    *group = id;
    const uint32_t key[4] = {0xe519c933u, id, 1, 0};
    return (Call<34>(persist, key, 0x656b8f0cu, out) & 0xff) != 0;
}

void cLotImposterManager::LoadLotResources(void *name)
{
    void *opened = nullptr;
    void *const current = reinterpret_cast<void *>(Call<8>(reinterpret_cast<void *>(Call<6>(GetCachedSlot23()))));
    const uint32_t length = Call<7>(current);
    const auto *const text = reinterpret_cast<const char *>(Call<6>(current));
    uint32_t group;
    bool ok;
    {
        LocalString lotName(text, length);
        ok = OpenLotResource(name, &lotName.s, &group, &opened);
    }
    if (ok) {
        void *const resources = OriginalFunction<void *(__cdecl *)(), 0x20deb5>()();
        const uint32_t key[4] = {0xac506764u, group, 0x486d, 0};
        void *list = nullptr;
        if (Call<5>(resources, key, 0x6cd04176u, &list, 0, 0) & 0xff) {
            struct Range {
                const uint8_t *begin, *end;
            };
            auto keys = [&] { return reinterpret_cast<const Range *>(Call<7>(list)); };
            const Range *range = keys();
            if ((range->end - range->begin) >> 4) {
                for (uint32_t i = 0;;) {
                    Call<10>(resources, keys()->begin + i * 16);
                    Call<26>(opened, keys()->begin + i * 16);
                    ++i;
                    range = keys();
                    if (i >= static_cast<uint32_t>((range->end - range->begin) >> 4)) break;
                }
            }
        }
        Call<26>(opened, key);
        if (list) Call<2>(list);
    }
    if (opened) Call<2>(opened);
}

void cLotImposterManager::SetImposterName(const char *text)
{
    void *const app = reinterpret_cast<void *>(Call<6>(GetCachedSingleton()));
    void *lot;
    if (app) {
        LocalString name(text);
        lot = reinterpret_cast<void *>(Call<56>(app, &name.s));
    } else {
        lot = reinterpret_cast<void *>(Call<9>(GetCachedSlot23()));
    }
    if (!lot) return;
    Call<16>(&m080.s, Call<8>(lot));
    m080.s.mString.append(m01c ? "!" : "_", 1);
}

// ---- lane 261111s (LT2b2a's left rows, row LT2b2) ----

cDesignModes *__fastcall ConstructDesignModes(cDesignModes *self, void *) { return new (self) cDesignModes(); }

bool cLotImposterManager::ApplyDefinition(void *object, cDesignModeNode *target)
{
    auto *const unknown = static_cast<cIGZUnknown *>(object);
    if (target->SetFromObject(unknown)) return true;
    cDesignModes loaded;
    cDesignModes mapped;
    loaded.LoadFromObject(unknown);
    if (!mDefinitions.Map(&loaded, &mapped)) return false;
    return target->Take(&mapped);
}

namespace {
// The director framework's slot 3: make an object of class `cls`, its `iid` face into *out.
enum : int { kFrameworkCreate = 3 };
// The generation services' wiring slots: cIGeometryNode::AddPart, cIShape::AddPart (part, lod), cIShapeRefNode::AddShape,
// cICompositionTreeNode::AddChild.
enum : int { kGeometryAddPart = 32, kShapeAddPart = 41, kShapeRefAddShape = 94, kTreeAddChild = 45 };
void *VirtualBaseOrNull(void *object) { return object ? VirtualBase(object, 4, 1) : nullptr; }
} // namespace

void cLotImposterManager::CreateGenerationServices()
{
    struct Made {
        fx::CountedRef<cIGZUnknown> *slot;
        uint32_t cls, iid;
    };
    void *framework = GetDirectorFrameworkObject();
    m038.Reset();
    Call<kFrameworkCreate>(framework, 0x9bffc10du, 0x9bffc101u, &m038.mObject);
    const Made made[] = {{&m03c[0], 0x7ba3838cu, 0x5ba38381u},
                         {&m03c[2], 0x65245517u, 0xa516cd10u},
                         {&m03c[1], 0xfc6eb1f7u, 0x3c6eb1f0u},
                         {&m03c[3], 0xe519c933u, 0xe519c92cu}};
    for (const Made &m : made) {
        framework = GetDirectorFrameworkObject();
        m.slot->Reset();
        Call<kFrameworkCreate>(framework, m.cls, m.iid, m.slot->Address());
    }
    Call<kGeometryAddPart>(m03c[0].Get(), VirtualBaseOrNull(m038.mObject));
    cVirtualBaseRef bounds;
    framework = GetDirectorFrameworkObject();
    bounds.Reset();
    Call<kFrameworkCreate>(framework, 0x1cfeceb8u, 0x3cfeceb2u, &bounds.mObject);
    Call<kGeometryAddPart>(m03c[0].Get(), VirtualBaseOrNull(bounds.mObject));
    Call<kShapeAddPart>(m03c[1].Get(), m03c[0].Get(), 0u);
    Call<kShapeRefAddShape>(m03c[2].Get(), m03c[1].Get());
    Call<kTreeAddChild>(m03c[3].Get(), m03c[2].Get());
}

void cLotImposterManager::LoadNamedLot(const char *text)
{
    LocalString name(text);
    LoadLotResources(&name.s);
}

void cLotImposterManager::LoadCurrentLot()
{
    LocalString name("", 0);
    if (CurrentLotName(&name.s)) LoadLotResources(&name.s);
}

void cObjectImposterCommand::Execute(nGZCommandParser::cIArguments *args)
{
    auto *const definitions = static_cast<cImposterDefinitions *>(mState);
    try {
        int32_t count = 0;
        const auto *const a = Call<10, const char *const *>(args, &count, 2u, 6u); // the arguments' range
        if (count == 2) {
            definitions->Define(a[0], a[1]);
        } else if (count == 6) {
            definitions->Define(a[0], a[1], a[2], a[3], a[4], a[5]);
        }
    } catch (const nGZCommandParser::cError &) {
    }
}

uint8_t __cdecl AddImposterDesc(void *, void *value, std::vector<PersistKey, GZServiceAllocator<PersistKey>> *keys)
{
    void *desc = nullptr;
    Call<kFrameworkCreate>(GetDirectorFrameworkObject(), 0xcd8b6498u, 0xed8b648bu, &desc);
    void *face = nullptr;
    uint8_t result = 0;
    if (desc && (Call<0>(desc, 0x456b8f1du, &face) & 0xff)) {
        Call<4>(desc, 1u);
        Call<9>(face, value);
        PersistKey key;
        Call<3>(face, &key); // its resource key
        keys->push_back(key);
        void *const persist = PersistSystem();
        Call<17>(persist, &key);
        result = static_cast<uint8_t>(Call<16>(persist, face, 0u)); // the answer's low byte, as the original keeps it
    }
    if (face) Call<2>(face);
    if (desc) Call<2>(desc);
    return result;
}

cLotImposterManager *cLotImposterManager::DeletingDestructor(uint32_t flags)
{
    this->~cLotImposterManager();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cLotImposterManager::MessageFace::DoMessage(void *message)
{
    if (!message) return false;
    void *holder = nullptr;
    Call<0>(message, 0x65297976u, &holder);
    bool handled = false;
    if (Call<4>(message) == kMessageGenerate) {
        cLotImposterManager *owner = Owner();
        if (owner->mGenerateOnMessage) Call<5>(owner);
        handled = true;
    }
    if (holder) Call<2>(holder);
    return handled;
}

void cLotImposterManager::LoadDefinitions()
{
    mDefinitions.Clear();
    GZStringList files;
    appinit::CommonImplSlot5(kDefinitionsOption, Word(&files), 1, 0, 0xffffffff);
    for (auto it = files.rbegin(); it != files.rend(); ++it) {
        cImposterString path(it->c_str());
        if (!cRZFile::PathExists(*path.s.AsInterface())) continue;
        void *parser = nullptr;
        void *framework = GetDirectorFrameworkObject();
        Call<3>(framework, kParserClass, kParserIid, &parser);
        Call<0>(parser, 0u);
        Call<3>(parser, &mDefinitions);
        void *const memory = ::operator new(sizeof(nGZCommandParser::cCommandT));
        void *command = nullptr;
        if (memory) {
            command = static_cast<nGZCommandParser::cCommand *>(memory)->Construct();
            *static_cast<const void **>(memory) = &ts2_rva_dbb480;
        }
        Call<4>(parser, command);
        Call<12>(parser, path.s.ToChar(), 0u);
        Call<1>(parser);
        if (parser) Call<2>(VirtualBase(parser, 4, 1));
    }
}

} // namespace nTSSG
