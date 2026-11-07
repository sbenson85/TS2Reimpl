#pragma once
#include "gz/cGZAny.h"                 // GZString
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

// cTSCatalog: the build/buy catalogue of products (objects, walls, fences, half walls, fence arches, deck foundations, floor
// and wall patterns, terrain brushes and paints, modular stairs, fireplaces, awnings, roofs, roof patterns, user tool proxies).
// RTTI cTSCatalog (type descriptor 0x10c47b4), bases cITSCatalog and cIGZUnknown at +0, one vtable (0xe91b4c, 111 slots,
// slot 81 the scalar deleting destructor). 0x1bc bytes. Its functions are /O1 ("size": cTSCatalog.size.cpp). Lane IK.
// RVAs are 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md): a real C++ class; the compiler emits its vtable. The game creates the catalogue
// only through the factory 0xc105a5 (operator new 0x1bc + the constructor 0xc0e538; registered by nTSMetaData::cCOMDirector),
// which is hooked to Create, so every catalogue carries our vtable. The game's vtable is referenced only by the constructor and
// the destructor 0xc0eafa (reached only from the deleting destructor, slot 81).
//
// Products. Every product the catalogue keeps is a pointer P to an object with a virtual base table pointer at P+4; its faces
// (virtual base subobjects) are at P + 4 + vbtable[i]:
//   face 1  the cIGZUnknown (slot 1 AddRef, slot 2 Release): what a ProductRef counts;
//   face 2  the material/appearance face (slots 0, 7, 9, 13, 15, 16, 26, 29, 31 used here);
//   face 3  the identity face: slot 0 SetId(id), slot 1 SetDefaultKey(key), slot 2 GetGuid (a name text for patterns),
//           slot 3 GetDefaultKey, slot 4 HasOwnDefault;
//   face 4  the wall-part face (slot 18, 22, 23 used here; its own vbptr at +4, so it is a product too);
//   face 5 / face 7  the half wall's source and own faces.
// The resource parsers the loaders create keep their virtual base table pointer at +8 instead (face 1 cIGZUnknown, face 2
// slot 0 Begin(0) / slot 1 End()); their own slots: 5 GetResource(index, type, &ref), 6 GetCount, 9 GetDefault(type, &ref),
// 10 GetMaterialValue(0).

// A counted product reference (the game's RZAutoRef-style holder for a product: the pointer is the product, the count is kept
// on its face 1). Copy constructor 0x5dd982 (AddRef when non-null), destructor 0x210cfc / 0x1fcd10 (Release when non-null),
// Assign 0x210dc9. No move constructor, like the game's: the vectors copy (AddRef + Release) when they reallocate.
class ProductRef {
public:
    ProductRef() noexcept = default; // 0x5ed57a
    explicit ProductRef(void *product);
    ProductRef(const ProductRef &other) : ProductRef(other.mProduct) {}
    ~ProductRef();
    // operator= of a reference: the address check (inline in the original), then Assign.
    ProductRef &operator=(const ProductRef &other)
    {
        if (this != &other) Assign(other.mProduct);
        return *this;
    }
    // 0x210dc9 (__thiscall, ret 4): when product differs from the one held, AddRef product (if non-null), store it, then
    // Release the old one (if non-null).
    void Assign(void *product);
    // 0x2296b4 / 0x258c70: clears the reference (releasing what it held) and gives its address, as the out argument of a
    // getter that fills it.
    void **Reset();
    void *Get() const { return mProduct; }

    static void AddRefProduct(void *product);
    static void ReleaseProduct(void *product);

private:
    void *mProduct = nullptr;
};
static_assert(sizeof(ProductRef) == 4, "ProductRef layout");

// The name maps' order: _stricmp of the two names (0xc0ece7), so names that differ only in case are one key.
struct NameLessIgnoringCase {
    bool operator()(const std::string &a, const std::string &b) const;
};

using ProductVector = std::vector<ProductRef>;
using ProductIdMap = std::map<uint32_t, ProductRef>;                              // node 0x18: key +0x10, reference +0x14
using ProductNameMap = std::map<std::string, ProductRef, NameLessIgnoringCase>; // node 0x2c: key +0x10, reference +0x28
static_assert(sizeof(ProductVector) == 12 && sizeof(ProductIdMap) == 8 && sizeof(ProductNameMap) == 8);

