// Differential tests of the shape nGZSceneGraph::cShape (lane RY, scenegraph H1a; src/sg/cShape.h): the game's shapes
// (factory 0xa5d3b0, constructor 0xa5bd40, its vtables and the bodies behind them) against ours (Create, our vtables), each
// side building its own shapes with its own factory. The slots not ported yet (H1b) run the game's bodies on both sides
// (on ours through OriginalFunction with our object), so what differs is the class's own code: the constructor and
// destructors, QueryInterface, Clone and the copy, Read, the type info and Write.
// Covered: 0xa5bd40, 0xa5c140 (destructor), 0xa5c4b0 (deleting destructor), 0xa5d070 (Clone), 0xa5d8a0 (CopyContentTo),
// 0xa5e4f0 (QueryInterface), 0xa5f9f0 (IsKindOf), 0xa5d470 (object name), 0xa5e580 (Read), 0xa5fb00 (GetTypeInfo),
// 0xa5fef0 (Write).
// Also covered through SG_Shape_Parts: 0xa5fbb0 (BindPartMaterials), 0xa5c520 (AddPart, slot 41), 0xa5f230 (RemovePart,
// slot 42), 0xa5d710 (OnEvent), 0xa5dca0 (slot 56), 0xa5efe0 (UpdateLods, slot 44), 0xa5dd50 (slot 43), 0xa5de40, 0xa5fa10,
// 0xa5dfa0, 0xa5e470, 0xa5c840 (the level chain, also through Read), 0xa5e280 (the preferred level), 0xa5f540 (slot 51),
// 0xa5cfd0 (slot 53), 0xa5dc60 (slot 57, with 0xa5daa0), and from H1b part 5: 0xa5c6f0 (slot 46: the part's render, materials
// and groups to a sink), 0xa5d4f0 (slot 60) and 0xa5d590 (slot 59) (bounds boxes of the first part), 0xa5f620 (slot 35: the
// subset-name iterators), 0xaed160 (the level part name, through single-part Read) and 0xa5daa0 (PartToShow). Fakes for them:
// the part's slot 30 (render) and 42 (subset names, iterators), the bounds record of slot 49, and the sink (kSink).
// H1b part 6 (lane SH), through SG_Shape_Parts and SG_Shape_Persist: 0xa5d9c0 (slot 45: a subset's draw variants) with
// 0xaeaf30 (one material's variants requested, walked, asked of the render: the render's slots 3 / 5, the element's format
// 800+, the material manager's slots 22 / 35, its variant iterators 810+ and variants 813+), 0xa5f840 (slot 54: the
// subset bindings' range) with the binding iterator (0xa5bea0 constructor, 0xa5be80 copy, 0xa5d0f0 Clone, 0xa5d1e0 Get,
// 0xa5c1e0 destructor, 0xa5c4e0 deleting destructor, pool allocate 0xa5c1f0 and free 0xa5c220 over the test page of
// BindingPool); and directly, SG_Shape_NameCell: 0xaed300 (a name's cell, also reached through slots 45 and 54).
//
//   SG_Shape_Factory   new shapes: their words, counts, the answers of QueryInterface and IsKindOf, release.
//   SG_Shape_Parts     parts added (slot 41) to random levels, materials bound, groups and words set, then clones
//                      (every mode, several interfaces), the type info, Write, release (the destructor's slot 42 loop).
//   SG_Shape_Persist   Read of streams built here (versions 3-8: the referent record from 5, levels from 7, parts by
//                      object reference or, from 8, by name; null references; bindings with groups, masks and words),
//                      through either persist face, then Write of what was read, then release.
//
// What the tests compare: the calls on the fakes (sgfx.h's pool, reader, writer, framework and resource manager; the
// fakes below), the notes: every shape's words but its vtable words, vbptr and pointer members (words 0, 7, 11, 13,
// 16-18, 19-23, 29-36, 38; word 10's upper half), its parts (level, object, materials, groups), bindings (subset handle,
// material index, mask, word), the five first-part words, its levels and part names, and the blocks freed.
//
// Fakes (ids): the parts 700+ (cIGeometryNode 0x5ba38381: 29 the render record's object, 36 / 37 the shape as listener,
// 40 bounds, 45 / 46 the subset names, 49 the bounds record), their resource faces 720+ (cISGResource 0x9b4bbca3: 8, 17
// the name, 20 the segment), their render objects 740+ (slot 4: five words, the first the subset count), the material
// manager 760 (GetCachedQueriedObject's cache 0x10f8fcc: 19 an index per name, 20 / 21 counts, 33 the info, the name
// first), the preferences service 761 (0xac1887e, what 0xa5e280 asks for "systemSpecifiedPreferredLOD"), the render-group
// registry (sg_renderable_fx.h's stand-in over 0xaeb370). The shape's statics: its registry 0xa5f010 (map 0x10f6318),
// its event-manager cell 0x10f6324 (a real connection manager), 0xa5e280's cached object 0x10f6310 (guard 0x10f6314),
// and the allocator service 762 (cache 0x10c95d4, guard 0x10c95d8: slot 3 / 4, what 0xa5e280's GZServiceString uses).
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz_registry_fx.h"
#include "sg_renderable_fx.h"
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/cMaterialManager.h"
#include "gz/cInputDeviceMap.h" // nGZEvent::cIEventBody
#include "rz/RZMemory.h"
#include "sg/cShape.h"
#include "sg/cSubsetBindingIterator.h"
#include "rz/StaticGetters2.h" // ServiceCell (0xaed300's cell)

#include <climits>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kFactory = 0xa5d3b0;
constexpr uint32_t kNode = 0x2c;
constexpr uint32_t kShapeTag = 0x7c000000;
constexpr unsigned kMax = 8;
constexpr unsigned kParts = 4;
constexpr unsigned kSeeds = 64;
constexpr uint32_t kResourceIid = 0x9b4bbca3;  // cISGResource
constexpr uint32_t kPrefsService = 0xac1887e, kPrefsIid = 0x4bb0d269;

// The shape's registry 0xa5f010 (the map, its guard) and its holder 0xa59480 (once byte, guard). It is ours now (verified in
// tests/sg_shape_registry.cpp): these tests plant the map and stand in for the function on both sides (shape::StandIn).
constexpr sgfx::RegistryStatics kShapeRegistry{0x10f6318, 0x10f6320, 0x10f6328, 0x10f6330};
constexpr uint32_t kEventCell = 0x10f6324;
constexpr uint32_t kMaterialCell = 0x10f8fcc;
constexpr uint32_t kLodObject = 0x10f6310, kLodGuard = 0x10f6314;
constexpr uint32_t kShownCell = 0x10f6308, kShownGuard = 0x10f630c;

#define U {0, Ret::Int, false, "", ""}
#define U4 U, U, U, U
#define U8 U4, U4
inline constexpr Slot kPartSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {4, Ret::Int, true, "OnEvent", "uuii"},               // 3
    {3, Ret::Int, true, "OnQuery", "uui"},                // 4
    {3, Ret::Int, true, "NotifyListeners", "uui"},        // 5 (NotifySubordinateAdding / Removing)
    U8, U8, U, U, U, U, U, U,                             // 6-27
    U,                                                    // 28
    {0, Ret::Int, true, "GetRenderConst", ""},            // 29
    {0, Ret::Int, true, "GetRender", ""},               // 30
    U4, U,                                                // 31-35
    {1, Ret::Int, true, "AddListener", "a"},              // 36
    {1, Ret::Int, true, "RemoveListener", "a"},           // 37
    U, U,                                                 // 38-39
    {2, Ret::Int, true, "SetBounds", "ua"},               // 40
    U,                                                    // 41
    {1, Ret::Int, true, "GetSubsetNames", "a"},        // 42
    U, U,                                                 // 43-44
    {1, Ret::Int, true, "GetElementName", "u"},           // 45
    {1, Ret::Int, true, "GetElementNameHandle", "u"},     // 46
    U, U,                                                 // 47-48
    {0, Ret::Int, true, "GetBoundsInfo", ""},             // 49
};
// The subset-name iterators GetSubsetNames hands out (slots 4-6 of cIGZForwardIterator): the cursor, the begin and the end.
inline constexpr Slot kIterSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {0, Ret::Int, true, "s3", ""},
    {1, Ret::Int, true, "IsEqual", "a"},                  // 4
    {0, Ret::Int, true, "Get", ""},                       // 5
    {0, Ret::Int, true, "Next", ""},                      // 6
};
inline constexpr drawfx::Iface kIter{"SubsetIterator", 7, kIterSlots};

