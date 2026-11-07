#include "rz/RZMessageServer.h"
#include "ts/cTSCatalog.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "rz/RZMemory.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZFile.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"

#include <algorithm>
#include <cstring>
#include <new>

// cTSCatalog (lane IK). Every call the original makes through a product's, a parser's or a service's vtable is made the same
// way and in the same order; the containers are the real ones (the game's STL is the same MSVC 14.40 library).

namespace {

// A call through slot kSlot of the vtable at `object` (thiscall), with the result type of the original's use.
template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// Face `index` of a product: its virtual base table pointer is at +4 and the faces are at +4 + vbtable[index].
void *Face(const void *product, int index)
{
    auto *base = static_cast<const uint8_t *>(product) + 4;
    const int32_t *table = *reinterpret_cast<const int32_t *const *>(base);
    return const_cast<uint8_t *>(base + table[index]);
}

// The same face, or null for a null product (the original's null-checked conversion).
void *FaceOrNull(const void *product, int index) { return product ? Face(product, index) : nullptr; }

// Face `index` of a resource parser (its virtual base table pointer at +8).
void *ParserFace(const void *parser, int index)
{
    auto *base = static_cast<const uint8_t *>(parser) + 8;
    const int32_t *table = *reinterpret_cast<const int32_t *const *>(base);
    return const_cast<uint8_t *>(base + table[index]);
}

// The identity face's key (face 3 slot 2): a product's guid, or its name text for patterns and terrain paints.
uint32_t GuidOf(const void *product) { return Call<2>(Face(product, 3)); }
const char *NameOf(const void *product) { return Call<2, const char *>(Face(product, 3)); }
// The wall-part face's key (face 4 slot 23).
uint32_t PartKeyOf(const void *product) { return Call<23>(Face(product, 4)); }

// `new T` of a class that is still the game's: operator new, then the original constructor (when the memory is not null); the
// memory is freed when the constructor throws, as the original's frame does.
template <typename Construct> void *NewGameObject(size_t size, Construct construct)
{
    void *memory = ::operator new(size);
    if (!memory) return nullptr;
    try {
        return construct(memory);
    } catch (...) {
        ::operator delete(memory, size);
        throw;
    }
}
using ConstructFn = void *(__thiscall *)(void *, uint32_t);

// A resource parser's lifetime in a loader: referenced (face 1), Begin(0) (face 2 slot 0); End (face 2 slot 1) and the release
// are explicit in each loader, where the original makes them.
void ParserAddRef(void *parser)
{
    if (parser) Call<1>(ParserFace(parser, 1));
}
void ParserBegin(void *parser) { Call<0>(ParserFace(parser, 2), 0u); }
void ParserEnd(void *parser) { Call<1>(ParserFace(parser, 2)); }
void ParserRelease(void *parser) { Call<2>(ParserFace(parser, 1)); }
// The parser's own slots: 5 GetResource(index, type, &ref), 6 GetCount, 9 GetDefault(type, &ref), 10 GetMaterialValue(0).
void GetResource(void *parser, int32_t index, uint32_t type, void **out) { Call<5>(parser, index, type, out); }
int32_t GetCount(void *parser) { return Call<6, int32_t>(parser); }
bool GetDefault(void *parser, uint32_t type, void **out) { return Call<9, bool>(parser, type, out); }
uint32_t GetMaterialValue(void *parser) { return Call<10>(parser, 0u); }

// Each file of the list that exists (cRZFile::PathExists on a cRZString of its name), in reverse order unless `forward`: the
// parser reads it (0xc13038, still the game's: the parser's face 2 slot 9 then slot 12(path, 0) under a catch of the game's
// exception base), then perFile runs.
// afterEach runs after every file of the list, read or not.
struct Nothing {
    void operator()() const {}
};
template <typename PerFile, typename AfterEach = Nothing>
void ForEachExistingFile(void *parser, GZStringList *files, bool forward, PerFile &&perFile, AfterEach &&afterEach = {})
{
    auto visit = [&](const GZString &entry) {
        GZString name(entry);
        cRZString path;
        path.Construct(name.c_str());
        const bool exists = cRZFile::PathExists(*path.AsInterface());
        path.Destruct();
        if (exists) {
            OriginalFunction<uint32_t(__cdecl *)(void *, const char *), 0xc13038>()(parser, name.c_str());
            perFile();
        }
        afterEach();
    };
    if (forward) {
        for (const GZString &entry : *files) visit(entry);
    } else {
        for (auto it = files->rbegin(); it != files->rend(); ++it) visit(*it);
    }
}

// The sort the loaders and slots 80 / 88 use (comparator 0xc0fb75): the products' face 2 (null for none) slot 20, signed, a's
// first. std::sort hands it the face-2 conversion of each element.
bool SortKeyLess(const ProductRef &a, const ProductRef &b)
{
    void *faceB = FaceOrNull(b.Get(), 2);
    void *faceA = FaceOrNull(a.Get(), 2);
    const int32_t keyA = Call<20, int32_t>(faceA);
    return keyA < Call<20, int32_t>(faceB);
}
void SortByKey(ProductVector &products)
{
    if (products.size() > 2) std::sort(products.begin(), products.end(), SortKeyLess);
}

// The pattern order (comparator 0xc143ab): _stricmp of the primary slot 11 names, b's name read first.
bool NameLess(const void *a, const void *b)
{
    const char *nameB = Call<11, const char *>(b);
    const char *nameA = Call<11, const char *>(a);
    return _stricmp(nameA, nameB) < 0;
}

// The service object's slot 5 into a fresh reference (null when there is no service object): the 37-byte getter 0x20db69.
void *QueryService(uint32_t id, uint32_t iid)
{
    void *out = nullptr;
    if (void *service = GetServiceObject()) Call<5>(service, id, iid, &out);
    return out;
}

void *CatalogService() { return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>(); } // 0xb390c0
void *SegmentService() { return CachedServiceGetter<0x10faf80, 0x0dc7057a, 0xcdb467ae>(); } // 0xb38fb0

// A product in a vector, or null when the index is past the end (slot 76's check).
template <typename Map, typename Key> void *FindIn(Map &map, const Key &key)
{
    auto it = map.find(key);
    return it == map.end() ? nullptr : it->second.Get();
}

// The first element of `products` that holds `product`, or end.
ProductVector::iterator FindProduct(ProductVector &products, void *product)
{
    auto it = products.begin();
    while (it != products.end() && it->Get() != product) ++it;
    return it;
}

// The resource keys slots 7 and 87 collect (16 bytes each), in the allocator service's memory.
struct ResourceKey {
    uint32_t words[4];
};
using ResourceKeyVector = std::vector<ResourceKey, GZServiceAllocator<ResourceKey>>;

constexpr uint32_t kPatternServiceId = 0x056b906e;
constexpr uint32_t kPatternServiceIid = 0x656b8efc;

} // namespace