// The resource file lists Load builds (0x20e147 constructs, 0x20fadb destroys): file names in the allocator service's memory,
// filled by the app's cTSAppCommonImpl slot 5 (0x1d0c60).
using GZStringList = std::list<GZString, GZServiceAllocator<GZString>>;
static_assert(sizeof(GZStringList) == 8, "GZStringList layout");

// The catalogue's own interface (cITSCatalog; interface id 0xdadba5f9). Slot 81 is the class's virtual destructor, declared at
// its place. Names say what the slot does; "key" is the value a face slot gives, "id" the catalogue's running id.
class cITSCatalog : public cIGZUnknown {
public:
    virtual void Init() = 0;                                              // slot 3
    virtual void Shutdown() = 0;                                          // slot 4
    virtual void Load() = 0;                                              // slot 5
    virtual void Unload() = 0;                                            // slot 6
    virtual void ParsePatternKeysFrom(void *source) = 0;                  // slot 7 (ret 4)
    virtual bool AllSegmentsOfKindOne(void *object) = 0;                  // slot 8 (ret 4)
    virtual bool FindFirstKnownSegment(void *object, void **out) = 0;     // slot 9 (ret 8)
    virtual void AddPattern(uint32_t type, void *pattern) = 0;            // slot 10 (ret 8)
    virtual uint32_t GetObjectCount() = 0;                                // slot 11
    virtual bool AddObject(void *definition) = 0;                         // slot 12 (ret 4)
    virtual void *GetObjectAt(uint32_t index) = 0;                        // slot 13
    virtual void *FindObject(uint32_t definition) = 0;                    // slot 14
    virtual bool RemoveObject(void *object) = 0;                          // slot 15
    virtual uint32_t GetWallPartCount() = 0;                              // slot 16
    virtual void *GetWallPartAt(uint32_t index) = 0;                      // slot 17
    virtual void *FindWallPart(uint32_t id) = 0;                          // slot 18
    virtual void *FindWallPartByKey(uint32_t key) = 0;                    // slot 19
    virtual uint32_t GetWallCount() = 0;                                  // slot 20
    virtual void *GetWallAt(uint32_t index) = 0;                          // slot 21
    virtual void *FindWall(uint32_t id) = 0;                              // slot 22
    virtual void *FindWallByKey(uint32_t key) = 0;                        // slot 23
    virtual uint32_t GetFenceCount() = 0;                                 // slot 24
    virtual void *GetFenceAt(uint32_t index) = 0;                         // slot 25
    virtual void *FindFenceByKey(uint32_t key, bool orDefault) = 0;       // slot 26
    virtual void *GetDefaultFence() = 0;                                  // slot 27
    virtual bool RemoveFence(void *fence) = 0;                            // slot 28
    virtual uint32_t GetHalfWallCount() = 0;                              // slot 29
    virtual void *GetHalfWallAt(uint32_t index) = 0;                      // slot 30
    virtual void *FindHalfWallByKey(uint32_t key) = 0;                    // slot 31
    virtual uint32_t GetFenceArchCount() = 0;                             // slot 32
    virtual void *GetFenceArchAt(uint32_t index) = 0;                     // slot 33
    virtual void *FindFenceArchByKey(uint32_t key, bool orDefault) = 0;   // slot 34
    virtual void *GetDefaultFenceArch() = 0;                              // slot 35
    virtual uint32_t GetDeckFoundationCount() = 0;                        // slot 36
    virtual void *GetDeckFoundationAt(uint32_t index) = 0;                // slot 37
    virtual void *FindDeckFoundation(uint32_t id) = 0;                    // slot 38
    virtual void *FindDeckFoundationByKey(uint32_t key) = 0;              // slot 39
    virtual uint32_t GetPatternCount(uint32_t type) = 0;                  // slot 40
    virtual void *GetPatternAt(uint32_t type, uint32_t index) = 0;        // slot 41
    virtual void *FindPattern(uint32_t type, const char *name, bool orDefault) = 0; // slot 42
    virtual void *GetDefaultPattern(uint32_t type) = 0;                   // slot 43
    virtual bool RemovePattern(void *pattern) = 0;                        // slot 44
    virtual uint32_t GetTerrainBrushCount() = 0;                          // slot 45
    virtual void *GetTerrainBrushAt(uint32_t index) = 0;                  // slot 46
    virtual void *FindTerrainBrush(const char *name) = 0;                 // slot 47
    virtual uint32_t GetTerrainPaintCount() = 0;                          // slot 48
    virtual void *GetTerrainPaintAt(uint32_t index) = 0;                  // slot 49
    virtual void *FindTerrainPaint(const char *name) = 0;                 // slot 50
    virtual bool RemoveTerrainPaint(void *paint) = 0;                     // slot 51
    virtual uint32_t GetStairsCount() = 0;                                // slot 52
    virtual void *GetStairsAt(uint32_t index) = 0;                        // slot 53
    virtual void *FindStairs(uint32_t guid) = 0;                          // slot 54
    virtual uint32_t GetFireplaceCount() = 0;                             // slot 55
    virtual void *GetFireplaceAt(uint32_t index) = 0;                     // slot 56
    virtual void *FindFireplace(uint32_t key) = 0;                        // slot 57
    virtual uint32_t GetAwningCount() = 0;                                // slot 58
    virtual void *GetAwningAt(uint32_t index) = 0;                        // slot 59
    virtual void *FindAwning(uint32_t key) = 0;                           // slot 60
    virtual void *FindAwningByKey(uint32_t key) = 0;                      // slot 61
    virtual uint32_t GetRoofCount() = 0;                                  // slot 62
    virtual void *GetRoofAt(uint32_t index) = 0;                          // slot 63
    virtual void *FindRoof(uint32_t key) = 0;                             // slot 64
    virtual uint32_t GetRoofPatternCount() = 0;                           // slot 65
    virtual void *GetRoofPatternAt(uint32_t index) = 0;                   // slot 66
    virtual void *FindRoofPattern(uint32_t key, bool orDefault) = 0;      // slot 67
    virtual void *GetDefaultRoofPattern() = 0;                            // slot 68
    virtual bool RemoveRoofPattern(void *pattern) = 0;                    // slot 69
    virtual uint32_t GetExtraCount() = 0;                                 // slot 70
    virtual void *GetExtraAt(uint32_t index) = 0;                         // slot 71
    virtual void *FindExtra(uint32_t guid) = 0;                           // slot 72
    virtual void *GetRandomRootExtra() = 0;                               // slot 73
    virtual bool RemoveExtra(void *extra) = 0;                            // slot 74
    virtual uint32_t GetUserToolCount() = 0;                              // slot 75
    virtual void *GetUserToolAt(uint32_t index) = 0;                      // slot 76
    virtual void *FindUserTool(uint32_t key) = 0;                         // slot 77
    virtual uint32_t Slot78() = 0;                                        // slot 78 (always 0)
    virtual float GetSlot79Value() = 0;                                   // slot 79
    virtual bool SortFences() = 0;                                        // slot 80
    virtual ~cITSCatalog() = default;                                     // slot 81 (scalar deleting destructor)
    virtual void AddRoofPattern(void *pattern) = 0;                       // slot 82
    virtual void AddFence(void *fence, bool isDefault) = 0;               // slot 83
    virtual void AddHalfWall(void *source) = 0;                           // slot 84
    virtual void AddExtra(void *extra) = 0;                               // slot 85
    virtual void AddTerrainPaint(void *paint) = 0;                        // slot 86
    virtual void ParseHeldPatternKeys() = 0;                              // slot 87
    virtual void AddObjectsFromDefinitions() = 0;                         // slot 88
    virtual void LoadPatterns(uint32_t type, GZStringList *files) = 0;    // slot 89
    virtual void LoadTerrainBrushes(GZStringList *files) = 0;             // slot 90
    virtual void LoadTerrainPaints(GZStringList *files) = 0;              // slot 91
    virtual void LoadWallsAndFences() = 0;                                // slot 92
    virtual void LoadWalls(GZStringList *files) = 0;                      // slot 93
    virtual void LoadFences(GZStringList *files) = 0;                     // slot 94
    virtual void LoadFenceArches(GZStringList *files) = 0;                // slot 95
    virtual void LoadDeckFoundations(GZStringList *files) = 0;            // slot 96
    virtual void LoadModularStairs(GZStringList *files) = 0;              // slot 97
    virtual void LoadFireplaces(GZStringList *files) = 0;                 // slot 98
    virtual void LoadAwnings(GZStringList *files) = 0;                    // slot 99
    virtual void LoadRoofs(GZStringList *files) = 0;                      // slot 100
    virtual void LoadRoofPatterns(GZStringList *files) = 0;               // slot 101
    virtual void LoadUserToolProxies(GZStringList *files) = 0;            // slot 102
    virtual void ResolveWallPartKeys() = 0;                               // slot 103
    virtual void ApplyDefaultPattern(uint32_t type) = 0;                  // slot 104
    virtual void ApplyDefaultRoofPattern() = 0;                           // slot 105
    virtual void ApplyMaterialValues() = 0;                               // slot 106
    virtual void ApplyPatternMaterialValues() = 0;                        // slot 107
    virtual void ApplyFenceMaterialValues() = 0;                          // slot 108
    virtual void ApplyFenceArchMaterialValues() = 0;                      // slot 109
    virtual void ApplyRoofPatternMaterialValues() = 0;                    // slot 110
};