// What slot 46 hands the part's drawing to (the sink's slot 28, eight words).
inline constexpr Slot kSinkSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    U4, U8, U8, U4, U,                                    // 3-27 (3-6, 7-14, 15-22, 23-26, 27)
    {8, Ret::Int, true, "Put", "auW1W1W1W2uu"},           // 28
};
inline constexpr drawfx::Iface kSink{"Sink", 29, kSinkSlots};

inline constexpr drawfx::Iface kPart{"GeometryNode", 50, kPartSlots};

inline constexpr Slot kPartResourceSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    U4, U,                                                // 3-7
    {0, Ret::Int, true, "GetPersistResource", ""},        // 8
    U8,                                                   // 9-16
    {0, Ret::Int, true, "GetName", ""},                   // 17
    U, U,                                                 // 18-19
    {0, Ret::Int, true, "GetSegment", ""},                // 20
};
inline constexpr drawfx::Iface kPartResource{"PartResource", 21, kPartResourceSlots};

inline constexpr Slot kRenderSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {1, Ret::Int, true, "IsSupported", "u"},              // 3 (slot 45's variants: the variant's format byte)
    {0, Ret::Int, true, "Record", ""},                    // 4
    {1, Ret::Int, true, "GetDrawInfo", "u"},              // 5 (slot 45: the element of a material index)
};
inline constexpr drawfx::Iface kRender{"GeometryRender", 6, kRenderSlots};

// The format object at +0xc of a draw element (slot 3: its index, of which 0xaeaf30 keeps seven bits).
inline constexpr Slot kFormatSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {0, Ret::Int, true, "FormatIndex", ""},               // 3
};
inline constexpr drawfx::Iface kFormat{"ElementFormat", 4, kFormatSlots};

// The material manager's variant iterators (slot 35's range) and the variants (slot 9: their record).
inline constexpr Slot kVariantIterSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {0, Ret::Int, true, "s3", ""},
    {1, Ret::Int, true, "IsEqual", "a"},                  // 4
    {1, Ret::Int, true, "Get", "a"},                      // 5 (four words: three key words, the variant)
    {0, Ret::Int, true, "Next", ""},                      // 6
};
inline constexpr drawfx::Iface kVariantIter{"VariantIterator", 7, kVariantIterSlots};
inline constexpr Slot kVariantSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    U4, U, U,                                             // 3-8
    {0, Ret::Int, true, "Record", ""},                    // 9
};
inline constexpr drawfx::Iface kVariant{"MaterialVariant", 10, kVariantSlots};

inline constexpr Slot kMaterialsSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    U8, U8,                                               // 3-18
    {1, Ret::Int, true, "FindOrLoad", "s"},               // 19
    {1, Ret::Int, true, "AddRefIndex", "u"},              // 20
    {1, Ret::Int, true, "ReleaseIndex", "u"},             // 21
    {5, Ret::Int, true, "GetVariant", "uW3uuu"},          // 22 (index, the key's three words, three words)
    U8, U, U,                                             // 23-32
    {2, Ret::Int, true, "GetInfo", "au"},                 // 33 (the hidden return, the index)
    U,                                                    // 34
    {2, Ret::Int, true, "VariantRange", "au"},            // 35 (the hidden return, the index)
};
inline constexpr drawfx::Iface kMaterials{"MaterialManager", 36, kMaterialsSlots};

inline constexpr Slot kPrefsSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    U4, U, U,                                             // 3-8
    {2, Ret::Int, true, "GetProperty", "aa"},             // 9 (the name string, the cIGZAny)
};
inline constexpr drawfx::Iface kPrefs{"Preferences", 10, kPrefsSlots};

// The preferred level's fallback object (0xa5e280: the cached 0x10f6310, which the test seeds): slot 10 says it has a value,
// slot 11 gives it.
inline constexpr Slot kLodSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {0, Ret::Int, true, "s3", ""}, {0, Ret::Int, true, "s4", ""}, {0, Ret::Int, true, "s5", ""}, {0, Ret::Int, true, "s6", ""},
    {0, Ret::Int, true, "s7", ""}, {0, Ret::Int, true, "s8", ""}, {0, Ret::Int, true, "s9", ""},
    {0, Ret::Int, true, "Has", ""},                       // 10
    {0, Ret::Int, true, "Value", ""},                     // 11
};
inline constexpr drawfx::Iface kLodFallback{"LodFallback", 12, kLodSlots};

// The allocator service the GZServiceString in 0xa5e280 allocates through (the image's cached pointer 0x10c95d4, guard
// 0x10c95d8): the harness's image has no heap of its own, so without it the string's block is corrupt.
inline constexpr Slot kAllocatorSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""},
    {1, Ret::Int, true, "Allocate", "u"},                 // 3
    {1, Ret::Int, true, "Free", "a"},                     // 4
};
inline constexpr drawfx::Iface kAllocator{"Allocator", 5, kAllocatorSlots};
constexpr uint32_t kAllocatorCell = 0x10c95d4, kAllocatorGuard = 0x10c95d8;
#undef U8
#undef U4
#undef U

const char *const kSubsets[] = {"body", "hair_lod1", "skin", "eyes_LOD"};
const char *const kPartNames[] = {"partA_cres", "partB_cres", "", nullptr};

struct PartFake {
    Fake part, resource, render, iter[3];
    uint32_t record[5];
    uint32_t bounds[4];
    float boxes[3][6];
    void *objects[2];
    Fake formats[2];          // the draw elements' format objects (slot 45)
    uint32_t elements[2][16]; // the draw elements GetDrawInfo hands out: +0xc the format, byte +0x34 a flag
};

struct World {
    cIShape *shapes[kMax];
    unsigned count = 0;
    PartFake parts[kParts];
    Fake materials, prefs, context, allocator, lodFallback, sink;
    Fake variantIters[3], variants[3]; // the material manager's slot 35 range and the variants it walks
    uint32_t variantRecords[3][4];     // slot 9's record: +0 in use, byte +0xc the format
    uint32_t variantShift = 0;         // how many variants a material has: (index + shift) % 4
    Fake listeners[2]; // behaviours the shapes' events are connected to (770 / 771)
    int blocks = 0; // allocator blocks outstanding
    std::string materialNames[16];
    unsigned materialCount = 0;
    int32_t preferredLod = -1;
    sgfx::Manager manager;
};
World w;

uint8_t *Object(cIShape *n) { return reinterpret_cast<uint8_t *>(n) - kNode; }
cShape *Members(cIShape *n) { return reinterpret_cast<cShape *>(Object(n)); } // the layout is the game's on both sides
uint32_t CountOf(cIShape *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }

// ---- the fakes' handlers ----
uint32_t PartRender(Fake &self, const uint32_t *) { return P(&static_cast<PartFake *>(self.owner)->render); }
uint32_t RenderRecord(Fake &self, const uint32_t *) { return P(static_cast<PartFake *>(self.owner)->record); }
// Slot 42: the three iterators (the cursor at the first name, the begin, the end past the part's names).
uint32_t PartSubsetNames(Fake &self, const uint32_t *a)
{
    PartFake &p = *static_cast<PartFake *>(self.owner);
    const uint32_t count = p.record[0] + 1;
    for (Fake &it : p.iter) it.refs = 1;
    p.iter[0].value[0] = 0;
    p.iter[1].value[0] = 0;
    p.iter[2].value[0] = count;
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = P(&p.iter[0]);
    out[1] = P(&p.iter[1]);
    out[2] = P(&p.iter[2]);
    return a[0];
}
uint32_t IterEqual(Fake &self, const uint32_t *a) { return self.value[0] == reinterpret_cast<Fake *>(uintptr_t(a[0]))->value[0]; }
uint32_t IterGet(Fake &self, const uint32_t *)
{
    PartFake &p = *static_cast<PartFake *>(self.owner);
    return P(kSubsets[(self.value[0] + p.part.value[0]) % 4]);
}
uint32_t IterNext(Fake &self, const uint32_t *)
{
    ++self.value[0];
    return 0;
}
uint32_t PartBounds(Fake &self, const uint32_t *) { return P(static_cast<PartFake *>(self.owner)->bounds); }
uint32_t RenderElement(Fake &self, const uint32_t *a) { return P(static_cast<PartFake *>(self.owner)->elements[a[0] % 2]); }
uint32_t FormatIndex(Fake &self, const uint32_t *) { return self.value[0]; }
// The material manager's slot 35 (out, index): the cursor and begin at the first variant, the end past the material's.
uint32_t MaterialVariants(Fake &, const uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (Fake &it : w.variantIters) it.refs = 1;
    w.variantIters[0].value[0] = 0;
    w.variantIters[1].value[0] = 0;
    w.variantIters[2].value[0] = (a[1] + w.variantShift) % 4;
    for (unsigned k = 0; k < 3; ++k) out[k] = P(&w.variantIters[k]);
    return a[0];
}
uint32_t VariantGet(Fake &self, const uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    const unsigned k = self.value[0] % 3;
    out[0] = 0x3100 + k;
    out[1] = 0x3200;
    out[2] = 0x3300;
    out[3] = P(&w.variants[k]);
    return a[0];
}
uint32_t VariantRecord(Fake &self, const uint32_t *) { return P(w.variantRecords[self.value[0]]); }
uint32_t ElementName(Fake &self, const uint32_t *a) { return P(kSubsets[(a[0] + self.value[0]) % 4]); }
uint32_t ElementHandle(Fake &self, const uint32_t *a) { return sgfx::gPool.Handle(kSubsets[(a[0] + self.value[0]) % 4]); }
uint32_t ResourceName(Fake &self, const uint32_t *) { return P(kPartNames[self.value[0] % 4]); }
uint32_t ResourceSegment(Fake &self, const uint32_t *)
{
    const unsigned k = self.value[1] % 4;
    return k < 3 ? P(&sgfx::gResources.segments[k]) : 0;
}
uint32_t ResourcePersist(Fake &self, const uint32_t *) { return P(&self); }