bool NameLessIgnoringCase::operator()(const std::string &a, const std::string &b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }

// ---- ProductRef ----

void ProductRef::AddRefProduct(void *product) { Call<1>(Face(product, 1)); }
void ProductRef::ReleaseProduct(void *product) { Call<2>(Face(product, 1)); }

ProductRef::ProductRef(void *product) : mProduct(product)
{
    if (product) AddRefProduct(product);
}

ProductRef::~ProductRef()
{
    if (mProduct) ReleaseProduct(mProduct);
}

void ProductRef::Assign(void *product)
{
    if (product == mProduct) return;
    if (product) AddRefProduct(product);
    void *old = mProduct;
    mProduct = product;
    if (old) ReleaseProduct(old);
}

void **ProductRef::Reset()
{
    if (mProduct) {
        void *old = mProduct;
        mProduct = nullptr;
        ReleaseProduct(old);
    }
    return &mProduct;
}

// ---- cPatternParsingFilter ----

bool cPatternParsingFilter::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cPatternParsingFilter::AddRef() { return ++mRefCount; }

uint32_t cPatternParsingFilter::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

bool cPatternParsingFilter::IsKeyIncluded(const uint32_t *key)
{
    switch (key[0]) {
    case 0x2ac90e94:
    case 0x2cb230b8:
    case 0x4dcadb7e:
    case 0x6d619378:
    case 0xac8ce8fa:
    case 0xaca8ea06:
    case 0xcca8e925:
        return true;
    default:
        return false;
    }
}

// ---- cTSCatalog: lifetime and cIGZUnknown ----

cTSCatalog::cTSCatalog() = default;

cTSCatalog::~cTSCatalog()
{
    if (mInitialized) Shutdown();
}

cTSCatalog *cTSCatalog::Create() { return new cTSCatalog; }

bool cTSCatalog::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSCatalog::AddRef() { return ++mRefCount; }

uint32_t cTSCatalog::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// Registers the pattern product factory (cTSProductPatternFactory, 0xc bytes, still the game's) with the catalogue service's
// slot 3 object for the five pattern resource types.
void cTSCatalog::Init()
{
    if (mInitialized) return;
    void *service = CatalogService();
    void *factory = NewGameObject(0xc, [](void *memory) { return OriginalFunction<void *(__thiscall *)(void *), 0xc0e8c8>()(memory); });
    if (factory) Call<1>(factory);
    static constexpr uint32_t kTypes[] = {0xcca8e925, 0xaca8ea06, 0x2cb230b8, 0x6d619378, 0x4dcadb7e};
    for (uint32_t type : kTypes) {
        void *registry = Call<3, void *>(service);
        Call<20>(registry, 0xaca74f79u, type, factory);
    }
    mInitialized = true;
    if (factory) Call<2>(factory);
}

void cTSCatalog::Shutdown()
{
    mInitialized = false;
    if (mLoaded) Unload();
}

void cTSCatalog::Load()
{
    if (!mInitialized) Init();
    if (mLoaded) Unload();
    mNextId = 0;
    mIdByGuid.clear();
    mFenceKeys.clear();
    LoadWallsAndFences();
    ParseHeldPatternKeys();
    SortFences();

    auto list = [](GZStringList &files, uint32_t option) {
        appinit::CommonImplSlot5(option, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&files)), 1, 0, 0xffffffff);
    };
    GZStringList wallPatterns;
    list(wallPatterns, 0x6bd04212);
    LoadPatterns(1, &wallPatterns);
    GZStringList brushes;
    list(brushes, 0x4bd0420e);
    LoadTerrainBrushes(&brushes);
    GZStringList paints;
    list(paints, 0xebd0423e);
    LoadTerrainPaints(&paints);
    GZStringList floorPatterns;
    list(floorPatterns, 0x6bd0423b);
    LoadPatterns(0, &floorPatterns);
    AddObjectsFromDefinitions();
    GZStringList decks;
    list(decks, 0xabd04238);
    LoadDeckFoundations(&decks);
    GZStringList stairs;
    list(stairs, 0x2bd04237);
    LoadModularStairs(&stairs);
    GZStringList fireplaces;
    list(fireplaces, 0xabd04234);
    LoadFireplaces(&fireplaces);
    GZStringList awnings;
    list(awnings, 0xf03a7ee3);
    LoadAwnings(&awnings);
    GZStringList roofs;
    list(roofs, 0xebd04231);
    LoadRoofs(&roofs);
    GZStringList roofPatterns;
    list(roofPatterns, 0xcbd77a21);
    LoadRoofPatterns(&roofPatterns);
    GZStringList userTools;
    list(userTools, 0x8c7194f2);
    LoadUserToolProxies(&userTools);
    ApplyMaterialValues();
    mLoaded = true;
}

// Empties the catalogue. The terrain brushes and the guid -> id map are kept (the original leaves them; Load clears the map).
void cTSCatalog::Unload()
{
    mLoaded = false;
    mObjects.clear();
    mObjectsByDefinition.clear();
    mWallParts.clear();
    mWallPartsById.clear();
    mWalls.clear();
    mWallsById.clear();
    mFences.clear();
    mFencesById.clear();
    mDefaultFence.Assign(nullptr);
    mFenceKeys.clear();
    mFenceArches.clear();
    mFenceArchesById.clear();
    mDefaultFenceArch.Assign(nullptr);
    mDeckFoundations.clear();
    mDeckFoundationsById.clear();
    mStairs.clear();
    mStairsByGuid.clear();
    mFireplaces.clear();
    mFireplacesByKey.clear();
    mRoofs.clear();
    mRoofsByKey.clear();
    mRoofPatterns.clear();
    mRoofPatternsByKey.clear();
    mDefaultRoofPattern.Assign(nullptr);
    mExtras.clear();
    mRootExtras.clear();
    mExtrasByGuid.clear();
    mUserTools.clear();
    mUserToolsByKey.clear();
    for (int type = 0; type < kPatternTypes; ++type) {
        mPatterns[type].clear();
        mPatternsByName[type].clear();
        mDefaultPattern[type].Assign(nullptr);
    }
    mTerrainPaints.clear();
    mTerrainPaintsByName.clear();
}