class cTSCatalog : public cITSCatalog {
public:
    static constexpr uint32_t kInterfaceId = 0xdadba5f9;
    static constexpr size_t kSize = 0x1bc;
    static constexpr int kPatternTypes = 2; // floor and wall patterns (vec +0x64, maps +0x12c, defaults +0x184, values +0x1b0)

    cTSCatalog();           // 0xc0e538
    ~cTSCatalog() override; // 0xc0eafa; slot 81 (0xc0ed88) is the compiler's scalar deleting destructor
    cTSCatalog(const cTSCatalog &) = delete;
    cTSCatalog &operator=(const cTSCatalog &) = delete;
    static cTSCatalog *Create(); // 0xc105a5 (cdecl): new, the constructor

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0xc143f6
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, 0xc1463e

    void Init() override;                                          // 0xc10768
    void Shutdown() override;                                      // 0xc14786
    void Load() override;                                          // 0xc109b4
    void Unload() override;                                        // 0xc0fb91
    void ParsePatternKeysFrom(void *source) override;              // 0xc0f5d8
    bool AllSegmentsOfKindOne(void *object) override;              // 0xc1087c
    bool FindFirstKnownSegment(void *object, void **out) override; // 0xc1061d
    void AddPattern(uint32_t type, void *pattern) override;        // 0xc0f414
    uint32_t GetObjectCount() override;                            // folded 0x806c60
    bool AddObject(void *definition) override;                     // 0xc0f2b2
    void *GetObjectAt(uint32_t index) override;                    // folded 0x808d30
    void *FindObject(uint32_t definition) override;                // 0x6e7c75
    bool RemoveObject(void *object) override;                      // 0xc101e8
    uint32_t GetWallPartCount() override;                          // folded 0x5773e0
    void *GetWallPartAt(uint32_t index) override;                  // folded 0x808190
    void *FindWallPart(uint32_t id) override;                      // 0xc142c6
    void *FindWallPartByKey(uint32_t key) override;                // 0xc142e8
    uint32_t GetWallCount() override;                              // folded 0x806c50
    void *GetWallAt(uint32_t index) override;                      // folded 0x807330
    void *FindWall(uint32_t id) override;                          // 0xc14909
    void *FindWallByKey(uint32_t key) override;                    // 0xc1492b
    uint32_t GetFenceCount() override;                             // 0xb52a00
    void *GetFenceAt(uint32_t index) override;                     // 0xc10508
    void *FindFenceByKey(uint32_t key, bool orDefault) override;   // 0xc10515
    void *GetDefaultFence() override;                              // folded 0x6f27ab
    bool RemoveFence(void *fence) override;                        // 0xc10110
    uint32_t GetHalfWallCount() override;                          // 0xc12f9a
    void *GetHalfWallAt(uint32_t index) override;                  // 0xc1070c
    void *FindHalfWallByKey(uint32_t key) override;                // 0xc10719
    uint32_t GetFenceArchCount() override;                         // folded 0x73e3d1
    void *GetFenceArchAt(uint32_t index) override;                 // 0xc1049a
    void *FindFenceArchByKey(uint32_t key, bool orDefault) override; // 0xc104a7
    void *GetDefaultFenceArch() override;                          // folded 0x530770
    uint32_t GetDeckFoundationCount() override;                    // folded 0x6bd993
    void *GetDeckFoundationAt(uint32_t index) override;            // 0xc100a6
    void *FindDeckFoundation(uint32_t id) override;                // 0xc10084
    void *FindDeckFoundationByKey(uint32_t key) override;          // 0xc100b3
    uint32_t GetPatternCount(uint32_t type) override;              // 0xc12fc4
    void *GetPatternAt(uint32_t type, uint32_t index) override;    // 0xc14398
    void *FindPattern(uint32_t type, const char *name, bool orDefault) override; // 0xc1432c
    void *GetDefaultPattern(uint32_t type) override;               // 0xc100f7
    bool RemovePattern(void *pattern) override;                    // 0xc1024a
    uint32_t GetTerrainBrushCount() override;                      // folded 0xa58000
    void *GetTerrainBrushAt(uint32_t index) override;              // 0xc1488b
    void *FindTerrainBrush(const char *name) override;             // 0xc14842
    uint32_t GetTerrainPaintCount() override;                      // 0xc12ff7
    void *GetTerrainPaintAt(uint32_t index) override;              // 0xc14832
    void *FindTerrainPaint(const char *name) override;             // 0xc147e9
    bool RemoveTerrainPaint(void *paint) override;                 // 0xc1036c
    uint32_t GetStairsCount() override;                            // 0xc12fa4
    void *GetStairsAt(uint32_t index) override;                    // 0xc12f2d
    void *FindStairs(uint32_t guid) override;                      // folded 0x707810
    uint32_t GetFireplaceCount() override;                         // 0xc12f8a
    void *GetFireplaceAt(uint32_t index) override;                 // 0xc10595
    void *FindFireplace(uint32_t key) override;                    // 0xc10573
    uint32_t GetAwningCount() override;                            // 0xc12f6f
    void *GetAwningAt(uint32_t index) override;                    // 0xc0fb21
    void *FindAwning(uint32_t key) override;                       // 0xc0faff
    void *FindAwningByKey(uint32_t key) override;                  // 0xc0fb31
    uint32_t GetRoofCount() override;                              // 0xc12fe7
    void *GetRoofAt(uint32_t index) override;                      // 0xc146cc
    void *FindRoof(uint32_t key) override;                         // 0xc146aa
    uint32_t GetRoofPatternCount() override;                       // 0xc12fd7
    void *GetRoofPatternAt(uint32_t index) override;               // 0xc1469a
    void *FindRoofPattern(uint32_t key, bool orDefault) override;  // 0xc14661
    void *GetDefaultRoofPattern() override;                        // folded 0x52fd40
    bool RemoveRoofPattern(void *pattern) override;                // 0xc102fc
    uint32_t GetExtraCount() override;                             // 0xc12fb4
    void *GetExtraAt(uint32_t index) override;                     // 0xc12f3d
    void *FindExtra(uint32_t guid) override;                       // 0xc12f4d
    void *GetRandomRootExtra() override;                           // 0xc105e0
    bool RemoveExtra(void *extra) override;                        // 0xc10178
    uint32_t GetUserToolCount() override;                          // 0xc13007
    void *GetUserToolAt(uint32_t index) override;                  // 0xc148d0
    void *FindUserTool(uint32_t key) override;                     // 0xc148ae
    uint32_t Slot78() override;                                    // folded 0x6c6b0
    float GetSlot79Value() override;                               // 0xc14616
    bool SortFences() override;                                    // 0xc14796
    void AddRoofPattern(void *pattern) override;                   // 0xc0f6c5
    void AddFence(void *fence, bool isDefault) override;           // 0xc0ef18
    void AddHalfWall(void *source) override;                       // 0xc0f0e6
    void AddExtra(void *extra) override;                           // 0xc0ee10
    void AddTerrainPaint(void *paint) override;                    // 0xc0f75b
    void ParseHeldPatternKeys() override;                          // 0xc11d1c
    void AddObjectsFromDefinitions() override;                     // 0xc11cbe
    void LoadPatterns(uint32_t type, GZStringList *files) override; // 0xc11ec4
    void LoadTerrainBrushes(GZStringList *files) override;         // 0xc1276e
    void LoadTerrainPaints(GZStringList *files) override;          // 0xc1256c
    void LoadWallsAndFences() override;                            // 0xc11df2
    void LoadWalls(GZStringList *files) override;                  // 0xc12b83
    void LoadFences(GZStringList *files) override;                 // 0xc111c0
    void LoadFenceArches(GZStringList *files) override;            // 0xc10e7d
    void LoadDeckFoundations(GZStringList *files) override;        // 0xc11650
    void LoadModularStairs(GZStringList *files) override;          // 0xc11a7e
    void LoadFireplaces(GZStringList *files) override;             // 0xc113e7
    void LoadAwnings(GZStringList *files) override;                // 0xc10c9e
    void LoadRoofs(GZStringList *files) override;                  // 0xc1232a
    void LoadRoofPatterns(GZStringList *files) override;           // 0xc120fc
    void LoadUserToolProxies(GZStringList *files) override;        // 0xc129a2
    void ResolveWallPartKeys() override;                           // 0xc0f83e
    void ApplyDefaultPattern(uint32_t type) override;              // 0xc0fa09
    void ApplyDefaultRoofPattern() override;                       // 0xc0fa8a
    void ApplyMaterialValues() override;                           // 0xc0ff0c
    void ApplyPatternMaterialValues() override;                    // 0xc0ffcc
    void ApplyFenceMaterialValues() override;                      // 0xc0ff7a
    void ApplyFenceArchMaterialValues() override;                  // 0xc0ff36
    void ApplyRoofPatternMaterialValues() override;                // 0xc1002f