uint32_t MaterialFind(Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    const std::string text = name ? name : "";
    for (unsigned i = 0; i < w.materialCount; ++i)
        if (w.materialNames[i] == text) return 0x40 + i;
    if (w.materialCount == 16) return 0xffffffffu;
    w.materialNames[w.materialCount] = text;
    return 0x40 + w.materialCount++;
}
uint32_t MaterialInfoOf(Fake &, const uint32_t *a)
{
    auto *info = reinterpret_cast<MaterialInfo *>(uintptr_t(a[0]));
    *info = {};
    const uint32_t i = a[1] - 0x40;
    if (i < w.materialCount) {
        info->nameHash = P(w.materialNames[i].c_str());
        info->references = 1;
    }
    return a[0];
}
// GetProperty(name, any): an AnyUint32 (vtable, value) given the preferred level when the test set one.
uint32_t PrefsProperty(Fake &, const uint32_t *a)
{
    if (w.preferredLod >= 0) reinterpret_cast<uint32_t *>(uintptr_t(a[1]))[1] = uint32_t(w.preferredLod);
    return 1;
}
uint32_t Service(Fake &f, const uint32_t *a)
{
    if (a[0] == kPrefsService && a[1] == kPrefsIid) {
        ++w.prefs.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &w.prefs;
        return 1;
    }
    return sgfx::detail::GetSystemService(f, a);
}
// The resource manager's slot 3 (key, iid, out, 0, 0), what 0xa5dfa0 asks for a part by name: part 3, counted, while
// the test's switch (the manager's value[0]) is set.
uint32_t ManagerGet(Fake &self, const uint32_t *a)
{
    if (!self.value[0]) return 0;
    Fake *part = &w.parts[3].part;
    ++part->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = part;
    return 1;
}
uint32_t ReaderGroup(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t LodHas(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t LodValue(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t Allocate(Fake &, const uint32_t *a)
{
    ++w.blocks;
    return P(RZMemory::Malloc(a[0] ? a[0] : 1)); // the game's heap, as the service is (the scope patches RZMemory)
}
uint32_t Free(Fake &, const uint32_t *a)
{
    if (a[0]) {
        --w.blocks;
        RZMemory::Free(reinterpret_cast<void *>(uintptr_t(a[0])));
    }
    return 0;
}

template <typename T> struct GameHeap {
    using value_type = T;
    GameHeap() = default;
    template <typename U> GameHeap(const GameHeap<U> &) noexcept {}
    T *allocate(size_t count) { return static_cast<T *>(RZMemory::Malloc(uint32_t(count * sizeof(T)))); }
    void deallocate(T *block, size_t) { RZMemory::Free(block); }
    template <typename U> bool operator==(const GameHeap<U> &) const noexcept { return true; }
    template <typename U> bool operator!=(const GameHeap<U> &) const noexcept { return false; }
};
using GameInts = GameHeap<int32_t>;

// A listener's slot 3: the body noted whole, except cMaterialBindingsUpdated's, whose three bytes after the flag the original
// leaves as the stack held them (its size and flag are noted).
uint32_t ShapeListenerEvent(Fake &self, const uint32_t *a)
{
    const auto *body = reinterpret_cast<const nGZEvent::cIEventBody *>(uintptr_t(a[2]));
    if (body && body->GetEventType() == 0x2c27f3de) {
        drawfx::Note("heard flag body", {body->GetSize(), body->GetBodySize(), *(reinterpret_cast<const uint8_t *>(body) + 4)});
        return 0;
    }
    sgfx::NoteBody("heard body", body);
    (void)self;
    return 0;
}

void MakePart(unsigned i, std::mt19937 &rng)
{
    PartFake &p = w.parts[i];
    p.part.Reset<kPart>(700 + i);
    p.part.owner = &p;
    p.part.value[0] = rng() % 4;
    p.part.AddFace(kResourceIid, p.resource);
    p.part.On(29, &PartRender);
    p.part.On(30, &PartRender);
    p.part.On(42, &PartSubsetNames);
    p.part.On(45, &ElementName);
    p.part.On(46, &ElementHandle);
    p.part.On(49, &PartBounds);
    p.resource.Reset<kPartResource>(720 + i);
    p.resource.value[0] = rng() % 4;
    p.resource.value[1] = rng() % 4;
    p.resource.On(8, &ResourcePersist);
    p.resource.On(17, &ResourceName);
    p.resource.On(20, &ResourceSegment);
    for (unsigned k = 0; k < 3; ++k) {
        p.iter[k].Reset<kIter>(780 + i * 3 + k);
        p.iter[k].owner = &p;
        p.iter[k].On(4, &IterEqual);
        p.iter[k].On(5, &IterGet);
        p.iter[k].On(6, &IterNext);
    }
    p.render.Reset<kRender>(740 + i);
    p.render.owner = &p;
    p.render.On(4, &RenderRecord);
    p.render.On(5, &RenderElement);
    {
        // Slot 45's draw elements: the format's index (0xaeaf30 keeps seven bits) and the flag byte +0x34 (zero or not).
        const uint32_t indices[] = {0x05, 0x85, 0x7f, 0x100};
        const uint32_t flags[] = {0, 1, 0x80, 0x100};
        for (unsigned k = 0; k < 2; ++k) {
            p.formats[k].Reset<kFormat>(800 + i * 2 + k);
            p.formats[k].value[0] = indices[rng() % 4];
            p.formats[k].On(3, &FormatIndex);
            for (uint32_t &word : p.elements[k]) word = 0x6200 + i * 32 + k;
            p.elements[k][3] = P(&p.formats[k]);
            p.elements[k][13] = flags[rng() % 4]; // byte +0x34: the last flag's low byte is zero
        }
    }
    p.record[0] = rng() % 3; // the subset count
    for (unsigned k = 1; k < 5; ++k) p.record[k] = 0x6100 + i * 16 + k;
    p.objects[0] = &w.parts[(i + 1) % kParts].part;
    p.objects[1] = &w.parts[(i + 2) % kParts].part;
    const bool some = rng() % 2;
    // The record slot 49 gives: the count of boxes and their array (slots 59 / 60); a box may be inverted (empty).
    const float coords[] = {-2.0f, -1.0f, 0.0f, 0.5f, 1.0f, 3.0f};
    for (auto &box : p.boxes) {
        for (float &c : box) c = coords[rng() % 6];
        if (rng() % 3 != 0)
            for (unsigned k = 0; k < 3; ++k)
                if (box[k] > box[k + 3]) std::swap(box[k], box[k + 3]);
    }
    p.bounds[0] = rng() % 4;
    p.bounds[1] = P(p.boxes);
    p.bounds[2] = some ? 2 : 0;
    p.bounds[3] = some ? P(p.objects) : 0;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 31u + 7u);
    sgfx::gPool.Reset(cells);
    for (const char *s : kSubsets) sgfx::gPool.Handle(s);
    sgfx::gReader.Reset();
    sgfx::gReader.fake.Settle<9, 0>();
    sgfx::gReader.fake.On(9, &sgfx::detail::ReaderUint32);
    sgfx::gReader.fake.Settle<28, 0>();
    sgfx::gReader.fake.On(28, &ReaderGroup);
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Settle<9, 1>("u");
    sgfx::gResources.Reset(cells);
    sgfx::gResources.framework.On(5, &Service);
    sgfx::gResources.manager.Settle<3, 5>("W4uauu");
    sgfx::gResources.manager.On(3, &ManagerGet);
    sgfx::gResources.manager.value[0] = rng() % 2;
    sgrx::Setup(rng);
    if (rng() % 3 == 0) { // the low sixteen bits named: the groups the shape names get bits of 16 and up (`bit & 0x1f`)
        sgrx::Registry &registry = sgrx::Registry::Get();
        for (unsigned i = 0; i < 16; ++i) {
            registry.store[i] = "pad" + std::to_string(i);
            registry.names[i] = registry.store[i].c_str();
        }
    }
    for (unsigned i = 0; i < kParts; ++i) MakePart(i, rng);
    for (PartFake &p : w.parts) drawfx::AddRange(p.objects, sizeof(p.objects), 0x7b000000u | uint32_t(&p - w.parts) << 8);
    w.sink.Reset<kSink>(790);
    w.materials.Reset<kMaterials>(760);
    w.materials.On(19, &MaterialFind);
    w.materials.On(33, &MaterialInfoOf);
    w.materials.On(35, &MaterialVariants);
    w.materialCount = 0;
    w.variantShift = rng() % 4;
    for (unsigned k = 0; k < 3; ++k) {
        w.variantIters[k].Reset<kVariantIter>(810 + k);
        w.variantIters[k].On(4, &IterEqual);
        w.variantIters[k].On(5, &VariantGet);
        w.variantIters[k].On(6, &IterNext);
        w.variants[k].Reset<kVariant>(813 + k);
        w.variants[k].value[0] = k;
        w.variants[k].On(9, &VariantRecord);
        w.variantRecords[k][0] = rng() % 2 ? 0x41u + k : 0u; // in use or not
        w.variantRecords[k][1] = 0x6300 + k;
        w.variantRecords[k][2] = 0x6310 + k;
        w.variantRecords[k][3] = 0x6320u + (rng() % 2 ? 0x80u : 0u) + k; // byte +0xc the format IsSupported is asked
    }
    w.prefs.Reset<kPrefs>(761);
    w.prefs.On(9, &PrefsProperty);
    {
        const int32_t levels[] = {0, 1, 2, 3, 15, 7};
        w.preferredLod = rng() % 3 ? -1 : levels[rng() % 6];
    }
    w.allocator.Reset<kAllocator>(762);
    w.allocator.On(3, &Allocate);
    w.allocator.On(4, &Free);
    w.blocks = 0;
    cells.Set(kAllocatorCell, &w.allocator);
    cells.Set(kAllocatorGuard, uint32_t(INT_MIN));
    cells.Set(kMaterialCell, &w.materials);
    w.lodFallback.Reset<kLodFallback>(763);
    w.lodFallback.On(10, &LodHas);
    w.lodFallback.On(11, &LodValue);
    {
        const uint32_t values[] = {0xffffffffu, 0u, 2u};
        w.lodFallback.value[0] = rng() % 2;
        w.lodFallback.value[1] = values[rng() % 3];
    }
    cells.Set(kLodObject, rng() % 2 ? P(&w.lodFallback) : 0u);
    cells.Set(kLodGuard, uint32_t(INT_MIN));
    cells.Set(kShownCell, 0u); // slot 46's dead static (0xa5c6f0 would query a service once): its init is skipped
    cells.Set(kShownGuard, uint32_t(INT_MIN));
    w.count = 0;
    w.manager.Drop();
    w.manager.Use(cells, kEventCell);
    sgfx::MakeListener(w.listeners[0], 770, true);
    sgfx::MakeListener(w.listeners[1], 771, rng() % 2 != 0);
    for (Fake &l : w.listeners) l.On(3, &ShapeListenerEvent);
}

using Factory = cIShape *(__cdecl *)();
Factory FactoryFor(difftest::Context &ctx, bool original)
{
    return original ? ctx.Original<Factory>(kFactory) : &cShape::Create;
}

bool Hold(cIShape *n, bool counted)
{
    if (w.count >= kMax) {
        if (counted) n->Release();
        return false;
    }
    drawfx::AddRange(Object(n), cShape::kSize, kShapeTag | (w.count << 8));
    if (!counted) n->AddRef();
    w.shapes[w.count++] = n;
    return true;
}

cIShape *Make(Factory make)
{
    cIShape *n = make();
    return Hold(n, false) ? n : nullptr;
}

uint32_t IdOf(const void *p)
{
    for (const PartFake &f : w.parts)
        if (p == &f.part) return f.part.id;
    for (unsigned i = 0; i < w.count; ++i)
        if (p == w.shapes[i]) return 0x900 + i;
    return p ? 0xeeee : 0;
}

void NoteShape(const char *label, unsigned index)
{
    cIShape *n = w.shapes[index];
    const uint32_t *words = reinterpret_cast<const uint32_t *>(Object(n));
    drawfx::Call c;
    c.note = label;
    c.words.push_back(index);
    // The plain words: the resource part (1-6, 8, 9, word 10's lower half), the count (12), the Impl's (14, 15), the five
    // first-part words (24-28), the vtordisp (37).
    for (unsigned k : {1u, 2u, 3u, 4u, 5u, 6u, 8u, 9u, 12u, 14u, 15u, 24u, 25u, 26u, 27u, 28u, 37u})
        c.words.push_back(drawfx::Norm(words[k]));
    c.words.push_back(words[10] & 0xffff);
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(Object(n) + 0x1c);
    c.words.push_back(uint32_t(vbtable[0]));
    c.words.push_back(uint32_t(vbtable[1]));
    c.words.push_back((words[17] - words[16]) / 4); // the referrers
    const cShape *s = Members(n);
    c.words.push_back(uint32_t(s->mParts.size()));
    for (const ShapePart &part : s->mParts) {
        c.words.push_back(uint32_t(part.mLod));
        c.words.push_back(IdOf(part.mObject));
        c.words.push_back(uint32_t(part.mMaterials.size()));
        for (const ShapeMaterialHandle &m : part.mMaterials) c.words.push_back(uint32_t(m.mIndex));
        c.words.push_back(uint32_t(part.mGroups.size()));
        for (const ShapeSubsetGroups &g : part.mGroups) {
            c.words.push_back(g.mMask);
            c.words.push_back(g.mWord);
        }
    }
    c.words.push_back(uint32_t(s->mBindings.size()));
    for (const auto &b : s->mBindings) {
        c.words.push_back(b.first.cell.mValue);
        c.words.push_back(uint32_t(b.second.mMaterial.mIndex));
        c.words.push_back(b.second.mMask);
        c.words.push_back(b.second.mWord);
    }
    c.words.push_back(uint32_t(s->mLods.size()));
    for (int32_t lod : s->mLods) c.words.push_back(uint32_t(lod));
    c.words.push_back(uint32_t(s->mPartNames.size()));
    for (const ShapePartName &name : s->mPartNames) {
        c.words.push_back(name.mName.cell.mValue);
        c.words.push_back(uint32_t(name.mLod));
        uint32_t cell = 0; // slot 55 answers the name at the level (counted; the shapes' release is what the tests compare)
        n->GetPartNameAtLod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&cell)), uint32_t(name.mLod));
        c.words.push_back(cell);
    }
    drawfx::gLog.push_back(std::move(c));
}