// ---- pattern keys (slots 7, 87) ----

void cTSCatalog::ParsePatternKeysFrom(void *source)
{
    if (!source) return;
    void *holder = QueryService(kPatternServiceId, kPatternServiceIid);
    cPatternParsingFilter *filter = new cPatternParsingFilter;
    filter->AddRef();
    {
        ResourceKeyVector keys;
        Call<19>(source, &keys, static_cast<cIGZPersistResourceKeyFilter *>(filter));
        for (ResourceKey &key : keys) OriginalFunction<void(__cdecl *)(ResourceKey *, cTSCatalog *), 0xc130db>()(&key, this);
    }
    filter->Release();
    ReleaseHeldObject(holder);
}

void cTSCatalog::ParseHeldPatternKeys()
{
    void *holder = QueryService(kPatternServiceId, kPatternServiceIid);
    cPatternParsingFilter *filter = new cPatternParsingFilter;
    filter->AddRef();
    {
        ResourceKeyVector keys;
        Call<18>(holder, &keys, static_cast<cIGZPersistResourceKeyFilter *>(filter));
        for (ResourceKey &key : keys) OriginalFunction<void(__cdecl *)(ResourceKey *, cTSCatalog *), 0xc130db>()(&key, this);
    }
    filter->Release();
    ReleaseHeldObject(holder);
}

// ---- segments (slots 8, 9) ----

// True when every segment the object's resource has answers interface 0x3b2271bb with an object whose slot 6 is 1.
bool cTSCatalog::AllSegmentsOfKindOne(void *object)
{
    SegmentService();
    uint32_t resource;
    if (!Call<34, bool>(object, &resource)) return false;
    void *service = CatalogService();
    ServiceRefVector segments{};
    Call<48>(service, resource, &segments);
    void *answer = nullptr;
    bool result = true;
    const int32_t count = static_cast<int32_t>(segments.Size());
    for (int32_t i = 0; i < count; ++i) {
        void *segment = segments.mFirst[i];
        if (answer) {
            void *old = answer;
            answer = nullptr;
            Call<2>(old);
        }
        if (Call<0, bool>(segment, 0x3b2271bbu, &answer) && Call<6>(answer) != 1) {
            result = false;
            break;
        }
    }
    segments.ReleaseAndFree();
    if (answer) Call<2>(answer);
    return result;
}

// The first segment of the object's resource, when the segment service knows every segment's name (slot 11 into a cRZString,
// the service's slot 16).
bool cTSCatalog::FindFirstKnownSegment(void *object, void **out)
{
    uint32_t resource;
    if (!object || !Call<34, bool>(object, &resource)) return false;
    void *service = CatalogService();
    ServiceRefVector segments{};
    Call<48>(service, resource, &segments);
    bool result = false;
    const int32_t count = static_cast<int32_t>(segments.Size());
    if (count >= 1) {
        void *names = SegmentService();
        bool known = true;
        for (int32_t i = 0; i < count; ++i) {
            void *segment = segments.mFirst[i];
            cRZString name;
            name.Construct();
            Call<11>(segment, name.AsInterface());
            const bool ok = Call<16, bool>(names, name.AsInterface());
            name.Destruct();
            if (!ok) {
                known = false;
                break;
            }
        }
        if (known) {
            void *first = segments.mFirst[0];
            *out = first;
            Call<1>(first);
            result = true;
        }
    }
    segments.ReleaseAndFree();
    return result;
}

// ---- adding products ----

void cTSCatalog::AddPattern(uint32_t type, void *pattern)
{
    if (!Call<4, bool>(Face(pattern, 3))) {
        if (void *fallback = mDefaultPattern[type].Get()) {
            void *identity = Face(pattern, 3);
            Call<1>(identity, GuidOf(fallback));
        }
    }
    ProductVector &patterns = mPatterns[type];
    auto it = std::lower_bound(patterns.begin(), patterns.end(), pattern,
                               [](const ProductRef &element, void *value) { return NameLess(element.Get(), value); });
    if (it != patterns.end() && std::strlen(Call<11, const char *>(pattern)) != 0) {
        const char *name = Call<11, const char *>(pattern);
        if (std::strcmp(Call<11, const char *>(it->Get()), name) == 0 &&
            std::strcmp(Call<11, const char *>(pattern), GameAddress<const char>(0x10806a8)) != 0)
            return;
    }
    {
        ProductRef held(pattern);
        patterns.insert(it, held);
    }
    mPatternsByName[type][std::string(NameOf(pattern))].Assign(pattern);
}

bool cTSCatalog::AddObject(void *definition)
{
    if (!definition || Call<38, bool>(definition) || Call<37, bool>(definition)) return false;
    void *product = NewGameObject(0x120, [](void *memory) { return OriginalFunction<ConstructFn, 0xc15270>()(memory, 1); });
    ProductRef object(FaceOrNull(product, 3));
    void *face = object.Get();
    if (!face) return false;
    Call<0>(face, definition);
    Call<15>(Face(face, 2), Call<76>(definition));
    void *material = Face(face, 2);
    const bool kindOne = AllSegmentsOfKindOne(material);
    Call<7>(Face(face, 2), static_cast<uint32_t>(kindOne));
    mObjects.push_back(object);
    mObjectsByDefinition[static_cast<uint32_t>(reinterpret_cast<uintptr_t>(definition))].Assign(face);
    return true;
}

void cTSCatalog::AddRoofPattern(void *pattern)
{
    mRoofPatterns.emplace_back(pattern);
    mRoofPatternsByKey[Call<7>(pattern)].Assign(pattern);
}