    // 0xc146e7 (__cdecl, two arguments): gives a material face (face 2) its value when it has no name yet (slot 31 null or
    // empty: slot 9(value)), then, if it has a name by now, tail-calls its slot 13.
    static void ApplyMaterialValue(void *face, uint32_t value);

    uint32_t mRefCount = 0;                   // +0x04
    uint32_t mNextId = 0;                     // +0x08 the running id of walls, fences, half walls and fence arches
    bool mInitialized = false;                // +0x0c
    bool mLoaded = false;                     // +0x0d
    ProductVector mObjects;                   // +0x10 object products (slot 12)
    ProductVector mWallParts;                 // +0x1c face 4 of every wall, fence, half wall and fence arch
    ProductVector mWalls;                     // +0x28 the four parts of each wall resource
    ProductVector mFences;                    // +0x34
    ProductVector mHalfWalls;                 // +0x40
    ProductVector mFenceArches;               // +0x4c
    ProductVector mDeckFoundations;           // +0x58
    ProductVector mPatterns[kPatternTypes];   // +0x64 sorted by name (_stricmp)
    ProductVector mTerrainBrushes;            // +0x7c
    ProductVector mTerrainPaints;             // +0x88
    ProductVector mStairs;                    // +0x94
    ProductVector mFireplaces;                // +0xa0
    ProductVector mAwnings;                   // +0xac
    ProductVector mRoofs;                     // +0xb8
    ProductVector mRoofPatterns;              // +0xc4
    ProductVector mExtras;                    // +0xd0 (slot 85's products)
    ProductVector mRootExtras;                // +0xdc those whose primary slot 7 is 0
    ProductVector mUserTools;                 // +0xe8
    ProductIdMap mObjectsByDefinition;        // +0xf4 key: the definition's address
    ProductIdMap mWallPartsById;              // +0xfc
    ProductIdMap mWallsById;                  // +0x104
    ProductIdMap mFencesById;                 // +0x10c
    ProductIdMap mHalfWallsById;              // +0x114
    ProductIdMap mFenceArchesById;            // +0x11c
    ProductIdMap mDeckFoundationsById;        // +0x124
    ProductNameMap mPatternsByName[kPatternTypes]; // +0x12c
    ProductNameMap mTerrainBrushesByName;     // +0x13c
    ProductNameMap mTerrainPaintsByName;      // +0x144
    ProductIdMap mStairsByGuid;               // +0x14c
    ProductIdMap mFireplacesByKey;            // +0x154
    ProductIdMap mAwningsByKey;               // +0x15c
    ProductIdMap mRoofsByKey;                 // +0x164
    ProductIdMap mRoofPatternsByKey;          // +0x16c
    ProductIdMap mExtrasByGuid;               // +0x174
    ProductIdMap mUserToolsByKey;             // +0x17c
    ProductRef mDefaultPattern[kPatternTypes]; // +0x184
    ProductRef mDefaultRoofPattern;           // +0x18c
    ProductRef mDefaultFence;                 // +0x190
    ProductRef mDefaultFenceArch;             // +0x194
    std::map<uint32_t, uint32_t> mIdByGuid;   // +0x198 guid -> running id
    std::set<uint32_t> mFenceKeys;            // +0x1a0 face 4 slot 23 of the fences added
    uint32_t mFenceMaterialValue = 0xffffffff;      // +0x1a8 (-1: none)
    uint32_t mFenceArchMaterialValue = 0xffffffff;  // +0x1ac
    uint32_t mPatternMaterialValue[kPatternTypes] = {0xffffffff, 0xffffffff}; // +0x1b0
    uint32_t mRoofPatternMaterialValue = 0xffffffff; // +0x1b8
};