void NoteAll(const char *label)
{
    for (unsigned i = 0; i < w.count; ++i) NoteShape(label, i);
}

void ReleaseAll()
{
    difftest::FreeRecorder freed;
    for (unsigned i = 0; i < w.count; ++i) w.shapes[i]->Release();
    w.count = 0;
    freed.Stop();
    size_t sizes[128];
    const size_t n = freed.SortedSizes(sizes, 128);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    for (const PartFake &p : w.parts)
        drawfx::Note("part refs", {p.part.id, uint32_t(p.part.refs), uint32_t(p.resource.refs), uint32_t(p.render.refs)});
    drawfx::Note("service refs", {uint32_t(w.materials.refs), uint32_t(w.prefs.refs), uint32_t(w.blocks)});
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

void NoteTypeInfo(cIShape *n)
{
    difftest::FreeRecorder freed;
    cPersistTypeInfo info = n->GetReferent()->GetTypeInfo();
    sgfx::NoteTypeInfo("type info", &info);
    if (info.mName) info.mName->Release();
    freed.Stop();
    drawfx::Note("type info frees", uint32_t(freed.Count()));
}

void Write(cIShape *n, std::mt19937 &rng)
{
    difftest::FreeRecorder freed;
    if (rng() % 2)
        n->GetReferent()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    else
        n->GetResource()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    freed.Stop();
    drawfx::Note("write frees", uint32_t(freed.Count()));
}

void CloneOf(unsigned index, std::mt19937 &rng)
{
    cIShape *n = w.shapes[index];
    const uint32_t iids[] = {cIShape::kIid, sgfx::kIidGraph, kResourceIid, 0x12345678};
    const uint32_t mode = rng() % 3, iid = iids[rng() % 4];
    sgfx::MakeCloneContext(w.context, nullptr);
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    n->Clone(mode, iid, &out, rng() % 2 ? reinterpret_cast<cINodeCloneContext *>(&w.context) : nullptr);
    const bool answered = out != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    drawfx::Note("clone", {mode, iid, uint8_t(answered), uint8_t(answered && out)});
    if (!answered || !out) return;
    // Hold the clone by its node face.
    cIShape *clone = nullptr;
    static_cast<cIGZUnknown *>(out)->QueryInterface(cIShape::kIid, reinterpret_cast<void **>(&clone));
    static_cast<cIGZUnknown *>(out)->Release();
    if (clone && Hold(clone, true)) drawfx::Note("clone held", {w.count - 1, CountOf(clone)});
}

__declspec(noinline) void QueryOne(difftest::Context &ctx, cIShape *n, uint32_t iid)
{
    char step[32];
    std::snprintf(step, sizeof(step), "query %08x", iid);
    sgfx::MarkStep(ctx, step);
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    const bool answered = n->QueryInterface(iid, &out);
    const uint32_t offset = answered && out ? uint32_t(static_cast<uint8_t *>(out) - Object(n)) : 0xffffffffu;
    drawfx::Note("query", {iid, uint8_t(answered), offset, CountOf(n)});
    // (The behaviour type's face, 0xfb859324, is handed out uncounted.)
    if (answered && out && iid != 0xfb859324) static_cast<cIGZUnknown *>(out)->Release();
    drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
    drawfx::Note("query without out", {iid, uint8_t(n->QueryInterface(iid, nullptr))});
}

void Queries(difftest::Context &ctx, cIShape *n)
{
    const uint32_t iids[] = {cIShape::kIid, 0x2e6a9ac3, sgfx::kIidGraph, sgfx::kIidTree, kResourceIid, 0x2a6e0b0b,
                             0x12345678, 0x65ab8d1a, 0xfb859324, 0x9c6ab984};
    for (uint32_t iid : iids) QueryOne(ctx, n, iid);
}

int32_t RandomLod(std::mt19937 &rng)
{
    const int32_t lods[] = {0, 1, 2, 3, 0, 15};
    return lods[rng() % 6];
}

// 0xa5fa10 reads past the parts when the level it is given has none (0xa5fa52, the original faults), so with two or more
// parts and two or more names the chain runs only when every name's level has a part.
bool SafeToUpdate(const cShape *s)
{
    if (s->mParts.size() < 2 || s->mPartNames.size() < 2) return true;
    for (const ShapePartName &name : s->mPartNames) {
        bool has = false;
        for (const ShapePart &part : s->mParts) has |= part.mLod == name.mLod;
        if (!has) return false;
    }
    return true;
}

void RandomBox(std::mt19937 &rng, float *box)
{
    const float coords[] = {-1.0f, 0.0f, 0.5f, 2.0f};
    for (unsigned k = 0; k < 3; ++k) {
        box[k] = coords[rng() % 4];
        box[k + 3] = box[k] + float(rng() % 3);
    }
}
void NoteBox(const char *label, const float *box)
{
    const uint32_t *bits = reinterpret_cast<const uint32_t *>(box);
    drawfx::Note(label, {bits[0], bits[1], bits[2], bits[3], bits[4], bits[5]});
}

// Slot 46 with random words and positions (a position may be 0: not moved).
void DrawShown(cIShape *n, std::mt19937 &rng)
{
    uint32_t posA = rng() % 4 == 0 ? 0 : 0x4000 + (rng() % 8) * 4, posB = rng() % 4 == 0 ? 0 : 0x8000 + (rng() % 8) * 4;
    void *sink = &w.sink;
    const uint32_t a = rng() % 100, d = rng() % 100, f = rng() % 100;
    n->QueuePartDraw(a, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&posA)), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&posB)), d,
              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sink)), f);
    drawfx::Note("positions", {posA, posB});
}

// The binding iterators' pool (cRZFastFixedAllocator2<0xc> 0x10f62f0, +4 its current page) points at a test page: 0x800
// aligned, header +8 the free list, +0xc the used count, set far from empty and from full so neither side reaches the
// page bookkeeping (game code not under test). As sg_geometry_node.cpp's IteratorPools.
struct BindingPool {
    static constexpr uint32_t kCurrent = 0x10f62f4, kBlock = 0xc, kPage = 0x800, kTag = 0x7e000000;
    uint8_t *page;
    uint32_t saved;
    BindingPool()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(kCurrent);
        *GameAddress<uint32_t>(kCurrent) = P(page);
    }
    ~BindingPool()
    {
        *GameAddress<uint32_t>(kCurrent) = saved;
        _aligned_free(page);
    }
    BindingPool(const BindingPool &) = delete;
    BindingPool &operator=(const BindingPool &) = delete;
    void Reset() // before each side: the same free list on both, the page registered
    {
        std::memset(page, 0, kPage);
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        header[3] = 1000;
        uint32_t next = 0;
        for (uint32_t at = kPage - kBlock; at >= 0x10; at -= kBlock) {
            *reinterpret_cast<uint32_t *>(page + at) = next;
            next = P(page + at);
        }
        header[2] = next;
        drawfx::AddRange(page, kPage, kTag);
    }
};
BindingPool *gBindingPool = nullptr;

void NotePool()
{
    if (!gBindingPool) return;
    const uint32_t *header = reinterpret_cast<const uint32_t *>(gBindingPool->page);
    drawfx::Note("binding pool", {drawfx::Ptr(header[2]), header[3]});
}

const char *RandomSubset(std::mt19937 &rng)
{
    const char *const names[] = {"body", "hair_lod1", "skin", "eyes_LOD", "none", "", nullptr};
    return names[rng() % 7];
}