void cTSCatalog::AddFence(void *fence, bool isDefault)
{
    if (mFenceKeys.count(PartKeyOf(fence))) return;
    mIdByGuid[GuidOf(fence)] = mNextId;
    Call<0>(Face(fence, 3), mNextId);
    mFences.push_back(ProductRef(fence));
    mFencesById[mNextId].Assign(fence);
    void *part = FaceOrNull(fence, 4);
    mWallParts.push_back(ProductRef(part));
    mWallPartsById[mNextId].Assign(part);
    if (isDefault) mDefaultFence.Assign(fence);
    ++mNextId;
    mFenceKeys.insert(PartKeyOf(fence));
}

void cTSCatalog::AddHalfWall(void *source)
{
    void *halfWall = NewGameObject(0x1b0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc170be>()(memory, 1); });
    ProductRef held(FaceOrNull(halfWall, 7));
    void *face = held.Get();
    Call<0>(face, FaceOrNull(source, 5));
    Call<3>(source, halfWall);
    mIdByGuid[GuidOf(face)] = mNextId;
    Call<0>(Face(face, 3), mNextId);
    mHalfWalls.push_back(held);
    mHalfWallsById[mNextId].Assign(face);
    void *part = FaceOrNull(face, 4);
    mWallParts.push_back(ProductRef(part));
    mWallPartsById[mNextId].Assign(part);
    ++mNextId;
}

void cTSCatalog::AddExtra(void *extra)
{
    if (mExtrasByGuid.count(GuidOf(extra))) return;
    mExtras.push_back(ProductRef(extra));
    mExtrasByGuid[GuidOf(extra)].Assign(extra);
    if (Call<7>(extra) == 0) mRootExtras.push_back(ProductRef(extra));
}

void cTSCatalog::AddTerrainPaint(void *paint)
{
    if (Call<29, bool>(Face(paint, 2))) {
        ProductRef held(paint);
        mTerrainPaints.insert(mTerrainPaints.begin(), held);
    } else {
        mTerrainPaints.push_back(ProductRef(paint));
    }
    mTerrainPaintsByName[std::string(Call<4, const char *>(paint))].Assign(paint);
}

// The objects of every object definition the definitions holder (GetCachedSingleton()'s slot 24) has, then sorted.
void cTSCatalog::AddObjectsFromDefinitions()
{
    void *definitions = Call<24, void *>(GetCachedSingleton());
    const int32_t count = Call<10, int32_t>(definitions);
    for (int32_t i = 0; i < count; ++i) AddObject(Call<13, void *>(definitions, i));
    SortByKey(mObjects);
}

// ---- counts, getters, finders ----

uint32_t cTSCatalog::GetObjectCount() { return static_cast<uint32_t>(mObjects.size()); }
void *cTSCatalog::GetObjectAt(uint32_t index) { return mObjects[index].Get(); }
void *cTSCatalog::FindObject(uint32_t definition) { return FindIn(mObjectsByDefinition, definition); }

bool cTSCatalog::RemoveObject(void *object)
{
    auto node = mObjectsByDefinition.find(Call<1>(object));
    if (node == mObjectsByDefinition.end()) return false;
    auto it = FindProduct(mObjects, object);
    if (it == mObjects.end()) return false;
    mObjectsByDefinition.erase(node);
    mObjects.erase(it);
    return true;
}

uint32_t cTSCatalog::GetWallPartCount() { return static_cast<uint32_t>(mWallParts.size()); }
void *cTSCatalog::GetWallPartAt(uint32_t index) { return mWallParts[index].Get(); }
void *cTSCatalog::FindWallPart(uint32_t id) { return FindIn(mWallPartsById, id); }

void *cTSCatalog::FindWallPartByKey(uint32_t key)
{
    for (auto &entry : mWallPartsById)
        if (Call<23>(entry.second.Get()) == key) return entry.second.Get();
    return nullptr;
}

uint32_t cTSCatalog::GetWallCount() { return static_cast<uint32_t>(mWalls.size()); }
void *cTSCatalog::GetWallAt(uint32_t index) { return mWalls[index].Get(); }
void *cTSCatalog::FindWall(uint32_t id) { return FindIn(mWallsById, id); }

void *cTSCatalog::FindWallByKey(uint32_t key)
{
    for (auto &entry : mWallsById)
        if (PartKeyOf(entry.second.Get()) == key) return entry.second.Get();
    return nullptr;
}

uint32_t cTSCatalog::GetFenceCount() { return static_cast<uint32_t>(mFences.size()); }
void *cTSCatalog::GetFenceAt(uint32_t index) { return mFences[index].Get(); }

void *cTSCatalog::FindFenceByKey(uint32_t key, bool orDefault)
{
    for (auto &entry : mFencesById)
        if (PartKeyOf(entry.second.Get()) == key) return entry.second.Get();
    return orDefault ? GetDefaultFence() : nullptr;
}

void *cTSCatalog::GetDefaultFence() { return mDefaultFence.Get(); }

bool cTSCatalog::RemoveFence(void *fence)
{
    auto node = mFencesById.find(GuidOf(fence));
    if (node == mFencesById.end()) return false;
    auto it = FindProduct(mFences, fence);
    if (it == mFences.end()) return false;
    mFencesById.erase(node);
    mFences.erase(it);
    return true;
}

uint32_t cTSCatalog::GetHalfWallCount() { return static_cast<uint32_t>(mHalfWalls.size()); }
void *cTSCatalog::GetHalfWallAt(uint32_t index) { return mHalfWalls[index].Get(); }

void *cTSCatalog::FindHalfWallByKey(uint32_t key)
{
    for (auto &entry : mHalfWallsById)
        if (PartKeyOf(entry.second.Get()) == key) return entry.second.Get();
    return nullptr;
}

uint32_t cTSCatalog::GetFenceArchCount() { return static_cast<uint32_t>(mFenceArches.size()); }
void *cTSCatalog::GetFenceArchAt(uint32_t index) { return mFenceArches[index].Get(); }

void *cTSCatalog::FindFenceArchByKey(uint32_t key, bool orDefault)
{
    for (auto &entry : mFenceArchesById)
        if (PartKeyOf(entry.second.Get()) == key) return entry.second.Get();
    return orDefault ? GetDefaultFenceArch() : nullptr;
}