static_assert(sizeof(cTSCatalog) == cTSCatalog::kSize, "cTSCatalog layout");
static_assert(offsetof(cTSCatalog, mRefCount) == 0x04);
static_assert(offsetof(cTSCatalog, mNextId) == 0x08);
static_assert(offsetof(cTSCatalog, mInitialized) == 0x0c);
static_assert(offsetof(cTSCatalog, mLoaded) == 0x0d);
static_assert(offsetof(cTSCatalog, mObjects) == 0x10);
static_assert(offsetof(cTSCatalog, mPatterns) == 0x64);
static_assert(offsetof(cTSCatalog, mTerrainBrushes) == 0x7c);
static_assert(offsetof(cTSCatalog, mUserTools) == 0xe8);
static_assert(offsetof(cTSCatalog, mObjectsByDefinition) == 0xf4);
static_assert(offsetof(cTSCatalog, mPatternsByName) == 0x12c);
static_assert(offsetof(cTSCatalog, mTerrainBrushesByName) == 0x13c);
static_assert(offsetof(cTSCatalog, mUserToolsByKey) == 0x17c);
static_assert(offsetof(cTSCatalog, mDefaultPattern) == 0x184);
static_assert(offsetof(cTSCatalog, mDefaultRoofPattern) == 0x18c);
static_assert(offsetof(cTSCatalog, mDefaultFenceArch) == 0x194);
static_assert(offsetof(cTSCatalog, mIdByGuid) == 0x198);
static_assert(offsetof(cTSCatalog, mFenceKeys) == 0x1a0);
static_assert(offsetof(cTSCatalog, mFenceMaterialValue) == 0x1a8);
static_assert(offsetof(cTSCatalog, mPatternMaterialValue) == 0x1b0);
static_assert(offsetof(cTSCatalog, mRoofPatternMaterialValue) == 0x1b8);