uint32_t TextSum(uint32_t text)
{
    uint32_t sum = 0;
    for (const char *c = reinterpret_cast<const char *>(uintptr_t(text)); c && *c; ++c) sum = sum * 31 + uint8_t(*c);
    return sum;
}

void NoteBinding(const char *label, cISubsetBindingIterator *it)
{
    cSubsetBinding b{0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e};
    const cSubsetBinding *r = it->Get(&b);
    drawfx::Note(label, {uint8_t(r == &b), uint8_t(b.mName != 0), TextSum(b.mName), b.mMaterial, b.mMask, b.mWord});
}

// Slot 45 (0xa5d9c0): its result is the last part's material count with parts, zero with none and a null subset, and
// undefined (what closing the key cell left in eax) when the subset has a binding but there are no parts or has no binding:
// noted only where it is defined.
void Prepare(cIShape *n, const char *subset)
{
    const cShape *s = Members(n);
    bool bound = false;
    if (subset) {
        const uint32_t cell = *subset ? sgfx::gPool.Handle(subset) : 0xffffffffu;
        for (const auto &b : s->mBindings) bound |= b.first.cell.mValue == cell;
    }
    const bool defined = !subset || (bound && !s->mParts.empty());
    drawfx::Note("prepare variants", {uint8_t(subset != nullptr), TextSum(P(subset)), uint8_t(bound), uint8_t(defined)});
    const uint32_t r = n->PrepareSubsetVariants(subset);
    drawfx::Note("prepared", defined ? r : 0xdddddddd);
}

uint32_t CountWord(cISubsetBindingIterator *it) { return it ? reinterpret_cast<const uint32_t *>(it)[1] : 0xffffffffu; }

// Slot 54 (0xa5f840): the bindings of a subset (or all of them), walked forward by the cursor and back from the end by a
// clone, queried, then released (the iterators 0xa5bea0 / 0xa5be80 / 0xa5d0f0 / 0xa5d1e0 / 0xa5c4e0, the pool 0xa5c1f0 /
// 0xa5c220).
void WalkBindings(cIShape *n, const char *subset)
{
    cISubsetBindingIterator *range[3] = {reinterpret_cast<cISubsetBindingIterator *>(uintptr_t(0x5e5e5e5e)),
                                         reinterpret_cast<cISubsetBindingIterator *>(uintptr_t(0x5e5e5e5e)),
                                         reinterpret_cast<cISubsetBindingIterator *>(uintptr_t(0x5e5e5e5e))};
    const uint32_t r = n->GetSubsetBindings(range, subset);
    drawfx::Note("bindings", {uint8_t(subset != nullptr), TextSum(P(subset)), uint8_t(r == P(range)),
                              drawfx::Ptr(range[0]), drawfx::Ptr(range[1]), drawfx::Ptr(range[2]), CountWord(range[0]),
                              CountWord(range[1]), CountWord(range[2])});
    NotePool();
    cISubsetBindingIterator *cursor = range[0], *begin = range[1], *end = range[2];
    if (!cursor || !begin || !end) {
        for (cISubsetBindingIterator *it : range)
            if (it) it->Release();
        return;
    }
    void *face = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    for (uint32_t riid : {1u, 2u, 0x12345678u}) {
        face = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const bool answered = cursor->QueryInterface(riid, &face);
        drawfx::Note("binding query", {riid, uint8_t(answered), uint8_t(face == cursor), CountWord(cursor)});
        if (answered) drawfx::Note("binding query release", static_cast<cIGZUnknown *>(face)->Release());
    }
    drawfx::Note("binding query without out", uint8_t(cursor->QueryInterface(1, nullptr)));
    drawfx::Note("binding addref", {cursor->AddRef(), cursor->Release()});
    unsigned steps = 0;
    for (; !cursor->IsEqual(end) && steps < 16; ++steps) {
        NoteBinding("binding", cursor);
        cursor->Next();
    }
    drawfx::Note("forward steps", {steps, uint8_t(cursor->IsEqual(begin)), uint8_t(begin->IsEqual(end))});
    cISubsetBindingIterator *back = end->Clone();
    drawfx::Note("clone", {drawfx::Ptr(back), CountWord(back), uint8_t(back && back->IsEqual(end))});
    NotePool();
    if (back) {
        back->AddRef();
        steps = 0;
        for (; !back->IsEqual(begin) && steps < 16; ++steps) {
            back->Prev();
            NoteBinding("binding back", back);
        }
        drawfx::Note("back steps", steps);
        drawfx::Note("clone release", back->Release());
    }
    for (cISubsetBindingIterator *it : {end, begin, cursor}) drawfx::Note("binding release", it->Release());
    NotePool();
}