void *cTSCatalog::GetDefaultFenceArch() { return mDefaultFenceArch.Get(); }

uint32_t cTSCatalog::GetDeckFoundationCount() { return static_cast<uint32_t>(mDeckFoundations.size()); }
void *cTSCatalog::GetDeckFoundationAt(uint32_t index) { return mDeckFoundations[index].Get(); }
void *cTSCatalog::FindDeckFoundation(uint32_t id) { return FindIn(mDeckFoundationsById, id); }

void *cTSCatalog::FindDeckFoundationByKey(uint32_t key)
{
    for (auto &entry : mDeckFoundationsById)
        if (Call<13>(entry.second.Get()) == key) return entry.second.Get();
    return nullptr;
}

uint32_t cTSCatalog::GetPatternCount(uint32_t type) { return static_cast<uint32_t>(mPatterns[type].size()); }
void *cTSCatalog::GetPatternAt(uint32_t type, uint32_t index) { return mPatterns[type][index].Get(); }

void *cTSCatalog::FindPattern(uint32_t type, const char *name, bool orDefault)
{
    if (!name) return nullptr;
    ProductNameMap &byName = mPatternsByName[type];
    auto it = byName.find(std::string(name));
    if (it != byName.end()) return it->second.Get();
    return orDefault ? GetDefaultPattern(type) : nullptr;
}

void *cTSCatalog::GetDefaultPattern(uint32_t type) { return mDefaultPattern[type].Get(); }

bool cTSCatalog::RemovePattern(void *pattern)
{
    const uint32_t type = Call<6>(pattern) == 1 ? 1 : 0;
    ProductNameMap &byName = mPatternsByName[type];
    auto node = byName.find(std::string(NameOf(pattern)));
    if (node == byName.end()) return false;
    auto it = FindProduct(mPatterns[type], pattern);
    if (it == mPatterns[type].end()) return false;
    byName.erase(node);
    mPatterns[type].erase(it);
    return true;
}

uint32_t cTSCatalog::GetTerrainBrushCount() { return static_cast<uint32_t>(mTerrainBrushes.size()); }
void *cTSCatalog::GetTerrainBrushAt(uint32_t index) { return mTerrainBrushes[index].Get(); }

void *cTSCatalog::FindTerrainBrush(const char *name)
{
    if (!name) return nullptr;
    return FindIn(mTerrainBrushesByName, std::string(name));
}

uint32_t cTSCatalog::GetTerrainPaintCount() { return static_cast<uint32_t>(mTerrainPaints.size()); }
void *cTSCatalog::GetTerrainPaintAt(uint32_t index) { return mTerrainPaints[index].Get(); }

void *cTSCatalog::FindTerrainPaint(const char *name)
{
    if (!name) return nullptr;
    return FindIn(mTerrainPaintsByName, std::string(name));
}

// Removes a terrain paint and tells the message server (message 0xec2b981 carrying the paint's face 1).
bool cTSCatalog::RemoveTerrainPaint(void *paint)
{
    auto node = mTerrainPaintsByName.find(std::string(NameOf(paint)));
    if (node == mTerrainPaintsByName.end()) return false;
    auto it = FindProduct(mTerrainPaints, paint);
    if (it == mTerrainPaints.end()) return false;
    void *message = nullptr;
    if (void *memory = RZMemory::Malloc(0x2c)) message = static_cast<cRZMessage2Standard *>(memory)->Construct();
    if (message) Call<1>(message);
    Call<5>(message, 0xec2b981u);
    Call<33>(message, Face(paint, 1));
    void *server = GetCachedMessageServer();
    Call<3>(server, message);
    mTerrainPaintsByName.erase(node);
    mTerrainPaints.erase(it);
    if (message) Call<2>(message);
    return true;
}

uint32_t cTSCatalog::GetStairsCount() { return static_cast<uint32_t>(mStairs.size()); }
void *cTSCatalog::GetStairsAt(uint32_t index) { return mStairs[index].Get(); }
void *cTSCatalog::FindStairs(uint32_t guid) { return FindIn(mStairsByGuid, guid); }

uint32_t cTSCatalog::GetFireplaceCount() { return static_cast<uint32_t>(mFireplaces.size()); }
void *cTSCatalog::GetFireplaceAt(uint32_t index) { return mFireplaces[index].Get(); }
void *cTSCatalog::FindFireplace(uint32_t key) { return FindIn(mFireplacesByKey, key); }

uint32_t cTSCatalog::GetAwningCount() { return static_cast<uint32_t>(mAwnings.size()); }
void *cTSCatalog::GetAwningAt(uint32_t index) { return mAwnings[index].Get(); }
void *cTSCatalog::FindAwning(uint32_t key) { return FindIn(mAwningsByKey, key); }

void *cTSCatalog::FindAwningByKey(uint32_t key)
{
    for (auto &entry : mAwningsByKey)
        if (Call<13>(entry.second.Get()) == key) return entry.second.Get();
    return nullptr;
}

uint32_t cTSCatalog::GetRoofCount() { return static_cast<uint32_t>(mRoofs.size()); }
void *cTSCatalog::GetRoofAt(uint32_t index) { return mRoofs[index].Get(); }
void *cTSCatalog::FindRoof(uint32_t key) { return FindIn(mRoofsByKey, key); }

uint32_t cTSCatalog::GetRoofPatternCount() { return static_cast<uint32_t>(mRoofPatterns.size()); }
void *cTSCatalog::GetRoofPatternAt(uint32_t index) { return mRoofPatterns[index].Get(); }

void *cTSCatalog::FindRoofPattern(uint32_t key, bool orDefault)
{
    auto it = mRoofPatternsByKey.find(key);
    if (it != mRoofPatternsByKey.end()) return it->second.Get();
    return orDefault ? GetDefaultRoofPattern() : nullptr;
}

void *cTSCatalog::GetDefaultRoofPattern() { return mDefaultRoofPattern.Get(); }