// cIGZPersistResourceKeyFilter (the interface cPatternParsingFilter implements): slot 3 IsKeyIncluded(key), slot 4 the
// deleting destructor.
class cIGZPersistResourceKeyFilter : public cIGZUnknown {
public:
    virtual bool IsKeyIncluded(const uint32_t *key) = 0; // slot 3 (key: type, group, instance)
    virtual ~cIGZPersistResourceKeyFilter() = default;   // slot 4
};

// cPatternParsingFilter (type descriptor 0x10c48a0, vtable 0xe91fe4, 8 bytes): the filter slots 7 and 87 hand to a resource
// source: it takes the keys of the five pattern resource types the catalogue registers in Init, and two more. Built inline by
// those two slots only (operator new 8, the vtable, count 0, AddRef), so with them ours it is only ever ours.
class cPatternParsingFilter : public cIGZPersistResourceKeyFilter {
public:
    static constexpr uint32_t kInterfaceId = 0x8865cd89;

    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, folded 0x72baec
    uint32_t AddRef() override;                              // slot 1, folded 0x148fb0
    uint32_t Release() override;                             // slot 2, folded 0x72c480
    bool IsKeyIncluded(const uint32_t *key) override;        // slot 3, 0xc10965
    ~cPatternParsingFilter() override = default;             // slot 4, 0xc0ed68 (the scalar deleting destructor)

    uint32_t mRefCount = 0; // +4
};
static_assert(sizeof(cPatternParsingFilter) == 8, "cPatternParsingFilter layout");