void Operate(std::mt19937 &rng)
{
    cIShape *n = w.shapes[rng() % w.count];
    switch (rng() % 32) { // (13 and 18-19: a level's part removed)
    case 0:
    case 1: {
        const unsigned i = rng() % kParts;
        const int32_t lod = RandomLod(rng);
        drawfx::Note("add part", {i, uint32_t(lod)});
        n->AddPart(reinterpret_cast<cIGZUnknown *>(&w.parts[i].part), lod);
        break;
    }
    case 2: {
        const char *const materials[] = {"skin_txmt", "*grp!hair_txmt", "", "glass"};
        const unsigned m = rng() % 4, s = rng() % 4;
        drawfx::Note("bind", {m, s});
        n->BindMaterial(materials[m], kSubsets[s]);
        break;
    }
    case 3: {
        const char *const groups[] = {"alpha", "shadow", "ui"};
        const unsigned g = rng() % 3, s = rng() % 4;
        drawfx::Note("group", {g, s});
        n->AddRenderGroup(groups[g], kSubsets[s]);
        break;
    }
    case 4: {
        const uint32_t mask = rng() % 2 ? 0xffffffffu : uint32_t(rng());
        const unsigned s = rng() % 4;
        drawfx::Note("mask", {mask, s});
        n->SetRenderGroupMask(mask, kSubsets[s]);
        break;
    }
    case 5: {
        const uint32_t word = rng() % 2 ? 0 : uint32_t(rng());
        const unsigned s = rng() % 4;
        drawfx::Note("word", {word, s});
        n->SetSubsetWord(word, kSubsets[s]);
        break;
    }
    case 7: {
        const char *const groups[] = {"alpha", "shadow", "ui"};
        const unsigned g = rng() % 3, s = rng() % 4;
        drawfx::Note("drop group", {g, s});
        n->RemoveRenderGroup(groups[g], kSubsets[s]);
        break;
    }
    case 8: {
        const uint32_t mask = rng() % 2 ? 0xffffffffu : uint32_t(rng());
        const unsigned s = rng() % 4;
        drawfx::Note("clear mask", {mask, s});
        n->ClearRenderGroupMask(mask, kSubsets[s]);
        break;
    }
    case 9: {
        const int32_t lod = RandomLod(rng);
        const uint32_t a = n->PartObjectAtLod(lod);
        const uint32_t b = n->PartObjectAtLod2(lod);
        drawfx::Note("part of level", {uint32_t(lod), a, b});
        break;
    }
    case 10: {
        // The caller's vector lives on the game's heap, as the game's do, and grows there on both sides (no reserve).
        std::vector<int32_t, GameInts> levels(rng() % 4, 7);
        n->GetPartLods(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&levels)));
        drawfx::Note("levels", uint32_t(levels.size()));
        for (const int32_t level : levels) drawfx::Note("level", uint32_t(level));
        break;
    }
    case 11: { // slot 52, 0xa5d7f0
        uint32_t words[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        n->GetRenderInfo(words);
        drawfx::Note("renderer words", {words[0], words[2], words[3], words[4], words[5], words[6], words[7]});
        drawfx::Note("first material", uint32_t(words[1] != 0));
        break;
    }
    case 12: { // slot 55, 0xa5e1d0
        uint32_t cell = 0;
        const int32_t lod = RandomLod(rng);
        n->GetPartNameAtLod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&cell)), uint32_t(lod));
        drawfx::Note("part name cell", {uint32_t(lod), cell});
        break;
    }
    case 14: { // a listener connected to one of the events the shape tells (or that it passes on)
        const uint32_t events[] = {0xdd041f30, 0x3d44b140, 0xedf79966, cSubordinateListChanged::kEventId,
                                   cGraphConnChanged::kEventId, 0x1234};
        const uint32_t event = events[rng() % 6];
        Fake &l = w.listeners[rng() % 2];
        drawfx::Note("connect", event);
        n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2));
        break;
    }
    case 15: { // OnEvent (0xa5d710): the two events the shape answers, and others
        const uint32_t types[] = {0x1d05597b, 0x9bb24282, 0x1d05597b, 0x9bb24283, 0x12345678};
        const uint32_t type = types[rng() % 5];
        drawfx::Note("event", type);
        n->OnEvent(type, rng() % 3, nullptr, 0);
        break;
    }
    case 16: { // every group of a subset, then one named group dropped (its bit may be 16 or more)
        const char *const groups[] = {"alpha", "shadow", "ui"};
        const unsigned g = rng() % 3, s = rng() % 4;
        drawfx::Note("all then drop", {g, s});
        n->SetRenderGroupMask(0xffffffffu, kSubsets[s]);
        n->RemoveRenderGroup(groups[g], kSubsets[s]);
        break;
    }
    case 17: { // a listener on what the part-list change tells, then the first part (if any) removed
        const uint32_t events[] = {0xdd041f30, 0x3d44b140, 0xedf79966};
        n->Connect(events[rng() % 3], reinterpret_cast<cIGZUnknown *>(&w.listeners[rng() % 2]), 0x500);
        const int32_t lod = Members(n)->mParts.empty() ? 0 : Members(n)->mParts.front().mLod;
        drawfx::Note("listen then remove", uint32_t(lod));
        n->RemovePart(lod);
        break;
    }
    case 20: { // slot 43, 0xa5dd50: the named parts loaded (the manager answers while its switch is set)
        drawfx::Note("load named parts", uint32_t(Members(n)->mPartNames.size()));
        n->LoadNamedParts();
        break;
    }
    case 21: { // slot 44, 0xa5efe0 (0xa5e280 -> 0xa5de40 -> 0xa5fa10)
        // 0xa5fa10 reads past the parts when the level it is given has none (0xa5fa52, the original faults), so with two
        // or more parts and two or more names the chain runs only when every name's level has a part.
        const bool safe = SafeToUpdate(Members(n));
        drawfx::Note("update levels", uint32_t(safe));
        if (safe) n->UpdateLods();
        break;
    }
    case 22: { // slot 51, 0xa5f540: the levels become the words given
        int32_t levels[6];
        const uint32_t count = rng() % 7;
        for (uint32_t i = 0; i < count; ++i) levels[i] = RandomLod(rng);
        drawfx::Note("set levels", count);
        n->SetLods(count, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(levels)));
        break;
    }
    case 23: { // slot 53, 0xa5cfd0: the name of a subset's material
        const unsigned s = rng() % 4;
        const char *name = reinterpret_cast<const char *>(uintptr_t(n->MaterialNameOf(kSubsets[s])));
        uint32_t sum = 0;
        for (const char *c = name; c && *c; ++c) sum = sum * 31 + uint8_t(*c);
        drawfx::Note("material name", {s, uint8_t(name != nullptr), sum});
        break;
    }
    case 24: { // slot 57, 0xa5dc60: the level to show
        drawfx::Note("level to show", n->GetShownLod());
        break;
    }
    case 25: { // slot 60, 0xa5d4f0: a box grown by the first part's boxes
        float box[6];
        RandomBox(rng, box);
        n->GrowBoxByParts(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(box)));
        NoteBox("grown by bounds", box);
        break;
    }
    case 26: { // slot 59, 0xa5d590: the same, each carried by a rigid transform
        float box[6];
        RandomBox(rng, box);
        const float rotations[][4] = {{0, 0, 0, 1}, {0, 0, 0.70710678f, 0.70710678f}, {0.5f, 0.5f, 0.5f, 0.5f}, {0.6f, 0, 0, 0.8f}};
        float base[7], rigids[4][7];
        float *const transforms[] = {base, rigids[0], rigids[1], rigids[2], rigids[3]};
        for (float *t : transforms) {
            for (unsigned k = 0; k < 3; ++k) t[k] = float(int(rng() % 5) - 2);
            const float *r = rotations[rng() % 4];
            for (unsigned k = 0; k < 4; ++k) t[3 + k] = r[k];
        }
        const uint32_t count = rng() % 5;
        n->GrowBoxByPartsMoved(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(box)), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)),
                  count, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rigids)));
        NoteBox("grown through rigids", box);
        break;
    }
    case 27: { // slot 46, 0xa5c6f0: the part to show drawn into the sink, the two positions moved on
        DrawShown(n, rng);
        break;
    }
    case 29: { // slot 46 on shapes made for it: the part to show not the first, then one without materials
        while (!Members(n)->mParts.empty()) n->RemovePart(Members(n)->mParts.front().mLod);
        const unsigned i = rng() % kParts, j = (i + 1 + rng() % (kParts - 1)) % kParts;
        if (rng() % 2) { // two parts, levels 0 and 1, the preferred level the second's
            w.parts[i].record[0] = 1 + rng() % 2;
            w.parts[j].record[0] = 1 + rng() % 2;
            w.preferredLod = 1;
            drawfx::Note("two parts", {i, j});
            n->AddPart(reinterpret_cast<cIGZUnknown *>(&w.parts[i].part), 0);
            n->AddPart(reinterpret_cast<cIGZUnknown *>(&w.parts[j].part), 1);
        } else { // one part whose geometry has no elements: no materials
            w.parts[i].record[0] = 0;
            drawfx::Note("empty part", i);
            n->AddPart(reinterpret_cast<cIGZUnknown *>(&w.parts[i].part), RandomLod(rng));
        }
        DrawShown(n, rng);
        break;
    }
    case 28: { // slot 35, 0xa5f620: a material bound to every subset name of every part
        const char *const materials[] = {"skin_txmt", "*grp!hair_txmt", "", "glass"};
        const unsigned m = rng() % 4;
        drawfx::Note("bind all", m);
        n->BindMaterialToAllSubsets(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(materials[m])));
        break;
    }
    case 30: { // slot 45, 0xa5d9c0 (-> 0xaeaf30, 0xaed300): the draw variants of a subset's material, or of every material
        Prepare(n, RandomSubset(rng));
        break;
    }
    case 31: { // slot 54, 0xa5f840: a subset's bindings walked
        WalkBindings(n, RandomSubset(rng));
        break;
    }
    default: {
        const int32_t lod = RandomLod(rng);
        drawfx::Note("remove part", uint32_t(lod));
        n->RemovePart(lod);
        break;
    }
    }
}