bool cTSCatalog::RemoveRoofPattern(void *pattern)
{
    auto node = mRoofPatternsByKey.find(GuidOf(pattern));
    if (node == mRoofPatternsByKey.end()) return false;
    auto it = FindProduct(mRoofPatterns, pattern);
    if (it == mRoofPatterns.end()) return false;
    mRoofPatternsByKey.erase(node);
    mRoofPatterns.erase(it);
    return true;
}

uint32_t cTSCatalog::GetExtraCount() { return static_cast<uint32_t>(mExtras.size()); }
void *cTSCatalog::GetExtraAt(uint32_t index) { return mExtras[index].Get(); }
void *cTSCatalog::FindExtra(uint32_t guid) { return FindIn(mExtrasByGuid, guid); }

void *cTSCatalog::GetRandomRootExtra()
{
    cRZRandom random;
    random.ConstructAt(0xffffffff, 0xc105f3);
    const uint32_t count = static_cast<uint32_t>(mRootExtras.size());
    if (count == 0) return nullptr;
    return mRootExtras[random.RandomUint32Uniform(count)].Get();
}

bool cTSCatalog::RemoveExtra(void *extra)
{
    auto node = mExtrasByGuid.find(GuidOf(extra));
    if (node == mExtrasByGuid.end()) return false;
    auto it = FindProduct(mExtras, extra);
    if (it == mExtras.end()) return false;
    mExtrasByGuid.erase(node);
    mExtras.erase(it);
    return true;
}

uint32_t cTSCatalog::GetUserToolCount() { return static_cast<uint32_t>(mUserTools.size()); }

void *cTSCatalog::GetUserToolAt(uint32_t index)
{
    if (index >= mUserTools.size()) return nullptr;
    return mUserTools[index].Get();
}

void *cTSCatalog::FindUserTool(uint32_t key) { return FindIn(mUserToolsByKey, key); }

uint32_t cTSCatalog::Slot78() { return 0; }

float cTSCatalog::GetSlot79Value() { return *GameAddress<const float>(0xdb13e8); }

bool cTSCatalog::SortFences()
{
    if (mFences.size() <= 2) return false;
    std::sort(mFences.begin(), mFences.end(), SortKeyLess);
    return true;
}

// ---- loaders ----

// Floor (type 0) or wall (type 1) patterns: each file's default pattern, then its patterns through AddPattern; sorted by name,
// the parser's material value kept for the type, the default applied.
void cTSCatalog::LoadPatterns(uint32_t type, GZStringList *files)
{
    constexpr uint32_t kResource = 0xe9da450e;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc18a8b>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        ProductRef resource;
        if (GetDefault(parser, kResource, resource.Reset())) mDefaultPattern[type].Assign(resource.Get());
        for (int32_t i = 0; i < count; ++i) {
            GetResource(parser, i, kResource, resource.Reset());
            AddPattern(type, resource.Get());
        }
    });
    if (mPatterns[type].size() > 2)
        std::sort(mPatterns[type].begin(), mPatterns[type].end(),
                  [](const ProductRef &a, const ProductRef &b) { return NameLess(a.Get(), b.Get()); });
    mPatternMaterialValue[type] = GetMaterialValue(parser);
    GetPatternCount(type);
    ParserEnd(parser);
    ApplyDefaultPattern(type);
    ParserRelease(parser);
}

// Terrain brushes, the first of each name.
void cTSCatalog::LoadTerrainBrushes(GZStringList *files)
{
    constexpr uint32_t kResource = 0x8b968389;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc0e98b>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef brush;
            GetResource(parser, i, kResource, brush.Reset());
            if (mTerrainBrushesByName.find(std::string(Call<6, const char *>(brush.Get()))) == mTerrainBrushesByName.end()) {
                mTerrainBrushes.push_back(brush);
                mTerrainBrushesByName[std::string(Call<6, const char *>(brush.Get()))].Assign(brush.Get());
            }
        }
    });
    ParserEnd(parser);
    ParserRelease(parser);
}

void cTSCatalog::LoadTerrainPaints(GZStringList *files)
{
    constexpr uint32_t kResource = 0x8b8d69b7;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc0e8f2>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef paint;
            GetResource(parser, i, kResource, paint.Reset());
            mTerrainPaints.push_back(paint);
            mTerrainPaintsByName[std::string(Call<4, const char *>(paint.Get()))].Assign(paint.Get());
        }
    });
    ParserEnd(parser);
    ParserRelease(parser);
}

void cTSCatalog::LoadWallsAndFences()
{
    auto list = [](GZStringList &files, uint32_t option) {
        appinit::CommonImplSlot5(option, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&files)), 1, 0, 0xffffffff);
    };
    GZStringList walls;
    list(walls, 0x2bd0423c);
    LoadWalls(&walls);
    GZStringList fences;
    list(fences, 0xebd04239);
    LoadFences(&fences);
    GZStringList arches;
    list(arches, 0xcbd04239);
    LoadFenceArches(&arches);
    ResolveWallPartKeys();
}

// Walls: each wall resource with a guid not loaded yet gives four parts (face 4 slot 22), each with its own running id; the
// wall's guid maps to the first part's id.
void cTSCatalog::LoadWalls(GZStringList *files)
{
    constexpr uint32_t kResource = 0x09da4246;
    void *parser = NewGameObject(0x204, [](void *memory) { return OriginalFunction<ConstructFn, 0xc19b92>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, true, [&] {
        const int32_t count = GetCount(parser);
        ProductRef wall;
        for (int32_t i = 0; i < count; ++i) {
            GetResource(parser, i, kResource, wall.Reset());
            if (mIdByGuid.find(GuidOf(wall.Get())) != mIdByGuid.end()) continue; // a guid already loaded: skipped
            mIdByGuid[GuidOf(wall.Get())] = mNextId;
            for (uint32_t side = 0; side < 4; ++side) {
                ProductRef part;
                void *source = Face(wall.Get(), 4);
                Call<22>(source, kResource, part.Reset());
                Call<0>(Face(part.Get(), 3), mNextId);
                Call<18>(Face(part.Get(), 4), side);
                uint32_t a = 0, b = 0, c = 0;
                if (Call<26, bool>(Face(part.Get(), 2), side, &a, &b, &c)) Call<0>(Face(part.Get(), 2), a, b, c);
                mWalls.push_back(part);
                mWallsById[mNextId].Assign(part.Get());
                void *wallPart = FaceOrNull(part.Get(), 4);
                mWallParts.push_back(ProductRef(wallPart));
                mWallPartsById[mNextId].Assign(wallPart);
                ++mNextId;
            }
        }
    });
    ParserEnd(parser);
    ParserRelease(parser);
}

void cTSCatalog::LoadFences(GZStringList *files)
{
    constexpr uint32_t kResource = 0xaad51b06;
    void *parser = NewGameObject(0x1f8, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1a432>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        ProductRef fence;
        ProductRef fallback;
        GetDefault(parser, kResource, fallback.Reset());
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            GetResource(parser, i, kResource, fence.Reset());
            AddFence(fence.Get(), fence.Get() == fallback.Get());
        }
        fallback.Reset();
    });
    SortByKey(mFences);
    mFenceMaterialValue = GetMaterialValue(parser);
    ParserEnd(parser);
    ParserRelease(parser);
}

void cTSCatalog::LoadFenceArches(GZStringList *files)
{
    constexpr uint32_t kResource = 0x0b4ce79b;
    void *parser = NewGameObject(0x1f8, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1ab57>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        ProductRef arch;
        ProductRef fallback;
        GetDefault(parser, kResource, fallback.Reset());
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            GetResource(parser, i, kResource, arch.Reset());
            if (arch.Get() == fallback.Get()) mDefaultFenceArch.Assign(arch.Get());
            mIdByGuid[GuidOf(arch.Get())] = mNextId;
            Call<0>(Face(arch.Get(), 3), mNextId);
            mFenceArches.push_back(arch);
            mFenceArchesById[mNextId].Assign(arch.Get());
            void *part = FaceOrNull(arch.Get(), 4);
            mWallParts.push_back(ProductRef(part));
            mWallPartsById[mNextId].Assign(part);
            ++mNextId;
        }
        fallback.Reset();
    });
    SortByKey(mFenceArches);
    mFenceArchMaterialValue = GetMaterialValue(parser);
    ParserEnd(parser);
    ParserRelease(parser);
}

// Deck foundations: each new resource (by guid) gives two parts (primary slot 11), numbered by a local counter; afterwards
// each part's default key (face 3 slot 3) is resolved to the counter value of that guid's first part.
void cTSCatalog::LoadDeckFoundations(GZStringList *files)
{
    constexpr uint32_t kResource = 0x2acd1426;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1b0ff>()(memory, 1); });
    ParserAddRef(parser);
    std::map<uint32_t, uint32_t> firstIdByGuid;
    ParserBegin(parser);
    std::set<int32_t> seen;
    uint32_t counter = 0;
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef deck;
            GetResource(parser, i, kResource, deck.Reset());
            if (seen.count(static_cast<int32_t>(GuidOf(deck.Get())))) continue;
            seen.insert(static_cast<int32_t>(GuidOf(deck.Get())));
            firstIdByGuid[GuidOf(deck.Get())] = counter;
            for (uint32_t side = 0; side < 2; ++side) {
                ProductRef part;
                Call<11>(deck.Get(), kResource, part.Reset());
                Call<0>(Face(part.Get(), 3), counter);
                Call<10>(part.Get(), side);
                uint32_t a = 0, b = 0, c = 0;
                if (Call<26, bool>(Face(part.Get(), 2), side, &a, &b, &c)) Call<0>(Face(part.Get(), 2), a, b, c);
                mDeckFoundations.push_back(part);
                mDeckFoundationsById[counter].Assign(part.Get());
                ++counter;
                if (GuidOf(deck.Get()) == 5) break;
            }
        }
    }, [&] {
        // The default keys are resolved after every file of the list (read or not), over every part loaded so far.
        for (ProductRef &part : mDeckFoundations) {
            const uint32_t key = Call<3>(Face(part.Get(), 3));
            auto it = firstIdByGuid.find(key);
            if (it != firstIdByGuid.end()) Call<1>(Face(part.Get(), 3), firstIdByGuid[key]);
        }
    });
    ParserEnd(parser);
    ParserRelease(parser);
}

void cTSCatalog::LoadModularStairs(GZStringList *files)
{
    constexpr uint32_t kResource = 0x2b394006;
    void *parser = NewGameObject(0x1ec, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1bd0c>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef stairs;
            GetResource(parser, i, kResource, stairs.Reset());
            if (mStairsByGuid.find(GuidOf(stairs.Get())) == mStairsByGuid.end()) {
                mStairs.push_back(stairs);
                mStairsByGuid[GuidOf(stairs.Get())].Assign(stairs.Get());
            }
        }
    });
    SortByKey(mStairs);
    ParserEnd(parser);
    ParserRelease(parser);
}

// Fireplaces; a fireplace whose definition (primary slot 7) the definitions holder knows takes a value from it (face 2 slot 16).
void cTSCatalog::LoadFireplaces(GZStringList *files)
{
    constexpr uint32_t kResource = 0x4b5f3733;
    void *definitions = Call<24, void *>(GetCachedSingleton());
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1c34f>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef fireplace;
            GetResource(parser, i, kResource, fireplace.Reset());
            const uint32_t definitionId = Call<7>(fireplace.Get());
            if (definitionId && definitions) {
                if (void *definition = Call<16, void *>(definitions, definitionId)) {
                    if (void *resource = Call<4, void *>(definition))
                        Call<16>(Face(fireplace.Get(), 2), Call<3>(resource, 0x12u));
                }
            }
            mFireplaces.push_back(fireplace);
            mFireplacesByKey[Call<6>(fireplace.Get())].Assign(fireplace.Get());
        }
    });
    SortByKey(mFireplaces);
    ParserEnd(parser);
    ParserRelease(parser);
}

void cTSCatalog::LoadAwnings(GZStringList *files)
{
    constexpr uint32_t kResource = 0x5036a9c9;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1c9a7>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef awning;
            GetResource(parser, i, kResource, awning.Reset());
            mAwnings.push_back(awning);
            mAwningsByKey[Call<13>(awning.Get())].Assign(awning.Get());
        }
    });
    ParserEnd(parser);
    ParserRelease(parser);
}