// A stream for Read at `version` (the queues of sgfx's reader; its texts are at most 8).
void FillStream(uint32_t version, std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    unsigned texts = 2;
    r.Version(version);
    r.Version(rng() % 3); // cSGResource's record
    r.Text(rng() % 2 ? "shape_shpe" : "");
    if (version >= 5) r.Version(1); // the referent Impl's
    r.Version(4);                   // the object-graph Impl's
    r.Word(0);                      // no extensions
    r.Text(rng() % 4 ? "" : "alpha");
    if (version > 6) {
        const uint32_t levels = rng() % 4;
        r.Word(levels);
        const int32_t lods[] = {0, 1, 2, -1, 40, 30, 31};
        for (uint32_t i = 0; i < levels; ++i) r.Word(uint32_t(lods[rng() % 7]));
    }
    // The parts: all by name or all by reference. The level Read settles on (0xa5e280 -> 0xa5de40) is one of the part
    // names' levels, and 0xa5fa10 then looks it up among the parts with two or more of them, reading past the end when
    // it is not there (the original faults at 0xa5fa52). So a stream with two or more parts by reference names every
    // one of them (resource names 0 and 1), and names and references are not mixed: a game shape's parts are of one
    // kind and named.
    const uint32_t parts = rng() % 4;
    const bool byNames = version >= 8 && rng() % 2;
    r.Word(parts);
    for (uint32_t i = 0; i < parts; ++i) {
        if (version > 5) r.Word(uint32_t(RandomLod(rng)));
        if (version >= 8) r.Word(byNames);
        if (byNames) {
            const char *const names[] = {"partA_cres", "partB_cres", "##0x1c0532fa!partC_cres"};
            r.Text(names[rng() % 3]);
            ++texts;
            continue;
        }
        PartFake *part = rng() % 5 ? &w.parts[rng() % kParts] : nullptr;
        if (part && parts > 1) part->resource.value[0] %= 2;
        r.Ref(part ? &part->part : nullptr);
    }
    const uint32_t bindings = texts <= 6 ? rng() % 3 : 0;
    r.Word(bindings);
    for (uint32_t b = 0; b < bindings; ++b) {
        if (texts > 6) {
            // (No room for this binding's texts: the reader answers "" for each.)
            r.Word(0);
            r.Word(0);
            r.Word(0);
            continue;
        }
        const char *const materials[] = {"skin_txmt", "*grp!hair_txmt", "", "glass"};
        r.Text(kSubsets[rng() % 4]);
        r.Text(materials[rng() % 4]);
        texts += 2;
        r.Word(rng() % 2); // all groups
        const uint32_t groups = texts < 8 ? rng() % 2 : 0;
        r.Word(groups);
        const char *const groupNames[] = {"alpha", "shadow", "ui"};
        for (uint32_t g = 0; g < groups; ++g) r.Text(groupNames[rng() % 3]), ++texts;
        r.Word(rng() % 2 ? 0 : 0x77000000u + rng() % 256);
    }
}

// What a game does with a shape it has read (its parts named by the stream): ask the level to show, load the named parts,
// update the levels, the material name of a subset, and the level to show again.
void AfterRead(cIShape *n, std::mt19937 &rng)
{
    drawfx::Note("level to show", n->GetShownLod());
    if (rng() % 2) {
        drawfx::Note("load named parts", uint32_t(Members(n)->mPartNames.size()));
        n->LoadNamedParts();
    }
    const bool safe = SafeToUpdate(Members(n));
    drawfx::Note("update levels", uint32_t(safe));
    if (safe && rng() % 2) n->UpdateLods();
    const unsigned subset = rng() % 4;
    const char *name = reinterpret_cast<const char *>(uintptr_t(n->MaterialNameOf(kSubsets[subset])));
    uint32_t sum = 0;
    for (const char *c = name; c && *c; ++c) sum = sum * 31 + uint8_t(*c);
    drawfx::Note("material name", {subset, uint8_t(name != nullptr), sum});
    drawfx::Note("level to show", n->GetShownLod());
    Prepare(n, RandomSubset(rng)); // slot 45 on the bindings and parts the stream made
    WalkBindings(n, rng() % 3 ? nullptr : RandomSubset(rng));
}

} // namespace

DIFF_TEST(SG_Shape_Factory, kFactory)
{
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry(kShapeRegistry);
    shape::StandIn registryStand{0xa5f010, &cShapeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kShapeRegistry.map>};
    sgrx::Services services;
    sgfx::CheckSides(ctx, "factory", 8, Build, [&](bool original, std::mt19937 &) {
        const Factory make = FactoryFor(ctx, original);
        sgfx::MarkStep(ctx, "make");
        cIShape *a = make();
        cIShape *b = make();
        drawfx::Note("counts", {CountOf(a), CountOf(b)});
        Hold(a, false);
        Hold(b, false);
        sgfx::MarkStep(ctx, "note");
        NoteAll("new shape");
        sgfx::MarkStep(ctx, "queries");
        Queries(ctx, a);
        sgfx::MarkStep(ctx, "type info");
        NoteTypeInfo(a);
        sgfx::MarkStep(ctx, "release");
        ReleaseAll();
    });
    w.manager.Drop();
    gBindingPool = nullptr;
}

DIFF_TEST(SG_Shape_Parts, kFactory)
{
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry(kShapeRegistry);
    shape::StandIn registryStand{0xa5f010, &cShapeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kShapeRegistry.map>};
    sgrx::Services services;
    BindingPool pool;
    gBindingPool = &pool;
    const auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        pool.Reset();
    };
    sgfx::CheckSides(ctx, "parts", kSeeds, build, [&](bool original, std::mt19937 &rng) {
        const Factory make = FactoryFor(ctx, original);
        const unsigned n = 1 + rng() % 3;
        for (unsigned i = 0; i < n; ++i) Make(make);
        for (int k = 0; k < 12; ++k) {
            sgfx::MarkStep(ctx, "operate");
            Operate(rng);
        }
        NoteAll("built");
        for (int k = 0; k < 3; ++k) {
            sgfx::MarkStep(ctx, "clone");
            CloneOf(rng() % w.count, rng);
        }
        NoteAll("cloned");
        sgfx::MarkStep(ctx, "write");
        Write(w.shapes[rng() % w.count], rng);
        Queries(ctx, w.shapes[0]);
        sgfx::MarkStep(ctx, "release");
        ReleaseAll();
    });
    w.manager.Drop();
    gBindingPool = nullptr;
}

DIFF_TEST(SG_Shape_Persist, kFactory)
{
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry(kShapeRegistry);
    shape::StandIn registryStand{0xa5f010, &cShapeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kShapeRegistry.map>};
    sgrx::Services services;
    BindingPool pool;
    gBindingPool = &pool;
    const auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        pool.Reset();
    };
    sgfx::CheckSides(ctx, "persist", kSeeds * 2, build, [&](bool original, std::mt19937 &rng) {
        const Factory make = FactoryFor(ctx, original);
        // A fresh shape, as every load reads into: a shape that already holds two or more parts and reads none by
        // reference leaves 0xa5de40 nothing to choose from, and 0xa5fa10 then reads past the parts (the original
        // faults at 0xa5fa52).
        cIShape *n = Make(make);
        const uint32_t versions[] = {3, 4, 5, 6, 7, 8, 8, 8};
        const uint32_t version = versions[rng() % 8];
        FillStream(version, rng);
        sgfx::gReader.fake.value[0] = rng() % 2 ? 0 : 0x1c0532fau; // the stream's group (slot 28)
        drawfx::Note("version", version);
        sgfx::MarkStep(ctx, "read");
        if (rng() % 2)
            n->GetReferent()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake));
        else
            n->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake));
        const sgfx::Reader &r = sgfx::gReader;
        sgfx::MarkStep(ctx, "read done");
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        NoteAll("after read");
        sgfx::MarkStep(ctx, "noted");
        sgfx::gReader.NoteNames();
        sgfx::MarkStep(ctx, "after read");
        AfterRead(n, rng);
        sgfx::MarkStep(ctx, "write");
        Write(n, rng);
        if (rng() % 3 == 0) {
            sgfx::MarkStep(ctx, "clone");
            CloneOf(0, rng);
            NoteAll("cloned");
        }
        sgfx::MarkStep(ctx, "release");
        ReleaseAll();
    });
    w.manager.Drop();
    gBindingPool = nullptr;
}

// 0xaed300 (hooked: about twenty game functions outside cShape call it): a subset name's cell, the empty cell (-1) for a
// null or empty text, else the string pool's handle opened. Both sides from a garbage cell; the result is the cell.
DIFF_TEST(SG_Shape_NameCell, 0xaed300)
{
    using MakeCell = ServiceCell *(__cdecl *)(ServiceCell *, const char *);
    sgfx::CheckSides(ctx, "name cell", 16, Build, [&](bool original, std::mt19937 &rng) {
        const MakeCell make = original ? ctx.Original<MakeCell>(0xaed300) : &MakeNameCell;
        const char *const texts[] = {nullptr, "", "body", "skin", "fresh_name", "x"};
        for (int k = 0; k < 6; ++k) {
            const char *text = texts[rng() % 6];
            ServiceCell cell{0x5e5e5e5e};
            const ServiceCell *r = make(&cell, text);
            drawfx::Note("cell", {uint8_t(text != nullptr), TextSum(P(text)), uint8_t(r == &cell), cell.mValue});
            if (cell.mValue != 0xffffffffu && rng() % 4) cell.Close();
        }
        sgfx::gPool.NoteCounts();
    });
}