// Roofs, the first of each key (primary slot 4).
void cTSCatalog::LoadRoofs(GZStringList *files)
{
    constexpr uint32_t kResource = 0x8b79864b;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1d540>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    std::set<int32_t> seen;
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef roof;
            GetResource(parser, i, kResource, roof.Reset());
            if (!seen.count(static_cast<int32_t>(Call<4>(roof.Get())))) {
                mRoofs.push_back(roof);
                mRoofsByKey[Call<4>(roof.Get())].Assign(roof.Get());
                seen.insert(static_cast<int32_t>(Call<4>(roof.Get())));
            }
        }
    });
    ParserEnd(parser);
    seen.clear();
    ParserRelease(parser);
}

void cTSCatalog::LoadRoofPatterns(GZStringList *files)
{
    constexpr uint32_t kResource = 0xcbd7624f;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1da3d>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        ProductRef pattern;
        if (GetDefault(parser, kResource, pattern.Reset())) mDefaultRoofPattern.Assign(pattern.Get());
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            GetResource(parser, i, kResource, pattern.Reset());
            AddRoofPattern(pattern.Get());
        }
    });
    SortByKey(mRoofPatterns);
    mRoofPatternMaterialValue = GetMaterialValue(parser);
    ParserEnd(parser);
    ApplyDefaultRoofPattern();
    ParserRelease(parser);
}

void cTSCatalog::LoadUserToolProxies(GZStringList *files)
{
    constexpr uint32_t kResource = 0xec716b82;
    void *parser = NewGameObject(0x1e0, [](void *memory) { return OriginalFunction<ConstructFn, 0xc1e16d>()(memory, 1); });
    ParserAddRef(parser);
    ParserBegin(parser);
    ForEachExistingFile(parser, files, false, [&] {
        const int32_t count = GetCount(parser);
        for (int32_t i = 0; i < count; ++i) {
            ProductRef tool;
            GetResource(parser, i, kResource, tool.Reset());
            if (tool.Get()) {
                mUserTools.push_back(tool);
                mUserToolsByKey[Call<1>(tool.Get())].Assign(tool.Get());
            }
        }
    });
    ParserEnd(parser);
    ParserRelease(parser);
}

// ---- defaults and material values ----

// Every wall part without its own default takes the default fence's and the default fence arch's key (when it answers their
// resource types), then its default key (a guid) is resolved to that guid's running id.
void cTSCatalog::ResolveWallPartKeys()
{
    for (ProductRef &entry : mWallParts) {
        void *part = entry.Get();
        if (!Call<4, bool>(Face(part, 3))) {
            if (void *fence = mDefaultFence.Get()) {
                ProductRef answer;
                if (Call<0, bool>(Face(part, 1), 0xaad51b06u, answer.Reset()) && answer.Get() != fence)
                    Call<1>(Face(answer.Get(), 3), GuidOf(mDefaultFence.Get()));
            }
            if (void *arch = mDefaultFenceArch.Get()) {
                ProductRef answer;
                if (Call<0, bool>(Face(part, 1), 0x0b4ce79bu, answer.Reset()) && answer.Get() != arch)
                    Call<1>(Face(answer.Get(), 3), GuidOf(mDefaultFenceArch.Get()));
            }
        }
        const uint32_t key = Call<3>(Face(part, 3));
        if (mIdByGuid.find(key) != mIdByGuid.end()) Call<1>(Face(part, 3), mIdByGuid[key]);
    }
}

void cTSCatalog::ApplyDefaultPattern(uint32_t type)
{
    for (ProductRef &entry : mPatterns[type]) {
        void *pattern = entry.Get();
        if (Call<4, bool>(Face(pattern, 3))) continue;
        void *fallback = mDefaultPattern[type].Get();
        if (fallback && pattern != fallback) Call<1>(Face(pattern, 3), GuidOf(fallback));
    }
}

void cTSCatalog::ApplyDefaultRoofPattern()
{
    for (ProductRef &entry : mRoofPatterns) {
        void *pattern = entry.Get();
        if (Call<4, bool>(Face(pattern, 3))) continue;
        void *fallback = mDefaultRoofPattern.Get();
        if (fallback && pattern != fallback) Call<1>(Face(pattern, 3), GuidOf(fallback));
    }
}

void cTSCatalog::ApplyMaterialValues()
{
    ApplyPatternMaterialValues();
    ApplyFenceMaterialValues();
    ApplyFenceArchMaterialValues();
    ApplyRoofPatternMaterialValues();
}

void cTSCatalog::ApplyPatternMaterialValues()
{
    for (int type = 0; type < kPatternTypes; ++type) {
        if (mPatternMaterialValue[type] == 0xffffffff) continue;
        for (ProductRef &entry : mPatterns[type]) ApplyMaterialValue(FaceOrNull(entry.Get(), 2), mPatternMaterialValue[type]);
    }
}

// A fence whose primary slot 11 answers true takes the value 0x24 instead of the parser's.
void cTSCatalog::ApplyFenceMaterialValues()
{
    if (mFenceMaterialValue == 0xffffffff) return;
    for (ProductRef &entry : mFences) {
        const uint32_t value = Call<11, bool>(entry.Get()) ? 0x24 : mFenceMaterialValue;
        ApplyMaterialValue(Face(entry.Get(), 2), value);
    }
}

void cTSCatalog::ApplyFenceArchMaterialValues()
{
    if (mFenceArchMaterialValue == 0xffffffff) return;
    for (ProductRef &entry : mFenceArches) ApplyMaterialValue(FaceOrNull(entry.Get(), 2), mFenceArchMaterialValue);
}

void cTSCatalog::ApplyRoofPatternMaterialValues()
{
    if (mRoofPatternMaterialValue == 0xffffffff) return;
    for (ProductRef &entry : mRoofPatterns) ApplyMaterialValue(FaceOrNull(entry.Get(), 2), mRoofPatternMaterialValue);
}

void cTSCatalog::ApplyMaterialValue(void *face, uint32_t value)
{
    if (const char *name = Call<31, const char *>(face); name && std::strlen(Call<31, const char *>(face)) != 0) return;
    Call<9>(face, value);
    if (const char *name = Call<31, const char *>(face); name && std::strlen(Call<31, const char *>(face)) != 0) Call<13>(face);
}
